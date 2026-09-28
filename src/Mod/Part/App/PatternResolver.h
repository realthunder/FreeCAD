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

#ifndef PART_PATTERNRESOLVER_H
#define PART_PATTERNRESOLVER_H

#include <App/Pattern.h>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** Resolves the references of App::Pattern that are shapes
 *
 * A straight edge or a planar face as a direction, a straight or circular edge
 * as an axis, the axes of a sketch, edges or the first wire of a shape as a
 * path, the vertices of a shape as points. A reference goes through
 * Part::Feature::getTopoShape(), so it may be any object with a shape, reached
 * through links and groups.
 */
class PartExport PatternResolver: public App::Pattern::Resolver
{
public:
    bool getDirection(App::DocumentObject* obj,
                      const std::string& sub,
                      Base::Vector3d& dir) const override;
    bool getAxis(App::DocumentObject* obj,
                 const std::string& sub,
                 App::Pattern::Axis& axis) const override;
    std::unique_ptr<App::Pattern::Path> getPath(App::DocumentObject* obj,
                                                const std::vector<std::string>& subs) const override;
    bool getPoints(App::DocumentObject* obj,
                   const std::vector<std::string>& subs,
                   std::vector<Base::Vector3d>& points) const override;

    /// Add the resolver to App::Pattern, once
    static void init();
};

}  // namespace Part

#endif  // PART_PATTERNRESOLVER_H
