/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_SANDBOXREMOTE_H
#define GUI_SANDBOXREMOTE_H

#include <cstddef>
#include <cstdint>

#include <FCGlobal.h>

namespace App
{
class Document;
}

namespace Render
{
struct SceneBridgeRequest;
}

namespace Gui
{
namespace SandboxRemote
{

/** The host end of a sandbox guest running in a viewer's page
 * (docs/Sandbox.md 7.20, C2): one endpoint per connection, holding the
 * handle table of that guest -- the per-connection twin of ImageHost's
 * one table -- dispatched into the same bridge ops the desktop's own
 * guest reaches.
 *
 * An Op runs under the CLIENT's principal (C3: clientPrincipalId of the
 * connection's verified identity, else its admitting grant, else the
 * connection; ExpressionSecurity::Runtime::Scope, pushed per op) with the
 * served document `doc` as the table's owner: the guest reaches that
 * document and nothing else, its FreeCAD.ActiveDocument is that document,
 * and it is held to the catalog's client column.  A view-only connection
 * gets a read-only bridge: doc.write.self refused whatever was granted.
 * The audit line names the connection.  An End clears the handles its
 * statement minted.
 * Every Op is answered through SceneStreamServer::sendBridge -- empty
 * on a build without the sandbox host.
 *
 * GUI thread only.
 */
GuiExport void handle(App::Document* doc, Render::SceneBridgeRequest& req);

/// The connection went away: its handles are released.
GuiExport void drop(uint64_t client);

/// Live endpoints (tests).
GuiExport std::size_t endpointCount();

}  // namespace SandboxRemote
}  // namespace Gui

#endif  // GUI_SANDBOXREMOTE_H
