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
# include <BRepAdaptor_Curve.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepLProp_SLProps.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <BRep_Tool.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <GeomAPI_ProjectPointOnSurf.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <gp.hxx>
#endif

#include <boost/algorithm/string/predicate.hpp>

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
#include <Mod/Part/App/GizmoHelper.h>
#include <Mod/PartDesign/App/FeatureFillet.h>

#include "ui_TaskFilletParameters.h"
#include "TaskFilletParameters.h"


using namespace PartDesignGui;
using namespace Gui;

namespace {
// The column of a corner's setbacks
constexpr int SetbackColumn = 4;
}

/* TRANSLATOR PartDesignGui::TaskFilletParameters */

FilletSegmentDelegate::FilletSegmentDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *FilletSegmentDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    // A corner takes setbacks, an edge segments
    auto item = static_cast<QTreeWidgetItem*>(index.internalPointer());
    if (TaskFilletParameters::getCornerItem(item)) {
        if (index.column() != SetbackColumn)
            return nullptr;
        Gui::QuantitySpinBox *editor = new Gui::QuantitySpinBox(parent);
        editor->setUnit(Base::Unit::Length);
        editor->setMinimum(0.0);
        editor->setMaximum(INT_MAX);
        editor->setSingleStep(0.1);
        if (auto owner = qobject_cast<TaskFilletParameters*>(this->parent()))
            owner->setBinding(editor, index);
        return editor;
    }
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
    model->setData(index, QString::fromStdString(value.getUserString()), Qt::DisplayRole);
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

    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(getDressUpView()->getObject());
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

    // a vertex is a corner (Fillet::Corners)
    allowVertexes = true;
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
"point with absolute distance along the edge.\n\n"
"A vertex sets back the corner there: each fillet ending at\n"
"the vertex stops 'Setback' away from it, and one smooth patch\n"
"closes the opening. The rows under the vertex are its fillets;\n"
"one given a setback of its own stops there instead. 'Clear'\n"
"removes the setbacks of single fillets."));

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

    Base::connect(ui->treeWidgetReferences, &QTreeWidget::currentItemChanged,
        [this](QTreeWidgetItem *, QTreeWidgetItem *) { setCornerGizmoPositions(); });

    refresh();
    ui->filletRadius->selectAll();

    setupGizmos(DressUpView);
}

void TaskFilletParameters::setupGizmos(ViewProviderDressUp* vp)
{
    if (!GizmoContainer::isEnabled()) {
        return;
    }

    radiusGizmo = new Gui::LinearGizmo(ui->filletRadius);
    radiusGizmo2 = new Gui::LinearGizmo(ui->filletRadius);

    // The handles of the current corner, one per fillet, each driving a
    // hidden spin box. The container takes a fixed list, so there is a pool.
    cornerGizmos.resize(CornerGizmoCount);
    for (int i=0; i<CornerGizmoCount; ++i) {
        auto &cornerGizmo = cornerGizmos[i];
        auto spinBox = new Gui::QuantitySpinBox(this);
        spinBox->hide();
        spinBox->setUnit(Base::Unit::Length);
        spinBox->setMinimum(0.0);
        spinBox->setMaximum(INT_MAX);
        cornerGizmo.spinBox = spinBox;
        cornerGizmo.gizmo = new Gui::LinearGizmo(spinBox);
        Base::connect(spinBox, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            [this, i](double value) {
                auto DressUpView = getDressUpView();
                const auto &target = cornerGizmos[i];
                if (!DressUpView || target.vertex.empty())
                    return;
                auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
                setupTransaction();
                pcFillet->Corners.setValue(target.vertex, target.edge, value);
                recompute();
            });
    }
    // and its faces' depth handles, the same way
    faceGizmos.resize(CornerFaceGizmoCount);
    for (int i=0; i<CornerFaceGizmoCount; ++i) {
        auto &faceGizmo = faceGizmos[i];
        auto spinBox = new Gui::QuantitySpinBox(this);
        spinBox->hide();
        spinBox->setUnit(Base::Unit::Length);
        spinBox->setMinimum(0.0);
        spinBox->setMaximum(INT_MAX);
        faceGizmo.spinBox = spinBox;
        faceGizmo.gizmo = new Gui::LinearGizmo(spinBox);
        Base::connect(spinBox, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            [this, i](double value) {
                auto DressUpView = getDressUpView();
                const auto &target = faceGizmos[i];
                if (!DressUpView || target.vertex.empty() || value <= 0.0)
                    return;
                auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
                setupTransaction();
                pcFillet->Corners.setValue(target.vertex, target.edge, value);
                recompute();
            });
    }
    static_assert(CornerGizmoCount == 6 && CornerFaceGizmoCount == 4,
                  "the container below lists every corner gizmo");
    gizmoContainer = GizmoContainer::create({radiusGizmo, radiusGizmo2,
                                             cornerGizmos[0].gizmo, cornerGizmos[1].gizmo,
                                             cornerGizmos[2].gizmo, cornerGizmos[3].gizmo,
                                             cornerGizmos[4].gizmo, cornerGizmos[5].gizmo,
                                             faceGizmos[0].gizmo, faceGizmos[1].gizmo,
                                             faceGizmos[2].gizmo, faceGizmos[3].gizmo}, vp);

    setGizmoPositions();
    showDraggerHints();
}

void TaskFilletParameters::setGizmoPositions()
{
    if (!gizmoContainer) {
        return;
    }

    auto DressUpView = getDressUpView();
    auto fillet = DressUpView ? dynamic_cast<PartDesign::Fillet*>(DressUpView->getObject()) : nullptr;
    if (!fillet || fillet->isError()) {
        gizmoContainer->visible = false;
        return;
    }
    Part::TopoShape baseShape = fillet->getBaseShape(true);
    std::vector<Part::TopoShape> shapes = fillet->getContinuousEdges(baseShape);

    if (shapes.size() == 0) {
        gizmoContainer->visible = false;
        return;
    }
    gizmoContainer->visible = true;

    // Attach the arrow to the first edge
    Part::TopoShape edge = shapes[0];
    auto [face1, face2] = getAdjacentFacesFromEdge(edge, baseShape);

    DraggerPlacementProps props1 = getDraggerPlacementFromEdgeAndFace(edge, face1);
    radiusGizmo->Gizmo::setDraggerPlacement(props1.position, props1.dir);

    DraggerPlacementProps props2 = getDraggerPlacementFromEdgeAndFace(edge, face2);
    radiusGizmo2->Gizmo::setDraggerPlacement(props2.position, props2.dir);

    // The dragger length won't be equal to the radius if the two faces
    // are not orthogonal so this correction is needed
    double angle = props1.dir.GetAngle(props2.dir);
    double correction = 1 / std::tan(angle / 2);

    radiusGizmo->setMultFactor(correction);
    radiusGizmo2->setMultFactor(correction);

    setCornerGizmoPositions();
}

void TaskFilletParameters::setCornerGizmoPositions()
{
    if (!gizmoContainer || cornerGizmos.empty())
        return;
    for (auto *pool : {&cornerGizmos, &faceGizmos}) {
        for (auto &cornerGizmo : *pool) {
            cornerGizmo.gizmo->setVisibility(false);
            cornerGizmo.vertex.clear();
            cornerGizmo.edge.clear();
        }
    }

    auto DressUpView = getDressUpView();
    auto fillet = DressUpView ? dynamic_cast<PartDesign::Fillet*>(DressUpView->getObject()) : nullptr;
    auto item = getCornerItem(ui->treeWidgetReferences->currentItem());
    if (!fillet || !item)
        return;
    std::string vertexName = getGeometryItemText(item).constData();
    Part::TopoShape baseShape = fillet->getBaseShape(true);
    TopoDS_Shape vertex = baseShape.getSubShape(vertexName.c_str(), true);
    if (vertex.IsNull() || vertex.ShapeType() != TopAbs_VERTEX)
        return;
    gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(vertex));
    const auto *corner = fillet->Corners.getValue(vertexName);

    auto cornerEdges = getCornerEdges();
    setCornerFaceGizmoPositions(fillet, vertexName, point, cornerEdges[vertexName]);
    int i = 0;
    for (const auto &edge : cornerEdges[vertexName]) {
        if (i >= CornerGizmoCount)
            break;
        if (Part::PropertyFilletCorners::isFaceName(edge.first))
            continue;
        // along the edge, away from the vertex
        BRepAdaptor_Curve curve(TopoDS::Edge(edge.second.getShape()));
        double first = curve.FirstParameter();
        double last = curve.LastParameter();
        bool atLast = curve.Value(last).SquareDistance(point)
            < curve.Value(first).SquareDistance(point);
        gp_Pnt pos;
        gp_Vec dir;
        curve.D1(atLast ? last : first, pos, dir);
        if (dir.Magnitude() < gp::Resolution())
            continue;
        if (atLast)
            dir.Reverse();
        dir.Normalize();

        double value = 0.0;
        bool own = false;
        if (corner) {
            auto it = corner->edges.find(edge.first);
            own = it != corner->edges.end();
            if (own)
                value = it->second;
            else if (corner->setback > 0.0)
                value = corner->setback;
        }
        // no handle for a setback an expression drives
        App::ObjectIdentifier path(fillet->Corners);
        path << App::ObjectIdentifier::SimpleComponent(vertexName)
             << App::ObjectIdentifier::SimpleComponent(own ? edge.first : std::string("Setback"));
        if (fillet->getExpression(path).expression)
            continue;

        auto &cornerGizmo = cornerGizmos[i++];
        cornerGizmo.vertex = vertexName;
        cornerGizmo.edge = edge.first;
        {
            QSignalBlocker blocker(cornerGizmo.spinBox);
            cornerGizmo.spinBox->setValue(value);
        }
        cornerGizmo.gizmo->Gizmo::setDraggerPlacement(Base::Vector3d(point.X(), point.Y(), point.Z()),
                                                      Base::Vector3d(dir.X(), dir.Y(), dir.Z()));
        cornerGizmo.gizmo->setDragLength(value);
        cornerGizmo.gizmo->setVisibility(true);
    }
}

void TaskFilletParameters::setCornerFaceGizmoPositions(PartDesign::Fillet *fillet,
                                                       const std::string &vertexName,
                                                       const gp_Pnt &point,
                                                       const CornerEdges &faces)
{
    if (faceGizmos.empty() || fillet->isError())
        return;
    // The corner's patch in the result: its face nearest the vertex that is
    // not one of the base's kinds (planes, cylinders... ; the patch is a
    // B-spline)
    Part::TopoShape result = fillet->Shape.getShape();
    result.setTransform(Base::Matrix4D());
    TopoDS_Shape vertex = BRepBuilderAPI_MakeVertex(point).Vertex();
    TopoDS_Face patch;
    double nearest = DBL_MAX;
    for (TopExp_Explorer xp(result.getShape(), TopAbs_FACE); xp.More(); xp.Next()) {
        const TopoDS_Face &face = TopoDS::Face(xp.Current());
        if (BRepAdaptor_Surface(face).GetType() != GeomAbs_BSplineSurface)
            continue;
        BRepExtrema_DistShapeShape dist(vertex, face);
        if (dist.IsDone() && dist.Value() < nearest) {
            nearest = dist.Value();
            patch = face;
        }
    }
    if (patch.IsNull())
        return;
    const auto *corner = fillet->Corners.getValue(vertexName);

    int i = 0;
    for (const auto &entry : faces) {
        if (i >= CornerFaceGizmoCount)
            break;
        if (!Part::PropertyFilletCorners::isFaceName(entry.first))
            continue;
        const TopoDS_Face &face = TopoDS::Face(entry.second.getShape());
        Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
        // the patch's boundary on the face: an edge of the patch lying on
        // the face's surface (its middle too, which a fillet's cut does not)
        auto onFace = [&](const gp_Pnt &p) {
            GeomAPI_ProjectPointOnSurf proj(p, surface);
            return proj.NbPoints() > 0 && proj.LowerDistance() < 1e-3;
        };
        bool found = false;
        gp_Pnt a, b, far;
        double bow = 0.0;
        for (TopExp_Explorer xp(patch, TopAbs_EDGE); xp.More() && !found; xp.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(xp.Current()));
            double first = curve.FirstParameter(), last = curve.LastParameter();
            a = curve.Value(first);
            b = curve.Value(last);
            if (a.Distance(b) < Precision::Confusion() || !onFace(a) || !onFace(b)
                    || !onFace(curve.Value((first + last) / 2)))
                continue;
            found = true;
            gp_Lin chord(a, gp_Dir(gp_Vec(a, b)));
            for (int k = 1; k < 32; ++k) {
                gp_Pnt p = curve.Value(first + (last - first) * k / 32);
                double d = chord.Distance(p);
                if (d > bow) {
                    bow = d;
                    far = p;
                }
            }
        }
        if (!found)
            continue;
        // from the chord's middle, square to it in the face, away from the
        // vertex
        gp_Pnt mid((a.XYZ() + b.XYZ()) / 2);
        GeomAPI_ProjectPointOnSurf proj(mid, surface);
        double u, v;
        proj.LowerDistanceParameters(u, v);
        BRepAdaptor_Surface adaptor(face, false);
        BRepLProp_SLProps props(adaptor, u, v, 1, Precision::Confusion());
        if (!props.IsNormalDefined())
            continue;
        gp_Vec dir = gp_Vec(props.Normal()).Crossed(gp_Vec(a, b));
        if (dir.Magnitude() < gp::Resolution())
            continue;
        if (dir.Dot(gp_Vec(point, mid)) < 0)
            dir.Reverse();
        dir.Normalize();

        double value = bow;
        if (corner) {
            auto it = corner->edges.find(entry.first);
            if (it != corner->edges.end())
                value = it->second;
        }
        // no handle for a depth an expression drives
        App::ObjectIdentifier path(fillet->Corners);
        path << App::ObjectIdentifier::SimpleComponent(vertexName)
             << App::ObjectIdentifier::SimpleComponent(entry.first);
        if (fillet->getExpression(path).expression)
            continue;

        auto &faceGizmo = faceGizmos[i++];
        faceGizmo.vertex = vertexName;
        faceGizmo.edge = entry.first;
        {
            QSignalBlocker blocker(faceGizmo.spinBox);
            faceGizmo.spinBox->setValue(value);
        }
        faceGizmo.gizmo->Gizmo::setDraggerPlacement(Base::Vector3d(mid.X(), mid.Y(), mid.Z()),
                                                    Base::Vector3d(dir.X(), dir.Y(), dir.Z()));
        faceGizmo.gizmo->setDragLength(value);
        faceGizmo.gizmo->setVisibility(true);
    }
}

void TaskFilletParameters::finishedRecomputeFeature()
{
    TaskDressUpParameters::finishedRecomputeFeature();
    // A corner's fillets follow the edge list
    auto cornerEdges = getCornerEdges();
    for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto item = ui->treeWidgetReferences->topLevelItem(i);
        if (getCornerItem(item))
            refreshCorner(item, cornerEdges[getGeometryItemText(item).constData()]);
    }
    // The edge list or the base may have changed; the radius alone does not
    // move the gizmos, but reading the placement again is cheap.
    setGizmoPositions();
}

void TaskFilletParameters::onRefDeleted() {
    Base::StateLocker guard(busy);
    removeSegments();
}

void TaskFilletParameters::setBinding(Gui::ExpressionBinding *binding,
                                      const QModelIndex &index)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView || !index.isValid())
        return;
    auto item = static_cast<QTreeWidgetItem*>(index.internalPointer());
    if (!item)
        return;
    if (getCornerItem(item)) {
        binding->bind(getCornerPath(item));
        return;
    }
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
    auto DressUpView = getDressUpView();
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
    auto cornerEdges = getCornerEdges();
    for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto item = ui->treeWidgetReferences->topLevelItem(i);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (getCornerItem(item)) {
            refreshCorner(item, cornerEdges[getGeometryItemText(item).constData()]);
            continue;
        }
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
    if (getCornerItem(item)) {
        if (column == SetbackColumn)
            updateCorner(item);
        return;
    }
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
    auto DressUpView = getDressUpView();
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
    auto DressUpView = getDressUpView();
    if(!DressUpView)
        return;
    std::set<QTreeWidgetItem*> items;
    std::vector<QTreeWidgetItem*> corners;
    for (auto item : ui->treeWidgetReferences->selectedItems()) {
        if (getCornerItem(item)) {
            corners.push_back(item);
            continue;
        }
        if (auto parent = item->parent())
            item = parent;
        items.insert(item);
    }
    setupTransaction();
    clearCornerEdges(corners);
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
    auto DressUpView = getDressUpView();
    if(!DressUpView)
        return;
    setupTransaction();
    PartDesign::Fillet* pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    std::vector<QTreeWidgetItem*> cornerEdges;
    for (auto item : ui->treeWidgetReferences->selectedItems()) {
        auto parent = item->parent();
        if (!parent)
            continue;
        // the fillet of a corner stays; it loses its own setback
        if (getCornerItem(item)) {
            cornerEdges.push_back(item);
            item->setSelected(false);
            continue;
        }
        pcFillet->Segments.removeValue(getGeometryItemText(parent).constData(),
                                       parent->indexOfChild(item));
        delete item;
    }
    clearCornerEdges(cornerEdges);
    TaskDressUpParameters::onRefDeleted();
    recompute();
}

void TaskFilletParameters::setSegment(QTreeWidgetItem *item, double param, double radius, double length)
{
    auto DressUpView = getDressUpView();
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
        item->setText(index, noText ? QString() : QString::fromStdString(q.getUserString()));
    };
    setupItem("Radius", 1, Base::Quantity(radius, Base::Unit::Length), false);
    setupItem("Param", 2, Base::Quantity(param), length>0.0);
    setupItem("Length", 3, Base::Quantity(length, Base::Unit::Length), length==0.0);
}

void TaskFilletParameters::newSegment(int editColumn)
{
    auto current = getCurrentItem();
    if (!current || getCornerItem(current))
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
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
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
    auto DressUpView = getDressUpView();
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
    // A picked vertex is a corner, set back by the radius to begin with
    auto DressUpView = getDressUpView();
    if (!DressUpView || !getCornerItem(item))
        return;
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    std::string vertex = getGeometryItemText(item).constData();
    if (pcFillet->Corners.getValue(vertex))
        return;
    setupTransaction();
    pcFillet->Corners.setValue(vertex, getRadius());
    // Setting the corner put its vertex in Base already, so the reference
    // sync that called here finds nothing changed and does not recompute
    if (!cornerRecomputePending) {
        cornerRecomputePending = true;
        QMetaObject::invokeMethod(this, [this]() {
            cornerRecomputePending = false;
            recompute();
        }, Qt::QueuedConnection);
    }
}

QTreeWidgetItem *TaskFilletParameters::getCornerItem(QTreeWidgetItem *item)
{
    if (!item)
        return nullptr;
    if (auto parent = item->parent())
        item = parent;
    if (!boost::starts_with(getGeometryItemText(item).constData(), "Vertex"))
        return nullptr;
    return item;
}

std::map<std::string, TaskFilletParameters::CornerEdges> TaskFilletParameters::getCornerEdges() const
{
    std::map<std::string, CornerEdges> res;
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return res;
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    std::vector<std::string> vertexes;
    for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto item = ui->treeWidgetReferences->topLevelItem(i);
        if (getCornerItem(item))
            vertexes.emplace_back(getGeometryItemText(item).constData());
    }
    if (vertexes.empty())
        return res;
    try {
        Part::TopoShape baseShape = pcFillet->getBaseShape(true);
        if (baseShape.isNull())
            return res;
        auto edges = pcFillet->UseAllEdges.getValue() ? baseShape.getSubTopoShapes(TopAbs_EDGE)
                                                      : pcFillet->getContinuousEdges(baseShape);
        for (const auto &name : vertexes) {
            TopoDS_Shape vertex = baseShape.getSubShape(name.c_str(), true);
            if (vertex.IsNull())
                continue;
            auto &cornerEdges = res[name];
            for (const auto &edge : edges) {
                for (const auto &v : edge.getSubShapes(TopAbs_VERTEX)) {
                    if (v.IsSame(vertex)) {
                        int index = baseShape.findShape(edge.getShape());
                        if (index)
                            cornerEdges.emplace_back("Edge" + std::to_string(index), edge);
                        break;
                    }
                }
            }
            // then the faces at the vertex, which can be given depths
            int index = 0;
            for (const auto &face : baseShape.getSubTopoShapes(TopAbs_FACE)) {
                ++index;
                for (const auto &v : face.getSubShapes(TopAbs_VERTEX)) {
                    if (v.IsSame(vertex)) {
                        cornerEdges.emplace_back("Face" + std::to_string(index), face);
                        break;
                    }
                }
            }
        }
    }
    // a bad reference is the recompute's to report
    catch (Base::Exception &) {
    }
    catch (Standard_Failure &) {
    }
    return res;
}

App::ObjectIdentifier TaskFilletParameters::getCornerPath(QTreeWidgetItem *item) const
{
    auto DressUpView = getDressUpView();
    auto corner = getCornerItem(item);
    if (!DressUpView || !corner)
        return App::ObjectIdentifier();
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    App::ObjectIdentifier path(pcFillet->Corners);
    path << App::ObjectIdentifier::SimpleComponent(std::string(getGeometryItemText(corner).constData()))
         << App::ObjectIdentifier::SimpleComponent(item == corner ? std::string("Setback")
                                                                 : item->text(0).toStdString());
    return path;
}

void TaskFilletParameters::refreshCorner(QTreeWidgetItem *item, const CornerEdges &edges)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    QSignalBlocker blocker(ui->treeWidgetReferences);
    std::string vertex = getGeometryItemText(item).constData();
    const auto *corner = pcFillet->Corners.getValue(vertex);
    double setback = corner ? corner->setback : -1.0;

    auto linkColor = QVariant::fromValue(QApplication::palette().color(QPalette::Link));
    auto inherited = QVariant::fromValue(QApplication::palette().color(QPalette::PlaceholderText));
    // own: the setback is the item's, not the corner's
    auto setupItem = [&](QTreeWidgetItem *child, double value, bool own) {
        child->setFlags(child->flags() | Qt::ItemIsEditable);
        child->setData(SetbackColumn, Qt::UserRole, std::max(value, 0.0));
        QString text;
        if (value >= 0.0) {
            text = QString::fromStdString(Base::Quantity(value, Base::Unit::Length).getUserString());
            if (!own)
                text = QStringLiteral("(%1)").arg(text);
        }
        child->setText(SetbackColumn, text);
        auto expr = pcFillet->getExpression(getCornerPath(child)).expression;
        if (expr) {
            child->setData(SetbackColumn, Qt::ToolTipRole, QString::fromUtf8(expr->toString().c_str()));
            child->setData(SetbackColumn, Qt::ForegroundRole, linkColor);
        }
        else {
            child->setData(SetbackColumn, Qt::ToolTipRole, QVariant());
            child->setData(SetbackColumn, Qt::ForegroundRole, own ? QVariant() : inherited);
        }
    };
    setupItem(item, setback, true);

    // The fillets ending at the vertex, then any other edge given a setback
    // (a tangent chain names its contour by any of its edges)
    std::vector<std::string> names;
    for (const auto &edge : edges)
        names.push_back(edge.first);
    if (corner) {
        for (const auto &v : corner->edges) {
            if (std::find(names.begin(), names.end(), v.first) == names.end())
                names.push_back(v.first);
        }
    }
    int j = 0;
    for (const auto &name : names) {
        auto child = j < item->childCount() ? item->child(j) : new QTreeWidgetItem(item);
        ++j;
        child->setText(0, QString::fromStdString(name));
        const double *own = nullptr;
        if (corner) {
            auto it = corner->edges.find(name);
            if (it != corner->edges.end())
                own = &it->second;
        }
        bool isFace = Part::PropertyFilletCorners::isFaceName(name);
        if (own)
            setupItem(child, *own, true);
        else
            // a face without a depth: the fairest curve, nothing to show
            setupItem(child, isFace ? -1.0 : setback, false);
        if (isFace && !child->data(SetbackColumn, Qt::ToolTipRole).isValid())
            child->setData(SetbackColumn, Qt::ToolTipRole,
                tr("Depth: how far the corner patch's boundary bows into this face,\n"
                   "away from the vertex, at its middle. Empty: the fairest curve."));
    }
    while (item->childCount() > j)
        delete item->child(item->childCount()-1);
    item->setExpanded(true);
}

void TaskFilletParameters::updateCorner(QTreeWidgetItem *item)
{
    auto DressUpView = getDressUpView();
    auto corner = getCornerItem(item);
    if (!DressUpView || !corner)
        return;
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    std::string vertex = getGeometryItemText(corner).constData();
    double value = item->data(SetbackColumn, Qt::UserRole).toDouble();
    setupTransaction();
    std::string name = item->text(0).toStdString();
    if (item == corner)
        pcFillet->Corners.setValue(vertex, value);
    else if (value <= 0.0 && Part::PropertyFilletCorners::isFaceName(name))
        // a depth of 0 is none: the fairest curve
        pcFillet->Corners.removeValue(vertex, name);
    else
        pcFillet->Corners.setValue(vertex, name, value);
    recompute();
}

void TaskFilletParameters::clearCornerEdges(const std::vector<QTreeWidgetItem*> &items)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView || items.empty())
        return;
    auto pcFillet = static_cast<PartDesign::Fillet*>(DressUpView->getObject());
    for (auto item : items) {
        auto corner = getCornerItem(item);
        if (!corner)
            continue;
        std::string vertex = getGeometryItemText(corner).constData();
        if (item != corner) {
            pcFillet->Corners.removeValue(vertex, item->text(0).toStdString());
            continue;
        }
        if (const auto *value = pcFillet->Corners.getValue(vertex)) {
            Part::PropertyFilletCorners::Corner cleared;
            cleared.setback = value->setback;
            pcFillet->Corners.setValue(vertex, cleared);
        }
    }
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
