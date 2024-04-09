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

#ifndef DRAWINGGUI_QGIDECORATION_H
#define DRAWINGGUI_QGIDECORATION_H

#include <unordered_map>

#include <Mod/TechDraw/TechDrawGlobal.h>

#include <QBrush>
#include <QGraphicsItemGroup>
#include <QPen>


QT_BEGIN_NAMESPACE
class QPainter;
class QStyleOptionGraphicsItem;
QT_END_NAMESPACE

#include <App/DocumentObserver.h>
#include <Base/Parameter.h>
#include <Base/Console.h>
#include <Base/Vector3D.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Mod/TechDraw/TechDrawGlobal.h>

namespace TechDrawGui
{

#define DECORNODRAG 0
#define DECORDRAGSTARTED 1
#define DECORDRAGGING 2

class TechDrawGuiExport QGIDecoration : public QGraphicsItemGroup
{
    using inherited = QGraphicsItemGroup;
public:
    explicit QGIDecoration();
    ~QGIDecoration() override = default;
    enum {Type = QGraphicsItem::UserType + 173};
    int type() const override { return Type;}

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter * painter,
               const QStyleOptionGraphicsItem * option,
               QWidget * widget = nullptr ) override;
    virtual void draw();

    // Mouse handling
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

    virtual void onDragFinished();

    void setWidth(double w);
    double getWidth() { return m_width; }
    void setStyle(Qt::PenStyle s);
    void setColor(QColor c);
    QColor getColor() { return m_colNormal; }
    void setFill(Qt::BrushStyle bs) { m_brushCurrent = bs; }
    void makeMark(double x, double y);
    void makeMark(Base::Vector3d v);

    void setFeature(App::DocumentObject *feature);
    App::DocumentObject *getFeature() const;

    template<class T>
    T *getFeatureAs() const {
        return Base::freecad_dynamic_cast<T>(getFeature());
    }

    template<class T>
    T *getFeatureViewProvider() const {
        return Base::freecad_dynamic_cast<T>(
                Gui::Application::Instance->getViewProvider(getFeature()));
    }

    const App::DocumentObjectT & getFeatureT() const { return m_feature; }

    virtual void setPreselect(bool enable);

protected:
    void addMovableItem(QGraphicsItem *item);
    void removeMovableItem(QGraphicsItem *item);
    void clearMovableItems();
    virtual void onItemMoved(QGraphicsItem *item, QPointF &oldPos, QGraphicsSceneMouseEvent *);

    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    virtual void setPrettyNormal() {}
    virtual void setPrettyPre() {}
    virtual void setPrettySel() {}
    virtual void setupEventFilter();

    virtual QColor prefNormalColor();
    virtual QColor prefPreColor();
    virtual QColor prefSelectColor();

    QPen m_pen;
    QBrush m_brush;
    QColor m_colCurrent;
    QColor m_colNormal;
    double m_width;
    Qt::PenStyle m_styleCurrent;
    Qt::BrushStyle m_brushCurrent;
    App::DocumentObjectT m_feature;

    int m_dragState;
    QGraphicsItem *m_moveProxy = nullptr;
    bool m_filterInstalled = false;
    bool m_hasHover = false;

    std::unordered_map<QGraphicsItem*, QPointF> m_moveableItems;
    QPointF m_oldPos;

private:
    bool m_busy = false;
};

}
#endif
