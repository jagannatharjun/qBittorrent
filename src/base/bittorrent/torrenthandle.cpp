/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#include "torrenthandle.h"

#include <type_traits>

namespace BitTorrent
{
    uint qHash(const TorrentState key, const uint seed)
    PieceRequest::PieceRequest(int index, QObject *parent)
        : QObject(parent)
        , m_index(index)
    {
    }

    int PieceRequest::index() const
    {
        return m_index;
    }

    void PieceRequest::notifyCompleteAndDie(const QByteArray &data)
    {
        emit complete(data);
    }

    void PieceRequest::notifyErrorAndDie(const QString &message)
    {
        emit error(message);
    }

    uint qHash(const BitTorrent::TorrentState key, const uint seed)
    {
        return ::qHash(static_cast<std::underlying_type_t<TorrentState>>(key), seed);
    }

    // TorrentHandle

    const qreal TorrentHandle::USE_GLOBAL_RATIO = -2.;
    const qreal TorrentHandle::NO_RATIO_LIMIT = -1.;

    const int TorrentHandle::USE_GLOBAL_SEEDING_TIME = -2;
    const int TorrentHandle::NO_SEEDING_TIME_LIMIT = -1;

    const qreal TorrentHandle::MAX_RATIO = 9999.;
    const int TorrentHandle::MAX_SEEDING_TIME = 525600;

    void TorrentHandle::toggleSequentialDownload()
    {
        setSequentialDownload(!isSequentialDownload());
    }

    void TorrentHandle::toggleFirstLastPiecePriority()
    {
        setFirstLastPiecePriority(!hasFirstLastPiecePriority());
    }
}
