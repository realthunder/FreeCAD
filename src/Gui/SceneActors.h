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

#ifndef GUI_SCENEACTORS_H
#define GUI_SCENEACTORS_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <FCGlobal.h>

namespace App
{
class Document;
struct Actor;
}

namespace Render
{
struct SceneClientInfo;
}

namespace Gui
{

/** The connections of the scene server as actors of the transaction log
 * (docs/TransactionLog.md sec 30.3 S.b, 30.6): who a client is, set as the
 * App::ActorScope for as long as the Gui replays its event or runs its
 * operation, so the rows it makes name it as their author; and its coming
 * and going recorded in the log of the document it joined. GUI thread.
 */
namespace SceneActors
{

/** The actor a connection is, from its roster row.
 *
 * `Verified` under the identity an authenticating front door asserted
 * (docs/ShareAccess.md sec 4); `Invited` under its name when the door
 * admitted it on an invitation issued to that one name (sec 30.6 U6,
 * SceneClientInfo::invited); else `Declared` under the name of its hello,
 * `guest` when it gave none. One login per connection: its id.
 */
GuiExport App::Actor describe(const Render::SceneClientInfo& info);

/** The actor of connection `client`. Known from the roster the first time
 * it is asked for and kept until the connection is gone; a connection the
 * roster no longer has is a declared `guest`, so what it left queued is
 * still not the desktop user's.
 */
GuiExport std::shared_ptr<const App::Actor> of(uint64_t client);

/// The logins a served document has recorded, by connection.
using Logins = std::map<uint64_t, std::shared_ptr<const App::Actor>>;

/** The roster changed: record in `doc`'s log every connection that has
 * joined `group` since the last call -- past the door and with its hello
 * said, view-only ones too -- as a login, and close the session of each
 * that has left it or is now someone else. `logins` is the caller's, one
 * per served document.
 */
GuiExport void sync(const std::string& group, App::Document* doc, Logins& logins);

}  // namespace SceneActors
}  // namespace Gui

#endif  // GUI_SCENEACTORS_H
