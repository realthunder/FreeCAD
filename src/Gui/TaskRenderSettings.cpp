/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef _PreComp_
# include <QCheckBox>
# include <QDoubleSpinBox>
# include <QGridLayout>
# include <QLabel>
# include <QLineEdit>
# include <QPushButton>
#endif

#include <App/PropertyFile.h>
#include <App/PropertyGeo.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "TaskRenderSettings.h"
#include "Application.h"
#include "Document.h"
#include "FileDialog.h"
#include "Selection.h"
#include "ViewProviderGeometryObject.h"

using namespace Gui;

namespace {

// The Render_* dynamic properties are optional: a row is "overridden"
// when its property exists on the view provider. ensureProp creates a
// missing one (ViewProviderGeometryObject::addDynamicProperty applies
// fresh properties immediately).
template<class PropT>
PropT *getProp(ViewProviderGeometryObject *vp, const char *name)
{
    return Base::freecad_dynamic_cast<PropT>(vp->getPropertyByName(name));
}

template<class PropT>
PropT *ensureProp(ViewProviderGeometryObject *vp, const char *type,
                  const char *name, const char *doc)
{
    if (auto prop = getProp<PropT>(vp, name))
        return prop;
    return Base::freecad_dynamic_cast<PropT>(
            vp->addDynamicProperty(type, name, "Render", doc));
}

void removeProp(ViewProviderGeometryObject *vp, const char *name)
{
    if (vp->getPropertyByName(name))
        vp->removeDynamicProperty(name);
}


} // anonymous namespace

/* TRANSLATOR Gui::RenderSettingsWidget */

RenderSettingsWidget::RenderSettingsWidget(
        std::vector<ViewProviderGeometryObject*> &&vps)
    : vps(std::move(vps))
{
    setWindowTitle(tr("Render settings"));

    auto grid = new QGridLayout(this);
    int row = 0;

    auto addOverride = [&](const QString &text) {
        auto check = new QCheckBox(text, this);
        grid->addWidget(check, row, 0);
        return check;
    };
    auto addSpin = [&](int col, double min, double max, double step,
                       int colspan = 1) {
        auto spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(3);
        grid->addWidget(spin, row, col, 1, colspan);
        return spin;
    };

    // Water body of the volumetric lighting pass.
    waterCheck = addOverride(tr("Water body"));
    waterDensitySpin = addSpin(1, 0.0, 1.0e6, 0.01, 2);
    waterDensitySpin->setSpecialValueText(tr("auto density"));
    waterDensitySpin->setToolTip(
        tr("Extinction density in inverse model units; 0 = automatic "
           "(from the body size)"));
    ++row;

    // Glass body: refraction + absorption + reflection of the render
    // engine.
    glassCheck = addOverride(tr("Glass body"));
    glassIORSpin = addSpin(1, 1.0, 5.0, 0.05, 2);
    glassIORSpin->setPrefix(tr("IOR "));
    glassIORSpin->setValue(1.5);
    glassIORSpin->setToolTip(tr("Index of refraction"));
    glassDensitySpin = addSpin(2, 0.0, 1.0e6, 0.01, 2);
    glassDensitySpin->setSpecialValueText(tr("auto density"));
    glassDensitySpin->setToolTip(
        tr("Absorption density in inverse model units; 0 = automatic "
           "(from the body size)"));
    ++row;
    glassRoughSpin = addSpin(1, 0.0, 1.0, 0.05, 2);
    glassRoughSpin->setPrefix(tr("roughness "));
    glassRoughSpin->setToolTip(
        tr("Glass surface roughness (blurs the reflection)"));
    ++row;

    // Cloud body of the volumetric lighting pass.
    cloudCheck = addOverride(tr("Cloud body"));
    cloudDensitySpin = addSpin(1, 0.0, 1.0e6, 0.01, 2);
    cloudDensitySpin->setSpecialValueText(tr("auto density"));
    cloudDensitySpin->setToolTip(
        tr("Cloud extinction density in inverse model units; 0 = "
           "automatic (from the body size)"));
    cloudDetailSpin = addSpin(2, 0.0, 1.0e6, 0.01, 2);
    cloudDetailSpin->setSpecialValueText(tr("auto detail"));
    cloudDetailSpin->setToolTip(
        tr("Noise detail scale in inverse model units; 0 = automatic"));
    ++row;
    cloudSpeedSpin = addSpin(1, 0.0, 100.0, 0.1, 2);
    cloudSpeedSpin->setPrefix(tr("drift "));
    cloudSpeedSpin->setValue(1.0);
    cloudSpeedSpin->setToolTip(tr("Drift animation speed multiplier"));
    ++row;

    // Fire body of the volumetric lighting pass.
    fireCheck = addOverride(tr("Fire body"));
    fireIntensitySpin = addSpin(1, 0.0, 1.0e6, 0.1, 2);
    fireIntensitySpin->setSpecialValueText(tr("auto intensity"));
    fireIntensitySpin->setToolTip(
        tr("Flame brightness multiplier; 0 = default (1)"));
    fireDetailSpin = addSpin(2, 0.0, 1.0e6, 0.01, 2);
    fireDetailSpin->setSpecialValueText(tr("auto detail"));
    fireDetailSpin->setToolTip(
        tr("Noise detail scale in inverse model units; 0 = automatic"));
    ++row;
    fireSpeedSpin = addSpin(1, 0.0, 100.0, 0.1, 2);
    fireSpeedSpin->setPrefix(tr("rise "));
    fireSpeedSpin->setValue(1.0);
    fireSpeedSpin->setToolTip(tr("Rise animation speed multiplier"));
    ++row;

    // Fountain body of the volumetric lighting pass.
    fountainCheck = addOverride(tr("Fountain body"));
    fountainDensitySpin = addSpin(1, 0.0, 1.0e6, 0.1, 2);
    fountainDensitySpin->setSpecialValueText(tr("auto density"));
    fountainDensitySpin->setToolTip(
        tr("Spray density in inverse model units; 0 = automatic"));
    fountainDetailSpin = addSpin(2, 0.0, 1.0e6, 0.01, 2);
    fountainDetailSpin->setSpecialValueText(tr("auto detail"));
    fountainDetailSpin->setToolTip(
        tr("Noise detail scale in inverse model units; 0 = automatic"));
    ++row;
    fountainSpeedSpin = addSpin(1, 0.0, 100.0, 0.1, 2);
    fountainSpeedSpin->setPrefix(tr("flow "));
    fountainSpeedSpin->setValue(1.0);
    fountainSpeedSpin->setToolTip(tr("Flow animation speed multiplier"));
    ++row;

    // Light-source body: unshaded emitter + bloom + unshadowed point
    // light.
    lightCheck = addOverride(tr("Light source"));
    lightIntensitySpin = addSpin(1, 0.0, 1.0e6, 0.5, 2);
    lightIntensitySpin->setSpecialValueText(tr("auto intensity"));
    lightIntensitySpin->setToolTip(
        tr("Emission strength (bloom halo and point-light brightness); "
           "0 = 1"));
    lightRangeSpin = addSpin(2, 0.0, 1.0e6, 1.0, 1);
    lightRangeSpin->setSpecialValueText(tr("auto range"));
    lightRangeSpin->setToolTip(
        tr("Point-light range in model units; 0 = automatic from the "
           "shape size"));
    ++row;
    lightShadowCheck = new QCheckBox(tr("    cast light shadows"), this);
    lightShadowCheck->setToolTip(
        tr("The light casts shadows (a cached shadow-map tile; costs a "
           "scene depth render when the model or light changes)"));
    grid->addWidget(lightShadowCheck, row, 0, 1, 3);
    ++row;
    lightShadowExtCheck = new QCheckBox(
        tr("    extended (all directions)"), this);
    lightShadowExtCheck->setToolTip(
        tr("Shadow in every direction from the light (six cube-face "
           "tiles instead of one downward cone; costs up to six tile "
           "renders when the model or light changes)"));
    grid->addWidget(lightShadowExtCheck, row, 0, 1, 3);
    ++row;

    // Texture images (embedded into the document by
    // App::PropertyFileIncluded).
    auto addFile = [&](const QString &text, QCheckBox *&check,
                       QLineEdit *&edit) {
        check = addOverride(text);
        edit = new QLineEdit(this);
        grid->addWidget(edit, row, 1);
        auto button = new QPushButton(QStringLiteral("..."), this);
        button->setMaximumWidth(30);
        grid->addWidget(button, row, 2);
        connect(button, &QPushButton::clicked,
                this, [this, edit = edit]() { browse(edit); });
        ++row;
    };
    addFile(tr("Base color texture"), baseColorCheck, baseColorEdit);
    addFile(tr("Normal map"), normalMapCheck, normalMapEdit);
    addFile(tr("Emissive map"), emissiveMapCheck, emissiveMapEdit);
    addFile(tr("Occlusion map"), occlusionMapCheck, occlusionMapEdit);
    addFile(tr("Metallic-roughness map"), metallicRoughnessMapCheck,
            metallicRoughnessMapEdit);

    texScaleCheck = addOverride(tr("Texture scale"));
    texScaleX = addSpin(1, -1.0e4, 1.0e4, 0.1);
    texScaleY = addSpin(2, -1.0e4, 1.0e4, 0.1);
    texScaleX->setValue(1.0);
    texScaleY->setValue(1.0);
    ++row;
    texOffsetCheck = addOverride(tr("Texture offset"));
    texOffsetX = addSpin(1, -1.0e4, 1.0e4, 0.05);
    texOffsetY = addSpin(2, -1.0e4, 1.0e4, 0.05);
    ++row;
    texRotationCheck = addOverride(tr("Texture rotation"));
    texRotationSpin = addSpin(1, -360.0, 360.0, 5.0, 2);
    texRotationSpin->setSuffix(QStringLiteral(" \xc2\xb0"));
    ++row;

    // Shadow participation; the properties only materialize for the
    // non-default (off) states.
    castShadowCheck = new QCheckBox(tr("Cast shadows"), this);
    castShadowCheck->setChecked(true);
    grid->addWidget(castShadowCheck, row, 0, 1, 3);
    ++row;
    receiveShadowCheck = new QCheckBox(tr("Receive shadows"), this);
    receiveShadowCheck->setChecked(true);
    grid->addWidget(receiveShadowCheck, row, 0, 1, 3);
    ++row;

    if (this->vps.size() > 1) {
        auto note = new QLabel(
            tr("%1 objects selected; applying replaces the settings "
               "of all of them.").arg(this->vps.size()), this);
        note->setWordWrap(true);
        grid->addWidget(note, row, 0, 1, 3);
        ++row;
    }

    // Enable the value editors only while their override is on.
    auto enables = [](QCheckBox *check,
                      std::initializer_list<QWidget*> widgets) {
        for (auto w : widgets) {
            w->setEnabled(check->isChecked());
            connect(check, &QCheckBox::toggled, w, &QWidget::setEnabled);
        }
    };
    load();
    enables(waterCheck, {waterDensitySpin});
    enables(glassCheck, {glassIORSpin, glassDensitySpin, glassRoughSpin});
    enables(cloudCheck, {cloudDensitySpin, cloudDetailSpin,
                         cloudSpeedSpin});
    enables(fireCheck, {fireIntensitySpin, fireDetailSpin,
                        fireSpeedSpin});
    enables(fountainCheck, {fountainDensitySpin, fountainDetailSpin,
                            fountainSpeedSpin});
    enables(lightCheck, {lightIntensitySpin, lightRangeSpin,
                         lightShadowCheck});
    enables(lightShadowCheck, {lightShadowExtCheck});
    enables(baseColorCheck, {baseColorEdit});
    enables(normalMapCheck, {normalMapEdit});
    enables(emissiveMapCheck, {emissiveMapEdit});
    enables(occlusionMapCheck, {occlusionMapEdit});
    enables(metallicRoughnessMapCheck, {metallicRoughnessMapEdit});
    enables(texScaleCheck, {texScaleX, texScaleY});
    enables(texOffsetCheck, {texOffsetX, texOffsetY});
    enables(texRotationCheck, {texRotationSpin});
}

void RenderSettingsWidget::browse(QLineEdit *edit)
{
    QString file = FileDialog::getOpenFileName(this,
        tr("Select image file"), edit->text(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;"
           "All files (*)"));
    if (!file.isEmpty())
        edit->setText(file);
}

void RenderSettingsWidget::load()
{
    // Initialize from the first selected object's current properties.
    if (vps.empty())
        return;
    auto vp = vps.front();

    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Water")) {
        waterCheck->setChecked(prop->getValue());
        if (auto dens = getProp<App::PropertyFloat>(
                    vp, "Render_WaterDensity"))
            waterDensitySpin->setValue(dens->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Glass")) {
        glassCheck->setChecked(prop->getValue());
        if (auto ior = getProp<App::PropertyFloat>(
                    vp, "Render_GlassIOR"))
            glassIORSpin->setValue(ior->getValue());
        if (auto dens = getProp<App::PropertyFloat>(
                    vp, "Render_GlassDensity"))
            glassDensitySpin->setValue(dens->getValue());
        if (auto rough = getProp<App::PropertyFloat>(
                    vp, "Render_GlassRoughness"))
            glassRoughSpin->setValue(rough->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Cloud")) {
        cloudCheck->setChecked(prop->getValue());
        if (auto dens = getProp<App::PropertyFloat>(
                    vp, "Render_CloudDensity"))
            cloudDensitySpin->setValue(dens->getValue());
        if (auto det = getProp<App::PropertyFloat>(
                    vp, "Render_CloudDetail"))
            cloudDetailSpin->setValue(det->getValue());
        if (auto spd = getProp<App::PropertyFloat>(
                    vp, "Render_CloudSpeed"))
            cloudSpeedSpin->setValue(spd->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Fire")) {
        fireCheck->setChecked(prop->getValue());
        if (auto inten = getProp<App::PropertyFloat>(
                    vp, "Render_FireIntensity"))
            fireIntensitySpin->setValue(inten->getValue());
        if (auto det = getProp<App::PropertyFloat>(
                    vp, "Render_FireDetail"))
            fireDetailSpin->setValue(det->getValue());
        if (auto spd = getProp<App::PropertyFloat>(
                    vp, "Render_FireSpeed"))
            fireSpeedSpin->setValue(spd->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Fountain")) {
        fountainCheck->setChecked(prop->getValue());
        if (auto dens = getProp<App::PropertyFloat>(
                    vp, "Render_FountainDensity"))
            fountainDensitySpin->setValue(dens->getValue());
        if (auto det = getProp<App::PropertyFloat>(
                    vp, "Render_FountainDetail"))
            fountainDetailSpin->setValue(det->getValue());
        if (auto spd = getProp<App::PropertyFloat>(
                    vp, "Render_FountainSpeed"))
            fountainSpeedSpin->setValue(spd->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Light")) {
        lightCheck->setChecked(prop->getValue());
        if (auto inten = getProp<App::PropertyFloat>(
                    vp, "Render_LightIntensity"))
            lightIntensitySpin->setValue(inten->getValue());
        if (auto rng = getProp<App::PropertyFloat>(
                    vp, "Render_LightRange"))
            lightRangeSpin->setValue(rng->getValue());
        if (auto sh = getProp<App::PropertyBool>(
                    vp, "Render_LightShadow"))
            lightShadowCheck->setChecked(sh->getValue());
        if (auto ext = getProp<App::PropertyBool>(
                    vp, "Render_LightShadowExtended"))
            lightShadowExtCheck->setChecked(ext->getValue());
    }
    if (auto prop = getProp<App::PropertyFileIncluded>(
                vp, "Render_BaseColorTexture")) {
        baseColorCheck->setChecked(true);
        baseColorEdit->setText(QString::fromUtf8(prop->getValue()));
    }
    if (auto prop = getProp<App::PropertyFileIncluded>(
                vp, "Render_NormalMap")) {
        normalMapCheck->setChecked(true);
        normalMapEdit->setText(QString::fromUtf8(prop->getValue()));
    }
    if (auto prop = getProp<App::PropertyFileIncluded>(
                vp, "Render_EmissiveMap")) {
        emissiveMapCheck->setChecked(true);
        emissiveMapEdit->setText(QString::fromUtf8(prop->getValue()));
    }
    if (auto prop = getProp<App::PropertyFileIncluded>(
                vp, "Render_OcclusionMap")) {
        occlusionMapCheck->setChecked(true);
        occlusionMapEdit->setText(QString::fromUtf8(prop->getValue()));
    }
    if (auto prop = getProp<App::PropertyFileIncluded>(
                vp, "Render_MetallicRoughnessMap")) {
        metallicRoughnessMapCheck->setChecked(true);
        metallicRoughnessMapEdit->setText(
            QString::fromUtf8(prop->getValue()));
    }
    if (auto prop = getProp<App::PropertyVector>(
                vp, "Render_TextureScale")) {
        texScaleCheck->setChecked(true);
        texScaleX->setValue(prop->getValue().x);
        texScaleY->setValue(prop->getValue().y);
    }
    if (auto prop = getProp<App::PropertyVector>(
                vp, "Render_TextureOffset")) {
        texOffsetCheck->setChecked(true);
        texOffsetX->setValue(prop->getValue().x);
        texOffsetY->setValue(prop->getValue().y);
    }
    if (auto prop = getProp<App::PropertyAngle>(
                vp, "Render_TextureRotation")) {
        texRotationCheck->setChecked(true);
        texRotationSpin->setValue(prop->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_CastShadow"))
        castShadowCheck->setChecked(prop->getValue());
    if (auto prop = getProp<App::PropertyBool>(
                vp, "Render_ReceiveShadow"))
        receiveShadowCheck->setChecked(prop->getValue());
}

void RenderSettingsWidget::apply(ViewProviderGeometryObject *vp)
{
    // Water body flag + optional density.
    if (waterCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Water",
                    "Render the closed shape as a water body of the "
                    "render engine's volumetric lighting"))
            prop->setValue(true);
        if (waterDensitySpin->value() > 0.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat", "Render_WaterDensity",
                        "Water extinction density in inverse model "
                        "units; 0 = automatic"))
                prop->setValue(waterDensitySpin->value());
        }
        else {
            removeProp(vp, "Render_WaterDensity");
        }
    }
    else {
        removeProp(vp, "Render_Water");
        removeProp(vp, "Render_WaterDensity");
    }

    // Glass body flag + optional IOR/density/roughness.
    if (glassCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Glass",
                    "Render the closed shape as a glass body of the "
                    "render engine (refraction, absorption, "
                    "reflection)"))
            prop->setValue(true);
        if (auto prop = ensureProp<App::PropertyFloat>(
                    vp, "App::PropertyFloat", "Render_GlassIOR",
                    "Glass index of refraction; 0 = default (1.5)"))
            prop->setValue(glassIORSpin->value());
        if (glassDensitySpin->value() > 0.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat", "Render_GlassDensity",
                        "Glass absorption density in inverse model "
                        "units; 0 = automatic"))
                prop->setValue(glassDensitySpin->value());
        }
        else {
            removeProp(vp, "Render_GlassDensity");
        }
        if (glassRoughSpin->value() > 0.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat",
                        "Render_GlassRoughness",
                        "Glass surface roughness (blurs the "
                        "reflection)"))
                prop->setValue(glassRoughSpin->value());
        }
        else {
            removeProp(vp, "Render_GlassRoughness");
        }
    }
    else {
        removeProp(vp, "Render_Glass");
        removeProp(vp, "Render_GlassIOR");
        removeProp(vp, "Render_GlassDensity");
        removeProp(vp, "Render_GlassRoughness");
    }

    // Cloud body flag + optional density/detail/speed.
    if (cloudCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Cloud",
                    "Render the closed shape as a cloud body of the "
                    "render engine's volumetric lighting"))
            prop->setValue(true);
        auto applyOptional = [vp](double value, const char *name,
                                  const char *doc) {
            if (value > 0.0) {
                if (auto prop = ensureProp<App::PropertyFloat>(
                            vp, "App::PropertyFloat", name, doc))
                    prop->setValue(value);
            }
            else {
                removeProp(vp, name);
            }
        };
        applyOptional(cloudDensitySpin->value(), "Render_CloudDensity",
                      "Cloud extinction density in inverse model "
                      "units; 0 = automatic");
        applyOptional(cloudDetailSpin->value(), "Render_CloudDetail",
                      "Cloud noise detail scale in inverse model "
                      "units; 0 = automatic");
        if (cloudSpeedSpin->value() != 1.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat", "Render_CloudSpeed",
                        "Cloud drift animation speed multiplier"))
                prop->setValue(cloudSpeedSpin->value());
        }
        else {
            removeProp(vp, "Render_CloudSpeed");
        }
    }
    else {
        removeProp(vp, "Render_Cloud");
        removeProp(vp, "Render_CloudDensity");
        removeProp(vp, "Render_CloudDetail");
        removeProp(vp, "Render_CloudSpeed");
    }

    // Fire body flag + optional intensity/detail/speed.
    if (fireCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Fire",
                    "Render the closed shape as a fire body of the "
                    "render engine's volumetric lighting"))
            prop->setValue(true);
        auto applyOptional = [vp](double value, const char *name,
                                  const char *doc) {
            if (value > 0.0) {
                if (auto prop = ensureProp<App::PropertyFloat>(
                            vp, "App::PropertyFloat", name, doc))
                    prop->setValue(value);
            }
            else {
                removeProp(vp, name);
            }
        };
        applyOptional(fireIntensitySpin->value(),
                      "Render_FireIntensity",
                      "Flame brightness multiplier; 0 = default (1)");
        applyOptional(fireDetailSpin->value(), "Render_FireDetail",
                      "Fire noise detail scale in inverse model "
                      "units; 0 = automatic");
        if (fireSpeedSpin->value() != 1.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat", "Render_FireSpeed",
                        "Fire rise animation speed multiplier"))
                prop->setValue(fireSpeedSpin->value());
        }
        else {
            removeProp(vp, "Render_FireSpeed");
        }
    }
    else {
        removeProp(vp, "Render_Fire");
        removeProp(vp, "Render_FireIntensity");
        removeProp(vp, "Render_FireDetail");
        removeProp(vp, "Render_FireSpeed");
    }

    // Fountain body flag + optional density/detail/speed.
    if (fountainCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Fountain",
                    "Render the closed shape as a fountain spray body "
                    "of the render engine's volumetric lighting"))
            prop->setValue(true);
        auto applyOptional = [vp](double value, const char *name,
                                  const char *doc) {
            if (value > 0.0) {
                if (auto prop = ensureProp<App::PropertyFloat>(
                            vp, "App::PropertyFloat", name, doc))
                    prop->setValue(value);
            }
            else {
                removeProp(vp, name);
            }
        };
        applyOptional(fountainDensitySpin->value(),
                      "Render_FountainDensity",
                      "Spray density in inverse model units; "
                      "0 = automatic");
        applyOptional(fountainDetailSpin->value(),
                      "Render_FountainDetail",
                      "Fountain noise detail scale in inverse model "
                      "units; 0 = automatic");
        if (fountainSpeedSpin->value() != 1.0) {
            if (auto prop = ensureProp<App::PropertyFloat>(
                        vp, "App::PropertyFloat",
                        "Render_FountainSpeed",
                        "Fountain flow animation speed multiplier"))
                prop->setValue(fountainSpeedSpin->value());
        }
        else {
            removeProp(vp, "Render_FountainSpeed");
        }
    }
    else {
        removeProp(vp, "Render_Fountain");
        removeProp(vp, "Render_FountainDensity");
        removeProp(vp, "Render_FountainDetail");
        removeProp(vp, "Render_FountainSpeed");
    }

    // Light-source body flag + optional intensity/range.
    if (lightCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", "Render_Light",
                    "Render the shape as a light-source body: unshaded "
                    "emitter with a bloom halo, shining as an "
                    "unshadowed point light on nearby surfaces"))
            prop->setValue(true);
        auto applyOptional = [vp](double value, const char *name,
                                  const char *doc) {
            if (value > 0.0) {
                if (auto prop = ensureProp<App::PropertyFloat>(
                            vp, "App::PropertyFloat", name, doc))
                    prop->setValue(value);
            }
            else {
                removeProp(vp, name);
            }
        };
        applyOptional(lightIntensitySpin->value(),
                      "Render_LightIntensity",
                      "Emission strength (bloom halo and point-light "
                      "brightness); 0 = 1");
        applyOptional(lightRangeSpin->value(), "Render_LightRange",
                      "Point-light range in model units; 0 = automatic "
                      "from the shape size");
        if (lightShadowCheck->isChecked()) {
            if (auto prop = ensureProp<App::PropertyBool>(
                        vp, "App::PropertyBool", "Render_LightShadow",
                        "The light casts shadows (a cached shadow-map "
                        "tile rendered by the engine)"))
                prop->setValue(true);
            if (lightShadowExtCheck->isChecked()) {
                if (auto prop = ensureProp<App::PropertyBool>(
                            vp, "App::PropertyBool",
                            "Render_LightShadowExtended",
                            "Shadow in every direction from the light "
                            "(six cube-face tiles instead of one "
                            "downward cone)"))
                    prop->setValue(true);
            }
            else {
                removeProp(vp, "Render_LightShadowExtended");
            }
        }
        else {
            removeProp(vp, "Render_LightShadow");
            removeProp(vp, "Render_LightShadowExtended");
        }
    }
    else {
        removeProp(vp, "Render_Light");
        removeProp(vp, "Render_LightIntensity");
        removeProp(vp, "Render_LightRange");
        removeProp(vp, "Render_LightShadow");
        removeProp(vp, "Render_LightShadowExtended");
    }

    // Texture images: PropertyFileIncluded copies the file into the
    // document, so rewriting the unchanged (already transient) path is
    // skipped.
    auto applyFile = [vp](QCheckBox *check, QLineEdit *edit,
                          const char *name, const char *doc) {
        QString path = edit->text().trimmed();
        if (!check->isChecked() || path.isEmpty()) {
            removeProp(vp, name);
            return;
        }
        auto prop = ensureProp<App::PropertyFileIncluded>(
                vp, "App::PropertyFileIncluded", name, doc);
        if (prop && path != QString::fromUtf8(prop->getValue()))
            prop->setValue(path.toUtf8().constData());
    };
    applyFile(baseColorCheck, baseColorEdit, "Render_BaseColorTexture",
              "Base color texture image (embedded in the document)");
    applyFile(normalMapCheck, normalMapEdit, "Render_NormalMap",
              "Tangent-space normal map or grayscale height map "
              "(embedded in the document)");
    applyFile(emissiveMapCheck, emissiveMapEdit, "Render_EmissiveMap",
              "Emissive map added to the lit color by the render "
              "engine (embedded in the document)");
    applyFile(occlusionMapCheck, occlusionMapEdit, "Render_OcclusionMap",
              "Ambient occlusion map multiplying the ambient/"
              "environment light of the render engine (embedded in "
              "the document)");
    applyFile(metallicRoughnessMapCheck, metallicRoughnessMapEdit,
              "Render_MetallicRoughnessMap",
              "glTF metallic-roughness map of the render engine PBR "
              "shading: green multiplies roughness, blue metallic "
              "(embedded in the document)");

    auto applyVector = [vp](QCheckBox *check, QDoubleSpinBox *x,
                            QDoubleSpinBox *y, const char *name,
                            const char *doc) {
        if (!check->isChecked()) {
            removeProp(vp, name);
            return;
        }
        if (auto prop = ensureProp<App::PropertyVector>(
                    vp, "App::PropertyVector", name, doc))
            prop->setValue(Base::Vector3d(x->value(), y->value(), 0.0));
    };
    applyVector(texScaleCheck, texScaleX, texScaleY,
                "Render_TextureScale", "Texture coordinate scale");
    applyVector(texOffsetCheck, texOffsetX, texOffsetY,
                "Render_TextureOffset", "Texture coordinate offset");
    if (texRotationCheck->isChecked()) {
        if (auto prop = ensureProp<App::PropertyAngle>(
                    vp, "App::PropertyAngle", "Render_TextureRotation",
                    "Texture coordinate rotation"))
            prop->setValue(texRotationSpin->value());
    }
    else {
        removeProp(vp, "Render_TextureRotation");
    }

    // Shadow flags: the default (on) keeps the document clean — only
    // the off states materialize a property.
    auto applyShadow = [vp](QCheckBox *check, const char *name,
                            const char *doc) {
        if (check->isChecked()) {
            removeProp(vp, name);
            return;
        }
        if (auto prop = ensureProp<App::PropertyBool>(
                    vp, "App::PropertyBool", name, doc))
            prop->setValue(false);
    };
    applyShadow(castShadowCheck, "Render_CastShadow",
                "Whether the object casts shadows");
    applyShadow(receiveShadowCheck, "Render_ReceiveShadow",
                "Whether the object receives shadows");
}

bool RenderSettingsWidget::accept()
{
    for (auto vp : vps) {
        try {
            apply(vp);
        }
        catch (Base::Exception &e) {
            e.ReportException();
        }
    }
    return true;
}

/* TRANSLATOR Gui::TaskRenderSettings */

TaskRenderSettings::TaskRenderSettings()
{
    widget = new RenderSettingsWidget(selectedViewProviders());
    taskbox = new TaskView::TaskBox(
        QPixmap(), widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskRenderSettings::~TaskRenderSettings() = default;

bool TaskRenderSettings::accept()
{
    return widget->accept();
}

bool TaskRenderSettings::reject()
{
    return true;
}

std::vector<ViewProviderGeometryObject*>
TaskRenderSettings::selectedViewProviders()
{
    std::vector<ViewProviderGeometryObject*> res;
    for (auto &sel : Selection().getCompleteSelection()) {
        // Links resolve to their linked geometry object.
        auto obj = sel.pResolvedObject ? sel.pResolvedObject
                                       : sel.pObject;
        if (!obj)
            continue;
        auto vp = Base::freecad_dynamic_cast<ViewProviderGeometryObject>(
                Application::Instance->getViewProvider(obj));
        if (vp && std::find(res.begin(), res.end(), vp) == res.end())
            res.push_back(vp);
    }
    return res;
}

#include "moc_TaskRenderSettings.cpp"
