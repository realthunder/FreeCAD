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

const App::PropertyFloatConstraint::Constraints UnitRange = {0.0, 1.0, 0.05};

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

    // PBR scalars: only meaningful to the render engine's PBR shading
    // path, overriding the global/per-view defaults per object.
    metallicCheck = addOverride(tr("Metallic"));
    metallicSpin = addSpin(1, 0.0, 1.0, 0.05, 2);
    ++row;
    roughnessCheck = addOverride(tr("Roughness"));
    roughnessSpin = addSpin(1, 0.0, 1.0, 0.05, 2);
    ++row;

    // Water body of the volumetric lighting pass.
    waterCheck = addOverride(tr("Water body"));
    waterDensitySpin = addSpin(1, 0.0, 1.0e6, 0.01, 2);
    waterDensitySpin->setSpecialValueText(tr("auto density"));
    waterDensitySpin->setToolTip(
        tr("Extinction density in inverse model units; 0 = automatic "
           "(from the body size)"));
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
    enables(metallicCheck, {metallicSpin});
    enables(roughnessCheck, {roughnessSpin});
    enables(waterCheck, {waterDensitySpin});
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

    if (auto prop = getProp<App::PropertyFloat>(vp, "Render_Metallic")) {
        metallicCheck->setChecked(true);
        metallicSpin->setValue(prop->getValue());
    }
    if (auto prop = getProp<App::PropertyFloat>(vp, "Render_Roughness")) {
        roughnessCheck->setChecked(true);
        roughnessSpin->setValue(prop->getValue());
    }
    if (auto prop = getProp<App::PropertyBool>(vp, "Render_Water")) {
        waterCheck->setChecked(prop->getValue());
        if (auto dens = getProp<App::PropertyFloat>(
                    vp, "Render_WaterDensity"))
            waterDensitySpin->setValue(dens->getValue());
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
    // Scalar overrides (0..1 constrained like the per-view properties).
    auto applyScalar = [vp](QCheckBox *check, QDoubleSpinBox *spin,
                            const char *name, const char *doc) {
        if (!check->isChecked()) {
            removeProp(vp, name);
            return;
        }
        auto prop = ensureProp<App::PropertyFloatConstraint>(
                vp, "App::PropertyFloatConstraint", name, doc);
        if (prop) {
            if (prop->getConstraints() != &UnitRange)
                prop->setConstraints(&UnitRange);
            prop->setValue(spin->value());
        }
    };
    applyScalar(metallicCheck, metallicSpin, "Render_Metallic",
                "Per-object PBR metalness of the render engine");
    applyScalar(roughnessCheck, roughnessSpin, "Render_Roughness",
                "Per-object PBR roughness of the render engine");

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
