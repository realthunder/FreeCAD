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

#include <Base/Reader.h>
#include <Base/Tools.h>

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
    App::Material mat(App::Material::DEFAULT);
    mat.diffuseColor.set(r, g, b);
    mat.transparency = Base::fromPercent(initialTransparency);
    ADD_PROPERTY_TYPE(ShapeAppearance, (mat), osgroup, App::Prop_None, "Shape appearance");
    ADD_PROPERTY_TYPE(ShapeMaterial, (mat), osgroup, App::Prop_None, "Shape material");
    // Retired as a store; kept only so old macros and old documents still
    // land somewhere. One datum should not be two rows in the editor.
    ShapeMaterial.setStatus(App::Property::Hidden, true);
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
        const Base::Color &c = ShapeColor.getValue();
        pcShapeMaterial->diffuseColor.setValue(c.r, c.g, c.b);
        if (c != ShapeAppearance.getDiffuseColor(0))
            ShapeAppearance.setDiffuseColor(c);
    }
    else if (prop == &ShapeMaterial) {
        const App::Material &mat = ShapeMaterial.getValue();
        setCoinAppearance(mat);
        if (!(mat == ShapeAppearance.getMaterial(0)))
            ShapeAppearance.setValue(mat);
    }
    else if (prop == &Transparency) {
        long value = Base::toPercent(ShapeAppearance.getTransparency(0));
        if (value != Transparency.getValue()) {
            float trans = Base::fromPercent(Transparency.getValue());
            pcShapeMaterial->transparency = trans;
            ShapeAppearance.setTransparency(trans);
        }
    }
    else if (prop == &ShapeAppearance) {
        if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange))
            getObject()->touch(true);
        long value = Base::toPercent(ShapeAppearance.getTransparency(0));
        if (value != Transparency.getValue())
            Transparency.setValue(value);
        // Only a single appearance can be pushed into the one Coin material
        // node; a per-face list is carried by the shape's own material arrays
        // (ViewProviderPartExt), so leave the node alone in that case.
        if (ShapeAppearance.getSize() == 1)
            setCoinAppearance(ShapeAppearance[0]);
        // Refresh the two compatibility names off the appearance. mirrorValue,
        // not setValue: these read from the store they would otherwise write
        // straight back into.
        ShapeColor.mirrorValue(ShapeAppearance.getDiffuseColor(0));
        ShapeMaterial.mirrorValue(ShapeAppearance.getMaterial(0));
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

TYPESYSTEM_SOURCE(Gui::PropertyShapeColor, App::PropertyColor)
TYPESYSTEM_SOURCE(Gui::PropertyShapeMaterial, App::PropertyMaterial)

void PropertyShapeColor::setValue(const Base::Color &col)
{
    App::PropertyColor::setValue(col);
    if (_appearance)
        _appearance->setDiffuseColor(col);
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
    // A per-face appearance is the more specific value and already restored:
    // leave it alone rather than collapse or overwrite its first entry.
    if (_appearance && _appearance->getDiffuseColors().size() <= 1)
        _appearance->setDiffuseColor(getValue());
}

void PropertyShapeColor::Restore(Base::XMLReader &reader)
{
    App::PropertyColor::Restore(reader);
    applyToAppearance();
}

void PropertyShapeMaterial::setValue(const App::Material &mat)
{
    App::PropertyMaterial::setValue(mat);
    if (_appearance)
        _appearance->setValue(mat);
}

void PropertyShapeMaterial::applyToAppearance()
{
    if (_appearance && _appearance->getSize() <= 1)
        _appearance->setValue(getValue());
}

void PropertyShapeMaterial::Restore(Base::XMLReader &reader)
{
    App::PropertyMaterial::Restore(reader);
    applyToAppearance();
}

void ViewProviderGeometryObject::setCoinAppearance(const App::Material &mat)
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
            && strcmp(TypeName, App::PropertyMaterial::getClassTypeId().getName()) == 0) {
        App::PropertyMaterial old;
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

} // anonymous namespace

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
    bool wantTexture = (color && color[0]) || (bump && bump[0])
        || (emissive && emissive[0]) || (occlusion && occlusion[0])
        || (metallicroughness && metallicroughness[0]);
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

void ViewProviderGeometryObject::updateRenderMaterial()
{
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

    if (metallic < 0.0f && roughness < 0.0f && !water && !glass
            && !cloud && !fire && !fountain && !light) {
        if (pcRenderMaterial) {
            int idx = pcRoot->findChild(pcRenderMaterial);
            if (idx >= 0)
                pcRoot->removeChild(idx);
            pcRenderMaterial->unref();
            pcRenderMaterial = nullptr;
        }
        return;
    }
    if (!pcRenderMaterial) {
        pcRenderMaterial = new SoFCRenderMaterial;
        pcRenderMaterial->ref();
        pcRoot->insertChild(pcRenderMaterial, 0);
    }
    pcRenderMaterial->metallic = metallic;
    pcRenderMaterial->roughness = roughness;
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
    if (strcmp(name, "Render_BaseColorTexture") == 0
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
}

void ViewProviderGeometryObject::updateData(const App::Property* prop)
{
    if(prop->isDerivedFrom(App::PropertyComplexGeoData::getClassTypeId()))
        updateBoundingBox();

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

void ViewProviderGeometryObject::finishRestoring()
{
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
