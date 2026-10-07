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


#ifndef GUI_TASKVIEW_TaskFilletParameters_H
#define GUI_TASKVIEW_TaskFilletParameters_H

#include <QStandardItemModel>
#include <QItemDelegate>

#include <App/ObjectIdentifier.h>
#include <Gui/Inventor/Draggers/Gizmo.h>
#include <Mod/Part/App/TopoShape.h>

#include "TaskDressUpParameters.h"
#include "ViewProviderFillet.h"

class Ui_TaskFilletParameters;

namespace Gui {
class ExpressionBinding;
class QuantitySpinBox;
}

namespace PartDesignGui {

class FilletSegmentDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    FilletSegmentDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

class TaskFilletParameters : public TaskDressUpParameters
{
    Q_OBJECT

public:
    explicit TaskFilletParameters(ViewProviderDressUp *DressUpView, QWidget *parent=nullptr);
    ~TaskFilletParameters() override;

    void apply() override;
    void setBinding(Gui::ExpressionBinding *binding, const QModelIndex &index);

private Q_SLOTS:
    void onAddAllEdges();
    void onCheckBoxUseAllEdgesToggled(bool checked);
    void onLengthChanged(double);

protected:
    void changeEvent(QEvent *e) override;
    void refresh() override;
    void onNewItem(QTreeWidgetItem *item) override;
    void onRefDeleted() override;
    void finishedRecomputeFeature() override;

    void removeSegments();
    void clearSegments();
    void newSegment(int editColumn=0);
    void updateSegments(QTreeWidgetItem *);
    void updateSegment(QTreeWidgetItem *, int column);
    void setSegment(QTreeWidgetItem *item, double param, double radius, double length=0.0);
    double getRadius() const;

    /** @name Setback corners (docs/CornerBlending.md)
     * A vertex in the references is a corner of the fillet; its row holds the
     * setback of all its fillets and its children the fillets ending there,
     * each with an optional setback of its own.
     */
    //@{
    /// The vertex row of a corner, from its row or one of its edges'
    static QTreeWidgetItem *getCornerItem(QTreeWidgetItem *item);
    /// A fillet edge at a corner, by name
    typedef std::vector<std::pair<std::string, Part::TopoShape>> CornerEdges;
    /// The fillet edges ending at each corner vertex of the references
    std::map<std::string, CornerEdges> getCornerEdges() const;
    void refreshCorner(QTreeWidgetItem *item, const CornerEdges &edges);
    void updateCorner(QTreeWidgetItem *item);
    /// Clears the setbacks of single fillets of the corners of the items
    void clearCornerEdges(const std::vector<QTreeWidgetItem*> &items);
    App::ObjectIdentifier getCornerPath(QTreeWidgetItem *item) const;
    //@}

    friend class FilletSegmentDelegate;

private:
    std::unique_ptr<Ui_TaskFilletParameters> ui;

    std::unique_ptr<Gui::GizmoContainer> gizmoContainer;
    Gui::LinearGizmo* radiusGizmo = nullptr;
    Gui::LinearGizmo* radiusGizmo2 = nullptr;
    void setupGizmos(ViewProviderDressUp* vp);
    void setGizmoPositions();

    /// One handle per fillet of the current corner, at most this many
    static constexpr int CornerGizmoCount = 6;
    struct CornerGizmo {
        Gui::LinearGizmo *gizmo = nullptr;
        /// Hidden; the gizmo drives a spin box
        Gui::QuantitySpinBox *spinBox = nullptr;
        std::string vertex;
        std::string edge;
    };
    std::vector<CornerGizmo> cornerGizmos;
    bool cornerRecomputePending = false;
    void setCornerGizmoPositions();
};

/// simulation dialog for the TaskView
class TaskDlgFilletParameters : public TaskDlgDressUpParameters
{
    Q_OBJECT

public:
    explicit TaskDlgFilletParameters(ViewProviderFillet *DressUpView);
    ~TaskDlgFilletParameters() override;

public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskFilletParameters_H
