/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <juergen.riegel@web.de>             *
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

#include "PreCompiled.h"

#if defined(__linux__)
# include <execinfo.h>
#endif

#ifndef _PreComp_
# include <typeinfo>

# include <Bnd_Box.hxx>
# include <BRep_Tool.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepMesh_Deflection.hxx>
# include <BRepMesh_IncrementalMesh.hxx>
# include <BRepMesh_ShapeTool.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <BRepGProp.hxx>
# include <GProp_GProps.hxx>
# include <Geom_Line.hxx>
# include <Geom_Plane.hxx>
# include <Geom_TrimmedCurve.hxx>
# include <gp_Cone.hxx>
# include <gp_Cylinder.hxx>
# include <gp_Pln.hxx>
# include <gp_Trsf.hxx>
# include <Precision.hxx>
# include <Poly_Array1OfTriangle.hxx>
# include <Poly_Polygon3D.hxx>
# include <Poly_PolygonOnTriangulation.hxx>
# include <Poly_Triangulation.hxx>
# include <Poly_TriangulationParameters.hxx>
# include <Standard_Version.hxx>
# include <TColgp_Array1OfDir.hxx>
# include <TColgp_Array1OfPnt.hxx>
# include <TColStd_Array1OfInteger.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Shape.hxx>
# include <TopoDS_Iterator.hxx>
# include <TopoDS_Vertex.hxx>
# include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <TopTools_MapOfShape.hxx>

# include <QApplication>
# include <QAction>
# include <QCoreApplication>
# include <QTimer>
# include <QMenu>
# include <deque>
# include <map>
# include <optional>
# include <chrono>
# include <sstream>

# include <Inventor/SoPickedPoint.h>
# include <Inventor/actions/SoSearchAction.h>
# include <Inventor/misc/SoChildList.h>
# include <Inventor/details/SoFaceDetail.h>
# include <Inventor/details/SoLineDetail.h>
# include <Inventor/details/SoPointDetail.h>
# include <Inventor/errors/SoDebugError.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoMaterialBinding.h>
# include <Inventor/nodes/SoMatrixTransform.h>
# include <Inventor/nodes/SoNormal.h>
# include <Inventor/nodes/SoNormalBinding.h>
# include <Inventor/nodes/SoPolygonOffset.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShapeHints.h>
# include <Inventor/nodes/SoTextureCoordinate2.h>
# include <QAction>
# include <QMenu>

# include <boost/algorithm/string/predicate.hpp>
#endif

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/MappedElement.h>
#include <map>
#include <Base/Console.h>
#include <Base/Sequencer.h>
#include <Base/Parameter.h>
#include <Base/ProgramVersion.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Action.h>
#include <Gui/Selection.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/Utilities.h>
#include <Gui/ViewProviderLink.h>
#include <Gui/TaskElementColors.h>
#include <Gui/Inventor/SoFCRenderMaterial.h>
#include <Gui/Inventor/SoFCShapeInfo.h>
#include <Gui/Inventor/SoFCVertexCache.h>
#include <Gui/InventorBase.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Control.h>
#include <Gui/SoFCSelectionAction.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/ViewParams.h>
#include <Gui/RenderParams.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Gui/Renderer/Renderer.h>
#include <Gui/Renderer/MeshSimplify.h>
#include <Gui/Renderer/MeshSource.h>
#include <Mod/Part/App/Tools.h>

#include "ViewProviderExt.h"
#include "MeshLevelSource.h"
#include "PartParams.h"
#include "SoBrepEdgeSet.h"
#include "SoBrepFaceSet.h"
#include "SoBrepPointSet.h"
#include "TaskFaceColors.h"


#include "ViewProviderPartExtPy.h"

FC_LOG_LEVEL_INIT("Part", true, true)

using namespace PartGui;
namespace bio = boost::iostreams;

PROPERTY_SOURCE(PartGui::ViewProviderPartExt, Gui::ViewProviderGeometryObject)

namespace PartGui {

namespace {
bool levelDebugOn();
}

// Private class used by ViewProviderExt to update its visual nodes up on
// receiving SoGetBoundingBoxAction
class SoFCCoordinate3: public SoCoordinate3
{
public:
    virtual void getBoundingBox(SoGetBoundingBoxAction * action) {
        // A visual parked for the progressive-load queue must stay parked:
        // the first repaint after a load traverses the whole scene, and
        // building on demand here would hand back the very stall the queue
        // exists to break up. It contributes nothing until its slice comes.
        if (vp && vp->VisualTouched && !vp->VisualDeferred) {
            // Named under the level debug flag: this on-demand build
            // runs inside whatever traversal asked for the bbox, and
            // the 1.8s giant rebuilds attributed to "the drain" turned
            // out not to be drain builds at all -- whether THIS is the
            // caller is exactly what the line answers.
            if (levelDebugOn())
                Base::Console().Message(
                    "on-demand visual build (bbox) for %s\n",
                    vp->getFullName().c_str());
            vp->updateVisual();
        }
        SoCoordinate3::getBoundingBox(action);
    }

    ViewProviderPartExt *vp = nullptr;
};

void initShapeInstancingGateObserver();  // PartParams.cpp

namespace {

/// Whether the level plan is narrating; the diagnostics below cost a
/// walk of every face and are silent without it.
bool levelDebugOn()
{
    static const bool env = std::getenv("FC_LEVEL_DEBUG") != nullptr;
    return env || Gui::RenderParams::getLevelDebug();
}

/// Why the check below said no, so that a check which refuses too much
/// can be improved by evidence instead of by argument. A refusal is
/// cheap and safe; a refusal for a reason nobody measured is how the
/// saving stays on the table.
enum class MeshRefusal {
    None = 0,       ///< redundant: the call would write nothing
    NoFaces,        ///< edges and vertices only -- the call builds those
    NoTriangulation,///< a face with no mesh at all: the first tessellation
    TooCoarse,      ///< resident mesh coarser than the ask
    TooFine,        ///< resident finer: the descent wants that memory back
    BadIndices,     ///< #25080: a triangulation OCCT would discard
    FreeEdge,       ///< a free edge whose 3D polygon is missing or stale
    Count
};

struct MeshVerdict {
    MeshRefusal why = MeshRefusal::None;
    /// The deflection pair that decided a TooCoarse/TooFine refusal, so
    /// the report can say by how much and in which direction.
    double current = 0.0, required = 0.0;
    bool redundant() const { return why == MeshRefusal::None; }
};

/// Would the tessellation call about to be made write anything at all?
///
/// Measured (#13d), half of a mass descent's tessellation calls changed
/// no triangle and still cost ~19ms each -- 27% of the descent's whole
/// GUI-thread rebuild time -- because BRepMesh_IncrementalMesh cannot
/// conclude "already adequate" without first building its internal model
/// of the shape: every face, every wire, every edge, discretized.
///
/// This asks the same question off what the faces already carry, and the
/// answer is only worth trusting because it is OCCT's OWN question,
/// copied rather than invented:
///
///   * per face, BRepMesh_ModelPreProcessor's TriangulationConsistency --
///     the deflection the triangulation was BUILT at (its parameters,
///     which is the ask; its own Deflection() is an estimate of the
///     result and can land either side of it) against the deflection the
///     model would have computed for that face, through the same
///     BRepMesh_Deflection::IsConsistent with the same AllowQualityDecrease
///     the call itself passes;
///   * the same #25080 guard on triangle indices, because a triangulation
///     OCCT would have discarded as corrupt is one this fill would read;
///   * every FREE edge's 3D polygon, by BRepMesh_EdgeDiscret's rule. Free
///     edges are the one thing OCCT still rewrites when every face is
///     reused: face-bound polygons are committed only for faces it
///     re-meshed (BRepMesh_ModelPostProcessor), so with every face
///     consistent the call writes nothing and skipping it is not a
///     substitution, it is the same outcome without the model build.
///
/// Deliberately ALL-OR-NOTHING per shape, where OCCT decides per face:
/// one inconsistent face and the real call runs, which then does exactly
/// what it does today (mesh that face, reuse the rest). There is no arm
/// in which guessing beats asking, because the fallback IS the answer.
///
/// The face deflection is the model's own formula less its vertex term:
/// max(ask, 2 * max face tolerance), where the model takes the wire
/// average of max(ask, vertex-to-curve gap) -- always >= the ask, and
/// equal to it unless the shape's vertices sit off their curves. So this
/// can only UNDER-state what the model would require, and understating
/// it errs the safe way: a mesh finer than the ask stays, one coarser
/// than the ask is never accepted.
///
/// /!\ A resident mesh FINER than the ask is NOT adequate by default,
/// and that is the whole point of AllowQualityDecrease being on: the
/// descent asks coarse on purpose to hand memory back. `acceptFiner`
/// (Render_MeshSkipFinerResident) drops that half of the rule, because
/// measurement says it is where nearly all the refused saving sits --
/// see the parameter's own doc for the numbers and the risk.
///
/// /!\ Where this check and OCCT can disagree, they disagree in ONE
/// direction only, and that is what makes the approximation safe: the
/// required deflection computed here is a LOWER bound on the model's,
/// so the upper test (`current < 1.1 * required`) is strictly tighter
/// than OCCT's and a mesh too COARSE for the ask can never be accepted.
/// A disagreement is always OCCT wanting to coarsen a mesh this kept --
/// measured at 1 call in 7403 -- which costs memory, never fidelity.
MeshVerdict tessellationIsRedundant(const TopoDS_Shape &shape, double deflection,
                                    bool acceptFiner)
{
#if OCC_VERSION_HEX >= 0x070500
    // Mirrors the parameters the live call passes below; the pre-7.5
    // constructor has no such flag and its rule is "no coarser than
    // asked" alone -- which is exactly what accepting a finer resident
    // mesh asks for, so the two spell the same thing here.
    const bool allowDecrease = !acceptFiner;
#else
    (void)acceptFiner;
    const bool allowDecrease = false;
#endif
    MeshVerdict verdict;

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
    // A shape with no faces is edges and vertices, whose tessellation is
    // the thing the call would build. Nothing to be redundant about.
    if (faceMap.IsEmpty()) {
        verdict.why = MeshRefusal::NoFaces;
        return verdict;
    }

    TopTools_MapOfShape facedEdges;
    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const TopoDS_Face &face = TopoDS::Face(faceMap(i));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) {
            verdict.why = MeshRefusal::NoTriangulation;
            return verdict;
        }

#if OCC_VERSION_HEX >= 0x070600
        const Handle(Poly_TriangulationParameters) &built = tri->Parameters();
        const double current = (!built.IsNull() && built->HasDeflection())
            ? built->Deflection() : tri->Deflection();
#else
        const double current = tri->Deflection();
#endif
        const double required = std::max(deflection,
                2.0 * BRepMesh_ShapeTool::MaxFaceTolerance(face));
        if (!BRepMesh_Deflection::IsConsistent(current, required, allowDecrease)) {
            verdict.why = current >= required ? MeshRefusal::TooCoarse
                                              : MeshRefusal::TooFine;
            verdict.current = current;
            verdict.required = required;
            return verdict;
        }

        const int nbNodes = tri->NbNodes();
#if OCC_VERSION_HEX < 0x070600
        const Poly_Array1OfTriangle &triangles = tri->Triangles();
#endif
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            Standard_Integer n1 = 0, n2 = 0, n3 = 0;
#if OCC_VERSION_HEX < 0x070600
            triangles(t).Get(n1, n2, n3);
#else
            tri->Triangle(t).Get(n1, n2, n3);
#endif
            if (n1 < 1 || n1 > nbNodes || n2 < 1 || n2 > nbNodes
                    || n3 < 1 || n3 > nbNodes) {
                verdict.why = MeshRefusal::BadIndices;
                return verdict;
            }
        }

        for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next())
            facedEdges.Add(ex.Current());
    }

    for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        if (facedEdges.Contains(ex.Current()))
            continue;
        TopLoc_Location loc;
        Handle(Poly_Polygon3D) poly =
            BRep_Tool::Polygon3D(TopoDS::Edge(ex.Current()), loc);
        if (poly.IsNull() || !poly->HasParameters()
                || !BRepMesh_Deflection::IsConsistent(poly->Deflection(),
                                                      deflection, allowDecrease)) {
            verdict.why = MeshRefusal::FreeEdge;
            return verdict;
        }
    }
    return verdict;
}

/// Does the tessellation call in a rebuild actually TESSELLATE?
///
/// The comment at that call has always claimed a resident mesh makes it
/// nearly free, and the phase split (#13d) measured it at 71% of a mass
/// descent's GUI-thread time on shapes the refine pool had already
/// meshed. One of those is wrong, and the difference decides the whole
/// workstream: a call that rebuilds is a defect in what the descent
/// hands over, while a call that merely validates expensively is an
/// OCCT cost to be avoided rather than moved.
///
/// So this asks the shape itself, before and after: how many triangles
/// it holds, and at what deflection. A count that moves is a rebuild.
struct MeshCallProbe {
    struct Stats {
        std::size_t calls = 0, rebuilt = 0;
        double timeRebuilt = 0, timeValidated = 0;
        /// Last rebuild's evidence, so the report can say WHY rather
        /// than only how often: what was asked, and the coarsest and
        /// finest deflection standing when it was.
        double lastAsked = 0, lastResidentMin = 0, lastResidentMax = 0;
        std::size_t noTriangulation = 0;
        /// The redundancy check against the call it stands in for. Only
        /// `skipped` counts calls not made; the rest are calls that WERE
        /// made, and are what says whether skipping them would have been
        /// safe:
        ///   agreed  -- check said redundant, the call indeed rebuilt
        ///              nothing. The saving on offer.
        ///   wrong   -- check said redundant and the call REBUILT. Must
        ///              be zero; anything else is a mesh that would have
        ///              been silently wrong, and the feature is unsafe
        ///              until it is explained.
        ///   missed  -- check said no, the call rebuilt nothing anyway.
        ///              The saving left behind by being conservative.
        std::size_t checks = 0, skipped = 0, agreed = 0, wrong = 0, missed = 0;
        double timeChecking = 0;
        /// Why the check refused, counted only over the calls that then
        /// turned out to be REDUNDANT -- a refusal on a call that really
        /// did rebuild is the check working, and mixing the two would
        /// bury the interesting histogram under the ordinary one.
        std::size_t refused[std::size_t(MeshRefusal::Count)] = {};
        /// The widest deflection disagreement seen, by ratio, on such a
        /// call. One sample, but it says the direction and the size.
        double worstRatio = 0, worstCurrent = 0, worstRequired = 0;
        /// Of those refusals, how many were on an object whose
        /// tessellation is already SPENT -- one whose descent has
        /// stopped re-tessellating, and whose ask therefore keeps
        /// doubling away from a mesh that will never move again
        /// (ViewProviderExt onScaleDown, "re-tessellating buys
        /// nothing"). If these track `too fine`, the refusal is not a
        /// deflection question at all: it is a call nobody should be
        /// making. Counted PER CLAIM, because only one of the two is a
        /// proof: a Proved object's call is free to skip (a coarser
        /// mesh was measured not to exist), while a BoxChosen object
        /// may still coarsen, so skipping there pins memory exactly
        /// the way accept-finer did (753/3381). The split is what
        /// decides how much of the refusal mass a safe skip can claim.
        std::size_t refusedSpentProved = 0;
        std::size_t refusedSpentBox = 0;
        /// The other half of the same audit: calls on a spent object
        /// that REBUILT anyway. For a skip keyed on the claim rather
        /// than on the redundancy check, every one of these is a
        /// rebuild the skip would have wrongly suppressed -- the WRONG
        /// column of that skip, and the number that decides whether
        /// `Proved` is actually a proof about future calls or only
        /// about the one that established it.
        /// MEASURED (rack model audits, 2026-08-13): 395 and 426
        /// proved rebuilds against 1548 and 1689 claims over two runs
        /// -- the flag-only skip is refuted at ~20% WRONG.
        std::size_t rebuiltSpentProved = 0;
        std::size_t rebuiltSpentBox = 0;
        /// The same two cells restricted to calls the check had
        /// refused ONLY because the resident mesh is finer than the
        /// ask (TooFine). This is the audit of the COMBINED rule --
        /// skip when the check's one unprovable case coincides with
        /// the descent's proof -- which survives residency changes
        /// the flag alone cannot see: a call whose verdict was
        /// NoTriangulation or TooCoarse (a demote dropped the rung)
        /// is still made under the combined rule, so its rebuild is
        /// not wrongly suppressed and must not be counted against it.
        /// MEASURED (rack model audit, 2026-08-13): 422 of the 426
        /// proved rebuilds WERE finer-only -- the combined rule is
        /// refuted by the same 20% that killed the flag-only skip.
        /// The proof is sound at the step that established it and
        /// LEAKY as a permanent claim: as the ask keeps doubling, one
        /// proved shape in five resumes shrinking at some coarser
        /// deflection, and those rebuilds are real memory the skip
        /// would forgo. No spent-keyed skip ships; these counters
        /// stay as the guard that keeps that conclusion measured.
        std::size_t finerRebuiltProved = 0;
        std::size_t finerRebuiltBox = 0;
        /// The deflection-invariance rule (Render_MeshSkipInvariant),
        /// scored the same two-sided way as everything above:
        /// `invariantSkipped` counts only when the skip is ON and is
        /// therefore vacuous as evidence; right/wrong come from the
        /// audit arm (skip OFF, the claimed call made anyway and the
        /// probe's rebuilt verdict compared). WRONG here would mean a
        /// shape of planes and lines whose mesh moved with the
        /// deflection -- a refutation of the geometry argument itself,
        /// so it is the line to watch.
        std::size_t invariantSkipped = 0;
        std::size_t invariantRight = 0;
        std::size_t invariantWrong = 0;
        /// The landing rule (Render_MeshSkipLanded), scored the same
        /// two-sided way: skipped only counts with the skip ON;
        /// right/wrong come from the audit arm (skip OFF, the claimed
        /// call made anyway). WRONG would mean a rebuild whose caller
        /// had just installed the triangulation and BRepMesh still
        /// changed it -- a refutation of the landing argument itself.
        std::size_t landedSkipped = 0;
        std::size_t landedRight = 0;
        std::size_t landedWrong = 0;
    };
    static Stats &stats()
    {
        static Stats s;
        return s;
    }

    const TopoDS_Shape &shape;
    double asked;
    bool active;
    /// What the redundancy check said about the call being made anyway,
    /// so this can score the check against the only authority there is.
    MeshVerdict verdict;
    /// Whether -- and on whose authority -- the object's descent had
    /// already stopped re-tessellating (proof vs box choice).
    ViewProviderPartExt::ScaleSpent spent = ViewProviderPartExt::ScaleSpent::No;
    /// The deflection-invariance rule claimed this call and it is being
    /// made anyway (the audit arm): score the claim in the destructor.
    bool invariantClaim = false;
    /// Same audit arm for the landing rule (Render_MeshSkipLanded).
    bool landedClaim = false;
    int trisBefore = 0, facesBefore = 0, facesTotal = 0;
    double residentMin = 0.0, residentMax = 0.0;
    std::chrono::high_resolution_clock::time_point start;

    static void sample(const TopoDS_Shape &shape, int &tris, int &faces,
                       int &total, double *dmin, double *dmax)
    {
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
        total = faceMap.Extent();
        for (int i = 1; i <= faceMap.Extent(); ++i) {
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri =
                BRep_Tool::Triangulation(TopoDS::Face(faceMap(i)), loc);
            if (tri.IsNull())
                continue;
            ++faces;
            tris += tri->NbTriangles();
            if (!dmin)
                continue;
            const double d = tri->Deflection();
            *dmin = *dmin == 0.0 ? d : std::min(*dmin, d);
            *dmax = std::max(*dmax, d);
        }
    }

    MeshCallProbe(const TopoDS_Shape &s, double deflection,
                  const MeshVerdict &v,
                  ViewProviderPartExt::ScaleSpent tessellationSpent,
                  bool invariantClaimed = false,
                  bool landedClaimed = false)
        : shape(s), asked(deflection), active(levelDebugOn()), verdict(v),
          spent(tessellationSpent), invariantClaim(invariantClaimed),
          landedClaim(landedClaimed)
    {
        if (!active)
            return;
        sample(shape, trisBefore, facesBefore, facesTotal,
               &residentMin, &residentMax);
        start = std::chrono::high_resolution_clock::now();
    }

    ~MeshCallProbe()
    {
        if (!active)
            return;
        const double elapsed = std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - start).count();
        int trisAfter = 0, facesAfter = 0, total = 0;
        sample(shape, trisAfter, facesAfter, total, nullptr, nullptr);
        Stats &st = stats();
        ++st.calls;
        if (!facesBefore && facesTotal)
            ++st.noTriangulation;
        const bool rebuilt =
            trisAfter != trisBefore || facesAfter != facesBefore;
        if (invariantClaim)
            ++(rebuilt ? st.invariantWrong : st.invariantRight);
        if (landedClaim) {
            ++(rebuilt ? st.landedWrong : st.landedRight);
            // Every WRONG claim in full: the aggregate says the rule
            // leaks, only the per-call evidence says WHERE, and the
            // decision between "fix the claim" and "drop the rule"
            // hangs on the pattern.
            if (rebuilt)
                Base::Console().Message(
                    "landed claim WRONG: why %d asked %.6f resident "
                    "%.6f..%.6f tris %d->%d faces %d->%d of %d\n",
                    int(verdict.why), asked, residentMin, residentMax,
                    trisBefore, trisAfter, facesBefore, facesAfter,
                    facesTotal);
        }
        if (rebuilt) {
            ++st.rebuilt;
            st.timeRebuilt += elapsed;
            st.lastAsked = asked;
            st.lastResidentMin = residentMin;
            st.lastResidentMax = residentMax;
            if (spent == ViewProviderPartExt::ScaleSpent::Proved) {
                ++st.rebuiltSpentProved;
                if (verdict.why == MeshRefusal::TooFine)
                    ++st.finerRebuiltProved;
            }
            else if (spent == ViewProviderPartExt::ScaleSpent::BoxChosen) {
                ++st.rebuiltSpentBox;
                if (verdict.why == MeshRefusal::TooFine)
                    ++st.finerRebuiltBox;
            }
        }
        else
            st.timeValidated += elapsed;
        // The check scored against the call it wanted to replace.
        if (verdict.redundant())
            ++(rebuilt ? st.wrong : st.agreed);
        else if (!rebuilt) {
            // Refused, and the call proved it could have been skipped.
            // This is the histogram worth having: it says which reason
            // is costing the saving, rather than that some reason is.
            ++st.missed;
            ++st.refused[std::size_t(verdict.why)];
            if (spent == ViewProviderPartExt::ScaleSpent::Proved)
                ++st.refusedSpentProved;
            else if (spent == ViewProviderPartExt::ScaleSpent::BoxChosen)
                ++st.refusedSpentBox;
            if (verdict.required > 0.0) {
                const double ratio = verdict.current / verdict.required;
                const double off = ratio > 1.0 ? ratio : 1.0 / std::max(ratio, 1e-9);
                if (off > st.worstRatio) {
                    st.worstRatio = off;
                    st.worstCurrent = verdict.current;
                    st.worstRequired = verdict.required;
                }
            }
        }
    }
};

/// Where a visual rebuild's time goes, reported only once it has become
/// worth reporting (docs/SceneStreaming.md #13d).
///
/// A single build costs microseconds and a line per build would bury the
/// run; a MASS DESCENT rebuilds thousands of objects and stalls the GUI
/// thread for over a second, which is the thing being complained about.
/// So this is a self-selecting reporter: it accumulates silently and
/// speaks only when the cost since the last line has passed a threshold,
/// which on a quiet session is never.
///
/// Every term is a DELTA since the last report, and the unattributed
/// remainder is printed rather than left to be inferred -- a split whose
/// parts do not add up to the whole is how a missing cost stays missing.
struct VisualSplitReporter {
    /// What was already reported, so each line describes its own window.
    struct Mark {
        double build = 0, mesh = 0, fill = 0, prologue = 0, instance = 0;
        double highlight = 0;
        std::size_t count = 0;
    };
    static Mark &mark()
    {
        static Mark m;
        return m;
    }
    /// Accumulated GUI-thread rebuild cost a line is worth, in seconds.
    /// Well above any single build and well below the 1.6s frame this
    /// exists to explain.
    static constexpr double kReportThreshold = 0.2;

    ~VisualSplitReporter()
    {
        using VP = Gui::ViewProvider;
        if (!levelDebugOn())
            return;
        Mark &m = mark();
        const double build = VP::VisualBuildTime.count() - m.build;
        if (build < kReportThreshold)
            return;
        const double mesh = VP::VisualMeshTime.count() - m.mesh;
        const double fill = VP::VisualFillTime.count() - m.fill;
        const double prologue = VP::VisualPrologueTime.count() - m.prologue;
        const double instance = VP::VisualInstanceTime.count() - m.instance;
        const double highlight = VP::VisualHighlightTime.count() - m.highlight;
        const std::size_t count = VP::VisualBuildCount - m.count;
        m = Mark{VP::VisualBuildTime.count(), VP::VisualMeshTime.count(),
                 VP::VisualFillTime.count(), VP::VisualPrologueTime.count(),
                 VP::VisualInstanceTime.count(),
                 VP::VisualHighlightTime.count(), VP::VisualBuildCount};
        // The traversal is what a worker thread could take; the mesh is
        // already on the refine pool; the prologue, the instancing and
        // the highlight epilogue are GUI-thread work that threading
        // would not touch at all.
        const double traversal = fill - mesh;
        const double rest =
            build - traversal - mesh - prologue - instance - highlight;
        Base::Console().Message(
            "visual build: %zu builds in %.3fs = traversal %.3fs (%.0f%%) + "
            "mesh %.3fs (%.0f%%) + prologue %.3fs (%.0f%%) + instancing "
            "%.3fs (%.0f%%) + highlight %.3fs (%.0f%%) + unattributed "
            "%.3fs (%.0f%%)\n",
            count, build,
            traversal, 100.0 * traversal / build,
            mesh, 100.0 * mesh / build,
            prologue, 100.0 * prologue / build,
            instance, 100.0 * instance / build,
            highlight, 100.0 * highlight / build,
            rest, 100.0 * rest / build);
        // What that mesh term actually was. Reported beside the split
        // rather than separately: the question "is the descent paying
        // to re-tessellate what the pool already built" is only ever
        // asked about this number.
        MeshCallProbe::Stats &ms = MeshCallProbe::stats();
        if (ms.calls) {
            Base::Console().Message(
                "visual build: mesh calls %zu, REBUILT %zu (%.3fs) vs "
                "validated only %zu (%.3fs); %zu had no triangulation at "
                "all; last rebuild asked %.6f, resident %.6f..%.6f\n",
                ms.calls, ms.rebuilt, ms.timeRebuilt,
                ms.calls - ms.rebuilt, ms.timeValidated,
                ms.noTriangulation, ms.lastAsked, ms.lastResidentMin,
                ms.lastResidentMax);
        }
        // The redundancy check, scored. WRONG is the number that decides
        // whether skipping is allowed at all, so it is printed even when
        // it is zero -- an absent line reads as "not measured", which is
        // the one thing it must never be confused with.
        if (ms.checks) {
            Base::Console().Message(
                "visual build: redundancy check %zu in %.3fs, SKIPPED %zu; "
                "of the %zu calls still made it got %zu right, %zu WRONG, "
                "and left %zu redundant calls unclaimed\n",
                ms.checks, ms.timeChecking, ms.skipped,
                ms.calls, ms.agreed, ms.wrong, ms.missed);
        }
        // Why those unclaimed calls were refused. A check that is safe
        // but claims little is improved here or not at all.
        if (ms.missed) {
            Base::Console().Message(
                "visual build: unclaimed by reason -- no faces %zu, no "
                "triangulation %zu, too coarse %zu, too fine %zu, bad "
                "indices %zu, free edge %zu; on SPENT objects %zu by "
                "proof + %zu by box choice, and spent calls that REBUILT "
                "anyway: %zu proved (%zu with only a finer resident) + "
                "%zu box (%zu finer); widest deflection "
                "miss %.6f vs %.6f required (x%.2f)\n",
                ms.refused[std::size_t(MeshRefusal::NoFaces)],
                ms.refused[std::size_t(MeshRefusal::NoTriangulation)],
                ms.refused[std::size_t(MeshRefusal::TooCoarse)],
                ms.refused[std::size_t(MeshRefusal::TooFine)],
                ms.refused[std::size_t(MeshRefusal::BadIndices)],
                ms.refused[std::size_t(MeshRefusal::FreeEdge)],
                ms.refusedSpentProved, ms.refusedSpentBox,
                ms.rebuiltSpentProved, ms.finerRebuiltProved,
                ms.rebuiltSpentBox, ms.finerRebuiltBox,
                ms.worstCurrent, ms.worstRequired, ms.worstRatio);
        }
        // The invariance rule, both arms on one line. WRONG is
        // meaningful only from the audit arm (skip off); skipped is
        // only ever nonzero with the skip on.
        if (ms.invariantSkipped || ms.invariantRight
            || ms.invariantWrong) {
            Base::Console().Message(
                "visual build: invariant rule -- skipped %zu; audit "
                "right %zu, WRONG %zu\n",
                ms.invariantSkipped, ms.invariantRight, ms.invariantWrong);
        }
        // The landing rule, same two arms (Render_MeshSkipLanded).
        if (ms.landedSkipped || ms.landedRight || ms.landedWrong) {
            Base::Console().Message(
                "visual build: landed rule -- skipped %zu; audit "
                "right %zu, WRONG %zu\n",
                ms.landedSkipped, ms.landedRight, ms.landedWrong);
        }
        if (ms.calls || ms.checks)
            ms = MeshCallProbe::Stats();
    }
};

/// The single-build counterpart of VisualSplitReporter (#13d): one line
/// naming the object, for any rebuild whose OWN cost passes
/// Render_LevelSlowBuildMS. The aggregate split says where a mass
/// descent's time goes; the landing pump's worst turn is one object's
/// whole rebuild, and only a per-build line says what THAT object spent
/// it on -- which is what decides how its rebuild gets split.
struct SlowBuildProbe {
    const Gui::ViewProviderDocumentObject *vp;
    std::chrono::high_resolution_clock::time_point start;
    double mesh, fill, prologue, instance, highlight;
    /// Which ladder state decided this build's deflection, filled by
    /// updateVisual once it has decided (empty on the paths that never
    /// get there). The 2s builds were exact-deflection re-tessellations
    /// of shapes holding no mesh at all, and WHY the build took the
    /// exact branch is precisely what the time split cannot say.
    std::string note;
    explicit SlowBuildProbe(const Gui::ViewProviderDocumentObject *vp)
        : vp(vp)
        , start(std::chrono::high_resolution_clock::now())
        , mesh(Gui::ViewProvider::VisualMeshTime.count())
        , fill(Gui::ViewProvider::VisualFillTime.count())
        , prologue(Gui::ViewProvider::VisualPrologueTime.count())
        , instance(Gui::ViewProvider::VisualInstanceTime.count())
        , highlight(Gui::ViewProvider::VisualHighlightTime.count())
    {}
    ~SlowBuildProbe()
    {
        if (!levelDebugOn())
            return;
        const long thresholdMS = Gui::RenderParams::getLevelSlowBuildMS();
        if (thresholdMS <= 0)
            return;
        const double total = std::chrono::duration<double>(
            std::chrono::high_resolution_clock::now() - start).count();
        if (total * 1000.0 < double(thresholdMS))
            return;
        using VP = Gui::ViewProvider;
        const double m = VP::VisualMeshTime.count() - mesh;
        const double f = VP::VisualFillTime.count() - fill;
        const double p = VP::VisualPrologueTime.count() - prologue;
        const double i = VP::VisualInstanceTime.count() - instance;
        const double h = VP::VisualHighlightTime.count() - highlight;
        Base::Console().Message(
            "slow visual build: %s %.3fs = traversal %.3f + mesh %.3f + "
            "prologue %.3f + instancing %.3f + highlight %.3f + "
            "unattributed %.3f%s%s\n",
            vp->getFullName().c_str(), total, f - m, m, p, i, h,
            total - (f - m) - m - p - i - h,
            note.empty() ? "" : " | ", note.c_str());
#if defined(__linux__)
        // The callers, for a slow build no instrumented context owns
        // (neither the drain nor the pump per the note): every queued
        // path dispatches as the same meta-call to the application
        // object, and only the stack says which code queued THIS one.
        // Mangled names and library offsets are enough to name the
        // family; this fires rarely (slow builds only) and only under
        // the level debug flag that gates the whole probe.
        if (note.find("drain 0 pump 0") != std::string::npos) {
            void *frames[24];
            const int n = ::backtrace(frames, 24);
            char **symbols = ::backtrace_symbols(frames, n);
            if (symbols) {
                for (int fi = 0; fi < n; ++fi)
                    Base::Console().Message("  slow build frame %d: %s\n",
                                            fi, symbols[fi]);
                free(symbols);
            }
        }
#endif
    }
};

/// The visual builds a document restore asked for and did not get. Held by
/// weak handle: a slice may run long after the load, and the document may
/// have been closed by then.
struct DeferredVisualQueue {
    std::deque<App::DocumentObjectT> pending;
    /// Counted per drain, for the one line the queue reports itself with.
    std::size_t built = 0;
    std::size_t slices = 0;
    std::chrono::duration<double> spent {0};
};

/// One queue per document, keyed by name (the queue outlives objects, and
/// documents come and go between slices). Documents load independently --
/// a second file opened while the first still drains, a reload of one of
/// them -- and one queue for all of them made every decision the front
/// item's document's decision: a document still restoring stalled everyone
/// behind it, and the counts of any two loads ran together into one line
/// that described neither.
struct DeferredVisuals {
    std::map<std::string, DeferredVisualQueue> docs;
    bool scheduled = false;
    /// A serve inside a slice reports progress, and a progress indicator
    /// pumps events -- from which this slice's own timer can fire. The
    /// walk is not re-entrant: it holds an iterator into the map.
    bool running = false;
    /// The drain's own progress indicator (KeepInteractive: it reports,
    /// it does not take the window away). Created at the first slice
    /// that can work, one step per parked visual popped, reset when the
    /// last document's queue empties -- without it the longest phase of
    /// a progressive load ran with a dead status bar.
    std::unique_ptr<Base::SequencerLauncher> seq;
};

/// The three drawables' worth of emitted vertex-cache content
/// (docs/WorkerVertexCache.md).
struct EmittedVCache {
    std::shared_ptr<SoFCVertexCache::PrebuiltContent> face, line, point;
};

// The nested-struct definition must sit at PartGui scope, not inside
// the anonymous namespace this section otherwise lives in.
} // anonymous namespace
struct ViewProviderPartExt::PendingVisualVCache : EmittedVCache {};
namespace {

/// True while the deferred-visual drain is the caller of updateVisual.
/// The stand-in gate reads it (buildCoarseStandIn): a drain build of an
/// oversized bare shape is the LiveImport situation in every way that
/// matters -- geometry arriving faster than it can be tessellated while
/// the GUI is supposed to stay live -- but a plain .FCStd restore never
/// carries the LiveImport status (only the STEP import path sets it),
/// so the drain used to tessellate 20k-face compounds inline: measured
/// 1.8s single builds, the worst per-item stalls of both the load and
/// the drop phase.
bool s_drainVisualBuild = false;

/// True while a pump item runs the rebuild it deferred out of a
/// landing (see the gate at the top of updateVisual): the re-entered
/// updateVisual is inside the pump too, and without this it would
/// defer itself forever.
bool s_deferredVisualRun = false;

DeferredVisuals &deferredVisuals()
{
    static DeferredVisuals visuals;
    // A closing document takes its queue with it, rather than leaving it to
    // be swept when some later slice finds the name unresolvable: the same
    // file reopened right away answers to the same name, and stale handles
    // would resolve against the new document's objects.
    static bool observing = []() {
        App::GetApplication().signalDeleteDocument.connect(
                [](const App::Document &doc) {
                    visuals.docs.erase(doc.getName());
                    // The close that empties the drain also ends its
                    // progress sequence, or the bar reports a stuck
                    // "Building visuals..." forever.
                    if (visuals.docs.empty())
                        visuals.seq.reset();
                    if (Gui::Application::Instance)
                        Gui::Application::Instance->setBuildingVisuals(
                                !visuals.docs.empty());
                });
        return true;
    }();
    (void)observing;
    return visuals;
}

/// Publish "geometry is still being built into the views" for readers
/// outside this workbench (Gui::Application::isBuildingVisuals). This
/// queue being non-empty IS that state, and it is the only phase of a
/// progressive load in which geometry reaches a renderer at all: the
/// App restore and the deferred view-provider drain both complete with
/// the 3D scene still empty, so a reader that watches only those two is
/// told the load is over exactly when the geometry starts arriving.
///
/// Called at every mutation of the map rather than derived on demand,
/// because the map is a static of this translation unit and the readers
/// are in Gui, which must not depend on a workbench.
void syncBuildingVisuals()
{
    if (Gui::Application::Instance)
        Gui::Application::Instance->setBuildingVisuals(
                !deferredVisuals().docs.empty());
}

/// A Render::MeshData view over what the display nodes already hold.
///
/// The decimator (Gui/Renderer/MeshSimplify.h) speaks MeshData and this
/// class speaks Coin, and the two lay indices out differently. Nothing
/// here re-tessellates: positions and normals ALIAS the node storage
/// (SbVec3f is three floats and nothing else), and only the index
/// re-layouts allocate.
///
///  - triangles: Coin stores i0,i1,i2,SO_END_FACE_INDEX per triangle,
///    MeshData three indices with no delimiter.
///  - face parts: Coin's partIndex is a per-face triangle COUNT,
///    MeshData's triangleParts a {start, count} pair in INDEX units.
///  - lines: Coin stores polylines separated by -1, MeshData GL_LINES
///    style vertex PAIRS, so an n-point polyline becomes n-1 segments.
///
/// Only the arrays a decimated rung has to preserve are carried. Colours
/// are per-object here (the material nodes hold them, not the vertices),
/// so there are none to cluster.
struct CoinMeshView {
    std::vector<int32_t> tris, lines, points;
    Render::MeshData mesh;

    /// False when there is no triangle geometry to work on -- an
    /// edges-only or points-only object, which the decimator has
    /// nothing to say about and which is not what holds a budget open.
    bool build(const SoCoordinate3 *coords, const SoNormal *norm,
               const SoBrepFaceSet *faceset, const SoBrepEdgeSet *lineset,
               const SoBrepPointSet *nodeset, const SoCoordinate3 *pcoords,
               const SoTextureCoordinate2 *texcoords = nullptr)
    {
        const int nv = coords ? coords->point.getNum() : 0;
        const int nidx = faceset ? faceset->coordIndex.getNum() : 0;
        if (nv <= 0 || nidx < 4)
            return false;

        mesh.numVertices = nv;
        mesh.positions =
            reinterpret_cast<const float *>(coords->point.getValues(0));
        if (norm && norm->vector.getNum() == nv)
            mesh.normals =
                reinterpret_cast<const float *>(norm->vector.getValues(0));
        // Present only when sized to the vertices: the build path zeroes
        // this node to the coordinate count, so a mismatch is a node
        // some other rewrite left behind, not per-vertex UVs.
        if (texcoords && texcoords->point.getNum() == nv)
            mesh.texCoords = reinterpret_cast<const float *>(
                    texcoords->point.getValues(0));

        const int32_t *ci = faceset->coordIndex.getValues(0);
        tris.reserve(size_t(nidx / 4) * 3);
        for (int i = 0; i + 3 < nidx; i += 4) {
            if (ci[i] < 0 || ci[i + 1] < 0 || ci[i + 2] < 0)
                continue;
            tris.push_back(ci[i]);
            tris.push_back(ci[i + 1]);
            tris.push_back(ci[i + 2]);
        }
        if (tris.empty())
            return false;
        mesh.triangleIndices = tris.data();
        mesh.numTriangleIndices = int(tris.size());

        // Face parts, counts to ranges. A face that tessellated to
        // nothing still occupies a slot: the table is read by index, so
        // dropping empties would renumber every face after it.
        const int nparts = faceset->partIndex.getNum();
        if (nparts > 0) {
            const int32_t *pi = faceset->partIndex.getValues(0);
            mesh.triangleParts.reserve(size_t(nparts));
            int at = 0;
            for (int p = 0; p < nparts; ++p) {
                const int count = std::max<int>(0, pi[p]) * 3;
                mesh.triangleParts.emplace_back(at, count);
                at += count;
            }
        }

        if (lineset && lineset->coordIndex.getNum() > 0) {
            const int nl = lineset->coordIndex.getNum();
            const int32_t *li = lineset->coordIndex.getValues(0);
            lines.reserve(size_t(nl) * 2);
            int runStart = int(lines.size());
            for (int i = 0; i < nl; ++i) {
                if (li[i] < 0) {
                    if (int(lines.size()) > runStart)
                        mesh.lineParts.emplace_back(
                                runStart, int(lines.size()) - runStart);
                    runStart = int(lines.size());
                    continue;
                }
                // Pair this point with the previous one of the same run,
                // which is the segment between them.
                if (i > 0 && li[i - 1] >= 0) {
                    lines.push_back(li[i - 1]);
                    lines.push_back(li[i]);
                }
            }
            if (int(lines.size()) > runStart)
                mesh.lineParts.emplace_back(runStart,
                                            int(lines.size()) - runStart);
            if (!lines.empty()) {
                mesh.lineIndices = lines.data();
                mesh.numLineIndices = int(lines.size());
            }
        }

        // Vertex points index a DIFFERENT coordinate node (pcoords), so
        // they cannot ride the same clustering as the surface: their
        // indices would address the wrong array. They are left out and
        // put back untouched, which is also the right answer for what
        // they are -- a shape's vertices do not decimate.
        (void)nodeset;
        (void)pcoords;
        return true;
    }
};

/// A CoinMeshView the worker pool may read: the view's attribute
/// pointers aim into the live Coin nodes, so the decimation job copies
/// them out here, on the GUI thread, at enqueue. Everything the job
/// touches afterwards is owned by this snapshot.
struct OwnedMeshSnapshot {
    std::vector<float> positions, normals, texCoords;
    std::vector<int32_t> tris, lines;
    Render::MeshData mesh;

    bool build(const SoCoordinate3 *coords, const SoNormal *norm,
               const SoBrepFaceSet *faceset, const SoBrepEdgeSet *lineset,
               const SoBrepPointSet *nodeset, const SoCoordinate3 *pcoords,
               const SoTextureCoordinate2 *texcoords)
    {
        CoinMeshView view;
        if (!view.build(coords, norm, faceset, lineset, nodeset, pcoords,
                        texcoords))
            return false;
        const int nv = view.mesh.numVertices;
        positions.assign(view.mesh.positions,
                         view.mesh.positions + size_t(nv) * 3);
        if (view.mesh.normals)
            normals.assign(view.mesh.normals,
                           view.mesh.normals + size_t(nv) * 3);
        if (view.mesh.texCoords)
            texCoords.assign(view.mesh.texCoords,
                             view.mesh.texCoords + size_t(nv) * 2);
        tris = std::move(view.tris);
        lines = std::move(view.lines);
        // Copies the part tables; the array pointers are then rewired
        // to the owned copies (stable under the shared_ptr the job
        // holds -- nothing moves this struct after build).
        mesh = view.mesh;
        mesh.positions = positions.data();
        mesh.normals = normals.empty() ? nullptr : normals.data();
        mesh.texCoords = texCoords.empty() ? nullptr : texCoords.data();
        mesh.triangleIndices = tris.data();
        mesh.numTriangleIndices = int(tris.size());
        mesh.lineIndices = lines.empty() ? nullptr : lines.data();
        mesh.numLineIndices = int(lines.size());
        return true;
    }
};

} // anonymous namespace

/// Key of one shared tessellation in the global instance-geometry table:
/// the underlying TShape with its orientation (a reversed occurrence winds
/// differently) and the quantized tessellation parameters (objects with
/// different deviation settings must not fight over one mesh).
struct InstGeomKey {
    const void *tshape = nullptr;
    int orientation = 0;
    int64_t deflection = 0;      ///< quantized linear deflection
    int64_t angdeflection = 0;   ///< quantized angular deflection
    bool operator<(const InstGeomKey &o) const {
        return std::tie(tshape, orientation, deflection, angdeflection)
            < std::tie(o.tshape, o.orientation, o.deflection, o.angdeflection);
    }
};

/// One color variant of a shared tessellation: instances whose resolved
/// per-element colors diverge in value bake them per part -- one variant
/// per DISTINCT resolved color vector (the key), lazily created,
/// refcounted, shared by every instance (across objects) applying that
/// exact vector. The variant shape node references the geometry's
/// coordinate/normal/texcoord nodes; only the per-part material is its
/// own. Variants never mutate -- a different vector materializes a new
/// variant. Face variants bake diffuse+transparency, line/point variants
/// diffuse only (like the flattened per-edge/per-vertex paths).
struct ColorVariant {
    std::vector<uint32_t> key;   ///< packed RGBA per local element
    Gui::CoinPtr<SoGroup> group;
    SoShape *shape = nullptr;    ///< the variant's own face/edge/point set
    int refcount = 0;
};

/// One shared, immutable tessellation of a leaf sub-shape in its local
/// frame: the face/edge/vertex subgraphs referenced by every instance
/// separator (across objects -- the table is global). Entries never mutate
/// after build; a changed shape produces a new TShape and thus a new entry.
struct InstGeometry {
    Gui::CoinPtr<SoGroup> faceGroup;
    Gui::CoinPtr<SoGroup> edgeGroup;
    Gui::CoinPtr<SoGroup> vertexGroup;
    // Owned by the groups above; kept for detail/element mapping.
    SoBrepFaceSet *faceset = nullptr;
    SoBrepEdgeSet *lineset = nullptr;
    SoBrepPointSet *nodeset = nullptr;
    // Shared geometry nodes (children of faceGroup) the color variants
    // reference; the face branching never touches edges/vertices.
    SoCoordinate3 *coordsNode = nullptr;
    SoCoordinate3 *pcoordsNode = nullptr;
    SoNormal *normNode = nullptr;
    SoTextureCoordinate2 *texcoordsNode = nullptr;
    std::list<ColorVariant> variants;        ///< face color variants
    std::list<ColorVariant> lineVariants;
    std::list<ColorVariant> pointVariants;
    int faceCount = 0;
    int edgeCount = 0;
    int vertexCount = 0;
    int refcount = 0;
};

static std::map<InstGeomKey, InstGeometry> _InstGeomTable;

static inline uint32_t packColorRGBA(const App::Color &c)
{
    return c.getPackedValue();
}

/// Pack \a slice into \a key and return an existing variant with that
/// exact vector (referenced) or null -- the caller then builds one.
static ColorVariant *lookupColorVariant(std::list<ColorVariant> &variants,
                                        const std::vector<App::Color> &slice,
                                        std::vector<uint32_t> &key)
{
    key.reserve(slice.size());
    for (const auto &c : slice)
        key.push_back(packColorRGBA(c));
    for (auto &variant : variants) {
        if (variant.key == key) {
            ++variant.refcount;
            return &variant;
        }
    }
    return nullptr;
}

/// Find or build the variant of \a geom baking exactly the resolved
/// per-face colors \a slice (diffuse rgb + opacity in alpha), and
/// take a reference on it.
static ColorVariant *acquireColorVariant(InstGeometry &geom,
                                         const std::vector<App::Color> &slice)
{
    std::vector<uint32_t> key;
    if (auto *found = lookupColorVariant(geom.variants, slice, key))
        return found;

    geom.variants.emplace_back();
    ColorVariant &variant = geom.variants.back();
    variant.key = std::move(key);
    variant.refcount = 1;

    int n = int(slice.size());
    auto bind = new SoMaterialBinding;
    bind->value = SoMaterialBinding::PER_PART;
    auto mat = new SoMaterial;
    mat->diffuseColor.setNum(n);
    mat->transparency.setNum(n);
    SbColor *dc = mat->diffuseColor.startEditing();
    float *t = mat->transparency.startEditing();
    for (int i = 0; i < n; ++i) {
        dc[i].setValue(slice[i].r, slice[i].g, slice[i].b);
        t[i] = slice[i].transparency();
    }
    mat->diffuseColor.finishEditing();
    mat->transparency.finishEditing();
    // Everything but diffuse/transparency inherits the object material.
    mat->ambientColor.setIgnored(TRUE);
    mat->specularColor.setIgnored(TRUE);
    mat->emissiveColor.setIgnored(TRUE);
    mat->shininess.setIgnored(TRUE);

    auto faceset = new SoBrepFaceSet;
    // Same forced UV capture as the base faceset -- the variant must
    // produce identical geometry arrays (incl. texcoords) so the
    // backend's content-keyed geometry buffers are shared.
    faceset->forceTexCoords = TRUE;
    // Lets the render cache manager seed this node's vertex cache with
    // the base's -- the geometry arrays stay CPU-shared, only the baked
    // color array is owned.
    faceset->protoNode = geom.faceset;
    faceset->coordIndex = geom.faceset->coordIndex;
    faceset->partIndex = geom.faceset->partIndex;
    if (geom.faceset->shapeInfo.getNum())
        faceset->shapeInfo = geom.faceset->shapeInfo;
    faceset->setSiblings({geom.lineset, geom.nodeset});

    auto root = new Gui::SoFCSelectionRoot;
    root->addChild(geom.coordsNode);
    root->addChild(geom.normNode);
    root->addChild(geom.texcoordsNode);
    root->addChild(bind);
    root->addChild(mat);
    root->addChild(faceset);
    variant.group = root;
    variant.shape = faceset;
    return &variant;
}

/// A diffuse-only per-element SoMaterial for line/point variants and
/// overrides: everything else (incl. transparency -- lines/points never
/// carry one, like the flattened paths) inherits.
static SoMaterial *makeDiffuseOnlyMaterial()
{
    auto mat = new SoMaterial;
    mat->ambientColor.setIgnored(TRUE);
    mat->specularColor.setIgnored(TRUE);
    mat->emissiveColor.setIgnored(TRUE);
    mat->shininess.setIgnored(TRUE);
    mat->transparency.setIgnored(TRUE);
    return mat;
}

/// Find or build the line variant of \a geom baking exactly the resolved
/// per-edge colors \a slice, and take a reference on it.
static ColorVariant *acquireLineColorVariant(InstGeometry &geom,
                                             const std::vector<App::Color> &slice)
{
    std::vector<uint32_t> key;
    if (auto *found = lookupColorVariant(geom.lineVariants, slice, key))
        return found;

    geom.lineVariants.emplace_back();
    ColorVariant &variant = geom.lineVariants.back();
    variant.key = std::move(key);
    variant.refcount = 1;

    int n = int(slice.size());
    auto bind = new SoMaterialBinding;
    // Although an indexed lineset is used the binding must be PER_FACE
    // (one polyline per edge) -- same as the flattened per-edge path.
    bind->value = SoMaterialBinding::PER_FACE;
    auto mat = makeDiffuseOnlyMaterial();
    mat->diffuseColor.setNum(n);
    SbColor *dc = mat->diffuseColor.startEditing();
    for (int i = 0; i < n; ++i)
        dc[i].setValue(slice[i].r, slice[i].g, slice[i].b);
    mat->diffuseColor.finishEditing();

    auto lineset = new SoBrepEdgeSet;
    lineset->protoNode = geom.lineset;
    lineset->coordIndex = geom.lineset->coordIndex;
    if (geom.lineset->seamIndices.getNum())
        lineset->seamIndices = geom.lineset->seamIndices;
    lineset->setSiblings({geom.faceset, geom.nodeset});

    auto root = new Gui::SoFCSelectionRoot;
    root->addChild(geom.coordsNode);
    root->addChild(bind);
    root->addChild(mat);
    root->addChild(lineset);
    variant.group = root;
    variant.shape = lineset;
    return &variant;
}

/// Find or build the point variant of \a geom baking exactly the
/// resolved per-vertex colors \a slice, and take a reference on it.
static ColorVariant *acquirePointColorVariant(InstGeometry &geom,
                                              const std::vector<App::Color> &slice)
{
    std::vector<uint32_t> key;
    if (auto *found = lookupColorVariant(geom.pointVariants, slice, key))
        return found;

    geom.pointVariants.emplace_back();
    ColorVariant &variant = geom.pointVariants.back();
    variant.key = std::move(key);
    variant.refcount = 1;

    int n = int(slice.size());
    auto bind = new SoMaterialBinding;
    bind->value = SoMaterialBinding::PER_VERTEX;
    auto mat = makeDiffuseOnlyMaterial();
    mat->diffuseColor.setNum(n);
    SbColor *dc = mat->diffuseColor.startEditing();
    for (int i = 0; i < n; ++i)
        dc[i].setValue(slice[i].r, slice[i].g, slice[i].b);
    mat->diffuseColor.finishEditing();

    auto nodeset = new SoBrepPointSet;
    nodeset->protoNode = geom.nodeset;
    nodeset->startIndex = geom.nodeset->startIndex;
    nodeset->setSiblings({geom.faceset, geom.lineset});

    auto root = new Gui::SoFCSelectionRoot;
    root->addChild(geom.pcoordsNode);
    root->addChild(bind);
    root->addChild(mat);
    root->addChild(nodeset);
    variant.group = root;
    variant.shape = nodeset;
    return &variant;
}

static void releaseColorVariant(std::list<ColorVariant> &variants,
                                ColorVariant *variant)
{
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (&*it == variant) {
            if (--it->refcount <= 0)
                variants.erase(it);
            return;
        }
    }
}

/// The TShape-instanced representation of one view provider: which global
/// geometry entries it references and, per placed instance, the wrapper
/// separators plus the global element index bases (global FaceN/EdgeN/
/// VertexN = base + index local to the shared node).
struct ShapeInstanceRep {
    struct Instance {
        InstGeometry *geom = nullptr;
        Gui::CoinPtr<SoSeparator> faceSep;
        Gui::CoinPtr<SoSeparator> edgeSep;
        Gui::CoinPtr<SoSeparator> vertexSep;
        /// uniform-slice per-instance color: rides the render-cache
        /// material at the wrapper (the Link mechanism) -- never baked
        Gui::CoinPtr<SoMaterial> overrideMat;
        /// divergent-slice per-instance colors: baked color variant
        ColorVariant *variant = nullptr;
        /// same pair for per-edge and per-vertex colors
        Gui::CoinPtr<SoMaterial> lineOverrideMat;
        ColorVariant *lineVariant = nullptr;
        Gui::CoinPtr<SoMaterial> pointOverrideMat;
        ColorVariant *pointVariant = nullptr;
        int faceBase = 0;
        int edgeBase = 0;
        int vertexBase = 0;
    };
    std::vector<Instance> instances;
    std::vector<InstGeomKey> keys;   ///< table refs to release
    /// any of an instance's three wrapper separators -> instance index
    std::unordered_map<const SoNode *, int> sepToInstance;
    /// a shared faceset/lineset/nodeset (or variant faceset) -> the
    /// geometry entry
    std::unordered_map<const SoNode *, const InstGeometry *> nodeToGeom;

    /// Recompute nodeToGeom from the instances' current variant choices.
    void rebuildNodeMap()
    {
        nodeToGeom.clear();
        for (const auto &inst : instances) {
            nodeToGeom[inst.geom->faceset] = inst.geom;
            nodeToGeom[inst.geom->lineset] = inst.geom;
            nodeToGeom[inst.geom->nodeset] = inst.geom;
            if (inst.variant)
                nodeToGeom[inst.variant->shape] = inst.geom;
            if (inst.lineVariant)
                nodeToGeom[inst.lineVariant->shape] = inst.geom;
            if (inst.pointVariant)
                nodeToGeom[inst.pointVariant->shape] = inst.geom;
        }
    }

    ~ShapeInstanceRep()
    {
        // Variant references go first -- the geometry entries they nest in
        // are still held by the keys released below.
        for (auto &inst : instances) {
            if (inst.variant)
                releaseColorVariant(inst.geom->variants, inst.variant);
            if (inst.lineVariant)
                releaseColorVariant(inst.geom->lineVariants, inst.lineVariant);
            if (inst.pointVariant)
                releaseColorVariant(inst.geom->pointVariants,
                                    inst.pointVariant);
        }
        for (const auto &key : keys) {
            auto it = _InstGeomTable.find(key);
            if (it != _InstGeomTable.end() && --it->second.refcount <= 0) {
                unregisterMeshLevelSource(it->second.faceset,
                                          it->second.lineset);
                // Drop any worker vertex-cache content still waiting on
                // these nodes: the erase below destroys them, and the
                // registry is keyed on the raw pointer, so an unconsumed
                // entry would hold its arrays until some future node
                // happened to land at the same address. Same duty
                // beforeDelete() performs for a view provider's own
                // nodes (docs/WorkerVertexCache.md). The last sharer of
                // the leaf runs this -- the entry is global and
                // refcounted, so a leaf that also appears under another
                // object's compound keeps it alive until that object is
                // gone too.
                SoFCVertexCache::setPrebuilt(it->second.faceset, nullptr);
                SoFCVertexCache::setPrebuilt(it->second.lineset, nullptr);
                SoFCVertexCache::setPrebuilt(it->second.nodeset, nullptr);
                _InstGeomTable.erase(it);
            }
        }
    }
};

/// Make an instance wrapper match its current color choice: children
/// [matrixTransform, overrideMat?, base group or variant group]. No-op
/// when the wrapper already has that structure.
static void restructureInstanceSep(SoSeparator *sep, SoMaterial *overrideMat,
                                   SoNode *body)
{
    if (!sep || sep->getNumChildren() < 1)
        return;
    int n = sep->getNumChildren();
    bool same = overrideMat
        ? (n == 3 && sep->getChild(1) == overrideMat
                  && sep->getChild(2) == body)
        : (n == 2 && sep->getChild(1) == body);
    if (same)
        return;
    while (sep->getNumChildren() > 1)
        sep->removeChild(sep->getNumChildren() - 1);
    if (overrideMat)
        sep->addChild(overrideMat);
    sep->addChild(body);
}

static void restructureInstanceFace(ShapeInstanceRep::Instance &inst)
{
    restructureInstanceSep(inst.faceSep, inst.overrideMat,
                           inst.variant ? inst.variant->group.get()
                                        : inst.geom->faceGroup.get());
}

static void restructureInstanceEdge(ShapeInstanceRep::Instance &inst)
{
    restructureInstanceSep(inst.edgeSep, inst.lineOverrideMat,
                           inst.lineVariant ? inst.lineVariant->group.get()
                                            : inst.geom->edgeGroup.get());
}

static void restructureInstanceVertex(ShapeInstanceRep::Instance &inst)
{
    restructureInstanceSep(inst.vertexSep, inst.pointOverrideMat,
                           inst.pointVariant ? inst.pointVariant->group.get()
                                             : inst.geom->vertexGroup.get());
}

/// The environment part of the shape-instancing gate: the feature param,
/// a backend renderer live (plain Coin/GL always flattens -- without
/// GPU instancing many small shared nodes are a net loss), and the
/// backend's published instancing capability. The backend question is
/// asked of the actual renderer state, not the Render Type preference:
/// a backend can be attached with the pref still "Default" (per-view or
/// scripted selection), and a pref naming a backend yields none when
/// creation fails (plain-GL fallback).
static bool shapeInstancingActive()
{
    if (!PartParams::getShapeInstancing())
        return false;
    if (Gui::ViewParams::getRenderCache() != 3)
        return false;
    if (Render::Renderer::activeCount() == 0)
        return false;
    return Render::Renderer::instancingHint();
}

} // namespace PartGui

//**************************************************************************
// PropertyDiffuseColor -- a name over ShapeAppearance's diffuse field

// Registered with a leading underscore: this is a name over ShapeAppearance's
// diffuse field, wired by the view provider to its own member, so it is not a
// type a user may add (App::Property::isInternalType, and the note beside
// Gui::PropertyShapeColor).
TYPESYSTEM_SOURCE_P(PartGui::PropertyDiffuseColor)
void PartGui::PropertyDiffuseColor::init()
{
    initSubclass(PartGui::PropertyDiffuseColor::classTypeId,
                 "PartGui::_PropertyDiffuseColor", "App::PropertyColorList",
                 &PartGui::PropertyDiffuseColor::create);
}

void PropertyDiffuseColor::setAppearance(App::PropertyAppearanceList *appearance,
                                         const App::PropertyColor *shapeColor)
{
    _appearance = appearance;
    _shapeColor = shapeColor;
    // Whatever the constructor's ADD_PROPERTY put in the base list is a copy
    // of what the appearance already holds. Drop it rather than leave a
    // second, stale answer behind where a base-pointer read could find it.
    _lValueList.clear();
}

const std::vector<Base::Color> &PropertyDiffuseColor::getValues() const
{
    if (_appearance) {
        // Resolved out of the base and the overriding faces, into the member
        // this property keeps for it -- see the note on _resolved
        _resolved = _appearance->getDiffuseColors();
        return _resolved;
    }
    return App::PropertyColorList::getValues();
}

int PropertyDiffuseColor::getSize() const
{
    // Not through getValues(): the size is the appearance's entry count and
    // asking for it must not resolve a colour per face
    if (_appearance)
        return _appearance->getSize();
    return static_cast<int>(App::PropertyColorList::getValues().size());
}

void PropertyDiffuseColor::setValues(std::vector<Base::Color> &&colors)
{
    if (!_appearance) {
        App::PropertyColorList::setValues(std::move(colors));
        return;
    }
    if (colors.empty() && _shapeColor) {
        // Emptying the list has always meant "every face back to the object
        // colour"; an empty field in the appearance means the default
        // material's colour instead, which is a different colour whenever
        // the object has one of its own.
        _appearance->setDiffuseColor(_shapeColor->getValue());
        return;
    }
    _appearance->setDiffuseColors(colors);
}

void PropertyDiffuseColor::set1Value(int idx, const Base::Color &col)
{
    if (!_appearance) {
        App::PropertyColorList::set1Value(idx, col);
        return;
    }
    // The appearance grows the list itself when idx names a new entry, and
    // takes -1 as "append" the way the base does.
    _appearance->setDiffuseColor(idx < 0 ? _appearance->getSize() : idx, col);
}

void PropertyDiffuseColor::setSize(int newSize)
{
    if (!_appearance) {
        App::PropertyColorList::setSize(newSize);
        return;
    }
    _appearance->setSize(newSize);
}

void PropertyDiffuseColor::setSize(int newSize, const Base::Color &def)
{
    if (!_appearance) {
        App::PropertyColorList::setSize(newSize, def);
        return;
    }
    App::MaterialAppearance mat = _appearance->getMaterial(0);
    mat.diffuseColor = def;
    _appearance->setSize(newSize, mat);
}

unsigned int PropertyDiffuseColor::getMemSize() const
{
    // Nothing of its own: the colours are counted where they are stored.
    if (_appearance)
        return 0;
    return App::PropertyColorList::getMemSize();
}

bool PropertyDiffuseColor::isSame(const App::Property &other) const
{
    if (&other == this)
        return true;
    if (auto redirected = Base::freecad_dynamic_cast<const PropertyDiffuseColor>(&other))
        return getValues() == redirected->getValues();
    auto list = Base::freecad_dynamic_cast<const App::PropertyColorList>(&other);
    return list && getValues() == list->getValues();
}

App::Property *PropertyDiffuseColor::Copy() const
{
    // A plain list, so that whoever holds the copy -- undo, mostly -- holds
    // the values and not a second pointer into this object's appearance.
    auto copy = new App::PropertyColorList();
    copy->setValues(getValues());
    return copy;
}

void PropertyDiffuseColor::Paste(const App::Property &from)
{
    if (auto redirected = Base::freecad_dynamic_cast<const PropertyDiffuseColor>(&from))
        setValues(redirected->getValues());
    else
        setValues(dynamic_cast<const App::PropertyColorList&>(from).getValues());
}

PyObject *PropertyDiffuseColor::getPyObject()
{
    const auto &values = getValues();
    PyObject *list = PyList_New(values.size());
    int i = 0;
    for (const auto &color : values) {
        PyObject *rgba = PyTuple_New(4);
        PyTuple_SetItem(rgba, 0, PyFloat_FromDouble(color.r));
        PyTuple_SetItem(rgba, 1, PyFloat_FromDouble(color.g));
        PyTuple_SetItem(rgba, 2, PyFloat_FromDouble(color.b));
        PyTuple_SetItem(rgba, 3, PyFloat_FromDouble(color.a));
        PyList_SetItem(list, i++, rgba);
    }
    return list;
}

void PropertyDiffuseColor::setPyObject(PyObject *value)
{
    // A single colour, which the base spells through its non-virtual
    // setValue -- hence the copy of that logic rather than a call to it. The
    // read is what may fail here; the write must not have its exception
    // swallowed and reported as a bad sequence.
    Base::Color color;
    bool single = false;
    try {
        color = getPyValue(value);
        single = true;
    }
    catch (...) {
    }
    if (single) {
        std::vector<Base::Color> colors(1, color);
        guardLegacyAlpha(colors);
        setValues(std::move(colors));
        return;
    }
    // A sequence, parsed by a plain colour list so the guard below sees the
    // whole assignment at once. (Handing the object to the inherited
    // setPyObject would write colour by colour, past any chance to judge it.)
    App::PropertyColorList parsed;
    parsed.App::PropertyColorList::setPyObject(value);
    std::vector<Base::Color> colors = parsed.getValues();
    guardLegacyAlpha(colors);
    setValues(std::move(colors));
}

/** Catch a macro written for the old meaning of alpha
 *
 * A colour's alpha means opacity now (see Base/Color.h); it used to mean
 * transparency, and every macro from that era spells "opaque" as 0.0 -- which
 * today reads as invisible. An assignment in which EVERY alpha is exactly 0.0
 * is therefore either such a macro or a deliberate attempt to make every face
 * invisible through DiffuseColor; the first is common and the second has
 * better spellings (Transparency = 100, or Visibility). So the guard assumes
 * the macro, makes the colours opaque, and says so once per session.
 *
 * A genuine read-modify-write of an already invisible object presents the
 * same all-zero alphas; that is what the current-state test excuses. Partial
 * transparency (0.5 both ways), and anything mixed, is left exactly as given
 * -- those values mean the same or cannot be judged, and guessing would be
 * worse than either reading.
 */
void PropertyDiffuseColor::guardLegacyAlpha(std::vector<Base::Color> &colors) const
{
    if (colors.empty())
        return;
    for (const auto &color : colors) {
        if (color.a != 0.0f)
            return;
    }
    // Already fully transparent: the zeroes are this object's own state
    // coming back, not a legacy macro's idea of opaque.
    const auto &current = getValues();
    if (!current.empty()) {
        bool invisible = true;
        for (const auto &color : current) {
            if (color.a != 0.0f) {
                invisible = false;
                break;
            }
        }
        if (invisible)
            return;
    }
    static bool warned;
    if (!warned) {
        warned = true;
        Base::Console().Warning(
            "DiffuseColor: an assignment set every alpha to 0, the old spelling of "
            "opaque, and was read as opaque. Alpha means opacity now (1 = opaque); "
            "use Transparency or Visibility to hide an object.\n");
    }
    for (auto &color : colors)
        color.a = 1.0f;
}

void PropertyDiffuseColor::Save(Base::Writer &writer) const
{
    // With the values: this is the name every reader that predates
    // ShapeAppearance knows, so as long as the appearance says nothing a
    // colour list cannot say, the document carries the face colours under
    // both names and an older FreeCAD opens it with its colours intact. The
    // alpha written here is a TRANSPARENCY, which is what those readers
    // expect -- saveXML and saveStream below convert it.
    //
    // The cost is that a per-face import stores its colours twice. That is
    // the price of the compatibility and it is paid deliberately.
    if (!_appearance || _appearance->variesOnlyInDiffuse()) {
        App::PropertyColorList::Save(writer);
        return;
    }
    // Without them: the appearance varies a field a colour list has no room
    // for, so any copy written here would be a lossy second answer to the
    // same question. The element still goes out, so a document keeps a
    // DiffuseColor of this type, which is what distinguishes it from an
    // older one that carries the values.
    writer.Stream() << writer.ind() << '<' << xmlName() << " file=\"\"/>\n";
}

void PropertyDiffuseColor::Restore(Base::XMLReader &reader)
{
    if (!_appearance) {
        App::PropertyColorList::Restore(reader);
        return;
    }
    // PropertyLists::Restore with neither values nor a file clears the list,
    // which here would empty the appearance that is about to be restored
    // into. Everything else is as the base does it -- including registering
    // *this* for a separate file, which is why an older document's
    // DiffuseColor is restored through this property and not a stand-in
    // (the file is read long after the XML pass has moved on).
    reader.readElement(xmlName());
    std::string file(reader.getAttribute("file", ""));
    if (!file.empty())
        reader.addFile(file.c_str(), this);
    else if (reader.hasAttribute("count"))
        restoreXML(reader);
}

void PropertyDiffuseColor::restoreXML(Base::XMLReader &reader)
{
    if (!_appearance) {
        App::PropertyColorList::restoreXML(reader);
        return;
    }
    int count = reader.getAttributeAsInteger("count");
    // Whether alpha means opacity is a property of the file, not of the
    // element, so it is asked here as well -- the same question the inherited
    // archive-member path asks (App::PropertyColorList::RestoreDocFile).
    bool convert = !Base::alphaIsOpacity(reader);
    std::vector<Base::Color> values(count);
    auto &stream = reader.beginCharStream() >> std::hex;
    for (int i = 0; i < count; ++i) {
        uint32_t packed;
        stream >> packed;
        values[i].setPackedValue(packed);
        if (convert)
            values[i].a = 1.0F - values[i].a;
    }
    stream >> std::dec;
    reader.endCharStream();
    setValues(std::move(values));
}

bool PropertyDiffuseColor::saveXML(Base::Writer &writer) const
{
    if (!_appearance)
        return App::PropertyColorList::saveXML(writer);
    const bool convert = !Base::writerAlphaIsOpacity();
    writer.Stream() << ">\n" << std::hex;
    for (auto color : getValues()) {
        if (convert)
            color.a = 1.0F - color.a;
        writer.Stream() << color.getPackedValue() << '\n';
    }
    writer.Stream() << std::dec;
    return false;
}

void PropertyDiffuseColor::restoreStream(Base::InputStream &str, unsigned count)
{
    if (!_appearance) {
        App::PropertyColorList::restoreStream(str, count);
        return;
    }
    std::vector<Base::Color> values(count);
    uint32_t packed = 0;  // must be 32 bit long
    for (auto &color : values) {
        str >> packed;
        color.setPackedValue(packed);
    }
    setValues(std::move(values));
}

void PropertyDiffuseColor::saveStream(Base::OutputStream &str) const
{
    if (!_appearance) {
        App::PropertyColorList::saveStream(str);
        return;
    }
    const bool convert = !Base::writerAlphaIsOpacity();
    for (auto color : getValues()) {
        if (convert)
            color.a = 1.0F - color.a;
        str << color.getPackedValue();
    }
}

unsigned int PropertyDiffuseColor::getSaveSize(Base::Writer &writer) const
{
    (void)writer;
    // getMemSize() is deliberately 0 -- the colours are counted where they are
    // stored -- but the inline-versus-archive rule reads it as the cost of
    // writing this property, and now that the values do go out, that cost is
    // real again.
    if (!_appearance)
        return App::PropertyColorList::getMemSize();
    return static_cast<unsigned int>(getValues().size() * sizeof(uint32_t));
}

//**************************************************************************
// Construction/Destruction

App::PropertyFloatConstraint::Constraints ViewProviderPartExt::sizeRange = {1.0,64.0,1.0};
App::PropertyFloatConstraint::Constraints ViewProviderPartExt::tessRange = {0.01,100.0,0.01};
App::PropertyQuantityConstraint::Constraints ViewProviderPartExt::angDeflectionRange = {1.0,180.0,0.05};
const char* ViewProviderPartExt::LightingEnums[]= {"One side","Two side",nullptr};
const char* ViewProviderPartExt::DrawStyleEnums[]= {"Solid","Dashed","Dotted","Dashdot",nullptr};

ViewProviderPartExt::ViewProviderPartExt()
{
    static bool _inited;
    if (!_inited) {
        _inited = true;
        tessRange = {PartParams::getMinimumDeviation(),100.0,0.01};
        angDeflectionRange = {PartParams::getMinimumAngularDeflection(),180.0,0.05};
    }

    UpdatingColor = false;
    VisualTouched = true;
    forceUpdateCount = 0;

    // Rebuild Part visuals when a parameter of the shape-instancing gate
    // flips (render cache mode / renderer type), so the representation
    // switches between the instanced and the flattened build.
    initShapeInstancingGateObserver();

    // get default line color
    unsigned long lcol = Gui::ViewParams::getDefaultShapeLineColor(); // dark grey (25,25,25)
    float lr,lg,lb;
    lr = ((lcol >> 24) & 0xff) / 255.0; lg = ((lcol >> 16) & 0xff) / 255.0; lb = ((lcol >> 8) & 0xff) / 255.0;
    // get default vertex color
    unsigned long vcol = Gui::ViewParams::getDefaultShapeVertexColor(); 
    float vr,vg,vb;
    vr = ((vcol >> 24) & 0xff) / 255.0; vg = ((vcol >> 16) & 0xff) / 255.0; vb = ((vcol >> 8) & 0xff) / 255.0;
    int lwidth = Gui::ViewParams::getDefaultShapeLineWidth();
    int psize = Gui::ViewParams::getDefaultShapePointSize();
	


    NormalsFromUV = PartParams::getNormalsFromUVNodes();

    long twoside = PartParams::getTwoSideRendering() ? 1 : 0;

    static const char *osgroup = "Object Style";

    App::MaterialAppearance lmat;
    lmat.ambientColor.set(0.2f,0.2f,0.2f);
    lmat.diffuseColor.set(lr,lg,lb);
    lmat.specularColor.set(0.0f,0.0f,0.0f);
    lmat.emissiveColor.set(0.0f,0.0f,0.0f);
    lmat.shininess = 1.0f;
    lmat.transparency = 0.0f;

    App::MaterialAppearance vmat;
    vmat.ambientColor.set(0.2f,0.2f,0.2f);
    vmat.diffuseColor.set(vr,vg,vb);
    vmat.specularColor.set(0.0f,0.0f,0.0f);
    vmat.emissiveColor.set(0.0f,0.0f,0.0f);
    vmat.shininess = 1.0f;
    vmat.transparency = 0.0f;

    ADD_PROPERTY_TYPE(LineMaterial,(lmat), osgroup, App::Prop_None, "Object line material.");
    ADD_PROPERTY_TYPE(PointMaterial,(vmat), osgroup, App::Prop_None, "Object point material.");
    ADD_PROPERTY_TYPE(LineColor, (lmat.diffuseColor), osgroup, App::Prop_None, "Set object line color.");
    ADD_PROPERTY_TYPE(PointColor, (vmat.diffuseColor), osgroup, App::Prop_None, "Set object point color");
    ADD_PROPERTY_TYPE(PointColorArray, (PointColor.getValue()), osgroup, App::Prop_None, "Object point color array.");
    ADD_PROPERTY_TYPE(DiffuseColor,(ShapeColor.getValue()), osgroup, App::Prop_None, "Object diffuse color.");
    // From here on the face colours are the appearance's, which already
    // holds this same colour as its single entry. Wired after the
    // ADD_PROPERTY above, whose write must not be redirected into a property
    // the base class is still setting up.
    DiffuseColor.setAppearance(&ShapeAppearance, &ShapeColor);
    ADD_PROPERTY_TYPE(LineColorArray,(LineColor.getValue()), osgroup, App::Prop_None, "Object line color array.");
    ADD_PROPERTY_TYPE(LineWidth,(lwidth), osgroup, App::Prop_None, "Set object line width.");
    LineWidth.setConstraints(&sizeRange);
    PointSize.setConstraints(&sizeRange);
    ADD_PROPERTY_TYPE(PointSize,(psize), osgroup, App::Prop_None, "Set object point size.");
    ADD_PROPERTY_TYPE(Deviation,(PartParams::getMeshDeviation()), osgroup, App::Prop_None,
            "Sets the accuracy of the polygonal representation of the model\n"
            "in the 3D view (tessellation). Lower values indicate better quality.\n"
            "The value is in percent of object's size.");
    Deviation.setConstraints(&tessRange);
    ADD_PROPERTY_TYPE(AngularDeflection,(PartParams::getMeshAngularDeflection()), osgroup, App::Prop_None,
            "Specify how finely to generate the mesh for rendering on screen or when exporting.\n"
            "The default value is 28.5 degrees, or 0.5 radians. The smaller the value\n"
            "the smoother the appearance in the 3D view, and the finer the mesh that will be exported.");
    AngularDeflection.setConstraints(&angDeflectionRange);
    ADD_PROPERTY_TYPE(Lighting,(twoside), osgroup, App::Prop_None, "Set object lighting.");
    Lighting.setEnums(LightingEnums);
    ADD_PROPERTY_TYPE(DrawStyle,((long int)0), osgroup, App::Prop_None, "Defines the style of the edges in the 3D view.");
    DrawStyle.setEnums(DrawStyleEnums);

    ADD_PROPERTY_TYPE(MappedColors,(),"",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly),"");

    ADD_PROPERTY(MapFaceColor,(PartParams::getMapFaceColor()));
    ADD_PROPERTY(MapLineColor,(PartParams::getMapLineColor()));
    ADD_PROPERTY(MapPointColor,(PartParams::getMapPointColor()));
    ADD_PROPERTY(MapTransparency,(PartParams::getMapTransparency()));
    ADD_PROPERTY(ForceMapColors,(false));

    coords = new SoFCCoordinate3();
    static_cast<SoFCCoordinate3*>(coords)->vp = this;
    coords->ref();
    pcoords = new SoCoordinate3();
    // Born EMPTY, not with Coin's default single (0,0,0): a point set
    // draws every coordinate it can see, no index in between, so an
    // unfilled nodeset paints a phantom dot at the object's origin --
    // and it passes every element gate, because unfilled means
    // unclassified and unclassified must always draw. MEASURED: 5909
    // such dots covered the first ~165 frames of a rack model load.
    pcoords->point.setNum(0);
    pcoords->ref();
    faceset = new SoBrepFaceSet();
    faceset->ref();
    norm = new SoNormal;
    norm->ref();
    texcoords = new SoTextureCoordinate2;
    texcoords->point.setNum(0);
    texcoords->ref();
    normb = new SoNormalBinding;
    normb->value = SoNormalBinding::PER_VERTEX_INDEXED;
    normb->ref();
    lineset = new SoBrepEdgeSet();
    lineset->ref();
    nodeset = new SoBrepPointSet();
    nodeset->ref();

    faceset->setSiblings({lineset,nodeset});
    lineset->setSiblings({faceset,nodeset});
    nodeset->setSiblings({faceset,lineset});

    pcFaceBind = new SoMaterialBinding();
    pcFaceBind->ref();

    pcLineBind = new SoMaterialBinding();
    pcLineBind->ref();
    pcLineMaterial = new SoMaterial;
    pcLineMaterial->ref();
    LineMaterial.touch();

    pcPointBind = new SoMaterialBinding();
    pcPointBind->ref();
    pcPointMaterial = new SoMaterial;
    pcPointMaterial->ref();
    PointMaterial.touch();

    pcLineStyle = new SoDrawStyle();
    pcLineStyle->ref();
    pcLineStyle->style = SoDrawStyle::LINES;
    pcLineStyle->lineWidth = LineWidth.getValue();

    pcPointStyle = new SoDrawStyle();
    pcPointStyle->ref();
    pcPointStyle->style = SoDrawStyle::POINTS;
    pcPointStyle->pointSize = PointSize.getValue();

    pShapeHints = new SoShapeHints;
    pShapeHints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
    pShapeHints->ref();
    Lighting.touch();
    DrawStyle.touch();

    sPixmap = "Part_3D_object";
}

ViewProviderPartExt::~ViewProviderPartExt()
{
    unregisterMeshLevelSource(faceset, lineset);
    // The pooled visual fill keys its worker job on the coords node
    // (its own token slot); the unregister above cannot cancel it,
    // and its landing captures `this`.
    cancelMeshLevelWork(coords);
    pcFaceBind->unref();
    pcLineBind->unref();
    pcPointBind->unref();
    pcLineMaterial->unref();
    pcPointMaterial->unref();
    pcLineStyle->unref();
    pcPointStyle->unref();
    pShapeHints->unref();
    static_cast<SoFCCoordinate3*>(coords)->vp = nullptr;
    coords->unref();
    pcoords->unref();
    faceset->unref();
    norm->unref();
    texcoords->unref();
    normb->unref();
    lineset->unref();
    nodeset->unref();
}

void ViewProviderPartExt::handleChangedPropertyType(Base::XMLReader &reader,
                                                    const char *TypeName,
                                                    App::Property *prop)
{
    // DiffuseColor kept its name and its bytes but changed type when its
    // storage moved into ShapeAppearance, so a document written before that
    // arrives here rather than at Restore, where the base does nothing and
    // every per-face colour would be dropped in silence.
    //
    // Restored through the property itself, not a stand-in: a colour list
    // large enough to live in its own archive entry is read long after this
    // returns, and it is the pointer handed to the reader now that the read
    // will write into.
    if (prop == &DiffuseColor
            && strcmp(TypeName, App::PropertyColorList::getClassTypeId().getName()) == 0) {
        DiffuseColor.Restore(reader);
        return;
    }
    inherited::handleChangedPropertyType(reader, TypeName, prop);
}

void ViewProviderPartExt::onChanged(const App::Property* prop)
{
    Gui::ColorUpdater colorUpdater;

    if (prop == &MappedColors ||
        prop == &MapFaceColor ||
        prop == &MapLineColor ||
        prop == &MapPointColor ||
        prop == &MapTransparency || 
        prop == &ForceMapColors) 
    {
        if(!prop->testStatus(App::Property::User3)) {
            if(prop == &MapFaceColor) {
                if(!MapFaceColor.getValue()) {
                    ShapeColor.touch();
                    return;
                }
            }else if(prop == &MapLineColor) {
                if(!MapLineColor.getValue()) {
                    LineColor.touch();
                    return;
                }
            }else if(prop == &MapPointColor) {
                if(!MapPointColor.getValue()) {
                    PointColor.touch();
                    return;
                }
            }
            updateColors();
        }
        return;
    }
    
    if (isRestoring()) {
        if (prop == &LineColor
                || prop == &LineMaterial
                || prop == &PointColor
                || prop == &PointMaterial
                || prop == &ShapeColor
                || prop == &ShapeAppearance)
        {
            // When restoring, rely on
            // DiffuseColor/LineColorArray/PointColorArray to setup the colors.
            // Because the order of restoring say DiffuseColor and ShapeColor
            // may be different depending on whether the PropertyColorList is
            // saved into a separate file or embedded inside xml.
            ViewProviderDocumentObject::onChanged(prop);
            return;
        }
    }

    // The lower limit of the deviation has been increased to avoid
    // to freeze the GUI
    // https://forum.freecad.org/viewtopic.php?f=3&t=24912&p=195613
    if (prop == &Deviation) {
        if (!prop->testStatus(App::Property::User3)) {
            if(isUpdateForced()||Visibility.getValue()) 
                updateVisual();
            else
                VisualTouched = true;
        }
    }
    if (prop == &AngularDeflection) {
        if (!prop->testStatus(App::Property::User3)) {
            if(isUpdateForced()||Visibility.getValue()) 
                updateVisual();
            else
                VisualTouched = true;
        }
    }
    if (prop == &LineWidth) {
        if (PartParams::getRespectSystemDPI())
            pcLineStyle->lineWidth = std::max(1.0, qApp->devicePixelRatio()*LineWidth.getValue());
        else
            pcLineStyle->lineWidth = LineWidth.getValue();
    }
    else if (prop == &PointSize) {
        if (PartParams::getRespectSystemDPI())
            pcPointStyle->pointSize = std::max(1.0, qApp->devicePixelRatio()*PointSize.getValue());
        else
            pcPointStyle->pointSize = PointSize.getValue();
    }
    else if (prop == &LineColor) {
        const App::Color& c = LineColor.getValue();
        pcLineMaterial->diffuseColor.setValue(c.r,c.g,c.b);
        if (c != LineMaterial.getValue().diffuseColor)
            LineMaterial.setDiffuseColor(c);
        LineColorArray.setValue(LineColor.getValue());
        if(!prop->testStatus(App::Property::User3))
            updateColors();
    }
    else if (prop == &PointColor) {
        const App::Color& c = PointColor.getValue();
        pcPointMaterial->diffuseColor.setValue(c.r,c.g,c.b);
        if (c != PointMaterial.getValue().diffuseColor)
            PointMaterial.setDiffuseColor(c);
        PointColorArray.setValue(PointColor.getValue());
        if(!prop->testStatus(App::Property::User3))
            updateColors();
    }
    else if (prop == &LineMaterial) {
        const App::MaterialAppearance& Mat = LineMaterial.getValue();
        if (LineColor.getValue() != Mat.diffuseColor)
            LineColor.setValue(Mat.diffuseColor);
        pcLineMaterial->ambientColor.setValue(Mat.ambientColor.r,Mat.ambientColor.g,Mat.ambientColor.b);
        pcLineMaterial->diffuseColor.setValue(Mat.diffuseColor.r,Mat.diffuseColor.g,Mat.diffuseColor.b);
        pcLineMaterial->specularColor.setValue(Mat.specularColor.r,Mat.specularColor.g,Mat.specularColor.b);
        pcLineMaterial->emissiveColor.setValue(Mat.emissiveColor.r,Mat.emissiveColor.g,Mat.emissiveColor.b);
        pcLineMaterial->shininess.setValue(Mat.shininess);
        pcLineMaterial->transparency.setValue(Mat.transparency);
    }
    else if (prop == &PointMaterial) {
        const App::MaterialAppearance& Mat = PointMaterial.getValue();
        if (PointColor.getValue() != Mat.diffuseColor)
            PointColor.setValue(Mat.diffuseColor);
        pcPointMaterial->ambientColor.setValue(Mat.ambientColor.r,Mat.ambientColor.g,Mat.ambientColor.b);
        pcPointMaterial->diffuseColor.setValue(Mat.diffuseColor.r,Mat.diffuseColor.g,Mat.diffuseColor.b);
        pcPointMaterial->specularColor.setValue(Mat.specularColor.r,Mat.specularColor.g,Mat.specularColor.b);
        pcPointMaterial->emissiveColor.setValue(Mat.emissiveColor.r,Mat.emissiveColor.g,Mat.emissiveColor.b);
        pcPointMaterial->shininess.setValue(Mat.shininess);
        pcPointMaterial->transparency.setValue(Mat.transparency);
    }
    else if (prop == &PointColorArray) {
        setHighlightedPoints(PointColorArray.getValues());
        Gui::ColorUpdater::addObject(getObject());
    }
    else if (prop == &LineColorArray) {
        setHighlightedEdges(LineColorArray.getValues());
        Gui::ColorUpdater::addObject(getObject());
    }
    else if (prop == &DiffuseColor) {
        // Only a touch() reaches this now -- a write to DiffuseColor lands in
        // ShapeAppearance and is announced there, by the branch below.
        applyShapeAppearance();
        Gui::ColorUpdater::addObject(getObject());
    }
    else if (prop == &ShapeAppearance) {
        // The appearance changed, whichever name it arrived under. The
        // base class pushes a single appearance into the Coin material node;
        // a per-face one is carried by this view provider's own material
        // arrays, which is what applyShapeAppearance fills in -- colours
        // alone while diffuse is the only varying field, whole materials
        // once any other field varies per face.
        applyShapeAppearance();
        Gui::ColorUpdater::addObject(getObject());
    }
    else if(prop == &ShapeColor) {
        if(!ShapeColor.testStatus(App::Property::User3)) {
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard(
                    App::Property::User3, &ShapeColor);
            // The base class writes it into the appearance's BASE, which is
            // where an object colour belongs: the faces holding one of their
            // own keep it (docs/ShapeAppearanceDesign.md 12.2). This used to
            // push a single colour through DiffuseColor as well, which
            // COLLAPSED every painted face to it -- harmless while the
            // mirror only refreshed on a uniform list, and destructive now
            // that it follows the base whatever the faces hold.
            ViewProviderGeometryObject::onChanged(prop);
            updateColors();
        }
        return;
    }
    else if (prop == &Transparency) {
        long value = (long)(100*ShapeAppearance.getBase().transparency);
        if (value != Transparency.getValue()) {
            float trans = Transparency.getValue()/100.0f;
            // One write: a transparency IS the diffuse alphas now, so this
            // both sets every face and announces once through the
            // ShapeAppearance branch above. The old form had to detach the
            // property from its container to write one of its two stores
            // without the other seeing, then push DiffuseColor by hand --
            // the duplication that store paid for, and the reason it is gone.
            ShapeAppearance.setTransparency(trans);
            if(!prop->testStatus(App::Property::User3)
                    && (MapTransparency.getValue() || MappedColors.getSize()))
                updateColors();
        }
    }
    else if (prop == &Lighting) {
        if (Lighting.getValue() == 0)
            pShapeHints->vertexOrdering = SoShapeHints::UNKNOWN_ORDERING;
        else
            pShapeHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    }
    else if (prop == &DrawStyle) {
        if (DrawStyle.getValue() == 0)
            pcLineStyle->linePattern = 0xffff;
        else if (DrawStyle.getValue() == 1)
            pcLineStyle->linePattern = 0xf00f;
        else if (DrawStyle.getValue() == 2)
            pcLineStyle->linePattern = 0x0f0f;
        else
            pcLineStyle->linePattern = 0xff88;
    }
    else {
        // if the object was invisible and has been changed, recreate the visual
        if (prop == &Visibility && (isUpdateForced() || Visibility.getValue()) && VisualTouched) {
            updateVisual();
        }
    }

    ViewProviderGeometryObject::onChanged(prop);
}

bool ViewProviderPartExt::allowOverride(const App::DocumentObject &) const {
    // Many derived view providers still uses static_cast to get object
    // pointer, so check for exact type here.
    return is<ViewProviderPartExt>();
}

void ViewProviderPartExt::attach(App::DocumentObject *pcFeat)
{
    // call parent attach method
    ViewProviderGeometryObject::attach(pcFeat);

    if (auto feat = Base::freecad_dynamic_cast<Part::Feature>(pcFeat)) {
        conn = feat->signalMapShapeColors.connect([this](App::Document *doc) {
            updateColors(doc, true);
        });
    }

    // Workaround for #0000433, i.e. use SoSeparator instead of SoGroup
    auto* pcNormalRoot = new SoSeparator();
    pcNormalRoot->setName("NormalRoot");
    auto* pcFlatRoot = new SoSeparator();
    pcFlatRoot->setName("FlatRoot");
    auto* pcWireframeRoot = new SoSeparator();
    pcWireframeRoot->setName("WireframeRoot");
    auto* pcPointsRoot = new SoSeparator();
    pcPointsRoot->setName("PointsRoot");
    auto* wireframe = new SoSeparator();

    // Must turn off all intermediate render caching, and let pcRoot to handle
    // cache without interference.
    pcNormalRoot->renderCaching =
        pcFlatRoot->renderCaching =
        pcWireframeRoot->renderCaching =
        pcPointsRoot->renderCaching =
        wireframe->renderCaching = SoSeparator::OFF;

    pcNormalRoot->boundingBoxCaching =
        pcFlatRoot->boundingBoxCaching =
        pcWireframeRoot->boundingBoxCaching =
        pcPointsRoot->boundingBoxCaching =
        wireframe->boundingBoxCaching = SoSeparator::OFF;

    // Avoid any Z-buffer artifacts, so that the lines always appear on top of the faces
    // The correct order is Edges, Polygon offset, Faces.
    SoPolygonOffset* offset = new SoPolygonOffset();

    // wireframe node
    wireframe->setName("Edge");
    wireframe->addChild(pcLineBind);
    wireframe->addChild(pcLineMaterial);
    wireframe->addChild(pcLineStyle);
    wireframe->addChild(lineset);

    // normal viewing with edges and points
    pcNormalRoot->addChild(offset);
    pcNormalRoot->addChild(pcFlatRoot);
    pcNormalRoot->addChild(wireframe);
    pcNormalRoot->addChild(pcPointsRoot);

    // just faces with no edges or points
    pcFlatRoot->addChild(pShapeHints);
    pcFlatRoot->addChild(pcFaceBind);
    pcFlatRoot->addChild(pcShapeMaterial);
    SoDrawStyle* pcFaceStyle = new SoDrawStyle();
    pcFaceStyle->setName("FaceStyle");
    pcFaceStyle->style = SoDrawStyle::FILLED;
    pcFlatRoot->addChild(pcFaceStyle);
    pcFlatRoot->addChild(norm);
    pcFlatRoot->addChild(normb);
    pcFlatRoot->addChild(texcoords);
    pcFlatRoot->addChild(faceset);

    // Roots of the TShape-instanced representation (updateVisual fills
    // them and empties the flat nodes above when instancing is active).
    pFaceInstRoot = new SoGroup;
    pcFlatRoot->addChild(pFaceInstRoot);
    pEdgeInstRoot = new SoGroup;
    wireframe->addChild(pEdgeInstRoot);
    pVertexInstRoot = new SoGroup;

    // edges and points
    pcWireframeRoot->addChild(wireframe);
    pcWireframeRoot->addChild(pcPointsRoot);

    // normal viewing with edges and points
    auto pnormb = new SoNormalBinding;
    pnormb->value = SoNormalBinding::OVERALL;
    pcPointsRoot->addChild(pnormb);
    pcPointsRoot->addChild(pcoords);
    pcPointsRoot->addChild(pcPointBind);
    pcPointsRoot->addChild(pcPointMaterial);
    pcPointsRoot->addChild(pcPointStyle);
    pcPointsRoot->addChild(nodeset);
    pcPointsRoot->addChild(pVertexInstRoot);

    // Move 'coords' before the switch
    pcRoot->insertChild(coords,pcRoot->findChild(pcModeSwitch));

    // putting all together with the switch
    addDisplayMaskMode(pcNormalRoot, "Flat Lines");
    pFaceEdgeRoot = pcNormalRoot;
    addDisplayMaskMode(pcFlatRoot, "Shaded");
    pFaceRoot = pcFlatRoot;
    addDisplayMaskMode(pcWireframeRoot, "Wireframe");
    pEdgeRoot = pcWireframeRoot;
    addDisplayMaskMode(pcPointsRoot, "Points");
    pVertexRoot = pcPointsRoot;
}

void ViewProviderPartExt::setDisplayMode(const char* ModeName)
{
    if ( strcmp("Flat Lines",ModeName)==0 )
        setDisplayMaskMode("Flat Lines");
    else if ( strcmp("Shaded",ModeName)==0 )
        setDisplayMaskMode("Shaded");
    else if ( strcmp("Wireframe",ModeName)==0 )
        setDisplayMaskMode("Wireframe");
    else if ( strcmp("Points",ModeName)==0 )
        setDisplayMaskMode("Points");

    ViewProviderGeometryObject::setDisplayMode( ModeName );
}

std::vector<std::string> ViewProviderPartExt::getDisplayModes() const
{
    // get the modes of the father
    std::vector<std::string> StrList = ViewProviderGeometryObject::getDisplayModes();

    // add your own modes
    StrList.emplace_back("Flat Lines");
    StrList.emplace_back("Shaded");
    StrList.emplace_back("Wireframe");
    StrList.emplace_back("Points");

    return StrList;
}

Part::TopoShape ViewProviderPartExt::getShape() const
{
    Part::TopoShape shape;
    if (!isAttachedToDocument() || !getObject())
        return shape;

    auto prop = Base::freecad_dynamic_cast<Part::PropertyPartShape>(
            getObject()->getPropertyByName(getShapePropertyName()));
    if (prop)
        return prop->getShape();
    return Part::Feature::getTopoShape(getObject());
}

void ViewProviderPartExt::setShapePropertyName(const char *propName)
{
    if(propName)
        shapePropName = propName;
    else
        shapePropName.clear();
    if (isUpdateForced()||Visibility.getValue())
        updateVisual();
    else
        VisualTouched = true;
}

const char * ViewProviderPartExt::getShapePropertyName() const
{
    return shapePropName.empty()?"Shape":shapePropName.c_str();
}

bool ViewProviderPartExt::getElementPicked(const SoPickedPoint *pp, std::string &subname) const
{
    const SoDetail *detail = pp->getDetail();
    if (!detail)
        return inherited::getElementPicked(pp,subname);

    std::ostringstream ss;
    auto node = pp->getPath()->getTail();

    // TShape-instanced representation: the tail is a shared face/edge/
    // point set; the picked instance comes from its wrapper separator in
    // the pick path, whose element bases turn the local index global.
    if (instanced) {
        auto git = instanced->nodeToGeom.find(node);
        if (git != instanced->nodeToGeom.end()) {
            const SoFullPath *path =
                static_cast<const SoFullPath *>(pp->getPath());
            const ShapeInstanceRep::Instance *inst = nullptr;
            for (int i = path->getLength() - 1; i >= 0; --i) {
                auto sit = instanced->sepToInstance.find(path->getNode(i));
                if (sit != instanced->sepToInstance.end()) {
                    inst = &instanced->instances[sit->second];
                    break;
                }
            }
            if (inst && inst->geom == git->second) {
                const InstGeometry *geom = git->second;
                if ((node == geom->faceset
                        || (inst->variant && node == inst->variant->shape))
                        && detail->isOfType(SoFaceDetail::getClassTypeId())) {
                    int face = static_cast<const SoFaceDetail*>(detail)
                                   ->getPartIndex() + 1;
                    ss << "Face" << (inst->faceBase + face);
                } else if ((node == geom->lineset
                            || (inst->lineVariant
                                && node == inst->lineVariant->shape))
                        && detail->isOfType(SoLineDetail::getClassTypeId())) {
                    int edge = static_cast<const SoLineDetail*>(detail)
                                   ->getLineIndex() + 1;
                    ss << "Edge" << (inst->edgeBase + edge);
                } else if ((node == geom->nodeset
                            || (inst->pointVariant
                                && node == inst->pointVariant->shape))
                        && detail->isOfType(SoPointDetail::getClassTypeId())) {
                    int vertex = static_cast<const SoPointDetail*>(detail)
                                     ->getCoordinateIndex()
                        - geom->nodeset->startIndex.getValue() + 1;
                    ss << "Vertex" << (inst->vertexBase + vertex);
                } else
                    return inherited::getElementPicked(pp, subname);
                subname = ss.str();
                return true;
            }
        }
        // fall through: not one of ours (or no instance on the path)
    }

    if (node == faceset && detail->isOfType(SoFaceDetail::getClassTypeId())) {
        const SoFaceDetail* face_detail = static_cast<const SoFaceDetail*>(detail);
        int face = face_detail->getPartIndex() + 1;
        ss << "Face" << face;
    } else if (node == lineset && detail->isOfType(SoLineDetail::getClassTypeId())) {
        const SoLineDetail* line_detail = static_cast<const SoLineDetail*>(detail);
        int edge = line_detail->getLineIndex() + 1;
        ss << "Edge" << edge;
    } else if (node == nodeset && detail->isOfType(SoPointDetail::getClassTypeId())) {
        const SoPointDetail* point_detail = static_cast<const SoPointDetail*>(detail);
        int vertex = point_detail->getCoordinateIndex() - nodeset->startIndex.getValue() + 1;
        ss << "Vertex" << vertex;
    } else
        return inherited::getElementPicked(pp,subname);

    subname = ss.str();
    return true;
}

std::string ViewProviderPartExt::getElement(const SoDetail *detail) const
{
    // This function is providered here for backward compatibility. It provides
    // mostly the same function as getElementPick(), but cannot work with exotic
    // viewproviders that also group other nodes here.
    if (!detail)
        return inherited::getElement(detail);

    // TShape-instanced representation: a bare detail carries a local
    // part index of an unknown instance -- unresolvable without the pick
    // path; getElementPicked is the reliable route.
    if (instanced)
        return inherited::getElement(detail);

    std::ostringstream ss;
    if (detail->isOfType(SoFaceDetail::getClassTypeId())) {
        const SoFaceDetail* face_detail = static_cast<const SoFaceDetail*>(detail);
        int face = face_detail->getPartIndex() + 1;
        ss << "Face" << face;
    } else if (detail->isOfType(SoLineDetail::getClassTypeId())) {
        const SoLineDetail* line_detail = static_cast<const SoLineDetail*>(detail);
        int edge = line_detail->getLineIndex() + 1;
        ss << "Edge" << edge;
    } else if (detail->isOfType(SoPointDetail::getClassTypeId())) {
        const SoPointDetail* point_detail = static_cast<const SoPointDetail*>(detail);
        int vertex = point_detail->getCoordinateIndex() - nodeset->startIndex.getValue() + 1;
        ss << "Vertex" << vertex;
    } else
        return inherited::getElement(detail);

    return ss.str();
}

bool ViewProviderPartExt::getDetailPath(const char *subname,
                                    SoFullPath *pPath,
                                    bool append,
                                    SoDetail *&det) const
{
    auto subelement = Data::findElementName(subname);
    if (!subelement || subelement != subname)
        return inherited::getDetailPath(subname, pPath, append, det);

    if (Data::hasMissingElement(subname))
        return false;

    if(pcRoot->findChild(pcModeSwitch) < 0) {
        // this is possible in case of editing, where the switch node of the
        // linked view object is temporarily removed from its root. We must
        // still return true here, to prevent the selection action leaking to
        // parent and sibling nodes.
        if(append)
            pPath->append(pcRoot);
        return true;
    }

    if (append) {
        pPath->append(pcRoot);
        pPath->append(pcModeSwitch);
    }

    // TShape-instanced representation: per-instance sub-element
    // highlight. The instance wrappers are SoFCSelectionRoot and the
    // selection contexts key on the traversed selection-root stack, so
    // a path ending at the picked instance's wrapper binds the detail's
    // context to that instance alone; the detail carries the LOCAL
    // element index of the shared (or variant) shape node. Anything
    // unresolvable degrades to whole-object highlight.
    if (instanced) {
        const auto &shape = getShape();
        Data::IndexedName element = shape.getElementName(subelement).index;
        auto res = shape.shapeTypeAndIndex(element);
        if (!res.second)
            return true;
        const ShapeInstanceRep::Instance *inst = nullptr;
        int local = 0;
        for (const auto &i : instanced->instances) {
            int base = 0, count = 0;
            switch (res.first) {
            case TopAbs_FACE:
                base = i.faceBase; count = i.geom->faceCount; break;
            case TopAbs_EDGE:
                base = i.edgeBase; count = i.geom->edgeCount; break;
            case TopAbs_VERTEX:
                base = i.vertexBase; count = i.geom->vertexCount; break;
            default:
                return true;   // whole sub-shapes: whole-object
            }
            if (res.second > base && res.second <= base + count) {
                inst = &i;
                local = res.second - base;
                break;
            }
        }
        if (!inst)
            return true;
        SoSeparator *wrapper = res.first == TopAbs_FACE ? inst->faceSep
            : res.first == TopAbs_EDGE ? inst->edgeSep
                                       : inst->vertexSep;
        // Append the graph chain from the mode switch down to the
        // wrapper. The chain crosses display-mode roots (plain
        // separators -- they never enter the context stack, so any of
        // the wrapper's parent paths keys identically); a search keeps
        // this independent of the mode graph layout.
        SoSearchAction sa;
        sa.setNode(wrapper);
        sa.setSearchingAll(true);
        sa.apply(pcModeSwitch);
        SoFullPath *found = static_cast<SoFullPath *>(sa.getPath());
        if (!found || found->getLength() < 2)
            return true;
        for (int i = 1; i < found->getLength(); ++i) {
            SoNode *next = found->getNode(i);
            SoNode *tail = pPath->getTail();
            auto group = tail ? tail->getChildren() : nullptr;
            if (!group || group->find(next) < 0)
                return true;   // tail mismatch (e.g. Link snapshot type)
            pPath->append(next);
        }
        switch (res.first) {
        case TopAbs_FACE: {
            auto fdet = new SoFCFaceDetail;
            det = fdet;
            fdet->setPartIndex(local - 1);
            fdet->setContext(inst->variant ? inst->variant->shape
                                           : inst->geom->faceset);
            break;
        }
        case TopAbs_EDGE: {
            auto ldet = new SoFCLineDetail;
            det = ldet;
            ldet->setLineIndex(local - 1);
            ldet->setContext(inst->lineVariant ? inst->lineVariant->shape
                                               : inst->geom->lineset);
            break;
        }
        default: {
            auto pdet = new SoFCPointDetail;
            det = pdet;
            pdet->setCoordinateIndex(
                local + inst->geom->nodeset->startIndex.getValue() - 1);
            pdet->setContext(inst->pointVariant ? inst->pointVariant->shape
                                                : inst->geom->nodeset);
            break;
        }}
        return true;
    }

    const auto &shape = getShape();
    Data::IndexedName element = shape.getElementName(subelement).index;
    auto res = shape.shapeTypeAndIndex(element);
    if(!res.second) {
        // no SoDetail provided, which cause full selection
        return true;
    }

    Part::TopoShape subshape = shape.getSubTopoShape(res.first, res.second, true);
    if(subshape.isNull())
        return true;

    switch(res.first) {
    case TopAbs_FACE:
        if (!highlightFaceEdges) {
            auto fdet = new SoFCFaceDetail;
            det = fdet;
            fdet->setPartIndex(res.second - 1);
            fdet->setContext(faceset);
        } else {
            auto fdet = new SoFCDetail;
            det = fdet;
            fdet->addIndex(SoFCDetail::Face, res.second-1);
            for(auto &s : subshape.getSubShapes(TopAbs_EDGE)) {
                int idx = shape.findShape(s);
                if(idx>0)
                    fdet->addIndex(SoFCDetail::Edge, idx-1);
            }
            fdet->setContext(SoFCDetail::Face, faceset);
        }
        break;
    case TopAbs_EDGE:
        det = new SoFCLineDetail();
        static_cast<SoFCLineDetail*>(det)->setLineIndex(res.second - 1);
        static_cast<SoFCLineDetail*>(det)->setContext(lineset);
        break;
    case TopAbs_VERTEX:
        det = new SoFCPointDetail();
        static_cast<SoFCPointDetail*>(det)->setCoordinateIndex(res.second + nodeset->startIndex.getValue() - 1);
        static_cast<SoFCPointDetail*>(det)->setContext(nodeset);
        break;
    default: {
        auto fcDetail = new SoFCDetail;
        det = fcDetail;
        for(auto &s : subshape.getSubShapes(TopAbs_FACE)) {
            int index = shape.findShape(s);
            if(index>0)
                fcDetail->addIndex(SoFCDetail::Face, index-1);
        }
        fcDetail->setContext(SoFCDetail::Face, faceset);
        for(auto &s : subshape.getSubShapes(TopAbs_EDGE)) {
            int index = shape.findShape(s);
            if(index>0)
                fcDetail->addIndex(SoFCDetail::Edge, index-1);
        }
        fcDetail->setContext(SoFCDetail::Edge, lineset);
        for(auto &s : subshape.getSubShapes(TopAbs_VERTEX)) {
            int index = shape.findShape(s);
            if(index>0)
                fcDetail->addIndex(SoFCDetail::Vertex, index-1);
        }
        fcDetail->setContext(SoFCDetail::Vertex, nodeset);
    }}
    return true;
}

SoDetail* ViewProviderPartExt::getDetail(const char* subelement) const
{
    // TShape-instanced representation: no per-instance details (see
    // getDetailPath) -- null causes whole-object treatment.
    if (instanced)
        return nullptr;

    const auto &shape = getShape();
    Data::IndexedName element = shape.getElementName(subelement).index;
    auto res = shape.shapeTypeAndIndex(element);
    if(!res.second)
        return nullptr;

    Part::TopoShape subshape = shape.getSubTopoShape(res.first, res.second, true);
    if(subshape.isNull())
        return nullptr;

    SoDetail *detail = nullptr;
    switch(res.first) {
    case TopAbs_FACE:
        if (!highlightFaceEdges) {
            detail = new SoFaceDetail();
            static_cast<SoFaceDetail*>(detail)->setPartIndex(res.second - 1);
        } else {
            detail = new SoFCDetail;
            static_cast<SoFCDetail*>(detail)->addIndex(SoFCDetail::Face, res.second-1);
            for(auto &s : subshape.getSubShapes(TopAbs_EDGE)) {
                int idx = shape.findShape(s);
                if(idx>0)
                    static_cast<SoFCDetail*>(detail)->addIndex(SoFCDetail::Edge, idx-1);
            }
        }
        break;
    case TopAbs_EDGE:
        detail = new SoLineDetail();
        static_cast<SoLineDetail*>(detail)->setLineIndex(res.second - 1);
        break;
    case TopAbs_VERTEX:
        detail = new SoPointDetail();
        static_cast<SoPointDetail*>(detail)->setCoordinateIndex(res.second + nodeset->startIndex.getValue() - 1);
        break;
    default:
        detail = new SoFCDetail;
        for(auto &s : subshape.getSubShapes(TopAbs_FACE)) {
            int index = shape.findShape(s);
            if(index>0)
                static_cast<SoFCDetail*>(detail)->addIndex(SoFCDetail::Face, index-1);
        }
        for(auto &s : subshape.getSubShapes(TopAbs_EDGE)) {
            int index = shape.findShape(s);
            if(index>0)
                static_cast<SoFCDetail*>(detail)->addIndex(SoFCDetail::Edge, index-1);
        }
        for(auto &s : subshape.getSubShapes(TopAbs_VERTEX)) {
            int index = shape.findShape(s);
            if(index>0)
                static_cast<SoFCDetail*>(detail)->addIndex(SoFCDetail::Vertex, index-1);
        }
        break;
    }
    return detail;
}

void ViewProviderPartExt::setHighlightFaceEdges(bool enable)
{
    highlightFaceEdges = enable;
}

std::vector<Base::Vector3d> ViewProviderPartExt::getModelPoints(const SoPickedPoint* pp) const
{
    try {
        std::vector<Base::Vector3d> pts;
        std::string element;
        this->getElementPicked(pp, element);
        const auto &shape = getShape();

        TopoDS_Shape subShape = shape.getSubShape(element.c_str());

        // get the point of the vertex directly
        if (subShape.ShapeType() == TopAbs_VERTEX) {
            const TopoDS_Vertex& v = TopoDS::Vertex(subShape);
            gp_Pnt p = BRep_Tool::Pnt(v);
            pts.emplace_back(p.X(),p.Y(),p.Z());
        }
        // get the nearest point on the edge
        else if (subShape.ShapeType() == TopAbs_EDGE) {
            const SbVec3f& vec = pp->getPoint();
            BRepBuilderAPI_MakeVertex mkVert(gp_Pnt(vec[0],vec[1],vec[2]));
            BRepExtrema_DistShapeShape distSS(subShape, mkVert.Vertex(), 0.1);
            if (distSS.NbSolution() > 0) {
                gp_Pnt p = distSS.PointOnShape1(1);
                pts.emplace_back(p.X(),p.Y(),p.Z());
            }
        }
        // get the nearest point on the face
        else if (subShape.ShapeType() == TopAbs_FACE) {
            const SbVec3f& vec = pp->getPoint();
            BRepBuilderAPI_MakeVertex mkVert(gp_Pnt(vec[0],vec[1],vec[2]));
            BRepExtrema_DistShapeShape distSS(subShape, mkVert.Vertex(), 0.1);
            if (distSS.NbSolution() > 0) {
                gp_Pnt p = distSS.PointOnShape1(1);
                pts.emplace_back(p.X(),p.Y(),p.Z());
            }
        }

        return pts;
    }
    catch (...) {
    }

    // if something went wrong returns an empty array
    return {};
}

std::vector<Base::Vector3d> ViewProviderPartExt::getSelectionShape(const char* /*Element*/) const
{
    return {};
}

// Per-face material divergence the instanced representation cannot carry:
// the color variants bake diffuse+transparency only; every other component
// must stay uniform in value to ride the inherited object material.
static bool materialsUnrepresentable(const std::vector<App::MaterialAppearance> &mats)
{
    for (size_t i = 1; i < mats.size(); ++i) {
        if (mats[i].ambientColor != mats[0].ambientColor
                || mats[i].specularColor != mats[0].specularColor
                || mats[i].emissiveColor != mats[0].emissiveColor
                || mats[i].shininess != mats[0].shininess
                // A per-face surface finish is carried by the material
                // index, which only means the face while the shape
                // binds its materials per part -- an instanced
                // representation states one material per instance and
                // would quietly drop every face's finish but the first.
                || mats[i].finish != mats[0].finish)
            return true;
    }
    return false;
}

void ViewProviderPartExt::applyShapeAppearance()
{
    // The colour path serves PBR mode too: the Phong reading keeps the
    // diffuse as the base colour (see getPhongMaterial), so a list whose
    // other fields are uniform is still a colour list here.
    if (ShapeAppearance.variesOnlyInDiffuse()) {
        setHighlightedFaces(DiffuseColor.getValues());
        return;
    }
    int count = ShapeAppearance.getSize();
    std::vector<App::MaterialAppearance> mats;
    mats.reserve(count);
    for (int i = 0; i < count; ++i)
        mats.push_back(ShapeAppearance.getPhongMaterial(i));
    setHighlightedFaces(mats);
}

void ViewProviderPartExt::setHighlightedFaces(const std::vector<App::Color>& colors)
{
    // Not during a restore: the eager path touched and then had the touch
    // purged by afterRestore; the deferred drain runs after that purge, so
    // the touch would survive and a document would open already modified.
    if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange)
            && !App::Document::isAnyRestoring())
        getObject()->touch(true);

    // Any per-face color VECTOR is representable by the instanced
    // representation since the color-variant layer; the divergence flag
    // only tracks unrepresentable per-face MATERIAL divergence (raised
    // by the material overload below). A plain color apply clears it.
    if (appliedFaceColorsDivergent) {
        appliedFaceColorsDivergent = false;
        if (!instanced && instancingCandidate())
            VisualTouched = true;   // may re-qualify; rebuilds lazily
    }
    if (instanced) {
        applyInstancedFaceColors(colors);
        return;
    }

    Gui::SoUpdateVBOAction action;
    action.apply(this->faceset);
    // That traversal touches the face set (SoBrepFaceSet::doAction
    // calls touch() to force the VBO refresh), which would void the
    // vertex-cache entry the last rebuild registered for it. Only the
    // stamp goes stale here -- the geometry this appearance apply left
    // alone is exactly what the entry mirrors -- so move the stamp
    // rather than lose the content (docs/WorkerVertexCache.md).
    SoFCVertexCache::restamp(this->faceset);

    // A colour vector varies diffuse+transparency only; the other fields
    // come from the document appearance's entry 0. Pushed here, not only
    // by the base class (which pushes a single-ENTRY appearance), so that
    // a multi-entry appearance whose non-diffuse fields are uniform still
    // reaches the node -- and so that any per-face arrays a previous
    // whole-material apply left there collapse back to scalars.
    {
        const App::MaterialAppearance m = ShapeAppearance.getPhongBase();
        const SbColor ambient(m.ambientColor.r, m.ambientColor.g, m.ambientColor.b);
        const SbColor specular(m.specularColor.r, m.specularColor.g, m.specularColor.b);
        const SbColor emissive(m.emissiveColor.r, m.emissiveColor.g, m.emissiveColor.b);
        if (pcShapeMaterial->ambientColor.getNum() != 1
                || pcShapeMaterial->ambientColor[0] != ambient)
            pcShapeMaterial->ambientColor.setValue(ambient);
        if (pcShapeMaterial->specularColor.getNum() != 1
                || pcShapeMaterial->specularColor[0] != specular)
            pcShapeMaterial->specularColor.setValue(specular);
        if (pcShapeMaterial->emissiveColor.getNum() != 1
                || pcShapeMaterial->emissiveColor[0] != emissive)
            pcShapeMaterial->emissiveColor.setValue(emissive);
        if (pcShapeMaterial->shininess.getNum() != 1
                || pcShapeMaterial->shininess[0] != m.shininess)
            pcShapeMaterial->shininess.setValue(m.shininess);
    }

    int size = static_cast<int>(colors.size());
    if (size > 1) {
        int numfaces = this->faceset->partIndex.getNum();
        if(size > numfaces)
            size = numfaces;
        pcFaceBind->value = SoMaterialBinding::PER_PART;
        pcShapeMaterial->diffuseColor.setNum(numfaces);
        pcShapeMaterial->transparency.setNum(numfaces);
        SbColor* ca = pcShapeMaterial->diffuseColor.startEditing();
        float *t = pcShapeMaterial->transparency.startEditing();
        int i=0;
        for (; i < size; i++) {
            ca[i].setValue(colors[i].r, colors[i].g, colors[i].b);
            t[i] = colors[i].transparency();
        }
        const auto &color = ShapeColor.getValue();
        float trans = ShapeAppearance.getBase().transparency;
        for (; i < numfaces; i++) { 
            ca[i].setValue(color.r, color.g, color.b);
            t[i] = trans;
        }
        pcShapeMaterial->diffuseColor.finishEditing();
        pcShapeMaterial->transparency.finishEditing();
        return;
    }

    const auto &color = colors.size()==1?colors[0]:ShapeColor.getValue();
    pcFaceBind->value = SoMaterialBinding::OVERALL;
    pcShapeMaterial->diffuseColor.setValue(color.r, color.g, color.b);
    //pcShapeMaterial->transparency = colors[0].a; do not get transparency from DiffuseColor in this case
    pcShapeMaterial->transparency.setValue(ShapeAppearance.getBase().transparency);

}

void ViewProviderPartExt::setHighlightedFaces(const std::vector<App::MaterialAppearance>& colors)
{
    // Not during a restore, for the same reason as the colour overload.
    if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange)
            && !App::Document::isAnyRestoring())
        getObject()->touch(true);

    // Instanced representation: diffuse+transparency divergence goes
    // through the color-variant path; anything beyond that must bake
    // whole materials per face -- rebuild flattened (the raised flag
    // blocks re-instancing until a representable apply clears it).
    bool divergent = materialsUnrepresentable(colors);
    if (divergent != appliedFaceColorsDivergent) {
        appliedFaceColorsDivergent = divergent;
        if (divergent) {
            if (instanced)
                updateVisual();
        } else if (!instanced && instancingCandidate())
            VisualTouched = true;
    }
    if (instanced) {
        // The uniform-valued non-diffuse components ride the object
        // material; diffuse+transparency partition the instances.
        const App::MaterialAppearance m0 = colors.empty() ? ShapeAppearance.getPhongBase() : colors[0];
        pcShapeMaterial->ambientColor.setValue(
            m0.ambientColor.r, m0.ambientColor.g, m0.ambientColor.b);
        pcShapeMaterial->specularColor.setValue(
            m0.specularColor.r, m0.specularColor.g, m0.specularColor.b);
        pcShapeMaterial->emissiveColor.setValue(
            m0.emissiveColor.r, m0.emissiveColor.g, m0.emissiveColor.b);
        pcShapeMaterial->shininess.setValue(m0.shininess);
        std::vector<App::Color> diffuse;
        diffuse.reserve(colors.size());
        for (const auto &m : colors) {
            App::Color c = m.diffuseColor;
            c.setTransparency(m.transparency);
            diffuse.push_back(c);
        }
        applyInstancedFaceColors(diffuse);
        return;
    }

    Gui::SoUpdateVBOAction action;
    action.apply(this->faceset);
    // That traversal touches the face set (SoBrepFaceSet::doAction
    // calls touch() to force the VBO refresh), which would void the
    // vertex-cache entry the last rebuild registered for it. Only the
    // stamp goes stale here -- the geometry this appearance apply left
    // alone is exactly what the entry mirrors -- so move the stamp
    // rather than lose the content (docs/WorkerVertexCache.md).
    SoFCVertexCache::restamp(this->faceset);

    int size = static_cast<int>(colors.size());
    if (size > 1) {
        int numfaces = this->faceset->partIndex.getNum();
        if(size > numfaces)
            size = numfaces;

        pcFaceBind->value = SoMaterialBinding::PER_PART;

        pcShapeMaterial->diffuseColor.setNum(numfaces);
        pcShapeMaterial->ambientColor.setNum(numfaces);
        pcShapeMaterial->specularColor.setNum(numfaces);
        pcShapeMaterial->emissiveColor.setNum(numfaces);
        pcShapeMaterial->shininess.setNum(numfaces);
        pcShapeMaterial->transparency.setNum(numfaces);

        SbColor* dc = pcShapeMaterial->diffuseColor.startEditing();
        SbColor* ac = pcShapeMaterial->ambientColor.startEditing();
        SbColor* sc = pcShapeMaterial->specularColor.startEditing();
        SbColor* ec = pcShapeMaterial->emissiveColor.startEditing();
        float* sh = pcShapeMaterial->shininess.startEditing();
        float* tr = pcShapeMaterial->transparency.startEditing();

        int i=0;
        for (; i < size; i++) {
            dc[i].setValue(colors[i].diffuseColor.r, colors[i].diffuseColor.g, colors[i].diffuseColor.b);
            ac[i].setValue(colors[i].ambientColor.r, colors[i].ambientColor.g, colors[i].ambientColor.b);
            sc[i].setValue(colors[i].specularColor.r, colors[i].specularColor.g, colors[i].specularColor.b);
            ec[i].setValue(colors[i].emissiveColor.r, colors[i].emissiveColor.g, colors[i].emissiveColor.b);
            sh[i] = colors[i].shininess;
            tr[i] = colors[i].transparency;
        }

        // The BASE: the faces this short apply leaves unstated wear what
        // the object wears, which is exactly what the base is
        const App::MaterialAppearance material = ShapeAppearance.getBase();
        for (; i < numfaces; ++i) {
            dc[i].setValue(material.diffuseColor.r, material.diffuseColor.g, material.diffuseColor.b);
            ac[i].setValue(material.ambientColor.r, material.ambientColor.g, material.ambientColor.b);
            sc[i].setValue(material.specularColor.r, material.specularColor.g, material.specularColor.b);
            ec[i].setValue(material.emissiveColor.r, material.emissiveColor.g, material.emissiveColor.b);
            sh[i] = material.shininess;
            tr[i] = material.transparency;
        }

        pcShapeMaterial->diffuseColor.finishEditing();
        pcShapeMaterial->ambientColor.finishEditing();
        pcShapeMaterial->specularColor.finishEditing();
        pcShapeMaterial->emissiveColor.finishEditing();
        pcShapeMaterial->shininess.finishEditing();
        pcShapeMaterial->transparency.finishEditing();
        return;
    }

    const App::MaterialAppearance material = colors.size()==1?colors[0]:ShapeAppearance.getBase();
    pcFaceBind->value = SoMaterialBinding::OVERALL;
    pcShapeMaterial->diffuseColor.setValue(material.diffuseColor.r, material.diffuseColor.g, material.diffuseColor.b);
    pcShapeMaterial->ambientColor.setValue(material.ambientColor.r, material.ambientColor.g, material.ambientColor.b);
    pcShapeMaterial->specularColor.setValue(material.specularColor.r, material.specularColor.g, material.specularColor.b);
    pcShapeMaterial->emissiveColor.setValue(material.emissiveColor.r, material.emissiveColor.g, material.emissiveColor.b);
    pcShapeMaterial->shininess.setValue(material.shininess);
    pcShapeMaterial->transparency.setValue(material.transparency);
}

static inline App::PropertyLinkSub *getColoredElements(const App::DocumentObject *obj) {
    if(!obj || !obj->getNameInDocument())
        return 0;
    return Base::freecad_dynamic_cast<App::PropertyLinkSub>(
            obj->getPropertyByName("ColoredElements"));
}

std::map<std::string,App::Color> ViewProviderPartExt::getElementColors(const char *element) const {
    std::map<std::string,App::Color> ret;

    if(!element || !element[0]) {
        auto color = ShapeColor.getValue();
        color.setTransparency(Transparency.getValue()/100.0f);
        ret["Face"] = color;
        ret["Edge"] = LineColor.getValue();
        ret["Vertex"] = PointColor.getValue();

        auto prop = getColoredElements(pcObject);
        if(prop && prop->getValue()==pcObject) {
            const auto &subs = prop->getSubValues();
            const auto &colors = MappedColors.getValues();
            if(subs.size()==colors.size()) {
                for(size_t i=0;i<subs.size();++i)
                    ret.emplace(subs[i],colors[i]);
            }
        }
        return ret;
    }

    std::string tmp;
    if(Data::isMappedElement(element)) {
        Data::IndexedName indexedName = getShape().getElementName(element).index;
        if (indexedName)
            element = indexedName.appendToStringBuffer(tmp);
        else {
            for(auto &mapped : Part::Feature::getRelatedElements(getObject(),element)) {
                tmp.clear();
                for(auto &v : getElementColors(mapped.index.appendToStringBuffer(tmp)))
                    ret.insert(v);
            }
            return ret;
        }
    }

    if(boost::starts_with(element,"Face")) {
        auto size = DiffuseColor.getSize();
        if(element[4]=='*') {
            auto color = ShapeColor.getValue();
            color.setTransparency(Transparency.getValue()/100.0f);
            bool singleColor = true;
            for(int i=0;i<size;++i) {
                if(DiffuseColor[i]!=color)
                    ret[std::string(element,4)+std::to_string(i+1)] = DiffuseColor[i];
                singleColor = singleColor && DiffuseColor[0]==DiffuseColor[i];
            }
            if(size && singleColor) {
                color = DiffuseColor[0];
                color.setTransparency(Transparency.getValue()/100.0f);
                ret.clear();
            }
            ret["Face"] = color;
        }else{
            int idx = atoi(element+4);
            if(idx>0 && idx<=size)
                ret[element] = DiffuseColor[idx-1];
            else
                ret[element] = ShapeColor.getValue();
            if(size==1)
                ret[element].setTransparency(Transparency.getValue()/100.0f);
        }
    } else if (boost::starts_with(element,"Edge")) {
        auto size = LineColorArray.getSize();
        if(element[4]=='*') {
            auto color = LineColor.getValue();
            bool singleColor = true;
            for(int i=0;i<size;++i) {
                if(LineColorArray[i]!=color)
                    ret[std::string(element,4)+std::to_string(i+1)] = LineColorArray[i];
                singleColor = singleColor && LineColorArray[0]==LineColorArray[i];
            }
            if(singleColor && size) {
                color = LineColorArray[0];
                ret.clear();
            }
            ret["Edge"] = color;
        }else{
            int idx = atoi(element+4);
            if(idx>0 && idx<=size)
                ret[element] = LineColorArray[idx-1];
            else
                ret[element] = LineColor.getValue();
        }
    } else if (boost::starts_with(element,"Vertex")) {
        auto size = PointColorArray.getSize();
        if(element[6]=='*') {
            auto color = PointColor.getValue();
            bool singleColor = true;
            for(int i=0;i<size;++i) {
                if(PointColorArray[i]!=color)
                    ret[std::string(element,6)+std::to_string(i+1)] = PointColorArray[i];
                singleColor = singleColor && PointColorArray[0]==PointColorArray[i];
            }
            if(singleColor && size) {
                color = PointColorArray[0];
                ret.clear();
            }
            ret["Vertex"] = color;
        }else{
            int idx = atoi(element+6);
            if(idx>0 && idx<=size)
                ret[element] = PointColorArray[idx-1];
            else
                ret[element] = PointColor.getValue();
        }
    }
    return ret;
}

void ViewProviderPartExt::setElementColors(const std::map<std::string,App::Color> &info) 
{
    auto propColoredElements = getColoredElements(pcObject);
    if(!propColoredElements)
        return;
    std::vector<App::Color> colors;
    std::vector<std::string> subs;
    colors.reserve(info.size());
    subs.reserve(info.size());
    bool touched = false;
    for(auto &v : info) {
        if(v.first == "Face") {
            if(ShapeColor.getValue()!=v.second) {
                touched = true;
                ShapeColor.setValue(v.second);
            }
            if(v.second.transparency()*100 != Transparency.getValue()) {
                Transparency.setValue(v.second.transparency()*100);
                touched = true;
            }
        } else if(v.first == "Edge") {
            if(LineColor.getValue()!=v.second) {
                LineColor.setValue(v.second);
                touched = true;
            }
        } else if(v.first == "Vertex"){
            if(PointColor.getValue()!=v.second) {
                PointColor.setValue(v.second);
                touched = true;
            }
        } else {
            subs.push_back(v.first);
            colors.push_back(v.second);
        }
    }
    if(colors!=MappedColors.getValues()) {
        touched = true;
        Base::ObjectStatusLocker<App::Property::Status,App::Property> guard(
                App::Property::User3, &MappedColors);
        MappedColors.setValues(colors);
    }
    if(subs.empty())
        propColoredElements->setValue(0);
    else if(subs!=propColoredElements->getSubValues())
        propColoredElements->setValue(pcObject,subs);
    else if(touched)
        updateColors();
}

void ViewProviderPartExt::unsetHighlightedFaces()
{
    applyShapeAppearance();
}

void ViewProviderPartExt::setHighlightedEdges(const std::vector<App::Color>& colors)
{
    if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange))
        getObject()->touch(true);

    // Any per-edge color vector is representable by the instanced
    // representation since the line color variants (diffuse-only, like
    // the flattened per-edge path below).
    if (instanced) {
        applyInstancedLineColors(colors);
        return;
    }

    int size = static_cast<int>(colors.size());
    if (size > 1) {
        // Although indexed lineset is used the material binding must be PER_FACE!
        pcLineBind->value = SoMaterialBinding::PER_FACE;
        const int32_t* cindices = this->lineset->coordIndex.getValues(0);
        int numindices = this->lineset->coordIndex.getNum();
        int linecount = 0;
        for (int i = 0; i < numindices; ++i) {
            if (cindices[i] < 0)
                ++linecount;
        }
        pcLineMaterial->diffuseColor.setNum(linecount);
        SbColor* ca = pcLineMaterial->diffuseColor.startEditing();

        if(size > linecount)
            size = linecount;

        int i=0;
        for (; i < size; ++i) 
            ca[i].setValue(colors[i].r, colors[i].g, colors[i].b);

        const auto &color = LineColor.getValue();
        for (; i < linecount; ++i)
            ca[i].setValue(color.r, color.g, color.b);

        pcLineMaterial->diffuseColor.finishEditing();
        return;
    }

    const auto &color = colors.size()==1?colors[0]:LineColor.getValue();
    pcLineBind->value = SoMaterialBinding::OVERALL;
    pcLineMaterial->diffuseColor.setValue(color.r, color.g, color.b);
}

void ViewProviderPartExt::unsetHighlightedEdges()
{
    setHighlightedEdges(LineColorArray.getValues());
}

void ViewProviderPartExt::setHighlightedPoints(const std::vector<App::Color>& colors)
{
    if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange))
        getObject()->touch(true);

    // Any per-vertex color vector is representable by the instanced
    // representation since the point color variants (diffuse-only, like
    // the flattened per-vertex path below).
    if (instanced) {
        applyInstancedPointColors(colors);
        return;
    }

    int size = static_cast<int>(colors.size());
    if (size > 1) {
        int numpoints = pcoords->point.getNum();
        if(size > numpoints)
            size = numpoints;
        pcPointBind->value = SoMaterialBinding::PER_VERTEX;
        pcPointMaterial->diffuseColor.setNum(numpoints);
        SbColor* ca = pcPointMaterial->diffuseColor.startEditing();
        int i=0;
        for (; i < size; ++i)
            ca[i].setValue(colors[i].r, colors[i].g, colors[i].b);
        const auto &color = PointColor.getValue();
        for (; i < numpoints; ++i)
            ca[i].setValue(color.r, color.g, color.b);
        pcPointMaterial->diffuseColor.finishEditing();
        return;
    }

    const auto &color = size==1?colors[0]:PointColor.getValue();
    pcPointBind->value = SoMaterialBinding::OVERALL;
    pcPointMaterial->diffuseColor.setValue(color.r, color.g, color.b);
}

void ViewProviderPartExt::unsetHighlightedPoints()
{
    setHighlightedPoints(PointColorArray.getValues());
}

void ViewProviderPartExt::reload()
{
    bool update = false;
    double pointsize = PointSize.getValue();
    double linewidth = LineWidth.getValue();
    if (PartParams::getRespectSystemDPI()) {
        auto dpi = qApp->devicePixelRatio();
        pointsize = std::max(1.0, pointsize*dpi);
        linewidth = std::max(1.0, linewidth*dpi);
    }
    if (pcPointStyle->pointSize.getValue() != pointsize
            || pcLineStyle->lineWidth.getValue() != linewidth)
    {
        pcPointStyle->pointSize = pointsize;
        pcLineStyle->lineWidth = linewidth;
        update = true;
    }
    if (NormalsFromUV != PartParams::getNormalsFromUVNodes()) {
        update = true;
        NormalsFromUV = !NormalsFromUV;
    }

    tessRange.LowerBound = PartParams::getMinimumDeviation();
    angDeflectionRange.LowerBound = PartParams::getMinimumAngularDeflection();

    if (Deviation.getValue() != PartParams::getMeshDeviation()
            || Deviation.getValue() < PartParams::getMinimumDeviation()
            || AngularDeflection.getValue() != PartParams::getMeshAngularDeflection()
            || AngularDeflection.getValue() < PartParams::getMinimumAngularDeflection())
        update = true;

    if (!update)
        return;

    if (!PartParams::getOverrideTessellation()) {
        Base::ObjectStatusLocker<App::Property::Status,App::Property> guard(
                App::Property::User3, &Deviation);

        Deviation.setValue(PartParams::getMeshDeviation());
        Base::ObjectStatusLocker<App::Property::Status,App::Property> guard2(
                App::Property::User3, &AngularDeflection);
        AngularDeflection.setValue(PartParams::getMeshAngularDeflection());
    }
    updateVisual();
}

namespace {

/** One of the three per-element colour arrays, picked by element type
 *
 * The two places below want "the colour list for this kind of element" and
 * do not care which one it is. They used to hold an App::PropertyColorList
 * pointer to one of DiffuseColor, LineColorArray and PointColorArray, which
 * stops being safe the moment DiffuseColor keeps its colours somewhere else
 * than the base list does: the reads would come back empty, for faces only
 * and without a word. So the branch lives here instead, once, and every
 * access goes through the property's own static type.
 */
class ElementColors
{
public:
    ElementColors() = default;
    ElementColors(TopAbs_ShapeEnum type, ViewProviderPartExt *vp)
        : _type(type), _vp(vp)
    {}

    explicit operator bool() const { return _vp != nullptr; }

    int getSize() const
    {
        switch (_type) {
        case TopAbs_VERTEX:
            return _vp->PointColorArray.getSize();
        case TopAbs_EDGE:
            return _vp->LineColorArray.getSize();
        default:
            return _vp->DiffuseColor.getSize();
        }
    }

    const std::vector<App::Color> &getValues() const
    {
        switch (_type) {
        case TopAbs_VERTEX:
            return _vp->PointColorArray.getValues();
        case TopAbs_EDGE:
            return _vp->LineColorArray.getValues();
        default:
            return _vp->DiffuseColor.getValues();
        }
    }

    void setValues(const std::vector<App::Color> &colors)
    {
        switch (_type) {
        case TopAbs_VERTEX:
            _vp->PointColorArray.setValue(colors);
            break;
        case TopAbs_EDGE:
            _vp->LineColorArray.setValue(colors);
            break;
        default:
            _vp->DiffuseColor.setValue(colors);
            break;
        }
    }

    void touch()
    {
        switch (_type) {
        case TopAbs_VERTEX:
            _vp->PointColorArray.touch();
            break;
        case TopAbs_EDGE:
            _vp->LineColorArray.touch();
            break;
        default:
            _vp->DiffuseColor.touch();
            break;
        }
    }

private:
    TopAbs_ShapeEnum _type = TopAbs_FACE;
    ViewProviderPartExt *_vp = nullptr;
};

}  // namespace

static bool getLinkColor(const Data::MappedName &mapped, App::DocumentObject *&obj,
        ViewProviderPartExt *&svp, App::Color &color)
{
    if(!obj)
        return false;
    bool colorFound = false;
    for(int depth=0;;++depth) {
        auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderLink>(
                Gui::Application::Instance->getViewProvider(obj));
        if(vp && vp->getDefaultMode()==1) {
            svp = Base::freecad_dynamic_cast<ViewProviderPartExt>(
                    vp->ChildViewProvider.getObject().get());
            if(svp)
                return false;
        }
        auto link = obj->getExtensionByType<App::LinkBaseExtension>(true);
        if(vp && vp->OverrideMaterial.getValue()) {
            colorFound = true;
            color = vp->ShapeAppearance.getBase().diffuseColor;
            color.setTransparency(vp->ShapeAppearance.getBase().transparency);
            if(!link || !link->getElementCountValue())
                return true;
        }
        // check for link array
        if(link && !link->getShowElementValue() && link->getElementCountValue()) {
            int pos = mapped.rfind(Data::indexPostfix());
            if(pos >= 0) {
                int offset = pos + static_cast<int>(Data::indexPostfix().size());
                QByteArray bytes = mapped.toRawBytes(offset);
                bio::stream<bio::array_source> iss(bytes.constData(), bytes.size());
                int index = 0;
                char sep = 0;
                iss >> index >> sep;
                if(sep==';' && 
                    vp->OverrideMaterialList.getSize()>index && 
                    vp->OverrideMaterialList[index] &&
                    vp->MaterialList.getSize()>index)
                {
                    color = vp->MaterialList.getDiffuseColor(index);
                    color.setTransparency(vp->MaterialList.getTransparency(index));
                    return true;
                }
                if(colorFound)
                    return colorFound;
                obj = obj->getSubObject((std::to_string(index)+".").c_str());
                return false;
            }
        }
        auto linked = obj->getLinkedObject(false,0,false,depth);
        if(!linked || linked==obj)
            break;
        obj = linked;
    }
    return colorFound;
}

struct ElementCache
{
    Part::TopoShape shape;
    ViewProviderPartExt *vp;
    bool inited = false;
};

static App::Color getElementColor(App::Color color, 
                                  const Part::TopoShape &shape,
                                  App::Document *doc,
                                  int type,
                                  const Data::MappedName &name,
                                  std::map<App::DocumentObject*, ElementCache> &caches)
{
    if (!name)
        return color;

    Data::MappedName mapped(name);
    bool colorFound = false;
    std::vector<Data::MappedName> history;
    std::vector<Data::MappedName> prevHistory;
    Data::MappedName original;
    long tag = shape.getElementHistory(mapped,&original,&prevHistory);
    while(1) {
        if(!tag)
            return color;
        auto obj = doc->getObjectByID(std::abs(tag));
        if(!obj || !obj->getNameInDocument())
            return color;
        auto & cache = caches[obj];
        if (!cache.inited) {
            cache.inited = true;
            cache.shape = Part::Feature::getTopoShape(obj);
            cache.vp = Base::freecad_dynamic_cast<ViewProviderPartExt>(
                    Gui::Application::Instance->getViewProvider(obj));
        }
        const Part::TopoShape & shape = cache.shape;
        ViewProviderPartExt *vp = cache.vp;
        if(shape.isNull() || getLinkColor(original,obj,vp,color) || !obj)
            return color;
        if(!vp) {
            // Not a part view provider. No problem, just trace deeper into the
            // history until we find one.
            doc = obj->getDocument();
            mapped = original;
            prevHistory.clear();
            tag = shape.getElementHistory(mapped,&original,&prevHistory);
            continue;
        }

        if(colorFound)
            return color;

        float trans = vp->Transparency.getValue()/100.0;
        ElementColors prop((TopAbs_ShapeEnum)type, vp);
        if(prop.getSize()==0)
            return color;

        mapped = original;
        // Normally, TopoShape::getElementHistory() returns the mapped element
        // name of the previous step (in 'original'), and tag is the ID of the
        // previous feature. 'mapped' is the mapped element name of the next
        // modeling step in shape history.
        //
        // However, if there are intermediate modeling steps, things get a bit
        // tricky. getElementHistory() returns intermediate element names in
        // 'history', but the last entry of 'history' may actually contain the real
        // mapped element name of the 'current' modeling step. That's why we are
        // calling getElementHistory() for previous modeling step now, before
        // retrieving the element for the current step using getElementName(),
        // because we need to check intermediate history names of the previous
        // model step. The 'original' returned by getElementHistory() here may
        // or may not contain a valid element name for the previous step. We can
        // only decide after another loop hits here.
        std::swap(history, prevHistory);
        prevHistory.clear();
        tag = shape.getElementHistory(mapped,&original,&prevHistory);
        Data::IndexedName indexedName = shape.getIndexedName(mapped);
        if (!indexedName && !history.empty())
           indexedName = shape.getIndexedName(history.back());
        auto idx = Part::TopoShape::shapeTypeAndIndex(indexedName);
        if(idx.second>0 && idx.second<=(int)shape.countSubShapes(idx.first)) {
            if(idx.first==type) {
                if(prop.getSize()==1) {
                    color = prop.getValues()[0];
                    color.setTransparency(trans);
                }
                else if(idx.second<=prop.getSize())
                    return prop.getValues()[idx.second-1];
            }else{
                // This means the element is generated from a different type of source element,
                // e.g. face generated by an edge.
                auto aidx = shape.findAncestor(shape.findShape(idx.first,idx.second),(TopAbs_ShapeEnum)type);
                if(aidx>0) {
                    if(prop.getSize()==1) {
                        color = prop.getValues()[0];
                        color.setTransparency(trans);
                    }
                    else if(aidx<=prop.getSize())
                        return prop.getValues()[aidx-1];
                }
            }
        }
        return color;
    }
}

std::vector<App::Color> ViewProviderPartExt::getShapeColors(const Part::TopoShape &shape, 
        App::Color &defColor, App::Document *sourceDoc, bool linkOnly)
{
    defColor.setPackedValue(Gui::ViewParams::getDefaultShapeColor());
    defColor.a = 1.0f;  // opaque whatever the preference's alpha byte says

    if(!sourceDoc) {
        sourceDoc = App::GetApplication().getActiveDocument();
        if(!sourceDoc)
            return {};
    }
    size_t count = shape.countSubShapes(TopAbs_FACE);
    if(!count)
        return {};

    Data::MappedName mapped = shape.getMappedName(
            Data::IndexedName::fromConst("Face", 1), true);

    ViewProviderPartExt *vp=0;
    auto obj = sourceDoc->getObjectByID(shape.Tag);
    if(getLinkColor(mapped,obj,vp,defColor))
        return {defColor};
    else if(linkOnly || !obj)
        return {};

    if(!vp)
        vp = Base::freecad_dynamic_cast<ViewProviderPartExt>(
                Gui::Application::Instance->getViewProvider(obj));
    if(vp) {
        defColor = vp->ShapeColor.getValue();
        defColor.setTransparency(vp->Transparency.getValue()/100.0f);
        return vp->DiffuseColor.getValues();
    }

    std::vector<App::Color> colors(count,defColor);
    bool touched = false;
    std::map<App::DocumentObject*,ElementCache> caches;
    for(size_t i=0;i<=count;++i) {
        mapped = shape.getMappedName(Data::IndexedName::fromConst("Face", i+1));
        if (mapped) {
            auto color = getElementColor(defColor,shape,sourceDoc,TopAbs_FACE,mapped,caches);
            if(color!=defColor) {
                colors[i] = color;
                touched = true;
            }
        }
    }
    if(!touched)
        return {};
    return colors;
}

bool ViewProviderPartExt::hasBaseFeature() const {
    return !claimChildren().empty();
}

struct ColorInfo {
    TopAbs_ShapeEnum type;
    ElementColors prop;
    App::Color defaultColor;
    std::map<int,App::Color> colors;
    bool mapColor;

    void init(TopAbs_ShapeEnum t, ViewProviderPartExt *vp) {
        type = t;
        prop = ElementColors(t, vp);
        switch(type) {
        case TopAbs_VERTEX:
            defaultColor = vp->PointColor.getValue();
            mapColor = vp->MapPointColor.getValue();
            break;
        case TopAbs_EDGE:
            defaultColor = vp->LineColor.getValue();
            mapColor = vp->MapLineColor.getValue();
            break;
        case TopAbs_FACE:
            defaultColor = vp->ShapeColor.getValue();
            defaultColor.setTransparency(vp->Transparency.getValue()/100.0f);
            mapColor = vp->MapFaceColor.getValue();
            break;
        default:
            assert(0);
        }
    }
};

void ViewProviderPartExt::checkColorUpdate()
{
    if (MapFaceColor.getValue()
            || MapLineColor.getValue()
            || MapPointColor.getValue()
            || MapTransparency.getValue())
        updateColors();
}

void ViewProviderPartExt::updateColors(App::Document *sourceDoc, bool forceColorMap) 
{
    if (UpdatingColor
            || !getObject()
            || !getObject()->getDocument()
            || getObject()->getDocument()->testStatus(App::Document::Restoring))
        return;

    auto geoFeature = Base::freecad_dynamic_cast<App::GeoFeature>(pcObject);

    Base::FlagToggler<> flag(UpdatingColor);
    auto prop = getColoredElements(pcObject);
    if(prop && prop->getSubValues().size()!=(size_t)MappedColors.getSize()) {
        if(prop->getSubValues().size()<(size_t)MappedColors.getSize())
            MappedColors.setSize(prop->getSubValues().size());
        else {
            auto subs = prop->getSubValues();
            subs.resize(MappedColors.getSize());
            prop->setValue(pcObject,subs);
        }
        return;
    }

    auto shape = getShape();
    if(shape.isNull())
        return;

    if(!sourceDoc)
        sourceDoc = pcObject->getDocument();

    std::vector<std::pair<std::string,std::string> > _subs;
    const auto &subs = prop?prop->getShadowSubs():_subs;

    std::array<ColorInfo,TopAbs_SHAPE> infos;
    infos[TopAbs_VERTEX].init(TopAbs_VERTEX,this);
    infos[TopAbs_EDGE].init(TopAbs_EDGE,this);
    infos[TopAbs_FACE].init(TopAbs_FACE,this);
    bool noColorMap = !ForceMapColors.getValue() && !forceColorMap && !hasBaseFeature();

    std::set<Data::MappedName> subMap;
    for(auto &v : subs) {
        if(v.first.size())
            subMap.insert(shape.getElementName(v.first.c_str()).name);
    }
    int i=-1;
    for(auto &v : subs) {
        ++i;
        Data::IndexedName element;
        if (v.first.size())
            element = shape.getElementName(v.first.c_str()).index;
        else
            element = Data::IndexedName(v.second.c_str());
        auto idx = shape.shapeTypeAndIndex(element);
        if(idx.second) {
            infos[idx.first].colors[idx.second-1] = MappedColors[i];
            continue;
        }else if(v.first.empty())
            continue;

        for(auto &names : Part::Feature::getRelatedElements(pcObject,v.first.c_str())) {
            if(!subMap.insert(names.name).second)
                continue;
            auto idx = Part::TopoShape::shapeTypeAndIndex(names.index);
            if(idx.second>0)
                infos[idx.first].colors[idx.second-1] = MappedColors[i];
        }
    }
    std::map<App::DocumentObject*,ElementCache> caches;
    for(auto &info : infos) {
        if(!info.prop) continue;
        if(noColorMap || !info.mapColor) {
            if(info.colors.empty())
                info.prop.touch();
            else {
                auto colors = info.prop.getValues();
                if(colors.size()!=shape.countSubShapes(info.type)) {
                    colors.clear();
                    colors.resize(shape.countSubShapes(info.type),info.defaultColor);
                }
                for(auto &v : info.colors) {
                    if(v.first>=(int)colors.size())
                        break;
                    colors[v.first] = v.second;
                }
                info.prop.setValues(colors);
            }
            continue;
        }
        int count = shape.countSubShapes(info.type);
        bool touched = false;
        std::vector<App::Color> colors(count,info.defaultColor);
        auto it = info.colors.begin();
        const char * typeName = shape.shapeName(info.type).c_str();
        float trans = Transparency.getValue()/100.0f;
        for(int i=0;i<count;++i) {
            if(it!=info.colors.end() && i==it->first) {
                if(colors[i]!=it->second) {
                    touched = true;
                    colors[i] = it->second;
                }
                ++it;
                continue;
            }
            Data::MappedName mapped = shape.getMappedName(
                    Data::IndexedName::fromConst(typeName, i+1));
            if(!mapped)
                continue;

            App::Document *doc = sourceDoc;
            if(geoFeature) {
                auto owner = geoFeature->getElementOwner(mapped);
                if(owner)
                    doc = owner->getDocument();
            }
            auto color = getElementColor(info.defaultColor, shape, doc,info.type,mapped,caches);
            if(!MapTransparency.getValue())
                color.setTransparency(trans);
            if(color != colors[i]) {
                touched = true;
                colors[i] = color;
            }
        }
        if(!touched) {
            colors.clear();
            colors.push_back(info.defaultColor);
        }
        info.prop.setValues(colors);
    }
}

void ViewProviderPartExt::updateData(const App::Property* prop)
{
    const char *shapeProp = shapePropName.empty()?"Shape":shapePropName.c_str();
    const char *propName = prop?prop->getName():"";
    if(strcmp(propName,"ColoredElements")==0
            || strcmp(propName,shapeProp)==0
            || strstr(propName,"Touched")!=0)
    {
        TopoDS_Shape cShape = getShape().getShape();
        if(cachedShape.getShape().IsPartner(cShape)) {
            updateColors();
            Gui::ViewProviderGeometryObject::updateData(prop);
            return;
        }

        // calculate the visual only if visible
        if (isUpdateForced() || Visibility.getValue())
            updateVisual();
        else
            VisualTouched = true;

        updateColors();

        if (!VisualTouched) {
            if (this->faceset->partIndex.getNum() >
                this->pcShapeMaterial->diffuseColor.getNum()) {
                this->pcFaceBind->value = SoMaterialBinding::OVERALL;
            }
        }
    }
    Gui::ViewProviderGeometryObject::updateData(prop);
}

void ViewProviderPartExt::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    QIcon iconObject = Gui::BitmapFactory().pixmap("Part_ColorFace.svg");
    Gui::ViewProviderGeometryObject::setupContextMenu(menu, receiver, member);
    QAction* act = menu->addAction(iconObject, QObject::tr("Set colors..."), receiver, member);
    act->setData(QVariant((int)ViewProvider::Color));
}

void ViewProviderPartExt::setEditViewer(Gui::ViewerContext *viewer, int ModNum) {
    if (ModNum == ViewProvider::Color)
        Gui::Control().showDialog(new Gui::TaskElementColors(this,true));
    else
        Gui::ViewProviderGeometryObject::setEditViewer(viewer,ModNum);
}

bool ViewProviderPartExt::changeFaceColors()
{
    return Gui::Application::Instance->activeDocument()->setEdit(this, (int)ViewProvider::Color);
}

bool ViewProviderPartExt::setEdit(int ModNum)
{
    if (ModNum == ViewProvider::Color) {
        // When double-clicking on the item for this pad the
        // object unsets and sets its edit mode without closing
        // the task panel
        Gui::TaskView::TaskDialog *dlg = Gui::Control().activeDialog();
        if (dlg) {
            Gui::Control().showDialog(dlg);
            return false;
        }

        // TaskFaceColors is replaced by TaskElementColors, and is inited in 
        // setEditViewer() in order to handle editing context
        //
        // Gui::Control().showDialog(new TaskFaceColors(this));
        return true;
    }
    else {
        return Gui::ViewProviderGeometryObject::setEdit(ModNum);
    }
}

void ViewProviderPartExt::unsetEdit(int ModNum)
{
    if (ModNum == ViewProvider::Color) {
        // Do nothing here
    }
    else {
        Gui::ViewProviderGeometryObject::unsetEdit(ModNum);
    }
}

namespace {
struct ShapeInfo {
    Gui::CoinPtr<SoFCShapeInfo> node;
    int refcount = 0;
};

static std::unordered_map<void*, ShapeInfo> _ShapeTable;

static void registerShape(Part::TopoShape &shape, const Part::TopoShape &newshape)
{
    for (auto &s : newshape.getSubTopoShapes(TopAbs_SOLID)) {
        int count = s.countSubShapes(TopAbs_FACE);
        if (!count)
            continue;
        auto &info = _ShapeTable[s.getShape().TShape().get()];
        ++info.refcount;
        if (!info.node) {
            info.node = new SoFCShapeInfo;
            info.node->partCount = count;
            info.node->shapeType.setValue(SoFCShapeInfo::SOLID);
        }
    }
    for (auto &s : shape.getSubShapes(TopAbs_SOLID)) {
        auto it = _ShapeTable.find(s.TShape().get());
        if (it != _ShapeTable.end() && --it->second.refcount <= 0)
            _ShapeTable.erase(it);
    }
    shape = newshape;
}
}

bool ViewProviderPartExt::instancingCandidate() const
{
    return shapeInstancingActive() && !cachedShape.isNull()
        && cachedShape.getShape().ShapeType() == TopAbs_COMPOUND;
}

bool ViewProviderPartExt::buildInstanced()
{
    if (!shapeInstancingActive())
        return false;
    if (appliedFaceColorsDivergent)
        return false;
    if (!pFaceInstRoot || !pEdgeInstRoot || !pVertexInstRoot)
        return false;
    const TopoDS_Shape &cShape = cachedShape.getShape();
    if (cShape.IsNull() || cShape.ShapeType() != TopAbs_COMPOUND)
        return false;

    // Divergent per-element colors never disqualify: faces, edges and
    // vertices all partition the instances over baked color variants
    // after the build (applyInstanced*Colors, called by the
    // setHighlighted* re-applies that follow).

    // Collect the non-compound leaves (locations composed by the
    // iterator through nested compounds).
    std::vector<TopoDS_Shape> leaves;
    std::vector<TopoDS_Shape> pending;
    pending.push_back(cShape);
    while (!pending.empty()) {
        TopoDS_Shape s = pending.back();
        pending.pop_back();
        for (TopoDS_Iterator it(s); it.More(); it.Next()) {
            if (it.Value().IsNull())
                continue;
            if (it.Value().ShapeType() == TopAbs_COMPOUND)
                pending.push_back(it.Value());
            else
                leaves.push_back(it.Value());
        }
    }
    if (leaves.size() < 2)
        return false;

    // Qualification: some TShape must repeat (else nothing to share), no
    // mirror placements (they flip the triangle winding), and no two
    // leaves identical including location (TopExp::MapShapes would
    // deduplicate the second one and shift the global element numbering).
    std::unordered_map<const void *, std::vector<int>> byTShape;
    int maxCount = 0;
    for (int i = 0; i < int(leaves.size()); ++i) {
        const TopoDS_Shape &leaf = leaves[i];
        if (leaf.Location().Transformation().IsNegative())
            return false;
        auto &group = byTShape[leaf.TShape().get()];
        for (int j : group) {
            if (leaf.IsSame(leaves[j]))
                return false;
        }
        group.push_back(i);
        maxCount = std::max(maxCount, int(group.size()));
    }
    if (maxCount < 2)
        return false;

    // Per-leaf element counts, and the defensive contiguity check: the
    // global FaceN/EdgeN/VertexN numbering must equal the concatenation
    // of the leaves' local numbering in traversal order.
    struct LeafCounts { int faces = 0, edges = 0, vertices = 0; };
    std::vector<LeafCounts> counts(leaves.size());
    int faceBase = 0, edgeBase = 0, vertexBase = 0;
    for (size_t i = 0; i < leaves.size(); ++i) {
        const TopoDS_Shape &leaf = leaves[i];
        TopTools_IndexedMapOfShape m;
        TopExp::MapShapes(leaf, TopAbs_FACE, m);
        counts[i].faces = m.Extent();
        m.Clear();
        TopExp::MapShapes(leaf, TopAbs_EDGE, m);
        counts[i].edges = m.Extent();
        m.Clear();
        TopExp::MapShapes(leaf, TopAbs_VERTEX, m);
        counts[i].vertices = m.Extent();

        TopExp_Explorer exp(leaf, TopAbs_FACE);
        if (exp.More() && cachedShape.findShape(exp.Current()) != faceBase + 1)
            return false;
        exp.Init(leaf, TopAbs_EDGE);
        if (exp.More() && cachedShape.findShape(exp.Current()) != edgeBase + 1)
            return false;
        exp.Init(leaf, TopAbs_VERTEX);
        if (exp.More() && cachedShape.findShape(exp.Current()) != vertexBase + 1)
            return false;
        faceBase += counts[i].faces;
        edgeBase += counts[i].edges;
        vertexBase += counts[i].vertices;
    }

    // The linear deflection of a shared tessellation derives from the
    // LEAF bounding box (same formula as the flattened build, which uses
    // the whole shape) -- the same part in differently sized parents must
    // agree on one mesh. Different deviation settings key apart.
    auto leafDeflection = [&](const TopoDS_Shape &s) -> Standard_Real {
        Bnd_Box bounds;
        BRepBndLib::Add(s, bounds);
        bounds.SetGap(0.0);
        Standard_Real x0, y0, z0, x1, y1, z1;
        bounds.Get(x0, y0, z0, x1, y1, z1);
        Standard_Real defl = std::max(Precision::Confusion(),
            ((x1-x0)+(y1-y0)+(z1-z0))/300.0 *
                std::max(PartParams::getOverrideTessellation()
                             ? PartParams::getMeshDeviation()
                             : Deviation.getValue(),
                         PartParams::getMinimumDeviation()));
        if (defl < gp::Resolution())
            defl = Precision::Confusion();
        return std::min(defl, 20.0);
    };
    Standard_Real angDefl = std::max(Precision::Angular(),
        std::max((PartParams::getOverrideTessellation()
                      ? PartParams::getMeshAngularDeflection()
                      : AngularDeflection.getValue()),
                  PartParams::getMinimumAngularDeflection()) / 180.0 * M_PI);

    auto rep = std::make_unique<ShapeInstanceRep>();
    faceBase = edgeBase = vertexBase = 0;
    for (size_t i = 0; i < leaves.size(); ++i) {
        const TopoDS_Shape &leaf = leaves[i];
        InstGeomKey key;
        key.tshape = leaf.TShape().get();
        key.orientation = int(leaf.Orientation());
        // Deflection from the LOCAL-frame bbox: the located bbox varies
        // with the instance rotation and would split the table key.
        Standard_Real defl = leafDeflection(leaf.Located(TopLoc_Location()));
        // Coarse-first publish, like the flattened build: the leaf is
        // tessellated at a ladder rung and the display parameters go
        // to the registration for the on-demand exact build.
        Standard_Real exactDefl = defl, exactAng = angDefl;
        Standard_Real useAngDefl = angDefl;
        float builtError = 0.0f;
        const int coarseLvl =
            coarseTessellationLevel(pcObject ? pcObject->getDocument() : nullptr);
        if (coarseLvl >= 0) {
            Bnd_Box leafBounds;
            BRepBndLib::Add(leaf.Located(TopLoc_Location()), leafBounds);
            leafBounds.SetGap(0.0);
            if (!leafBounds.IsVoid()) {
                Standard_Real x0, y0, z0, x1, y1, z1;
                leafBounds.Get(x0, y0, z0, x1, y1, z1);
                double diag = std::sqrt((x1 - x0) * (x1 - x0)
                                        + (y1 - y0) * (y1 - y0)
                                        + (z1 - z0) * (z1 - z0));
                if (diag > 0) {
                    defl = meshLevelDeflection(diag, unsigned(coarseLvl));
                    useAngDefl = meshLevelAngle(unsigned(coarseLvl));
                    builtError =
                        float(1.0 / double(8u << unsigned(coarseLvl)));
                }
            }
        }
        key.deflection = int64_t(defl * 1e9);
        key.angdeflection = int64_t(useAngDefl * 1e9);

        auto res = _InstGeomTable.emplace(key, InstGeometry());
        InstGeometry &geom = res.first->second;
        ++geom.refcount;
        rep->keys.push_back(key);
        if (res.second) {
            // First user of this (TShape, orientation, tessellation):
            // build the shared subgraphs from the leaf in its local frame.
            TopoDS_Shape local = leaf.Located(TopLoc_Location());
            auto gcoords = new SoCoordinate3;
            auto gpcoords = new SoCoordinate3;
            // Same phantom-dot guard as the constructor's pcoords: an
            // SoCoordinate3 is born holding one (0,0,0) and a point set
            // draws all coordinates, so every sharer of an unfilled
            // entry would submit a dot at its instance placement.
            gpcoords->point.setNum(0);
            auto gnorm = new SoNormal;
            auto gtexcoords = new SoTextureCoordinate2;
            gtexcoords->point.setNum(0);
            auto gfaceset = new SoBrepFaceSet;
            // The shared cache is built by whichever sharer traverses
            // first; capture UVs unconditionally so a build under an
            // untextured user still serves textured sharers.
            gfaceset->forceTexCoords = TRUE;
            auto glineset = new SoBrepEdgeSet;
            auto gnodeset = new SoBrepPointSet;
            gfaceset->setSiblings({glineset, gnodeset});
            glineset->setSiblings({gfaceset, gnodeset});
            gnodeset->setSiblings({gfaceset, glineset});
            // The shared subgraphs are SoFCSelectionRoot -- the render
            // cache's child boundary. Entering the same root under N
            // transforms flattens into shared-cache entries with
            // per-instance matrices (the App::Link mechanism).
            geom.faceGroup = new Gui::SoFCSelectionRoot;
            geom.faceGroup->addChild(gcoords);
            geom.faceGroup->addChild(gnorm);
            geom.faceGroup->addChild(gtexcoords);
            geom.faceGroup->addChild(gfaceset);
            geom.edgeGroup = new Gui::SoFCSelectionRoot;
            geom.edgeGroup->addChild(gcoords);
            geom.edgeGroup->addChild(glineset);
            geom.vertexGroup = new Gui::SoFCSelectionRoot;
            geom.vertexGroup->addChild(gpcoords);
            geom.vertexGroup->addChild(gnodeset);
            geom.faceset = gfaceset;
            geom.lineset = glineset;
            geom.nodeset = gnodeset;
            geom.coordsNode = gcoords;
            geom.pcoordsNode = gpcoords;
            geom.normNode = gnorm;
            geom.texcoordsNode = gtexcoords;
            geom.faceCount = counts[i].faces;
            geom.edgeCount = counts[i].edges;
            geom.vertexCount = counts[i].vertices;
            int nt = 0, nn = 0, np = 0, nno = 0, nf = 0, ne = 0, nl = 0;
            buildVisualNodes(local, defl, useAngDefl, NormalsFromUV,
                             gcoords, gpcoords, gnorm, gtexcoords,
                             gfaceset, glineset, gnodeset,
                             nt, nn, np, nno, nf, ne, nl);
            // Level generation for the shared leaf tessellation
            // (MeshLevelSource.h); released with the geometry entry.
            // The whole coarse <-> exact cycle of the entry lives in
            // registerInstancedLevelEntry (sec 13): climb on plan demand,
            // demotion back under memory pressure.
            registerInstancedLevelEntry(local, false, builtError,
                                        defl, useAngDefl,
                                        exactDefl, exactAng, NormalsFromUV,
                                        gcoords, gpcoords, gnorm,
                                        gtexcoords, gfaceset, glineset,
                                        gnodeset);
            // Solid knowledge for the section-cap pass, in local part
            // numbering (the cache reads it per shape node).
            if (local.ShapeType() == TopAbs_SOLID && counts[i].faces > 0) {
                auto it = _ShapeTable.find(local.TShape().get());
                if (it != _ShapeTable.end() && it->second.node) {
                    auto instNode = new SoFCShapeInstance;
                    instNode->partIndex = 1;
                    instNode->shapeInfo = it->second.node;
                    gfaceset->shapeInfo.setValue(instNode);
                }
            }
            // Last write to these nodes for this build; stamp now
            // (docs/WorkerVertexCache.md). The shapeInfo above is
            // exactly the kind of late touch that would void an entry
            // registered any earlier.
            emitAndRegisterSharedVertexCache(gcoords, gpcoords, gnorm,
                                             gfaceset, glineset, gnodeset);
        }

        ShapeInstanceRep::Instance inst;
        inst.geom = &geom;
        SbMatrix mat = convert(Part::TopoShape(leaf).getTransform());
        // The wrappers are SoFCSelectionRoot: selection/highlight
        // contexts key on the traversed selection-root stack, so a
        // per-instance root gives each instance its own sub-element
        // context over the shared shape nodes (the App::Link
        // mechanism) -- see getDetailPath.
        auto makeSep = [&mat](SoGroup *group) -> SoSeparator * {
            auto sep = new Gui::SoFCSelectionRoot;
            sep->renderCaching = SoSeparator::OFF;
            sep->boundingBoxCaching = SoSeparator::OFF;
            auto mt = new SoMatrixTransform;
            mt->matrix = mat;
            sep->addChild(mt);
            sep->addChild(group);
            return sep;
        };
        inst.faceSep = makeSep(geom.faceGroup);
        inst.edgeSep = makeSep(geom.edgeGroup);
        inst.vertexSep = makeSep(geom.vertexGroup);
        pFaceInstRoot->addChild(inst.faceSep);
        pEdgeInstRoot->addChild(inst.edgeSep);
        pVertexInstRoot->addChild(inst.vertexSep);
        inst.faceBase = faceBase;
        inst.edgeBase = edgeBase;
        inst.vertexBase = vertexBase;
        int instIdx = int(rep->instances.size());
        rep->sepToInstance[inst.faceSep] = instIdx;
        rep->sepToInstance[inst.edgeSep] = instIdx;
        rep->sepToInstance[inst.vertexSep] = instIdx;
        rep->nodeToGeom[geom.faceset] = &geom;
        rep->nodeToGeom[geom.lineset] = &geom;
        rep->nodeToGeom[geom.nodeset] = &geom;
        rep->instances.push_back(std::move(inst));
        faceBase += counts[i].faces;
        edgeBase += counts[i].edges;
        vertexBase += counts[i].vertices;
    }

    // The flattened member nodes stay in the graph but carry nothing.
    coords->point.setNum(0);
    pcoords->point.setNum(0);
    norm->vector.setNum(0);
    texcoords->point.setNum(0);
    faceset->coordIndex.setNum(0);
    faceset->partIndex.setNum(0);
    faceset->shapeInfo.setNum(0);
    lineset->coordIndex.setNum(0);
    nodeset->startIndex.setValue(0);

    instanced = std::move(rep);
    FC_TRACE(getFullName() << " instanced: " << leaves.size()
             << " leaves over " << instanced->keys.size() << " geometries");
    return true;
}

void ViewProviderPartExt::applyInstancedFaceColors(const std::vector<App::Color> &colors)
{
    if (!instanced)
        return;

    auto setOverall = [&](const App::Color &c, float trans) {
        pcFaceBind->value = SoMaterialBinding::OVERALL;
        pcShapeMaterial->diffuseColor.setValue(c.r, c.g, c.b);
        pcShapeMaterial->transparency.setValue(trans);
    };

    // Uniform resolved colors: every instance back on the shared base
    // subgraph, color riding the object material.
    auto clearInstanceColors = [&]() {
        bool changed = false;
        for (auto &inst : instanced->instances) {
            if (inst.variant) {
                releaseColorVariant(inst.geom->variants, inst.variant);
                inst.variant = nullptr;
                changed = true;
            }
            inst.overrideMat = nullptr;
            restructureInstanceFace(inst);
        }
        if (changed)
            instanced->rebuildNodeMap();
    };

    if (colors.size() <= 1) {
        clearInstanceColors();
        const App::Color &c = colors.size() == 1 ? colors[0] : ShapeColor.getValue();
        // do not get transparency from DiffuseColor in this case
        setOverall(c, ShapeAppearance.getBase().transparency);
        return;
    }

    // Resolve the full per-face vector: a short apply keeps the base
    // color on the remaining faces (flat-path semantics -- transparency
    // rides the alpha channel when applied as an array).
    int total = 0;
    for (const auto &inst : instanced->instances)
        total += inst.geom->faceCount;
    App::Color base = ShapeColor.getValue();
    base.setTransparency(ShapeAppearance.getBase().transparency);
    std::vector<App::Color> resolved(size_t(total), base);
    for (size_t i = 0; i < colors.size() && i < resolved.size(); ++i)
        resolved[i] = colors[i];

    bool uniform = true;
    for (size_t i = 1; i < resolved.size(); ++i) {
        if (resolved[i] != resolved[0]) {
            uniform = false;
            break;
        }
    }
    if (uniform) {
        clearInstanceColors();
        const App::Color &c = resolved.empty() ? base : resolved[0];
        setOverall(c, c.a);
        return;
    }

    // Divergent by value: partition the instances by their resolved
    // slice -- a uniform slice rides a per-instance override material on
    // the shared base subgraph (cross-instance sharable whatever its
    // value, the Link mechanism), a divergent slice a baked, refcounted
    // color variant shared by every instance applying that exact vector.
    setOverall(ShapeColor.getValue(), ShapeAppearance.getBase().transparency);
    bool structureChanged = false;
    int faceBase = 0;
    for (auto &inst : instanced->instances) {
        int count = inst.geom->faceCount;
        std::vector<App::Color> slice(resolved.begin() + faceBase,
                                      resolved.begin() + faceBase + count);
        faceBase += count;
        bool sliceUniform = true;
        for (size_t i = 1; i < slice.size(); ++i) {
            if (slice[i] != slice[0]) {
                sliceUniform = false;
                break;
            }
        }
        if (sliceUniform) {
            if (inst.variant) {
                releaseColorVariant(inst.geom->variants, inst.variant);
                inst.variant = nullptr;
                structureChanged = true;
            }
            const App::Color &c = slice.empty() ? base : slice[0];
            if (!inst.overrideMat) {
                inst.overrideMat = new SoMaterial;
                inst.overrideMat->ambientColor.setIgnored(TRUE);
                inst.overrideMat->specularColor.setIgnored(TRUE);
                inst.overrideMat->emissiveColor.setIgnored(TRUE);
                inst.overrideMat->shininess.setIgnored(TRUE);
            }
            inst.overrideMat->diffuseColor.setValue(c.r, c.g, c.b);
            inst.overrideMat->transparency.setValue(c.transparency());
        } else {
            ColorVariant *variant = acquireColorVariant(*inst.geom, slice);
            if (variant != inst.variant) {
                if (inst.variant)
                    releaseColorVariant(inst.geom->variants, inst.variant);
                inst.variant = variant;
                structureChanged = true;
            } else {
                // re-acquired the one already held; drop the extra ref
                releaseColorVariant(inst.geom->variants, variant);
            }
            inst.overrideMat = nullptr;
        }
        restructureInstanceFace(inst);
    }
    if (structureChanged)
        instanced->rebuildNodeMap();
}

void ViewProviderPartExt::applyInstancedLineColors(const std::vector<App::Color> &colors)
{
    if (!instanced)
        return;

    auto setOverall = [&](const App::Color &c) {
        pcLineBind->value = SoMaterialBinding::OVERALL;
        pcLineMaterial->diffuseColor.setValue(c.r, c.g, c.b);
    };
    auto clearInstanceColors = [&]() {
        bool changed = false;
        for (auto &inst : instanced->instances) {
            if (inst.lineVariant) {
                releaseColorVariant(inst.geom->lineVariants,
                                    inst.lineVariant);
                inst.lineVariant = nullptr;
                changed = true;
            }
            inst.lineOverrideMat = nullptr;
            restructureInstanceEdge(inst);
        }
        if (changed)
            instanced->rebuildNodeMap();
    };

    if (colors.size() <= 1) {
        clearInstanceColors();
        setOverall(colors.size() == 1 ? colors[0] : LineColor.getValue());
        return;
    }

    // Resolve the full per-edge vector: a short apply keeps the base
    // line color on the remaining edges (flat-path semantics; alpha is
    // never applied to lines).
    int total = 0;
    for (const auto &inst : instanced->instances)
        total += inst.geom->edgeCount;
    const App::Color &base = LineColor.getValue();
    std::vector<App::Color> resolved(size_t(total), base);
    for (size_t i = 0; i < colors.size() && i < resolved.size(); ++i)
        resolved[i] = colors[i];

    bool uniform = true;
    for (size_t i = 1; i < resolved.size(); ++i) {
        if (resolved[i] != resolved[0]) {
            uniform = false;
            break;
        }
    }
    if (uniform) {
        clearInstanceColors();
        setOverall(resolved.empty() ? base : resolved[0]);
        return;
    }

    // Divergent by value: partition the instances by their resolved
    // slice like applyInstancedFaceColors -- uniform slices ride a
    // per-instance override material, divergent slices a baked,
    // refcounted line color variant.
    setOverall(base);
    bool structureChanged = false;
    int edgeBase = 0;
    for (auto &inst : instanced->instances) {
        int count = inst.geom->edgeCount;
        std::vector<App::Color> slice(resolved.begin() + edgeBase,
                                      resolved.begin() + edgeBase + count);
        edgeBase += count;
        bool sliceUniform = true;
        for (size_t i = 1; i < slice.size(); ++i) {
            if (slice[i] != slice[0]) {
                sliceUniform = false;
                break;
            }
        }
        if (sliceUniform) {
            if (inst.lineVariant) {
                releaseColorVariant(inst.geom->lineVariants,
                                    inst.lineVariant);
                inst.lineVariant = nullptr;
                structureChanged = true;
            }
            const App::Color &c = slice.empty() ? base : slice[0];
            if (!inst.lineOverrideMat)
                inst.lineOverrideMat = makeDiffuseOnlyMaterial();
            inst.lineOverrideMat->diffuseColor.setValue(c.r, c.g, c.b);
        } else {
            ColorVariant *variant =
                acquireLineColorVariant(*inst.geom, slice);
            if (variant != inst.lineVariant) {
                if (inst.lineVariant)
                    releaseColorVariant(inst.geom->lineVariants,
                                        inst.lineVariant);
                inst.lineVariant = variant;
                structureChanged = true;
            } else {
                // re-acquired the one already held; drop the extra ref
                releaseColorVariant(inst.geom->lineVariants, variant);
            }
            inst.lineOverrideMat = nullptr;
        }
        restructureInstanceEdge(inst);
    }
    if (structureChanged)
        instanced->rebuildNodeMap();
}

void ViewProviderPartExt::applyInstancedPointColors(const std::vector<App::Color> &colors)
{
    if (!instanced)
        return;

    auto setOverall = [&](const App::Color &c) {
        pcPointBind->value = SoMaterialBinding::OVERALL;
        pcPointMaterial->diffuseColor.setValue(c.r, c.g, c.b);
    };
    auto clearInstanceColors = [&]() {
        bool changed = false;
        for (auto &inst : instanced->instances) {
            if (inst.pointVariant) {
                releaseColorVariant(inst.geom->pointVariants,
                                    inst.pointVariant);
                inst.pointVariant = nullptr;
                changed = true;
            }
            inst.pointOverrideMat = nullptr;
            restructureInstanceVertex(inst);
        }
        if (changed)
            instanced->rebuildNodeMap();
    };

    if (colors.size() <= 1) {
        clearInstanceColors();
        setOverall(colors.size() == 1 ? colors[0] : PointColor.getValue());
        return;
    }

    // Resolve the full per-vertex vector, base point color on the rest
    // (flat-path semantics; alpha is never applied to points).
    int total = 0;
    for (const auto &inst : instanced->instances)
        total += inst.geom->vertexCount;
    const App::Color &base = PointColor.getValue();
    std::vector<App::Color> resolved(size_t(total), base);
    for (size_t i = 0; i < colors.size() && i < resolved.size(); ++i)
        resolved[i] = colors[i];

    bool uniform = true;
    for (size_t i = 1; i < resolved.size(); ++i) {
        if (resolved[i] != resolved[0]) {
            uniform = false;
            break;
        }
    }
    if (uniform) {
        clearInstanceColors();
        setOverall(resolved.empty() ? base : resolved[0]);
        return;
    }

    setOverall(base);
    bool structureChanged = false;
    int vertexBase = 0;
    for (auto &inst : instanced->instances) {
        int count = inst.geom->vertexCount;
        std::vector<App::Color> slice(resolved.begin() + vertexBase,
                                      resolved.begin() + vertexBase + count);
        vertexBase += count;
        bool sliceUniform = true;
        for (size_t i = 1; i < slice.size(); ++i) {
            if (slice[i] != slice[0]) {
                sliceUniform = false;
                break;
            }
        }
        if (sliceUniform) {
            if (inst.pointVariant) {
                releaseColorVariant(inst.geom->pointVariants,
                                    inst.pointVariant);
                inst.pointVariant = nullptr;
                structureChanged = true;
            }
            const App::Color &c = slice.empty() ? base : slice[0];
            if (!inst.pointOverrideMat)
                inst.pointOverrideMat = makeDiffuseOnlyMaterial();
            inst.pointOverrideMat->diffuseColor.setValue(c.r, c.g, c.b);
        } else {
            ColorVariant *variant =
                acquirePointColorVariant(*inst.geom, slice);
            if (variant != inst.pointVariant) {
                if (inst.pointVariant)
                    releaseColorVariant(inst.geom->pointVariants,
                                        inst.pointVariant);
                inst.pointVariant = variant;
                structureChanged = true;
            } else {
                // re-acquired the one already held; drop the extra ref
                releaseColorVariant(inst.geom->pointVariants, variant);
            }
            inst.pointOverrideMat = nullptr;
        }
        restructureInstanceVertex(inst);
    }
    if (structureChanged)
        instanced->rebuildNodeMap();
}

void ViewProviderPartExt::registerInstancedLevelEntry(
        const TopoDS_Shape &local, bool exact, float builtError,
        double coarseDefl, double coarseAng,
        double exactDefl, double exactAng, bool normalsFromUV,
        SoCoordinate3 *coords, SoCoordinate3 *pcoords, SoNormal *norm,
        SoTextureCoordinate2 *texcoords, SoBrepFaceSet *faceset,
        SoBrepEdgeSet *lineset, SoBrepPointSet *nodeset)
{
    if (!exact) {
        // A coarse desktop build climbs back to exact through the
        // registration (sec 13): the callback rebuilds the SHARED nodes
        // in place -- every instance refines at once, the per-proto
        // ladder -- and it captures the nodes, not any view provider:
        // the entry outlives any one sharer, and a live registration
        // token is what guarantees the entry (release unregisters
        // first). Re-registering exact is what keeps a later sharer's
        // rebuild from queueing the work again.
        std::function<void(const TopoDS_Shape &)> onExact;
        if (builtError > 0.0f) {
            onExact = [=](const TopoDS_Shape &meshed) {
                transferMeshLevels(meshed, local);
                int nt = 0, nn = 0, np = 0, nno = 0;
                int nf = 0, ne = 0, nl = 0;
                buildVisualNodes(local, exactDefl, exactAng, normalsFromUV,
                                 coords, pcoords, norm, texcoords,
                                 faceset, lineset, nodeset,
                                 nt, nn, np, nno, nf, ne, nl,
                                 ScaleSpent::No, nullptr,
                                 /*residentLanded*/ true);
                registerInstancedLevelEntry(local, true, builtError,
                                            coarseDefl, coarseAng,
                                            exactDefl, exactAng,
                                            normalsFromUV, coords, pcoords,
                                            norm, texcoords, faceset,
                                            lineset, nodeset);
                emitAndRegisterSharedVertexCache(coords, pcoords, norm,
                                                 faceset, lineset, nodeset);
            };
        }
        // No document: the entry is shared by every instance of the
        // leaf, across objects and potentially across documents, and
        // deliberately captures no view provider -- so the gate reads
        // process-wide here (null doc), not one sharer's document.
        registerMeshLevelSource(local, normalsFromUV, faceset, lineset,
                                builtError, exactDefl, exactAng,
                                std::move(onExact), {}, 0.0f, {}, nullptr,
                                "instanced-leaf");
        return;
    }
    // Exact-resident: registered at error 0 with both ways back down
    // armed (sec 13 step 3) -- the coarse triangulation never left the
    // shape. The demote (CPU-memory ceiling only) drops the exact
    // rung; the downgrade (GPU budget) merely re-activates the coarse
    // one, keeping the exact resident so the climb back is instant.
    // Either way the coarse node rebuild finds its mesh resident, and
    // the coarse re-registration arms a fresh climb.
    auto onDemote = [=]() {
        if (!demoteMeshLevels(local))
            return;
        int nt = 0, nn = 0, np = 0, nno = 0, nf = 0, ne = 0, nl = 0;
        buildVisualNodes(local, coarseDefl, coarseAng, normalsFromUV,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         nt, nn, np, nno, nf, ne, nl,
                         ScaleSpent::No, nullptr, /*residentLanded*/ true);
        registerInstancedLevelEntry(local, false, builtError,
                                    coarseDefl, coarseAng,
                                    exactDefl, exactAng, normalsFromUV,
                                    coords, pcoords, norm, texcoords,
                                    faceset, lineset, nodeset);
        emitAndRegisterSharedVertexCache(coords, pcoords, norm,
                                         faceset, lineset, nodeset);
    };
    auto onDowngrade = [=]() {
        if (!downgradeMeshLevels(local))
            return;
        int nt = 0, nn = 0, np = 0, nno = 0, nf = 0, ne = 0, nl = 0;
        buildVisualNodes(local, coarseDefl, coarseAng, normalsFromUV,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         nt, nn, np, nno, nf, ne, nl,
                         ScaleSpent::No, nullptr, /*residentLanded*/ true);
        registerInstancedLevelEntry(local, false, builtError,
                                    coarseDefl, coarseAng,
                                    exactDefl, exactAng, normalsFromUV,
                                    coords, pcoords, norm, texcoords,
                                    faceset, lineset, nodeset);
        emitAndRegisterSharedVertexCache(coords, pcoords, norm,
                                         faceset, lineset, nodeset);
    };
    registerMeshLevelSource(local, normalsFromUV, faceset, lineset,
                            0.0f, exactDefl, exactAng, {},
                            std::move(onDemote), builtError,
                            std::move(onDowngrade), nullptr,
                            "instanced-leaf-exact");
}

// Progressive import of an oversized part (docs/SceneStreaming.md
// sec 13): a shape over the CoarseDeferFaces threshold shows a
// 12-triangle bounding-box stand-in immediately, and even its coarse
// tessellation runs on the refine worker pool. The registration
// declares the stand-in's error (0.5 of the diagonal) and names the
// coarse rung parameters as its climb target, so the ordinary level
// plan fires the build; the meshed copy arrives on the GUI thread,
// its triangulation transfers onto the live shape, and the rerun of
// updateVisual() finds every face resident -- an instant rebuild. The
// exact rung follows the normal ladder from there. Returns whether
// the stand-in was built (the caller is done then).
bool ViewProviderPartExt::buildCoarseStandIn(bool underPressure)
{
    const long deferFaces = Gui::RenderParams::getCoarseDeferFaces();
    if (cachedShape.isNull() || (deferFaces < 0 && !underPressure)) {
        return false;
    }
    TopoDS_Shape cShape = cachedShape.getShape();
    if (cShape.IsNull()) {
        return false;
    }
    // Under pressure the box is a DESCENT, not a stand-in: the object
    // has a mesh and the plan asked for it back, so the tests that say
    // "a mesh is already here" or "this import is not live" are the
    // wrong questions -- they exist to keep the import path from
    // standing in for work already done.
    if (!underPressure) {
        if ((meshLadder.coarseResolved || meshLadder.exactResident)
            && meshLadder.anchor == cShape.TShape().get()) {
            return false;
        }
        // The deferred-visual drain is the restore's LiveImport: same
        // geometry-outrunning-the-tessellator situation, same fix. A
        // plain .FCStd restore never sets LiveImport (only the STEP
        // import path does), and without this the drain tessellated
        // oversized compounds inline -- the measured 1.8s builds that
        // were the worst per-item stalls of the whole gate run.
        auto d = pcObject ? pcObject->getDocument() : nullptr;
        if (!d
            || (!d->testStatus(App::Document::LiveImport)
                && !s_drainVisualBuild)) {
            return false;
        }
        if (long(cachedShape.countSubShapes(TopAbs_FACE)) <= deferFaces) {
            return false;
        }
    }
    auto doc = pcObject ? pcObject->getDocument() : nullptr;
    const int coarseLvl = coarseTessellationLevel(doc);
    if (coarseLvl < 0) {
        return false;
    }
    // Kept outside the try so the failure below can report the extents
    // that produced it -- the exception itself carries no message.
    double dx = 0.0, dy = 0.0, dz = 0.0;
    try {
        Bnd_Box bounds;
        BRepBndLib::Add(cShape, bounds);
        bounds.SetGap(0.0);
        if (bounds.IsVoid()) {
            return false;
        }
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        dx = xMax - xMin;
        dy = yMax - yMin;
        dz = zMax - zMin;
        double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(diag > 0)) {
            return false;
        }
        double deflection = meshLevelDeflection(diag, unsigned(coarseLvl));
        double angDefl = meshLevelAngle(unsigned(coarseLvl));
        // A flat shape gives the box a zero side, and the builder wants a
        // side STRICTLY greater than the tolerance -- clamping AT
        // Precision::Confusion() is the one value it refuses, so every
        // exactly-flat object (64 of them in one IFC building) lost its
        // stand-in here. Twice the tolerance is the nearest thickness it
        // accepts.
        const double minSide = 2.0 * Precision::Confusion();
        TopoDS_Shape standIn =
            BRepPrimAPI_MakeBox(gp_Pnt(xMin, yMin, zMin),
                                std::max(dx, minSide),
                                std::max(dy, minSide),
                                std::max(dz, minSide))
                .Shape();
        int nt = 0, nn = 0, np = 0, nno = 0, nf = 0, ne = 0, nl = 0;
        buildVisualNodes(standIn, deflection, angDefl, false,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         nt, nn, np, nno, nf, ne, nl);
        // whatever the instance table held indexes the real shape's
        // faces, not the stand-in's six
        faceset->shapeInfo.setNum(0);
        FC_LOG(getFullName() << " bounding-box stand-in ("
               << cachedShape.countSubShapes(TopAbs_FACE)
               << " faces deferred to the refine pool)");
        const void *tsh = cShape.TShape().get();
        auto onCoarse = [this, tsh, underPressure](const TopoDS_Shape &meshed) {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh) {
                return;
            }
            transferMeshLevels(meshed, cur);
            if (meshLadder.anchor == tsh) {
                meshLadder.coarseResolved = true;
                meshLadder.residentLanded = true;
                if (underPressure)
                    meshLadder.resetDescent();
            }
            FC_LOG(getFullName() << " stand-in resolved: coarse mesh in");
            updateVisual();
        };
        registerMeshLevelSource(cShape, NormalsFromUV, faceset, lineset,
                                /*builtError*/ 0.5f, deflection, angDefl,
                                std::move(onCoarse), {}, 0.0f, {},
                                pcObject ? pcObject->getDocument() : nullptr,
                                "standin");
    }
    catch (const Standard_Failure &e) {
        // GetMessageString() is empty on the path that actually fails here,
        // so the exception type and the extents that produced it are the
        // whole diagnosis -- without them this class is unreadable in a log.
        // typeid rather than DynamicType(): OCCT 8.0 stopped deriving
        // Standard_Failure from Standard_Transient, so only one of the two
        // compiles against both kernels this tree builds on.
        FC_ERR("Failed to build the stand-in for the shape of "
               << pcObject->getFullName() << ": " << typeid(e).name()
               << " '" << e.GetMessageString() << "' for extents "
               << dx << " x " << dy << " x " << dz);
        return false;
    }
    return true;
}

bool ViewProviderPartExt::simplifyVisualInPlace(double cellSize,
                                                double shapeDiag,
                                                float builtErrorNow)
{
    if (!Gui::RenderParams::getSimplifyExhausted() || !(cellSize > 0.0))
        return false;
    if (!coords || !faceset)
        return false;

    CoinMeshView view;
    if (!view.build(coords, norm, faceset, lineset, nodeset, pcoords,
                    texcoords))
        return false;

    Render::SimplifyOptions opts;
    // Welding across faces is what actually removes geometry on the
    // population that gets here. This seam is reached only when
    // deflection has PROVED it cannot coarsen the shape -- which is
    // what a shape of planar faces answers at every deflection, two
    // triangles a face however coarse the ask. Clustering each face on
    // its own grid cannot go below those two, so per-face clustering
    // removes almost nothing exactly where the ladder has run out. The
    // cost is the crease: attributes then average over the whole mesh
    // rather than within a face. Off by default all the same, so the
    // cheaper and more faithful arm is what ships until measured.
    opts.weldAcrossParts = Gui::RenderParams::getSimplifyMergeParts();

    Render::SimplifiedMesh out;
    Render::SimplifyStats stats;
    if (!Render::simplifyMesh(view.mesh, float(cellSize), out, opts, &stats))
        return false;

    return applySimplifiedRung(out, stats, view.tris.size(), cellSize,
                               shapeDiag, builtErrorNow);
}

bool ViewProviderPartExt::applySimplifiedRung(
        const Render::SimplifiedMesh &out, const Render::SimplifyStats &stats,
        size_t beforeIndexCount, double cellSize, double shapeDiag,
        float builtErrorNow)
{
    if (!coords || !faceset)
        return false;
    // Did it buy anything? The descent has a cheaper next step (the
    // bounding box), so a rung that barely removes triangles is worse
    // than useless -- it costs a rebuild and still holds the memory.
    // The caller reads false as "decimation is spent, take the box".
    const size_t before = beforeIndexCount;
    const size_t after = out.triangleIndices.size();
    const double keepRatio =
        Gui::RenderParams::getSimplifyMinReduction() / 100.0;
    if (before == 0 || double(before - after) < keepRatio * double(before))
        return false;

    const int nv = out.numVertices();
    if (nv <= 0 || out.triangleIndices.empty())
        return false;

    const int beforeVertices = coords->point.getNum();

    // This rewrite owns the arrays now: an in-flight pooled fill
    // (Render_VisualFillOnPool) queued before it must not land its
    // pre-decimation content over the rung.
    ++meshLadder.visualFillSeq;

    // --- write the rung back into the display nodes -----------------
    coords->point.setNum(nv);
    std::memcpy(coords->point.startEditing(), out.positions.data(),
                size_t(nv) * 3 * sizeof(float));
    coords->point.finishEditing();

    if (!out.normals.empty() && int(out.normals.size()) == nv * 3) {
        norm->vector.setNum(nv);
        std::memcpy(norm->vector.startEditing(), out.normals.data(),
                    size_t(nv) * 3 * sizeof(float));
        norm->vector.finishEditing();
    }
    else {
        norm->vector.setNum(0);
    }

    // Texture coordinates are per vertex and the vertices are new ones.
    // When the source carried them, the clustering carried them too
    // (averaged over the merged vertices, MeshSimplify) -- a textured
    // object on this rung keeps a recognizable texture instead of one
    // stretched texel (user ruling). Zeroed otherwise, not dropped:
    // the build path always sizes this node to the coordinate count,
    // and a shorter array is what a reader indexing by vertex would
    // run off the end of.
    if (texcoords) {
        texcoords->point.setNum(nv);
        SbVec2f *uv = texcoords->point.startEditing();
        if (int(out.texCoords.size()) == nv * 2)
            std::memcpy(uv, out.texCoords.data(),
                        size_t(nv) * 2 * sizeof(float));
        else
            for (int i = 0; i < nv; ++i)
                uv[i] = SbVec2f(0.0f, 0.0f);
        texcoords->point.finishEditing();
    }

    const int numTri = int(out.triangleIndices.size() / 3);
    faceset->coordIndex.setNum(numTri * 4);
    int32_t *ci = faceset->coordIndex.startEditing();
    for (int t = 0; t < numTri; ++t) {
        ci[t * 4]     = out.triangleIndices[size_t(t) * 3];
        ci[t * 4 + 1] = out.triangleIndices[size_t(t) * 3 + 1];
        ci[t * 4 + 2] = out.triangleIndices[size_t(t) * 3 + 2];
        ci[t * 4 + 3] = SO_END_FACE_INDEX;
    }
    faceset->coordIndex.finishEditing();

    // The face table, ranges back to counts. Every source face keeps its
    // slot even when it decimated away to nothing: partIndex is read BY
    // FACE NUMBER -- per-face colours, face selection, the element name
    // a picked triangle resolves to -- so dropping the empties would
    // silently renumber every face after the first one to collapse.
    if (!out.triangleParts.empty()) {
        faceset->partIndex.setNum(int(out.triangleParts.size()));
        int32_t *pi = faceset->partIndex.startEditing();
        for (size_t p = 0; p < out.triangleParts.size(); ++p)
            pi[p] = out.triangleParts[p].second / 3;
        faceset->partIndex.finishEditing();
    }
    else {
        faceset->partIndex.setNum(0);
    }
    // Triangle counts moved, so anything derived from the old ones is
    // stale; the solid table is rebuilt by the next full build.
    faceset->shapeInfo.setNum(0);

    if (lineset) {
        // Segments back to polylines, ONE RUN PER SOURCE EDGE including
        // the collapsed ones. SoBrepEdgeSet derives the edge id from
        // the ordinal of the -1 separator (see its notify()), so an
        // omitted empty run would renumber every edge behind it. An
        // empty run draws nothing and cannot be picked.
        std::vector<int32_t> runs;
        runs.reserve(out.lineIndices.size() + out.lineParts.size() + 1);
        auto emitRun = [&](int start, int count) {
            for (int i = 0; i + 1 < count; i += 2) {
                const int32_t a = out.lineIndices[size_t(start + i)];
                const int32_t b = out.lineIndices[size_t(start + i + 1)];
                if (runs.empty() || runs.back() != a)
                    runs.push_back(a);
                runs.push_back(b);
            }
            runs.push_back(SO_END_LINE_INDEX);
        };
        if (!out.lineParts.empty()) {
            for (const auto &p : out.lineParts)
                emitRun(p.first, p.second);
        }
        else if (!out.lineIndices.empty()) {
            emitRun(0, int(out.lineIndices.size()));
        }
        lineset->coordIndex.setNum(int(runs.size()));
        if (!runs.empty()) {
            std::memcpy(lineset->coordIndex.startEditing(), runs.data(),
                        runs.size() * sizeof(int32_t));
            lineset->coordIndex.finishEditing();
        }
        // The seam filter does not survive the weld: a merged edge can
        // fold a seam and a non-seam edge together. Cleared rather than
        // left stale, so hidden-line mode hides nothing it cannot
        // justify on this rung.
        lineset->seamIndices.setNum(0);
    }

    // Restate the rung this object now stands on. The registry was told
    // what the TESSELLATION errs by, and the decimation just moved the
    // display well past it -- and the error is not a guess here, it is
    // the clustering's own measured worst displacement, relative to the
    // diagonal like every other published error.
    //
    // It has to be said or the object can get STUCK LOOKING DECIMATED:
    // the refine pass wants a source when levelError * diagPx exceeds
    // the tolerance, so an error understating how coarse the object
    // became is one that may never ask for it back once the pressure
    // that decimated it has gone. Restating it also prices the next
    // descent step from the rung the object is actually on.
    //
    // RMS, not the worst vertex, and the difference is not academic.
    // Every other published error on this ladder is a NOMINAL figure --
    // the grid a rung was built on, scale/(8<<level) -- so a worst-case
    // one is not comparable with the numbers it is about to be judged
    // against. Measured: maxDisplacement registered a median relative
    // error of 0.17 against a LevelScaleBoxError of 0.25, i.e. it
    // declared most decimated objects nearly box-grade, and the plan
    // believed it -- boxes rose from 1029 to 1204 and the median live
    // memory from 29.2 to 69.6MB, giving back most of what the rung had
    // won. The typical displacement is what the object looks like; the
    // worst one is what its single worst vertex looks like.
    if (shapeDiag > 0.0 && stats.rmsDisplacement > 0.0f) {
        // Clamped, and the ceiling is not defensive noise: 5 of 995
        // rungs measured a displacement LARGER than the shape diagonal
        // they were divided by, one of them 9.4x it, which is
        // impossible for a clustering bounded by its own grid. Whatever
        // that is -- a compound whose bounds do not cover its own
        // tessellation is the suspect -- the plan must not be handed a
        // number that says an object errs by nine times its own size.
        // Floored at the tessellation's error too, since a decimated
        // mesh cannot be FINER than what it was decimated from.
        const float raw = float(double(stats.rmsDisplacement) / shapeDiag);
        const float rungError = std::min(1.0f, std::max(raw, builtErrorNow));
        if (raw > 1.0f)
            FC_WARN(getFullName() << " decimated rung error " << raw
                    << " exceeds the shape diagonal -- clamped; the shape"
                       " bounds and its tessellation disagree");
        auto &reg = Render::MeshSourceRegistry::instance();
        // Both drawables of the object, because both were decimated and
        // the plan reaches a source by whichever tag a draw carries.
        if (faceset)
            reg.setPublishedError(faceset, rungError);
        if (lineset)
            reg.setPublishedError(lineset, rungError);
        FC_LOG(getFullName() << " decimated rung: cell " << cellSize
                << ", triangles " << (before / 3) << " -> " << (after / 3)
                << ", vertices " << beforeVertices << " -> " << nv
                << ", displacement rms " << stats.rmsDisplacement
                << " max " << stats.maxDisplacement
                << ", published error " << rungError);
    }
    else {
        FC_LOG(getFullName() << " decimated rung: cell " << cellSize
                << ", triangles " << (before / 3) << " -> " << (after / 3)
                << ", vertices " << beforeVertices << " -> " << nv
                << ", max displacement " << stats.maxDisplacement
                << " (rung NOT restated: no diagonal)");
    }

    // The rewrite replaced the arrays any earlier emission mirrored;
    // re-emit the vertex-cache content from the rung just written
    // (docs/WorkerVertexCache.md). The rung is decimation-small, so
    // this GUI-thread walk is proportional to what survived, and it
    // is what lets the next publish adopt instead of re-capturing.
    // The caller's epilogue registers the stash after its highlight
    // re-apply.
    emitVisualVertexCacheFromNodes();
    return true;
}

void ViewProviderPartExt::armMeshLevelSource()
{
    TopoDS_Shape cShape = cachedShape.getShape();
    if (cShape.IsNull() || meshLadder.anchor != cShape.TShape().get())
        return;
    const bool exactResident = meshLadder.exactResident;
    const int coarseLvl = exactResident ? -1 : meshLadder.coarseLevel;
    const double shapeDiag = meshLadder.shapeDiag;
    const double exactDeflection = meshLadder.exactDefl;
    const double exactAngle = meshLadder.exactAng;
    // The rung this shape stands on, re-derived from the stored
    // context exactly as updateVisual derived it at build time.
    float builtError = 0.0f;
    double deflection = exactDeflection;
    double AngDeflectionRads = exactAngle;
    if (coarseLvl >= 0 && shapeDiag > 0.0) {
        const double scale = std::max(1.0, meshLadder.errorScale);
        deflection = meshLevelDeflection(shapeDiag, unsigned(coarseLvl))
            * scale;
        AngDeflectionRads = std::min(
            meshLevelAngle(unsigned(coarseLvl)) * scale, M_PI / 2.0);
        builtError = float(scale / double(8u << unsigned(coarseLvl)));
    }

    // On a coarse desktop build the registration also carries the
    // climb back to exact (sec 13): the worker meshes a copy at the
    // display parameters and this callback -- GUI thread, and only
    // while the registration is still the live one, which is what
    // makes capturing `this` sound (the destructor unregisters) --
    // transfers the triangulation onto the flattened shape and
    // rebuilds through the ordinary visual path.
    std::function<void(const TopoDS_Shape &)> onExact;
    if (builtError > 0.0f) {
        const void *tsh = cShape.TShape().get();
        onExact = [this, tsh, builtError](const TopoDS_Shape &meshed) {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh)
                return;
            transferMeshLevels(meshed, cur);
            if (meshLadder.anchor == tsh) {
                meshLadder.exactResident = true;
                meshLadder.exactCoarseError = builtError;
                meshLadder.residentLanded = true;
            }
            updateVisual();
        };
    }
    // Exact by refine: the coarse triangulation never left the
    // shape (transferMeshLevels keeps it), so both ways back down
    // are armed (sec 13 step 3). The demote -- only ever under an
    // observed CPU-memory ceiling -- drops the exact rung outright;
    // the downgrade -- the GPU budget's -- merely re-activates the
    // coarse rung for display and keeps the exact one resident,
    // so the climb back is instant. Both are the CHEAP kind of
    // descent (the coarse rung is already resident, the rebuild is a
    // fill), so they stay synchronous.
    std::function<void()> onDemote, onDowngrade;
    if (exactResident && meshLadder.exactCoarseError > 0.0f) {
        const void *tsh = cShape.TShape().get();
        onDemote = [this, tsh]() {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh)
                return;
            if (!demoteMeshLevels(cur))
                return;
            if (meshLadder.anchor == tsh) {
                meshLadder.exactResident = false;
                meshLadder.residentLanded = true;
            }
            updateVisual();
        };
        onDowngrade = [this, tsh]() {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh)
                return;
            if (!downgradeMeshLevels(cur))
                return;
            if (meshLadder.anchor == tsh) {
                meshLadder.exactResident = false;
                meshLadder.residentLanded = true;
            }
            updateVisual();
        };
    }
    // The dynamic-scale descent (sec 13): what this object can still
    // give up once it is already showing its coarse rung and the
    // budget is not met. Armed only on a coarse-built source with
    // a scale to apply -- an exact-resident one has the ordinary
    // demote/downgrade above, which is cheaper and comes first.
    std::function<void()> onScaleDown;
    float scaledError = 0.0f;
    const double levelScale = Gui::RenderParams::getLevelScale();
    if (builtError > 0.0f && levelScale > 1.0 && shapeDiag > 0.0) {
        scaledError = float(builtError * levelScale);
        const void *tsh = cShape.TShape().get();
        const double nextScale =
            std::max(1.0, meshLadder.errorScale) * levelScale;
        const double nextDefl = deflection * levelScale;
        const double nextAng = std::min(AngDeflectionRads * levelScale,
                                        M_PI / 2.0);
        const double boxError = Gui::RenderParams::getLevelScaleBoxError();
        onScaleDown = [this, tsh, nextScale, nextDefl, nextAng,
                       scaledError, boxError]() {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh)
                return;
            // Past the box error, or once deflection has proved it
            // cannot coarsen this shape, re-tessellating buys
            // nothing: only a representation that drops FACES does.
            if (meshLadder.scaleSpent != ScaleSpent::No
                || (boxError > 0.0 && scaledError >= boxError)) {
                // The scale still advances here, synchronously: no
                // build can fail to land, and it is what grows the
                // decimation rung's grid step over step.
                meshLadder.errorScale = nextScale;
                // The threshold is a CHOICE, not a proof, and must
                // not overwrite one: a Proved shape stays Proved.
                if (meshLadder.scaleSpent == ScaleSpent::No)
                    meshLadder.scaleSpent = ScaleSpent::BoxChosen;
                if (meshLadder.decimationSpent) {
                    // The bounding box -- 12 triangles, built inline:
                    // the one visible descent cheap enough to stay
                    // synchronous. updateVisual takes that path off
                    // the flags.
                    updateVisual();
                    return;
                }
                // The decimation rung: the hook only ENQUEUES -- the
                // clustering runs on the refine pool over a snapshot,
                // and the landing rewrites the nodes and re-arms
                // (sec 13c). Running it here, in the plan callback,
                // froze the GUI for minutes when a live budget drop
                // ordered hundreds of steps back-to-back.
                queueDecimationDescent();
                return;
            }
            // Otherwise mesh a copy coarser on the refine pool and
            // adopt it: the transfer brings it in beside the finer
            // rung and the demote, which keeps the fewest nodes,
            // drops that rung and its edge polygons.
            //
            // The scale is committed in the APPLY, not here. A job
            // the worker drops at its memory floor, or that a
            // re-registration cancels, would otherwise leave the
            // ladder's state one rung below the mesh still
            // displayed -- and the exhaustion proof below never
            // evaluated for that step.
            queueMeshLevelBuild(
                faceset ? static_cast<SoNode *>(faceset)
                        : static_cast<SoNode *>(lineset),
                cur, nextDefl, nextAng,
                [this, tsh, nextScale](const TopoDS_Shape &meshed) {
                    TopoDS_Shape live = cachedShape.getShape();
                    if (live.IsNull() || live.TShape().get() != tsh)
                        return;
                    if (meshLadder.anchor == tsh)
                        meshLadder.errorScale = nextScale;
                    const int before = meshLevelNodeCount(live);
                    transferMeshLevels(meshed, live);
                    demoteMeshLevels(live);
                    const int after = meshLevelNodeCount(live);
                    // Did asking for a coarser mesh actually
                    // produce one? A shape of planar faces answers
                    // no at every deflection, and there is no point
                    // discovering that once per step.
                    if (before > 0 && after * 10 >= before * 9
                        && meshLadder.anchor == tsh)
                        meshLadder.scaleSpent = ScaleSpent::Proved;
                    if (meshLadder.anchor == tsh)
                        meshLadder.residentLanded = true;
                    updateVisual();
                });
        };
    }
    registerMeshLevelSource(cShape, NormalsFromUV, faceset, lineset,
                            builtError, exactDeflection, exactAngle,
                            std::move(onExact), std::move(onDemote),
                            exactResident ? meshLadder.exactCoarseError
                                          : 0.0f,
                            std::move(onDowngrade),
                            pcObject ? pcObject->getDocument() : nullptr,
                            "per-object", std::move(onScaleDown),
                            scaledError);
}

void ViewProviderPartExt::queueDecimationDescent()
{
    TopoDS_Shape cur = cachedShape.getShape();
    if (cur.IsNull() || meshLadder.anchor != cur.TShape().get())
        return;
    const double shapeDiag = meshLadder.shapeDiag;
    const int coarseLvl = meshLadder.coarseLevel;
    float builtError = 0.0f;
    if (coarseLvl >= 0 && shapeDiag > 0.0) {
        const double scale = std::max(1.0, meshLadder.errorScale);
        builtError = float(scale / double(8u << unsigned(coarseLvl)));
    }
    const double cellSize = double(builtError) * shapeDiag;
    // A rung that cannot run at all is decimation spent: the next
    // order takes the box. Re-arming is what keeps the source
    // reachable -- the order that got here consumed its hook.
    auto spent = [this]() {
        meshLadder.decimationSpent = true;
        FC_LOG(getFullName() << " decimation spent, bounding box is next");
        armMeshLevelSource();
    };
    if (!Gui::RenderParams::getSimplifyExhausted() || !(cellSize > 0.0)
        || !coords || !faceset) {
        spent();
        return;
    }
    auto snap = std::make_shared<OwnedMeshSnapshot>();
    if (!snap->build(coords, norm, faceset, lineset, nodeset, pcoords,
                     texcoords)) {
        spent();
        return;
    }
    Render::SimplifyOptions opts;
    opts.weldAcrossParts = Gui::RenderParams::getSimplifyMergeParts();
    const void *tsh = meshLadder.anchor;
    const size_t beforeIndexCount = snap->tris.size();
    const float builtErrorNow = builtError;
    const double diag = shapeDiag;
    SoNode *tag = faceset ? static_cast<SoNode *>(faceset)
                          : static_cast<SoNode *>(lineset);
    queueMeshDescentWork(
        tag,
        [this, tsh, snap, cellSize, opts, beforeIndexCount, builtErrorNow,
         diag]() -> std::function<void()> {
            // The worker half: pure clustering over the snapshot.
            auto out = std::make_shared<Render::SimplifiedMesh>();
            auto stats = std::make_shared<Render::SimplifyStats>();
            const bool ok = Render::simplifyMesh(snap->mesh, float(cellSize),
                                                 *out, opts, stats.get());
            // The landing half: node writes and the re-arm, marshalled
            // to the GUI thread; it runs only while the registration
            // that queued this is still the live one (the worker
            // queue's token), which is what makes `this` sound here --
            // the destructor unregisters, and unregistering cancels.
            return [this, tsh, ok, out, stats, cellSize, beforeIndexCount,
                    builtErrorNow, diag]() {
                TopoDS_Shape live = cachedShape.getShape();
                if (live.IsNull() || live.TShape().get() != tsh
                    || meshLadder.anchor != tsh)
                    return;
                // Re-arm FIRST: the registration publishes the nominal
                // rung error, and the apply's restatement (the
                // measured one, setPublishedError) must land on top of
                // it, not under it -- the order updateVisual ran.
                armMeshLevelSource();
                if (!ok
                    || !applySimplifiedRung(*out, *stats, beforeIndexCount,
                                            cellSize, diag, builtErrorNow)) {
                    // Decimation is spent too. The object keeps the
                    // mesh it has; the next order finds the flag set
                    // and takes the box.
                    meshLadder.decimationSpent = true;
                    FC_LOG(getFullName()
                           << " decimation spent, bounding box is next");
                }
                else {
                    // The arrays under the highlight/selection state
                    // were rewritten; re-apply like updateVisual does.
                    applyShapeAppearance();
                    setHighlightedEdges(LineColorArray.getValues());
                    setHighlightedPoints(PointColorArray.getValue());
                    // ...and register the content the rewrite emitted
                    // from the rung (docs/WorkerVertexCache.md), ids
                    // stamped after that last touch.
                    registerPendingVisualVertexCache();
                }
            };
        });
}

bool ViewProviderPartExt::deferVisualForLoad()
{
    if (!Gui::RenderParams::getProgressiveLoad())
        return false;
    auto obj = getObject();
    auto doc = obj ? obj->getDocument() : nullptr;
    if (!doc)
        return false;
    if (!doc->testStatus(App::Document::Restoring)) {
        // The deferred view-provider drain counts as loading too: its
        // slices run with the Restoring bit clear between them, and a
        // visual built in such a gap is walked by the staging sweep that
        // follows -- the very interleaving the queue itself refuses
        // (runDeferredVisualSlice checks this same flag before building).
        // Without the same gate here, a direct updateVisual -- e.g. from
        // the camera-fit path while a second document's drain is mid-way --
        // builds into a half-staged subtree, and the content never reaches
        // the renderer: built Coin-side, never drawn.
        auto guiDoc = Gui::Application::Instance->getDocument(doc);
        if (!guiDoc || !guiDoc->isRestoringViewProviders())
            return false;
    }

    VisualTouched = true;
    if (!VisualDeferred) {
        VisualDeferred = true;
        deferredVisuals().docs[doc->getName()].pending.emplace_back(obj);
        syncBuildingVisuals();
    }
    // The restore pumps events through its progress sequencer, so a slice
    // can be posted now; it will find the document still restoring and put
    // itself off until the load has let go.
    scheduleDeferredVisualSlice();
    return true;
}

void ViewProviderPartExt::scheduleDeferredVisualSlice(int delayMs)
{
    auto &visuals = deferredVisuals();
    if (visuals.scheduled || visuals.docs.empty())
        return;
    visuals.scheduled = true;
    QTimer::singleShot(delayMs, QCoreApplication::instance(),
                       []() { ViewProviderPartExt::runDeferredVisualSlice(); });
}

void ViewProviderPartExt::runDeferredVisualSlice()
{
    auto &visuals = deferredVisuals();
    visuals.scheduled = false;
    if (visuals.docs.empty() || visuals.running)
        return;
    Base::StateLocker walking(visuals.running);

    const double budget =
        std::max(1L, Gui::RenderParams::getProgressiveLoadBudgetMS()) / 1000.0;
    auto start = std::chrono::high_resolution_clock::now();
    auto elapsed = [&start]() {
        return std::chrono::duration<double>(
                std::chrono::high_resolution_clock::now() - start);
    };

    // Which documents can be worked on at all right now. A document still
    // loading is not one of them: everything built now would only be parked
    // again. The deferred view provider drain counts as loading -- a visual
    // built between its slices is re-touched by the property sweep that
    // follows, and every action the sweep applies then walks a populated
    // node. Per document, because another document's load says nothing
    // about whether this one's visuals may be built.
    auto eligible = [](const std::string &name) {
        auto doc = App::GetApplication().getDocument(name.c_str());
        if (!doc)
            return static_cast<App::Document*>(nullptr);
        auto guiDoc = Gui::Application::Instance->getDocument(doc);
        if (doc->testStatus(App::Document::Restoring)
                || (guiDoc && guiDoc->isRestoringViewProviders()))
            return static_cast<App::Document*>(nullptr);
        return doc;
    };
    std::size_t ready = 0;
    for (const auto &e : visuals.docs) {
        if (eligible(e.first))
            ++ready;
    }
    if (!ready) {
        // Everything left belongs to a document still loading. Wait rather
        // than spin on the events its restore pumps.
        scheduleDeferredVisualSlice(100);
        return;
    }

    // Say what this drain owes and how it is going -- the visual build
    // is the longest phase of a progressive load, and it used to run
    // with a dead status bar. Created here rather than at park time, so
    // the total covers everything the load queued.
    if (!visuals.seq) {
        std::size_t total = 0;
        for (const auto &e : visuals.docs)
            total += e.second.pending.size();
        if (total)
            visuals.seq = std::make_unique<Base::SequencerLauncher>(
                    "Building visuals...", total,
                    Base::SequencerLauncher::KeepInteractive);
    }

    // One budget for the slice, split evenly between the documents that can
    // use it: what has to stay bounded is the time before the event loop
    // gets its turn back, and that is per slice however many documents are
    // draining. An even split is also what keeps two loads progressing at
    // once -- with the whole budget to the first document, a large one
    // would hold the others up until it finished, which is the
    // serialization the single shared queue used to impose.
    const double share = budget / ready;
    bool more = false;
    for (auto it = visuals.docs.begin(); it != visuals.docs.end(); ) {
        auto &queue = it->second;
        auto doc = eligible(it->first);
        if (!doc) {
            if (!App::GetApplication().getDocument(it->first.c_str())) {
                // Closed while its builds were queued: nothing left to build
                // them for, and nothing of it may resolve against a document
                // reopened under the same name.
                it = visuals.docs.erase(it);
                continue;
            }
            more = true;
            ++it;
            continue;
        }
        auto mark = elapsed();
        auto charge = [&queue, &elapsed, &mark]() {
            queue.spent += elapsed() - mark;
        };
        // This document's share, never past the slice's own end.
        const double limit = std::min(budget, mark.count() + share);
        const double left = std::max(0.001, limit - mark.count());

        // Deferred shape restore (docs/DocumentLoad.md sec 14): serve this
        // document's parked archive entries BEFORE any of its visuals is
        // built. Shapes materializing inside the visual fill was the
        // two-document lesson in reverse -- mid-drain content arriving
        // through a path the staging never audited left link snapshots and
        // renderer caches stale. Served first, a shape arrives through the
        // same property change notification an ordinary edit uses, and the
        // visual fill that follows runs with every shape present -- the
        // exact dynamics of the non-deferred load, with the reads moved off
        // the blocking open into these first slices. (The document drains
        // these on its own timer too, for the entries no queued visual
        // would ever ask about; whichever gets there first, the other finds
        // nothing left to do.)
        if (doc->serveDeferredFiles(left)) {
            charge();
            more = true;
            ++it;
            continue;
        }

        ++queue.slices;
        while (!queue.pending.empty()) {
            auto obj = queue.pending.front().getObject();
            queue.pending.pop_front();
            if (visuals.seq)
                visuals.seq->next();
            auto vp = obj ? Base::freecad_dynamic_cast<ViewProviderPartExt>(
                                Gui::Application::Instance->getViewProvider(obj))
                          : nullptr;
            if (vp && vp->VisualDeferred) {
                vp->VisualDeferred = false;
                // Something may have built it in the meantime -- counted
                // only when this slice is what built it, or the line would
                // report work it never did.
                if (vp->VisualTouched) {
                    // Under the drain flag, so an oversized bare shape
                    // may take the stand-in path instead of an inline
                    // tessellation (see s_drainVisualBuild).
                    Base::StateLocker drainBuild(s_drainVisualBuild);
                    vp->updateVisual();
                    ++queue.built;
                }
            }
            if (elapsed().count() >= limit)
                break;
        }
        charge();

        if (!queue.pending.empty()) {
            more = true;
            ++it;
            continue;
        }
        FC_LOG("progressive load " << it->first << ": " << queue.built
                << " visuals in " << queue.slices << " slices, "
                << queue.spent.count() << 's');
        it = visuals.docs.erase(it);
    }

    // After every erase this slice made, so the state falls on the slice
    // that empties the queue -- the frame after it is the first one that
    // may draw the elements the load gate was holding back.
    syncBuildingVisuals();

    if (visuals.docs.empty())
        visuals.seq.reset();

    if (more)
        scheduleDeferredVisualSlice();
}

namespace {

/// The projection frame of one face, as the three SbVec4f the render
/// material carries -- (origin, kind), (axis, radius), (xdir, spare),
/// matching Render::SurfaceFrame.
///
/// A surface finish is a pattern the tool left, and the tool worked in
/// the surface's own frame: a plane's axes, or the axis a cylinder or
/// cone was turned about. Read here because this is the last place the
/// analytic OCCT surface is in hand -- past this point there is only a
/// triangle soup, and the renderer's fallback is to guess the frame from
/// the object-space normal (fc_finish.sh's triplanar projection), which
/// cannot know that a cylinder has an axis.
///
/// Answers false for a surface neither planar nor a surface of
/// revolution -- a freeform patch has no frame worth stating, and no
/// finish anyone specifies wants one.
///
/// WARNING: the frame is CANONICALIZED, and that is what keeps the palette
/// small enough to be worth having: the origin's component along the
/// axis is dropped for a plane (invisible: the pattern is in-plane) and
/// set to the axis point nearest the object origin for a radial one
/// (which only shifts the axial phase), and the axis sign is fixed. So
/// coplanar faces, parallel faces and the coaxial cylinders of a stepped
/// shaft all collapse onto one entry, rather than each spending one of
/// the sixteen a draw may hold.
bool faceProjectionFrame(const TopoDS_Face &face, SbVec4f out[3])
{
    TopLoc_Location loc;
    if (BRep_Tool::Surface(face, loc).IsNull())
        return false;   // purely triangulated: no analytic surface at all

    gp_Pnt origin;
    gp_Dir axis;
    gp_Dir xdir;
    float radius = 0.0f;
    float kind = 0.0f;
    try {
        // Restricted to the face, so a cone's radius below is measured
        // over the part of it this face actually is.
        BRepAdaptor_Surface adapt(face);
        switch (adapt.GetType()) {
        case GeomAbs_Plane: {
            const gp_Ax3 &pos = adapt.Plane().Position();
            origin = pos.Location();
            axis = pos.Direction();
            xdir = pos.XDirection();
            kind = 1.0f;    // Render::SurfaceFrame::Planar
            break;
        }
        case GeomAbs_Cylinder: {
            const gp_Cylinder cyl = adapt.Cylinder();
            const gp_Ax3 &pos = cyl.Position();
            origin = pos.Location();
            axis = pos.Direction();
            xdir = pos.XDirection();
            radius = float(cyl.Radius());
            kind = 2.0f;    // Render::SurfaceFrame::Radial
            break;
        }
        case GeomAbs_Cone: {
            const gp_Cone cone = adapt.Cone();
            const gp_Ax3 &pos = cone.Position();
            origin = pos.Location();
            axis = pos.Direction();
            xdir = pos.XDirection();
            // R(v) = RefRadius + v * sin(semi-angle): the radius at the
            // middle of the face, which is the circumference the pattern
            // period is fitted to. A cone's is not constant, so the
            // pattern stretches a little toward the wide end -- which is
            // what a knurl rolled onto a taper actually does.
            const double v = 0.5 * (adapt.FirstVParameter()
                                    + adapt.LastVParameter());
            radius = float(std::fabs(cone.RefRadius()
                                     + v * std::sin(cone.SemiAngle())));
            kind = 2.0f;
            break;
        }
        default:
            return false;
        }
    }
    catch (const Standard_Failure &) {
        return false;
    }

    gp_Vec vaxis(axis);
    gp_Vec vx(xdir);
    // Fix the axis sign so a face and its reversed twin -- the two sides
    // of a plate, a cylinder met from either end -- share one entry.
    const double a[3] = {vaxis.X(), vaxis.Y(), vaxis.Z()};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(a[i]) > 1.0e-9) {
            if (a[i] < 0.0) {
                vaxis.Reverse();
                vx.Reverse();
            }
            break;
        }
    }
    // Drop the component along the axis, which is the one component
    // neither kind can show: a plane's pattern is laid IN the plane, so
    // sliding the origin along its normal changes nothing, and a radial
    // frame's origin only has to name a point ON the axis -- the nearest
    // one to the object origin, which is what this leaves. Two faces
    // that agree about everything visible now agree about the frame too.
    gp_Vec vo(origin.X(), origin.Y(), origin.Z());
    vo -= vaxis * vo.Dot(vaxis);

    out[0].setValue(float(vo.X()), float(vo.Y()), float(vo.Z()), kind);
    out[1].setValue(float(vaxis.X()), float(vaxis.Y()), float(vaxis.Z()),
                    radius);
    out[2].setValue(float(vx.X()), float(vx.Y()), float(vx.Z()), 0.0f);
    return true;
}

} // anonymous namespace

void ViewProviderPartExt::updateVisual()
{
    if (!getObject()
            || !getObject()->getDocument()
            || isRestoring())
    {
        VisualTouched = true;
        return;
    }

    if (deferVisualForLoad())
        return;

    // A giant rebuild called from a pump item is deferred into its OWN
    // pump item (Render_VisualFillOnPool): the landing that called this
    // -- a climb's transfer, a demote's rung drop -- stays cheap, and
    // the capture or build that follows gets its own budget-checked
    // turn. Without this, a turn stacked small landings up to the
    // budget and then one landing's inline giant capture on top:
    // measured 9 landings and 757ms in one turn against a 50ms budget.
    // Placed before the prologue and clearInstanced so the displayed
    // representation -- instanced included -- stays intact for the
    // turn or two until the item runs. The seq bump supersedes any
    // in-flight pooled fill now, exactly as the full run would; the
    // residentLanded claim is NOT consumed here, so the re-entered
    // run finds it as this call did.
    if (Gui::RenderParams::getVisualFillOnPool()
        && inLandingPump() && !s_deferredVisualRun && !s_drainVisualBuild
        && !cachedShape.isNull() && (faceset || lineset)
        && long(cachedShape.countSubShapes(TopAbs_FACE))
            >= std::max(1L, Gui::RenderParams::getVisualFillMinFaces())) {
        ++meshLadder.visualFillSeq;
        const void *tsh = cachedShape.getShape().TShape().get();
        const unsigned seq = meshLadder.visualFillSeq;
        queueLevelGuiWork(
            faceset ? static_cast<const void *>(faceset)
                    : static_cast<const void *>(lineset),
            [this, tsh, seq]() {
                // Sound while the item lives: unregister purges by the
                // same primary tag before the owner may die. A newer
                // rebuild or decimation rewrite moved the seq and owns
                // the arrays now; a different TShape is a different
                // shape's claim.
                TopoDS_Shape live = cachedShape.getShape();
                if (live.IsNull() || live.TShape().get() != tsh
                    || meshLadder.visualFillSeq != seq)
                    return;
                Base::StateLocker deferred(s_deferredVisualRun);
                updateVisual();
            },
            /*descent*/ true);
        VisualTouched = false;
        return;
    }

    // Where a rebuild's time actually goes (#13d). Declared BEFORE the
    // build timer so it is destroyed after it and sees this build's own
    // cost, and it reports on every exit path this function has.
    VisualSplitReporter splitReport;
    // ...and where THIS build's went, if it was slow enough to matter
    // on its own (a paced descent's turn is one object's rebuild).
    SlowBuildProbe slowReport(this);
    // A restore or a live import runs this thousands of times inside another
    // stage's timing; the accumulator is what makes that share visible.
    Gui::ViewProvider::VisualBuildTimer buildTimer;

    {
        // The prologue is work proportional to what is being DISCARDED --
        // three action traversals over the nodes about to be refilled --
        // so it is timed apart from the fill: a mass descent pays it once
        // per object whether or not the new content is cheap.
        Gui::ViewProvider::VisualBuildTimer prologueTimer(
                Gui::ViewProvider::VisualPrologueTime, nullptr);
        Gui::SoUpdateVBOAction action;
        action.apply(this->faceset);

        // Clear selection
        Gui::SoSelectionElementAction saction(Gui::SoSelectionElementAction::None);
        saction.apply(this->faceset);
        saction.apply(this->lineset);
        saction.apply(this->nodeset);

        // Clear highlighting
        Gui::SoHighlightElementAction haction;
        haction.apply(this->faceset);
        haction.apply(this->lineset);
        haction.apply(this->nodeset);
    }

    // Drop any previous TShape-instanced representation; the qualifying
    // path rebuilds it below, every other path leaves only the flat
    // member nodes filled.
    auto clearInstanced = [this]() {
        if (pFaceInstRoot)
            pFaceInstRoot->removeAllChildren();
        if (pEdgeInstRoot)
            pEdgeInstRoot->removeAllChildren();
        if (pVertexInstRoot)
            pVertexInstRoot->removeAllChildren();
        instanced.reset();
    };
    clearInstanced();

    Part::TopoShape toposhape = getShape();
    // We must reset the location here because the transformation data
    // are set in the placement property
    TopLoc_Location aLoc;
    toposhape.setShape(toposhape.getShape().Located(aLoc), false);
    lineset ->seamIndices.setNum(0);
    registerShape(cachedShape, toposhape);
    // The ladder state this object carries belongs to a shape, so a
    // different TShape starts every claim over -- otherwise an edited
    // object would inherit the coarseness the plan bought against a
    // shape that is gone. THE one reset, placed before EVERY early
    // return (the null path included): a claim that survives an early
    // return can meet a recycled TShape address two shapes later, and
    // the null install is the one path that frees the old TShape
    // without installing a coexisting successor (see MeshLadderState
    // in the header).
    meshLadder.rebind(cachedShape.getShape().IsNull()
                          ? nullptr
                          : cachedShape.getShape().TShape().get());
    // The landing claim covers exactly ONE rebuild -- consumed here,
    // before every early exit, so a claim made for a build that took
    // the stand-in or instanced path (or errored out) cannot leak
    // into a later rebuild it knows nothing about.
    const bool residentLanded = meshLadder.residentLanded;
    meshLadder.residentLanded = false;
    // Whatever path this rebuild takes below -- the null install, the
    // stand-in, the instanced build, the inline or the pooled fill --
    // it owns the display arrays from here: an in-flight pooled fill
    // queued by an earlier rebuild is superseded and must not land
    // over what this one leaves (Render_VisualFillOnPool). Same for a
    // vertex-cache stash an aborted earlier epilogue left behind: it
    // mirrors arrays this rebuild is about to replace.
    ++meshLadder.visualFillSeq;
    pendingVCache.reset();
    if (cachedShape.isNull()) {
        coords  ->point      .setNum(0);
        pcoords ->point      .setNum(0);
        norm    ->vector     .setNum(0);
        texcoords->point     .setNum(0);
        faceset ->coordIndex .setNum(0);
        faceset ->partIndex  .setNum(0);
        faceset ->shapeInfo  .setNum(0);
        lineset ->coordIndex .setNum(0);
        nodeset ->startIndex .setValue(0);
        VisualTouched = false;
        return;
    }

    // Progressive import of an oversized part (sec 13): even the coarse
    // build of a many-face shape (or many-leaf compound) stalls the
    // GUI for seconds, and the import stall scales with the largest
    // single part. Build a 12-triangle bounding-box stand-in instead
    // and let the level plan run the coarse build on the refine pool.
    // ...and the same box as a DESCENT (sec 13, dynamic scale): an
    // object whose deflection can no longer be coarsened is drawn as
    // its 12-triangle box, which is the one representation here that
    // actually removes faces rather than subdividing them less.
    // The box is the LAST step, not the first one past deflection:
    // it is taken only once decimation has been tried and has itself
    // stopped paying (see simplifyVisualInPlace at the end of the
    // build, which is what sets decimationSpent).
    if (buildCoarseStandIn(meshLadder.scaleSpent != ScaleSpent::No
                           && meshLadder.decimationSpent)) {
        VisualTouched = false;
        Gui::ViewProvider::VisualBuildTimer highlightTimer(
                Gui::ViewProvider::VisualHighlightTime, nullptr);
        applyShapeAppearance();
        setHighlightedEdges(LineColorArray.getValues());
        setHighlightedPoints(PointColorArray.getValue());
        return;
    }

    // TShape-instanced build of qualifying compounds (shared sub-shape
    // tessellation under per-instance transforms); everything else runs
    // the flattened build below, unchanged. A shape whose coarse mesh
    // arrived behind a stand-in stays on the flattened build: the
    // resident triangulation was built at the whole-shape rung, and the
    // instanced build's per-leaf rungs would re-tessellate every leaf
    // inline -- the very stall the stand-in existed to avoid.
    bool instancedOk = false;
    const bool standInResolved = meshLadder.coarseResolved;
    // Scoped to the ATTEMPT alone: a failed instanced build still costs
    // its analysis, and lumping that into the flat fill below would
    // report the fallback as expensive rather than the try.
    std::optional<Gui::ViewProvider::VisualBuildTimer> instanceTimer(
            std::in_place, Gui::ViewProvider::VisualInstanceTime, nullptr);
    try {
        if (!standInResolved)
            instancedOk = buildInstanced();
    }
    catch (Base::Exception &e) {
        FC_ERR("Failed instanced representation for the shape of "
               << pcObject->getFullName() << ": " << e.what());
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Failed instanced representation for the shape of "
               << pcObject->getFullName() << ": " << e.GetMessageString());
    }
    catch (...) {
        FC_ERR("Failed instanced representation for the shape of "
               << pcObject->getFullName());
    }
    instanceTimer.reset();
    if (instancedOk) {
        VisualTouched = false;
        // The material has to be checked again (colors verified uniform)
        Gui::ViewProvider::VisualBuildTimer highlightTimer(
                Gui::ViewProvider::VisualHighlightTime, nullptr);
        applyShapeAppearance();
        setHighlightedEdges(LineColorArray.getValues());
        setHighlightedPoints(PointColorArray.getValue());
        return;
    }
    // An aborted attempt may have left partial instance wrappers behind.
    clearInstanced();

    faceset->shapeInfo.enableNotify(FALSE);
    faceset->shapeInfo.setNum(cachedShape.countSubShapes(TopAbs_SOLID));
    int i = -1;
    for (auto &s : cachedShape.getSubTopoShapes(TopAbs_SOLID)) {
        int count = s.countSubShapes(TopAbs_FACE);
        if (!count)
            continue;
        ++i;
        auto node = faceset->shapeInfo.getNode(i);
        SoFCShapeInstance *instance = nullptr;
        if (node && node->isOfType(SoFCShapeInstance::getClassTypeId()))
            instance = static_cast<SoFCShapeInstance*>(node);
        else
            instance = new SoFCShapeInstance;
        int idx = cachedShape.findShape(s.getSubShape(TopAbs_FACE, 1));
        assert(idx > 0);
        instance->partIndex = idx;
        instance->transform = convert(s.getTransform());
        auto &info = _ShapeTable[s.getShape().TShape().get()];
        assert(info.node);
        instance->shapeInfo = info.node;
        if (instance != node)
            faceset->shapeInfo.replaceNode(i, instance);
    }
    faceset->shapeInfo.enableNotify(TRUE);

    TopoDS_Shape cShape = cachedShape.getShape();

    // copy edge sub shape to work around OCC trangulation bug (in
    // case the edge is part of a face of some other shape in a
    // different location). Seems OCC 7.4 has fixed problem.
#if OCC_VERSION_HEX < 0x070400
    if (!toposhape.hasSubShape(TopAbs_FACE) && toposhape.hasSubShape(TopAbs_EDGE))
        cShape = BRepBuilderAPI_Copy(cShape).Shape();
#endif

    // time measurement and book keeping
    Base::TimeInfo start_time;
    int numTriangles=0,numNodes=0,numPoints=0,numNorms=0,numFaces=0,numEdges=0,numLines=0;

    try {
        // calculating the deflection value
        Bnd_Box bounds;
        BRepBndLib::Add(cShape, bounds);
        bounds.SetGap(0.0);
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        Standard_Real deflection = std::max(Precision::Confusion(),
            ((xMax-xMin)+(yMax-yMin)+(zMax-zMin))/300.0 *
                std::max(PartParams::getOverrideTessellation() ? PartParams::getMeshDeviation() : Deviation.getValue(),
                     PartParams::getMinimumDeviation()));

        // Since OCCT 7.6 a value of equal 0 is not allowed any more, this can happen if a single vertex
        // should be displayed.
        if (deflection < gp::Resolution()) {
            deflection = Precision::Confusion();
        }

        // For very big objects the computed deflection can become very high and thus leads to a useless
        // tessellation. To avoid this the upper limit is set to 20.0
        // See also forum: https://forum.freecad.org/viewtopic.php?t=77521
        deflection = std::min(deflection, 20.0);

        // create or use the mesh on the data structure
        Standard_Real AngDeflectionRads = std::max(Precision::Angular(),
            std::max((PartParams::getOverrideTessellation() ?
                        PartParams::getMeshAngularDeflection() : AngularDeflection.getValue()),
                      PartParams::getMinimumAngularDeflection()) / 180.0 * M_PI);

        // Coarse-first publish (docs/SceneStreaming.md sec 7): a headless
        // streaming server tessellates every shape at a ladder rung
        // instead of the full display deviation -- the exact mesh is
        // then generated on demand, where a viewer's camera asks. The
        // display parameters are kept for that on-demand build.
        double exactDeflection = deflection;
        double exactAngle = AngDeflectionRads;
        float builtError = 0.0f;
        // The desktop refine already put this very TShape's exact
        // triangulation in place (sec 13): build at the display deviation
        // -- the mesher finds the finer mesh resident and keeps it -- and
        // register at error 0. The rebind at the top of updateVisual
        // already started a different TShape over, so the claim is
        // about this shape or it is gone.
        const bool exactResident = meshLadder.exactResident;
        const int coarseLvl = exactResident
            ? -1 : coarseTessellationLevel(pcObject ? pcObject->getDocument() : nullptr);
        double shapeDiag = 0.0;
        if (coarseLvl >= 0) {
            double dx = xMax - xMin, dy = yMax - yMin, dz = zMax - zMin;
            double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
            shapeDiag = diag;
            if (diag > 0) {
                const double scale = std::max(1.0, meshLadder.errorScale);
                deflection = meshLevelDeflection(diag, unsigned(coarseLvl))
                    * scale;
                // The angular tolerance rides the same scale, clamped
                // short of the half turn at which a revolved face stops
                // being a surface at all.
                AngDeflectionRads = std::min(
                    meshLevelAngle(unsigned(coarseLvl)) * scale, M_PI / 2.0);
                builtError =
                    float(scale / double(8u << unsigned(coarseLvl)));
            }
        }

        if (levelDebugOn()
            && Gui::RenderParams::getLevelSlowBuildMS() > 0) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "asked %.3f exactResident %d coarseLvl %d scale %.1f "
                     "spent %d dec %d faces %d drain %d pump %d",
                     deflection, int(exactResident), coarseLvl,
                     meshLadder.errorScale, int(meshLadder.scaleSpent),
                     int(meshLadder.decimationSpent),
                     int(cachedShape.countSubShapes(TopAbs_FACE)),
                     int(s_drainVisualBuild), int(inLandingPump()));
            slowReport.note = buf;
        }

        // The scene server can now re-tessellate this shape at a
        // coarser deviation when a viewer asks for a declared level of
        // the meshes these nodes feed (MeshLevelSource.h). Re-runs
        // replace the previous shape under the same node tags. The
        // arming itself is armMeshLevelSource, off the context stored
        // here -- so a worker landing can re-arm the next step without
        // paying this function's rebuild. Stored BEFORE the fill (the
        // values are this build's decisions either way): the pooled
        // path below returns without filling, and its landing arms
        // off this very context.
        meshLadder.shapeDiag = shapeDiag;
        meshLadder.coarseLevel = coarseLvl;
        meshLadder.exactDefl = exactDeflection;
        meshLadder.exactAng = exactAngle;

        // The pooled fill (Render_VisualFillOnPool): a big landing
        // rebuild captures here and fills on the refine pool -- the
        // traversal fill was the landing pump's per-item floor
        // (0.3-0.65s per 15-21k-face compound) once the mesh call
        // learned to skip. Only pump items take it: an ordinary edit's
        // rebuild stays inline, as does everything below the face
        // threshold and the load-time drain (its giants have
        // stand-ins).
        const long fillMinFaces =
            std::max(1L, Gui::RenderParams::getVisualFillMinFaces());
        if (Gui::RenderParams::getVisualFillOnPool()
            && inLandingPump() && !s_drainVisualBuild
            && int(cachedShape.countSubShapes(TopAbs_FACE)) >= fillMinFaces
            && queueVisualFillOnPool(cShape, deflection, AngDeflectionRads,
                                     residentLanded, builtError, shapeDiag)) {
            // The old arrays stay on display until the landing; the
            // epilogue (arm, decimation post-step, highlight) lands
            // with them.
            VisualTouched = false;
            return;
        }

        buildVisualNodes(cShape, deflection, AngDeflectionRads, NormalsFromUV,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         numTriangles, numNodes, numPoints, numNorms,
                         numFaces, numEdges, numLines,
                         meshLadder.scaleSpent, &meshLadder,
                         residentLanded, pcRenderMaterial);
        armMeshLevelSource();

        // The rung between a spent tessellation and the bounding box
        // (docs/SceneStreaming.md #13c). Applied HERE, as a post-step of
        // the ordinary build rather than in the descent callback that
        // asked for it, so the state is durable: an updateVisual run for
        // any other reason -- a colour change, a placement edit -- would
        // otherwise rebuild this object at full detail and quietly undo
        // the descent, leaving the plan to discover the memory back and
        // ask all over again.
        //
        // The grid is the error this rung already commits, in world
        // units: builtError is relative to the diagonal and carries
        // the error scale, so each descent step clusters coarser than
        // the last and the sequence terminates.
        if (meshLadder.scaleSpent != ScaleSpent::No
                && !meshLadder.decimationSpent
                && builtError > 0.0f && shapeDiag > 0.0) {
            if (!simplifyVisualInPlace(double(builtError) * shapeDiag,
                                       shapeDiag, builtError)) {
                // Decimation is spent too. The object keeps the mesh it
                // has for now; the descent's next step finds the flag
                // set and takes the box.
                meshLadder.decimationSpent = true;
                FC_LOG(getFullName()
                       << " decimation spent, bounding box is next");
            }
        }
    }
    catch (Base::Exception &e) {
        FC_ERR("Failed to compute Inventor representation for the shape of " << pcObject->getFullName() << ": " << e.what());
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Cannot compute Inventor representation for the shape of "
               << pcObject->getFullName() << ": " << e.GetMessageString());
    }
    catch (...) {
        FC_ERR("Failed to compute Inventor representation for the shape of " << pcObject->getFullName());
    }

    // printing some information
    FC_TRACE(getFullName() << " update time: " << Base::TimeInfo::diffTimeF(start_time,Base::TimeInfo()));
    FC_TRACE("Shape tria info: Faces:" << numFaces << " Edges:" << numEdges
             << " Points:" << numPoints << " Nodes:" << numNodes
             << " Triangles:" << numTriangles << " IdxVec:" << numLines);
    VisualTouched = false;

    // The inline build emits its vertex-cache content too
    // (docs/WorkerVertexCache.md). Emitted HERE, from the final node
    // arrays, rather than inside the fill: this epilogue is where every
    // inline path arrives -- ordinary rebuild, load-time drain, coarse
    // rung, bounding-box stand-in -- and the arrays are whatever the
    // publish is about to capture, so the mirror is of the real thing
    // and not of some intermediate the post-steps below still rewrite.
    //
    // Only the POOLED fill used to emit, and it takes landing-pump
    // items alone (>= Render_VisualFillMinFaces, never the drain), so
    // an ordinary load registered nothing and every publish adopted
    // nothing -- 0 adopted of N offered, "no entry" every time.
    // Off-thread this walk is free; here it is not, so it stays behind
    // Render_WorkerVertexCache (checked inside), and it buys back more
    // than it costs: the traversal capture it replaces walks Coin's
    // generatePrimitives with a wider dedup key.
    //
    // A decimation post-step above may already have stashed exactly
    // this content from the same nodes -- do not walk them twice.
    if (!pendingVCache)
        emitVisualVertexCacheFromNodes();

    {
        // The material has to be checked again
        Gui::ViewProvider::VisualBuildTimer highlightTimer(
                Gui::ViewProvider::VisualHighlightTime, nullptr);
        applyShapeAppearance();
        setHighlightedEdges(LineColorArray.getValues());
        setHighlightedPoints(PointColorArray.getValue());
    }
    // Register once the last node touch of this rebuild is done: the
    // stamp is the node id, and the appearance/highlight writes above
    // would void an entry taken before them.
    registerPendingVisualVertexCache();
}

/// One rebuild's fill, detached from the display nodes (see the
/// header declaration). The capture pins every handle the fill
/// dereferences -- a triangulation demoted or replaced while a pooled
/// job flies cannot be freed under it; the job then lands stale and
/// loses to the generation check, but it never reads freed memory.
/// The topology of `shape` itself is immutable at runtime (runtime
/// mutation is only the triangulations and polygons attached to it),
/// so the fill walks it freely on any thread.
struct ViewProviderPartExt::VisualFillData {
    // -- captured on the GUI thread --------------------------------
    TopoDS_Shape shape;
    double deflection = 0.0;
    double angDeflectionRads = 0.0;
    bool normalsFromUV = false;
    /// The default-texture-coordinate projection frame (the shape's
    /// own bounding box). Computed at capture: the bound reads the
    /// resident triangulations, which are exactly the state another
    /// thread may swap.
    Standard_Real xMin = 0, yMin = 0, zMin = 0;
    Standard_Real xMax = 0, yMax = 0, zMax = 0;
    struct FaceMesh {
        Handle(Poly_Triangulation) mesh;
        TopLoc_Location loc;
    };
    /// Per face of the face map, in map order.
    std::vector<FaceMesh> faceMeshes;
    /// Per (face, edge) pair of the fill's own edge exploration --
    /// every edge of every face, positional, no dedup (the fill
    /// dedups) -- the polygon-on-triangulation against that face's
    /// captured mesh.
    std::vector<Handle(Poly_PolygonOnTriangulation)> facePolys;
    /// Where each face's run starts in facePolys
    /// (faceMeshes.size() + 1 entries, the last = facePolys.size()).
    std::vector<size_t> facePolyStart;
    struct EdgePoly {
        Handle(Poly_Polygon3D) poly;
        TopLoc_Location loc;
    };
    /// Per edge of the edge map, in map order; empty for edges that
    /// belong to a face (their nodes come with the face mesh).
    std::vector<EdgePoly> freeEdgePolys;
    /// Edge -> one owning face, the free-edge/seam classification;
    /// built during capture, read again by the fill.
    std::unordered_map<TopoDS_Shape, TopoDS_Face, Part::ShapeHasher,
                       Part::ShapeHasher> faceEdges;

    // -- filled on the worker (or inline) --------------------------
    std::vector<SbVec3f> verts;
    std::vector<SbVec3f> norms;
    std::vector<SbVec2f> texcoords;
    std::vector<SbVec3f> points;
    std::vector<int32_t> faceIndex;
    std::vector<int32_t> partIndex;
    std::vector<int32_t> lineIndex;
    std::vector<int32_t> seamEdges;
    int numTriangles = 0, numNodes = 0, numPoints = 0, numNorms = 0,
        numFaces = 0, numEdges = 0, numLines = 0;
    bool nodesAttachedOnly = false, linesAttachedOnly = false;
    /// The projection frames a surface finish is laid out in: the
    /// deduplicated palette and one index per face (see
    /// faceProjectionFrame). Computed with the geometry, but written
    /// into the render material by applyVisualFill -- a Coin field is
    /// the GUI thread's, and on the pooled path the fill is not on it.
    std::vector<SbVec4f> framePalette;
    std::vector<int32_t> frameIndices;
    bool anyFrame = false;

    // -- worker-emitted vertex cache content -----------------------
    // (docs/WorkerVertexCache.md) Built by the fill next to the display
    // arrays when the pooled path asked for it; the landing stamps the
    // node ids and registers them for the next publish to adopt.
    bool emitVCache = false;
    std::shared_ptr<SoFCVertexCache::PrebuiltContent> vcFace, vcLine, vcPoint;

    // -- the pooled path's bookkeeping -----------------------------
    bool failed = false;
    double captureSec = 0.0, workerSec = 0.0;
};

bool ViewProviderPartExt::captureVisualFill(const TopoDS_Shape &cShape,
        double deflection, double AngDeflectionRads,
        bool NormalsFromUV,
        ScaleSpent tessellationSpent, MeshLadderState *ladder,
        bool residentLanded, VisualFillData &data)
{
    data.shape = cShape;
    data.deflection = deflection;
    data.angDeflectionRads = AngDeflectionRads;
    data.normalsFromUV = NormalsFromUV;

    {
        // The default-texture-coordinate projection frame comes from this
        // shape's own bounding box (for the flattened build that is the
        // same whole-shape box the deflection derives from).
        Bnd_Box bounds;
        BRepBndLib::Add(cShape, bounds);
        bounds.SetGap(0.0);
        bounds.Get(data.xMin, data.yMin, data.zMin,
                   data.xMax, data.yMax, data.zMax);

        {
            // Separated from the node building around it: a mesh already
            // resident (an instance sharing this TShape, a stand-in resolved
            // by the pool) makes this call nearly free, and only the split
            // says whether a bulk fill is paying for tessellation at all.
            //
            // ...and MEASURED, it is not nearly free: 71% of a mass
            // descent's GUI-thread rebuild time was spent here, on shapes
            // the refine pool had already meshed, and HALF of those calls
            // rebuilt nothing at all -- 19ms each to conclude the mesh was
            // already right (#13d). So the call is now asked for only when
            // something might come of it: tessellationIsRedundant answers
            // the same question off the resident triangulations, and a
            // shape that is already meshed the way this rebuild wants
            // never enters OCCT at all.
            //
            // The timer spans the CHECK as well as the call it replaces.
            // A split whose mesh term excluded the thing that made the
            // mesh term small would be reporting its own success.
            Gui::ViewProvider::VisualBuildTimer meshTimer(
                    Gui::ViewProvider::VisualMeshTime, nullptr);
            const bool debugCheck = levelDebugOn();
            const bool skipRedundant = Gui::RenderParams::getMeshSkipRedundant();
            const bool skipInvariant = Gui::RenderParams::getMeshSkipInvariant();
            const bool skipLanded = Gui::RenderParams::getMeshSkipLanded();
            MeshVerdict verdict;
            // With the feature off, the check still runs under the level
            // debug flag and its answer is scored against the real call
            // below -- which is the only way "safe to skip" is ever more
            // than an argument.
            if (skipRedundant || skipInvariant || debugCheck) {
                const auto checkStart =
                    std::chrono::high_resolution_clock::now();
                verdict = tessellationIsRedundant(cShape, deflection,
                        Gui::RenderParams::getMeshSkipFinerResident());
                if (debugCheck) {
                    MeshCallProbe::Stats &ms = MeshCallProbe::stats();
                    ++ms.checks;
                    ms.timeChecking += std::chrono::duration<double>(
                        std::chrono::high_resolution_clock::now()
                        - checkStart).count();
                    if (skipRedundant && verdict.redundant())
                        ++ms.skipped;
                }
            }
            // The deflection-invariance rule (Render_MeshSkipInvariant):
            // a shape of planar faces and straight edges tessellates
            // the SAME at every deflection -- a plane deviates from its
            // triangulation by zero, a straight edge discretizes to its
            // endpoints -- so a call refused only for a deflection
            // mismatch, too fine or too coarse, would rebuild the
            // identical mesh. Classified from geometry TYPES once per
            // anchor, conservatively (anything not literally a plane or
            // a line counts as curved); residency is re-checked per
            // call, because a face with no triangulation is exactly
            // what the call would build. Immune to the exhaustion
            // proof's measured leak: the proved shapes that resume
            // coarsening at a later ask are the curved ones this
            // refuses to claim, and an all-linear mesh cannot shrink,
            // so no reclaim is ever forgone.
            const bool invariantCase =
                (verdict.why == MeshRefusal::TooFine
                 || verdict.why == MeshRefusal::TooCoarse)
                && (skipInvariant || debugCheck) && ladder
                && ladder->anchor == cShape.TShape().get();
            bool invariantSkip = false;
            if (invariantCase) {
                using MI = MeshLadderState::MeshInvariance;
                if (ladder->meshInvariance == MI::Unknown) {
                    auto classify = [&cShape]() {
                        for (TopExp_Explorer fx(cShape, TopAbs_FACE);
                             fx.More(); fx.Next()) {
                            TopLoc_Location loc;
                            Handle(Geom_Surface) surf = BRep_Tool::Surface(
                                    TopoDS::Face(fx.Current()), loc);
                            if (surf.IsNull()
                                || !surf->IsKind(STANDARD_TYPE(Geom_Plane)))
                                return MI::Varies;
                        }
                        for (TopExp_Explorer ex(cShape, TopAbs_EDGE);
                             ex.More(); ex.Next()) {
                            const TopoDS_Edge &edge =
                                TopoDS::Edge(ex.Current());
                            if (BRep_Tool::Degenerated(edge))
                                return MI::Varies;
                            Standard_Real f = 0, l = 0;
                            Handle(Geom_Curve) curve =
                                BRep_Tool::Curve(edge, f, l);
                            if (Handle(Geom_TrimmedCurve) trimmed =
                                    Handle(Geom_TrimmedCurve)::DownCast(curve))
                                curve = trimmed->BasisCurve();
                            if (curve.IsNull()
                                || !curve->IsKind(STANDARD_TYPE(Geom_Line)))
                                return MI::Varies;
                        }
                        return MI::Invariant;
                    };
                    ladder->meshInvariance = classify();
                }
                if (ladder->meshInvariance == MI::Invariant) {
                    invariantSkip = true;
                    for (TopExp_Explorer fx(cShape, TopAbs_FACE); fx.More();
                         fx.Next()) {
                        TopLoc_Location loc;
                        if (BRep_Tool::Triangulation(TopoDS::Face(fx.Current()),
                                                     loc).IsNull()) {
                            invariantSkip = false;
                            break;
                        }
                    }
                }
                if (debugCheck && skipInvariant && invariantSkip)
                    ++MeshCallProbe::stats().invariantSkipped;
            }
            // The landing rule (Render_MeshSkipLanded): this fill is
            // the display half of a landing -- the caller has just
            // installed (transfer) or re-activated (demote/downgrade)
            // the very triangulation the rebuild is to display, and on
            // every landing path the resident rung is never coarser
            // than the ask, so BRepMesh here can only validate. Keyed
            // on the PATH of this one rebuild, not on any claim about
            // the shape's history -- the exhaustion-proof leak that
            // killed the spent-keyed skip does not reach it.
            // A face with NO triangulation is claimed too: on a landing
            // it is a face the same mesher just FAILED at these very
            // parameters on the worker (the transfer moves every
            // triangulation the copy got), and BRepMesh is
            // deterministic, so the GUI-thread retry re-fails it --
            // measured as ~1s validated-only calls re-failing the same
            // faces on every landing of the biggest compounds. The
            // audit arm scores these claims like every other.
            const bool landedSkip =
                residentLanded && (skipLanded || debugCheck);
            if (debugCheck && skipLanded && landedSkip)
                ++MeshCallProbe::stats().landedSkipped;
            if (!((skipRedundant && verdict.redundant())
                  || (skipInvariant && invariantSkip)
                  || (skipLanded && landedSkip))) {
                // Behind the level debug flag, the probe says whether the
                // call REBUILDS (triangle counts move) or merely
                // validates, and what deflection it found resident
                // against the one asked for. It walks every face twice
                // more. The invariant claim rides along so the audit arm
                // (skip off, the claimed call made anyway) can score it
                // -- read its WRONG column there or not at all.
                MeshCallProbe probe(cShape, deflection, verdict,
                                    tessellationSpent,
                                    invariantSkip && !skipInvariant,
                                    landedSkip && !skipLanded);
#if OCC_VERSION_HEX >= 0x070500
                IMeshTools_Parameters meshParams;
                meshParams.Deflection = deflection;
                meshParams.Relative = Standard_False;
                meshParams.Angle = AngDeflectionRads;
                meshParams.InParallel = Standard_True;
                meshParams.AllowQualityDecrease = Standard_True;

                BRepMesh_IncrementalMesh(cShape, meshParams);
#else
                BRepMesh_IncrementalMesh(cShape, deflection, Standard_False, AngDeflectionRads, Standard_True);
#endif
            }
        }


        // The handle snapshot: everything the fill dereferences that
        // another thread could swap -- the resident triangulation of
        // each face, the polygon-on-triangulation of each (face,
        // edge) pair, and the 3D polygon of each free edge. These
        // lookups walk mutable representation lists on the TShape and
        // so belong here; everything else the fill reads is immutable
        // topology or geometry.
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(cShape, TopAbs_FACE, faceMap);
        data.faceMeshes.reserve(faceMap.Extent());
        data.facePolyStart.reserve(faceMap.Extent() + 1);
        for (int i = 1; i <= faceMap.Extent(); i++) {
            const TopoDS_Face &face = TopoDS::Face(faceMap(i));
            VisualFillData::FaceMesh fm;
            fm.mesh = Part::Tools::triangulationOfFace(
                    face, fm.loc, deflection, AngDeflectionRads);
            data.facePolyStart.push_back(data.facePolys.size());
            TopExp_Explorer xp;
            for (xp.Init(face, TopAbs_EDGE); xp.More(); xp.Next()) {
                const TopoDS_Edge &edge = TopoDS::Edge(xp.Current());
                data.faceEdges.emplace(edge, face);
                data.facePolys.push_back(fm.mesh.IsNull()
                        ? Handle(Poly_PolygonOnTriangulation)()
                        : BRep_Tool::PolygonOnTriangulation(edge, fm.mesh,
                                                            fm.loc));
            }
            data.faceMeshes.push_back(std::move(fm));
        }
        data.facePolyStart.push_back(data.facePolys.size());

        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(cShape, TopAbs_EDGE, edgeMap);
        data.freeEdgePolys.resize(edgeMap.Extent());
        for (int i = 1; i <= edgeMap.Extent(); i++) {
            const TopoDS_Edge &edge = TopoDS::Edge(edgeMap(i));
            // Note: The assumption that if for an edge BRep_Tool::Polygon3D
            // returns a valid object is wrong. This e.g. happens for ruled
            // surfaces which gets created by two edges or wires.
            // So, we have to store the hashes of the edges associated to a face.
            // If the hash of a given edge is not in this list we know it's really
            // a free edge.
            if (data.faceEdges.count(edge))
                continue;
            auto &ep = data.freeEdgePolys[i - 1];
            ep.poly = Part::Tools::polygonOfEdge(edge, ep.loc, deflection,
                                                 AngDeflectionRads);
        }
    }
    return true;
}

void ViewProviderPartExt::fillVisualArrays(VisualFillData &data)
{
    const TopoDS_Shape &cShape = data.shape;
    int &numTriangles = data.numTriangles;
    int &numNodes = data.numNodes;
    int &numPoints = data.numPoints;
    int &numNorms = data.numNorms;
    int &numFaces = data.numFaces;
    int &numEdges = data.numEdges;
    int &numLines = data.numLines;
    const bool NormalsFromUV = data.normalsFromUV;

    {
        // A face without a geometric surface is a purely triangulated one
        // (e.g. a glTF import); its stored UV nodes are real texture
        // coordinates, unlike the parametric UV nodes of a regular face.
        auto isMeshOnlyFace = [](const TopoDS_Face &face) {
            TopLoc_Location loc;
            return BRep_Tool::Surface(face, loc).IsNull();
        };

        // count triangles and nodes in the mesh
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(cShape, TopAbs_FACE, faceMap);
        for (int i=1; i <= faceMap.Extent(); i++) {
            const Handle(Poly_Triangulation) &mesh = data.faceMeshes[i-1].mesh;
            // Note: we must also count empty faces
            if (!mesh.IsNull()) {
                numTriangles += mesh->NbTriangles();
                numNodes     += mesh->NbNodes();
                numNorms     += mesh->NbNodes();
            }
            numFaces++;
        }

        // get an indexed map of edges
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(cShape, TopAbs_EDGE, edgeMap);

         // key is the edge number, value the coord indexes. This is needed to keep the same order as the edges.
        std::map<int, std::vector<int32_t> > lineSetMap;
        std::set<int>          edgeIdxSet;
        std::vector<int32_t>   &seamEdges = data.seamEdges;

        // count and index the edges
        for (int i=1; i <= edgeMap.Extent(); i++) {
            edgeIdxSet.insert(i);
            numEdges++;

            const TopoDS_Edge& aEdge = TopoDS::Edge(edgeMap(i));

            // free-edge/seam classification off the captured map (see
            // the note in captureVisualFill for why Polygon3D alone
            // cannot answer it)
            auto it = data.faceEdges.find(aEdge);
            if (it != data.faceEdges.end()) {
                if (BRep_Tool::IsClosed(aEdge, it->second))
                    seamEdges.push_back(i-1);
            } else {
                const Handle(Poly_Polygon3D) &aPoly =
                    data.freeEdgePolys[i-1].poly;
                if (!aPoly.IsNull()) {
                    int nbNodesInEdge = aPoly->NbNodes();
                    numNodes += nbNodesInEdge;
                }
            }
        }

        // create memory for the nodes and indexes
        data.verts.resize(numNodes);
        data.norms.resize(numNorms);
        data.texcoords.resize(numNodes);
        data.faceIndex.resize(numTriangles*4);
        data.partIndex.resize(numFaces);
        // get the raw memory for fast fill up
        SbVec3f* verts = data.verts.data();
        SbVec3f* norms = data.norms.data();
        SbVec2f* texcoordArr = numNodes > 0 ? data.texcoords.data() : nullptr;

        // Default texture coordinates take the shape bounding box as the
        // projection frame, the largest dimension as the texel scale
        // (uniform across faces).
        const SbVec3f bbMin((float)data.xMin, (float)data.yMin,
                            (float)data.zMin);
        float maxDim = (float)std::max({data.xMax - data.xMin,
                                        data.yMax - data.yMin,
                                        data.zMax - data.zMin});
        float invMaxDim = maxDim > 0.0f ? 1.0f / maxDim : 0.0f;
        int32_t* index = data.faceIndex.data();
        int32_t* parts = data.partIndex.data();

        // preset the normal vector with null vector
        for (int i=0;i < numNorms;i++)
            norms[i]= SbVec3f(0.0,0.0,0.0);
        for (int i=0; texcoordArr && i < numNodes; i++)
            texcoordArr[i] = SbVec2f(0.0f, 0.0f);

        // The projection frames a surface finish is laid out in, one
        // entry per DISTINCT frame and one index per face (see
        // faceProjectionFrame and SoFCRenderMaterial::framePalette).
        // Entry 0 is the FIRST face's frame, following the finish
        // palette's rule: a mesh whose per-vertex stream collapses --
        // which is what happens when every face is framed alike, the
        // ordinary case for a turned part -- carries no index to read,
        // and entry 0 is what the backend gives it.
        std::vector<SbVec4f> framePalette;
        std::vector<int32_t> frameIndices(numFaces, 0);
        bool anyFrame = false;

        int ii = 0,faceNodeOffset=0,faceTriaOffset=0;
        for (int i=1; i <= faceMap.Extent(); i++, ii++) {
            const TopoDS_Face &actFace = TopoDS::Face(faceMap(i));
            // the captured mesh of this face
            const Handle(Poly_Triangulation) &mesh = data.faceMeshes[i-1].mesh;
            const TopLoc_Location &aLoc = data.faceMeshes[i-1].loc;
            if (mesh.IsNull()) {
                parts[ii] = 0;
                continue;
            }

            // getting the transformation of the shape/face
            gp_Trsf myTransf;
            Standard_Boolean identity = true;
            if (!aLoc.IsIdentity()) {
                identity = false;
                myTransf = aLoc.Transformation();
            }

            // getting size of node and triangle array of this face
            int nbNodesInFace = mesh->NbNodes();
            int nbTriInFace   = mesh->NbTriangles();
            // check orientation
            TopAbs_Orientation orient = actFace.Orientation();

            // The face's projection frame, deduplicated into the
            // palette. An unclassifiable face states the UNFRAMED frame
            // rather than being skipped, so that it keeps its place in
            // the palette's indexing and shades triplanarly.
            SbVec4f frame[3] = {SbVec4f(0.0f, 0.0f, 0.0f, 0.0f),
                                SbVec4f(0.0f, 0.0f, 1.0f, 0.0f),
                                SbVec4f(1.0f, 0.0f, 0.0f, 0.0f)};
            if (ii < int(frameIndices.size())) {
                if (faceProjectionFrame(actFace, frame))
                    anyFrame = true;
                int idx = -1;
                for (std::size_t k = 0; k < framePalette.size() && idx < 0;
                     k += 3) {
                    if (framePalette[k] == frame[0]
                            && framePalette[k + 1] == frame[1]
                            && framePalette[k + 2] == frame[2])
                        idx = int(k / 3);
                }
                if (idx < 0) {
                    // Past the cap the face takes entry 0 -- the first
                    // face's frame -- the way an overflowing finish
                    // takes the object's own.
                    if (framePalette.size() / 3
                            >= std::size_t(Render::MaxFramePalette))
                        idx = 0;
                    else {
                        idx = int(framePalette.size() / 3);
                        framePalette.insert(framePalette.end(),
                                            frame, frame + 3);
                    }
                }
                frameIndices[ii] = idx;
            }

            // purely triangulated faces carry authored texture coordinates
            // and normals in the stored mesh -- use both as-is
            bool meshOnly = isMeshOnlyFace(actFace);
            if (texcoordArr && meshOnly && mesh->HasUVNodes()) {
                for (int n = 1; n <= nbNodesInFace; n++) {
#if OCC_VERSION_HEX < 0x070600
                    const gp_Pnt2d uv = mesh->UVNodes()(n);
#else
                    const gp_Pnt2d uv = mesh->UVNode(n);
#endif
                    texcoordArr[faceNodeOffset+n-1].setValue((float)uv.X(), (float)uv.Y());
                }
            }
            bool normalsFromUV = NormalsFromUV || (meshOnly && mesh->HasNormals());


            // cycling through the poly mesh
#if OCC_VERSION_HEX < 0x070600
            const Poly_Array1OfTriangle& Triangles = mesh->Triangles();
            const TColgp_Array1OfPnt& Nodes = mesh->Nodes();
            TColgp_Array1OfDir Normals (Nodes.Lower(), Nodes.Upper());
#else
            int numNodes =  mesh->NbNodes();
            TColgp_Array1OfDir Normals (1, numNodes);
#endif
            if (normalsFromUV)
                Part::Tools::getPointNormals(actFace, mesh, Normals);

            for (int g=1;g<=nbTriInFace;g++) {
                // Get the triangle
                Standard_Integer N1,N2,N3;
#if OCC_VERSION_HEX < 0x070600
                Triangles(g).Get(N1,N2,N3);
#else
                mesh->Triangle(g).Get(N1,N2,N3);
#endif

                // change orientation of the triangle if the face is reversed
                if ( orient != TopAbs_FORWARD ) {
                    Standard_Integer tmp = N1;
                    N1 = N2;
                    N2 = tmp;
                }

                // get the 3 points of this triangle
#if OCC_VERSION_HEX < 0x070600
                gp_Pnt V1(Nodes(N1)), V2(Nodes(N2)), V3(Nodes(N3));
#else
                gp_Pnt V1(mesh->Node(N1)), V2(mesh->Node(N2)), V3(mesh->Node(N3));
#endif

                // get the 3 normals of this triangle
                gp_Vec NV1, NV2, NV3;
                if (normalsFromUV) {
                    NV1.SetXYZ(Normals(N1).XYZ());
                    NV2.SetXYZ(Normals(N2).XYZ());
                    NV3.SetXYZ(Normals(N3).XYZ());
                }
                else {
                    gp_Vec v1(V1.X(),V1.Y(),V1.Z()),
                           v2(V2.X(),V2.Y(),V2.Z()),
                           v3(V3.X(),V3.Y(),V3.Z());
                    gp_Vec normal = (v2-v1)^(v3-v1);
                    NV1 = normal;
                    NV2 = normal;
                    NV3 = normal;
                }

                // transform the vertices and normals to the place of the face
                if (!identity) {
                    V1.Transform(myTransf);
                    V2.Transform(myTransf);
                    V3.Transform(myTransf);
                    if (normalsFromUV) {
                        NV1.Transform(myTransf);
                        NV2.Transform(myTransf);
                        NV3.Transform(myTransf);
                    }
                }

                // add the normals for all points of this triangle
                norms[faceNodeOffset+N1-1] += SbVec3f(NV1.X(),NV1.Y(),NV1.Z());
                norms[faceNodeOffset+N2-1] += SbVec3f(NV2.X(),NV2.Y(),NV2.Z());
                norms[faceNodeOffset+N3-1] += SbVec3f(NV3.X(),NV3.Y(),NV3.Z());

                // set the vertices
                verts[faceNodeOffset+N1-1].setValue((float)(V1.X()),(float)(V1.Y()),(float)(V1.Z()));
                verts[faceNodeOffset+N2-1].setValue((float)(V2.X()),(float)(V2.Y()),(float)(V2.Z()));
                verts[faceNodeOffset+N3-1].setValue((float)(V3.X()),(float)(V3.Y()),(float)(V3.Z()));

                // set the index vector with the 3 point indexes and the end delimiter
                index[faceTriaOffset*4+4*(g-1)]   = faceNodeOffset+N1-1;
                index[faceTriaOffset*4+4*(g-1)+1] = faceNodeOffset+N2-1;
                index[faceTriaOffset*4+4*(g-1)+2] = faceNodeOffset+N3-1;
                index[faceTriaOffset*4+4*(g-1)+3] = SO_END_FACE_INDEX;
            }

            parts[ii] = nbTriInFace; // new part

            // handling the edges lying on this face
            TopExp_Explorer Exp;
            size_t polyCursor = data.facePolyStart[i-1];
            for(Exp.Init(actFace,TopAbs_EDGE);Exp.More();Exp.Next(),++polyCursor) {
                const TopoDS_Edge &curEdge = TopoDS::Edge(Exp.Current());
                // get the overall index of this edge
                int edgeIndex = edgeMap.FindIndex(curEdge);
                // already processed this index ?
                if (edgeIdxSet.find(edgeIndex)!=edgeIdxSet.end()) {

                    // this holds the indices of the edge's triangulation to the current polygon
                    const Handle(Poly_PolygonOnTriangulation) &aPoly =
                        data.facePolys[polyCursor];
                    if (aPoly.IsNull())
                        continue; // polygon does not exist

                    // getting the indexes of the edge polygon
                    const TColStd_Array1OfInteger& indices = aPoly->Nodes();
                    for (Standard_Integer i=indices.Lower();i <= indices.Upper();i++) {
                        int nodeIndex = indices(i);
                        int index = faceNodeOffset+nodeIndex-1;
                        lineSetMap[edgeIndex].push_back(index);

                        // usually the coordinates for this edge are already set by the
                        // triangles of the face this edge belongs to. However, there are
                        // rare cases where some points are only referenced by the polygon
                        // but not by any triangle. Thus, we must apply the coordinates to
                        // make sure that everything is properly set.
#if OCC_VERSION_HEX < 0x070600
                        gp_Pnt p(Nodes(nodeIndex));
#else
                        gp_Pnt p(mesh->Node(nodeIndex));
#endif
                        if (!identity)
                            p.Transform(myTransf);
                        verts[index].setValue((float)(p.X()),(float)(p.Y()),(float)(p.Z()));
                    }

                    // remove the handled edge index from the set
                    edgeIdxSet.erase(edgeIndex);
                }
            }

            // Default texture coordinates for regular B-Rep faces:
            // nothing else in this pipeline generates UVs for them
            // (Coin's default texgen never runs for the Brep face sets),
            // so a textured Part shape used to sample one constant
            // texel. Project the face along the dominant axis of its
            // accumulated normal onto the shape bounding box (box
            // mapping; the per-face node range is complete here - faces
            // share no nodes).
            if (texcoordArr && !(meshOnly && mesh->HasUVNodes())) {
                SbVec3f nsum(0.0f, 0.0f, 0.0f);
                for (int n = 0; n < nbNodesInFace; n++)
                    nsum += norms[faceNodeOffset + n];
                float ax = fabsf(nsum[0]);
                float ay = fabsf(nsum[1]);
                float az = fabsf(nsum[2]);
                int axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
                int i0 = axis == 0 ? 1 : 0;
                int i1 = axis == 2 ? 1 : 2;
                for (int n = 0; n < nbNodesInFace; n++) {
                    const SbVec3f &p = verts[faceNodeOffset + n];
                    texcoordArr[faceNodeOffset + n].setValue(
                        (p[i0] - bbMin[i0]) * invMaxDim,
                        (p[i1] - bbMin[i1]) * invMaxDim);
                }
            }

            // counting up the per Face offsets
            faceNodeOffset += nbNodesInFace;
            faceTriaOffset += nbTriInFace;
        }

        // The frames travel with the display arrays rather than being
        // written here: this runs on the refine pool for a pooled fill,
        // and a Coin field is the GUI thread's. applyVisualFill states
        // them on the render material.
        data.anyFrame = anyFrame;
        data.framePalette = std::move(framePalette);
        data.frameIndices = std::move(frameIndices);

        // handling of the free edges -- the polygons were captured
        // (an edge that belongs to a face has no entry: its nodes
        // came with the face mesh)
        for (int i=1; i <= edgeMap.Extent(); i++) {
            Standard_Boolean identity = true;
            gp_Trsf myTransf;

            const Handle(Poly_Polygon3D) &aPoly = data.freeEdgePolys[i-1].poly;
            if (!aPoly.IsNull()) {
                const TopLoc_Location &aLoc = data.freeEdgePolys[i-1].loc;
                if (!aLoc.IsIdentity()) {
                    identity = false;
                    myTransf = aLoc.Transformation();
                }

                const TColgp_Array1OfPnt& aNodes = aPoly->Nodes();
                int nbNodesInEdge = aPoly->NbNodes();

                gp_Pnt pnt;
                for (Standard_Integer j=1;j <= nbNodesInEdge;j++) {
                    pnt = aNodes(j);
                    if (!identity)
                        pnt.Transform(myTransf);
                    int index = faceNodeOffset+j-1;
                    verts[index].setValue((float)(pnt.X()),(float)(pnt.Y()),(float)(pnt.Z()));
                    lineSetMap[i].push_back(index);
                }

                faceNodeOffset += nbNodesInEdge;
            }
        }

        // handling of the vertices
        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(cShape, TopAbs_VERTEX, vertexMap);

        numPoints = vertexMap.Extent();
        data.points.resize(numPoints);
        verts = data.points.data();

        for (int i=0; i<numPoints; i++) {
            const TopoDS_Vertex& aVertex = TopoDS::Vertex(vertexMap(i+1));
            gp_Pnt pnt = BRep_Tool::Pnt(aVertex);
            verts[i].setValue((float)(pnt.X()),(float)(pnt.Y()),(float)(pnt.Z()));
        }

        // Which of these two drawables the display may suppress under
        // memory pressure (docs/SceneStreaming.md #13b). OCCT answers
        // it: a vertex with no edge among its ancestors floats, an edge
        // with no face among its ancestors floats, and a drawable is
        // suppressable only when NOTHING in it floats -- a point cloud,
        // a wire, a sketch or a datum line is then never dropped,
        // because nothing else on screen would show it.
        //
        // All or nothing per drawable, deliberately: objects are in
        // practice either all floating or none, so a per-element subset
        // would buy nothing measurable and cost an index permutation --
        // and the coordinate order is the picking identity (the vertex
        // number is getCoordinateIndex() - startIndex + 1), which such
        // a permutation would silently break.
        auto nothingFloats = [&cShape](TopAbs_ShapeEnum of,
                                       TopAbs_ShapeEnum in) {
            TopTools_IndexedDataMapOfShapeListOfShape ancestors;
            TopExp::MapShapesAndAncestors(cShape, of, in, ancestors);
            // MEASURED: thousands of point sets come out of here
            // "floating" during a load and "attached" once settled, on
            // the same objects -- so which of the two roads to false
            // was taken is the whole question. An EMPTY map is "there
            // is nothing here to judge", which is not the same claim as
            // "something in here floats", and only the second one means
            // the drawable must always draw.
            if (ancestors.IsEmpty()) {
                if (Gui::RenderParams::getLevelDebug()) {
                    static std::map<int, size_t> empties;
                    const size_t n = ++empties[int(of)];
                    if ((n & (n - 1)) == 0)
                        Base::Console().Message(
                            "render levels: element class: %s ancestor map "
                            "EMPTY -- cannot judge, answering 'floats' "
                            "(x%zu)\n",
                            of == TopAbs_VERTEX ? "VERTEX/EDGE"
                                                : "EDGE/FACE", n);
                }
                return false;
            }
            for (int i = 1; i <= ancestors.Extent(); ++i) {
                if (ancestors.FindFromIndex(i).IsEmpty()) {
                    if (Gui::RenderParams::getLevelDebug()) {
                        static std::map<int, size_t> floats;
                        const size_t n = ++floats[int(of)];
                        if ((n & (n - 1)) == 0)
                            Base::Console().Message(
                                "render levels: element class: %s a real "
                                "floating element (x%zu)\n",
                                of == TopAbs_VERTEX ? "VERTEX/EDGE"
                                                    : "EDGE/FACE", n);
                    }
                    return false;
                }
            }
            return true;
        };
        data.nodesAttachedOnly = nothingFloats(TopAbs_VERTEX, TopAbs_EDGE);
        data.linesAttachedOnly = nothingFloats(TopAbs_EDGE, TopAbs_FACE);

        // normalize all normals
        for (int i = 0; i< numNorms ;i++)
            norms[i].normalize();

        std::vector<int32_t> &lineSetCoords = data.lineIndex;
        for (const auto & it : lineSetMap) {
            lineSetCoords.insert(lineSetCoords.end(), it.second.begin(), it.second.end());
            lineSetCoords.push_back(-1);
        }
        numLines = lineSetCoords.size();
    }

    if (data.emitVCache)
        emitVisualVertexCache(data);
}

namespace {

/// The capture this replaces walks the primitives Coin generates from
/// the display arrays and dedups vertices in first-seen order
/// (SoFCVertexCache::addTriangle/addLine/addPoint). Replicate that
/// walk over the same arrays. The dedup key is (position, normal):
/// everything else in the capture's key -- color, texture
/// coordinates, marker -- is constant under the uniform-color
/// contract the adoption checks, and a constant key member cannot
/// split vertices.
EmittedVCache emitVCacheCore(const SbVec3f *verts,
                             const SbVec3f *norms,
                             const int32_t *faceIndex, std::size_t nFaceIndex,
                             const int32_t *lineIndex, std::size_t nLineIndex,
                             const SbVec3f *points, std::size_t nPoints)
{
    struct VKey {
        SbVec3f pos;
        SbVec3f normal;
        bool operator==(const VKey &o) const {
            return this->pos == o.pos && this->normal == o.normal;
        }
    };
    struct VKeyHash {
        std::size_t operator()(const VKey &k) const {
            std::size_t seed = 0;
            auto h = [&seed](float f) {
                // std::hash<float> hashes -0.0f and 0.0f alike, matching
                // the float equality the capture's dedup uses.
                seed ^= std::hash<float>()(f) + 0x9e3779b9
                        + (seed << 6) + (seed >> 2);
            };
            h(k.pos[0]); h(k.pos[1]); h(k.pos[2]);
            h(k.normal[0]); h(k.normal[1]); h(k.normal[2]);
            return seed;
        }
    };
    typedef std::unordered_map<VKey, int32_t, VKeyHash> VertexMap;

    EmittedVCache emitted;

    // Triangles: coordIndex quads (v0 v1 v2 -1) in order; the normal is
    // per-vertex-indexed off the same index (the norm node's array).
    if (nFaceIndex && verts && norms) {
        auto content = std::make_shared<SoFCVertexCache::PrebuiltContent>();
        VertexMap vmap;
        content->triangleindices.reserve(nFaceIndex / 4 * 3);
        for (std::size_t i = 0; i + 3 < nFaceIndex; i += 4) {
            for (int k = 0; k < 3; ++k) {
                const int32_t idx = faceIndex[i + k];
                VKey key{verts[idx], norms[idx]};
                auto res = vmap.emplace(key,
                        (int32_t)content->vertices.size());
                if (res.second) {
                    content->vertices.push_back(key.pos);
                    content->normals.push_back(key.normal);
                }
                content->triangleindices.push_back(res.first->second);
            }
        }
        emitted.face = std::move(content);
    }

    // Lines: -1-separated polylines; the capture sees one segment per
    // consecutive pair, tagged with the polyline ordinal, and a
    // constant normal (no normal node scopes over the edge root -- the
    // cache's normal array is truncated at close, so none is emitted).
    if (nLineIndex && verts) {
        auto content = std::make_shared<SoFCVertexCache::PrebuiltContent>();
        VertexMap vmap;
        int32_t run = 0;
        int32_t prev = -1;
        for (std::size_t i = 0; i < nLineIndex; ++i) {
            const int32_t v = lineIndex[i];
            if (v < 0) {
                ++run;
                prev = -1;
                continue;
            }
            if (prev >= 0) {
                for (int32_t idx : {prev, v}) {
                    VKey key{verts[idx], SbVec3f(0.f, 0.f, 0.f)};
                    auto res = vmap.emplace(key,
                            (int32_t)content->vertices.size());
                    if (res.second)
                        content->vertices.push_back(key.pos);
                    content->lineindices.push_back(res.first->second);
                }
                content->linepartindices.push_back(run);
            }
            prev = v;
        }
        if (!content->lineindices.empty())
            emitted.line = std::move(content);
    }

    // Points: every pcoords vertex in order, deduplicated by position
    // (coincident vertices share one cache vertex, exactly as the
    // capture merges them).
    if (nPoints && points) {
        auto content = std::make_shared<SoFCVertexCache::PrebuiltContent>();
        VertexMap vmap;
        content->pointindices.reserve(nPoints);
        for (std::size_t i = 0; i < nPoints; ++i) {
            VKey key{points[i], SbVec3f(0.f, 0.f, 0.f)};
            auto res = vmap.emplace(key, (int32_t)content->vertices.size());
            if (res.second)
                content->vertices.push_back(key.pos);
            content->pointindices.push_back(res.first->second);
        }
        emitted.point = std::move(content);
    }

    return emitted;
}

/// Emit from the DISPLAY NODES rather than from fill data. Whatever the
/// nodes hold is what the publish is about to capture, so a mirror taken
/// here is exact by construction -- after any post-step (decimation, a
/// coarse rung, a stand-in) has finished rewriting them.
EmittedVCache emitVCacheFromNodes(const SoCoordinate3 *coords,
                                  const SoCoordinate3 *pcoords,
                                  const SoNormal *norm,
                                  const SoBrepFaceSet *faceset,
                                  const SoBrepEdgeSet *lineset)
{
    if (!coords)
        return EmittedVCache();
    const SbVec3f *verts = coords->point.getValues(0);
    const int nVerts = coords->point.getNum();
    const SbVec3f *norms = norm ? norm->vector.getValues(0) : nullptr;
    const int nNorms = norm ? norm->vector.getNum() : 0;
    const int32_t *fi = faceset ? faceset->coordIndex.getValues(0) : nullptr;
    std::size_t nFi = faceset ? faceset->coordIndex.getNum() : 0;
    // The face dedup key needs per-vertex-indexed normals; without a
    // matching normal array the traversal generates a normal cache this
    // emission cannot mirror -- leave faces to the traversal capture.
    if (nNorms != nVerts)
        nFi = 0;
    // Same verdict for a node that forces UV capture (the shared
    // instanced tessellation does, so a build under an untextured
    // sharer still serves a textured one): the cache then opens with
    // texture unit 0 enabled, which is outside the prebuilt contract --
    // SoFCVertexCache::prebuiltReject() answers "texture unit" -- and
    // no texture coordinates are emitted for it to install. Emitting
    // faces here would register content that is certain to be refused.
    if (faceset && faceset->forceTexCoords.getValue())
        nFi = 0;
    const int32_t *li = lineset ? lineset->coordIndex.getValues(0) : nullptr;
    const std::size_t nLi = lineset ? lineset->coordIndex.getNum() : 0;
    const SbVec3f *pts = pcoords ? pcoords->point.getValues(0) : nullptr;
    const std::size_t nPts = pcoords ? pcoords->point.getNum() : 0;

    return emitVCacheCore(verts, norms, fi, nFi, li, nLi, pts, nPts);
}

} // anonymous namespace

void ViewProviderPartExt::emitVisualVertexCache(VisualFillData &data)
{
    EmittedVCache emitted = emitVCacheCore(
            data.verts.data(), data.norms.data(),
            data.faceIndex.data(), data.faceIndex.size(),
            data.lineIndex.data(), data.lineIndex.size(),
            data.points.data(), data.points.size());
    data.vcFace = std::move(emitted.face);
    data.vcLine = std::move(emitted.line);
    data.vcPoint = std::move(emitted.point);
}

void ViewProviderPartExt::emitVisualVertexCacheFromNodes()
{
    if (Gui::RenderParams::getWorkerVertexCache() <= 0)
        return;
    if (!coords || !faceset)
        return;
    EmittedVCache emitted =
        emitVCacheFromNodes(coords, pcoords, norm, faceset, lineset);
    if (!pendingVCache)
        pendingVCache.reset(new PendingVisualVCache);
    static_cast<EmittedVCache &>(*pendingVCache) = std::move(emitted);
}

void ViewProviderPartExt::emitAndRegisterSharedVertexCache(
        SoCoordinate3 *coords, SoCoordinate3 *pcoords, SoNormal *norm,
        SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
        SoBrepPointSet *nodeset)
{
    // The shared instanced tessellation (_InstGeomTable) has no view
    // provider -- one entry serves every sharer of the leaf, across
    // objects and documents -- so it cannot stash into pendingVCache
    // and let an epilogue register later. It registers here instead,
    // which is sound because the caller has just finished writing these
    // nodes and the stamp is taken now.
    //
    // Faces are normally absent from what this emits: the shared face
    // set forces UV capture, which puts the cache outside the prebuilt
    // contract (see emitVCacheFromNodes). Lines and points carry no
    // such field and adopt normally -- once per shared leaf, however
    // many instances reference it.
    if (Gui::RenderParams::getWorkerVertexCache() <= 0)
        return;
    if (!coords)
        return;
    EmittedVCache emitted =
        emitVCacheFromNodes(coords, pcoords, norm, faceset, lineset);
    auto reg = [](SoNode *node,
                  std::shared_ptr<SoFCVertexCache::PrebuiltContent> &c) {
        if (node && c) {
            c->nodeid = node->getNodeId();
            SoFCVertexCache::setPrebuilt(node, std::move(c));
        }
    };
    reg(faceset, emitted.face);
    reg(lineset, emitted.line);
    reg(nodeset, emitted.point);
}

void ViewProviderPartExt::registerPendingVisualVertexCache()
{
    if (!pendingVCache)
        return;
    auto reg = [](SoNode *node,
                  std::shared_ptr<SoFCVertexCache::PrebuiltContent> &c) {
        if (node && c) {
            c->nodeid = node->getNodeId();
            SoFCVertexCache::setPrebuilt(node, std::move(c));
        }
    };
    reg(faceset, pendingVCache->face);
    reg(lineset, pendingVCache->line);
    reg(nodeset, pendingVCache->point);
    pendingVCache.reset();
}

void ViewProviderPartExt::applyVisualFill(const VisualFillData &data,
        SoCoordinate3 *coords, SoCoordinate3 *pcoords,
        SoNormal *norm, SoTextureCoordinate2 *texcoords,
        SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
        SoBrepPointSet *nodeset,
        int &numTriangles, int &numNodes, int &numPoints, int &numNorms,
        int &numFaces, int &numEdges, int &numLines,
        Gui::SoFCRenderMaterial *rendermat)
{
    // The frames reach the shapes only while a finish is stated
    // somewhere -- SoFCRenderMaterial decides that, since the finish
    // is the appearance's business and this is the geometry's.
    if (rendermat) {
        if (data.anyFrame) {
            // setNum first: setValues grows a field but never
            // shrinks one, and a re-tessellation may state fewer
            // frames than the last one did.
            rendermat->framePalette.setNum(int(data.framePalette.size()));
            rendermat->framePalette.setValues(
                    0, int(data.framePalette.size()), data.framePalette.data());
            rendermat->frameIndices.setNum(int(data.frameIndices.size()));
            rendermat->frameIndices.setValues(
                    0, int(data.frameIndices.size()), data.frameIndices.data());
        }
        else {
            rendermat->framePalette.setNum(0);
            rendermat->frameIndices.setNum(0);
        }
    }

    numTriangles = data.numTriangles;
    numNodes = data.numNodes;
    numPoints = data.numPoints;
    numNorms = data.numNorms;
    numFaces = data.numFaces;
    numEdges = data.numEdges;
    numLines = data.numLines;

    // The same setNum + startEditing/finishEditing bracket the inline
    // fill always used (the finish is what notifies the render
    // caches), just over a memcpy from the detached arrays instead of
    // an in-place walk.
    coords->point.setNum(data.numNodes);
    SbVec3f *verts = coords->point.startEditing();
    if (data.numNodes > 0)
        memcpy(verts, data.verts.data(), data.numNodes * sizeof(SbVec3f));

    norm->vector.setNum(data.numNorms);
    SbVec3f *norms = norm->vector.startEditing();
    if (data.numNorms > 0)
        memcpy(norms, data.norms.data(), data.numNorms * sizeof(SbVec3f));

    texcoords->point.setNum(data.numNodes);
    if (data.numNodes > 0) {
        memcpy(texcoords->point.startEditing(), data.texcoords.data(),
               data.numNodes * sizeof(SbVec2f));
        texcoords->point.finishEditing();
    }

    faceset->coordIndex.setNum(data.numTriangles * 4);
    int32_t *index = faceset->coordIndex.startEditing();
    if (data.numTriangles > 0)
        memcpy(index, data.faceIndex.data(),
               size_t(data.numTriangles) * 4 * sizeof(int32_t));

    faceset->partIndex.setNum(data.numFaces);
    int32_t *parts = faceset->partIndex.startEditing();
    if (data.numFaces > 0)
        memcpy(parts, data.partIndex.data(),
               data.numFaces * sizeof(int32_t));

    pcoords->point.setNum(data.numPoints);
    SbVec3f *points = pcoords->point.startEditing();
    if (data.numPoints > 0)
        memcpy(points, data.points.data(),
               data.numPoints * sizeof(SbVec3f));

    if (nodeset)
        nodeset->attachedOnly = data.nodesAttachedOnly;
    if (lineset)
        lineset->attachedOnly = data.linesAttachedOnly;

    lineset->coordIndex.setNum(data.numLines);
    int32_t *lines = lineset->coordIndex.startEditing();
    if (data.numLines > 0)
        memcpy(lines, data.lineIndex.data(),
               data.numLines * sizeof(int32_t));

    // end the editing of the nodes
    coords  ->point       .finishEditing();
    pcoords ->point       .finishEditing();
    norm    ->vector      .finishEditing();
    faceset ->coordIndex  .finishEditing();
    faceset ->partIndex   .finishEditing();
    lineset ->coordIndex  .finishEditing();
    if (data.seamEdges.size())
        lineset->seamIndices.setValues(0, data.seamEdges.size(),
                                       data.seamEdges.data());
}

void ViewProviderPartExt::buildVisualNodes(const TopoDS_Shape &cShape,
        double deflection, double AngDeflectionRads,
        bool NormalsFromUV,
        SoCoordinate3 *coords, SoCoordinate3 *pcoords,
        SoNormal *norm, SoTextureCoordinate2 *texcoords,
        SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
        SoBrepPointSet *nodeset,
        int &numTriangles, int &numNodes, int &numPoints, int &numNorms,
        int &numFaces, int &numEdges, int &numLines,
        ScaleSpent tessellationSpent, MeshLadderState *ladder,
        bool residentLanded, Gui::SoFCRenderMaterial *rendermat)
{
    // Everything this function does, tessellation INCLUDED -- the mesh
    // accumulator nests inside this one, and the reporter subtracts it
    // to state the traversal alone (#13d). The capture/fill/apply
    // split exists for the pooled path (Render_VisualFillOnPool);
    // here all three run inline and the timing reads as it always
    // did.
    Gui::ViewProvider::VisualBuildTimer fillTimer(
            Gui::ViewProvider::VisualFillTime, nullptr);

    VisualFillData data;
    if (!captureVisualFill(cShape, deflection, AngDeflectionRads,
                           NormalsFromUV, tessellationSpent, ladder,
                           residentLanded, data))
        return;
    fillVisualArrays(data);
    applyVisualFill(data, coords, pcoords, norm, texcoords,
                    faceset, lineset, nodeset,
                    numTriangles, numNodes, numPoints, numNorms,
                    numFaces, numEdges, numLines, rendermat);
}

bool ViewProviderPartExt::queueVisualFillOnPool(const TopoDS_Shape &cShape,
        double deflection, double angDeflectionRads, bool residentLanded,
        float builtError, double shapeDiag)
{
    auto data = std::make_shared<VisualFillData>();
    const auto capture0 = std::chrono::steady_clock::now();
    try {
        // The capture is GUI-thread traversal and reports as such --
        // the slow-build line of a pooled rebuild shows exactly what
        // the GUI still pays.
        Gui::ViewProvider::VisualBuildTimer fillTimer(
                Gui::ViewProvider::VisualFillTime, nullptr);
        if (!captureVisualFill(cShape, deflection, angDeflectionRads,
                               NormalsFromUV, meshLadder.scaleSpent,
                               &meshLadder, residentLanded, *data))
            return false;
        // The worker also emits the vertex-cache content of the
        // drawables (docs/WorkerVertexCache.md); the landing registers
        // it for the next publish to adopt in place of the traversal
        // capture. The param is read here on the GUI thread.
        data->emitVCache = Gui::RenderParams::getWorkerVertexCache() > 0;
    }
    catch (const Standard_Failure &) {
        // The inline fill runs into the same failure under
        // updateVisual's own catch, which is where it reports.
        return false;
    }
    catch (const std::bad_alloc &) {
        return false;
    }
    data->captureSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - capture0).count();

    const void *tsh = cShape.TShape().get();
    const unsigned seq = meshLadder.visualFillSeq;
    const float builtErrorNow = builtError;
    const double diag = shapeDiag;
    queueMeshDescentWork(
        // The coords node: this job family's OWN token slot. On the
        // faceset tag it would supersede -- and be superseded by --
        // the decimation and coarser-mesh jobs; on its own tag the
        // only supersession is a newer fill for the same object,
        // which is exactly the semantics the generation check wants.
        coords,
        [this, data, tsh, seq, builtErrorNow, diag]()
                -> std::function<void()> {
            // The worker half: the fill over the captured handles and
            // the immutable topology -- no Coin nodes, no document.
            const auto t0 = std::chrono::steady_clock::now();
            try {
                fillVisualArrays(*data);
            }
            catch (...) {
                data->failed = true;
            }
            data->workerSec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            // The landing half: array writes plus the epilogue the
            // rebuild skipped. It runs only while the worker queue's
            // token is live -- the destructor cancels the coords tag,
            // which is what makes `this` sound -- and only while this
            // fill is still the newest owner of the arrays.
            return [this, data, tsh, seq, builtErrorNow, diag]() {
                TopoDS_Shape live = cachedShape.getShape();
                if (live.IsNull() || live.TShape().get() != tsh
                    || meshLadder.anchor != tsh
                    || meshLadder.visualFillSeq != seq)
                    return;
                if (data->failed) {
                    // Keep the arrays on display; arming keeps the
                    // object reachable so the plan can order again.
                    armMeshLevelSource();
                    return;
                }
                const auto apply0 = std::chrono::steady_clock::now();
                Gui::ViewProvider::VisualBuildTimer buildTimer;
                {
                    // Prologue parity: updateVisual ran its own at
                    // queue time, but the per-element selection and
                    // highlight indices refer to the arrays being
                    // replaced NOW.
                    Gui::ViewProvider::VisualBuildTimer prologueTimer(
                            Gui::ViewProvider::VisualPrologueTime, nullptr);
                    Gui::SoUpdateVBOAction action;
                    action.apply(this->faceset);
                    Gui::SoSelectionElementAction saction(
                            Gui::SoSelectionElementAction::None);
                    saction.apply(this->faceset);
                    saction.apply(this->lineset);
                    saction.apply(this->nodeset);
                    Gui::SoHighlightElementAction haction;
                    haction.apply(this->faceset);
                    haction.apply(this->lineset);
                    haction.apply(this->nodeset);
                }
                int nt = 0, nn = 0, np = 0, nno = 0;
                int nf = 0, ne = 0, nl = 0;
                {
                    Gui::ViewProvider::VisualBuildTimer fillTimer(
                            Gui::ViewProvider::VisualFillTime, nullptr);
                    applyVisualFill(*data, coords, pcoords, norm, texcoords,
                                    faceset, lineset, nodeset,
                                    nt, nn, np, nno, nf, ne, nl,
                                    pcRenderMaterial);
                }
                armMeshLevelSource();
                // The decimation post-step, exactly as the inline
                // build runs it (see updateVisual): a state that says
                // "deflection is spent" must decimate what it just
                // displayed or quietly undo the descent.
                bool decimationRewrote = false;
                if (meshLadder.scaleSpent != ScaleSpent::No
                    && !meshLadder.decimationSpent
                    && builtErrorNow > 0.0f && diag > 0.0) {
                    if (!simplifyVisualInPlace(double(builtErrorNow) * diag,
                                               diag, builtErrorNow)) {
                        meshLadder.decimationSpent = true;
                        FC_LOG(getFullName()
                               << " decimation spent, bounding box is next");
                    }
                    else
                        decimationRewrote = true;
                }
                {
                    Gui::ViewProvider::VisualBuildTimer highlightTimer(
                            Gui::ViewProvider::VisualHighlightTime, nullptr);
                    applyShapeAppearance();
                    setHighlightedEdges(LineColorArray.getValues());
                    setHighlightedPoints(PointColorArray.getValue());
                }
                // Stash the worker-emitted content unless a decimation
                // rewrite replaced the arrays it mirrors -- the rewrite
                // (applySimplifiedRung) re-emitted from the final
                // arrays into the same stash -- then register, id
                // stamped LAST: any later touch of a node voids that
                // node's entry at adoption.
                if (!decimationRewrote
                    && (data->vcFace || data->vcLine || data->vcPoint)) {
                    if (!pendingVCache)
                        pendingVCache.reset(new PendingVisualVCache);
                    pendingVCache->face = std::move(data->vcFace);
                    pendingVCache->line = std::move(data->vcLine);
                    pendingVCache->point = std::move(data->vcPoint);
                }
                registerPendingVisualVertexCache();
                if (levelDebugOn()
                    && Gui::RenderParams::getLevelSlowBuildMS() > 0) {
                    const double applySec = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - apply0).count();
                    const double slow =
                        Gui::RenderParams::getLevelSlowBuildMS() / 1000.0;
                    if (data->captureSec + data->workerSec + applySec
                            >= slow)
                        Base::Console().Message(
                            "pooled fill: %s capture %.3f + worker %.3f "
                            "+ apply %.3f s, faces %d\n",
                            getFullName().c_str(), data->captureSec,
                            data->workerSec, applySec, data->numFaces);
                }
            };
        });
    return true;
}

void ViewProviderPartExt::forceUpdate(bool enable) {
    if(enable) {
        if(++forceUpdateCount == 1) {
            if(!isShow() && VisualTouched)
                updateVisual();
        }
    }else if(forceUpdateCount)
        --forceUpdateCount;
}

PyObject* ViewProviderPartExt::getPyObject()
{
    if (!pyViewObject)
        pyViewObject = new ViewProviderPartExtPy(this);
    pyViewObject->IncRef();
    return pyViewObject;
}

void ViewProviderPartExt::enableFullSelectionHighlight(bool face, bool line, bool point)
{
    if(!face) 
        faceset->highlightIndices.setValue(-1);
    else if (faceset->highlightIndices.getNum()==1 && faceset->highlightIndices[0]==-1)
        faceset->highlightIndices.setNum(0);
    if(!line) 
        lineset->highlightIndices.setValue(-1);
    else if (lineset->highlightIndices.getNum()==1 && lineset->highlightIndices[0]==-1)
        lineset->highlightIndices.setNum(0);
    if(!point) 
        nodeset->highlightIndices.setValue(-1);
    else if (nodeset->highlightIndices.getNum()==1 && nodeset->highlightIndices[0]==-1)
        nodeset->highlightIndices.setNum(0);
}

void ViewProviderPartExt::beforeDelete()
{
    setStatus(Gui::Detach, true);
    // Drop any worker-emitted vertex-cache content still waiting for a
    // publish that will never come (docs/WorkerVertexCache.md) -- the
    // registry must not hold arrays for nodes about to die.
    SoFCVertexCache::setPrebuilt(faceset, nullptr);
    SoFCVertexCache::setPrebuilt(lineset, nullptr);
    SoFCVertexCache::setPrebuilt(nodeset, nullptr);
    inherited::beforeDelete();
    // clear coin nodes to free up some memory
    updateVisual();
}

void ViewProviderPartExt::reattach(App::DocumentObject *obj)
{
    inherited::reattach(obj);
    if(isUpdateForced() || Visibility.getValue()) 
        updateVisual();
}

bool ViewProviderPartExt::getFaceWeights(std::vector<double> &weights) const
{
    const Part::TopoShape shape = getShape();
    if (shape.isNull())
        return false;
    // In the order the appearance's entries are in, which is the order the
    // faces are explored in everywhere else here
    weights.clear();
    weights.reserve(static_cast<std::size_t>(ShapeAppearance.getSize()));
    try {
        for (TopExp_Explorer it(shape.getShape(), TopAbs_FACE); it.More(); it.Next()) {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(it.Current(), props);
            weights.push_back(props.Mass());
        }
    }
    catch (const Standard_Failure &) {
        // A face OCCT cannot measure is not a reason to lose the whole
        // heuristic; the entry count decides instead
        weights.clear();
        return false;
    }
    return !weights.empty();
}

void ViewProviderPartExt::finishRestoring()
{
    inherited::finishRestoring();

    auto syncMaterial = [](const App::MaterialAppearance &Mat, SoMaterial *pcMaterial) {
        pcMaterial->ambientColor.setValue(Mat.ambientColor.r,Mat.ambientColor.g,Mat.ambientColor.b);
        pcMaterial->specularColor.setValue(Mat.specularColor.r,Mat.specularColor.g,Mat.specularColor.b);
        pcMaterial->emissiveColor.setValue(Mat.emissiveColor.r,Mat.emissiveColor.g,Mat.emissiveColor.b);
        pcMaterial->shininess.setValue(Mat.shininess);
        if (pcMaterial->diffuseColor.getNum() == 1)
            pcMaterial->transparency.setValue(Mat.transparency);
    };
    syncMaterial(LineMaterial.getValue(), pcLineMaterial);
    syncMaterial(PointMaterial.getValue(), pcPointMaterial);
    syncMaterial(ShapeAppearance.getBase(), pcShapeMaterial);

    if(VisualTouched && (isUpdateForced() || Visibility.getValue()))
        updateVisual();
}

Base::BoundBox3d
ViewProviderPartExt::_getBoundingBox(const char *subname,
                                     const Base::Matrix4D *mat,
                                     bool transform,
                                     const Gui::View3DInventorViewer *view,
                                     int depth) const
{
    // A bounds question must not BUILD THE VISUAL. It used to: a
    // camera fit or animation start right after a load walked the
    // whole scene through getSceneBoundBox, and every shape whose
    // build was still pending tessellated inline -- measured 1.7-1.8s
    // per 20k-face compound in one event-loop dispatch, the worst
    // per-item stalls of the interactivity gate (the stack was
    // viewIsometric -> findBoundingSphere -> here -> updateVisual).
    // The geometry knows its bounds without a single triangle. The
    // display nodes hold the shape in its LOCAL frame under the
    // placement transform, so `transform` false strips the location
    // exactly as the reset path strips pcTransform below.
    // Sub-element queries keep the building path: they need the
    // detail-path machinery of the node graph, and asking about one
    // sub-element of a never-built shape is rare enough that the
    // build is acceptable there.
    if (VisualTouched && !(subname && subname[0])) {
        try {
            TopoDS_Shape shape = getShape().getShape();
            if (!shape.IsNull()) {
                if (!transform)
                    shape = shape.Located(TopLoc_Location());
                Bnd_Box bounds;
                BRepBndLib::Add(shape, bounds);
                bounds.SetGap(0.0);
                if (!bounds.IsVoid()) {
                    Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
                    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
                    Base::BoundBox3d bbox(xMin, yMin, zMin,
                                          xMax, yMax, zMax);
                    if (mat)
                        bbox = bbox.Transformed(*mat);
                    return bbox;
                }
            }
        }
        catch (Standard_Failure &) {
            // fall through to the building path below
        }
    }
    if (VisualTouched)
        const_cast<ViewProviderPartExt*>(this)->updateVisual();
    return inherited::_getBoundingBox(subname, mat, transform, view, depth);
}
