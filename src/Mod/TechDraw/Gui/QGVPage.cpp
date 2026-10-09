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

#include "PreCompiled.h"

#include <Mod/TechDraw/App/TechDrawParams.h>

#include <Gui/ViewParams.h>
#ifndef _PreComp_
#include <cmath>

#include <QApplication>
#include <QBitmap>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#endif

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <Base/Parameter.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/NavigationStyle.h>
#include <Gui/Selection.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>

#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawViewClip.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/DrawSVGTemplate.h>

#include <unordered_map>
#include <vector>

#include <QGraphicsSvgItem>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTextureBlitter>
#include <QOpenGLWidget>
#include <QPaintEngine>
#include <QStyleOptionGraphicsItem>

#include <Gui/Renderer/Page2D.h>
#include <Gui/Renderer/Renderer.h>
#include <Mod/TechDraw/App/Preferences.h>

#include "MDIViewPage.h"
#include "PageFeed.h"
#include "PreferencesGui.h"
#include "QGISVGTemplate.h"
#include "QGIView.h"
#include "QGIViewClip.h"
#include "QGSPage.h"
#include "QGVNavStyleBlender.h"
#include "QGVNavStyleCAD.h"
#include "QGVNavStyleGesture.h"
#include "QGVNavStyleInventor.h"
#include "QGVNavStyleMaya.h"
#include "QGVNavStyleOCC.h"
#include "QGVNavStyleOpenSCAD.h"
#include "QGVNavStyleRevit.h"
#include "QGVNavStyleSolidWorks.h"
#include "QGVNavStyleTinkerCAD.h"
#include "QGVNavStyleTouchpad.h"
#include "QGVPage.h"
#include "Rez.h"
#include "ViewProviderPage.h"


// used SVG namespaces
#define CC_NS_URI "http://creativecommons.org/ns#"
#define DC_NS_URI "http://purl.org/dc/elements/1.1/"
#define RDF_NS_URI "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define INKSCAPE_NS_URI "http://www.inkscape.org/namespaces/inkscape"
#define SODIPODI_NS_URI "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"

/*** pan-style cursor *******/

#define PAN_WIDTH 16
#define PAN_HEIGHT 16
#define PAN_BYTES ((PAN_WIDTH + 7) / 8) * PAN_HEIGHT
#define PAN_HOT_X 7
#define PAN_HOT_Y 7

static unsigned char pan_bitmap[PAN_BYTES] = {
    0xc0, 0x03, 0x60, 0x02, 0x20, 0x04, 0x10, 0x08, 0x68, 0x16, 0x54, 0x2a, 0x73, 0xce, 0x01, 0x80,
    0x01, 0x80, 0x73, 0xce, 0x54, 0x2a, 0x68, 0x16, 0x10, 0x08, 0x20, 0x04, 0x40, 0x02, 0xc0, 0x03};

static unsigned char pan_mask_bitmap[PAN_BYTES] = {
    0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x07, 0xf0, 0x0f, 0xe8, 0x17, 0xdc, 0x3b, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xdc, 0x3b, 0xe8, 0x17, 0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0xc0, 0x03};
/*** zoom-style cursor ******/

#define ZOOM_WIDTH 16
#define ZOOM_HEIGHT 16
#define ZOOM_BYTES ((ZOOM_WIDTH + 7) / 8) * ZOOM_HEIGHT
#define ZOOM_HOT_X 5
#define ZOOM_HOT_Y 7

static unsigned char zoom_bitmap[ZOOM_BYTES] = {
    0x00, 0x0f, 0x80, 0x1c, 0x40, 0x38, 0x20, 0x70, 0x90, 0xe4, 0xc0, 0xcc, 0xf0, 0xfc, 0x00, 0x0c,
    0x00, 0x0c, 0xf0, 0xfc, 0xc0, 0xcc, 0x90, 0xe4, 0x20, 0x70, 0x40, 0x38, 0x80, 0x1c, 0x00, 0x0f};

static unsigned char zoom_mask_bitmap[ZOOM_BYTES] = {
    0x00, 0x0f, 0x80, 0x1f, 0xc0, 0x3f, 0xe0, 0x7f, 0xf0, 0xff, 0xf0, 0xff, 0xf0, 0xff, 0x00, 0x0f,
    0x00, 0x0f, 0xf0, 0xff, 0xf0, 0xff, 0xf0, 0xff, 0xe0, 0x7f, 0xc0, 0x3f, 0x80, 0x1f, 0x00, 0x0f};
using namespace Gui;
using namespace TechDraw;
using namespace TechDrawGui;

class QGVPage::Private: public ParameterGrp::ObserverType
{
public:
    /// handle to the viewer parameter group
    ParameterGrp::handle hGrp;
    /// TechDraw's own General group, for the page renderer switch
    ParameterGrp::handle hGrpGeneral;
    QGVPage* page;
    explicit Private(QGVPage* page) : page(page)
    {
        // attach parameter Observer
        hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
        hGrp->Attach(this);
        hGrpGeneral = Preferences::getPreferenceGroup("General");
        hGrpGeneral->Attach(this);
    }
    void init()
    {
        page->m_atCursor = hGrp->GetBool("ZoomAtCursor", Gui::ViewParams::defaultZoomAtCursor());
        page->m_invertZoom = hGrp->GetBool("InvertZoom", Gui::ViewParams::defaultInvertZoom());
        page->m_zoomIncrement = hGrp->GetFloat("ZoomStep", Gui::ViewParams::defaultZoomStep());

        page->m_reversePan = Preferences::getPreferenceGroup("General")->GetInt("KbPan", TechDraw::TechDrawParams::defaultKbPan());
        page->m_reverseScroll = Preferences::getPreferenceGroup("General")->GetInt("KbScroll", TechDraw::TechDrawParams::defaultKbScroll());
    }
    /// Observer message from the ParameterGrp
    void OnChange(ParameterGrp::SubjectType& rCaller, ParameterGrp::MessageType Reason) override
    {
        const ParameterGrp& rGrp = static_cast<ParameterGrp&>(rCaller);
        if (strcmp(Reason, "PageRendererVg") == 0
            || strcmp(Reason, "PageRendererVgComposite") == 0
            || strcmp(Reason, "PageRendererVgVerify") == 0) {
            // Which renderer draws the page is decided in the
            // background's paint, and the background is cached: without
            // this the switch waits for whatever next invalidates it.
            page->m_vgWarmupTried = false;
            page->resetCachedContent();
            page->viewport()->update();
        }
        else if (strcmp(Reason, "NavigationStyle") == 0) {
            std::string model =
                rGrp.GetASCII("NavigationStyle", Gui::ViewParams::defaultNavigationStyle().c_str());
            page->setNavigationStyle(model);
        }
        else if (strcmp(Reason, "InvertZoom") == 0) {
            page->m_invertZoom = rGrp.GetBool("InvertZoom", Gui::ViewParams::defaultInvertZoom());
        }
        else if (strcmp(Reason, "ZoomStep") == 0) {
            page->m_zoomIncrement = rGrp.GetFloat("ZoomStep", Gui::ViewParams::defaultZoomStep());
        }
        else if (strcmp(Reason, "ZoomAtCursor") == 0) {
            page->m_atCursor = rGrp.GetBool("ZoomAtCursor", Gui::ViewParams::defaultZoomAtCursor());
            if (page->m_atCursor) {
                page->setResizeAnchor(QGVPage::AnchorUnderMouse);
                page->setTransformationAnchor(QGVPage::AnchorUnderMouse);
            }
            else {
                page->setResizeAnchor(QGVPage::AnchorViewCenter);
                page->setTransformationAnchor(QGVPage::AnchorViewCenter);
            }
        }
    }
    void detach()
    {
        hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
        hGrp->Detach(this);
        hGrpGeneral->Detach(this);
    }
};

QGVPage::QGVPage(ViewProviderPage* vpPage, QGSPage* scenePage, QWidget* parent)
    : QGraphicsView(parent), m_renderer(Native), drawBkg(true), m_vpPage(nullptr),
      m_scene(scenePage), balloonPlacing(false), m_showGrid(false),
      m_navStyle(nullptr), d(new Private(this))
{
    assert(vpPage);
    m_vpPage = vpPage;
    const char* name = vpPage->getDrawPage()->getNameInDocument();
    setObjectName(QString::fromLocal8Bit(name));

    setScene(scenePage);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    m_parentMDI = static_cast<MDIViewPage*>(parent);
    m_saveContextEvent = nullptr;

    setCacheMode(QGraphicsView::CacheBackground);
    setRenderer(Native);
    //    setRenderer(OpenGL);  //gives rotten quality, don't use this
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    d->init();
    if (m_atCursor) {
        setResizeAnchor(AnchorUnderMouse);
        setTransformationAnchor(AnchorUnderMouse);
    }
    else {
        setResizeAnchor(AnchorViewCenter);
        setTransformationAnchor(AnchorViewCenter);
    }
    setAlignment(Qt::AlignCenter);

    //    setDragMode(ScrollHandDrag);
    setDragMode(QGraphicsView::NoDrag);
    resetCursor();

    bkgBrush = new QBrush(getBackgroundColor());

    balloonCursor = new QLabel(this);
    balloonCursor->setPixmap(
        prepareCursorPixmap("TechDraw_Balloon.svg", balloonHotspot = QPoint(8, 59)));
    balloonCursor->hide();

    initNavigationStyle();

    createStandardCursors(devicePixelRatio());
}

QGVPage::~QGVPage()
{
    if (m_vgBlitter) {
        // The blitter's GL program lives in the viewport's context;
        // destroying it needs that context current or the destructor
        // leaks with a warning.
        if (auto glvp = qobject_cast<QOpenGLWidget*>(viewport()))
            glvp->makeCurrent();
        delete m_vgBlitter;
    }
    delete bkgBrush;
    delete m_navStyle;
    d->detach();
}

void QGVPage::centerOnPage(void) { centerOn(m_vpPage->getQGSPage()->getTemplateCenter()); }

void QGVPage::initNavigationStyle()
{
    std::string navParm = getNavStyleParameter();
    setNavigationStyle(navParm);
}

void QGVPage::setNavigationStyle(std::string navParm)
{
    //    Base::Console().Message("QGVP::setNavigationStyle(%s)\n", navParm.c_str());
    if (m_navStyle) {
        delete m_navStyle;
    }

    std::size_t foundBlender = navParm.find("Blender");
    std::size_t foundCAD = navParm.find("Gui::CAD");
    std::size_t foundTouchPad = navParm.find("Touchpad");
    std::size_t foundInventor = navParm.find("Inventor");
    std::size_t foundTinker = navParm.find("TinkerCAD");
    std::size_t foundGesture = navParm.find("Gui::Gesture");
    std::size_t foundMaya = navParm.find("Gui::Maya");
    std::size_t foundOCC = navParm.find("OpenCascade");
    std::size_t foundOpenSCAD = navParm.find("OpenSCAD");
    std::size_t foundRevit = navParm.find("Revit");
    std::size_t foundSolidWorks = navParm.find("SolidWorks");

    if (foundBlender != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleBlender(this));
    }
    else if (foundCAD != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleCAD(this));
    }
    else if (foundTouchPad != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleTouchpad(this));
    }
    else if (foundInventor != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleInventor(this));
    }
    else if (foundTinker != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleTinkerCAD(this));
    }
    else if (foundGesture != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleGesture(this));
    }
    else if (foundMaya != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleMaya(this));
    }
    else if (foundOCC != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleOCC(this));
    }
    else if (foundOpenSCAD != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleOpenSCAD(this));
    }
    else if (foundRevit != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleRevit(this));
    }
    else if (foundSolidWorks != std::string::npos) {
        m_navStyle = static_cast<QGVNavStyle*>(new QGVNavStyleSolidWorks(this));
    }
    else {
        m_navStyle = new QGVNavStyle(this);
    }
}

void QGVPage::startBalloonPlacing(DrawView* parent)
{
    //    Base::Console().Message("QGVP::startBalloonPlacing(%s)\n", parent->getNameInDocument());
    balloonPlacing = true;
    m_balloonParent = parent;
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    activateCursor(
        QCursor(balloonCursor->pixmap(Qt::ReturnByValue), balloonHotspot.x(), balloonHotspot.y()));
#else
    activateCursor(QCursor(*balloonCursor->pixmap(), balloonHotspot.x(), balloonHotspot.y()));
#endif
}

void QGVPage::cancelBalloonPlacing()
{
    balloonPlacing = false;
    m_balloonParent = nullptr;
    balloonCursor->hide();
    resetCursor();
}

void QGVPage::drawBackground(QPainter* painter, const QRectF&)
{
    // Settled anew by every paint: whether the backend's layer is the
    // page's picture this time (drawItems asks).
    m_vgDrawn = false;
    m_vgItemsCovered = 0;
    m_vgItemsPainted = 0;

    //Note: Background is not part of scene()
    if (!drawBkg)
        return;

    if (!m_vpPage) {
        return;
    }

    if (!m_vpPage->getDrawPage()) {
        //        Base::Console().Message("QGVP::drawBackground - no Page Feature!\n");
        return;
    }

    painter->save();
    painter->resetTransform();
    // The sheet's outline, said rather than inherited. The cached
    // background is painted into a pixmap, whose painter starts with a
    // black pen and no antialiasing; with the cache off (the backend's
    // page layer needs it off) this is the viewport's painter, whose
    // pen is the palette's text colour and which antialiases -- a dark
    // theme then drew the outline in light grey, a pixel off.
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(Qt::black, 0));

    painter->setBrush(*bkgBrush);
    painter->drawRect(
        viewport()->rect().adjusted(-2, -2, 2, 2));//just bigger than viewport to prevent artifacts

    // Default to A3 landscape, though this is currently relevant
    // only for opening corrupt docs, etc.
    float pageWidth = 420, pageHeight = 297;

    if (m_vpPage->getDrawPage()->hasValidTemplate()) {
        pageWidth = Rez::guiX(m_vpPage->getDrawPage()->getPageWidth());
        pageHeight = Rez::guiX(m_vpPage->getDrawPage()->getPageHeight());
    }

    // Draw the white page
    QRectF paperRect(0, -pageHeight, pageWidth, pageHeight);
    QPolygon poly = mapFromScene(paperRect);

    QBrush pageBrush(PreferencesGui::pageQColor());
    painter->setBrush(pageBrush);

    painter->drawRect(poly.boundingRect());

    painter->restore();

    // The page drawn by the backend: Render::Page2D draws the views
    // and the template, and the scene items of what it drew are not
    // painted after it (drawItems) -- they stay for the mouse.
    // Runtime-gated.
    if (TechDraw::Preferences::getPreferenceGroup("General")
            ->GetBool("PageRendererVg", TechDraw::TechDrawParams::defaultPageRendererVg())) {
        drawVgPreview(painter);
    }
    else if (m_vgPage) {
        // Switched off in a running session: hand the page back to Qt
        // the way the constructor set it up (queued -- this is the
        // viewport's own paint event).
        QMetaObject::invokeMethod(
            this,
            [this]() {
                if (m_vgPage
                    && !TechDraw::Preferences::getPreferenceGroup("General")
                            ->GetBool("PageRendererVg", TechDraw::TechDrawParams::defaultPageRendererVg())) {
                    leaveVgPreview();
                }
            },
            Qt::QueuedConnection);
    }
}

void QGVPage::setVgViewport(bool gl)
{
    auto glvp = qobject_cast<QOpenGLWidget*>(viewport());
    if (gl == (glvp != nullptr))
        return;
    if (glvp && m_vgBlitter) {
        // The blitter's GL program lives in the outgoing viewport's
        // context and goes with it.
        glvp->makeCurrent();
        delete m_vgBlitter;
        m_vgBlitter = nullptr;
        glvp->doneCurrent();
    }
    setRenderer(gl ? OpenGL : Native);
}

void QGVPage::leaveVgPreview()
{
    setVgViewport(false);
    m_vgViews.clear();
    m_vgDirty.clear();
    m_vgState.clear();
    m_vgCovered.clear();
    m_vgTemplateCovered = false;
    m_vgDrawn = false;
    QObject::disconnect(m_vgSceneConnection);
    m_vgPageStructure = 0;
    m_vgTemplateStamp = 0;
    m_vgPage.reset();
    m_vgWarmupTried = false;
    m_vgCompositeLogged = false;
    // every item is Qt's to paint again, the way Qt does it by itself
    setOptimizationFlag(QGraphicsView::IndirectPainting, false);
    setCacheMode(QGraphicsView::CacheBackground);
    viewport()->update();
}

void QGVPage::vgSceneChanged(const QList<QRectF>& region)
{
    if (!m_vgPage)
        return;
    m_vgResolve = true;
    bool any = false;
    for (auto& v : m_vgViews) {
        if (v.second.fedVisible < 0 || m_vgState.count(v.first))
            continue;
        for (const QRectF& rect : region) {
            // a rectangle with no area is still somewhere: the bounds
            // of a horizontal line
            if (v.second.extent.intersects(rect.adjusted(-1, -1, 1, 1))) {
                m_vgState.insert(v.first);
                any = true;
                break;
            }
        }
    }
    if (any)
        viewport()->update();
}

void QGVPage::drawItems(QPainter* painter, int numItems, QGraphicsItem* items[],
                        const QStyleOptionGraphicsItem options[])
{
    // The layer did not reach the page this time (no device, a frame
    // that failed, the first paint after the switch): the Qt items are
    // the page then, all of them.
    //
    // PageRendererVgVerify paints them all over the layer as well, which
    // is how the layer was first looked at: where the two pictures
    // differ, the page shows both.
    if (!m_vgDrawn
        || TechDraw::Preferences::getPreferenceGroup("General")
               ->GetBool("PageRendererVgVerify", TechDraw::TechDrawParams::defaultPageRendererVgVerify())) {
        m_vgItemsPainted = numItems;
        QGraphicsView::drawItems(painter, numItems, items, options);
        return;
    }

    // What the layer holds is not painted a second time: the items of
    // a view it was fed, and the template's picture. The rest is Qt's
    // as before -- a clip group and what is in it, the fields of the
    // template, a tracker, a view the layer has not been fed yet.
    //
    // A top-level item with nothing of the layer's under it goes to
    // Qt whole, children, clipping and all. Under one that is the
    // layer's, an item that is not is painted by itself.
    const QTransform viewTransform = painter->worldTransform();
    std::unordered_map<const QGraphicsItem*, bool> mixedTop;
    std::unordered_map<const QGraphicsItem*, bool> coveredUnder;
    std::vector<QGraphicsItem*> whole;
    std::vector<QStyleOptionGraphicsItem> wholeOptions;

    auto underCoveredView = [this, &coveredUnder](const QGraphicsItem* parent) {
        auto memo = coveredUnder.find(parent);
        if (memo != coveredUnder.end())
            return memo->second;
        bool covered = false;
        for (const QGraphicsItem* p = parent; p; p = p->parentItem()) {
            if (dynamic_cast<const QGIView*>(p)) {
                covered = m_vgCovered.count(p) > 0;
                break;
            }
        }
        coveredUnder.emplace(parent, covered);
        return covered;
    };

    for (int i = 0; i < numItems; ++i) {
        QGraphicsItem* item = items[i];
        const QGraphicsItem* top = item->topLevelItem();
        auto memo = mixedTop.find(top);
        if (memo == mixedTop.end()) {
            const bool mixed = m_vgCovered.count(top) > 0
                || (m_vgTemplateCovered
                    && dynamic_cast<const QGISVGTemplate*>(top));
            memo = mixedTop.emplace(top, mixed).first;
        }
        if (!memo->second) {
            whole.push_back(item);
            wholeOptions.push_back(options[i]);
            ++m_vgItemsPainted;
            continue;
        }
        bool covered = false;
        if (m_vgCovered.count(item)) {
            covered = true;
        }
        else if (item == top) {
            covered = false;  // the template's group
        }
        else if (m_vgCovered.count(top)) {
            covered = underCoveredView(item->parentItem());
        }
        else {
            // under the template: its picture, not its fields
            covered = item->parentItem() == top
                && dynamic_cast<const QGraphicsSvgItem*>(item);
        }
        if (covered) {
            ++m_vgItemsCovered;
            continue;
        }
        ++m_vgItemsPainted;
        painter->save();
        painter->setWorldTransform(item->deviceTransform(viewTransform));
        painter->setOpacity(item->effectiveOpacity());
        item->paint(painter, &options[i], viewport());
        painter->restore();
    }
    if (!whole.empty()) {
        painter->setWorldTransform(viewTransform);
        QGraphicsView::drawItems(painter, (int)whole.size(), whole.data(),
                                 wholeOptions.data());
    }
}

void QGVPage::drawVgPreview(QPainter* painter)
{
    TechDraw::DrawPage* page = getDrawPage();
    if (!page)
        return;

    // The view runs with CacheBackground, which would freeze this
    // layer at its first paint; drop the cache while the preview is
    // active (queued -- this runs inside a paint event). With it, the
    // view takes the painting of the items into its own hands
    // (drawItems is only asked with IndirectPainting), and listens to
    // the scene for what changes in it.
    if (cacheMode() != QGraphicsView::CacheNone
        || !(optimizationFlags() & QGraphicsView::IndirectPainting)
        || !m_vgSceneConnection) {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                if (!m_vgPage)
                    return;
                setCacheMode(QGraphicsView::CacheNone);
                resetCachedContent();
                setOptimizationFlag(QGraphicsView::IndirectPainting, true);
                if (!m_vgSceneConnection && m_scene)
                    m_vgSceneConnection =
                        connect(m_scene, &QGraphicsScene::changed, this,
                                &QGVPage::vgSceneChanged);
                viewport()->update();
            },
            Qt::QueuedConnection);
    }

    if (!m_vgPage)
        m_vgPage = std::make_unique<Render::Page2D>();

    // Structure pass: a cheap ordered hash of the view names. On a
    // change (view added, removed or reordered -- a deleted view's
    // items would survive any re-feed) the tracked set rebuilds and
    // the retained page resets wholesale; that is the rare case. The
    // common case, an edited view, arrives through signalGuiPaint --
    // fired on the GUI thread when HLR lands, when faces land and on
    // repaint-worthy property changes -- and damages only that view.
    // A clip group and the views in it are not the layer's: their X/Y
    // are relative to the group and the layer does not clip. They are
    // left to Qt whole, so being put into a group or taken out of one
    // is a change of structure too.
    auto clipped = [](App::DocumentObject* obj) {
        if (obj->isDerivedFrom<TechDraw::DrawViewClip>())
            return true;
        auto dv = dynamic_cast<TechDraw::DrawView*>(obj);
        return dv && dv->isInClip();
    };
    size_t structure = 0;
    for (App::DocumentObject* obj : page->getAllViews()) {
        if (const char* name = obj->getNameInDocument())
            structure = (structure * 31 + std::hash<std::string> {}(name)) * 2
                + (clipped(obj) ? 1 : 0);
    }
    if (structure != m_vgPageStructure) {
        m_vgPageStructure = structure;
        m_vgPage->clear(); // drops the image registry too
        m_vgTemplateStamp = 0;
        m_vgViews.clear();
        m_vgDirty.clear();
        m_vgState.clear();
        uint32_t layer = 1; // layer 0 is the template's
        for (App::DocumentObject* obj : page->getAllViews()) {
            auto dv = dynamic_cast<TechDraw::DrawView*>(obj);
            if (dv && dv->getNameInDocument() && !clipped(obj)) {
                std::string name = dv->getNameInDocument();
                VgViewTrack& track = m_vgViews[name];
                track.layer = layer;
                track.repaint = dv->signalGuiPaint.connect(
                    [this, name](const TechDraw::DrawView*) {
                        m_vgDirty.insert(name);
                        viewport()->update();
                    });
                m_vgDirty.insert(name);
            }
            ++layer;
        }
        m_vgResolve = true;
    }
    else {
        // X/Y moves purge their touch on the App side and signal
        // nothing -- and a Visibility toggle signals nothing either;
        // catch both by comparing against what was last fed.
        for (auto& v : m_vgViews) {
            if (m_vgDirty.count(v.first) || v.second.clipped)
                continue;
            auto dv = dynamic_cast<TechDraw::DrawView*>(
                page->getDocument()->getObject(v.first.c_str()));
            if (!dv)
                continue;
            auto vpd = dynamic_cast<Gui::ViewProviderDocumentObject*>(
                Gui::Application::Instance->getViewProvider(dv));
            const int8_t visNow = !vpd || vpd->isShow() ? 1 : 0;
            double pageX = 0.0, pageY = 0.0;
            PageFeed::pagePosition(dv, pageX, pageY);
            if ((float)pageX != v.second.fedX
                || (float)pageY != v.second.fedY
                || visNow != v.second.fedVisible)
                m_vgDirty.insert(v.first);
            else if (v.second.qgiv
                     && v.second.qgiv->scenePos() != v.second.fedScenePos)
                // carried along by the view it sits on: what was read
                // off its Qt items is where they were
                m_vgState.insert(v.first);
        }
    }
    // The Qt item of each view, looked for when the scene may have a
    // new one: after a change of structure, and after any change of
    // the scene while one is missing (not made yet, or gone -- a page
    // redrawn makes new ones; some views never have one). One pass
    // over the scene for all of them. A view whose item turns up is
    // read again: nothing of it was, or what was came from an item
    // that is gone.
    if (m_vgResolve && m_scene) {
        m_vgResolve = false;
        bool missing = false;
        for (auto& v : m_vgViews)
            missing = missing || !v.second.qgiv;
        if (missing) {
            std::map<std::string, QGIView*> found;
            for (QGIView* qv : m_scene->getViews())
                found.emplace(qv->getViewNameAsString(), qv);
            for (auto& v : m_vgViews) {
                if (v.second.qgiv)
                    continue;
                auto it = found.find(v.first);
                if (it == found.end())
                    continue;
                v.second.qgiv = it->second;
                v.second.clipped = false;
                for (QGraphicsItem* p = it->second->parentItem(); p;
                     p = p->parentItem()) {
                    if (dynamic_cast<QGIViewClip*>(p)) {
                        v.second.clipped = true;
                        break;
                    }
                }
                if (v.second.fedVisible >= 0)
                    m_vgState.insert(v.first);
            }
        }
    }

    // Feed exactly the damaged views; stable item ids confine each
    // feed to its own items. A view still computing on its worker gets
    // fed with what it has -- completion signals more damage, so there
    // is no per-paint re-feed while pending.
    if (!m_vgDirty.empty()) {
        std::set<std::string> dirty;
        dirty.swap(m_vgDirty);
        for (const std::string& name : dirty) {
            auto it = m_vgViews.find(name);
            if (it == m_vgViews.end() || it->second.clipped)
                continue;
            auto dv = dynamic_cast<TechDraw::DrawView*>(
                page->getDocument()->getObject(name.c_str()));
            if (!dv)
                continue;
            QGIView* qgiv = it->second.qgiv;
            if (auto dvp = dynamic_cast<TechDraw::DrawViewPart*>(dv)) {
                PageFeed::feedViewPart(dvp, *m_vgPage, PageFeed::Style(),
                                       it->second.layer);
            }
            // What is read off the Qt items: all of an annotation (the
            // capture converts the QGI subtree the Qt tier has already
            // laid out by the time this paint runs); of a part view,
            // its decorations and frame and what is preselected or
            // selected in it.
            if (qgiv) {
                PageFeed::feedViewState(qgiv, *m_vgPage, it->second.layer,
                                        0.0f);
                it->second.fedScenePos = qgiv->scenePos();
                it->second.extent = PageFeed::sceneExtent(qgiv);
            }
            m_vgState.erase(name);
            ++m_vgViewFeeds;
            double pageX = 0.0, pageY = 0.0;
            PageFeed::pagePosition(dv, pageX, pageY);
            it->second.fedX = (float)pageX;
            it->second.fedY = (float)pageY;
            auto vpd = dynamic_cast<Gui::ViewProviderDocumentObject*>(
                Gui::Application::Instance->getViewProvider(dv));
            it->second.fedVisible = !vpd || vpd->isShow() ? 1 : 0;
        }
    }

    // The views whose Qt items changed and nothing else: their state.
    if (!m_vgState.empty()) {
        std::set<std::string> state;
        state.swap(m_vgState);
        for (const std::string& name : state) {
            auto it = m_vgViews.find(name);
            if (it == m_vgViews.end() || !it->second.qgiv
                || it->second.clipped || it->second.fedVisible < 0)
                continue;
            QGIView* qgiv = it->second.qgiv;
            PageFeed::feedViewState(qgiv, *m_vgPage, it->second.layer, 0.0f);
            it->second.fedScenePos = qgiv->scenePos();
            it->second.extent = PageFeed::sceneExtent(qgiv);
            ++m_vgViewFeeds;
        }
    }

    // What drawItems leaves unpainted: the Qt items of the views the
    // layer holds now.
    m_vgCovered.clear();
    for (auto& v : m_vgViews) {
        if (v.second.fedVisible >= 0 && v.second.qgiv)
            m_vgCovered.insert(v.second.qgiv.data());
    }

    // The template: rasterized SVG, re-fed when its content changes
    // (template swap, editable text edit) or when the zoom crosses a
    // band -- the raster tracks the band scale so the sheet stays
    // sharp. The stamp folds all of that into one comparison.
    {
        const float band =
            Render::Page2D::bandScale((float)transform().m11());
        size_t stamp = std::hash<float> {}(band);
        if (App::DocumentObject* tmpl = page->Template.getValue()) {
            if (const char* tname = tmpl->getNameInDocument())
                stamp = stamp * 31 + std::hash<std::string> {}(tname);
            if (auto svgt = dynamic_cast<TechDraw::DrawSVGTemplate*>(tmpl)) {
                for (const auto& kv : svgt->EditableTexts.getValues())
                    stamp = stamp * 31 + std::hash<std::string> {}(kv.second);
            }
        }
        if (stamp != m_vgTemplateStamp) {
            m_vgTemplateStamp = stamp;
            PageFeed::feedTemplate(page, *m_vgPage, band, /*everyBand*/ true);
        }
        m_vgTemplateCovered = PageFeed::hasTemplate(*m_vgPage);
    }

    // Map the QGraphicsView transform onto the page view: uniform
    // scale, scene origin in viewport coordinates as the pan.
    const QPointF origin = mapFromScene(QPointF(0.0, 0.0));
    Render::Page2D::View view;
    view.zoom = (float)transform().m11();
    view.panX = (float)origin.x();
    view.panY = (float)origin.y();

    // The real compositor: with a GL viewport whose context shares
    // with the bgfx device, the page renders into a persistent texture
    // and a textured blit puts it under the scene items -- no
    // readback, no QImage. Everything below degrades to the readback
    // path when a piece is missing (non-GL viewport on the first
    // paint, a Vulkan device, no device at all and warmup refused).
    const bool wantComposite =
        TechDraw::Preferences::getPreferenceGroup("General")
            ->GetBool("PageRendererVgComposite", TechDraw::TechDrawParams::defaultPageRendererVgComposite());
    // Whether it CAN engage is settled before the viewport is touched.
    // The backend's device is one per process, so where the session
    // runs on anything but OpenGL (Direct3D 11 is the Windows default)
    // the answer is no for good, and the page keeps its raster
    // viewport and takes the readback path below: a GL viewport would
    // buy nothing there and costs the Qt items their quality. It used
    // to be asked the other way round -- GL viewport first, then a
    // warm-up from inside its first paint -- and on a Direct3D session
    // that warm-up left the painter without a GL context, which Qt
    // dereferenced.
    bool canComposite =
        wantComposite && Render::RendererFactory::deviceSharesQtGL();
    if (wantComposite && !canComposite && !m_vgWarmupTried) {
        // Once per page host -- a refusal will not change.
        m_vgWarmupTried = true;
        // Only a session with no device at all is warmed up, on GL,
        // which is what sharing needs. The main window's own warm-up
        // surface supplies the format, as it does at startup: the
        // backend keeps a view keyed by the widget it is handed, and
        // this viewport does not outlive the page.
        auto warm = Render::RendererFactory::drawDevice()
            ? nullptr
            : Gui::getMainWindow()->findChild<QOpenGLWidget*>(
                  QStringLiteral("GLSurfaceWarmup"));
        if (warm && Render::RendererFactory::warmup("bgfx - OpenGL", warm))
            canComposite = Render::RendererFactory::deviceSharesQtGL();
    }
    auto glvp = qobject_cast<QOpenGLWidget*>(viewport());
    if (canComposite != (glvp != nullptr)) {
        // Switch the viewport for the next paint -- to GL, or back
        // when the composite was switched off; this one is the old
        // viewport's own paint event, so it cannot be destroyed from
        // here.
        QMetaObject::invokeMethod(
            this,
            [this, canComposite]() {
                setVgViewport(canComposite);
                setCacheMode(QGraphicsView::CacheNone);
                viewport()->update();
            },
            Qt::QueuedConnection);
    }
    if (canComposite && glvp
        && painter->paintEngine()->type() == QPaintEngine::OpenGL2) {
        if (Render::RendererFactory::deviceSharesQtGL()) {
            const qreal dpr = glvp->devicePixelRatioF();
            Render::Page2D::View pv = view;
            pv.zoom *= (float)dpr;
            pv.panX *= (float)dpr;
            pv.panY *= (float)dpr;
            pv.devicePixelRatio = (float)dpr;
            m_vgPage->setView(pv);
            const int pw = (int)std::lround(viewport()->width() * dpr);
            const int ph = (int)std::lround(viewport()->height() * dpr);

            painter->beginNativePainting();
            // The device frame runs under the device's own context;
            // hand the widget's back first (its makeCurrent() also
            // re-binds the widget framebuffer afterwards).
            glvp->doneCurrent();
            const uintptr_t tex =
                m_vgPage->renderToTexture((uint16_t)pw, (uint16_t)ph);
            glvp->makeCurrent();
            if (tex) {
                auto* f = QOpenGLContext::currentContext()->functions();
                // The QPainter GL engine leaves its clip machinery on;
                // this quad is the whole viewport.
                f->glDisable(GL_SCISSOR_TEST);
                f->glDisable(GL_STENCIL_TEST);
                f->glDisable(GL_DEPTH_TEST);
                f->glViewport(0, 0, pw, ph);
                // vg blended onto a transparent clear = premultiplied.
                f->glEnable(GL_BLEND);
                f->glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                                       GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                if (!m_vgBlitter)
                    m_vgBlitter = new QOpenGLTextureBlitter;
                if (!m_vgBlitter->isCreated())
                    m_vgBlitter->create();
                const QRect all(0, 0, pw, ph);
                m_vgBlitter->bind();
                m_vgBlitter->blit(
                    (GLuint)tex,
                    QOpenGLTextureBlitter::targetTransform(all, all),
                    QOpenGLTextureBlitter::OriginBottomLeft);
                m_vgBlitter->release();
                f->glDisable(GL_BLEND);
                m_vgDrawn = true;
            }
            painter->endNativePainting();
            if (tex && !m_vgCompositeLogged) {
                m_vgCompositeLogged = true;
                Base::Console().Log(
                    "QGVPage: vg compositor active (shared-GL texture)\n");
            }
            // With a shared GL device the readback fallback is not an
            // option from inside this paint -- renderOffscreen would
            // pump the device's frame into the widget's context. A
            // failed frame skips; the next paint retries.
            return;
        }
    }

    // The readback path: what a session whose device is not OpenGL
    // gets on every paint. The same layer the compositor blits -- device
    // pixels, transparent where the page draws nothing, so the sheet
    // and the backdrop painted above stay.
    const qreal dpr = viewport()->devicePixelRatioF();
    view.zoom *= (float)dpr;
    view.panX *= (float)dpr;
    view.panY *= (float)dpr;
    view.devicePixelRatio = (float)dpr;
    m_vgPage->setView(view);
    const int width = (int)std::lround(viewport()->width() * dpr);
    const int height = (int)std::lround(viewport()->height() * dpr);
    std::vector<uint8_t> rgba;
    if (!m_vgPage->renderOffscreen((uint16_t)width, (uint16_t)height, rgba,
                                   true)) {
        return;
    }

    QImage image(rgba.data(), width, height, width * 4,
                 QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(dpr);
    painter->save();
    painter->resetTransform();
    painter->drawImage(0, 0, image);
    painter->restore();
    m_vgDrawn = true;
}

void QGVPage::setRenderer(RendererType type)
{
    m_renderer = type;

    if (m_renderer == OpenGL) {
#ifndef QT_NO_OPENGL
        setViewport(new QOpenGLWidget);
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
#endif
    }
    else {
        setViewport(new QWidget);
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }
}

void QGVPage::setHighQualityAntialiasing(bool highQualityAntialiasing)
{
#ifndef QT_NO_OPENGL
    setRenderHint(QPainter::Antialiasing, highQualityAntialiasing);
#else
    Q_UNUSED(highQualityAntialiasing);
#endif
}

void QGVPage::paintEvent(QPaintEvent* event)
{
    if (m_renderer == Image) {
        if (m_image.size() != viewport()->size()) {
            m_image = QImage(viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        }

        QPainter imagePainter(&m_image);
        QGraphicsView::render(&imagePainter);
        imagePainter.end();

        QPainter p(viewport());
        p.drawImage(0, 0, m_image);
    }
    else {
        QGraphicsView::paintEvent(event);
    }
}

void QGVPage::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_navStyle->allowContextMenu(event)) {
        QGraphicsView::contextMenuEvent(
            event);//this eats the event. mouseReleaseEvent will not be called.
        return;
    }

    //delete the old saved event before creating a new one to avoid memory leak
    //NOTE: saving the actual event doesn't work as the event gets deleted somewhere in Qt
    if (m_saveContextEvent) {
        delete m_saveContextEvent;
    }
    m_saveContextEvent =
        new QContextMenuEvent(QContextMenuEvent::Mouse, event->pos(), event->globalPos());
}

void QGVPage::pseudoContextEvent()
{
    if (m_saveContextEvent) {
        m_parentMDI->contextMenuEvent(m_saveContextEvent);
    }
}

void QGVPage::wheelEvent(QWheelEvent* event)
{
    m_navStyle->handleWheelEvent(event);
    event->accept();
}

void QGVPage::keyPressEvent(QKeyEvent* event)
{
    m_navStyle->handleKeyPressEvent(event);
    if (!event->isAccepted()) {
        QGraphicsView::keyPressEvent(event);
    }
}

void QGVPage::keyReleaseEvent(QKeyEvent* event)
{
    m_navStyle->handleKeyReleaseEvent(event);
    if (!event->isAccepted()) {
        QGraphicsView::keyReleaseEvent(event);
    }
}

void QGVPage::focusOutEvent(QFocusEvent* event)
{
    Q_UNUSED(event);
    m_navStyle->handleFocusOutEvent(event);
}

void QGVPage::kbPanScroll(int xMove, int yMove)
{
    if (xMove != 0) {
        QScrollBar* hsb = horizontalScrollBar();
        //        int hRange = hsb->maximum() - hsb->minimum();     //default here is 100?
        //        int hDelta = xMove/hRange
        int hStep = hsb->singleStep() * xMove * m_reversePan;
        int hNow = hsb->value();
        hsb->setValue(hNow + hStep);
    }
    if (yMove != 0) {
        QScrollBar* vsb = verticalScrollBar();
        int vStep = vsb->singleStep() * yMove * m_reverseScroll;
        int vNow = vsb->value();
        vsb->setValue(vNow + vStep);
    }
}

#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
void QGVPage::enterEvent(QEvent* event)
#else
void QGVPage::enterEvent(QEnterEvent* event)
#endif
{
    QGraphicsView::enterEvent(event);
    m_navStyle->handleEnterEvent(event);
    QGraphicsView::enterEvent(event);
}

void QGVPage::leaveEvent(QEvent* event)
{
    m_navStyle->handleLeaveEvent(event);
    QGraphicsView::leaveEvent(event);
}

void QGVPage::mousePressEvent(QMouseEvent* event)
{
    m_navStyle->handleMousePressEvent(event);
    QGraphicsView::mousePressEvent(event);
}

void QGVPage::mouseMoveEvent(QMouseEvent* event)
{
    m_navStyle->handleMouseMoveEvent(event);
    QGraphicsView::mouseMoveEvent(event);
}

void QGVPage::mouseReleaseEvent(QMouseEvent* event)
{
    m_navStyle->handleMouseReleaseEvent(event);
    QGraphicsView::mouseReleaseEvent(event);
    resetCursor();
}

TechDraw::DrawPage* QGVPage::getDrawPage() { return m_vpPage->getDrawPage(); }

QColor QGVPage::getBackgroundColor()
{
    App::Color fcColor;
    fcColor.setPackedValue(Preferences::getPreferenceGroup("Colors")->GetUnsigned("Background", TechDraw::TechDrawParams::defaultBackground()));
    return fcColor.asValue<QColor>();
}

double QGVPage::getDevicePixelRatio() const
{
    for (Gui::MDIView* view : m_vpPage->getDocument()->getMDIViews()) {
        if (view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
            return static_cast<Gui::View3DInventor*>(view)->getViewer()->devicePixelRatio();
        }
    }

    return 1.0;
}

QPixmap QGVPage::prepareCursorPixmap(const char* iconName, QPoint& hotspot)
{

    QPointF floatHotspot(hotspot);
    double pixelRatio = getDevicePixelRatio();

    // Due to impossibility to query cursor size via Qt API, we stick to (32x32)*device_pixel_ratio
    // as FreeCAD Wiki suggests - see https://wiki.freecad.org/HiDPI_support#Custom_cursor_size
    double cursorSize = 32.0 * pixelRatio;

    QPixmap pixmap = Gui::BitmapFactory().pixmapFromSvg(iconName, QSizeF(cursorSize, cursorSize));
    pixmap.setDevicePixelRatio(pixelRatio);

    // The default (and here expected) SVG cursor graphics size is 64x64 pixels, thus we must adjust
    // the 64x64 based hotspot position for our 32x32 based cursor pixmaps accordingly
    floatHotspot *= 0.5;

#if !defined(Q_OS_WIN32) && !defined(Q_OS_MAC)
    // On XCB platform, the pixmap device pixel ratio is not taken into account for cursor hot spot,
    // therefore we must take care of the transformation ourselves...
    // Refer to QTBUG-68571 - https://bugreports.qt.io/browse/QTBUG-68571
    if (qGuiApp->platformName() == QStringLiteral("xcb")) {
        floatHotspot *= pixelRatio;
    }
#endif

    hotspot = floatHotspot.toPoint();
    return pixmap;
}

void QGVPage::activateCursor(QCursor cursor)
{
    this->setCursor(cursor);
    viewport()->setCursor(cursor);
}

void QGVPage::resetCursor()
{
    this->setCursor(Qt::ArrowCursor);
    viewport()->setCursor(Qt::ArrowCursor);
}

void QGVPage::setPanCursor() { activateCursor(panCursor); }

void QGVPage::setZoomCursor() { activateCursor(zoomCursor); }

void QGVPage::zoomIn()
{
    m_navStyle->zoomIn();
}

void QGVPage::zoomOut()
{
    m_navStyle->zoomOut();
}

void QGVPage::drawForeground(QPainter* painter, const QRectF& rect)
{
    Q_UNUSED(rect);
    if (m_showGrid) {
        QPen gridPen(PreferencesGui::gridQColor());
        QPen savePen = painter->pen();
        painter->setPen(gridPen);
        painter->drawPath(m_gridPath);
        painter->setPen(savePen);
    }
}

void QGVPage::makeGrid(int gridWidth, int gridHeight, double gridStep)
{
    QPainterPath grid;
    double width = Rez::guiX(gridWidth);
    double height = Rez::guiX(gridHeight);
    double step = Rez::guiX(gridStep);
    double horizStart = 0.0;
    double vPos = 0;
    int rows = (height / step) + 1;
    //draw horizontal lines
    for (int i = 0; i < rows; i++) {
        vPos = i * step;
        QPointF start(horizStart, -vPos);
        QPointF end(width, -vPos);
        grid.moveTo(start);
        grid.lineTo(end);
    }
    //draw vertical lines
    double vertStart = 0.0;
    double hPos = 0.0;
    int cols = (width / step) + 1;
    for (int i = 0; i < cols; i++) {
        hPos = i * step;
        QPointF start(hPos, -vertStart);
        QPointF end(hPos, -height);
        grid.moveTo(start);
        grid.lineTo(end);
    }
    m_gridPath = grid;
}


std::string QGVPage::getNavStyleParameter()
{
    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    std::string model =
        hGrp->GetASCII("NavigationStyle", Gui::ViewParams::defaultNavigationStyle().c_str());
    return model;
}

Base::Type QGVPage::getStyleType(std::string model)
{
    Base::Type type = Base::Type::fromName(model.c_str());
    return type;
}

void QGVPage::createStandardCursors(double dpr)
{
    (void)dpr;//avoid clang warning re unused parameter
    QBitmap cursor = QBitmap::fromData(QSize(PAN_WIDTH, PAN_HEIGHT), pan_bitmap);
    QBitmap mask = QBitmap::fromData(QSize(PAN_WIDTH, PAN_HEIGHT), pan_mask_bitmap);
#if defined(Q_OS_WIN32)
    cursor.setDevicePixelRatio(dpr);
    mask.setDevicePixelRatio(dpr);
#endif
    panCursor = QCursor(cursor, mask, PAN_HOT_X, PAN_HOT_Y);

    cursor = QBitmap::fromData(QSize(ZOOM_WIDTH, ZOOM_HEIGHT), zoom_bitmap);
    mask = QBitmap::fromData(QSize(ZOOM_WIDTH, ZOOM_HEIGHT), zoom_mask_bitmap);
#if defined(Q_OS_WIN32)
    cursor.setDevicePixelRatio(dpr);
    mask.setDevicePixelRatio(dpr);
#endif
    zoomCursor = QCursor(cursor, mask, ZOOM_HOT_X, ZOOM_HOT_Y);
}

#include <Mod/TechDraw/Gui/moc_QGVPage.cpp>
