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
# include <QGridLayout>
# include <QHBoxLayout>
# include <QLabel>
# include <QMenu>
# include <QRadioButton>
# include <QSlider>
# include <QWidgetAction>
#endif

#include <App/PropertyContainer.h>
#include <App/PropertyStandard.h>
#include <Base/Tools.h>

#include "ShadingOptions.h"
#include "Application.h"
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

/// Shadow_DisplayMode: which draw style the Shadow mode hosts. Absent
/// until the Shadow draw style has been entered once.
App::PropertyEnumeration *shadowStyleProp(App::PropertyContainer *view)
{
    return viewProp<App::PropertyEnumeration>(view, "Shadow", "DisplayMode");
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
    defaultRadio = new QRadioButton(tr("Default"), this);
    defaultRadio->setToolTip(tr("The renderer's default headlight shading"));
    pbrRadio = new QRadioButton(tr("Realistic"), this);
    pbrRadio->setToolTip(doc(RenderParams::docPBR()));
    matcapRadio = new QRadioButton(tr("Matcap"), this);
    matcapRadio->setToolTip(doc(RenderParams::docMatcap()));
    auto models = new QButtonGroup(this);
    models->addButton(defaultRadio);
    models->addButton(pbrRadio);
    models->addButton(matcapRadio);
    modelRow->addWidget(defaultRadio);
    modelRow->addWidget(pbrRadio);
    modelRow->addWidget(matcapRadio);
    modelRow->addStretch();
    layout->addWidget(new QLabel(tr("Model:"), this), 1, 0);
    layout->addLayout(modelRow, 1, 1);

    matcapLabel = new QLabel(tr("Matcap:"), this);
    matcapCombo = new QComboBox(this);
    matcapCombo->addItem(tr("Studio"));
    matcapCombo->addItem(tr("Clay"));
    matcapCombo->addItem(tr("Metal"));
    matcapCombo->addItem(tr("Pearl"));
    matcapCombo->setToolTip(doc(RenderParams::docMatcapPreset()));
    layout->addWidget(matcapLabel, 2, 0);
    layout->addWidget(matcapCombo, 2, 1);

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
           "This is the Shadow draw style: it is what puts a scene light in "
           "the graph at all,\nso it is a shading switch rather than a style, "
           "and it hosts the style you\nare in rather than replacing it. "
           "Its light, ground plane and shadow quality\nare the Shadow_* "
           "properties of the view."));
    bloomCheck = new QCheckBox(tr("Bloom"), this);
    bloomCheck->setToolTip(doc(RenderParams::docBloom()));
    flags->addWidget(cavityCheck, 0, 0);
    flags->addWidget(aoCheck, 0, 1);
    flags->addWidget(shadowCheck, 1, 0);
    flags->addWidget(bloomCheck, 1, 1);
    layout->addLayout(flags, 3, 0, 1, 2);

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
    layout->addWidget(cavityRadiusLabel, 4, 0);
    layout->addLayout(radiusRow, 4, 1);

    hint = new QLabel(tr("Needs the render engine: set the render cache "
                         "to the renderer mode\nand pick a renderer type "
                         "in the 3D view preferences."), this);
    hint->setEnabled(false);
    layout->addWidget(hint, 5, 0, 1, 2);

    connect(defaultRadio, &QRadioButton::toggled, this, [this](bool on) {
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
    connect(matcapCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyEnumeration>(activeView(),
                                                            "MatcapPreset"))
            prop->setValue(long(index));
    });
    connect(cavityCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Cavity", on);
        updateCavityRadiusEnabled();
    });
    connect(cavityRadiusSlider, &QSlider::valueChanged, this, [this](int value) {
        cavityRadiusValue->setText(tr("%1 px").arg(value));
        if (loading)
            return;
        if (auto prop = renderProp<App::PropertyFloat>(activeView(),
                                                       "CavityRadius"))
            prop->setValue(double(value));
    });
    connect(aoCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("AO", on);
    });
    connect(shadowCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!loading)
            setShadow(on);
    });
    connect(bloomCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Bloom", on);
    });
}

App::PropertyContainer *ShadingOptionsWidget::activeView() const
{
    return qobject_cast<View3DInventor*>(Application::Instance->activeView());
}

View3DInventorViewer *ShadingOptionsWidget::activeViewer() const
{
    auto view = qobject_cast<View3DInventor*>(Application::Instance->activeView());
    return view ? view->getViewer() : nullptr;
}

namespace {

// The four styles the Shadow draw style can host, in Shadow_DisplayMode
// order (View3DInventorViewer::Private::activateShadow).
const char * const _shadowStyles[] = {"Flat Lines", "Shaded", "As Is",
                                      "Hidden Line"};
constexpr int kShadowStyleAsIs = 2;

int shadowStyleIndex(const std::string &mode)
{
    for (int i = 0; i < 4; ++i) {
        if (mode == _shadowStyles[i])
            return i;
    }
    // Points, Wireframe, No Shading and Tessellation have no shadow-mode
    // equivalent; they come back as As Is.
    return kShadowStyleAsIs;
}

} // namespace

void ShadingOptionsWidget::setShadow(bool on)
{
    auto viewer = activeViewer();
    if (!viewer)
        return;
    const bool active = viewer->getOverrideMode() == "Shadow";
    if (on == active)
        return;

    if (on) {
        // Carry the style the user is looking at into shadow mode, so the
        // scene keeps its appearance and only gains the light.
        const int index = shadowStyleIndex(viewer->getOverrideMode());
        viewer->setOverrideMode("Shadow");
        // Shadow_DisplayMode is materialized by entering the mode, so it
        // can only be set afterwards; the property change re-applies the
        // override (View3DInventorViewer::onViewPropertyChanged).
        if (auto prop = shadowStyleProp(activeView()))
            prop->setValue(long(index));
        if (auto prop = renderProp<App::PropertyBool>(activeView(), "Shadow"))
            prop->setValue(true);
    }
    else {
        // Hand the hosted style back as the plain draw style.
        int index = kShadowStyleAsIs;
        if (auto prop = shadowStyleProp(activeView()))
            index = int(prop->getValue());
        if (index < 0 || index > 3)
            index = kShadowStyleAsIs;
        viewer->setOverrideMode(_shadowStyles[index]);
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

void ShadingOptionsWidget::setModel(bool pbr, bool matcap)
{
    auto view = activeView();
    if (auto prop = renderProp<App::PropertyBool>(view, "PBR"))
        prop->setValue(pbr);
    if (auto prop = renderProp<App::PropertyBool>(view, "Matcap"))
        prop->setValue(matcap);
    matcapLabel->setEnabled(matcap);
    matcapCombo->setEnabled(matcap);
}

void ShadingOptionsWidget::setFlag(const char *name, bool value)
{
    if (loading)
        return;
    if (auto prop = renderProp<App::PropertyBool>(activeView(), name))
        prop->setValue(value);
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
    defaultRadio->setChecked(!pbr && !matcap);
    if (auto prop = renderProp<App::PropertyEnumeration>(view, "MatcapPreset"))
        matcapCombo->setCurrentIndex(int(prop->getValue()));
    cavityCheck->setChecked(renderFlag(view, "Cavity", false));
    if (auto prop = renderProp<App::PropertyFloat>(view, "CavityRadius"))
        cavityRadiusSlider->setValue(int(prop->getValue() + 0.5));
    else
        cavityRadiusSlider->setValue(int(RenderParams::getCavityRadius() + 0.5));
    // valueChanged does not fire when the value is already what it was,
    // so the readout is set here rather than left to the signal.
    cavityRadiusValue->setText(tr("%1 px").arg(cavityRadiusSlider->value()));
    aoCheck->setChecked(renderFlag(view, "AO", false));
    // Shadows follow the draw style, not Render_Shadow: without the style
    // there is no scene light to cast one, and Render_Shadow only drops
    // the map of a light the style already provides.
    auto viewer = activeViewer();
    shadowCheck->setChecked(viewer && viewer->getOverrideMode() == "Shadow");
    bloomCheck->setChecked(renderFlag(view, "Bloom", false));

    defaultRadio->setEnabled(available);
    pbrRadio->setEnabled(available);
    matcapRadio->setEnabled(available);
    matcapLabel->setEnabled(available && matcap);
    matcapCombo->setEnabled(available && matcap);
    cavityCheck->setEnabled(available);
    updateCavityRadiusEnabled();
    aoCheck->setEnabled(available);
    // The one control here that is not renderer-only: the Shadow draw
    // style predates the backend and works on the plain Coin path too.
    shadowCheck->setEnabled(viewer != nullptr);
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
