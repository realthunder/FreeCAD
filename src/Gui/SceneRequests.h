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
 * (docs/TransactionLog.md sec 30.20 H4-H7, 30.23, 30.29): kept as it came,
 * and not read. Nothing of it is opened, replayed or made until the owner
 * asks -- the log panel's request list is where -- and what it then becomes
 * is a branch, by Document::importSentFile(), with the sender in the
 * import's record.
 *
 * It is kept in the document's transaction log (App::Document::
 * keepSentFile): a row and a blob, saved with the history, so it is there
 * after a restart. A document that keeps no log has nowhere to put it, and
 * its files are kept by this process, under its temp path, as all were.
 */
struct SceneRequest
{
    /// Its number: the row of the log that holds it (`inLog`), which is
    /// one document's and may change when records are written again; else
    /// a number of this process.
    uint64_t id {0};
    bool inLog {false};
    std::string document;   ///< the served document it was sent to, by name
    QString name;           ///< the file's name, reduced to one
    QString path;           ///< where the host put it; empty when `inLog`
    qint64 size {0};
    QString sender;         ///< who sent it
    QString senderKind;     ///< how that name is known: App::Actor's kinds
    double time {0};        ///< when it arrived, seconds since the epoch
};

namespace SceneRequests
{

/// The sent files that wait, unread, for `document`, oldest first; every
/// document's when empty. One brought in and not merged is a branch, and
/// is not among them.
GuiExport std::vector<SceneRequest> list(const std::string& document = std::string());
/// Request `id` of `document` (of any, when empty); false when there is
/// none.
GuiExport bool find(uint64_t id, SceneRequest& request,
                    const std::string& document = std::string());
/// Drop request `id` of `document` unread: its file goes, and the log has
/// a record of it. `keepFile` leaves the file of one this process keeps.
/// False when there is none.
GuiExport bool drop(uint64_t id, bool keepFile = false,
                    const std::string& document = std::string());
/// The bytes held for `document`, waiting or brought in and not merged:
/// what totalLimit() bounds.
GuiExport qint64 heldBytes(const std::string& document);
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
/// Whether the preference is what decides: false while `FC_SERVE_UPLOAD_MB`
/// or setUploadLimit() holds the limit, when a control for the preference
/// would change nothing.
GuiExport bool uploadLimitIsPreference();
/// Override the limit, in bytes; 0 goes back to the preference.
GuiExport void setUploadLimit(qint64 bytes);
/// What no setting raises the limit past.
GuiExport qint64 ceiling();

/** The most that may wait for one document, in bytes, all its sent files
 * together (docs/TransactionLog.md sec 30.28 J4, 30.29): a file that would
 * take it past is refused, `TooMany`. A file waits in the document's
 * history and is saved with it, so this bounds what a client can make a
 * file grow by. The preference `RequestsTotalMB` of
 * `BaseApp/Preferences/SceneShare`, 64 unless set; `FC_SERVE_REQUESTS_MB`
 * presets it and setTotalLimit() overrides it, as for uploadLimit().
 */
GuiExport qint64 totalLimit();
GuiExport bool totalLimitIsPreference();
/// Override the total, in bytes; 0 goes back to the preference.
GuiExport void setTotalLimit(qint64 bytes);

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
