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
#include <Base/Matrix.h>
#include <Gui/Inventor/Draggers/Gizmo.h>
#include <Mod/Part/App/PropertyDressUp.h>
#include <Mod/Part/App/TopoShape.h>

#include "TaskDressUpParameters.h"
#include "ViewProviderFillet.h"

#include <gp_Pnt.hxx>

namespace PartDesign {
class Fillet;
}

class QMenu;
class Ui_TaskFilletParameters;

namespace Gui {
class ExpressionBinding;
class OnViewValueSet;
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
    bool isBaseItem(QTreeWidgetItem *item) const override;
    void syncOtherItems() override;
    void populateOtherItems() override;
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
     * each with an optional setback of its own, then the faces at the vertex,
     * each with an optional depth (how far the patch's boundary bows into it).
     */
    //@{
    /// The vertex row of a corner, from its row or one of its edges'
    static QTreeWidgetItem *getCornerItem(QTreeWidgetItem *item);
    /// A fillet edge or a face at a corner, by name
    typedef std::vector<std::pair<std::string, Part::TopoShape>> CornerEdges;
    /// The fillet edges ending at each corner vertex of the references, then
    /// the faces there
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
    void setupGizmos(ViewProviderDressUp* vp);
    void setGizmoPositions();
    /// Show the values in the view the feature is edited in, once it has one
    void attachOnViewValues();

    /** @name The radii in the view (docs/OnViewValueEditing.md 3.3)
     * One arrow on every edge, in whichever of its two faces is turned more
     * toward the camera. An edge that takes the default radius has one, a
     * handle of the default's value; a customised edge one per point of its
     * own, each a value of its own. Each arrow's root button opens the edge's
     * menu: its mode (default, one radius, two radii) and Remove.
     */
    //@{
    /// The default radius's value in the set
    static constexpr int DefaultValue = 0;
    /// Arrows at most; edges past them have none
    static constexpr int MaxEdgeHandles = 64;
    /// edgeMode() of an edge whose points are none of the three modes
    static constexpr int VaryingMode = 3;
    struct HandlePlacement {
        Base::Vector3d position;
        Base::Vector3d dir;
    };
    struct EdgeHandle {
        std::string edge;
        /// The point of the edge's segments it drives; -1, the default radius
        int point = -1;
        Gui::LinearGizmo* gizmo = nullptr;
        /// Hidden, bound to the point's radius; none for the default's
        Gui::QuantitySpinBox* box = nullptr;
        /// In each of the edge's two faces
        HandlePlacement placements[2];
        int face = 0;
        bool placed = false;
    };
    std::vector<EdgeHandle> edgeHandles;
    /// What edgeHandles were made for: edges and their numbers of points
    std::string edgeHandleLayout;
    /// The fillet's edges by name, every edge of the base with UseAllEdges
    std::vector<std::string> getFilletEdges() const;
    /// Make the arrows when the edges or their points changed; place them
    void updateEdgeHandles();
    void clearEdgeHandles();
    /// Put each arrow in the face turned more toward the camera
    void placeEdgeHandles(bool force);
    /// From the gizmos' frame to the world's
    Base::Matrix4D gizmoToWorld() const;
    EdgeHandle* findEdgeHandle(const Gui::LinearGizmo* gizmo);
    void fillEdgeMenu(QMenu& menu, Gui::LinearGizmo* gizmo);
    /// 0 default radius, 1 one radius, 2 two radii, VaryingMode otherwise
    static int edgeMode(const Part::PropertyFilletSegments::Segments& segments);
    void setEdgeMode(const std::string& edge, int mode, double radius);
    void setEdgePointRadius(const std::string& edge, int point, double radius);
    /// Take the edge out of the fillet, as the panel's removal does
    void removeEdge(const std::string& edge);
    //@}

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
    /// One depth handle per face of the current corner, at most this many;
    /// 'edge' is the face's name
    static constexpr int CornerFaceGizmoCount = 4;
    std::vector<CornerGizmo> faceGizmos;
    bool cornerRecomputePending = false;
    void setCornerGizmoPositions();
    void setCornerFaceGizmoPositions(PartDesign::Fillet *fillet,
                                     const std::string &vertexName,
                                     const gp_Pnt &point,
                                     const CornerEdges &faces);

    /// The values shown and typed in the view. Last, so that it goes before
    /// the handles it highlights.
    std::unique_ptr<Gui::OnViewValueSet> onViewValues;
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
