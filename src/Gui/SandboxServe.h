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

#ifndef GUI_SANDBOXSERVE_H
#define GUI_SANDBOXSERVE_H

#include <FCGlobal.h>

namespace Gui
{
namespace SandboxServe
{

/** Mount the sandbox guest's files on the scene stream server, for a
 * guest booted in the viewer's page (docs/Sandbox.md 7.20, C1):
 *
 *   GET /pyodide/boot.json                  behind the door
 *   GET /pyodide/runtime/<version>/<file>   the pinned runtime files
 *   GET /pyodide/wheels/<file>              fcx_image and the bundled wheels
 *   GET /pyodide/packages/<file>            the user's package set
 *
 * boot.json names what the desktop runtime would boot with -- the same
 * runtime, wheel, bundled wheels and package set, resolved the same way
 * (ImageHost::location, Pyodide::layout) -- so a page's guest is the
 * desktop's.  The files are served ahead of the door: they are published
 * code, and pyodide fetches them by URL arithmetic that cannot carry the
 * link's ?token=.  Only the files boot.json names are served, nothing
 * else under those directories.
 *
 * Resolved here, on the calling thread, and answered from that snapshot
 * on the server's threads: the evaluation lock of ImageHost is never
 * taken by a request.  Called again (every serve does), it re-resolves.
 */
GuiExport void install();

}  // namespace SandboxServe
}  // namespace Gui

#endif  // GUI_SANDBOXSERVE_H
