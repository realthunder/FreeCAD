/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef GUI_CLIPBOARDFORMATS_H
#define GUI_CLIPBOARDFORMATS_H

#include <QString>
#include <QStringList>

#include <FCGlobal.h>

namespace Gui
{

/// The formats the clipboard offers, asked of it once for each change of
/// it.
///
/// For a command's isActive(), which must not ask the clipboard itself. It
/// is called on every pass over the commands -- each change of the
/// selection, each object a load brings in -- and a question to the
/// clipboard is a call into another process. Where the clipboard is
/// watched (a managed Windows box) that is 170 ms a question: Std_Paste
/// asked five and Material_Paste one, 0.76 to 0.9 s for a pass that takes
/// under a millisecond without them, with a document open or none; 13 such
/// passes ran between the slices of one load of a 686-object file.
///
/// What a paste then reads is the clipboard, as before: a list that is
/// out of date can only show the command enabled or disabled wrongly.
///
/// GUI thread only.
class GuiExport ClipboardFormats
{
public:
    /// The formats, as QMimeData::formats() of the clipboard gave them
    /// last. QMimeData::hasUrls() is the format "text/uri-list".
    static const QStringList& get();
    static bool has(const QString& format)
    {
        return get().contains(format);
    }
    /// The clipboard has changed: ask it again at the next get(). The
    /// class listens to QClipboard::dataChanged itself; this is for a
    /// listener that reads the list from its own slot and may run first.
    static void invalidate();
    /// Whether the clipboard holds what MainWindow::insertFromMimeData()
    /// takes (in MainWindow.cpp, with the formats it writes)
    static bool insertable();
};

}  // namespace Gui

#endif  // GUI_CLIPBOARDFORMATS_H
