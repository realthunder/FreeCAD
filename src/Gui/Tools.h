/***************************************************************************
 *   Copyright (c) 2020 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef GUI_TOOLS_H
#define GUI_TOOLS_H

#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QWidget>
#include <FCGlobal.h>

namespace Gui {

/*!
 * \brief The QtTools class
 * Helper class to reduce adding a lot of extra QT_VERSION checks to client code.
 */
class GuiExport QtTools {
public:
    static int horizontalAdvance(const QFontMetrics& fm, QChar ch) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        return fm.horizontalAdvance(ch);
#else
        return fm.width(ch);
#endif
    }
    static int horizontalAdvance(const QFontMetrics& fm, const QString& text, int len = -1) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
        return fm.horizontalAdvance(text, len);
#else
        return fm.width(text, len);
#endif
    }
    static bool matches(QKeyEvent* ke, const QKeySequence& ks) {
        uint searchkey = (ke->modifiers() | ke->key()) & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
        return ks == QKeySequence(searchkey);
    }
    /*!
     * The platform's conventional key for deleting. Upstream keeps this
     * beside the other Qt-version helpers; note upstream's QtTools is a
     * namespace of inline functions where ours is a class of statics, so
     * this is a static member rather than a free function. Call sites
     * spell it the same either way.
     */
    static QKeySequence::StandardKey deleteKeySequence() {
#ifdef FC_OS_MACOSX
        return QKeySequence::Backspace;
#else
        return QKeySequence::Delete;
#endif
    }
};

class ChildrenSignalBlocker
{
public:
    ChildrenSignalBlocker(QWidget *parent,
                          const std::vector<QWidget*> &excludes = {})
    {
        for (auto widget : parent->findChildren<QWidget*>()) {
            if (std::find(excludes.begin(), excludes.end(), widget) != excludes.end())
                continue;
            children.emplace_back(widget);
        }
    }
private:
    std::vector<QSignalBlocker> children;
};

} // namespace Gui

#endif // GUI_TOOLS_H
