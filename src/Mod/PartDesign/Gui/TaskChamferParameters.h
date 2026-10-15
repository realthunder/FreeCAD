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

#include <Base/Matrix.h>
#include <Gui/Inventor/Draggers/Gizmo.h>

#include "TaskDressUpParameters.h"
#include "ViewProviderChamfer.h"
#include <Gui/ExpressionBinding.h>

class QMenu;
class Ui_TaskChamferParameters;
namespace PartDesign {
class Chamfer;
}
namespace Gui {
class OnViewValueSet;
class QuantitySpinBox;
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
    void finishedRecomputeFeature() override;
    
private:
    void setUpUI(PartDesign::Chamfer* pcChamfer);

    std::unique_ptr<Ui_TaskChamferParameters> ui;

    std::unique_ptr<Gui::GizmoContainer> gizmoContainer;
    void setupGizmos(ViewProviderDressUp* vp);
    void setGizmoPositions();
    /// Show the values in the view the feature is edited in, once it has one
    void attachOnViewValues();
    PartDesign::Chamfer* getChamfer() const;

    /** @name The values in the view (docs/OnViewValueEditing.md 3.4)
     * As the fillet's: the default values' labels at the top middle (the
     * size, and the second size or the angle by the chamfer's type), and
     * handles on every edge. An edge that takes the defaults has handles of
     * the default values; a customised edge (ChamferInfo) handles of its own
     * values, whose labels show beside them while active. A size is an arrow
     * in a face: an equal size in whichever face is turned more toward the
     * camera, the first of two sizes in the face the flip names and the
     * second in the other; an angle is a rotation handle below the size's
     * arrow. A right click on a handle opens the edge's menu: its mode, its
     * flip, its group, Remove.
     */
    //@{
    /// Arrows at most; edges past them have none
    static constexpr int MaxEdgeHandles = 64;
    /// What a handle drives: an edge's size, second size or angle
    enum Field { SizeField, Size2Field, AngleField };
    /// edgeMode(): the defaults, or an edge's own size, two sizes, size and angle
    enum Mode { DefaultMode, SizeMode, TwoSizesMode, SizeAngleMode };
    struct HandlePlacement {
        Base::Vector3d position;
        Base::Vector3d dir;
    };
    struct EdgeHandle {
        std::string edge;
        Field field = SizeField;
        Gui::Gizmo* gizmo = nullptr;
        /// Hidden, bound to the edge's own value; none for a default's
        Gui::QuantitySpinBox* box = nullptr;
        /// In each of the edge's two faces, the first the flip names first
        HandlePlacement placements[2];
        int face = 0;
        bool placed = false;
        /// An equal size: in the face turned more toward the camera
        bool followsCamera = false;
        /// An angle's handle: the size's arrow it stands below
        Gui::LinearGizmo* sizeGizmo = nullptr;
    };
    std::vector<EdgeHandle> edgeHandles;
    /// What edgeHandles were made for: the type, the edges, their modes
    std::string edgeHandleLayout;
    /// The chamfer's edges by name, every edge of the base with UseAllEdges
    std::vector<std::string> getChamferEdges() const;
    /// Make the handles when the edges or their modes changed; place them
    void updateEdgeHandles();
    void clearEdgeHandles();
    /// Put each equal size's arrow in the face turned more toward the camera
    void placeEdgeHandles(bool force);
    /// From the gizmos' frame to the world's
    Base::Matrix4D gizmoToWorld() const;
    EdgeHandle* findEdgeHandle(const Gui::Gizmo* gizmo);
    void fillEdgeMenu(QMenu& menu, Gui::Gizmo* gizmo);
    /// The values an edge is chamfered with: its own, or the defaults
    Part::TopoShape::ChamferInfo edgeInfo(const std::string& edge) const;
    static Mode edgeMode(const Part::TopoShape::ChamferInfo& info);
    void setEdgeMode(const std::string& edge, Mode mode);
    void setEdgeValue(const std::string& edge, Field field, double value);
    /// An own edge's flip; a default edge's is the chamfer's
    void setEdgeFlip(const std::string& edge, bool flip);
    /// Put a customised edge in \a group; 0, a new group of its own
    void setEdgeGroup(const std::string& edge, int group);
    /// Take the edge out of the chamfer, as the panel's removal does
    void removeEdge(const std::string& edge);
    /// A group's values in a few words: "S 1 mm", "S 1 mm / 2 mm" ...
    static QString groupSummary(const Part::TopoShape::ChamferInfo& info);
    //@}

    /// The values shown and typed in the view. Last, so that it goes before
    /// the handles it highlights.
    std::unique_ptr<Gui::OnViewValueSet> onViewValues;
    /** Every recompute of the chamfer, whoever made it: the type's combo, a
     * script, the property view and an undo recompute without the panel's
     * finishedRecomputeFeature(), and the handles must follow all of them.
     * After onViewValues, so that it goes first.
     */
    fastsignals::scoped_connection connRecomputed;
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
