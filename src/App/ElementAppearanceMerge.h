// SPDX-License-Identifier: LGPL-2.1-or-later

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

#ifndef APP_ELEMENTAPPEARANCEMERGE_H
#define APP_ELEMENTAPPEARANCEMERGE_H

#include <string>
#include <vector>

#include "DocumentObject.h"

namespace App
{

class PropertyElementAppearance;

/** The looks of an object's elements, merged by what they are given to
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.5, docs/TransactionLog.md sec
 * 31.20. What DocumentObject::mergeUnit() does for a unit that is one
 * PropertyElementAppearance, whatever holds it: a shape's, whose names are
 * its elements', a link's or an App::Part's, whose names are paths.
 *
 * @param store: the owner's property, asked what kind of names it has and
 *               for nothing of its value
 * @param prop: its name, which is what the states have it by
 * @return False where the value cannot be merged, or the rule for what both
 *         branches gave a look says to ask: the whole of it is one question
 *         then.
 */
AppExport bool mergeElementAppearance(const PropertyElementAppearance& store,
                                      const std::string& prop,
                                      const DocumentObject::MergeUnitState& base,
                                      const DocumentObject::MergeUnitSide& ours,
                                      const DocumentObject::MergeUnitSide& theirs,
                                      DocumentObject::MergeUnitState& merged,
                                      std::vector<DocumentObject::MergeUnitNote>& notes);

}  // namespace App

#endif  // APP_ELEMENTAPPEARANCEMERGE_H
