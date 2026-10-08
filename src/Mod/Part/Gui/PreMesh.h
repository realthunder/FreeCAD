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
#include <cstdint>
#include <vector>

#include <Bnd_Box.hxx>
#include <TopoDS_Shape.hxx>

#include <Mod/Part/PartGlobal.h>

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
/// A WORKER NEVER SEES THE DOCUMENT'S SHAPE (sec 18.9). It used to mesh
/// it in place, and everything else that reads a shape -- a save, a copy,
/// a boolean, a script, a click in the tree -- read the very lists it was
/// writing (sec 18.8). Now a request is queued and nothing more; when its
/// turn comes the GUI thread makes a private twin of the shape
/// (Part::MeshTwin), a thread of the compute pool meshes the twin, and the
/// result comes back to the GUI thread, which puts it on the shape. So:
///
///   * Every function here is the GUI thread's -- the thread the shapes
///     belong to. Each of them first takes what the workers have handed
///     back and hands out what can go, so the queue moves for as long as
///     anybody asks, event loop or no event loop (a load that builds
///     inside the restore turns none).
///   * A face is meshed once. A shape made of faces another request has
///     out waits for that one to come back, and its own twin then carries
///     the mesh those faces took: two objects holding one solid, an object
///     and the compound over it, are both requests, and the second finds
///     its work done.
///
/// Two rules of the in-place design remain, for other reasons than they
/// had:
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
///   * A build leaves a shape in flight alone. Nothing is being written
///     any more, so this is no longer what keeps it safe: it is what
///     keeps the GUI thread from meshing a shape a worker is about to
///     deliver. A claim is IN FLIGHT from the submit until its mesh is on
///     the shape, and updateVisual parks on such a shape, or waits for it.
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
    /// A shape whose resident mesh already answers the ask is given no
    /// worker (Render_MeshSkipRedundant): the build would not have meshed
    /// it either, and a worker handed it would decide by OCCT's rule where
    /// the build decides by meshAnswersAsk.
    bool skipResident = true;
    /// Render_MeshSkipFinerResident, for that check.
    bool acceptFiner = false;
};

/// Does the mesh \a shape holds already answer an ask for \a deflection?
/// The display build's own check (ViewProviderExt.cpp), without the
/// parameters it is switched by.
PartGuiExport bool meshAnswersAsk(const TopoDS_Shape &shape, double deflection,
                                  bool acceptFiner);

/// Claim every item in flight and queue it for the workers. Returns at
/// once; \a items is consumed. An item whose TShape is already in flight
/// is left to the claim that has it.
PartGuiExport void submitPreMesh(std::vector<PreMeshItem> &&items);

/// Is a pre-mesh of \a tshape still to come? A build leaves the shape to
/// it while it is.
///
/// This asks about a claim's own shape. A build asks the other one: a
/// shape that was never claimed can be made of the faces of one that is.
PartGuiExport bool preMeshInFlight(const void *tshape);

/// Is \a shape, or any face or edge it is made of, still to be meshed by
/// a claim? What a build asks: a compound over claimed shapes, a
/// boolean's result, a shell of their faces is made of faces a claim is
/// about to deliver without being claimed itself. Costs nothing when
/// nothing is in flight.
PartGuiExport bool preMeshInFlight(const TopoDS_Shape &shape);

/// Take the meshes as they come back until nothing \a shape is made of
/// is in flight, at most \a seconds; false on the timeout.
PartGuiExport bool waitPreMesh(const TopoDS_Shape &shape, double seconds);

/// Take the meshes as they come back until \a tshape's own is on it, at
/// most \a seconds. True once it is -- or once the claim is gone --
/// false on the timeout.
///
/// For the load that has no drain behind it (ProgressiveLoad off): its
/// builds run inside the restore, so there is nowhere to park one to,
/// and parking it anyway would turn a synchronous load into a
/// progressive one -- an open returning with the document still
/// arriving is the one thing that preference rules out. The GUI thread
/// has nothing else to do inside such a load, so it waits here, landing
/// what the workers hand back and handing out the next, and then builds
/// as it always did.
PartGuiExport bool waitPreMesh(const void *tshape, double seconds);

/// The geometry box the pre-mesh measured for \a tshape while its claim
/// is still IN FLIGHT. False -- no claim, or its mesh landed -- leaves
/// \a box untouched.
///
/// For the bounds question that must not build the visual
/// (ViewProviderPartExt::_getBoundingBox): the box is the one the build
/// will derive its ask from, and it is there without measuring anything.
PartGuiExport bool preMeshBox(const void *tshape, Bnd_Box &box);

/// What the pre-mesh has done so far, for the line the drain reports
/// itself with: claims made, meshes on their shapes (a shape that needed
/// none counts), claims that ended without one. \a wall runs from the
/// first submit to the last mesh taken.
PartGuiExport void preMeshStats(std::size_t &claimed, std::size_t &meshed,
                                std::size_t &failed, double &wall);

/// How many claims have ended so far -- meshed, failed, given up or
/// dropped -- after taking what has come back, as every question here
/// does. Only ever counts up. Nothing that answers as in flight stops
/// doing so without this moving, which is what a drain needs to know that
/// has asked about everything it has left and found all of it in flight:
/// until the number moves there is nothing to ask again.
PartGuiExport std::uint64_t preMeshEnded();

/// Forget the claims. One that has not been handed to a worker is
/// dropped: its callers know that no build is going to ask. One a worker
/// has STAYS, answering as in flight, until its mesh is taken, and is
/// counted for nobody.
PartGuiExport void clearPreMeshClaims();

/// Stop and wait until no worker is in the mesher: a worker finishes the
/// twin it is on and starts no other, the meshes that are ready are put
/// on their shapes, and every other claim ends unmeshed. Called when the
/// application is about to quit -- a process must not leave with workers
/// inside OCCT -- and usable again afterwards.
PartGuiExport void stopPreMesh();

/// Whether the pre-mesh runs at all (Render_PreMeshOnLoad).
bool preMeshEnabled();

} // namespace PartGui

#endif // PARTGUI_PREMESH_H
