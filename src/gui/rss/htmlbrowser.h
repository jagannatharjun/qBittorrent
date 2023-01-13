/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2013  Mladen Milinkovic <max@smoothware.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#pragma once

#include <QHash>
#include <QTextBrowser>

#include <QMutex>
#include <QThread>


class QNetworkAccessManager;
class QNetworkDiskCache;
class QNetworkReply;

class NetImageLoader : public QObject
{
    Q_OBJECT
public:
    NetImageLoader(QObject *parent = nullptr);
    ~NetImageLoader();

    // all public functions are reentarent

    void load(const QUrl &url);

    const QSize &maxLoadSize() const;
    void setMaxLoadSize(const QSize &newMaxLoadSize);

signals:
    void updated(const QUrl &url, QImage incompleteImage);
    void finished(const QUrl &url, QImage image);

    void abortDownloads();

private slots:
    void handleReplyFinished(QNetworkReply *reply);
    void handleProgressUpdated();

    void readIncompleteImages();

    void _loadImpl(const QUrl &url);

private:
    QNetworkAccessManager *m_netManager = nullptr;
    QSet<QNetworkReply *> m_dirty;
    QSet<QUrl> m_activeRequests;
    bool m_readIncompleteImagesEnqueued = false;

    mutable QMutex m_lock;
    // follwing variables are protected under 'lock'
    QSize m_maxLoadSize {};
};

class HtmlBrowser final : public QTextBrowser
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(HtmlBrowser)

public:
    explicit HtmlBrowser(QWidget* parent = nullptr);
    ~HtmlBrowser();

    void setHtml(const QString &html);
    QVariant loadResource(int type, const QUrl &name) override;

private slots:
    void enqueueRefresh();
    void resourceLoaded(const QUrl &url, const QImage image);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    class ImageCache;

    bool m_refreshEnqueued = false;
    QThread m_workerThread;
    std::unique_ptr<NetImageLoader> m_imageLoader;
    std::unique_ptr<ImageCache> m_imageCache;
};

