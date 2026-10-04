// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef PART_LINKARRAYS_H
#define PART_LINKARRAYS_H

#include <App/LinkArray.h>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** Upstream's link arrays, one class per kind of pattern
 *
 * Each is an App::LinkArray with its PatternType preset, so that upstream's
 * files load. The array itself, and the switch to another kind, are
 * App::LinkArray's. An array is saved as the class of the kind it has then,
 * so that upstream reads it as that kind.
 */
class PartExport LinkArrayLinear: public App::LinkArray
{
    PROPERTY_HEADER_WITH_EXTENSIONS(Part::LinkArrayLinear);

public:
    LinkArrayLinear();
};

class PartExport LinkArrayPolar: public App::LinkArray
{
    PROPERTY_HEADER_WITH_EXTENSIONS(Part::LinkArrayPolar);

public:
    LinkArrayPolar();
};

class PartExport LinkArrayCircular: public App::LinkArray
{
    PROPERTY_HEADER_WITH_EXTENSIONS(Part::LinkArrayCircular);

public:
    LinkArrayCircular();
};

class PartExport LinkArrayPath: public App::LinkArray
{
    PROPERTY_HEADER_WITH_EXTENSIONS(Part::LinkArrayPath);

public:
    LinkArrayPath();
};

class PartExport LinkArrayPoint: public App::LinkArray
{
    PROPERTY_HEADER_WITH_EXTENSIONS(Part::LinkArrayPoint);

public:
    LinkArrayPoint();
};

}  // namespace Part

#endif  // PART_LINKARRAYS_H
