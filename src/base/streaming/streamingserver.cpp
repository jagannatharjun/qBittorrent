#include "streamingserver.h"

#include "base/algorithm.h"
#include "base/http/responsegenerator.h"
#include "base/http/requestparser.h"
#include "base/logger.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

StreamingServer::StreamingServer(QObject *parent)
    : m_server {new QTcpServer(this)}
{
    connect(m_server, &QTcpServer::newConnection, this, &StreamingServer::handleNewConnection);

    auto *dropConnectionTimer = new QTimer(this);
    connect(dropConnectionTimer, &QTimer::timeout, this, [this]()
    {
        Algorithm::removeIf(m_connections, [](StreamingConnection *connection)
        {
            const auto KEEP_ALIVE_DURATION = 7 * 1000;
            if (!connection->hasExpired(KEEP_ALIVE_DURATION))
                return false;

            qDebug("removing connection");
            connection->deleteLater();
            return true;
        });

    });

    dropConnectionTimer->start(3 * 1000);
}

void StreamingServer::listen()
{
    const QHostAddress ip = QHostAddress::Any;
    const int port = 0;

    if (m_server->isListening())
    {
        if (m_server->serverPort() == port)
            // Already listening on the right port, just return
            return;

        // Wrong port, closing the server
        m_server->close();
    }

    // Listen on the predefined port
    const bool listenSuccess = m_server->listen(ip, port);

    if (listenSuccess)
    {
        LogMsg(tr("Torrent streaming server: Now listening on IP: %1, port: %2")
                   .arg(ip.toString(), QString::number(m_server->serverPort()))
                   , Log::INFO);
    }
    else
    {
        LogMsg(tr("Torrent streaming server: Unable to bind to IP: %1, port: %2. Reason: %3")
                   .arg(ip.toString(), QString::number(port), m_server->errorString()),
               Log::WARNING);
    }
}

StreamRequest *StreamingServer::nextPendingRequest()
{
    if (!hasPendingRequest())
        return nullptr;

    StreamRequest *request {};
    do
    {
        request = m_pendingRequest.back();
        m_pendingRequest.pop_back();
    } while (!request && !m_pendingRequest.empty());

    return request;
}

bool StreamingServer::hasPendingRequest()
{
    return !m_pendingRequest.empty();
}

QHostAddress StreamingServer::serverAddress() const
{
    return m_server->serverAddress();
}

quint16 StreamingServer::serverPort() const
{
    return m_server->serverPort();
}

void StreamingServer::handleNewConnection()
{
    qDebug("StreamingServer::handleNewConnection");
    QTcpSocket *connection {};
    while ((connection = m_server->nextPendingConnection()))
    {
        auto con = new StreamingConnection(connection, this);
        connect(con, &StreamingConnection::readyRequest,
                this, [this](StreamRequest *request)
        {
            m_pendingRequest.push_back(request);
            emit newPendingRequest();
        });

        m_connections.insert(con);
    }
}

StreamingConnection::StreamingConnection(QTcpSocket *socket, QObject *parent)
    : QObject {parent}
    , m_socket {socket}
{
    m_socket->setParent(this);

    connect(m_socket, &QTcpSocket::readyRead, this, &StreamingConnection::parseRequest);
    connect(m_socket, &QTcpSocket::bytesWritten, this, [this]()
    {
        m_idleTimer.restart();
    });

    connect(m_socket, &QTcpSocket::disconnected, this, &StreamingConnection::freeRequest);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this]()
    {
        qDebug("socket error %s", qUtf8Printable(m_socket->errorString()));
    });

    parseRequest();
}

bool StreamingConnection::hasExpired(qint64 timeout)
{
    return !m_socket->isOpen()
            || (m_idleTimer.hasExpired(timeout)
                && (m_socket->bytesToWrite() == 0)
                && !m_request);
}

void StreamingConnection::close()
{
    qDebug("closing connection");
    m_socket->close();
}

void StreamingConnection::parseRequest()
{
    assert(!m_request); // a request is already in processed

    m_idleTimer.restart();
    m_receivedData.append(m_socket->readAll());
    qDebug("parsing request data %d, \"%s\"", m_receivedData.size(), m_receivedData.data());

    using namespace Http;

    while (!m_receivedData.isEmpty())
    {
        const RequestParser::ParseResult result = RequestParser::parse(m_receivedData);

        switch (result.status)
        {
        case RequestParser::ParseStatus::Incomplete:
            {
                const long bufferLimit = RequestParser::MAX_CONTENT_SIZE * 1.1;  // some margin for headers
                if (m_receivedData.size() > bufferLimit)
                {
                    Logger::instance()->addMessage(tr("Http request size exceeds limitation, closing socket. Limit: %1, IP: %2")
                        .arg(bufferLimit).arg(m_socket->peerAddress().toString()), Log::WARNING);

                    Response resp(413, u"Payload Too Large"_qs);
                    resp.headers[HEADER_CONNECTION] = u"close"_qs;

                    m_socket->write(toByteArray(resp));
                    m_socket->close();
                }
            }
            return;

        case RequestParser::ParseStatus::BadRequest:
            {
                Logger::instance()->addMessage(tr("Bad Http request, closing socket. IP: %1")
                    .arg(m_socket->peerAddress().toString()), Log::WARNING);

                Response resp(400, u"Bad Request"_qs);
                resp.headers[HEADER_CONNECTION] = u"close"_qs;

                m_socket->write(toByteArray(resp));
                m_socket->close();
            }
            return;

        case RequestParser::ParseStatus::OK:
            {
                m_request = new StreamRequest(result.request, m_socket, this);
                connect(m_request, &StreamRequest::completed, this, &StreamingConnection::freeRequest);

                emit readyRequest(m_request);
                m_receivedData = m_receivedData.mid(result.frameSize);
            }
            break;

        default:
            Q_ASSERT(false);
            return;
        }
    }
}

void StreamingConnection::freeRequest()
{
    if (!m_request)
        return;

    if (m_request->closeAfter())
        m_socket->close();

    m_request->deleteLater();
    m_request = nullptr;
}

StreamResponse::StreamResponse(QIODevice *sink, qint64 maxSize, QObject *parent)
    : QObject {parent}
    , m_sink {sink}
    , m_maxSize {maxSize}
{
    connect(m_sink, &QIODevice::bytesWritten, this, &StreamResponse::bytesWritten);
}

void StreamResponse::write(const char *data, qint64 len)
{
    assert(m_maxSize >= len);

    m_maxSize = m_maxSize - len;
    m_sink->write(data, len);

    if (m_maxSize <= 0)
        emit completed();
}

qint64 StreamResponse::bytesToWrite() const
{
    return m_sink->bytesToWrite();
}

void StreamResponse::close()
{
    m_sink->close();
}

StreamRequest::StreamRequest(const Http::Request &request, QIODevice *sink, QObject *parent)
    : QObject {parent}
    , m_sink {sink}
    , m_request {request}
{
}

const Http::Request &StreamRequest::request() const
{
    return m_request;
}

StreamResponse *StreamRequest::send(const Http::ResponseStatus &status, Http::HeaderMap headers, const qint64 contentSize)
{
    using namespace Http;

    m_closeAfter = headers.value(Http::HEADER_CONNECTION, u"closed"_qs) != u"keep-alive"_qs;

    QByteArray buf;
    buf.reserve(10 * 1024);

    // Status Line
    buf += u"HTTP/%1 %2 %3"_qs
        .arg(u"1.1"_qs,  // TODO: depends on request
             QString::number(status.code), status.text)
        .toLatin1()
        .append(CRLF);

    // Header Fields
    headers[HEADER_DATE] = httpDate();
    for (auto i = headers.constBegin(); i != headers.constEnd(); ++i)
        buf += QString::fromLatin1("%1: %2").arg(i.key(), i.value()).toLatin1().append(CRLF);

    if (contentSize != 0)
        buf += CRLF;

    m_sink->write(buf.data(), buf.size());

    qDebug("contentSize %lld", contentSize);
    if (contentSize != 0)
    {
        m_streamResponse = new StreamResponse(m_sink, contentSize, this);
        connect(m_streamResponse, &StreamResponse::completed, this, [this]()
        {
            emit completed();

            m_streamResponse = nullptr;
        });

        return m_streamResponse;
    }

    emit completed();
    return nullptr;
}

 bool StreamRequest::closeAfter() const
 {
     return m_closeAfter;
 }
