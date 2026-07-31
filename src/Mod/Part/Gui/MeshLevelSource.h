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

#ifndef PARTGUI_MESH_LEVEL_SOURCE_H
#define PARTGUI_MESH_LEVEL_SOURCE_H

/// The shape-backed level generator (docs/SceneStreaming.md §7): when
/// the scene server is asked for a coarser level of a published mesh,
/// re-tessellate the source shape at the level's deviation instead of
/// decimating the exact mesh. Edge polylines come from
/// Poly_PolygonOnTriangulation of the same coarse triangulation, so
/// they lie on the coarse surface by construction — the structural fix
/// for edges floating off decimated facets at silhouettes — and the
/// per-face/per-edge part tables fall out of the shape's own element
/// order, index for index with the exact mesh.
///
/// Registration follows the display tessellation: whoever builds the
/// visual nodes for a shape registers that shape under the very node
/// pointers the render feed will carry as MeshData::sourceTag
/// (Render::MeshSourceRegistry). The registered closure owns a
/// refcounted shape handle and runs on the server's level threads,
/// tessellating a fresh structure copy — never the live shape.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class SoNode;
class TopoDS_Shape;

namespace PartGui {

/// Everything a level build needs beyond the shape and the source
/// chunk — the whole job is a pure function of these (MeshLevelBuild),
/// which is what lets the scene server run level builds on several
/// threads at once (and would let the job move behind a process
/// boundary later, docs/ComputeBoundaries.md).
struct MeshLevelJob {
    uint32_t level = 0;
    bool normalsFromUV = false;
    /// The full display-formula parameters, used when \a level is
    /// Render::kExactMeshLevel (the on-demand exact build of a
    /// coarse-first ladder).
    double exactDeflection = 0;
    double exactAngle = 0;
};

/// Build the bytes of the requested level chunk from \a shape,
/// honoring the source chunk's element tables index for index (false
/// on any mismatch — the caller falls back to decimation). Pure and
/// thread-safe; tessellates a structure copy, never the shape itself.
bool buildMeshLevel(const TopoDS_Shape &shape, const MeshLevelJob &job,
                    const void *sourceChunk, size_t sourceSize,
                    std::vector<uint8_t> &out);

/// Register the level generator for \a shape — the exact shape the
/// display tessellation just meshed (flattened whole shape, or an
/// instanced leaf in its local frame) — under the face and edge shape
/// nodes it was meshed into. Either tag may be null. Replaces any
/// previous registration of the same tags.
///
/// \a builtError states what the display tessellation itself is: 0
/// when it ran at the full display deviation (the usual case), else
/// the ladder error (relative to the shape diagonal) it was
/// deliberately built coarse at — coarse-first publish, in which case
/// \a exactDeflection / \a exactAngle carry the full display
/// parameters the on-demand *exact* build (kExactMeshLevel) will use.
///
/// \a onExactBuilt is the desktop tier's climb back to exact
/// (docs/SceneStreaming.md §13): given for a coarse-first build on a
/// plain desktop process (no scene server — a serving process's
/// viewers drive the exact rung themselves), the worker pool meshes a
/// structure copy of the shape at the exact display parameters
/// off-thread and calls back ON THE GUI THREAD with the meshed copy.
/// The callback transfers the triangulation and rebuilds its nodes.
/// It fires at most once, and never after the tags were re-registered
/// or unregistered — which is also the cancellation: a build obsoleted
/// mid-job completes, fails that check, and is dropped.
void registerMeshLevelSource(const TopoDS_Shape &shape, bool normalsFromUV,
                             SoNode *faceTag, SoNode *lineTag,
                             float builtError = 0.0f,
                             double exactDeflection = 0.0,
                             double exactAngle = 0.0,
                             std::function<void(const TopoDS_Shape &)>
                                 onExactBuilt = {});

/// Drop the registration made under these tags (before the nodes die;
/// their addresses may be reused).
void unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag);

/// The coarse-first tessellation level for display builds; negative
/// means tessellate at the full display deviation as always. Resolved
/// from the CoarseTessellation render parameter — per-view
/// Render_CoarseTessellation overrides it — wherever something can
/// climb the build back to exact: a scene stream server (viewers'
/// cameras ask for the exact rung, docs/SceneStreaming.md §7), or a
/// desktop view on the bgfx renderer (the refine worker rebuilds it,
/// §13). Plain Coin display keeps the exact tessellation — a coarse
/// build there would simply stay coarse. The FC_COARSE_TESSELLATION
/// environment variable overrides everything for a whole process.
int coarseTessellationLevel();

/// Mesh a structure copy of \a shape at the given display parameters
/// and return it (null on failure). Pure and thread-safe — the copy
/// shares geometry but owns fresh TShapes, so the live shape is never
/// touched; the worker-pool half of the desktop exact refine.
TopoDS_Shape meshLevelExactCopy(const TopoDS_Shape &shape,
                                double deflection, double angle);

/// Move the triangulations of \a from (a meshed structure copy) onto
/// \a to (the live shape it was copied from): face triangulations,
/// their edges' polygons-on-triangulation, and free edges' 3D
/// polygons, matched by the copy's preserved sub-shape order. GUI
/// thread — the reader of these is the display build. No-op when the
/// two shapes do not correspond.
void transferMeshLevels(const TopoDS_Shape &from, const TopoDS_Shape &to);

/// The linear / angular deflection of ladder level \a level for a
/// shape of the given bbox diagonal — the generator's own grid
/// (error 1/(8<<level) of the diagonal), exposed so a coarse-first
/// display build tessellates exactly the rung it will publish as.
double meshLevelDeflection(double diagonal, unsigned level);
double meshLevelAngle(unsigned level);

} // namespace PartGui

#endif // PARTGUI_MESH_LEVEL_SOURCE_H
