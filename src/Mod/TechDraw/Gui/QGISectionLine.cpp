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
# include <QGraphicsScene>
# include <QGraphicsSceneMouseEvent>
# include <QPainter>
# include <QPainterPath>
# include <QStyleOptionGraphicsItem>
# include <QTimer>
#endif

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <Base/Tools.h>
#include <Gui/Command.h>
#include <Mod/TechDraw/App/DrawUtil.h>

#include "Rez.h"
#include "PreferencesGui.h"
#include "QGIArrow.h"
#include "QGIEdge.h"
#include "QGISectionLine.h"
#include "QGIView.h"
#include "ViewProviderViewSection.h"
#include "ZVALUE.h"


#define ANSISTANDARD 0
#define ISOSTANDARD 1
#define SINGLEDIRECTIONMODE 0       //both arrows point along the section normal
#define MULTIDIRECTIONMODE 1        //the arrows point along the normal of their section line segment

using namespace TechDrawGui;
using namespace TechDraw;

QGISectionLine::QGISectionLine() :
    m_pathMode(false),
    m_arrowMode()
{
    m_symbol = "";
    m_symSize = 0.0;

    m_extLen = 1.5 * Rez::guiX(QGIArrow::getPrefArrowSize());   //is there a standard for this??
    m_arrowSize = QGIArrow::getPrefArrowSize();

    m_line = new QGIEdge(-1);
    addToGroup(m_line);
    m_extend= new QGIEdge(-2);
    addToGroup(m_extend);
    m_arrow1 = new QGIArrow();
    addToGroup(m_arrow1);
    m_arrow2 = new QGIArrow();
    addToGroup(m_arrow2);
    m_symbol1 = new QGCustomText();
    m_symbol1->setTightBounding(true);
    m_symbol1->setZValue(ZVALUE::SECTIONLINE + 2);
    addToGroup(m_symbol1);
    m_symbol2 = new QGCustomText();
    m_symbol2->setTightBounding(true);
    m_symbol2->setZValue(ZVALUE::SECTIONLINE + 2);
    addToGroup(m_symbol2);

    setWidth(Rez::guiX(0.75));          //a default?
    setStyle(getSectionStyle());
    setColor(getSectionColor());
}

void QGISectionLine::setInteractive(bool enable, bool movable)
{
    if (enable == m_interactive && movable == m_movablePoints)
        return;

    if (!enable)
        movable = false;
    m_movablePoints = movable;
    m_interactive = enable;
    setHandlesChildEvents(false);
    setAcceptHoverEvents(false);
    setFlag(QGraphicsItem::ItemIsSelectable, enable);
    setFlag(QGraphicsPathItem::ItemIsMovable, movable);
    m_line->setFlag(QGraphicsItem::ItemIsSelectable, enable);
    m_line->setFlag(QGraphicsItem::ItemIsMovable, movable);
    m_line->setAcceptHoverEvents(enable);
    m_extend->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_extend->setFlag(QGraphicsItem::ItemIsMovable, false);
    m_extend->setAcceptHoverEvents(false);
    m_symbol1->setFlag(QGraphicsItem::ItemIsSelectable, enable);
    m_symbol1->setFlag(QGraphicsItem::ItemIsMovable, enable);
    m_symbol1->setAcceptHoverEvents(enable);
    m_symbol2->setFlag(QGraphicsItem::ItemIsSelectable, enable);
    m_symbol2->setFlag(QGraphicsItem::ItemIsMovable, enable);
    m_symbol2->setAcceptHoverEvents(enable);

    for (auto cPointItem : m_changePointMarks) {
        cPointItem->setAcceptHoverEvents(enable);
        cPointItem->setFlag(QGraphicsItem::ItemIsSelectable, enable);
        if (movable)
            addMovableItem(cPointItem);
        else
            removeMovableItem(cPointItem);
    }
    
    setupEventFilter();
}

void QGISectionLine::setupEventFilter()
{
    if (m_movablePoints)
        m_moveProxy = m_line;
    addMovableItem(m_symbol1);
    addMovableItem(m_symbol2);
    inherited::setupEventFilter();
}

bool QGISectionLine::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
    return inherited::sceneEventFilter(watched, event);
}

void QGISectionLine::draw()
{
    prepareGeometryChange();
    int format = getPrefSectionStandard();
    if (format == ANSISTANDARD) {                           //"ASME"/"ANSI"
        extensionEndsTrad();
    } else {
        extensionEndsISO();
    }

    if (!pathMode()) {
        makeSectionLine();
    }
    makeExtensionLine();
    makeArrows();
    makeSymbols();
    makeChangePointMarks();
    setTools();
    inherited::draw();
}

void QGISectionLine::makeExtensionLine()
{
    m_extend->setCapStyle(Qt::FlatCap);
    m_extend->setStyle(Qt::SolidLine);
    m_extend->setNormalColor(getSectionColor());
    m_extend->setWidth(getWidth());

    QPainterPath pp;
    pp.moveTo(m_beginExt1);
    pp.lineTo(m_endExt1);

    pp.moveTo(m_beginExt2);
    pp.lineTo(m_endExt2);
    m_extend->setPath(pp);
}

//make the traditional straight section line
void QGISectionLine::makeSectionLine()
{
    QPainterPath pp;
    pp.moveTo(m_start);
    pp.lineTo(m_end);
    m_line->setPath(pp);
}

void QGISectionLine::makeArrows()
{
    int format = getPrefSectionStandard();
    if (format == ANSISTANDARD) {
        makeArrowsTrad();
    } else {
        makeArrowsISO();
    }
}

//make Euro (ISO) Arrows
void QGISectionLine::makeArrowsISO()
{
    m_arrow1->setStyle(0);
    m_arrow1->setSize(QGIArrow::getPrefArrowSize());
    m_arrow1->setPos(m_start);
    m_arrow2->setStyle(0);
    m_arrow2->setSize(QGIArrow::getPrefArrowSize());
    m_arrow2->setPos(m_end);

    if (m_arrowMode == SINGLEDIRECTIONMODE) {
        double arrowRotation = getArrowRotation(m_arrowDir);
        m_arrow1->setRotation(arrowRotation);                   //rotation = 0  ==>  ->  horizontal, pointing right
        m_arrow2->setRotation(arrowRotation);
    } else {
        double arrowRotation1 = getArrowRotation(m_arrowDir1);
        m_arrow1->setRotation(arrowRotation1);
        double arrowRotation2 = getArrowRotation(m_arrowDir2);
        m_arrow2->setRotation(arrowRotation2);
    }
    m_arrow1->draw();
    m_arrow2->draw();
}

//make traditional (ASME) section arrows
void QGISectionLine::makeArrowsTrad()
{
    m_arrow1->setStyle(0);
    m_arrow1->setSize(QGIArrow::getPrefArrowSize());
    m_arrow2->setStyle(0);
    m_arrow2->setSize(QGIArrow::getPrefArrowSize());

    if (m_arrowMode == SINGLEDIRECTIONMODE) {
        double arrowRotation = getArrowRotation(m_arrowDir);
        m_arrow1->setRotation(arrowRotation);                   //rotation = 0  ==>  ->  horizontal, pointing right
        m_arrow2->setRotation(arrowRotation);
        m_arrowPos1 = getArrowPosition(m_arrowDir, m_start);
        m_arrow1->setPos(m_arrowPos1);
        m_arrowPos2 = getArrowPosition(m_arrowDir, m_end);
        m_arrow2->setPos(m_arrowPos2);
    } else {
        double arrowRotation1 = getArrowRotation(m_arrowDir1);
        m_arrow1->setRotation(arrowRotation1);
        m_arrowPos1 = getArrowPosition(m_arrowDir1, m_start);
        m_arrow1->setPos(m_arrowPos1);
        double arrowRotation2 = getArrowRotation(m_arrowDir2);
        m_arrow2->setRotation(arrowRotation2);
        m_arrowPos2 = getArrowPosition(m_arrowDir2, m_end);
        m_arrow2->setPos(m_arrowPos2);
    }

    m_arrow1->draw();
    m_arrow2->draw();
}

void QGISectionLine::calcSymbolPos(QPointF &symPos1, QPointF &symPos2)
{
    QRectF symRect = m_symbol1->relaxedBoundingRect();
    double symHeight = symRect.height();
    double gap = 0.5 * symHeight;  //symHeight as surrogate for char box

    int format = getPrefSectionStandard();
    if (format == ANSISTANDARD) {
        //arrows go at arrowhead position.
        QPointF motion1(m_arrowDir1.x, -m_arrowDir1.y);    //move in same direction as arrow
        QPointF motion2(m_arrowDir2.x, -m_arrowDir2.y);     //Qt y coords!
        symPos1 = m_arrowPos1 + motion1 * gap;
        symPos2 = m_arrowPos2 + motion2 * gap;
    } else {
        //symbols go at ends of extensions
        QPointF motion1(-m_arrowDir1.x, m_arrowDir1.y);     //move away from extension end
        QPointF motion2(-m_arrowDir2.x, m_arrowDir2.y);     //Qt y coords!
        symPos1 = m_endExt1 + motion1 * gap;
        symPos2 = m_endExt2 + motion2 * gap;
    }

}

void QGISectionLine::updateSymbolOffset()
{
    QPointF symPos1, symPos2;
    calcSymbolPos(symPos1, symPos2);

    auto rect1 = m_symbol1->boundingRect();
    auto rect2 = m_symbol2->boundingRect();

    auto pos1 = m_symbol1->pos() - rect1.center();
    pos1 = QTransform().rotate(rotation() - 360).map(pos1);
    pos1 += rect1.center() + QPointF(rect1.width()/2, rect1.height()/2);
    m_symbolOffset1 = pos1 - symPos1;

    auto pos2 = m_symbol2->pos() - rect2.center();
    pos2 = QTransform().rotate(rotation() - 360).map(pos2);
    pos2 += rect2.center() + QPointF(rect2.width()/2, rect2.height()/2);
    m_symbolOffset2 = pos2 - symPos2;

    if (auto vp = getFeatureViewProvider<ViewProviderViewSection>()) {
        try {
            App::AutoTransaction guard(QT_TRANSLATE_NOOP("Command", "Move section line"),
                    /* tmpName */false, /* recordViewObject */true);
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard1(App::Property::User1, &vp->SymbolOffset1);
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard2(App::Property::User1, &vp->SymbolOffset2);
            vp->SymbolOffset1.setValue(DrawUtil::toVector3d(m_symbolOffset1));
            vp->SymbolOffset2.setValue(DrawUtil::toVector3d(m_symbolOffset2));
        } catch (const Base::Exception &e) {
            e.ReportException();
        }
    }

    makeSymbols();
    update();
}

void QGISectionLine::makeSymbols()
{
    prepareGeometryChange();

    int fontSize = QGIView::exactFontSize(Base::Tools::toStdString(m_symFont.family()), m_symSize);
    m_symFont.setPixelSize(fontSize);
    m_symbol1->setFont(m_symFont);
    m_symbol1->setPlainText(QString::fromUtf8(m_symbol));
    m_symbol2->setFont(m_symFont);
    m_symbol2->setPlainText(QString::fromUtf8(m_symbol));

    QPointF symPos1, symPos2;
    calcSymbolPos(symPos1, symPos2);

    symPos1 += m_symbolOffset1;
    symPos2 += m_symbolOffset2;

    m_symbol1->centerAt(symPos1);
    m_symbol2->centerAt(symPos2);

    m_symbol1->setTransformOriginPoint(m_symbol1->mapFromParent(symPos1));
    m_symbol1->setRotation(360.0 - rotation());         //to Qt angle
    m_symbol2->setTransformOriginPoint(m_symbol2->mapFromParent(symPos2));
    m_symbol2->setRotation(360.0 - rotation());
}

//extension lines are on the stock side of the section line
void QGISectionLine::extensionEndsTrad()
{
    if (m_arrowMode == SINGLEDIRECTIONMODE) {
        QPointF offsetDir(m_arrowDir.x, -m_arrowDir.y);   //inverted Y
        offsetDir = DrawUtil::normalize(offsetDir);

        //draw from section line endpoint
        QPointF offsetEnd = m_extLen * offsetDir;
        m_beginExt1 = m_start;
        m_endExt1   = m_start + offsetEnd;
        m_beginExt2 = m_end;
        m_endExt2   = m_end + offsetEnd;
   } else {
        //extension lines run from point on section line to arrowhead
        m_beginExt1 = m_start;
        m_endExt1   = getArrowPosition(m_arrowDir1, m_start);
        m_beginExt2 = m_end;
        m_endExt2   = getArrowPosition(m_arrowDir2, m_end);
    }
}

//the extension lines are on the waste side of the section line!
void QGISectionLine::extensionEndsISO()
{
    if (m_arrowMode == SINGLEDIRECTIONMODE) {
        QPointF offsetDir(-m_arrowDir.x, m_arrowDir.y);     //reversed and inverted y
        offsetDir = DrawUtil::normalize(offsetDir);

        //draw from section line endpoint less arrow length
        QPointF offsetStart = offsetDir * Rez::guiX(QGIArrow::getPrefArrowSize());
        QPointF offsetEnd = m_extLen * offsetDir;

        m_beginExt1 = m_start + offsetStart;
        m_endExt1   = m_start + offsetStart + offsetEnd;
        m_beginExt2 = m_end + offsetStart;
        m_endExt2   = m_end + offsetStart + offsetEnd;
    } else {
        //extension lines run in reverse of arrow direction from base of arrowhead for distance m_extLen
        QPointF offsetDir1(-m_arrowDir1.x, m_arrowDir1.y);      //reversed and inverted y
        offsetDir1 = DrawUtil::normalize(offsetDir1);
        QPointF offsetStart1 =  offsetDir1 * Rez::guiX(QGIArrow::getPrefArrowSize());
        QPointF offsetEnd1 = m_extLen * offsetDir1;
        m_beginExt1 = m_start + offsetStart1;
        m_endExt1   = m_start + offsetStart1 + offsetEnd1;

        QPointF offsetDir2(-m_arrowDir2.x, m_arrowDir2.y);      //reversed and inverted y
        offsetDir2 = DrawUtil::normalize(offsetDir2);
        QPointF offsetStart2 =  offsetDir2 * Rez::guiX(QGIArrow::getPrefArrowSize());
        QPointF offsetEnd2 = m_extLen * offsetDir2;
        m_beginExt2 = m_end + offsetStart2;
        m_endExt2   = m_end + offsetStart2 + offsetEnd2;
    }
}

void QGISectionLine::makeChangePointMarks()
{
    double segmentLength = 0.50 * QGIArrow::getPrefArrowSize();
    int i = 0;
    while (m_changePointMarks.size() > m_changePointData.size()) {
        auto cPoint = m_changePointMarks.back();
        m_changePointMarks.pop_back();
        removeMovableItem(cPoint);
        cPoint->hide();
        scene()->removeItem(cPoint);
        delete cPoint;
    }
    for (auto& cPoint : m_changePointData) {
        QGIEdge *cPointItem;
        if (i >= static_cast<int>(m_changePointMarks.size())) {
            cPointItem = new QGIEdge(i);
            addToGroup(cPointItem);
            if (m_movablePoints)
                addMovableItem(cPointItem);
            m_changePointMarks.push_back(cPointItem);
            cPointItem->setZValue(ZVALUE::SECTIONLINE + 1);
            cPointItem->installSceneEventFilter(this);
            cPointItem->setAcceptHoverEvents(m_interactive);
            cPointItem->setFlag(QGraphicsItem::ItemIsSelectable, m_interactive);
        } else
            cPointItem = m_changePointMarks[i];
        ++i;

        cPointItem->setWidth(2*getWidth());
        cPointItem->setNormalColor(getSectionColor());
        cPointItem->setStyle(Qt::SolidLine);

        QPainterPath pPath;
        QPointF start = cPoint.getPreDirection() * segmentLength;
        QPointF end   = cPoint.getPostDirection() * segmentLength;
        pPath.moveTo(Rez::guiPt(start));
        pPath.lineTo(QPointF(0,0));
        pPath.lineTo(Rez::guiPt(end));

        cPointItem->setPath(pPath);
        cPointItem->setPos(Rez::guiPt(cPoint.getLocation()));
    }
}

void QGISectionLine::setEnds(const Base::Vector3d &l1, const Base::Vector3d &l2)
{
    m_start = QPointF(l1.x, l1.y);
    m_end = QPointF(l2.x, l2.y);
}

void QGISectionLine::setEnds(const QPointF &start, const QPointF &end)
{
    m_start = start;
    m_end = end;
}

void QGISectionLine::setBounds(double x1, double y1, double x2, double y2)
{
    m_start = QPointF(x1, y1);
    m_end = QPointF(x2, y2);
}

void QGISectionLine::setSymbol(char* sym)
{
    m_symbol = sym;
}

void QGISectionLine::setDirection(double xDir, double yDir)
{
    Base::Vector3d newDir(xDir, yDir, 0.0);
    setDirection(newDir);
}

void QGISectionLine::setDirection(const Base::Vector3d &dir)
{
    m_arrowMode = SINGLEDIRECTIONMODE;
    m_arrowDir = dir;
    m_arrowDir.Normalize();
    m_arrowDir1 = dir;
    m_arrowDir1.Normalize();
    m_arrowDir2 = dir;
    m_arrowDir2.Normalize();
}

void QGISectionLine::setArrowDirections(const Base::Vector3d &dir1, const Base::Vector3d &dir2)
{
    m_arrowMode = MULTIDIRECTIONMODE;
    m_arrowDir1 = dir1;
    m_arrowDir1.Normalize();
    m_arrowDir2 = dir2;
    m_arrowDir2.Normalize();
}

//convert an arrow direction vector into a Qt rotation angle degrees
double QGISectionLine::getArrowRotation(const Base::Vector3d &arrowDir)
{
    double angle = atan2f(arrowDir.y, arrowDir.x);
    if (angle < 0.0) {
        angle = 2 * M_PI + angle;
    }
    double arrowRotation = 360.0 - angle * (180.0/M_PI);   //convert to Qt rotation (clockwise degrees)
    return arrowRotation;
}

QPointF QGISectionLine::getArrowPosition(const Base::Vector3d &arrowDir, const QPointF &refPoint)
{
    QPointF qArrowDir(arrowDir.x, -arrowDir.y);              //remember Y dir is flipped
    qArrowDir = DrawUtil::normalize(qArrowDir);

    double offsetLength = m_extLen + Rez::guiX(QGIArrow::getPrefArrowSize());
    QPointF offsetVec = offsetLength * qArrowDir;

    return QPointF(refPoint + offsetVec);
}

void QGISectionLine::setFont(QFont f, double fsize)
{
    m_symFont = f;
    m_symSize = fsize;
}

void QGISectionLine::setPath(const QPainterPath& path)
{
    m_line->setPath(path);
}

void QGISectionLine::setChangePoints(const TechDraw::ChangePointVector &changePointData)
{
    m_changePointData = changePointData;
    clearChangePointMarks();
}

void QGISectionLine::clearChangePoints()
{
    clearChangePointMarks();
    m_changePointData.clear();
}

void QGISectionLine::clearChangePointMarks()
{
    if (!m_changePointMarks.empty()) {
        for (auto& cPoint : m_changePointMarks) {
            removeMovableItem(cPoint);
            cPoint->hide();
            scene()->removeItem(cPoint);
            delete cPoint;
        }
        m_changePointMarks.clear();
    }
}

void QGISectionLine::setSectionColor(const QColor &c)
{
    setColor(c);
}

QColor QGISectionLine::getSectionColor()
{
    return PreferencesGui::sectionLineQColor();
}

//SectionLineStyle
void QGISectionLine::setSectionStyle(int style)
{
    Qt::PenStyle sectStyle = static_cast<Qt::PenStyle> (style);
    setStyle(sectStyle);
}

Qt::PenStyle QGISectionLine::getSectionStyle()
{
    return PreferencesGui::sectionLineStyle();
}

//ASME("traditional") vs ISO("reference arrow method") arrows
int QGISectionLine::getPrefSectionStandard()
{
    return Preferences::getPreferenceGroup("Standards")->GetInt("SectionLineStandard", ISOSTANDARD);
}


void QGISectionLine::setTools()
{
    m_line->setStyle(m_styleCurrent);
    m_line->setWidth(m_width);
    m_line->setNormalColor(m_colCurrent);
    m_line->setCapStyle(Qt::RoundCap);

    m_arrow1->setNormalColor(m_colCurrent);
    m_arrow1->setFillColor(m_colCurrent);
    m_arrow1->setPrettyNormal();
    m_arrow2->setNormalColor(m_colCurrent);
    m_arrow2->setFillColor(m_colCurrent);
    m_arrow2->setPrettyNormal();

    m_symbol1->setDefaultTextColor(m_colCurrent);
    m_symbol2->setDefaultTextColor(m_colCurrent);
}

void QGISectionLine::setPrettyPre()
{
    m_line->setPrettyPre();
    m_extend->setPrettyPre();
    m_symbol1->setPrettyPre();
    m_symbol2->setPrettyPre();
    m_arrow1->setPrettyPre();
    m_arrow2->setPrettyPre();
    for (auto pointItem : m_changePointMarks) {
        pointItem->setPrettyPre();
    }
}

void QGISectionLine::setPrettySel()
{
    m_line->setPrettySel();
    m_extend->setPrettySel();
    m_arrow1->setPrettySel();
    m_arrow2->setPrettySel();
    if (m_symbol1->hasHover())
        m_symbol1->setPrettyPre();
    else
        m_symbol1->setPrettySel();
    if (m_symbol2->hasHover())
        m_symbol2->setPrettyPre();
    else
        m_symbol2->setPrettySel();
    for (auto pointItem : m_changePointMarks) {
        if (pointItem->hasHover())
            pointItem->setPrettyPre();
        else
            pointItem->setPrettySel();
    }
}

void QGISectionLine::setPrettyNormal()
{
    m_line->setPrettyNormal();
    m_extend->setPrettyNormal();
    m_arrow1->setPrettyNormal();
    m_arrow2->setPrettyNormal();
    if (m_symbol1->hasHover())
        m_symbol1->setPrettyPre();
    else if (m_symbol1->isSelected())
        m_symbol1->setPrettySel();
    else
        m_symbol1->setPrettyNormal();
    if (m_symbol2->hasHover())
        m_symbol2->setPrettyPre();
    else if (m_symbol2->isSelected())
        m_symbol2->setPrettySel();
    else
        m_symbol2->setPrettyNormal();
    for (auto pointItem : m_changePointMarks) {
        if (pointItem->hasHover())
            pointItem->setPrettyPre();
        else if (pointItem->isSelected())
            pointItem->setPrettySel();
        else
            pointItem->setPrettyNormal();
    }
}

QPainterPath QGISectionLine::shape() const
{
    return QPainterPath();
}

void QGISectionLine::onItemMoved(QGraphicsItem *item, QPointF &oldPos, QGraphicsSceneMouseEvent *ev)
{
    auto doneMoving = [this, ev](const char *msg) {
        auto objT = getFeatureT();
        auto changePointData = m_changePointData;
        QTimer::singleShot(0, [changePointData, objT, msg]() {
            if (auto drawSection = Base::freecad_dynamic_cast<TechDraw::DrawViewSection>(objT.getObject())) {
                try {
                    App::AutoTransaction guard(msg);
                    drawSection->setChangePoints(changePointData);
                    Gui::Command::updateActive();
                } catch (Base::Exception &e) {
                    e.ReportException();
                }
            }
        });
    };

    if (item == m_symbol1 || item == m_symbol2) {
        updateSymbolOffset();
    }
    else if (item == m_line) {
        if (ev->type() == QEvent::GraphicsSceneMouseRelease) {
            auto offset = Rez::appX(this->pos()) - Rez::appX(oldPos);
            for (auto &point : m_changePointData)
                point.setLocation(point.getLocation() + offset);
            if (!pathMode()) {
                setEnds(Rez::guiX(m_changePointData[0].getLocation()),
                        Rez::guiX(m_changePointData[1].getLocation()));
            }
            this->setPos(QPointF(0, 0));
            draw();
            doneMoving(QT_TRANSLATE_NOOP("Command", "Move section line"));
        }
    }
    else if (auto pointItem = qgraphicsitem_cast<QGIEdge*>(item)) {
        int index = pointItem->getProjIndex();
        int count = static_cast<int>(m_changePointData.size());
        if (index >= 0 && index < count) {
            int preIdx = index==0 ? count-1 : index-1;
            int postIdx = index==count-1 ? 0 : index+1;
            Base::Vector3d pos = DrawUtil::toVector3d(Rez::appX(pointItem->pos()));
            auto &point = m_changePointData[index];
            auto &prePoint = m_changePointData[preIdx];
            auto &postPoint = m_changePointData[postIdx];
            if (count == 2) {
                // Means it's a simple line with only two points. We shall
                // rotate around middle point with fixed length.
                Base::Vector3d pos0 = DrawUtil::toVector3d(Rez::appX(oldPos));
                Base::Vector3d pos1 = DrawUtil::toVector3d(postPoint.getLocation());
                auto mid = (pos1+pos0)/2;
                auto dir = postIdx<index ? mid-pos : pos-mid;
                dir.Normalize();

                double length = Base::Distance(mid, pos0);
                pos = mid + dir * length;
                pos1 = mid - dir * length;

                postPoint.setLocation(DrawUtil::toQPointF(pos1));
            }
            point.setLocation(DrawUtil::toQPointF(pos));
            point.setPreDirection(DrawUtil::normalize(point.getLocation() - prePoint.getLocation()));
            point.setPostDirection(DrawUtil::normalize(postPoint.getLocation() - point.getLocation()));
            prePoint.setPostDirection(-point.getPreDirection());
            postPoint.setPreDirection(-point.getPostDirection());
            if (m_changePointData.size() == 2) {
                setEnds(Rez::guiX(m_changePointData[0].getLocation()),
                        Rez::guiX(m_changePointData[1].getLocation()));

                if (m_arrowMode == SINGLEDIRECTIONMODE) {
                    QPointF dir = m_changePointData[0].getPostDirection();
                    setDirection(dir.y(), dir.x());
                }
            }
            oldPos = pointItem->pos();
            if (ev->type() != QEvent::GraphicsSceneMouseRelease)
                draw();
            else
                doneMoving(QT_TRANSLATE_NOOP("Command", "Rotate section line"));
        }
    }
}

void QGISectionLine::setSymbolOffsets(const QPointF &p1, const QPointF &p2)
{
    if (p1 == m_symbolOffset1 || p2 == m_symbolOffset2)
        return;
    m_symbolOffset1 = p1;
    m_symbolOffset2 = p2;
    makeSymbols();
    update();
}

void QGISectionLine::setLinePen(QPen isoPen)
{
    m_pen = isoPen;
}
