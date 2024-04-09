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

#ifndef TECHDRAWGUI_QGISECTIONLINE_H
#define TECHDRAWGUI_QGISECTIONLINE_H

#include <Mod/TechDraw/TechDrawGlobal.h>

#include <QColor>
#include <QFont>
#include <QPainterPath>
#include <QPointF>

#include <Base/Vector3D.h>
#include <Mod/TechDraw/App/DrawViewSection.h>

#include "QGIDecoration.h"
#include "QGCustomText.h"


namespace TechDrawGui
{

class QGIArrow;
class QGCustomText;
class QGIEdge;

class TechDrawGuiExport QGISectionLine : public QGIDecoration
{
    using inherited = QGIDecoration;
public:
    explicit QGISectionLine();
    ~QGISectionLine() override = default;

    enum {Type = QGraphicsItem::UserType + 172};
    int type() const override { return Type;}
    QPainterPath shape() const override;
    void draw() override;

    void setEnds(const Base::Vector3d &l1, const Base::Vector3d &l2);
    void setEnds(const QPointF &start, const QPointF &end);
    void setBounds(double x1, double y1, double x2, double y2);
    void setPath(const QPainterPath& path);
    void setSymbol(char* sym);
    void setDirection(double xDir, double yDir);
    void setDirection(const Base::Vector3d &dir);
    void setArrowDirections(const Base::Vector3d &dir1, const Base::Vector3d &dir2);
    void setFont(QFont f, double fsize);
    void setSectionStyle(int style);
    void setSectionColor(const QColor &c);
    void setPathMode(bool mode) { m_pathMode = mode; }
    bool pathMode() { return m_pathMode; }
    void setChangePoints(const TechDraw::ChangePointVector &changePoints);
    void clearChangePoints();
    void setInteractive(bool enable, bool movable);
    void setLinePen(QPen isoPen);

    void setSymbolOffsets(const QPointF &p1, const QPointF &p2);

protected:
    void updateSymbolOffset();
    void calcSymbolPos(QPointF &symPos1, QPointF &symPos2);
    void setPrettyNormal() override;
    void setPrettyPre() override;
    void setPrettySel() override;
    void setupEventFilter() override;
    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;
    void onItemMoved(QGraphicsItem *item, QPointF &oldPos, QGraphicsSceneMouseEvent *) override;
    QColor getSectionColor();
    Qt::PenStyle getSectionStyle();
    void makeSectionLine();
    void makeExtensionLine();
    void makeArrows();
    void makeArrowsTrad();
    void makeArrowsISO();
    void makeSymbols();
    void makeChangePointMarks();
    void setTools();
    int  getPrefSectionStandard();
    void extensionEndsISO();
    void extensionEndsTrad();
    double getArrowRotation(const Base::Vector3d &arrowDir);
    QPointF getArrowPosition(const Base::Vector3d &arrowDir, const QPointF &refPoint);
    void clearChangePointMarks();

private:
    const char*        m_symbol;
    QGIEdge          * m_line;
    QGIEdge          * m_extend;
    QGIArrow*          m_arrow1;
    QGIArrow*          m_arrow2;
    QPointF            m_symbolOffset1;
    QPointF            m_symbolOffset2;
    QGCustomText*      m_symbol1;
    QGCustomText*      m_symbol2;
    QPointF            m_start;         //start of section line
    QPointF            m_end;           //end of section line
    Base::Vector3d     m_arrowDir;
    std::string        m_symFontName;
    QFont              m_symFont;
    double             m_symSize;
    double             m_arrowSize;
    double             m_extLen;
    QPointF            m_beginExt1;     //start of extension line 1
    QPointF            m_endExt1;       //end of extension line 1
    QPointF            m_beginExt2;     //start of extension line 2
    QPointF            m_endExt2;       //end of extension line 1
    bool               m_pathMode;      //use external path for line
    int                m_arrowMode;     //0 = 1 direction for both arrows, 1 = direction for each arrow
    Base::Vector3d     m_arrowDir1;
    Base::Vector3d     m_arrowDir2;
    QPointF            m_arrowPos1;
    QPointF            m_arrowPos2;
    std::vector<QGIEdge*> m_changePointMarks;
    TechDraw::ChangePointVector m_changePointData;
    bool               m_movablePoints = false;
    bool               m_interactive = false;
};

}

#endif // TECHDRAWGUI_QGISECTIONLINE_H
