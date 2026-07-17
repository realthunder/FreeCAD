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
# include <TopoDS_Iterator.hxx>
# include <XCAFDoc_ColorTool.hxx>
# include <XCAFDoc_DocumentTool.hxx>
# include <XCAFDoc_GraphNode.hxx>
# include <XCAFDoc_ShapeTool.hxx>
# include <XCAFDoc_VisMaterial.hxx>
# include <XCAFDoc_VisMaterialTool.hxx>
# include <Image_Texture.hxx>
#endif

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
    defaultFaceColor.setPackedValue(0xCCCCCC00);
    defaultFaceColor.a = 0;

    defaultEdgeColor.setPackedValue(421075455UL);
    defaultEdgeColor.a = 0;
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
    defaultOptions.merge = settings.getReadShapeCompoundMode();
    defaultOptions.useLegacyImporter = settings.getUseLegacyImporter();
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
    defaultOptions.defaultFaceColor.a = 0;

    defaultOptions.defaultEdgeColor.setPackedValue(
        hGrp->GetUnsigned("DefaultShapeLineColor",
                          defaultOptions.defaultEdgeColor.getPackedValue()));
    defaultOptions.defaultEdgeColor.a = 0;

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

    const XCAFDoc_VisMaterialPBR& pbr = visMat->PbrMaterial();
    // Both factors at the glTF defaults (1.0) with no textures is what a
    // color-only material reads back as - glTF has no way to say "no PBR".
    // Skip it so plain colored exports do not grow render properties (and
    // turn fully metallic) on re-import.
    if (pbr.BaseColorTexture.IsNull() && pbr.NormalTexture.IsNull()
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

    mat.valid = true;
    return true;
}

bool ImportOCAF2::createObject(App::Document* doc,
                               TDF_Label label,
                               const TopoDS_Shape& shape,
                               Info& info,
                               bool newDoc)
{
    if (shape.IsNull() || !TopExp_Explorer(shape, TopAbs_VERTEX).More()) {
        FC_WARN(Tools::labelName(label) << " has empty shape");
        return false;
    }

    getColor(shape, info);
    bool hasFaceColors = false;
    bool hasEdgeColors = false;

    ColorInfo colors;
    colors.tshape.setShape(shape);
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

    feature = static_cast<Part::Feature*>(doc->addObject("Part::Feature",tshape.shapeName().c_str()));
    feature->Shape.setValue(shape);
    // feature->Visibility.setValue(false);

    applyFaceColors(feature,{info.faceColor});
    applyEdgeColors(feature,{info.edgeColor});
    if(colors.faceColors.size())
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
        aShapeTool->Search(childShape, childLabel, Standard_True, Standard_True, Standard_False);
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

