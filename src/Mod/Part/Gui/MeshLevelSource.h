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
/// refcounted shape handle and runs on the server's level worker
/// thread, tessellating a fresh structure copy — never the live shape.

class SoNode;
class TopoDS_Shape;

namespace PartGui {

/// Register the level generator for \a shape — the exact shape the
/// display tessellation just meshed (flattened whole shape, or an
/// instanced leaf in its local frame) — under the face and edge shape
/// nodes it was meshed into. Either tag may be null. Replaces any
/// previous registration of the same tags.
void registerMeshLevelSource(const TopoDS_Shape &shape, bool normalsFromUV,
                             SoNode *faceTag, SoNode *lineTag);

/// Drop the registration made under these tags (before the nodes die;
/// their addresses may be reused).
void unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag);

} // namespace PartGui

#endif // PARTGUI_MESH_LEVEL_SOURCE_H
