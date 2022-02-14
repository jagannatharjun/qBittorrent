#include "streamingmanager.h"

#include "base/bittorrent/common.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrent.h"
#include "base/bittorrent/infohash.h"
#include "base/bittorrent/torrentinfo.h"
#include "base/http/httperror.h"

#include <QUrl>
#include <QMimeType>
#include <QMimeDatabase>

namespace
{
    QString makePath(BitTorrent::Torrent *torrent, int fileIndex)
    {
        auto fileName = torrent->actualFilePath(fileIndex);
        if (fileName.hasExtension(QB_EXT))
            fileName.removeExtension(QB_EXT);

        return u"%1/%2/%3"_qs
                .arg(torrent->id().toString()
                     , QString::number(fileIndex)
                     , QString::fromUtf8(fileName.toString().toUtf8().toPercentEncoding()));
    }

    struct TorrentFile
    {
        BitTorrent::Torrent *torrent;
        int fileIndex;
    };

    std::optional<TorrentFile> breakPath(const QString &path)
    {
        using namespace BitTorrent;

        qDebug("breakPath %s", path.toUtf8().data());
        const QChar sep = u'/';
        const auto torrentIDEnd = path.indexOf(sep);

        const auto torrentID = TorrentID::fromString(path.left(torrentIDEnd));
        qDebug("breakPath torrentID %s %s", path.left(torrentIDEnd).toUtf8().data(), torrentID.toString().toUtf8().data());
        if (!torrentID.isValid())
            return std::nullopt;

        auto torrent = Session::instance()->getTorrent(torrentID);
        if (!torrent)
            return std::nullopt;

        const auto fileIndexStart = torrentIDEnd + 1;
        const auto fileIndexEnd = path.indexOf(sep, fileIndexStart);
        if (fileIndexEnd == -1)
            return std::nullopt;

        bool ok = true;
        const auto fileIndex = path.mid(fileIndexStart, fileIndexEnd - fileIndexStart).toInt(&ok);
        if (!ok)
            return std::nullopt;

        return TorrentFile {torrent, fileIndex};
    }

    struct Range
    {
        qint64 firstBytePos, lastBytePos;

        qint64 size() const
        {
            return lastBytePos - firstBytePos + 1;
        }
    };

    std::optional<Range> extractRangeFromHTTPfield(const QString &value, qint64 fileSize)
    {
        // range value field should be in form -> "<unit>=<range>"
        // where unit is "bytes"
        // range is "<firstBytePos>-<lastBytePos>"

        qDebug("extractRangeFromHTTPfield %s, fileSize %lld", value.toUtf8().data(), fileSize);
        const int unitRangeSeparatorIndex = value.indexOf(u'=');
        if (unitRangeSeparatorIndex == -1)
            return std::nullopt;

        const QString unit = value.left(unitRangeSeparatorIndex);
        const QString acceptedUnit = u"bytes"_qs;
        if (unit != acceptedUnit)
            return std::nullopt;

        const int rangeSeparatorIndex = value.indexOf(u'-', (unitRangeSeparatorIndex + 1));
        if (rangeSeparatorIndex == -1)
            return std::nullopt;

        bool isValidInt;
        const qint64 firstBytePos = value.mid(unitRangeSeparatorIndex + 1, (rangeSeparatorIndex - unitRangeSeparatorIndex - 1))
                                        .toLongLong(&isValidInt);
        if (!isValidInt)
            return std::nullopt;

        qint64 lastBytePos =
            value.mid(rangeSeparatorIndex + 1).toLongLong(&isValidInt);
        if (!isValidInt)
            // lastBytePos is optional, if not provided we should assume lastBytePos is at the end of the file
            lastBytePos = fileSize - 1;

        if (firstBytePos > lastBytePos)
            return std::nullopt;

        return Range {firstBytePos, lastBytePos};
    }

    int pieceDownloadTimeMsec(const BitTorrent::Torrent *torrent)
    {
        const int downloadRate = qMax(torrent->downloadPayloadRate(), 512000);
        return (torrent->pieceLength() * 1000) / downloadRate;
    }
}

StreamingManager *StreamingManager::m_instance = nullptr;

void StreamingManager::initInstance()
{
    assert(!m_instance);
    m_instance = new StreamingManager;
}

void StreamingManager::freeInstance()
{
    assert(m_instance);
    delete m_instance;
}

StreamingManager *StreamingManager::instance()
{
    return m_instance;
}

QString StreamingManager::streamURL(BitTorrent::Torrent *torrent, int fileIndex)
{
    qDebug("path %s", makePath(torrent, fileIndex).toUtf8().data());
    return QString::fromLatin1("http://localhost:%1/%2")
                .arg(QString::number(m_server.serverPort())
                     , makePath(torrent, fileIndex));
}

void StreamingManager::handleRequest()
{
    while (auto request = m_server.nextPendingRequest())
        processRequest(request);
}

StreamingManager::StreamingManager(QObject *parent)
    : QObject {parent}
{
    connect(&m_server, &StreamingServer::newPendingRequest
            , this, &StreamingManager::handleRequest);

    m_server.listen();
}

void StreamingManager::processRequest(StreamRequest *request)
try
{
    qDebug("new request");
    const auto httpRequest = request->request();

    const auto torrentFile = breakPath(httpRequest.path.mid(1));
    if (!torrentFile.has_value())
        throw NotFoundHTTPError();

    if (httpRequest.method == Http::HEADER_REQUEST_METHOD_HEAD)
        doHEAD(request, torrentFile->torrent, torrentFile->fileIndex);
    else if (httpRequest.method == Http::HEADER_REQUEST_METHOD_GET)
        doGET(request, torrentFile->torrent, torrentFile->fileIndex);
    else
        throw MethodNotAllowedHTTPError();
}
catch (const HTTPError &error)
{
    qDebug("request error %d", error.statusCode());
    const auto content = error.message().toUtf8();
    auto response = request->send({static_cast<uint>(error.statusCode()), error.statusText()}
                                  , {
                                      {Http::HEADER_CONTENT_TYPE, Http::CONTENT_TYPE_TXT},
                                      {Http::HEADER_CONTENT_LENGTH, QString::number(content.length())}
                                  }, content.length());

    if (content.size() > 0)
        response->write(content.data(), content.size());
}

void StreamingManager::doHEAD(StreamRequest *request, BitTorrent::Torrent *torrent, int fileIndex)
{
    qDebug("doHEAD");
    const auto fileSize = torrent->fileSize(fileIndex);
    const auto fileMIMEType = QMimeDatabase().mimeTypeForFile(torrent->filePath(fileIndex).data()).name();
    request->send({200, u"Ok"_qs}
                  , {
                      {u"accept-ranges"_qs, u"bytes"_qs},
                      {u"connection"_qs, u"close"_qs},
                      {u"content-length"_qs, QString::number(fileSize)},
                      {Http::HEADER_CONTENT_TYPE, fileMIMEType}
                  });
}

void StreamingManager::doGET(StreamRequest *request, BitTorrent::Torrent *torrent, int fileIndex)
{
    qDebug("doGET");
    const auto httpRequest = request->request();
    const auto fileSize = torrent->fileSize(fileIndex);
    const auto fileMIMEType = QMimeDatabase().mimeTypeForFile(torrent->filePath(fileIndex).data()).name();

    Range range {0, fileSize - 1};

    if (httpRequest.headers.contains(Http::HEADER_RANGE))
    {
        const auto httpRange = extractRangeFromHTTPfield(httpRequest.headers[Http::HEADER_RANGE], fileSize);
        if (!httpRange.has_value() || (httpRange->size() > fileSize))
            throw InvalidRangeHTTPError();

        range = *httpRange;
    }

    qDebug("range %lld-%lld", range.firstBytePos, range.lastBytePos);
    auto streamResponse = request->send({206, u"Partial Content"_qs},
                                        {
                                            {u"accept-ranges"_qs, u"bytes"_qs},
                                            {u"content-length"_qs, QString::number(range.size())},
                                            {Http::HEADER_CONTENT_TYPE, fileMIMEType},
                                            {u"content-range"_qs, u"bytes %1-%2/%3"_qs
                                                                  .arg(QString::number(range.firstBytePos)
                                                                       , QString::number(range.lastBytePos)
                                                                       , QString::number(fileSize))
                                            }
                                        }, range.size());

    auto fileReader = new TorrentFileReader(torrent, fileIndex, range.firstBytePos, range.lastBytePos, streamResponse, request);

    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentAboutToBeRemoved, request, [fileReader, torrent](BitTorrent::Torrent *removedTorrent)
    {
        if (removedTorrent == torrent)
            delete fileReader;
    });
}

TorrentFileReader::TorrentFileReader(BitTorrent::Torrent *torrent, int fileIndex
                    , qint64 firstBytePos, qint64 lastBytePos, StreamResponse *sink, QObject *parent)
    : QObject {parent}
    , m_torrent {torrent}
    , m_fileIndex {fileIndex}
    , m_lastBytePos {lastBytePos}
    , m_firstBytePos {firstBytePos}
    , m_lastPiece {m_torrent->info().mapFile(fileIndex, lastBytePos, 0).index}
    , m_sink {sink}
{
    connect(m_sink, &StreamResponse::bytesWritten
            , this, &TorrentFileReader::tryEnqueueReadPiece);

    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentReadPieceFinished
            , this, &TorrentFileReader::handleReadPiece);

    m_pendingEnqueue = true;
    tryEnqueueReadPiece();

    const bool requiresEndPiece = lastBytePos + torrent->pieceLength() >= torrent->fileSize(fileIndex);
    if (requiresEndPiece && !torrent->havePiece(m_lastPiece))
        torrent->setPieceDeadline(m_lastPiece, 2000, false);
}

TorrentFileReader::~TorrentFileReader()
{
    for (const auto index: m_advancePieces)
        m_torrent->resetPieceDeadline(index);
}


void TorrentFileReader::handleReadPiece(BitTorrent::Torrent *torrent, const int pieceIndex
                                        , const boost::shared_array<char> &data, const int size)
{
    if ((torrent != m_torrent) || (pieceIndex != m_currentPieceInfo.index) || !m_sink || m_sink->isClosed())
        return;

    assert(m_currentPieceInfo.start < size);
    const int requiredSize = qMin(m_currentPieceInfo.length, size - m_currentPieceInfo.start);
    m_sink->write(data.get() + m_currentPieceInfo.start, requiredSize);

    m_firstBytePos += requiredSize;
    m_currentPieceInfo = {-1, -1, -1};
    m_pendingEnqueue = (m_firstBytePos < m_lastBytePos);
    tryEnqueueReadPiece();
}

void TorrentFileReader::handleReadPieceFailed(BitTorrent::Torrent *torrent, const int pieceIndex, const QString &error)
{
    qDebug("failed to read %d, msg: %s", pieceIndex, qUtf8Printable(error));

    if (m_sink)
        m_sink->close();
}

void TorrentFileReader::readPiece()
{
    m_pendingEnqueue = false;

    m_currentPieceInfo = m_torrent->info().mapFile(m_fileIndex, m_firstBytePos, qMin(m_lastBytePos - m_firstBytePos + 1, m_torrent->pieceLength()));

    const int pieceDownloadTime = pieceDownloadTimeMsec(m_torrent);

    if (m_torrent->havePiece(m_currentPieceInfo.index))
        m_torrent->readPiece(m_currentPieceInfo.index);
    else
        m_torrent->setPieceDeadline(m_currentPieceInfo.index, 500, true);
}

void TorrentFileReader::tryEnqueueReadPiece()
{
    if (!m_sink || m_sink->isClosed() || !m_pendingEnqueue)
        return;

    int MAX_OUTSTANDING_SIZE = 32 * 1024 * 1024;
    if (m_sink->bytesToWrite() < MAX_OUTSTANDING_SIZE)
        readPiece();

    prioritizeAdvancePieces();
}

void TorrentFileReader::prioritizeAdvancePieces()
{
    const auto oldAdvancePieces = m_advancePieces;
    m_advancePieces.clear();

    const int BUFFER_SIZE = 30 * 1024 * 1024;

    const auto advancePieceCount = BUFFER_SIZE / m_torrent->pieceLength();
    const auto advancePieceStart = m_currentPieceInfo.index + 1;

    const int pieceDownloadTime = pieceDownloadTimeMsec(m_torrent);

    for (auto i = 0, piece = advancePieceStart + i
         ; (m_advancePieces.size() < advancePieceCount) && piece < m_lastPiece
         ; piece = advancePieceStart + (++i))
    {

        if (!m_torrent->havePiece(piece))
        {
            // libtorrent will automatically not request pieces if it overloads the peers
            m_torrent->setPieceDeadline(piece, 1000 + (100 * ( m_advancePieces.size() + 2)), false);
            m_advancePieces.push_back(piece);
        }
    }

    m_advancePieces.push_back(m_currentPieceInfo.index);

    for (const auto index: oldAdvancePieces)
    {
        m_torrent->resetPieceDeadline(index);
    }
}
