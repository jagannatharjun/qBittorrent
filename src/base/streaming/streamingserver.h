#ifndef STREAMINGSERVER_H
#define STREAMINGSERVER_H

#include "base/http/types.h"

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>

class QIODevice;
class QTcpServer;
class QTcpSocket;
class QUrl;

class StreamResponse : public QObject
{
    Q_OBJECT
public:
    StreamResponse(QIODevice *sink , qint64 maxSize, QObject *parent = nullptr);

    void write(const char *data, qint64 len);

    qint64 bytesToWrite() const;

    qint64 pendingSize() const { return m_maxSize; }

    bool isCompleted() const { return m_maxSize == 0; }

    bool isClosed() const { return !m_sink->isOpen(); }

    void close();

signals:
    void bytesWritten();
    void completed();

private:
    QIODevice *m_sink;
    qint64 m_maxSize;
};

class StreamRequest : public QObject
{
    Q_OBJECT
public:
    StreamRequest(const Http::Request &request, QIODevice *sink, QObject *parent = nullptr);

    const Http::Request &request() const;

    StreamResponse *send(const Http::ResponseStatus &status, Http::HeaderMap headers, qint64 contentSize = 0);

    bool closeAfter() const;

signals:
    void completed();

private:
    QIODevice *m_sink;
    Http::Request m_request;
    StreamResponse *m_streamResponse {};
    bool m_closeAfter = false;
};

class StreamingConnection : public QObject
{
    Q_OBJECT

public:
    StreamingConnection(QTcpSocket *socket, QObject *parent = nullptr);

    bool hasExpired(qint64 timeout);

    void close();

signals:
    void readyRequest(StreamRequest *request);

private slots:
    void parseRequest();
    void freeRequest();

private:
    QTcpSocket *m_socket {};
    QByteArray m_receivedData;
    QElapsedTimer m_idleTimer;
    QPointer<StreamRequest> m_request {};
};

class StreamingServer : public QObject
{
    Q_OBJECT

public:
    StreamingServer(QObject *parent = nullptr);

    void listen();

    StreamRequest *nextPendingRequest();

    bool hasPendingRequest();

    QHostAddress serverAddress() const;

    quint16 serverPort() const;

signals:
    void newPendingRequest();

private slots:
    void handleNewConnection();

private:
    QTcpServer *m_server;
    QSet<StreamingConnection *> m_connections;
    QVector<QPointer<StreamRequest>> m_pendingRequest;
};

#endif // STREAMINGSERVER_H
