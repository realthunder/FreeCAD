/***************************************************************************
 *   Copyright (c) 2012-2013 Luke Parry <l.parry@warwick.ac.uk>            *
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

#ifndef DRAWINGGUI_QGRAPHICSITEMVIEW_H
#define DRAWINGGUI_QGRAPHICSITEMVIEW_H

#include <Mod/TechDraw/TechDrawGlobal.h>

#include <fastsignals/signal.h>

#include <QColor>
#include <QFont>
#include <QGraphicsItemGroup>
#include <QImage>
#include <QObject>
#include <QTransform>
#include <QPen>
#include <QPointF>

#include <Base/Parameter.h>
#include <Base/Vector3D.h>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
class QGraphicsSceneMouseEvent;
QT_END_NAMESPACE

namespace App
{
class DocumentObject;
}

namespace Gui
{
class ViewProvider;
}

namespace TechDraw
{
class DrawView;
}

namespace TechDrawGui
{
class QGSPage;
class QGVPage;
class ViewProviderPage;
class QGCustomBorder;
class QGCustomLabel;
class QGCustomText;
class QGICaption;
class MDIViewPage;
class QGIViewClip;
class QGCustomImage;
class QGTracker;
class QGIVertex;

class TechDrawGuiExport  QGIView : public QObject, public QGraphicsItemGroup
{
    using inherited = QGraphicsItemGroup;
    Q_OBJECT
public:
    QGIView();

    enum {Type = QGraphicsItem::UserType + 101};
    int type() const override { return Type;}
    QRectF boundingRect() const override;
    /** Where a click or the pointer picks the view. Its whole frame, or,
     * for a view that passes clicks (passesClicks()), its frame's line --
     * a band along it, while the frame is shown -- its label and its
     * caption: its lines and faces are picked themselves, and a click on
     * the rest goes through to what is under it (docs/HandsOnQueue.md
     * entries 149 and 155)
     */
    QPainterPath shape() const override;
    /** shape(), and for a view that picks by its content
     * (picksByContent()) wherever that is drawn: "anything non transparent
     * should register click" (entry 155). Qt asks this for a point.
     */
    bool contains(const QPointF& point) const override;
    /// The same for a press or the pointer: Qt picks an item at a point
    /// by a pixel-sized path, through this, not contains()
    bool collidesWithPath(const QPainterPath& path,
                          Qt::ItemSelectionMode mode = Qt::IntersectsItemShape) const override;
    void paint( QPainter *painter,
                        const QStyleOptionGraphicsItem *option,
                        QWidget *widget = nullptr ) override;

    const char *      getViewName() const;
    const std::string getViewNameAsString() const;
    void setViewFeature(TechDraw::DrawView *obj);
    TechDraw::DrawView * getViewObject() const;
    MDIViewPage* getMDIViewPage() const;

    double getScale();

    void hideFrame();               //used by derived classes that don't display a frame

    virtual bool getFrameState();
    /// Whether anything of \a dv -- of its projection group, for a group's
    /// view -- is selected: with the page's frames hidden, its frame shows
    /// then (getFrameState(), docs/HandsOnQueue.md entry 155)
    static bool selectionInside(TechDraw::DrawView* dv);
    virtual void toggleCache(bool state);
    virtual void updateView(bool update = false);
    virtual void drawBorder();
    virtual void isVisible(bool state);
    virtual bool isVisible();

    virtual bool getGroupSelection();
    virtual void setGroupSelection(bool isSelected);
    virtual void setGroupSelection(bool isSelected, const std::vector<std::string> &subNames);

    virtual void draw();
    virtual void drawCaption();
    virtual void rotateView();
    void makeMark(double xPos, double yPos, QColor color = Qt::red);
    void makeMark(Base::Vector3d pos, QColor color = Qt::red);
    void makeMark(QPointF pos, QColor color = Qt::red);

    //! the child items of type T whose projection index is in indexes.
    //! explicitly instantiated for QGIVertex* and QGIEdge* in QGIView.cpp.
    template <typename T>
    std::vector<T> getObjects(const std::vector<int>& indexes);


    /** Methods to ensure that Y-Coordinates are orientated correctly.
     * @{ */
    void setPosition(qreal xPos, qreal yPos);
    inline qreal getY() { return y() * -1; }
    bool isInnerView() const { return m_innerView; }
    void isInnerView(bool state) { m_innerView = state; }
    QGIViewClip* getClipGroup();


    void alignTo(QGraphicsItem*, const QString &alignment);

    QColor prefNormalColor(); //preference
    QColor getNormalColor() { return m_colNormal; }  //current setting
    void setNormalColor(QColor color) { m_colNormal = color; }
    QColor getPreColor();     //preference
    QColor getSelectColor();  //preference
    QColor getCurrentColor() { return m_colCurrent; }
    void setCurrentColor(QColor color)  {m_colCurrent = color; }
    QColor getSettingColor() { return m_colSetting; }
    void   setSettingColor(QColor color) { m_colSetting = color; }

    virtual void setStack(int z);
    virtual void setStackFromVP();

    static Gui::ViewProvider* getViewProvider(App::DocumentObject* obj);
    static ViewProviderPage* getViewProviderPage(TechDraw::DrawView* dView);

    static int calculateFontPixelSize(double sizeInMillimetres);
    static int calculateFontPixelWidth(const QFont &font);
    static const double DefaultFontSizeInMM;

    static QString getPrefFont();
    static double getPrefFontSize();
    static double getDimFontSize();
    QFont getFont() { return m_font; };
    void setFont(QFont font) { m_font = font; }

    static int exactFontSize(std::string fontFamily, double nominalSize);

    virtual void removeChild(QGIView* child);

    virtual void addArbitraryItem(QGraphicsItem* qgi);

    // Mouse handling
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

    virtual void setPreselect(bool enable=true);

public Q_SLOTS:
    virtual void onSourceChange(TechDraw::DrawView* newParent);

protected:
    QGIView* getQGIVByName(std::string name);

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    virtual void dragFinished();

    // Preselection events:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) final;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) final;
    virtual QRectF customChildrenBoundingRect() const;
    void dumpRect(const char* text, QRectF rect);
    /// Whether a click on the view's empty inside goes through (shape())
    virtual bool passesClicks() const { return false; }
    /// Whether, passing clicks, the view is picked where its content --
    /// its items but the frame, label, caption and lock -- is drawn
    /// (contains()); a part view's are picked themselves
    virtual bool picksByContent() const { return false; }
    /// Forget what contains() found drawn: the content changed
    void dropOpaqueMask() { m_opaqueMask = QImage(); }

    Base::Reference<ParameterGrp> getParmGroupCol();

private:
    TechDraw::DrawView *viewObj;
    std::string viewName;

    QHash<QString, QGraphicsItem*> alignHash;
    bool m_innerView;                                                  //View is inside another View
    bool m_multiselectActivated;

    QPen m_pen;
    QBrush m_brush;
    QColor m_colCurrent;
    QColor m_colNormal;
    QColor m_colPre;
    QColor m_colSel;
    QColor m_colSetting;
    QFont m_font;
    QGCustomLabel* m_label;
    QGCustomBorder* m_border;
    QGICaption* m_caption;
    QGCustomImage* m_lock;
    QPen m_decorPen;
    double m_lockWidth;
    double m_lockHeight;
    int m_zOrder;
    bool m_hasHover = false;

    /// Whether the content is drawn at a point of the view (contains())
    bool opaqueAt(const QPointF& point) const;
    /// What of the content is drawn, painted once and kept until the view
    /// draws again (dropOpaqueMask()), and the map from the view's
    /// coordinates to its pixels
    mutable QImage m_opaqueMask;
    mutable QTransform m_opaqueMap;
    mutable double m_opaqueScale = 1.0;
};

} // namespace

#endif // DRAWINGGUI_QGRAPHICSITEMVIEW_H
