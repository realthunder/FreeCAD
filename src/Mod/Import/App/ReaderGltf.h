// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#ifndef IMPORT_READER_GLTF_H
#define IMPORT_READER_GLTF_H

#include <Mod/Import/ImportGlobal.h>
#include <Base/FileInfo.h>
#include <TDocStd_Document.hxx>
#include <TDF_LabelSequence.hxx>
#include <TopoDS_Shape.hxx>

namespace Import
{

class ImportExport ReaderGltf
{
public:
    explicit ReaderGltf(const Base::FileInfo& file);

    void read(Handle(TDocStd_Document) hDoc);
    bool cleanup() const;
    void setCleanup(bool);

    /** What a glTF mesh becomes: the triangulation the file states, or
     * B-Rep geometry sewn from its facets.
     *
     * glTF is a mesh format, and the rebuild is a translation with a
     * price: it goes through points and facets, so it drops the UVs and
     * the authored normals with everything else the file said about the
     * surface -- a mesh authored for images cannot be shaded after it --
     * and on a large mesh it dominates the import (chess_set.glb, 1.5M
     * triangles: fifteen minutes and unfinished, against 0.2 seconds
     * without). What it buys is a shape with edges and vertices, which
     * is what CAD work selects, snaps and dimensions.
     *
     * The `GltfRebuildBRep` preference under Mod/Import states which,
     * and defaults to None.
     */
    enum class RebuildBRep
    {
        /// Never. Each mesh arrives as the face (or faces) the reader
        /// built, carrying its triangulation, its UVs and its normals.
        None = 0,
        /// Only where nothing needs what the rebuild would drop: a mesh
        /// whose material names a texture, or whose triangulation states
        /// texture coordinates, is left alone.
        Auto = 1,
        /// Always, whatever the mesh states. Upstream FreeCAD's
        /// behaviour, and the escape hatch for a file wanted purely as
        /// geometry.
        All = 2,
    };
    RebuildBRep rebuildBRep() const;
    void setRebuildBRep(RebuildBRep);

private:
    /// Whether \a shape is rebuilt. \a textured says the mesh's own
    /// visualization material names a texture map.
    bool rebuilds(const TopoDS_Shape& shape, bool textured) const;
    TopoDS_Shape fixShape(TopoDS_Shape);
    void processDocument(Handle(TDocStd_Document) hDoc);
    TopoDS_Shape processSubShapes(Handle(TDocStd_Document) hDoc,
                                  const TDF_LabelSequence& subShapeLabels);

private:
    Base::FileInfo file;
    bool clean = true;
    RebuildBRep rebuild = RebuildBRep::None;
};

}  // namespace Import

#endif  // IMPORT_READER_GLTF_H
