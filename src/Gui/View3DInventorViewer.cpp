/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <cfloat>
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# include <GL/glext.h>
# include <GL/glu.h>
# endif

# include <Inventor/SbBox.h>
# include <Inventor/SoEventManager.h>
# include <Inventor/SoPickedPoint.h>
# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/actions/SoGetMatrixAction.h>
# include <Inventor/actions/SoGetPrimitiveCountAction.h>
# include <Inventor/actions/SoHandleEventAction.h>
# include <Inventor/actions/SoRayPickAction.h>
# include <Inventor/annex/HardCopy/SoVectorizePSAction.h>
# include <Inventor/details/SoDetail.h>
# include <Inventor/elements/SoLightModelElement.h>
# include <Inventor/elements/SoOverrideElement.h>
# include <Inventor/elements/SoViewportRegionElement.h>
# include <Inventor/errors/SoDebugError.h>
# include <Inventor/events/SoEvent.h>
# include <Inventor/events/SoKeyboardEvent.h>
# include <Inventor/events/SoMotion3Event.h>
# include <Inventor/manips/SoClipPlaneManip.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoCallback.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoDirectionalLight.h>
# include <Inventor/nodes/SoEventCallback.h>
# include <Inventor/nodes/SoFaceSet.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
# include <Inventor/nodes/SoPolygonOffset.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSelection.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoTransform.h>
# include <Inventor/nodes/SoTranslation.h>
# include <Inventor/events/SoMouseButtonEvent.h>
# include <Inventor/nodes/SoTexture2.h>
# include <QApplication>
# include <QBitmap>
# include <QEventLoop>
# include <QKeyEvent>
# include <QMessageBox>
# include <QMimeData>
# include <QFileInfo>
# include <QTimer>
# include <QVariantAnimation>
# include <QWheelEvent>
#endif

#include <Inventor/SbImage.h>
#include <Inventor/sensors/SoTimerSensor.h>
#include <Inventor/SoEventManager.h>
#include <Inventor/annex/FXViz/nodes/SoShadowGroup.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoTextureUnit.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/manips/SoDirectionalLightManip.h>
#include <Inventor/manips/SoSpotLightManip.h>
#include <Inventor/draggers/SoDirectionalLightDragger.h>

#include <QOpenGLWidget>
#include <QGesture>

#include <boost/algorithm/string/predicate.hpp>
#include <sstream>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Sequencer.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/PropertyUnits.h>
#include <App/PropertyFile.h>
#include <App/ComplexGeoDataPy.h>
#include <App/Document.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Quarter/devices/InputDevice.h>
#include <Quarter/eventhandlers/EventFilter.h>

#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "Application.h"
#include "CornerCrossLetters.h"
#include "Document.h"
#include "GLPainter.h"
#include "MainWindow.h"
#include "NaviCube.h"
#include "NavigationStyle.h"
#include "Selection.h"
#include "SoAxisCrossKit.h"
#include "SoFCBackgroundGradient.h"
#include "SoFCBoundingBox.h"
#include "SoFCDB.h"
#include "SoFCInteractiveElement.h"
#include "SoFCOffscreenRenderer.h"
#include "SoFCSelection.h"
#include "Inventor/SoFCDisplayMode.h"
#include "SoFCDirectionalLight.h"
#include "SoFCSpotLight.h"
#include "SoFCSelectionAction.h"
#include "SoFCUnifiedSelection.h"
#include "SoFCVectorizeSVGAction.h"
#include "SoFCVectorizeU3DAction.h"
#include "SoTouchEvents.h"
#include "SpaceballEvent.h"
#include "Utilities.h"
#include "BitmapFactory.h"
#include "View3DInventorRiftViewer.h"
#include "View3DViewerPy.h"

#include "Inventor/SoFCRenderCacheManager.h"
#include <Inventor/draggers/SoCenterballDragger.h>
#include <Inventor/annex/Profiler/SoProfiler.h>
#include <Inventor/annex/HardCopy/SoVectorizePSAction.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoLightModelElement.h>

#include "ViewParams.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderLink.h"
#include "Renderer/Renderer.h"

namespace bp = boost::placeholders;


FC_LOG_LEVEL_INIT("3DViewer",true,true)

//#define FC_LOGGING_CB

using namespace Gui;
using namespace Render;

#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif

// Disabled as bitmap cursor looks really bad on high DPI screen. Use Qt
// built-in cursor for now.
//
// #define FC_USE_BITMAP_CURSOR

#ifdef FC_USE_BITMAP_CURSOR
/*** zoom-style cursor ******/

#define ZOOM_WIDTH 16
#define ZOOM_HEIGHT 16
#define ZOOM_BYTES ((ZOOM_WIDTH + 7) / 8) * ZOOM_HEIGHT
#define ZOOM_HOT_X 5
#define ZOOM_HOT_Y 7

static unsigned char zoom_bitmap[ZOOM_BYTES] =
{
  0x00, 0x0f, 0x80, 0x1c, 0x40, 0x38, 0x20, 0x70,
  0x90, 0xe4, 0xc0, 0xcc, 0xf0, 0xfc, 0x00, 0x0c,
  0x00, 0x0c, 0xf0, 0xfc, 0xc0, 0xcc, 0x90, 0xe4,
  0x20, 0x70, 0x40, 0x38, 0x80, 0x1c, 0x00, 0x0f
};

static unsigned char zoom_mask_bitmap[ZOOM_BYTES] =
{
 0x00,0x0f,0x80,0x1f,0xc0,0x3f,0xe0,0x7f,0xf0,0xff,0xf0,0xff,0xf0,0xff,0x00,
 0x0f,0x00,0x0f,0xf0,0xff,0xf0,0xff,0xf0,0xff,0xe0,0x7f,0xc0,0x3f,0x80,0x1f,
 0x00,0x0f
};

/*** pan-style cursor *******/

#define PAN_WIDTH 16
#define PAN_HEIGHT 16
#define PAN_BYTES ((PAN_WIDTH + 7) / 8) * PAN_HEIGHT
#define PAN_HOT_X 7
#define PAN_HOT_Y 7

static unsigned char pan_bitmap[PAN_BYTES] =
{
  0xc0, 0x03, 0x60, 0x02, 0x20, 0x04, 0x10, 0x08,
  0x68, 0x16, 0x54, 0x2a, 0x73, 0xce, 0x01, 0x80,
  0x01, 0x80, 0x73, 0xce, 0x54, 0x2a, 0x68, 0x16,
  0x10, 0x08, 0x20, 0x04, 0x40, 0x02, 0xc0, 0x03
};

static unsigned char pan_mask_bitmap[PAN_BYTES] =
{
 0xc0,0x03,0xe0,0x03,0xe0,0x07,0xf0,0x0f,0xe8,0x17,0xdc,0x3b,0xff,0xff,0xff,
 0xff,0xff,0xff,0xff,0xff,0xdc,0x3b,0xe8,0x17,0xf0,0x0f,0xe0,0x07,0xc0,0x03,
 0xc0,0x03
};

/*** rotate-style cursor ****/

#define ROTATE_WIDTH 16
#define ROTATE_HEIGHT 16
#define ROTATE_BYTES ((ROTATE_WIDTH + 7) / 8) * ROTATE_HEIGHT
#define ROTATE_HOT_X 6
#define ROTATE_HOT_Y 8

static unsigned char rotate_bitmap[ROTATE_BYTES] = {
  0xf0, 0xef, 0x18, 0xb8, 0x0c, 0x90, 0xe4, 0x83,
  0x34, 0x86, 0x1c, 0x83, 0x00, 0x81, 0x00, 0xff,
  0xff, 0x00, 0x81, 0x00, 0xc1, 0x38, 0x61, 0x2c,
  0xc1, 0x27, 0x09, 0x30, 0x1d, 0x18, 0xf7, 0x0f
};

static unsigned char rotate_mask_bitmap[ROTATE_BYTES] = {
 0xf0,0xef,0xf8,0xff,0xfc,0xff,0xfc,0xff,0x3c,0xfe,0x1c,0xff,0x00,0xff,0x00,
 0xff,0xff,0x00,0xff,0x00,0xff,0x38,0x7f,0x3c,0xff,0x3f,0xff,0x3f,0xff,0x1f,
 0xf7,0x0f
};
#endif

/*!
As ProgressBar has no chance to control the incoming Qt events of Quarter so we need to stop
the event handling to prevent the scenegraph from being selected or deselected
while the progress bar is running.
*/
class Gui::ViewerEventFilter : public QObject
{
public:
    ViewerEventFilter() {}
    ~ViewerEventFilter() override {}

    bool eventFilter(QObject* obj, QEvent* event) override {
        // Bug #0000607: Some mice also support horizontal scrolling which however might
        // lead to some unwanted zooming when pressing the MMB for panning.
        // Thus, we filter out horizontal scrolling.
        if (event->type() == QEvent::Wheel) {
            auto we = static_cast<QWheelEvent*>(event);
            if (qAbs(we->angleDelta().x()) > qAbs(we->angleDelta().y()))
                return true;
        }
        else if (event->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(event);
            if (ke->matches(QKeySequence::SelectAll)) {
                static_cast<View3DInventorViewer*>(obj)->selectAll();
                return true;
            }
        }
        if (Base::Sequencer().isRunning() && Base::Sequencer().isBlocking())
            return false;

        if (event->type() == Spaceball::ButtonEvent::ButtonEventType) {
            auto buttonEvent = static_cast<Spaceball::ButtonEvent*>(event);
            if (!buttonEvent) {
                Base::Console().Log("invalid spaceball button event\n");
                return true;
            }
        }
        else if (event->type() == Spaceball::MotionEvent::MotionEventType) {
            auto motionEvent = static_cast<Spaceball::MotionEvent*>(event);
            if (!motionEvent) {
                Base::Console().Log("invalid spaceball motion event\n");
                return true;
            }
        }

        return false;
    }
};

class SpaceNavigatorDevice : public Quarter::InputDevice {
public:
    SpaceNavigatorDevice() {}
    ~SpaceNavigatorDevice() override {}
    const SoEvent* translateEvent(QEvent* event) override {

        if (event->type() == Spaceball::MotionEvent::MotionEventType) {
            auto motionEvent = static_cast<Spaceball::MotionEvent*>(event);
            if (!motionEvent) {
                Base::Console().Log("invalid spaceball motion event\n");
                return nullptr;
            }

            motionEvent->setHandled(true);

            float xTrans, yTrans, zTrans;
            xTrans = static_cast<float>(motionEvent->translationX());
            yTrans = static_cast<float>(motionEvent->translationY());
            zTrans = static_cast<float>(motionEvent->translationZ());
            SbVec3f translationVector(xTrans, yTrans, zTrans);

            static float rotationConstant(.0001f);
            SbRotation xRot, yRot, zRot;
            xRot.setValue(SbVec3f(1.0, 0.0, 0.0), static_cast<float>(motionEvent->rotationX()) * rotationConstant);
            yRot.setValue(SbVec3f(0.0, 1.0, 0.0), static_cast<float>(motionEvent->rotationY()) * rotationConstant);
            zRot.setValue(SbVec3f(0.0, 0.0, 1.0), static_cast<float>(motionEvent->rotationZ()) * rotationConstant);

            auto motion3Event = new SoMotion3Event;
            motion3Event->setTranslation(translationVector);
            motion3Event->setRotation(xRot * yRot * zRot);
            motion3Event->setPosition(this->mousepos);

            return motion3Event;
        }

        return nullptr;
    }
};

template<class PropT, class ValueT, class CallbackT>
static ValueT _shadowParam(View3DInventor *view, const char *_name, const char *_docu, const ValueT &def, CallbackT cb) {
    if (!view)
        return def;
    return view->getProperty<PropT, ValueT>(_name, _docu, "Shadow", def, cb);
}

template<class PropT, class ValueT>
static ValueT _shadowParam(View3DInventor *view, const char *_name, const char *_docu, const ValueT &def) {
    if (!view)
        return def;
    auto cb = [](PropT &){};
    return view->getProperty<PropT, ValueT>(_name, _docu, "Shadow", def, cb);
}

template<class PropT, class ValueT>
static void _shadowSetParam(View3DInventor *view, const char *_name, const ValueT &def) {
    if (!view)
        return;
    view->getProperty<PropT, ValueT>(_name, nullptr, "Shadow", def, 
        [&def](PropT &prop) {
            Base::ObjectStatusLocker<App::Property::Status,App::Property> guard(App::Property::User3, &prop);
            prop.setValue(def);
        });
}

template<class PropT, class ValueT, class CallbackT>
static ValueT _hiddenLineParam(View3DInventor *view, const char *_name, const char *_docu, const ValueT &def, CallbackT cb) {
    if (!view)
        return def;
    return view->getProperty<PropT, ValueT>(_name, _docu, "HiddenLine", def, cb);
}

template<class PropT, class ValueT>
static ValueT _hiddenLineParam(View3DInventor *view, const char *_name, const char *_docu, const ValueT &def) {
    if (!view)
        return def;
    auto cb = [](PropT &){};
    return view->getProperty<PropT, ValueT>(_name, _docu, "HiddenLine", def, cb);
}

struct View3DInventorViewer::Private
{
    View3DInventor                    *view;
    View3DInventorViewer              *owner;

    CoinPtr<SoShadowGroup>            pcShadowGroup;
    CoinPtr<SoShapeHints>             pcShadowShapeHints;
    CoinPtr<SoFCDirectionalLight>     pcShadowDirectionalLight;
    CoinPtr<SoFCSpotLight>            pcShadowSpotLight;
    CoinPtr<SoGroup>                  pcShadowGroundGroup;
    CoinPtr<SoSwitch>                 pcShadowGroundSwitch;
    CoinPtr<SoCoordinate3>            pcShadowGroundCoords;
    CoinPtr<SoFaceSet>                pcShadowGround;
    CoinPtr<SoShadowStyle>            pcShadowGroundStyle;
    CoinPtr<SoMaterial>               pcShadowMaterial;
    CoinPtr<SoTexture2>               pcShadowGroundTexture;
    CoinPtr<SoTextureCoordinate2>     pcShadowGroundTextureCoords;
    CoinPtr<SoBumpMap>                pcShadowGroundBumpMap;
    CoinPtr<SoLightModel>             pcShadowGroundLightModel;
    CoinPtr<SoShapeHints>             pcShadowGroundShapeHints;
    CoinPtr<SoPickStyle>              pcShadowPickStyle;
    uint32_t                          shadowNodeId = 0;
    uint32_t                          cameraNodeId = 0;
    bool                              shadowExtraRedraw = false;
    bool                              animating = false;

    QTimer                            timer;

    CoinPtr<SoTempPath>     tmpPath;

    CoinPtr<SoTransform>    pickTransform;
    CoinPtr<SoSeparator>    pickRoot;
    CoinPtr<SoNode>         pickDummy;
    SoSearchAction          pickSearch;
    SoRayPickAction         pickAction;
    SoGetMatrixAction       pickMatrixAction;

    SoFCDisplayModeElement::HiddenLineConfig hiddenLineConfig;

    std::unique_ptr<Renderer> renderer;

    Private(View3DInventorViewer *owner)
        :view(qobject_cast<View3DInventor*>(owner->parent()))
        ,owner(owner)
        ,tmpPath(new SoTempPath(10))
        ,pickAction(SbViewportRegion())
        ,pickMatrixAction(SbViewportRegion())
    {}

    void activateShadow();
    void deactivateShadow();
    void updateShadowGround(const SbBox3f &box);
    void redraw();
    void onRender();
    bool toggleDragger(int toggle);

    SoPickedPoint* getPointOnRay(const SbVec2s& pos, const ViewProvider* vp);

    void getBoundingBox(SbBox3f &box) {
        SoNode *node = nullptr;
        if (pcShadowSpotLight && pcShadowSpotLight->showDragger.getValue())
            node = pcShadowSpotLight;
        else if (pcShadowDirectionalLight && pcShadowDirectionalLight->showDragger.getValue())
            node = pcShadowDirectionalLight;
        if (node) {
            SoGetBoundingBoxAction action(owner->getSoRenderManager()->getViewportRegion());
            action.apply(node);
            box.extendBy(action.getBoundingBox());
        }
    }

    int checkElementIntersection(ViewProviderDocumentObject *vp,
                                 const char *subname,
                                 const Base::ViewProjMethod &proj,
                                 const Base::Polygon2d &polygon,
                                 App::DocumentObject *prevObj = nullptr);

    void initHiddenLineConfig(bool activate=false);

    static void onDragFinish(void *data, SoDragger *d);
};

/** \defgroup View3D 3D Viewer
 *  \ingroup GUI
 *
 * The 3D Viewer is one of the major components in a CAD/CAE systems.
 * Therefore an overview and some remarks to the FreeCAD 3D viewing system.
 *
 * \section overview Overview
 * \todo Overview and complements for the 3D Viewer
 *
 * \section trouble Troubleshooting
 * When it's needed to capture OpenGL function calls then the utility apitrace
 * can be very useful: https://github.com/apitrace/apitrace/blob/master/docs/USAGE.markdown
 *
 * To better locate the problematic code it's possible to add custom log messages.
 * For the prerequisites check:
 * https://github.com/apitrace/apitrace/blob/master/docs/USAGE.markdown#
 * emitting-annotations-to-the-trace
 * \code
 * #include <GL/glext.h>
 * #include <Inventor/C/glue/gl.h>
 *
 * void GLRender(SoGLRenderAction* glra)
 * {
 *     int context = glra->getCacheContext();
 *     const cc_glglue * glue = cc_glglue_instance(context);
 *
 *     PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup = (PFNGLPUSHDEBUGGROUPPROC)
 *     cc_glglue_getprocaddress(glue, "glPushDebugGroup");
 *     PFNGLDEBUGMESSAGEINSERTARBPROC glDebugMessageInsert = (PFNGLDEBUGMESSAGEINSERTARBPROC)
 *     cc_glglue_getprocaddress(glue, "glDebugMessageInsert");
 *     PFNGLPOPDEBUGGROUPPROC glPopDebugGroup = (PFNGLPOPDEBUGGROUPPROC)
 *     cc_glglue_getprocaddress(glue, "glPopDebugGroup");
 *
 *     glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, __FUNCTION__);
 * ...
 *     glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER,
 *                          0, GL_DEBUG_SEVERITY_MEDIUM, -1, "begin_blabla");
 * ...
 *     glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER,
 *                          0, GL_DEBUG_SEVERITY_MEDIUM, -1, "end_blabla");
 * ...
 *     glPopDebugGroup();
 * }
 * \endcode
 */


// *************************************************************************

View3DInventorViewer::View3DInventorViewer(QWidget* parent, const QtGLWidget* sharewidget)
    : Quarter::SoQTQuarterAdaptor(parent, sharewidget)
    , SelectionObserver(false, ResolveMode::NoResolve)
    , editViewProvider(nullptr)
    , navigation(nullptr)
    , renderType(Native)
    , framebuffer(nullptr)
    , axisCross(nullptr)
    , axisGroup(nullptr)
    , editing(false)
    , redirected(false)
    , allowredir(false)
    , overrideMode("As Is")
    , _viewerPy(nullptr)
{
    init();
}

View3DInventorViewer::View3DInventorViewer(const QtGLFormat& format, QWidget* parent, const QtGLWidget* sharewidget)
    : Quarter::SoQTQuarterAdaptor(format, parent, sharewidget)
    , SelectionObserver(false, ResolveMode::NoResolve)
    , editViewProvider(nullptr)
    , navigation(nullptr)
    , renderType(Native)
    , framebuffer(nullptr)
    , axisCross(nullptr)
    , axisGroup(nullptr)
    , editing(false)
    , redirected(false)
    , allowredir(false)
    , overrideMode("As Is")
    , _viewerPy(nullptr)
{
    init();
}

void View3DInventorViewer::init()
{
    pcEditingRoot = nullptr;

    _pimpl.reset(new Private(this));
    _pimpl->timer.setSingleShot(true);
    connect(&_pimpl->timer,SIGNAL(timeout()),this,SLOT(redrawShadow()));

    static bool _cacheModeInited;
    if(!_cacheModeInited) {
        _cacheModeInited = true;
        pcViewProviderRoot = nullptr;
        selectionRoot = nullptr;
        setRenderCache(-1);
    }

    shading = true;
    fpsEnabled = false;
    vboEnabled = false;

    attachSelection();

    // Coin should not clear the pixel-buffer, so the background image
    // is not removed.
    this->setClearWindow(false);

    // In order to support blit FBO rendered by external render engine, we
    // shall not clear depth buffer here.
    this->setClearZBuffer(false);

    // setting up the defaults for the spin rotation
    initialize();

    auto cam = new SoOrthographicCamera;
    cam->position = SbVec3f(0, 0, 1);
    cam->height = 1;
    cam->nearDistance = 0.5;
    cam->farDistance = 1.5;

    // setup light sources
    SoDirectionalLight* hl = this->getHeadlight();
    backlight = new SoDirectionalLight();
    backlight->ref();
    backlight->setName("backlight");
    backlight->direction.setValue(-hl->direction.getValue());
    backlight->on.setValue(false); // by default off

    // Set up background scenegraph with image in it.
    backgroundroot = new SoSeparator;
    backgroundroot->ref();
    this->backgroundroot->addChild(cam);

    // Background stuff
    pcBackGround = new SoFCBackgroundGradient;
    pcBackGround->ref();
    pcBackGroundSwitch = new SoSwitch;
    pcBackGroundSwitch->ref();
    pcBackGroundSwitch->addChild(pcBackGround);
    backgroundroot->addChild(pcBackGroundSwitch);

    // Set up foreground, overlaid scenegraph.
    this->foregroundroot = new SoSeparator;
    this->foregroundroot->ref();

    auto lm = new SoLightModel;
    lm->model = SoLightModel::BASE_COLOR;

    auto bc = new SoBaseColor;
    bc->rgb = SbColor(1, 1, 0);

    cam = new SoOrthographicCamera;
    cam->position = SbVec3f(0, 0, 5);
    cam->height = 10;
    cam->nearDistance = 0;
    cam->farDistance = 10;

    this->foregroundroot->addChild(cam);
    this->foregroundroot->addChild(lm);
    this->foregroundroot->addChild(bc);

    // NOTE: For every mouse click event the SoFCUnifiedSelection searches for the picked
    // point which causes a certain slow-down because for all objects the primitives
    // must be created. Using an SoSeparator avoids this drawback.
    selectionRoot = new Gui::SoFCUnifiedSelection();
    selectionRoot->applySettings();
    selectionRoot->setViewer(this);

    // set the ViewProvider root node
    pcViewProviderRoot = selectionRoot;

    // increase refcount before passing it to setScenegraph(), to avoid
    // premature destruction
    pcViewProviderRoot->ref();
    // is not really working with Coin3D.
    //redrawOverlayOnSelectionChange(pcSelection);
    setSceneGraph(pcViewProviderRoot);
    // Event callback node
    pEventCallback = new SoEventCallback();
    pEventCallback->setUserData(this);
    pEventCallback->ref();
    pcViewProviderRoot->addChild(pEventCallback);
    pEventCallback->addEventCallback(SoEvent::getClassTypeId(), handleEventCB, this);

    // This is a callback node that logs all action that traverse the Inventor tree.
#if defined (FC_DEBUG) && defined(FC_LOGGING_CB)
    SoCallback* cb = new SoCallback;
    cb->setCallback(interactionLoggerCB, this);
    pcViewProviderRoot->addChild(cb);
#endif

    selectionAction.reset(new SoFCSelectionAction);
    highlightAction.reset(new SoFCHighlightAction);

    inventorSelection = std::make_unique<View3DInventorSelection>(this, selectionRoot);

    dimensionRoot = new SoSwitch(SO_SWITCH_NONE);
    dimensionRoot->setName("DimensionRoot");
    // To not get effect shadow drawing, add this in upper hierarchy.
    //
    // selectionRoot->addChild(dimensionRoot);
    inventorSelection->getAuxRoot()->addChild(dimensionRoot);
    dimensionRoot->addChild(new SoSwitch()); //first one will be for the 3d dimensions.
    dimensionRoot->addChild(new SoSwitch()); //second one for the delta dimensions.

    pcClipPlane = nullptr;

    pcEditingRoot = new SoSeparator;
    pcEditingRoot->ref();
    pcEditingRoot->setName("EditingRoot");
    pcEditingTransform = new SoTransform;
    pcEditingTransform->ref();
    pcEditingTransform->setName("EditingTransform");
    restoreEditingRoot = false;
    pcEditingRoot->addChild(pcEditingTransform);

    inventorSelection->getAuxRoot()->addChild(pcEditingRoot);

    // Set our own render action which show a bounding box if
    // the SoFCSelection::BOX style is set
    //
    // Important note:
    // When creating a new GL render action we have to copy over the cache context id
    // because otherwise we may get strange rendering behaviour. For more details see
    // http://forum.freecad.org/viewtopic.php?f=10&t=7486&start=120#p74398 and for
    // the fix and some details what happens behind the scene have a look at this
    // http://forum.freecad.org/viewtopic.php?f=10&t=7486&p=74777#p74736
    uint32_t id = this->getSoRenderManager()->getGLRenderAction()->getCacheContext();
    this->getSoRenderManager()->setGLRenderAction(new SoBoxSelectionRenderAction);
    this->getSoRenderManager()->getGLRenderAction()->setCacheContext(id);

    // set the transparency and antialiasing settings
    getSoRenderManager()->getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);

    // Settings
    setSeekTime(0.4f);

    if (!isSeekValuePercentage())
        setSeekValueAsPercentage(true);

    setSeekDistance(100);
    setViewing(false);

    setBackgroundColor(QColor(25, 25, 25));
    setGradientBackground(Background::LinearGradient);

    // set some callback functions for user interaction
    addStartCallback(interactionStartCB);
    addFinishCallback(interactionFinishCB);

    //filter a few qt events
    viewerEventFilter = new ViewerEventFilter;
    installEventFilter(viewerEventFilter);
    getEventFilter()->registerInputDevice(new SpaceNavigatorDevice);
    getEventFilter()->registerInputDevice(new GesturesDevice(this));

    try{
        this->grabGesture(Qt::PanGesture);
        this->grabGesture(Qt::PinchGesture);
    } catch (Base::Exception &e) {
        Base::Console().Warning("Failed to set up gestures. Error: %s\n", e.what());
    } catch (...) {
        Base::Console().Warning("Failed to set up gestures. Unknown error.\n");
    }

    //create the cursors

#ifdef FC_USE_BITMAP_CURSOR
    spinCursor = QCursor(Qt::SizeBDiagCursor);
    zoomCursor = QCursor(Qt::SizeVerCursor);
    panCursor = QCursor(Qt::SizeAllCursor);
#else

    createStandardCursors(devicePixelRatio());
    connect(this, &View3DInventorViewer::devicePixelRatioChanged,
            this, &View3DInventorViewer::createStandardCursors);
#endif

    naviCube = new NaviCube(this);
    naviCubeEnabled = true;

    updateHatchTexture();

}

View3DInventorViewer::~View3DInventorViewer()
{
    // to prevent following OpenGL error message: "Texture is not valid in the current context. Texture has not been destroyed"
    aboutToDestroyGLContext();

    // It can happen that a document has several MDI views and when the about to be
    // closed 3D view is in edit mode the corresponding view provider must be restored
    // because otherwise it might be left in a broken state
    // See https://forum.freecad.org/viewtopic.php?f=3&t=39720
    if (restoreEditingRoot) {
        resetEditingRoot(false);
    }

    // cleanup
    this->backgroundroot->unref();
    this->backgroundroot = nullptr;
    this->foregroundroot->unref();
    this->foregroundroot = nullptr;
    this->pcBackGround->unref();
    this->pcBackGround = nullptr;
    this->pcBackGroundSwitch->unref();
    this->pcBackGroundSwitch = nullptr;

    setSceneGraph(nullptr);
    this->pEventCallback->unref();
    this->pEventCallback = nullptr;
    // Note: It can happen that there is still someone who references
    // the root node but isn't destroyed when closing this viewer so
    // that it prevents all children from being deleted. To reduce this
    // likelihood we explicitly remove all child nodes now.
    coinRemoveAllChildren(this->pcViewProviderRoot);
    this->pcViewProviderRoot->unref();
    this->pcViewProviderRoot = nullptr;
    this->backlight->unref();
    this->backlight = nullptr;

    inventorSelection.reset(nullptr);

    this->pcEditingRoot->unref();
    this->pcEditingTransform->unref();

    if (this->pcClipPlane)
        this->pcClipPlane->unref();

    delete this->navigation;

    // Note: When closing the application the main window doesn't exist any more.
    if (getMainWindow())
        getMainWindow()->setPaneText(2, QStringLiteral(""));

    detachSelection();

    removeEventFilter(viewerEventFilter);
    delete viewerEventFilter;

    if (_viewerPy) {
        static_cast<View3DInventorViewerPy*>(_viewerPy)->_viewer = nullptr;
        Py_DECREF(_viewerPy);
    }

    // In the init() function we have overridden the default SoGLRenderAction with our
    // own instance of SoBoxSelectionRenderAction and SoRenderManager destroyed the default.
    // But it does this only once so that now we have to explicitly destroy our instance in
    // order to free the memory.
    SoGLRenderAction* glAction = this->getSoRenderManager()->getGLRenderAction();
    this->getSoRenderManager()->setGLRenderAction(nullptr);
    delete glAction;
}

void View3DInventorViewer::createStandardCursors(double dpr)
{
#ifdef FC_USE_BITMAP_CURSOR
    QBitmap cursor = QBitmap::fromData(QSize(ROTATE_WIDTH, ROTATE_HEIGHT), rotate_bitmap);
    QBitmap mask = QBitmap::fromData(QSize(ROTATE_WIDTH, ROTATE_HEIGHT), rotate_mask_bitmap);
#   if defined(Q_OS_WIN32)
    cursor.setDevicePixelRatio(dpr);
    mask.setDevicePixelRatio(dpr);
#   else
    Q_UNUSED(dpr)
#   endif
    spinCursor = QCursor(cursor, mask, ROTATE_HOT_X, ROTATE_HOT_Y);

    cursor = QBitmap::fromData(QSize(ZOOM_WIDTH, ZOOM_HEIGHT), zoom_bitmap);
    mask = QBitmap::fromData(QSize(ZOOM_WIDTH, ZOOM_HEIGHT), zoom_mask_bitmap);
#   if defined(Q_OS_WIN32)
    cursor.setDevicePixelRatio(dpr);
    mask.setDevicePixelRatio(dpr);
#   endif
    zoomCursor = QCursor(cursor, mask, ZOOM_HOT_X, ZOOM_HOT_Y);

    cursor = QBitmap::fromData(QSize(PAN_WIDTH, PAN_HEIGHT), pan_bitmap);
    mask = QBitmap::fromData(QSize(PAN_WIDTH, PAN_HEIGHT), pan_mask_bitmap);
#   if defined(Q_OS_WIN32)
    cursor.setDevicePixelRatio(dpr);
    mask.setDevicePixelRatio(dpr);
#   endif
    panCursor = QCursor(cursor, mask, PAN_HOT_X, PAN_HOT_Y);

#else
    (void)dpr;
#endif
}

void View3DInventorViewer::aboutToDestroyGLContext()
{
    if (naviCube) {
        auto gl = qobject_cast<QtGLWidget*>(this->viewport());
        if (gl)
            gl->makeCurrent();
        delete naviCube;
        naviCube = nullptr;
        naviCubeEnabled = false;
    }
}

void View3DInventorViewer::setDocument(Gui::Document* pcDocument)
{
    // write the document the viewer belongs to the selection node
    guiDocument = pcDocument;
    selectionRoot->setDocument(pcDocument);
    inventorSelection->setDocument(pcDocument);

    if(pcDocument && _pimpl->view) {
        const auto &sels = Selection().getSelection(pcDocument->getDocument()->getName(), ResolveMode::NoResolve);
        for(auto &sel : sels) {
            SelectionChanges Chng(SelectionChanges::ShowSelection,
                    sel.DocName,sel.FeatName,sel.SubName);
            onSelectionChanged(Chng);
        }

        std::vector<App::Property*> props;
        auto doc = pcDocument->getDocument();
        doc->getPropertyList(props);
        for (auto prop : props) {
            // Migrate shadow property in document to parent view
            if (prop->getName() && boost::starts_with(prop->getName(), "Shadow_")) {
                auto myProp = _pimpl->view->addDynamicProperty(prop->getTypeId().getName(),
                        prop->getName(), prop->getGroup(), prop->getDocumentation());
                Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                        App::Property::User3, myProp);
                myProp->Paste(*prop);
                doc->removeDynamicProperty(prop->getName());
            }
        }
    }
}

void View3DInventorViewer::onViewPropertyChanged(const App::Property &prop)
{
    if(!prop.getName() || prop.testStatus(App::Property::User3) || !_pimpl->view)
        return;

    if (&prop == &_pimpl->view->ShowNaviCube) {
        naviCubeEnabled  = _pimpl->view->ShowNaviCube.getValue();
        this->getSoRenderManager()->scheduleRedraw();
    } else if(!_applyingOverride) {
        if ((boost::starts_with(prop.getName(),"Shadow_")
                || boost::starts_with(prop.getName(),"HiddenLine_"))
             && (overrideMode == "Shadow"
                 || overrideMode == "Hidden Line"))
        {
            Base::StateLocker guard(_applyingOverride);
            applyOverrideMode();
        }
    }
}

Document* View3DInventorViewer::getDocument() {
    return guiDocument;
}


void View3DInventorViewer::initialize()
{
    navigation = new CADNavigationStyle();
    navigation->setViewer(this);

    this->axiscrossEnabled = true;
    this->axiscrossSize = 10;
}

const std::set<App::SubObjectT> &View3DInventorViewer::getObjectsOnTop() const
{
    return inventorSelection->getObjectsOnTop();
}

void View3DInventorViewer::clearGroupOnTop(bool alt)
{
    inventorSelection->clearGroupOnTop(alt);
}

bool View3DInventorViewer::isInGroupOnTop(const App::SubObjectT &objT, bool altOnly) const
{
    return inventorSelection->isInGroupOnTop(objT, altOnly);
}

bool View3DInventorViewer::hasOnTopObject() const
{
    return inventorSelection->hasOnTopObject();
}

void View3DInventorViewer::refreshGroupOnTop()
{
    inventorSelection->refreshGroupOnTop();
}

void View3DInventorViewer::checkGroupOnTop(const SelectionChanges &Reason, bool alt)
{
    inventorSelection->checkGroupOnTop(Reason, alt);
}

/// @cond DOXERR
void View3DInventorViewer::onSelectionChanged(const SelectionChanges &_Reason)
{
    if(!getDocument())
        return;

    SelectionChanges Reason(_Reason);

    if(Reason.pDocName && *Reason.pDocName &&
       strcmp(getDocument()->getDocument()->getName(),Reason.pDocName)!=0)
        return;

    switch(Reason.Type) {
    case SelectionChanges::ShowSelection:
        Reason.Type = SelectionChanges::AddSelection;
        // fall through
    case SelectionChanges::HideSelection:
    case SelectionChanges::SetPreselect:
    case SelectionChanges::RmvPreselect:
    case SelectionChanges::SetSelection:
    case SelectionChanges::AddSelection:
    case SelectionChanges::RmvSelection:
    case SelectionChanges::ClrSelection:
        inventorSelection->checkGroupOnTop(Reason);
        break;
    default:
        return;
    }

    if(Reason.Type == SelectionChanges::HideSelection)
        Reason.Type = SelectionChanges::RmvSelection;

    switch(Reason.Type) {
    case SelectionChanges::RmvPreselect:
    case SelectionChanges::SetPreselect:
        if (highlightAction->SelChange)
            FC_WARN("Recursive highlight notification");
        else {
            highlightAction->SelChange = &Reason;
            highlightAction->apply(pcViewProviderRoot);
            highlightAction->SelChange = nullptr;
        }
        break;
    default:
        if (selectionAction->SelChange)
            FC_WARN("Recursive selection notification");
        else {
            selectionAction->SelChange = &Reason;
            selectionAction->apply(pcViewProviderRoot);
            selectionAction->SelChange = nullptr;
        }
    }
}
/// @endcond

SbBool View3DInventorViewer::searchNode(SoNode* node) const
{
    if (node == pcEditingRoot)
        return true;
    SoSearchAction searchAction;
    searchAction.setNode(node);
    searchAction.setInterest(SoSearchAction::FIRST);
    if (pcEditingRoot->getNumChildren()) {
        searchAction.apply(pcEditingRoot);
        if (searchAction.getPath())
            return true;
    }
    searchAction.apply(this->getSceneGraph());
    SoPath* selectionPath = searchAction.getPath();
    return selectionPath ? true : false;
}

SbBool View3DInventorViewer::hasViewProvider(ViewProvider* pcProvider) const
{
    return _ViewProviderSet.find(pcProvider) != _ViewProviderSet.end();
}

SbBool View3DInventorViewer::containsViewProvider(const ViewProvider* vp) const
{
    return hasViewProvider(const_cast<ViewProvider*>(vp));
    // SoSearchAction sa;
    // sa.setNode(vp->getRoot());
    // sa.setSearchingAll(true);
    // sa.apply(getSoRenderManager()->getSceneGraph());
    // return sa.getPath() != nullptr;
}

/// adds an ViewProvider to the view, e.g. from a feature
void View3DInventorViewer::addViewProvider(ViewProvider* pcProvider)
{
    if(!_ViewProviderSet.insert(pcProvider).second)
        return;

    SoSeparator* root = pcProvider->getRoot();

    if (root) {
        if(!guiDocument->isClaimed3D(pcProvider) && pcProvider->canAddToSceneGraph())
            pcViewProviderRoot->addChild(root);
    }

    SoSeparator* fore = pcProvider->getFrontRoot();
    if (fore)
        foregroundroot->addChild(fore);

    SoSeparator* back = pcProvider->getBackRoot();
    if (back)
        backgroundroot->addChild(back);

    pcProvider->setOverrideMode(this->getOverrideMode());
}

void View3DInventorViewer::removeViewProvider(ViewProvider* pcProvider)
{
    if (this->editViewProvider == pcProvider)
        resetEditingViewProvider();

    auto it = _ViewProviderSet.find(pcProvider);
    if(it == _ViewProviderSet.end())
        return;
    _ViewProviderSet.erase(it);

    SoSeparator* root = pcProvider->getRoot();

    if (root) {
        int index = pcViewProviderRoot->findChild(root);
        if(index>=0)
            pcViewProviderRoot->removeChild(index);
    }

    SoSeparator* fore = pcProvider->getFrontRoot();
    if (fore)
        foregroundroot->removeChild(fore);

    SoSeparator* back = pcProvider->getBackRoot();
    if (back)
        backgroundroot->removeChild(back);
}

void View3DInventorViewer::toggleViewProvider(ViewProvider *vp) {
    if(!_ViewProviderSet.count(vp))
        return;
    SoSeparator* root = vp->getRoot();
    if(!root || !guiDocument)
        return;
    int index = pcViewProviderRoot->findChild(root);
    if(index>=0) {
        if(guiDocument->isClaimed3D(vp) || !vp->canAddToSceneGraph())
            pcViewProviderRoot->removeChild(index);
    } else if(!guiDocument->isClaimed3D(vp) && vp->canAddToSceneGraph())
        pcViewProviderRoot->addChild(root);
}

void View3DInventorViewer::setEditingTransform(const Base::Matrix4D &mat) {
    if(pcEditingTransform) {
        double dMtrx[16];
        mat.getGLMatrix(dMtrx);
        pcEditingTransform->setMatrix(SbMatrix(
                    dMtrx[0], dMtrx[1], dMtrx[2],  dMtrx[3],
                    dMtrx[4], dMtrx[5], dMtrx[6],  dMtrx[7],
                    dMtrx[8], dMtrx[9], dMtrx[10], dMtrx[11],
                    dMtrx[12],dMtrx[13],dMtrx[14], dMtrx[15]));
    }
}

void View3DInventorViewer::setupEditingRoot(SoNode *node, const Base::Matrix4D *mat) {
    if(!editViewProvider)
        return;
    resetEditingRoot(false);

    if(mat)
        setEditingTransform(*mat);
    else
        setEditingTransform(getDocument()->getEditingTransform());
    if(node) {
        restoreEditingRoot = false;
        pcEditingRoot->addChild(node);
        return;
    }

    restoreEditingRoot = true;
    auto root = editViewProvider->getRoot();
    for(int i=0,count=root->getNumChildren();i<count;++i) {
        SoNode *node = root->getChild(i);
        if(node != editViewProvider->getTransformNode())
            pcEditingRoot->addChild(node);
    }
    coinRemoveAllChildren(root);
    ViewProviderLink::updateLinks(editViewProvider);
}

void View3DInventorViewer::resetEditingRoot(bool updateLinks)
{
    if(!editViewProvider || pcEditingRoot->getNumChildren()<=1)
        return;
    if(!restoreEditingRoot) {
        pcEditingRoot->getChildren()->truncate(1);
        return;
    }
    restoreEditingRoot = false;
    auto root = editViewProvider->getRoot();
    if(root->getNumChildren())
        FC_ERR("WARNING!!! Editing view provider root node is tampered");
    root->addChild(editViewProvider->getTransformNode());
    for(int i=1,count=pcEditingRoot->getNumChildren();i<count;++i)
        root->addChild(pcEditingRoot->getChild(i));
    pcEditingRoot->getChildren()->truncate(1);

    // handle exceptions eventually raised by ViewProviderLink
    try {
        if (updateLinks)
            ViewProviderLink::updateLinks(editViewProvider);
    }
    catch (const Py::Exception& e) {
        /* coverity[UNCAUGHT_EXCEPT] Uncaught exception */
        // Coverity created several reports when removeViewProvider()
        // is used somewhere in a destructor which indirectly invokes
        // resetEditingRoot().
        // Now theoretically Py::type can throw an exception which nowhere
        // will be handled and thus terminates the application. So, add an
        // extra try/catch block here.
        try {
            Py::Object o = Py::type(e);
            if (o.isString()) {
                Py::String s(o);
                Base::Console().Warning("%s\n", s.as_std_string("utf-8").c_str());
            }
            else {
                Py::String s(o.repr());
                Base::Console().Warning("%s\n", s.as_std_string("utf-8").c_str());
            }
            // Prints message to console window if we are in interactive mode
            PyErr_Print();
        }
        catch (Py::Exception& e) {
            e.clear();
            Base::Console().Error("Unexpected exception raised in View3DInventorViewer::resetEditingRoot\n");
        }
    }
}

SoPickedPoint* View3DInventorViewer::getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const
{
    return _pimpl->getPointOnRay(pos, vp);
}

SoPickedPoint* View3DInventorViewer::Private::getPointOnRay(const SbVec2s& pos, const ViewProvider* vp)
{
    if (!pickRoot) {
        pickRoot = new SoSeparator;
        pickTransform = new SoTransform;
        pickDummy = new SoGroup;
        pickRoot->addChild(owner->getSoRenderManager()->getCamera());
        pickRoot->addChild(pickTransform);
        pickRoot->addChild(pickDummy);
    }
    if (pickRoot->getChild(0) != owner->getSoRenderManager()->getCamera())
        pickRoot->replaceChild(0, owner->getSoRenderManager()->getCamera());
    CoinPtr<SoPath> path;
    if(vp == owner->editViewProvider && owner->pcEditingRoot->getNumChildren()>1) {
        path = tmpPath;
        path->truncate(0);
        path->append(owner->pcEditingRoot);
    }else{
        SoSearchAction &sa = pickSearch;
        //first get the path to this node and calculate the current transformation
        sa.setNode(vp->getRoot());
        sa.setSearchingAll(true);
        sa.apply(owner->getSoRenderManager()->getSceneGraph());
        path = sa.getPath();
        if (!path)
            return nullptr;
    }
    SoGetMatrixAction &gm = pickMatrixAction;
    gm.setViewportRegion(owner->getSoRenderManager()->getViewportRegion());
    gm.apply(path);

    pickTransform->setMatrix(gm.getMatrix());

    // build a temporary scenegraph only keeping this viewproviders nodes and the accumulated
    // transformation
    pickRoot->replaceChild(2, path->getTail());
    tmpPath->truncate(0);

    //get the picked point
    SoRayPickAction &rp = pickAction;
    rp.setViewportRegion(owner->getSoRenderManager()->getViewportRegion());
    rp.setPoint(pos);
    rp.setRadius(owner->getPickRadius());
    rp.apply(pickRoot);

    // pickRoot->replaceChild(2, pickDummy);

    SoPickedPoint* pick = rp.getPickedPoint();
    return (pick ? new SoPickedPoint(*pick) : nullptr);
}

SoPickedPoint* View3DInventorViewer::getPointOnRay(const SbVec3f& pos, const SbVec3f& dir, const ViewProvider* vp) const
{
    // Note: There seems to be a  bug with setRay() which causes SoRayPickAction
    // to fail to get intersections between the ray and a line
    
    CoinPtr<SoPath> path;
    if(vp == editViewProvider && pcEditingRoot->getNumChildren()>1) {
        path = _pimpl->tmpPath;
        path->truncate(0);
        path->append(pcEditingRoot);
    }
    else {
        //first get the path to this node and calculate the current setTransformation
        SoSearchAction sa;
        sa.setNode(vp->getRoot());
        sa.setSearchingAll(true);
        sa.apply(getSoRenderManager()->getSceneGraph());
        path = sa.getPath();
        if (!path)
            return nullptr;
    }
    SoGetMatrixAction gm(getSoRenderManager()->getViewportRegion());
    gm.apply(path);

    // build a temporary scenegraph only keeping this viewproviders nodes and the accumulated
    // transformation
    auto trans = new SoTransform;
    trans->ref();
    trans->setMatrix(gm.getMatrix());

    auto root = new SoSeparator;
    root->ref();
    root->addChild(getSoRenderManager()->getCamera());
    root->addChild(trans);
    root->addChild(path->getTail());

    //get the picked point
    SoRayPickAction rp(getSoRenderManager()->getViewportRegion());
    rp.setRay(pos,dir);
    rp.setRadius(getPickRadius());
    rp.apply(root);
    root->unref();
    trans->unref();

    _pimpl->tmpPath->truncate(0);

    // returns a copy of the point
    SoPickedPoint* pick = rp.getPickedPoint();
    //return (pick ? pick->copy() : 0); // needs the same instance of CRT under MS Windows
    return (pick ? new SoPickedPoint(*pick) : nullptr);
}

void View3DInventorViewer::setEditingViewProvider(Gui::ViewProvider* p, int ModNum)
{
    this->editViewProvider = p;
    this->editViewProvider->setEditViewer(this, ModNum);
    addEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,this->editViewProvider);
}

/// reset from edit mode
void View3DInventorViewer::resetEditingViewProvider()
{
    if (this->editViewProvider) {

        // In case the event action still has grabbed a node when leaving edit mode
        // force to release it now
        SoEventManager* mgr = getSoEventManager();
        SoHandleEventAction* heaction = mgr->getHandleEventAction();
        if (heaction && heaction->getGrabber())
            heaction->releaseGrabber();

        resetEditingRoot();

        this->editViewProvider->unsetEditViewer(this);
        removeEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,this->editViewProvider);
        this->editViewProvider = nullptr;
    }
}

/// reset from edit mode
SbBool View3DInventorViewer::isEditingViewProvider() const
{
    return this->editViewProvider ? true : false;
}

/// display override mode
void View3DInventorViewer::setOverrideMode(const std::string& mode)
{
    if (mode == overrideMode)
        return;

    _pimpl->deactivateShadow();
    overrideMode = mode;
    applyOverrideMode();

    if (!_pimpl->view)
        return;

    if (!_pimpl->view->DrawStyle.testStatus(App::Property::User3)) {
        Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                App::Property::User3, &_pimpl->view->DrawStyle);
        _pimpl->view->DrawStyle.setValue(mode.c_str());
    }
    Application::Instance->signalViewModeChanged(_pimpl->view);
}

void View3DInventorViewer::applyOverrideMode()
{
    this->overrideBGColor = 0;
    auto views = getDocument()->getViewProvidersOfType(Gui::ViewProvider::getClassTypeId());

    const char * mode = this->overrideMode.c_str();
    if (SoFCUnifiedSelection::DisplayModeNoShading == mode) {
        this->shading = false;
        this->selectionRoot->overrideMode = SoFCUnifiedSelection::DisplayModeNoShading;
        this->getSoRenderManager()->setRenderMode(SoRenderManager::AS_IS);
    }
    else if (SoFCUnifiedSelection::DisplayModeTessellation == mode) {
        this->shading = true;
        this->selectionRoot->overrideMode = SoFCUnifiedSelection::DisplayModeTessellation;
        this->getSoRenderManager()->setRenderMode(SoRenderManager::HIDDEN_LINE);
    }
    else if (SoFCUnifiedSelection::DisplayModeHiddenLine == mode) {
        _pimpl->initHiddenLineConfig(true);
    }
    else if (overrideMode == "Shadow") {
        _pimpl->activateShadow();
    }
    else {
        this->shading = true;
        this->selectionRoot->overrideMode = overrideMode.c_str();
        this->getSoRenderManager()->setRenderMode(SoRenderManager::AS_IS);
    }
}

const SoFCDisplayModeElement::HiddenLineConfig &
View3DInventorViewer::getHiddenLineConfig() const
{
    return _pimpl->hiddenLineConfig;
}

void View3DInventorViewer::Private::initHiddenLineConfig(bool activate)
{
    if (!view)
        return;

    auto bgColor = _hiddenLineParam<App::PropertyColor>(
            view, "Background",
            ViewParams::docHiddenLineBackground(),
            App::Color(uint32_t(ViewParams::getHiddenLineBackground()))).getPackedValue();
    bool overrideBGColor = _hiddenLineParam<App::PropertyBool>(
                    view, "BackgroundOverride",
                    ViewParams::docHiddenLineOverrideBackground(),
                    ViewParams::getHiddenLineOverrideBackground());
    bool shading = _hiddenLineParam<App::PropertyBool>(
                view, "Shaded",
                ViewParams::docHiddenLineShaded(),
                ViewParams::getHiddenLineShaded());
    if (activate) {
        if ((owner->overrideBGColor = overrideBGColor))
            owner->overrideBGColor = bgColor;
        owner->shading = shading;
        owner->selectionRoot->overrideMode = SoFCUnifiedSelection::DisplayModeHiddenLine;
        owner->getSoRenderManager()->setRenderMode(SoRenderManager::AS_IS);
    }

    hiddenLineConfig.shaded = owner->shading;

    hiddenLineConfig.outline = _hiddenLineParam<App::PropertyBool>(
            view, "ShowOutline",
            ViewParams::docHiddenLineShowOutline(),
            ViewParams::getHiddenLineShowOutline());

    hiddenLineConfig.perFaceOutline = _hiddenLineParam<App::PropertyBool>(
            view, "PerFaceOutline",
            ViewParams::docHiddenLinePerFaceOutline(),
            ViewParams::getHiddenLinePerFaceOutline());

    hiddenLineConfig.hideVertex = _hiddenLineParam<App::PropertyBool>(
            view, "HideVertex",
            ViewParams::docHiddenLineHideVertex(),
            ViewParams::getHiddenLineHideVertex());

    hiddenLineConfig.hideFace = _hiddenLineParam<App::PropertyBool>(
            view, "HideFace",
            ViewParams::docHiddenLineHideFace(),
            ViewParams::getHiddenLineHideFace());

    hiddenLineConfig.hideSeam = _hiddenLineParam<App::PropertyBool>(
            view, "HideSeam",
            ViewParams::docHiddenLineHideSeam(),
            ViewParams::getHiddenLineHideSeam());

    hiddenLineConfig.sceneOutline = _hiddenLineParam<App::PropertyBool>(
            view, "SceneOutline",
            ViewParams::docHiddenLineSceneOutline(),
            ViewParams::getHiddenLineSceneOutline());

    static const App::PropertyFloatConstraint::Constraints _width_cstr(0.0,100.0,0.5);
    hiddenLineConfig.outlineWidth = _hiddenLineParam<App::PropertyFloatConstraint>(
            view, "OutlineWidth",
            ViewParams::docHiddenLineOutlineWidth(),
            ViewParams::getHiddenLineOutlineWidth(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_width_cstr);
            });

    hiddenLineConfig.hasFaceColor = _hiddenLineParam<App::PropertyBool>(
            view, "FaceColorOverride",
            ViewParams::docHiddenLineOverrideFaceColor(),
            ViewParams::getHiddenLineOverrideFaceColor());

    hiddenLineConfig.faceColor = _hiddenLineParam<App::PropertyColor>(
            view, "FaceColor",
            ViewParams::docHiddenLineFaceColor(),
            App::Color(uint32_t(ViewParams::getHiddenLineFaceColor()))).getPackedValue();

    hiddenLineConfig.hasLineColor = _hiddenLineParam<App::PropertyBool>(
            view, "LineColorOverride",
            ViewParams::docHiddenLineOverrideColor(),
            ViewParams::getHiddenLineOverrideColor());

    hiddenLineConfig.lineColor = _hiddenLineParam<App::PropertyColor>(
            view, "LineColor",
            ViewParams::docHiddenLineColor(),
            App::Color(uint32_t(ViewParams::getHiddenLineColor()))).getPackedValue();

    hiddenLineConfig.hasTransparency = _hiddenLineParam<App::PropertyBool>(
            view, "TransparencyOverride",
            ViewParams::docHiddenLineOverrideTransparency(),
            ViewParams::getHiddenLineOverrideTransparency());

    static const App::PropertyFloatConstraint::Constraints _transp_cstr(0.0,1.0,0.1);
    hiddenLineConfig.transparency = _hiddenLineParam<App::PropertyFloatConstraint>(
            view, "Transparency",
            ViewParams::docHiddenLineTransparency(),
            ViewParams::getHiddenLineTransparency(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_transp_cstr);
            });

    hiddenLineConfig.lineWidth = _hiddenLineParam<App::PropertyFloatConstraint>(
            view, "LineWidth",
            ViewParams::docHiddenLineWidth(),
            ViewParams::getHiddenLineWidth(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_width_cstr);
            });

    hiddenLineConfig.pointSize = _hiddenLineParam<App::PropertyFloatConstraint>(
            view, "PointSize",
            ViewParams::docHiddenLinePointSize(),
            ViewParams::getHiddenLinePointSize(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_width_cstr);
            });
}

void View3DInventorViewer::Private::deactivateShadow()
{
    if(pcShadowGroup) {
        auto superScene = static_cast<SoGroup*>(owner->getSoRenderManager()->getSceneGraph());
        int index = superScene->findChild(pcShadowGroup);
        if(index >= 0)
            superScene->replaceChild(index, owner->pcViewProviderRoot);
        index = superScene->findChild(pcShadowShapeHints);
        if (index >= 0)
            superScene->removeChild(index);
        pcShadowShapeHints.reset();
        pcShadowGroup.reset();
        pcShadowGroundSwitch->whichChild = -1;
        owner->inventorSelection->getRootPath()->truncate(1);
    }
}

void View3DInventorViewer::Private::activateShadow()
{
    if (!view)
        return;

    owner->shading = true;

    App::Document *doc = owner->guiDocument?owner->guiDocument->getDocument():nullptr;

    static const char *_ShadowDisplayMode[] = {"Flat Lines", "Shaded", "As Is", "Hidden Line", nullptr};
    int displayMode = _shadowParam<App::PropertyEnumeration>(view, "DisplayMode",
            ViewParams::docShadowDisplayMode(), ViewParams::getShadowDisplayMode(),
            [](App::PropertyEnumeration &prop) {
                if (!prop.getEnum().isValid())
                    prop.setEnums(_ShadowDisplayMode);
            });

    App::PropertyBool *flatlines = Base::freecad_dynamic_cast<App::PropertyBool>(
            doc->getPropertyByName("FlatLines"));
    if (flatlines) {
        owner->selectionRoot->overrideMode = flatlines->getValue()?"Shaded":"Flat Lines";
        _shadowSetParam<App::PropertyEnumeration>(view, "DisplayMode", flatlines->getValue()?0:1);
        doc->removeDynamicProperty("Shadow_FlatLines");
    } else {
        SbName mode;
        switch (displayMode) {
        case 0:
            mode = SoFCUnifiedSelection::DisplayModeFlatLines;
            break;
        case 1:
            mode = SoFCUnifiedSelection::DisplayModeShaded;
            break;
        case 3:
            mode = SoFCUnifiedSelection::DisplayModeHiddenLine;
            break;
        default:
            mode = SoFCUnifiedSelection::DisplayModeAsIs;
            break;
        }
        if (owner->selectionRoot->overrideMode.getValue() != mode) {
            if (mode == SoFCUnifiedSelection::DisplayModeHiddenLine)
                initHiddenLineConfig(true);
            else
                owner->selectionRoot->overrideMode = mode;
        }
    }
    owner->getSoRenderManager()->setRenderMode(SoRenderManager::AS_IS);

    bool spotlight = _shadowParam<App::PropertyBool>(view, "SpotLight",
            ViewParams::docShadowSpotLight(), ViewParams::getShadowSpotLight());

    if(pcShadowGroup) {
        if((spotlight && pcShadowGroup->findChild(pcShadowSpotLight)<0)
            || (!spotlight && pcShadowGroup->findChild(pcShadowDirectionalLight)<0))
        {
            coinRemoveAllChildren(pcShadowGroup);
            auto superScene = static_cast<SoGroup*>(owner->getSoRenderManager()->getSceneGraph());
            int index = superScene->findChild(pcShadowGroup);
            if(index >= 0)
                superScene->replaceChild(index, owner->pcViewProviderRoot);
            pcShadowGroup.reset();
        }
    }
    if(!pcShadowGroup) {
        pcShadowGroup = new SoShadowGroup;
        // pcShadowGroup->renderCaching = SoSeparator::OFF;
        // pcShadowGroup->boundingBoxCaching = SoSeparator::OFF;

        if(!pcShadowDirectionalLight) {
            pcShadowDirectionalLight = new SoFCDirectionalLight;
            pcShadowDirectionalLight->getDragger()->addFinishCallback(onDragFinish, this);
        }

        if(!pcShadowSpotLight) {
            pcShadowSpotLight = new SoFCSpotLight;
            pcShadowSpotLight->getDragger()->addFinishCallback(onDragFinish, this);
        }

        auto shadowStyle = new SoShadowStyle;
        shadowStyle->style = SoShadowStyle::NO_SHADOWING;
        pcShadowGroup->addChild(shadowStyle);

        if(spotlight)
            pcShadowGroup->addChild(pcShadowSpotLight);
        else
            pcShadowGroup->addChild(pcShadowDirectionalLight);

        shadowStyle = new SoShadowStyle;
        shadowStyle->style = SoShadowStyle::CASTS_SHADOW_AND_SHADOWED;
        pcShadowGroup->addChild(shadowStyle);

        pcShadowPickStyle = new SoPickStyle;
        pcShadowGroup->addChild(pcShadowPickStyle);

        pcShadowGroup->addChild(owner->pcViewProviderRoot);

        if(!pcShadowGroundSwitch) {
            pcShadowGroundSwitch = new SoSwitch;
            pcShadowGroundSwitch->setName("ShadowGround");

            pcShadowGroundStyle = new SoShadowStyle;
            pcShadowGroundStyle->style = SoShadowStyle::SHADOWED;

            pcShadowMaterial = new SoMaterial;

            pcShadowGroundTextureCoords = new SoTextureCoordinate2;

            pcShadowGroundTexture = new SoTexture2;
            // pcShadowGroundTexture->model = SoMultiTextureImageElement::BLEND;

            pcShadowGroundCoords = new SoCoordinate3;

            pcShadowGround = new SoFaceSet;

            pcShadowGroundShapeHints = new SoShapeHints;
            pcShadowGroundShapeHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;

            auto pickStyle = new SoPickStyle;
            pickStyle->style = SoPickStyle::UNPICKABLE;

            auto tu = new SoTextureUnit;
            tu->unit = 1;

            pcShadowGroundLightModel = new SoLightModel;

            pcShadowGroundGroup = new SoSeparator;

            pcShadowGroundGroup->addChild(pcShadowGroundLightModel);
            pcShadowGroundGroup->addChild(pickStyle);
            pcShadowGroundGroup->addChild(pcShadowGroundShapeHints);
            pcShadowGroundGroup->addChild(pcShadowGroundTextureCoords);
            pcShadowGroundGroup->addChild(tu);

            // We deliberately insert the same SoTextureCoordinate2 twice.
            // The first one with default texture unit 0, and the second
            // one with unit 1. The reason for unit 1 is because unit 0
            // texture does not work with bump map (Coin3D bug?). The
            // reason for unit 0 texture coordinate is because Coin3D
            // crashes if there is at least one texture coordinate node,
            // but no unit 0 texture coordinate, with the following call
            // stack.
            //
            // SoMultiTextureCoordinateelement::get4()
            // SoMultiTextureCoordinateelement::get4()
            // SoFaceSet::generatePrimitives()
            // SoShape::validatePVCache()
            // SoShape::shouldGLRender()
            // ...
            //
            pcShadowGroundGroup->addChild(pcShadowGroundTextureCoords);

            pcShadowGroundGroup->addChild(pcShadowGroundTexture);
            pcShadowGroundGroup->addChild(pcShadowMaterial);
            pcShadowGroundGroup->addChild(pcShadowGroundCoords);
            pcShadowGroundGroup->addChild(pcShadowGroundStyle);

            // We need to add an polygon offset for the group here because there
            // is on in PartGui::ViewProviderPartExt, which means almost every
            // shape in freecad is displayed an offset face rendering. If we
            // don't add the same polygon offset here, the group may rendered on
            // top of the shapes because of the depth offset.
            //
            // However, Coin3D bumpmap rendering
            // (soshape_bumpmap::renderBumpMap()) in correctly uses a hard coded
            // polygon offset as well, which will cause even more visual
            // artifacts. The fix is simple in Coin3D is simple. Just get the
            // current polygon offset and add some extra.
            SoPolygonOffset* offset = new SoPolygonOffset();
            pcShadowGroundGroup->addChild(offset);

            pcShadowGroundGroup->addChild(pcShadowGround);

            pcShadowGroundSwitch->addChild(pcShadowGroundGroup);
        }

        pcShadowGroup->addChild(pcShadowGroundSwitch);

        // SoShadowGroup is currently incapable of supporting per object
        // lighting model setup. It only checks the setting on toggling shadow
        // enable/disable. So as a not so good work aournd, we put a shape
        // hints node before entering SoShadowGroup to provide a default light
        // model (LIGHT_MODEL_TWO_SIDEDE).
        pcShadowShapeHints = new SoShapeHints;
        pcShadowShapeHints->vertexOrdering = ViewParams::getForceSolidSingleSideLighting() ?
            SoShapeHints::UNKNOWN_ORDERING : SoShapeHints::COUNTERCLOCKWISE;
        pcShadowShapeHints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;

    }
    static const App::PropertyFloatConstraint::Constraints _precision_cstr(0.0,1.0,0.1);
    // pcShadowGroup->quality = _shadowParam<App::PropertyFloatConstraint>(
    //         owner, "Quality", 1.0f,
    //         [](App::PropertyFloatConstraint &prop) {
    //             if(!prop.getConstraints())
    //                 prop.setConstraints(&_precision_cstr);
    //         });

    pcShadowGroup->precision = _shadowParam<App::PropertyFloatConstraint>(view, "Precision",
            ViewParams::docShadowPrecision(), ViewParams::getShadowPrecision(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_precision_cstr);
            });

    SoLight *light;
    auto _dir = _shadowParam<App::PropertyVector>(
            view, "LightDirection", nullptr,
            Base::Vector3d(ViewParams::getShadowLightDirectionX(),
                            ViewParams::getShadowLightDirectionY(),
                            ViewParams::getShadowLightDirectionZ()));
    _dir.Normalize();
    SbVec3f dir(_dir.x,_dir.y,_dir.z);

    SbBox3f bbox;
    owner->getSceneBoundBox(bbox);

    static const App::PropertyPrecision::Constraints _epsilon_cstr(0.0,1000.0,1e-5);
    auto epsilon = _shadowParam<App::PropertyPrecision>(view, "Epsilon",
            ViewParams::docShadowEpsilon(), ViewParams::getShadowEpsilon(),
            [](App::PropertyFloatConstraint &prop) {
                if(prop.getConstraints() != &_epsilon_cstr)
                    prop.setConstraints(&_epsilon_cstr);
            });
    if (epsilon == 0.0)
        epsilon = 1e-5;
    pcShadowGroup->epsilon = epsilon;

    static const App::PropertyFloatConstraint::Constraints _threshold_cstr(0.0,1.0,0.1);
    pcShadowGroup->threshold = _shadowParam<App::PropertyFloatConstraint>(view, "Threshold",
            ViewParams::docShadowThreshold(), ViewParams::getShadowThreshold(),
            [](App::PropertyFloatConstraint &prop) {
                if(prop.getConstraints() != &_threshold_cstr)
                    prop.setConstraints(&_threshold_cstr);
            });

    if(spotlight) {
        light = pcShadowSpotLight;
        pcShadowSpotLight->direction = dir;
        Base::Vector3d initPos;
        if(isValidBBox(bbox)) {
            SbVec3f center = bbox.getCenter();
            initPos.x = center[0];
            initPos.y = center[1];
            initPos.z = center[2] + (_dir.z < 0 ? 1.0f : -1.0f) * (bbox.getMax()[2] - bbox.getMin()[2]);
        }
        auto pos = _shadowParam<App::PropertyVector>(view, "SpotLightPosition", nullptr, initPos);
        pcShadowSpotLight->location = SbVec3f(pos.x,pos.y,pos.z);
        static const App::PropertyFloatConstraint::Constraints _drop_cstr(-0.01,1.0,0.01);
        pcShadowSpotLight->dropOffRate =
            _shadowParam<App::PropertyFloatConstraint>(view, "SpotLightDropOffRate", nullptr, 0.0,
                [](App::PropertyFloatConstraint &prop) {
                    if(!prop.getConstraints())
                        prop.setConstraints(&_drop_cstr);
                });
        pcShadowSpotLight->cutOffAngle =
            M_PI * _shadowParam<App::PropertyAngle>(view, "SpotLightCutOffAngle", nullptr, 45.0) / 180.0;

        // pcShadowGroup->visibilityFlag = SoShadowGroup::ABSOLUTE_RADIUS;
        // pcShadowGroup->visibilityNearRadius = _shadowParam<App::PropertyFloat>(view, "SpotLightRadiusNear", -1.0);
        // pcShadowGroup->visibilityRadius = _shadowParam<App::PropertyFloat>(view, "SpotLightRadius", -1.0);
    } else {
        light = pcShadowDirectionalLight;
        pcShadowDirectionalLight->direction = dir;
        if(light->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
            static const App::PropertyFloatConstraint::Constraints _dist_cstr(-1.0,DBL_MAX,10.0);
            static_cast<SoShadowDirectionalLight*>(light)->maxShadowDistance =
                _shadowParam<App::PropertyFloatConstraint>(view, "MaxDistance",
                    ViewParams::docShadowMaxDistance(), ViewParams::getShadowMaxDistance(),
                    [](App::PropertyFloatConstraint &prop) {
                        if(!prop.getConstraints())
                            prop.setConstraints(&_dist_cstr);
                    });
        }
    }

    static const App::PropertyFloatConstraint::Constraints _cstr(0.0,1000.0,0.1);
    light->intensity = _shadowParam<App::PropertyFloatConstraint>(view, "LightIntensity",
            ViewParams::docShadowLightIntensity(), ViewParams::getShadowLightIntensity(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_cstr);
            });

    App::Color color = _shadowParam<App::PropertyColor>(view, "LightColor",
            ViewParams::docShadowLightColor(), App::Color((uint32_t)ViewParams::getShadowLightColor()));
    SbColor sbColor;
    float f;
    sbColor.setPackedValue(color.getPackedValue(),f);
    light->color = sbColor;

    color = _shadowParam<App::PropertyColor>(view, "GroundColor",
            ViewParams::docShadowGroundColor(), App::Color((uint32_t)ViewParams::getShadowGroundColor()));
    sbColor.setPackedValue(color.getPackedValue(),f);
    pcShadowMaterial->diffuseColor = sbColor;
    pcShadowMaterial->specularColor = SbColor(0,0,0);

    static const App::PropertyFloatConstraint::Constraints _transp_cstr(0.0,1.0,0.1);
    double transp = _shadowParam<App::PropertyFloatConstraint>(view, "GroundTransparency",
            ViewParams::docShadowGroundTransparency(), ViewParams::getShadowGroundTransparency(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_transp_cstr);
            });

    if(_shadowParam<App::PropertyBool>(view, "GroundBackFaceCull",
            ViewParams::docShadowGroundBackFaceCull(), ViewParams::getShadowGroundBackFaceCull()))
    {
        pcShadowGroundShapeHints->shapeType = SoShapeHints::SOLID;
        pcShadowGroundShapeHints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    } else {
        pcShadowGroundShapeHints->shapeType = SoShapeHints::UNKNOWN_SHAPE_TYPE;
        pcShadowGroundShapeHints->vertexOrdering = SoShapeHints::UNKNOWN_ORDERING;
    }

    pcShadowMaterial->transparency = transp;
    pcShadowGroundStyle->style = (transp == 1.0 ? 0x4 : 0) | SoShadowStyle::SHADOWED;

    if(_shadowParam<App::PropertyBool>(view, "ShowGround",
            ViewParams::docShadowShowGround(), ViewParams::getShadowShowGround()))
        pcShadowGroundSwitch->whichChild = 0;
    else
        pcShadowGroundSwitch->whichChild = -1;

    if(isValidBBox(bbox))
        updateShadowGround(bbox);

    pcShadowGroundTexture->filename = _shadowParam<App::PropertyFileIncluded>(view, "GroundTexture",
            ViewParams::docShadowGroundTexture(), ViewParams::getShadowGroundTexture().c_str());

    const char *bumpmap = _shadowParam<App::PropertyFileIncluded>(view, "GroundBumpMap",
            ViewParams::docShadowGroundBumpMap(), ViewParams::getShadowGroundBumpMap().c_str());
    if(bumpmap && bumpmap[0]) {
        if(!pcShadowGroundBumpMap) {
            pcShadowGroundBumpMap = new SoBumpMap;
        }
        pcShadowGroundBumpMap->filename = bumpmap;
        if (pcShadowGroundGroup->findChild(pcShadowGroundBumpMap) < 0) {
            int idx = pcShadowGroundGroup->findChild(pcShadowMaterial);
            if (idx >= 0)
                pcShadowGroundGroup->insertChild(pcShadowGroundBumpMap,idx);
        }
    } else if (pcShadowGroundBumpMap) {
        int idx = pcShadowGroundGroup->findChild(pcShadowGroundBumpMap);
        if (idx >= 0)
            pcShadowGroundGroup->removeChild(idx);
    }

    if(_shadowParam<App::PropertyBool>(view, "GroundShading",
            ViewParams::docShadowGroundShading(), ViewParams::getShadowGroundShading()))
        pcShadowGroundLightModel->model = SoLightModel::PHONG;
    else
        pcShadowGroundLightModel->model = SoLightModel::BASE_COLOR;

    SbBool isActive = TRUE;
    if (_shadowParam<App::PropertyBool>(view, "TransparentShadow",
            ViewParams::docShadowTransparentShadow(), ViewParams::getShadowTransparentShadow()))
        isActive |= 2;
    if (pcShadowGroup->isActive.getValue() != isActive)
        pcShadowGroup->isActive = isActive;

    auto superScene = static_cast<SoGroup*>(owner->getSoRenderManager()->getSceneGraph());
    int index = superScene->findChild(owner->pcViewProviderRoot);
    if(index >= 0) {
        superScene->replaceChild(index, pcShadowGroup);
        superScene->insertChild(pcShadowShapeHints, index);
        owner->inventorSelection->getRootPath()->append(pcShadowGroup);
    }
}

void View3DInventorViewer::setViewportCB(void*, SoAction* action)
{
    // Make sure to override the value set inside SoOffscreenRenderer::render()
    if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
        SoFCOffscreenRenderer& renderer = SoFCOffscreenRenderer::instance();
        const SbViewportRegion& vp = renderer.getViewportRegion();
        SoViewportRegionElement::set(action->getState(), vp);
        static_cast<SoGLRenderAction*>(action)->setViewportRegion(vp);
    }
}

void View3DInventorViewer::clearBufferCB(void*, SoAction* action)
{
    if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
        // do stuff specific for GL rendering here.
        glClear(GL_DEPTH_BUFFER_BIT);
    }
}

void View3DInventorViewer::setGLWidgetCB(void* userdata, SoAction* action)
{
    //FIXME: This causes the Coin error message:
    // Coin error in SoNode::GLRenderS(): GL error: 'GL_STACK_UNDERFLOW', nodetype:
    // Separator (set envvar COIN_GLERROR_DEBUGGING=1 and re-run to get more information)
    if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
        auto gl = static_cast<QWidget*>(userdata);
        SoGLWidgetElement::set(action->getState(), qobject_cast<QtGLWidget*>(gl));
    }
}

void View3DInventorViewer::handleEventCB(void* ud, SoEventCallback* n)
{
    auto that = static_cast<View3DInventorViewer*>(ud);
    SoGLRenderAction* glra = that->getSoRenderManager()->getGLRenderAction();
    SoAction* action = n->getAction();
    SoGLRenderActionElement::set(action->getState(), glra);
    SoGLWidgetElement::set(action->getState(), qobject_cast<QtGLWidget*>(that->getGLWidget()));
}

void View3DInventorViewer::setGradientBackground(View3DInventorViewer::Background grad)
{
    int whichChild = 0;
    switch (grad) {
    case Background::NoGradient:
        whichChild = -1;
        break;
    case Background::LinearGradient:
        pcBackGround->setGradient(SoFCBackgroundGradient::LINEAR);
        break;
    case Background::RadialGradient:
        pcBackGround->setGradient(SoFCBackgroundGradient::RADIAL);
        break;
    }
    if(pcBackGroundSwitch->whichChild.getValue() != whichChild)
        pcBackGroundSwitch->whichChild.setValue(whichChild);
}

bool View3DInventorViewer::hasGradientBackground() const
{
    return getGradientBackground() != Background::NoGradient;
}

View3DInventorViewer::Background View3DInventorViewer::getGradientBackground() const
{
    if (pcBackGroundSwitch->whichChild.getValue() == -1) {
        return Background::NoGradient;
    }

    if (pcBackGround->getGradient() == SoFCBackgroundGradient::LINEAR) {
        return Background::LinearGradient;
    }

    return Background::RadialGradient;
}

void View3DInventorViewer::setGradientBackgroundColor(const SbColor& fromColor,
                                                      const SbColor& toColor)
{
    pcBackGround->setColorGradient(fromColor, toColor);
}

void View3DInventorViewer::setGradientBackgroundColor(const SbColor& fromColor,
                                                      const SbColor& toColor,
                                                      const SbColor& midColor)
{
    pcBackGround->setColorGradient(fromColor, toColor, midColor);
}

void View3DInventorViewer::setEnabledFPSCounter(bool on)
{
    fpsEnabled = on;
}

void View3DInventorViewer::setEnabledVBO(bool on)
{
    vboEnabled = on;
}

bool View3DInventorViewer::isEnabledVBO() const
{
    return vboEnabled;
}

void View3DInventorViewer::setRenderCache(int mode)
{
    static int canAutoCache = -1;

    if (mode<0) {
        // Work around coin bug of unmatched call of
        // SoGLLazyElement::begin/endCaching() when on top rendering
        // transparent object with SORTED_OBJECT_SORTED_TRIANGLE_BLEND
        // transparency type.
        //
        // For more details see:
        // https://forum.freecad.org/viewtopic.php?f=18&t=43305&start=10#p412537
        coin_setenv("COIN_AUTO_CACHING", "0", TRUE);

        int setting = ViewParams::getRenderCache();
        if (mode == -2) {
            if (pcViewProviderRoot && setting != 1)
                pcViewProviderRoot->renderCaching = SoSeparator::ON;
            mode = 2;
        }
        else {
            if (pcViewProviderRoot)
                pcViewProviderRoot->renderCaching = SoSeparator::AUTO;
            mode = setting;
        }
    }

    if (canAutoCache < 0) {
        const char *env = coin_getenv("COIN_AUTO_CACHING");
        canAutoCache = env ? atoi(env) : 1;
    }

    // If coin auto cache is disabled, do not use 'Auto' render cache mode, but
    // fallback to 'Distributed' mode.
    if (!canAutoCache && mode != 2 && mode != 3)
        mode = 1;

    auto caching = mode == 0 ? SoSeparator::AUTO :
                  (mode == 1 ? SoSeparator::ON :
                               SoSeparator::OFF);

    if (this->selectionRoot)
        this->selectionRoot->renderCaching = mode == 3 ?
            SoSeparator::OFF : SoSeparator::ON;
    SoFCSeparator::setCacheMode(caching);
}

void View3DInventorViewer::setEnabledNaviCube(bool on)
{
    naviCubeEnabled = on;
    if (_pimpl->view)
        _pimpl->view->ShowNaviCube.setValue(on);
}

bool View3DInventorViewer::isEnabledNaviCube() const
{
    return naviCubeEnabled;
}

void View3DInventorViewer::setNaviCubeCorner(int c)
{
    if (naviCube)
        naviCube->setCorner(static_cast<NaviCube::Corner>(c));
}

NaviCube* View3DInventorViewer::getNaviCube() const
{
    return naviCube;
}

void View3DInventorViewer::setAxisCross(bool on)
{
    SoNode* scene = getSoRenderManager()->getSceneGraph();
    auto sep = static_cast<SoSeparator*>(scene);

    if (on) {
        if (!axisGroup) {
            axisCross = new Gui::SoShapeScale;
            auto axisKit = new Gui::SoAxisCrossKit();
            axisKit->set("xAxis.appearance.drawStyle", "lineWidth 2");
            axisKit->set("yAxis.appearance.drawStyle", "lineWidth 2");
            axisKit->set("zAxis.appearance.drawStyle", "lineWidth 2");
            axisCross->setPart("shape", axisKit);
            axisCross->scaleFactor = 1.0f;
            axisGroup = new SoSkipBoundingGroup;
            axisGroup->addChild(axisCross);

            sep->addChild(axisGroup);
        }
    }
    else {
        if (axisGroup) {
            sep->removeChild(axisGroup);
            axisGroup = nullptr;
        }
    }
}

bool View3DInventorViewer::hasAxisCross()
{
    return axisGroup;
}

void View3DInventorViewer::setNavigationType(Base::Type t)
{
    if (this->navigation && this->navigation->getTypeId() == t)
        return; // nothing to do

    Base::Type type = Base::Type::getTypeIfDerivedFrom(t.getName(), NavigationStyle::getClassTypeId());
    auto ns = static_cast<NavigationStyle*>(type.createInstance());
    // createInstance could return a null pointer
    if (!ns) {
#if FC_DEBUG
        SoDebugError::postWarning("View3DInventorViewer::setNavigationType",
                                  "Navigation object must be of type NavigationStyle.");
#endif // FC_DEBUG
        return;
    }

    if (this->navigation) {
        ns->operator = (*this->navigation);
        delete this->navigation;
    }
    this->navigation = ns;
    this->navigation->setViewer(this);
}

NavigationStyle* View3DInventorViewer::navigationStyle() const
{
    return this->navigation;
}

SoDirectionalLight* View3DInventorViewer::getBacklight() const
{
    return this->backlight;
}

void View3DInventorViewer::setBacklight(SbBool on)
{
    this->backlight->on = on;
}

SbBool View3DInventorViewer::isBacklight() const
{
    return this->backlight->on.getValue();
}

void View3DInventorViewer::setSceneGraph(SoNode* root)
{
    inherited::setSceneGraph(root);
    if (!root) {
        _ViewProviderSet.clear();
        editViewProvider = nullptr;
    }

    SoSearchAction sa;
    sa.setNode(this->backlight);
    //we want the rendered scene with all lights and cameras, viewer->getSceneGraph would return
    //the geometry scene only
    SoNode* scene = this->getSoRenderManager()->getSceneGraph();
    if (scene && scene->getTypeId().isDerivedFrom(SoSeparator::getClassTypeId())) {
        sa.apply(scene);
        if (!sa.getPath())
            static_cast<SoSeparator*>(scene)->insertChild(this->backlight, 0);
    }
}

void View3DInventorViewer::savePicture(int w, int h, int s, const QColor& bg, QImage& img) const
{
    // Save picture methods:
    // FramebufferObject -- viewer renders into FBO (no offscreen)
    // CoinOffscreenRenderer -- Coin's offscreen rendering method
    // Otherwise (Default) -- Qt's FBO used for offscreen rendering
    std::string saveMethod = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/View")->GetASCII("SavePicture");

    if (selectionRoot->getRenderManager()) {
        if (saveMethod != "FramebufferObject" && saveMethod != "GrabFramebuffer")
            saveMethod = "FramebufferObject";
    }

    bool useFramebufferObject = false;
    bool useGrabFramebuffer = false;
    bool useCoinOffscreenRenderer = false;
    if (saveMethod == "FramebufferObject") {
        useFramebufferObject = true;
    }
    else if (saveMethod == "GrabFramebuffer") {
        useGrabFramebuffer = true;
    }
    else if (saveMethod == "CoinOffscreenRenderer") {
        useCoinOffscreenRenderer = true;
    }

    if (useFramebufferObject) {
        auto self = const_cast<View3DInventorViewer*>(this);
        self->imageFromFramebuffer(w, h, s, bg, img);
        return;
    }
    else if (useGrabFramebuffer) {
        auto self = const_cast<View3DInventorViewer*>(this);
        img = self->grabFramebuffer();
        img = img.mirrored();
        img = img.scaledToWidth(w);
        return;
    }

    // if no valid color use the current background
    bool useBackground = false;
    SbViewportRegion vp(getSoRenderManager()->getViewportRegion());

    if (w>0 && h>0)
        vp.setWindowSize((short)w, (short)h);

    //NOTE: To support pixels per inch we must use SbViewportRegion::setPixelsPerInch( ppi );
    //The default value is 72.0.
    //If we need to support grayscale images with must either use SoOffscreenRenderer::LUMINANCE or
    //SoOffscreenRenderer::LUMINANCE_TRANSPARENCY.

    SoCallback* cb = nullptr;

    // for an invalid color use the viewer's current background color
    QColor bgColor;
    if (!bg.isValid()) {
        if (!hasGradientBackground()) {
            bgColor = this->backgroundColor();
        }
        else {
            useBackground = true;
            cb = new SoCallback;
            cb->setCallback(clearBufferCB);
        }
    }
    else {
        bgColor = bg;
    }

    auto root = new SoSeparator;
    root->ref();

#if (COIN_MAJOR_VERSION >= 4)
    // The behaviour in Coin4 has changed so that when using the same instance of 'SoFCOffscreenRenderer'
    // multiple times internally the biggest viewport size is stored and set to the SoGLRenderAction.
    // The trick is to add a callback node and override the viewport size with what we want.
    if (useCoinOffscreenRenderer) {
        auto cbvp = new SoCallback;
        cbvp->setCallback(setViewportCB);
        root->addChild(cbvp);
    }
#endif

    SoCamera* camera = getSoRenderManager()->getCamera();

    if (useBackground) {
        root->addChild(backgroundroot);
        root->addChild(cb);
    }

    if (!this->shading) {
        auto lm = new SoLightModel;
        lm->model = SoLightModel::BASE_COLOR;
        root->addChild(lm);
    }

    root->addChild(getHeadlight());
    root->addChild(camera);
    auto gl = new SoCallback;
    gl->setCallback(setGLWidgetCB, this->getGLWidget());
    root->addChild(gl);
    root->addChild(pcViewProviderRoot);
    root->addChild(inventorSelection->getGroupOnTopSwitch());

    root->addChild(foregroundroot);

    try {
        // render the scene
        if (!useCoinOffscreenRenderer) {
            SoQtOffscreenRenderer renderer(vp);
            renderer.setNumPasses(s);
            renderer.setInternalTextureFormat(getInternalTextureFormat());
            if (bgColor.isValid())
                renderer.setBackgroundColor(SbColor4f(bgColor.redF(), bgColor.greenF(), bgColor.blueF(), bgColor.alphaF()));
            if (!renderer.render(root))
                throw Base::RuntimeError("Offscreen rendering failed");

            renderer.writeToImage(img);
            root->unref();
        }
        else {
            SoFCOffscreenRenderer& renderer = SoFCOffscreenRenderer::instance();
            renderer.setViewportRegion(vp);
            renderer.getGLRenderAction()->setSmoothing(true);
            renderer.getGLRenderAction()->setNumPasses(s);
            renderer.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
            if (bgColor.isValid())
                renderer.setBackgroundColor(SbColor(bgColor.redF(), bgColor.greenF(), bgColor.blueF()));
            if (!renderer.render(root))
                throw Base::RuntimeError("Offscreen rendering failed");

            renderer.writeToImage(img);
            root->unref();
        }

        if (!bgColor.isValid() || bgColor.alphaF() == 1.0) {
            QImage image(img.width(), img.height(), QImage::Format_RGB32);
            QPainter painter(&image);
            painter.fillRect(image.rect(), Qt::black);
            painter.drawImage(0, 0, img);
            painter.end();
            img = image;
        }
    }
    catch (...) {
        root->unref();
        throw; // re-throw exception
    }
}

void View3DInventorViewer::saveGraphic(int pagesize, const QColor& bgcolor, SoVectorizeAction* va) const
{
    if (bgcolor.isValid())
        va->setBackgroundColor(true, SbColor(bgcolor.redF(), bgcolor.greenF(), bgcolor.blueF()));

    float border = 10.0f;
    SbVec2s vpsize = this->getSoRenderManager()->getViewportRegion().getViewportSizePixels();
    float vpratio = ((float)vpsize[0]) / ((float)vpsize[1]);

    if (vpratio > 1.0f) {
        va->setOrientation(SoVectorizeAction::LANDSCAPE);
        vpratio = 1.0f / vpratio;
    }
    else {
        va->setOrientation(SoVectorizeAction::PORTRAIT);
    }

    va->beginStandardPage(SoVectorizeAction::PageSize(pagesize), border);

    // try to fill as much "paper" as possible
    SbVec2f size = va->getPageSize();

    float pageratio = size[0] / size[1];
    float xsize, ysize;

    if (pageratio < vpratio) {
        xsize = size[0];
        ysize = xsize / vpratio;
    }
    else {
        ysize = size[1];
        xsize = ysize * vpratio;
    }

    float offx = border + (size[0]-xsize) * 0.5f;
    float offy = border + (size[1]-ysize) * 0.5f;

    va->beginViewport(SbVec2f(offx, offy), SbVec2f(xsize, ysize));
    va->calibrate(this->getSoRenderManager()->getViewportRegion());

    va->apply(this->getSoRenderManager()->getSceneGraph());

    va->endViewport();
    va->endPage();
}

AbstractMouseSelection *
View3DInventorViewer::startSelection(View3DInventorViewer::SelectionMode mode)
{
    navigation->startSelection(NavigationStyle::SelectionMode(mode));
    return navigation->currentSelection();
}

void View3DInventorViewer::abortSelection()
{
    setCursorEnabled(true);
    navigation->abortSelection();
}

void View3DInventorViewer::stopSelection()
{
    setCursorEnabled(true);
    navigation->stopSelection();
}

bool View3DInventorViewer::isSelecting() const
{
    return navigation->isSelecting();
}

const std::vector<SbVec2s>& View3DInventorViewer::getPolygon(SelectionRole* role) const
{
    return navigation->getPolygon(role);
}

void View3DInventorViewer::setSelectionEnabled(const SbBool enable)
{
    selectionRoot->selectionRole.setValue(enable);
}

SbBool View3DInventorViewer::isSelectionEnabled() const
{
    return selectionRoot->selectionRole.getValue();
}

SbVec2f View3DInventorViewer::screenCoordsOfPath(SoPath* path) const
{
    // Generate a matrix (well, a SoGetMatrixAction) that
    // moves us to the picked object's coordinate space.
    SoGetMatrixAction gma(getSoRenderManager()->getViewportRegion());
    gma.apply(path);

    // Use that matrix to translate the origin in the picked
    // object's coordinate space into object space
    SbVec3f imageCoords(0, 0, 0);
    SbMatrix m = gma.getMatrix().transpose();
    m.multMatrixVec(imageCoords, imageCoords);

    // Now, project the object space coordinates of the object
    // into "normalized" screen coordinates.
    SbViewVolume  vol = getSoRenderManager()->getCamera()->getViewVolume();
    vol.projectToScreen(imageCoords, imageCoords);

    // Translate "normalized" screen coordinates to pixel coords.
    //
    // Note: for some reason, projectToScreen() doesn't seem to
    // handle non-square viewports properly.  The X and Y are
    // scaled such that [0,1] fits within the smaller of the window
    // width or height.  For instance, in a window that's 400px
    // tall and 800px wide, the Y will be within [0,1], but X can
    // vary within [-0.5,1.5]...
    int width = getGLWidget()->width(),
        height = getGLWidget()->height();

    if (width >= height) {
        // "Landscape" orientation, to square
        imageCoords[0] *= height;
        imageCoords[0] += (width-height) / 2.0;
        imageCoords[1] *= height;

    }
    else {
        // "Portrait" orientation
        imageCoords[0] *= width;
        imageCoords[1] *= width;
        imageCoords[1] += (height-width) / 2.0;
    }

    return SbVec2f(imageCoords[0], imageCoords[1]);
}

std::vector<SbVec2f> View3DInventorViewer::getGLPolygon(const std::vector<SbVec2s>& pnts) const
{
    const SbViewportRegion &vp = this->getSoRenderManager()->getViewportRegion();
    const SbVec2s &winSize = vp.getWindowSize();
    short w, h;
    winSize.getValue(w, h);
    const SbVec2s &sp = vp.getViewportSizePixels();
    const SbVec2s &op = vp.getViewportOriginPixels();
    const SbVec2f &vpSize = vp.getViewportSize();
    float dX, dY;
    vpSize.getValue(dX, dY);
    float fRatio = vp.getViewportAspectRatio();

    std::vector<SbVec2f> poly;
    for (const auto & pnt : pnts) {
        SbVec2s loc = pnt - op;
        SbVec2f pos((float)loc[0] / (float)sp[0], (float)loc[1] / (float)sp[1]);
        float pX, pY;
        pos.getValue(pX, pY);

        // now calculate the real points respecting aspect ratio information
        //
        if (fRatio > 1.0f) {
            pX = (pX - 0.5f * dX) * fRatio + 0.5f * dX;
            pos.setValue(pX, pY);
        }
        else if (fRatio < 1.0f) {
            pY = (pY - 0.5f * dY) / fRatio + 0.5f * dY;
            pos.setValue(pX, pY);
        }

        poly.push_back(pos);
    }

    return poly;
}

std::vector<SbVec2f> View3DInventorViewer::getGLPolygon(SelectionRole* role) const
{
    const std::vector<SbVec2s>& pnts = navigation->getPolygon(role);
    return getGLPolygon(pnts);
}

// defined in SoFCDB.cpp
extern SoNode* replaceSwitchesInSceneGraph(SoNode*);

void View3DInventorViewer::dump(const char *filename, bool onlyVisible) const
{
    SoGetPrimitiveCountAction action;
    action.setCanApproximate(true);

    SoNode *node;
    if(overrideMode == "Shadow" && _pimpl->pcShadowGroup)
        node = _pimpl->pcShadowGroup;
    else
        node = pcViewProviderRoot;

    action.apply(node);
    if (onlyVisible) {
        node = replaceSwitchesInSceneGraph(node);
        node->ref();
    }

    if ( action.getTriangleCount() > 100000 || action.getPointCount() > 30000 || action.getLineCount() > 10000 )
        dumpToFile(node, filename, true);
    else
        dumpToFile(node, filename, false);

    if (onlyVisible) {
        node->unref();
    }
}

bool View3DInventorViewer::dumpToFile(SoNode* node, const char* filename, bool binary) const
{
    bool ret = false;
    Base::FileInfo fi(filename);

    if (fi.hasExtension("idtf") || fi.hasExtension("svg")) {
        int ps=4;
        QColor c = Qt::white;
        std::unique_ptr<SoVectorizeAction> vo;

        if (fi.hasExtension("svg")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeSVGAction());
        }
        else if (fi.hasExtension("idtf")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeU3DAction());
        }
        else if (fi.hasExtension("ps") || fi.hasExtension("eps")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoVectorizePSAction());
        }
        else {
            throw Base::ValueError("Not supported vector graphic");
        }

        SoVectorOutput* out = vo->getOutput();
        if (!out || !out->openFile(filename)) {
            std::ostringstream a_out;
            a_out << "Cannot open file '" << filename << "'";
            throw Base::FileSystemError(a_out.str());
        }

        saveGraphic(ps,c,vo.get());
        out->closeFile();
    }
    else {
        // Try VRML and Inventor format
        ret = SoFCDB::writeToFile(node, filename, binary);
    }

    return ret;
}

/**
 * Sets the SoFCInteractiveElement to \a true.
 */
void View3DInventorViewer::interactionStartCB(void*, SoQTQuarterAdaptor* viewer)
{
    SoGLRenderAction* glra = viewer->getSoRenderManager()->getGLRenderAction();
    SoFCInteractiveElement::set(glra->getState(), viewer->getSceneGraph(), true);
}

/**
 * Sets the SoFCInteractiveElement to \a false and forces a redraw.
 */
void View3DInventorViewer::interactionFinishCB(void*, SoQTQuarterAdaptor* viewer)
{
    SoGLRenderAction* glra = viewer->getSoRenderManager()->getGLRenderAction();
    SoFCInteractiveElement::set(glra->getState(), viewer->getSceneGraph(), false);
    viewer->redraw();
}

/**
 * Logs the type of the action that traverses the Inventor tree.
 */
void View3DInventorViewer::interactionLoggerCB(void*, SoAction* action)
{
    Base::Console().Log("%s\n", action->getTypeId().getName().getString());
}

void View3DInventorViewer::addGraphicsItem(GLGraphicsItem* item)
{
    this->graphicsItems.push_back(item);
}

void View3DInventorViewer::removeGraphicsItem(GLGraphicsItem* item)
{
    this->graphicsItems.remove(item);
}

std::list<GLGraphicsItem*> View3DInventorViewer::getGraphicsItems() const
{
    return graphicsItems;
}

std::list<GLGraphicsItem*> View3DInventorViewer::getGraphicsItemsOfType(const Base::Type& type) const
{
    std::list<GLGraphicsItem*> items;
    for (std::list<GLGraphicsItem*>::const_iterator it = this->graphicsItems.begin(); it != this->graphicsItems.end(); ++it) {
        if ((*it)->isDerivedFrom(type))
            items.push_back(*it);
    }

    return items;
}

void View3DInventorViewer::clearGraphicsItems()
{
    this->graphicsItems.clear();
}

int View3DInventorViewer::getNumSamples()
{
    int samples = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/View")->GetInt("AntiAliasing", 0);

    switch (samples) {
    case View3DInventorViewer::MSAA2x:
        return 2;
    case View3DInventorViewer::MSAA4x:
        return 4;
    case View3DInventorViewer::MSAA8x:
        return 8;
    case View3DInventorViewer::Smoothing:
        return 1;
    default:
        return 0;
    }
}

GLenum View3DInventorViewer::getInternalTextureFormat() const
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/View");
    std::string format = hGrp->GetASCII("InternalTextureFormat", "Default");

    if (format == "GL_RGB") {
        return GL_RGB;
    }
    else if (format == "GL_RGBA") {
        return GL_RGBA;
    }
    else if (format == "GL_RGB8") {
        return GL_RGB8;
    }
    else if (format == "GL_RGBA8") {
        return GL_RGBA8;
    }
    else if (format == "GL_RGB10") {
        return GL_RGB10;
    }
    else if (format == "GL_RGB10_A2") {
        return GL_RGB10_A2;
    }
    else if (format == "GL_RGB16") {
        return GL_RGB16;
    }
    else if (format == "GL_RGBA16") {
        return GL_RGBA16;
    }
    else if (format == "GL_RGB32F") {
        return GL_RGB32F_ARB;
    }
    else if (format == "GL_RGBA32F") {
        return GL_RGBA32F_ARB;
    }
    else {
        QOpenGLFramebufferObjectFormat fboFormat;
        return fboFormat.internalTextureFormat();
    }
}

void View3DInventorViewer::setRenderType(const RenderType type)
{
    renderType = type;

    glImage = QImage();
    if (type != Framebuffer) {
        delete framebuffer;
        framebuffer = nullptr;
    }

    switch (type) {
    case Native:
        break;
    case Framebuffer:
        if (!framebuffer) {
            const SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();
            SbVec2s size = vp.getViewportSizePixels();
            int width = size[0];
            int height = size[1];

            auto gl = static_cast<QtGLWidget*>(this->viewport());
            gl->makeCurrent();
            QOpenGLFramebufferObjectFormat fboFormat;
            fboFormat.setSamples(getNumSamples());
            fboFormat.setAttachment(QtGLFramebufferObject::CombinedDepthStencil);
            auto fbo = new QtGLFramebufferObject(width, height, fboFormat);
            if (fbo->format().samples() > 0) {
                renderToFramebuffer(fbo);
                framebuffer = new QtGLFramebufferObject(fbo->size());
                // this is needed to be able to render the texture later
                QOpenGLFramebufferObject::blitFramebuffer(framebuffer, fbo);
                delete fbo;
            }
            else {
                renderToFramebuffer(fbo);
                framebuffer = fbo;
            }
        }
        break;
    case Image:
        {
            glImage = grabFramebuffer();
        }
        break;
    }
}

View3DInventorViewer::RenderType View3DInventorViewer::getRenderType() const
{
    return this->renderType;
}

QImage View3DInventorViewer::grabFramebuffer()
{
    auto gl = static_cast<QtGLWidget*>(this->viewport());
    gl->makeCurrent();

    QImage res;
    const SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();
    SbVec2s size = vp.getViewportSizePixels();
    int width = size[0];
    int height = size[1];

    int samples = getNumSamples();
    if (samples == 0) {
        // if anti-aliasing is off we can directly use glReadPixels
        QImage img(QSize(width, height), QImage::Format_RGB32);
        glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, img.bits());
        res = img;
    }
    else {
        QOpenGLFramebufferObjectFormat fboFormat;
        fboFormat.setSamples(getNumSamples());
        fboFormat.setAttachment(QOpenGLFramebufferObject::Depth);
        fboFormat.setTextureTarget(GL_TEXTURE_2D);
        fboFormat.setInternalTextureFormat(getInternalTextureFormat());

        QOpenGLFramebufferObject fbo(width, height, fboFormat);
        renderToFramebuffer(&fbo);

        res = fbo.toImage(false);

        QImage image(res.width(), res.height(), QImage::Format_RGB32);
        QPainter painter(&image);
        painter.fillRect(image.rect(),Qt::black);
        painter.drawImage(0, 0, res);
        painter.end();
        res = image;
    }

    return res;
}

void View3DInventorViewer::imageFromFramebuffer(int width, int height, int samples,
                                                const QColor& bgcolor, QImage& img)
{
    auto gl = static_cast<QtGLWidget*>(this->viewport());
    gl->makeCurrent();

    const QtGLContext* context = QtGLContext::currentContext();
    if (!context) {
        Base::Console().Warning("imageFromFramebuffer failed because no context is active\n");
        return;
    }

    QtGLFramebufferObjectFormat fboFormat;
    fboFormat.setSamples(samples);
    fboFormat.setAttachment(QtGLFramebufferObject::CombinedDepthStencil);
    fboFormat.setInternalTextureFormat(getInternalTextureFormat());

    QtGLFramebufferObject fbo(width, height, fboFormat);

    const QColor col = backgroundColor();
    auto grad = getGradientBackground();

    if (bgcolor.isValid()) {
        setBackgroundColor(bgcolor);
        setGradientBackground(Background::NoGradient);
    }
    renderToFramebuffer(&fbo);
    setBackgroundColor(col);
    setGradientBackground(grad);
    img = fbo.toImage();
}

void View3DInventorViewer::renderToFramebuffer(QtGLFramebufferObject* fbo)
{
    static_cast<QtGLWidget*>(this->viewport())->makeCurrent();
    fbo->bind();
    int width = fbo->size().width();
    int height = fbo->size().height();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);

    const QColor col = this->backgroundColor();
    glViewport(0, 0, width, height);
    glClearColor(col.redF(), col.greenF(), col.blueF(), col.alphaF());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SoBoxSelectionRenderAction gl(SbViewportRegion(width, height));
    // When creating a new GL render action we have to copy over the cache context id
    // For further details see init().
    uint32_t id = this->getSoRenderManager()->getGLRenderAction()->getCacheContext();
    gl.setCacheContext(id);
    gl.setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);

    gl.apply(this->backgroundroot);
    // The render action of the render manager has set the depth function to GL_LESS
    // while creating a new render action has it set to GL_LEQUAL. So, in order to get
    // the exact same result set it explicitly to GL_LESS.
    glDepthFunc(GL_LESS);
    gl.apply(this->getSoRenderManager()->getSceneGraph());
    gl.apply(this->foregroundroot);

    if (this->axiscrossEnabled) {
        this->drawAxisCross();
    }

    fbo->release();
}

void View3DInventorViewer::actualRedraw()
{
    switch (renderType) {
    case Native:
        if (guiDocument && guiDocument->getDocument()->testStatus(App::Document::Recomputing))
            break;
        renderScene();
        break;
    case Framebuffer:
        renderFramebuffer();
        break;
    case Image:
        renderGLImage();
        break;
    }
}

void View3DInventorViewer::renderFramebuffer()
{
    const SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();
    SbVec2s size = vp.getViewportSizePixels();

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glViewport(0, 0, size[0], size[1]);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, this->framebuffer->texture());
    glColor3f(1.0, 1.0, 1.0);

    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(-1.0, -1.0f);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(-1.0f, 1.0f);
    glEnd();

    printDimension();
    navigation->redraw();

    for (std::list<GLGraphicsItem*>::iterator it = this->graphicsItems.begin(); it != this->graphicsItems.end(); ++it)
        (*it)->paintGL();

    if (naviCubeEnabled)
        naviCube->drawNaviCube();

    glPopAttrib();
}

void View3DInventorViewer::renderGLImage()
{
    const SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();
    SbVec2s size = vp.getViewportSizePixels();

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glViewport(0, 0, size[0], size[1]);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, size[0], 0, size[1], 0, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    glRasterPos2f(0,0);
    glDrawPixels(glImage.width(),glImage.height(),GL_BGRA,GL_UNSIGNED_BYTE,glImage.bits());

    printDimension();
    navigation->redraw();

    for (std::list<GLGraphicsItem*>::iterator it = this->graphicsItems.begin(); it != this->graphicsItems.end(); ++it)
        (*it)->paintGL();

    if (naviCubeEnabled)
        naviCube->drawNaviCube();

    glPopAttrib();
}

void View3DInventorViewer::onGetBoundingBox(SoGetBoundingBoxAction *action)
{
    if (_pimpl->renderer) {
        float xmin, ymin, zmin, xmax, ymax, zmax;
        if (_pimpl->renderer->boundBox(xmin, ymin, zmin, xmax, ymax, zmax))
            action->extendBy(SbBox3f(xmin, ymin, zmin, xmax, ymax, zmax));
    }
}

void View3DInventorViewer::setRendererType(const std::string &type)
{
    if (ViewParams::getRenderCache() != 3)
        _pimpl->renderer.reset();
    else if (!_pimpl->renderer || _pimpl->renderer->type() != type) {
        _pimpl->renderer = RendererFactory::create(
                type, qobject_cast<QOpenGLWidget*>(getGLWidget()));
        getSoRenderManager()->scheduleRedraw();
    }
}

// #define ENABLE_GL_DEPTH_RANGE
// The calls of glDepthRange inside renderScene() causes problems with transparent objects
// so that's why it is disabled now: http://forum.freecad.org/viewtopic.php?f=3&t=6037&hilit=transparency

// Documented in superclass. Overrides this method to be able to draw
// the axis cross, if selected, and to keep a continuous animation
// upon spin.
void View3DInventorViewer::renderScene()
{
    // Must set up the OpenGL viewport manually, as upon resize
    // operations, Coin won't set it up until the SoGLRenderAction is
    // applied again. And since we need to do glClear() before applying
    // the action..
    const SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();
    SbVec2s origin = vp.getViewportOriginPixels();
    SbVec2s size = vp.getViewportSizePixels();
    glViewport(origin[0], origin[1], size[0], size[1]);

    bool restoreGradient = false;

    QColor col;
    auto grad = getGradientBackground();
    if(overrideBGColor) {
        col = App::Color(overrideBGColor).asValue<QColor>();
        if(hasGradientBackground()) {
            setGradientBackground(Background::NoGradient);
            restoreGradient = true;
        }
    } else
        col = this->backgroundColor();

    SoCamera* cam = getSoRenderManager()->getCamera();
    if (cam && _pimpl->renderer) {
        SbMatrix viewMat, projMat;
        const SbViewportRegion vp = getSoRenderManager()->getViewportRegion();
        SbViewVolume vol = cam->getViewVolume(vp.getViewportAspectRatio());
        vol.getMatrices(viewMat, projMat);
        if (_pimpl->renderer->render(col, &viewMat.getValue(), &projMat.getValue()))
            getSoRenderManager()->scheduleRedraw();
    } else {
        glClearColor(col.redF(), col.greenF(), col.blueF(), 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    glEnable(GL_DEPTH_TEST);

#if defined(ENABLE_GL_DEPTH_RANGE)
    // using 90% of the z-buffer for the background and the main node
    glDepthRange(0.1,1.0);
#endif

    // Render our scenegraph with the image.
    SoGLRenderAction* glra = this->getSoRenderManager()->getGLRenderAction();
    SoState* state = glra->getState();
    SoGLWidgetElement::set(state, qobject_cast<QtGLWidget*>(this->getGLWidget()));
    SoGLRenderActionElement::set(state, glra);
    SoGLVBOActivatedElement::set(state, this->vboEnabled);
    drawSingleBackground(col);
    glra->apply(this->backgroundroot);

    navigation->updateAnimation();

    SoBoxSelectionRenderAction *glbra = nullptr;
    if(glra->isOfType(SoBoxSelectionRenderAction::getClassTypeId())) {
        glbra = static_cast<SoBoxSelectionRenderAction*>(glra);
        glbra->checkRootNode(this->getSoRenderManager()->getSceneGraph());
    }
    try {
        // Render normal scenegraph.
        inherited::actualRedraw();
    }
    catch (const Base::MemoryException&) {
        // FIXME: If this exception appears then the background and camera position get broken somehow. (Werner 2006-02-01)
        for (std::set<ViewProvider*>::iterator it = _ViewProviderSet.begin(); it != _ViewProviderSet.end(); ++it)
            (*it)->hide();

        inherited::actualRedraw();
        QMessageBox::warning(parentWidget(), QObject::tr("Out of memory"),
                             QObject::tr("Not enough memory available to display the data."));
    }
    if (glbra) {
        glbra->checkRootNode(nullptr);
    }

#if defined (ENABLE_GL_DEPTH_RANGE)
    // using 10% of the z-buffer for the foreground node
    glDepthRange(0.0,0.1);
#endif

    // Render overlay front scenegraph.
    glra->apply(this->foregroundroot);

    if (this->axiscrossEnabled) {
        this->drawAxisCross();
    }

#if defined (ENABLE_GL_DEPTH_RANGE)
    // using the main portion of z-buffer again (for frontbuffer highlighting)
    glDepthRange(0.1,1.0);
#endif

    // Immediately reschedule to get continuous spin animation.
    if (this->isAnimating()) {
        this->getSoRenderManager()->scheduleRedraw();
    } else
        _pimpl->onRender();

    printDimension();
    navigation->redraw();

    for (std::list<GLGraphicsItem*>::iterator it = this->graphicsItems.begin(); it != this->graphicsItems.end(); ++it)
        (*it)->paintGL();

    //fps rendering
    if (fpsEnabled) {
        static FC_COIN_THREAD_LOCAL std::ostringstream stream;
        stream.str("");
        stream.precision(1);
        stream.setf(std::ios::fixed | std::ios::showpoint);
        stream << framesPerSecond[0] << " ms / " << framesPerSecond[1] << " fps";

        if (auto manager = selectionRoot->getRenderManager()) {
            auto stats = manager->getRenderStatistics();
            if (stats)
                stream << ". " << stats;
        }

        draw2DString(stream.str().c_str(), SbVec2s(10,10), SbVec2f(0.1f,0.1f));
    }

    if (naviCubeEnabled)
        naviCube->drawNaviCube();

    if(restoreGradient)
        setGradientBackground(grad);
}

void View3DInventorViewer::setSeekMode(SbBool on)
{
    // Overrides this method to make sure any animations are stopped
    // before we go into seek mode.

    // Note: this method is almost identical to the setSeekMode() in the
    // SoQtFlyViewer and SoQtPlaneViewer, so migrate any changes.

    if (this->isAnimating()) {
        this->stopAnimating();
    }

    inherited::setSeekMode(on);
    navigation->setViewingMode(on ? NavigationStyle::SEEK_WAIT_MODE :
                               (this->isViewing() ?
                                NavigationStyle::IDLE : NavigationStyle::INTERACT));
}

SbVec3f View3DInventorViewer::getCenterPointOnFocalPlane() const {
    SoCamera* cam = getSoRenderManager()->getCamera();
    if (!cam)
        return SbVec3f(0. ,0. ,0. );

    SbVec3f direction;
    cam->orientation.getValue().multVec(SbVec3f(0, 0, -1), direction);
    return cam->position.getValue() + cam->focalDistance.getValue() * direction;
}

float View3DInventorViewer::getMaxDimension() const {
    float fHeight = -1.0;
    float fWidth = -1.0;
    getDimensions(fHeight, fWidth);
    return std::max(fHeight, fWidth);
}

void View3DInventorViewer::getDimensions(float& fHeight, float& fWidth) const {
    SoCamera* camera = getSoRenderManager()->getCamera();
    if (!camera) // no camera there
        return;

    float aspectRatio = getViewportRegion().getViewportAspectRatio();

    SoType type = camera->getTypeId();
    if (type.isDerivedFrom(SoOrthographicCamera::getClassTypeId())) {
        fHeight = static_cast<SoOrthographicCamera*>(camera)->height.getValue();
        fWidth = fHeight;
    }
    else if (type.isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        float fHeightAngle = static_cast<SoPerspectiveCamera*>(camera)->heightAngle.getValue();
        fHeight = std::tan(fHeightAngle / 2.0) * 2.0 * camera->focalDistance.getValue();
        fWidth = fHeight;
    }

    if (aspectRatio > 1.0) {
        fWidth *= aspectRatio;
    }
    else {
        fHeight *= aspectRatio;
    }
}

void View3DInventorViewer::printDimension()
{
    float fHeight = -1.0;
    float fWidth = -1.0;
    getDimensions(fHeight, fWidth);

    QString dim;

    if (fWidth >= 0.0 && fHeight >= 0.0) {
        // Translate screen units into user's unit schema
        Base::Quantity qWidth(Base::Quantity::MilliMetre);
        Base::Quantity qHeight(Base::Quantity::MilliMetre);
        qWidth.setValue(fWidth);
        qHeight.setValue(fHeight);
        QString wStr = Base::UnitsApi::schemaTranslate(qWidth);
        QString hStr = Base::UnitsApi::schemaTranslate(qHeight);

        // Create final string and update window
        dim = QStringLiteral("%1 x %2").arg(wStr, hStr);
    }

    getMainWindow()->setPaneText(2, dim);
}

void View3DInventorViewer::selectAll()
{
    std::vector<App::DocumentObject*> objs;

    for (std::set<ViewProvider*>::iterator it = _ViewProviderSet.begin(); it != _ViewProviderSet.end(); ++it) {
        if ((*it)->getTypeId().isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
            auto vp = static_cast<ViewProviderDocumentObject*>(*it);
            App::DocumentObject* obj = vp->getObject();

            if (obj) objs.push_back(obj);
        }
    }

    if (!objs.empty())
        Gui::Selection().setSelection(objs);
}

bool View3DInventorViewer::processSoEvent(const SoEvent* ev)
{
    if (naviCubeEnabled && naviCube->processSoEvent(ev))
        return true;
    if (isRedirectedToSceneGraph()) {
        SbBool processed = inherited::processSoEvent(ev);

        if (!processed)
            processed = navigation->processEvent(ev);

        return processed;
    }

    if (ev->getTypeId().isDerivedFrom(SoKeyboardEvent::getClassTypeId())) {
        // filter out 'Q' and 'ESC' keys
        const auto ke = static_cast<const SoKeyboardEvent*>(ev);

        switch (ke->getKey()) {
        case SoKeyboardEvent::ESCAPE:
            if (QApplication::queryKeyboardModifiers() == Qt::ShiftModifier) {
                if (Selection().hasSelection()) {
                    Selection().clearSelection();
                    return true;
                }
            }
            else if (_pimpl->toggleDragger(0))
                return true;
            else if (Selection().hasPreselection()) {
                Selection().rmvPreselect();
                return true;
            }
            //fall through
        case SoKeyboardEvent::Q: // ignore 'Q' keys (to prevent app from being closed)
            return inherited::processSoEvent(ev);
        default:
            break;
        }
    } else if (ev->isOfType(SoMouseButtonEvent::getClassTypeId())
                && ev->wasShiftDown()
                && ev->wasCtrlDown())
    {
        if(static_cast<const SoMouseButtonEvent*>(ev)->getButton() == SoMouseButtonEvent::BUTTON4
                || static_cast<const SoMouseButtonEvent*>(ev)->getButton() == SoMouseButtonEvent::BUTTON5)
            return processSoEventBase(ev);
    }

    return navigation->processEvent(ev);
}

SbBool View3DInventorViewer::processSoEventBase(const SoEvent* const ev)
{
    return inherited::processSoEvent(ev);
}

SbVec3f View3DInventorViewer::getViewDirection() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)  // this is the default
        return SbVec3f(0,0,-1);

    SbVec3f projDir = cam->getViewVolume().getProjectionDirection();
    return projDir;
}

void View3DInventorViewer::setViewDirection(SbVec3f dir)
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (cam)
        cam->orientation.setValue(SbRotation(SbVec3f(0, 0, -1), dir));
}

SbVec3f View3DInventorViewer::getUpDirection() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)
        return SbVec3f(0,1,0);

    SbRotation camrot = cam->orientation.getValue();
    SbVec3f upvec(0, 1, 0); // init to default up vector
    camrot.multVec(upvec, upvec);
    return upvec;
}

SbRotation View3DInventorViewer::getCameraOrientation() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)
        return SbRotation(0,0,0,1); // this is the default

    return cam->orientation.getValue();
}

SbVec2f View3DInventorViewer::getNormalizedPosition(const SbVec2s& pnt) const
{
    const SbViewportRegion& vp = this->getSoRenderManager()->getViewportRegion();

    short x,y;
    pnt.getValue(x,y);
    SbVec2f siz = vp.getViewportSize();
    float dX, dY;
    siz.getValue(dX, dY);

    float fRatio = vp.getViewportAspectRatio();
    float pX = (float)x / float(vp.getViewportSizePixels()[0]);
    float pY = (float)y / float(vp.getViewportSizePixels()[1]);

    // now calculate the real points respecting aspect ratio information
    //
    if (fRatio > 1.0f) {
        pX = (pX - 0.5f*dX) * fRatio + 0.5f*dX;
    }
    else if (fRatio < 1.0f) {
        pY = (pY - 0.5f*dY) / fRatio + 0.5f*dY;
    }

    return SbVec2f(pX, pY);
}

SbVec3f View3DInventorViewer::getPointOnFocalPlane(const SbVec2s& pnt) const
{
    SbVec2f pnt2d = getNormalizedPosition(pnt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam)  // return invalid point
        return SbVec3f();

    SbViewVolume  vol = pCam->getViewVolume();

    float nearDist = pCam->nearDistance.getValue();
    float farDist = pCam->farDistance.getValue();
    float focalDist = pCam->focalDistance.getValue();

    if (focalDist < nearDist || focalDist > farDist)
        focalDist = 0.5f*(nearDist + farDist);

    SbLine line;
    SbVec3f pt;
    SbPlane focalPlane = vol.getPlane(focalDist);
    vol.projectPointToLine(pnt2d, line);
    focalPlane.intersect(line, pt);

    return pt;
}

SbVec2s View3DInventorViewer::getPointOnViewport(const SbVec3f& pnt) const
{
    const SbViewportRegion& vp = this->getSoRenderManager()->getViewportRegion();
    float fRatio = vp.getViewportAspectRatio();
    const SbVec2s& sp = vp.getViewportSizePixels();
    SbViewVolume vv = this->getSoRenderManager()->getCamera()->getViewVolume(fRatio);

    SbVec3f pt(pnt);
    vv.projectToScreen(pt, pt);

    auto x = short(std::roundf(pt[0] * sp[0]));
    auto y = short(std::roundf(pt[1] * sp[1]));

    return SbVec2s(x, y);
}

QPoint View3DInventorViewer::toQPoint(const SbVec2s& pnt) const
{
    const SbViewportRegion& vp = this->getSoRenderManager()->getViewportRegion();
    const SbVec2s& vps = vp.getViewportSizePixels();
    int xpos = pnt[0];
    int ypos = vps[1] - pnt[1] - 1;

    qreal dev_pix_ratio = devicePixelRatio();
    xpos = int(std::roundf(xpos / dev_pix_ratio));
    ypos = int(std::roundf(ypos / dev_pix_ratio));

    return QPoint(xpos, ypos);
}

SbVec2s View3DInventorViewer::fromQPoint(const QPoint& pnt) const
{
    const SbViewportRegion& vp = this->getSoRenderManager()->getViewportRegion();
    const SbVec2s& vps = vp.getViewportSizePixels();
    int xpos = pnt.x();
    int ypos = pnt.y();

    qreal dev_pix_ratio = devicePixelRatio();
    xpos = int(std::roundf(xpos * dev_pix_ratio));
    ypos = int(std::roundf(ypos * dev_pix_ratio));

    return SbVec2s(short(xpos), vps[1] - short(ypos) - 1);
}

void View3DInventorViewer::getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const
{
    SoCamera* pCam = getSoRenderManager()->getCamera();

    if (!pCam)  // just do nothing
        return;

    SbViewVolume vol = pCam->getViewVolume();

    // get the normal of the front clipping plane
    SbPlane nearPlane = vol.getPlane(vol.nearDist);
    float d = nearPlane.getDistanceFromOrigin();
    rcNormal = nearPlane.getNormal();
    rcNormal.normalize();
    float nx, ny, nz;
    rcNormal.getValue(nx, ny, nz);
    rcPt.setValue(d*rcNormal[0], d*rcNormal[1], d*rcNormal[2]);
}

void View3DInventorViewer::getFarPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const
{
    SoCamera* pCam = getSoRenderManager()->getCamera();

    if (!pCam)  // just do nothing
        return;

    SbViewVolume vol = pCam->getViewVolume();

    // get the normal of the back clipping plane
    SbPlane farPlane = vol.getPlane(vol.nearDist+vol.nearToFar);
    float d = farPlane.getDistanceFromOrigin();
    rcNormal = farPlane.getNormal();
    rcNormal.normalize();
    float nx, ny, nz;
    rcNormal.getValue(nx, ny, nz);
    rcPt.setValue(d*rcNormal[0], d*rcNormal[1], d*rcNormal[2]);
}

SbVec3f View3DInventorViewer::projectOnNearPlane(const SbVec2f& pt) const
{
    SbVec3f pt1, pt2;
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)  // return invalid point
        return SbVec3f();

    SbViewVolume vol = cam->getViewVolume();
    vol.projectPointToLine(pt, pt1, pt2);
    return pt1;
}

SbVec3f View3DInventorViewer::projectOnFarPlane(const SbVec2f& pt) const
{
    SbVec3f pt1, pt2;
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)  // return invalid point
        return SbVec3f();

    SbViewVolume vol = cam->getViewVolume();
    vol.projectPointToLine(pt, pt1, pt2);
    return pt2;
}

void View3DInventorViewer::projectPointToLine(const SbVec2s& pt, SbVec3f& pt1, SbVec3f& pt2) const
{
    SbVec2f pnt2d = getNormalizedPosition(pt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam)
        return;

    SbViewVolume vol = pCam->getViewVolume();
    vol.projectPointToLine(pnt2d, pt1, pt2);
}

void View3DInventorViewer::toggleClippingPlane(int toggle, bool beforeEditing,
        bool noManip, const Base::Placement &pla)
{
    if(pcClipPlane) {
        if(toggle<=0) {
            pcViewProviderRoot->removeChild(pcClipPlane);
            pcClipPlane->unref();
            pcClipPlane = nullptr;
        }
        return;
    }else if(toggle==0)
        return;

    Base::Vector3d dir;
    pla.getRotation().multVec(Base::Vector3d(0,0,-1),dir);
    Base::Vector3d base = pla.getPosition();

    if(!noManip) {
        auto clip = new SoClipPlaneManip;
        pcClipPlane = clip;
        SbBox3f box = getBoundingBox();

        if (isValidBBox(box)) {
            // adjust to overall bounding box of the scene
            clip->setValue(box, SbVec3f(dir.x,dir.y,dir.z), 1.0f);
        }
    }else
        pcClipPlane = new SoClipPlane;
    pcClipPlane->plane.setValue(
            SbPlane(SbVec3f(dir.x,dir.y,dir.z),SbVec3f(base.x,base.y,base.z)));
    pcClipPlane->ref();
    if(beforeEditing)
        pcViewProviderRoot->insertChild(pcClipPlane,0);
    else
        pcViewProviderRoot->insertChild(pcClipPlane,pcViewProviderRoot->findChild(pcEditingRoot)+1);
}

bool View3DInventorViewer::hasClippingPlane() const
{
    return !!pcClipPlane;
}

/**
 * This method picks the closest point to the camera in the underlying scenegraph
 * and returns its location and normal.
 * If no point was picked false is returned.
 */
bool View3DInventorViewer::pickPoint(const SbVec2s& pos,SbVec3f& point,SbVec3f& norm) const
{
    // attempting raypick in the event_cb() callback method
    SoRayPickAction rp(getSoRenderManager()->getViewportRegion());
    rp.setPoint(pos);
    rp.apply(getSoRenderManager()->getSceneGraph());
    SoPickedPoint* Point = rp.getPickedPoint();

    if (Point) {
        point = Point->getObjectPoint();
        norm  = Point->getObjectNormal();
        return true;
    }

    return false;
}

/**
 * This method is provided for convenience and does basically the same as method
 * above unless that it returns an SoPickedPoint object with additional information.
 * \note It is in the response of the client programmer to delete the returned
 * SoPickedPoint object.
 */
SoPickedPoint* View3DInventorViewer::pickPoint(const SbVec2s& pos) const
{
    SoRayPickAction rp(getSoRenderManager()->getViewportRegion());
    rp.setPoint(pos);
    rp.apply(getSoRenderManager()->getSceneGraph());

    // returns a copy of the point
    SoPickedPoint* pick = rp.getPickedPoint();
    //return (pick ? pick->copy() : 0); // needs the same instance of CRT under MS Windows
    return (pick ? new SoPickedPoint(*pick) : nullptr);
}

SoPickedPoint* View3DInventorViewer::getPickedPoint(SoEventCallback* n) const
{
    if (selectionRoot)
        return selectionRoot->getPickedPoint(n->getAction());
    auto pp = n->getPickedPoint();
    return pp?pp->copy():0;
}

std::vector<App::SubObjectT>
View3DInventorViewer::getPickedList(const SbVec2s &_pos, bool singlePick, bool mapCoords) const {
    SbVec2s pos;
    if (!mapCoords)
        pos = _pos;
    else {
        QPoint p = this->mapFromGlobal(QPoint(_pos[0],_pos[1]));
        pos[0] = p.x();
        pos[1] = this->height() - p.y() - 1;
        pos *= this->devicePixelRatio();
    }
    return selectionRoot->getPickedSelections(pos,
            getSoRenderManager()->getViewportRegion(), singlePick);
}

std::vector<App::SubObjectT>
View3DInventorViewer::getPickedList(bool singlePick) const {
    auto pos = QCursor::pos();
    return this->getPickedList(SbVec2s(pos.x(), pos.y()), singlePick, true);
}

SbBool View3DInventorViewer::pubSeekToPoint(const SbVec2s& pos)
{
    return this->seekToPoint(pos);
}

void View3DInventorViewer::pubSeekToPoint(const SbVec3f& pos)
{
    this->seekToPoint(pos);
}

void View3DInventorViewer::setCameraOrientation(const SbRotation& rot, SbBool moveTocenter)
{
    navigation->setCameraOrientation(rot, moveTocenter);
}

void View3DInventorViewer::setCameraType(SoType t)
{
    inherited::setCameraType(t);

    if (t.isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        // When doing a viewAll() for an orthographic camera and switching
        // to perspective the scene looks completely strange because of the
        // heightAngle. Setting it to 45 deg also causes an issue with a too
        // close camera but we don't have this other ugly effect.
        SoCamera* cam = this->getSoRenderManager()->getCamera();

        if(!cam)
            return;

        static_cast<SoPerspectiveCamera*>(cam)->heightAngle = (float)(M_PI / 4.0);
    }
}

namespace Gui {
    class CameraAnimation : public QVariantAnimation
    {
        SoCamera* camera;
        SbRotation startRot, endRot;
        SbVec3f startPos, endPos;

    public:
        CameraAnimation(SoCamera* camera, const SbRotation& rot, const SbVec3f& pos)
            : camera(camera), endRot(rot), endPos(pos)
        {
            startPos = camera->position.getValue();
            startRot = camera->orientation.getValue();
        }
        ~CameraAnimation() override
        {
        }
    protected:
        void updateCurrentValue(const QVariant & value) override
        {
            int steps = endValue().toInt();
            int curr = value.toInt();

            float s = static_cast<float>(curr)/static_cast<float>(steps);
            SbVec3f curpos = startPos * (1.0f-s) + endPos * s;
            SbRotation currot = SbRotation::slerp(startRot, endRot, s);
            camera->orientation.setValue(currot);
            camera->position.setValue(curpos);
        }
    };
}

void View3DInventorViewer::moveCameraTo(const SbRotation& rot, const SbVec3f& pos, int steps, int ms)
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (!cam)
        return;

    CameraAnimation anim(cam, rot, pos);
    anim.setDuration(Base::clamp<int>(ms,0,ViewParams::getMaxCameraAnimatePeriod()));
    anim.setStartValue(static_cast<int>(0));
    anim.setEndValue(steps);

    QEventLoop loop;
    QObject::connect(&anim, &CameraAnimation::finished, &loop, &QEventLoop::quit);
    anim.start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    cam->orientation.setValue(rot);
    cam->position.setValue(pos);
}

bool View3DInventorViewer::getSceneBoundBox(Base::BoundBox3d &box) const {
    if (!inventorSelection)
        return false;

    SoGetBoundingBoxAction action(this->getSoRenderManager()->getViewportRegion());
    SoSkipBoundingBoxElement::set(action.getState(), SoSkipBoundingGroup::EXCLUDE_BBOX);

    auto manager = selectionRoot->getRenderManager();
    SbBox3f bbox;
    if (manager && manager->getSceneNodeId() == selectionRoot->getNodeId())
        manager->getBoundingBox(bbox);
    if (isValidBBox(bbox)) {
        float minx,miny,minz,maxx,maxy,maxz;
        bbox.getBounds(minx, miny, minz, maxx, maxy, maxz);
        box.MinX = minx;
        box.MinY = miny;
        box.MinZ = minz;
        box.MaxX = maxx;
        box.MaxY = maxy;
        box.MaxZ = maxz;
    } else {
        if(guiDocument && ViewParams::getUseTightBoundingBox()) {
            for(int i=0;i<pcViewProviderRoot->getNumChildren();++i) {
                auto node = pcViewProviderRoot->getChild(i);
                auto vp = guiDocument->getViewProvider(node);
                if(!vp) {
                    action.apply(node);
                    auto bbox = action.getBoundingBox();
                    if(isValidBBox(bbox)) {
                        float minx,miny,minz,maxx,maxy,maxz;
                        bbox.getBounds(minx,miny,minz,maxx,maxy,maxz);
                        box.Add(Base::BoundBox3d(minx,miny,minz,maxx,maxy,maxz));
                    }
                    continue;
                }
                if(!vp->isVisible())
                    continue;
                auto sbox = vp->getBoundingBox(0,0,true,this);
                if(sbox.IsValid())
                    box.Add(sbox);
            }
        } else {
            action.apply(pcViewProviderRoot);
            auto bbox = action.getBoundingBox();
            if(isValidBBox(bbox)) {
                float minx,miny,minz,maxx,maxy,maxz;
                bbox.getBounds(minx,miny,minz,maxx,maxy,maxz);
                box.MinX = minx;
                box.MinY = miny;
                box.MinZ = minz;
                box.MaxX = maxx;
                box.MaxY = maxy;
                box.MaxZ = maxz;
            }
        }

        auto pcGroupOnTopSwitch = inventorSelection->getGroupOnTopSwitch();
        if (pcGroupOnTopSwitch) {
            action.apply(pcGroupOnTopSwitch);
            auto bbox = action.getBoundingBox();
            if(isValidBBox(bbox)) {
                float minx,miny,minz,maxx,maxy,maxz;
                bbox.getBounds(minx,miny,minz,maxx,maxy,maxz);
                box.Add(Base::BoundBox3d(minx,miny,minz,maxx,maxy,maxz));
            }
        }
    }

    if (pcEditingRoot) { 
        action.apply(pcEditingRoot);
        auto bbox = action.getBoundingBox();
        if(isValidBBox(bbox)) {
            float minx,miny,minz,maxx,maxy,maxz;
            bbox.getBounds(minx,miny,minz,maxx,maxy,maxz);
            box.Add(Base::BoundBox3d(minx,miny,minz,maxx,maxy,maxz));
        }
    }

    if (!box.IsValid())
        return false;

    // Coin3D camera seems stuck if zoomed to close because the boundbox is too
    // small. So, we limit the boundbox size
    const double minLength = 1e-7;
    if (std::fabs(box.MinX - box.MaxX) < minLength
            && std::fabs(box.MinY - box.MaxY) < minLength
            && std::fabs(box.MinZ - box.MaxZ) < minLength)
    {
        const double margin = 0.1;
        box.MinX -= margin;
        box.MinY -= margin;
        box.MinZ -= margin;
        box.MaxX += margin;
        box.MaxY += margin;
        box.MaxZ += margin;
    }
    return true;
}

SoGroup *View3DInventorViewer::getAuxSceneGraph() const
{
    return inventorSelection->getAuxRoot();
}

bool View3DInventorViewer::getSceneBoundBox(SbBox3f &box) const {
    Base::BoundBox3d fcbox;
    getSceneBoundBox(fcbox);
    if(!fcbox.IsValid())
        return false;
    box.setBounds(fcbox.MinX,fcbox.MinY,fcbox.MinZ,
                  fcbox.MaxX,fcbox.MaxY,fcbox.MaxZ);
    return true;
}

void View3DInventorViewer::animatedViewAll(const SbBox3f &box, int steps, int ms)
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (!cam)
        return;

    SbVec3f campos = cam->position.getValue();
    SbRotation camrot = cam->orientation.getValue();
    SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();

#if (COIN_MAJOR_VERSION >= 3)
    float aspectRatio = vp.getViewportAspectRatio();
#endif

    SbSphere sphere;
    sphere.circumscribe(box);
    if (sphere.getRadius() == 0)
        return;

    SbVec3f direction, pos(0.0f, 0.0f, 0.0f);
    camrot.multVec(SbVec3f(0, 0, -1), direction);

    bool isOrthographic = false;
    float height = 0;
    float diff = 0;

    if (cam->isOfType(SoOrthographicCamera::getClassTypeId())) {
        isOrthographic = true;
        height = static_cast<SoOrthographicCamera*>(cam)->height.getValue();
#if (COIN_MAJOR_VERSION >= 3)
        if (aspectRatio < 1.0f)
            diff = sphere.getRadius() * 2 - height * aspectRatio;
        else
#endif
        diff = sphere.getRadius() * 2 - height;
        pos = (box.getCenter() - direction * sphere.getRadius());
    }
    else if (cam->isOfType(SoPerspectiveCamera::getClassTypeId())) {
        float movelength = sphere.getRadius()/float(tan(static_cast<SoPerspectiveCamera*>
            (cam)->heightAngle.getValue() / 2.0));
        pos = box.getCenter() - direction * movelength;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    Base::StateLocker guard(_pimpl->animating);
    for (int i=0; i<steps; i++) {
        float s = float(i)/float(steps);

        if (isOrthographic) {
            float camHeight = height + diff * s;
            static_cast<SoOrthographicCamera*>(cam)->height.setValue(camHeight);
        }

        SbVec3f curpos = campos * (1.0f-s) + pos * s;
        cam->position.setValue(curpos);
        timer.start(Base::clamp<int>(ms,0,ViewParams::getMaxCameraAnimatePeriod()));
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    _pimpl->onRender();
}

#if BUILD_VR
extern View3DInventorRiftViewer* oculusStart(void);
extern bool oculusUp   (void);
extern void oculusStop (void);
void oculusSetTestScene(View3DInventorRiftViewer *window);
#endif

void View3DInventorViewer::viewVR()
{
#if BUILD_VR
    if (oculusUp()) {
        oculusStop();
    }
    else {
        View3DInventorRiftViewer* riftWin = oculusStart();
        riftWin->setSceneGraph(pcViewProviderRoot);
    }
#endif
}

void View3DInventorViewer::boxZoom(const SbBox2s& box)
{
    navigation->boxZoom(box);
}

SbBox3f View3DInventorViewer::getBoundingBox() const
{
    SbBox3f box;
    getSceneBoundBox(box);
    return box;
}


void View3DInventorViewer::viewAll()
{
    SbBox3f box;
    if(!getSceneBoundBox(box))
        return;

    _pimpl->updateShadowGround(box);

    // Set the height angle to 45 deg
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (cam && cam->getTypeId().isDerivedFrom(SoPerspectiveCamera::getClassTypeId()))
        static_cast<SoPerspectiveCamera*>(cam)->heightAngle = (float)(M_PI / 4.0);

    viewBoundBox(box);
}

void View3DInventorViewer::Private::updateShadowGround(const SbBox3f &box)
{
    App::Document *doc = owner->guiDocument?owner->guiDocument->getDocument():nullptr;

    if (!pcShadowGroup || !doc || !view)
        return;

    SbVec3f size = box.getSize();
    SbVec3f center = box.getCenter();

    if(pcShadowDirectionalLight) {
        static const App::PropertyFloatConstraint::Constraints _cstr(1.0,1000.0,0.1);
        double scale = _shadowParam<App::PropertyFloatConstraint>(view, "BoundBoxScale",
                ViewParams::docShadowBoundBoxScale(), ViewParams::getShadowBoundBoxScale(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_cstr);
            });
        pcShadowDirectionalLight->bboxSize = size * float(scale);
        pcShadowDirectionalLight->bboxCenter = center;
    }

    if(pcShadowGroundSwitch && pcShadowGroundSwitch->whichChild.getValue()>=0) {
        float z = size[2];
        float width, length;
        if(_shadowParam<App::PropertyBool>(view, "GroundSizeAuto",
                    "Auto adjust ground size based on the scene bounding box", true))
        {
            double scale = _shadowParam<App::PropertyFloat>(view, "GroundSizeScale",
                    ViewParams::docShadowGroundScale(), ViewParams::getShadowGroundScale());
            if(scale <= 0.0)
                scale = 1.0;
            width = length = scale * std::max(std::max(size[0],size[1]),size[2]);
        } else {
            width = _shadowParam<App::PropertyLength>(view, "GroundSizeX", "", 100.0);
            length = _shadowParam<App::PropertyLength>(view, "GroundSizeY", "", 100.0);
        }

        Base::Placement pla = _shadowParam<App::PropertyPlacement>(
                view, "GroundPlacement",
                "Ground placement. If 'GroundAutoPosition' is on, this specifies an additional offset of the ground",
                Base::Placement());

        if(!_shadowParam<App::PropertyBool>(view, "GroundAutoPosition",
                    "Auto place the ground face at the Z bottom of the scene", true))
        {
            center[0] = pla.getPosition().x;
            center[1] = pla.getPosition().y;
            z = pla.getPosition().z;
            pla = Base::Placement();
        } else {
            z = center[2]-z/2-1;
        }
        SbVec3f coords[4] = {
            {center[0]-width, center[1]-length, z},
            {center[0]+width, center[1]-length, z},
            {center[0]+width, center[1]+length, z},
            {center[0]-width, center[1]+length, z},
        };
        if(!pla.isIdentity()) {
            SbMatrix mat = ViewProvider::convert(pla.toMatrix());
            for(auto &coord : coords)
                mat.multVecMatrix(coord,coord);
        }
        pcShadowGroundCoords->point.setValues(0, 4, coords);

        static const App::PropertyQuantityConstraint::Constraints _texture_cstr = {0,DBL_MAX,10.0};
        float textureSize = _shadowParam<App::PropertyLength>(view, "GroundTextureSize",
            ViewParams::docShadowGroundTextureSize(), ViewParams::getShadowGroundTextureSize(),
            [](App::PropertyLength &prop) {
                if(prop.getConstraints() != &_texture_cstr)
                    prop.setConstraints(&_texture_cstr);
            });
        if(textureSize < 1e-5)
            pcShadowGroundTextureCoords->point.setNum(0);
        else {
            float w = width*2.0/textureSize;
            float l = length*2.0/textureSize;
            SbVec2f points[4] = {{0,l}, {w,l}, {w,0}, {0,0}};
            pcShadowGroundTextureCoords->point.setValues(0,4,points);
        }

        SbBox3f gbox = box;
        for(int i=0; i<4; ++i)
            gbox.extendBy(coords[i]);
        size = gbox.getSize();
    }

    static const App::PropertyIntegerConstraint::Constraints _smooth_cstr(0,100,1);
    double smoothBorder = _shadowParam<App::PropertyIntegerConstraint>(view, "SmoothBorder",
            ViewParams::docShadowSmoothBorder(), ViewParams::getShadowSmoothBorder(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_smooth_cstr)
                    prop.setConstraints(&_smooth_cstr);
            });

    static const App::PropertyIntegerConstraint::Constraints _spread_cstr(0,1000000,500);
    double spread = _shadowParam<App::PropertyIntegerConstraint>(view, "SpreadSize",
            ViewParams::docShadowSpreadSize(), ViewParams::getShadowSpreadSize(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_spread_cstr)
                    prop.setConstraints(&_spread_cstr);
            });

    static const App::PropertyIntegerConstraint::Constraints _sample_cstr(0,7,1);
    double sample = _shadowParam<App::PropertyIntegerConstraint>(view, "SpreadSampleSize",
            ViewParams::docShadowSpreadSampleSize(), ViewParams::getShadowSpreadSampleSize(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_sample_cstr)
                    prop.setConstraints(&_sample_cstr);
            });

    float maxSize = std::max(size[0],std::max(size[1],size[2]));
    if (maxSize > 256.0 && pcShadowGroup->findChild(pcShadowSpotLight)>=0)
        spread *= 256.0/maxSize;
    pcShadowGroup->smoothBorder = smoothBorder/10.0f + sample/100.0f + spread/1000000.0f;
}

void View3DInventorViewer::viewAll(float factor)
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam)
        return;

    if (factor <= 0.0f)
        return;

    if (factor != 1.0f) {
        SbBox3f box;
        if(!getSceneBoundBox(box))
            return;

        float dx,dy,dz;
        box.getSize(dx,dy,dz);

        float x,y,z;
        box.getCenter().getValue(x,y,z);

        box.setBounds(x-dx*factor,y-dy*factor,z-dz*factor,
                      x+dx*factor,y+dy*factor,z+dz*factor);

        viewBoundBox(box);
    }
    else {
        viewAll();
    }
}

// Recursively check if any sub-element intersects with a given projected 2D polygon
int
View3DInventorViewer::Private::checkElementIntersection(ViewProviderDocumentObject *vp,
                                                        const char *subname,
                                                        const Base::ViewProjMethod &proj,
                                                        const Base::Polygon2d &polygon,
                                                        App::DocumentObject *prevObj)
{
    auto obj = vp->getObject();
    if(!obj || !obj->getNameInDocument())
        return -1;

    App::DocumentObject *sobj = obj;
    if (subname && subname[0]) {
        App::DocumentObject *parent = nullptr;
        std::string childName;
        sobj = obj->resolve(subname,&parent,&childName);
        if(!sobj)
            return -1;
        if(!owner->isInGroupOnTop(App::SubObjectT(obj, subname), false)
                && !sobj->testStatus(App::ObjEditing)) {
            int vis;
            if(!parent || (vis=parent->isElementVisibleEx(
                            childName.c_str(),App::DocumentObject::GS_SELECT))<0)
                vis = sobj->Visibility.getValue()?1:0;
            if(!vis)
                return -1;
        }
    } else if (!owner->isInGroupOnTop(App::SubObjectT(obj, ""), false)
            && !obj->testStatus(App::ObjEditing)
            && !obj->Visibility.getValue())
    {
        return -1;
    }

    auto bbox3 = vp->getBoundingBox(subname);
    if(!bbox3.IsValid())
        return -1;

    auto bbox = bbox3.ProjectBox(&proj);
    if(!bbox.Intersect(polygon))
        return 0;

    std::vector<std::string> subs;
    if (sobj != prevObj)
        subs = sobj->getSubObjects(App::DocumentObject::GS_SELECT);
    if(subs.size()) {
        int res = -1;
        for(auto &sub : subs) {
            if (subname && subname[0])
                sub.insert(sub.begin(), subname, subname + strlen(subname));
            int r = checkElementIntersection(vp, sub.c_str(), proj, polygon, sobj);
            if (r == 0)
                res = 0;
            if (r > 0)
                return 1;
            // Return < 0 means either the object does not have shape, or the shape
            // type does not implement sub-element intersection check. So Just
            // ignore it and continue
        }
        return res;
    }

    Base::PyGILStateLocker lock;
    PyObject *pyobj = nullptr;
    Base::Matrix4D mat;
    obj->getSubObject(subname,&pyobj,&mat);
    if(!pyobj)
        return -1;
    Py::Object pyobject(pyobj,true);
    if(!PyObject_TypeCheck(pyobj,&Data::ComplexGeoDataPy::Type))
        return -1;
    auto data = static_cast<Data::ComplexGeoDataPy*>(pyobj)->getComplexGeoDataPtr();
    int res = -1;
    size_t count = data->countSubElements("Face");
    // Try face first. And only try edge if there is no face.
    if(count) {
        std::string element("Face");
        Base::Polygon2d loop;
        std::vector<Base::Vector3d> points;
        std::vector<Base::Vector3d> pointNormals; // not used
        std::vector<Data::ComplexGeoData::Facet> faces;
        for(size_t i=1;i<=count;++i) {
            element.resize(4);
            element += std::to_string(i);
            std::unique_ptr<Data::Segment> segment(data->getSubElementByName(element.c_str()));
            if(!segment)
                continue;
            points.clear();
            pointNormals.clear();
            faces.clear();
            // Call getFacesFromSubElement to obtain the triangulation of this face.
            try {
                data->getFacesFromSubElement(segment.get(),points,pointNormals,faces);
            } catch (const Base::Exception &) {
            }

            if(faces.empty())
                continue;
            res = 0;
            for(auto &facet : faces) {
                loop.DeleteAll();
                auto v = proj(points[facet.I1]);
                loop.Add(Base::Vector2d(v.x, v.y));
                v = proj(points[facet.I2]);
                loop.Add(Base::Vector2d(v.x, v.y));
                v = proj(points[facet.I3]);
                loop.Add(Base::Vector2d(v.x, v.y));
                if(polygon.Intersect(loop))
                    return 1;
            }
        }
    }
    // res < 0 means no face found
    if (res < 0 && (count = data->countSubElements("Edge"))) {
        std::string element("Edge");
        Base::Polygon2d loop;
        std::vector<Base::Vector3d> points;
        std::vector<Data::ComplexGeoData::Line> lines;
        for(size_t i=1;i<=count;++i) {
            element.resize(4);
            element += std::to_string(i);
            std::unique_ptr<Data::Segment> segment(data->getSubElementByName(element.c_str()));
            if(!segment)
                continue;
            points.clear();
            lines.clear();
            try {
                data->getLinesFromSubElement(segment.get(),points,lines);
            } catch (const Base::Exception &) {
            }
            if(lines.empty())
                continue;
            res = 0;
            for(auto &line : lines) {
                loop.DeleteAll();
                auto v = proj(points[line.I1]);
                loop.Add(Base::Vector2d(v.x, v.y));
                v = proj(points[line.I2]);
                loop.Add(Base::Vector2d(v.x, v.y));
                if(polygon.Intersect(loop))
                    return 1;
            }
        }
    }
    if (res < 0) {
        std::vector<Base::Vector3d> pointNormals; // not used
        std::vector<Base::Vector3d> points;
        try {
            data->getPoints(points,pointNormals,-1.0);
        } catch (const Base::Exception &) {
        }
        if (points.size()) {
            res = 0;
            for (auto &pt : points) {
                auto v = proj(pt);
                if(polygon.Contains(Base::Vector2d(v.x, v.y)))
                    return 1;
            }
        }
    }
    return res;
}

void View3DInventorViewer::viewSelection(bool extend)
{
    // Disable extended view selection if there is an editing view provider, so
    // that we don't mess up the current editing view.
    if (extend && editViewProvider)
        return;

    if (!guiDocument)
        return;

    auto sels = Gui::Selection().getSelectionT(guiDocument->getDocument()->getName(),ResolveMode::NoResolve);
    if (sels.empty()) {
        sels.push_back(Gui::Selection().getContext());
        if (sels.back().getDocument() != guiDocument->getDocument())
            return;
    } else if (ViewParams::getMaxViewSelections() < (int)sels.size())
        sels.resize(ViewParams::getMaxViewSelections());
    viewObjects(sels, extend);
}

void View3DInventorViewer::viewObjects(const std::vector<App::SubObjectT> &objs, bool extend)
{
    if(!guiDocument)
        return;

    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if(!cam)
        return;

    // When calling with extend = true, we are supposed to make sure the
    // current view volume at least include some geometry sub-element of all
    // given objects. The volume does not have to include the whole object. The
    // implementation below uses the screen dimension as a rectangle selection
    // and recursively test intersection. The algorithm used is similar to
    // Command Std_BoxElementSelection.
    SbViewVolume vv = cam->getViewVolume();
    ViewVolumeProjection proj(vv);
    Base::Polygon2d polygon;
    SbViewportRegion viewport = getSoRenderManager()->getViewportRegion();
    const SbVec2s& sp = viewport.getViewportSizePixels();
    auto pos = getGLPolygon({{0,0}, sp});
    polygon.Add(Base::Vector2d(pos[0][0], pos[1][1]));
    polygon.Add(Base::Vector2d(pos[0][0], pos[0][1]));
    polygon.Add(Base::Vector2d(pos[1][0], pos[0][1]));
    polygon.Add(Base::Vector2d(pos[1][0], pos[1][1]));

    Base::BoundBox3d bbox;
    for(auto &objT : objs) {
        auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                guiDocument->getViewProvider(objT.getObject()));
        if(!vp)
            continue;

        if(!extend
            || !_pimpl->checkElementIntersection(vp, objT.getSubName().c_str(), proj, polygon))
        {
            bbox.Add(vp->getBoundingBox(objT.getSubName().c_str()));
        }
    }

    if (bbox.IsValid()) {
        SbBox3f box(bbox.MinX,bbox.MinY,bbox.MinZ,bbox.MaxX,bbox.MaxY,bbox.MaxZ);
        if(extend) { // whether to extend the current view volume to include the objects

            // Replace the following bounding box intersection test with finer
            // sub-element intersection test.
#if 0
            SbVec3f center = box.getCenter();
            SbVec3f size = box.getSize()
                * 0.5f * ViewParams::getViewSelectionExtendFactor();

            // scale the box by the configured factor, so that we don't have to
            // change the camera if the selection is partially in view.
            SbBox3f sbox(center-size, center+size);

            int cullbits = 7;
            // test if the scaled box is completely outside of view
            if(!sbox.outside(vv.getMatrix(),cullbits)) {
                return;
            }
#endif

            float vx,vy,vz;
            SbVec3f vcenter = vv.getProjectionPoint()
                + vv.getProjectionDirection()*(vv.getDepth()*0.5+vv.getNearDist());
            vcenter.getValue(vx,vy,vz);

            float radius = std::max(vv.getWidth(),vv.getHeight())*0.5f;

            // A rough estimation of the view bounding box. Note that
            // SoCamera::viewBoundingBox() is not accurate as well. It uses a
            // sphere to surround the bounding box for easy calculation.
            SbBox3f vbox(vx-radius,vy-radius,vz-radius,vx+radius,vy+radius,vz+radius);

            // extend the view box to include the selection
            vbox.extendBy(box);

            // obtain the entire scene bounding box
            SbBox3f scenebox;
            getSceneBoundBox(scenebox);

            // extend to include the selection, just to be sure
            scenebox.extendBy(box);

            float minx, miny, minz, maxx, maxy, maxz;
            vbox.getBounds(minx, miny, minz, maxx, maxy, maxz);

            // clip the extended current view box to the scene box
            float minx2, miny2, minz2, maxx2, maxy2, maxz2;
            scenebox.getBounds(minx2, miny2, minz2, maxx2, maxy2, maxz2);
            if(minx < minx2) minx = minx2;
            if(miny < miny2) miny = miny2;
            if(minz < minz2) minz = minz2;
            if(maxx > maxx2) maxx = maxx2;
            if(maxy > maxy2) maxy = maxy2;
            if(maxz > maxz2) maxz = maxz2;
            box.setBounds(minx, miny, minz, maxx, maxy, maxz);
        }
        viewBoundBox(box);
    }
}

void View3DInventorViewer::viewSelectionNormal(bool backFacing)
{
    SoCamera* cam = getSoRenderManager()->getCamera();
    if(!cam)
        return;

    std::vector<App::SubObjectT> sels;
    std::vector<Base::Vector3d> pickedPoints;
    auto preselT = Selection().getPreselection().Object;
    if (!preselT.getObjectName().empty()) {
        sels.push_back(preselT);
        const auto &presel = Selection().getPreselection();
        pickedPoints.emplace_back(presel.x, presel.y, presel.z);
    }
    else {
        for (auto sel : Selection().getSelection("", ResolveMode::NoResolve)) {
            sels.emplace_back(sel.pObject,sel.SubName);
            pickedPoints.emplace_back(sel.x, sel.y, sel.z);
        }
    }

    Base::Vector3d normal;
    Base::Vector3d xnormal;
    Base::Vector3d base;
    int count = 0;
    int i=-1;
    for (const auto &sel : sels) {
        ++i;
        if (auto obj = sel.getObject()) {
            Base::PyGILStateLocker lock;
            PyObject *pyobj = nullptr;
            Base::Matrix4D mat;
            obj->getSubObject(sel.getSubName().c_str(), &pyobj, &mat);
            if (!pyobj)
                continue;
            Py::Object pyObj = Py::asObject(pyobj);
            if (!PyObject_TypeCheck(pyobj, &Data::ComplexGeoDataPy::Type))
                continue;
            Base::Rotation rot;
            if (!static_cast<Data::ComplexGeoDataPy*>(pyobj)->getComplexGeoDataPtr()->getRotation(rot))
                rot = mat;
            normal += rot.multVec(Base::Vector3d(0,0,1));
            xnormal += rot.multVec(Base::Vector3d(1,0,0));
            if (pickedPoints[i] == Base::Vector3d())
                base += Base::Placement(mat).getPosition();
            else
                base += pickedPoints[i];
            if (++count == ViewParams::getMaxViewSelections())
                break;
        }
    }
    if (count == 0)
        return;
    normal /= count;
    normal.Normalize();
    xnormal /= count;
    xnormal.Normalize();
    base /= count;

#if 1
    Base::Vector3d xdir(xnormal);
#else
    Base::Vector3d xdir(1,0,0);
    Base::Vector3d origin(0,0,0);
    xdir.ProjectToPlane(base, normal);
    origin.ProjectToPlane(base, normal);
    xdir -= origin;
    if (xdir.Sqr() < 1e-14)
        xdir = Base::Vector3d(1,0,0);
    else
        xdir.Normalize();
#endif

    Base::Vector3d zdir(0,0,1);
    bool faceBack = zdir.Dot(normal) < 0.f;
    if (backFacing != faceBack) {
        normal = -normal;
        xdir = -xdir;
    }
    Base::Rotation rot(zdir, normal);
    rot *= Base::Rotation(Base::Vector3d(1,0,0), rot.multVec(xdir));

    // Make sure the camera relative position of the base/picked point is unchanged

    const SbVec3f &camBase = cam->position.getValue();
    const SbRotation &camRot = cam->orientation.getValue();
    SbRotation newCamRot(rot[0], rot[1], rot[2], rot[3]);

    SbVec3f oldBase(base.x, base.y, base.z);
    SbVec3f newBase;
    (camRot.inverse()*newCamRot).multVec(oldBase-camBase, newBase);
    newBase += camBase;

    SbVec3f center = camBase - newBase + oldBase;
    navigation->setCameraOrientation(newCamRot, &center);
    navigation->setRotationCenter(oldBase);
}

void View3DInventorViewer::setRotationCenterSelection()
{
    if (!guiDocument)
        return;
    auto sels = Gui::Selection().getSelectionT(guiDocument->getDocument()->getName(),ResolveMode::NoResolve);
    if (sels.empty()) {
        const auto &presel = Gui::Selection().getPreselection().Object;
        sels.emplace_back(presel.getDocumentName().c_str(),
                          presel.getObjectName().c_str(),
                          presel.getSubName().c_str());
    }

    Base::BoundBox3d bound;
    for(auto &objT : sels) {
        auto obj = objT.getObject();
        auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                guiDocument->getViewProvider(obj));
        if(!vp)
            continue;
        auto bbox = vp->getBoundingBox(objT.getSubName().c_str());
        if (!bbox.IsValid())
            continue;

        Base::PyGILStateLocker lock;
        PyObject *pyobj = nullptr;
        Base::Matrix4D mat;
        obj->getSubObject(objT.getSubName().c_str(),&pyobj,&mat,true);
        if(pyobj) {
            Py::Object pyobject(pyobj,true);
            if (PyObject_TypeCheck(pyobj,&Data::ComplexGeoDataPy::Type)) {
                auto data = static_cast<Data::ComplexGeoDataPy*>(pyobj)->getComplexGeoDataPtr();
                Base::Vector3d center;
                if (data->getCenterOfGravity(center)) {
                    bound.Add(center);
                    continue;
                }
            }
        }
        bound.Add(bbox.GetCenter());
    }

    if (bound.IsValid()) {
        auto center = bound.GetCenter();
        navigation->setRotationCenter(SbVec3f(center.x, center.y, center.z));
    }
}

void View3DInventorViewer::viewBoundBox(const SbBox3f &box) {
    if (isAnimationEnabled())
        animatedViewAll(box, 10, 20);

    SoCamera* cam = getSoRenderManager()->getCamera();
    if(!cam)
        return;

#if (COIN_MAJOR_VERSION >= 4)
    float aspectratio = getSoRenderManager()->getViewportRegion().getViewportAspectRatio();
    switch (cam->viewportMapping.getValue()) {
        case SoCamera::CROP_VIEWPORT_FILL_FRAME:
        case SoCamera::CROP_VIEWPORT_LINE_FRAME:
        case SoCamera::CROP_VIEWPORT_NO_FRAME:
            aspectratio = 1.0f;
            break;
        default:
            break;
    }
    cam->viewBoundingBox(box,aspectratio,1.0);
#else
    SoPath & path = _pimpl->tmpPath;
    path.truncate(0);
    auto pcGroup = new SoGroup;
    pcGroup->ref();
    auto pcTransform = new SoTransform;
    pcGroup->addChild(pcTransform);
    pcTransform->translation = box.getCenter();
    auto *pcCube = new SoCube;
    pcGroup->addChild(pcCube);
    float sizeX,sizeY,sizeZ;
    box.getSize(sizeX,sizeY,sizeZ);
    pcCube->width = sizeX;
    pcCube->height = sizeY;
    pcCube->depth = sizeZ;
    path.append(pcGroup);
    path.append(pcCube);
    cam->viewAll(&path,getSoRenderManager()->getViewportRegion());
    path.truncate(0);
    pcGroup->unref();
#endif
}

/*!
  Decide if it should be possible to start a spin animation of the
  model in the viewer by releasing the mouse button while dragging.

  If the \a enable flag is \c false and we're currently animating, the
  spin will be stopped.
*/
void
View3DInventorViewer::setAnimationEnabled(const SbBool enable)
{
    navigation->setAnimationEnabled(enable);
}

/*!
  Query whether or not it is possible to start a spinning animation by
  releasing the left mouse button while dragging the mouse.
*/

SbBool
View3DInventorViewer::isAnimationEnabled() const
{
    return navigation->isAnimationEnabled();
}

/*!
  Query if the model in the viewer is currently in spinning mode after
  a user drag.
*/
SbBool View3DInventorViewer::isAnimating() const
{
    return navigation->isAnimating();
}

/*!
 * Starts programmatically the viewer in animation mode. The given axis direction
 * is always in screen coordinates, not in world coordinates.
 */
void View3DInventorViewer::startAnimating(const SbVec3f& axis, float velocity)
{
    navigation->startAnimating(axis, velocity);
}

void View3DInventorViewer::stopAnimating()
{
    navigation->stopAnimating();
}

void View3DInventorViewer::setPopupMenuEnabled(const SbBool on)
{
    navigation->setPopupMenuEnabled(on);
}

SbBool View3DInventorViewer::isPopupMenuEnabled() const
{
    return navigation->isPopupMenuEnabled();
}

/*!
  Set the flag deciding whether or not to show the axis cross.
*/

void
View3DInventorViewer::setFeedbackVisibility(const SbBool enable)
{
    if (enable == this->axiscrossEnabled) {
        return;
    }

    this->axiscrossEnabled = enable;

    if (this->isViewing()) {
        this->getSoRenderManager()->scheduleRedraw();
    }
}

/*!
  Check if the feedback axis cross is visible.
*/

SbBool
View3DInventorViewer::isFeedbackVisible() const
{
    return this->axiscrossEnabled;
}

/*!
  Set the size of the feedback axiscross.  The value is interpreted as
  an approximate percentage chunk of the dimensions of the total
  canvas.
*/
void
View3DInventorViewer::setFeedbackSize(const int size)
{
    if (size < 1) {
        return;
    }

    this->axiscrossSize = size;

    if (this->isFeedbackVisible() && this->isViewing()) {
        this->getSoRenderManager()->scheduleRedraw();
    }
}

/*!
  Return the size of the feedback axis cross. Default is 10.
*/

int
View3DInventorViewer::getFeedbackSize() const
{
    return this->axiscrossSize;
}

/*!
  Decide whether or not the mouse pointer cursor should be visible in
  the rendering canvas.
*/
void View3DInventorViewer::setCursorEnabled(SbBool /*enable*/)
{
    this->setCursorRepresentation(navigation->getViewingMode());
}

void View3DInventorViewer::afterRealizeHook()
{
    inherited::afterRealizeHook();
    this->setCursorRepresentation(navigation->getViewingMode());
}

// Documented in superclass. This method overridden from parent class
// to make sure the mouse pointer cursor is updated.
void View3DInventorViewer::setViewing(SbBool enable)
{
    if (this->isViewing() == enable) {
        return;
    }

    navigation->setViewingMode(enable ?
        NavigationStyle::IDLE : NavigationStyle::INTERACT);
    inherited::setViewing(enable);
}

void View3DInventorViewer::drawAxisCross()
{
    // FIXME: convert this to a superimposition scenegraph instead of
    // OpenGL calls. 20020603 mortene.

    // Store GL state.
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLfloat depthrange[2];
    glGetFloatv(GL_DEPTH_RANGE, depthrange);
    GLdouble projectionmatrix[16];
    glGetDoublev(GL_PROJECTION_MATRIX, projectionmatrix);

    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glDepthRange(0, 0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glDisable(GL_BLEND); // Kills transparency.

    // Set the viewport in the OpenGL canvas. Dimensions are calculated
    // as a percentage of the total canvas size.
    SbVec2s view = this->getSoRenderManager()->getSize();
    const int pixelarea = int(float(this->axiscrossSize)/100.0f * std::min(view[0], view[1]));
    SbVec2s origin(view[0] - pixelarea, 0);
    glViewport(origin[0], origin[1], pixelarea, pixelarea);

    // Set up the projection matrix.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float NEARVAL = 0.1f;
    const float FARVAL = 10.0f;
    const float dim = NEARVAL * float(tan(M_PI / 8.0)); // FOV is 45 deg (45/360 = 1/8)
    glFrustum(-dim, dim, -dim, dim, NEARVAL, FARVAL);


    // Set up the model matrix.
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    SbMatrix mx;
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    // If there is no camera (like for an empty scene, for instance),
    // just use an identity rotation.
    if (cam) {
        mx = cam->orientation.getValue();
    }
    else {
        mx = SbMatrix::identity();
    }

    mx = mx.inverse();
    mx[3][2] = -3.5; // Translate away from the projection point (along z axis).
    glLoadMatrixf((float*)mx);


    // Find unit vector end points.
    SbMatrix px;
    glGetFloatv(GL_PROJECTION_MATRIX, (float*)px);
    SbMatrix comb = mx.multRight(px); // clazy:exclude=rule-of-two-soft

    SbVec3f xpos;
    comb.multVecMatrix(SbVec3f(1,0,0), xpos);
    xpos[0] = (1 + xpos[0]) * view[0]/2;
    xpos[1] = (1 + xpos[1]) * view[1]/2;
    SbVec3f ypos;
    comb.multVecMatrix(SbVec3f(0,1,0), ypos);
    ypos[0] = (1 + ypos[0]) * view[0]/2;
    ypos[1] = (1 + ypos[1]) * view[1]/2;
    SbVec3f zpos;
    comb.multVecMatrix(SbVec3f(0,0,1), zpos);
    zpos[0] = (1 + zpos[0]) * view[0]/2;
    zpos[1] = (1 + zpos[1]) * view[1]/2;


    // Render the cross.
    {
        glLineWidth(2.0);

        enum { XAXIS, YAXIS, ZAXIS };
        int idx[3] = { XAXIS, YAXIS, ZAXIS };
        float val[3] = { xpos[2], ypos[2], zpos[2] };

        // Bubble sort.. :-}
        if (val[0] < val[1]) {
            std::swap(val[0], val[1]);
            std::swap(idx[0], idx[1]);
        }

        if (val[1] < val[2]) {
            std::swap(val[1], val[2]);
            std::swap(idx[1], idx[2]);
        }

        if (val[0] < val[1]) {
            std::swap(val[0], val[1]);
            std::swap(idx[0], idx[1]);
        }

        assert((val[0] >= val[1]) && (val[1] >= val[2])); // Just checking..

        for (const int & i : idx) {
            glPushMatrix();

            if (i == XAXIS) {                        // X axis.
                if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
                    glColor3f(0.500f, 0.5f, 0.5f);
                else
                    glColor3f(0.500f, 0.125f, 0.125f);
            }
            else if (i == YAXIS) {                   // Y axis.
                glRotatef(90, 0, 0, 1);

                if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
                    glColor3f(0.400f, 0.4f, 0.4f);
                else
                    glColor3f(0.125f, 0.500f, 0.125f);
            }
            else {                                        // Z axis.
                glRotatef(-90, 0, 1, 0);

                if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
                    glColor3f(0.300f, 0.3f, 0.3f);
                else
                    glColor3f(0.125f, 0.125f, 0.500f);
            }

            this->drawArrow();
            glPopMatrix();
        }
    }

    // Render axis notation letters ("X", "Y", "Z").
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, view[0], 0, view[1], -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLint unpack;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
        glColor3fv(SbVec3f(1.0f, 1.0f, 1.0f).getValue());
    else
        glColor3fv(SbVec3f(0.0f, 0.0f, 0.0f).getValue());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelZoom((float)axiscrossSize/30, (float)axiscrossSize/30); // 30 = 3 (character pixmap ratio) * 10 (default axiscrossSize)
    glRasterPos2d(xpos[0], xpos[1]);
    glDrawPixels(XPM_WIDTH, XPM_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, XPM_PIXEL_DATA);
    glRasterPos2d(ypos[0], ypos[1]);
    glDrawPixels(YPM_WIDTH, YPM_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, YPM_PIXEL_DATA);
    glRasterPos2d(zpos[0], zpos[1]);
    glDrawPixels(ZPM_WIDTH, ZPM_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, ZPM_PIXEL_DATA);

    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    glPopMatrix();

    // Reset original state.

    // FIXME: are these 3 lines really necessary, as we push
    // GL_ALL_ATTRIB_BITS at the start? 20000604 mortene.
    glDepthRange(depthrange[0], depthrange[1]);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixd(projectionmatrix);

    glPopAttrib();
}

// Draw an arrow for the axis representation directly through OpenGL.
void View3DInventorViewer::drawArrow()
{
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    glVertex3f(0.0f, -0.02f, 0.02f);
    glVertex3f(0.0f, 0.02f, 0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.02f, 0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.02f, 0.02f);

    glVertex3f(0.0f, -0.02f, -0.02f);
    glVertex3f(0.0f, 0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.02f, -0.02f);

    glVertex3f(0.0f, -0.02f, 0.02f);
    glVertex3f(0.0f, -0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.02f, 0.02f);

    glVertex3f(0.0f, 0.02f, 0.02f);
    glVertex3f(0.0f, 0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.02f, -0.02f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.02f, 0.02f);

    glVertex3f(0.0f, 0.02f, 0.02f);
    glVertex3f(0.0f, 0.02f, -0.02f);
    glVertex3f(0.0f, -0.02f, -0.02f);
    glVertex3f(0.0f, -0.02f, 0.02f);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex3f(1.0f, 0.0f, 0.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, +0.5f / 4.0f, 0.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.5f / 4.0f, 0.0f);
    glVertex3f(1.0f, 0.0f, 0.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.0f, +0.5f / 4.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.0f, -0.5f / 4.0f);
    glEnd();
    glBegin(GL_QUADS);
    glVertex3f(1.0f - 1.0f / 3.0f, +0.5f / 4.0f, 0.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.0f, +0.5f / 4.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, -0.5f / 4.0f, 0.0f);
    glVertex3f(1.0f - 1.0f / 3.0f, 0.0f, -0.5f / 4.0f);
    glEnd();
}

void View3DInventorViewer::drawSingleBackground(const QColor& col)
{
    // Note: After changing the NaviCube code the content of an image plane may appear black.
    // A workaround is this function.
    // See also: https://github.com/FreeCAD/FreeCAD/pull/9356#issuecomment-1529521654
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glPushAttrib(GL_ENABLE_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_TRIANGLE_STRIP);
    glColor3f(col.redF(), col.greenF(), col.blueF());
    glVertex2f(-1, 1);
    glColor3f(col.redF(), col.greenF(), col.blueF());
    glVertex2f(-1, -1);
    glColor3f(col.redF(), col.greenF(), col.blueF());
    glVertex2f(1, 1);
    glColor3f(col.redF(), col.greenF(), col.blueF());
    glVertex2f(1, -1);
    glEnd();
    glPopAttrib();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

// ************************************************************************

// Set cursor graphics according to mode.
void View3DInventorViewer::setCursorRepresentation(int modearg)
{
    // There is a synchronization problem between Qt and SoQt which
    // happens when popping up a context-menu. In this case the
    // Qt::WA_UnderMouse attribute is reset and never set again
    // even if the mouse is still in the canvas. Thus, the cursor
    // won't be changed as long as the user doesn't leave and enter
    // the canvas. To fix this we explicitly set Qt::WA_UnderMouse
    // if the mouse is inside the canvas.
    QWidget* glWindow = this->getGLWidget();

    // When a widget is added to the QGraphicsScene and the user
    // hovered over it the 'WA_SetCursor' attribute is set to the
    // GL widget but never reset and thus would cause that the
    // cursor on this widget won't be set.
    if (glWindow)
        glWindow->setAttribute(Qt::WA_SetCursor, false);

    if (glWindow && glWindow->rect().contains(QCursor::pos()))
        glWindow->setAttribute(Qt::WA_UnderMouse);

    QWidget *widget = QWidget::mouseGrabber();
    if (!widget)
        widget = this->getWidget();

    switch (modearg) {
    case NavigationStyle::IDLE:
    case NavigationStyle::INTERACT:
        if (isEditing())
            widget->setCursor(this->editCursor);
        else
            widget->setCursor(QCursor(Qt::ArrowCursor));
        break;

    case NavigationStyle::DRAGGING:
    case NavigationStyle::SPINNING:
        widget->setCursor(spinCursor);
        break;

    case NavigationStyle::ZOOMING:
        widget->setCursor(zoomCursor);
        break;

    case NavigationStyle::SEEK_MODE:
    case NavigationStyle::SEEK_WAIT_MODE:
    case NavigationStyle::BOXZOOM:
        widget->setCursor(Qt::CrossCursor);
        break;

    case NavigationStyle::PANNING:
        widget->setCursor(panCursor);
        break;

    case NavigationStyle::SELECTION:
        widget->setCursor(Qt::PointingHandCursor);
        break;

    default:
        assert(0);
        break;
    }
}

void View3DInventorViewer::setEditing(SbBool edit)
{
    this->editing = edit;
    this->getWidget()->setCursor(QCursor(Qt::ArrowCursor));
    this->editCursor = QCursor();
}

void View3DInventorViewer::setComponentCursor(const QCursor& cursor)
{
    this->getWidget()->setCursor(cursor);
}

void View3DInventorViewer::setEditingCursor(const QCursor& cursor)
{
    this->getWidget()->setCursor(cursor);
    this->editCursor = this->getWidget()->cursor();
}

void View3DInventorViewer::selectCB(void* viewer, SoPath* path)
{
    ViewProvider* vp = static_cast<View3DInventorViewer*>(viewer)->getViewProviderByPath(path);
    if (vp && vp->useNewSelectionModel()) {
        // do nothing here
    }
}

void View3DInventorViewer::deselectCB(void* viewer, SoPath* path)
{
    ViewProvider* vp = static_cast<View3DInventorViewer*>(viewer)->getViewProviderByPath(path);
    if (vp && vp->useNewSelectionModel()) {
        // do nothing here
    }
}

SoPath* View3DInventorViewer::pickFilterCB(void* viewer, const SoPickedPoint* pp)
{
    ViewProvider* vp = static_cast<View3DInventorViewer*>(viewer)->getViewProviderByPath(pp->getPath());
    if (vp && vp->useNewSelectionModel()) {
        std::string e = vp->getElement(pp->getDetail());
        vp->getSelectionShape(e.c_str());
        static char buf[513];
        snprintf(buf,
                 sizeof(buf),
                 "Hovered: %s (%f,%f,%f)"
                 ,e.c_str()
                 ,pp->getPoint()[0]
                 ,pp->getPoint()[1]
                 ,pp->getPoint()[2]);

        getMainWindow()->showMessage(QString::fromUtf8(buf),3000);
    }

    return pp->getPath();
}

void View3DInventorViewer::addEventCallback(SoType eventtype, SoEventCallbackCB* cb, void* userdata)
{
    pEventCallback->addEventCallback(eventtype, cb, userdata);
}

void View3DInventorViewer::removeEventCallback(SoType eventtype, SoEventCallbackCB* cb, void* userdata)
{
    pEventCallback->removeEventCallback(eventtype, cb, userdata);
}

ViewProvider* View3DInventorViewer::getViewProviderByPath(SoPath* path) const
{
    if (!guiDocument) {
        Base::Console().Warning("View3DInventorViewer::getViewProviderByPath: No document set\n");
        return nullptr;
    }
    return guiDocument->getViewProviderByPathFromHead(path);
}

ViewProvider* View3DInventorViewer::getViewProviderByPathFromTail(SoPath* path) const
{
    if (!guiDocument) {
        Base::Console().Warning("View3DInventorViewer::getViewProviderByPathFromTail: No document set\n");
        return nullptr;
    }
    return guiDocument->getViewProviderByPathFromTail(path);
}

std::vector<ViewProvider*> View3DInventorViewer::getViewProvidersOfType(const Base::Type& typeId) const
{
    if (!guiDocument) {
        Base::Console().Warning("View3DInventorViewer::getViewProvidersOfType: No document set\n");
        return {};
    }
    return guiDocument->getViewProvidersOfType(typeId);
}

void View3DInventorViewer::turnAllDimensionsOn()
{
    dimensionRoot->whichChild = SO_SWITCH_ALL;
}

void View3DInventorViewer::turnAllDimensionsOff()
{
    dimensionRoot->whichChild = SO_SWITCH_NONE;
}

void View3DInventorViewer::eraseAllDimensions()
{
    coinRemoveAllChildren(static_cast<SoSwitch*>(dimensionRoot->getChild(0)));
    coinRemoveAllChildren(static_cast<SoSwitch*>(dimensionRoot->getChild(1)));
}

void View3DInventorViewer::turn3dDimensionsOn()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->whichChild = SO_SWITCH_ALL;
}

void View3DInventorViewer::turn3dDimensionsOff()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->whichChild = SO_SWITCH_NONE;
}

void View3DInventorViewer::addDimension3d(SoNode* node)
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->addChild(node);
}

void View3DInventorViewer::addDimensionDelta(SoNode* node)
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->addChild(node);
}

void View3DInventorViewer::turnDeltaDimensionsOn()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->whichChild = SO_SWITCH_ALL;
}

void View3DInventorViewer::turnDeltaDimensionsOff()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->whichChild = SO_SWITCH_NONE;
}

PyObject *View3DInventorViewer::getPyObject()
{
    if (!_viewerPy)
        _viewerPy = new View3DInventorViewerPy(this);

    Py_INCREF(_viewerPy);
    return _viewerPy;
}

/**
 * Drops the event \a e and loads the files into the given document.
 */
void View3DInventorViewer::dropEvent (QDropEvent * e)
{
    const QMimeData* data = e->mimeData();
    if (data->hasUrls() && guiDocument) {
        getMainWindow()->loadUrls(guiDocument->getDocument(), data->urls());
    }
    else {
        inherited::dropEvent(e);
    }
}

void View3DInventorViewer::dragEnterEvent (QDragEnterEvent * e)
{
    // Here we must allow uri drags and check them in dropEvent
    const QMimeData* data = e->mimeData();
    if (data->hasUrls()) {
        e->accept();
    }
    else {
        inherited::dragEnterEvent(e);
    }
}

void View3DInventorViewer::dragMoveEvent(QDragMoveEvent *e)
{
    const QMimeData* data = e->mimeData();
    if (data->hasUrls() && guiDocument) {
        e->accept();
    }
    else {
        inherited::dragMoveEvent(e);
    }
}

void View3DInventorViewer::dragLeaveEvent(QDragLeaveEvent *e)
{
    inherited::dragLeaveEvent(e);
}

void View3DInventorViewer::callEventFilter(QEvent *e)
{
    getEventFilter()->eventFilter(this, e);
}

void View3DInventorViewer::Private::onRender()
{
    if (!pcShadowGroup)
        return;
    SoCamera* cam = owner->getSoRenderManager()->getCamera();
    if(cam) {
        if(animating || shadowNodeId != pcShadowGroup->getNodeId() || cameraNodeId != cam->getNodeId())
            timer.start(100);
        else if (shadowExtraRedraw) {
            shadowExtraRedraw = false;
            owner->getSoRenderManager()->scheduleRedraw();
        }
    }
}

void View3DInventorViewer::redrawShadow()
{
    _pimpl->redraw();
}

void View3DInventorViewer::Private::redraw()
{
    if (animating) {
        timer.start(100);
        return;
    }
    timer.stop();
    SoCamera* cam = owner->getSoRenderManager()->getCamera();
    if(pcShadowGroup && pcShadowGroundSwitch && cam) {
        // Work around coin shadow rendering bug. On Windows, (and occasionally
        // on Linux), when shadow group is touched, it renders nothing when the
        // shadow cache is freshly built. We work around this issue using an
        // extra redraw, and the node renders fine with the already built
        // cache.
        //
        // Amendment: directional shadow light requires update on camera change
        // (not sure why or if it's absolutely needed yet). A patch has been
        // added to Coin3D to perform only quick partial update if there is no
        // scene changes.  We shall schedule an extra redraw to perform a full
        // update by touching the shadow group.
        pcShadowGroup->touch();
        SbBox3f bbox;
        if(owner->getSceneBoundBox(bbox))
            updateShadowGround(bbox);
        shadowNodeId = pcShadowGroup->getNodeId();
        cameraNodeId = cam->getNodeId();
        owner->getSoRenderManager()->scheduleRedraw();
        shadowExtraRedraw = ViewParams::getShadowExtraRedraw();
    }
}

void View3DInventorViewer::toggleShadowLightManip(int toggle)
{
    _pimpl->toggleDragger(toggle);
}

void View3DInventorViewer::Private::onDragFinish(void *data, SoDragger *d)
{
    auto self = reinterpret_cast<Private*>(data);

    App::AutoTransaction guard(QT_TRANSLATE_NOOP("View", "Change shadow light"));
    SbVec3f dir;
    if (self->pcShadowSpotLight && d == self->pcShadowSpotLight->getDragger()) {
        SbVec3f pos = self->pcShadowSpotLight->location.getValue();
        _shadowSetParam<App::PropertyVector>(self->view, "SpotLightPosition",
                Base::Vector3d(pos[0], pos[1], pos[2]));
        _shadowSetParam<App::PropertyAngle>(self->view, "SpotLightCutOffAngle",
                self->pcShadowSpotLight->cutOffAngle.getValue() * 180.0 / M_PI);
        dir = self->pcShadowSpotLight->direction.getValue();
    }
    else if (self->pcShadowDirectionalLight && d == self->pcShadowDirectionalLight->getDragger()) {
        dir = self->pcShadowDirectionalLight->direction.getValue();
    }
    else {
        return;
    }
    _shadowSetParam<App::PropertyVector>(self->view, "LightDirection",
            Base::Vector3d(dir[0], dir[1], dir[2]));
}

bool View3DInventorViewer::Private::toggleDragger(int toggle)
{
    App::Document *doc = owner->guiDocument?owner->guiDocument->getDocument():nullptr;
    if (!pcShadowGroup || !doc || !view)
        return false;

    bool dirlight = pcShadowGroup->findChild(pcShadowDirectionalLight) >= 0;
    SoSFBool &showDragger = dirlight?pcShadowDirectionalLight->showDragger:pcShadowSpotLight->showDragger;

    if (showDragger.getValue() && toggle <= 0) {
        showDragger = FALSE;
        pcShadowPickStyle->style = SoPickStyle::SHAPE;
        return true;

    } else if (!showDragger.getValue() && toggle != 0) {
        pcShadowPickStyle->style = SoPickStyle::UNPICKABLE;
        SbBox3f bbox;
        showDragger = TRUE;
        owner->getSceneBoundBox(bbox);
        this->getBoundingBox(bbox);
        if (isValidBBox(bbox))
            owner->viewBoundBox(bbox);
        return true;
    }
    return false;
}

static std::vector<std::string> getBoxSelection(const Base::Vector3d *dir,
        ViewProviderDocumentObject *vp, bool center, bool pickElement,
        const Base::ViewProjMethod &proj, const Base::Polygon2d &polygon,
        const Base::Matrix4D &mat, bool transform=true, int depth=0)
{
    std::vector<std::string> ret;
    auto obj = vp->getObject();
    if(!obj || !obj->getNameInDocument())
        return ret;

    // DO NOT check this view object Visibility, let the caller do this. Because
    // we may be called by upper object hierarchy that manages our visibility.

    auto bbox3 = vp->getBoundingBox(0,&mat,transform);
    if(!bbox3.IsValid())
        return ret;

    auto bbox = bbox3.ProjectBox(&proj);

    // check if both two boundary points are inside polygon, only
    // valid since we know the given polygon is a box.
    if(!pickElement
            && polygon.Contains(Base::Vector2d(bbox.MinX,bbox.MinY))
            && polygon.Contains(Base::Vector2d(bbox.MaxX,bbox.MaxY)))
    {
        ret.emplace_back("");
        return ret;
    }

    if(!bbox.Intersect(polygon))
        return ret;

    const auto &subs = obj->getSubObjects(App::DocumentObject::GS_SELECT);
    if(subs.empty()) {
        if(!pickElement) {
            if(!center || polygon.Contains(bbox.GetCenter()))
                ret.emplace_back("");
            return ret;
        }
        Base::PyGILStateLocker lock;
        PyObject *pyobj = nullptr;
        Base::Matrix4D matCopy(mat);
        obj->getSubObject(nullptr,&pyobj,&matCopy,transform,depth);
        if(!pyobj)
            return ret;
        Py::Object pyobject(pyobj,true);
        if(!PyObject_TypeCheck(pyobj,&Data::ComplexGeoDataPy::Type))
            return ret;
        auto data = static_cast<Data::ComplexGeoDataPy*>(pyobj)->getComplexGeoDataPtr();
        Base::Polygon2d loop;
        for(auto type : data->getElementTypes()) {
            size_t count = data->countSubElements(type);
            if(!count)
                continue;
            for(size_t i=1;i<=count;++i) {
                std::string element(type);
                element += std::to_string(i);
                std::unique_ptr<Data::Segment> segment(data->getSubElementByName(element.c_str()));
                if(!segment)
                    continue;
                std::vector<Base::Vector3d> points;
                std::vector<Data::ComplexGeoData::Line> lines;

                std::vector<Base::Vector3d> pointNormals; // not used
                std::vector<Data::ComplexGeoData::Facet> faces;

                // Call getFacesFromSubElement to obtain the triangulation of
                // the segment.
                data->getFacesFromSubElement(segment.get(),points,pointNormals,faces);
                if(faces.empty()) {
                    data->getLinesFromSubElement(segment.get(),points,lines);
                    if(lines.empty()) {
                        if(points.empty())
                            continue;
                        auto v = proj(points[0]);
                        if(polygon.Contains(Base::Vector2d(v.x,v.y)))
                            ret.push_back(element);
                        continue;
                    }
                    loop.DeleteAll();
                    auto v = proj(points[lines.front().I1]);
                    loop.Add(Base::Vector2d(v.x,v.y));
                    for(auto &line : lines) {
                        for(auto i=line.I1;i<line.I2;++i) {
                            auto v = proj(points[i+1]);
                            loop.Add(Base::Vector2d(v.x,v.y));
                        }
                    }
                    if(polygon.Intersect(loop)
                        // Center selection for edges doesn't seem to make much sense, or does it?
                        //
                        // && (mode!=CENTER || polygon.Contains(loop.CalcBoundBox().GetCenter())
                      )
                    {
                        ret.push_back(element);
                    }
                    continue;
                }

                loop.DeleteAll();
                bool hit = false;
                for(auto &facet : faces) {
                    // back face cull
                    if (dir) {
                        Base::Vector3d normal = (points[facet.I2] - points[facet.I1])
                            % (points[facet.I3] - points[facet.I1]);
                        normal.Normalize();
                        if (normal.Dot(*dir) < 0.0f)
                            continue;
                    }
                    auto v = proj(points[facet.I1]);
                    loop.Add(Base::Vector2d(v.x, v.y));
                    v = proj(points[facet.I2]);
                    loop.Add(Base::Vector2d(v.x, v.y));
                    v = proj(points[facet.I3]);
                    loop.Add(Base::Vector2d(v.x, v.y));
                    if (!center) {
                        if(polygon.Intersect(loop)) {
                            hit = true;
                            break;
                        }
                        loop.DeleteAll();
                    }
                }
                if (center && loop.GetCtVectors()
                           && polygon.Contains(loop.CalcBoundBox().GetCenter()))
                    hit = true;
                if (hit)
                    ret.push_back(element);
            }
        }
        return ret;
    }

    size_t count = 0;
    for(auto &sub : subs) {
        App::DocumentObject *parent = nullptr;
        std::string childName;
        Base::Matrix4D smat(mat);
        auto sobj = obj->resolve(sub.c_str(),&parent,&childName,0,0,&smat,transform,depth+1);
        if(!sobj)
            continue;
        int vis;
        if(!parent || (vis=parent->isElementVisibleEx(childName.c_str(),App::DocumentObject::GS_SELECT))<0)
            vis = sobj->Visibility.getValue()?1:0;

        if(!vis)
            continue;

        auto svp = dynamic_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(sobj));
        if(!svp)
            continue;

        const auto &sels = getBoxSelection(dir,svp,center,pickElement,proj,polygon,smat,false,depth+1);
        if(sels.size()==1 && sels[0] == "")
            ++count;
        for(auto &sel : sels)
            ret.emplace_back(sub+sel);
    }
    if(count==subs.size()) {
        ret.resize(1);
        ret[0].clear();
    }
    return ret;
}


std::vector<App::SubObjectT>
View3DInventorViewer::getPickedList(const std::vector<SbVec2f> &pts,
                                    bool center,
                                    bool pickElement,
                                    bool backfaceCull,
                                    bool currentSelection,
                                    bool unselect,
                                    bool mapCoords) const
{
    std::vector<App::SubObjectT> res;

    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc)
        return res;

    auto getPt = [this,mapCoords](const SbVec2f &p) -> Base::Vector2d {
        Base::Vector2d pt(p[0], p[1]);
        if (mapCoords) {
            pt.y = this->height() - pt.y - 1;
            if (this->width())
                pt.x /= this->width();
            if (this->height())
                pt.y /= this->height();
        }
        return pt;
    };

    Base::Polygon2d polygon;
    if (pts.size() == 2) {
        auto pt1 = getPt(pts[0]);
        auto pt2 = getPt(pts[1]);
        polygon.Add(Base::Vector2d(pt1.x, pt1.y));
        polygon.Add(Base::Vector2d(pt1.x, pt2.y));
        polygon.Add(Base::Vector2d(pt2.x, pt2.y));
        polygon.Add(Base::Vector2d(pt2.x, pt1.y));
    } else {
        for (auto &pt : pts)
            polygon.Add(getPt(pt));
    }

    Base::Vector3d vdir, *pdir = nullptr;
    if (backfaceCull) {
        SbVec3f pnt, dir;
        this->getNearPlane(pnt, dir);
        vdir = Base::Vector3d(dir[0],dir[1],dir[2]);
        pdir = &vdir;
    }

    SoCamera* cam = this->getSoRenderManager()->getCamera();
    SbViewVolume vv = cam->getViewVolume();
    Gui::ViewVolumeProjection proj(vv);

    std::set<App::SubObjectT> sels;
    std::map<App::SubObjectT, std::vector<const App::SubObjectT*> > selObjs;
    if(currentSelection || unselect) {
        for (auto &sel : Gui::Selection().getSelectionT(doc->getName(),ResolveMode::NoResolve)) {
            auto r = sels.insert(sel);
            const App::SubObjectT &objT = *r.first;
            if (currentSelection || (unselect && !pickElement))
                selObjs[App::SubObjectT(sel.getDocumentName().c_str(),
                                        sel.getObjectName().c_str(),
                                        sel.getSubNameNoElement().c_str())].push_back(&objT);
        }
    }

    auto handler = [&](App::SubObjectT &&objT) {
        if (!unselect) {
            res.push_back(std::move(objT));
            return;
        }
        if (pickElement) {
            if (sels.count(objT))
                res.push_back(std::move(objT));
            return;
        }
        auto it = selObjs.find(objT);
        if (it != selObjs.end()) {
            for (auto selT : it->second)
                res.push_back(*selT);
        }
    };

    if(currentSelection && sels.size()) {
        for(auto &v : selObjs) {
            auto &sel = v.first;
            App::DocumentObject *obj = sel.getObject();
            if (!obj)
                continue;
            Base::Matrix4D mat;
            App::DocumentObject *sobj = obj->getSubObject(sel.getSubName().c_str(),nullptr,&mat);
            auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                    Application::Instance->getViewProvider(sobj));
            if(!vp)
                continue;
            for(auto &sub : getBoxSelection(pdir,vp,center,pickElement,proj,polygon,mat,false))
                handler(App::SubObjectT(obj, (sel.getSubName()+sub).c_str()));
        }

    } else {
        for(auto obj : doc->getObjects()) {
            if(App::GeoFeatureGroupExtension::isNonGeoGroup(obj)
                    || App::GeoFeatureGroupExtension::getGroupOfObject(obj))
                continue;

            auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                    Application::Instance->getViewProvider(obj));
            if (!vp || !vp->isVisible() || !vp->isShowable())
                continue;

            Base::Matrix4D mat;
            for(auto &sub : getBoxSelection(pdir,vp,center,pickElement,proj,polygon,mat))
                handler(App::SubObjectT(obj, sub.c_str()));
        }
    }

    return res;
}

void View3DInventorViewer::setTransparencyOnTop(float t)
{
    inventorSelection->getGroupOnTopDispMode()->transparency = t;
}

struct HatchTextureFile {
    QDateTime date;
    SbImage image;
};

static std::map<QString, HatchTextureFile> _HatchTextures;

void View3DInventorViewer::refreshRenderCache()
{
    if (auto manager = selectionRoot->getRenderManager()) {
        manager->clear();
        selectionRoot->touch();
    }
}

void View3DInventorViewer::updateHatchTexture()
{
    if (auto manager = selectionRoot->getRenderManager()) {
        QString path = QString::fromUtf8(ViewParams::getSectionHatchTexture().c_str());
        QDateTime date;
        auto &entry = _HatchTextures[path];
        if (!path.startsWith(QLatin1Char(':'))) {
            QFileInfo finfo(path);
            if (!finfo.exists()) {
                manager->setHatchImage(nullptr,0,0,0);
                return;
            }
            date = finfo.lastModified();
        }
        if (entry.date != date || !entry.image.hasData()) {
            entry.date = date;
            QImage img = QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            SoSFImage tmp;
            BitmapFactory().convert(img, tmp);
            entry.image = tmp.getValue();
        }
        SbVec2s size;
        int nc;
        auto dataptr = entry.image.getValue(size,nc);
        manager->setHatchImage(dataptr,nc,size[0],size[1]);
        redraw();
    }
}

const SoPath *View3DInventorViewer::getGroupOnTopPath()
{
    return inventorSelection->getGroupOnTopPath();
}

const SoPath *View3DInventorViewer::getRootPath()
{
    return inventorSelection->getRootPath();
}

const SoPathList *View3DInventorViewer::getLatePickPaths() const
{
    auto action = this->getSoRenderManager()->getGLRenderAction();
    if (action->isOfType(SoBoxSelectionRenderAction::getClassTypeId()))
        return static_cast<SoBoxSelectionRenderAction*>(action)->getLatePickPaths();
    return nullptr;
}


#include "moc_View3DInventorViewer.cpp"
