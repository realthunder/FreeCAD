/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#ifndef GUI_COIN_LAZY_ELEMENT_EX_H
#define GUI_COIN_LAZY_ELEMENT_EX_H

#include <cstdint>
#include "../InventorBase.h"

class SoState;

namespace Gui {

/** Runtime binding to the coin fork's extended lazy element.
 *
 * SoLazyElement carries diffuse and transparency as pointer-plus-count
 * but ambient, emissive, specular and shininess as single values, so
 * per-face arrays of those four die at the element boundary. The coin
 * fork adds a derived element carrying their array form, reachable
 * through a small exported C surface (coin_lazyex_*). This class binds
 * that surface with dlsym at startup: present means the render cache
 * can capture per-face materials, absent (stock Coin) means exactly
 * the old behavior. Nothing here is a link-time dependency on the
 * fork.
 */
class GuiExport CoinLazyElementEx
{
public:
    /// Bind the symbols and install the element into SoCallbackAction.
    /// Idempotent; false when running against a Coin without the
    /// extension. Must be called after SoDB::init().
    static bool install();

    /// True once install() succeeded.
    static bool available();

    /// Each returns the entry count of the field's array form in the
    /// given traversal state (0 = scalar only, or extension absent)
    /// and borrows a pointer valid only during the traversal: 3 floats
    /// per entry for the colors, 1 for shininess. \a nodeid receives
    /// the id of the node the array was captured from.
    //@{
    static int getAmbient(SoState *state, const float **values, uint64_t *nodeid = nullptr);
    static int getEmissive(SoState *state, const float **values, uint64_t *nodeid = nullptr);
    static int getSpecular(SoState *state, const float **values, uint64_t *nodeid = nullptr);
    static int getShininess(SoState *state, const float **values, uint64_t *nodeid = nullptr);
    //@}
};

} // namespace Gui

#endif // GUI_COIN_LAZY_ELEMENT_EX_H
