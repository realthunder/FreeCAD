// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>               *
 *   Copyright (c) 2022 Zheng, Lei <realthunder.dev@gmail.com>              *
 *   Copyright (c) 2023 FreeCAD Project Association                         *
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


#include "PreCompiled.h"  // NOLINT

#ifndef _PreComp_
#include <cstdlib>
#endif

#include <boost/regex.hpp>

#include "ComplexGeoData.h"
#include "ElementMap.h"
#include "ElementNamingUtils.h"

#include <Base/BoundBox.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Placement.h>
#include <Base/Reader.h>
#include <Base/Rotation.h>
#include <Base/Writer.h>


TYPESYSTEM_SOURCE_ABSTRACT(Data::Segment, Base::BaseClass)           // NOLINT
TYPESYSTEM_SOURCE_ABSTRACT(Data::ComplexGeoData, Base::Persistence)  // NOLINT

namespace bio = boost::iostreams;
using namespace Data;

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)

ComplexGeoData::ComplexGeoData() = default;

std::pair<std::string, unsigned long> ComplexGeoData::getTypeAndIndex(const char* Name)
{
    int index = 0;
    std::string element;
    boost::regex ex("^([^0-9]*)([0-9]*)$");
    boost::cmatch what;

    if (Name && boost::regex_match(Name, what, ex)) {
        element = what[1].str();
        index = std::atoi(what[2].str().c_str());
    }

    return std::make_pair(element, index);
}

Data::Segment* ComplexGeoData::getSubElementByName(const char* name) const
{
    auto type = getTypeAndIndex(name);
    return getSubElement(type.first.c_str(), type.second);
}

void ComplexGeoData::applyTransform(const Base::Matrix4D& rclTrf)
{
    setTransform(rclTrf * getTransform());
}

void ComplexGeoData::applyTranslation(const Base::Vector3d& mov)
{
    Base::Matrix4D mat;
    mat.move(mov);
    setTransform(mat * getTransform());
}

void ComplexGeoData::applyRotation(const Base::Rotation& rot)
{
    Base::Matrix4D mat;
    rot.getValue(mat);
    setTransform(mat * getTransform());
}

void ComplexGeoData::setPlacement(const Base::Placement& rclPlacement)
{
    setTransform(rclPlacement.toMatrix());
}

Base::Placement ComplexGeoData::getPlacement() const
{
    Base::Matrix4D mat = getTransform();

    return {Base::Vector3d(mat[0][3], mat[1][3], mat[2][3]), Base::Rotation(mat)};
}

double ComplexGeoData::getAccuracy() const
{
    return 0.0;
}

void ComplexGeoData::getLinesFromSubElement(const Segment* segment,
                                            std::vector<Base::Vector3d>& Points,
                                            std::vector<Line>& lines) const
{
    (void)segment;
    (void)Points;
    (void)lines;
}

void ComplexGeoData::getFacesFromSubElement(const Segment* segment,
                                            std::vector<Base::Vector3d>& Points,
                                            std::vector<Base::Vector3d>& PointNormals,
                                            std::vector<Facet>& faces) const
{
    (void)segment;
    (void)Points;
    (void)PointNormals;
    (void)faces;
}

Base::Vector3d ComplexGeoData::getPointFromLineIntersection(const Base::Vector3f& base,
                                                            const Base::Vector3f& dir) const
{
    (void)base;
    (void)dir;
    return Base::Vector3d();
}

void ComplexGeoData::getPoints(std::vector<Base::Vector3d>& Points,
                               std::vector<Base::Vector3d>& Normals,
                               double Accuracy,
                               uint16_t flags) const
{
    (void)Points;
    (void)Normals;
    (void)Accuracy;
    (void)flags;
}

void ComplexGeoData::getLines(std::vector<Base::Vector3d>& Points,
                              std::vector<Line>& lines,
                              double Accuracy,
                              uint16_t flags) const
{
    (void)Points;
    (void)lines;
    (void)Accuracy;
    (void)flags;
}

void ComplexGeoData::getFaces(std::vector<Base::Vector3d>& Points,
                              std::vector<Facet>& faces,
                              double Accuracy,
                              uint16_t flags) const
{
    (void)Points;
    (void)faces;
    (void)Accuracy;
    (void)flags;
}

bool ComplexGeoData::getCenterOfGravity(Base::Vector3d& unused) const
{
    (void)unused;
    return false;
}

// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
