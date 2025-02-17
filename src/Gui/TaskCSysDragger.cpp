/***************************************************************************
 *   Copyright (c) 2015 Thomas Anderson <blobfish[at]gmx.com>              *
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
#include <cassert>
#include <limits>
#include <QApplication>
#include <QTimer>
#include <QMessageBox>
#include <QGridLayout>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include <App/Document.h>
#include <App/Transactions.h>
#include "Document.h" // must be before TaskCSysDragger.h
#include "TaskCSysDragger.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "MainWindow.h"
#include "QuantitySpinBox.h"
#include "SoFCCSysDragger.h"
#include "PrefWidgets.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewParams.h"
#include "ViewProviderDragger.h"
#include "TaskView/TaskView.h"

using namespace Gui;


static double degreesToRadians(const double &degreesIn)
{
  return degreesIn * (M_PI / 180.0);
}


TaskCSysDragger::TaskCSysDragger(Gui::ViewProviderDocumentObject* vpObjectIn, Gui::SoFCCSysDragger* draggerIn) :
  dragger(draggerIn)
{
  assert(vpObjectIn);
  assert(draggerIn);
  vpObject = vpObjectIn->getObject();
  dragger->ref();
  setupGui();

  App::GetApplication().setActiveTransaction(QT_TRANSLATE_NOOP("Command", "Transform"));

  connUndo = App::GetApplication().signalUndo.connect([this]() {
      if (auto gdoc = Application::Instance->editDocument()) {
          auto doc = gdoc->getDocument();
          if (transactionId && doc->getTransactionID(/*undo*/false) == transactionId) {
              lastTransactionId = transactionId;
              transactionId = 0;
          }
      }
  });

  connRedo = App::GetApplication().signalRedo.connect([this]() {
      if (auto gdoc = Application::Instance->editDocument()) {
          auto doc = gdoc->getDocument();
          if (lastTransactionId && doc->getTransactionID(/*undo*/true) == lastTransactionId) {
              transactionId = lastTransactionId;
              lastTransactionId = 0;
          }
      }
  });

  connTransaction = App::GetApplication().signalBeforeCloseTransaction.connect([this](bool abort) {
      if(!abort && transactionId == 0) {
          transactionId = App::Transaction::getLastID();
          lastTransactionId = 0;
      }
  });

  QTimer::singleShot(0, this, [this]() {
    auto obj = vpObject.getObject();
    if (!obj)
        return;
    for(auto &v : obj->ExpressionEngine.getExpressions()) {
      auto str = v.first.toString();
      if (str == "Placement"
            || boost::starts_with(str, "Placement.")
            || boost::starts_with(str, ".Placement."))
      {
        auto res = QMessageBox::warning(getMainWindow(),
                        tr("Warning"),
                        tr("Warning! There is expression binding on object's placement. "
                            "Moving such object may cause unexpected result.\n\n"
                            "Do you still want to continue?"),
                        QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::No)
          Application::Instance->setEditDocument(nullptr);
        break;
      }
    }
  });
}

TaskCSysDragger::~TaskCSysDragger()
{
  onToggleShowOnTop(false);

  dragger->unref();

  Gui::Application::Instance->commandManager().getCommandByName("Std_OrthographicCamera")->setEnabled(true);
  Gui::Application::Instance->commandManager().getCommandByName("Std_PerspectiveCamera")->setEnabled(true);
}

void TaskCSysDragger::dragStartCallback(void *, SoDragger *)
{
    // This is called when a manipulator is about to manipulating
    if (!App::GetApplication().getActiveTransaction()) {
        App::GetApplication().setActiveTransaction("Transform");
    }
}

void TaskCSysDragger::setupGui()
{
    auto incrementsBox = new Gui::TaskView::TaskBox(
      Gui::BitmapFactory().pixmap("button_valid"),
      tr("Increments"), true, nullptr);

    auto gridLayout = new QGridLayout();
  gridLayout->setColumnStretch(1, 1);

  auto tLabel = new QLabel(tr("Translation Increment:"), incrementsBox);
  gridLayout->addWidget(tLabel, 0, 0, Qt::AlignRight);

  QFontMetrics metrics(QApplication::font());
  int spinBoxWidth = metrics.averageCharWidth() * 20;
  tSpinBox = new QuantitySpinBox(incrementsBox);
  tSpinBox->setMinimum(0.0);
  tSpinBox->setMaximum(std::numeric_limits<double>::max());
  tSpinBox->setUnit(Base::Unit::Length);
  tSpinBox->setMinimumWidth(spinBoxWidth);
  gridLayout->addWidget(tSpinBox, 0, 1, Qt::AlignLeft);

  auto rLabel = new QLabel(tr("Rotation Increment:"), incrementsBox);
  gridLayout->addWidget(rLabel, 1, 0, Qt::AlignRight);

  rSpinBox = new QuantitySpinBox(incrementsBox);
  rSpinBox->setMinimum(0.0);
  rSpinBox->setMaximum(180.0);
  rSpinBox->setUnit(Base::Unit::Angle);
  rSpinBox->setMinimumWidth(spinBoxWidth);
  gridLayout->addWidget(rSpinBox, 1, 1, Qt::AlignLeft);

  checkBoxShowOnTop = new PrefCheckBox(incrementsBox);
  checkBoxShowOnTop->setText(tr("Show on top"));
  checkBoxShowOnTop->setParamGrpPath(QByteArray("Dialog/TaskCSysDragger"));
  checkBoxShowOnTop->setEntryName(QByteArray("ShowOnTop"));
  checkBoxShowOnTop->setChecked(true);
  checkBoxShowOnTop->initAutoSave();
  checkBoxShowOnTop->setText(tr("Show on top"));
  connect(checkBoxShowOnTop, SIGNAL(toggled(bool)), this, SLOT(onToggleShowOnTop(bool)), Qt::QueuedConnection);
  gridLayout->addWidget(checkBoxShowOnTop, 2, 0);

  checkBoxRecompute = new PrefCheckBox(incrementsBox);
  checkBoxRecompute->setText(tr("Auto recompute"));
  checkBoxRecompute->setParamGrpPath(QByteArray("Dialog/TaskCSysDragger"));
  checkBoxRecompute->setEntryName(QByteArray("AutoRecompute"));
  checkBoxRecompute->initAutoSave();
  gridLayout->addWidget(checkBoxRecompute, 3, 0, 1, 2);
  
  timer = new QTimer(this);
  timer->setSingleShot(true);
  QObject::connect(timer, &QTimer::timeout, [this]() {recompute(false);});

  incrementsBox->groupLayout()->addLayout(gridLayout);
  Content.push_back(incrementsBox);

  connect(tSpinBox, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskCSysDragger::onTIncrementSlot);
  connect(rSpinBox, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskCSysDragger::onRIncrementSlot);

  if (checkBoxShowOnTop->isChecked())
      onToggleShowOnTop(true);
}

void TaskCSysDragger::onEndMove()
{
    if (checkBoxRecompute->isChecked())
        timer->start(10);
}

void TaskCSysDragger::onToggleShowOnTop(bool checked)
{
    auto doc = Application::Instance->getDocument(editObj.getDocument());
    if (doc) {
        auto view = Base::freecad_dynamic_cast<View3DInventor>(doc->getActiveView());
        if (view) {
            view->getViewer()->checkGroupOnTop(SelectionChanges(
                        SelectionChanges::RmvSelection,
                        editObj.getDocumentName(),
                        editObj.getObjectName(),
                        editObj.getSubName()), true);
        }
        editObj = App::SubObjectT();
    }
    if (!checked)
        return;

    doc = Application::Instance->editDocument();
    if (!doc)
        return;
    auto view = Base::freecad_dynamic_cast<View3DInventor>(
            doc->setActiveView(nullptr, View3DInventor::getClassTypeId()));
    if (!view)
        return;
    editObj = doc->getInEditT();
    view->getViewer()->checkGroupOnTop(
            SelectionChanges(SelectionChanges::AddSelection,
                             editObj.getDocumentName(),
                             editObj.getObjectName(),
                             editObj.getSubName()), true);
}

void TaskCSysDragger::onTIncrementSlot(double freshValue)
{
  dragger->translationIncrement.setValue(freshValue);
}

void TaskCSysDragger::onRIncrementSlot(double freshValue)
{
  dragger->rotationIncrement.setValue(degreesToRadians(freshValue));
}

void TaskCSysDragger::open()
{
  dragger->addStartCallback(dragStartCallback, this);
  //we can't have user switching camera types while dragger is shown.
  Gui::Application::Instance->commandManager().getCommandByName("Std_OrthographicCamera")->setEnabled(false);
  Gui::Application::Instance->commandManager().getCommandByName("Std_PerspectiveCamera")->setEnabled(false);
//   dragger->translationIncrement.setValue(lastTranslationIncrement);
//   dragger->rotationIncrement.setValue(lastRotationIncrement);
  ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/History/Dragger");
  double lastTranslationIncrement = hGrp->GetFloat("LastTranslationIncrement", 1.0);
  double lastRotationIncrement = hGrp->GetFloat("LastRotationIncrement", 15.0);
  tSpinBox->setValue(lastTranslationIncrement);
  rSpinBox->setValue(lastRotationIncrement);

  Gui::TaskView::TaskDialog::open();
}

void TaskCSysDragger::recompute(bool finish)
{
  App::DocumentObject* dObject = vpObject.getObject();
  if (dObject) {
    Gui::Document* document = Gui::Application::Instance->editDocument();
    if (document) {
        if (!App::GetApplication().getActiveTransaction()) {
            App::GetApplication().setActiveTransaction("Transform");
        }
        document->getDocument()->recompute();
        if (finish) {
            document->resetEdit();
        }
    }
  }
}

bool TaskCSysDragger::accept()
{
  ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/History/Dragger");
  hGrp->SetFloat("LastTranslationIncrement", tSpinBox->rawValue());
  hGrp->SetFloat("LastRotationIncrement", rSpinBox->rawValue());

  recompute(true);
  return Gui::TaskView::TaskDialog::accept();
}

bool TaskCSysDragger::reject()
{
  App::GetApplication().closeActiveTransaction(/*abort*/true, transactionId);
  Gui::Command::doCommand(Gui::Command::Gui,"Gui.activeDocument().resetEdit()");
  return true;
}

#include "moc_TaskCSysDragger.cpp"
