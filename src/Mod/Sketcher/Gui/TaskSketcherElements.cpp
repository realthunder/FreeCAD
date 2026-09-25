/***************************************************************************
 *   Copyright (c) 2014 Abdullah Tahiri <abdullah.tahiri.yo@gmail.com>     *
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
# include <boost/core/ignore_unused.hpp>
# include <QContextMenuEvent>
# include <QMenu>
# include <QShortcut>
# include <QString>
# include <QApplication>
# include <QHelpEvent>
# include <QImage>
# include <QMouseEvent>
# include <QPainter>
# include <QStyledItemDelegate>
# include <QToolTip>
# include <QPixmap>
# include <QListWidget>
# include <QPointer>
# include <QWidgetAction>
# include <QTimer>
# include <boost/core/ignore_unused.hpp>
#endif

#include <Base/Tools.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/MappedElement.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/MenuManager.h>
#include <Gui/Notifications.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>

#include <Mod/Sketcher/App/ExternalGeometryFacade.h>
#include <Mod/Sketcher/App/GeometryFacade.h>
#include <Mod/Sketcher/App/SketchObject.h>


#include "TaskSketcherElements.h"
#include "Utils.h"
#include "ViewProviderSketch.h"
#include "ui_TaskSketcherElements.h"

using namespace Sketcher;
using namespace SketcherGui;
using namespace Gui::TaskView;

// Translation block for context menu: do not remove
#if 0
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Point Coincidence");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Point on Object");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Vertical Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Horizontal Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Parallel Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Perpendicular Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Tangent Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Equal Length");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Symmetric");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Block Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Lock Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Horizontal Distance");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Vertical Distance");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Length Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Radius Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Diameter Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Radiam Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Angle Constraint");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Toggle construction geometry");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Select Constraints");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Select Origin");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Select Horizontal Axis");
QT_TRANSLATE_NOOP("SketcherGui::ElementView", "Select Vertical Axis");
#endif

// The Mode filter's entries, in bit order: the element's kind, then "All
// types" and the geometry types (upstream's list and parameter).
static const char *filterLabels[] = {
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Normal"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Construction"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Internal"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "External"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "All types"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Point"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Line"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Circle"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Ellipse"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Arc of circle"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Arc of ellipse"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Arc of hyperbola"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "Arc of parabola"),
    QT_TRANSLATE_NOOP("SketcherGui::TaskSketcherElements", "B-spline"),
};
static constexpr int filterCount = int(sizeof(filterLabels) / sizeof(filterLabels[0]));
static constexpr int filterAllTypes = 4;

enum ColumnIndex {
    ColType,
    ColName,
    ColReference,
    ColFlags,
    ColMapped,
};

class MultIcon {
    
public:
    MultIcon &operator=(const char *);
    QIcon Normal;
    QIcon Construction;
    QIcon External;
    QIcon Internal;
};

// helper class to store additional information about the treeWidget entry.
class ElementItem : public QTreeWidgetItem
{
public:
    ElementItem(QTreeWidget *parent, Sketcher::SketchObject *sketch,
            int elementnr, Part::Geometry *geo)
        : QTreeWidgetItem(parent)
        , ElementNbr(elementnr)
        , isLineSelected(false)
        , isStartingPointSelected(false)
        , isEndPointSelected(false)
        , isMidPointSelected(false)
        , sketchObject(sketch)
    {
        // read by ViewProviderSketch::selectAll()
        setData(0, Qt::UserRole, elementnr);
        StartingVertex = sketch->getVertexIndexGeoPos(elementnr,Sketcher::PointPos::start),
        MidVertex = sketch->getVertexIndexGeoPos(elementnr,Sketcher::PointPos::mid),
        EndVertex = sketch->getVertexIndexGeoPos(elementnr,Sketcher::PointPos::end),
        GeometryType = geo->getTypeId();
        isConstruction = GeometryFacade::getConstruction(geo);
        isInternalAligned = GeometryFacade::isInternalAligned(geo);
        activePart = defaultPart();

        static std::map<Base::Type,QString> typeMap;
        if(typeMap.empty()) {
            typeMap[Part::GeomPoint::getClassTypeId()] = QObject::tr("Point");
            typeMap[Part::GeomLineSegment::getClassTypeId()] = QObject::tr("Line");
            typeMap[Part::GeomArcOfCircle::getClassTypeId()] = QObject::tr("Arc");
            typeMap[Part::GeomCircle::getClassTypeId()] = QObject::tr("Circle");
            typeMap[Part::GeomEllipse::getClassTypeId()] = QObject::tr("Ellipse");
            typeMap[Part::GeomArcOfEllipse::getClassTypeId()] = QObject::tr("Elliptical Arc");
            typeMap[Part::GeomArcOfHyperbola::getClassTypeId()] = QObject::tr("Hyperbolic Arc");
            typeMap[Part::GeomArcOfParabola::getClassTypeId()] = QObject::tr("Parabolic Arc");
            typeMap[Part::GeomBSplineCurve::getClassTypeId()] = QObject::tr("BSpline");
        }
        auto it = typeMap.find(GeometryType);
        if(it == typeMap.end())
            setText(ColumnIndex::ColType, QObject::tr("Other") +
                    QLatin1Char('-') + QString::fromUtf8(GeometryType.getName()));
        else
            setText(ColumnIndex::ColType, it->second);
        if(ElementNbr>=0) {
            // a group's handle is listed as the group, its members are not listed at all
            for (const auto *c : sketch->Constraints.getValues()) {
                if ((c->Type == Sketcher::Group || c->Type == Sketcher::Text)
                        && c->getGeoId(0) == ElementNbr) {
                    isTextHandle = (c->Type == Sketcher::Text);
                    setText(ColumnIndex::ColType, isTextHandle
                            ? QObject::tr("Text") : QObject::tr("Group"));
                    break;
                }
            }
            isGroupMember = sketch->isInGroup(ElementNbr, false);

            if(GeometryFacade::getConstruction(geo))
                setText(ColumnIndex::ColFlags,QObject::tr("Construction"));
            isMissing = false;
            isExternal = false;
        }else{
            auto egf = ExternalGeometryFacade::getFacade(geo);
            isMissing = egf->testFlag(ExternalGeometryExtension::Missing);
            isExternal = !egf->getRef().empty();

            QString text;
            if(egf->testFlag(ExternalGeometryExtension::Defining))
                text = QObject::tr("Defining");
            if(egf->testFlag(ExternalGeometryExtension::Frozen)) {
                if(text.size())
                    text += QLatin1Char('-');
                text += QObject::tr("Frozen");
            }
            setText(ColumnIndex::ColFlags,text);

            setText(ColumnIndex::ColReference, QString::fromUtf8(sketch->getGeometryReference(ElementNbr).c_str()));
        }
    }

    ~ElementItem()
    {
    }

    /// \a filterState is the Mode filter, one bit per entry of its list
    /// (filterLabels below): listed when both the element's kind and its
    /// geometry type are ticked. A type the list does not name is not
    /// filtered by type.
    void setVisibility(int filterState)
    {
        if (isGroupMember) {
            // the group's handle stands for its members
            this->setHidden(true);
            return;
        }
        int kindBit = ElementNbr < 0 ? 3 : isInternalAligned ? 2 : isConstruction ? 1 : 0;
        static const std::map<Base::Type, int> typeBits = {
            {Part::GeomPoint::getClassTypeId(), 5},
            {Part::GeomLineSegment::getClassTypeId(), 6},
            {Part::GeomCircle::getClassTypeId(), 7},
            {Part::GeomEllipse::getClassTypeId(), 8},
            {Part::GeomArcOfCircle::getClassTypeId(), 9},
            {Part::GeomArcOfEllipse::getClassTypeId(), 10},
            {Part::GeomArcOfHyperbola::getClassTypeId(), 11},
            {Part::GeomArcOfParabola::getClassTypeId(), 12},
            {Part::GeomBSplineCurve::getClassTypeId(), 13},
        };
        auto it = typeBits.find(GeometryType);
        bool shown = ((filterState >> kindBit) & 1)
            && (it == typeBits.end() || ((filterState >> it->second) & 1));
        this->setHidden(!shown);
    }

    /// the icon of one part of this element: 0 edge, 1 start, 2 end, 3 centre
    QIcon partIcon(int element) const {
        static std::map<std::pair<Base::Type,int>, MultIcon> iconMap;
        static QIcon none;
        if(iconMap.empty()) {
            none = QIcon(Gui::BitmapFactory().pixmap("Sketcher_Element_SelectionTypeInvalid"));

            iconMap[std::make_pair(Part::GeomPoint::getClassTypeId(),1)] =
                "Sketcher_Element_Point_StartingPoint";

            iconMap[std::make_pair(Part::GeomLineSegment::getClassTypeId(),0)] =
                "Sketcher_Element_Line_Edge";

            iconMap[std::make_pair(Part::GeomLineSegment::getClassTypeId(),1)] =
                "Sketcher_Element_Line_StartingPoint";

            iconMap[std::make_pair(Part::GeomLineSegment::getClassTypeId(),2)] =
                "Sketcher_Element_Line_EndPoint";

            iconMap[std::make_pair(Part::GeomArcOfCircle::getClassTypeId(),0)] =
                "Sketcher_Element_Arc_Edge";

            iconMap[std::make_pair(Part::GeomArcOfCircle::getClassTypeId(),1)] =
                "Sketcher_Element_Arc_StartingPoint";

            iconMap[std::make_pair(Part::GeomArcOfCircle::getClassTypeId(),2)] =
                "Sketcher_Element_Arc_EndPoint";

            iconMap[std::make_pair(Part::GeomArcOfCircle::getClassTypeId(),3)] =
                "Sketcher_Element_Arc_MidPoint";

            iconMap[std::make_pair(Part::GeomCircle::getClassTypeId(),0)] =
                "Sketcher_Element_Circle_Edge";

            iconMap[std::make_pair(Part::GeomCircle::getClassTypeId(),3)] =
                "Sketcher_Element_Circle_MidPoint";

            iconMap[std::make_pair(Part::GeomEllipse::getClassTypeId(),0)] =
                "Sketcher_Element_Ellipse_Edge_2";

            iconMap[std::make_pair(Part::GeomEllipse::getClassTypeId(),3)] =
                "Sketcher_Element_Ellipse_CentrePoint";

            iconMap[std::make_pair(Part::GeomArcOfEllipse::getClassTypeId(),0)] =
                "Sketcher_Element_Elliptical_Arc_Edge";

            iconMap[std::make_pair(Part::GeomArcOfEllipse::getClassTypeId(),1)] =
                "Sketcher_Element_Elliptical_Arc_Start_Point";

            iconMap[std::make_pair(Part::GeomArcOfEllipse::getClassTypeId(),2)] =
                "Sketcher_Element_Elliptical_Arc_End_Point";

            iconMap[std::make_pair(Part::GeomArcOfEllipse::getClassTypeId(),3)] =
                "Sketcher_Element_Elliptical_Arc_Centre_Point";

            iconMap[std::make_pair(Part::GeomArcOfHyperbola::getClassTypeId(),0)] =
                "Sketcher_Element_Hyperbolic_Arc_Edge";

            iconMap[std::make_pair(Part::GeomArcOfHyperbola::getClassTypeId(),1)] =
                "Sketcher_Element_Hyperbolic_Arc_Start_Point";

            iconMap[std::make_pair(Part::GeomArcOfHyperbola::getClassTypeId(),2)] =
                "Sketcher_Element_Hyperbolic_Arc_End_Point";

            iconMap[std::make_pair(Part::GeomArcOfHyperbola::getClassTypeId(),3)] =
                "Sketcher_Element_Hyperbolic_Arc_Centre_Point";

            iconMap[std::make_pair(Part::GeomArcOfParabola::getClassTypeId(),0)] =
                "Sketcher_Element_Parabolic_Arc_Edge";

            iconMap[std::make_pair(Part::GeomArcOfParabola::getClassTypeId(),1)] =
                "Sketcher_Element_Parabolic_Arc_Start_Point";

            iconMap[std::make_pair(Part::GeomArcOfParabola::getClassTypeId(),2)] =
                "Sketcher_Element_Parabolic_Arc_End_Point";

            iconMap[std::make_pair(Part::GeomArcOfParabola::getClassTypeId(),3)] =
                "Sketcher_Element_Parabolic_Arc_Centre_Point";

            iconMap[std::make_pair(Part::GeomBSplineCurve::getClassTypeId(),0)] =
                "Sketcher_Element_BSpline_Edge";

            iconMap[std::make_pair(Part::GeomBSplineCurve::getClassTypeId(),1)] =
                "Sketcher_Element_BSpline_StartPoint";

            iconMap[std::make_pair(Part::GeomBSplineCurve::getClassTypeId(),2)] =
                "Sketcher_Element_BSpline_EndPoint";
        }
        QIcon icon;
        if(isMissing)
            icon = none;
        else {
            auto it = iconMap.find(std::make_pair(GeometryType,element));
            if(it == iconMap.end()) {
                if(!element && GeometryType != Part::GeomPoint::getClassTypeId())
                    icon = none;
            }
            else if (isConstruction)
                icon = it->second.Construction;
            else if (isExternal)
                icon = it->second.External;
            else if (isInternalAligned)
                icon = it->second.Internal;
            else
                icon = it->second.Normal;
        }
        return icon;
    }

    void setElement(Sketcher::SketchObject *sketch, int element, int filterIndex) {
        QIcon icon = partIcon(element);
        setIcon(0,icon);
        setVisibility(filterIndex);

        Data::IndexedName name = sketch->shapeTypeFromGeoId(ElementNbr, (Sketcher::PointPos)(element));
        std::string tmp;
        setText(ColumnIndex::ColName, QString::fromUtf8(name.appendToStringBuffer(tmp)));
        std::string mapped = sketch->convertSubName(name,false);
        setText(ColumnIndex::ColMapped, QString::fromUtf8(mapped.c_str()));
    }

    int ElementNbr;
    int StartingVertex;
    int MidVertex;
    int EndVertex;
    bool isLineSelected;
    bool isStartingPointSelected;
    bool isEndPointSelected;
    bool isMidPointSelected;
    bool isMissing;
    Base::Type GeometryType;
    bool isConstruction;
    bool isExternal;
    bool isInternalAligned;

    /// The part the row's icon shows: 0 edge, 1 start, 2 end, 3 centre
    /// point (PointPos). The one selected last, or the default with none.
    int activePart = 0;

    /// a point is its start vertex; everything else its edge
    int defaultPart() const
    {
        return GeometryType == Part::GeomPoint::getClassTypeId() ? 1 : 0;
    }

    bool partSelected(int part) const
    {
        switch (part) {
        case 1: return isStartingPointSelected;
        case 2: return isEndPointSelected;
        case 3: return isMidPointSelected;
        default: return isLineSelected;
        }
    }

    /// what the icon shows when the part it showed is deselected
    int firstSelectedPart() const
    {
        if (GeometryType == Part::GeomPoint::getClassTypeId())
            return 1;
        for (int part : {0, 1, 2, 3}) {
            if (partSelected(part))
                return part;
        }
        return defaultPart();
    }

    void followSelection(int part, bool select)
    {
        if (select)
            activePart = GeometryType == Part::GeomPoint::getClassTypeId() ? 1 : part;
        else if (part == activePart || !partSelected(activePart))
            activePart = firstSelectedPart();
    }
    // a member of a group: the list shows the group's handle in its place
    bool isGroupMember = false;
    // the construction line a Text constraint hangs its geometry on
    bool isTextHandle = false;
    // the sketch this element belongs to; the list is rebuilt whenever the sketch changes
    Sketcher::SketchObject* sketchObject = nullptr;
};

// Column 0's icon is a button that drops down the element's parts. The
// decoration is a rectangle: a strip on the left carries the drop-down
// arrow, the square icon sits to its right (the tree's icon size is set
// that wide, see arrowStrip()).
class ElementIconDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /// the width of the arrow strip for an icon of \a size pixels
    static int arrowStrip(int size)
    {
        return std::max(8, size / 3);
    }

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // The base shrinks the decoration to the icon's own, square size;
        // keep the view's wider one, which holds the arrow strip.
        if (auto view = qobject_cast<const QAbstractItemView*>(option->widget)) {
            if (!option->icon.isNull())
                option->decorationSize = view->iconSize();
        }
        option->decorationAlignment = Qt::AlignRight | Qt::AlignVCenter;
    }

    QRect iconRect(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        if (opt.icon.isNull())
            return {};
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        return style->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, w);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        QRect icon = iconRect(option, index);
        if (icon.isEmpty())
            return;
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        int strip = icon.width() - icon.height();
        if (strip <= 0)
            return;
        QStyleOption arrow;
        arrow.rect = QRect(icon.left(), icon.top() + (icon.height() - strip) / 2, strip, strip);
        arrow.palette = option.palette;
        arrow.state = QStyle::State_Enabled;
        style->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrow, painter, w);
    }
};

ElementView::ElementView(QWidget *parent)
    : QTreeWidget(parent)
{
    setItemDelegateForColumn(0, new ElementIconDelegate(this));
}

QRect ElementView::iconRect(QTreeWidgetItem *item) const
{
    auto delegate = static_cast<ElementIconDelegate*>(itemDelegateForColumn(0));
    QModelIndex index = indexFromItem(item, 0);
    if (!delegate || !index.isValid())
        return {};
    QStyleOptionViewItem opt;
    initViewItemOption(&opt);
    opt.rect = visualRect(index);
    return delegate->iconRect(opt, index);
}

void ElementView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->position().toPoint();
        QTreeWidgetItem *item = itemAt(pos);
        QRect icon = item ? iconRect(item) : QRect();
        if (icon.contains(pos)) {
            // the icon is a button: it does not select the row
            Q_EMIT partButtonClicked(item, viewport()->mapToGlobal(icon.bottomLeft()));
            event->accept();
            return;
        }
    }
    inherited::mousePressEvent(event);
}

bool ElementView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto help = static_cast<QHelpEvent*>(event);
        QTreeWidgetItem *item = itemAt(help->pos());
        if (item && iconRect(item).contains(help->pos())) {
            QToolTip::showText(help->globalPos(),
                tr("Click to choose which part of this element to select: its edge, "
                   "or its start, end or centre point. Ctrl adds to the selection.\n"
                   "The icon shows the part selected last."), viewport());
            return true;
        }
    }
    return inherited::viewportEvent(event);
}

ElementView::~ElementView()
{}

void ElementView::contextMenuEvent (QContextMenuEvent* event)
{
    QMenu menu;
    auto items = selectedItems();
    Gui::MenuItem mitems;
    mitems << "Sketcher_ConstrainCoincident"
           << "Sketcher_ConstrainPointOnObject"
           << "Sketcher_ConstrainVertical"
           << "Sketcher_ConstrainHorizontal"
           << "Sketcher_ConstrainParallel"
           << "Sketcher_ConstrainPerpendicular"
           << "Sketcher_ConstrainTangent"
           << "Sketcher_ConstrainEqual"
           << "Sketcher_ConstrainSymmetric"
           << "Sketcher_ConstrainLock"
           << "Sketcher_ConstrainBlock"
           << "Sketcher_ConstrainGroup"
           << "Sketcher_ConstrainDistanceX"
           << "Sketcher_ConstrainDistanceY"
           << "Sketcher_ConstrainDistance"
           << "Sketcher_ConstrainRadius"
           << "Sketcher_ConstrainDiameter"
           << "Sketcher_ConstrainRadiam"
           << "Sketcher_ConstrainAngle"

           << "Separator"

           << "Sketcher_ToggleConstruction"
           << "Sketcher_ExternalCmds"

           << "Separator"

           << "Sketcher_SelectConstraints"
           << "Sketcher_SelectOrigin"
           << "Sketcher_SelectHorizontalAxis"
           << "Sketcher_SelectVerticalAxis"

           << "Separator";

    Gui::MenuManager::getInstance()->setupContextMenu(&mitems, menu);

    // a text can be turned into ordinary geometry by dropping its handle
    if (items.size() == 1 && static_cast<ElementItem*>(items.first())->isTextHandle) {
        menu.addAction(tr("Convert to geometry"), this, &ElementView::convertTextToGeometry);
        menu.addSeparator();
    }

    QAction* remove = menu.addAction(tr("Delete"), this, &ElementView::deleteSelectedItems);
    remove->setShortcut(QKeySequence(QKeySequence::Delete));
    remove->setEnabled(!items.isEmpty());

    menu.menuAction()->setIconVisibleInMenu(true);

    menu.exec(event->globalPos());
}

void ElementView::convertTextToGeometry()
{
    auto items = selectedItems();
    if (items.isEmpty()) {
        return;
    }

    auto* item = static_cast<ElementItem*>(items.first());
    if (!item->isTextHandle) {
        return;
    }

    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc) {
        return;
    }

    // Deleting the handle takes the Text constraint with it, because the constraint refers
    // to the handle. The glyph geometry stays, as ordinary geometry.
    Gui::Selection().clearSelection();
    doc->openTransaction("Convert text to geometry");
    Gui::Command::doCommand(Gui::Command::Doc,
                            "App.getDocument('%s').getObject('%s').delGeometry(%d)",
                            item->sketchObject->getDocument()->getName(),
                            item->sketchObject->getNameInDocument(),
                            item->ElementNbr);
    doc->commitTransaction();
}

void ElementView::deleteSelectedItems()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc)
        return;

    doc->openTransaction("Delete element");
    std::vector<Gui::SelectionObject> sel = Gui::Selection().getSelectionEx(doc->getName());
    for (std::vector<Gui::SelectionObject>::iterator ft = sel.begin(); ft != sel.end(); ++ft) {
        Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(ft->getObject());
        if (vp) {
            vp->onDelete(ft->getSubNames());
        }
    }
    doc->commitTransaction();
}


// ----------------------------------------------------------------------------

/* TRANSLATOR SketcherGui::TaskSketcherElements */

TaskSketcherElements::TaskSketcherElements(ViewProviderSketch* sketchView)
    : TaskBox(Gui::BitmapFactory().pixmap("document-new"), tr("Elements"), true, nullptr)
    , sketchView(sketchView)
    , ui(new Ui_TaskSketcherElements())
    , focusItemIndex(-1)
    , previouslySelectedItemIndex(-1)
    , inhibitSelectionUpdate(false)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
#ifdef Q_OS_MAC
    QString cmdKey = QString::fromUtf8("\xe2\x8c\x98");// U+2318
#else
    // translate the text (it's offered by Qt's translation files)
    // but avoid being picked up by lupdate
    const char* ctrlKey = "Ctrl";
    QString cmdKey = QShortcut::tr(ctrlKey);
#endif
    ui->Explanation->setText(tr("<html><head/><body><p>&quot;%1&quot;: multiple selection</p>"
                                "<p>Click an element's icon to select one of its points</p>"
                                "</body></html>")
                             .arg(cmdKey));
    ui->elementsWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->elementsWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->elementsWidget->setMouseTracking(true);
    {
        // the icon is a button: its size is a preference (Display page)
        int size = App::GetApplication()
                       .GetParameterGroupByPath(
                           "User parameter:BaseApp/Preferences/Mod/Sketcher/Elements")
                       ->GetInt("ElementIconSize", 32);
        size = std::clamp(size, 16, 128);
        ui->elementsWidget->setIconSize(
            QSize(size + ElementIconDelegate::arrowStrip(size), size));
    }
    ui->elementsWidget->setColumnCount(5);
    ui->elementsWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->elementsWidget->header()->setStretchLastSection(false);
    ui->elementsWidget->headerItem()->setText(ColumnIndex::ColType, tr("Type"));
    ui->elementsWidget->headerItem()->setText(ColumnIndex::ColName, tr("Name"));
    ui->elementsWidget->headerItem()->setText(ColumnIndex::ColReference, tr("Reference"));
    ui->elementsWidget->headerItem()->setText(ColumnIndex::ColFlags, tr("Flags"));
    ui->elementsWidget->headerItem()->setText(ColumnIndex::ColMapped, tr("Mapped"));

    // connecting the needed signals
    QObject::connect(
        ui->elementsWidget, SIGNAL(itemSelectionChanged()),
        this                     , SLOT  (on_elementsWidget_itemSelectionChanged())
       );
    QObject::connect(
        ui->elementsWidget, SIGNAL(itemChanged(QTreeWidgetItem *, int)),
        this                     , SLOT  (on_elementsWidget_itemChanged(QTreeWidgetItem *, int))
       );
    QObject::connect(
        ui->elementsWidget, SIGNAL(itemEntered(QTreeWidgetItem *, int)),
        this                     , SLOT  (on_elementsWidget_itemEntered(QTreeWidgetItem *))
       );
    QObject::connect(
        ui->elementsWidget, &ElementView::partButtonClicked,
        this, &TaskSketcherElements::onPartButtonClicked);

    connectionElementsChanged = sketchView->getSketchObject()->signalElementsChanged.connect(
        std::bind(&SketcherGui::TaskSketcherElements::slotElementsChanged, this));

    // The list shows a group by its handle and hides its members, so it has to follow
    // constraint changes as well -- but only the ones that change a group.
    connectionConstraintsChanged = sketchView->signalConstraintsChanged.connect(
        std::bind(&SketcherGui::TaskSketcherElements::slotConstraintsChanged, this));

    this->groupLayout()->addWidget(proxy);


    // The Mode filter: a checkable list in the button's pop-up, which stays
    // open while entries are ticked. Its state is upstream's parameter.
    {
        ParameterGrp::handle hGeneral = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher/General");
        int state = hGeneral->GetInt("ElementFilterState", std::numeric_limits<int>::max());
        filterList = new QListWidget();
        for (int i = 0; i < filterCount; ++i) {
            auto item = new QListWidgetItem(
                QCoreApplication::translate("SketcherGui::TaskSketcherElements",
                                            filterLabels[i]),
                filterList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(((state >> i) & 1) ? Qt::Checked : Qt::Unchecked);
        }
        filterList->setFixedHeight(filterList->sizeHintForRow(0) * filterCount
                                   + 2 * filterList->frameWidth());
        auto action = new QWidgetAction(this);
        action->setDefaultWidget(filterList);
        auto menu = new QMenu(ui->filterButton);
        menu->addAction(action);
        ui->filterButton->setMenu(menu);
        QObject::connect(filterList, &QListWidget::itemChanged,
                         this, &TaskSketcherElements::onFilterItemChanged);
        // "All types" follows the types under it
        onFilterItemChanged(filterList->item(filterAllTypes + 1));
    }

    slotElementsChanged();
}

TaskSketcherElements::~TaskSketcherElements()
{
    connectionElementsChanged.disconnect();
    connectionConstraintsChanged.disconnect();
}

void TaskSketcherElements::sketchClosed()
{
    connectionElementsChanged.disconnect();
    connectionConstraintsChanged.disconnect();
    // Nor does it follow the selection any more: its view provider may be
    // gone by the next message (a document closed before this panel is
    // deleted clears the selection).
    detachSelection();
    QSignalBlocker blocker(ui->elementsWidget);
    ui->elementsWidget->clear();
    // clear() deleted the items this maps to, and the panel goes on
    // observing the selection until it is destroyed.
    itemMap.clear();
}
static void setPosSelected(ElementItem *ite, Sketcher::PointPos PosId, bool select)
{
    switch(PosId) {
    case Sketcher::PointPos::start:
        ite->isStartingPointSelected=select;
        break;
    case Sketcher::PointPos::end:
        ite->isEndPointSelected=select;
        break;
    case Sketcher::PointPos::mid:
        ite->isMidPointSelected=select;
        break;
    default:
        ite->isLineSelected=select;
        break;
    }
    int part = PosId == Sketcher::PointPos::none ? 0 : int(PosId);
    ite->followSelection(part, select);
}

static void showSelected(ElementItem *ite, int element)
{
    switch(element){
    case 0:
        ite->setSelected(ite->isLineSelected);
        break;
    case 1:
        ite->setSelected(ite->isStartingPointSelected);
        break;
    case 2:
        ite->setSelected(ite->isEndPointSelected);
        break;
    case 3:
        ite->setSelected(ite->isMidPointSelected);
        break;
    }
}

void TaskSketcherElements::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    std::string temp;
    if (msg.Type == Gui::SelectionChanges::ClrSelection) {
        clearWidget();
    }
    else if (msg.Type == Gui::SelectionChanges::AddSelection
             || msg.Type == Gui::SelectionChanges::RmvSelection) {
        bool select = (msg.Type == Gui::SelectionChanges::AddSelection);
        App::DocumentObject *selObj = msg.Object.getObject();
        // is it this object??
        if (selObj && selObj->getLinkedObject() == sketchView->getObject()) {
            if (!msg.pSubName)
                return;
            int GeoId;
            Sketcher::PointPos PosId;
            if(!sketchView->getSketchObject()->geoIdFromShapeType(msg.pSubName,GeoId,PosId))
                return;

            auto it = itemMap.find(GeoId);
            if(it == itemMap.end())
                return;

            ElementItem* ite = static_cast<ElementItem*>(it->second);
            setPosSelected(ite, PosId, select);
            ite->setElement(sketchView->getSketchObject(), ite->activePart, filterState());

            // update the listwidget
            ui->elementsWidget->blockSignals(true);
            showSelected(ite, 0);
            if(select)
                ui->elementsWidget->scrollToItem(ite);
            ui->elementsWidget->blockSignals(false);
        }
    }
    else if (msg.Type == Gui::SelectionChanges::SetSelection) {
        // What a paused batch (Selection().addSelections()) turns into once it
        // holds more than MaxSelectionNotification changes: the item by item
        // messages are dropped and this says "re-read the selection".
        if (itemMap.empty())
            return;
        for (auto &v : itemMap) {
            auto ite = static_cast<ElementItem*>(v.second);
            ite->isLineSelected = false;
            ite->isStartingPointSelected = false;
            ite->isEndPointSelected = false;
            ite->isMidPointSelected = false;
        }
        auto sketch = sketchView->getSketchObject();
        for (const auto &sel : observedSelection().getSelectionEx(
                 "*", App::DocumentObject::getClassTypeId(),
                 Gui::ResolveMode::OldStyleElement)) {
            const App::DocumentObject *obj = sel.getObject();
            if (!obj || obj->getLinkedObject() != sketchView->getObject())
                continue;
            for (const auto &sub : sel.getSubNames()) {
                int GeoId;
                Sketcher::PointPos PosId;
                if (!sketch->geoIdFromShapeType(sub.c_str(), GeoId, PosId))
                    continue;
                auto it = itemMap.find(GeoId);
                if (it != itemMap.end())
                    setPosSelected(static_cast<ElementItem*>(it->second), PosId, true);
            }
        }
        for (auto &v : itemMap) {
            auto ite = static_cast<ElementItem*>(v.second);
            ite->activePart = ite->firstSelectedPart();
        }
        QSignalBlocker blocker(ui->elementsWidget);
        for (auto &v : itemMap)
            showSelected(static_cast<ElementItem*>(v.second), 0);
        updateIcons();
    }
}


void TaskSketcherElements::on_elementsWidget_itemSelectionChanged(void)
{
    ui->elementsWidget->blockSignals(true);


    // selection changed because we acted on the current entered item
    // we can not do this with ItemPressed because that signal is triggered after this one.
    // A row stands for its element's edge; a point's row for its vertex. The
    // other parts are picked from the row's icon (onPartButtonClicked).
    const int element = 0;

    ElementItem * itf;

    if(focusItemIndex>-1 && focusItemIndex<ui->elementsWidget->topLevelItemCount())
      itf=static_cast<ElementItem*>(ui->elementsWidget->topLevelItem(focusItemIndex));
    else
      itf=nullptr;

    bool multipleselection=true; // ctrl type of selection in listWidget
    bool multipleconsecutiveselection=false; // shift type of selection in listWidget

    if (!inhibitSelectionUpdate) {
        if(itf) {
            switch(element){
            case 0:
                itf->isLineSelected=!itf->isLineSelected;
                itf->followSelection(0, itf->isLineSelected);
                break;
            case 1:
                itf->isStartingPointSelected=!itf->isStartingPointSelected;
                break;
            case 2:
                itf->isEndPointSelected=!itf->isEndPointSelected;
                break;
            case 3:
                itf->isMidPointSelected=!itf->isMidPointSelected;
                break;
            }
        }

        if (QApplication::keyboardModifiers()==Qt::ControlModifier)// multiple ctrl selection?
            multipleselection=true;
        else
            multipleselection=false;

        if (QApplication::keyboardModifiers()==Qt::ShiftModifier)// multiple shift selection?
            multipleconsecutiveselection=true;
        else
            multipleconsecutiveselection=false;

        if (multipleselection && multipleconsecutiveselection) { // ctrl takes priority over shift functionality
            multipleselection=true;
            multipleconsecutiveselection=false;
        }
    }

    for (int i=0;i<ui->elementsWidget->topLevelItemCount(); i++) {
        ElementItem * ite=static_cast<ElementItem*>(ui->elementsWidget->topLevelItem(i));

        if(multipleselection==false && multipleconsecutiveselection==false && ite!=itf) {
            ite->isLineSelected=false;
            ite->isStartingPointSelected=false;
            ite->isEndPointSelected=false;
            ite->isMidPointSelected=false;
        }

        if( multipleconsecutiveselection) {
            if ((( i>focusItemIndex && i<previouslySelectedItemIndex ) ||
                 ( i<focusItemIndex && i>previouslySelectedItemIndex )) &&
                previouslySelectedItemIndex>=0){
              // select the element of the Item
                      switch(element){
                  case 0:
                      ite->isLineSelected=true;
                      break;
                  case 1:
                      ite->isStartingPointSelected=true;
                      break;
                  case 2:
                      ite->isEndPointSelected=true;
                      break;
                  case 3:
                      ite->isMidPointSelected=true;
                      break;
                }
            }
        }

    }

    syncSceneSelection();
    ui->elementsWidget->blockSignals(false);

    if (focusItemIndex>-1 && focusItemIndex<ui->elementsWidget->topLevelItemCount())
        previouslySelectedItemIndex=focusItemIndex;
}

void TaskSketcherElements::on_elementsWidget_itemEntered(QTreeWidgetItem *item)
{
    ElementItem *it = dynamic_cast<ElementItem*>(item);
    if (!it) return;

    Gui::Selection().rmvPreselect();

    ui->elementsWidget->setFocus();

    int tempitemindex=ui->elementsWidget->indexOfTopLevelItem(item);

    std::string doc_name = sketchView->getSketchObject()->getDocument()->getName();
    std::string obj_name = sketchView->getSketchObject()->getNameInDocument();

    /* 0 - Lines
     * 1 - Starting Points
     * 2 - End Points
     * 3 - Middle Points
     */
    std::stringstream ss;


    const int element = 0;

    focusItemIndex=tempitemindex;

    int vertex;

    switch(element)
    {
    case 0:
        if (it->GeometryType == Part::GeomPoint::getClassTypeId()) {
            vertex= it->StartingVertex;
            if (vertex!=-1) {
                ss << "Vertex" << vertex + 1;
                sketchView->selectElement(ss.str().c_str(), true);
            }
        } 
        else {
            if(it->ElementNbr>=0)
                ss << "Edge" << it->ElementNbr + 1;
            else
                ss << "ExternalEdge" << -it->ElementNbr - 2;
            sketchView->selectElement(ss.str().c_str(), true);
        }
        break;
    case 1:
    case 2:
    case 3:
        vertex= sketchView->getSketchObject()->getVertexIndexGeoPos(it->ElementNbr,static_cast<Sketcher::PointPos>(element));
        if (vertex!=-1) {
            ss << "Vertex" << vertex + 1;
            sketchView->selectElement(ss.str().c_str(), true);
        }
        break;
    }
}

void TaskSketcherElements::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    Gui::Selection().rmvPreselect();
    ui->elementsWidget->clearFocus();
}

std::map<int, int> TaskSketcherElements::collectGroupRoles() const
{
    std::map<int, int> roles;
    for (const auto *c : sketchView->getSketchObject()->Constraints.getValues()) {
        if (c->Type != Sketcher::Group && c->Type != Sketcher::Text) {
            continue;
        }
        for (int i = 0; c->hasElement(i); ++i) {
            // the handle carries the constraint's type, a member carries None
            roles[c->getGeoId(i)] = (i == 0) ? static_cast<int>(c->Type)
                                             : static_cast<int>(Sketcher::None);
        }
    }
    return roles;
}

void TaskSketcherElements::slotConstraintsChanged()
{
    if (collectGroupRoles() != groupRoles) {
        slotElementsChanged();
    }
}

void TaskSketcherElements::slotElementsChanged()
{
    assert(sketchView);
    groupRoles = collectGroupRoles();
    // Build up ListView with the elements
    Sketcher::SketchObject* sketch = sketchView->getSketchObject();
    const std::vector<Part::Geometry*>& vals = sketch->Geometry.getValues();

    int currentRow = -1;
    auto currentIndex = ui->elementsWidget->currentIndex();
    if (currentIndex.isValid() && ui->elementsWidget->currentItem()->isSelected())
        currentRow = currentIndex.row();

    ui->elementsWidget->blockSignals(true);
    ui->elementsWidget->clear();
    itemMap.clear();

    int filterindex = filterState();

    for(int i=0;i<(int)vals.size();++i) {
        auto item = new ElementItem(ui->elementsWidget,sketch, i, vals[i]);
        item->setElement(sketch,item->activePart, filterindex);
        // The visual layer: ticked is shown, unticked is the hidden layer.
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, sketchView->isGeometryHidden(i) ? Qt::Unchecked : Qt::Checked);
        itemMap[item->ElementNbr] = item;
    }

    const std::vector< Part::Geometry * > &ext_vals = sketchView->getSketchObject()->getExternalGeometry();
    for(int i=2;i<(int)ext_vals.size();++i) {
        auto item = new ElementItem(ui->elementsWidget,sketch, -i-1, ext_vals[i]);
        item->setElement(sketch,item->activePart, filterindex);
        // external geometry has no layer: always shown, not toggleable
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        itemMap[item->ElementNbr] = item;
    }

    for(int i = 0; i < ui->elementsWidget->columnCount(); i++)
        ui->elementsWidget->resizeColumnToContents(i);

    ui->elementsWidget->blockSignals(false);

    if (currentRow >= ui->elementsWidget->topLevelItemCount())
        currentRow = ui->elementsWidget->topLevelItemCount()-1;
    if (currentRow >= 0) {
        if (auto item = ui->elementsWidget->topLevelItem(currentRow)) {
            ui->elementsWidget->setCurrentItem(item);
        }
    }
}


void TaskSketcherElements::onFilterItemChanged(QListWidgetItem *item)
{
    {
        QSignalBlocker blocker(filterList);
        int row = filterList->row(item);
        if (row == filterAllTypes) {
            Qt::CheckState state = item->checkState() == Qt::Unchecked
                ? Qt::Unchecked : Qt::Checked;
            item->setCheckState(state);
            for (int i = filterAllTypes + 1; i < filterCount; ++i)
                filterList->item(i)->setCheckState(state);
        }
        else if (row > filterAllTypes) {
            int checked = 0;
            for (int i = filterAllTypes + 1; i < filterCount; ++i)
                checked += filterList->item(i)->checkState() == Qt::Checked;
            filterList->item(filterAllTypes)->setCheckState(
                checked == 0 ? Qt::Unchecked
                : checked == filterCount - filterAllTypes - 1 ? Qt::Checked
                : Qt::PartiallyChecked);
        }
    }
    App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/General")
        ->SetInt("ElementFilterState", filterState());
    updateVisibility(filterState());
    updateFilterButton();
}

int TaskSketcherElements::filterState() const
{
    int state = 0;
    for (int i = 0; i < filterCount; ++i) {
        if (filterList->item(i)->checkState() == Qt::Checked)
            state |= 1 << i;
    }
    return state;
}

void TaskSketcherElements::updateFilterButton()
{
    bool all = true;
    for (int i = 0; i < filterCount; ++i)
        all = all && filterList->item(i)->checkState() == Qt::Checked;
    ui->filterButton->setText(all ? tr("All") : tr("Filtered"));
}

void TaskSketcherElements::updatePreselection()
{
    inhibitSelectionUpdate=true;
    on_elementsWidget_itemSelectionChanged();
    inhibitSelectionUpdate=false;
}

void TaskSketcherElements::on_elementsWidget_itemChanged(QTreeWidgetItem *item, int column)
{
    auto ite = dynamic_cast<ElementItem*>(item);
    if (column != 0 || !ite || ite->ElementNbr < 0
            || !(item->flags() & Qt::ItemIsUserCheckable))
        return;
    bool shown = item->checkState(0) == Qt::Checked;
    if (shown != sketchView->isGeometryHidden(ite->ElementNbr))
        return;  // already on that layer
    // Changing the geometry rebuilds this list, deleting the item whose
    // signal this is; do it once the signal has returned.
    int geoId = ite->ElementNbr;
    QPointer<TaskSketcherElements> self(this);
    QTimer::singleShot(0, this, [self, geoId, shown]() {
        if (self)
            self->setGeometryLayer(geoId, shown ? 0 : 2);
    });
}

void TaskSketcherElements::setGeometryLayer(int geoId, int layer)
{
    auto sketch = sketchView->getSketchObject();
    const std::vector<Part::Geometry*> &geometry = sketch->Geometry.getValues();
    if (geoId < 0 || geoId >= (int)geometry.size()
            || int(getSafeGeomLayerId(geometry[geoId])) == layer)
        return;

    App::Document *doc = sketch->getDocument();
    doc->openTransaction("Geometry layer change");
    std::unique_ptr<Part::Geometry> geo(geometry[geoId]->clone());
    setSafeGeomLayerId(geo.get(), layer);
    sketch->Geometry.set1Value(geoId, std::move(geo));
    sketch->solve();
    doc->commitTransaction();

    if (layer == 2) {
        // what is hidden cannot stay selected: it is not drawn
        const std::string docName = doc->getName();
        const std::string objName = sketch->getNameInDocument();
        auto deselect = [&](const std::string &name) {
            Gui::Selection().rmvSelection(docName.c_str(), objName.c_str(),
                                          sketch->convertSubName(name).c_str());
        };
        deselect("Edge" + std::to_string(geoId + 1));
        for (auto pos : {Sketcher::PointPos::start, Sketcher::PointPos::end,
                         Sketcher::PointPos::mid}) {
            int vertex = sketch->getVertexIndexGeoPos(geoId, pos);
            if (vertex >= 0)
                deselect("Vertex" + std::to_string(vertex + 1));
        }
    }
}

void TaskSketcherElements::clearWidget()
{
    QSignalBlocker sigblk(ui->elementsWidget);
    ui->elementsWidget->clearSelection ();

    // update widget
    int countItems = ui->elementsWidget->topLevelItemCount();
    for (int i=0; i < countItems; i++) {
      ElementItem* item = static_cast<ElementItem*> (ui->elementsWidget->topLevelItem(i));
      item->isLineSelected=false;
      item->isStartingPointSelected=false;
      item->isEndPointSelected=false;
      item->isMidPointSelected=false;
      item->activePart = item->defaultPart();
    }
    updateIcons();
}

void TaskSketcherElements::setItemVisibility(int elementindex,int filterState)
{
    ElementItem* item = static_cast<ElementItem*> (ui->elementsWidget->topLevelItem(elementindex));
    item->setVisibility(filterState);
}

void TaskSketcherElements::updateVisibility(int filterState)
{
    for (int i=0;i<ui->elementsWidget->topLevelItemCount(); i++) {
        setItemVisibility(i,filterState);
    }
}

void TaskSketcherElements::updateIcons()
{
    int filterindex = filterState();
    auto sketch = sketchView->getSketchObject();
    for (int i=0;i<ui->elementsWidget->topLevelItemCount(); i++) {
        auto ite = static_cast<ElementItem *>(ui->elementsWidget->topLevelItem(i));
        ite->setElement(sketch, ite->activePart, filterindex);
    }
}

void TaskSketcherElements::syncSceneSelection()
{
    bool block = this->blockSelection(true); // avoid to be notified by itself
    Gui::Selection().clearSelection();
    auto sketch = sketchView->getSketchObject();
    for (int i=0;i<ui->elementsWidget->topLevelItemCount(); i++) {
        ElementItem * ite=static_cast<ElementItem*>(ui->elementsWidget->topLevelItem(i));
        // a row is highlighted for its element's edge (a point: its vertex)
        showSelected(ite, 0);
        if (!ite->partSelected(ite->activePart))
            ite->activePart = ite->firstSelectedPart();
        ite->setElement(sketch, ite->activePart, filterState());

        // Every selected part goes to the scene, not only the first: the
        // icon's menu can select several parts of one element.
        auto selectVertex = [this](int vertex) {
            if (vertex != -1)
                sketchView->selectElement(("Vertex" + std::to_string(vertex + 1)).c_str());
        };
        if (ite->isLineSelected) {
            if (ite->GeometryType == Part::GeomPoint::getClassTypeId())
                selectVertex(ite->StartingVertex);
            else if (ite->ElementNbr >= 0)
                sketchView->selectElement(("Edge" + std::to_string(ite->ElementNbr + 1)).c_str());
            else
                sketchView->selectElement(
                    ("ExternalEdge" + std::to_string(-ite->ElementNbr - 2)).c_str());
        }
        if (ite->isStartingPointSelected
                && !(ite->isLineSelected && ite->GeometryType == Part::GeomPoint::getClassTypeId()))
            selectVertex(ite->StartingVertex);
        if (ite->isEndPointSelected)
            selectVertex(ite->EndVertex);
        if (ite->isMidPointSelected)
            selectVertex(ite->MidVertex);
    }
    this->blockSelection(block);
}

void TaskSketcherElements::onPartButtonClicked(QTreeWidgetItem *item, const QPoint &globalPos)
{
    auto ite = dynamic_cast<ElementItem*>(item);
    if (!ite)
        return;
    auto sketch = sketchView->getSketchObject();
    bool isPoint = ite->GeometryType == Part::GeomPoint::getClassTypeId();

    // The parts this element has, each with its own icon; ticked if selected.
    QMenu menu;
    struct PartEntry { int part; const char *text; };
    static const PartEntry parts[] = {
        {0, QT_TR_NOOP("Edge")},
        {1, QT_TR_NOOP("Start point")},
        {2, QT_TR_NOOP("End point")},
        {3, QT_TR_NOOP("Centre point")},
    };
    for (const auto &p : parts) {
        if (isPoint ? p.part != 1
                    : (p.part != 0
                       && sketch->getVertexIndexGeoPos(ite->ElementNbr,
                                                       static_cast<Sketcher::PointPos>(p.part)) < 0))
            continue;
        QAction *action = menu.addAction(ite->partIcon(p.part),
                                         isPoint ? tr("Point") : tr(p.text));
        action->setCheckable(true);
        action->setChecked(isPoint ? (ite->isLineSelected || ite->isStartingPointSelected)
                                   : ite->partSelected(p.part));
        action->setData(p.part);
    }
    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    int part = chosen->data().toInt();
    bool add = QApplication::keyboardModifiers() & Qt::ControlModifier;
    // With Ctrl the pick toggles that part (the menu has already toggled
    // the tick); without, it becomes the whole selection.
    bool select = add ? chosen->isChecked() : true;
    if (!add) {
        for (int i=0;i<ui->elementsWidget->topLevelItemCount(); i++) {
            auto other = static_cast<ElementItem*>(ui->elementsWidget->topLevelItem(i));
            other->isLineSelected = false;
            other->isStartingPointSelected = false;
            other->isEndPointSelected = false;
            other->isMidPointSelected = false;
            other->activePart = other->defaultPart();
        }
    }
    if (isPoint) {
        ite->isLineSelected = false;
        setPosSelected(ite, Sketcher::PointPos::start, select);
    }
    else
        setPosSelected(ite, part == 0 ? Sketcher::PointPos::none
                                      : static_cast<Sketcher::PointPos>(part), select);

    QSignalBlocker blocker(ui->elementsWidget);
    syncSceneSelection();
}

void TaskSketcherElements::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
    }
}

MultIcon & MultIcon::operator=(const char* name)
{
    int hue, sat, val, alp;
    Normal = Gui::BitmapFactory().iconFromTheme(name);
    QImage imgConstr(Normal.pixmap(qAsConst(Normal).availableSizes()[0]).toImage());
    QImage imgExt(imgConstr);
    QImage imgInt(imgConstr);

    //Create construction/external/internal icons by changing colors.
    for(int ix=0 ; ix<imgConstr.width() ; ix++) {
        for(int iy=0 ; iy<imgConstr.height() ; iy++) {
            QColor clr(imgConstr.pixelColor(ix,iy));
            clr.getHsv(&hue, &sat, &val, &alp);
            if (alp > 127 && hue >= 0) {
                if (sat > 127 && (hue > 330 || hue < 30)) { //change the color of red points.
                    clr.setHsv((hue + 240) % 360, sat, val, alp);
                    imgConstr.setPixelColor(ix, iy, clr);
                    clr.setHsv((hue + 300) % 360, sat, val, alp);
                    imgExt.setPixelColor(ix, iy, clr);
                    clr.setHsv((hue + 60) % 360, (int) (sat / 3), std::min((int) (val * 8 / 7), 255), alp);
                    imgInt.setPixelColor(ix, iy, clr);
                }
                else if (sat < 64 && val > 192) { //change the color of white edges.
                    clr.setHsv(240, (255-sat), val, alp);
                    imgConstr.setPixel(ix, iy, clr.rgba());
                    clr.setHsv(300, (255-sat), val, alp);
                    imgExt.setPixel(ix, iy, clr.rgba());
                    clr.setHsv(60, (int) (255-sat) / 2, val, alp);
                    imgInt.setPixel(ix, iy, clr.rgba());
                }
            }
        }
    }
    Construction = QIcon(QPixmap::fromImage(imgConstr));
    External = QIcon(QPixmap::fromImage(imgExt));
    Internal = QIcon(QPixmap::fromImage(imgInt));
    return *this;
}

#include "moc_TaskSketcherElements.cpp"
