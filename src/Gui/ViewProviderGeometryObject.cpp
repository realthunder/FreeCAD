/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include "ViewProviderDocumentObject.h"

#ifndef _PreComp_
# include <algorithm>
# include <Inventor/SoPickedPoint.h>
# include <Inventor/actions/SoRayPickAction.h>
# include <Inventor/actions/SoSearchAction.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoCamera.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoFont.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoDirectionalLight.h>
# include <Inventor/sensors/SoNodeSensor.h>
#endif

#include <Inventor/nodes/SoResetTransform.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>

#include <QImage>
#include <QMenu>

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Type.h>

#include <App/Document.h>
#include <App/PropertyFile.h>

#include <App/GeoFeature.h>
#include <App/PropertyGeo.h>
#include <App/PropertyUnits.h>

#include "ViewProviderGeometryObject.h"
#include "ActionFunction.h"
#include "Application.h"
#include "Control.h"
#include "Document.h"
#include "TaskRenderSettings.h"
#include "ViewParams.h"

#include "SoFCBoundingBox.h"
#include "SoFCSelection.h"
#include "SoFCUnifiedSelection.h"
#include "Inventor/SoFCRenderMaterial.h"
#include <Inventor/nodes/SoShaderProgram.h>
#include "Renderer/Renderer.h"
#include "ViewProviderShaderObject.h"
#include "View3DInventorViewer.h"


using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderGeometryObject, Gui::ViewProviderDragger)

const App::PropertyIntegerConstraint::Constraints intPercent = {0, 100, 5};

ViewProviderGeometryObject::ViewProviderGeometryObject()
{
    float r,g,b;

    if (ViewParams::getRandomColor()){
        // compute a random color in the HSV space
        SbColor color;
        color.setHSVValue(((float)rand()/RAND_MAX), // [0-1]
                          ((float)rand()/RAND_MAX) * 0.3 + 0.2, // [0.2-0.5]
                          ((float)rand()/RAND_MAX) * 0.35 + 0.55); // [0.55-0.9]
        r = color[0];
        g = color[1];
        b = color[2];
    }
    else {
        unsigned long shcol = ViewParams::getDefaultShapeColor();
        r = ((shcol >> 24) & 0xff) / 255.0;
        g = ((shcol >> 16) & 0xff) / 255.0;
        b = ((shcol >> 8) & 0xff) / 255.0;
    }

    int initialTransparency = ViewParams::getDefaultShapeTransparency(); 

    static const char *dogroup = "Display Options";
    static const char *osgroup = "Object Style";

    ADD_PROPERTY_TYPE(ShapeColor, (r, g, b), osgroup, App::Prop_None, "Set shape color");
    ADD_PROPERTY_TYPE(Transparency, (initialTransparency), osgroup, App::Prop_None, "Set object transparency");
    Transparency.setConstraints(&intPercent);

    // The appearance is the one storage now, so it starts out agreeing with
    // ShapeColor and Transparency instead of carrying DEFAULT's colour until
    // the first change syncs them. The default material itself stays this
    // fork's (STEEL under USER_DEFINED), not upstream's.
    App::MaterialAppearance mat(App::MaterialAppearance::DEFAULT);
    mat.diffuseColor.set(r, g, b);
    mat.transparency = Base::fromPercent(initialTransparency);
    ADD_PROPERTY_TYPE(ShapeAppearance, (mat), osgroup, App::Prop_None, "Shape appearance");
    ADD_PROPERTY_TYPE(ShapeMaterial, (mat), osgroup, App::Prop_None,
                      "Retired: a compatibility mirror of ShapeAppearance's base, "
                      "and it shares its name with the material CARD on the object. "
                      "Read ShapeAppearance instead");
    // Retired as a store; kept only so old macros and old documents still
    // land somewhere. One datum should not be two rows in the editor -- and
    // under "Show all", where this one does turn up, Legacy is what draws it
    // in red italic (docs/MaterialStorage.md 15.6).
    ShapeMaterial.setStatus(App::Property::Hidden, true);
    ShapeMaterial.setStatus(App::Property::Legacy, true);
    ADD_PROPERTY_TYPE(BoundingBox, (false), dogroup, App::Prop_None, "Display object bounding box");

    // Both names write through to the appearance from here on. Wired after
    // the ADD_PROPERTY calls above, whose own writes must not be redirected
    // while the appearance is still being constructed.
    ShapeColor.setAppearance(&ShapeAppearance);
    ShapeMaterial.setAppearance(&ShapeAppearance);

    pcShapeMaterial = new SoMaterial;
    setCoinAppearance(mat);
    pcShapeMaterial->ref();

    sPixmap = "Feature";
}

ViewProviderGeometryObject::~ViewProviderGeometryObject()
{
    pcShapeMaterial->unref();
    if (pcMaterialXNode) {
        pcMaterialXNode->unref();
        ViewProviderShaderBinding::releaseMaterialXNode(
                getObject() ? getObject()->getDocument() : nullptr, materialXHash);
    }
    if (pcRenderMaterial)
        pcRenderMaterial->unref();
    if (pcRenderTexture)
        pcRenderTexture->unref();
    if (pcRenderTexTransform)
        pcRenderTexTransform->unref();
    if (pcRenderBumpMap)
        pcRenderBumpMap->unref();
    if (pcRenderEmissiveMap)
        pcRenderEmissiveMap->unref();
    if (pcRenderOcclusionMap)
        pcRenderOcclusionMap->unref();
    if (pcRenderMetallicRoughnessMap)
        pcRenderMetallicRoughnessMap->unref();
    for (auto *node : pcFaceTextures)
        node->unref();
    if (pcRenderShadowStyle)
        pcRenderShadowStyle->unref();
    if(pcBoundingBox)
        pcBoundingBox->unref();
    if(pcBoundSwitch)
        pcBoundSwitch->unref();
    if(pcBoundColor)
        pcBoundColor->unref();
    delete pcSwitchSensor;
}

namespace {

/** Give the whole appearance one material, without losing the face colours
 *
 * A single material is a statement about the object, and an object having one
 * appearance does not mean its faces have stopped having colours of their own.
 * So a per-face diffuse field survives and every other field is set across the
 * list; only an appearance that already holds one diffuse colour is replaced
 * outright. Without this, restoring a document whose ShapeMaterial follows its
 * per-face colours -- which is every document written before ShapeAppearance,
 * since ShapeMaterial sorts after DiffuseColor -- throws those colours away.
 */
void applyWholeMaterial(App::PropertyAppearanceList &appearance, const App::MaterialAppearance &value)
{
    // ShapeMaterial cannot state a shading model -- it is a plain material
    // and its serialised form has no room for one -- so it must never
    // restate the appearance's. Without this the compatibility name would
    // convert a PBR appearance to Phong just by being restored after it
    // (it sorts later), or by an old macro writing through it.
    App::MaterialAppearance mat = value;
    mat.pbr = appearance.isPBR();
    // A surface finish is the same case one field further on: ShapeMaterial's
    // serialised form has no room for one either, so the value arriving here
    // always states None -- and this write states every field of the base,
    // which would wipe a finish the appearance had just restored (this name
    // sorts after ShapeAppearance). Take the appearance's own, and the
    // texture beside it for the same reason.
    mat.finish = appearance.getBase().finish;
    mat.texture = appearance.getBase().texture;
    // The BASE, which is exactly what this compatibility name has always
    // meant: the object's look. The overriding faces keep what they hold
    // (docs/ShapeAppearanceDesign.md 12.2), where the old whole-value write
    // had to choose between collapsing them and skipping half the fields.
    appearance.setBase(mat);
}

}  // namespace

void ViewProviderGeometryObject::onChanged(const App::Property* prop)
{
    Gui::ColorUpdater colorUpdater;

    // ShapeColor and Transparency are the single-value face of ShapeAppearance,
    // kept as their own properties because they are what the user reaches for.
    // The appearance holds the value; these two mirror entry 0 of it.
    if (prop == &ShapeColor) {
        // The push lives here, not only in ShapeColor::setValue, because the
        // base's setPyObject and Paste call setValue non-virtually and would
        // otherwise update the mirror without ever reaching the appearance.
        // Guarded, so the appearance mirroring back cannot ping-pong.
        //
        // The rgb only: the entry's alpha is its opacity, Transparency's to
        // move. With one store a whole-colour push would drag whatever alpha
        // ShapeColor happens to hold over the object's transparency -- the
        // old second store used to absorb exactly that.
        Base::Color c = ShapeColor.getValue();
        pcShapeMaterial->diffuseColor.setValue(c.r, c.g, c.b);
        const Base::Color entry = ShapeAppearance.getBase().diffuseColor;
        c.a = entry.a;
        if (c != entry)
            ShapeAppearance.setDiffuseColor(c);
    }
    else if (prop == &ShapeMaterial) {
        const App::MaterialAppearance &mat = ShapeMaterial.getValue();
        // Only a single appearance can be pushed into the one Coin material
        // node, as below: a per-face one is carried by the shape's own
        // material arrays and pushing a single colour would wipe them.
        if (!ShapeAppearance.variesInDiffuse())
            setCoinAppearance(mat);
        if (!(mat == ShapeAppearance.getBase()))
            applyWholeMaterial(ShapeAppearance, mat);
    }
    else if (prop == &Transparency) {
        long value = Base::toPercent(ShapeAppearance.getBase().transparency);
        if (value != Transparency.getValue()) {
            float trans = Base::fromPercent(Transparency.getValue());
            pcShapeMaterial->transparency = trans;
            ShapeAppearance.setTransparency(trans);
        }
    }
    else if (prop == &ShapeAppearance) {
        if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange))
            getObject()->touch(true);
        long value = Base::toPercent(ShapeAppearance.getBase().transparency);
        if (value != Transparency.getValue())
            Transparency.setValue(value);
        // Only a single appearance can be pushed into the one Coin material
        // node; a per-face list is carried by the shape's own material arrays
        // (ViewProviderPartExt), so leave the node alone in that case. The
        // node gets the Phong reading; the ShapeMaterial mirror below stays
        // raw, because it writes back into the appearance and a derived
        // mirror would quietly convert the stored values.
        if (ShapeAppearance.getSize() == 1)
            setCoinAppearance(ShapeAppearance.getPhongBase());
        // Refresh the two compatibility names off the appearance. mirrorValue,
        // not setValue: these read from the store they would otherwise write
        // straight back into.
        //
        // The BASE, whatever the faces hold: it is the object's look, which
        // is what these two names mean, and it exists whether or not any
        // face overrides it (docs/ShapeAppearanceDesign.md 12.1 -- before
        // the base they could only mirror a uniform list, and on a per-face
        // one held whatever the last uniform value had been).
        //
        // Alpha included: the appearance's diffuse alpha IS the entry's
        // opacity, and a mirror carrying any other alpha stops being equal
        // to it -- so the equality check in mirrorValue fires, the write
        // announces, and the onChanged web pushes the stale alpha straight
        // back into the appearance it was mirroring.
        ShapeColor.mirrorValue(ShapeAppearance.getBase().diffuseColor);
        ShapeMaterial.mirrorValue(ShapeAppearance.getBase());
        // A flag just set -- from Python, or from the panel's "As material"
        // -- takes the card's look at once (docs/MaterialStorage.md 15.7).
        // Not during a restore: the document's own answer is landing, and
        // finishRestoring re-derives it there without marking it modified.
        if (!App::Document::isAnyRestoring())
            applyMaterialAppearance();
        // A PBR-mode appearance rides the render material node (its
        // metallic/roughness), so it has to follow appearance changes too
        updateRenderMaterial();
        // ... and the per-face images it may now carry decide whether
        // the unit-0 stand-in that makes texture coordinates exist is
        // needed at all.
        updateRenderTexture();
        Gui::ColorUpdater::addObject(getObject());
    }
    else if (prop == &BoundingBox) {
        showBoundingBox(BoundingBox.getValue());
    }
    else if (prop->getName()
             && strncmp(prop->getName(), "Render_", 7) == 0) {
        // Render engine per-object settings (dynamic properties, group
        // "Render"), mirrored into SoFCRenderMaterial / texture /
        // shadow style nodes.
        updateRenderProperty(prop->getName());
    }

    ViewProviderDragger::onChanged(prop);
}

// ⭐ Registered under a leading underscore, which is what marks a property
// type as one container's own member rather than a value a user may add
// (App::Property::isInternalType): both of these are names over
// ShapeAppearance and are wired to it by the constructor below, so a
// standalone instance -- all addDynamicProperty can make -- would be wired
// to nothing. The C++ names are left alone; only the registered type name
// carries the mark. Nothing reads the pre-underscore name: both types were
// added during this development cycle, so no document anywhere states one.
TYPESYSTEM_SOURCE_P(Gui::PropertyShapeColor)
void Gui::PropertyShapeColor::init()
{
    initSubclass(Gui::PropertyShapeColor::classTypeId, "Gui::_PropertyShapeColor",
                 "App::PropertyColor", &Gui::PropertyShapeColor::create);
}

TYPESYSTEM_SOURCE_P(Gui::PropertyShapeAppearance)
void Gui::PropertyShapeAppearance::init()
{
    initSubclass(Gui::PropertyShapeAppearance::classTypeId, "Gui::_PropertyShapeAppearance",
                 "App::PropertyAppearance", &Gui::PropertyShapeAppearance::create);
    // Was Gui::_PropertyShapeMaterial, and every GuiDocument.xml written so
    // far says so -- the render examples under data/examples/render alone
    // carry eighteen of them. The alias is what keeps those readable; without
    // it the restore takes the "type changed" branch and drops the value.
    Base::Type::addLegacyName(Gui::PropertyShapeAppearance::classTypeId,
                              "Gui::_PropertyShapeMaterial");
}

void PropertyShapeColor::setValue(const Base::Color &col)
{
    App::PropertyColor::setValue(col);
    if (_appearance) {
        // The rgb only; the entry's alpha is the object's transparency and
        // moves through Transparency (see the onChanged push).
        Base::Color c = col;
        c.a = _appearance->getDiffuseColor(0).a;
        _appearance->setDiffuseColor(c);
    }
}

void PropertyShapeColor::setValue(float r, float g, float b, float a)
{
    setValue(Base::Color(r, g, b, a));
}

void PropertyShapeColor::setValue(uint32_t rgba)
{
    Base::Color col;
    col.setPackedValue(rgba);
    setValue(col);
}

void PropertyShapeColor::applyToAppearance()
{
    // Not onto a per-face appearance: on a document written before there
    // was a base, this value IS the hint the base is derived from
    // (docs/ShapeAppearanceDesign.md 12.4), and writing it into a default
    // base first would answer the question before the heuristic asks it.
    // The rgb only, as every ShapeColor push: an old document's transparency
    // arrives through ShapeMaterial and Transparency, which restore later.
    if (_appearance && !_appearance->hasOverrides()) {
        Base::Color c = getValue();
        c.a = _appearance->getBase().diffuseColor.a;
        _appearance->setDiffuseColor(c);
    }
}

void PropertyShapeColor::Restore(Base::XMLReader &reader)
{
    App::PropertyColor::Restore(reader);
    applyToAppearance();
}

void PropertyShapeAppearance::setValue(const App::MaterialAppearance &mat)
{
    App::PropertyAppearance::setValue(mat);
    if (_appearance)
        applyWholeMaterial(*_appearance, mat);
}

void PropertyShapeAppearance::applyToAppearance()
{
    // Also the entry point for an old document's ShapeMaterial, which arrives
    // after its per-face colours: those are the more specific value and the
    // rest of the material still applies over them.
    if (_appearance)
        applyWholeMaterial(*_appearance, getValue());
}

void PropertyShapeAppearance::Restore(Base::XMLReader &reader)
{
    App::PropertyAppearance::Restore(reader);
    applyToAppearance();
}

void ViewProviderGeometryObject::setCoinAppearance(const App::MaterialAppearance &mat)
{
    pcShapeMaterial->ambientColor.setValue(mat.ambientColor.r, mat.ambientColor.g, mat.ambientColor.b);
    pcShapeMaterial->diffuseColor.setValue(mat.diffuseColor.r, mat.diffuseColor.g, mat.diffuseColor.b);
    pcShapeMaterial->specularColor.setValue(mat.specularColor.r, mat.specularColor.g, mat.specularColor.b);
    pcShapeMaterial->emissiveColor.setValue(mat.emissiveColor.r, mat.emissiveColor.g, mat.emissiveColor.b);
    pcShapeMaterial->shininess.setValue(mat.shininess);
    pcShapeMaterial->transparency.setValue(mat.transparency);
}

void ViewProviderGeometryObject::handleChangedPropertyType(Base::XMLReader &reader,
                                                           const char *TypeName,
                                                           App::Property *prop)
{
    // ShapeColor and ShapeMaterial kept their names but changed type when
    // their storage moved into ShapeAppearance, so a document written before
    // that arrives here rather than at handleChangedPropertyName. Restore
    // through a stand-in of the old type and fold the value in; without this
    // the base does nothing and every such object silently loses its colour.
    if (prop == &ShapeColor
            && strcmp(TypeName, App::PropertyColor::getClassTypeId().getName()) == 0) {
        App::PropertyColor old;
        old.Restore(reader);
        ShapeColor.mirrorValue(old.getValue());
        ShapeColor.applyToAppearance();
        return;
    }
    if (prop == &ShapeMaterial
            && strcmp(TypeName, App::PropertyAppearance::getClassTypeId().getName()) == 0) {
        App::PropertyAppearance old;
        old.Restore(reader);
        ShapeMaterial.mirrorValue(old.getValue());
        ShapeMaterial.applyToAppearance();
        return;
    }
    ViewProviderDragger::handleChangedPropertyType(reader, TypeName, prop);
}

namespace {

// Load an image file into a SoSFImage field (RGB8/RGBA8). Qt does the
// decoding so no optional Coin/simage image support is needed; the pixels
// embed in the node, which also feeds the mode-3 render cache texture
// capture. An image without an alpha channel uploads as 3 components —
// a 4-component image is what marks the texture (and every draw using
// it) transparent. keepGray preserves grayscale images as one component
// (a bump map's component count is what tells a height field from a
// tangent-space normal map).
bool loadTextureImage(const char *path, SoSFImage &field,
                      bool keepGray = false)
{
    QImage img;
    if (!path || !path[0] || !img.load(QString::fromUtf8(path)))
        return false;
    bool alpha = img.hasAlphaChannel();
    bool gray = keepGray && !alpha && img.isGrayscale();
    img = img.convertToFormat(gray ? QImage::Format_Grayscale8
                              : alpha ? QImage::Format_RGBA8888
                                      : QImage::Format_RGB888);
    // Coin images are bottom-up.
    img = img.mirrored(false, true);
    int nc = gray ? 1 : alpha ? 4 : 3;
    int rowLen = img.width() * nc;
    if (img.bytesPerLine() == rowLen) {
        field.setValue(SbVec2s(short(img.width()), short(img.height())), nc,
                       img.constBits());
    }
    else {
        // QImage scanlines are 4-byte aligned; Coin expects packed rows
        std::vector<unsigned char> packed(size_t(rowLen) * img.height());
        for (int y = 0; y < img.height(); ++y)
            memcpy(packed.data() + size_t(y) * rowLen, img.constScanLine(y), rowLen);
        field.setValue(SbVec2s(short(img.width()), short(img.height())), nc,
                       packed.data());
    }
    return true;
}

// Load an image a material card carried as CONTENT rather than as a
// path (App::MaterialAppearance::image, upstream's "TextureImage"): the encoded
// bytes of an image file, base64 in every card that has ever held one.
// Falls back to reading it as raw file bytes, so a card written with
// the payload unencoded still draws.
bool loadTextureImageData(const std::string &data, SoSFImage &field)
{
    if (data.empty())
        return false;
    QByteArray raw = QByteArray::fromBase64(
            QByteArray::fromRawData(data.c_str(), int(data.size())));
    QImage img;
    if (!img.loadFromData(raw)
            && !img.loadFromData(QByteArray::fromRawData(
                       data.c_str(), int(data.size()))))
        return false;
    const bool alpha = img.hasAlphaChannel();
    img = img.convertToFormat(alpha ? QImage::Format_RGBA8888
                                    : QImage::Format_RGB888);
    img = img.mirrored(false, true);   // Coin images are bottom-up
    const int nc = alpha ? 4 : 3;
    const int rowLen = img.width() * nc;
    std::vector<unsigned char> packed(size_t(rowLen) * img.height());
    for (int y = 0; y < img.height(); ++y)
        memcpy(packed.data() + size_t(y) * rowLen, img.constScanLine(y),
               rowLen);
    field.setValue(SbVec2s(short(img.width()), short(img.height())), nc,
                   packed.data());
    return true;
}

// Fill in what a stated finish leaves unsaid: a pattern authored with no
// pitch (or with the placeholder minimum App::SurfaceFinish::normalize()
// floors an unstated one to) gets the size that pattern has on a real
// part, and a depth that keeps its flank slopes machinable. Millimetres,
// absolute rather than scaled to the model -- a finish is a physical
// fact about the surface, and a 0.8 mm knurl stays 0.8 mm whether it is
// cut on a thumbscrew or on a capstan.
void applyFinishDefaults(App::SurfaceFinish &finish)
{
    float pitch = 0.0f;
    float depthratio = 0.0f;
    switch (finish.pattern) {
        case App::SurfaceFinish::Knurl:
        case App::SurfaceFinish::KnurlStraight:
            pitch = 0.8f;   // a common medium diamond knurl
            depthratio = 0.3f;
            break;
        case App::SurfaceFinish::Brushed:
            pitch = 0.15f;  // fine scratch lay
            depthratio = 0.1f;
            break;
        case App::SurfaceFinish::Blasted:
            pitch = 0.12f;  // bead craters
            // Shallow: a crater as deep as a third of its width reads
            // as lunar rather than as blasted (measured against the
            // depth sweep, 2026-08-14)
            depthratio = 0.15f;
            break;
        case App::SurfaceFinish::Turned:
            pitch = 0.25f;  // lathe feed marks
            depthratio = 0.12f;
            break;
        default:
            // None, or a pattern only a later build knows: nothing to
            // default, and the backend draws neither
            return;
    }
    if (finish.pitch <= App::SurfaceFinish::MinPitch)
        finish.pitch = pitch;
    if (finish.depth <= 0.0f)
        finish.depth = finish.pitch * depthratio;
}

} // anonymous namespace

float ViewProviderGeometryObject::faceTextureScale() const
{
    auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
            getPropertyByName("Render_FaceTextureScale"));
    const float scale = prop ? float(prop->getValue()) : -1.0f;
    if (scale == 0.0f)
        return -1.0f;    // an explicit zero reads as "the mesh's UVs"
    if (scale < 0.0f)
        return 25.0f;    // unset: a hand-sized marking
    return scale;
}

void ViewProviderGeometryObject::updateRenderTexture()
{
    auto fileProp = [this](const char *name) -> const char * {
        auto prop = Base::freecad_dynamic_cast<App::PropertyFileIncluded>(
                getPropertyByName(name));
        return prop ? prop->getValue() : nullptr;
    };
    const char *color = fileProp("Render_BaseColorTexture");
    const char *bump = fileProp("Render_NormalMap");
    const char *emissive = fileProp("Render_EmissiveMap");
    const char *occlusion = fileProp("Render_OcclusionMap");
    const char *metallicroughness = fileProp("Render_MetallicRoughnessMap");

    // Base color texture (unit-0 SoTexture2, modulate). When only a
    // bump/emissive/occlusion/metallic-roughness map is set, a 1x1 white
    // stand-in still goes in: an enabled texture unit is what makes the
    // shapes generate texture coordinates (both in Coin GL and in the
    // render cache capture).
    //
    // A per-face image laid out on the mesh's OWN coordinates needs the
    // stand-in for exactly the same reason: without an enabled unit the
    // shapes generate no coordinates, the vertex cache captures none,
    // and every fragment reads the image's corner texel. The frame
    // projection does not -- it makes its own coordinates out of the
    // object-space position.
    const bool faceImages = ShapeAppearance.hasImage();
    const bool faceImagesOnMeshUV = faceImages && faceTextureScale() <= 0.0f;
    // A MaterialX shader graph is the third case, and the plainest one:
    // its image nodes read `texcoord`, which is the mesh's OWN
    // coordinates in both backends -- the generated raster code samples
    // v_texcoord0 and the path tracer's interpreter emits a texture
    // coordinate node reading ATTR_STD_UV. Without an enabled unit there
    // are none of either, and every one of the document's maps comes out
    // as its corner texel: MaterialX's chess set rendered as flat grey
    // paint (docs/MaterialStorage.md sec 17.13).
    const bool materialXGraph =
        ShapeAppearance.getSize() && !ShapeAppearance.getBase().materialx.empty();
    bool wantTexture = (color && color[0]) || (bump && bump[0])
        || (emissive && emissive[0]) || (occlusion && occlusion[0])
        || (metallicroughness && metallicroughness[0])
        || faceImagesOnMeshUV || materialXGraph;
    if (!wantTexture) {
        if (pcRenderTexture) {
            int idx = pcRoot->findChild(pcRenderTexture);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderTexture->unref();
            pcRenderTexture = nullptr;
        }
    }
    else {
        if (!pcRenderTexture) {
            pcRenderTexture = new SoTexture2;
            pcRenderTexture->ref();
            pcRoot->insertChild(pcRenderTexture, 0);
        }
        if (!loadTextureImage(color, pcRenderTexture->image)) {
            static const unsigned char white[3] = {255, 255, 255};
            pcRenderTexture->image.setValue(SbVec2s(1, 1), 3, white);
        }
    }

    // Texture transform (Render_TextureScale/Offset/Rotation): an
    // SoTexture2Transform node feeding both Coin's own GL texturing and
    // the render cache's texture matrix capture. Only kept while a
    // texture (or bump map) is active and any value is non-default.
    auto vectorProp = [this](const char *name, float def,
                             float out[2]) -> bool {
        out[0] = out[1] = def;
        auto prop = Base::freecad_dynamic_cast<App::PropertyVector>(
                getPropertyByName(name));
        if (!prop)
            return false;
        out[0] = float(prop->getValue().x);
        out[1] = float(prop->getValue().y);
        return out[0] != def || out[1] != def;
    };
    float texScale[2], texOffset[2], texRotation = 0.0f;
    bool wantTransform = false;
    if (wantTexture) {
        wantTransform |= vectorProp("Render_TextureScale", 1.0f, texScale);
        wantTransform |= vectorProp("Render_TextureOffset", 0.0f,
                                    texOffset);
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyAngle>(
                    getPropertyByName("Render_TextureRotation"))) {
            texRotation = float(prop->getValue());
            wantTransform |= texRotation != 0.0f;
        }
    }
    if (!wantTransform) {
        if (pcRenderTexTransform) {
            int idx = pcRoot->findChild(pcRenderTexTransform);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderTexTransform->unref();
            pcRenderTexTransform = nullptr;
        }
    }
    else {
        if (!pcRenderTexTransform) {
            pcRenderTexTransform = new SoTexture2Transform;
            pcRenderTexTransform->ref();
            pcRoot->insertChild(pcRenderTexTransform, 0);
        }
        pcRenderTexTransform->scaleFactor.setValue(texScale[0],
                                                   texScale[1]);
        pcRenderTexTransform->translation.setValue(texOffset[0],
                                                   texOffset[1]);
        pcRenderTexTransform->rotation
            = texRotation * float(M_PI) / 180.0f;
    }

    // Tangent-space normal map / grayscale height map (SoBumpMap; only
    // the external render backends draw it).
    if (!(bump && bump[0])) {
        if (pcRenderBumpMap) {
            int idx = pcRoot->findChild(pcRenderBumpMap);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderBumpMap->unref();
            pcRenderBumpMap = nullptr;
        }
    }
    else {
        if (!pcRenderBumpMap) {
            pcRenderBumpMap = new SoBumpMap;
            pcRenderBumpMap->ref();
            pcRoot->insertChild(pcRenderBumpMap, 0);
        }
        loadTextureImage(bump, pcRenderBumpMap->image, true);
    }

    // Emissive/occlusion/metallic-roughness material maps
    // (SoFCRenderTexture; only the external render backends draw them).
    auto syncRenderTexture = [this](const char *path,
                                    SoFCRenderTexture::Slot slot,
                                    SoFCRenderTexture *&node) {
        if (!(path && path[0])) {
            if (node) {
                int idx = pcRoot->findChild(node);
                if (idx >= 0)
                    pcRoot->removeChild(idx);
                node->unref();
                node = nullptr;
            }
            return;
        }
        if (!node) {
            node = new SoFCRenderTexture;
            node->ref();
            node->slot = slot;
            pcRoot->insertChild(node, 0);
        }
        loadTextureImage(path, node->image);
    };
    syncRenderTexture(emissive, SoFCRenderTexture::EMISSIVE,
                      pcRenderEmissiveMap);
    syncRenderTexture(occlusion, SoFCRenderTexture::OCCLUSION,
                      pcRenderOcclusionMap);
    syncRenderTexture(metallicroughness,
                      SoFCRenderTexture::METALLIC_ROUGHNESS,
                      pcRenderMetallicRoughnessMap);
}

void ViewProviderGeometryObject::updateFaceTextures(
        std::vector<int32_t> &indices)
{
    indices.clear();

    // What each face names: its own image path, or the encoded image
    // the appearance carries inline. Faces naming the same one share a
    // palette layer, so a part with two markings costs two layers
    // however many faces wear them.
    const int count = ShapeAppearance.getSize();
    std::vector<std::pair<std::string, std::string>> palette;  // path, data
    bool any = false;
    if (count > 0 && ShapeAppearance.hasImage()) {
        indices.assign(std::size_t(count), 0);
        for (int i = 0; i < count; ++i) {
            const std::string &path = ShapeAppearance.getImagePath(i);
            const std::string &data = ShapeAppearance.getImage(i);
            if (path.empty() && data.empty())
                continue;
            int layer = 0;
            for (std::size_t k = 0; k < palette.size() && !layer; ++k) {
                if (palette[k].first == path && palette[k].second == data)
                    layer = int(k) + 1;
            }
            if (!layer) {
                // Past the cap a face keeps layer 0 -- untextured --
                // rather than wearing some other face's marking.
                if (palette.size() + 1
                        >= std::size_t(Render::MaxFaceTexturePalette))
                    continue;
                palette.emplace_back(path, data);
                layer = int(palette.size());
            }
            indices[std::size_t(i)] = layer;
            any = true;
        }
    }
    if (!any) {
        indices.clear();
        palette.clear();
    }

    // One node a layer under the view provider root. Rebuilt only when
    // the palette really changed: every write notifies, and a
    // notification off one of these invalidates the render caches below.
    while (pcFaceTextures.size() > palette.size()) {
        SoFCRenderTexture *node = pcFaceTextures.back();
        pcFaceTextures.pop_back();
        int idx = pcRoot->findChild(node);
        if (idx >= 0)
            pcRoot->removeChild(idx);
        node->unref();
    }
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const bool fresh = i >= pcFaceTextures.size();
        if (fresh) {
            auto *node = new SoFCRenderTexture;
            node->ref();
            node->slot = SoFCRenderTexture::FACE;
            node->layer = int(i) + 1;
            pcRoot->insertChild(node, 0);
            pcFaceTextures.push_back(node);
        }
        SoFCRenderTexture *node = pcFaceTextures[i];
        if (!fresh && node->layer.getValue() == int(i) + 1
                && faceTextureSources[i] == palette[i])
            continue;
        if (node->layer.getValue() != int(i) + 1)
            node->layer = int(i) + 1;
        if (!loadTextureImage(palette[i].first.c_str(), node->image)
                && !loadTextureImageData(palette[i].second, node->image)) {
            // Neither the path nor the payload gave an image: a 1x1
            // white layer, so the faces naming it draw as they would
            // untextured instead of sampling whatever was there before.
            static const unsigned char white[3] = {255, 255, 255};
            node->image.setValue(SbVec2s(1, 1), 3, white);
        }
    }
    faceTextureSources = std::move(palette);
}

void ViewProviderGeometryObject::updateMaterialXNode()
{
    // Uniform for now: the BASE's document set. A per-face palette of
    // documents is the per-triangle shader slot work of step 9.
    //
    // The column itself is real -- the appearance stores, saves and
    // restores a document set per face -- so an object CAN hold one that
    // nothing draws. Say so once, on the edit that starts it, rather
    // than let the picture disagree with the data in silence.
    bool varies = ShapeAppearance.variesInMaterialX();
    if (varies != materialXVaries) {
        materialXVaries = varies;
        if (varies && getObject()) {
            Base::Console().Warning(
                "%s: a per-face MaterialX document set is stored but not "
                "drawn; every face shows the base's document "
                "(docs/MaterialStorage.md sec 17.11)\n",
                getObject()->getFullName().c_str());
        }
    }
    std::string hash = ShapeAppearance.getSize() ? ShapeAppearance.getBase().materialx
                                                 : std::string();
    App::Document *doc = getObject() ? getObject()->getDocument() : nullptr;
    if (hash == materialXHash && (hash.empty() || pcMaterialXNode))
        return;
    if (pcMaterialXNode) {
        int idx = pcRoot->findChild(pcMaterialXNode);
        if (idx >= 0)
            pcRoot->removeChild(idx);
        pcMaterialXNode->unref();
        pcMaterialXNode = nullptr;
        ViewProviderShaderBinding::releaseMaterialXNode(doc, materialXHash);
    }
    materialXHash = hash;
    if (hash.empty() || !doc)
        return;
    // Null while the blobs have not all arrived: finishRestoring() comes
    // back through updateRenderMaterial() once the archive is drained.
    SoShaderProgram *node = ViewProviderShaderBinding::acquireMaterialXNode(doc, hash);
    if (!node)
        return;
    node->ref();
    pcMaterialXNode = node;
    // At the head of the root, where a Scope=Object binding puts its node:
    // the capture callback routes a material-stage program found there
    // into this object's own render cache.
    //
    // Index 0 is also the losing end on purpose. The cache's
    // setUserShader keeps the LAST material-stage program traversed, and
    // an explicit binding beats the card an object wears, so the card
    // goes IN FRONT of a binding node already there. The other direction
    // is the binding's to keep: applyDirectBindings() inserts behind
    // this node when it finds one.
    pcRoot->insertChild(node, 0);
}

void ViewProviderGeometryObject::updateRenderMaterial()
{
    updateMaterialXNode();
    // The Render_* dynamic properties are optional per-object render
    // engine settings; a SoFCRenderMaterial node at the head of the view
    // provider root carries them into the mode-3 render cache (the node
    // has no effect on Coin's own GL rendering). The node is created on
    // first use and dropped when no property carries a value anymore.
    auto floatProp = [this](const char *name) -> float {
        auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
                getPropertyByName(name));
        return prop ? float(prop->getValue()) : -1.0f;
    };
    float metallic = floatProp("Render_Metallic");
    float roughness = floatProp("Render_Roughness");
    // A PBR-mode appearance is authored material data and beats the
    // Render_* overrides, which predate it as the only way to state
    // these. Entry 0 -- exact for a uniform appearance; a per-face one
    // is approximated by its first entry until the per-face streams
    // carry the raw values.
    const bool pbr = ShapeAppearance.isPBR();
    // Per-face factor pair, when the appearance's entries genuinely
    // differ in it: neither factor has a Coin material field to ride
    // (see SoFCPbrElement), so the arrays travel on the node below and
    // the render cache bakes them into the per-vertex material stream.
    std::vector<float> metallics;
    std::vector<float> roughnesses;
    if (pbr) {
        metallic = ShapeAppearance.getMetallic(0);
        // The renderer reads a roughness <= 0 as unset (and derives from
        // the shininess), so an authored mirror finish stops at the
        // shader's own lower clamp instead
        roughness = ShapeAppearance.getRoughness(0);
        if (roughness < 0.02f)
            roughness = 0.02f;
        const int count = ShapeAppearance.getSize();
        bool varies = false;
        for (int i = 1; i < count && !varies; ++i) {
            varies = ShapeAppearance.getMetallic(i) != ShapeAppearance.getMetallic(0)
                || ShapeAppearance.getRoughness(i) != ShapeAppearance.getRoughness(0);
        }
        if (varies) {
            metallics.reserve(count);
            roughnesses.reserve(count);
            for (int i = 0; i < count; ++i) {
                metallics.push_back(ShapeAppearance.getMetallic(i));
                roughnesses.push_back(
                        std::max(ShapeAppearance.getRoughness(i), 0.02f));
            }
        }
    }
    // The machined surface finish (App::SurfaceFinish) the render engine
    // shades as a procedural pattern. Authored material data beats the
    // Render_* knobs here exactly as it does for the PBR pair above: an
    // appearance that states a finish is the finish, and the knobs are
    // how an appearance that carries none gets one. Entry 0 -- exact for
    // a uniform appearance, the first face's for a per-face one until
    // the per-face finish stream lands.
    App::SurfaceFinish finish = ShapeAppearance.getBase().finish;
    // An appearance that states a finish ANYWHERE is the authority for
    // every face, not only for the faces it finished: a face left
    // unfinished in a per-face appearance is a statement too, and letting
    // the knobs fill it in would contradict the palette built below
    // (whose entry 0 is what these scalars repeat).
    if (!ShapeAppearance.hasFinish()) {
        // The pattern knob reads as a name (an enumeration the object
        // carries its own list for, or a plain string) or as the raw
        // Pattern value, so it can be stated from the property editor
        // and from a script without either having to guess the other's
        // spelling.
        uint8_t pattern = App::SurfaceFinish::None;
        App::Property *prop = getPropertyByName("Render_Finish");
        if (auto enumprop =
                Base::freecad_dynamic_cast<App::PropertyEnumeration>(prop)) {
            if (enumprop->getEnum().isValid()) {
                if (const char *name = enumprop->getValueAsString())
                    pattern = App::SurfaceFinish::patternFromName(name);
            }
        }
        else if (auto strprop =
                     Base::freecad_dynamic_cast<App::PropertyString>(prop)) {
            pattern = App::SurfaceFinish::patternFromName(strprop->getValue());
        }
        else if (auto intprop =
                     Base::freecad_dynamic_cast<App::PropertyInteger>(prop)) {
            long value = intprop->getValue();
            if (value > 0 && value < App::SurfaceFinish::PatternCount)
                pattern = static_cast<uint8_t>(value);
        }
        if (pattern != App::SurfaceFinish::None) {
            finish.pattern = pattern;
            // <= 0 = automatic, the convention every Render_* size knob
            // follows (floatProp answers -1 for a property nobody added)
            float value = floatProp("Render_FinishPitch");
            finish.pitch = value > 0.0f ? value : 0.0f;
            value = floatProp("Render_FinishDepth");
            finish.depth = value > 0.0f ? value : 0.0f;
            value = floatProp("Render_FinishAngle");
            finish.angle = value > 0.0f ? value : 0.0f;
            finish.normalize();
        }
    }
    applyFinishDefaults(finish);

    // Per-face form of the finish, when the appearance's entries
    // genuinely differ in it. Four numbers per face is three arrays more
    // than a per-vertex stream should carry, so the distinct finishes
    // become a PALETTE and each face carries one index into it (see
    // SoFCFinishElement); entry 0 is what the scalars above repeat, so a
    // consumer that ignores the palette keeps the per-object look.
    std::vector<SbVec4f> palette;
    std::vector<int32_t> finishIndices;
    // Asked of the storage before the whole field is built: resolving it
    // materialises one record per face, and a list where no face states a
    // finish of its own has nothing here to do
    if (ShapeAppearance.variesInFinish()) {
        const std::vector<App::SurfaceFinish> finishes = ShapeAppearance.getFinishes();
        finishIndices.reserve(finishes.size());
        for (const App::SurfaceFinish &entry : finishes) {
            App::SurfaceFinish face = entry;
            applyFinishDefaults(face);
            const SbVec4f value(float(face.pattern), face.pitch, face.depth,
                                face.angle);
            int idx = -1;
            for (std::size_t k = 0; k < palette.size() && idx < 0; ++k) {
                if (palette[k] == value)
                    idx = int(k);
            }
            if (idx < 0) {
                // Past the cap the face falls back to entry 0 -- the
                // object's own finish -- rather than to an arbitrary
                // neighbour's. A part with more than this many distinct
                // finishes is not a part anyone machined.
                if (palette.size() >= std::size_t(Render::MaxFinishPalette))
                    idx = 0;
                else {
                    palette.push_back(value);
                    idx = int(palette.size()) - 1;
                }
            }
            finishIndices.push_back(idx);
        }
    }
    if (palette.size() < 2) {
        palette.clear();
        finishIndices.clear();
    }

    // The images the appearance puts on individual faces, as a palette
    // of texture nodes plus one layer index per face.
    std::vector<int32_t> faceTextureIndices;
    updateFaceTextures(faceTextureIndices);
    // Where those images are laid out: millimetres of object space per
    // tile, the physical size a printed decal or a machined marking
    // has. A NEGATIVE value hands them the mesh's own texture
    // coordinates instead, for a shape that really was UV mapped;
    // unset (the ordinary case, and a CAD shape carries no UVs) takes
    // the default below.
    const float faceTexScale = faceTextureScale();

    // Render_Water turns the object's closed shape into a water body of
    // the render engine's volumetric lighting pass (tinted by the shape
    // color); Render_WaterDensity <= 0 = automatic.
    auto boolProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Water"));
    bool water = boolProp && boolProp->getValue();
    float waterDensity = water ? floatProp("Render_WaterDensity") : 0.0f;
    // Render_Glass renders the object's closed shape as a glass body:
    // screen-space refraction (Render_GlassIOR <= 0 = default 1.5),
    // per-channel absorption tinted by the shape color
    // (Render_GlassDensity <= 0 = automatic) and environment
    // reflection blurred by Render_GlassRoughness.
    auto glassProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Glass"));
    bool glass = glassProp && glassProp->getValue();
    float glassIOR = glass ? floatProp("Render_GlassIOR") : 0.0f;
    float glassDensity = glass ? floatProp("Render_GlassDensity") : 0.0f;
    float glassRoughness = glass ? floatProp("Render_GlassRoughness") : 0.0f;
    // Render_Cloud turns the closed shape into a procedural-density
    // scattering medium of the volumetric lighting pass (the body
    // geometry itself is not rendered); density/detail <= 0 =
    // automatic, speed scales the drift animation.
    auto cloudProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Cloud"));
    bool cloud = cloudProp && cloudProp->getValue();
    float cloudDensity = cloud ? floatProp("Render_CloudDensity") : 0.0f;
    float cloudDetail = cloud ? floatProp("Render_CloudDetail") : 0.0f;
    float cloudSpeed = 1.0f;
    if (cloud) {
        float v = floatProp("Render_CloudSpeed");
        if (v >= 0.0f)
            cloudSpeed = v;
    }

    // Render_Fire turns the closed shape into an emissive flame medium
    // of the volumetric lighting pass (the body geometry itself is not
    // rendered); intensity <= 0 = 1, detail <= 0 = automatic, speed
    // scales the rise animation.
    auto fireProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Fire"));
    bool fire = fireProp && fireProp->getValue();
    float fireIntensity = fire ? floatProp("Render_FireIntensity") : 0.0f;
    float fireDetail = fire ? floatProp("Render_FireDetail") : 0.0f;
    float fireSpeed = 1.0f;
    if (fire) {
        float v = floatProp("Render_FireSpeed");
        if (v >= 0.0f)
            fireSpeed = v;
    }

    // Render_Fountain turns the closed shape into a water-spray
    // scattering medium of the volumetric lighting pass (a rising jet
    // plus a parabolic fall envelope; the body geometry itself is not
    // rendered); density/detail <= 0 = automatic, speed scales the
    // flow animation.
    auto fountainProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Fountain"));
    bool fountain = fountainProp && fountainProp->getValue();
    float fountainDensity =
        fountain ? floatProp("Render_FountainDensity") : 0.0f;
    float fountainDetail =
        fountain ? floatProp("Render_FountainDetail") : 0.0f;
    float fountainSpeed = 1.0f;
    if (fountain) {
        float v = floatProp("Render_FountainSpeed");
        if (v >= 0.0f)
            fountainSpeed = v;
    }

    // Render_Light renders the shape as a light-source body: unshaded
    // at its diffuse color, glowing through the render engine's bloom
    // pass and shining as an unshadowed point light on nearby lit
    // surfaces; intensity <= 0 = 1, range <= 0 = automatic (from the
    // shape bounds).
    auto lightProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_Light"));
    bool light = lightProp && lightProp->getValue();
    float lightIntensity = light ? floatProp("Render_LightIntensity") : 0.0f;
    float lightRange = light ? floatProp("Render_LightRange") : 0.0f;
    auto lightShadowProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_LightShadow"));
    bool lightShadow = light && lightShadowProp
        && lightShadowProp->getValue();
    auto lightShadowExtProp = Base::freecad_dynamic_cast<App::PropertyBool>(
            getPropertyByName("Render_LightShadowExtended"));
    bool lightShadowExt = lightShadow && lightShadowExtProp
        && lightShadowExtProp->getValue();

    if (!pbr && metallic < 0.0f && roughness < 0.0f && !finish.isSet()
            && palette.empty() && faceTextureIndices.empty()
            && !water && !glass && !cloud && !fire && !fountain && !light) {
        if (pcRenderMaterial) {
            int idx = pcRoot->findChild(pcRenderMaterial);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderMaterial->unref();
            pcRenderMaterial = nullptr;
            // The frames went with the node: the next one has to ask
            // for them again, or a finish removed and restated shades
            // triplanarly for the rest of the session.
            renderGeometryAsked = false;
        }
        return;
    }
    const bool freshNode = !pcRenderMaterial;
    if (!pcRenderMaterial) {
        pcRenderMaterial = new SoFCRenderMaterial;
        pcRenderMaterial->ref();
        pcRoot->insertChild(pcRenderMaterial, 0);
    }
    // A node that did not exist when the shape was tessellated missed
    // the projection frames, and a finish or a per-face image laid out
    // in object space needs them -- otherwise every face is projected
    // in the first face's frame. Ask the geometry to state them, once.
    if (freshNode && !renderGeometryAsked
            && (finish.isSet() || !palette.empty()
                || !faceTextureIndices.empty())
            && pcRenderMaterial->framePalette.getNum() == 0) {
        renderGeometryAsked = true;
        renderMaterialNeedsGeometry();
    }
    pcRenderMaterial->metallic = metallic;
    pcRenderMaterial->roughness = roughness;
    // Rewritten only when they really change: every write notifies, and
    // a notification off this node invalidates the render caches below.
    auto syncFactors = [](SoMFFloat &field, const std::vector<float> &values) {
        const int num = static_cast<int>(values.size());
        if (field.getNum() == num
                && (num == 0
                    || std::equal(values.begin(), values.end(),
                                  field.getValues(0))))
            return;
        field.setNum(num);   // setValues() grows but never shrinks
        if (num)
            field.setValues(0, num, values.data());
    };
    syncFactors(pcRenderMaterial->metallics, metallics);
    syncFactors(pcRenderMaterial->roughnesses, roughnesses);
    pcRenderMaterial->finish = finish.pattern;
    pcRenderMaterial->finishPitch = finish.pitch;
    pcRenderMaterial->finishDepth = finish.depth;
    pcRenderMaterial->finishAngle = finish.angle;
    // Same "only when they really change" rule as the factor arrays: a
    // write notifies, and a notification off this node invalidates the
    // render caches below it.
    auto syncPalette = [](SoMFVec4f &field, const std::vector<SbVec4f> &values) {
        const int num = static_cast<int>(values.size());
        if (field.getNum() == num
                && (num == 0
                    || std::equal(values.begin(), values.end(),
                                  field.getValues(0))))
            return;
        field.setNum(num);
        if (num)
            field.setValues(0, num, values.data());
    };
    auto syncIndices = [](SoMFInt32 &field, const std::vector<int32_t> &values) {
        const int num = static_cast<int>(values.size());
        if (field.getNum() == num
                && (num == 0
                    || std::equal(values.begin(), values.end(),
                                  field.getValues(0))))
            return;
        field.setNum(num);
        if (num)
            field.setValues(0, num, values.data());
    };
    syncPalette(pcRenderMaterial->finishPalette, palette);
    syncIndices(pcRenderMaterial->finishIndices, finishIndices);
    syncIndices(pcRenderMaterial->faceTextureIndices, faceTextureIndices);
    if (pcRenderMaterial->faceTextureScale.getValue() != faceTexScale)
        pcRenderMaterial->faceTextureScale = faceTexScale;
    pcRenderMaterial->water = water;
    pcRenderMaterial->waterDensity = waterDensity < 0.0f ? 0.0f
                                                         : waterDensity;
    pcRenderMaterial->glass = glass;
    pcRenderMaterial->glassIOR = glassIOR < 0.0f ? 0.0f : glassIOR;
    pcRenderMaterial->glassDensity = glassDensity < 0.0f ? 0.0f
                                                         : glassDensity;
    pcRenderMaterial->glassRoughness = glassRoughness < 0.0f
        ? 0.0f : glassRoughness;
    pcRenderMaterial->cloud = cloud;
    pcRenderMaterial->cloudDensity = cloudDensity < 0.0f ? 0.0f
                                                         : cloudDensity;
    pcRenderMaterial->cloudDetail = cloudDetail < 0.0f ? 0.0f
                                                       : cloudDetail;
    pcRenderMaterial->cloudSpeed = cloudSpeed;
    pcRenderMaterial->fire = fire;
    pcRenderMaterial->fireIntensity = fireIntensity < 0.0f
        ? 0.0f : fireIntensity;
    pcRenderMaterial->fireDetail = fireDetail < 0.0f ? 0.0f
                                                     : fireDetail;
    pcRenderMaterial->fireSpeed = fireSpeed;
    pcRenderMaterial->fountain = fountain;
    pcRenderMaterial->fountainDensity = fountainDensity < 0.0f
        ? 0.0f : fountainDensity;
    pcRenderMaterial->fountainDetail = fountainDetail < 0.0f
        ? 0.0f : fountainDetail;
    pcRenderMaterial->fountainSpeed = fountainSpeed;
    pcRenderMaterial->lightSource = light;
    pcRenderMaterial->lightIntensity = lightIntensity < 0.0f
        ? 0.0f : lightIntensity;
    pcRenderMaterial->lightRange = lightRange < 0.0f ? 0.0f : lightRange;
    pcRenderMaterial->lightShadow = lightShadow;
    pcRenderMaterial->lightShadowExtended = lightShadowExt;
}

void ViewProviderGeometryObject::updateRenderProperty(const char *name)
{
    if (strcmp(name, "Render_FaceTextureScale") == 0) {
        // Both: the scale is a field of the material node, and it is
        // also what decides whether the per-face images want the mesh's
        // own coordinates -- which only exist while a unit is enabled.
        updateRenderTexture();
        updateRenderMaterial();
    }
    else if (strcmp(name, "Render_BaseColorTexture") == 0
            || strcmp(name, "Render_NormalMap") == 0
            || strcmp(name, "Render_EmissiveMap") == 0
            || strcmp(name, "Render_OcclusionMap") == 0
            || strcmp(name, "Render_MetallicRoughnessMap") == 0
            || strncmp(name, "Render_Texture", 14) == 0)
        updateRenderTexture();
    else if (strcmp(name, "Render_CastShadow") == 0
            || strcmp(name, "Render_ReceiveShadow") == 0)
        updateRenderShadowStyle();
    else
        updateRenderMaterial();
}

App::Property* ViewProviderGeometryObject::addDynamicProperty(
        const char* type, const char* name, const char* group,
        const char* doc, short attr, bool ro, bool hidden)
{
    // Convention: a property named "<Prefix>_<Rest>" belongs in the
    // "<Prefix>" group. When no group is given (e.g. a Render_* property
    // added from Python or the property editor with the group omitted),
    // derive it from the name prefix so the property lands in its proper
    // group instead of the default one.
    std::string derivedGroup;
    if ((!group || !*group) && name) {
        if (const char* us = strchr(name, '_')) {
            if (us != name) {
                derivedGroup.assign(name, us);
                group = derivedGroup.c_str();
            }
        }
    }
    auto prop = inherited::addDynamicProperty(type, name, group, doc,
                                              attr, ro, hidden);
    // A freshly added Render_* property applies right away: writes of
    // the (unchanged) default value do not notify onChanged, so e.g.
    // adding Render_CastShadow (default false) must not stay inert
    // until its value is toggled. Restore resyncs in finishRestoring
    // once all values are loaded.
    if (prop && prop->getName() && !isRestoring()
            && strncmp(prop->getName(), "Render_", 7) == 0)
        updateRenderProperty(prop->getName());
    return prop;
}

bool ViewProviderGeometryObject::removeDynamicProperty(const char* name)
{
    // Like addDynamicProperty above: removal does not notify onChanged,
    // so reverting a Render_* setting (property editor or the render
    // settings panel) must resync the scene graph nodes itself.
    std::string n = name ? name : "";
    bool res = inherited::removeDynamicProperty(name);
    if (res && strncmp(n.c_str(), "Render_", 7) == 0 && !isRestoring())
        updateRenderProperty(n.c_str());
    return res;
}

void ViewProviderGeometryObject::setupContextMenu(QMenu* menu,
                                                  QObject* receiver,
                                                  const char* member)
{
    inherited::setupContextMenu(menu, receiver, member);
    // Per-object render engine settings (the optional Render_* dynamic
    // property set) via a task panel.
    auto func = new Gui::ActionFunction(menu);
    QAction *act = menu->addAction(QObject::tr("Render settings..."));
    func->trigger(act, []() {
        Gui::Control().showDialog(new TaskRenderSettings());
    });
}

void ViewProviderGeometryObject::updateRenderShadowStyle()
{
    // Render_CastShadow / Render_ReceiveShadow map onto Coin's
    // SoShadowStyle bitmask, honored by the GL Shadow draw style and by
    // the render engine's shadow pass alike (Material::shadowstyle).
    // The node only exists while a flag is off - both true is Coin's
    // default state.
    auto boolProp = [this](const char *name) -> bool {
        auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(
                getPropertyByName(name));
        return !prop || prop->getValue();
    };
    bool casts = boolProp("Render_CastShadow");
    bool receives = boolProp("Render_ReceiveShadow");

    if (casts && receives) {
        if (pcRenderShadowStyle) {
            int idx = pcRoot->findChild(pcRenderShadowStyle);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderShadowStyle->unref();
            pcRenderShadowStyle = nullptr;
        }
        return;
    }
    if (!pcRenderShadowStyle) {
        pcRenderShadowStyle = new SoShadowStyle;
        pcRenderShadowStyle->ref();
        pcRoot->insertChild(pcRenderShadowStyle, 0);
    }
    pcRenderShadowStyle->style = (casts ? SoShadowStyle::CASTS_SHADOW : 0)
        | (receives ? SoShadowStyle::SHADOWED : 0);
}

void ViewProviderGeometryObject::attach(App::DocumentObject *pcObj)
{
    ViewProviderDragger::attach(pcObj);
    // The card's look, for a FRESH object that carries one: nothing
    // announces it here the way a later card change does, and an object
    // that has never been given an appearance of its own follows its card
    // (docs/MaterialStorage.md 15.3). A restored one is not fresh, and the
    // guard inside stands this down for it.
    applyMaterialAppearance();
}

void ViewProviderGeometryObject::applyMaterialAppearance()
{
    auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
    if (!geometry) {
        return;
    }
    // The follow gates the moment a card is SET, and nothing else. A
    // restore is the file's own record landing: the appearance it states,
    // the Render_* properties it states, are what this object looks like,
    // and re-taking the card over them would overwrite a look the user
    // chose and saved (docs/MaterialStorage.md 15.3). The base is stored
    // while following, so there is nothing to re-derive here either.
    if (App::Document::isAnyRestoring()) {
        return;
    }
    const App::MaterialAppearance card = geometry->getMaterialAppearance();
    const App::MaterialAppearance none;
    if (card == none) {
        return;   // no card, or a card with nothing to say about the look
    }
    // The flag is the whole answer, and it starts TRUE -- which is what
    // makes a fresh object with a card follow it. An appearance the user
    // set outranks the card, and setting one is what ends the follow.
    if (!ShapeAppearance.isFollowingMaterial()) {
        return;
    }
    // The BASE only: the faces holding a look of their own keep it, where
    // the old whole-value write had to refuse a per-face appearance outright
    ShapeAppearance.followMaterial(card);
    // The card's MaterialX document set, if it has one, is in the store by
    // now (Materials::PropertyMaterial::setValue); the appearance takes its
    // own hold on it so the look outlives the card property's
    ShapeAppearance.holdStoredBlobs();
    // Only where the card is in control of the look: the same guard that
    // stops us overwriting a hand-picked appearance has to stop us clearing
    // a hand-set Render_Glass. A card stating none clears the previous
    // card's, which is the point of applying it even when empty.
    applyMaterialRenderProperties(this, geometry->getMaterialRenderProperties());
}

bool ViewProviderGeometryObject::canResetAppearanceToMaterial() const
{
    auto geometry = dynamic_cast<const App::GeoFeature*>(getObject());
    if (!geometry || ShapeAppearance.isFollowingMaterial()) {
        return false;
    }
    return geometry->getMaterialAppearance() != App::MaterialAppearance();
}

bool ViewProviderGeometryObject::resetAppearanceToMaterial()
{
    auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
    if (!geometry) {
        return false;
    }
    const App::MaterialAppearance card = geometry->getMaterialAppearance();
    if (card == App::MaterialAppearance()) {
        return false;   // nothing to go back to
    }
    // The BASE, and following again from now on. The faces holding a look
    // of their own keep it (docs/MaterialStorage.md 15.5).
    ShapeAppearance.followMaterial(card);
    // The card's MaterialX document set, if it has one, is in the store by
    // now (Materials::PropertyMaterial::setValue); the appearance takes its
    // own hold on it so the look outlives the card property's
    ShapeAppearance.holdStoredBlobs();
    // The card's render features come back with its colours: a Render_Glass
    // the abandoned look left behind is the card's to state again, or to
    // clear by stating none.
    applyMaterialRenderProperties(this, geometry->getMaterialRenderProperties());
    return true;
}

void ViewProviderGeometryObject::deriveFollowMaterial()
{
    if (ShapeAppearance.isFollowingMaterial()) {
        return;   // the file stated it
    }
    auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
    if (!geometry) {
        return;
    }
    const App::MaterialAppearance card = geometry->getMaterialAppearance();
    const App::MaterialAppearance none;
    if (card == none) {
        return;   // a document without a card restores not following
    }
    const App::MaterialAppearance &base = ShapeAppearance.getBase();
    if (base == card || base == none) {
        ShapeAppearance.setFollowMaterial(true);
    }
}

void ViewProviderGeometryObject::updateData(const App::Property* prop)
{
    if(prop->isDerivedFrom(App::PropertyComplexGeoData::getClassTypeId()))
        updateBoundingBox();
    else if (strcmp(prop->getName(), "ShapeMaterial") == 0) {
        // The object's material card changed. Whether that reaches the look
        // is the appearance's own flag now, where it used to be a runtime
        // member this view provider kept and never saved -- so the follow
        // survives a reopen (docs/MaterialStorage.md 15.1).
        applyMaterialAppearance();
    }

    ViewProviderDragger::updateData(prop);
}

void ViewProviderGeometryObject::updateBoundingBox() {
    if(pcBoundingBox) {
        Base::BoundBox3d box = this->getBoundingBox(0,0,false);
        if(!box.IsValid())
            return;
        pcBoundingBox->minBounds.setValue(box.MinX, box.MinY, box.MinZ);
        pcBoundingBox->maxBounds.setValue(box.MaxX, box.MaxY, box.MaxZ);
    }
}

/** Bring the compatibility names level with the appearance, after a restore
 *
 * ShapeColor, ShapeMaterial and Transparency are the single-value face of
 * ShapeAppearance: derived from it, not a second store. A document this fork
 * wrote states all four and restores each, so this finds nothing to do -- the
 * mirrors already agree, and both writes below are refused as no changes.
 *
 * A document written by a release that has only ShapeAppearance (upstream from
 * 1.0 on) states none of them, and then the mirrors are whatever the
 * constructor left: a grey object at zero transparency in the property editor,
 * over an appearance that says otherwise. The announcement the appearance
 * makes while restoring does not reach them -- measured on a 1.1 file, where
 * an appearance read from its own archive entry arrives correct and leaves
 * both stale, while the same write after the restore mirrors normally -- so
 * the derivation belongs here, once, after every value the file carries has
 * landed.
 *
 * Transparency first: the reaction to a ShapeColor change folds it into the
 * appearance's diffuse alpha, which is where this fork keeps an entry's
 * transparency, and it has to be able to read the right one.
 *
 * From the BASE, for the reason onChanged's mirror block gives: the base is
 * the object's look whether or not a face overrides it, where entry 0 of a
 * per-face appearance is one face and not the object.
 */
void ViewProviderGeometryObject::refreshAppearanceMirrors()
{
    long transparency = Base::toPercent(ShapeAppearance.getBase().transparency);
    if (transparency != Transparency.getValue()) {
        // NoModify, or a document nobody has touched opens already modified.
        Base::ObjectStatusLocker<App::Property::Status, App::Property>
                guard(App::Property::NoModify, &Transparency);
        Transparency.setValue(transparency);
    }

    // Alpha included, for the reason onChanged's mirror block gives: any
    // other alpha makes the mirror unequal to what it mirrors, and the
    // announcement pushes that difference back into the appearance.
    Base::Color color = ShapeAppearance.getBase().diffuseColor;
    if (color != ShapeColor.getValue()) {
        Base::ObjectStatusLocker<App::Property::Status, App::Property>
                guard(App::Property::NoModify, &ShapeColor);
        ShapeColor.mirrorValue(color);
    }
    App::MaterialAppearance material = ShapeAppearance.getBase();
    if (!(material == ShapeMaterial.getValue())) {
        Base::ObjectStatusLocker<App::Property::Status, App::Property>
                guard(App::Property::NoModify, &ShapeMaterial);
        ShapeMaterial.mirrorValue(material);
    }
}

bool ViewProviderGeometryObject::getFaceWeights(std::vector<double> & /*weights*/) const
{
    return false;
}

void ViewProviderGeometryObject::deriveAppearanceBase()
{
    if (ShapeAppearance.hasDerivedBase())
        return;
    // The mirror, alpha included: on a document this fork wrote it holds
    // what the object looked like before its faces were painted, and on an
    // import it holds the constructor's grey, which occurs in no imported
    // list -- so it answers the fork's own documents and declines the
    // others (docs/ShapeAppearanceDesign.md 12.4).
    Base::Color hint = ShapeColor.getValue();
    hint.setTransparency(Base::fromPercent(Transparency.getValue()));
    std::vector<double> weights;
    if (!ShapeAppearance.namesDiffuse(hint)) {
        getFaceWeights(weights);
    }
    ShapeAppearance.deriveBase(&hint, weights.empty() ? nullptr : &weights);
}

void ViewProviderGeometryObject::finishRestoring()
{
    deriveAppearanceBase();
    // Only the flag, never the card's look: the flag decides what the NEXT
    // card set does, while what this object looks like now is what the file
    // said it looks like (docs/MaterialStorage.md 15.3).
    deriveFollowMaterial();
    refreshAppearanceMirrors();
    updateBoundingBox();
    // Restored Render_* dynamic properties (per-object render engine
    // settings) need their scene graph nodes rebuilt.
    updateRenderMaterial();
    updateRenderTexture();
    updateRenderShadowStyle();
    inherited::finishRestoring();
}

SoPickedPointList ViewProviderGeometryObject::getPickedPoints(const SbVec2s& pos, const View3DInventorViewer& viewer,bool pickAll) const
{
    auto root = new SoSeparator;
    root->ref();
    root->addChild(viewer.getHeadlight());
    root->addChild(viewer.getSoRenderManager()->getCamera());
    root->addChild(getRoot());

    SoRayPickAction rp(viewer.getSoRenderManager()->getViewportRegion());
    rp.setPickAll(pickAll);
    rp.setRadius(viewer.getPickRadius());
    rp.setPoint(pos);
    rp.apply(root);
    root->unref();

    // returns a copy of the list
    return rp.getPickedPointList();
}

SoPickedPoint* ViewProviderGeometryObject::getPickedPoint(const SbVec2s& pos, const View3DInventorViewer& viewer) const
{
    auto root = new SoSeparator;
    root->ref();
    root->addChild(viewer.getHeadlight());
    root->addChild(viewer.getSoRenderManager()->getCamera());
    root->addChild(getRoot());

    SoRayPickAction rp(viewer.getSoRenderManager()->getViewportRegion());
    rp.setPoint(pos);
    rp.setRadius(viewer.getPickRadius());
    rp.apply(root);
    root->unref();

    // returns a copy of the point
    SoPickedPoint* pick = rp.getPickedPoint();
    //return (pick ? pick->copy() : 0); // needs the same instance of CRT under MS Windows
    return (pick ? new SoPickedPoint(*pick) : nullptr);
}

unsigned long ViewProviderGeometryObject::getBoundColor() const
{
    return ViewParams::getBoundingBoxColor();
}

void ViewProviderGeometryObject::addBoundSwitch() {
    if(!pcBoundSwitch)
        return;

    if(pcModeSwitch->isOfType(SoFCSwitch::getClassTypeId())) {
        if(pcModeSwitch->findChild(pcBoundSwitch)>=0)
            return;
        pcModeSwitch->addChild(pcBoundSwitch);
        // SoFCSwitch::tailChild is shown together with whichChild as long as
        // whichChild is not -1. Put the bound box there is better then putting
        // as the last node in all mode group node.  For example,
        // Arch.BuildingPart puts a SoTransform inside its mode group, which
        // messes up the bound box display.
        //
        // It is also better than putting the bound switch outside of mode
        // switch like before, because we can easily hide the bound box together
        // with the object, and also good for Link as it won't show the bound
        // box
        static_cast<SoFCSwitch*>(pcModeSwitch)->tailChild = pcModeSwitch->getNumChildren()-1;
        return;
    }

    for(int i=0;i<pcModeSwitch->getNumChildren();++i) {
        auto node = pcModeSwitch->getChild(i);
        if(!node->isOfType(SoGroup::getClassTypeId()))
            continue;
        auto group = static_cast<SoGroup*>(node);
        int idx = group->findChild(pcBoundSwitch);
        if(idx >= 0) {
            // make sure we are added last
            if(idx == group->getNumChildren()-1)
                continue;
            group->removeChild(idx);
        }
        group->addChild(pcBoundSwitch);
    }
}

namespace {
float getBoundBoxFontSize()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    return hGrp->GetFloat("BoundingBoxFontSize", 10.0);
}
}

void ViewProviderGeometryObject::showBoundingBox(bool show)
{
    if (!pcBoundSwitch && show) {
        unsigned long bbcol = getBoundColor();
        float r,g,b;
        r = ((bbcol >> 24) & 0xff) / 255.0; g = ((bbcol >> 16) & 0xff) / 255.0; b = ((bbcol >> 8) & 0xff) / 255.0;

        pcBoundSwitch = new SoSwitch();
        pcBoundSwitch->ref();
        auto pBoundingSep = new SoSeparator();
        auto lineStyle = new SoDrawStyle;
        lineStyle->lineWidth = 2.0f;
        pBoundingSep->addChild(lineStyle);

        if(!pcBoundColor) {
            pcBoundColor = new SoBaseColor;
            pcBoundColor->ref();
        }
        pcBoundColor->rgb.setValue(r, g, b);
        pBoundingSep->addChild(pcBoundColor);
        auto font = new SoFont();
        font->size.setValue(getBoundBoxFontSize());
        pBoundingSep->addChild(font);

        if(!pcBoundingBox) {
            pcBoundingBox = new SoFCBoundingBox;
            pcBoundingBox->ref();
        }
        pBoundingSep->addChild(pcBoundingBox);
        pcBoundingBox->coordsOn.setValue(false);
        pcBoundingBox->dimensionsOn.setValue(true);

        // add to the highlight node
        pcBoundSwitch->addChild(pBoundingSep);

        updateBoundingBox();

        addBoundSwitch();
        pcSwitchSensor = new SoNodeSensor;
        pcSwitchSensor->setData(this);
        pcSwitchSensor->attach(pcModeSwitch);
        pcSwitchSensor->setFunction([](void *data, SoSensor*) {
            reinterpret_cast<ViewProviderGeometryObject*>(data)->addBoundSwitch();
        });
    }

    if (pcBoundSwitch) {
        pcBoundSwitch->whichChild = (show ? 0 : -1);
    }
}


bool Gui::applyMaterialRenderProperties(App::PropertyContainer *vp,
                                        const App::MaterialRenderProperties &props)
{
    if (!vp) {
        return false;
    }

    // Every property a card could have stated, so the ones it did NOT
    // state are removed rather than left behind from a previous card.
    // Grouped by feature: naming any property of a feature keeps that
    // feature, naming none clears it whole, which is the same
    // all-or-nothing the Render Settings panel applies.
    static const char *glassProps[] = {
        "Render_Glass", "Render_GlassIOR", "Render_GlassDensity",
        "Render_GlassRoughness",
    };

    bool changed = false;
    for (const char *name : glassProps) {
        auto it = std::find_if(props.begin(), props.end(),
                [name](const App::MaterialRenderProperty &p) {
                    return p.name == name;
                });
        if (it == props.end()) {
            if (vp->getPropertyByName(name)) {
                removeRenderProperty(vp, name);
                changed = true;
            }
            continue;
        }
        // A property that did not exist is a change even when the
        // stated value is the type's default (Render_Glass false).
        const bool created = !vp->getPropertyByName(name);
        if (it->boolean) {
            auto prop = ensureRenderProperty<App::PropertyBool>(
                    vp, "App::PropertyBool", name,
                    "Render the closed shape as a glass body of the render "
                    "engine (refraction, absorption, reflection)");
            bool value = it->value != 0.0;
            if (prop && prop->getValue() != value) {
                prop->setValue(value);
                changed = true;
            }
        }
        else {
            auto prop = ensureRenderProperty<App::PropertyFloat>(
                    vp, "App::PropertyFloat", name, "Stated by the material");
            if (prop && prop->getValue() != it->value) {
                prop->setValue(it->value);
                changed = true;
            }
        }
        if (created) {
            changed = true;
        }
    }

    return changed;
}
