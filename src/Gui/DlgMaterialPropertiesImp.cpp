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

#include <App/PropertyStandard.h>

#include "DlgMaterialPropertiesImp.h"
#include "ui_DlgMaterialProperties.h"
#include "ViewProvider.h"


using namespace Gui::Dialog;


/* TRANSLATOR Gui::Dialog::DlgMaterialPropertiesImp */

/**
 *  Constructs a Gui::Dialog::DlgMaterialPropertiesImp as a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'.
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgMaterialPropertiesImp::DlgMaterialPropertiesImp(const std::string& mat, QWidget* parent,
                                                   Qt::WindowFlags fl)
  : QDialog(parent, fl)
  , ui(new Ui_DlgMaterialProperties)
  , material(mat)
{
    ui->setupUi(this);
    setupConnections();

    if (material != "ShapeAppearance") {
        ui->textLabel1->hide();
        ui->diffuseColor->hide();
    }

    ui->ambientColor->setAutoChangeColor(true);
    ui->diffuseColor->setAutoChangeColor(true);
    ui->emissiveColor->setAutoChangeColor(true);
    ui->specularColor->setAutoChangeColor(true);

    // Each edit applies as it is committed: a spin box on every deliberate
    // step, not on every keystroke of a half-typed number
    ui->shininess->setKeyboardTracking(false);
    ui->metallic->setKeyboardTracking(false);
    ui->roughness->setKeyboardTracking(false);

    // Until setViewProviders() finds a material list to toggle
    ui->shadingModelLabel->hide();
    ui->shadingModel->hide();
    updateModeView(false);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgMaterialPropertiesImp::~DlgMaterialPropertiesImp() = default;

void DlgMaterialPropertiesImp::setupConnections()
{
    connect(ui->shadingModel, qOverload<int>(&QComboBox::activated),
            this, &DlgMaterialPropertiesImp::onShadingModelActivated);
    // ColorButton::changed is the commit: it fires on a chosen colour and
    // again when the picker is cancelled and the old colour restored
    connect(ui->ambientColor, &ColorButton::changed,
            this, &DlgMaterialPropertiesImp::onAmbientColorChanged);
    connect(ui->diffuseColor, &ColorButton::changed,
            this, &DlgMaterialPropertiesImp::onDiffuseColorChanged);
    connect(ui->emissiveColor, &ColorButton::changed,
            this, &DlgMaterialPropertiesImp::onEmissiveColorChanged);
    connect(ui->specularColor, &ColorButton::changed,
            this, &DlgMaterialPropertiesImp::onSpecularColorChanged);
    connect(ui->shininess, qOverload<int>(&QSpinBox::valueChanged),
            this, &DlgMaterialPropertiesImp::onShininessValueChanged);
    connect(ui->metallic, qOverload<int>(&QSpinBox::valueChanged),
            this, &DlgMaterialPropertiesImp::onMetallicValueChanged);
    connect(ui->roughness, qOverload<int>(&QSpinBox::valueChanged),
            this, &DlgMaterialPropertiesImp::onRoughnessValueChanged);
}

QColor DlgMaterialPropertiesImp::diffuseColor() const
{
    return ui->diffuseColor->color();
}

App::PropertyAppearanceList* DlgMaterialPropertiesImp::listProperty(Gui::ViewProvider* vp) const
{
    return dynamic_cast<App::PropertyAppearanceList*>(vp->getPropertyByName(material.c_str()));
}

App::PropertyAppearance* DlgMaterialPropertiesImp::singleProperty(Gui::ViewProvider* vp) const
{
    return dynamic_cast<App::PropertyAppearance*>(vp->getPropertyByName(material.c_str()));
}

void DlgMaterialPropertiesImp::updateModeView(bool pbr)
{
    ui->textLabel2->setVisible(!pbr);
    ui->ambientColor->setVisible(!pbr);
    ui->textLabel4->setVisible(!pbr);
    ui->shininess->setVisible(!pbr);
    ui->metallicLabel->setVisible(pbr);
    ui->metallic->setVisible(pbr);
    ui->roughnessLabel->setVisible(pbr);
    ui->roughness->setVisible(pbr);
    ui->textLabel1->setText(pbr ? tr("Base color:") : tr("Diffuse color:"));
    ui->textLabel3->setText(pbr ? tr("Specular tint:") : tr("Specular color:"));
}

void DlgMaterialPropertiesImp::syncFromProperty()
{
    auto setButton = [](ColorButton* button, const App::Color& color) {
        button->setColor(QColor(int(color.r * 255.0f), int(color.g * 255.0f),
                                int(color.b * 255.0f)));
    };
    auto setSpin = [](QSpinBox* spin, float value) {
        QSignalBlocker block(spin);
        spin->setValue(int(100.0f * value + 0.5f));
    };
    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            const bool pbr = list->isPBR();
            {
                QSignalBlocker block(ui->shadingModel);
                ui->shadingModel->setCurrentIndex(pbr ? 1 : 0);
            }
            // The raw entry-0 slots: in PBR mode the diffuse IS the base
            // colour and the specular rgb the tint
            App::MaterialAppearance mat = list->getMaterial(0);
            setButton(ui->ambientColor, mat.ambientColor);
            setButton(ui->diffuseColor, mat.diffuseColor);
            setButton(ui->emissiveColor, mat.emissiveColor);
            setButton(ui->specularColor, mat.specularColor);
            // Each reading works in either mode, so nothing stale shows
            // after a toggle
            setSpin(ui->shininess, list->getPhongMaterial(0).shininess);
            setSpin(ui->metallic, list->getMetallic(0));
            setSpin(ui->roughness, list->getRoughness(0));
            updateModeView(pbr);
            return;
        }
        if (auto* single = singleProperty(vp)) {
            const App::MaterialAppearance& mat = single->getValue();
            setButton(ui->ambientColor, mat.ambientColor);
            setButton(ui->diffuseColor, mat.diffuseColor);
            setButton(ui->emissiveColor, mat.emissiveColor);
            setButton(ui->specularColor, mat.specularColor);
            setSpin(ui->shininess, mat.shininess);
            updateModeView(false);
            return;
        }
    }
}

/**
 * Toggles between the Phong and the PBR reading, converting the stored
 * values so the look survives the switch (see PropertyAppearanceList::convertPBR).
 */
void DlgMaterialPropertiesImp::onShadingModelActivated(int index)
{
    const bool pbr = index == 1;
    for (auto vp : Objects) {
        if (auto* list = listProperty(vp))
            list->convertPBR(pbr);
    }
    syncFromProperty();
}

/*
 * Each of the handlers below sets ONE field across the appearance, through
 * the per field setter rather than by reading entry 0, changing a field and
 * writing the whole material back. That round trip replaced the list with a
 * single entry, so editing the shininess of an object with per-face colours
 * threw the colours away. The colour writes are rgb-only for the same
 * reason: the diffuse alpha carries the opacity and the PBR specular alpha
 * the metallic factor, so a colour edit must not restate them.
 */

/**
 * Sets the ambient color.
 */
void DlgMaterialPropertiesImp::onAmbientColorChanged()
{
    QColor col = ui->ambientColor->color();
    float r = (float)col.red() / 255.0f;
    float g = (float)col.green() / 255.0f;
    float b = (float)col.blue() / 255.0f;
    App::Color ambient(r, g, b);

    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            list->setAmbientColor(ambient);
        }
        else if (auto* single = singleProperty(vp)) {
            single->setAmbientColor(ambient);
        }
    }
}

/**
 * Sets the diffuse color, which in PBR mode is the base color.
 */
void DlgMaterialPropertiesImp::onDiffuseColorChanged()
{
    QColor col = ui->diffuseColor->color();
    float r = (float)col.red() / 255.0f;
    float g = (float)col.green() / 255.0f;
    float b = (float)col.blue() / 255.0f;
    App::Color diffuse(r, g, b);

    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            list->setDiffuseRGB(diffuse);
        }
        else if (auto* single = singleProperty(vp)) {
            App::Color color = single->getValue().diffuseColor;
            color.r = diffuse.r;
            color.g = diffuse.g;
            color.b = diffuse.b;
            single->setDiffuseColor(color);
        }
    }
}

/**
 * Sets the emissive color.
 */
void DlgMaterialPropertiesImp::onEmissiveColorChanged()
{
    QColor col = ui->emissiveColor->color();
    float r = (float)col.red() / 255.0f;
    float g = (float)col.green() / 255.0f;
    float b = (float)col.blue() / 255.0f;
    App::Color emissive(r, g, b);

    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            list->setEmissiveColor(emissive);
        }
        else if (auto* single = singleProperty(vp)) {
            single->setEmissiveColor(emissive);
        }
    }
}

/**
 * Sets the specular color, which in PBR mode is the F0 tint whose alpha
 * carries the metallic factor.
 */
void DlgMaterialPropertiesImp::onSpecularColorChanged()
{
    QColor col = ui->specularColor->color();
    float r = (float)col.red() / 255.0f;
    float g = (float)col.green() / 255.0f;
    float b = (float)col.blue() / 255.0f;
    App::Color specular(r, g, b);

    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            list->setSpecularRGB(specular);
        }
        else if (auto* single = singleProperty(vp)) {
            App::Color color = single->getValue().specularColor;
            color.r = specular.r;
            color.g = specular.g;
            color.b = specular.b;
            single->setSpecularColor(color);
        }
    }
}

/**
 * Sets the current shininess.
 */
void DlgMaterialPropertiesImp::onShininessValueChanged(int sh)
{
    float shininess = (float)sh / 100.0f;
    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            list->setShininess(shininess);
        }
        else if (auto* single = singleProperty(vp)) {
            single->setShininess(shininess);
        }
    }
}

/**
 * Sets the metallic factor of a PBR-mode appearance.
 */
void DlgMaterialPropertiesImp::onMetallicValueChanged(int value)
{
    float metallic = (float)value / 100.0f;
    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            if (list->isPBR())
                list->setMetallic(metallic);
        }
    }
}

/**
 * Sets the roughness of a PBR-mode appearance.
 */
void DlgMaterialPropertiesImp::onRoughnessValueChanged(int value)
{
    float roughness = (float)value / 100.0f;
    for (auto vp : Objects) {
        if (auto* list = listProperty(vp)) {
            if (list->isPBR())
                list->setRoughness(roughness);
        }
    }
}

/**
 * Sets the document objects and their view providers to manipulate the material.
 */
void DlgMaterialPropertiesImp::setViewProviders(const std::vector<Gui::ViewProvider*>& Obj)
{
    Objects = Obj;

    // What Cancel restores, and whether there is a mode to toggle
    snapshots.clear();
    bool haveList = false;
    for (auto vp : Objects) {
        App::Property* prop = vp->getPropertyByName(material.c_str());
        if (prop && (prop->isDerivedFrom<App::PropertyAppearanceList>()
                     || prop->isDerivedFrom<App::PropertyAppearance>())) {
            snapshots.emplace_back(vp, std::unique_ptr<App::Property>(prop->Copy()));
            haveList = haveList || prop->isDerivedFrom<App::PropertyAppearanceList>();
        }
    }
    ui->shadingModelLabel->setVisible(haveList);
    ui->shadingModel->setVisible(haveList);

    syncFromProperty();
}

/**
 * Restores every appearance the dialog found, undoing the live-applied
 * edits.
 */
void DlgMaterialPropertiesImp::reject()
{
    for (auto& [vp, copy] : snapshots) {
        if (App::Property* prop = vp->getPropertyByName(material.c_str()))
            prop->Paste(*copy);
    }
    QDialog::reject();
}

#include "moc_DlgMaterialPropertiesImp.cpp"
