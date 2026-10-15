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

#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <QActionGroup>
#include <QMenu>
#include <QPointer>
#include <QSpinBox>
#include <QTimer>
#include <Precision.hxx>
#include <Standard_Failure.hxx>

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
#include <Gui/OnViewValue.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/Selection.h>
#include <Gui/SpinBox.h>
#include <Gui/Tools.h>
#include <Gui/ViewerContext.h>
#include <Gui/ViewProvider.h>
#include <Gui/WaitCursor.h>
#include <Gui/Inventor/Draggers/SoRotationDragger.h>
#include <Gui/Utilities.h>
#include <Mod/Part/App/GizmoHelper.h>
#include <Mod/PartDesign/App/FeatureChamfer.h>

#include "ui_TaskChamferParameters.h"
#include "TaskChamferParameters.h"


using namespace PartDesignGui;
using namespace Gui;

namespace {
// The column of a customised edge's group (docs/OnViewValueEditing.md 6.4)
constexpr int GroupColumn = 5;
}

ChamferInfoDelegate::ChamferInfoDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *ChamferInfoDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    // a customised edge's group: a number, joined by typing another's
    if (index.column() == GroupColumn) {
        if (index.model()->data(index, Qt::UserRole).toInt() <= 0)
            return nullptr;
        auto editor = new QSpinBox(parent);
        editor->setRange(1, 9999);
        return editor;
    }
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

    if (auto groupBox = qobject_cast<QSpinBox*>(editor)) {
        groupBox->setValue(data.toInt());
        groupBox->selectAll();
        return;
    }
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
    if (auto groupBox = qobject_cast<QSpinBox*>(editor)) {
        groupBox->interpretText();
        model->setData(index, QString::number(groupBox->value()), Qt::DisplayRole);
        model->setData(index, groupBox->value(), Qt::UserRole);
        return;
    }
    if (index.column() == 4) {
        model->setData(index, static_cast<QCheckBox*>(editor)->isChecked(), Qt::UserRole);
        return;
    }
    Gui::QuantitySpinBox *spinBox = static_cast<Gui::QuantitySpinBox*>(editor);
    spinBox->interpretText();
    Base::Quantity value = spinBox->value();
    model->setData(index, QString::fromStdString(value.getUserString()), Qt::DisplayRole);
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
    // A customised edge's group, after its values
    if (ui->treeWidgetReferences->columnCount() <= GroupColumn)
        ui->treeWidgetReferences->setColumnCount(GroupColumn + 1);
    ui->treeWidgetReferences->headerItem()->setText(GroupColumn, tr("Group"));
    ui->treeWidgetReferences->headerItem()->setToolTip(GroupColumn, tr(
"The group of an edge with values of its own. A change to the sizes\n"
"or the angle of one edge of a group is made to every edge of it;\n"
"its flip stays its own. Type another group's number to join it."));

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
    ui->chamferSize->selectAll();

    // The values in the view (docs/OnViewValueEditing.md 3.4): the defaults'
    // labels always at the top middle, handles on every edge -- an edge that
    // takes the defaults drives them, a customised one its own values, whose
    // labels show beside the handle while it is active
    onViewValues = std::make_unique<Gui::OnViewValueSet>();
    setupGizmos(DressUpView);
    connect(onViewValues.get(), &Gui::OnViewValueSet::cameraChanged, this, [this]() {
        placeEdgeHandles(false);
    });
    connRecomputed = pcChamfer->getDocument()->signalRecomputedObject.connect(
        [this, self = QPointer<TaskChamferParameters>(this)](const App::DocumentObject& obj) {
            if (&obj != getChamfer()) {
                return;
            }
            // After the recompute: the handles' layout may change, and the
            // recompute may be inside a drag of one of them
            QTimer::singleShot(0, self, [self]() {
                if (self) {
                    self->setGizmoPositions();
                }
            });
        });
    attachOnViewValues();
    // The view is told of the edit after the panel is made
    QTimer::singleShot(0, this, [this]() {
        attachOnViewValues();
    });
}

PartDesign::Chamfer* TaskChamferParameters::getChamfer() const
{
    auto vp = getDressUpView();
    return vp ? dynamic_cast<PartDesign::Chamfer*>(vp->getObject()) : nullptr;
}

void TaskChamferParameters::attachOnViewValues()
{
    auto vp = getDressUpView();
    if (!onViewValues || !vp) {
        return;
    }
    if (auto view = vp->getEditViewer()) {
        if (view != onViewValues->getView()) {
            onViewValues->attach(view);
            placeEdgeHandles(true);
        }
    }
    onViewValues->refresh();
}

Base::Matrix4D TaskChamferParameters::gizmoToWorld() const
{
    // The gizmos' frame is the editing transform less the feature's
    // placement (ViewProviderDragger::setEditViewer)
    auto vp = getDressUpView();
    auto chamfer = getChamfer();
    if (!vp || !chamfer) {
        return {};
    }
    Base::Matrix4D mat = vp->getDocument()->getEditingTransform();
    Base::Matrix4D placement = chamfer->Placement.getValue().toMatrix();
    placement.inverse();
    mat *= placement;
    return mat;
}

void TaskChamferParameters::setupGizmos(ViewProviderDressUp* vp)
{
    if (GizmoContainer::isEnabled()) {
        // Every handle is made per edge, once the edges are known
        gizmoContainer = GizmoContainer::create({}, vp);
    }
    setGizmoPositions();
    if (gizmoContainer) {
        showDraggerHints();
    }
}

void TaskChamferParameters::setGizmoPositions()
{
    // The default values' labels with or without handles, then the handles
    updateEdgeHandles();
    if (!gizmoContainer) {
        return;
    }
    // Not hidden when the chamfer fails: a reference that is gone fails it,
    // and the handles of the edges that are there stay (entry 135)
    gizmoContainer->visible = !edgeHandles.empty();
}

namespace {
/// Show or hide a handle of either kind
void showHandle(Gui::Gizmo* gizmo, bool shown)
{
    if (auto linear = dynamic_cast<Gui::LinearGizmo*>(gizmo)) {
        linear->setVisibility(shown);
    }
    else if (auto rotation = dynamic_cast<Gui::RotationGizmo*>(gizmo)) {
        rotation->setVisibility(shown);
    }
}
}  // namespace

std::vector<std::string> TaskChamferParameters::getChamferEdges() const
{
    std::vector<std::string> names;
    auto chamfer = getChamfer();
    if (!chamfer) {
        return names;
    }
    if (chamfer->UseAllEdges.getValue()) {
        Part::TopoShape baseShape = chamfer->getBaseShape(true);
        const int count = baseShape.countSubShapes(TopAbs_EDGE);
        for (int i = 1; i <= count; ++i) {
            names.push_back("Edge" + std::to_string(i));
        }
        return names;
    }
    for (const auto& sub : chamfer->Base.getSubValues(false)) {
        if (boost::starts_with(sub, "Edge")
                && std::find(names.begin(), names.end(), sub) == names.end()) {
            names.push_back(sub);
        }
    }
    return names;
}

Part::TopoShape::ChamferInfo TaskChamferParameters::edgeInfo(const std::string& edge) const
{
    // As the feature makes it (FeatureChamfer.cpp): the edge's own values,
    // or the defaults by the chamfer's type
    Part::TopoShape::ChamferInfo info;
    auto chamfer = getChamfer();
    if (!chamfer) {
        return info;
    }
    if (chamfer->ChamferInfo.getValue(edge, info)) {
        return info;
    }
    const int type = chamfer->ChamferType.getValue();
    info.size = chamfer->Size.getValue();
    info.size2 = type == 1 ? chamfer->Size2.getValue() : 0.0;
    info.angle = type == 2 ? chamfer->Angle.getValue() : 0.0;
    info.flip = chamfer->FlipDirection.getValue();
    return info;
}

TaskChamferParameters::Mode TaskChamferParameters::edgeMode(const Part::TopoShape::ChamferInfo& info)
{
    // As TopoShape::makEChamfer reads an edge's values: an angle wins over a
    // second size, and no second size is the first both ways
    if (info.angle > 0.0) {
        return SizeAngleMode;
    }
    if (info.size2 > 0.0) {
        return TwoSizesMode;
    }
    return SizeMode;
}

void TaskChamferParameters::clearEdgeHandles()
{
    if (onViewValues) {
        // the defaults too: which of them there are follows the type
        onViewValues->truncate(0);
    }
    // An angle's handle stands below its size's arrow and watches it: it
    // goes first
    for (auto it = edgeHandles.rbegin(); it != edgeHandles.rend(); ++it) {
        if (gizmoContainer) {
            gizmoContainer->removeGizmo(it->gizmo);
        }
        if (it->box) {
            it->box->deleteLater();
        }
    }
    edgeHandles.clear();
    edgeHandleLayout.clear();
}

void TaskChamferParameters::updateEdgeHandles()
{
    auto chamfer = getChamfer();
    if (!onViewValues || !chamfer) {
        return;
    }
    const int type = chamfer->ChamferType.getValue();
    // What the handles are made for: the type, the edges, each edge's mode
    // and group. While it stays they are only placed again -- a drag
    // recomputes on every move, and its handle must live through it.
    std::string layout = "T" + std::to_string(type) + ";";
    std::vector<std::string> shown;
    if (gizmoContainer) {
        int total = 0;
        for (const auto& edge : getChamferEdges()) {
            Part::TopoShape::ChamferInfo info;
            const bool own = chamfer->ChamferInfo.getValue(edge, info);
            const Mode mode = own ? edgeMode(info) : DefaultMode;
            // one handle for one size both ways, two for two sizes or a size
            // and an angle
            const int count = (own ? mode == SizeMode : type == 0) ? 1 : 2;
            if (total + count > MaxEdgeHandles) {
                break;
            }
            total += count;
            shown.push_back(edge);
            layout += edge + ":" + std::to_string(int(mode)) + ":"
                + std::to_string(chamfer->ChamferInfo.getGroup(edge)) + ";";
        }
    }

    if (layout != edgeHandleLayout) {
        clearEdgeHandles();
        edgeHandleLayout = layout;

        // The defaults, at the top middle: the size, and the second size or
        // the angle by the type
        auto menu = [this](QMenu& m, Gui::Gizmo* gizmo) {
            fillEdgeMenu(m, gizmo);
        };
        const int sizeValue = onViewValues->addValue(
            ui->chamferSize, tr("Size"), Gui::OnViewValueSet::Place::TopMiddle);
        onViewValues->setMenu(sizeValue, menu);
        int size2Value = -1;
        int angleValue = -1;
        if (type == 1) {
            size2Value = onViewValues->addValue(
                ui->chamferSize2, tr("Size 2"), Gui::OnViewValueSet::Place::TopMiddle);
            onViewValues->setMenu(size2Value, menu);
        }
        else if (type == 2) {
            angleValue = onViewValues->addValue(
                ui->chamferAngle, tr("Angle"), Gui::OnViewValueSet::Place::TopMiddle);
            onViewValues->setMenu(angleValue, menu);
        }

        for (const auto& edge : shown) {
            Part::TopoShape::ChamferInfo info;
            const bool own = chamfer->ChamferInfo.getValue(edge, info);
            if (!own) {
                // drives the defaults: an equal size's arrow follows the
                // camera, two sizes' are one in each face
                EdgeHandle size;
                size.edge = edge;
                size.field = SizeField;
                size.followsCamera = type == 0;
                auto sizeGizmo = new Gui::LinearGizmo(ui->chamferSize);
                size.gizmo = sizeGizmo;
                onViewValues->addHandle(sizeValue, sizeGizmo);
                gizmoContainer->appendGizmo(sizeGizmo);
                edgeHandles.push_back(size);
                if (type == 1) {
                    EdgeHandle size2;
                    size2.edge = edge;
                    size2.field = Size2Field;
                    size2.gizmo = new Gui::LinearGizmo(ui->chamferSize2);
                    onViewValues->addHandle(size2Value, size2.gizmo);
                    gizmoContainer->appendGizmo(size2.gizmo);
                    edgeHandles.push_back(size2);
                }
                else if (type == 2) {
                    EdgeHandle angle;
                    angle.edge = edge;
                    angle.field = AngleField;
                    angle.sizeGizmo = sizeGizmo;
                    angle.gizmo = new Gui::RotationGizmo(ui->chamferAngle);
                    onViewValues->addHandle(angleValue, angle.gizmo);
                    gizmoContainer->appendGizmo(angle.gizmo);
                    edgeHandles.push_back(angle);
                }
                continue;
            }

            // Its own values, each a value of its own in its group's colour
            const Mode mode = edgeMode(info);
            const int group = chamfer->ChamferInfo.getGroup(edge);
            auto ownValue = [&](Field field, const QString& name) -> EdgeHandle {
                EdgeHandle handle;
                handle.edge = edge;
                handle.field = field;
                auto box = new Gui::QuantitySpinBox(this);
                box->hide();
                const char* key = "Size";
                double value = info.size;
                if (field == AngleField) {
                    box->setUnit(Base::Unit::Angle);
                    box->setMinimum(chamfer->Angle.getMinimum());
                    box->setMaximum(chamfer->Angle.getMaximum());
                    key = "Angle";
                    value = info.angle;
                }
                else {
                    box->setUnit(Base::Unit::Length);
                    box->setMinimum(0.0);
                    box->setMaximum(INT_MAX);
                    if (field == Size2Field) {
                        key = "Size2";
                        value = info.size2;
                    }
                }
                App::ObjectIdentifier path(chamfer->ChamferInfo);
                path << App::ObjectIdentifier::SimpleComponent(edge)
                     << App::ObjectIdentifier::SimpleComponent(key);
                box->bind(path);
                box->setValue(value);
                handle.box = box;
                if (field == AngleField) {
                    handle.gizmo = new Gui::RotationGizmo(box);
                }
                else {
                    handle.gizmo = new Gui::LinearGizmo(box);
                }
                handle.gizmo->setColor(groupColor(group));
                // The group, then which value: "2  S", "2  S1" ...
                const int index = onViewValues->addValue(
                    box, QStringLiteral("%1  %2").arg(group).arg(name),
                    Gui::OnViewValueSet::Place::AtHandle, handle.gizmo);
                onViewValues->setGroup(index, group);
                onViewValues->setMenu(index, menu);
                gizmoContainer->appendGizmo(handle.gizmo);
                connect(box, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this,
                        [this, edge, field](double v) {
                            setEdgeValue(edge, field, v);
                        });
                return handle;
            };
            switch (mode) {
                case TwoSizesMode:
                    edgeHandles.push_back(ownValue(SizeField, tr("S1")));
                    edgeHandles.push_back(ownValue(Size2Field, tr("S2")));
                    break;
                case SizeAngleMode: {
                    edgeHandles.push_back(ownValue(SizeField, tr("S")));
                    auto sizeGizmo = static_cast<Gui::LinearGizmo*>(edgeHandles.back().gizmo);
                    EdgeHandle angle = ownValue(AngleField, tr("A"));
                    angle.sizeGizmo = sizeGizmo;
                    edgeHandles.push_back(angle);
                    break;
                }
                default: {
                    EdgeHandle size = ownValue(SizeField, tr("S"));
                    size.followsCamera = true;
                    edgeHandles.push_back(size);
                    break;
                }
            }
        }
    }

    if (!gizmoContainer) {
        return;
    }
    // Placed: each size's arrow in both faces, the first in the face the
    // flip names (upstream's two-gizmo chamfer did it so), the second size's
    // in the other
    Part::TopoShape baseShape = chamfer->getBaseShape(true);
    for (auto& handle : edgeHandles) {
        handle.placed = false;
        const Part::TopoShape::ChamferInfo info = edgeInfo(handle.edge);
        if (handle.box) {
            QSignalBlocker blocker(handle.box);
            handle.box->setValue(handle.field == AngleField ? info.angle
                                 : handle.field == Size2Field ? info.size2 : info.size);
        }
        try {
            Part::TopoShape edge = baseShape.getSubTopoShape(handle.edge.c_str(), true);
            if (edge.isNull()) {
                showHandle(handle.gizmo, false);
                continue;
            }
            auto [face1, face2] = getAdjacentFacesFromEdge(edge, baseShape);
            DraggerPlacementProps first = getDraggerPlacementFromEdgeAndFace(edge, face1);
            DraggerPlacementProps second = getDraggerPlacementFromEdgeAndFace(edge, face2);
            if (info.flip) {
                std::swap(first, second);
            }
            if (handle.field == Size2Field) {
                std::swap(first, second);
            }
            handle.placements[0] = {first.position, first.dir};
            handle.placements[1] = {second.position, second.dir};
        }
        catch (Standard_Failure&) {
            showHandle(handle.gizmo, false);
            continue;
        }
        catch (Base::Exception&) {
            showHandle(handle.gizmo, false);
            continue;
        }
        showHandle(handle.gizmo, true);
        handle.placed = true;
    }
    placeEdgeHandles(true);
}

void TaskChamferParameters::placeEdgeHandles(bool force)
{
    Gui::ViewerContext* view = onViewValues ? onViewValues->getView() : nullptr;
    const Base::Matrix4D toWorld = gizmoToWorld();
    Base::Vector3d dir;
    if (view) {
        // The view's direction where the arrows are
        Base::Matrix4D toGizmo = toWorld;
        toGizmo.inverse();
        const SbVec3f v = view->getViewDirection();
        dir = toGizmo * Base::Vector3d(v[0], v[1], v[2]) - toGizmo * Base::Vector3d();
        if (dir.Length() > Precision::Confusion()) {
            dir.Normalize();
        }
        else {
            dir = Base::Vector3d();
        }
    }
    bool moved = false;
    for (auto& handle : edgeHandles) {
        if (!handle.placed) {
            continue;
        }
        if (handle.field == AngleField) {
            // Below its size's arrow, turning in the plane of the two faces'
            // directions; its arrow is placed before it
            auto rotation = dynamic_cast<Gui::RotationGizmo*>(handle.gizmo);
            if (force && rotation && handle.sizeGizmo) {
                rotation->placeBelowLinearGizmo(handle.sizeGizmo);
                rotation->getDraggerContainer()->setArcNormalDirection(Base::convertTo<SbVec3f>(
                    -handle.placements[0].dir.Cross(handle.placements[1].dir)));
                if (onViewValues) {
                    onViewValues->setHandleAnchor(handle.gizmo,
                                                  toWorld * handle.placements[0].position);
                }
                moved = true;
            }
            continue;
        }
        int face = handle.face;
        if (handle.followsCamera && dir.Length() > 0.5) {
            // An arrow in the face turned toward the camera lies flat on the
            // screen; the other one points into it. A little either way of
            // even does not move it, so that it does not flicker.
            constexpr double hysteresis = 0.05;
            double along[2];
            for (int i = 0; i < 2; ++i) {
                Base::Vector3d d = handle.placements[i].dir;
                d.Normalize();
                along[i] = std::fabs(d * dir);
            }
            if (along[1 - face] + hysteresis < along[face]) {
                face = 1 - face;
            }
        }
        else if (!handle.followsCamera) {
            face = 0;
        }
        if (face == handle.face && !force) {
            continue;
        }
        handle.face = face;
        handle.gizmo->Gizmo::setDraggerPlacement(handle.placements[face].position,
                                                 handle.placements[face].dir);
        if (onViewValues) {
            onViewValues->setHandleAnchor(handle.gizmo, toWorld * handle.placements[face].position);
        }
        moved = true;
    }
    if (moved && gizmoContainer) {
        gizmoContainer->calculateScaleAndOrientation();
    }
}

TaskChamferParameters::EdgeHandle* TaskChamferParameters::findEdgeHandle(const Gui::Gizmo* gizmo)
{
    for (auto& handle : edgeHandles) {
        if (handle.gizmo == gizmo) {
            return &handle;
        }
    }
    return nullptr;
}

void TaskChamferParameters::fillEdgeMenu(QMenu& menu, Gui::Gizmo* gizmo)
{
    auto chamfer = getChamfer();
    EdgeHandle* handle = findEdgeHandle(gizmo);
    if (!chamfer || !handle) {
        return;
    }
    const std::string edge = handle->edge;
    Part::TopoShape::ChamferInfo own;
    const bool customised = chamfer->ChamferInfo.getValue(edge, own);
    const Mode mode = customised ? edgeMode(own) : DefaultMode;

    menu.addSection(QString::fromStdString(edge));
    auto modes = new QActionGroup(&menu);
    modes->setExclusive(true);
    const char* titles[] = {
        QT_TR_NOOP("Default size"),
        QT_TR_NOOP("Own size"),
        QT_TR_NOOP("Own two sizes"),
        QT_TR_NOOP("Own size and angle"),
    };
    for (int i = 0; i < 4; ++i) {
        QAction* action = menu.addAction(tr(titles[i]));
        action->setCheckable(true);
        action->setChecked(int(mode) == i);
        modes->addAction(action);
        // After the menu: the change takes this handle away
        connect(action, &QAction::triggered, this, [this, edge, i]() {
            QTimer::singleShot(0, this, [this, edge, i]() {
                setEdgeMode(edge, Mode(i));
            });
        });
    }

    // Which face takes the first size: an edge's own, or the chamfer's for
    // an edge that takes the defaults. One size both ways has none.
    menu.addSeparator();
    QAction* flip = menu.addAction(tr("Flip direction"));
    flip->setCheckable(true);
    flip->setChecked(edgeInfo(edge).flip);
    flip->setEnabled(customised ? mode != SizeMode : chamfer->ChamferType.getValue() != 0);
    connect(flip, &QAction::triggered, this, [this, edge](bool checked) {
        QTimer::singleShot(0, this, [this, edge, checked]() {
            setEdgeFlip(edge, checked);
        });
    });

    // A customised edge's group (docs/OnViewValueEditing.md 6.1): each item
    // leads with the group's id, then what its values are, in its colour
    const int group = chamfer->ChamferInfo.getGroup(edge);
    if (group > 0) {
        QMenu* groups = menu.addMenu(groupIcon(group), tr("Group"));
        auto groupActions = new QActionGroup(groups);
        groupActions->setExclusive(true);
        for (int id : chamfer->ChamferInfo.getGroups()) {
            const auto members = chamfer->ChamferInfo.getGroupMembers(id);
            Part::TopoShape::ChamferInfo info;
            chamfer->ChamferInfo.getValue(members.front(), info);
            QString text = QStringLiteral("%1   %2").arg(id).arg(groupSummary(info));
            if (members.size() > 1) {
                text += QStringLiteral("  ") + tr("(%1 edges)").arg(members.size());
            }
            QAction* action = groups->addAction(groupIcon(id), text);
            action->setCheckable(true);
            action->setChecked(id == group);
            groupActions->addAction(action);
            connect(action, &QAction::triggered, this, [this, edge, id]() {
                QTimer::singleShot(0, this, [this, edge, id]() {
                    setEdgeGroup(edge, id);
                });
            });
        }
        groups->addSeparator();
        QAction* add = groups->addAction(tr("New group"));
        // already alone in its group: a new one would only renumber it
        add->setEnabled(chamfer->ChamferInfo.getGroupMembers(group).size() > 1);
        connect(add, &QAction::triggered, this, [this, edge]() {
            QTimer::singleShot(0, this, [this, edge]() {
                setEdgeGroup(edge, 0);
            });
        });
    }
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Remove"));
    // every edge is chamfered then; the panel's check box says so
    remove->setEnabled(!chamfer->UseAllEdges.getValue());
    connect(remove, &QAction::triggered, this, [this, edge]() {
        QTimer::singleShot(0, this, [this, edge]() {
            removeEdge(edge);
        });
    });
}

void TaskChamferParameters::setEdgeMode(const std::string& edge, Mode mode)
{
    auto chamfer = getChamfer();
    if (!chamfer) {
        return;
    }
    Part::TopoShape::ChamferInfo info;
    const bool customised = chamfer->ChamferInfo.getValue(edge, info);
    if ((customised ? edgeMode(info) : DefaultMode) == mode) {
        return;
    }
    // From what the edge has now, own or default
    const Part::TopoShape::ChamferInfo current = edgeInfo(edge);
    setupTransaction();
    if (mode == DefaultMode) {
        chamfer->ChamferInfo.removeValue(edge);
    }
    else {
        info = current;
        switch (mode) {
            case SizeMode:
                info.size2 = 0.0;
                info.angle = 0.0;
                break;
            case TwoSizesMode:
                info.size2 = current.size2 > 0.0 ? current.size2 : current.size;
                info.angle = 0.0;
                break;
            default:
                info.size2 = 0.0;
                info.angle = current.angle > 0.0 ? current.angle : chamfer->Angle.getValue();
                break;
        }
        chamfer->ChamferInfo.setValue(edge, info);
    }
    refresh();
    recompute();
}

void TaskChamferParameters::setEdgeValue(const std::string& edge, Field field, double value)
{
    auto chamfer = getChamfer();
    Part::TopoShape::ChamferInfo info;
    if (!chamfer || !chamfer->ChamferInfo.getValue(edge, info)) {
        return;
    }
    double& target = field == AngleField ? info.angle
                     : field == Size2Field ? info.size2 : info.size;
    if (target == value) {
        return;
    }
    target = value;
    setupTransaction();
    chamfer->ChamferInfo.setValue(edge, info);
    refresh();
    recompute();
}

void TaskChamferParameters::setEdgeFlip(const std::string& edge, bool flip)
{
    auto chamfer = getChamfer();
    if (!chamfer || edgeInfo(edge).flip == flip) {
        return;
    }
    setupTransaction();
    Part::TopoShape::ChamferInfo info;
    if (chamfer->ChamferInfo.getValue(edge, info)) {
        // its own: not mirrored to its group
        info.flip = flip;
        chamfer->ChamferInfo.setValue(edge, info);
    }
    else {
        chamfer->FlipDirection.setValue(flip);
    }
    refresh();
    recompute();
}

QString TaskChamferParameters::groupSummary(const Part::TopoShape::ChamferInfo& info)
{
    auto length = [](double value) {
        return Gui::trimTrailingZeros(
            QString::fromStdString(Base::Quantity(value, Base::Unit::Length).getUserString()),
            QLocale());
    };
    switch (edgeMode(info)) {
        case TwoSizesMode:
            return tr("S %1 / %2").arg(length(info.size), length(info.size2));
        case SizeAngleMode:
            return tr("S %1, %2")
                .arg(length(info.size),
                     Gui::trimTrailingZeros(
                         QString::fromStdString(
                             Base::Quantity(info.angle, Base::Unit::Angle).getUserString()),
                         QLocale()));
        default:
            return tr("S %1").arg(length(info.size));
    }
}

void TaskChamferParameters::setEdgeGroup(const std::string& edge, int group)
{
    auto chamfer = getChamfer();
    if (!chamfer || chamfer->ChamferInfo.getGroup(edge) == 0) {
        return;
    }
    setupTransaction();
    // 0: a group of its own, by the lowest free id
    chamfer->ChamferInfo.setGroup(edge, group > 0 ? group : chamfer->ChamferInfo.newGroup());
    refresh();
    recompute();
}

void TaskChamferParameters::removeEdge(const std::string& edge)
{
    auto chamfer = getChamfer();
    if (!chamfer || chamfer->UseAllEdges.getValue()) {
        return;
    }
    QTreeWidgetItem* item = nullptr;
    for (int i = 0; i < ui->treeWidgetReferences->topLevelItemCount(); ++i) {
        auto top = ui->treeWidgetReferences->topLevelItem(i);
        if (getGeometryItemText(top).constData() == edge) {
            item = top;
            break;
        }
    }
    if (!item) {
        return;
    }
    // The panel's own removal, of this row alone
    setupTransaction();
    chamfer->ChamferInfo.removeValue(edge);
    ui->treeWidgetReferences->clearSelection();
    item->setSelected(true);
    onRefDeleted();
}

void TaskChamferParameters::finishedRecomputeFeature()
{
    TaskDressUpParameters::finishedRecomputeFeature();
    setGizmoPositions();
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
    auto DressUpView = getDressUpView();
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
    auto DressUpView = getDressUpView();
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
        else {
            // back to the defaults: no values of its own, no group
            for (int i = 1; i <= 3; ++i) {
                item->setData(i, Qt::UserRole, QVariant());
                item->setText(i, QString());
            }
            item->setData(GroupColumn, Qt::UserRole, 0);
            item->setText(GroupColumn, QString());
            item->setIcon(GroupColumn, QIcon());
        }
    }
    // The view's handles follow what the feature holds now -- after an undo
    // or a redo too, which put the boxes back without telling the handles
    if (gizmoContainer) {
        setGizmoPositions();
    }
    if (onViewValues) {
        onViewValues->refresh();
    }
}

void TaskChamferParameters::onCheckBoxUseAllEdgesToggled(bool checked)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
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
    if (column == GroupColumn) {
        setEdgeGroup(getGeometryItemText(item).constData(),
                     item->data(GroupColumn, Qt::UserRole).toInt());
        return;
    }
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
    auto DressUpView = getDressUpView();
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
    auto DressUpView = getDressUpView();
    if(!DressUpView)
        return;
    setupTransaction();
    auto items = ui->treeWidgetReferences->selectedItems();
    if (items.isEmpty()) {
        for (int i=0; i<ui->treeWidgetReferences->topLevelItemCount(); ++i)
            items.append(ui->treeWidgetReferences->topLevelItem(i));
    }
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    // The selected rows, or every row with none selected: it went over the
    // selected rows alone, and cleared every edge's values whatever it was
    const bool all = ui->treeWidgetReferences->selectedItems().isEmpty();
    for (auto item : items) {
        ui->treeWidgetReferences->closePersistentEditor(item, 4);
        for (int i=1; i<=4; ++i) {
            item->setData(i, Qt::UserRole, QVariant());
            item->setText(i, QString());
        }
        if (!all)
            pcChamfer->ChamferInfo.removeValue(getGeometryItemText(item).constData());
    }
    if (all)
        pcChamfer->ChamferInfo.setValue();
    refresh();
    recompute();
}

void TaskChamferParameters::setItem(QTreeWidgetItem *item, const Part::TopoShape::ChamferInfo &info)
{
    auto DressUpView = getDressUpView();
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
        item->setText(index, noText ? QString() : QString::fromStdString(q.getUserString()));
    };
    setupItem("Size", 1, Base::Quantity(info.size, Base::Unit::Length), false);
    setupItem("Size2", 2, Base::Quantity(info.size2, Base::Unit::Length), info.size2==0.0);
    setupItem("Angle", 3, Base::Quantity(info.angle, Base::Unit::Angle), info.angle==0.0);
    item->setData(4, Qt::UserRole, info.flip);

    const int group = pcChamfer->ChamferInfo.getGroup(getGeometryItemText(item).constData());
    item->setData(GroupColumn, Qt::UserRole, group);
    item->setText(GroupColumn, group > 0 ? QString::number(group) : QString());
    item->setIcon(GroupColumn, group > 0 ? groupIcon(group) : QIcon());

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
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    pcChamfer->ChamferType.setValue(index);
    ui->stackedWidget->setCurrentIndex(index);
    ui->flipDirection->setEnabled(index != 0); // Enable if type is not "Equal distance"
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onSizeChanged(double len)
{
    auto DressUpView = getDressUpView();
    if(!DressUpView)
        return;

    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Size.setValue(len);
    recompute();
}

void TaskChamferParameters::onSize2Changed(double len)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Size2.setValue(len);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onAngleChanged(double angle)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
    PartDesign::Chamfer* pcChamfer = static_cast<PartDesign::Chamfer*>(DressUpView->getObject());
    setupTransaction();
    pcChamfer->Angle.setValue(angle);
    pcChamfer->getDocument()->recomputeFeature(pcChamfer);
}

void TaskChamferParameters::onFlipDirection(bool flip)
{
    auto DressUpView = getDressUpView();
    if (!DressUpView)
        return;
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
    auto DressUpView = getDressUpView();
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
