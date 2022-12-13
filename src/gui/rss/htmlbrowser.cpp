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

#include "htmlbrowser.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFuture>
#include <QBuffer>
#include <QtConcurrent/QtConcurrent>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>

#include "base/global.h"
#include "base/path.h"
#include "base/profile.h"

namespace
{
    QImage fitImage(const QByteArray &data, const QSize &maxSize)
    {
        const auto image = QImage::fromData(data);
        if (image.width() < maxSize.width())
            return image;

        return image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

HtmlBrowser::HtmlBrowser(QWidget *parent)
    : QTextBrowser(parent)
{
    // required to serialize requests
    m_worker.setMaxThreadCount(1);

    m_netManager = new QNetworkAccessManager(this);
    m_diskCache = new QNetworkDiskCache(this);
    m_diskCache->setCacheDirectory((specialFolderLocation(SpecialFolder::Cache) / Path(u"rss"_qs)).data());
    m_diskCache->setMaximumCacheSize(50 * 1024 * 1024);
    qDebug() << "HtmlBrowser  cache path:" << m_diskCache->cacheDirectory() << " max size:" << m_diskCache->maximumCacheSize() / 1024 / 1024 << "MB";
    m_netManager->setCache(m_diskCache);

    connect(m_netManager, &QNetworkAccessManager::finished, this, &HtmlBrowser::resourceLoaded);
}

HtmlBrowser::~HtmlBrowser()
{
    m_worker.waitForDone();
}

QVariant HtmlBrowser::loadResource(int type, const QUrl &name)
{
    if (type == QTextDocument::ImageResource)
    {
        QUrl url(name);
        if (url.scheme().isEmpty())
            url.setScheme(u"http"_qs);

        // TODO add support for gif files
        if (Path(url.path()).hasExtension(u".gif"_qs))
            return {};

        auto image = m_imageCache.object(url);
        if (image && !image->isNull())
            return *image;

        if (m_queue.contains(url))
            return {};

        if (QIODevice *dev = m_diskCache->data(url))
        {
            asyncRead(url, dev);
        }
        else if (!m_activeRequests.contains(url))
        {
            m_activeRequests.insert(url);
            qDebug() << "HtmlBrowser::loadResource() get " << url.toString();
            QNetworkRequest req(url);
            req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            QNetworkReply *reply = m_netManager->get(req);
            connect(reply, &QNetworkReply::downloadProgress, this, &HtmlBrowser::handleProgressChanged);
        }

        return {};
    }

    return QTextBrowser::loadResource(type, name);
}

void HtmlBrowser::resourceLoaded(QNetworkReply *reply)
{
    m_activeRequests.remove(reply->request().url());
    m_dirty.remove(reply);

    if ((reply->error() == QNetworkReply::NoError) && (reply->size() > 0))
    {
        qDebug() << "HtmlBrowser::resourceLoaded() save " << reply->request().url().toString();

        asyncPeak(reply);
    }
    else
    {
        // If resource failed to load, replace it with warning icon and store it in cache for 1 day.
        // Otherwise HTMLBrowser will keep trying to download it every time article is displayed,
        // since it's not possible to cache error responses.
        QNetworkCacheMetaData metaData;
        QNetworkCacheMetaData::AttributesMap atts;
        metaData.setUrl(reply->request().url());
        metaData.setSaveToDisk(true);
        atts[QNetworkRequest::HttpStatusCodeAttribute] = 200;
        atts[QNetworkRequest::HttpReasonPhraseAttribute] = u"Ok"_qs;
        metaData.setAttributes(atts);
        metaData.setLastModified(QDateTime::currentDateTime());
        metaData.setExpirationDate(QDateTime::currentDateTime().addDays(1));
        QIODevice *dev = m_diskCache->prepare(metaData);
        if (!dev) return;

        QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32).save(dev, "PNG");
        m_diskCache->insert(dev);

        enqueueRefresh();
    }
}

void HtmlBrowser::handleProgressChanged(qint64 bytesReceived, qint64 bytesTotal)
{
    QNetworkReply *src = qobject_cast<QNetworkReply *>(sender());
    if (!src)
        return;

    m_dirty.insert(src);
    if (m_pendingDataReloadEnqueued)
        return;

    m_pendingDataReloadEnqueued = true;
    QTimer::singleShot(500, this, [this]()
    {
        m_pendingDataReloadEnqueued = false;

        for (auto reply : qAsConst(m_dirty))
            asyncPeak(reply);

        m_dirty.clear();
    });

}

void HtmlBrowser::enqueueRefresh()
{
    if (m_refreshEnqueued)
        return;

    m_refreshEnqueued = true;

    qDebug() << "HTMLBrowser::enqueueRefresh";

    QTimer::singleShot(100, this, [this]()
    {
        m_refreshEnqueued = false;

        // Refresh the document display and keep scrollbars where they are
        int sx = horizontalScrollBar()->value();
        int sy = verticalScrollBar()->value();
        document()->setHtml(document()->toHtml());
        horizontalScrollBar()->setValue(sx);
        verticalScrollBar()->setValue(sy);
    });
}

void HtmlBrowser::asyncRead(QUrl url, QIODevice *device)
{
    const QSize maxSize = size().shrunkBy(QMargins(20, 20, 20, 20));
    if (m_queue.contains(url))
        m_queue[url].cancel();

    (m_queue[url] = QtConcurrent::run(&m_worker, [device, maxSize]()
    {
        auto data = device->readAll();
        device->deleteLater();
        return QImage(fitImage(data, maxSize));
    })).then(this, [this, url](QImage image)
    {
        m_imageCache.insert(url, new QImage(image));
        m_queue.remove(url);
        enqueueRefresh();
    });
}

void HtmlBrowser::asyncPeak(QNetworkReply *reply)
{
    const auto url = reply->request().url();
    auto buffer = new QBuffer();
    buffer->setData(reply->peek(reply->bytesAvailable()));
    buffer->open(QIODevice::ReadOnly);
    asyncRead(url, buffer);
}
