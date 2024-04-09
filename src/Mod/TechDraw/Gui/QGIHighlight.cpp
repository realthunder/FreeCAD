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
# include <cmath>
# include <QPainter>
# include <QGraphicsSceneMouseEvent>
# include <QGraphicsSceneHoverEvent>
# include <QStyleOptionGraphicsItem>
# include <QTimer>
#endif

#include <App/AutoTransaction.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Mod/TechDraw/App/DrawUtil.h>

#include "QGIHighlight.h"
#include "PreferencesGui.h"
#include "QGIViewPart.h"
#include "Rez.h"
#include "ViewProviderViewPart.h"


using namespace TechDrawGui;
using namespace TechDraw;

QGIHighlight::QGIHighlight() :
    m_referenceAngle(0.0)
{
    m_refSize = 0.0;

    m_shape = new QGIEdge;
    addToGroup(m_shape);

    m_reference = new QGCustomText();
    m_reference->setTightBounding(true);
    addToGroup(m_reference);

    setWidth(Rez::guiX(0.75));
    m_brushCurrent = Qt::NoBrush;

    setInteractive(false);
}

QGIHighlight::~QGIHighlight()
{

}

void QGIHighlight::onDragFinished()
{
    QGraphicsItem* parent = parentItem();
    auto qgivp = dynamic_cast<QGIViewPart*>(parent);
    if (!qgivp)
        return;
    auto detail = getFeatureAs<DrawViewDetail>();
    if (!detail)
        return;
    auto anchor = detail->AnchorPoint.getValue();
    Base::Vector3d delta = Rez::appX(DrawUtil::toVector3d(pos())) / qgivp->getViewObject()->getScale();
    anchor += DrawUtil::invertY(delta);
    App::DocumentObjectT objT(m_feature);

    // Must not recompute inside graphics scene event loop because of graphics item deletion.
    QTimer::singleShot(0, [objT, anchor]() {
        if (auto detail = Base::freecad_dynamic_cast<DrawViewDetail>(objT.getObject())) {
            App::AutoTransaction guard(QT_TRANSLATE_NOOP("Command", "Move detail view"));
            detail->AnchorPoint.setValue(anchor);
            Gui::Command::updateActive();
        }
    });
}

void QGIHighlight::setupEventFilter()
{
    addMovableItem(m_reference);
    m_moveProxy = m_shape;
    inherited::setupEventFilter();
}

void QGIHighlight::draw()
{
    prepareGeometryChange();
    makeHighlight();
    makeReference();
    setTools();
    inherited::draw();
}

void QGIHighlight::makeHighlight()
{
    QPainterPath path;
    QRectF r(m_start, m_end);

    if (getHoleStyle() == 0) {
        path.addEllipse(r);
    } else {
        path.addRect(r);
    }
    m_shape->setPath(path);
}

void QGIHighlight::updateReferencePos()
{
    QRectF r(m_start, m_end);
    auto pos = m_reference->pos() - r.center();

    double l = sqrt(pos.x()*pos.x() + pos.y()*pos.y());
    if (l < 1e-6) {
        m_referenceAngle = 0;
    }
    else {
        m_referenceAngle = acos(pos.x()/l) * 180 / M_PI ;
        if (pos.y() > 0)
            m_referenceAngle = 360 - m_referenceAngle;
    }
    m_referenceOffset = l - r.width() / 2.0;

    if (auto detailVp = getFeatureViewProvider<ViewProviderViewPart>()) {
        try {
            App::AutoTransaction guard(QT_TRANSLATE_NOOP("Command", "Move detail view reference"),
                    /* tmpName */false, /* recordViewObject */true);
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard1(App::Property::User1, &detailVp->HighlightAdjust);
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard2(App::Property::User1, &detailVp->HighlightOffset);
            detailVp->HighlightAdjust.setValue(m_referenceAngle);
            detailVp->HighlightOffset.setValue(m_referenceOffset);
        } catch (const Base::Exception &e) {
            e.ReportException();
        }
    }

    // For verification
#if 0
    makeReference();
    update();
#endif
}

void QGIHighlight::makeReference()
{
    prepareGeometryChange();
    int fontSize = QGIView::exactFontSize(Base::Tools::toStdString(m_refFont.family()),
                                          m_refSize);
    m_refFont .setPixelSize(fontSize);
    m_reference->setFont(m_refFont);
    m_reference->setPlainText(m_refText);

    double vertOffset = 0.0;
    if (m_referenceAngle >= 0.0 &&
        m_referenceAngle <= 180.0) {
        //above the X axis
        //referenceText is positioned by top-left, need to adjust upwards by text height
        vertOffset = m_reference->relaxedBoundingRect().height();
    } else {
        //below X axis. need to adjust upwards a bit because there is blank space above text
        vertOffset = m_reference->tightBoundingAdjust().y();
    }

    double horizOffset = 0.0;
    if (m_referenceAngle > 90.0 &&
        m_referenceAngle < 270.0) {
        //to left of Y axis
        horizOffset = -m_reference->relaxedBoundingRect().width();
    }
    QRectF r(m_start, m_end);
    double radius = r.width() / 2.0;
    QPointF center = r.center();
    double angleRad = m_referenceAngle * M_PI / 180.0;
    if (m_referenceOffset != 0.0) {
        horizOffset = 0;
        vertOffset = 0;
        radius +=  m_referenceOffset;
    }

    double posX = center.x() + cos(angleRad) * radius + horizOffset;
    double posY = center.y() - sin(angleRad) * radius - vertOffset;
    m_reference->setPos(posX, posY);

    double highRot = rotation();
    if (!TechDraw::DrawUtil::fpCompare(highRot, 0.0)) {
        QRectF refBR = m_reference->boundingRect();
        QPointF refCenter = refBR.center();
        m_reference->setTransformOriginPoint(refCenter);
        m_reference->setRotation(-highRot);
    }
}

void QGIHighlight::setInteractive(bool state)
{
    setHandlesChildEvents(false);
    setAcceptHoverEvents(false);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    setFlag(QGraphicsItem::ItemIsMovable, state);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges, state);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, state);

    m_shape->setFlag(QGraphicsItem::ItemIsSelectable, state);
    m_shape->setAcceptHoverEvents(state);

    m_reference->setFlag(QGraphicsItem::ItemIsSelectable, state);
    m_reference->setFlag(QGraphicsItem::ItemIsMovable, state);
    m_reference->setAcceptHoverEvents(state);
}

void QGIHighlight::onItemMoved(QGraphicsItem *item, QPointF &, QGraphicsSceneMouseEvent *)
{
    if (item == m_reference)
        updateReferencePos();
}

void QGIHighlight::setBounds(double x1, double y1, double x2, double y2)
{
    m_start = QPointF(Rez::guiX(x1), Rez::guiX(-y1));
    m_end = QPointF(Rez::guiX(x2), Rez::guiX(-y2));
}

void QGIHighlight::setReference(const char* ref)
{
    m_refText = QString::fromUtf8(ref);
}

void QGIHighlight::setFont(QFont f, double fsize)
{
    m_refFont = f;
    m_refSize = fsize;
}


//obs?
QColor QGIHighlight::getHighlightColor()
{
    return PreferencesGui::sectionLineQColor();
}

//obs??
Qt::PenStyle QGIHighlight::getHighlightStyle()
{
    return PreferencesGui::sectionLineStyle();
}

int QGIHighlight::getHoleStyle()
{
    return TechDraw::Preferences::mattingStyle();
}

void QGIHighlight::paint ( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget) {
    inherited::paint (painter, option, widget);
}

void QGIHighlight::setTools()
{
    m_pen.setWidthF(m_width);
    m_pen.setColor(m_colCurrent);
    m_pen.setStyle(Qt::CustomDashLine);

    m_brush.setStyle(m_brushCurrent);
    m_brush.setColor(m_colCurrent);

    m_shape->setWidth(m_width);
    m_shape->setStyle(m_styleCurrent);
}

void QGIHighlight::setPrettyPre()
{
    m_shape->setPrettyPre();
    m_reference->setPrettyPre();
}

void QGIHighlight::setPrettySel()
{
    m_shape->setPrettySel();
    if (m_reference->hasHover())
        m_reference->setPrettyPre();
    else
        m_reference->setPrettySel();
}

void QGIHighlight::setPrettyNormal()
{
    m_shape->setNormalColor(m_colCurrent);
    if (m_reference->hasHover())
        m_reference->setPrettyPre();
    else if (m_reference->isSelected())
        m_reference->setPrettySel();
    else
        m_reference->setDefaultTextColor(m_colCurrent);
}

QPainterPath QGIHighlight::shape() const
{
    return QPainterPath();
}

void QGIHighlight::setLinePen(QPen isoPen)
{
    m_pen = isoPen;
}
