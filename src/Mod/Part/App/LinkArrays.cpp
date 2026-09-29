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

#include "PreCompiled.h"

#include "LinkArrays.h"

using namespace Part;

namespace
{
// The class a file names for an array of each kind, which upstream reads
const char* SaveTypes[] = {"Part::LinkArrayLinear",
                           "Part::LinkArrayPolar",
                           "Part::LinkArrayCircular",
                           "Part::LinkArrayPath",
                           "Part::LinkArrayPoint",
                           nullptr};
}  // namespace

PROPERTY_SOURCE_WITH_EXTENSIONS(Part::LinkArrayLinear, App::LinkArray)

LinkArrayLinear::LinkArrayLinear()
    : App::LinkArray(App::Pattern::Type::Linear, SaveTypes)
{}

PROPERTY_SOURCE_WITH_EXTENSIONS(Part::LinkArrayPolar, App::LinkArray)

LinkArrayPolar::LinkArrayPolar()
    : App::LinkArray(App::Pattern::Type::Polar, SaveTypes)
{}

PROPERTY_SOURCE_WITH_EXTENSIONS(Part::LinkArrayCircular, App::LinkArray)

LinkArrayCircular::LinkArrayCircular()
    : App::LinkArray(App::Pattern::Type::Circular, SaveTypes)
{}

PROPERTY_SOURCE_WITH_EXTENSIONS(Part::LinkArrayPath, App::LinkArray)

LinkArrayPath::LinkArrayPath()
    : App::LinkArray(App::Pattern::Type::Path, SaveTypes)
{}

PROPERTY_SOURCE_WITH_EXTENSIONS(Part::LinkArrayPoint, App::LinkArray)

LinkArrayPoint::LinkArrayPoint()
    : App::LinkArray(App::Pattern::Type::Point, SaveTypes)
{}
