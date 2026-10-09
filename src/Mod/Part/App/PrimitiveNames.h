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

#ifndef PART_PRIMITIVENAMES_H
#define PART_PRIMITIVENAMES_H

#include <Mod/Part/PartGlobal.h>

#include "TopoShape.h"

namespace Part
{

/** The names of a primitive's elements, by what each is to the primitive
 *
 * docs/PrimitiveNames.md. A box, a cylinder and the rest are built by the
 * kernel with nothing said of which face is which, and were known by number
 * alone: `Face3`, of a cylinder as of a box, and another face once a cone's
 * radius is zero or a cylinder is cut open. Here each element is given a
 * mapped name that says what it is -- `Top`, `Lateral`, `Start` -- so that
 * a reference to it, and every name made from it by what is modelled after,
 * follows the element and not its place in a list.
 *
 * A face is named by its role, read from the finished shape in the frame it
 * is built in: by which way it faces and where it lies. An edge is named by
 * the faces it is between, `Front_Top`, and a vertex by the faces that meet
 * in it, `Front_Left_Top_Corner`. An edge of one face is its seam,
 * `Lateral_Seam`, or where the face closes to a point, at the pole of one
 * end: `Lateral_BottomPole`, and the point `BottomPole`. What is left with
 * the name of another -- the two halves an ellipsoid is split in, the two
 * seams of a torus -- is numbered from the second on, in the order of
 * height, then of the angle about the axis, then of the distance from it:
 * `Lateral`, `Lateral2`.
 *
 * The roles:
 *
 *  - a box and a wedge: `Left` `Right` (-X, +X), `Front` `Rear` (-Y, +Y),
 *    `Bottom` `Top` (-Z, +Z), as the views are called;
 *  - a cylinder, a cone, a ball and an ellipsoid: `Lateral`, `Bottom`,
 *    `Top`, and where it is not all the way round `Start` (at the angle
 *    nought) and `End`;
 *  - a prism: `Bottom`, `Top`, `Side1` on, the first from the corner on +X;
 *  - a torus: `Lateral`, `Start`, `End`, and where the tube is not whole
 *    `TubeStart` and `TubeEnd`;
 *  - a plane: the face `Plane`, its edges `Front` `Rear` `Left` `Right`,
 *    its vertices by the two edges, `Front_Left`;
 *  - a line, a circle, an ellipse: the edge by that name, the vertices
 *    `Start` and `End`;
 *  - a helix and a spiral: `Segment1` on, `Start`, `End`, `Joint1` on;
 *  - a polygon: `Side1` on and `Corner1` on; a vertex: `Point`.
 *
 * A face that fits none of its kind's roles is `Other`, which should not
 * happen and is not an error.
 */
class PartExport PrimitiveNames
{
public:
    enum class Kind
    {
        Box,
        Wedge,
        Cylinder,
        Prism,
        Cone,
        Sphere,
        Ellipsoid,
        Torus,
        Plane,
        Line,
        Circle,
        Ellipse,
        Helix,
        Spiral,
        RegularPolygon,
        Vertex
    };

    /// What has to be said of a torus: its tube is told from what closes it
    /// by where it is, not by which way it faces
    struct Tube
    {
        double radius {0.0};  ///< from the axis to the middle of the tube
        double from {-180.0};  ///< where the tube begins, in degrees
        double to {180.0};  ///< and ends
    };

    /** Name the elements of \a shape, a primitive as it is built
     *
     * In its own frame: before a placement, and before anything is made of
     * it. Every name the shape had is dropped.
     */
    static void apply(TopoShape& shape, Kind kind);
    /// A torus, with what is said of its tube
    static void apply(TopoShape& shape, Kind kind, const Tube& tube);

    /// The same of a shape just built
    static TopoShape named(const TopoDS_Shape& shape, Kind kind);
    static TopoShape named(const TopoDS_Shape& shape, Kind kind, const Tube& tube);
};

}  // namespace Part

#endif  // PART_PRIMITIVENAMES_H
