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


#ifndef GUI_TASKVIEW_TaskChamferParameters_H
#define GUI_TASKVIEW_TaskChamferParameters_H

#include <QStandardItemModel>
#include <QItemDelegate>

#include "TaskDressUpParameters.h"
#include "ViewProviderChamfer.h"
#include <Gui/ExpressionBinding.h>

class Ui_TaskChamferParameters;
namespace PartDesign {
class Chamfer;
}

namespace PartDesignGui {

class ChamferInfoDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    ChamferInfoDelegate(QObject *parent = 0);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const;

    void setEditorData(QWidget *editor, const QModelIndex &index) const;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const;
};

class TaskChamferParameters : public TaskDressUpParameters
{
    Q_OBJECT
    friend class ChamferInfoDelegate;

public:
    explicit TaskChamferParameters(ViewProviderDressUp *DressUpView, QWidget *parent=nullptr);
    ~TaskChamferParameters() override;

    void apply() override;
    void setBinding(Gui::ExpressionBinding *binding, const QModelIndex &index);

private Q_SLOTS:
    void onTypeChanged(int);
    void onSizeChanged(double);
    void onSize2Changed(double);
    void onAngleChanged(double);
    void onFlipDirection(bool);
protected:
    void changeEvent(QEvent *e) override;
    void refresh() override;

    void removeItems();
    void clearItems();
    void updateItems(QTreeWidgetItem *);
    void onUpdateItem(QTreeWidgetItem *, int column);
    void updateItem(QTreeWidgetItem *);
    void setItem(QTreeWidgetItem *item, const Part::TopoShape::ChamferInfo &);

    Part::TopoShape::ChamferInfo getChamferInfo(QTreeWidgetItem *);

    void onAddAllEdges(void);
    void onCheckBoxUseAllEdgesToggled(bool checked);

    int getType() const;
    double getSize() const;
    double getSize2() const;
    double getAngle() const;
    bool getFlipDirection() const;
    void onNewItem(QTreeWidgetItem *item);
    
private:
    void setUpUI(PartDesign::Chamfer* pcChamfer);

    std::unique_ptr<Ui_TaskChamferParameters> ui;
};

/// simulation dialog for the TaskView
class TaskDlgChamferParameters : public TaskDlgDressUpParameters
{
    Q_OBJECT

public:
    explicit TaskDlgChamferParameters(ViewProviderChamfer *DressUpView);
    ~TaskDlgChamferParameters() override;

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskChamferParameters_H
