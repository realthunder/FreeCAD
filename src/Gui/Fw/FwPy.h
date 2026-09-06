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

#ifndef GUI_FW_PY_H
#define GUI_FW_PY_H

/* The host widget layer (docs/Sandbox.md 7.12): the Python surface.
 *
 * `FreeCADGui.FormWidgets` inspects the store -- the ids, each object's
 * class, properties and touched set, whether it is realized, its Qt
 * widget -- for the gates and for debugging.  Plus the variant <->
 * Python conversions the bridge shares.
 */

#include <QVariant>

#include <FCGlobal.h>

typedef struct _object PyObject;

namespace Gui
{
namespace Fw
{

/// A Python object for a variant: bool, int, float, str, list, dict;
/// None for an invalid one.  New reference.
GuiExport PyObject* variantToPy(const QVariant& value);
/// A variant for a Python object (the same types); invalid for others.
GuiExport QVariant pyToVariant(PyObject* obj);
/// Add the `FormWidgets` module to `parent` (FreeCADGui).
GuiExport void addPyModule(PyObject* parent);

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_PY_H
