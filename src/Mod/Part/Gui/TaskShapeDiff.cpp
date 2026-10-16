/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#endif

#include <Base/Quantity.h>
#include <Base/UnitsApi.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/TaskView/TaskView.h>

#include "TaskShapeDiff.h"
#include "ViewProviderExt.h"

using namespace PartGui;

/* TRANSLATOR PartGui::ShapeDiffPanel */

ShapeDiffPanel::ShapeDiffPanel(ViewProviderPartExt* vp, QWidget* parent)
    : QWidget(parent)
    , _object(vp->getObject())
    , _prop(vp->editedDiffProperty())
    , _before(vp->editedDiffBefore())
    , _after(vp->editedDiffAfter())
{
    setWindowTitle(tr("Last change"));
    auto layout = new QVBoxLayout(this);

    _what = new QLabel(this);
    _what->setObjectName(QStringLiteral("diffWhat"));
    _what->setWordWrap(true);
    _what->setText(tr("%1: before against after")
                       .arg(QString::fromStdString(vp->editedDiffRow())));
    layout->addWidget(_what);

    _mode = new QComboBox(this);
    _mode->setObjectName(QStringLiteral("diffMode"));
    _mode->addItem(tr("By the faces"));
    _mode->addItem(tr("By the material"));
    _mode->setToolTip(tr("What became of each face, or the material only one of the two "
                         "states has"));
    layout->addWidget(_mode);

    auto row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Before"), this));
    _position = new QSlider(Qt::Horizontal, this);
    _position->setObjectName(QStringLiteral("diffPosition"));
    _position->setRange(0, 100);
    _position->setValue(100);
    _position->setToolTip(tr("Which of the two states is drawn full, the other a ghost"));
    row->addWidget(_position, 1);
    row->addWidget(new QLabel(tr("After"), this));
    layout->addLayout(row);

    _only = new QCheckBox(tr("Differences only"), this);
    _only->setObjectName(QStringLiteral("diffOnly"));
    _only->setToolTip(tr("Leave out what is the same in both"));
    layout->addWidget(_only);

    _found = new QLabel(this);
    _found->setObjectName(QStringLiteral("diffFound"));
    _found->setWordWrap(true);
    layout->addWidget(_found);

    connect(_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        apply();
    });
    connect(_position, &QSlider::valueChanged, this, [this](int) { apply(); });
    connect(_only, &QCheckBox::toggled, this, [this](bool) { apply(); });
    _pending.setInterval(300);
    connect(&_pending, &QTimer::timeout, this, [this]() { apply(); });
    apply();
}

ViewProviderPartExt* ShapeDiffPanel::view() const
{
    return Base::freecad_dynamic_cast<ViewProviderPartExt>(
        Gui::Application::Instance->getViewProvider(_object.getObject()));
}

void ShapeDiffPanel::apply()
{
    ViewProviderPartExt* vp = view();
    if (!vp || !vp->showValueDiff(_prop.c_str(), _before, _after, _position->value() / 100.f,
                                  _only->isChecked(),
                                  _mode->currentIndex() == 1 ? "material" : "faces")) {
        _pending.stop();
        _found->setText(tr("The log does not hold the two shapes of this change."));
        return;
    }
    say();
}

void ShapeDiffPanel::say()
{
    ViewProviderPartExt* vp = view();
    std::map<std::string, double> found;
    if (vp)
        found = vp->getValueDiff();
    QString text;
    if (found["material"] > 0) {
        // The three in one unit, that of what changed: the unit a volume
        // is said in goes by its size (as the log's panel says them)
        double factor = 1.;
        std::string unit;
        const double changed = std::max(found["volume.added"], found["volume.removed"]);
        Base::Quantity(changed > 0. ? changed : found["volume.common"], Base::Unit::Volume)
            .getUserString(factor, unit);
        auto volume = [&](double value) {
            return QStringLiteral("%1 %2").arg(
                QLocale().toString(value / (factor != 0. ? factor : 1.), 'f',
                                   Base::UnitsApi::getDecimals()),
                QString::fromStdString(unit));
        };
        text = tr("%1 added, %2 removed, %3 in both")
                   .arg(volume(found["volume.added"]), volume(found["volume.removed"]),
                        volume(found["volume.common"]));
    }
    else {
        text = tr("%1 as they were, %2 changed, %3 new, %4 gone")
                   .arg(static_cast<int>(found["same"]))
                   .arg(static_cast<int>(found["changed"] + found["moved"]))
                   .arg(static_cast<int>(found["added"]))
                   .arg(static_cast<int>(found["removed"]));
        if (found["material.pending"] > 0)
            text = tr("working out the material; the faces meanwhile: %1").arg(text);
        else if (found["material.none"] > 0)
            text = tr("not two solids, by the faces: %1").arg(text);
    }
    _found->setText(text);
    if (found["material.pending"] > 0)
        _pending.start();
    else
        _pending.stop();
}

/* TRANSLATOR PartGui::TaskShapeDiff */

TaskShapeDiff::TaskShapeDiff(ViewProviderPartExt* vp)
    : _object(vp->getObject())
{
    auto panel = new ShapeDiffPanel(vp);
    auto box = new Gui::TaskView::TaskBox(QPixmap(), panel->windowTitle(), true, nullptr);
    box->groupLayout()->addWidget(panel);
    Content.push_back(box);
}

bool TaskShapeDiff::leave()
{
    // The edit over, the view provider takes the two states away and
    // closes this (unsetEdit())
    App::Document* doc = _object.getDocument();
    Gui::Document* gdoc = doc ? Gui::Application::Instance->getDocument(doc) : nullptr;
    if (gdoc)
        gdoc->resetEdit();
    return true;
}

bool TaskShapeDiff::accept()
{
    return leave();
}

bool TaskShapeDiff::reject()
{
    return leave();
}

#include "moc_TaskShapeDiff.cpp"
