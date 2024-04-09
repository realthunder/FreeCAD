/***************************************************************************
 *   Copyright (c) 2016 WandererFan <wandererfan@gmail.com>                *
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
# include <cassert>

# include <QGraphicsScene>
# include <QGraphicsSceneHoverEvent>
# include <QPainter>
# include <QStyleOptionGraphicsItem>
#endif

#include <App/Application.h>

#include <Gui/Selection.h>

#include <Mod/TechDraw/App/DrawView.h>

#include "QGIPrimPath.h"
#include "PreferencesGui.h"
#include "QGIView.h"


using namespace TechDrawGui;
using namespace TechDraw;

QGIPrimPath::QGIPrimPath():
    m_width(0),
    m_capStyle(Qt::RoundCap),
    m_fillStyleCurrent (Qt::NoBrush),
//    m_fillStyleCurrent (Qt::SolidPattern),
    m_fillOverride(false)
{
    setCacheMode(QGraphicsItem::NoCache);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(true);

    multiselectActivated = false;

    m_colOverride = false;
    m_colNormal = getNormalColor();
    m_colCurrent = m_colNormal;
    m_styleCurrent = Qt::SolidLine;

    multiselectActivated = false;

    m_capStyle = prefCapStyle();

    m_fillDef = Qt::NoBrush;
    m_fillSelect = Qt::SolidPattern;
    m_fillNormal = m_fillDef;
    m_fillStyleCurrent = m_fillNormal;

    m_colDefFill = Qt::white;
//    m_colDefFill = Qt::transparent;
    setFillColor(m_colDefFill);

    setPrettyNormal();
}

QVariant QGIPrimPath::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedHasChanged && scene()) {
        if(isSelected()) {
            setPrettySel();
        } else {
            setPrettyNormal();
        }
    }
    return inherited::itemChange(change, value);
}

void QGIPrimPath::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    setPreselect(true);
    inherited::hoverEnterEvent(event);
}

void QGIPrimPath::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    setPreselect(false);
    inherited::hoverLeaveEvent(event);
}

void QGIPrimPath::setPreselect(bool enable)
{
    if (m_hasHover != enable) {
        m_hasHover = enable;
        if (enable)
            setPrettyPre();
        else if (isSelected())
            setPrettySel();
        else
            setPrettyNormal();
        update();
    }
}

void QGIPrimPath::setPrettyNormal() {
    m_colCurrent = m_colNormal;
    m_fillColorCurrent = m_colNormalFill;
}

void QGIPrimPath::setPrettyPre() {
//    Base::Console().Message("QGIPP::setPrettyPre()\n");
    m_colCurrent = getPreColor();
    if (!m_fillOverride) {
        m_fillColorCurrent = getPreColor();
    }
}

void QGIPrimPath::setPrettySel() {
//    Base::Console().Message("QGIPP::setPrettySel()\n");
    m_colCurrent = getSelectColor();
    if (!m_fillOverride) {
        m_fillColorCurrent = getSelectColor();
    }
}

//wf: why would a face use its parent's normal colour?
//this always goes to parameter
QColor QGIPrimPath::getNormalColor()
{
    QGIView *parent;

    if (m_colOverride) {
        return m_colNormal;
    }

    QGraphicsItem* qparent = parentItem();
    if (!qparent) {
        parent = nullptr;
    } else {
        parent = dynamic_cast<QGIView *> (qparent);
    }

    if (parent) {
        return parent->getNormalColor();
    }
    return PreferencesGui::normalQColor();
}

QColor QGIPrimPath::getPreColor()
{
    QGIView *parent;
    QGraphicsItem* qparent = parentItem();
    if (!qparent) {
        parent = nullptr;
    } else {
        parent = dynamic_cast<QGIView *> (qparent);
    }

    if (parent) {
        return parent->getPreColor();
    }
    return PreferencesGui::preselectQColor();
}

QColor QGIPrimPath::getSelectColor()
{
    QGIView *parent;
    QGraphicsItem* qparent = parentItem();
    if (!qparent) {
        parent = nullptr;
    } else {
        parent = dynamic_cast<QGIView *> (qparent);
    }

    if (parent) {
        return parent->getSelectColor();
    }
    return PreferencesGui::selectQColor();
}

void QGIPrimPath::setStrokeWidth(double w)
{
    if (w != m_strokeWidth) {
        prepareGeometryChange();
        m_strokeWidth = w;
        m_boundingRect = QRectF();
        m_shapePath = QPainterPath();
        update();
    }
}

void QGIPrimPath::setWidth(double w)
{
    m_width = w;
}

void QGIPrimPath::setStyle(Qt::PenStyle s)
{
// TODO: edge lines for faces are drawn with setStyle(Qt::NoPen) and trigger this message.
//    Base::Console().Warning("QGIPP::setStyle(Qt: %d) is deprecated. Use setLinePen instead\n", s);
    m_styleNormal = s;
    m_styleCurrent = s;
}

void QGIPrimPath::setStyle(int s)
{
// TODO: edge lines for faces are drawn with setStyle(Qt::NoPen) and trigger this message.
//    Base::Console().Warning("QGIPP::setStyle(int: %d) is deprecated. Use setLinePen instead\n", s);
    m_styleCurrent = static_cast<Qt::PenStyle>(s);
    m_styleNormal = static_cast<Qt::PenStyle>(s);
}

void QGIPrimPath::setNormalColor(QColor c)
{
    m_colNormal = c;
    m_colOverride = true;
    m_colCurrent = m_colNormal;
}

void QGIPrimPath::setCapStyle(Qt::PenCapStyle c)
{
    m_capStyle = c;
}

Base::Reference<ParameterGrp> QGIPrimPath::getParmGroup()
{
    return Preferences::getPreferenceGroup("Colors");
}

void QGIPrimPath::setTools() const
{
    m_pen.setWidth(m_width);
    m_pen.setColor(m_colCurrent);
    m_pen.setStyle(m_styleCurrent);
    m_pen.setCapStyle(m_capStyle);
    m_brush.setStyle(m_fillStyleCurrent);
    m_brush.setColor(m_fillColorCurrent);
}

QPainterPath QGIPrimPath::opaqueArea() const
{
    if (m_brush.isOpaque())
        return isClipped() ? clipPath() : shape();
    return QGraphicsItem::opaqueArea();
}

//EdgeCapStyle param changed from UInt (Qt::PenCapStyle) to Int (QComboBox index)
Qt::PenCapStyle QGIPrimPath::prefCapStyle()
{
    return (Qt::PenCapStyle)Preferences::LineCapStyle();
}

void QGIPrimPath::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    Qt::KeyboardModifiers originalModifiers = event->modifiers();
    if (event->button()&Qt::LeftButton) {
        multiselectActivated = false;
    }

    if (event->button() == Qt::LeftButton
        && multiselectEligible()
        && PreferencesGui::multiSelection()) {

        auto parent = dynamic_cast<QGIView *>(parentItem());
        if (parent) {
            std::vector<Gui::SelectionObject> selection = Gui::Selection().getSelectionEx();
            if (selection.size() == 1
                && selection.front().getObject() == parent->getViewObject()) {

                multiselectActivated = true;
                event->setModifiers(originalModifiers | Qt::ControlModifier);
            }
        }
    }

    inherited::mousePressEvent(event);

    event->setModifiers(originalModifiers);
}

void QGIPrimPath::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Qt::KeyboardModifiers originalModifiers = event->modifiers();
    if ((event->button()&Qt::LeftButton) && multiselectActivated) {
        if (PreferencesGui::multiSelection()) {
            event->setModifiers(originalModifiers | Qt::ControlModifier);
        }

        multiselectActivated = false;
    }

    inherited::mouseReleaseEvent(event);

    event->setModifiers(originalModifiers);
}

void QGIPrimPath::setFill(QColor c, Qt::BrushStyle s) {
    setFillColor(c);
    m_fillNormal = s;
    m_fillStyleCurrent = s;
}

void QGIPrimPath::setFill(QBrush b) {
    setFillColor(b.color());
    m_fillNormal = b.style();
    m_fillStyleCurrent = b.style();
}

void QGIPrimPath::resetFill() {
    m_colNormalFill = m_colDefFill;
    m_fillNormal = m_fillDef;
    m_fillStyleCurrent = m_fillDef;
}

//set PlainFill
void QGIPrimPath::setFillColor(QColor c)
{
    m_colNormalFill = c;
    m_fillColorCurrent = m_colNormalFill;
}

void QGIPrimPath::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
    (void)option;
    (void)widget;
    setTools();
    painter->setPen(m_pen);
    painter->setBrush(m_brush);
    painter->drawPath(path());
}

QPainterPath QGIPrimPath::path() const
{
    return m_path;
}

void QGIPrimPath::setPath(const QPainterPath &path)
{
    if (m_path == path)
        return;
    prepareGeometryChange();
    m_path = path;
    m_shapePath = QPainterPath();
    m_boundingRect = QRectF();
    update();
}

// Copied from Qt's qgraphicsitem.cpp
QPainterPath QGIPrimPath::shapeFromPath(const QPainterPath &path, const QPen &pen, bool filled)
{
    // We unfortunately need this hack as QPainterPathStroker will set a width of 1.0
    // if we pass a value of 0.0 to QPainterPathStroker::setWidth()
    const qreal penWidthZero = qreal(0.00000001);

    if (path == QPainterPath() || pen == Qt::NoPen)
        return path;
    QPainterPathStroker ps;
    ps.setCapStyle(pen.capStyle());
    if (pen.widthF() <= 0.0)
        ps.setWidth(penWidthZero);
    else
        ps.setWidth(pen.widthF());
    ps.setJoinStyle(pen.joinStyle());
    ps.setMiterLimit(pen.miterLimit());
    QPainterPath p = ps.createStroke(path);
    if (filled)
        p.addPath(path);
    return p;
}

QPainterPath QGIPrimPath::shape() const
{
    if (m_shapePath.isEmpty()) {
        QPen pen;
        pen.setWidthF(m_strokeWidth);
        m_shapePath = shapeFromPath(m_path, pen, m_fillSelect!=Qt::NoBrush);
    }
    return m_shapePath;
}

QRectF QGIPrimPath::boundingRect() const
{
    if (m_boundingRect.isNull()) {
        if (m_strokeWidth == 0.0)
            m_boundingRect = m_path.controlPointRect();
        else {
            m_boundingRect = shape().controlPointRect();
        }
    }
    return m_boundingRect;
}
