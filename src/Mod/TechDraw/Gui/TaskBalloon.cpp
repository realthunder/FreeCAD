/***************************************************************************
 *   Copyright (c) 2016 WandererFan <wandererfan@gmail.com>                *
 *   Copyright (c) 2019 Franck Jullien <franck.jullien@gmail.com>          *
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
# include <cmath>
#endif // #ifndef _PreComp_

#include <App/Document.h>
#include <Base/Console.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/Control.h>

#include "TaskBalloon.h"
#include "ui_TaskBalloon.h"
#include "DrawGuiUtil.h"
#include "QGIViewBalloon.h"
#include "ViewProviderBalloon.h"


using namespace Gui;
using namespace TechDraw;
using namespace TechDrawGui;

TaskBalloon::TaskBalloon(QGIViewBalloon *parent, ViewProviderBalloon *balloonVP) :
    ui(new Ui_TaskBalloon)
{
    int i = 0;
    (void)parent;
    m_balloon = balloonVP->getObject();
    auto feat = getBalloonFeat();

    ui->setupUi(this);

    ui->qsbShapeScale->setValue(feat->ShapeScale.getValue());
    connect(ui->qsbShapeScale, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskBalloon::onShapeScaleChanged);

    ui->qsbSymbolScale->setValue(feat->EndTypeScale.getValue());
    connect(ui->qsbSymbolScale, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskBalloon::onEndSymbolScaleChanged);

    std::string value = feat->Text.getValue();
    QString qs = QString::fromUtf8(value.data(), value.size());
    ui->leText->setText(qs);
    ui->leText->selectAll();
    connect(ui->leText, &QLineEdit::textChanged, this, &TaskBalloon::onTextChanged);
    QTimer::singleShot(0, ui->leText, qOverload<>(&QLineEdit::setFocus));

    DrawGuiUtil::loadArrowBox(ui->comboEndSymbol);
    i = feat->EndType.getValue();
    ui->comboEndSymbol->setCurrentIndex(i);
    connect(ui->comboEndSymbol, qOverload<int>(&QComboBox::currentIndexChanged), this, &TaskBalloon::onEndSymbolChanged);

    i = feat->BubbleShape.getValue();
    ui->comboBubbleShape->setCurrentIndex(i);
    connect(ui->comboBubbleShape, qOverload<int>(&QComboBox::currentIndexChanged), this, &TaskBalloon::onBubbleShapeChanged);

    ui->qsbFontSize->setUnit(Base::Unit::Length);
    ui->qsbFontSize->setMinimum(0);

    ui->qsbLineWidth->setUnit(Base::Unit::Length);
    ui->qsbLineWidth->setSingleStep(0.100);
    ui->qsbLineWidth->setMinimum(0);

    // negative kink length is allowed, thus no minimum
    ui->qsbKinkLength->setUnit(Base::Unit::Length);

    if (balloonVP) {
        ui->textColor->setColor(balloonVP->Color.getValue().asValue<QColor>());
        connect(ui->textColor, &ColorButton::changed, this, &TaskBalloon::onColorChanged);
        ui->qsbFontSize->setValue(balloonVP->Fontsize.getValue());
        ui->comboLineVisible->setCurrentIndex(balloonVP->LineVisible.getValue());
        ui->qsbLineWidth->setValue(balloonVP->LineWidth.getValue());
    }
    // new balloons have already the preferences BalloonKink length
    ui->qsbKinkLength->setValue(feat->KinkLength.getValue());

    connect(ui->qsbFontSize, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskBalloon::onFontsizeChanged);
    connect(ui->comboLineVisible, qOverload<int>(&QComboBox::currentIndexChanged), this, &TaskBalloon::onLineVisibleChanged);
    connect(ui->qsbLineWidth, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskBalloon::onLineWidthChanged);
    connect(ui->qsbKinkLength, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskBalloon::onKinkLengthChanged);

}

TaskBalloon::~TaskBalloon()
{
}

bool TaskBalloon::accept()
{
    if (auto vp = getBalloonView()) {
        setupTransaction();
        Gui::Document* doc = vp->getDocument();
        Gui::Command::updateActive();
        Gui::Command::commitCommand();
        doc->resetEdit();
    }

    return true;
}

bool TaskBalloon::reject()
{
    App::GetApplication().closeActiveTransaction(true);
    if (auto vp = getBalloonView()) {
        Gui::Document* doc = vp->getDocument();
        doc->resetEdit();
    }

    return true;
}

void TaskBalloon::setupTransaction() {
    int tid = 0;
    const char *name = App::GetApplication().getActiveTransaction(&tid);
    if(tid && tid == m_transactionID)
        return;

    std::string n(QT_TRANSLATE_NOOP("Command", "Edit "));
    n += m_balloon.getObjectName();
    if(!name || n != name)
        App::GetApplication().setActiveTransaction(n.c_str());

    if(!m_transactionID)
        m_transactionID = tid;
}

void TaskBalloon::recomputeFeature()
{
    Gui::Command::updateActive();
}

DrawViewBalloon *TaskBalloon::getBalloonFeat()
{
    return Base::freecad_dynamic_cast<DrawViewBalloon>(m_balloon.getObject());
}

ViewProviderBalloon *TaskBalloon::getBalloonView()
{
    return Base::freecad_dynamic_cast<ViewProviderBalloon>(
            Gui::Application::Instance->getViewProvider(getBalloonFeat()));
}

void TaskBalloon::onTextChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->Text.setValue(ui->leText->text().toUtf8().constData());
        recomputeFeature();
    }
}

void TaskBalloon::onColorChanged()
{
    if (auto vp = getBalloonView()) {
        setupTransaction();
        App::Color ac;
        ac.setValue<QColor>(ui->textColor->color());
        vp->Color.setValue(ac);
        recomputeFeature();
    }
}

void TaskBalloon::onFontsizeChanged()
{
    if (auto vp = getBalloonView()) {
        setupTransaction();
        vp->Fontsize.setValue(ui->qsbFontSize->value().getValue());
        recomputeFeature();
    }
}

void TaskBalloon::onBubbleShapeChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->BubbleShape.setValue(ui->comboBubbleShape->currentIndex());
        recomputeFeature();
    }
}

void TaskBalloon::onShapeScaleChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->ShapeScale.setValue(ui->qsbShapeScale->value().getValue());
        recomputeFeature();
    }
}

void TaskBalloon::onEndSymbolChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->EndType.setValue(ui->comboEndSymbol->currentIndex());
        recomputeFeature();
    }
}

void TaskBalloon::onEndSymbolScaleChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->EndTypeScale.setValue(ui->qsbSymbolScale->value().getValue());
        recomputeFeature();
    }
}

void TaskBalloon::onLineVisibleChanged()
{
    if (auto vp = getBalloonView()) {
        setupTransaction();
        vp->LineVisible.setValue(ui->comboLineVisible->currentIndex());
        recomputeFeature();
    }
}

void TaskBalloon::onLineWidthChanged()
{
    if (auto vp = getBalloonView()) {
        setupTransaction();
        vp->LineWidth.setValue(ui->qsbLineWidth->value().getValue());
        recomputeFeature();
    }
}

void TaskBalloon::onKinkLengthChanged()
{
    if (auto feat = getBalloonFeat()) {
        setupTransaction();
        feat->KinkLength.setValue(ui->qsbKinkLength->value().getValue());
        recomputeFeature();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TaskDlgBalloon::TaskDlgBalloon(QGIViewBalloon *parent, ViewProviderBalloon *balloonVP) :
    TaskDialog()
{
    widget  = new TaskBalloon(parent, balloonVP);
    taskbox = new Gui::TaskView::TaskBox(Gui::BitmapFactory().pixmap("TechDraw_Balloon"), widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
    setAutoCloseOnTransactionChange(true);
}

TaskDlgBalloon::~TaskDlgBalloon()
{
}

void TaskDlgBalloon::update()
{
    //widget->updateTask();
}

//==== calls from the TaskView ===============================================================
void TaskDlgBalloon::open()
{
    App::GetApplication().setActiveTransaction("Edit balloon",true);
}

void TaskDlgBalloon::clicked(int i)
{
    Q_UNUSED(i);
}

bool TaskDlgBalloon::accept()
{
    widget->accept();
    return true;
}

bool TaskDlgBalloon::reject()
{
    widget->reject();
    return true;
}

#include <Mod/TechDraw/Gui/moc_TaskBalloon.cpp>
