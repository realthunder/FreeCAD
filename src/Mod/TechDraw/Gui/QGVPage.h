/***************************************************************************
 *   Copyright (c) 2013 Luke Parry <l.parry@warwick.ac.uk>                 *
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

#ifndef TECHDRAWGUI_QGVIEW_H
#define TECHDRAWGUI_QGVIEW_H

#include <Mod/TechDraw/TechDrawGlobal.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

#include <QGraphicsView>
#include <QPointer>

#include <memory>
#include <QLabel>
#include <QPainterPath>

#include <fastsignals/signal.h>

#include <Base/Type.h>

QT_BEGIN_NAMESPACE
class QOpenGLTextureBlitter;
QT_END_NAMESPACE

namespace App
{
class DocumentObject;
}

namespace TechDraw
{
class DrawView;
class DrawViewPart;
class DrawProjGroup;
class DrawViewDimension;
class DrawPage;
class DrawTemplate;
class DrawViewAnnotation;
class DrawViewSymbol;
class DrawViewClip;
class DrawViewCollection;
class DrawViewSpreadsheet;
class DrawViewImage;
class DrawLeaderLine;
class DrawViewBalloon;
class DrawRichAnno;
class DrawWeldSymbol;
}// namespace TechDraw

namespace Render
{
class Page2D;
}

namespace TechDrawGui
{
class MDIViewPage;
class QGSPage;
class QGIView;
class QGIViewDimension;
class QGITemplate;
class ViewProviderPage;
class QGIViewBalloon;
class QGILeaderLine;
class QGIRichAnno;
class QGITile;
class QGVNavStyle;

class TechDrawGuiExport QGVPage: public QGraphicsView
{
    Q_OBJECT
    /// What the last paint did with the scene's items while the backend
    /// draws the page: how many it left to the backend's layer, and how
    /// many Qt painted all the same (the ones the layer does not hold).
    /// Both 0 while Qt draws the page. For tests and for diagnosis.
    Q_PROPERTY(int vgItemsCovered READ vgItemsCovered)
    Q_PROPERTY(int vgItemsPainted READ vgItemsPainted)
    /// how many views the layer was fed again, in full or their state
    /// only, since the page view was made
    Q_PROPERTY(int vgViewFeeds READ vgViewFeeds)

public:
    enum RendererType
    {
        Native,
        OpenGL,
        Image
    };

    QGVPage(ViewProviderPage* vpPage, QGSPage* scenePage, QWidget* parent = nullptr);
    ~QGVPage() override;

    void setRenderer(RendererType type = Native);
    void drawBackground(QPainter* painter, const QRectF& rect) override;

    /// Mark the vg page preview stale; the next repaint rebuilds the
    /// tracked view set and re-feeds everything.
    void invalidateVgPage() { m_vgPageStructure = 0; }
    int vgItemsCovered() const { return m_vgItemsCovered; }
    int vgItemsPainted() const { return m_vgItemsPainted; }
    int vgViewFeeds() const { return m_vgViewFeeds; }

    QGSPage* getScene() { return m_scene; }

    void startBalloonPlacing(TechDraw::DrawView* parent);
    void cancelBalloonPlacing();

    TechDraw::DrawPage* getDrawPage();

    void makeGrid(int width, int height, double step);
    void showGrid(bool state) { m_showGrid = state; }
    void updateViewport() { viewport()->repaint(); }

    bool isBalloonPlacing() const { return balloonPlacing; }
    void setBalloonPlacing(bool isPlacing) { balloonPlacing = isPlacing; }

    QLabel* getBalloonCursor() const { return balloonCursor; }
    void setBalloonCursor(QLabel* label) { balloonCursor = label; }

    void kbPanScroll(int xMove = 1, int yMove = 1);
    QPointF getBalloonCursorPos() const { return balloonCursorPos; }
    void setBalloonCursorPos(QPoint pos) { balloonCursorPos = pos; }

    void activateCursor(QCursor cursor);
    void resetCursor();
    void setPanCursor();
    void setZoomCursor();

    void pseudoContextEvent();

    void centerOnPage();

    TechDraw::DrawView* getBalloonParent() { return m_balloonParent; }

    void zoomIn();
    void zoomOut();

public Q_SLOTS:
    void setHighQualityAntialiasing(bool highQualityAntialiasing);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    void enterEvent(QEvent* event) override;
#else
    void enterEvent(QEnterEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    QColor getBackgroundColor();

    double getDevicePixelRatio() const;
    QPixmap prepareCursorPixmap(const char* iconName, QPoint& hotspot);

    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void drawItems(QPainter* painter, int numItems, QGraphicsItem* items[],
                   const QStyleOptionGraphicsItem options[]) override;

    std::string getNavStyleParameter();
    Base::Type getStyleType(std::string model);

    void initNavigationStyle();
    void setNavigationStyle(std::string navParm);

    void createStandardCursors(double dpr);

private:
    RendererType m_renderer;

    bool drawBkg;
    QBrush* bkgBrush;
    QImage m_image;
    ViewProviderPage* m_vpPage;

    bool m_atCursor;
    bool m_invertZoom;
    double m_zoomIncrement;
    int m_reversePan;
    int m_reverseScroll;

    QGSPage* m_scene;
    bool balloonPlacing;
    QLabel* balloonCursor;
    QPoint balloonCursorPos;
    QPoint balloonHotspot;
    TechDraw::DrawView* m_balloonParent;//temp field. used during balloon placing.

    QPoint panOrigin;

    bool m_showGrid;
    QPainterPath m_gridPath;

    QGVNavStyle* m_navStyle;

    class Private;
    std::unique_ptr<Private> d;

    QCursor panCursor;
    QCursor zoomCursor;

    MDIViewPage* m_parentMDI;
    QContextMenuEvent* m_saveContextEvent;

    // The page drawn by the backend (docs/TechDrawPortAndSection.md
    // sec 16 and 37): parameter-gated. The layer is the page's picture;
    // the Qt items of what it holds stay in the scene for the mouse and
    // are not painted (drawItems).
    // Damage-driven: each tracked view's signalGuiPaint (HLR done,
    // faces done, any repaint-worthy property change) marks only that
    // view dirty; a paint re-feeds exactly the dirty views. X/Y carry
    // no signal (the App side purges their touch), so the track caches
    // the fed position and a paint-time compare catches moves.
    // What a view shows of its state -- preselected, selected, a label
    // being dragged -- changes Qt items only: the scene reports where
    // (QGraphicsScene::changed), and the views that read from there
    // are fed their state again (m_vgState), which computes no
    // geometry.
    struct VgViewTrack
    {
        uint32_t layer = 0;
        float fedX = 0.0f;
        float fedY = 0.0f;
        // Visibility carries no signal either; -1 = not fed yet.
        int8_t fedVisible = -1;
        fastsignals::scoped_connection repaint;
        // The view's Qt item, found again when it went away (a page
        // redrawn makes new ones), and where it stood in the scene when
        // it was last read: an item under a view that moves is carried
        // along with no X/Y of its own changing.
        QPointer<QGIView> qgiv;
        QPointF fedScenePos;
        // PageFeed::sceneExtent as of the last feed
        QRectF extent;
        // Its Qt item sits in a clip group, whatever the document says
        // of the view itself (a dimension of a view that is in one):
        // Qt's with the group, never fed.
        bool clipped = false;
    };
    void drawVgPreview(QPainter* painter);
    /// The scene changed there: mark the views that read from there.
    void vgSceneChanged(const QList<QRectF>& region);
    /// Swap the viewport between the Qt raster one and a QOpenGLWidget
    /// (the composite's). Never from inside a paint.
    void setVgViewport(bool gl);
    /// The preview switched off in a running session: drop its state
    /// and give the page back to Qt as the constructor set it up.
    void leaveVgPreview();
    std::unique_ptr<Render::Page2D> m_vgPage;
    std::map<std::string, VgViewTrack> m_vgViews;
    std::set<std::string> m_vgDirty;
    // views to feed their state again, nothing else
    std::set<std::string> m_vgState;
    // The Qt items of the views the layer holds, as of this paint:
    // what drawItems leaves unpainted, with everything under them.
    // Compared, never dereferenced.
    std::unordered_set<const QGraphicsItem*> m_vgCovered;
    bool m_vgTemplateCovered = false;
    // The layer reached the page in the paint under way. Until it
    // does, the Qt items are the page and all of them are painted.
    bool m_vgDrawn = false;
    // look for the Qt items of the views that have none (drawVgPreview)
    bool m_vgResolve = true;
    QMetaObject::Connection m_vgSceneConnection;
    int m_vgItemsCovered = 0;
    int m_vgItemsPainted = 0;
    int m_vgViewFeeds = 0;
    size_t m_vgPageStructure = 0;
    size_t m_vgTemplateStamp = 0;
    // The GL compositor's state: the blitter lives in the viewport's
    // context, warmup is attempted once, the active log line printed
    // once.
    QOpenGLTextureBlitter* m_vgBlitter = nullptr;
    bool m_vgWarmupTried = false;
    bool m_vgCompositeLogged = false;
};

}// namespace TechDrawGui

#endif// TECHDRAWGUI_QGVIEW_H
