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

#include <QStyle>
void HtmlBrowser::setContentHTML(const QString &heading, const QString &content)
{
    static const QString base = uR"(
<html>

<head>
    <style>
        body {
            background-color: {bg};
            color: {fg};
        }

        :root {
            --initial-max-width: 100%;
        }

        @media (min-resolution: 96dpi) {
            :root {
                --zoom-factor: 1;
                /* Default zoom factor */
            }
        }

        @media (min-resolution: 120dpi) {
            :root {
                --zoom-factor: 1.25;
                /* Adjust as needed */
            }
        }

        .container {
            display: flex;
            justify-content: center;
            flex-wrap: wrap;
            align-items: center;
        }

        .container img {
            margin: 10px;
            max-width:  calc(var(--initial-max-width) * var(--zoom-factor));
        }

    </style>
</head>

<body>
    {heading}
    <div class="container">
        {content}
    </div>
</body>

</html>

)"_qs;

    const QString bg = palette().color(QPalette::ColorRole::Base).name();
    const QString fg = palette().color(QPalette::ColorRole::Text).name();

    const auto html = QString(base)
            .replace(u"{bg}"_qs, bg)
            .replace(u"{fg}"_qs, fg)
            .replace(u"{heading}"_qs, heading)
            .replace(u"{content}"_qs, content);

    setHtml(html);
}
