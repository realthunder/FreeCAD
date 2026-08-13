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

#ifndef GUI_MESSAGECOLLAPSE_H
#define GUI_MESSAGECOLLAPSE_H

#include <cstddef>
#include <QChar>
#include <QString>

namespace Gui
{

/** Key two messages are "the same message" by, for collapsing repeats on screen.
 *
 * Digits are skipped rather than compared, because the messages that arrive in
 * floods are the same sentence carrying a different number - a source line, an
 * element index, a coordinate - and a reader gains nothing from seeing each
 * variant. Only the first @a keyLength non-digit characters are keyed on, so a
 * long message is judged by its opening rather than by a tail that may hold the
 * one number that was not skipped.
 *
 * FNV-1a. A collision costs one wrongly collapsed line and nothing else, so 64
 * bits is far more than this needs.
 */
inline std::size_t messageCollapseKey(const QString& text, int keyLength)
{
    std::size_t hash = 1469598103934665603ULL;
    int taken = 0;
    for (QChar chr : text) {
        if (chr.isDigit()) {
            continue;
        }
        hash = (hash ^ chr.unicode()) * 1099511628211ULL;
        if (++taken >= keyLength) {
            break;
        }
    }
    return hash;
}

}  // namespace Gui

#endif  // GUI_MESSAGECOLLAPSE_H
