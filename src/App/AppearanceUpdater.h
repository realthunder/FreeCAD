/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#ifndef APP_APPEARANCEUPDATER_H
#define APP_APPEARANCEUPDATER_H

#include <FCGlobal.h>

namespace App
{

class DocumentObject;

/** Tells the objects made from one that is drawn differently
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.2. A face takes the look of the
 * face it was made from, so an object whose elements are drawn differently
 * has to say so to what was made from it -- which is no recompute: no shape
 * is made again.
 *
 * An object whose drawn looks changed gives itself to addObject(). When the
 * outermost AppearanceUpdater in scope ends, every object that depends on
 * one of those is told (GeoFeature::onSourceAppearanceChanged()), once, in
 * the order of their dependencies. Those being recomputed or about to be are
 * left out: their shape is made again, and their looks with it.
 *
 * This is what Gui::ColorUpdater does among view providers.
 */
class AppExport AppearanceUpdater
{
public:
    AppearanceUpdater();
    ~AppearanceUpdater();
    AppearanceUpdater(const AppearanceUpdater &) = delete;
    AppearanceUpdater &operator=(const AppearanceUpdater &) = delete;

    /// \a obj is drawn differently. Nothing is kept outside a scope.
    static void addObject(DocumentObject *obj);
};

}  // namespace App

#endif  // APP_APPEARANCEUPDATER_H
