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

#ifndef _PreComp_
# include <Bnd_Box.hxx>
# include <BRep_Tool.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepMesh_IncrementalMesh.hxx>
# include <gp_Trsf.hxx>
# include <Precision.hxx>
# include <Poly_Array1OfTriangle.hxx>
# include <Poly_Polygon3D.hxx>
# include <Poly_PolygonOnTriangulation.hxx>
# include <Poly_Triangulation.hxx>
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
# include <TopTools_IndexedMapOfShape.hxx>

# include <QApplication>
# include <QAction>
# include <QMenu>
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
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Action.h>
#include <Gui/Selection.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/Utilities.h>
#include <Gui/ViewProviderLink.h>
#include <Gui/TaskElementColors.h>
#include <Gui/Inventor/SoFCShapeInfo.h>
#include <Gui/InventorBase.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Control.h>
#include <Gui/SoFCSelectionAction.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/ViewParams.h>
#include <Gui/RenderParams.h>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Gui/Renderer/Renderer.h>
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

// Private class used by ViewProviderExt to update its visual nodes up on
// receiving SoGetBoundingBoxAction
class SoFCCoordinate3: public SoCoordinate3
{
public:
    virtual void getBoundingBox(SoGetBoundingBoxAction * action) {
        if (vp && vp->VisualTouched)
            vp->updateVisual();
        SoCoordinate3::getBoundingBox(action);
    }

    ViewProviderPartExt *vp = nullptr;
};

void initShapeInstancingGateObserver();  // PartParams.cpp

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
/// per-element colors diverge in value bake them per part — one variant
/// per DISTINCT resolved color vector (the key), lazily created,
/// refcounted, shared by every instance (across objects) applying that
/// exact vector. The variant shape node references the geometry's
/// coordinate/normal/texcoord nodes; only the per-part material is its
/// own. Variants never mutate — a different vector materializes a new
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
/// separator (across objects — the table is global). Entries never mutate
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
/// exact vector (referenced) or null — the caller then builds one.
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
/// per-face colors \a slice (diffuse rgb + transparency in alpha), and
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
        t[i] = slice[i].a;
    }
    mat->diffuseColor.finishEditing();
    mat->transparency.finishEditing();
    // Everything but diffuse/transparency inherits the object material.
    mat->ambientColor.setIgnored(TRUE);
    mat->specularColor.setIgnored(TRUE);
    mat->emissiveColor.setIgnored(TRUE);
    mat->shininess.setIgnored(TRUE);

    auto faceset = new SoBrepFaceSet;
    // Same forced UV capture as the base faceset — the variant must
    // produce identical geometry arrays (incl. texcoords) so the
    // backend's content-keyed geometry buffers are shared.
    faceset->forceTexCoords = TRUE;
    // Lets the render cache manager seed this node's vertex cache with
    // the base's — the geometry arrays stay CPU-shared, only the baked
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
/// overrides: everything else (incl. transparency — lines/points never
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
    // (one polyline per edge) — same as the flattened per-edge path.
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
        /// material at the wrapper (the Link mechanism) — never baked
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
        // Variant references go first — the geometry entries they nest in
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
/// a backend renderer selected (plain Coin/GL always flattens — without
/// GPU instancing many small shared nodes are a net loss), and the
/// backend's published instancing capability.
static bool shapeInstancingActive()
{
    if (!PartParams::getShapeInstancing())
        return false;
    if (Gui::ViewParams::getRenderCache() != 3)
        return false;
    const std::string &type = Gui::RenderParams::getType();
    if (type.empty() || type == "Default")
        return false;
    return Render::Renderer::instancingHint();
}

} // namespace PartGui

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

    App::Material lmat;
    lmat.ambientColor.set(0.2f,0.2f,0.2f);
    lmat.diffuseColor.set(lr,lg,lb);
    lmat.specularColor.set(0.0f,0.0f,0.0f);
    lmat.emissiveColor.set(0.0f,0.0f,0.0f);
    lmat.shininess = 1.0f;
    lmat.transparency = 0.0f;

    App::Material vmat;
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
                || prop == &ShapeMaterial)
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
        const App::Material& Mat = LineMaterial.getValue();
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
        const App::Material& Mat = PointMaterial.getValue();
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
        setHighlightedFaces(DiffuseColor.getValues());
        Gui::ColorUpdater::addObject(getObject());
    }
    else if(prop == &ShapeColor) {
        if(!ShapeColor.testStatus(App::Property::User3)) {
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard(
                    App::Property::User3, &ShapeColor);
            ViewProviderGeometryObject::onChanged(prop);
            App::Color c = ShapeColor.getValue();
            c.a = Transparency.getValue()/100.0f;
            DiffuseColor.setValue(c);
            updateColors();
        }
        return;
    }
    else if (prop == &Transparency) {
        const App::Material& Mat = ShapeMaterial.getValue();
        long value = (long)(100*Mat.transparency);
        if (value != Transparency.getValue()) {
            float trans = Transparency.getValue()/100.0f;

            App::PropertyContainer* parent = ShapeMaterial.getContainer();
            ShapeMaterial.setContainer(nullptr);
            ShapeMaterial.setTransparency(trans);
            ShapeMaterial.setContainer(parent);

            if(!prop->testStatus(App::Property::User3)) {
                if(MapTransparency.getValue() || MappedColors.getSize()) {
                    updateColors();
                } else{
                    auto colors = DiffuseColor.getValues();
                    for (auto &c : colors)
                        c.a = trans;
                    DiffuseColor.setValues(colors);
                }
            }
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
    auto* pcFlatRoot = new SoSeparator();
    auto* pcWireframeRoot = new SoSeparator();
    auto* pcPointsRoot = new SoSeparator();
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
    // part index of an unknown instance — unresolvable without the pick
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
        // separators — they never enter the context stack, so any of
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
    // getDetailPath) — null causes whole-object treatment.
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
static bool materialsUnrepresentable(const std::vector<App::Material> &mats)
{
    for (size_t i = 1; i < mats.size(); ++i) {
        if (mats[i].ambientColor != mats[0].ambientColor
                || mats[i].specularColor != mats[0].specularColor
                || mats[i].emissiveColor != mats[0].emissiveColor)
            return true;
    }
    return false;
}

void ViewProviderPartExt::setHighlightedFaces(const std::vector<App::Color>& colors)
{
    if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange))
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
            t[i] = colors[i].a;
        }
        const auto &color = ShapeColor.getValue();
        float trans = ShapeMaterial.getValue().transparency;
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
    pcShapeMaterial->transparency.setValue(ShapeMaterial.getValue().transparency);

}

void ViewProviderPartExt::setHighlightedFaces(const std::vector<App::Material>& colors)
{
    // Instanced representation: diffuse+transparency divergence goes
    // through the color-variant path; anything beyond that must bake
    // whole materials per face — rebuild flattened (the raised flag
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
        const auto &m0 = colors.empty() ? ShapeMaterial.getValue() : colors[0];
        pcShapeMaterial->ambientColor.setValue(
            m0.ambientColor.r, m0.ambientColor.g, m0.ambientColor.b);
        pcShapeMaterial->specularColor.setValue(
            m0.specularColor.r, m0.specularColor.g, m0.specularColor.b);
        pcShapeMaterial->emissiveColor.setValue(
            m0.emissiveColor.r, m0.emissiveColor.g, m0.emissiveColor.b);
        std::vector<App::Color> diffuse;
        diffuse.reserve(colors.size());
        for (const auto &m : colors) {
            App::Color c = m.diffuseColor;
            c.a = m.transparency;
            diffuse.push_back(c);
        }
        applyInstancedFaceColors(diffuse);
        return;
    }

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

        SbColor* dc = pcShapeMaterial->diffuseColor.startEditing();
        SbColor* ac = pcShapeMaterial->ambientColor.startEditing();
        SbColor* sc = pcShapeMaterial->specularColor.startEditing();
        SbColor* ec = pcShapeMaterial->emissiveColor.startEditing();

        int i=0;
        for (; i < size; i++) {
            dc[i].setValue(colors[i].diffuseColor.r, colors[i].diffuseColor.g, colors[i].diffuseColor.b);
            ac[i].setValue(colors[i].ambientColor.r, colors[i].ambientColor.g, colors[i].ambientColor.b);
            sc[i].setValue(colors[i].specularColor.r, colors[i].specularColor.g, colors[i].specularColor.b);
            ec[i].setValue(colors[i].emissiveColor.r, colors[i].emissiveColor.g, colors[i].emissiveColor.b);
        }

        const auto &material = ShapeMaterial.getValue();
        for (; i < numfaces; ++i) {
            dc[i].setValue(material.diffuseColor.r, material.diffuseColor.g, material.diffuseColor.b);
            ac[i].setValue(material.ambientColor.r, material.ambientColor.g, material.ambientColor.b);
            sc[i].setValue(material.specularColor.r, material.specularColor.g, material.specularColor.b);
            ec[i].setValue(material.emissiveColor.r, material.emissiveColor.g, material.emissiveColor.b);
        }

        pcShapeMaterial->diffuseColor.finishEditing();
        pcShapeMaterial->ambientColor.finishEditing();
        pcShapeMaterial->specularColor.finishEditing();
        pcShapeMaterial->emissiveColor.finishEditing();
        return;
    }

    const auto &material = colors.size()==1?colors[0]:ShapeMaterial.getValue();
    pcFaceBind->value = SoMaterialBinding::OVERALL;
    pcShapeMaterial->diffuseColor.setValue(material.diffuseColor.r, material.diffuseColor.g, material.diffuseColor.b);
    pcShapeMaterial->ambientColor.setValue(material.ambientColor.r, material.ambientColor.g, material.ambientColor.b);
    pcShapeMaterial->specularColor.setValue(material.specularColor.r, material.specularColor.g, material.specularColor.b);
    pcShapeMaterial->emissiveColor.setValue(material.emissiveColor.r, material.emissiveColor.g, material.emissiveColor.b);
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
        color.a = Transparency.getValue()/100.0f;
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
            color.a = Transparency.getValue()/100.0f;
            bool singleColor = true;
            for(int i=0;i<size;++i) {
                if(DiffuseColor[i]!=color)
                    ret[std::string(element,4)+std::to_string(i+1)] = DiffuseColor[i];
                singleColor = singleColor && DiffuseColor[0]==DiffuseColor[i];
            }
            if(size && singleColor) {
                color = DiffuseColor[0];
                color.a = Transparency.getValue()/100.0f;
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
                ret[element].a = Transparency.getValue()/100.0f;
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
            if(v.second.a*100 != Transparency.getValue()) {
                Transparency.setValue(v.second.a*100);
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
    setHighlightedFaces(DiffuseColor.getValues());
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
            color = vp->ShapeMaterial.getValue().diffuseColor;
            color.a = vp->ShapeMaterial.getValue().transparency;
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
                    color = vp->MaterialList[index].diffuseColor;
                    color.a = vp->MaterialList[index].transparency;
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
        auto prop = &vp->DiffuseColor;
        if(type == TopAbs_VERTEX) 
            prop = &vp->PointColorArray;
        else if(type == TopAbs_EDGE)
            prop = &vp->LineColorArray;
        if(prop->getSize()==0)
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
                if(prop->getSize()==1) {
                    color = prop->getValues()[0];
                    color.a = trans;
                }
                else if(idx.second<=prop->getSize()) 
                    return prop->getValues()[idx.second-1];
            }else{
                // This means the element is generated from a different type of source element,
                // e.g. face generated by an edge.
                auto aidx = shape.findAncestor(shape.findShape(idx.first,idx.second),(TopAbs_ShapeEnum)type);
                if(aidx>0) {
                    if(prop->getSize()==1) {
                        color = prop->getValues()[0];
                        color.a = trans;
                    }
                    else if(aidx<=prop->getSize())
                        return prop->getValues()[aidx-1];
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
    defColor.a = 0;

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
        defColor.a = vp->Transparency.getValue()/100.0f;
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
    App::PropertyColorList *prop = 0;
    App::Color defaultColor;
    std::map<int,App::Color> colors;
    bool mapColor;

    void init(TopAbs_ShapeEnum t, ViewProviderPartExt *vp) {
        type = t;
        switch(type) {
        case TopAbs_VERTEX:
            defaultColor = vp->PointColor.getValue();
            prop = &vp->PointColorArray;
            mapColor = vp->MapPointColor.getValue();
            break;
        case TopAbs_EDGE:
            defaultColor = vp->LineColor.getValue();
            prop = &vp->LineColorArray;
            mapColor = vp->MapLineColor.getValue();
            break;
        case TopAbs_FACE:
            defaultColor = vp->ShapeColor.getValue();
            defaultColor.a = vp->Transparency.getValue()/100.0f;
            prop = &vp->DiffuseColor;
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
                info.prop->touch();
            else {
                auto colors = info.prop->getValues();
                if(colors.size()!=shape.countSubShapes(info.type)) {
                    colors.clear();
                    colors.resize(shape.countSubShapes(info.type),info.defaultColor);
                }
                for(auto &v : info.colors) {
                    if(v.first>=(int)colors.size())
                        break;
                    colors[v.first] = v.second;
                }
                info.prop->setValue(colors);
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
                color.a = trans;
            if(color != colors[i]) {
                touched = true;
                colors[i] = color;
            }
        }
        if(!touched) {
            colors.clear();
            colors.push_back(info.defaultColor);
        }
        info.prop->setValue(colors);
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

void ViewProviderPartExt::setEditViewer(Gui::View3DInventorViewer *viewer, int ModNum) {
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
    // the whole shape) — the same part in differently sized parents must
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
            // The shared subgraphs are SoFCSelectionRoot — the render
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
            // registerInstancedLevelEntry (§13): climb on plan demand,
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
        }

        ShapeInstanceRep::Instance inst;
        inst.geom = &geom;
        SbMatrix mat = convert(Part::TopoShape(leaf).getTransform());
        // The wrappers are SoFCSelectionRoot: selection/highlight
        // contexts key on the traversed selection-root stack, so a
        // per-instance root gives each instance its own sub-element
        // context over the shared shape nodes (the App::Link
        // mechanism) — see getDetailPath.
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
        setOverall(c, ShapeMaterial.getValue().transparency);
        return;
    }

    // Resolve the full per-face vector: a short apply keeps the base
    // color on the remaining faces (flat-path semantics — transparency
    // rides the alpha channel when applied as an array).
    int total = 0;
    for (const auto &inst : instanced->instances)
        total += inst.geom->faceCount;
    App::Color base = ShapeColor.getValue();
    base.a = ShapeMaterial.getValue().transparency;
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
    // slice — a uniform slice rides a per-instance override material on
    // the shared base subgraph (cross-instance sharable whatever its
    // value, the Link mechanism), a divergent slice a baked, refcounted
    // color variant shared by every instance applying that exact vector.
    setOverall(ShapeColor.getValue(), ShapeMaterial.getValue().transparency);
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
            inst.overrideMat->transparency.setValue(c.a);
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
    // slice like applyInstancedFaceColors — uniform slices ride a
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
        // registration (§13): the callback rebuilds the SHARED nodes
        // in place — every instance refines at once, the per-proto
        // ladder — and it captures the nodes, not any view provider:
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
                                 nt, nn, np, nno, nf, ne, nl);
                registerInstancedLevelEntry(local, true, builtError,
                                            coarseDefl, coarseAng,
                                            exactDefl, exactAng,
                                            normalsFromUV, coords, pcoords,
                                            norm, texcoords, faceset,
                                            lineset, nodeset);
            };
        }
        // No document: the entry is shared by every instance of the
        // leaf, across objects and potentially across documents, and
        // deliberately captures no view provider -- so the gate reads
        // process-wide here (null doc), not one sharer's document.
        registerMeshLevelSource(local, normalsFromUV, faceset, lineset,
                                builtError, exactDefl, exactAng,
                                std::move(onExact));
        return;
    }
    // Exact-resident: registered at error 0 with both ways back down
    // armed (§13 step 3) — the coarse triangulation never left the
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
                         nt, nn, np, nno, nf, ne, nl);
        registerInstancedLevelEntry(local, false, builtError,
                                    coarseDefl, coarseAng,
                                    exactDefl, exactAng, normalsFromUV,
                                    coords, pcoords, norm, texcoords,
                                    faceset, lineset, nodeset);
    };
    auto onDowngrade = [=]() {
        if (!downgradeMeshLevels(local))
            return;
        int nt = 0, nn = 0, np = 0, nno = 0, nf = 0, ne = 0, nl = 0;
        buildVisualNodes(local, coarseDefl, coarseAng, normalsFromUV,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         nt, nn, np, nno, nf, ne, nl);
        registerInstancedLevelEntry(local, false, builtError,
                                    coarseDefl, coarseAng,
                                    exactDefl, exactAng, normalsFromUV,
                                    coords, pcoords, norm, texcoords,
                                    faceset, lineset, nodeset);
    };
    registerMeshLevelSource(local, normalsFromUV, faceset, lineset,
                            0.0f, exactDefl, exactAng, {},
                            std::move(onDemote), builtError,
                            std::move(onDowngrade));
}

// Progressive import of an oversized part (docs/SceneStreaming.md
// §13): a shape over the CoarseDeferFaces threshold shows a
// 12-triangle bounding-box stand-in immediately, and even its coarse
// tessellation runs on the refine worker pool. The registration
// declares the stand-in's error (0.5 of the diagonal) and names the
// coarse rung parameters as its climb target, so the ordinary level
// plan fires the build; the meshed copy arrives on the GUI thread,
// its triangulation transfers onto the live shape, and the rerun of
// updateVisual() finds every face resident — an instant rebuild. The
// exact rung follows the normal ladder from there. Returns whether
// the stand-in was built (the caller is done then).
bool ViewProviderPartExt::buildCoarseStandIn()
{
    const long deferFaces = Gui::RenderParams::getCoarseDeferFaces();
    if (deferFaces < 0 || cachedShape.isNull()) {
        return false;
    }
    TopoDS_Shape cShape = cachedShape.getShape();
    if (cShape.IsNull() || CoarseMeshTShape == cShape.TShape().get()
        || ExactMeshTShape == cShape.TShape().get()) {
        return false;
    }
    auto doc = pcObject ? pcObject->getDocument() : nullptr;
    if (!doc || !doc->testStatus(App::Document::LiveImport)) {
        return false;
    }
    const int coarseLvl = coarseTessellationLevel(doc);
    if (coarseLvl < 0) {
        return false;
    }
    if (long(cachedShape.countSubShapes(TopAbs_FACE)) <= deferFaces) {
        return false;
    }
    try {
        Bnd_Box bounds;
        BRepBndLib::Add(cShape, bounds);
        bounds.SetGap(0.0);
        if (bounds.IsVoid()) {
            return false;
        }
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        double dx = xMax - xMin, dy = yMax - yMin, dz = zMax - zMin;
        double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(diag > 0)) {
            return false;
        }
        double deflection = meshLevelDeflection(diag, unsigned(coarseLvl));
        double angDefl = meshLevelAngle(unsigned(coarseLvl));
        TopoDS_Shape standIn =
            BRepPrimAPI_MakeBox(gp_Pnt(xMin, yMin, zMin),
                                std::max(dx, double(Precision::Confusion())),
                                std::max(dy, double(Precision::Confusion())),
                                std::max(dz, double(Precision::Confusion())))
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
        auto onCoarse = [this, tsh](const TopoDS_Shape &meshed) {
            TopoDS_Shape cur = cachedShape.getShape();
            if (cur.IsNull() || cur.TShape().get() != tsh) {
                return;
            }
            transferMeshLevels(meshed, cur);
            CoarseMeshTShape = tsh;
            FC_LOG(getFullName() << " stand-in resolved: coarse mesh in");
            updateVisual();
        };
        registerMeshLevelSource(cShape, NormalsFromUV, faceset, lineset,
                                /*builtError*/ 0.5f, deflection, angDefl,
                                std::move(onCoarse), {}, 0.0f, {},
                                pcObject ? pcObject->getDocument() : nullptr);
    }
    catch (const Standard_Failure &e) {
        FC_ERR("Failed to build the stand-in for the shape of "
               << pcObject->getFullName() << ": " << e.GetMessageString());
        return false;
    }
    return true;
}

void ViewProviderPartExt::updateVisual()
{
    if (!getObject()
            || !getObject()->getDocument()
            || isRestoring())
    {
        VisualTouched = true;
        return;
    }

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

    // Progressive import of an oversized part (§13): even the coarse
    // build of a many-face shape (or many-leaf compound) stalls the
    // GUI for seconds, and the import stall scales with the largest
    // single part. Build a 12-triangle bounding-box stand-in instead
    // and let the level plan run the coarse build on the refine pool.
    if (buildCoarseStandIn()) {
        VisualTouched = false;
        setHighlightedFaces(DiffuseColor.getValues());
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
    // inline — the very stall the stand-in existed to avoid.
    bool instancedOk = false;
    const bool standInResolved =
        cachedShape.getShape().TShape().get() == CoarseMeshTShape;
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
    if (instancedOk) {
        VisualTouched = false;
        // The material has to be checked again (colors verified uniform)
        setHighlightedFaces(DiffuseColor.getValues());
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

        // Coarse-first publish (docs/SceneStreaming.md §7): a headless
        // streaming server tessellates every shape at a ladder rung
        // instead of the full display deviation — the exact mesh is
        // then generated on demand, where a viewer's camera asks. The
        // display parameters are kept for that on-demand build.
        double exactDeflection = deflection;
        double exactAngle = AngDeflectionRads;
        float builtError = 0.0f;
        // The desktop refine already put this very TShape's exact
        // triangulation in place (§13): build at the display deviation
        // — the mesher finds the finer mesh resident and keeps it — and
        // register at error 0. A different TShape is a new shape, and
        // goes coarse-first again.
        const bool exactResident =
            !cShape.IsNull() && ExactMeshTShape == cShape.TShape().get();
        if (!exactResident)
            ExactMeshTShape = nullptr;
        const int coarseLvl = exactResident
            ? -1 : coarseTessellationLevel(pcObject ? pcObject->getDocument() : nullptr);
        if (coarseLvl >= 0) {
            double dx = xMax - xMin, dy = yMax - yMin, dz = zMax - zMin;
            double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (diag > 0) {
                deflection = meshLevelDeflection(diag, unsigned(coarseLvl));
                AngDeflectionRads = meshLevelAngle(unsigned(coarseLvl));
                builtError = float(1.0 / double(8u << unsigned(coarseLvl)));
            }
        }

        buildVisualNodes(cShape, deflection, AngDeflectionRads, NormalsFromUV,
                         coords, pcoords, norm, texcoords,
                         faceset, lineset, nodeset,
                         numTriangles, numNodes, numPoints, numNorms,
                         numFaces, numEdges, numLines);

        // The scene server can now re-tessellate this shape at a
        // coarser deviation when a viewer asks for a declared level of
        // the meshes these nodes feed (MeshLevelSource.h). Re-runs
        // replace the previous shape under the same node tags.
        //
        // On a coarse desktop build the registration also carries the
        // climb back to exact (§13): the worker meshes a copy at the
        // display parameters and this callback — GUI thread, and only
        // while the registration is still the live one, which is what
        // makes capturing `this` sound (the destructor unregisters) —
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
                ExactMeshTShape = tsh;
                ExactMeshCoarseError = builtError;
                updateVisual();
            };
        }
        // Exact by refine: the coarse triangulation never left the
        // shape (transferMeshLevels keeps it), so both ways back down
        // are armed (§13 step 3). The demote — only ever under an
        // observed CPU-memory ceiling — drops the exact rung outright;
        // the downgrade — the GPU budget's — merely re-activates the
        // coarse rung for display and keeps the exact one resident,
        // so the climb back is instant.
        std::function<void()> onDemote, onDowngrade;
        if (exactResident && ExactMeshCoarseError > 0.0f) {
            const void *tsh = cShape.TShape().get();
            onDemote = [this, tsh]() {
                TopoDS_Shape cur = cachedShape.getShape();
                if (cur.IsNull() || cur.TShape().get() != tsh)
                    return;
                if (!demoteMeshLevels(cur))
                    return;
                ExactMeshTShape = nullptr;
                updateVisual();
            };
            onDowngrade = [this, tsh]() {
                TopoDS_Shape cur = cachedShape.getShape();
                if (cur.IsNull() || cur.TShape().get() != tsh)
                    return;
                if (!downgradeMeshLevels(cur))
                    return;
                ExactMeshTShape = nullptr;
                updateVisual();
            };
        }
        registerMeshLevelSource(cShape, NormalsFromUV, faceset, lineset,
                                builtError, exactDeflection, exactAngle,
                                std::move(onExact), std::move(onDemote),
                                exactResident ? ExactMeshCoarseError
                                              : 0.0f,
                                std::move(onDowngrade),
                                pcObject ? pcObject->getDocument() : nullptr);
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

    // The material has to be checked again
    setHighlightedFaces(DiffuseColor.getValues());
    setHighlightedEdges(LineColorArray.getValues());
    setHighlightedPoints(PointColorArray.getValue());
}

void ViewProviderPartExt::buildVisualNodes(const TopoDS_Shape &cShape,
        double deflection, double AngDeflectionRads,
        bool NormalsFromUV,
        SoCoordinate3 *coords, SoCoordinate3 *pcoords,
        SoNormal *norm, SoTextureCoordinate2 *texcoords,
        SoBrepFaceSet *faceset, SoBrepEdgeSet *lineset,
        SoBrepPointSet *nodeset,
        int &numTriangles, int &numNodes, int &numPoints, int &numNorms,
        int &numFaces, int &numEdges, int &numLines)
{
    (void)nodeset;
    std::unordered_map<TopoDS_Shape, TopoDS_Face, Part::ShapeHasher, Part::ShapeHasher> faceEdges;
    TopLoc_Location aLoc;

    {
        // The default-texture-coordinate projection frame comes from this
        // shape's own bounding box (for the flattened build that is the
        // same whole-shape box the deflection derives from).
        Bnd_Box bounds;
        BRepBndLib::Add(cShape, bounds);
        bounds.SetGap(0.0);
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);

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
            TopoDS_Face face = TopoDS::Face(faceMap(i));
            Handle (Poly_Triangulation) mesh = Part::Tools::triangulationOfFace(face, aLoc, deflection, AngDeflectionRads);
            // Note: we must also count empty faces
            if (!mesh.IsNull()) {
                numTriangles += mesh->NbTriangles();
                numNodes     += mesh->NbNodes();
                numNorms     += mesh->NbNodes();
            }

            TopExp_Explorer xp;
            for (xp.Init(face,TopAbs_EDGE);xp.More();xp.Next())
                faceEdges.emplace(xp.Current(), face);
            numFaces++;
        }

        // get an indexed map of edges
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(cShape, TopAbs_EDGE, edgeMap);

         // key is the edge number, value the coord indexes. This is needed to keep the same order as the edges.
        std::map<int, std::vector<int32_t> > lineSetMap;
        std::set<int>          edgeIdxSet;
        std::vector<int32_t>   edgeVector;
        std::vector<int32_t>   seamEdges;

        // count and index the edges
        for (int i=1; i <= edgeMap.Extent(); i++) {
            edgeIdxSet.insert(i);
            numEdges++;

            const TopoDS_Edge& aEdge = TopoDS::Edge(edgeMap(i));
            TopLoc_Location aLoc;

            // handling of the free edge that are not associated to a face
            // Note: The assumption that if for an edge BRep_Tool::Polygon3D
            // returns a valid object is wrong. This e.g. happens for ruled
            // surfaces which gets created by two edges or wires.
            // So, we have to store the hashes of the edges associated to a face.
            // If the hash of a given edge is not in this list we know it's really
            // a free edge.
            auto it = faceEdges.find(aEdge);
            if (it != faceEdges.end()) {
                if (BRep_Tool::IsClosed(aEdge, it->second))
                    seamEdges.push_back(i-1);
            } else {
                Handle(Poly_Polygon3D) aPoly = Part::Tools::polygonOfEdge(aEdge, aLoc, deflection, AngDeflectionRads);
                if (!aPoly.IsNull()) {
                    int nbNodesInEdge = aPoly->NbNodes();
                    numNodes += nbNodesInEdge;
                }
            }
        }

        // create memory for the nodes and indexes
        coords  ->point      .setNum(numNodes);
        norm    ->vector     .setNum(numNorms);
        texcoords->point     .setNum(numNodes);
        faceset ->coordIndex .setNum(numTriangles*4);
        faceset ->partIndex  .setNum(numFaces);
        // get the raw memory for fast fill up
        SbVec3f* verts = coords  ->point       .startEditing();
        SbVec3f* norms = norm    ->vector      .startEditing();
        SbVec2f* texcoordArr = numNodes > 0 ? texcoords->point.startEditing() : nullptr;

        // Default texture coordinates take the shape bounding box as the
        // projection frame, the largest dimension as the texel scale
        // (uniform across faces).
        const SbVec3f bbMin((float)xMin, (float)yMin, (float)zMin);
        float maxDim = (float)std::max({xMax - xMin, yMax - yMin, zMax - zMin});
        float invMaxDim = maxDim > 0.0f ? 1.0f / maxDim : 0.0f;
        int32_t* index = faceset ->coordIndex  .startEditing();
        int32_t* parts = faceset ->partIndex   .startEditing();

        // preset the normal vector with null vector
        for (int i=0;i < numNorms;i++)
            norms[i]= SbVec3f(0.0,0.0,0.0);
        for (int i=0; texcoordArr && i < numNodes; i++)
            texcoordArr[i] = SbVec2f(0.0f, 0.0f);

        int ii = 0,faceNodeOffset=0,faceTriaOffset=0;
        for (int i=1; i <= faceMap.Extent(); i++, ii++) {
            TopLoc_Location aLoc;
            const TopoDS_Face &actFace = TopoDS::Face(faceMap(i));
            // get the mesh of the shape
            Handle (Poly_Triangulation) mesh = Part::Tools::triangulationOfFace(actFace, aLoc, deflection, AngDeflectionRads);
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

            // purely triangulated faces carry authored texture coordinates
            // and normals in the stored mesh — use both as-is
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
            for(Exp.Init(actFace,TopAbs_EDGE);Exp.More();Exp.Next()) {
                const TopoDS_Edge &curEdge = TopoDS::Edge(Exp.Current());
                // get the overall index of this edge
                int edgeIndex = edgeMap.FindIndex(curEdge);
                edgeVector.push_back((int32_t)edgeIndex-1);
                // already processed this index ?
                if (edgeIdxSet.find(edgeIndex)!=edgeIdxSet.end()) {

                    // this holds the indices of the edge's triangulation to the current polygon
                    Handle(Poly_PolygonOnTriangulation) aPoly = BRep_Tool::PolygonOnTriangulation(curEdge, mesh, aLoc);
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

            edgeVector.push_back(-1);

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

        // handling of the free edges
        for (int i=1; i <= edgeMap.Extent(); i++) {
            const TopoDS_Edge& aEdge = TopoDS::Edge(edgeMap(i));
            Standard_Boolean identity = true;
            gp_Trsf myTransf;
            TopLoc_Location aLoc;

            // handling of the free edge that are not associated to a face
            if (!faceEdges.count(aEdge)) {
                Handle(Poly_Polygon3D) aPoly = Part::Tools::polygonOfEdge(aEdge, aLoc, deflection, AngDeflectionRads);
                if (!aPoly.IsNull()) {
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
        }

        // handling of the vertices
        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(cShape, TopAbs_VERTEX, vertexMap);

        numPoints = vertexMap.Extent();
        pcoords->point.setNum(numPoints);
        verts = pcoords->point.startEditing();

        for (int i=0; i<numPoints; i++) {
            const TopoDS_Vertex& aVertex = TopoDS::Vertex(vertexMap(i+1));
            gp_Pnt pnt = BRep_Tool::Pnt(aVertex);
            verts[i].setValue((float)(pnt.X()),(float)(pnt.Y()),(float)(pnt.Z()));
        }

        // normalize all normals
        for (int i = 0; i< numNorms ;i++)
            norms[i].normalize();

        std::vector<int32_t> lineSetCoords;
        for (const auto & it : lineSetMap) {
            lineSetCoords.insert(lineSetCoords.end(), it.second.begin(), it.second.end());
            lineSetCoords.push_back(-1);
        }

        // preset the index vector size
        numLines =  lineSetCoords.size();
        lineset ->coordIndex .setNum(numLines);
        int32_t* lines = lineset ->coordIndex  .startEditing();

        int l=0;
        for (std::vector<int32_t>::const_iterator it=lineSetCoords.begin();it!=lineSetCoords.end();++it,l++)
            lines[l] = *it;

        // end the editing of the nodes
        coords  ->point       .finishEditing();
        pcoords ->point       .finishEditing();
        norm    ->vector      .finishEditing();
        if (texcoordArr)
            texcoords->point  .finishEditing();
        faceset ->coordIndex  .finishEditing();
        faceset ->partIndex   .finishEditing();
        lineset ->coordIndex  .finishEditing();
        if (seamEdges.size())
            lineset->seamIndices.setValues(0, seamEdges.size(), &seamEdges[0]);
    }
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

void ViewProviderPartExt::finishRestoring()
{
    inherited::finishRestoring();

    auto syncMaterial = [](const App::Material &Mat, SoMaterial *pcMaterial) {
        pcMaterial->ambientColor.setValue(Mat.ambientColor.r,Mat.ambientColor.g,Mat.ambientColor.b);
        pcMaterial->specularColor.setValue(Mat.specularColor.r,Mat.specularColor.g,Mat.specularColor.b);
        pcMaterial->emissiveColor.setValue(Mat.emissiveColor.r,Mat.emissiveColor.g,Mat.emissiveColor.b);
        pcMaterial->shininess.setValue(Mat.shininess);
        if (pcMaterial->diffuseColor.getNum() == 1)
            pcMaterial->transparency.setValue(Mat.transparency);
    };
    syncMaterial(LineMaterial.getValue(), pcLineMaterial);
    syncMaterial(PointMaterial.getValue(), pcPointMaterial);
    syncMaterial(ShapeMaterial.getValue(), pcShapeMaterial);

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
    if (VisualTouched)
        const_cast<ViewProviderPartExt*>(this)->updateVisual();
    return inherited::_getBoundingBox(subname, mat, transform, view, depth);
}
