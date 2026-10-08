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

#ifndef BASE_OLDERTYPENAME_H
#define BASE_OLDERTYPENAME_H

#include <FCGlobal.h>

namespace Base
{

class Type;

/** @name The name a file written for another FreeCAD says for a type
 *
 * A property keeps its name when its type is replaced by one of this
 * fork's that derives from it -- ShapeColor is a Gui::_PropertyShapeColor
 * that writes itself as the App::PropertyColor it was. A reader that has
 * the property under the type it was takes the type in the file for
 * another and steps over the value: this fork's release of 2025 read no
 * face colour out of a file written for it. Below schema 5, which is
 * written for readers that are not this build (Base::Writer::typeName()),
 * such a type is written by the name those readers have it under.
 *
 * Not Type::addLegacyName(): that makes a name resolve to the type, for a
 * type that was renamed. This name is another type's, and stays that
 * type's; the reader that finds it on a property of this type is handed
 * it as a type that changed (PropertyContainer::handleChangedPropertyType).
 */
//@{
/// Say what a file below schema 5 calls `type`
BaseExport void setOlderTypeName(const Type& type, const char* name);
/// What a file below schema 5 calls `type`, or nullptr: its own name
BaseExport const char* getOlderTypeName(const Type& type);
//@}

}  // namespace Base

#endif  // BASE_OLDERTYPENAME_H
