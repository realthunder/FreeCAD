/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef PART_MESHTWIN_H
#define PART_MESHTWIN_H

#include <vector>

#include <BRep_TEdge.hxx>
#include <BRep_TFace.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <TopoDS_Shape.hxx>

#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** A private copy of a shape, for a mesher on another thread.
 *
 * A mesher writes into the shape it is given: a triangulation into every
 * face, polygons into every edge's list of representations. A shape held by
 * a document has one thread, and everything else that reads it -- a save, a
 * copy, a boolean, a script, a selection -- reads those same lists
 * (docs/DocumentLoad.md sec 18.8). So a worker is never handed the
 * document's shape. It is handed a twin, and what it made there is put on
 * the original afterwards, by the thread the original belongs to:
 *
 *     MeshTwin twin(shape);                       // the shape's own thread
 *     BRepMesh_IncrementalMesh(twin.shape(), ask); // any ONE other thread
 *     twin.land();                                // the shape's own thread
 *
 * The twin has one new TShape for each of the original's, at every level,
 * so a solid the original holds at two places is one solid in the twin too
 * (BRepBuilderAPI_Copy makes two of it, and meshes it twice). Nothing of
 * the GEOMETRY is copied: a twin's face lies on the original's own surface
 * and its edges on the original's curves, which is what makes a mesh made
 * on the one valid on the other -- UV nodes mean the same thing on both.
 * Vertices are copied with their parameters: a boolean writes those.
 *
 * The mesh the original already holds is carried BY HANDLE, so the mesher
 * finds in the twin exactly what it would have found in the original, and
 * decides as it would have: a face whose mesh answers the ask is left
 * alone, an edge shared with a meshed face is discretized as that face
 * has it. That is what makes the result the one an in-place mesh gives.
 *
 * One thing of the original a worker does reach through a carried mesh: a
 * mesher that REPLACES a carried triangulation clears the "active" purpose
 * bit OCCT keeps inside the triangulation object itself, which the original
 * still holds as its active one. land() and abandon() put it back, so one of
 * the two is called for every twin that was meshed.
 *
 * A twin is built and landed on the thread its original belongs to. Every
 * read and every edit of the original's representations is made under the
 * OCCT fork's BRep_RepresentationLock, which does nothing for a shape that
 * is not Immutable: the day shape values are frozen, the landing is already
 * the edit of a frozen shape's caches that lock is for, and the copy may be
 * made on a worker.
 */
class PartExport MeshTwin
{
public:
    /// What a twin takes of the mesh its original holds.
    enum class Resident
    {
        /// All of it, by handle: the mesher decides as it would in place.
        Carry,
        /// None where there is geometry to mesh from: the mesher finds
        /// nothing and meshes everything. A face that is a triangulation
        /// and no surface keeps it, and an edge without a curve its polygon.
        Strip,
    };

    /// What a landing did.
    struct Landed
    {
        /// Faces that took the twin's mesh.
        int faces = 0;
        /// Faces somebody meshed between the copy and the landing: left as
        /// they are, the twin's work for them dropped.
        int overtaken = 0;
        /// Edge polygons put on the original, and taken off it because the
        /// triangulation they index was replaced.
        int polygonsAdded = 0;
        int polygonsRemoved = 0;
    };

    MeshTwin() = default;
    explicit MeshTwin(const TopoDS_Shape& original, Resident resident = Resident::Carry);

    bool isNull() const
    {
        return twin.IsNull();
    }

    /// The copy, with the original's location and orientation: what a
    /// mesher is handed.
    const TopoDS_Shape& shape() const
    {
        return twin;
    }

    /// The shape this is a twin of.
    const TopoDS_Shape& original() const
    {
        return source;
    }

    /// How many distinct faces and edges the twin holds.
    std::size_t faceCount() const
    {
        return faces.size();
    }
    std::size_t edgeCount() const
    {
        return edges.size();
    }

    /** Make the original's mesh what the twin's is.
     *
     * On the original's thread, once the twin's mesher has returned. A face
     * takes the twin's triangulations if it still holds what it held when
     * the twin was made; its edges take the polygons that index them and
     * lose those of a triangulation that went. What the twin's mesher left
     * alone is not touched.
     */
    Landed land() const;

    /** The twin's work is not wanted: leave the original as it is.
     *
     * On the original's thread. Puts back the one thing a mesher can have
     * changed through a carried mesh (see the class).
     */
    void abandon() const;

private:
    struct FacePair
    {
        occ::handle<BRep_TFace> original;
        occ::handle<BRep_TFace> twin;
        /// What the original held when the twin was made.
        occ::handle<Poly_Triangulation> activeAtCopy;
    };
    struct EdgePair
    {
        occ::handle<BRep_TEdge> original;
        occ::handle<BRep_TEdge> twin;
        /// The original's own polygon, for an edge that is on no face.
        occ::handle<Poly_Polygon3D> polygonAtCopy;
    };
    class Maker;

    TopoDS_Shape source;
    TopoDS_Shape twin;
    std::vector<FacePair> faces;
    std::vector<EdgePair> edges;
};

}  // namespace Part

#endif  // PART_MESHTWIN_H
