/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef GUI_SANDBOX_GUI_H
#define GUI_SANDBOX_GUI_H

/* The GUI side of the Python sandbox (docs/Sandbox.md 7.9, G2a): the
 * `gui.*` bridge ops a guest's FreeCADGui module reaches the host
 * through.  A command the guest registers becomes a PythonCommand
 * holding the guest proxy's stand-in; a workbench becomes a host
 * wrapper (a subclass of the Python Workbench base) forwarding its
 * hooks to the stand-in; the toolbar and menu calls of a guest
 * workbench come back as one op on its registered name.  Every op is
 * the catalog's `gui` permission.  Nothing here runs unless a guest
 * registers something.
 */

#include <FCGlobal.h>

namespace Gui
{
namespace SandboxGui
{

/// Register the `gui.*` op family with the sandbox bridge.  Idempotent;
/// a no-op in a build without the sandbox host.
GuiExport void registerOps();

}  // namespace SandboxGui
}  // namespace Gui

#endif  // GUI_SANDBOX_GUI_H
