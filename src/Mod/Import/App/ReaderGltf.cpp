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


#include "PreCompiled.h"
#ifndef _PreComp_
#include <boost/core/ignore_unused.hpp>
#include <Standard_Version.hxx>
#if OCC_VERSION_HEX >= 0x070500
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Message_ProgressRange.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <RWGltf_CafReader.hxx>
#include <TDF_Label.hxx>
#include <TDF_TagSource.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>
#endif
#endif

#include "ReaderGltf.h"
#include "Tools.h"
#include <App/Application.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/Tools.h>

using namespace Import;

// NOLINTNEXTLINE
ReaderGltf::ReaderGltf(const Base::FileInfo& file)
    : file {file}
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Import");
    const long stated = hGrp->GetInt("GltfRebuildBRep",
                                     static_cast<long>(RebuildBRep::None));
    switch (stated) {
        case static_cast<long>(RebuildBRep::Auto):
            rebuild = RebuildBRep::Auto;
            break;
        case static_cast<long>(RebuildBRep::All):
            rebuild = RebuildBRep::All;
            break;
        default:
            rebuild = RebuildBRep::None;
            break;
    }
}

// NOLINTNEXTLINE
void ReaderGltf::read(Handle(TDocStd_Document) hDoc)
{
#if OCC_VERSION_HEX >= 0x070500
    const double unit = 0.001;  // mm
    RWGltf_CafReader aReader;
    aReader.SetSystemLengthUnit(unit);
    aReader.SetSystemCoordinateSystem(RWMesh_CoordinateSystem_Zup);
    aReader.SetDocument(hDoc);
    aReader.SetParallel(true);

    TCollection_AsciiString filename(file.filePath().c_str());
    Standard_Boolean ret = aReader.Perform(filename, Message_ProgressRange());
    if (!ret) {
        throw Base::FileException("Cannot read from file: ", file);
    }

    processDocument(hDoc);

#else
    boost::ignore_unused(hDoc);
    THROWM(Base::RuntimeError, "gITF support requires OCCT 7.5.0 or later")
#endif
}

#if OCC_VERSION_HEX >= 0x070500
// Whether the label's visualization material references any texture map.
// A textured mesh must keep its stored triangulation (with the UV nodes
// the textures map through) — rebuilding B-Rep geometry from the facets
// would discard them.
static bool hasTexturedMaterial(const Handle(XCAFDoc_VisMaterialTool)& aVisTool,
                                const TDF_Label& label)
{
    Handle(XCAFDoc_VisMaterial) aVisMat = aVisTool->GetShapeMaterial(label);
    if (aVisMat.IsNull()) {
        return false;
    }
    if (aVisMat->HasPbrMaterial()) {
        const XCAFDoc_VisMaterialPBR& pbr = aVisMat->PbrMaterial();
        if (!pbr.BaseColorTexture.IsNull() || !pbr.MetallicRoughnessTexture.IsNull()
            || !pbr.NormalTexture.IsNull() || !pbr.EmissiveTexture.IsNull()
            || !pbr.OcclusionTexture.IsNull()) {
            return true;
        }
    }
    if (aVisMat->HasCommonMaterial() && !aVisMat->CommonMaterial().DiffuseTexture.IsNull()) {
        return true;
    }
    return false;
}

// Whether the shape's triangulation carries texture coordinates.
//
// A UV set is the file's own data and there is no way back to it: the
// B-Rep rebuild below goes through points and facets, so what comes out
// has no UVs and every image the mesh was authored for is lost. That is
// why a textured material already keeps its triangulation, and the same
// holds when the images are named by something else -- a MaterialX look
// beside the file shades these meshes through exactly these coordinates
// (docs/MaterialStorage.md sec 17.13).
static bool hasTextureCoordinates(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) {
        return false;
    }
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri =
            BRep_Tool::Triangulation(TopoDS::Face(it.Current()), loc);
        if (!tri.IsNull() && tri->HasUVNodes()) {
            return true;
        }
    }
    return false;
}
#endif

// NOLINTNEXTLINE
void ReaderGltf::processDocument(Handle(TDocStd_Document) hDoc)
{
#if OCC_VERSION_HEX >= 0x070500
    Handle(XCAFDoc_ShapeTool) aShapeTool = XCAFDoc_DocumentTool::ShapeTool(hDoc->Main());
    Handle(XCAFDoc_ColorTool) aColorTool = XCAFDoc_DocumentTool::ColorTool(hDoc->Main());
    Handle(XCAFDoc_VisMaterialTool) aVisTool = XCAFDoc_DocumentTool::VisMaterialTool(hDoc->Main());

    TDF_LabelSequence shapeLabels;
    aShapeTool->GetShapes(shapeLabels);
    for (Standard_Integer i = 1; i <= shapeLabels.Length(); i++) {
        auto topLevelshape = shapeLabels.Value(i);
        TopoDS_Shape shape = aShapeTool->GetShape(topLevelshape);
        if (!shape.IsNull() && !aShapeTool->IsAssembly(topLevelshape)) {
            TDF_LabelSequence subShapeLabels;
            if (XCAFDoc_ShapeTool::GetSubShapes(topLevelshape, subShapeLabels)) {
                TopoDS_Shape compound = processSubShapes(hDoc, subShapeLabels);
                aShapeTool->SetShape(topLevelshape, compound);
            }
            else {
                if (rebuilds(shape, hasTexturedMaterial(aVisTool, topLevelshape))) {
                    aShapeTool->SetShape(topLevelshape, fixShape(shape));
                }
                // like processSubShapes: ImportOCAF2 reads color labels,
                // not material labels
                Handle(XCAFDoc_VisMaterial) aVisMat =
                    aVisTool->GetShapeMaterial(topLevelshape);
                if (!aVisMat.IsNull()) {
                    aColorTool->SetColor(topLevelshape,
                                         aVisMat->BaseColor(),
                                         XCAFDoc_ColorSurf);
                }
            }
        }
    }
    // The shape labels now hold the fixed replacements; rebuild the
    // assembly compounds from their components so that traversing an
    // assembly yields shapes that can be mapped back to their labels
    // (ImportOCAF2 relies on XCAFDoc_ShapeTool::FindShape for material
    // and name lookups).
    aShapeTool->UpdateAssemblies();
#else
    boost::ignore_unused(hDoc);
#endif
}

// NOLINTNEXTLINE
TopoDS_Shape ReaderGltf::processSubShapes(Handle(TDocStd_Document) hDoc,
                                          const TDF_LabelSequence& subShapeLabels)
{
    TopoDS_Compound compound;
#if OCC_VERSION_HEX >= 0x070500
    Handle(XCAFDoc_ShapeTool) aShapeTool = XCAFDoc_DocumentTool::ShapeTool(hDoc->Main());
    Handle(XCAFDoc_ColorTool) aColorTool = XCAFDoc_DocumentTool::ColorTool(hDoc->Main());
    Handle(XCAFDoc_VisMaterialTool) aVisTool = XCAFDoc_DocumentTool::VisMaterialTool(hDoc->Main());

    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (Standard_Integer i = 1; i <= subShapeLabels.Length(); i++) {
        auto faceLabel = subShapeLabels.Value(i);

        // OCCT handles colors of a glTF with material labels but the ImportOCAF(2) class
        // expects color labels. Thus, the material labels are converted into color labels.
        Handle(XCAFDoc_VisMaterial) aVisMat = aVisTool->GetShapeMaterial(faceLabel);

        Quantity_ColorRGBA rgba;
        bool hasVisMat {false};
        if (!aVisMat.IsNull()) {
            rgba = aVisMat->BaseColor();
            hasVisMat = true;
        }

        TopoDS_Shape face = aShapeTool->GetShape(faceLabel);
        if (rebuilds(face, hasTexturedMaterial(aVisTool, faceLabel))) {
            TopoDS_Shape fixed = fixShape(face);
            aShapeTool->SetShape(faceLabel, fixed);
            face = fixed;
        }
        builder.Add(compound, face);

        if (hasVisMat) {
            aColorTool->SetColor(faceLabel, rgba, XCAFDoc_ColorSurf);
        }
    }
#else
    boost::ignore_unused(hDoc);
    boost::ignore_unused(subShapeLabels);
#endif

    return {std::move(compound)};
}

bool ReaderGltf::rebuilds(const TopoDS_Shape& shape, bool textured) const
{
#if OCC_VERSION_HEX >= 0x070500
    switch (rebuild) {
        case RebuildBRep::All:
            return true;
        case RebuildBRep::Auto:
            // What the rebuild would drop, asked of the material and then
            // of the mesh itself: a file whose images are named from
            // outside it -- a MaterialX look beside it, say -- has an
            // untextured material and UV nodes all the same
            return !textured && !hasTextureCoordinates(shape);
        case RebuildBRep::None:
            break;
    }
    return false;
#else
    boost::ignore_unused(shape);
    boost::ignore_unused(textured);
    return false;
#endif
}

bool ReaderGltf::cleanup() const
{
    return clean;
}

void ReaderGltf::setCleanup(bool value)
{
    clean = value;
}

ReaderGltf::RebuildBRep ReaderGltf::rebuildBRep() const
{
    return rebuild;
}

void ReaderGltf::setRebuildBRep(RebuildBRep value)
{
    rebuild = value;
}

TopoDS_Shape ReaderGltf::fixShape(TopoDS_Shape shape)  // NOLINT
{
    // The glTF reader creates a compound of faces that only contains the triangulation
    // but not the underlying surfaces. This leads to faces without boundaries.
    // The triangulation is used to create a valid shape.
    const double tolerance = 0.5;
    std::vector<Base::Vector3d> points;
    std::vector<Data::ComplexGeoData::Facet> facets;
    Part::TopoShape sh(shape);
    sh.getFaces(points, facets, tolerance);
    sh.setFaces(points, facets, tolerance);

    if (cleanup()) {
        sh.sewShape();
        return sh.removeSplitter();
    }

    return sh.getShape();
}
