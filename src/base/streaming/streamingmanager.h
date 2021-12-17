#ifndef STREAMINGMANAGER_HPP
#define STREAMINGMANAGER_HPP

#include "streamingserver.h"
#include "base/bittorrent/session.h"
#include "base/bittorrent/torrentinfo.h"
#include <QObject>

namespace BitTorrent
{
    class Session;
    class Torrent;
}

class TorrentFileReader : public QObject
{
    Q_OBJECT

public:
    TorrentFileReader(BitTorrent::Torrent *torrent, int fileIndex
                      , qint64 firstBytePos, qint64 lastBytePos, StreamResponse *sink, QObject *parent);

    ~TorrentFileReader();

private:
    void handleReadPiece(BitTorrent::Torrent *torrent, const int pieceIndex, const boost::shared_array<char> &data, int size);
    void handleReadPieceFailed(BitTorrent::Torrent *torrent, const int pieceIndex, const QString &error);

    void readPiece();
    void tryEnqueueReadPiece();
    void prioritizeAdvancePieces();

    BitTorrent::Torrent *m_torrent;
    const int m_fileIndex;
    const qint64 m_lastBytePos;
    qint64 m_firstBytePos;
    const qint64 m_lastPiece;
    QPointer<StreamResponse> m_sink;
    BitTorrent::PieceFileInfo m_currentPieceInfo;
    QVector<int> m_advancePieces;
    bool m_pendingEnqueue = false;
};

class StreamingManager : public QObject
{
    Q_OBJECT
public:
    static void initInstance();
    static void freeInstance();
    static StreamingManager *instance();

    QString streamURL(BitTorrent::Torrent *torrent, int fileIndex);

private slots:
    void handleRequest();

private:
    explicit StreamingManager(QObject *parent = nullptr);

    void processRequest(StreamRequest *request);
    void doHEAD(StreamRequest *request, BitTorrent::Torrent *torrent, int fileIndex);
    void doGET(StreamRequest *request, BitTorrent::Torrent *torrent, int fileIndex);

    static StreamingManager *m_instance;

    StreamingServer m_server;
};

#endif // STREAMINGMANAGER_HPP
