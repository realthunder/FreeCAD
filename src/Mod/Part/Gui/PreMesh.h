/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef PARTGUI_PREMESH_H
#define PARTGUI_PREMESH_H

#include <cstddef>
#include <vector>

#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

namespace PartGui
{

/// The load's parallel pre-mesh (docs/DocumentLoad.md sec 18).
///
/// A restored document's visual build is dominated by tessellation --
/// measured on a 17058-solid assembly, 22 of the 27s the whole build
/// takes -- and it runs one shape at a time on the GUI thread, because
/// that is where the display nodes are. OCCT's own parallelism does not
/// answer it: BRepMesh splits ONE shape over its faces, and a model made
/// of thousands of small parts gives it nothing to split, measured at
/// 2.04 cores of 28 over the build (turning it off costs 4.8s of 22, so
/// it helps -- it just cannot scale).
///
/// What does scale is meshing DIFFERENT shapes at once, which is what
/// this does: ahead of the drain that will display them, the parked
/// shapes are tessellated on worker threads, each shape whole and by
/// itself (InParallel off per job -- the split is across shapes now, and
/// nesting the two only oversubscribes). The drain's own BRepMesh call
/// then finds the mesh resident and skips (Render_MeshSkipRedundant).
///
/// Two things make that skip actually fire, and both are why this is
/// more than a thread pool:
///
///   * The ASK has to match. The display deflection derives from the
///     shape's bounding box, and BRepBndLib::Add prefers a resident
///     triangulation over the geometry -- enlarging the box by the
///     mesh's own deflection. So a pre-meshed shape measures BIGGER than
///     it did, the later build asks for something coarser than what is
///     resident, and the redundancy check refuses it (a finer resident
///     mesh is not adequate by default, and for a measured reason).
///     Every claim therefore carries the GEOMETRY box the pre-mesh
///     measured, and the build derives its ask from that box instead of
///     measuring again.
///   * A shape being meshed must not be touched. BRepMesh writes the
///     triangulation into the TShape; a GUI-thread build reading it
///     mid-write is a race whatever the mesh ends up being. So a claim
///     is IN FLIGHT until its worker is done, and updateVisual stays
///     parked on such a shape rather than building it.
struct PreMeshItem
{
    /// The shape exactly as the build will mesh it: location stripped,
    /// the same TShape the view provider's cachedShape holds.
    TopoDS_Shape shape;
    /// The bounding box measured BEFORE any triangulation existed --
    /// what the build's ask has to be derived from (see above).
    Bnd_Box geomBox;
    double deflection = 0.0;
    double angle = 0.0;
};

/// GUI thread: claim every item's TShape in flight and tessellate the
/// batch on worker threads. Returns at once; \a items is consumed.
void submitPreMesh(std::vector<PreMeshItem> &&items);

/// Is a pre-mesh of \a tshape still running? A build must leave the
/// shape alone while it is.
bool preMeshInFlight(const void *tshape);

/// Block until \a tshape's pre-mesh has published, at most \a seconds.
/// True once the shape is safe to read, false on the timeout -- where
/// the caller must go on treating the shape as in flight.
///
/// For the load that has no drain behind it (ProgressiveLoad off): its
/// builds run inside the restore, so there is nowhere to park one to,
/// and parking it anyway would turn a synchronous load into a
/// progressive one -- an open returning with the document still
/// arriving is the one thing that preference rules out. The GUI thread
/// has nothing else to do inside such a load, so it waits for the
/// worker and then builds as it always did.
bool waitPreMesh(const void *tshape, double seconds);

/// The geometry box the pre-mesh measured for \a tshape, if it claimed
/// it at all. False leaves \a box untouched.
bool preMeshBox(const void *tshape, Bnd_Box &box);

/// What the pre-mesh has done so far, for the line the drain reports
/// itself with. \a wall is the batch's own elapsed time (0 while one is
/// still running).
void preMeshStats(std::size_t &claimed, std::size_t &meshed,
                  std::size_t &failed, double &wall);

/// Forget every claim. A document closing invalidates the shapes the
/// claims are about; in-flight batches are left to finish (they own
/// their shapes) and their results are simply dropped.
void clearPreMeshClaims();

/// Whether the pre-mesh runs at all (Render_PreMeshOnLoad).
bool preMeshEnabled();

} // namespace PartGui

#endif // PARTGUI_PREMESH_H
