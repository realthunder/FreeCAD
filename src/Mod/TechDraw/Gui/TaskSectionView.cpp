/***************************************************************************
 *   Copyright (c) 2016 WandererFan <wandererfan@gmail.com>                *
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
#include <QMessageBox>
#include <cmath>
#endif// #ifndef _PreComp_

#include "Widgets/CompassWidget.h"
#include "Widgets/VectorEditWidget.h"
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>
#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/DrawViewSection.h>

// clang-format off
#include "ui_TaskSectionView.h"
#include "TaskSectionView.h"
// clang-format on


using namespace Gui;
using namespace TechDraw;
using namespace TechDrawGui;

//ctor for create
TaskSectionView::TaskSectionView(TechDraw::DrawViewPart* base) :
    ui(new Ui_TaskSectionView),
    m_base(base),
    m_section(nullptr),
    m_dirName(""),
    m_doc(nullptr),
    m_createMode(true),
    m_applyDeferred(0),
    m_directionIsSet(false),
    m_modelIsDirty(false),
    m_scaleEdited(false)
{
    //existence of base is guaranteed by CmdTechDrawSectionView (Command.cpp)

    m_sectionName = std::string();
    m_doc = m_base->getDocument();

    m_saveBaseName = m_base->getNameInDocument();
    m_savePageName = m_base->findParentPage()->getNameInDocument();

    ui->setupUi(this);
    setUiPrimary();

    m_applyDeferred = 0;//setting the direction widgets causes an increment of the deferred count,
                        //so we reset the counter and the message.

    init();
}

//ctor for edit
TaskSectionView::TaskSectionView(TechDraw::DrawViewSection* section) :
    ui(new Ui_TaskSectionView),
    m_base(nullptr),
    m_section(section),
    m_doc(nullptr),
    m_createMode(false),
    m_applyDeferred(0),
    m_directionIsSet(true),
    m_modelIsDirty(false),
    m_scaleEdited(false)
{
    //existence of section is guaranteed by ViewProviderViewSection.setEdit

    m_doc = m_section->getDocument();
    m_sectionName = m_section->getNameInDocument();
    App::DocumentObject* newObj = m_section->BaseView.getValue();
    m_base = dynamic_cast<TechDraw::DrawViewPart*>(newObj);
    if (!newObj || !m_base) {
        throw Base::RuntimeError("TaskSectionView - BaseView not found");
    }

    m_saveBaseName = m_base->getNameInDocument();
    m_savePageName = m_base->findParentPage()->getNameInDocument();

    ui->setupUi(this);

    m_dirName = m_section->SectionDirection.getValueAsString();
    setUiEdit();

    m_applyDeferred = 0;//setting the direction widgets causes an increment of the deferred count,
                        //so we reset the counter and the message.
    ui->lPendingUpdates->setText(QString());

    init();
}

void TaskSectionView::init()
{
    m_param = App::GetApplication().GetParameterGroupByPath("User parameter::BaseApp/Preferences/Mod/TechDraw/General");
    ui->cbLiveUpdate->setChecked(m_param->GetBool("SectionLiveUpdate", true));
    setupTransaction();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]{apply(true);});
}

void TaskSectionView::setUiPrimary()
{
    //    Base::Console().Message("TSV::setUiPrimary()\n");
    setWindowTitle(QObject::tr("Create Section View"));

    ui->sbScale->setValue(m_base->getScale());
    ui->cmbScaleType->setCurrentIndex(m_base->ScaleType.getValue());

    //Allow or prevent scale changing initially
    if (m_base->ScaleType.isValue("Custom")) {
        ui->sbScale->setEnabled(true);
    }
    else {
        ui->sbScale->setEnabled(false);
    }

    Base::Vector3d origin = m_base->getOriginalCentroid();
    setUiCommon(origin);

    m_compass->setDialAngle(0.0);
    m_viewDirectionWidget->setValueNoNotify(Base::Vector3d(1.0, 0.0, 0.0));

    //don't allow updates until a direction is picked
    ui->pbUpdateNow->setEnabled(false);
    ui->cbLiveUpdate->setEnabled(false);
    QString msgLiteral =
        QString::fromUtf8(QT_TRANSLATE_NOOP("TaskSectionView", "No direction set"));
    ui->lPendingUpdates->setText(msgLiteral);
}

void TaskSectionView::setUiEdit()
{
    //    Base::Console().Message("TSV::setUiEdit()\n");
    setWindowTitle(QObject::tr("Edit Section View"));
    std::string temp = m_section->SectionSymbol.getValue();
    QString qTemp = Base::Tools::fromStdString(temp);
    ui->leSymbol->setText(qTemp);

    ui->sbScale->setValue(m_section->getScale());
    ui->cmbScaleType->setCurrentIndex(m_section->ScaleType.getValue());
    //Allow or prevent scale changing initially
    if (m_section->ScaleType.isValue("Custom")) {
        ui->sbScale->setEnabled(true);
    }
    else {
        ui->sbScale->setEnabled(false);
    }

    Base::Vector3d origin = m_section->SectionOrigin.getValue();
    setUiCommon(origin);

    // convert section normal to view angle
    Base::Vector3d sectionNormalVec = m_section->SectionNormal.getValue();
    sectionNormalVec.Normalize();
    Base::Vector3d projectedViewDirection = m_base->projectPoint(sectionNormalVec, false);
    projectedViewDirection.Normalize();
    double viewAngle = atan2(-projectedViewDirection.y, -projectedViewDirection.x);
    m_compass->setDialAngle(viewAngle * 180.0 / M_PI);
    m_viewDirectionWidget->setValueNoNotify(sectionNormalVec * -1.0);
}

void TaskSectionView::setUiCommon(Base::Vector3d origin)
{
    std::string temp = m_base->getNameInDocument();
    QString qTemp = Base::Tools::fromStdString(temp);
    ui->leBaseView->setText(qTemp);

    ui->sbOrgX->setUnit(Base::Unit::Length);
    ui->sbOrgX->setValue(origin.x);
    ui->sbOrgY->setUnit(Base::Unit::Length);
    ui->sbOrgY->setValue(origin.y);
    ui->sbOrgZ->setUnit(Base::Unit::Length);
    ui->sbOrgZ->setValue(origin.z);

    enableAll(false);

    connect(ui->leSymbol, &QLineEdit::editingFinished, this, &TaskSectionView::onIdentifierChanged);

    //TODO: use event filter instead of keyboard tracking to capture enter/return keys
    // the UI file uses keyboardTracking = false so that a recomputation
    // will only be triggered when the arrow keys of the spinboxes are used
    //if this is not done, recomputes are triggered on each key press giving
    //unaccceptable UX
    connect(ui->sbScale, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskSectionView::onScaleChanged);
    connect(ui->sbOrgX, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskSectionView::onXChanged);
    connect(ui->sbOrgY, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskSectionView::onYChanged);
    connect(ui->sbOrgZ, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskSectionView::onZChanged);

    connect(ui->cmbScaleType, qOverload<int>(&QComboBox::currentIndexChanged), this, &TaskSectionView::scaleTypeChanged);

    connect(ui->pbUp, &QToolButton::clicked, this, &TaskSectionView::onUpClicked);
    connect(ui->pbDown, &QToolButton::clicked, this, &TaskSectionView::onDownClicked);
    connect(ui->pbRight, &QToolButton::clicked, this, &TaskSectionView::onRightClicked);
    connect(ui->pbLeft, &QToolButton::clicked, this, &TaskSectionView::onLeftClicked);

    connect(ui->pbUpdateNow, &QToolButton::clicked, this, &TaskSectionView::updateNowClicked);
    connect(ui->cbLiveUpdate, &QToolButton::clicked, this, &TaskSectionView::liveUpdateClicked);

    m_compass = new CompassWidget(this);
    auto layout = ui->compassLayout;
    layout->addWidget(m_compass);
    connect(m_compass, &CompassWidget::angleChanged, this, &TaskSectionView::slotChangeAngle);

    m_viewDirectionWidget = new VectorEditWidget(this);
    m_viewDirectionWidget->setLabel(QObject::tr("Current View Direction"));
    m_viewDirectionWidget->setToolTip(QObject::tr("The view direction in BaseView coordinates"));
    auto editLayout = ui->viewDirectionLayout;
    editLayout->addWidget(m_viewDirectionWidget);
    connect(m_viewDirectionWidget, &VectorEditWidget::valueChanged, this,
            &TaskSectionView::slotViewDirectionChanged);
}

//the VectorEditWidget reports a change in direction
void TaskSectionView::slotViewDirectionChanged(Base::Vector3d newDirection)
{
    //    Base::Console().Message("TSV::slotViewDirectionChanged(%s)\n",
    //                            DrawUtil::formatVector(newDirection).c_str());
    Base::Vector3d projectedViewDirection = m_base->projectPoint(newDirection, false);
    projectedViewDirection.Normalize();
    double viewAngle = atan2(projectedViewDirection.y, projectedViewDirection.x);
    m_compass->setDialAngle(viewAngle * 180.0 / M_PI);
    checkAll(false);
    applyAligned();
}

//the CompassWidget reports that the view direction angle has changed
void TaskSectionView::slotChangeAngle(double newAngle)
{
    //    Base::Console().Message("TSV::slotChangeAngle(%.3f)\n", newAngle);
    double angleRadians = newAngle * M_PI / 180.0;
    double unitX = cos(angleRadians);
    double unitY = sin(angleRadians);
    Base::Vector3d localUnit(unitX, unitY, 0.0);
    m_viewDirectionWidget->setValueNoNotify(localUnit);
    checkAll(false);
    applyAligned();
}

//preset view directions
void TaskSectionView::onUpClicked()
{
    //    Base::Console().Message("TSV::onUpClicked()\n");
    checkAll(false);
    m_compass->setToNorth();
    m_viewDirectionWidget->setValueNoNotify(Base::Vector3d(0.0, 1.0, 0.0));
    applyAligned();
}

void TaskSectionView::onDownClicked()
{
    //    Base::Console().Message("TSV::onDownClicked()\n");
    checkAll(false);
    m_compass->setToSouth();
    m_viewDirectionWidget->setValueNoNotify(Base::Vector3d(0.0, -1.0, 0.0));
    applyAligned();
}

void TaskSectionView::onLeftClicked()
{
    //    Base::Console().Message("TSV::onLeftClicked()\n");
    checkAll(false);
    m_compass->setToWest();
    m_viewDirectionWidget->setValueNoNotify(Base::Vector3d(-1.0, 0.0, 0.0));
    applyAligned();
}

void TaskSectionView::onRightClicked()
{
    //    Base::Console().Message("TSV::onRightClicked()\n");
    checkAll(false);
    m_compass->setToEast();
    m_viewDirectionWidget->setValueNoNotify(Base::Vector3d(1.0, 0.0, 0.0));
    applyAligned();
}

void TaskSectionView::onIdentifierChanged()
{
    checkAll(false);
    apply();
}

void TaskSectionView::onScaleChanged()
{
    m_scaleEdited = true;
    checkAll(false);
    apply();

}

//SectionOrigin changed
void TaskSectionView::onXChanged()
{
    checkAll(false);
    apply();
}
void TaskSectionView::onYChanged()
{
    checkAll(false);
    apply();
}
void TaskSectionView::onZChanged()
{
    checkAll(false);
    apply();
}

void TaskSectionView::scaleTypeChanged(int index)
{
    if (index == 0) {
        // Page Scale Type
        ui->sbScale->setEnabled(false);
        if (m_base->findParentPage()) {
            ui->sbScale->setValue(m_base->findParentPage()->Scale.getValue());
            ui->sbScale->setEnabled(false);
        }
    }
    else if (index == 1) {
        // Automatic Scale Type
        ui->sbScale->setEnabled(false);
        if (m_section) {
            ui->sbScale->setValue(m_section->autoScale());
        }
    }
    else if (index == 2) {
        // Custom Scale Type
        ui->sbScale->setEnabled(true);
        if (m_section) {
            ui->sbScale->setValue(m_section->Scale.getValue());
            ui->sbScale->setEnabled(true);
        }
    }
    else {
        return;
    }
}

void TaskSectionView::checkAll(bool check)
{
    ui->pbUp->setChecked(check);
    ui->pbDown->setChecked(check);
    ui->pbRight->setChecked(check);
    ui->pbLeft->setChecked(check);
}

void TaskSectionView::enableAll(bool enable)
{
    ui->leSymbol->setEnabled(enable);
    ui->sbScale->setEnabled(enable);
    ui->sbOrgX->setEnabled(enable);
    ui->sbOrgY->setEnabled(enable);
    ui->sbOrgZ->setEnabled(enable);
    ui->cmbScaleType->setEnabled(enable);
    QString qScaleType = ui->cmbScaleType->currentText();
    //Allow or prevent scale changing initially
    if (qScaleType == QString::fromUtf8("Custom")) {
        ui->sbScale->setEnabled(true);
    }
    else {
        ui->sbScale->setEnabled(false);
    }
}

void TaskSectionView::liveUpdateClicked() {
    m_param->SetBool("SectionLiveUpdate", ui->cbLiveUpdate->isChecked());
    apply(true);
}

void TaskSectionView::updateNowClicked() { apply(true); }

//******************************************************************************
bool TaskSectionView::apply(bool forceUpdate)
{
    //    Base::Console().Message("TSV::apply() - liveUpdate: %d force: %d deferred: %d\n",
    //                            ui->cbLiveUpdate->isChecked(), forceUpdate, m_applyDeferred);
    if (!forceUpdate) {
        if (ui->cbLiveUpdate->isChecked()) {
            m_timer.start(std::max(100L, m_param->GetInt("SectionUpdateDelay", 300)));
            return false;
        }
        //nothing to do
        m_applyDeferred++;
        QString msgLiteral =
            QString::fromUtf8(QT_TRANSLATE_NOOP("TaskPojGroup", " updates pending"));
        QString msgNumber = QString::number(m_applyDeferred);
        ui->lPendingUpdates->setText(msgNumber + msgLiteral);
        return false;
    }

    m_timer.stop();

    Gui::WaitCursor wc;
    m_modelIsDirty = true;

    if (m_dirName.empty()) {
        //this should never happen
        std::string msg =
            Base::Tools::toStdString(tr("Nothing to apply. No section direction picked yet"));
        Base::Console().Error((msg + "\n").c_str());
        return false;
    }
    if (!m_section) {
        m_section = createSectionView();
    }

    if (isSectionValid()) {
        updateSectionView();
    }
    else {
        failNoObject();
    }

    Gui::Command::updateActive();
    if (isBaseValid()) {
        m_base->requestPaint();
    }

    enableAll(true);
    checkAll(false);

    wc.restoreCursor();
    m_applyDeferred = 0;
    ui->lPendingUpdates->setText(QString());
    return true;
}

void TaskSectionView::applyQuick(std::string dir)
{
    //    Base::Console().Message("TSV::applyQuick(%s)\n", dir.c_str());
    m_dirName = dir;
    enableAll(true);
    apply();
}

void TaskSectionView::applyAligned()
{
    //    Base::Console().Message("TSV::applyAligned()\n");
    m_dirName = "Aligned";
    enableAll(true);
    m_directionIsSet = true;
    ui->pbUpdateNow->setEnabled(true);
    ui->cbLiveUpdate->setEnabled(true);
    apply();
}

//*********************************************************************

TechDraw::DrawViewSection* TaskSectionView::createSectionView(void)
{
    //    Base::Console().Message("TSV::createSectionView()\n");
    if (!isBaseValid()) {
        failNoObject();
        return nullptr;
    }

    setupTransaction();
    if (!m_section) {
        const std::string objectName("SectionView");
        m_sectionName = m_base->getDocument()->getUniqueObjectName(objectName.c_str());
        Gui::cmdAppDocument(m_base, std::ostringstream() 
                << "addObject('TechDraw::DrawViewSection','" << m_sectionName << "')");
        App::DocumentObject* newObj = m_base->getDocument()->getObject(m_sectionName.c_str());
        m_section = dynamic_cast<TechDraw::DrawViewSection*>(newObj);
        if (!newObj || !m_section) {
            throw Base::RuntimeError("TaskSectionView - new section object not found");
        }

        // section labels (Section A-A) are not unique, and are not the same as the object name (SectionView)
        // we pluck the generated suffix from the object name and append it to "Section" to generate
        // unique Labels
        QString qTemp = ui->leSymbol->text();
        std::string temp = Base::Tools::toStdString(qTemp);
        Gui::cmdAppObjectArgs(m_section, "SectionSymbol = '%s'", temp);

        Gui::cmdAppObjectArgs(m_section, "Label = '%s'",makeSectionLabel(qTemp));
        Gui::cmdAppObjectArgs(m_section, "translateLabel('DrawViewSection', 'Section', '%s')", makeSectionLabel(qTemp));


        TechDraw::DrawPage* page = m_base->findParentPage();
        Gui::cmdAppObjectArgs(page, "%s.addView(%s)", Gui::Command::getObjectCmd(m_section));
        Gui::cmdAppObjectArgs(m_section, "BaseView = %s", Gui::Command::getObjectCmd(m_base));
        Gui::cmdAppObjectArgs(m_section, "Source = %s.Source", Gui::Command::getObjectCmd(m_base));
        Gui::cmdAppObjectArgs(m_section, "SectionOrigin = FreeCAD.Vector(%.6f, %.6f, %.6f)",
                           ui->sbOrgX->value().getValue(),
                           ui->sbOrgY->value().getValue(),
                           ui->sbOrgZ->value().getValue());

        if (m_scaleEdited) {
            // user has changed the scale
            Gui::cmdAppObjectArgs(m_section, "Scale = %0.7f", ui->sbScale->value());
        } else {
            // scale is untouched, use value from base view
            Gui::cmdAppObjectArgs(m_section, "Scale = %s.Scale", Gui::Command::getObjectCmd(m_base));
        }

        int scaleType = ui->cmbScaleType->currentIndex();
        Gui::cmdAppObjectArgs(m_section, "ScaleType = %d", scaleType);
        Gui::cmdAppObjectArgs(m_section, "SectionDirection = '%s'", m_dirName);

        Base::Vector3d localUnit = m_viewDirectionWidget->value();
        localUnit.Normalize();
        if (m_dirName == "Aligned") {
            //localUnit is a view direction so we need to reverse it to make a
            //section normal
            m_section->setCSFromBase(localUnit * -1.0);
        }
        else {
            //Note: DirectionName is to be deprecated in the future
            m_section->setCSFromBase(m_dirName.c_str());
        }
        //auto orientation of view relative to base view
        double viewDirectionAngle = m_compass->positiveValue();
        double rotation = requiredRotation(viewDirectionAngle);
        Gui::cmdAppObjectArgs(m_section, "Rotation = %.6f", rotation);
    }
    Gui::Command::updateActive();
    return m_section;
}

void TaskSectionView::updateSectionView()
{
//    Base::Console().Message("TSV::updateSectionView() - m_sectionName: %s\n", m_sectionName.c_str());
    if (!isSectionValid()) {
        failNoObject();
        return;
    }

    setupTransaction();
    if (m_section) {
        Gui::cmdAppObjectArgs(m_section, "SectionDirection = '%s'", m_dirName);
        Gui::cmdAppObjectArgs(m_section, "SectionOrigin = FreeCAD.Vector(%.3f,%.3f,%.3f)",
                           ui->sbOrgX->value().getValue(),
                           ui->sbOrgY->value().getValue(),
                           ui->sbOrgZ->value().getValue());
        QString qTemp    = ui->leSymbol->text();
        std::string temp = Base::Tools::toStdString(qTemp);
        Gui::cmdAppObjectArgs(m_section, "SectionSymbol = '%s'", temp);
        Gui::cmdAppObjectArgs(m_section, "Label = '%s'", makeSectionLabel(qTemp));
        Gui::cmdAppObjectArgs(m_section, "translateLabel('DrawViewSection', 'Section', '%s')", makeSectionLabel(qTemp));

        if (m_scaleEdited) {
            // user has changed the scale
            Gui::cmdAppObjectArgs(m_section, "Scale = %0.7f", ui->sbScale->value());
        } else {
            // scale is untouched, use value from base view
            Gui::cmdAppObjectArgs(m_section, "Scale = %s.Scale", Gui::Command::getObjectCmd(m_base));
        }

        int scaleType = ui->cmbScaleType->currentIndex();
        Gui::cmdAppObjectArgs(m_section, "ScaleType = %d", scaleType);
        Gui::cmdAppObjectArgs(m_section, "SectionDirection = '%s'", m_dirName);
        Base::Vector3d localUnit = m_viewDirectionWidget->value();
        localUnit.Normalize();
        if (m_dirName == "Aligned") {
            //localUnit is a view direction so we need to reverse it to make a
            //section normal
            m_section->setCSFromBase(localUnit * -1.0);
        }
        else {
            //Note: DirectionName is to be deprecated in the future
            m_section->setCSFromBase(m_dirName.c_str());
        }
        //auto orientation of view relative to base view
        double viewDirectionAngle = m_compass->positiveValue();
        double rotation = requiredRotation(viewDirectionAngle);
        Gui::cmdAppObjectArgs(m_section, "Rotation = %.6f", rotation);
    }
    Gui::Command::updateActive();
}

std::string TaskSectionView::makeSectionLabel(QString symbol)
{
    const std::string objectName("SectionView");
    std::string uniqueSuffix{m_sectionName.substr(objectName.length(), std::string::npos)};
    std::string uniqueLabel = "Section" + uniqueSuffix;
    std::string temp = Base::Tools::toStdString(symbol);
    return ( uniqueLabel + " " + temp + " - " + temp );
}

void TaskSectionView::failNoObject(void)
{
    QString qsectionName = Base::Tools::fromStdString(m_sectionName);
    QString qbaseName = Base::Tools::fromStdString(m_baseName);
    QString msg = tr("Can not continue. Object * %1 or %2 not found.").arg(qsectionName, qbaseName);
    QMessageBox::critical(Gui::getMainWindow(), QObject::tr("Operation Failed"), msg);
    Gui::Control().closeDialog();
}

bool TaskSectionView::isBaseValid()
{
    if (!m_base)
        return false;

    App::DocumentObject* baseObj = m_doc->getObject(m_saveBaseName.c_str());
    if (!baseObj)
        return false;

    return true;
}

bool TaskSectionView::isSectionValid()
{
    if (!m_section)
        return false;

    App::DocumentObject* sectionObj = m_doc->getObject(m_sectionName.c_str());
    if (!sectionObj)
        return false;

    return true;
}

//get required rotation from input angle in [0, 360]
double TaskSectionView::requiredRotation(double inputAngle)
{
    double rotation = inputAngle - 90.0;
    if (rotation == 180.0) {
        //if the view direction is 90/270, then the section is drawn properly and no
        //rotation is needed.  90.0 becomes 0.0, but 270.0 needs special handling.
        rotation = 0.0;
    }
    return rotation;
}

void TaskSectionView::setupTransaction() {
    int tid = 0;
    const char *name = App::GetApplication().getActiveTransaction(&tid);
    if(tid && tid == m_transactionID)
        return;

    std::string n(QT_TRANSLATE_NOOP("Command", "Edit "));
    n += m_sectionName;
    if(!name || n != name)
        App::GetApplication().setActiveTransaction(n.c_str());

    if(!m_transactionID)
        m_transactionID = tid;
}


//******************************************************************************

bool TaskSectionView::accept()
{
    //    Base::Console().Message("TSV::accept()\n");
    apply(true);
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
    return true;
}

bool TaskSectionView::reject()
{
    //    Base::Console().Message("TSV::reject()\n");
    if (!m_section) {//no section created, nothing to undo
        Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
        return false;
    }

    if (!isSectionValid()) {//section !exist. nothing to undo
        if (isBaseValid()) {
            m_base->requestPaint();
        }
        Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
        return false;
    }

    if (m_transactionID) {
        int tid = 0;
        App::GetApplication().getActiveTransaction(&tid);
        if (m_transactionID == tid) {
            App::GetApplication().closeActiveTransaction(/*abort*/true);
        }
        else if (m_doc) {
            m_doc->undo(m_transactionID);
        }
    }
    Gui::Command::updateActive();
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
    return false;
}

void TaskSectionView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TaskDlgSectionView::TaskDlgSectionView(TechDraw::DrawViewPart* base) : TaskDialog()
{
    widget = new TaskSectionView(base);
    taskbox =
        new Gui::TaskView::TaskBox(Gui::BitmapFactory().pixmap("actions/TechDraw_SectionView"),
                                   widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskDlgSectionView::TaskDlgSectionView(TechDraw::DrawViewSection* section) : TaskDialog()
{
    widget = new TaskSectionView(section);
    taskbox =
        new Gui::TaskView::TaskBox(Gui::BitmapFactory().pixmap("actions/TechDraw_SectionView"),
                                   widget->windowTitle(), true, nullptr);

    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}
TaskDlgSectionView::~TaskDlgSectionView() {}

void TaskDlgSectionView::update()
{
    //widget->updateTask();
}

//==== calls from the TaskView ===============================================================
void TaskDlgSectionView::open() {}

bool TaskDlgSectionView::accept()
{
    widget->accept();
    return true;
}

bool TaskDlgSectionView::reject()
{
    widget->reject();
    return true;
}

#include <Mod/TechDraw/Gui/moc_TaskSectionView.cpp>
