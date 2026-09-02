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
    /** Whether a mesh that states texture coordinates keeps its
     * triangulation instead of being rebuilt as B-Rep geometry.
     *
     * The rebuild goes through points and facets and drops the UVs with
     * everything else the file said about the surface, so a mesh authored
     * for images cannot be shaded after it. On by default, and the
     * `GltfKeepMesh` preference under Mod/Import turns it off for a file
     * wanted as geometry rather than as an asset.
     */
    bool keepMesh() const;
    void setKeepMesh(bool);

private:
    bool keepsMesh(const TopoDS_Shape&) const;
    TopoDS_Shape fixShape(TopoDS_Shape);
    void processDocument(Handle(TDocStd_Document) hDoc);
    TopoDS_Shape processSubShapes(Handle(TDocStd_Document) hDoc,
                                  const TDF_LabelSequence& subShapeLabels);

private:
    Base::FileInfo file;
    bool clean = true;
    bool keep = true;
};

}  // namespace Import

#endif  // IMPORT_READER_GLTF_H
