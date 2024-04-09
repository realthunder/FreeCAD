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
#endif

#include "ui_TaskFilletParameters.h"
#include "TaskFilletParameters.h"
#include "Utils.h"
#include <Base/UnitsApi.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Expression.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/WaitCursor.h>
#include <Base/Console.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/Tools.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>
#include <Mod/PartDesign/App/FeatureFillet.h>

#include "ui_TaskFilletParameters.h"
#include "TaskFilletParameters.h"


using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskFilletParameters */

FilletSegmentDelegate::FilletSegmentDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *FilletSegmentDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    if (index.column() < 1 || index.column() > 3)
        return nullptr;
    if (!index.parent().isValid()) {
        if (auto owner = qobject_cast<TaskFilletParameters*>(this->parent()))
            owner->newSegment(index.column());
        return nullptr;
    }

    Gui::QuantitySpinBox *editor = new Gui::QuantitySpinBox(parent);
    if (index.column() != 2)
        editor->setUnit(Base::Unit::Length);
    editor->setMinimum(0.0);
    editor->setMaximum(INT_MAX);
    editor->setSingleStep(0.1);
    if (auto owner = qobject_cast<TaskFilletParameters*>(this->parent()))
        owner->setBinding(editor, index);
    return editor;
}

void FilletSegmentDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto value = index.model()->data(index, Qt::UserRole).toDouble();

    Gui::QuantitySpinBox *spinBox = static_cast<Gui::QuantitySpinBox*>(editor);
    spinBox->setValue(value);
    spinBox->selectNumber();
}

void FilletSegmentDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    Gui::QuantitySpinBox *spinBox = static_cast<Gui::QuantitySpinBox*>(editor);
    spinBox->interpretText();
    Base::Quantity value = spinBox->value();
    model->setData(index, value.getUserString(), Qt::DisplayRole);
    model->setData(index, value.getValue(), Qt::UserRole);
}

///////////////////////////////////////////////////////////////////////////////////////////
TaskFilletParameters::TaskFilletParameters(ViewProviderDressUp *DressUpView,QWidget *parent)
    : TaskDressUpParameters(DressUpView, true, true, parent)
    , ui(new Ui_TaskFilletParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    this->groupLayout()->addWidget(proxy);

    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    bool useAllEdges = pcFillet->UseAllEdges.getValue();
    ui->checkBoxUseAllEdges->setChecked(useAllEdges);

    ui->filletRadius->setUnit(Base::Unit::Length);
    ui->filletRadius->setMinimum(0);
    ui->filletRadius->selectNumber();
    ui->filletRadius->bind(pcFillet->Radius);
    QMetaObject::invokeMethod(ui->filletRadius, "setFocus", Qt::QueuedConnection);

    QMetaObject::connectSlotsByName(this);

    Base::connect(ui->filletRadius, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this, &TaskFilletParameters::onLengthChanged);

    setup(ui->message, ui->treeWidgetReferences, ui->buttonRefAdd);

    ui->treeWidgetReferences->setItemDelegate(new FilletSegmentDelegate(this));

    Base::connect(ui->btnClear, &QPushButton::clicked, this, &TaskFilletParameters::clearSegments);
    Base::connect(ui->btnAdd, &QPushButton::clicked, this, &TaskFilletParameters::newSegment);
    Base::connect(ui->btnRemove, &QPushButton::clicked, this, &TaskFilletParameters::removeSegments);

    Base::connect(ui->treeWidgetReferences, &QTreeWidget::itemChanged,
        this, &TaskFilletParameters::updateSegment);

    ui->treeWidgetReferences->header()->setToolTip(tr(
"Click '+' key to add new segment for various radius fillet.\n"
"You can use 'Parameter' (0 ~ 1) as an ratio to the Edge\n"
"length from to specify a point to morph from one radius\n"
"value to another. Or, you can use 'Length' to specify the\n"
"point with absolute distance along the edge."));

    static const char *_ParamPath = "User parameter:BaseApp/Preferences/General/Widgets/TaskFilletParameters";
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
      
    createAddAllEdgesAction(ui->treeWidgetReferences);
    Base::connect(addAllEdgesAction, &QAction::triggered, this, &TaskFilletParameters::onAddAllEdges);

    Base::connect(ui->checkBoxUseAllEdges, &QCheckBox::toggled,
        this, &TaskFilletParameters::onCheckBoxUseAllEdgesToggled);

    refresh();
}

void TaskFilletParameters::onRefDeleted() {
    Base::StateLocker guard(busy);
    removeSegments();
}

void TaskFilletParameters::setBinding(Gui::ExpressionBinding *binding,
                                      const QModelIndex &index)
{
    if (!DressUpView || !index.isValid())
        return;
    auto item = static_cast<QTreeWidgetItem*>(index.internalPointer());
    if (!item)
        return;
    auto parent = item->parent();
    if (!parent)
        return;
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    App::ObjectIdentifier path(pcFillet->Segments);
    path << App::ObjectIdentifier::SimpleComponent(std::string(getGeometryItemText(parent).constData()))
         << App::ObjectIdentifier::ArrayComponent(index.row())
         << App::ObjectIdentifier::SimpleComponent(index.column()==1 ? "Radius" : 
                                                   (index.column()==3 ? "Length" : "Param"));
    binding->bind(path);
}

void TaskFilletParameters::refresh()
{
    if(!DressUpView)
        return;

    TaskDressUpParameters::refresh();
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    double r = pcFillet->Radius.getValue();
    {
        QSignalBlocker blocker(ui->filletRadius);
        ui->filletRadius->setValue(r);
    }
    QSignalBlocker blocker(ui->treeWidgetReferences);
    for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto item = ui->treeWidgetReferences->topLevelItem(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        int j = 0;
        for (const auto &segment : pcFillet->Segments.getValue(getGeometryItemText(item).constData())) {
            setSegment(j<item->childCount() ? item->child(j) : new QTreeWidgetItem(item),
                   segment.param, segment.radius, segment.length);
            ++j;
        }
        while (j > item->childCount())
            delete item->child(item->childCount()-1);
    }
}

void TaskFilletParameters::updateSegment(QTreeWidgetItem *item, int column)
{
    if (column<1 || column>3)
        return;
    QSignalBlocker blocker(ui->treeWidgetReferences);
    double param = item->data(2, Qt::UserRole).toDouble();
    double radius = item->data(1, Qt::UserRole).toDouble();
    double length = item->data(3, Qt::UserRole).toDouble();
    if (column == 3 && length > 0.0) {
        if (param != 0.0) {
            param = 0.0;
            QSignalBlocker block(ui->treeWidgetReferences);
            item->setData(2, Qt::UserRole, 0.0);
        }
    } else if (column == 2 && param > 0.0) {
        if (length != 0.0) {
            length = 0.0;
            QSignalBlocker block(ui->treeWidgetReferences);
            item->setData(3, Qt::UserRole, 0.0);
        }
    }
    setSegment(item, param, radius, length);
    updateSegments(item);
}

void TaskFilletParameters::updateSegments(QTreeWidgetItem *item)
{
    if(!DressUpView)
        return;
    setupTransaction();
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    Part::PropertyFilletSegments::Segments segments;
    auto parent = item->parent();
    if (!parent)
        parent = item;
    for (int i=0; i<parent->childCount(); ++i) {
        auto child = parent->child(i);
        segments.emplace_back(child->data(2, Qt::UserRole).toDouble(),
                              child->data(1, Qt::UserRole).toDouble(),
                              child->data(3, Qt::UserRole).toDouble());
    }
    pcFillet->Segments.setValue(getGeometryItemText(parent).constData(), std::move(segments));
    recompute();
}

void TaskFilletParameters::clearSegments()
{
    if(!DressUpView)
        return;
    std::set<QTreeWidgetItem*> items;
    for (auto item : ui->treeWidgetReferences->selectedItems()) {
        if (auto parent = item->parent())
            item = parent;
        items.insert(item);
    }
    setupTransaction();
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    for (auto item : items) {
        for (auto child : item->takeChildren())
            delete child;
        pcFillet->Segments.removeValue(getGeometryItemText(item).constData());
    }
    recompute();
}

void TaskFilletParameters::removeSegments()
{
    if(!DressUpView)
        return;
    setupTransaction();
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    for (auto item : ui->treeWidgetReferences->selectedItems()) {
        auto parent = item->parent();
        if (!parent)
            continue;
        pcFillet->Segments.removeValue(getGeometryItemText(parent).constData(),
                                       parent->indexOfChild(item));
        delete item;
    }
    TaskDressUpParameters::onRefDeleted();
    recompute();
}

void TaskFilletParameters::setSegment(QTreeWidgetItem *item, double param, double radius, double length)
{
    if (!DressUpView)
        return;
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    QSignalBlocker blocker(ui->treeWidgetReferences);

    item->setFlags(item->flags() | Qt::ItemIsEditable);

    auto parent = item->parent();
    App::ObjectIdentifier path(pcFillet->Segments);
    path << App::ObjectIdentifier::SimpleComponent(std::string(getGeometryItemText(parent).constData()))
         << App::ObjectIdentifier::ArrayComponent(parent->indexOfChild(item));
    auto linkColor = QVariant::fromValue(QApplication::palette().color(QPalette::Link));

    auto setupItem = [&](const char *key, int index, const Base::Quantity &q, bool noText) {
        if (auto expr = pcFillet->getExpression(App::ObjectIdentifier(path)
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
    setupItem("Radius", 1, Base::Quantity(radius, Base::Unit::Length), false);
    setupItem("Param", 2, Base::Quantity(param), length>0.0);
    setupItem("Length", 3, Base::Quantity(length, Base::Unit::Length), length==0.0);
}

void TaskFilletParameters::newSegment(int editColumn)
{
    auto current = getCurrentItem();
    if (!current)
        return;
    auto parent = current->parent();
    QSignalBlocker blocker(ui->treeWidgetReferences);
    double param = 0.0;
    auto item = new QTreeWidgetItem;
    if (!parent) {
        current->addChild(item);
        if (current->childCount() != 1)
            param = 1.0;
    } else {
        int index = parent->indexOfChild(current);
        if (index == 0 && parent->childCount() == 1) {
            parent->addChild(item);
            param = 1.0;
        } else {
            parent->insertChild(index, item);
            if (index == 0)
                param = 0;
            else
                param = current->data(2, Qt::UserRole).toDouble();
        }
    }
    setSegment(item, param, getRadius());
    ui->treeWidgetReferences->setCurrentItem(item);
    if (editColumn)
        ui->treeWidgetReferences->editItem(item, editColumn);
    updateSegments(parent ? parent : current);
}

void TaskFilletParameters::onCheckBoxUseAllEdgesToggled(bool checked)
{
    ui->buttonRefAdd->setEnabled(!checked);
    ui->treeWidgetReferences->setEnabled(!checked);
    try {
        setupTransaction();
        auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
        pcFillet->UseAllEdges.setValue(checked);
        recompute();
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
}

void TaskFilletParameters::onAddAllEdges()
{
    TaskDressUpParameters::addAllEdges();
}

void TaskFilletParameters::onLengthChanged(double len)
{
    if(!DressUpView)
        return;

    clearButtons(none);
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    setupTransaction();
    pcFillet->Radius.setValue(len);
    recompute();
}

void TaskFilletParameters::onNewItem(QTreeWidgetItem *item)
{
    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

double TaskFilletParameters::getRadius() const
{
    return ui->filletRadius->value().getValue();
}

TaskFilletParameters::~TaskFilletParameters()
{
    Gui::Selection().rmvSelectionGate();
}

void TaskFilletParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
    }
}

void TaskFilletParameters::apply()
{
    ui->filletRadius->apply();
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgFilletParameters::TaskDlgFilletParameters(ViewProviderFillet *DressUpView)
    : TaskDlgDressUpParameters(DressUpView)
{
    parameter  = new TaskFilletParameters(DressUpView);

    Content.push_back(parameter);
}

TaskDlgFilletParameters::~TaskDlgFilletParameters() = default;

//==== calls from the TaskView ===============================================================


//void TaskDlgFilletParameters::open()
//{
//    // a transaction is already open at creation time of the fillet
//    if (!Gui::Command::hasPendingCommand()) {
//        QString msg = tr("Edit fillet");
//        Gui::Command::openCommand((const char*)msg.toUtf8());
//    }
//}
bool TaskDlgFilletParameters::accept()
{
    parameter->apply();

    return TaskDlgDressUpParameters::accept();
}

#include "moc_TaskFilletParameters.cpp"
