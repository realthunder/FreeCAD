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
 ***************************************************************************/

/// The tessellation of a mesh level, as a pure function — the whole
/// job is (shape, parameters, source chunk) -> level chunk bytes, with
/// no registry, no caches and no GUI. Purity is what lets the scene
/// server run several level builds concurrently on its level threads
/// (SceneServer.cpp), and is what would let the job move behind a
/// process boundary later (docs/ComputeBoundaries.md).

#include "PreCompiled.h"

#ifndef _PreComp_
# include <BRepBndLib.hxx>
# include <BRep_Builder.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepMesh_IncrementalMesh.hxx>
# include <BRep_Tool.hxx>
# include <Bnd_Box.hxx>
# include <Poly_Polygon3D.hxx>
# include <Poly_PolygonOnTriangulation.hxx>
# include <Poly_Triangulation.hxx>
# include <Precision.hxx>
# include <Standard_Failure.hxx>
# include <TColgp_Array1OfDir.hxx>
# include <TColStd_Array1OfInteger.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopLoc_Location.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
# include <TopTools_ListOfShape.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <gp_Trsf.hxx>
# if OCC_VERSION_HEX >= 0x070500
#  include <IMeshTools_Parameters.hxx>
# endif
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

#include <Mod/Part/App/Tools.h>
#include <Gui/Renderer/SceneDump.h>
#include <Gui/Renderer/SceneLadder.h>

#include "MeshLevelSource.h"

using namespace PartGui;

namespace {

/// The bounds side of the job, derived from the shape itself: the same
/// box for the face and the edge role is what makes their deviations —
/// and so their triangulations — identical.
struct BuildContext {
    double bbMin[3] = {0, 0, 0};
    double bbMax[3] = {0, 0, 0};
    double diagonal = 0;
    bool normalsFromUV = false;
};

bool debugOn()
{
    static const bool on = std::getenv("FC_DEBUG_MESH_SOURCE") != nullptr;
    return on;
}

/// The angular deflection of a level: coarser than the display default
/// (0.5 rad) at the coarse rungs, since the linear deviation alone
/// does not bound the segment count of small round features.
double angleForLevel(uint32_t level)
{
    switch (level) {
    case 0: return 1.0;
    case 1: return 0.7;
    default: return 0.5;
    }
}

/// The re-meshed copy for a level. A *structure* copy: fresh TShapes
/// so the coarse triangulation never touches the live shape the GUI
/// reads, stored meshes carried so purely triangulated faces (no
/// surface to re-mesh) keep their geometry. Pure — no cache: the face
/// and the edge role each re-mesh, and their triangulations agree
/// because the build is deterministic in (shape, deflection, angle).
TopoDS_Shape meshedCopy(const TopoDS_Shape &shape, double deflection,
                        double angle)
{
    BRepBuilderAPI_Copy copier(shape, /*copyGeom*/ Standard_False,
                               /*copyMesh*/ Standard_True);
    TopoDS_Shape copy = copier.Shape();
    if (copy.IsNull())
        return copy;

    // Strip the copied display tessellation wherever real geometry
    // exists to re-mesh from — BRepMesh does NOT coarsen an existing
    // finer triangulation (AllowQualityDecrease keeps it), it has to
    // find none. Purely triangulated faces (glTF imports) and
    // curve-less edges keep their stored mesh: it is all the geometry
    // they have, which is why the copy carried the mesh at all.
    BRep_Builder builder;
    for (TopExp_Explorer exp(copy, TopAbs_FACE); exp.More(); exp.Next()) {
        const TopoDS_Face &face = TopoDS::Face(exp.Current());
        TopLoc_Location loc;
        if (BRep_Tool::Surface(face, loc).IsNull())
            continue;
        builder.UpdateFace(face, Handle(Poly_Triangulation)());
    }
    for (TopExp_Explorer exp(copy, TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge &edge = TopoDS::Edge(exp.Current());
        TopLoc_Location loc;
        Standard_Real cf, cl;
        if (BRep_Tool::Curve(edge, loc, cf, cl).IsNull())
            continue;
        builder.UpdateEdge(edge, Handle(Poly_Polygon3D)());
    }

#if OCC_VERSION_HEX >= 0x070500
    IMeshTools_Parameters meshParams;
    meshParams.Deflection = deflection;
    meshParams.Relative = Standard_False;
    meshParams.Angle = angle;
    meshParams.InParallel = Standard_True;
    // The copied display mesh is far finer than any level wants; this
    // is what lets the re-mesh replace it instead of keeping it.
    meshParams.AllowQualityDecrease = Standard_True;
    BRepMesh_IncrementalMesh(copy, meshParams);
#else
    BRepMesh_IncrementalMesh(copy, deflection, Standard_False, angle,
                             Standard_True);
#endif

    return copy;
}

/// Node normals of one face's triangulation, the display rules exactly
/// (buildVisualNodes): from the surface UVs when the view provider was
/// built that way (or the face is purely triangulated with authored
/// normals), else accumulated triangle cross products; orientation
/// swap included via \a reversed at the caller. Returned unnormalized.
void faceNodeNormals(const TopoDS_Face &face,
                     const Handle(Poly_Triangulation) &mesh,
                     bool normalsFromUV, std::vector<float> &out)
{
    int nbNodes = mesh->NbNodes();
    out.assign(size_t(nbNodes) * 3, 0.0f);
    if (normalsFromUV) {
        TColgp_Array1OfDir dirs(1, nbNodes);
        Part::Tools::getPointNormals(face, mesh, dirs);
        for (int n = 1; n <= nbNodes; ++n) {
            const gp_Dir &d = dirs(n);
            out[size_t(n - 1) * 3] = float(d.X());
            out[size_t(n - 1) * 3 + 1] = float(d.Y());
            out[size_t(n - 1) * 3 + 2] = float(d.Z());
        }
        return;
    }
    for (int g = 1; g <= mesh->NbTriangles(); ++g) {
        Standard_Integer N1, N2, N3;
        mesh->Triangle(g).Get(N1, N2, N3);
        gp_Pnt V1(mesh->Node(N1)), V2(mesh->Node(N2)), V3(mesh->Node(N3));
        gp_Vec v1(V1.X(), V1.Y(), V1.Z()), v2(V2.X(), V2.Y(), V2.Z()),
            v3(V3.X(), V3.Y(), V3.Z());
        gp_Vec normal = (v2 - v1) ^ (v3 - v1);
        for (Standard_Integer n : {N1, N2, N3}) {
            out[size_t(n - 1) * 3] += float(normal.X());
            out[size_t(n - 1) * 3 + 1] += float(normal.Y());
            out[size_t(n - 1) * 3 + 2] += float(normal.Z());
        }
    }
}

/// Map the source mesh's solid ranges onto the generated part table.
/// A solid range covers whole consecutive faces; each maps face-exact
/// onto the coarse table or the whole classification is dropped —
/// capping the wrong triangles is worse than not capping a level.
bool mapSolidParts(const std::vector<std::pair<int, int>> &srcParts,
                   const std::vector<std::pair<int, int>> &srcSolid,
                   const std::vector<std::pair<int, int>> &genParts,
                   std::vector<std::pair<int, int>> &genSolid)
{
    if (srcParts.size() != genParts.size())
        return false;
    for (const auto &range : srcSolid) {
        size_t f0 = 0;
        while (f0 < srcParts.size()
               && (srcParts[f0].first != range.first
                   || srcParts[f0].second == 0))
            ++f0;
        if (f0 == srcParts.size())
            return false;
        int remaining = range.second;
        size_t f1 = f0;
        while (f1 < srcParts.size() && remaining > 0) {
            remaining -= srcParts[f1].second;
            if (remaining > 0)
                ++f1;
        }
        if (remaining != 0 || f1 == srcParts.size())
            return false;
        int start = genParts[f0].first;
        int end = genParts[f1].first + genParts[f1].second;
        genSolid.emplace_back(start, end - start);
    }
    return true;
}

/// Build the coarse face chunk: re-tessellated triangles with the part
/// table in the shape's own face order — index for index with the
/// exact mesh, validated against it.
bool buildFaceLevel(const BuildContext &st,
                    const Render::ParsedMeshChunk &src,
                    const TopoDS_Shape &copy, double deflection,
                    double angle, std::vector<uint8_t> &out)
{
    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(copy, TopAbs_FACE, faceMap);
    if (faceMap.IsEmpty())
        return false;
    if (!src.triangleParts.empty()
        && int(src.triangleParts.size()) != faceMap.Extent())
        return false;

    const bool wantNormals = src.normals != nullptr;
    const bool wantUVs = src.texCoords != nullptr;

    std::vector<float> positions, normals, uvs;
    std::vector<int32_t> triangles;
    std::vector<std::pair<int, int>> parts;
    parts.reserve(size_t(faceMap.Extent()));

    // The default-texture projection frame of the display build: the
    // shape's own bounding box, largest dimension as the texel scale.
    const double maxDim = std::max({st.bbMax[0] - st.bbMin[0],
                                    st.bbMax[1] - st.bbMin[1],
                                    st.bbMax[2] - st.bbMin[2]});
    const float invMaxDim = maxDim > 0 ? float(1.0 / maxDim) : 0.0f;

    std::vector<float> faceNormals;
    for (int i = 1; i <= faceMap.Extent(); ++i) {
        const TopoDS_Face &face = TopoDS::Face(faceMap(i));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) mesh =
            Part::Tools::triangulationOfFace(face, loc, deflection, angle);
        const int partStart = int(triangles.size());
        if (mesh.IsNull()) {
            parts.emplace_back(partStart, 0);
            continue;
        }

        gp_Trsf trsf;
        const bool identity = loc.IsIdentity();
        if (!identity)
            trsf = loc.Transformation();
        const bool reversed = face.Orientation() != TopAbs_FORWARD;
        TopLoc_Location surfLoc;
        const bool meshOnly = BRep_Tool::Surface(face, surfLoc).IsNull();
        const bool uvNormals =
            st.normalsFromUV || (meshOnly && mesh->HasNormals());

        const int nbNodes = mesh->NbNodes();
        const int nodeBase = int(positions.size() / 3);
        for (int n = 1; n <= nbNodes; ++n) {
            gp_Pnt p = mesh->Node(n);
            if (!identity)
                p.Transform(trsf);
            positions.push_back(float(p.X()));
            positions.push_back(float(p.Y()));
            positions.push_back(float(p.Z()));
        }

        if (wantNormals) {
            faceNodeNormals(face, mesh, uvNormals, faceNormals);
            // Normals are computed in the mesh-local frame; rotate them
            // along with the vertices (the display build transforms
            // per-triangle before accumulating — same thing, since the
            // accumulation is linear).
            if (!identity) {
                for (int n = 0; n < nbNodes; ++n) {
                    gp_Vec v(faceNormals[size_t(n) * 3],
                             faceNormals[size_t(n) * 3 + 1],
                             faceNormals[size_t(n) * 3 + 2]);
                    v.Transform(trsf);
                    faceNormals[size_t(n) * 3] = float(v.X());
                    faceNormals[size_t(n) * 3 + 1] = float(v.Y());
                    faceNormals[size_t(n) * 3 + 2] = float(v.Z());
                }
            }
            // The display build swaps the triangle winding of reversed
            // faces before taking the cross product; flipping the
            // accumulated sign is the same normal.
            const float sign = reversed && !uvNormals ? -1.0f : 1.0f;
            for (int n = 0; n < nbNodes * 3; ++n)
                normals.push_back(sign * faceNormals[size_t(n)]);
        }

        for (int g = 1; g <= mesh->NbTriangles(); ++g) {
            Standard_Integer N1, N2, N3;
            mesh->Triangle(g).Get(N1, N2, N3);
            if (reversed)
                std::swap(N1, N2);
            triangles.push_back(nodeBase + N1 - 1);
            triangles.push_back(nodeBase + N2 - 1);
            triangles.push_back(nodeBase + N3 - 1);
        }
        parts.emplace_back(partStart, int(triangles.size()) - partStart);

        if (wantUVs) {
            if (meshOnly && mesh->HasUVNodes()) {
                for (int n = 1; n <= nbNodes; ++n) {
                    const gp_Pnt2d uv = mesh->UVNode(n);
                    uvs.push_back(float(uv.X()));
                    uvs.push_back(float(uv.Y()));
                    uvs.push_back(0.0f);
                    uvs.push_back(1.0f);
                }
            }
            else {
                // Box projection along the dominant axis of the face's
                // accumulated normal, like the display build.
                double nsum[3] = {0, 0, 0};
                if (wantNormals) {
                    for (int n = 0; n < nbNodes; ++n)
                        for (int a = 0; a < 3; ++a)
                            nsum[a] +=
                                normals[size_t(nodeBase + n) * 3 + size_t(a)];
                }
                double ax = std::fabs(nsum[0]), ay = std::fabs(nsum[1]),
                       az = std::fabs(nsum[2]);
                int axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
                int i0 = axis == 0 ? 1 : 0;
                int i1 = axis == 2 ? 1 : 2;
                for (int n = 0; n < nbNodes; ++n) {
                    const float *p = &positions[size_t(nodeBase + n) * 3];
                    uvs.push_back(float((p[i0] - st.bbMin[i0]) * invMaxDim));
                    uvs.push_back(float((p[i1] - st.bbMin[i1]) * invMaxDim));
                    uvs.push_back(0.0f);
                    uvs.push_back(1.0f);
                }
            }
        }
    }

    if (triangles.empty())
        return false;

    // Normalize the accumulated normals (display does the same pass).
    for (size_t n = 0; n + 2 < normals.size(); n += 3) {
        float len = std::sqrt(normals[n] * normals[n]
                              + normals[n + 1] * normals[n + 1]
                              + normals[n + 2] * normals[n + 2]);
        if (len > 0) {
            normals[n] /= len;
            normals[n + 1] /= len;
            normals[n + 2] /= len;
        }
    }

    Render::MeshData m;
    m.numVertices = int(positions.size() / 3);
    m.positions = positions.data();
    if (wantNormals)
        m.normals = normals.data();
    if (wantUVs)
        m.texCoords = uvs.data();
    m.triangleIndices = triangles.data();
    m.numTriangleIndices = int(triangles.size());
    if (!src.triangleParts.empty())
        m.triangleParts = parts;

    // Non-flat is the vertex cache's own rule — a part whose bounding
    // box is non-degenerate on all three axes — applied to the coarse
    // geometry.
    if (!src.triangleParts.empty()) {
        for (const auto &part : m.triangleParts) {
            if (!part.second)
                continue;
            float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
            float hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (int t = part.first; t < part.first + part.second; ++t) {
                const float *p = &positions[size_t(triangles[size_t(t)]) * 3];
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], p[a]);
                    hi[a] = std::max(hi[a], p[a]);
                }
            }
            if (hi[0] - lo[0] > 1e-6f && hi[1] - lo[1] > 1e-6f
                && hi[2] - lo[2] > 1e-6f)
                m.nonFlatParts.push_back(part);
        }
    }

    if (src.hasSolid && !src.solidParts.empty()) {
        if (mapSolidParts(src.triangleParts, src.solidParts,
                          m.triangleParts, m.solidParts))
            m.hasSolid = src.hasSolid;
        else
            m.solidParts.clear();
    }
    else if (src.hasSolid == 2) {
        m.hasSolid = 2;
    }

    m.hasTransparency = src.hasTransparency;
    m.hasOpaqueParts = src.hasOpaqueParts;
    return Render::encodeMeshChunk(m, out);
}

/// Build the coarse edge chunk: polylines from
/// Poly_PolygonOnTriangulation of the same coarse triangulation the
/// face chunk is drawn from — the nodes ARE coarse surface nodes, so
/// the lines lie on the coarse facets by construction.
bool buildEdgeLevel(const BuildContext &st,
                    const Render::ParsedMeshChunk &src,
                    const TopoDS_Shape &copy, double deflection,
                    double angle, std::vector<uint8_t> &out)
{
    TopTools_IndexedMapOfShape faceMap, edgeMap;
    TopExp::MapShapes(copy, TopAbs_FACE, faceMap);
    TopExp::MapShapes(copy, TopAbs_EDGE, edgeMap);
    if (edgeMap.IsEmpty())
        return false;
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(copy, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

    const bool wantNormals = src.normals != nullptr;

    std::vector<float> positions, normals;
    // Edge index -> run of vertex indices, in edge order like the
    // display build's lineSetMap (edges the mesher gave no polygon
    // simply have no entry — the same compaction the exact mesh got,
    // checked against it below).
    std::map<int, std::pair<int, int>> polylines; // idx -> {first vertex, count}
    std::set<int> seamEdges;

    std::vector<float> faceNormals;
    std::set<int> pending;
    for (int i = 1; i <= edgeMap.Extent(); ++i)
        pending.insert(i);

    for (int i = 1; i <= faceMap.Extent() && !pending.empty(); ++i) {
        const TopoDS_Face &face = TopoDS::Face(faceMap(i));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) mesh =
            Part::Tools::triangulationOfFace(face, loc, deflection, angle);
        if (mesh.IsNull())
            continue;
        gp_Trsf trsf;
        const bool identity = loc.IsIdentity();
        if (!identity)
            trsf = loc.Transformation();

        bool haveNormals = false;
        TopExp_Explorer exp;
        for (exp.Init(face, TopAbs_EDGE); exp.More(); exp.Next()) {
            const TopoDS_Edge &edge = TopoDS::Edge(exp.Current());
            int idx = edgeMap.FindIndex(edge);
            if (!pending.count(idx))
                continue;
            Handle(Poly_PolygonOnTriangulation) poly =
                BRep_Tool::PolygonOnTriangulation(edge, mesh, loc);
            if (poly.IsNull())
                continue;
            if (wantNormals && !haveNormals) {
                TopLoc_Location surfLoc;
                const bool meshOnly =
                    BRep_Tool::Surface(face, surfLoc).IsNull();
                faceNodeNormals(face, mesh,
                                st.normalsFromUV
                                    || (meshOnly && mesh->HasNormals()),
                                faceNormals);
                haveNormals = true;
            }
            const TColStd_Array1OfInteger &nodes = poly->Nodes();
            const int first = int(positions.size() / 3);
            for (Standard_Integer n = nodes.Lower(); n <= nodes.Upper();
                 ++n) {
                gp_Pnt p = mesh->Node(nodes(n));
                if (!identity)
                    p.Transform(trsf);
                positions.push_back(float(p.X()));
                positions.push_back(float(p.Y()));
                positions.push_back(float(p.Z()));
                if (wantNormals) {
                    size_t off = size_t(nodes(n) - 1) * 3;
                    gp_Vec v(faceNormals[off], faceNormals[off + 1],
                             faceNormals[off + 2]);
                    if (!identity)
                        v.Transform(trsf);
                    double len = v.Magnitude();
                    if (len > 0)
                        v /= len;
                    normals.push_back(float(v.X()));
                    normals.push_back(float(v.Y()));
                    normals.push_back(float(v.Z()));
                }
            }
            polylines[idx] = {first, int(positions.size() / 3) - first};
            pending.erase(idx);
        }
    }

    // Free edges (no ancestor face) get their own discretization, and
    // seam edges are named so the hidden-line no-seam filter carries.
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge &edge = TopoDS::Edge(edgeMap(i));
        const TopTools_ListOfShape *owners =
            edgeFaces.Contains(edge) ? &edgeFaces.FindFromKey(edge) : nullptr;
        if (owners && !owners->IsEmpty()) {
            if (BRep_Tool::IsClosed(edge,
                                    TopoDS::Face(owners->First())))
                seamEdges.insert(i);
            continue;
        }
        if (!pending.count(i))
            continue;
        TopLoc_Location loc;
        Handle(Poly_Polygon3D) poly =
            Part::Tools::polygonOfEdge(edge, loc, deflection, angle);
        if (poly.IsNull())
            continue;
        gp_Trsf trsf;
        const bool identity = loc.IsIdentity();
        if (!identity)
            trsf = loc.Transformation();
        const TColgp_Array1OfPnt &nodes = poly->Nodes();
        const int first = int(positions.size() / 3);
        for (Standard_Integer n = nodes.Lower(); n <= nodes.Upper(); ++n) {
            gp_Pnt p = nodes(n);
            if (!identity)
                p.Transform(trsf);
            positions.push_back(float(p.X()));
            positions.push_back(float(p.Y()));
            positions.push_back(float(p.Z()));
            if (wantNormals) {
                // The display build never accumulates normals for free
                // edges; zero is what the exact mesh carries.
                normals.push_back(0.0f);
                normals.push_back(0.0f);
                normals.push_back(0.0f);
            }
        }
        polylines[i] = {first, int(positions.size() / 3) - first};
        pending.erase(i);
    }

    if (polylines.empty())
        return false;
    if (!src.lineParts.empty()
        && src.lineParts.size() != polylines.size())
        return false;

    std::vector<int32_t> lines, noSeam;
    std::vector<std::pair<int, int>> parts;
    bool anySeam = false;
    for (const auto &entry : polylines) {
        const int start = int(lines.size());
        for (int k = 0; k + 1 < entry.second.second; ++k) {
            lines.push_back(entry.second.first + k);
            lines.push_back(entry.second.first + k + 1);
        }
        parts.emplace_back(start, int(lines.size()) - start);
        const bool seam = seamEdges.count(entry.first) != 0;
        anySeam = anySeam || seam;
        if (!seam)
            noSeam.insert(noSeam.end(), lines.begin() + start, lines.end());
    }
    if (lines.empty())
        return false;

    Render::MeshData m;
    m.numVertices = int(positions.size() / 3);
    m.positions = positions.data();
    if (wantNormals)
        m.normals = normals.data();
    m.lineIndices = lines.data();
    m.numLineIndices = int(lines.size());
    if (!src.lineParts.empty())
        m.lineParts = parts;
    if (anySeam) {
        m.noSeamLineIndices = noSeam.data();
        m.numNoSeamLineIndices = int(noSeam.size());
    }
    m.hasTransparency = src.hasTransparency;
    m.hasOpaqueParts = src.hasOpaqueParts;
    return Render::encodeMeshChunk(m, out);
}
} // anonymous namespace

double PartGui::meshLevelDeflection(double diagonal, unsigned level)
{
    double deflection = diagonal / double(8u << level);
    deflection = std::max(deflection, double(Precision::Confusion()));
    return std::min(deflection, 20.0);
}

double PartGui::meshLevelAngle(unsigned level)
{
    return angleForLevel(level);
}

bool PartGui::buildMeshLevel(const TopoDS_Shape &shape,
                             const MeshLevelJob &job,
                             const void *sourceChunk, size_t sourceSize,
                             std::vector<uint8_t> &out)
{
    const bool exact = job.level == Render::kExactMeshLevel;
    if ((job.level >= 8 && !exact) || shape.IsNull())
        return false;
    if (exact && !(job.exactDeflection > 0))
        return false;

    Render::ParsedMeshChunk src;
    if (!Render::parseMeshChunk(sourceChunk, sourceSize, src)) {
        if (debugOn())
            std::fprintf(stderr, "mesh level: source chunk did not parse\n");
        return false;
    }
    // Per-vertex colors are baked by the color-variant machinery; a
    // re-tessellation cannot reproduce them. Decimation carries them.
    if (src.colors) {
        if (debugOn())
            std::fprintf(stderr, "mesh level: source has baked colors\n");
        return false;
    }

    try {
        BuildContext ctx;
        ctx.normalsFromUV = job.normalsFromUV;
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        bounds.SetGap(0.0);
        if (bounds.IsVoid())
            return false;
        bounds.Get(ctx.bbMin[0], ctx.bbMin[1], ctx.bbMin[2], ctx.bbMax[0],
                   ctx.bbMax[1], ctx.bbMax[2]);
        double dx = ctx.bbMax[0] - ctx.bbMin[0];
        double dy = ctx.bbMax[1] - ctx.bbMin[1];
        double dz = ctx.bbMax[2] - ctx.bbMin[2];
        ctx.diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(ctx.diagonal > 0))
            return false;

        // The deviation is the declared error of the level (1/(8<<L)
        // of the diagonal, writeMesh). The exact rung of a coarse-first
        // ladder is not on that grid: it is the display formula's own
        // parameters, carried in the job.
        const double deflection = exact
            ? std::min(std::max(job.exactDeflection,
                                double(Precision::Confusion())), 20.0)
            : meshLevelDeflection(ctx.diagonal, job.level);
        const double angle = exact ? job.exactAngle
                                   : angleForLevel(job.level);

        TopoDS_Shape copy = meshedCopy(shape, deflection, angle);
        if (copy.IsNull())
            return false;
        std::vector<uint8_t> chunk;
        bool ok = src.numTriangleIndices > 0
            ? buildFaceLevel(ctx, src, copy, deflection, angle, chunk)
            : src.numLineIndices > 0
                ? buildEdgeLevel(ctx, src, copy, deflection, angle, chunk)
                : false;
        if (debugOn() && !ok)
            std::fprintf(stderr, "mesh level: %s extraction refused\n",
                         src.numTriangleIndices > 0 ? "face" : "edge");
        // A "coarse" level that is not smaller than the source mesh is
        // not a rung worth a chunk — except the exact rung, which is
        // being *refined* from a coarse source and is larger by design.
        if (!ok || (!exact && chunk.size() >= sourceSize)) {
            if (debugOn() && ok)
                std::fprintf(stderr,
                             "mesh level: %zu bytes not below the source "
                             "%zu — not a rung\n",
                             chunk.size(), sourceSize);
            return false;
        }
        out = std::move(chunk);
        return true;
    }
    catch (const Standard_Failure &e) {
        if (debugOn())
            std::fprintf(stderr, "mesh level: OCCT failure: %s\n",
                         e.GetMessageString());
        return false;
    }
    catch (const std::bad_alloc &) {
        return false;
    }
}

TopoDS_Shape PartGui::meshLevelExactCopy(const TopoDS_Shape &shape,
                                         double deflection, double angle)
{
    if (shape.IsNull() || !(deflection > 0))
        return {};
    try {
        const double defl = std::min(
            std::max(deflection, double(Precision::Confusion())), 20.0);
        return meshedCopy(shape, defl, angle > 0 ? angle : 0.5);
    }
    catch (const Standard_Failure &e) {
        if (debugOn())
            std::fprintf(stderr, "mesh refine: OCCT failure: %s\n",
                         e.GetMessageString());
        return {};
    }
    catch (const std::bad_alloc &) {
        return {};
    }
}

void PartGui::transferMeshLevels(const TopoDS_Shape &from,
                                 const TopoDS_Shape &to)
{
    if (from.IsNull() || to.IsNull())
        return;
    // The copy preserves sub-shape order (the same assumption the
    // level builder's table validation stands on); a count mismatch
    // means these are not copy and original, and nothing moves.
    TopTools_IndexedMapOfShape fromFaces, toFaces, fromEdges, toEdges;
    TopExp::MapShapes(from, TopAbs_FACE, fromFaces);
    TopExp::MapShapes(to, TopAbs_FACE, toFaces);
    TopExp::MapShapes(from, TopAbs_EDGE, fromEdges);
    TopExp::MapShapes(to, TopAbs_EDGE, toEdges);
    if (fromFaces.Extent() != toFaces.Extent()
        || fromEdges.Extent() != toEdges.Extent()) {
        // The caller's rebuild then re-meshes on the GUI thread — the
        // jank the transfer exists to avoid, so a refusal is worth a
        // line under the same switch as the rest of the machinery.
        if (debugOn())
            std::fprintf(stderr,
                         "mesh refine: transfer refused (%d vs %d faces, "
                         "%d vs %d edges)\n",
                         fromFaces.Extent(), toFaces.Extent(),
                         fromEdges.Extent(), toEdges.Extent());
        return;
    }

    BRep_Builder builder;
    // Wipe the live curve-backed edges' polygon representations first,
    // exactly as the meshed copy itself was stripped: the entries
    // keyed to the outgoing coarse triangulations would otherwise
    // pin those triangulations alive for the shape's lifetime.
    for (int i = 1; i <= toEdges.Extent(); ++i) {
        const TopoDS_Edge &edge = TopoDS::Edge(toEdges(i));
        TopLoc_Location loc;
        Standard_Real cf, cl;
        if (BRep_Tool::Curve(edge, loc, cf, cl).IsNull())
            continue;
        builder.UpdateEdge(edge, Handle(Poly_Polygon3D)());
    }
    // Faces with a surface take the copy's triangulation handle
    // whole; purely triangulated faces (glTF imports) kept their own
    // mesh in the copy and keep it here.
    for (int i = 1; i <= toFaces.Extent(); ++i) {
        const TopoDS_Face &ff = TopoDS::Face(fromFaces(i));
        const TopoDS_Face &tf = TopoDS::Face(toFaces(i));
        TopLoc_Location loc;
        if (BRep_Tool::Surface(tf, loc).IsNull())
            continue;
        TopLoc_Location floc;
        Handle(Poly_Triangulation) tria = BRep_Tool::Triangulation(ff, floc);
        builder.UpdateFace(tf, tria);
        // The face's edges ride with its triangulation: their
        // polygons-on-triangulation index the very nodes just moved,
        // matched edge for edge in the same preserved order.
        TopExp_Explorer fe(ff, TopAbs_EDGE), te(tf, TopAbs_EDGE);
        for (; fe.More() && te.More(); fe.Next(), te.Next()) {
            const TopoDS_Edge &fromEdge = TopoDS::Edge(fe.Current());
            const TopoDS_Edge &toEdge = TopoDS::Edge(te.Current());
            Handle(Poly_PolygonOnTriangulation) poly =
                BRep_Tool::PolygonOnTriangulation(fromEdge, tria, floc);
            if (!poly.IsNull())
                builder.UpdateEdge(toEdge, poly, tria, floc);
        }
    }
    // Free edges: the 3D polygon is all the tessellation they have.
    for (int i = 1; i <= toEdges.Extent(); ++i) {
        const TopoDS_Edge &fromEdge = TopoDS::Edge(fromEdges(i));
        const TopoDS_Edge &toEdge = TopoDS::Edge(toEdges(i));
        TopLoc_Location loc;
        Standard_Real cf, cl;
        if (BRep_Tool::Curve(toEdge, loc, cf, cl).IsNull())
            continue;
        Handle(Poly_Polygon3D) poly = BRep_Tool::Polygon3D(fromEdge, loc);
        if (!poly.IsNull())
            builder.UpdateEdge(toEdge, poly);
    }
}
