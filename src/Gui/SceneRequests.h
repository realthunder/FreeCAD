/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_SCENEREQUESTS_H
#define GUI_SCENEREQUESTS_H

#include <cstdint>
#include <string>
#include <vector>

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <fastsignals/signal.h>
#include <FCGlobal.h>

namespace Gui
{

/** A file a client of a served document sent to be merged
 * (docs/TransactionLog.md sec 30.20 H4-H7, 30.23): kept as it came, and
 * not read. Nothing of it is opened, replayed or made until the owner asks
 * -- the log panel's request list is where -- and what it then becomes is
 * a branch, by Document::importFork(), with the sender in the import's
 * record.
 */
struct SceneRequest
{
    uint64_t id {0};
    std::string document;   ///< the served document it was sent to, by name
    QString name;           ///< the file's name, reduced to one
    QString path;           ///< where the host put it
    qint64 size {0};
    QString sender;         ///< who sent it
    QString senderKind;     ///< how that name is known: App::Actor's kinds
    double time {0};        ///< when it arrived, seconds since the epoch
};

namespace SceneRequests
{

/// The requests sent to `document`, oldest first; every one when empty.
GuiExport std::vector<SceneRequest> list(const std::string& document = std::string());
/// Request `id`; false when there is none.
GuiExport bool find(uint64_t id, SceneRequest& request);
/// Forget request `id`, its file with it unless `keepFile`. False when
/// there is none.
GuiExport bool drop(uint64_t id, bool keepFile = false);
/// Emitted when a request arrives or goes.
GuiExport fastsignals::signal<void()>& signalChanged();

/** The most one upload may carry, in bytes: a panel's file chooser
 * (`widgets.upload`, docs/Sandbox.md 7.22) and a request alike. A setting
 * (docs/TransactionLog.md sec 30.23): the preference `UploadLimitMB` of
 * `BaseApp/Preferences/SceneShare`, 16 unless set, which `FC_SERVE_UPLOAD_MB`
 * presets for a headless serve and setUploadLimit() overrides for the
 * life of the process. Never more than one frame of the socket carries
 * once the bytes are base64: ceiling().
 */
GuiExport qint64 uploadLimit();
/// Override the limit, in bytes; 0 goes back to the preference.
GuiExport void setUploadLimit(qint64 bytes);
/// What no setting raises the limit past.
GuiExport qint64 ceiling();

/// The name a client asked for, reduced to a plain file name: no directory
/// part, a conservative set of characters, no leading dot. Empty when
/// nothing is left.
GuiExport QString sanitizedName(const QString& given);
/// A path in `dir` nothing holds yet for a file named `base`.
GuiExport QString uniquePath(const QString& dir, const QString& base);
/** The bytes of an upload request's base64 `data`, checked against
 * uploadLimit() before and after decoding. False with `error` the reply
 * to send -- `TooLarge`, `BadRequest` -- when they cannot be taken.
 */
GuiExport bool decode(const QJsonObject& req, QByteArray& bytes, QJsonObject& error);

/// Register `requests.send`. Idempotent.
GuiExport void install();

}  // namespace SceneRequests
}  // namespace Gui

#endif  // GUI_SCENEREQUESTS_H
