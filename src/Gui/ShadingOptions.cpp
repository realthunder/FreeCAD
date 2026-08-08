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
# include <QWidgetAction>
#endif

#include <App/PropertyContainer.h>
#include <App/PropertyStandard.h>
#include <Base/Tools.h>

#include "ShadingOptions.h"
#include "Application.h"
#include "RenderParams.h"
#include "View3DInventor.h"

using namespace Gui;

namespace {

/// The Render_* property of \a view named \a name, if it is there and of
/// the expected type. Absent means no renderer backend is selected on
/// this view -- the properties are materialized by
/// View3DInventorViewer::setRendererType, not by the config feed.
template<class PropT>
PropT *renderProp(App::PropertyContainer *view, const char *name)
{
    if (!view)
        return nullptr;
    std::string propname("Render_");
    propname += name;
    auto prop = view->getPropertyByName(propname.c_str());
    if (!prop || !prop->isDerivedFrom(PropT::getClassTypeId()))
        return nullptr;
    return static_cast<PropT*>(prop);
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
    shadowCheck->setToolTip(doc(RenderParams::docShadow()));
    bloomCheck = new QCheckBox(tr("Bloom"), this);
    bloomCheck->setToolTip(doc(RenderParams::docBloom()));
    flags->addWidget(cavityCheck, 0, 0);
    flags->addWidget(aoCheck, 0, 1);
    flags->addWidget(shadowCheck, 1, 0);
    flags->addWidget(bloomCheck, 1, 1);
    layout->addLayout(flags, 3, 0, 1, 2);

    hint = new QLabel(tr("Needs the render engine: set the render cache "
                         "to the renderer mode\nand pick a renderer type "
                         "in the 3D view preferences."), this);
    hint->setEnabled(false);
    layout->addWidget(hint, 4, 0, 1, 2);

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
    });
    connect(aoCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("AO", on);
    });
    connect(shadowCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Shadow", on);
    });
    connect(bloomCheck, &QCheckBox::toggled, this, [this](bool on) {
        setFlag("Bloom", on);
    });
}

App::PropertyContainer *ShadingOptionsWidget::activeView() const
{
    return qobject_cast<View3DInventor*>(Application::Instance->activeView());
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
    aoCheck->setChecked(renderFlag(view, "AO", false));
    shadowCheck->setChecked(renderFlag(view, "Shadow", true));
    bloomCheck->setChecked(renderFlag(view, "Bloom", false));

    defaultRadio->setEnabled(available);
    pbrRadio->setEnabled(available);
    matcapRadio->setEnabled(available);
    matcapLabel->setEnabled(available && matcap);
    matcapCombo->setEnabled(available && matcap);
    cavityCheck->setEnabled(available);
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
