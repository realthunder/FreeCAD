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
# include <QButtonGroup>
# include <QCheckBox>
# include <QComboBox>
# include <QFileInfo>
# include <QGridLayout>
# include <QHBoxLayout>
# include <QLabel>
# include <QMenu>
# include <QPointer>
# include <QRadioButton>
# include <QSlider>
# include <QWidgetAction>
#endif

#include <App/PropertyContainer.h>
#include <App/PropertyFile.h>
#include <App/PropertyStandard.h>
#include <Base/Tools.h>

#include "ShadingOptions.h"
#include "Application.h"
#include "FileDialog.h"
#include "RenderParams.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"

using namespace Gui;

namespace {

/// The <group>_<name> property of \a view, if it is there and of the
/// expected type.
template<class PropT>
PropT *viewProp(App::PropertyContainer *view, const char *group,
                const char *name)
{
    if (!view)
        return nullptr;
    std::string propname(group);
    propname += '_';
    propname += name;
    auto prop = view->getPropertyByName(propname.c_str());
    if (!prop || !prop->isDerivedFrom(PropT::getClassTypeId()))
        return nullptr;
    return static_cast<PropT*>(prop);
}

/// A Render_* property. Absent means no renderer backend is selected on
/// this view -- they are materialized by
/// View3DInventorViewer::setRendererType, not by the config feed.
template<class PropT>
PropT *renderProp(App::PropertyContainer *view, const char *name)
{
    return viewProp<PropT>(view, "Render", name);
}

bool renderFlag(App::PropertyContainer *view, const char *name, bool def)
{
    if (auto prop = renderProp<App::PropertyBool>(view, name))
        return prop->getValue();
    return def;
}

QString doc(const char *text)
{
    return QString::fromUtf8(text);
}

} // namespace

ShadingOptionsWidget::ShadingOptionsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(12, 4, 12, 6);
    layout->setHorizontalSpacing(12);

    auto title = new QLabel(tr("Shading"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title, 0, 0, 1, 2);

    // The one genuinely exclusive choice here: each of the three replaces
    // the surface's response to light, so at most one can be on.
    auto modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(0, 0, 0, 0);
    // Three one-word labels read as one run of text at the style default
    // spacing -- wider than the 12 the checkbox grid below uses, because
    // those sit in aligned columns and these do not.
    modelRow->setSpacing(18);
    // Named for what it IS, not for its position in the list. "Default"
    // described only the fact that a view starts in it, which stops being
    // true the moment that changes and never said anything about the
    // shading either way.
    classicRadio = new QRadioButton(tr("Classic"), this);
    classicRadio->setToolTip(
        tr("Blinn-Phong headlight shading: the light follows the camera "
           "and the surface has no environment around it. What a view "
           "starts in."));
    pbrRadio = new QRadioButton(tr("Realistic"), this);
    pbrRadio->setToolTip(doc(RenderParams::docPBR()));
    matcapRadio = new QRadioButton(tr("Matcap"), this);
    matcapRadio->setToolTip(doc(RenderParams::docMatcap()));
    auto models = new QButtonGroup(this);
    models->addButton(classicRadio);
    models->addButton(pbrRadio);
    models->addButton(matcapRadio);
    modelRow->addWidget(classicRadio);
    modelRow->addWidget(pbrRadio);
    modelRow->addWidget(matcapRadio);
    modelRow->addStretch();
    layout->addWidget(new QLabel(tr("Model:"), this), 1, 0);
    layout->addLayout(modelRow, 1, 1);

    // Realistic's one real choice, and the reason the mode is worth
    // switching to: what the scene is standing in. It sits under the
    // radio rather than in the preferences for the same reason the
    // matcap preset does -- it is the knob you reach for immediately
    // after choosing the model, with the 3D view in sight.
    envLabel = new QLabel(tr("Environment:"), this);
    envCombo = new QComboBox(this);
    // The character of each one goes on the entry rather than into the
    // combo's own tooltip: which preset is the choice actually being
    // made here, and the names are the one thing that cannot say how
    // they differ. All six carry the same mean radiance, so none of
    // this is about brightness -- it is about where the light comes
    // from and whether anything in it has an edge.
    auto addEnvPreset = [this](const QString &name, const QString &tip) {
        envCombo->addItem(name);
        envCombo->setItemData(envCombo->count() - 1, tip, Qt::ToolTipRole);
    };
    addEnvPreset(tr("Studio"),
                 tr("Four soft boxes on a dark surround. The widest contrast "
                    "of the six,\nwith rectangular sources a polished surface "
                    "can reflect as sources --\nwhich is what makes it look "
                    "polished."));
    addEnvPreset(tr("Gradient"),
                 tr("Ground, horizon and sky in one smooth ramp, with no edge "
                    "anywhere\nand barely one stop from top to bottom. Nothing "
                    "in it reads as a\nlight, so every roughness reflects the "
                    "same flat grey. The oldest of\nthe six, kept so an older "
                    "document can have its look back."));
    addEnvPreset(tr("Overcast"),
                 tr("A bright dome weighted to the zenith, so the light "
                    "arrives from above\nand the band behind the model stays "
                    "dark enough for a near-white part\nto stand against it."));
    addEnvPreset(tr("Sunset"),
                 tr("A low warm sun over a deep sky: the widest span of hue "
                    "here, warm\ndown one side of a part and cool down the "
                    "other."));
    addEnvPreset(tr("Interior"),
                 tr("One window and a ceiling panel in a room with close "
                    "walls. The\ncrispest key of the six, and where a view "
                    "starts."));
    addEnvPreset(tr("Light tent"),
                 tr("A box of white panels, bright below as well as above. "
                    "The only one\nthat lights the wall of a standing "
                    "cylinder -- that wall reflects the\nhalf of the sphere "
                    "under the horizon, which is floor, and dark, in\nevery "
                    "other preset."));
    // An image is the same choice as a preset rather than a modifier of
    // one: the renderer takes one INSTEAD of the other
    // (SoFCRendererBridge::translatePBR resolves the image first and
    // only falls back to the preset). So it is the last entry of this
    // combo -- a separate browse button beside it would be a control
    // that silently disables the one next to it.
    envCombo->insertSeparator(envCombo->count());
    envImageIndex = envCombo->count();
    envCombo->addItem(tr("Image..."));
    envCombo->setToolTip(
        tr("What the scene stands in: the surroundings that light it, and "
           "that\nits reflections show. A preset, or an image of your own."));
    envLabel->setToolTip(envCombo->toolTip());
    // What a usable environment image has to be -- the format, the
    // proportions, the size -- said on the entry that opens the file
    // dialog, which is the last moment before the choice is made.
    // refresh() puts the current file above it once there is one.
    envCombo->setItemData(envImageIndex, envImageToolTip(), Qt::ToolTipRole);
    // The environment lights the scene whether or not it is DRAWN --
    // the flag gates the background pass alone. So this is the switch
    // for "light it like a studio, keep my background", which is a
    // thing people want often enough that it should not be four clicks
    // into the preferences.
    envBgCheck = new QCheckBox(tr("as background"), this);
    envBgCheck->setToolTip(doc(RenderParams::docPBREnvBackground()));
    // A path is the part of this setting least likely to survive being
    // sent to somebody else, so the copy that travels with the document
    // is offered where the image is picked rather than in the property
    // editor. Nothing to copy for a preset, so it greys out for one.
    envEmbedCheck = new QCheckBox(tr("keep a copy"), this);
    envEmbedCheck->setToolTip(doc(RenderParams::docPBREnvEmbed()));
    auto envRow = new QHBoxLayout;
    envRow->setContentsMargins(0, 0, 0, 0);
    envRow->addWidget(envCombo, 1);
    envRow->addWidget(envBgCheck);
    envRow->addWidget(envEmbedCheck);
    layout->addWidget(envLabel, 2, 0);
    layout->addLayout(envRow, 2, 1);

    matcapLabel = new QLabel(tr("Matcap:"), this);
    matcapCombo = new QComboBox(this);
    matcapCombo->addItem(tr("Studio"));
    matcapCombo->addItem(tr("Clay"));
    matcapCombo->addItem(tr("Metal"));
    matcapCombo->addItem(tr("Pearl"));
    matcapCombo->setToolTip(doc(RenderParams::docMatcapPreset()));
    layout->addWidget(matcapLabel, 3, 0);
    layout->addWidget(matcapCombo, 3, 1);

    // Matcap's other number, and the one that surprises people: at zero
    // the whole scene shades as a single material, so an assembly's
    // color coding disappears and only form is left. That is the point
    // of the default, but it reads as "the renderer lost my colors"
    // until you find the knob -- so the knob sits next to the preset,
    // beside the 3D view, rather than in the preferences.
    matcapTintLabel = new QLabel(tr("Matcap tint:"), this);
    matcapTintSlider = new QSlider(Qt::Horizontal, this);
    matcapTintSlider->setRange(0, 100);
    matcapTintSlider->setPageStep(10);
    matcapTintSlider->setToolTip(doc(RenderParams::docMatcapTint()));
    matcapTintLabel->setToolTip(matcapTintSlider->toolTip());
    matcapTintValue = new QLabel(this);
    matcapTintValue->setMinimumWidth(
        matcapTintValue->fontMetrics().horizontalAdvance(tr("000 %")));
    auto tintRow = new QHBoxLayout;
    tintRow->setContentsMargins(0, 0, 0, 0);
    tintRow->addWidget(matcapTintSlider, 1);
    tintRow->addWidget(matcapTintValue);
    layout->addWidget(matcapTintLabel, 4, 0);
    layout->addLayout(tintRow, 4, 1);

    // The modifiers: each composes with any shading model and with the
    // others, which is exactly why they are checkboxes and not entries in
    // the draw style list above.
    auto flags = new QGridLayout;
    flags->setContentsMargins(0, 4, 0, 0);
    flags->setHorizontalSpacing(12);
    cavityCheck = new QCheckBox(tr("Cavity"), this);
    cavityCheck->setToolTip(doc(RenderParams::docCavity()));
    aoCheck = new QCheckBox(tr("Ambient occlusion"), this);
    aoCheck->setToolTip(doc(RenderParams::docAO()));
    shadowCheck = new QCheckBox(tr("Shadows"), this);
    shadowCheck->setToolTip(
        tr("Light the scene with a directional light that casts shadows.\n"
           "\n"
           "The renderer's own scene light and the shadow map it casts: a "
           "shading\nswitch beside the others, leaving the display style "
           "alone. Its direction,\ncolour and spot form are the view's "
           "Render_Light* properties, its ground\nplane and map quality "
           "the RenderShadow_* ones."));
    bloomCheck = new QCheckBox(tr("Bloom"), this);
    bloomCheck->setToolTip(doc(RenderParams::docBloom()));
    flags->addWidget(cavityCheck, 0, 0);
    flags->addWidget(aoCheck, 0, 1);
    flags->addWidget(shadowCheck, 1, 0);
    flags->addWidget(bloomCheck, 1, 1);
    layout->addLayout(flags, 5, 0, 1, 2);

    // Cavity is the one modifier here whose usefulness depends on a
    // number rather than on being on: the radius decides which features
    // it can see at all, and the useful value moves with the model and
    // with the display's pixel density. A slider beside the switch, so
    // it can be found and dragged with the 3D view in sight -- the menu
    // stays open under a widget action.
    cavityRadiusLabel = new QLabel(tr("Cavity radius:"), this);
    cavityRadiusSlider = new QSlider(Qt::Horizontal, this);
    cavityRadiusSlider->setRange(1, 32);
    cavityRadiusSlider->setPageStep(2);
    cavityRadiusSlider->setToolTip(doc(RenderParams::docCavityRadius()));
    cavityRadiusLabel->setToolTip(cavityRadiusSlider->toolTip());
    cavityRadiusValue = new QLabel(this);
    // Wide enough for the longest reading, so the row does not shuffle
    // sideways as the number changes under the drag.
    cavityRadiusValue->setMinimumWidth(
        cavityRadiusValue->fontMetrics().horizontalAdvance(tr("00 px")));
    auto radiusRow = new QHBoxLayout;
    radiusRow->setContentsMargins(0, 0, 0, 0);
    radiusRow->addWidget(cavityRadiusSlider, 1);
    radiusRow->addWidget(cavityRadiusValue);
    layout->addWidget(cavityRadiusLabel, 6, 0);
    layout->addLayout(radiusRow, 6, 1);

    // Not "pick a renderer type in the preferences" any more: the
    // render path stopped being a stored choice in ca372262f1, and
    // both controls left the preference page with it. What is left
    // to say is that the engine is not running, which now means this
    // build has no backend or the one it has could not start --
    // neither of them something to fix in a dialog.
    hint = new QLabel(tr("Needs the render engine, which is not "
                         "running on this view."), this);
    hint->setEnabled(false);
    layout->addWidget(hint, 7, 0, 1, 2);

    connect(classicRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on && !loading)
            setModel(false, false);
    });
    connect(pbrRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on && !loading)
            setModel(true, false);
    });
    connect(matcapRadio, &QRadioButton::toggled, this, [this](bool on) {
        if (on && !loading)
            setModel(false, true);
    });
    connect(envBgCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("PBREnvBackground", on, &RenderParams::setPBREnvBackground);
    });
    connect(envEmbedCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("PBREnvEmbed", on, &RenderParams::setPBREnvEmbed);
    });
    connect(envCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (loading)
            return;
        if (index == envImageIndex) {
            chooseEnvImage();
            return;
        }
        // Leaving the image behind is part of picking a preset: an image
        // still set would go on winning, and the combo would read as a
        // control that does nothing.
        clearEnvImage();
        if (auto prop = renderProp<App::PropertyEnumeration>(activeView(),
                                                            "PBREnvPreset"))
            prop->setValue(long(index));
        RenderParams::setPBREnvPreset(long(index));
    });
    connect(matcapCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyEnumeration>(activeView(),
                                                            "MatcapPreset"))
            prop->setValue(long(index));
        RenderParams::setMatcapPreset(long(index));
    });
    connect(matcapTintSlider, &QSlider::valueChanged, this, [this](int value) {
        matcapTintValue->setText(tr("%1 %").arg(value));
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyFloat>(activeView(),
                                                       "MatcapTint"))
            prop->setValue(double(value) / 100.0);
        RenderParams::setMatcapTint(double(value) / 100.0);
    });
    connect(cavityCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Cavity", on, &RenderParams::setCavity);
        updateCavityRadiusEnabled();
    });
    connect(cavityRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        cavityRadiusValue->setText(tr("%1 px").arg(value));
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyFloat>(activeView(),
                                                       "CavityRadius"))
            prop->setValue(double(value));
        RenderParams::setCavityRadius(double(value));
    });
    connect(aoCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("AO", on, &RenderParams::setAO);
    });
    connect(shadowCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!loading)
            setShadow(on);
    });
    connect(bloomCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Bloom", on, &RenderParams::setBloom);
    });
}

App::PropertyContainer *ShadingOptionsWidget::activeView() const
{
    return qobject_cast<View3DInventor*>(Application::Instance->activeView());
}

// The Shadow draw style is gone (docs/CoinRetirement.md stage 4e); what
// this switch drives is what that style stood for -- the renderer's own
// scene light and the map it casts. The display style is left alone,
// which is the point of the move: shadows compose with the style the
// user is looking at instead of hosting it.
void ShadingOptionsWidget::setShadow(bool on)
{
    auto view = activeView();
    if (auto prop = renderProp<App::PropertyBool>(view, "Light"))
        prop->setValue(on);
    RenderParams::setLight(on);
    // The map follows the light on, and is left alone otherwise:
    // Render_Shadow drops the map while keeping the scene lit, which is
    // a state worth being able to hold.
    if (on) {
        if (auto prop = renderProp<App::PropertyBool>(view, "Shadow"))
            prop->setValue(true);
        RenderParams::setShadow(true);
    }
}

void ShadingOptionsWidget::updateCavityRadiusEnabled()
{
    // A radius with the pass switched off is a control that does
    // nothing; grey it rather than let it read as broken.
    const bool on = cavityCheck->isEnabled() && cavityCheck->isChecked();
    cavityRadiusLabel->setEnabled(on);
    cavityRadiusSlider->setEnabled(on);
    cavityRadiusValue->setEnabled(on);
}

void ShadingOptionsWidget::updateMatcapTintEnabled()
{
    // Same reasoning as the cavity radius: a tint that shades nothing is
    // a control that does nothing.
    const bool on = matcapRadio->isEnabled() && matcapRadio->isChecked();
    matcapTintLabel->setEnabled(on);
    matcapTintSlider->setEnabled(on);
    matcapTintValue->setEnabled(on);
}

void ShadingOptionsWidget::setModel(bool pbr, bool matcap)
{
    // The model is stated once, on the view's declared ShadingType;
    // the hidden Render_PBR / Render_Matcap facade follows it
    // (View3DInventor::onChanged). The preferences stay the pair.
    if (auto view = qobject_cast<View3DInventor*>(
                Application::Instance->activeView()))
        view->ShadingType.setValue(
                matcap ? View3DInventor::ShadingMatcap
                       : pbr ? View3DInventor::ShadingRealistic
                             : View3DInventor::ShadingClassic);
    RenderParams::setPBR(pbr);
    RenderParams::setMatcap(matcap);
    envLabel->setEnabled(pbr);
    envCombo->setEnabled(pbr);
    envBgCheck->setEnabled(pbr);
    updateEnvEmbedEnabled();
    matcapLabel->setEnabled(matcap);
    matcapCombo->setEnabled(matcap);
    updateMatcapTintEnabled();
}

void ShadingOptionsWidget::setFlag(const char *name, bool value,
                                   void (*pref)(const bool &))
{
    if (loading)
        return;
    if (auto prop = renderProp<App::PropertyBool>(activeView(), name))
        prop->setValue(value);
    if (pref)
        pref(value);
}

QString ShadingOptionsWidget::envImageToolTip(const QString &current) const
{
    // What to go and find, in the four facts that decide whether the
    // file works at all. The reasons behind each of them, and the rest
    // of the behaviour, are in RenderParams::docPBREnvImage, which the
    // property editor shows and which has room for them.
    QString tip = tr(
        "Light the scene with your own panorama.\n"
        "\n"
        "Wants a 2:1 lat-long Radiance .hdr at 1K or 2K. An .exr will not\n"
        "load, and an 8-bit photo has too little range to light with.\n"
        "Free ones: polyhaven.com/hdris. Shown as the background it is\n"
        "deliberately soft.");
    if (current.isEmpty())
        return tip;
    // The file first: once one is loaded, which one is the question this
    // entry raises, and the instructions are behind it.
    return current + QLatin1String("\n\n") + tip;
}

void ShadingOptionsWidget::updateEnvEmbedEnabled()
{
    // A preset has no file to copy, so the box would be a control that
    // does nothing -- and worse, one that claims the document is
    // carrying something it is not.
    envEmbedCheck->setEnabled(envCombo->isEnabled()
                              && !envImageName().isEmpty());
}

QString ShadingOptionsWidget::envImageName() const
{
    auto view = activeView();
    // The embedded copy wins in the bridge, so it wins here: it is what
    // the scene is standing in. A restored document holds the copy but
    // not the path it came from -- the source name is runtime state --
    // which is why the copy is asked first rather than second.
    if (auto prop = renderProp<App::PropertyFileIncluded>(view,
                                                          "PBREnvImageData")) {
        if (!prop->isEmpty()) {
            QString name = QString::fromUtf8(
                    prop->getOriginalFileName().c_str());
            if (name.isEmpty())
                name = QString::fromUtf8(prop->getValue());
            return name;
        }
    }
    if (auto prop = renderProp<App::PropertyFile>(view, "PBREnvImage")) {
        if (const char *path = prop->getValue())
            return QString::fromUtf8(path);
    }
    return QString();
}

void ShadingOptionsWidget::chooseEnvImage()
{
    auto view = activeView();
    auto prop = renderProp<App::PropertyFile>(view, "PBREnvImage");
    if (!prop) {
        refresh();
        return;
    }
    // The menu holds a popup grab, and a file dialog raised under one
    // closes it on the first click anyway. Dropping it here makes the
    // sequence the same every time instead of style dependent.
    if (auto menu = qobject_cast<QMenu*>(parentWidget()))
        menu->close();
    // The menu owns this widget, so it outlives the dialog -- but only
    // as long as the toolbar does not rebuild the menu underneath it,
    // which is exactly the kind of thing a modal loop lets happen.
    QPointer<ShadingOptionsWidget> self(this);
    QString path = FileDialog::getOpenFileName(
            this, tr("Environment image"),
            QString::fromUtf8(prop->getValue()),
            tr("Environment images (*.hdr *.pic *.png *.jpg *.jpeg *.bmp "
               "*.tif *.tiff);;All files (*)"));
    if (!self)
        return;
    // Cancelled: the combo has already moved onto the image entry, and
    // refresh puts it back on the preset that is still lighting the
    // scene.
    if (!path.isEmpty()) {
        prop->setValue(path.toUtf8().constData());
        RenderParams::setPBREnvImage(path.toUtf8().constData());
    }
    refresh();
}

void ShadingOptionsWidget::clearEnvImage()
{
    auto view = activeView();
    if (auto prop = renderProp<App::PropertyFile>(view, "PBREnvImage")) {
        if (prop->getValue() && prop->getValue()[0])
            prop->setValue("");
    }
    RenderParams::setPBREnvImage("");
    // The path change drops the embedded copy through
    // View3DInventorViewer::syncEnvImageEmbed -- except on a restored
    // document, whose path is already empty while the copy still lights
    // the scene, so the copy is cleared here rather than assumed gone.
    if (auto prop = renderProp<App::PropertyFileIncluded>(view,
                                                          "PBREnvImageData")) {
        if (!prop->isEmpty())
            prop->setValue("");
    }
}

void ShadingOptionsWidget::refresh()
{
    auto view = activeView();
    // The PBR flag stands for the whole family: they are materialized
    // together, so either all of them are there or none is.
    bool available = renderProp<App::PropertyBool>(view, "PBR") != nullptr;

    Base::StateLocker guard(loading);

    bool pbr = renderFlag(view, "PBR", false);
    bool matcap = renderFlag(view, "Matcap", false);
    // Matcap overrides physically based shading while on, so it wins the
    // radio when a document somehow carries both.
    matcapRadio->setChecked(matcap);
    pbrRadio->setChecked(pbr && !matcap);
    classicRadio->setChecked(!pbr && !matcap);
    // An image set on the view is what the scene is standing in, so the
    // combo names it; the entry carries the file name rather than the
    // bare "Image..." prompt, which is the only place the choice is
    // visible without opening the property editor.
    QString envImage = envImageName();
    if (envImageIndex >= 0) {
        envCombo->setItemText(envImageIndex, envImage.isEmpty()
                ? tr("Image...")
                : QFileInfo(envImage).fileName());
        envCombo->setItemData(envImageIndex, envImageToolTip(envImage),
                              Qt::ToolTipRole);
    }
    if (!envImage.isEmpty() && envImageIndex >= 0)
        envCombo->setCurrentIndex(envImageIndex);
    else if (auto prop = renderProp<App::PropertyEnumeration>(view,
                                                              "PBREnvPreset"))
        envCombo->setCurrentIndex(int(prop->getValue()));
    // The fallback is the parameter and not a literal false: this one
    // defaults ON, so a hardcoded false would draw the box unchecked
    // while the renderer went on drawing the environment.
    envBgCheck->setChecked(renderFlag(view, "PBREnvBackground",
                                      RenderParams::getPBREnvBackground()));
    // Defaults ON as well, and for the same reason the fallback is the
    // parameter rather than a literal.
    envEmbedCheck->setChecked(renderFlag(view, "PBREnvEmbed",
                                         RenderParams::getPBREnvEmbed()));
    if (auto prop = renderProp<App::PropertyEnumeration>(view, "MatcapPreset"))
        matcapCombo->setCurrentIndex(int(prop->getValue()));
    if (auto prop = renderProp<App::PropertyFloat>(view, "MatcapTint"))
        matcapTintSlider->setValue(int(prop->getValue() * 100.0 + 0.5));
    else
        matcapTintSlider->setValue(int(RenderParams::getMatcapTint() * 100.0
                                       + 0.5));
    // As with the radius: valueChanged is silent when the value has not
    // moved, so the readout is written here rather than left to the signal.
    matcapTintValue->setText(tr("%1 %").arg(matcapTintSlider->value()));
    cavityCheck->setChecked(renderFlag(view, "Cavity", false));
    if (auto prop = renderProp<App::PropertyFloat>(view, "CavityRadius"))
        cavityRadiusSlider->setValue(int(prop->getValue() + 0.5));
    else
        cavityRadiusSlider->setValue(int(RenderParams::getCavityRadius() + 0.5));
    // valueChanged does not fire when the value is already what it was,
    // so the readout is set here rather than left to the signal.
    cavityRadiusValue->setText(tr("%1 px").arg(cavityRadiusSlider->value()));
    aoCheck->setChecked(renderFlag(view, "AO", false));
    // Shadows are the renderer's scene light: Render_Shadow only drops
    // the map of a light Render_Light provides, so the switch follows
    // the light.
    shadowCheck->setChecked(renderFlag(view, "Light", false));
    bloomCheck->setChecked(renderFlag(view, "Bloom", false));

    classicRadio->setEnabled(available);
    pbrRadio->setEnabled(available);
    matcapRadio->setEnabled(available);
    envLabel->setEnabled(available && pbr && !matcap);
    envCombo->setEnabled(available && pbr && !matcap);
    envBgCheck->setEnabled(available && pbr && !matcap);
    updateEnvEmbedEnabled();
    matcapLabel->setEnabled(available && matcap);
    matcapCombo->setEnabled(available && matcap);
    updateMatcapTintEnabled();
    cavityCheck->setEnabled(available);
    updateCavityRadiusEnabled();
    aoCheck->setEnabled(available);
    shadowCheck->setEnabled(available);
    bloomCheck->setEnabled(available);
    hint->setVisible(!available);
}

void ShadingOptionsWidget::install(QMenu *menu)
{
    if (!menu)
        return;
    auto widget = menu->findChild<ShadingOptionsWidget*>();
    if (!widget) {
        menu->addSeparator();
        auto action = new QWidgetAction(menu);
        widget = new ShadingOptionsWidget(menu);
        action->setDefaultWidget(widget);
        menu->addAction(action);
    }
    widget->refresh();
}

#include "moc_ShadingOptions.cpp"
