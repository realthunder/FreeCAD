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

#include "PreCompiled.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include "ClipboardFormats.h"

using namespace Gui;

namespace
{

bool known = false;
bool listening = false;

QStringList& formats()
{
    static QStringList list;
    return list;
}

}  // namespace

const QStringList& ClipboardFormats::get()
{
    QClipboard* clipboard = QApplication::clipboard();
    if (!clipboard) {
        // No application (yet, or any more): nothing to remember
        formats().clear();
        return formats();
    }
    if (!listening) {
        listening = true;
        QObject::connect(clipboard, &QClipboard::dataChanged, clipboard, []() {
            known = false;
        });
        // And when the application is come back to, whether the change was
        // announced or not: what is pasted here from elsewhere was copied
        // while another application was the active one.
        QObject::connect(qApp,
                         &QGuiApplication::applicationStateChanged,
                         clipboard,
                         [](Qt::ApplicationState state) {
                             if (state == Qt::ApplicationActive) {
                                 known = false;
                             }
                         });
    }
    if (!known) {
        formats().clear();
        if (const QMimeData* mime = clipboard->mimeData()) {
            formats() = mime->formats();
        }
        known = true;
    }
    return formats();
}

void ClipboardFormats::invalidate()
{
    known = false;
}
