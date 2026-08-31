/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef SPREADSHEETGUI_SHEETCONTROL_H
#define SPREADSHEETGUI_SHEETCONTROL_H

namespace SpreadsheetGui
{

/// Put a Spreadsheet::Sheet on the thin client's control channel
/// (docs/SpreadsheetRemote.md sec 3): the ops `sheet.get` (the used
/// range, formatted the way the desktop formats it) and `sheet.set`
/// (one cell edit, transacted and recomputed), plus the debounced
/// unsolicited `sheet.changed` push that tells a viewer to re-get.
///
/// Registered from the Spreadsheet Gui module's init, not from core
/// Gui: the control channel lives in Gui and must not link a
/// workbench, so ops arrive through Gui::registerSceneControlOp.
void installSheetControlOps();

}  // namespace SpreadsheetGui

#endif  // SPREADSHEETGUI_SHEETCONTROL_H
