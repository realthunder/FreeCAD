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
# include <QComboBox>
# include <QDoubleSpinBox>
# include <QEvent>
# include <QFileInfo>
# include <QFormLayout>
# include <QGridLayout>
# include <QHBoxLayout>
# include <QLabel>
# include <QMenu>
# include <QPointer>
# include <QPushButton>
# include <QSlider>
# include <QSpinBox>
# include <QStandardItemModel>
# include <QWidgetAction>
#endif

#include <App/PropertyContainer.h>
#include <App/PropertyFile.h>
#include <App/PropertyStandard.h>
#include <Base/Tools.h>

#include "ShadingOptions.h"
#include "Application.h"
#include "FileDialog.h"
#include "MainWindow.h"
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

    // The one genuinely exclusive choice here: each of the four replaces
    // the surface's response to light, so at most one can be on. A combo
    // rather than a row of radios: the list has grown to where the radios
    // read as four separate switches, and a closed combo states the one
    // fact that matters -- which model this view is in.
    auto modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(0, 0, 0, 0);
    modelCombo = new QComboBox(this);
    // The character of each model goes on its entry, the same way the
    // environment presets carry theirs: the tip belongs to the choice
    // being weighed, not to the combo as a whole.
    auto addModel = [this](const QString &name, const QString &tip) {
        modelCombo->addItem(name);
        modelCombo->setItemData(modelCombo->count() - 1, tip,
                                Qt::ToolTipRole);
    };
    // Named for what it IS, not for its position in the list. "Default"
    // described only the fact that a view starts in it, which stops being
    // true the moment that changes and never said anything about the
    // shading either way.
    addModel(tr("Classic"),
             tr("Blinn-Phong headlight shading: the light follows the camera "
                "and the surface has no environment around it. What a view "
                "starts in."));
    addModel(tr("Realistic"), doc(RenderParams::docPBR()));
    addModel(tr("Matcap"), doc(RenderParams::docMatcap()));
    addModel(tr("External"),
             tr("Hand the view to an external path tracer (Cycles): real\n"
                "reflections, refraction and soft shadows, refining\n"
                "progressively over the raster frame and restarting on every\n"
                "change. Which device and how hard it refines is behind\n"
                "Settings..."));
    modelCombo->setToolTip(
        tr("How a surface answers light: the shading model this view is "
           "drawn with."));
    modelRow->addWidget(modelCombo, 1);
    layout->addWidget(new QLabel(tr("Model:"), this), 1, 0);
    layout->addLayout(modelRow, 1, 1);

    // External's knobs live behind a button rather than in rows here:
    // they are set once per machine (which device, what budget), not
    // reached for while modelling the way the environment or the tint
    // is, and five rows of session tuning would swamp the section. The
    // button shows only while External is selected -- for the other
    // models it configures nothing -- and opens on hover, like the
    // submenu item it stands in for.
    externalSettings = new QPushButton(tr("Settings..."), this);
    externalSettings->setToolTip(
        tr("The external renderer and its session options: device,\n"
           "samples, time limit, denoise, preview pixel size."));
    // Hidden still takes its space: the section lives inside an open
    // menu, whose widget action is not re-measured when the model
    // choice makes the button appear -- growing the row then would
    // push it past the menu's laid-out width.
    QSizePolicy settingsPolicy = externalSettings->sizePolicy();
    settingsPolicy.setRetainSizeWhenHidden(true);
    externalSettings->setSizePolicy(settingsPolicy);
    externalSettings->installEventFilter(this);
    modelRow->addWidget(externalSettings);

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
    // A chosen image puts its file name on the combo; without a cap that
    // name is what sizes the whole menu.
    envCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    envCombo->setMinimumContentsLength(12);
    layout->addWidget(envLabel, 2, 0);
    layout->addWidget(envCombo, 2, 1);
    // The two switches under the combo rather than beside it: on one
    // row the three of them set the width of the whole menu.
    auto envChecks = new QHBoxLayout;
    envChecks->setContentsMargins(0, 0, 0, 0);
    envChecks->setSpacing(12);
    envChecks->addWidget(envBgCheck);
    envChecks->addWidget(envEmbedCheck);
    envChecks->addStretch();
    layout->addLayout(envChecks, 3, 1);

    // How much of the environment is left to see. It belongs beside
    // the switch that draws it and not in a preference page, because
    // it is the answer to a question asked with the 3D view in sight:
    // the same view under the external path tracer shows the backdrop
    // sharp, and until this existed there was no way to say which of
    // the two was wanted. Blender puts the same slider in the same
    // place, next to the environment its viewport shading is standing
    // in.
    envBlurLabel = new QLabel(tr("Env blur:"), this);
    envBlurSlider = new QSlider(Qt::Horizontal, this);
    envBlurSlider->setRange(0, 100);
    envBlurSlider->setPageStep(10);
    envBlurSlider->setToolTip(doc(RenderParams::docPBREnvBlur()));
    envBlurLabel->setToolTip(envBlurSlider->toolTip());
    envBlurValue = new QLabel(this);
    envBlurValue->setMinimumWidth(
        envBlurValue->fontMetrics().horizontalAdvance(tr("000 %")));
    auto blurRow = new QHBoxLayout;
    blurRow->setContentsMargins(0, 0, 0, 0);
    blurRow->addWidget(envBlurSlider, 1);
    blurRow->addWidget(envBlurValue);
    layout->addWidget(envBlurLabel, 4, 0);
    layout->addLayout(blurRow, 4, 1);

    matcapLabel = new QLabel(tr("Matcap:"), this);
    matcapCombo = new QComboBox(this);
    matcapCombo->addItem(tr("Studio"));
    matcapCombo->addItem(tr("Clay"));
    matcapCombo->addItem(tr("Metal"));
    matcapCombo->addItem(tr("Pearl"));
    matcapCombo->setToolTip(doc(RenderParams::docMatcapPreset()));
    layout->addWidget(matcapLabel, 5, 0);
    layout->addWidget(matcapCombo, 5, 1);

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
    layout->addWidget(matcapTintLabel, 6, 0);
    layout->addLayout(tintRow, 6, 1);

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
    layout->addLayout(flags, 7, 0, 1, 2);

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
    layout->addWidget(cavityRadiusLabel, 8, 0);
    layout->addLayout(radiusRow, 8, 1);

    // Not "pick a renderer type in the preferences" any more: the
    // render path stopped being a stored choice in ca372262f1, and
    // both controls left the preference page with it. What is left
    // to say is that the engine is not running, which now means this
    // build has no backend or the one it has could not start --
    // neither of them something to fix in a dialog.
    hint = new QLabel(tr("Needs the render engine, which is not "
                         "running on this view."), this);
    hint->setEnabled(false);
    layout->addWidget(hint, 9, 0, 1, 2);

    connect(modelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0 && !loading)
            setModel(long(index));
    });
    // Hover is the way in (the event filter), but a click must not be a
    // dead end for whoever aims and presses anyway.
    connect(externalSettings, &QPushButton::clicked, this, [this]() {
        showExternalSettings();
    });
    connect(envBgCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("PBREnvBackground", on, &RenderParams::setPBREnvBackground);
        updateEnvBlurEnabled();
    });
    connect(envBlurSlider, &QSlider::valueChanged, this, [this](int value) {
        envBlurValue->setText(tr("%1 %").arg(value));
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyFloat>(activeView(),
                                                       "PBREnvBlur"))
            prop->setValue(double(value) / 100.0);
        RenderParams::setPBREnvBlur(double(value) / 100.0);
    });
    connect(envEmbedCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("PBREnvEmbed", on, &RenderParams::setPBREnvEmbed);
    });
    // activated, not currentIndexChanged, and for two reasons. Picking
    // the entry that is ALREADY current is still a request: with a file
    // loaded the combo sits on the image entry, and currentIndexChanged
    // is silent there, so "choose a different image" had no way in. And
    // activated is emitted only for a pick the user made, which is what
    // these writes are for -- refresh's own setCurrentIndex is silent
    // by construction rather than by the loading guard.
    connect(envCombo, &QComboBox::activated, this, [this](int index) {
        if (loading || index < 0)
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
    const bool on = matcapCombo->isEnabled();
    matcapTintLabel->setEnabled(on);
    matcapTintSlider->setEnabled(on);
    matcapTintValue->setEnabled(on);
}

void ShadingOptionsWidget::updateExternalSettings()
{
    // Session options for a model that is not in effect configure
    // nothing, so for the other models the button is not there at all
    // -- it appears with the choice that gives it meaning. Greyed (not
    // hidden) when External is selected but the engine is missing: that
    // state deserves to look broken, because it is.
    const bool external =
        modelCombo->currentIndex() == View3DInventor::ShadingExternal;
    externalSettings->setVisible(external);
    externalSettings->setEnabled(modelCombo->isEnabled()
                                 && viewProp<App::PropertyEnumeration>(
                                         activeView(), "Cycles", "Device")
                                     != nullptr);
}

bool ShadingOptionsWidget::eventFilter(QObject *watched, QEvent *event)
{
    // The button stands in for a submenu item, so it opens the way one
    // does: the pointer arriving is the request.
    if (watched == externalSettings && event->type() == QEvent::Enter
            && externalSettings->isEnabled())
        showExternalSettings();
    return QWidget::eventFilter(watched, event);
}

void ShadingOptionsWidget::showExternalSettings()
{
    // A second hover while the popup is up raises it, not a twin of it.
    if (externalSettingsMenu) {
        externalSettingsMenu->raise();
        return;
    }
    auto mdiView = qobject_cast<View3DInventor*>(
            Application::Instance->activeView());
    if (!mdiView)
        return;
    // The device property doubles as the availability probe: it is
    // materialized exactly when the engine exists (initRenderProperties).
    auto device = viewProp<App::PropertyEnumeration>(mdiView, "Cycles",
                                                     "Device");
    if (!device) {
        refresh();
        return;
    }

    // Every control applies as it is touched -- the session restarts
    // behind the popup, which IS the feedback -- so there is no Ok or
    // Cancel, just a menu that closes the way menus close. Writes go
    // through the view looked up by name at fire time: the properties
    // can be dropped while the popup is up (backend deselected), and a
    // stale pointer must not be the thing that finds out.
    QPointer<View3DInventor> guard(mdiView);
    auto cyclesProp = [guard](const char *name) -> App::Property* {
        if (!guard)
            return nullptr;
        std::string propname("Cycles_");
        propname += name;
        return guard->getPropertyByName(propname.c_str());
    };

    auto menu = new QMenu(externalSettings);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    externalSettingsMenu = menu;
    auto panel = new QWidget(menu);
    auto form = new QFormLayout(panel);
    // The section's own margins, so the popup reads as more of the same
    // surface rather than a bare form floating in a frame.
    form->setContentsMargins(12, 8, 12, 8);

    auto rendererCombo = new QComboBox(panel);
    for (const auto &name : mdiView->ExternalRenderType.getEnumVector())
        rendererCombo->addItem(QString::fromUtf8(name.c_str()));
    rendererCombo->setCurrentIndex(int(mdiView->ExternalRenderType.getValue()));
    rendererCombo->setToolTip(
            doc(mdiView->ExternalRenderType.getDocumentation()));
    form->addRow(tr("Renderer:"), rendererCombo);
    connect(rendererCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            panel, [guard](int index) {
        // No preference behind this one on purpose: External is never
        // a default, so neither is which engine it would pick.
        if (guard && index >= 0)
            guard->ExternalRenderType.setValue(long(index));
    });

    auto deviceCombo = new QComboBox(panel);
    for (const auto &name : device->getEnumVector())
        deviceCombo->addItem(QString::fromUtf8(name.c_str()));
    if (device->isValid())
        deviceCombo->setCurrentIndex(int(device->getValue()));
    deviceCombo->setToolTip(doc(RenderParams::docCyclesDevice()));
    form->addRow(tr("Device:"), deviceCombo);
    connect(deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            panel, [cyclesProp](int index) {
        if (index < 0)
            return;
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyEnumeration>(
                    cyclesProp("Device"))) {
            prop->setValue(long(index));
            // The preference carries the NAME: an index means a
            // different device on the next machine.
            if (prop->isValid())
                RenderParams::setCyclesDevice(prop->getValueAsString());
        }
    });

    auto samplesSpin = new QSpinBox(panel);
    samplesSpin->setRange(1, 1000000);
    if (auto prop = viewProp<App::PropertyInteger>(mdiView, "Cycles",
                                                   "Samples"))
        samplesSpin->setValue(int(prop->getValue()));
    else
        samplesSpin->setValue(int(RenderParams::getCyclesSamples()));
    samplesSpin->setToolTip(doc(RenderParams::docCyclesSamples()));
    form->addRow(tr("Samples:"), samplesSpin);
    connect(samplesSpin, qOverload<int>(&QSpinBox::valueChanged),
            panel, [cyclesProp](int value) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyInteger>(
                    cyclesProp("Samples")))
            prop->setValue(long(value));
        RenderParams::setCyclesSamples(long(value));
    });

    auto timeLimitSpin = new QDoubleSpinBox(panel);
    timeLimitSpin->setRange(0.0, 3600.0);
    timeLimitSpin->setDecimals(1);
    timeLimitSpin->setSuffix(tr(" s"));
    // Zero means "the sample budget alone decides", and a reading of
    // "0.0 s" says stopped; name the meaning instead.
    timeLimitSpin->setSpecialValueText(tr("No limit"));
    if (auto prop = viewProp<App::PropertyFloat>(mdiView, "Cycles",
                                                 "TimeLimit"))
        timeLimitSpin->setValue(prop->getValue());
    else
        timeLimitSpin->setValue(RenderParams::getCyclesTimeLimit());
    timeLimitSpin->setToolTip(doc(RenderParams::docCyclesTimeLimit()));
    form->addRow(tr("Time limit:"), timeLimitSpin);
    connect(timeLimitSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            panel, [cyclesProp](double value) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyFloat>(
                    cyclesProp("TimeLimit")))
            prop->setValue(value);
        RenderParams::setCyclesTimeLimit(value);
    });

    auto denoiseCheck = new QCheckBox(panel);
    if (auto prop = viewProp<App::PropertyBool>(mdiView, "Cycles", "Denoise"))
        denoiseCheck->setChecked(prop->getValue());
    else
        denoiseCheck->setChecked(RenderParams::getCyclesDenoise());
    denoiseCheck->setToolTip(doc(RenderParams::docCyclesDenoise()));
    form->addRow(tr("Denoise:"), denoiseCheck);
    connect(denoiseCheck, &QCheckBox::toggled, panel, [cyclesProp](bool on) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyBool>(
                    cyclesProp("Denoise")))
            prop->setValue(on);
        RenderParams::setCyclesDenoise(on);
    });

    auto pixelSizeSpin = new QSpinBox(panel);
    pixelSizeSpin->setRange(1, 8);
    if (auto prop = viewProp<App::PropertyInteger>(mdiView, "Cycles",
                                                   "PixelSize"))
        pixelSizeSpin->setValue(int(prop->getValue()));
    else
        pixelSizeSpin->setValue(int(RenderParams::getCyclesPixelSize()));
    pixelSizeSpin->setToolTip(doc(RenderParams::docCyclesPixelSize()));
    form->addRow(tr("Pixel size:"), pixelSizeSpin);
    connect(pixelSizeSpin, qOverload<int>(&QSpinBox::valueChanged),
            panel, [cyclesProp](int value) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyInteger>(
                    cyclesProp("PixelSize")))
            prop->setValue(long(value));
        RenderParams::setCyclesPixelSize(long(value));
    });

    auto action = new QWidgetAction(menu);
    action->setDefaultWidget(panel);
    menu->addAction(action);

    // The popup outlives its reason to exist otherwise: the display
    // style menu closing takes the whole surface this hangs off.
    if (auto parentMenu = qobject_cast<QMenu*>(parentWidget()))
        connect(parentMenu, &QMenu::aboutToHide, menu, &QMenu::close);

    // Off the button's top-right corner, where a submenu of this entry
    // would unfold; popup() slides it back on-screen if that runs out
    // of room.
    menu->popup(externalSettings->mapToGlobal(
            QPoint(externalSettings->width(), 0)));
}

void ShadingOptionsWidget::setModel(long model)
{
    // The model is stated once, on the view's declared ShadingType;
    // the hidden Render_PBR / Render_Matcap facade and the external
    // session follow it (View3DInventor::onChanged). The preferences
    // stay the raster pair -- and External leaves them alone: it
    // cannot be a default (the app should not open straight into a
    // path tracer), so they keep the raster model to fall back to.
    if (auto view = qobject_cast<View3DInventor*>(
                Application::Instance->activeView()))
        view->ShadingType.setValue(model);
    const bool pbr = model == View3DInventor::ShadingRealistic;
    const bool matcap = model == View3DInventor::ShadingMatcap;
    if (model != View3DInventor::ShadingExternal) {
        RenderParams::setPBR(pbr);
        RenderParams::setMatcap(matcap);
    }
    // External stands in the same environment: the path tracer bakes
    // its world from these very properties, and its session forces the
    // PBR flag on so Render_PBREnvBackground is honoured there too
    // (View3DInventorViewer::feedCyclesViewport). Greyed, the row said
    // the opposite of what the frame showed.
    const bool env = envUsed(model);
    envLabel->setEnabled(env);
    envCombo->setEnabled(env);
    envBgCheck->setEnabled(env);
    updateEnvBlurEnabled();
    updateEnvEmbedEnabled();
    matcapLabel->setEnabled(matcap);
    matcapCombo->setEnabled(matcap);
    updateMatcapTintEnabled();
    updateExternalSettings();
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
        "Free ones: polyhaven.com/hdris. How sharp it is drawn behind the\n"
        "model is Env blur, below.");
    if (current.isEmpty())
        return tip;
    // The file first: once one is loaded, which one is the question this
    // entry raises, and the instructions are behind it.
    return current + QLatin1String("\n\n") + tip;
}

bool ShadingOptionsWidget::envUsed(long model)
{
    // Both models stand the scene in this environment. Only the raster
    // ones that do not -- Classic and Matcap -- leave the row dead.
    return model == View3DInventor::ShadingRealistic
        || model == View3DInventor::ShadingExternal;
}

void ShadingOptionsWidget::updateEnvBlurEnabled()
{
    // The blur softens the background pass and nothing else, so with
    // the background off it is a control that does nothing.
    envBlurLabel->setEnabled(envBgCheck->isEnabled()
                             && envBgCheck->isChecked());
    envBlurSlider->setEnabled(envBlurLabel->isEnabled());
    envBlurValue->setEnabled(envBlurLabel->isEnabled());
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
    if (!renderProp<App::PropertyFile>(activeView(), "PBREnvImage")) {
        refresh();
        return;
    }
    // The menu holds a popup grab, and a file dialog raised under one
    // closes it on the first click anyway. Dropping it here makes the
    // sequence the same every time instead of style dependent.
    if (auto menu = qobject_cast<QMenu*>(parentWidget()))
        menu->close();
    // ...and the dialog opens from the event loop rather than from
    // inside the combo's own activation, because closing the menu is
    // not the end of the grabs. The combo's popup still holds the
    // mouse and keyboard while the handler that emitted this runs --
    // it hides on the way out of it -- and a modal dialog raised under
    // that grab gets no input at all: on Windows nothing appeared and
    // the entry read as a dead control. Queued, every popup is down by
    // the time the dialog is asked for.
    QPointer<ShadingOptionsWidget> self(this);
    QMetaObject::invokeMethod(this, [self]() {
        if (self)
            self->openEnvImage();
    }, Qt::QueuedConnection);
}

void ShadingOptionsWidget::openEnvImage()
{
    auto prop = renderProp<App::PropertyFile>(activeView(), "PBREnvImage");
    if (!prop) {
        refresh();
        return;
    }
    // Parented to the main window, NOT to this widget. A dialog takes
    // its transient parent from its parent's WINDOW, and this widget's
    // window is the menu -- the one just closed above. Windows then
    // owns a modal dialog to a hidden popup: it is created, it is
    // never raised or activated, and the entry reads as a control that
    // does nothing. The main window is the surface this belongs to
    // once the menu is gone anyway.
    QPointer<ShadingOptionsWidget> self(this);
    QString path = FileDialog::getOpenFileName(
            getMainWindow(), tr("Environment image"),
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
    // The external model additionally needs the Cycles engine in the
    // build; its device property is materialized exactly when it is
    // (initRenderProperties), so existence is the probe -- the same
    // move the PBR flag makes for the section.
    bool cyclesAvailable =
        viewProp<App::PropertyEnumeration>(view, "Cycles", "Device")
            != nullptr;

    Base::StateLocker guard(loading);

    // The declared enum is the single stated truth of the model
    // (part of the view, so it exists backend or not); the facade
    // bools cannot say External, which reads off them as Classic.
    long model = View3DInventor::ShadingClassic;
    if (auto mdiView = qobject_cast<View3DInventor*>(
                Application::Instance->activeView()))
        model = mdiView->ShadingType.getValue();
    const bool matcap = model == View3DInventor::ShadingMatcap;
    modelCombo->setCurrentIndex(int(model));
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
    if (auto prop = renderProp<App::PropertyFloat>(view, "PBREnvBlur"))
        envBlurSlider->setValue(int(prop->getValue() * 100.0 + 0.5));
    else
        envBlurSlider->setValue(int(RenderParams::getPBREnvBlur() * 100.0
                                    + 0.5));
    // valueChanged is silent when the value has not moved, so the
    // readout is written here rather than left to the signal.
    envBlurValue->setText(tr("%1 %").arg(envBlurSlider->value()));
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

    modelCombo->setEnabled(available);
    // The whole combo stays live without the Cycles engine -- only its
    // External entry has nothing behind it then, so only that entry
    // greys out.
    if (auto items = qobject_cast<QStandardItemModel*>(modelCombo->model())) {
        if (auto item = items->item(View3DInventor::ShadingExternal))
            item->setEnabled(available && cyclesAvailable);
    }
    updateExternalSettings();
    const bool env = available && envUsed(model);
    envLabel->setEnabled(env);
    envCombo->setEnabled(env);
    envBgCheck->setEnabled(env);
    updateEnvBlurEnabled();
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
