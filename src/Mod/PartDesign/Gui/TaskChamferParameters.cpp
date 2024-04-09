/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
# include <QAction>
# include <QFontMetrics>
# include <QKeyEvent>
# include <QListWidget>
# include <QMessageBox>
#endif

#include "ui_TaskChamferParameters.h"
#include "TaskChamferParameters.h"
#include "Utils.h"
#include <App/Application.h>
#include <App/Document.h>
#include <App/Expression.h>
#include <Base/Console.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Selection.h>
#include <Gui/Tools.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>
#include <Mod/PartDesign/App/FeatureChamfer.h>

#include "ui_TaskChamferParameters.h"
#include "TaskChamferParameters.h"


using namespace PartDesignGui;
using namespace Gui;

ChamferInfoDelegate::ChamferInfoDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *ChamferInfoDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    if (index.column() < 1 || index.column() > 4)
        return nullptr;
    auto owner = qobject_cast<TaskChamferParameters*>(this->parent());
    if (!owner)
        return nullptr;

    if (index.column() == 4) {
        auto editor = new QCheckBox(parent);
        QObject::connect(editor, &QCheckBox::toggled, this,
                [&](bool) {const_cast<ChamferInfoDelegate*>(this)->commitData(editor);});
        return editor;
    }
    Gui::QuantitySpinBox *editor = new Gui::QuantitySpinBox(parent);
    editor->setMinimum(0.0);
    if (index.column() < 3) {
        editor->setUnit(Base::Unit::Length);
        editor->setMaximum(INT_MAX);
        editor->setSingleStep(0.1);
    }
    else if (index.column() == 3) {
        editor->setUnit(Base::Unit::Angle);
        editor->setSingleStep(1.0);
        editor->setMaximum(180.0);
    }

    owner->setBinding(editor, index);
    return editor;
}

void ChamferInfoDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto owner = qobject_cast<TaskChamferParameters*>(this->parent());
    if (!owner)
        return;

    auto data = index.model()->data(index, Qt::UserRole);

    if (index.column() == 4) {
        QCheckBox *checkBox = static_cast<QCheckBox*>(editor);
        QSignalBlocker blocker(checkBox);
        checkBox->setChecked(data.isValid() ? data.toBool() : owner->getFlipDirection());
        return;
    }

    double value;
    if (data.isValid())
        value = data.toDouble();
    else {
        switch(index.column()) {
        case 1:
            value = owner->getSize();
            break;
        case 2:
            value = owner->getType() == 1 ? owner->getSize2() : 0.0;
            break;
        case 3:
            value = owner->getType() == 2 ? owner->getAngle() : 0.0;
            break;
        }
    }

    Gui::QuantitySpinBox *spinBox = static_cast<Gui::QuantitySpinBox*>(editor);
    spinBox->setValue(value);
}

void ChamferInfoDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    if (index.column() == 4) {
        model->setData(index, static_cast<QCheckBox*>(editor)->isChecked(), Qt::UserRole);
        return;
    }
    Gui::QuantitySpinBox *spinBox = static_cast<Gui::QuantitySpinBox*>(editor);
    spinBox->interpretText();
    Base::Quantity value = spinBox->value();
    model->setData(index, value.getUserString(), Qt::DisplayRole);
    model->setData(index, value.getValue(), Qt::UserRole);
}

///////////////////////////////////////////////////////////////////////////////////////////
/* TRANSLATOR PartDesignGui::TaskChamferParameters */

TaskChamferParameters::TaskChamferParameters(ViewProviderDressUp *DressUpView,QWidget *parent)
    : TaskDressUpParameters(DressUpView, true, true, parent)
    , ui(new Ui_TaskChamferParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    this->groupLayout()->addWidget(proxy);

    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());

    setUpUI(pcChamfer);

    bool useAllEdges = pcChamfer->UseAllEdges.getValue();
    ui->checkBoxUseAllEdges->setChecked(useAllEdges);
    ui->buttonRefAdd->setEnabled(!useAllEdges);
    ui->treeWidgetReferences->setEnabled(!useAllEdges);
    QMetaObject::invokeMethod(ui->chamferSize, "setFocus", Qt::QueuedConnection);

    QMetaObject::connectSlotsByName(this);

    Base::connect(ui->chamferType, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &TaskChamferParameters::onTypeChanged);
    Base::connect(ui->chamferSize, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this, &TaskChamferParameters::onSizeChanged);
    Base::connect(ui->chamferSize2, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this, &TaskChamferParameters::onSize2Changed);
    Base::connect(ui->chamferAngle, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this, &TaskChamferParameters::onAngleChanged);
    Base::connect(ui->flipDirection, &QCheckBox::toggled,
        this, &TaskChamferParameters::onFlipDirection);

    setup(ui->message, ui->treeWidgetReferences, ui->buttonRefAdd);

    ui->treeWidgetReferences->setItemDelegate(new ChamferInfoDelegate(this));

    Base::connect(ui->btnClear, &QPushButton::clicked,
            this, &TaskChamferParameters::clearItems);
    Base::connect(ui->treeWidgetReferences, &QTreeWidget::itemChanged,
            this, &TaskChamferParameters::onUpdateItem);

    static const char *_ParamPath = "User parameter:BaseApp/Preferences/General/Widgets/TaskChamferParameters";
    auto hParam = App::GetApplication().GetParameterGroupByPath(_ParamPath);
    for (int i=0; i<ui->treeWidgetReferences->header()->count(); ++i) {
        std::string key("ColumnSize");
        key += std::to_string(i+1);
        if (auto size = hParam->GetUnsigned(key.c_str(),0))
            ui->treeWidgetReferences->header()->resizeSection(i, size);
    }

    Base::connect(ui->treeWidgetReferences->header(), &QHeaderView::sectionResized,
        [hParam](int idx, int, int newSize) {
            std::string key("ColumnSize");
            key += std::to_string(idx+1);
            hParam->SetUnsigned(key.c_str(), newSize);
        });

    refresh();

    Base::connect(ui->checkBoxUseAllEdges, &QCheckBox::toggled,
            this, &TaskChamferParameters::onCheckBoxUseAllEdgesToggled);

    createAddAllEdgesAction(ui->treeWidgetReferences);
    Base::connect(addAllEdgesAction, &QAction::triggered, this, &TaskChamferParameters::onAddAllEdges);
}

void TaskChamferParameters::setUpUI(PartDesign::Chamfer* pcChamfer)
{
    const int index = pcChamfer->ChamferType.getValue();
    ui->chamferType->setCurrentIndex(index);

    ui->flipDirection->setEnabled(index != 0); // Enable if type is not "Equal distance"
    ui->flipDirection->setChecked(pcChamfer->FlipDirection.getValue());

    ui->chamferSize->setUnit(Base::Unit::Length);
    ui->chamferSize->setMinimum(0);
    ui->chamferSize->setValue(pcChamfer->Size.getValue());
    ui->chamferSize->bind(pcChamfer->Size);
    ui->chamferSize->selectNumber();

    ui->chamferSize2->setUnit(Base::Unit::Length);
    ui->chamferSize2->setMinimum(0);
    ui->chamferSize2->setValue(pcChamfer->Size2.getValue());
    ui->chamferSize2->bind(pcChamfer->Size2);

    ui->chamferAngle->setUnit(Base::Unit::Angle);
    ui->chamferAngle->setMinimum(pcChamfer->Angle.getMinimum());
    ui->chamferAngle->setMaximum(pcChamfer->Angle.getMaximum());
    ui->chamferAngle->setValue(pcChamfer->Angle.getValue());
    ui->chamferAngle->bind(pcChamfer->Angle);

    ui->stackedWidget->setFixedHeight(ui->chamferSize2->sizeHint().height());

    QFontMetrics fm(ui->typeLabel->font());
    int minWidth = Gui::QtTools::horizontalAdvance(fm, ui->typeLabel->text());
    minWidth = std::max<int>(minWidth, Gui::QtTools::horizontalAdvance(fm, ui->sizeLabel->text()));
    minWidth = std::max<int>(minWidth, Gui::QtTools::horizontalAdvance(fm, ui->size2Label->text()));
    minWidth = std::max<int>(minWidth, Gui::QtTools::horizontalAdvance(fm, ui->angleLabel->text()));
    minWidth = minWidth + 5; //spacing
    ui->typeLabel->setMinimumWidth(minWidth);
    ui->sizeLabel->setMinimumWidth(minWidth);
    ui->size2Label->setMinimumWidth(minWidth);
    ui->angleLabel->setMinimumWidth(minWidth);

}

void TaskChamferParameters::setBinding(Gui::ExpressionBinding *binding,
                                      const QModelIndex &index)
{
    if (!DressUpView || !index.isValid())
        return;
    auto item = static_cast<QTreeWidgetItem*>(index.internalPointer());
    if (!item)
        return;
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    App::ObjectIdentifier path(pcChamfer->ChamferInfo);
    path << App::ObjectIdentifier::SimpleComponent(std::string(getGeometryItemText(item).constData()))
         << App::ObjectIdentifier::SimpleComponent(index.column()==1 ? "Size" : 
                                                   (index.column()==2 ? "Size2" : 
                                                    (index.column()==3 ? "Angle" : "Flip")));
    binding->bind(path);
}

void TaskChamferParameters::refresh()
{
    if(!DressUpView)
        return;

    TaskDressUpParameters::refresh();

    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    const int index = pcChamfer->ChamferType.getValue();
    {
        QSignalBlocker bocker(ui->chamferType);
        ui->chamferType->setCurrentIndex(index);
    }

    {
        ui->flipDirection->setEnabled(index != 0); // Enable if type is not "Equal distance"
        QSignalBlocker blocker(ui->flipDirection);
        ui->flipDirection->setChecked(pcChamfer->FlipDirection.getValue());
    }

    {
        QSignalBlocker blocker(ui->chamferSize);
        ui->chamferSize->setValue(pcChamfer->Size.getValue());
    }

    {
        QSignalBlocker blocker(ui->chamferSize2);
        ui->chamferSize2->setValue(pcChamfer->Size2.getValue());
    }

    {
        QSignalBlocker blocker(ui->chamferAngle);
        ui->chamferAngle->setValue(pcChamfer->Angle.getValue());
    }

    QSignalBlocker blocker(ui->treeWidgetReferences);
    for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto item = ui->treeWidgetReferences->topLevelItem(i);
        Part::TopoShape::ChamferInfo info;
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (pcChamfer->ChamferInfo.getValue(getGeometryItemText(item).constData(), info)) {
            setItem(item, info);
        }
    }
}

void TaskChamferParameters::onCheckBoxUseAllEdgesToggled(bool checked)
{
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    ui->buttonRefAdd->setEnabled(!checked);
    ui->treeWidgetReferences->setEnabled(!checked);
    pcChamfer->UseAllEdges.setValue(checked);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onNewItem(QTreeWidgetItem *item)
{
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

Part::TopoShape::ChamferInfo TaskChamferParameters::getChamferInfo(QTreeWidgetItem *item)
{
    Part::TopoShape::ChamferInfo info;
    QVariant data = item->data(1, Qt::UserRole);
    info.size = data.isValid() ? data.toDouble() : getSize();
    data = item->data(2, Qt::UserRole);
    info.size2 = data.isValid() ? data.toDouble() : (getType()==1 ? getSize2() : 0.0);
    data = item->data(3, Qt::UserRole);
    info.angle = data.isValid() ? data.toDouble() : (getType()==2 ? getAngle() : 0.0);
    data = item->data(4, Qt::UserRole);
    info.flip = data.isValid() ? data.toBool() : getFlipDirection();
    return info;
}

void TaskChamferParameters::onUpdateItem(QTreeWidgetItem *item, int column)
{
    if (column<1 || column>4)
        return;
    QSignalBlocker blocker(ui->treeWidgetReferences);
    auto info = getChamferInfo(item);
    if (column == 3 && info.angle > 0.0) {
        if (info.size2 != 0.0) {
            info.size2 = 0.0;
            QSignalBlocker block(ui->treeWidgetReferences);
            item->setData(2, Qt::UserRole, 0.0);
        }
    } else if (column == 2 && info.size2 > 0.0) {
        if (info.angle != 0.0) {
            info.angle = 0.0;
            QSignalBlocker block(ui->treeWidgetReferences);
            item->setData(3, Qt::UserRole, 0.0);
        }
    }
    setItem(item, info);
    updateItem(item);
}

void TaskChamferParameters::updateItem(QTreeWidgetItem *item)
{
    if(!DressUpView)
        return;
    setupTransaction();
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    if (item->data(1, Qt::UserRole).isValid()) {
        Part::TopoShape::ChamferInfo info;
        info.size = item->data(1, Qt::UserRole).toDouble(),
        info.size2 = item->data(2, Qt::UserRole).toDouble(),
        info.angle = item->data(3, Qt::UserRole).toDouble(),
        info.flip = item->data(4, Qt::UserRole).toBool();
        pcChamfer->ChamferInfo.setValue(getGeometryItemText(item).constData(), info);
    }
    else
        pcChamfer->ChamferInfo.removeValue(getGeometryItemText(item).constData());
    recompute();
}

void TaskChamferParameters::clearItems()
{
    if(!DressUpView)
        return;
    setupTransaction();
    auto items = ui->treeWidgetReferences->selectedItems();
    if (items.isEmpty()) {
        for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i)
            items.append(ui->treeWidgetReferences->topLevelItem(i));
    }
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    for (auto item : ui->treeWidgetReferences->selectedItems()) {
        for (int i=1; i<=4; ++i) {
            ui->treeWidgetReferences->closePersistentEditor(item, 4);
            item->setData(i, Qt::UserRole, QVariant());
            item->setText(i, QString());
        }
    }
    pcChamfer->ChamferInfo.setValue();
    recompute();
}

void TaskChamferParameters::setItem(QTreeWidgetItem *item, const Part::TopoShape::ChamferInfo &info)
{
    if (!DressUpView)
        return;
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    QSignalBlocker blocker(ui->treeWidgetReferences);

    App::ObjectIdentifier path(pcChamfer->ChamferInfo);
    path << App::ObjectIdentifier::SimpleComponent(std::string(getGeometryItemText(item).constData()));
    auto linkColor = QVariant::fromValue(QApplication::palette().color(QPalette::Link));

    auto setupItem = [&](const char *key, int index, const Base::Quantity &q, bool noText) {
        if (auto expr = pcChamfer->getExpression(App::ObjectIdentifier(path)
                    << App::ObjectIdentifier::SimpleComponent(key)).expression) {
            item->setData(index, Qt::ToolTipRole, QString::fromUtf8(expr->toString().c_str()));
            item->setData(index, Qt::ForegroundRole, linkColor);
        }
        else {
            item->setData(index, Qt::ForegroundRole, QVariant());
            item->setData(index, Qt::ToolTipRole, QVariant());
        }
        item->setData(index, Qt::UserRole, q.getValue());
        item->setText(index, noText ? QString() : q.getUserString());
    };
    setupItem("Size", 1, Base::Quantity(info.size, Base::Unit::Length), false);
    setupItem("Size2", 2, Base::Quantity(info.size2, Base::Unit::Length), info.size2==0.0);
    setupItem("Angle", 3, Base::Quantity(info.angle, Base::Unit::Angle), info.angle==0.0);
    item->setData(4, Qt::UserRole, info.flip);

    if (!ui->treeWidgetReferences->isPersistentEditorOpen(item, 4))
        ui->treeWidgetReferences->openPersistentEditor(item, 4);
}

void TaskChamferParameters::onAddAllEdges()
{
    ui->checkBoxUseAllEdges->setChecked(false);
    TaskDressUpParameters::addAllEdges();
}

void TaskChamferParameters::onTypeChanged(int index)
{
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    pcChamfer->ChamferType.setValue(index);
    ui->stackedWidget->setCurrentIndex(index);
    ui->flipDirection->setEnabled(index != 0); // Enable if type is not "Equal distance"
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onSizeChanged(double len)
{
    if(!DressUpView)
        return;

    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Size.setValue(len);
    recompute();
}

void TaskChamferParameters::onSize2Changed(double len)
{
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Size2.setValue(len);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onAngleChanged(double angle)
{
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Angle.setValue(angle);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onFlipDirection(bool flip)
{
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->FlipDirection.setValue(flip);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

int TaskChamferParameters::getType() const
{
    return ui->chamferType->currentIndex();
}

double TaskChamferParameters::getSize() const
{
    return ui->chamferSize->value().getValue();
}

double TaskChamferParameters::getSize2() const
{
    return ui->chamferSize2->value().getValue();
}

double TaskChamferParameters::getAngle() const
{
    return ui->chamferAngle->value().getValue();
}

bool TaskChamferParameters::getFlipDirection() const
{
    return ui->flipDirection->isChecked();
}

TaskChamferParameters::~TaskChamferParameters()
{
    Gui::Selection().rmvSelectionGate();
}

void TaskChamferParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
    }
}

void TaskChamferParameters::apply()
{
    if(!DressUpView)
        return;

    std::string name = DressUpView->getObject()->getNameInDocument();

    //Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Chamfer changed"));

    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());

    const int chamfertype = pcChamfer->ChamferType.getValue();

    switch(chamfertype) {

        case 0: // "Equal distance"
            ui->chamferSize->apply();
            break;
        case 1: // "Two distances"
            ui->chamferSize->apply();
            ui->chamferSize2->apply();
            break;
        case 2: // "Distance and Angle"
            ui->chamferSize->apply();
            ui->chamferAngle->apply();
            break;
    }
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgChamferParameters::TaskDlgChamferParameters(ViewProviderChamfer *DressUpView)
    : TaskDlgDressUpParameters(DressUpView)
{
    parameter  = new TaskChamferParameters(DressUpView);

    Content.push_back(parameter);
}

TaskDlgChamferParameters::~TaskDlgChamferParameters() = default;

//==== calls from the TaskView ===============================================================


//void TaskDlgChamferParameters::open()
//{
//    // a transaction is already open at creation time of the chamfer
//    if (!Gui::Command::hasPendingCommand()) {
//        QString msg = tr("Edit chamfer");
//        Gui::Command::openCommand((const char*)msg.toUtf8());
//    }
//}
bool TaskDlgChamferParameters::accept()
{
    parameter->apply();

    return TaskDlgDressUpParameters::accept();
}

#include "moc_TaskChamferParameters.cpp"
