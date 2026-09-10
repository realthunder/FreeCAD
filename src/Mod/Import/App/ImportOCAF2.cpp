/****************************************************************************
 *   Copyright (c) 2018 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"
#if defined(__MINGW32__)
#define WNT  // avoid conflict with GUID
#endif
#ifndef _PreComp_
# include <gp_Trsf.hxx>
# include <Interface_Static.hxx>
# include <BRep_Builder.hxx>
# include <Quantity_ColorRGBA.hxx>
# include <Standard_Failure.hxx>
# include <Standard_Version.hxx>
# include <TDataStd_Name.hxx>
# include <TDF_AttributeSequence.hxx>
# include <TDF_ChildIterator.hxx>
# include <TDF_Label.hxx>
# include <TDF_LabelSequence.hxx>
# include <TDF_Tool.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS_Compound.hxx>
# include <TopoDS_Iterator.hxx>
# include <XCAFDoc_ColorTool.hxx>
# include <XCAFDoc_DocumentTool.hxx>
# include <XCAFDoc_GraphNode.hxx>
# include <XCAFDoc_ShapeTool.hxx>
# include <XCAFDoc_VisMaterial.hxx>
# include <XCAFDoc_VisMaterialTool.hxx>
# include <Image_Texture.hxx>
#endif

#include <algorithm>

#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm/replace_if.hpp>
#include <boost/format.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>
#include <App/DocumentObserver.h>
#include <App/GroupExtension.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Base/Base64.h>
#include <Base/FileInfo.h>
#include <Base/Parameter.h>
#include <Mod/Part/App/FeatureCompound.h>
#include <Mod/Part/App/Interface.h>
#include <Mod/Part/App/OCAF/ImportExportSettings.h>

#include "ImportOCAF2.h"


#if OCC_VERSION_HEX >= 0x070500
// See https://dev.opencascade.org/content/occt-3d-viewer-becomes-srgb-aware
#define OCC_COLOR_SPACE Quantity_TOC_sRGB
#else
#define OCC_COLOR_SPACE Quantity_TOC_RGB
#endif

FC_LOG_LEVEL_INIT("Import", true, true)

using namespace Import;

ImportOCAFOptions::ImportOCAFOptions()
{
    defaultFaceColor.setPackedValue(0xCCCCCCFF);
    defaultFaceColor.a = 1.0f;  // opaque

    defaultEdgeColor.setPackedValue(421075455UL);  // 0x191919FF
    defaultEdgeColor.a = 1.0f;  // opaque
}

ImportOCAF2::ImportOCAF2(Handle(TDocStd_Document) hDoc, App::Document* doc, const std::string& name)
    : pDoc(hDoc)
    , pDocument(doc)
    , default_name(name)
{
    aShapeTool = XCAFDoc_DocumentTool::ShapeTool(pDoc->Main());
    aColorTool = XCAFDoc_DocumentTool::ColorTool(pDoc->Main());
    aMaterialTool = XCAFDoc_DocumentTool::VisMaterialTool(pDoc->Main());

    if (pDocument->isSaved()) {
        Base::FileInfo fi(pDocument->FileName.getValue());
        filePath = fi.dirPath();
    }

    setUseLinkGroup(options.useLinkGroup);
}

ImportOCAF2::~ImportOCAF2() = default;

ImportOCAFOptions ImportOCAF2::customImportOptions()
{
    Part::OCAF::ImportExportSettings settings;

    ImportOCAFOptions defaultOptions;
    // useLegacyImporter is deliberately not read from preferences: the legacy
    // importer is retired and reachable only by explicitly passing legacy=True
    // through the Python API, which calls setUseLegacyImporter() afterwards.
    defaultOptions.merge = settings.getReadShapeCompoundMode();
    defaultOptions.useLinkGroup = settings.getUseLinkGroup();
    defaultOptions.useBaseName = settings.getUseBaseName();
    defaultOptions.importHidden = settings.getImportHiddenObject();
    defaultOptions.reduceObjects = settings.getReduceObjects();
    defaultOptions.showProgress = settings.getShowProgress();
    defaultOptions.mode = static_cast<int>(settings.getImportMode());

    auto hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    defaultOptions.defaultFaceColor.setPackedValue(
        hGrp->GetUnsigned("DefaultShapeColor", defaultOptions.defaultFaceColor.getPackedValue()));
    defaultOptions.defaultFaceColor.a = 1.0f;  // opaque

    defaultOptions.defaultEdgeColor.setPackedValue(
        hGrp->GetUnsigned("DefaultShapeLineColor",
                          defaultOptions.defaultEdgeColor.getPackedValue()));
    defaultOptions.defaultEdgeColor.a = 1.0f;  // opaque

    return defaultOptions;
}

void ImportOCAF2::setImportOptions(ImportOCAFOptions opts)
{
    options = opts;
    setUseLinkGroup(options.useLinkGroup);
}

void ImportOCAF2::setUseLinkGroup(bool enable)
{
    options.useLinkGroup = enable;

    // Interface_Static::SetIVal("read.stepcaf.subshapes.name",1);
    aShapeTool->SetAutoNaming(!enable);
}

void ImportOCAF2::setMode(int m)
{
    if (m < 0 || m >= ModeMax) {
        FC_WARN("Invalid import mode " << m);
    }
    else {
        options.mode = m;
    }

    if (options.mode != SingleDoc) {
        if (pDocument->isSaved()) {
            Base::FileInfo fi(pDocument->FileName.getValue());
            filePath = fi.dirPath();
        }
        else {
            FC_WARN("Disable multi-document mode because the input document is not saved.");
        }
    }
}

static void setPlacement(App::PropertyPlacement* prop, const TopoDS_Shape& shape)
{
    prop->setValue(Base::Placement(Part::TopoShape::convert(shape.Location().Transformation()))
                   * prop->getValue());
}

std::string ImportOCAF2::getLabelName(TDF_Label label)
{
    std::string name;
    if (label.IsNull()) {
        return name;
    }
    if (!XCAFDoc_ShapeTool::IsReference(label)) {
        return Tools::labelName(label);
    }
    if (!options.useBaseName) {
        name = Tools::labelName(label);
    }
    TDF_Label ref;
    if (name.empty() && XCAFDoc_ShapeTool::GetReferredShape(label, ref)) {
        name = Tools::labelName(ref);
    }
    return name;
}

void ImportOCAF2::setObjectName(Info &info, TDF_Label label, bool checkExistingName) {
    if(!info.obj) {
        return;
    }
    std::string name = getLabelName(label);
    if (!checkExistingName || info.baseName.size() < name.size()) {
        info.baseName = std::move(name);
    }
    info.baseName = getLabelName(label);
    if (!info.baseName.empty()) {
        info.obj->Label.setValue(info.baseName.c_str());
    }
    else {
        auto linked = info.obj->getLinkedObject(false);
        if (!linked || linked == info.obj) {
            return;
        }
        info.obj->Label.setValue(linked->Label.getValue());
    }
}

bool ImportOCAF2::getColor(const TopoDS_Shape& shape, Info& info, bool check, bool noDefault)
{
    bool ret = false;
    Quantity_ColorRGBA aColor;
    if (aColorTool->GetColor(shape, XCAFDoc_ColorSurf, aColor)) {
        App::Color c = Tools::convertColor(aColor);
        if (!check || info.faceColor != c) {
            info.faceColor = c;
            info.hasFaceColor = true;
            ret = true;
        }
    }
    if (!noDefault && !info.hasFaceColor && aColorTool->GetColor(shape, XCAFDoc_ColorGen, aColor)) {
        App::Color c = Tools::convertColor(aColor);
        if (!check || info.faceColor != c) {
            info.faceColor = c;
            info.hasFaceColor = true;
            ret = true;
        }
    }
    if (aColorTool->GetColor(shape, XCAFDoc_ColorCurv, aColor)) {
        App::Color c = Tools::convertColor(aColor);
        // Some STEP include a curve color with the same value of the face
        // color. And this will look weird in FC. So for shape with face
        // we'll ignore the curve color, if it is the same as the face color.
        if ((c != info.faceColor || !TopExp_Explorer(shape, TopAbs_FACE).More())
            && (!check || info.edgeColor != c)) {
            info.edgeColor = c;
            info.hasEdgeColor = true;
            ret = true;
        }
    }
    if (!check) {
        if (!info.hasFaceColor) {
            info.faceColor = options.defaultFaceColor;
        }
        if (!info.hasEdgeColor) {
            info.edgeColor = options.defaultEdgeColor;
        }
    }
    return ret;
}

struct ImportOCAF2::ColorInfo {
    Part::TopoShape tshape;
    std::vector<App::Color> faceColors;
    std::vector<App::Color> edgeColors;
    /// One whole material per face; filled by scanFaceMaterials only when
    /// a field beyond diffuse varies across the faces
    std::vector<App::MaterialAppearance> faceMaterials;
    /// faceMaterials carry raw PBR slots (every contributing material had
    /// the PBR definition) -- the appearance they land in goes PBR mode
    bool pbrMaterials = false;
    /// at least one face material carries an image of its own, so the
    /// appearance -- not a whole-object texture property -- is where
    /// this shape's base colour images live
    bool faceImages = false;
    App::Color faceColor;
    App::Color edgeColor;
    bool hasFaceColor = false;
    bool hasEdgeColor = false;
};

// Check for uniform color
static void mergeColor(bool &hasColors, App::Color &color, std::vector<App::Color> &colors) {
    if(colors.empty())
        return;
    if(!hasColors) {
        colors.clear();
        return;
    }
    hasColors = false;
    auto &firstColor = colors[0];
    for(auto &c : colors) {
        if(c!=firstColor) {
            hasColors = true;
            return;
        }
    }
    color = firstColor;
    colors.clear();
}

bool ImportOCAF2::getRenderMaterial(TDF_Label label, RenderMaterial& mat)
{
    if (label.IsNull() || aMaterialTool.IsNull()) {
        return false;
    }

    // The label itself first; glTF meshes usually carry the material on
    // the (face) sub shape labels instead, and a component label refers
    // to the actual shape label. First found wins - per-face materials
    // collapse to one whole-object material for now.
    auto lookup = [this](TDF_Label l) -> Handle(XCAFDoc_VisMaterial) {
        Handle(XCAFDoc_VisMaterial) m = aMaterialTool->GetShapeMaterial(l);
        if (!m.IsNull() && m->HasPbrMaterial()) {
            return m;
        }
        TDF_LabelSequence seq;
        if (aShapeTool->GetSubShapes(l, seq)) {
            for (Standard_Integer i = 1; i <= seq.Length(); ++i) {
                m = aMaterialTool->GetShapeMaterial(seq.Value(i));
                if (!m.IsNull() && m->HasPbrMaterial()) {
                    return m;
                }
            }
        }
        return nullptr;
    };
    Handle(XCAFDoc_VisMaterial) visMat = lookup(label);
    if (visMat.IsNull()) {
        TDF_Label ref;
        if (XCAFDoc_ShapeTool::IsReference(label)
            && XCAFDoc_ShapeTool::GetReferredShape(label, ref)) {
            visMat = lookup(ref);
        }
    }
    if (visMat.IsNull()) {
        return false;
    }
    return extractRenderMaterial(visMat, mat);
}

namespace {
/// A glTF texture's own encoded bytes, base64 -- what a per-face
/// material carries an image AS (App::MaterialAppearance::image).
///
/// The whole-object texture properties are PropertyFileIncluded and can
/// name the temp file the reader extracts, because the property copies
/// it into the document. A per-face image has no such property behind
/// it: App::MaterialAppearance::imagePath is persisted verbatim, so a temp file
/// would be gone by the time the document is reopened. The bytes travel
/// instead, once per face that wears them.
std::string encodeTexture(const Handle(Image_Texture)& tex)
{
    if (tex.IsNull()) {
        return {};
    }
    std::ostringstream str(std::ios::out | std::ios::binary);
    // The file name is the message text OCCT names on failure; nothing
    // is written to disk on this overload.
    if (!tex->WriteImage(str, "gltf_face_image")) {
        return {};
    }
    const std::string raw = str.str();
    if (raw.empty()) {
        return {};
    }
    return Base::base64_encode(raw.data(), raw.size());
}
} // namespace

bool ImportOCAF2::extractRenderMaterial(const Handle(XCAFDoc_VisMaterial)& visMat,
                                        RenderMaterial& mat)
{
    if (visMat.IsNull() || !visMat->HasPbrMaterial()) {
        return false;
    }

    const XCAFDoc_VisMaterialPBR& pbr = visMat->PbrMaterial();
    // Both factors at the glTF defaults (1.0) with no textures is what a
    // color-only material reads back as - glTF has no way to say "no PBR".
    // Skip it so plain colored exports do not grow render properties (and
    // turn fully metallic) on re-import.
    if (pbr.BaseColorTexture.IsNull() && pbr.NormalTexture.IsNull()
        && pbr.EmissiveTexture.IsNull() && pbr.OcclusionTexture.IsNull()
        && pbr.MetallicRoughnessTexture.IsNull()
        && pbr.Metallic >= 1.0f && pbr.Roughness >= 1.0f) {
        return false;
    }
    mat.metallic = pbr.Metallic;
    mat.roughness = pbr.Roughness;
    mat.hasBaseColor = true;
    mat.baseColor = Tools::convertColor(pbr.BaseColor);

    // Extract texture images (embedded in a .glb, or referenced files) to
    // temporary files; PropertyFileIncluded copies them into the document.
    auto extract = [](const Handle(Image_Texture)& tex,
                      const char* tag) -> std::string {
        if (tex.IsNull()) {
            return {};
        }
        std::string ext = tex->ProbeImageFileFormat().ToCString();
        if (ext.empty()) {
            ext = "png";
        }
        // getTempFileName appends the unique part after the given name,
        // so the extension goes on afterwards (image loaders sniff the
        // content anyway; the extension is for the user's benefit).
        std::string path =
            Base::FileInfo::getTempFileName(tag) + "." + ext;
        if (!tex->WriteImage(path.c_str())) {
            return {};
        }
        return path;
    };
    mat.baseColorTexture = extract(pbr.BaseColorTexture, "gltf_basecolor");
    mat.normalMapTexture = extract(pbr.NormalTexture, "gltf_normal");
    mat.emissiveTexture = extract(pbr.EmissiveTexture, "gltf_emissive");
    mat.occlusionTexture = extract(pbr.OcclusionTexture, "gltf_occlusion");
    mat.metallicRoughnessTexture =
        extract(pbr.MetallicRoughnessTexture, "gltf_metalrough");

    mat.valid = true;
    return true;
}

void ImportOCAF2::scanElementColors(TDF_Label label,
                                    ColorInfo& colors,
                                    Info& info,
                                    bool& hasFaceColors,
                                    bool& hasEdgeColors)
{
    Part::TopoShape &tshape = colors.tshape;
    TDF_LabelSequence seq;
    if(!label.IsNull() && aShapeTool->GetSubShapes(label,seq)) {
        colors.faceColors.assign(tshape.countSubShapes(TopAbs_FACE),info.faceColor);
        colors.edgeColors.assign(tshape.countSubShapes(TopAbs_EDGE),info.edgeColor);
        // Two passes to get sub shape colors. First pass, look for solid, and
        // second pass look for face and edges. This allows lower level
        // subshape to override color of higher level ones.
        for (int j = 0; j < 2; ++j) {
            for (int i = 1; i <= seq.Length(); ++i) {
                TDF_Label l = seq.Value(i);
                TopoDS_Shape subShape = aShapeTool->GetShape(l);
                if (subShape.IsNull()) {
                    continue;
                }
                if (subShape.ShapeType() == TopAbs_FACE || subShape.ShapeType() == TopAbs_EDGE) {
                    if (j == 0) {
                        continue;
                    }
                }
                else if (j != 0) {
                    continue;
                }

                bool foundFaceColor = false;
                bool checkSubFaceColor = false;
                bool checkSubEdgeColor = false;
                App::Color faceColor,edgeColor;
                Quantity_ColorRGBA aColor;
                if(aColorTool->GetColor(l, XCAFDoc_ColorSurf, aColor) ||
                   aColorTool->GetColor(l, XCAFDoc_ColorGen, aColor))
                {
                    foundFaceColor = true;
                    faceColor = Tools::convertColor(aColor);
                    checkSubFaceColor = faceColor!=info.faceColor;
                }
                if (aColorTool->GetColor(l, XCAFDoc_ColorCurv, aColor)) {
                    edgeColor = Tools::convertColor(aColor);
                    checkSubEdgeColor = edgeColor!=info.edgeColor;
                    if (j == 0
                            && foundFaceColor
                            && !colors.faceColors.empty()
                            && edgeColor == faceColor) {
                        // Do not set edge the same color as face
                        checkSubEdgeColor = false;
                    }
                }

                if(checkSubFaceColor) {
                    for(TopExp_Explorer exp(subShape,TopAbs_FACE);exp.More();exp.Next()) {
                        int idx = tshape.findShape(exp.Current())-1;
                        if(idx>=0 && idx<(int)colors.faceColors.size()) {
                            colors.faceColors[idx] = faceColor;
                            hasFaceColors = true;
                            info.hasFaceColor = true;
                        }
                    }
                }
                if(checkSubEdgeColor) {
                    for(TopExp_Explorer exp(subShape,TopAbs_EDGE);exp.More();exp.Next()) {
                        int idx = tshape.findShape(exp.Current())-1;
                        if(idx>=0 && idx<(int)colors.edgeColors.size()) {
                            colors.edgeColors[idx] = edgeColor;
                            hasEdgeColors = true;
                            info.hasEdgeColor = true;
                        }
                    }
                }
            }
        }
    }
}

bool ImportOCAF2::scanFaceMaterials(TDF_Label label, ColorInfo& colors, const Info& info)
{
    Part::TopoShape& tshape = colors.tshape;
    TDF_LabelSequence seq;
    if (label.IsNull() || aMaterialTool.IsNull() || !aShapeTool->GetSubShapes(label, seq)) {
        return false;
    }
    int numFaces = (int)tshape.countSubShapes(TopAbs_FACE);
    if (!numFaces) {
        return false;
    }

    // Gather first, convert after: which conversion applies depends on
    // whether EVERY contributing material has the PBR definition. When
    // all do (glTF -- the format is PBR by definition), the raw factors
    // go into the appearance's PBR slots exactly and the list goes PBR
    // mode; otherwise (STEP's reflectance materials above all) the
    // common (Phong) representation, as always. One list, one mode.
    std::vector<std::pair<int, Handle(XCAFDoc_VisMaterial)>> faceMatList;
    for (int i = 1; i <= seq.Length(); ++i) {
        TDF_Label l = seq.Value(i);
        TopoDS_Shape subShape = aShapeTool->GetShape(l);
        if (subShape.IsNull() || !TopExp_Explorer(subShape, TopAbs_FACE).More()) {
            continue;
        }
        Handle(XCAFDoc_VisMaterial) visMat = aMaterialTool->GetShapeMaterial(l);
        if (visMat.IsNull() || visMat->IsEmpty()) {
            continue;
        }
        for (TopExp_Explorer exp(subShape, TopAbs_FACE); exp.More(); exp.Next()) {
            int idx = tshape.findShape(exp.Current()) - 1;
            if (idx >= 0 && idx < numFaces) {
                faceMatList.emplace_back(idx, visMat);
            }
        }
    }
    Handle(XCAFDoc_VisMaterial) wholeMat;
    if (faceMatList.empty()) {
        // No face label carries a material: a single-primitive mesh (or a
        // whole-object style merged by our own exporter) has it on the
        // shape label itself.
        wholeMat = aMaterialTool->GetShapeMaterial(label);
        if (wholeMat.IsNull() || wholeMat->IsEmpty()) {
            TDF_Label ref;
            if (XCAFDoc_ShapeTool::IsReference(label)
                && XCAFDoc_ShapeTool::GetReferredShape(label, ref)) {
                wholeMat = aMaterialTool->GetShapeMaterial(ref);
            }
        }
        if (wholeMat.IsNull() || wholeMat->IsEmpty()) {
            return false;
        }
    }

    bool allPbr = wholeMat.IsNull() || wholeMat->HasPbrMaterial();
    for (const auto& v : faceMatList) {
        if (!v.second->HasPbrMaterial()) {
            allPbr = false;
            break;
        }
    }

    // The base colour images, keyed by the texture the reader shares
    // per glTF image: several materials may name the same one, and the
    // encoding is a copy of the whole file.
    std::map<const Image_Texture*, std::string> encoded;
    auto convert = [&](const Handle(XCAFDoc_VisMaterial)& visMat,
                       bool withImage) -> App::MaterialAppearance {
        if (allPbr) {
            // The raw factors: metallic into the specular alpha under a
            // white tint, roughness into the shininess slot -- exact, no
            // Phong approximation. The base colour rides the resolved
            // face colours below, as always, and the emissive reads
            // through the Common conversion exactly as it always has (the
            // stage-4 colour-space conventions, and their verified round
            // trip, hold unchanged). Material() rather than DEFAULT: its
            // untouched fields equal the appearance property's own
            // defaults, so they cost no storage.
            App::MaterialAppearance mat;
            const XCAFDoc_VisMaterialPBR& pbr = visMat->PbrMaterial();
            // Tagged, not converted: the slots below ARE the PBR reading,
            // and the tag is what carries that to the appearance the
            // values are assigned to.
            mat.pbr = true;
            mat.specularColor.set(1.0f, 1.0f, 1.0f);
            mat.specularColor.a = pbr.Metallic;
            mat.shininess = pbr.Roughness;
            XCAFDoc_VisMaterialCommon common = visMat->HasCommonMaterial()
                ? visMat->CommonMaterial()
                : visMat->ConvertToCommonMaterial();
            if (common.IsDefined) {
                mat.emissiveColor = Tools::convertColor(Quantity_ColorRGBA(common.EmissiveColor));
            }
            // The face's own picture. Only when the faces really do
            // carry materials one by one: a single whole-object
            // material states its texture through the Render_* file
            // properties, and stating it twice would modulate twice.
            if (withImage && !pbr.BaseColorTexture.IsNull()) {
                const Image_Texture* key = pbr.BaseColorTexture.get();
                auto it = encoded.find(key);
                if (it == encoded.end()) {
                    it = encoded.emplace(key, encodeTexture(pbr.BaseColorTexture)).first;
                }
                mat.image = it->second;
            }
            return mat;
        }
        App::MaterialAppearance mat(App::MaterialAppearance::DEFAULT);
        XCAFDoc_VisMaterialCommon common = visMat->HasCommonMaterial()
            ? visMat->CommonMaterial()
            : visMat->ConvertToCommonMaterial();
        if (common.IsDefined) {
            mat.ambientColor = Tools::convertColor(Quantity_ColorRGBA(common.AmbientColor));
            mat.specularColor = Tools::convertColor(Quantity_ColorRGBA(common.SpecularColor));
            mat.emissiveColor = Tools::convertColor(Quantity_ColorRGBA(common.EmissiveColor));
            mat.shininess = common.Shininess;
        }
        return mat;
    };
    auto hasEmissive = [](const App::MaterialAppearance& mat) {
        return mat.emissiveColor.r > 0.004f || mat.emissiveColor.g > 0.004f
            || mat.emissiveColor.b > 0.004f;
    };
    // What a face with no material of its own reads as, per mode: the PBR
    // filler mirrors the appearance property's own unset reading (white
    // tint, metallic 0, mid roughness) so a uniform run of it elides.
    App::MaterialAppearance defMat(App::MaterialAppearance::DEFAULT);
    if (allPbr) {
        defMat = App::MaterialAppearance();
        defMat.pbr = true;
        defMat.specularColor.set(1.0f, 1.0f, 1.0f);
        defMat.specularColor.a = 0.0f;
        defMat.shininess = 0.5f;
    }

    std::vector<App::MaterialAppearance> mats;
    if (!faceMatList.empty()) {
        mats.assign(numFaces, defMat);
        std::unordered_map<const XCAFDoc_VisMaterial*, App::MaterialAppearance> converted;
        for (const auto& v : faceMatList) {
            auto it = converted.find(v.second.get());
            if (it == converted.end()) {
                it = converted.emplace(v.second.get(), convert(v.second, true)).first;
            }
            mats[v.first] = it->second;
        }
    }
    else {
        App::MaterialAppearance mat = convert(wholeMat, false);
        // A Phong whole-object material matters only for its emissive --
        // see the gate below. A PBR one always matters: its metallic and
        // roughness are authored factors the renderer shades natively.
        if (!allPbr && !hasEmissive(mat)) {
            return false;
        }
        mats.assign(numFaces, mat);
    }

    // A Phong list is meaningful when a field a colour list cannot carry
    // varies across the faces -- or when a uniform emissive is lit at
    // all: emissive has an unambiguous default (black) and no other
    // property carries it, so dropping it loses light, while a uniform
    // specular or shininess only re-skins what the default look already
    // approximates. A PBR list is always meaningful, as above.
    if (!allPbr) {
        bool varies = false;
        for (int idx = 1; idx < numFaces; ++idx) {
            if (mats[idx].ambientColor != mats[0].ambientColor
                || mats[idx].specularColor != mats[0].specularColor
                || mats[idx].emissiveColor != mats[0].emissiveColor
                || mats[idx].shininess != mats[0].shininess) {
                varies = true;
                break;
            }
        }
        if (!varies && !hasEmissive(mats[0])) {
            return false;
        }
    }
    colors.pbrMaterials = allPbr;
    colors.faceImages =
        std::any_of(mats.begin(), mats.end(),
                    [](const App::MaterialAppearance& m) { return !m.image.empty(); });

    // Diffuse and transparency ride the resolved face colours (the reader
    // mirrors each material's base colour into the colour labels, and a
    // colour label overrides).
    for (int idx = 0; idx < numFaces; ++idx) {
        App::Color c = (int)colors.faceColors.size() > idx ? colors.faceColors[idx]
                                                           : info.faceColor;
        mats[idx].diffuseColor = c;
        mats[idx].transparency = 1.0f - c.a;
    }
    colors.faceMaterials = std::move(mats);
    return true;
}

// glTF meshes may carry a distinct visualization material per face
// (each glTF primitive imports as one face). Most of such a material
// the faces of one object can now each say for themselves - colour,
// metallic, roughness, and since the per-face texture palette the base
// colour image too - but the remaining maps (normal, emissive,
// occlusion, metallic-roughness) are still one per draw and live in
// the object's Render_* properties. So when the face sub shape labels
// resolve to more than one of THOSE - or to one that does not cover
// every face - the shape splits into one feature per group under the
// same group container an assembly uses (links to the label then
// reference the container). Everything else keeps the plain
// one-feature import below.
struct ImportOCAF2::MaterialGroups
{
    std::vector<int> faceGroup;
    std::vector<RenderMaterial> mats;
    std::vector<std::string> names;
    bool split = false;
};

void ImportOCAF2::scanMaterialGroups(TDF_Label label,
                                     Part::TopoShape& tshape,
                                     MaterialGroups& groups)
{
    TDF_LabelSequence seq;
    if (label.IsNull() || !aShapeTool->GetSubShapes(label, seq)) {
        return;
    }
    if (seq.Length() > 0 && !aMaterialTool.IsNull()) {
        int numFaces = (int)tshape.countSubShapes(TopAbs_FACE);
        std::vector<int> faceGroup(numFaces, 0);
        std::vector<RenderMaterial> &groupMats = groups.mats;
        std::vector<std::string> &groupNames = groups.names;
        // Group by what a face CANNOT say for itself, which is the
        // whole reason a shape ever splits: the maps that are one per
        // draw. Everything else a glTF material carries is expressible
        // face by face and merges into one group --
        //
        //   base colour        -> the face colours (always was)
        //   metallic/roughness -> the appearance's per-face PBR slots
        //   base colour image  -> the appearance's per-face image
        //
        // so a mesh whose primitives differ only in those keeps one
        // feature. Texture identity is the Image_Texture handle - the
        // reader shares one per glTF image.
        using MatKey = std::tuple<const void*, const void*, const void*,
                                  const void*>;
        auto matKey = [](const XCAFDoc_VisMaterialPBR& pbr) -> MatKey {
            return std::make_tuple((const void*)pbr.MetallicRoughnessTexture.get(),
                                   (const void*)pbr.NormalTexture.get(),
                                   (const void*)pbr.EmissiveTexture.get(),
                                   (const void*)pbr.OcclusionTexture.get());
        };
        const MatKey noMaps {nullptr, nullptr, nullptr, nullptr};
        std::map<MatKey, int> matGroups;
        bool grouped = false;
        for (int i = 1; i <= seq.Length(); ++i) {
            TDF_Label l = seq.Value(i);
            TopoDS_Shape subShape = aShapeTool->GetShape(l);
            // The sub shape may be more than a bare face: an untextured
            // glTF primitive gets its B-Rep rebuilt (and possibly sewn
            // into a shell) by the reader's fixShape.
            if (subShape.IsNull()
                || !TopExp_Explorer(subShape, TopAbs_FACE).More()) {
                continue;
            }
            Handle(XCAFDoc_VisMaterial) visMat = aMaterialTool->GetShapeMaterial(l);
            if (visMat.IsNull() || !visMat->HasPbrMaterial()) {
                continue;
            }
            const MatKey key = matKey(visMat->PbrMaterial());
            if (key == noMaps) {
                // Nothing here needs a draw of its own: the face keeps
                // group 0 and says all of it itself.
                continue;
            }
            int group = 0;
            auto it = matGroups.find(key);
            if (it != matGroups.end()) {
                group = it->second;
            }
            else {
                RenderMaterial groupMat;
                if (extractRenderMaterial(visMat, groupMat)) {
                    groupMats.push_back(std::move(groupMat));
                    group = (int)groupMats.size();
                    groupNames.push_back(
                        visMat->RawName().IsNull()
                            ? std::string()
                            : visMat->RawName()->ToCString());
                }
                matGroups.emplace(key, group);
            }
            if (!group) {
                continue;
            }
            for (TopExp_Explorer exp(subShape, TopAbs_FACE); exp.More();
                 exp.Next()) {
                int idx = tshape.findShape(exp.Current()) - 1;
                if (idx >= 0 && idx < numFaces) {
                    faceGroup[idx] = group;
                    grouped = true;
                }
            }
        }
        bool ungrouped =
            std::find(faceGroup.begin(), faceGroup.end(), 0) != faceGroup.end();
        if (grouped && numFaces > 1 && (groupMats.size() > 1 || ungrouped)) {
            groups.split = true;
            groups.faceGroup = std::move(faceGroup);
        }
    }
}

bool ImportOCAF2::createObject(App::Document* doc,
                               TDF_Label label,
                               const TopoDS_Shape& shape,
                               Info& info,
                               bool newDoc)
{
    // a purely triangulated face (e.g. a textured glTF mesh kept as-is to
    // preserve its UV nodes) has no vertices — check for faces too
    if (shape.IsNull()
        || (!TopExp_Explorer(shape, TopAbs_VERTEX).More()
            && !TopExp_Explorer(shape, TopAbs_FACE).More())) {
        FC_WARN(Tools::labelName(label) << " has empty shape");
        return false;
    }

    getColor(shape, info);
    bool hasFaceColors = false;
    bool hasEdgeColors = false;

    ColorInfo colors;
    colors.tshape.setShape(shape);
    Part::TopoShape &tshape = colors.tshape;

    scanElementColors(label, colors, info, hasFaceColors, hasEdgeColors);

    Part::Feature* feature;

    if (newDoc && (options.mode == ObjectPerDoc || options.mode == ObjectPerDir)) {
        doc = getDocument(doc, label);
    }

    mergeColor(hasFaceColors,info.faceColor,colors.faceColors);
    mergeColor(hasEdgeColors,info.edgeColor,colors.edgeColors);

    colors.faceColor = info.faceColor;
    colors.edgeColor = info.edgeColor;
    colors.hasFaceColor = info.hasFaceColor;
    colors.hasEdgeColor = info.hasEdgeColor;

    scanFaceMaterials(label, colors, info);

    MaterialGroups matGroups;
    scanMaterialGroups(label, tshape, matGroups);
    if (matGroups.split) {
        const auto &faceGroup = matGroups.faceGroup;
        const auto &groupMats = matGroups.mats;
        const auto &groupNames = matGroups.names;
        int numFaces = (int)tshape.countSubShapes(TopAbs_FACE);
        {
            std::vector<App::DocumentObject*> children;
            boost::dynamic_bitset<> visibilities;
            for (int g = 0; g <= (int)groupMats.size(); ++g) {
                BRep_Builder builder;
                TopoDS_Compound comp;
                builder.MakeCompound(comp);
                std::vector<App::Color> childColors;
                std::vector<App::MaterialAppearance> childMats;
                for (int idx = 0; idx < numFaces; ++idx) {
                    if (faceGroup[idx] != g) {
                        continue;
                    }
                    builder.Add(comp, tshape.getSubShape(TopAbs_FACE, idx + 1));
                    childColors.push_back((int)colors.faceColors.size() > idx
                                              ? colors.faceColors[idx]
                                              : info.faceColor);
                    if (!colors.faceMaterials.empty()) {
                        childMats.push_back(colors.faceMaterials[idx]);
                    }
                }
                if (childColors.empty()) {
                    continue;
                }
                auto child = static_cast<Part::Feature*>(
                    doc->addObject("Part::Feature", tshape.shapeName().c_str()));
                child->Shape.setValue(comp);
                if (g > 0 && !groupNames[g - 1].empty()) {
                    child->Label.setValue(groupNames[g - 1].c_str());
                }
                applyFaceColors(child, {info.faceColor});
                applyEdgeColors(child, {info.edgeColor});
                if (!childMats.empty()) {
                    // The group shares its Render_* material, but the common
                    // fields (emissive above all) may still differ inside it
                    // -- they are not part of the grouping key.
                    applyFaceMaterials(child, childMats, colors.pbrMaterials);
                }
                else {
                    applyFaceColors(child, childColors);
                }
                if (g > 0) {
                    applyRenderMaterial(child, groupMats[g - 1]);
                }
                children.push_back(child);
                visibilities.push_back(true);
            }
            if (children.size() > 1
                && createGroup(doc, info, shape, children, visibilities)) {
                return true;
            }
        }
    }

    feature = static_cast<Part::Feature*>(doc->addObject("Part::Feature",tshape.shapeName().c_str()));
    feature->Shape.setValue(shape);
    // feature->Visibility.setValue(false);

    applyFaceColors(feature,{info.faceColor});
    applyEdgeColors(feature,{info.edgeColor});
    if (!colors.faceMaterials.empty())
        applyFaceMaterials(feature, colors.faceMaterials, colors.pbrMaterials);
    else if(colors.faceColors.size())
        applyFaceColors(feature,colors.faceColors);
    if(colors.edgeColors.size())
        applyEdgeColors(feature,colors.edgeColors);

    RenderMaterial rmat;
    if (getRenderMaterial(label, rmat))
        applyRenderMaterial(feature, rmat);

    info.propPlacement = &feature->Placement;
    info.obj = feature;
    return true;
}

App::Document* ImportOCAF2::getDocument(App::Document* doc, TDF_Label label)
{
    if (filePath.empty() || options.mode == SingleDoc || options.merge) {
        return doc;
    }

    auto name = getLabelName(label);
    if (name.empty()) {
        return doc;
    }

    auto newDoc = App::GetApplication().newDocument(name.c_str(), name.c_str(), false);
    myNewDocuments.push_back(newDoc);
    newDoc->setUndoMode(0);

    std::ostringstream ss;
    Base::FileInfo fi(doc->FileName.getValue());
    std::string path = fi.dirPath();
    if (options.mode == GroupPerDir || options.mode == ObjectPerDir) {
        for (int i = 0; i < 1000; ++i) {
            ss.str("");
            ss << path << '/' << fi.fileNamePure() << "_parts";
            if (i > 0) {
                ss << '_' << std::setfill('0') << std::setw(3) << i;
            }
            Base::FileInfo fi2(ss.str());
            if (fi2.exists()) {
                if (!fi2.isDir()) {
                    continue;
                }
            }
            else if (!fi2.createDirectory()) {
                FC_WARN("Failed to create directory " << fi2.filePath());
                break;
            }
            path = fi2.filePath();
            break;
        }
    }
    std::string fname = newDoc->Label.getValue();
    // First, try save file using more descriptive label name.
    // Replace some common invalid characters
    boost::replace_if(fname, boost::is_any_of("<>:\"/\\|?*"), '_');
    for (int i = 0; i < 1000; ++i) {
        ss.str("");
        ss << path << '/' << fname;
        if(i>0) 
            ss << '_' << std::setfill('0') << std::setw(3) << i;
        ss << ".fcstd";
        Base::FileInfo fi(ss.str());
        if(!fi.exists()) {
            // No way to be sure the file name is legal, so just create one.
            Base::ofstream of(fi);
            if (!of)
                continue;
            of.close();
            fi.deleteFile();
            if(!newDoc->saveAs(fi.filePath().c_str()))
                break;
            return newDoc;
        }
    }

    // Using label as file name failed, fall back to internal name
    for (int i = 0; i < 1000; ++i) {
        ss.str("");
        ss << path << '/' << newDoc->getName();
        if(i>0)
            ss << '_' << std::setfill('0') << std::setw(3) << i;
        ss << ".fcstd";
        Base::FileInfo fi(ss.str());
        if (!fi.exists()) {
            if (!newDoc->saveAs(fi.filePath().c_str())) {
                break;
            }
            return newDoc;
        }
    }

    FC_WARN("Cannot save document for part '" << name << "'");
    return doc;
}

bool ImportOCAF2::createGroup(App::Document* doc,
                              Info& info,
                              const TopoDS_Shape& shape,
                              std::vector<App::DocumentObject*>& children,
                              const boost::dynamic_bitset<>& visibilities,
                              bool canReduce)
{
    assert(children.size() == visibilities.size());
    if (children.empty()) {
        return false;
    }
    bool hasColor = getColor(shape, info, false, true);
    if (canReduce && !hasColor && options.reduceObjects && children.size() == 1
        && visibilities[0]) {
        info.obj = children.front();
        info.free = true;
        info.propPlacement =
            dynamic_cast<App::PropertyPlacement*>(info.obj->getPropertyByName("Placement"));
        myCollapsedObjects.emplace(info.obj, info.propPlacement);
        return true;
    }
    for (auto& child : children) {
        if (child->getDocument() != doc) {
            auto link = static_cast<App::Link*>(doc->addObject("App::Link", "Link"));
            link->Label.setValue(child->Label.getValue());
            link->setLink(-1, child);
            auto pla = Base::freecad_dynamic_cast<App::PropertyPlacement>(
                child->getPropertyByName("Placement"));
            if (pla) {
                link->Placement.setValue(pla->getValue());
            }
            child = link;
        }
    }
    App::DocumentObject *group;
    if(!options.useLinkGroup) {
        auto part = static_cast<App::Part*>(doc->addObject("App::Part","Part"));
        group = part;
        int i=0;
        for(auto child : children)
            child->Visibility.setValue(visibilities[i++]);
        part->addObjects(children);
        info.propPlacement = &part->Placement;
    } else {
        auto linkGroup = static_cast<App::LinkGroup*>(doc->addObject("App::LinkGroup","LinkGroup"));
        group = linkGroup;
        linkGroup->ElementList.setValues(children);
        linkGroup->VisibilityList.setValue(visibilities);
        info.propPlacement = &linkGroup->Placement;
    }
    info.obj = group;
    if (getColor(shape, info, false, true)) {
        if (info.hasFaceColor) {
            applyLinkColor(group, -1, info.faceColor);
        }
    }
    return true;
}

App::DocumentObject* ImportOCAF2::loadShapes()
{
    if (options.useLegacyImporter) {
        ImportLegacy legacy(*this);
        legacy.setMerge(options.merge);
        legacy.loadShapes();
        return nullptr;
    }

    aShapeTool->SetAutoNaming(Standard_False);

    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
        Tools::dumpLabels(pDoc->Main(), aShapeTool, aColorTool);
    }

    TDF_LabelSequence labels;
    aShapeTool->GetShapes(labels);
    Base::SequencerLauncher seq("Importing...", labels.Length());
    FC_MSG("free shape count " << labels.Length());
    sequencer = options.showProgress ? &seq : nullptr;

    labels.Clear();
    myShapes.clear();
    myNames.clear();
    myCollapsedObjects.clear();
    myNewDocuments.clear();
    myDocumentStack.clear();

    std::vector<App::DocumentObject*> objs;
    aShapeTool->GetFreeShapes(labels);
    boost::dynamic_bitset<> vis;
    int count = 0;
    for (Standard_Integer i = 1; i <= labels.Length(); i++) {
        auto label = labels.Value(i);
        if (!options.importHidden && !aColorTool->IsVisible(label)) {
            continue;
        }
        ++count;
    }
    for (Standard_Integer i = 1; i <= labels.Length(); i++) {
        auto label = labels.Value(i);
        if (!options.importHidden && !aColorTool->IsVisible(label)) {
            continue;
        }
        auto obj = loadShape(pDocument, label, aShapeTool->GetShape(label), false, count > 1);
        if (obj) {
            objs.push_back(obj);
            vis.push_back(aColorTool->IsVisible(label));
        }
    }
    App::DocumentObject* ret = nullptr;
    if (objs.size() == 1) {
        ret = objs.front();
    }
    else {
        Info info;
        if (createGroup(pDocument, info, TopoDS_Shape(), objs, vis)) {
            ret = info.obj;
        }
    }
    if (ret) {
        ret->recomputeFeature(true);
    }
    if (options.merge && ret && !ret->isDerivedFrom(Part::Feature::getClassTypeId())) {
        auto shape = Part::Feature::getTopoShape(ret);
        auto feature =
            static_cast<Part::Feature*>(pDocument->addObject("Part::Feature", "Feature"));
        auto name = Tools::labelName(pDoc->Main());
        feature->Label.setValue(name.empty() ? default_name.c_str() : name.c_str());
        feature->Shape.setValue(shape);
        applyFaceColors(feature, {});

        std::vector<std::pair<App::Document*,std::string> > objNames;
        for (auto obj : App::Document::getDependencyList(objs, App::Document::DepSort)) {
            objNames.emplace_back(obj->getDocument(), obj->getNameInDocument());
        }
        for (auto rit = objNames.rbegin(); rit != objNames.rend(); ++rit) {
            rit->first->removeObject(rit->second.c_str());
        }
        ret = feature;
        ret->recomputeFeature(true);
    }
    sequencer = nullptr;
    for (auto doc : myNewDocuments)
        doc->setUndoMode(1);
    return ret;
}

// ---------------------------------------------------------------------------
// Progressive (two-stage) import
//
// analyze() mirrors the loadShapes()/loadShape()/createObject()/
// createAssembly() traversal but produces ProgOp descriptors instead of
// document objects, touching only the XCAF document (worker-thread
// safe). Node creation order is the apply order: a group op is reserved
// before its members are walked and a shape's first use creates its op
// before any reuse can reference it, so parents always precede children
// and link targets always precede links. SHUO element colors are only
// checked for existence (they feed the reduce decision); the concrete
// importers' applyElementColors() overrides are all empty, so nothing
// is lost by not applying them.

int ImportOCAF2::newProgOp(ProgOp::Type type)
{
    // The lock serializes the vector reallocation against the GUI thread's
    // locked reads (applyNextOp/resolveOp/progObject) during streaming.
    std::lock_guard<std::mutex> guard(myProgMutex);
    myProgOps.emplace_back();
    myProgOps.back().type = type;
    return int(myProgOps.size()) - 1;
}

int ImportOCAF2::resolveOp(int node) const
{
    std::lock_guard<std::mutex> guard(myProgMutex);
    while (node >= 0 && myProgOps[node].type == ProgOp::Collapsed) {
        node = myProgOps[node].resolveTo;
    }
    return node;
}

bool ImportOCAF2::analyzeBegin()
{
    myProgOps.clear();
    myProgObjs.clear();
    myProgApplied = 0;
    myProgRoot = -1;
    myProgFailed = false;
    myShapeNodes.clear();
    myLabelNodes.clear();
    myProgPublished = 0;
    myRootGroup = -1;
    myRootChildren = 0;
    myLastRootChild = -1;
    myAnalyzedRoots.clear();
    myClaimedSealed.clear();
    myInstanceNodes.clear();
    mySkeleton.clear();
    myNodeGroups.clear();
    myStreamedNodes.clear();
    myAdoptedNodes.clear();
    myRootAssemblyPending = false;

    // Excluded per design: multi-document modes mutate documents during
    // the traversal, merge throws the intermediate objects away again,
    // and the legacy importer is its own code path.
    if (options.merge || options.useLegacyImporter || options.mode != SingleDoc) {
        return false;
    }

    aShapeTool->SetAutoNaming(Standard_False);
    sequencer = nullptr;

    // Always reserve the root container: whether it survives is only known
    // once all roots are seen, and analyzeEnd() collapses it for a single
    // child (loadShapes() creates none then). The publication gate below
    // keeps it unapplied until a second child proves it final.
    myRootGroup = newProgOp(ProgOp::Group);
    return true;
}

bool ImportOCAF2::beginSkeleton(const std::vector<AssemblyNode>& nodes)
{
    mySkeleton = nodes;
    // Node 0 is the root itself, standing for the reserved root container.
    myNodeGroups.assign(nodes.size() + 1, -1);
    myNodeGroups[0] = myRootGroup;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (!node.isAssembly) {
            continue;
        }
        // An owner always precedes what it holds, so its container is
        // reserved by now; anything else is a tree this analysis cannot use.
        if (node.parent < 0 || node.parent >= int(myNodeGroups.size())
            || myNodeGroups[node.parent] < 0) {
            return false;
        }
        int group = newProgOp(ProgOp::Group);
        auto& op = myProgOps[group];
        op.parent = myNodeGroups[node.parent];
        op.placement = node.placement;
        op.label = node.name;
        myNodeGroups[i + 1] = group;
    }
    return true;
}

void ImportOCAF2::addStreamedShapes(const std::vector<std::pair<TopoDS_Shape, int>>& shapes)
{
    for (const auto& v : shapes) {
        int node = v.second;
        if (!v.first.IsNull() && node > 0 && node <= int(mySkeleton.size())) {
            myStreamedNodes[v.first] = node;
        }
    }
}

int ImportOCAF2::skeletonNodeOf(const TopoDS_Shape& shape) const
{
    // The assembly a shape stands for is told by the components streamed into
    // it: those arrived as free shapes and are the same shapes the assembly
    // holds, while the assembly itself is rebuilt when its parts are healed
    // and so cannot be recognized by its own shape.
    for (TopoDS_Iterator it(shape, Standard_False, Standard_False); it.More(); it.Next()) {
        auto found = myStreamedNodes.find(it.Value());
        if (found != myStreamedNodes.end()) {
            return mySkeleton[found->second - 1].parent;
        }
        if (it.Value().ShapeType() != TopAbs_COMPOUND) {
            continue;
        }
        int deeper = skeletonNodeOf(it.Value());
        if (deeper > 0) {
            return mySkeleton[deeper - 1].parent;
        }
    }
    return 0;
}

bool ImportOCAF2::analyzeRoots()
{
    TDF_LabelSequence labels;
    aShapeTool->GetFreeShapes(labels);
    for (Standard_Integer i = 1; i <= labels.Length(); i++) {
        auto label = labels.Value(i);
        if (!myAnalyzedRoots.emplace(label, true).second) {
            continue;
        }
        if (!options.importHidden && !aColorTool->IsVisible(label)) {
            continue;
        }
        auto shape = aShapeTool->GetShape(label);
        if (myRootAssemblyPending && !shape.IsNull() && aShapeTool->IsAssembly(label)) {
            // The root whose components streamed ahead of it: its assembly
            // takes over the reserved root container rather than nesting a
            // second group inside it.
            myRootAssemblyPending = false;
            if (!analyzeRootAssembly(label)) {
                return false;
            }
            continue;
        }
        // A component streamed from below the top level belongs to the
        // container reserved for its owner, not to the root.
        int parent = myRootGroup;
        const AssemblyNode* streamed = nullptr;
        auto it = myStreamedNodes.find(shape);
        if (it != myStreamedNodes.end()) {
            streamed = &mySkeleton[it->second - 1];
            if (myNodeGroups[streamed->parent] >= 0) {
                parent = myNodeGroups[streamed->parent];
            }
        }
        int node = analyzeShape(label, shape, parent, aColorTool->IsVisible(label));
        if (myProgFailed) {
            return false;
        }
        if (node >= 0) {
            if (parent == myRootGroup) {
                ++myRootChildren;
                myLastRootChild = node;
            }
            if (myStreamComponents) {
                myInstanceNodes.emplace(shape, resolveOp(node));
            }
            // Names are read with the root, so a streamed object would carry
            // none until then; the product structure knows one already.
            int emitted = resolveOp(node);
            if (streamed && !streamed->name.empty() && emitted >= 0
                && myProgOps[emitted].label.empty()) {
                myProgOps[emitted].label = streamed->name;
            }
        }
    }
    // Streaming the components of a root means the root container is that
    // root's assembly and stays, so its ops may be handed over right away -
    // including the reserved containers, which show the assembly tree while
    // the parts are still coming.
    if (myRootChildren >= 2 || myStreamComponents) {
        publishOps();
    }
    return true;
}

bool ImportOCAF2::analyzeEnd()
{
    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
        Tools::dumpLabels(pDoc->Main(), aShapeTool, aColorTool);
    }
    if (myProgFailed || myRootGroup < 0 || myRootChildren == 0) {
        return false;
    }
    for (std::size_t node = 1; node < myNodeGroups.size(); ++node) {
        // Every reserved container must have been taken over by the assembly
        // it stands for; one that was not is a container the synchronous path
        // does not have, so hand the import back rather than diverge.
        if (myNodeGroups[node] >= 0 && !myAdoptedNodes.count(int(node))) {
            FC_WARN("reserved container " << node << " never met its assembly");
            return false;
        }
    }
    if (myRootChildren == 1) {
        // loadShapes() creates no root group around a single result; the
        // publication gate guarantees these ops are still unapplied.
        auto& root = myProgOps[myRootGroup];
        root.type = ProgOp::Collapsed;
        root.resolveTo = myLastRootChild;
        int emitted = resolveOp(myLastRootChild);
        if (emitted >= 0) {
            myProgOps[emitted].parent = -1;
        }
    }
    myProgRoot = myRootGroup;
    publishOps();
    return true;
}

bool ImportOCAF2::analyze()
{
    if (!analyzeBegin()) {
        return false;
    }
    if (!analyzeRoots()) {
        return false;
    }
    return analyzeEnd();
}

void ImportOCAF2::publishOps()
{
    std::lock_guard<std::mutex> guard(myProgMutex);
    myProgPublished = myProgOps.size();
}

std::size_t ImportOCAF2::opsPublished() const
{
    std::lock_guard<std::mutex> guard(myProgMutex);
    return myProgPublished;
}

void ImportOCAF2::rollbackOps()
{
    for (auto rit = myProgObjs.rbegin(); rit != myProgObjs.rend(); ++rit) {
        auto obj = *rit;
        if (obj && obj->getNameInDocument()) {
            pDocument->removeObject(obj->getNameInDocument());
        }
    }
    myProgObjs.clear();
}

int ImportOCAF2::analyzeShape(TDF_Label label,
                              const TopoDS_Shape& shape,
                              int parent,
                              bool visible,
                              bool baseOnly)
{
    if (shape.IsNull()) {
        return -1;
    }
    auto baseShape = shape.Located(TopLoc_Location());
    auto it = myShapeNodes.find(baseShape);
    if (it == myShapeNodes.end()) {
        auto baseLabel = aShapeTool->FindShape(baseShape);
        int node;
        if (baseLabel.IsNull() || !aShapeTool->IsAssembly(baseLabel)) {
            node = analyzeObject(baseLabel, baseShape);
        }
        else {
            node = analyzeAssembly(baseLabel, baseShape);
        }
        if (node < 0 || myProgFailed) {
            return -1;
        }
        // Like setObjectName() after the create: name from the base
        // label onto the created object (the replacement child when a
        // group collapsed); an empty name keeps what is already there
        // (the apply-time linked-label fallback covers links).
        std::string baseName = getLabelName(baseLabel);
        if (!baseName.empty()) {
            myProgOps[resolveOp(node)].label = std::move(baseName);
        }
        it = myShapeNodes.emplace(baseShape, node).first;
    }
    int facadeIdx = it->second;
    if (baseOnly) {
        return facadeIdx;
    }

    // A collapsed group keeps its own free flag and color basis while
    // placement/label/parent claims write through to the replacement
    // child, mirroring the two Info records of the synchronous path.
    int nodeIdx = resolveOp(facadeIdx);

    Info useInfo;
    useInfo.faceColor = myProgOps[facadeIdx].faceColor;
    useInfo.edgeColor = myProgOps[facadeIdx].edgeColor;
    useInfo.hasFaceColor = myProgOps[facadeIdx].hasFaceColor;
    useInfo.hasEdgeColor = myProgOps[facadeIdx].hasEdgeColor;
    getColor(shape, useInfo, true);

    auto placement =
        Base::Placement(Part::TopoShape::convert(shape.Location().Transformation()));

    if (opFree(facadeIdx)) {
        bool sealed;
        {
            std::lock_guard<std::mutex> guard(myProgMutex);
            sealed = std::size_t(facadeIdx) < myProgPublished
                || std::size_t(nodeIdx) < myProgPublished;
        }
        if (sealed) {
            // The op (or its collapsed replacement) may already be applied;
            // claim its object through an op of its own instead of mutating
            // the sealed record.
            myClaimedSealed.insert(facadeIdx);
            int claim = newProgOp(ProgOp::Claim);
            auto &cop = myProgOps[claim];
            cop.target = nodeIdx;
            cop.parent = parent;
            cop.visible = visible;
            cop.placement = placement;
            cop.label = getLabelName(label);
            myLabelNodes.emplace(label, std::make_pair(nodeIdx, -1));
            return facadeIdx;
        }
        // First use claims the object directly (the synchronous path's
        // color re-application here re-applies the base color, i.e. is
        // a no-op, so no color fields change on a claim).
        myProgOps[facadeIdx].free = false;
        auto &op = myProgOps[nodeIdx];
        std::string name = getLabelName(label);
        if (!name.empty()) {
            op.label = std::move(name);
        }
        op.placement = placement * op.placement;
        op.parent = parent;
        op.visible = visible;
        myLabelNodes.emplace(label, std::make_pair(nodeIdx, -1));
        return facadeIdx;
    }

    int link = newProgOp(ProgOp::Link);
    auto &lop = myProgOps[link];
    lop.target = nodeIdx;
    lop.parent = parent;
    lop.visible = visible;
    lop.placement = placement;
    lop.label = getLabelName(label);
    if (useInfo.faceColor != myProgOps[facadeIdx].faceColor) {
        lop.linkColor = useInfo.faceColor;
        lop.hasLinkColor = true;
    }
    myLabelNodes.emplace(label, std::make_pair(link, -1));
    return link;
}

int ImportOCAF2::analyzeObject(TDF_Label label, const TopoDS_Shape& shape)
{
    if (shape.IsNull()
        || (!TopExp_Explorer(shape, TopAbs_VERTEX).More()
            && !TopExp_Explorer(shape, TopAbs_FACE).More())) {
        FC_WARN(Tools::labelName(label) << " has empty shape");
        return -1;
    }

    Info info;
    getColor(shape, info);
    bool hasFaceColors = false;
    bool hasEdgeColors = false;

    ColorInfo colors;
    colors.tshape.setShape(shape);

    scanElementColors(label, colors, info, hasFaceColors, hasEdgeColors);

    MaterialGroups matGroups;
    scanMaterialGroups(label, colors.tshape, matGroups);
    if (matGroups.split) {
        // Would split into one feature per material group; hand the
        // whole import back to the synchronous path.
        myProgFailed = true;
        return -1;
    }

    mergeColor(hasFaceColors, info.faceColor, colors.faceColors);
    mergeColor(hasEdgeColors, info.edgeColor, colors.edgeColors);

    scanFaceMaterials(label, colors, info);

    std::string internalName = colors.tshape.shapeName();
    RenderMaterial rmat;
    getRenderMaterial(label, rmat);

    int node = newProgOp(ProgOp::Object);
    auto &op = myProgOps[node];
    op.shape = shape;
    op.internalName = std::move(internalName);
    op.faceColor = info.faceColor;
    op.edgeColor = info.edgeColor;
    op.hasFaceColor = info.hasFaceColor;
    op.hasEdgeColor = info.hasEdgeColor;
    op.faceColors = std::move(colors.faceColors);
    op.edgeColors = std::move(colors.edgeColors);
    op.faceMaterials = std::move(colors.faceMaterials);
    op.pbrMaterials = colors.pbrMaterials;
    op.material = std::move(rmat);
    return node;
}

int ImportOCAF2::analyzeAssembly(TDF_Label label, const TopoDS_Shape& shape, int groupIdx)
{
    (void)label;

    const bool ownGroup = groupIdx < 0;
    if (ownGroup) {
        groupIdx = newProgOp(ProgOp::Group);
    }

    struct AChild
    {
        TopoDS_Shape shape;
        bool vis = true;
        std::vector<Base::Placement> plas;
        boost::dynamic_bitset<> visList;
        std::map<int, App::Color> colors;
        std::vector<TDF_Label> labels;
    };
    std::vector<int> order;
    std::map<int, AChild> childMap;

    int childCount = 0;
    int lastChild = -1;
    bool hasSHUO = false;

    for (TopoDS_Iterator it(shape, Standard_False, Standard_False); it.More(); it.Next()) {
        TopoDS_Shape childShape = it.Value();
        if (childShape.IsNull()) {
            continue;
        }
        TDF_Label childLabel;
        // SearchUsingMap, not Search: for a located shape - which every
        // assembly component is - Search enumerates every shape in the
        // document and walks each assembly's components, so filling a tree
        // of N components costs O(N^2). SearchUsingMap answers the same two
        // questions (top-level instance, then component of an assembly) from
        // myShapeLabels and the label's back references, which XCAF maintains
        // anyway. OCCT's own STEPCAFControl_Reader uses it for this reason.
        aShapeTool->SearchUsingMap(childShape, childLabel, Standard_False, Standard_False);
        auto streamed = myInstanceNodes.find(childShape);
        if (streamed == myInstanceNodes.end() && !mySkeleton.empty()
            && childShape.ShapeType() == TopAbs_COMPOUND) {
            // A sub-assembly whose container was reserved before its parts
            // arrived is analyzed into that container rather than a second
            // one nested in it.
            int snode = skeletonNodeOf(childShape);
            if (snode > 0 && myNodeGroups[snode] >= 0 && myAdoptedNodes.insert(snode).second) {
                if (!adoptSkeleton(childLabel, childShape, myNodeGroups[snode])) {
                    return -1;
                }
                ++childCount;
                lastChild = myNodeGroups[snode];
                continue;
            }
        }
        if (streamed != myInstanceNodes.end()) {
            // This very instance streamed ahead of the assembly and is
            // already a child of the group: it only counts here, except
            // for its name, which the batches it streamed in did not read
            // yet (that pass scans the whole model and runs once, with the
            // root). A claim carries it to the object.
            ++childCount;
            lastChild = streamed->second;
            std::string name = getLabelName(childLabel);
            if (name.empty()) {
                name = getLabelName(aShapeTool->FindShape(childShape.Located(TopLoc_Location())));
            }
            if (!name.empty() && name != myProgOps[streamed->second].label) {
                int claim = newProgOp(ProgOp::Claim);
                auto& cop = myProgOps[claim];
                cop.target = streamed->second;
                cop.parent = -1;
                cop.label = std::move(name);
            }
            continue;
        }
        if (!childLabel.IsNull() && !options.importHidden && !aColorTool->IsVisible(childLabel)) {
            continue;
        }
        bool vis = true;
        if (!childLabel.IsNull() && aShapeTool->IsComponent(childLabel)) {
            vis = aColorTool->IsVisible(childLabel);
        }
        if (!options.reduceObjects) {
            int c = analyzeShape(childLabel, childShape, groupIdx, vis);
            if (myProgFailed) {
                return -1;
            }
            if (c < 0) {
                continue;
            }
            ++childCount;
            lastChild = c;
            continue;
        }

        int base = analyzeShape(childLabel, childShape, groupIdx, vis, true);
        if (myProgFailed) {
            return -1;
        }
        if (base < 0) {
            continue;
        }
        auto &ci = childMap[base];
        if (ci.plas.empty()) {
            order.push_back(base);
            ci.vis = vis;
            ci.shape = childShape;
        }
        ci.visList.push_back(vis);
        ci.labels.push_back(childLabel);
        ci.plas.emplace_back(
            Part::TopoShape::convert(childShape.Location().Transformation()));
        Quantity_ColorRGBA aColor;
        if (aColorTool->GetColor(childShape, XCAFDoc_ColorSurf, aColor)) {
            ci.colors[int(ci.plas.size()) - 1] = Tools::convertColor(aColor);
        }
    }

    if (options.reduceObjects) {
        for (int base : order) {
            auto &ci = childMap[base];
            if (ci.plas.size() == 1) {
                int c = analyzeShape(ci.labels.front(), ci.shape, groupIdx, ci.vis);
                if (myProgFailed) {
                    return -1;
                }
                if (c < 0) {
                    continue;
                }
                ++childCount;
                lastChild = c;
                if (hasSHUOColors(ci.labels.front())) {
                    hasSHUO = true;
                }
                continue;
            }

            int resolved = resolveOp(base);
            int arr = newProgOp(ProgOp::LinkArray);
            auto &aop = myProgOps[arr];
            aop.parent = groupIdx;
            aop.visible = true;
            aop.target = resolved;
            if (myProgOps[base].type == ProgOp::Collapsed) {
                // The collapsed single-component assembly's placement
                // must be honoured by every array element.
                for (auto &pla : ci.plas) {
                    pla *= myProgOps[resolved].placement;
                }
            }
            aop.placements = std::move(ci.plas);
            aop.visList = ci.visList;
            aop.elemColors = std::move(ci.colors);
            aop.label = getLabelName(ci.labels.front());
            ++childCount;
            lastChild = arr;
            int elem = 0;
            for (auto &cl : ci.labels) {
                myLabelNodes.emplace(cl, std::make_pair(arr, elem++));
                if (hasSHUOColors(cl)) {
                    hasSHUO = true;
                }
            }
        }
    }

    if (!childCount) {
        if (!ownGroup) {
            // An adopted container is handed over already and cannot be
            // dropped the way this one would be; hand the import back.
            myProgFailed = true;
            return -1;
        }
        myProgOps[groupIdx].type = ProgOp::Dropped;
        return -1;
    }

    Info ginfo;
    bool hasColor = getColor(shape, ginfo, false, true);
    if (!hasSHUO && !hasColor && options.reduceObjects && childCount == 1 && !ownGroup
        && groupIdx != myRootGroup) {
        // Same for a container the synchronous path would dissolve into its
        // only child: the reserved ones are chosen so that this does not
        // happen, so reaching here means the tree was read differently than
        // it translated.
        myProgFailed = true;
        return -1;
    }
    if (!hasSHUO && !hasColor && options.reduceObjects && childCount == 1 && ownGroup) {
        int emitted = resolveOp(lastChild);
        if (emitted >= 0 && myProgOps[emitted].visible) {
            auto &g = myProgOps[groupIdx];
            g.type = ProgOp::Collapsed;
            g.resolveTo = lastChild;
            g.free = true;
            // The claim of this assembly re-parents the child.
            myProgOps[emitted].parent = -1;
            return groupIdx;
        }
    }

    bool sealed = false;
    if (!ownGroup) {
        std::lock_guard<std::mutex> guard(myProgMutex);
        sealed = std::size_t(groupIdx) < myProgPublished;
    }
    if (sealed) {
        // The adopted container may already be applied, so its color reaches
        // the object through an op of its own rather than a field of the
        // sealed record (analyzeRootAssembly() claims the name the same way).
        if (ginfo.hasFaceColor) {
            int claim = newProgOp(ProgOp::Claim);
            auto& cop = myProgOps[claim];
            cop.target = groupIdx;
            cop.parent = -1;
            cop.groupColor = ginfo.faceColor;
            cop.hasGroupColor = true;
        }
        return groupIdx;
    }

    auto &g = myProgOps[groupIdx];
    g.faceColor = ginfo.faceColor;
    g.edgeColor = ginfo.edgeColor;
    g.hasFaceColor = ginfo.hasFaceColor;
    g.hasEdgeColor = ginfo.hasEdgeColor;
    if (ginfo.hasFaceColor) {
        g.groupColor = ginfo.faceColor;
        g.hasGroupColor = true;
    }
    return groupIdx;
}

bool ImportOCAF2::analyzeRootAssembly(TDF_Label label)
{
    auto shape = aShapeTool->GetShape(label);
    if (shape.IsNull()) {
        return false;
    }
    auto baseShape = shape.Located(TopLoc_Location());
    // The reserved root container stands for this assembly, so the assembly's
    // shape must resolve to it for any later reference.
    myShapeNodes.emplace(baseShape, myRootGroup);
    if (analyzeAssembly(aShapeTool->FindShape(baseShape), baseShape, myRootGroup) < 0
        || myProgFailed) {
        return false;
    }

    // The container is applied by now, so its name (and the placement of a
    // located root) are claimed rather than written into the sealed record.
    claimGroup(myRootGroup,
               Base::Placement(Part::TopoShape::convert(shape.Location().Transformation())),
               getLabelName(label));
    // The root container is the assembly itself: analyzeEnd() must not
    // collapse it into a single child.
    myRootChildren = 2;
    return true;
}

bool ImportOCAF2::adoptSkeleton(TDF_Label label, const TopoDS_Shape& shape, int group)
{
    auto baseShape = shape.Located(TopLoc_Location());
    myShapeNodes.emplace(baseShape, group);
    auto baseLabel = aShapeTool->FindShape(baseShape);
    if (analyzeAssembly(baseLabel, baseShape, group) < 0 || myProgFailed) {
        return false;
    }
    std::string name = getLabelName(label);
    if (name.empty()) {
        name = getLabelName(baseLabel);
    }
    // The container was placed from the product structure, ahead of the
    // transfer; this is where the placement the transfer produced arrives,
    // which is the one that counts.
    claimGroup(group,
               Base::Placement(Part::TopoShape::convert(shape.Location().Transformation())),
               name);
    return true;
}

void ImportOCAF2::claimGroup(int group,
                             const Base::Placement& placement,
                             const std::string& label)
{
    bool sealed;
    {
        std::lock_guard<std::mutex> guard(myProgMutex);
        sealed = std::size_t(group) < myProgPublished;
    }
    auto& op = myProgOps[group];
    if (!sealed) {
        op.placement = placement;
        if (!label.empty()) {
            op.label = label;
        }
        return;
    }
    // A claim composes with what the object carries, so the difference to
    // the placement it was reserved with is what has to be claimed.
    Base::Placement delta = placement * op.placement.inverse();
    if (delta.isIdentity() && (label.empty() || label == op.label)) {
        return;
    }
    int claim = newProgOp(ProgOp::Claim);
    auto& cop = myProgOps[claim];
    cop.target = group;
    cop.parent = -1;
    cop.placement = delta;
    if (!label.empty() && label != op.label) {
        cop.label = label;
    }
}

// Existence-only mirror of getSHUOColors(): does this component label
// carry style-usage overrides that would have produced element color
// entries? Only the reduce decision consumes the answer.
bool ImportOCAF2::hasSHUOColors(TDF_Label label)
{
    TDF_AttributeSequence seq;
    if (label.IsNull() || !aShapeTool->GetAllComponentSHUO(label, seq)) {
        return false;
    }
    for (int i = 1; i <= seq.Length(); ++i) {
        Handle(XCAFDoc_GraphNode) shuo = Handle(XCAFDoc_GraphNode)::DownCast(seq.Value(i));
        if (shuo.IsNull()) {
            continue;
        }
        TDF_Label slabel = shuo->Label();
        TDF_LabelSequence uppers;
        aShapeTool->GetSHUOUpperUsage(slabel, uppers);
        if (uppers.Length()) {
            continue;
        }
        bool resolved = true;
        while (true) {
            TDF_Label l = shuo->Label().Father();
            if (!myLabelNodes.count(l)) {
                resolved = false;
                break;
            }
            if (!shuo->NbChildren()) {
                break;
            }
            shuo = shuo->GetChild(1);
        }
        if (!resolved) {
            continue;
        }
        if (!aColorTool->IsVisible(slabel)) {
            return true;
        }
        Quantity_ColorRGBA aColor;
        if (aColorTool->GetColor(slabel, XCAFDoc_ColorSurf, aColor)
            || aColorTool->GetColor(slabel, XCAFDoc_ColorGen, aColor)) {
            return true;
        }
    }
    return false;
}

App::DocumentObject* ImportOCAF2::progObject(int node) const
{
    node = resolveOp(node);
    if (node < 0 || node >= int(myProgObjs.size())) {
        return nullptr;
    }
    return myProgObjs[node];
}

bool ImportOCAF2::applyNextOp()
{
    // Copy the op out under the lock: the worker may still be appending to
    // myProgOps (sealed ops themselves never change, but the vector storage
    // moves), and applyOp() must run unlocked (it re-enters resolveOp()).
    ProgOp op;
    int idx;
    {
        std::lock_guard<std::mutex> guard(myProgMutex);
        if (myProgObjs.size() < myProgPublished) {
            myProgObjs.resize(myProgPublished, nullptr);
        }
        for (;;) {
            if (myProgApplied >= myProgPublished) {
                return false;
            }
            idx = int(myProgApplied++);
            if (myProgOps[idx].type == ProgOp::Dropped
                || myProgOps[idx].type == ProgOp::Collapsed) {
                continue;
            }
            op = myProgOps[idx];
            break;
        }
    }
    applyOp(op, idx);
    return true;
}

void ImportOCAF2::applyOp(ProgOp& op, int index)
{
    App::DocumentObject *obj = nullptr;
    switch (op.type) {
    case ProgOp::Dropped:
    case ProgOp::Collapsed:
        return;
    case ProgOp::Object: {
        auto feature = static_cast<Part::Feature*>(
            pDocument->addObject("Part::Feature", op.internalName.c_str()));
        feature->Shape.setValue(op.shape);
        applyFaceColors(feature, {op.faceColor});
        applyEdgeColors(feature, {op.edgeColor});
        if (!op.faceMaterials.empty()) {
            applyFaceMaterials(feature, op.faceMaterials, op.pbrMaterials);
        }
        else if (!op.faceColors.empty()) {
            applyFaceColors(feature, op.faceColors);
        }
        if (!op.edgeColors.empty()) {
            applyEdgeColors(feature, op.edgeColors);
        }
        if (op.material.valid) {
            applyRenderMaterial(feature, op.material);
        }
        feature->Placement.setValue(op.placement);
        obj = feature;
        break;
    }
    case ProgOp::Group: {
        if (!options.useLinkGroup) {
            auto part = static_cast<App::Part*>(pDocument->addObject("App::Part", "Part"));
            part->Placement.setValue(op.placement);
            obj = part;
        }
        else {
            auto group = static_cast<App::LinkGroup*>(
                pDocument->addObject("App::LinkGroup", "LinkGroup"));
            group->Placement.setValue(op.placement);
            obj = group;
        }
        if (op.hasGroupColor) {
            applyLinkColor(obj, -1, op.groupColor);
        }
        break;
    }
    case ProgOp::Link: {
        auto target = progObject(op.target);
        if (!target) {
            return;
        }
        auto link = static_cast<App::Link*>(pDocument->addObject("App::Link", "Link"));
        link->setLink(-1, target);
        link->Placement.setValue(op.placement);
        if (op.hasLinkColor) {
            applyLinkColor(link, -1, op.linkColor);
        }
        obj = link;
        break;
    }
    case ProgOp::Claim: {
        // First use of an object applied in an earlier batch: relabel,
        // re-place and reparent it (what the direct claim would have done
        // to the op before it was sealed).
        auto target = progObject(op.target);
        if (!target) {
            return;
        }
        auto pla = Base::freecad_dynamic_cast<App::PropertyPlacement>(
            target->getPropertyByName("Placement"));
        if (pla) {
            pla->setValue(op.placement * pla->getValue());
        }
        if (op.hasGroupColor) {
            applyLinkColor(target, -1, op.groupColor);
        }
        obj = target;
        break;
    }
    case ProgOp::LinkArray: {
        auto target = progObject(op.target);
        if (!target) {
            return;
        }
        auto link = static_cast<App::Link*>(pDocument->addObject("App::Link", "Link"));
        link->setLink(-1, target);
        link->ShowElement.setValue(false);
        link->ElementCount.setValue(int(op.placements.size()));
        link->PlacementList.setValue(op.placements);
        link->VisibilityList.setValue(op.visList);
        for (auto &v : op.elemColors) {
            applyLinkColor(link, v.first, v.second);
        }
        obj = link;
        break;
    }
    }
    if (!obj) {
        return;
    }
    myProgObjs[index] = obj;

    if (!op.label.empty()) {
        obj->Label.setValue(op.label.c_str());
    }
    else {
        auto linked = obj->getLinkedObject(false);
        if (linked && linked != obj) {
            obj->Label.setValue(linked->Label.getValue());
        }
    }

    int parent = resolveOp(op.parent);
    if (parent >= 0 && parent < int(myProgObjs.size()) && myProgObjs[parent]) {
        auto parentObj = myProgObjs[parent];
        if (auto group = Base::freecad_dynamic_cast<App::LinkGroup>(parentObj)) {
            auto elems = group->ElementList.getValues();
            elems.push_back(obj);
            group->ElementList.setValues(elems);
            boost::dynamic_bitset<> vis = group->VisibilityList.getValues();
            vis.push_back(op.visible);
            group->VisibilityList.setValue(vis);
        }
        else if (auto part = Base::freecad_dynamic_cast<App::Part>(parentObj)) {
            obj->Visibility.setValue(op.visible);
            part->addObject(obj);
        }
    }
}

App::DocumentObject* ImportOCAF2::finishOps()
{
    auto ret = progObject(myProgRoot);
    if (ret) {
        ret->recomputeFeature(true);
    }
    return ret;
}

void ImportOCAF2::getSHUOColors(TDF_Label label,
                                std::map<std::string, App::Color>& colors,
                                bool appendFirst)
{
    TDF_AttributeSequence seq;
    if (label.IsNull() || !aShapeTool->GetAllComponentSHUO(label, seq)) {
        return;
    }
    std::ostringstream ss;
    for (int i = 1; i <= seq.Length(); ++i) {
        Handle(XCAFDoc_GraphNode) shuo = Handle(XCAFDoc_GraphNode)::DownCast(seq.Value(i));
        if (shuo.IsNull()) {
            continue;
        }

        TDF_Label slabel = shuo->Label();

        // We only want to process the main shuo, i.e. those without upper_usage
        TDF_LabelSequence uppers;
        aShapeTool->GetSHUOUpperUsage(slabel, uppers);
        if (uppers.Length()) {
            continue;
        }

        // appendFirst tells us whether we shall append the object name of the first label
        bool skipFirst = !appendFirst;
        ss.str("");
        while (true) {
            if (skipFirst) {
                skipFirst = false;
            }
            else {
                TDF_Label l = shuo->Label().Father();
                auto it = myNames.find(l);
                if (it == myNames.end()) {
                    FC_WARN("Failed to find object of label " << Tools::labelName(l));
                    ss.str("");
                    break;
                }
                if (!it->second.empty()) {
                    ss << it->second << '.';
                }
            }
            if (!shuo->NbChildren()) {
                break;
            }
            shuo = shuo->GetChild(1);
        }
        std::string subname = ss.str();
        if (subname.empty()) {
            continue;
        }
        if (!aColorTool->IsVisible(slabel)) {
            subname += App::DocumentObject::hiddenMarker();
            colors.emplace(subname, App::Color());
        }
        else {
            Quantity_ColorRGBA aColor;
            if (aColorTool->GetColor(slabel, XCAFDoc_ColorSurf, aColor)
                || aColorTool->GetColor(slabel, XCAFDoc_ColorGen, aColor)) {
                colors.emplace(subname, Tools::convertColor(aColor));
            }
        }
    }
}

App::DocumentObject* ImportOCAF2::loadShape(App::Document* doc,
                                            TDF_Label label,
                                            const TopoDS_Shape& shape,
                                            bool baseOnly,
                                            bool newDoc)
{
    if (shape.IsNull()) {
        return nullptr;
    }

    auto baseShape = shape.Located(TopLoc_Location());
    auto it = myShapes.find(baseShape);
    if (it == myShapes.end()) {
        Info info;
        auto baseLabel = aShapeTool->FindShape(baseShape);
        if (sequencer && !baseLabel.IsNull() && aShapeTool->IsTopLevel(baseLabel)) {
            sequencer->next(true);
        }
        bool res;
        if (baseLabel.IsNull() || !aShapeTool->IsAssembly(baseLabel)) {
            res = createObject(doc, baseLabel, baseShape, info, newDoc);
        }
        else {
            res = createAssembly(doc, baseLabel, baseShape, info, newDoc);
        }
        if (!res) {
            return nullptr;
        }
        setObjectName(info, baseLabel);
        it = myShapes.emplace(baseShape, info).first;
    }
    if (baseOnly) {
        return it->second.obj;
    }

    if (doc != it->second.obj->getDocument()) {
        // Check if we need to move the object to this document to avoid
        // dependency loop
        auto &info = it->second;
        auto objDoc = info.obj->getDocument();
        for (auto &v : myDocumentStack) {
            if (v.doc != objDoc)
                continue;
            // v.children is the pending children objects in upper hierarchy
            // that is yet to be grouped under a new group object in that
            // hierarchy. It is possible that the lower hierarchy here refers to
            // one of the children in upper hierarchy. To avoid dependency loop,
            // we need to move that child object to this hierarchy, and convert
            // the higher hierarchy child into an App::Link.
            auto res = doc->copyObject({info.obj}, true);
            if (!res.empty()) {
                auto copyObj = res[0];
                auto it = std::find(v.children.begin(), v.children.end(), info.obj);
                if (it != v.children.end()) {
                    auto link = static_cast<App::Link*>(info.obj->getDocument()->addObject("App::Link","Link"));
                    link->setLink(-1,copyObj);
                    if (info.propPlacement)
                        link->Placement.setValue(info.propPlacement->getValue());
                    *it = link;
                }
                std::set<App::DocumentObject*> objSet;
                std::vector<App::Property*> props;
                std::map<App::DocumentObject*, std::vector<App::DocumentObjectT>> newLinks;
                for (auto inObj : info.obj->getInList()) {
                    if (inObj->getDocument()==doc || !objSet.insert(inObj).second)
                        continue;
                    props.clear();
                    inObj->getPropertyList(props);
                    std::vector<App::DocumentObjectT> propsFound;
                    for (auto prop : props) {
                        if (prop->getContainer() != inObj)
                            continue;
                        if (auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop)) {
                            for (auto link : propLink->linkedObjects()) {
                                if (link == info.obj)
                                    propsFound.emplace_back(prop);
                            }
                        }
                    }
                    if (propsFound.size()) {
                        auto link = static_cast<App::Link*>(doc->addObject("App::Link","Link"));
                        link->setLink(-1,copyObj);
                        if (info.propPlacement)
                            link->Placement.setValue(info.propPlacement->getValue());
                        newLinks[link] = std::move(propsFound);
                    }
                }
                for (const auto &v : newLinks) {
                    for (const auto &propT : v.second) {
                        if (auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkBase>(propT.getProperty())) {
                            std::unique_ptr<App::Property> copy(
                                    propLink->CopyOnLinkReplace(propT.getObject(), info.obj, v.first));
                            if (copy)
                                propLink->Paste(*copy);
                        }
                    }
                }
                info.obj->getDocument()->removeObject(info.obj->getNameInDocument());
                info.obj = copyObj;
                info.propPlacement = Base::freecad_dynamic_cast<App::PropertyPlacement>(
                        copyObj->getPropertyByName("Placement"));
            }
        }
    }

    auto info = it->second;
    getColor(shape, info, true);

    if(info.free && doc==info.obj->getDocument()) {
        it->second.free = false;
        if(info.faceColor!=it->second.faceColor) {
            info.faceColor = it->second.faceColor;
            if (auto feature = Base::freecad_dynamic_cast<Part::Feature>(info.obj))
                applyFaceColors(feature,{info.faceColor});
        }
        if(info.edgeColor!=it->second.edgeColor) {
            info.edgeColor = it->second.edgeColor;
            if (auto feature = Base::freecad_dynamic_cast<Part::Feature>(info.obj))
                applyEdgeColors(feature,{info.edgeColor});
        }
        setObjectName(info, label, true);
        setPlacement(info.propPlacement,shape);
        myNames.emplace(label,info.obj->getNameInDocument());
        return info.obj;
    }

    auto link = static_cast<App::Link*>(doc->addObject("App::Link", "Link"));
    link->setLink(-1, info.obj);
    setPlacement(&link->Placement, shape);
    info.obj = link;
    setObjectName(info, label);
    if (info.faceColor != it->second.faceColor) {
        applyLinkColor(link, -1, info.faceColor);
    }

    myNames.emplace(label, link->getNameInDocument());
    return link;
}

struct ChildInfo
{
    std::vector<Base::Placement> plas;
    boost::dynamic_bitset<> vis;
    std::map<size_t, App::Color> colors;
    std::vector<TDF_Label> labels;
    TopoDS_Shape shape;
};

bool ImportOCAF2::createAssembly(App::Document* _doc,
                                 TDF_Label label,
                                 const TopoDS_Shape& shape,
                                 Info& info,
                                 bool newDoc)
{
    (void)label;

    std::vector<App::DocumentObject*> children;
    std::map<App::DocumentObject*, ChildInfo> childrenMap;
    boost::dynamic_bitset<> visibilities;
    std::map<std::string, App::Color> shuoColors;

    auto doc = _doc;
    if (newDoc) {
        doc = getDocument(_doc, label);
    }

    bool pushed = false;
    if (myDocumentStack.empty() || myDocumentStack.back().doc != doc) {
        pushed = true;
        myDocumentStack.emplace_back(doc, children);
    }

    for (TopoDS_Iterator it(shape, Standard_False, Standard_False); it.More(); it.Next()) {
        TopoDS_Shape childShape = it.Value();
        if (childShape.IsNull()) {
            continue;
        }
        TDF_Label childLabel;
        // Same O(N^2) as the streamed path above; the synchronous importer
        // walks assemblies the same way.
        aShapeTool->SearchUsingMap(childShape, childLabel, Standard_False, Standard_False);
        if (!childLabel.IsNull() && !options.importHidden && !aColorTool->IsVisible(childLabel)) {
            continue;
        }
        auto obj = loadShape(doc, childLabel, childShape, options.reduceObjects);
        if (!obj) {
            continue;
        }
        bool vis = true;
        if (!childLabel.IsNull() && aShapeTool->IsComponent(childLabel)) {
            vis = aColorTool->IsVisible(childLabel);
        }
        if (!options.reduceObjects) {
            visibilities.push_back(vis);
            children.push_back(obj);
            getSHUOColors(childLabel, shuoColors, true);
            continue;
        }

        auto& childInfo = childrenMap[obj];
        if (childInfo.plas.empty()) {
            children.push_back(obj);
            visibilities.push_back(vis);
            childInfo.shape = childShape;
        }

        childInfo.vis.push_back(vis);
        childInfo.labels.push_back(childLabel);
        childInfo.plas.emplace_back(
            Part::TopoShape::convert(childShape.Location().Transformation()));
        Quantity_ColorRGBA aColor;
        if (aColorTool->GetColor(childShape, XCAFDoc_ColorSurf, aColor)) {
            childInfo.colors[childInfo.plas.size() - 1] = Tools::convertColor(aColor);
        }
    }
    assert(visibilities.size() == children.size());

    if (children.empty()) {
        if (doc != _doc) {
            for (;;) {
                auto it = std::find(myNewDocuments.begin(), myNewDocuments.end(), doc);
                if (it == myNewDocuments.end())
                    break;
                myNewDocuments.erase(it);
            }
            App::GetApplication().closeDocument(doc->getName());
        } else if (pushed)
            myNewDocuments.pop_back();
        return false;
    }

    if (options.reduceObjects) {
        int i = -1;
        for (auto& child : children) {
            ++i;
            auto& childInfo = childrenMap[child];
            if (childInfo.plas.size() == 1) {
                child = loadShape(doc, childInfo.labels.front(), childInfo.shape);
                getSHUOColors(childInfo.labels.front(), shuoColors, true);
                continue;
            }

            visibilities[i] = true;

            // Okay, we are creating a link array
            auto link = static_cast<App::Link*>(doc->addObject("App::Link", "Link"));
            link->setLink(-1, child);
            link->ShowElement.setValue(false);
            link->ElementCount.setValue(childInfo.plas.size());
            auto it = myCollapsedObjects.find(child);
            if (it != myCollapsedObjects.end()) {
                // child is a single component assembly that has been
                // collapsed, so we have to honour its placement
                for (auto& pla : childInfo.plas) {
                    pla *= it->second->getValue();
                }
            }
            link->PlacementList.setValue(childInfo.plas);
            link->VisibilityList.setValue(childInfo.vis);

            for (auto& v : childInfo.colors) {
                applyLinkColor(link, v.first, v.second);
            }

            int i = 0;
            std::string name = link->getNameInDocument();
            name += '.';
            for (auto childLabel : childInfo.labels) {
                myNames.emplace(childLabel, name + std::to_string(i++));
                getSHUOColors(childLabel, shuoColors, true);
            }

            child = link;
            Info objInfo;
            objInfo.obj = child;
            setObjectName(objInfo, childInfo.labels.front());
        }
    }

    bool res = createGroup(doc,info,shape,children,visibilities,shuoColors.empty());
    if (res && !shuoColors.empty())
        applyElementColors(info.obj,shuoColors);

    if (pushed)
        myDocumentStack.pop_back();
    return res;
}

// ----------------------------------------------------------------------------

ImportOCAFExt::ImportOCAFExt(Handle(TDocStd_Document) hStdDoc,
                             App::Document* doc,
                             const std::string& name)
    : ImportOCAF2(hStdDoc, doc, name)
{}

void ImportOCAFExt::applyFaceColors(Part::Feature* part, const std::vector<App::Color>& colors)
{
    partColors[part] = colors;
}

