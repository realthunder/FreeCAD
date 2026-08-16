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
# include <Inventor/nodes/SoAnnotation.h>
# include <Inventor/nodes/SoBaseColor.h>
# include <Inventor/nodes/SoCallback.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoDirectionalLight.h>
# include <Inventor/nodes/SoDrawStyle.h>
# include <Inventor/nodes/SoEnvironment.h>
# include <Inventor/nodes/SoEventCallback.h>
# include <Inventor/nodes/SoFaceSet.h>
# include <Inventor/nodes/SoIndexedFaceSet.h>
# include <Inventor/nodes/SoIndexedLineSet.h>
# include <Inventor/nodes/SoLightModel.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
# include <Inventor/nodes/SoPolygonOffset.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoRotation.h>
# include <Inventor/nodes/SoScale.h>
# include <Inventor/nodes/SoSelection.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoSwitch.h>
# include <Inventor/nodes/SoTransform.h>
# include <Inventor/nodes/SoTransformSeparator.h>
# include <Inventor/nodes/SoTranslation.h>
# include <Inventor/events/SoMouseButtonEvent.h>
# include <Inventor/nodes/SoTexture2.h>
# include <QApplication>
# include <QBitmap>
# include <QCoreApplication>
# include <QDir>
# include <QElapsedTimer>
# include <QPointer>
# include <QEventLoop>
# include <QKeyEvent>
# include <QMessageBox>
# include <QMimeData>
# include <QFileInfo>
# include <QTemporaryFile>
# include <QTimer>
# include <QVariantAnimation>
# include <QWheelEvent>
# include <QFontMetrics>
# include <QImage>
# include <QPainter>
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
#include <Base/Builder3D.h>
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
#include "Clipping.h"
#include "CornerCrossLetters.h"
#include "Document.h"
#include "GLPainter.h"
#include "MainWindow.h"
#include "NaviCube.h"
#include "NavigationStyle.h"
#include "Selection.h"
#include "SoAxisCrossKit.h"
#include "SoTextImage.h"
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
#include "SoDatumLabel.h"
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
#include "ObjectMetaFeed.h"
#include "RenderParams.h"
#include "RenderTiming.h"
// The render cache's entries hold references to vertex caches, and its
// header only forward-declares the type; a translation unit that reaches
// it has to complete it.
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/ScenePublishDelta.h"
#include "ViewProviderDocumentObject.h"
#include "ViewProviderLink.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneServer.h"
#include "SceneControl.h"
#include "NavigationAnimator.h"
#include "NavigationAnimation.h"
#include "Utilities.h"

FC_LOG_LEVEL_INIT("3DViewer",true,true)

//#define FC_LOGGING_CB

using namespace Gui;
using namespace Render;

// Disabled as bitmap cursor looks really bad on high DPI screen. Use Qt
// built-in cursor for now.
//
// #define FC_USE_BITMAP_CURSOR

#ifdef FC_USE_BITMAP_CURSOR

// NOLINTBEGIN
// clang-format off
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
    0x00, 0x0f, 0x80, 0x1f, 0xc0, 0x3f, 0xe0, 0x7f,
    0xf0, 0xff, 0xf0, 0xff, 0xf0, 0xff, 0x00, 0x0f,
    0x00, 0x0f, 0xf0, 0xff, 0xf0, 0xff, 0xf0, 0xff,
    0xe0, 0x7f, 0xc0, 0x3f, 0x80, 0x1f, 0x00, 0x0f
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
    0xc0, 0x03, 0xe0, 0x03, 0xe0, 0x07, 0xf0, 0x0f,
    0xe8, 0x17, 0xdc, 0x3b, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xdc, 0x3b, 0xe8, 0x17,
    0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0xc0, 0x03
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
    0xf0, 0xef, 0xf8, 0xff, 0xfc, 0xff, 0xfc, 0xff,
    0x3c, 0xfe, 0x1c, 0xff, 0x00, 0xff, 0x00, 0xff,
    0xff, 0x00, 0xff, 0x00, 0xff, 0x38, 0x7f, 0x3c,
    0xff, 0x3f, 0xff, 0x3f, 0xff, 0x1f, 0xf7, 0x0f
};
#endif
// clang-format on
// NOLINTEND


/*!
As ProgressBar has no chance to control the incoming Qt events of Quarter so we need to stop
the event handling to prevent the scenegraph from being selected or deselected
while the progress bar is running.
*/
class Gui::ViewerEventFilter : public QObject
{
public:
    ViewerEventFilter() = default;
    ~ViewerEventFilter() override = default;

    bool eventFilter(QObject* obj, QEvent* event) override {
        // Bug #0000607: Some mice also support horizontal scrolling which however might
        // lead to some unwanted zooming when pressing the MMB for panning.
        // Thus, we filter out horizontal scrolling.
        if (event->type() == QEvent::Wheel) {
            auto we = static_cast<QWheelEvent*>(event);  // NOLINT
            if (qAbs(we->angleDelta().x()) > qAbs(we->angleDelta().y())) {
                return true;
            }
        }
        else if (event->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(event);  // NOLINT
            if (ke->matches(QKeySequence::SelectAll)) {
                static_cast<View3DInventorViewer*>(obj)->selectAll();
                return true;
            }
        }

        if (Base::Sequencer().isRunning() && Base::Sequencer().isBlocking()) {
            return false;
        }

        if (event->type() == Spaceball::ButtonEvent::ButtonEventType) {
            auto buttonEvent = static_cast<Spaceball::ButtonEvent*>(event);  // NOLINT
            if (!buttonEvent) {
                Base::Console().Log("invalid spaceball button event\n");
                return true;
            }
        }
        else if (event->type() == Spaceball::MotionEvent::MotionEventType) {
            auto motionEvent = static_cast<Spaceball::MotionEvent*>(event);  // NOLINT
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
    SpaceNavigatorDevice() = default;
    ~SpaceNavigatorDevice() override = default;
    const SoEvent* translateEvent(QEvent* event) override {

        if (event->type() == Spaceball::MotionEvent::MotionEventType) {
            auto motionEvent = static_cast<Spaceball::MotionEvent*>(event);  // NOLINT
            if (!motionEvent) {
                Base::Console().Log("invalid spaceball motion event\n");
                return nullptr;
            }

            motionEvent->setHandled(true);

            float xTrans{};
            float yTrans{};
            float zTrans{};
            xTrans = static_cast<float>(motionEvent->translationX());
            yTrans = static_cast<float>(motionEvent->translationY());
            zTrans = static_cast<float>(motionEvent->translationZ());
            SbVec3f translationVector(xTrans, yTrans, zTrans);

            constexpr const float rotationConstant(.0001F);
            SbRotation xRot;
            SbRotation yRot;
            SbRotation zRot;
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

/// View3DInventor::getProperty, but on any property container: the
/// Render_* overrides are materialized identically on a 3D view and on a
/// view-less publisher (docs/HeadlessServe.md §3.3), and only
/// getPropertyByName/addDynamicProperty are ever needed.
///
/// A group of several words names its properties without the spaces --
/// group "Render Shadow" carries RenderShadow_Epsilon -- which is the
/// property editor's own rule (PropertyModel.cpp setPropertyItemName
/// drops a `<group without spaces>_` prefix from what it shows), so such
/// a property displays as plain "Epsilon" under a "Render Shadow"
/// heading. Stating the group is therefore the whole of grouping.
template<class PropT, class ValueT, class CallbackT>
static ValueT _containerProperty(App::PropertyContainer *view,
                                 const char *_name, const char *_docu,
                                 const char *group, const ValueT &def,
                                 CallbackT cb, short attr = App::Prop_None) {
    char name[128];
    char prefix[64];
    size_t n = 0;
    for (const char *c = group; *c && n + 1 < sizeof(prefix); ++c) {
        if (*c != ' ')
            prefix[n++] = *c;
    }
    prefix[n] = '\0';
    snprintf(name, sizeof(name)-1, "%s_%s", prefix, _name);
    auto prop = view->getPropertyByName(name);
    if (prop && !prop->isDerivedFrom(PropT::getClassTypeId()))
        return def;
    if (!prop) {
        // The attribute has to be given here: Prop_NoPersist reaches the
        // property through syncType at construction, and setStatus cannot
        // add it afterwards -- Property::setStatusValue masks that bit out
        // and keeps whatever the property was created with.
        prop = view->addDynamicProperty(PropT::getClassTypeId().getName(),
                                        name, group, _docu, attr);
        static_cast<PropT*>(prop)->setValue(def);
    }
    cb(*static_cast<PropT*>(prop));
    return static_cast<PropT*>(prop)->getValue();
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

template<class PropT, class ValueT>
static ValueT _renderParam(App::PropertyContainer *view, const char *_name, const char *_docu, const ValueT &def) {
    if (!view)
        return def;
    auto cb = [](PropT &){};
    return _containerProperty<PropT, ValueT>(view, _name, _docu, "Render", def, cb);
}

template<class PropT, class ValueT, class CallbackT>
static ValueT _renderParam(App::PropertyContainer *view, const char *_name, const char *_docu, const ValueT &def, CallbackT cb) {
    if (!view)
        return def;
    return _containerProperty<PropT, ValueT>(view, _name, _docu, "Render", def, cb);
}

/// The scene light's shadow map and its ground receiver
/// (docs/CoinRetirement.md stage 4d). Its own group, so the twenty-odd
/// knobs the Shadow draw style used to carry do not swamp the Render
/// one: RenderShadow_<Name>, shown as <Name> under "Render Shadow".
template<class PropT, class ValueT, class CallbackT>
static ValueT _shadowRenderParam(App::PropertyContainer *view, const char *_name,
                                 const char *_docu, const ValueT &def, CallbackT cb) {
    if (!view)
        return def;
    return _containerProperty<PropT, ValueT>(view, _name, _docu,
                                             "Render Shadow", def, cb);
}

template<class PropT, class ValueT>
static ValueT _shadowRenderParam(App::PropertyContainer *view, const char *_name,
                                 const char *_docu, const ValueT &def) {
    if (!view)
        return def;
    auto cb = [](PropT &){};
    return _containerProperty<PropT, ValueT>(view, _name, _docu,
                                             "Render Shadow", def, cb);
}

/// A render setting that states what the machine can afford rather than
/// what the model should look like. Same per-view property as the rest,
/// but created Prop_NoPersist so that it never reaches a file and so
/// never arrives on somebody else's installation as their budget.
template<class PropT, class ValueT>
static ValueT _localRenderParam(App::PropertyContainer *view, const char *_name,
                                const char *_docu, const ValueT &def) {
    if (!view)
        return def;
    auto cb = [](PropT &){};
    return _containerProperty<PropT, ValueT>(view, _name, _docu, "Render", def, cb,
                                             App::Prop_NoPersist);
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

    // Overlay captures (raw-GL overlay Coin-ification): mirror the
    // foreground superimposition and the corner axis cross to the
    // backend's overlay feed, each through a dedicated render-cache
    // manager in overlay mode (SoFCRenderCacheManager::setExternalOverlay).
    enum OverlayId {
        OverlayForeground = 1,
        OverlayAxisCross = 2,
        OverlayGraphicsItems = 3,
        OverlayFpsText = 4,
        OverlayNaviCube = 5,
        OverlayNaviButtons = 6,
        OverlayEditing = 7,
        OverlayDimensions = 8,
        OverlayDebugLabel = 9,
    };
    struct OverlayCapture {
        CoinPtr<SoNode> root;
        // The capture runs inside its own tiny GL render action traversal
        // (an SoCallback under applyRoot) so it gets a live traversal
        // state without touching the viewer's shared render action.
        CoinPtr<SoSeparator> applyRoot;
        std::unique_ptr<SoFCRenderCacheManager> manager;
    };
    OverlayCapture foregroundCapture;
    OverlayCapture axisCrossCapture;
    OverlayCapture graphicsItemsCapture;
    OverlayCapture fpsTextCapture;
    OverlayCapture naviCubeCapture;
    OverlayCapture naviButtonCapture;
    // In-scene editing overlays (Sketcher constraints/datums, edit-mode
    // helpers) captured into a scene-camera overlay feed: the edit graph
    // lives under pcEditingRoot, a sibling of the render-cache-captured
    // selectionRoot, so it never reaches the main scene feed.
    OverlayCapture editingCapture;
    // In-scene Measure/Part dimensions (leaders, arrows, dimension text)
    // captured into a scene-camera overlay feed: dimensionRoot is attached to
    // the aux root (a sibling of the render-cache-captured selectionRoot), so
    // it never reaches the main scene feed. Mirrors editingCapture.
    OverlayCapture dimensionCapture;
    // Whether the editing overlay is currently fed to (and thus drawn by) the
    // external backend. When true, renderScene() suppresses the raw-GL datum
    // draw so it is not doubled with the backend's.
    bool editingBackendFed = false;
    // fps overlay state: the string renderScene() wants displayed (empty
    // when the readout is off) and the nodes/values last fed.
    std::string fpsText;
    CoinPtr<SoTexture2> fpsTexture;
    CoinPtr<SoCoordinate3> fpsCoords;
    std::string fpsFedText;
    SbVec2s fpsFedVp {0, 0};
    // Render-debug capture burn-in (docs/RenderDebug.md §4.3): the
    // self-describing corner label fed while RenderDebug_Label is on.
    OverlayCapture debugLabelCapture;
    CoinPtr<SoTexture2> debugLabelTexture;
    CoinPtr<SoCoordinate3> debugLabelCoords;
    std::string debugLabelFedText;
    SbVec2s debugLabelFedVp {0, 0};

    // What the backend draws outside the scene graph — today the shadow
    // ground — has no node for a bounding box traversal to find, so auto
    // clipping cuts it away: SoRenderManagerP::setClippingPlanes applies
    // an SoGetBoundingBoxAction on every render, and the answer holds the
    // model alone. This callback carries the backend's own bounds into
    // that traversal.
    //
    // It has to sit *directly under the render manager's superscene*,
    // which Quarter builds with boundingBoxCaching OFF, so it is asked
    // every traversal. The obvious place — SoFCUnifiedSelection::
    // getBoundingBox, which already reports the same bounds — is below
    // whatever separator the scene root is wrapped in (SoShadowGroup, in
    // the very draw style that has a ground), and that one caches: it
    // answers once and nothing the backend draws can invalidate a Coin
    // cache. docs/CoinRetirement.md §1c.
    CoinPtr<SoCallback> rendererBoundsNode;
    static void rendererBoundsCB(void *ud, SoAction *action);
    void addRendererBoundsNode();

    // Redraw throttle for a document being filled by a live operation. The
    // clock is monotonic and starts with the viewer, so the zero stamps below
    // read as long overdue: the first request of an import always renders at
    // once rather than waiting out a phantom interval.
    QElapsedTimer throttleClock;
    QTimer throttleTimer;
    qint64 lastRedrawMs = 0;
    qint64 lastInputMs = 0;
    double frameCostMs = 0.0;

    Private(View3DInventorViewer *owner)
        :view(qobject_cast<View3DInventor*>(owner->parent()))
        ,owner(owner)
        ,tmpPath(new SoTempPath(10))
        ,pickAction(SbViewportRegion())
        ,pickMatrixAction(SbViewportRegion())
    {
        throttleClock.start();
    }

    /** Whether this redraw request should be held back.
     *
     * Returns true when the frame is dropped, having armed the timer that
     * asks for it again once the interval is up — a deferred request is never
     * lost, so the last state of a finished import is always drawn.
     */
    bool deferRedraw();
    void noteInput()
    {
        lastInputMs = throttleClock.elapsed();
    }
    void noteFrameCost(double ms)
    {
        // Smoothed, because the throttle should follow what a frame costs on
        // this scene, not what one unlucky frame cost: a shadow map rebuild
        // must not lock the view down for the rest of an import, and one
        // cheap frame must not unlock it.
        frameCostMs = frameCostMs > 0.0 ? 0.5 * frameCostMs + 0.5 * ms : ms;
    }

    /// Describe the window background so the backend draws it behind
    /// the scene: transparent geometry must blend against the real
    /// background, not the backend's clear color. Shared by the
    /// on-screen frame and offscreen captures.
    Render::Background backgroundFeed(const QColor &col) const;

    void updateOverlayCaptures(SoGLRenderAction *glra);
    void clearOverlayCaptures();
    static void overlayCaptureCB(void *ud, SoAction *action);

    void activateShadow();
    void deactivateShadow();
    void updateShadowExtent(const SbBox3f &box);
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

// Coin geometry equivalent of drawArrow()/drawAxisCross(): one arrow along
// +x (shaft box + crossed head fins), instanced three times with the axis
// colors and rotations, plus the "X"/"Y"/"Z" labels as SoTextImage glyph
// companions just beyond the arrow tips. The labels billboard (backend-side,
// against the overlay's own mini camera) so they stay screen-aligned under any
// camera — including the WASM viewer's independent orbit, where the old baked
// counter-rotation skewed. Captured through the backend's overlay feed so the
// corner axis cross renders without the immediate-mode GL path (and in the
// WASM viewer).
static SoSeparator *createAxisCrossOverlayGraph()
{
    constexpr float shaftEnd = 1.0F - 1.0F / 3.0F;
    constexpr float s = 0.02F;       // shaft half thickness
    constexpr float f = 0.5F / 4.0F; // head fin half width
    static const SbVec3f coords[13] = {
        // shaft box corners (x = 0 and x = shaftEnd)
        {0, -s,  s}, {0,  s,  s}, {0,  s, -s}, {0, -s, -s},
        {shaftEnd, -s,  s}, {shaftEnd,  s,  s},
        {shaftEnd,  s, -s}, {shaftEnd, -s, -s},
        // head: tip + fin corners
        {1, 0, 0},
        {shaftEnd,  f, 0}, {shaftEnd, -f, 0},
        {shaftEnd, 0,  f}, {shaftEnd, 0, -f},
    };
    static const int32_t indices[] = {
        0, 1, 5, 4, -1,    // shaft +z side
        3, 2, 6, 7, -1,    // shaft -z side
        0, 3, 7, 4, -1,    // shaft -y side
        1, 2, 6, 5, -1,    // shaft +y side
        0, 1, 2, 3, -1,    // shaft end cap at x = 0
        8, 9, 10, -1,      // head fin in the xy plane
        8, 11, 12, -1,     // head fin in the xz plane
        9, 11, 10, 12, -1, // head base quad
    };

    auto coord = new SoCoordinate3;
    coord->point.setValues(0, 13, coords);
    auto faces = new SoIndexedFaceSet;
    faces->coordIndex.setValues(
        0, int(sizeof(indices) / sizeof(indices[0])), indices);

    auto root = new SoSeparator;
    auto lightModel = new SoLightModel; // like glDisable(GL_LIGHTING)
    lightModel->model = SoLightModel::BASE_COLOR;
    root->addChild(lightModel);

    const SbColor axisColors[3] = {
        {0.5F, 0.125F, 0.125F},
        {0.125F, 0.5F, 0.125F},
        {0.125F, 0.125F, 0.5F},
    };
    const SbRotation axisRotations[3] = {
        SbRotation::identity(),
        SbRotation(SbVec3f(0, 0, 1), float(M_PI_2)),  // y axis
        SbRotation(SbVec3f(0, 1, 0), -float(M_PI_2)), // z axis
    };
    for (int i = 0; i < 3; ++i) {
        auto sep = new SoSeparator;
        auto color = new SoBaseColor;
        color->rgb = axisColors[i];
        sep->addChild(color);
        if (i) {
            auto rot = new SoRotation;
            rot->rotation = axisRotations[i];
            sep->addChild(rot);
        }
        sep->addChild(coord);
        sep->addChild(faces);
        root->addChild(sep);
    }

    // Axis labels: black like drawAxisCross()'s letter pixmaps, placed just
    // beyond each arrow tip. The white glyph atlas is MODULATE-tinted by this
    // base colour; the SoTextImage companion billboards itself to face the
    // viewer, so no per-frame counter-rotation is needed.
    auto letterColor = new SoBaseColor;
    letterColor->rgb = SbColor(0.0F, 0.0F, 0.0F);
    root->addChild(letterColor);
    // Just past each arrow tip (arrows reach 1.0); kept inside the tight
    // corner frustum so the vertically-centred glyphs do not clip at the edge.
    const SbVec3f tips[3] = {
        {1.15F, 0, 0}, {0, 1.15F, 0}, {0, 0, 1.15F}};
    static const char *const letters[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        auto sep = new SoSeparator;
        auto trans = new SoTranslation;
        trans->translation = tips[i];
        sep->addChild(trans);
        Gui::SoTextImage *img = nullptr;
        SoSeparator *companion = Gui::SoTextImage::createSubGraph(&img);
        img->string.setValue(letters[i]);
        img->fontSize = 14.0F;
        img->justification = Gui::SoTextImage::CENTER;
        img->vcenter = TRUE;
        sep->addChild(companion);
        root->addChild(sep);
    }
    return root;
}

// SoCallback body of an overlay capture: runs mid-traversal of the
// capture's own render action, so the cache build sees a live state.
void View3DInventorViewer::Private::overlayCaptureCB(void *ud, SoAction *action)
{
    if (!action->isOfType(SoGLRenderAction::getClassTypeId()))
        return;
    auto *capture = static_cast<OverlayCapture *>(ud);
    capture->manager->capture(static_cast<SoGLRenderAction *>(action),
                              capture->root);
}

Render::Background
View3DInventorViewer::Private::backgroundFeed(const QColor &col) const
{
    Render::Background rbg;
    if (owner->hasGradientBackground()) {
        SbColor fcol, tcol, mcol;
        rbg.hasMid = owner->pcBackGround->getColorGradient(fcol, tcol, mcol);
        rbg.type = owner->getGradientBackground() == Background::LinearGradient
            ? Render::Background::LinearGradient
            : Render::Background::RadialGradient;
        rbg.fromColor = fcol.getPackedValue();
        rbg.toColor = tcol.getPackedValue();
        if (rbg.hasMid)
            rbg.midColor = mcol.getPackedValue();
    } else {
        rbg.type = Render::Background::Flat;
        rbg.fromColor = (uint32_t(col.red()) << 24)
            | (uint32_t(col.green()) << 16)
            | (uint32_t(col.blue()) << 8) | 0xff;
    }
    return rbg;
}

void View3DInventorViewer::Private::updateOverlayCaptures(SoGLRenderAction *glra)
{
    if (!renderer)
        return;

    auto initCapture = [](OverlayCapture &capture, SoNode *root) {
        capture.root = root;
        capture.manager.reset(new SoFCRenderCacheManager);
        capture.applyRoot = new SoSeparator;
        auto cb = new SoCallback;
        cb->setCallback(overlayCaptureCB, &capture);
        capture.applyRoot->addChild(cb);
    };
    // The capture traversals never draw; a private action keeps their
    // state handling away from the viewer's shared render action. It
    // must still declare the real GL cache context — Coin initializes
    // its GL glue for the action's context id even when nothing renders.
    SoGLRenderAction captureAction(
        owner->getSoRenderManager()->getViewportRegion());
    captureAction.setCacheContext(glra->getCacheContext());

    if (!foregroundCapture.manager)
        initCapture(foregroundCapture, owner->foregroundroot);
    // Full-viewport orthographic anchor mirroring the foreground root's own
    // camera (position (0,0,5), height 10, near 0, far 10).
    Render::OverlayAnchor fgAnchor;
    fgAnchor.corner = Render::OverlayAnchor::FullViewport;
    fgAnchor.orthoHeight = 10.0F;
    fgAnchor.cameraDistance = 5.0F;
    fgAnchor.nearPlane = 0.0F;
    fgAnchor.farPlane = 10.0F;
    foregroundCapture.manager->setExternalOverlay(
        renderer.get(), OverlayForeground, fgAnchor);
    captureAction.apply(foregroundCapture.applyRoot);

    if (owner->axiscrossEnabled) {
        if (!axisCrossCapture.manager)
            initCapture(axisCrossCapture, createAxisCrossOverlayGraph());
        // The X/Y/Z labels billboard themselves backend-side (against the
        // overlay's mini camera), so no per-frame counter-rotation is needed;
        // this also fixes the letters skewing under the WASM viewer's own orbit.
        // Corner mini-perspective anchor matching drawAxisCross(): a square
        // viewport of axiscrossSize percent of the smaller viewport edge in
        // the bottom-right corner, 45 deg FOV, content rotated by the scene
        // camera orientation at eye distance 3.5.
        Render::OverlayAnchor anchor;
        anchor.corner = Render::OverlayAnchor::BottomRight;
        anchor.sizeFraction = float(owner->axiscrossSize) / 100.0F;
        anchor.fovDeg = 45.0F;
        anchor.cameraDistance = 3.5F;
        anchor.nearPlane = 0.1F;
        anchor.farPlane = 10.0F;
        anchor.orientFromScene = true;
        axisCrossCapture.manager->setExternalOverlay(
            renderer.get(), OverlayAxisCross, anchor);
        captureAction.apply(axisCrossCapture.applyRoot);
    }
    else if (axisCrossCapture.manager) {
        // Detaching removes the overlay from the backend.
        axisCrossCapture.manager->setExternalOverlay(
            nullptr, OverlayAxisCross, Render::OverlayAnchor());
        axisCrossCapture.manager.reset();
        axisCrossCapture.applyRoot.reset();
        axisCrossCapture.root.reset();
    }

    auto dropCapture = [](OverlayCapture &capture, int id) {
        if (!capture.manager)
            return;
        capture.manager->setExternalOverlay(
            nullptr, id, Render::OverlayAnchor());
        capture.manager.reset();
        capture.applyRoot.reset();
        capture.root.reset();
    };
    // Pixel-space anchor shared by the screen-space overlays below: one
    // model unit is one pixel, origin top-left, y down (Qt coordinates).
    Render::OverlayAnchor pixelAnchor;
    pixelAnchor.corner = Render::OverlayAnchor::FullViewport;
    pixelAnchor.pixelSpace = true;

    // Screen-space GLGraphicsItem drawings (rubber band, polyline):
    // aggregate the items' Coin overlay graphs under one pixel-space
    // feed. Items without an overlay port (e.g. flag leader lines) keep
    // painting through GL in renderScene().
    std::vector<SoSeparator *> itemGraphs;
    for (auto item : owner->graphicsItems) {
        if (auto graph = item->getOverlaySceneGraph())
            itemGraphs.push_back(graph);
    }
    if (!itemGraphs.empty()) {
        if (!graphicsItemsCapture.manager)
            initCapture(graphicsItemsCapture, new SoSeparator);
        auto aggRoot =
            static_cast<SoSeparator *>(graphicsItemsCapture.root.get());
        bool changed = aggRoot->getNumChildren() != int(itemGraphs.size());
        for (int i = 0; !changed && i < aggRoot->getNumChildren(); ++i)
            changed = aggRoot->getChild(i) != itemGraphs[size_t(i)];
        if (changed) {
            coinRemoveAllChildren(aggRoot);
            for (auto graph : itemGraphs)
                aggRoot->addChild(graph);
        }
        graphicsItemsCapture.manager->setExternalOverlay(
            renderer.get(), OverlayGraphicsItems, pixelAnchor);
        captureAction.apply(graphicsItemsCapture.applyRoot);
    }
    else {
        dropCapture(graphicsItemsCapture, OverlayGraphicsItems);
    }

    // fps/stats readout (draw2DString): the string rendered into a small
    // texture on one pixel-space quad. GL parity: yellow text one percent
    // in from the viewport's bottom-left corner.
    if (!fpsText.empty()) {
        if (!fpsTextCapture.manager) {
            auto root = new SoSeparator;
            auto lightModel = new SoLightModel;
            lightModel->model = SoLightModel::BASE_COLOR;
            root->addChild(lightModel);
            fpsTexture = new SoTexture2;
            root->addChild(fpsTexture);
            fpsCoords = new SoCoordinate3;
            root->addChild(fpsCoords);
            auto texCoords = new SoTextureCoordinate2;
            // Coin images are bottom-up; the quad below starts at its
            // bottom-left corner (largest y in pixel space).
            const SbVec2f uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            texCoords->point.setValues(0, 4, uvs);
            root->addChild(texCoords);
            auto quad = new SoIndexedFaceSet;
            static const int32_t quadIdx[] = {0, 1, 2, 3, -1};
            quad->coordIndex.setValues(0, 5, quadIdx);
            quad->textureCoordIndex.setValues(0, 5, quadIdx);
            root->addChild(quad);
            initCapture(fpsTextCapture, root);
            fpsFedText.clear();
            fpsFedVp = SbVec2s(0, 0);
        }
        const SbVec2s vpsize = owner->getSoRenderManager()
            ->getViewportRegion().getViewportSizePixels();
        if (fpsFedText != fpsText || fpsFedVp != vpsize) {
            QFont font(QStringLiteral("monospace"));
            font.setPixelSize(14);
            QFontMetrics fm(font);
            QString text = QString::fromUtf8(fpsText.c_str());
            int tw = fm.horizontalAdvance(text) + 2;
            int th = fm.height() + 2;
            QImage img(tw, th, QImage::Format_RGBA8888);
            img.fill(Qt::transparent);
            {
                QPainter painter(&img);
                painter.setFont(font);
                painter.setPen(QColor(255, 255, 0));
                painter.drawText(1, 1 + fm.ascent(), text);
            }
            // Coin images are bottom-up, QImage is top-down.
            img = img.mirrored();
            fpsTexture->image.setValue(SbVec2s(short(tw), short(th)), 4,
                                       img.constBits());
            // draw2DString(pos (0.1, 0.1) in a 10x10 ortho): one percent
            // in from the bottom-left corner.
            float x = 0.01F * float(vpsize[0]);
            float yBottom = float(vpsize[1]) - 0.01F * float(vpsize[1]);
            const SbVec3f quadPts[4] = {
                {x, yBottom, 0},
                {x + float(tw), yBottom, 0},
                {x + float(tw), yBottom - float(th), 0},
                {x, yBottom - float(th), 0},
            };
            fpsCoords->point.setValues(0, 4, quadPts);
            fpsFedText = fpsText;
            fpsFedVp = vpsize;
        }
        fpsTextCapture.manager->setExternalOverlay(
            renderer.get(), OverlayFpsText, pixelAnchor);
        captureAction.apply(fpsTextCapture.applyRoot);
    }
    else {
        dropCapture(fpsTextCapture, OverlayFpsText);
        fpsTexture.reset();
        fpsCoords.reset();
    }

    // Render-debug capture burn-in (docs/RenderDebug.md §4.3): with
    // the DebugLabel parameter on, a corner label names the active
    // debug view mode, the freeze state and every custom RenderDebug_*
    // parameter value, so a captured frame documents its settings
    // without its sidecar. Rides the overlay feed like the fps readout
    // -- the WASM viewer burns the same label into its own captures.
    // The switches are global RenderParams; only the custom shader
    // parameters still live on the view.
    std::string debugLabel;
    if (view) {
        if (RenderParams::getDebugLabel()) {
            std::ostringstream ss;
            ss << "RenderDebug ";
            static const char* _viewModeNames[] =
                {"Off", "Depth", "Normal", "AO", "Shadow", "ShadowTile",
                 "Overdraw", "ShadowFilter", "UV", "Reflection",
                 "ImpactMap", "InstanceId"};
            const long mode = RenderParams::getDebugViewMode();
            ss << (mode >= 0
                    && mode < long(sizeof(_viewModeNames)
                                   / sizeof(_viewModeNames[0]))
                    ? _viewModeNames[mode] : "?");
            if (RenderParams::getDebugFreezeFrame())
                ss << " freeze";
            // Custom named parameters (the same set the bridge feeds as
            // uniforms), name=value.
            std::map<std::string, App::Property*> props;
            view->getPropertyMap(props);
            for (const auto &v : props) {
                if (v.first.compare(0, 12, "RenderDebug_") != 0)
                    continue;
                std::string name = v.first.substr(12);
                if (name.empty() || name == "ViewMode"
                        || name == "FreezeFrame" || name == "Label")
                    continue;
                ss << ' ' << name << '=';
                App::Property *prop = v.second;
                auto num = [&ss](double d) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%g", d);
                    ss << buf;
                };
                if (auto p = dynamic_cast<App::PropertyBool*>(prop))
                    ss << (p->getValue() ? "1" : "0");
                else if (auto p = dynamic_cast<App::PropertyEnumeration*>(prop))
                    ss << p->getValue();
                else if (auto p = dynamic_cast<App::PropertyInteger*>(prop))
                    ss << p->getValue();
                else if (auto p = dynamic_cast<App::PropertyFloat*>(prop))
                    num(p->getValue());
                else if (auto p = dynamic_cast<App::PropertyColor*>(prop)) {
                    App::Color c = p->getValue();
                    ss << '[';
                    num(c.r); ss << ','; num(c.g); ss << ',';
                    num(c.b); ss << ']';
                }
                else if (auto p = dynamic_cast<App::PropertyVector*>(prop)) {
                    Base::Vector3d vec = p->getValue();
                    ss << '[';
                    num(vec.x); ss << ','; num(vec.y); ss << ',';
                    num(vec.z); ss << ']';
                }
                else if (auto p = dynamic_cast<App::PropertyFloatList*>(prop)) {
                    ss << '[';
                    for (int i = 0; i < p->getSize(); ++i) {
                        if (i) ss << ',';
                        num(p->getValues()[i]);
                    }
                    ss << ']';
                }
                else if (auto p = dynamic_cast<App::PropertyIntegerList*>(prop)) {
                    ss << '[';
                    for (int i = 0; i < p->getSize(); ++i) {
                        if (i) ss << ',';
                        ss << p->getValues()[i];
                    }
                    ss << ']';
                }
                else
                    ss << '?';
            }
            debugLabel = ss.str();
        }
    }
    if (!debugLabel.empty()) {
        if (!debugLabelCapture.manager) {
            auto root = new SoSeparator;
            auto lightModel = new SoLightModel;
            lightModel->model = SoLightModel::BASE_COLOR;
            root->addChild(lightModel);
            debugLabelTexture = new SoTexture2;
            root->addChild(debugLabelTexture);
            debugLabelCoords = new SoCoordinate3;
            root->addChild(debugLabelCoords);
            auto texCoords = new SoTextureCoordinate2;
            const SbVec2f uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            texCoords->point.setValues(0, 4, uvs);
            root->addChild(texCoords);
            auto quad = new SoIndexedFaceSet;
            static const int32_t quadIdx[] = {0, 1, 2, 3, -1};
            quad->coordIndex.setValues(0, 5, quadIdx);
            quad->textureCoordIndex.setValues(0, 5, quadIdx);
            root->addChild(quad);
            initCapture(debugLabelCapture, root);
            debugLabelFedText.clear();
            debugLabelFedVp = SbVec2s(0, 0);
        }
        const SbVec2s vpsize = owner->getSoRenderManager()
            ->getViewportRegion().getViewportSizePixels();
        if (debugLabelFedText != debugLabel || debugLabelFedVp != vpsize) {
            QFont font(QStringLiteral("monospace"));
            font.setPixelSize(14);
            QFontMetrics fm(font);
            QString text = QString::fromUtf8(debugLabel.c_str());
            int tw = fm.horizontalAdvance(text) + 2;
            int th = fm.height() + 2;
            QImage img(tw, th, QImage::Format_RGBA8888);
            img.fill(Qt::transparent);
            {
                QPainter painter(&img);
                painter.setFont(font);
                // Orange, distinct from the yellow fps readout.
                painter.setPen(QColor(255, 160, 0));
                painter.drawText(1, 1 + fm.ascent(), text);
            }
            // Coin images are bottom-up, QImage is top-down.
            img = img.mirrored();
            debugLabelTexture->image.setValue(SbVec2s(short(tw), short(th)),
                                              4, img.constBits());
            // One percent in from the top-left corner (the fps readout
            // keeps the bottom-left).
            float x = 0.01F * float(vpsize[0]);
            float yTop = 0.01F * float(vpsize[1]);
            const SbVec3f quadPts[4] = {
                {x, yTop + float(th), 0},
                {x + float(tw), yTop + float(th), 0},
                {x + float(tw), yTop, 0},
                {x, yTop, 0},
            };
            debugLabelCoords->point.setValues(0, 4, quadPts);
            debugLabelFedText = debugLabel;
            debugLabelFedVp = vpsize;
        }
        debugLabelCapture.manager->setExternalOverlay(
            renderer.get(), OverlayDebugLabel, pixelAnchor);
        captureAction.apply(debugLabelCapture.applyRoot);
    }
    else {
        dropCapture(debugLabelCapture, OverlayDebugLabel);
        debugLabelTexture.reset();
        debugLabelCoords.reset();
    }

    // NaviCube (phase C): the rotating cube and the viewport-fixed
    // buttons/menu each ride their own anchor. The graphs live in the
    // NaviCube implementation (per-viewer, over shared textures); null
    // means hidden (auto-hide) or not yet initialized. A root swap
    // (rebuild after a parameter change) re-creates the capture.
    auto feedNaviGraph = [&](OverlayCapture &capture, int id,
                             SoSeparator *graph,
                             const Render::OverlayAnchor &anchor) {
        if (!graph) {
            dropCapture(capture, id);
            return;
        }
        if (capture.manager && capture.root != graph)
            dropCapture(capture, id);
        if (!capture.manager)
            initCapture(capture, graph);
        capture.manager->setExternalOverlay(renderer.get(), id, anchor);
        captureAction.apply(capture.applyRoot);
    };
    if (owner->naviCubeEnabled && owner->naviCube) {
        Render::OverlayAnchor cubeAnchor;
        feedNaviGraph(naviCubeCapture, OverlayNaviCube,
                      owner->naviCube->getOverlayCubeGraph(cubeAnchor),
                      cubeAnchor);
        Render::OverlayAnchor btnAnchor;
        feedNaviGraph(naviButtonCapture, OverlayNaviButtons,
                      owner->naviCube->getOverlayButtonGraph(btnAnchor),
                      btnAnchor);
    }
    else {
        dropCapture(naviCubeCapture, OverlayNaviCube);
        dropCapture(naviButtonCapture, OverlayNaviButtons);
    }

    // In-scene editing overlays (Sketcher constraints/datums, edit-mode
    // draggers): during editing the edit ViewProvider's graph is moved
    // under pcEditingRoot (setEditingRoot), which is a sibling of the
    // render-cache-captured selectionRoot and so never reaches the main
    // scene feed — these are the raw-GL nodes ported in Phase D. Capture
    // pcEditingRoot into a scene-camera overlay so it renders through the
    // backend (and the WASM viewer) sharing the main scene camera; the
    // world-space geometry lines up with the main scene for free. Gated on
    // there being edit content (more than just the editing transform).
    if (owner->pcEditingRoot && owner->pcEditingRoot->getNumChildren() > 1) {
        if (!editingCapture.manager)
            initCapture(editingCapture, owner->pcEditingRoot);
        Render::OverlayAnchor editAnchor;
        editAnchor.sceneCamera = true;
        editingCapture.manager->setExternalOverlay(
            renderer.get(), OverlayEditing, editAnchor);
        captureAction.apply(editingCapture.applyRoot);
        editingBackendFed = true;
    }
    else {
        dropCapture(editingCapture, OverlayEditing);
        editingBackendFed = false;
    }

    // In-scene Measure/Part dimensions: dimensionRoot is a sibling of the
    // captured selectionRoot (attached to the aux root), so its leaders,
    // arrows and dimension text never reach the main scene feed. Capture it
    // into a scene-camera overlay so it renders through the backend (and the
    // WASM viewer) sharing the main scene camera — world-space geometry lines
    // up with the main scene for free. Gated on the dimension switches being
    // turned on with actual content, so an empty capture is skipped.
    bool dimHasContent = false;
    if (owner->dimensionRoot
        && owner->dimensionRoot->whichChild.getValue() != SO_SWITCH_NONE) {
        for (int i = 0; i < owner->dimensionRoot->getNumChildren(); ++i) {
            auto *sw = static_cast<SoSwitch *>(owner->dimensionRoot->getChild(i));
            if (sw->whichChild.getValue() != SO_SWITCH_NONE
                && sw->getNumChildren() > 0) {
                dimHasContent = true;
                break;
            }
        }
    }
    if (dimHasContent) {
        if (!dimensionCapture.manager)
            initCapture(dimensionCapture, owner->dimensionRoot);
        Render::OverlayAnchor dimAnchor;
        dimAnchor.sceneCamera = true;
        dimensionCapture.manager->setExternalOverlay(
            renderer.get(), OverlayDimensions, dimAnchor);
        captureAction.apply(dimensionCapture.applyRoot);
    }
    else {
        dropCapture(dimensionCapture, OverlayDimensions);
    }
}

void View3DInventorViewer::Private::clearOverlayCaptures()
{
    for (auto capture : {&foregroundCapture, &axisCrossCapture,
                         &graphicsItemsCapture, &fpsTextCapture,
                         &naviCubeCapture, &naviButtonCapture,
                         &editingCapture, &dimensionCapture}) {
        if (capture->manager) {
            capture->manager->setExternalOverlay(
                nullptr, 0, Render::OverlayAnchor());
            capture->manager.reset();
        }
        capture->applyRoot.reset();
        capture->root.reset();
    }
    fpsTexture.reset();
    fpsCoords.reset();
}

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
    , nonObjectGroup(nullptr)
    , navigation(nullptr)
    , renderType(Native)
    , framebuffer(nullptr)
    , axisCross(nullptr)
    , axisGroup(nullptr)
    , rotationCenterGroup(nullptr)
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
    , nonObjectGroup(nullptr)
    , navigation(nullptr)
    , renderType(Native)
    , framebuffer(nullptr)
    , axisCross(nullptr)
    , axisGroup(nullptr)
    , rotationCenterGroup(nullptr)
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

    // A redraw held back by the throttle comes back through this timer, so a
    // scene that stops changing still gets its last frame.
    _pimpl->throttleTimer.setSingleShot(true);
    connect(&_pimpl->throttleTimer, &QTimer::timeout, this, [this] { redraw(); });

    static bool _cacheModeInited;
    if (!_cacheModeInited) {
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

    // NOLINTBEGIN
    auto cam = new SoOrthographicCamera;
    cam->position = SbVec3f(0, 0, 1);
    cam->height = 1;
    cam->nearDistance = 0.5;
    cam->farDistance = 1.5;
    // NOLINTEND

    // setup light sources
    SoDirectionalLight* hl = this->getHeadlight();
    backlight = new SoDirectionalLight();
    backlight->ref();
    backlight->setName("backlight");
    backlight->direction.setValue(-hl->direction.getValue());
    backlight->on.setValue(false); // by default off

    // The third light of a three-point rig. Unlike the headlight it is not
    // fixed to the eye: it lives after the camera, in world space, under a
    // rotation slaved to the camera orientation, so its direction stays
    // camera-relative while the geometry below it is not disturbed.
    fillLight = new SoDirectionalLight();
    fillLight->ref();
    fillLight->setName("filllight");
    fillLight->direction.setValue(-0.60F, -0.35F, -0.79F);
    fillLight->intensity.setValue(0.6F);
    fillLight->color.setValue(0.95F, 0.95F, 1.0F);
    fillLight->on.setValue(false); // by default off

    // Coin's own default (0.2 grey) until the preferences say otherwise, so
    // simply having the node changes nothing.
    environment = new SoEnvironment();
    environment->ref();
    environment->setName("environment");

    lightRotation = new SoRotation;
    lightRotation->ref();

    auto threePointLightingSeparator = new SoTransformSeparator;
    threePointLightingSeparator->addChild(lightRotation);
    threePointLightingSeparator->addChild(fillLight);

    viewerLightingRoot = new SoGroup;
    viewerLightingRoot->ref();
    viewerLightingRoot->setName("viewerLightingRoot");
    viewerLightingRoot->addChild(threePointLightingSeparator);
    viewerLightingRoot->addChild(environment);

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

    // NOLINTBEGIN
    cam = new SoOrthographicCamera;
    cam->position = SbVec3f(0, 0, 5);
    cam->height = 10;
    cam->nearDistance = 0;
    cam->farDistance = 10;
    // NOLINTEND

    this->foregroundroot->addChild(cam);
    this->foregroundroot->addChild(lm);
    this->foregroundroot->addChild(bc);

    // NOTE: For every mouse click event the SoFCUnifiedSelection searches for the picked
    // point which causes a certain slow-down because for all objects the primitives
    // must be created. Using an SoSeparator avoids this drawback.
    selectionRoot = new Gui::SoFCUnifiedSelection();
    selectionRoot->applySettings();
    selectionRoot->setViewer(this);
    // The view object answers for the section and clipping style this
    // viewer draws with, wherever it carries a Section_* override.
    selectionRoot->setViewObject(_pimpl->view);

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

    // Create group for the non physical object
    nonObjectGroup = new SoGroup();
    nonObjectGroup->setName("NonObjectGroup");
    nonObjectGroup->ref();
    pcViewProviderRoot->addChild(nonObjectGroup);

    // Set our own render action which show a bounding box if
    // the SoFCSelection::BOX style is set
    //
    // Important note:
    // When creating a new GL render action we have to copy over the cache context id
    // because otherwise we may get strange rendering behaviour. For more details see
    // https://forum.freecad.org/viewtopic.php?f=10&t=7486&start=120#p74398 and for
    // the fix and some details what happens behind the scene have a look at this
    // https://forum.freecad.org/viewtopic.php?f=10&t=7486&p=74777#p74736
    uint32_t id = this->getSoRenderManager()->getGLRenderAction()->getCacheContext();
    this->getSoRenderManager()->setGLRenderAction(new SoBoxSelectionRenderAction);
    this->getSoRenderManager()->getGLRenderAction()->setCacheContext(id);

    // set the transparency and antialiasing settings
    getSoRenderManager()->getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);

    // Settings
    setSeekTime(0.4F);  // NOLINT

    if (!isSeekValuePercentage()) {
        setSeekValueAsPercentage(true);
    }

    setSeekDistance(100);  // NOLINT
    setViewing(false);

    setBackgroundColor(QColor(25, 25, 25));  // NOLINT
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

    // Detach the external render backend before _pimpl->renderer is
    // destroyed; the selection root (and its render cache manager) may
    // outlive this viewer through external references.
    _pimpl->clearOverlayCaptures();
    if (this->selectionRoot)
        this->selectionRoot->setExternalRenderer(nullptr);

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
    this->nonObjectGroup->unref();
    this->nonObjectGroup = nullptr;
    this->backlight->unref();
    this->backlight = nullptr;
    this->viewerLightingRoot->unref();
    this->viewerLightingRoot = nullptr;
    this->lightRotation->rotation.disconnect();
    this->lightRotation->unref();
    this->lightRotation = nullptr;
    this->fillLight->unref();
    this->fillLight = nullptr;
    this->environment->unref();
    this->environment = nullptr;

    inventorSelection.reset(nullptr);

    this->pcEditingRoot->unref();
    this->pcEditingTransform->unref();

    if (this->pcClipPlane) {
        this->pcClipPlane->unref();
    }

    delete this->navigation;

    // Note: When closing the application the main window doesn't exist any more.
    if (getMainWindow()) {
        getMainWindow()->setPaneText(2, QString());
    }

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
    // NOLINTBEGIN
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
    // NOLINTEND
}

void View3DInventorViewer::aboutToDestroyGLContext()
{
    if (naviCube) {
        if (auto gl = qobject_cast<QtGLWidget*>(this->viewport())) {
            gl->makeCurrent();
        }
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

static void syncEnvImageEmbed(View3DInventor *view);

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
        else if (boost::starts_with(prop.getName(),"Light_")) {
            applyLightProperty(prop);
            Dialog::Clipping::onViewPropertyChanged(_pimpl->view, prop.getName());
            getSoRenderManager()->scheduleRedraw();
        }
        else if (boost::starts_with(prop.getName(),"Section_")) {
            // The section style is read afresh per frame, so a redraw is
            // all it takes -- but the Clipping panel is showing what the
            // view says, and the clip plane widget is the panel's own.
            Dialog::Clipping::onViewPropertyChanged(_pimpl->view, prop.getName());
            // The two exceptions: whether an on-top draw is sectioned is
            // baked into the translated draw call (RendererBridge::
            // SectionOnTop), and a draw list is translated when the scene
            // is republished, not per frame. Nothing else would republish
            // it, so this pair has to ask for the re-bake. Not
            // refreshRenderCache(): dropping the caches costs a traversal
            // and takes the selection and highlight feeds with it, which
            // nothing restores until the user selects something again.
            if (!strcmp(prop.getName(), "Section_NoOnTop")
                    || !strcmp(prop.getName(), "Section_Concave"))
                selectionRoot->refreshExternalFeed();
            getSoRenderManager()->scheduleRedraw();
        }
        else if (boost::starts_with(prop.getName(),"Render_")
                 || boost::starts_with(prop.getName(),"RenderDebug_")) {
            // The embedded environment-image copy follows the path and
            // the embed toggle.
            if (!strcmp(prop.getName(), "Render_PBREnvImage")
                    || !strcmp(prop.getName(), "Render_PBREnvEmbed"))
                syncEnvImageEmbed(_pimpl->view);
            // Per-view render engine settings; the per-frame config feed
            // re-reads them, so a redraw is enough.
            getSoRenderManager()->scheduleRedraw();
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
    this->axiscrossSize = 10;  // NOLINT
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
void View3DInventorViewer::onSelectionChanged(const SelectionChanges & reason)
{
    if(!getDocument()) {
        return;
    }

    SelectionChanges Reason(reason);

    if(Reason.pDocName && *Reason.pDocName &&
       strcmp(getDocument()->getDocument()->getName(),Reason.pDocName)!=0) {
        return;
    }

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

bool View3DInventorViewer::searchNode(SoNode* node) const
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

bool View3DInventorViewer::hasViewProvider(ViewProvider* pcProvider) const
{
    return _ViewProviderSet.find(pcProvider) != _ViewProviderSet.end();
}

bool View3DInventorViewer::containsViewProvider(const ViewProvider* vp) const
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
        if(!guiDocument->isClaimed3D(pcProvider) && pcProvider->canAddToSceneGraph()) {
            if (pcProvider->isPartOfPhysicalObject()) {
                pcViewProviderRoot->addChild(root);
            }
            else {
                nonObjectGroup->addChild(root);
            }
        }
    }

    if (SoSeparator* fore = pcProvider->getFrontRoot()) {
        foregroundroot->addChild(fore);
    }

    if (SoSeparator* back = pcProvider->getBackRoot()) {
        backgroundroot->addChild(back);
    }

    pcProvider->setOverrideMode(this->getOverrideMode());
}

void View3DInventorViewer::removeViewProvider(ViewProvider* pcProvider)
{
    if (this->editViewProvider == pcProvider) {
        resetEditingViewProvider();
    }

    auto it = _ViewProviderSet.find(pcProvider);
    if(it == _ViewProviderSet.end())
        return;
    _ViewProviderSet.erase(it);

    SoSeparator* root = pcProvider->getRoot();

    if (root) {
        int index = nonObjectGroup->findChild(root);
        if (index >= 0) {
            nonObjectGroup->removeChild(index);
        }

        index = pcViewProviderRoot->findChild(root);
        if (index >= 0) {
            pcViewProviderRoot->removeChild(index);
        }
    }

    if (SoSeparator* fore = pcProvider->getFrontRoot()) {
        foregroundroot->removeChild(fore);
    }

    if (SoSeparator* back = pcProvider->getBackRoot()) {
        backgroundroot->removeChild(back);
    }
}

void View3DInventorViewer::toggleViewProvider(ViewProvider *vp) {
    SoSeparator* root = vp->getRoot();
    if (!root || !guiDocument)
        return;
    if (!_ViewProviderSet.count(vp))
        addViewProvider(vp);
    else if (guiDocument->isClaimed3D(vp) || !vp->canAddToSceneGraph()) {
        removeViewProvider(vp);
    }
}

void View3DInventorViewer::appendDetailPath(SoPath *path, ViewProvider *vp)
{
    if (_ViewProviderSet.count(vp)
            && !guiDocument->isClaimed3D(vp))
    {
        path->append(pcViewProviderRoot);
        if (!vp->isPartOfPhysicalObject())
            path->append(nonObjectGroup);
    }
}

SoFCRenderCacheManager *View3DInventorViewer::getRenderCacheManager() const
{
    return selectionRoot ? selectionRoot->getRenderManager() : nullptr;
}

void View3DInventorViewer::setEditingTransform(const Base::Matrix4D &mat)
{
    // NOLINTBEGIN
    if (pcEditingTransform) {
        double dMtrx[16];
        mat.getGLMatrix(dMtrx);
        pcEditingTransform->setMatrix(SbMatrix(
                    dMtrx[0], dMtrx[1], dMtrx[2],  dMtrx[3],
                    dMtrx[4], dMtrx[5], dMtrx[6],  dMtrx[7],
                    dMtrx[8], dMtrx[9], dMtrx[10], dMtrx[11],
                    dMtrx[12],dMtrx[13],dMtrx[14], dMtrx[15]));
    }
    // NOLINTEND
}

void View3DInventorViewer::setupEditingRoot(SoNode *node, const Base::Matrix4D *mat) {
    if(!editViewProvider) {
        return;
    }

    resetEditingRoot(false);
    if(mat) {
        setEditingTransform(*mat);
    }
    else {
        setEditingTransform(getDocument()->getEditingTransform());
    }
    if(node) {
        restoreEditingRoot = false;
        pcEditingRoot->addChild(node);
        return;
    }

    restoreEditingRoot = true;
    auto root = editViewProvider->getRoot();
    for(int i=0,count=root->getNumChildren();i<count;++i) {
        SoNode *node = root->getChild(i);
        if(node != editViewProvider->getTransformNode()) {
            pcEditingRoot->addChild(node);
        }
    }
    coinRemoveAllChildren(root);
    ViewProviderLink::updateLinks(editViewProvider);
}

void View3DInventorViewer::resetEditingRoot(bool updateLinks)
{
    if(!editViewProvider || pcEditingRoot->getNumChildren()<=1) {
        return;
    }
    if(!restoreEditingRoot) {
        pcEditingRoot->getChildren()->truncate(1);
        return;
    }
    restoreEditingRoot = false;
    auto root = editViewProvider->getRoot();
    if (root->getNumChildren()) {
        FC_ERR("WARNING!!! Editing view provider root node is tampered");
    }
    root->addChild(editViewProvider->getTransformNode());
    for (int i=1,count=pcEditingRoot->getNumChildren();i<count;++i) {
        root->addChild(pcEditingRoot->getChild(i));
    }
    pcEditingRoot->getChildren()->truncate(1);

    // handle exceptions eventually raised by ViewProviderLink
    try {
        if (updateLinks) {
            ViewProviderLink::updateLinks(editViewProvider);
        }
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
            Py::Object py = Py::type(e);
            if (py.isString()) {
                Py::String str(py);
                Base::Console().Warning("%s\n", str.as_std_string("utf-8").c_str());
            }
            else {
                Py::String str(py.repr());
                Base::Console().Warning("%s\n", str.as_std_string("utf-8").c_str());
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
        if (!path) {
            return nullptr;
        }
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
    if(vp == editViewProvider && pcEditingRoot->getNumChildren() > 1) {
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
        if (!path) {
            return nullptr;
        }
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

void View3DInventorViewer::setEditingViewProvider(Gui::ViewProvider* vp, int ModNum)
{
    this->editViewProvider = vp;
    this->editViewProvider->setEditViewer(this, ModNum);
    this->navigation->findBoundingSphere();
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
        if (heaction && heaction->getGrabber()) {
            heaction->releaseGrabber();
        }

        resetEditingRoot();

        this->editViewProvider->unsetEditViewer(this);
        removeEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,this->editViewProvider);
        this->editViewProvider = nullptr;
    }
}

/// reset from edit mode
bool View3DInventorViewer::isEditingViewProvider() const
{
    return this->editViewProvider != nullptr;
}

/// display override mode
void View3DInventorViewer::setOverrideMode(const std::string& mode)
{
    if (mode == overrideMode) {
        return;
    }

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
        // Coin's HIDDEN_LINE opens with clearBuffers(TRUE, TRUE) -- a
        // colour+depth clear of the whole framebuffer, taken before it
        // fills the scene depth-only. On the plain path that is exactly
        // right. On the composited path it runs *after* the backend has
        // rendered its frame into the same buffer, ignores the
        // clearwindow/clearzbuffer arguments it was passed, and throws
        // that frame away; and since the backend owns the geometry,
        // SoFCRenderer emits none to put back. The window is left
        // showing the clear colour.
        //
        // The backend implements this style itself -- faces filled in
        // the background colour to occlude, then the triangle edges as
        // geometry (BGFXView::submitTessellation) -- so it wants the
        // plain mode and no second opinion from Coin.
        this->getSoRenderManager()->setRenderMode(
                _pimpl->renderer ? SoRenderManager::AS_IS
                                 : SoRenderManager::HIDDEN_LINE);
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
    if (!view) {
        return;
    }

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
        owner->inventorSelection->getRootPath()->truncate(1);
    }
}

/// Materialize the shadow map's and the ground receiver's settings as
/// RenderShadow_* view properties, and return the handful a Coin shadow
/// group still needs. One function because there is one set: the
/// backend reads every one of them through the bridge, which only
/// *reads* -- `_shadowRenderParam` is what brings a property into being
/// -- and the properties have to exist whether or not any draw style
/// ever asks for them (docs/CoinRetirement.md stage 4d).
///
/// The defaults are still the ViewParams `Shadow*` preferences. Moving
/// those into RenderParams is a stage of its own, after the draw style
/// itself goes; nothing about the per-view surface depends on it.
Gui::ShadowRenderParams Gui::materializeShadowRenderParams(App::PropertyContainer *view)
{
    ShadowRenderParams res;
    if (!view)
        return res;

    // --- the shadow map itself ---
    static const App::PropertyFloatConstraint::Constraints _precision_cstr(0.0,1.0,0.1);
    res.precision = _shadowRenderParam<App::PropertyFloatConstraint>(view, "Precision",
            ViewParams::docShadowPrecision(), ViewParams::getShadowPrecision(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_precision_cstr);
            });
    // The variance shadow map needs a small non-zero epsilon or its
    // Chebyshev bound is numerically unstable (docs/ShaderDesign.md);
    // ShadowEpsilonMinimum is the enforced lower bound, and a value that
    // predates the constraint is clamped to it.
    static App::PropertyPrecision::Constraints _epsilon_cstr(1e-6,1000.0,1e-5);
    _epsilon_cstr.LowerBound = ViewParams::getShadowEpsilonMinimum();
    double epsilonDef = ViewParams::getShadowEpsilon();
    if (epsilonDef < _epsilon_cstr.LowerBound)
        epsilonDef = _epsilon_cstr.LowerBound;
    res.epsilon = _shadowRenderParam<App::PropertyPrecision>(view, "Epsilon",
            ViewParams::docShadowEpsilon(), epsilonDef,
            [](App::PropertyFloatConstraint &prop) {
                if(prop.getConstraints() != &_epsilon_cstr)
                    prop.setConstraints(&_epsilon_cstr);
            });
    if (res.epsilon < _epsilon_cstr.LowerBound)
        res.epsilon = _epsilon_cstr.LowerBound;
    static const App::PropertyFloatConstraint::Constraints _threshold_cstr(0.0,1.0,0.1);
    res.threshold = _shadowRenderParam<App::PropertyFloatConstraint>(view, "Threshold",
            ViewParams::docShadowThreshold(), ViewParams::getShadowThreshold(),
            [](App::PropertyFloatConstraint &prop) {
                if(prop.getConstraints() != &_threshold_cstr)
                    prop.setConstraints(&_threshold_cstr);
            });
    static const App::PropertyIntegerConstraint::Constraints _smooth_cstr(0,100,1);
    res.smoothBorder = _shadowRenderParam<App::PropertyIntegerConstraint>(view, "SmoothBorder",
            ViewParams::docShadowSmoothBorder(), ViewParams::getShadowSmoothBorder(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_smooth_cstr)
                    prop.setConstraints(&_smooth_cstr);
            });
    static const App::PropertyIntegerConstraint::Constraints _spread_cstr(0,1000000,500);
    res.spreadSize = _shadowRenderParam<App::PropertyIntegerConstraint>(view, "SpreadSize",
            ViewParams::docShadowSpreadSize(), ViewParams::getShadowSpreadSize(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_spread_cstr)
                    prop.setConstraints(&_spread_cstr);
            });
    static const App::PropertyIntegerConstraint::Constraints _sample_cstr(0,7,1);
    res.spreadSampleSize = _shadowRenderParam<App::PropertyIntegerConstraint>(view, "SpreadSampleSize",
            ViewParams::docShadowSpreadSampleSize(), ViewParams::getShadowSpreadSampleSize(),
            [](App::PropertyIntegerConstraint &prop) {
                if(prop.getConstraints() != &_sample_cstr)
                    prop.setConstraints(&_sample_cstr);
            });

    // --- the ground receiver ---
    _shadowRenderParam<App::PropertyBool>(view, "ShowGround",
            ViewParams::docShadowShowGround(),
            ViewParams::getShadowShowGround());
    _shadowRenderParam<App::PropertyColor>(view, "GroundColor",
            ViewParams::docShadowGroundColor(),
            App::Color((uint32_t)ViewParams::getShadowGroundColor()));
    static const App::PropertyFloatConstraint::Constraints _transp_cstr(0.0,1.0,0.1);
    _shadowRenderParam<App::PropertyFloatConstraint>(view, "GroundTransparency",
            ViewParams::docShadowGroundTransparency(),
            ViewParams::getShadowGroundTransparency(),
            [](App::PropertyFloatConstraint &prop) {
                if(!prop.getConstraints())
                    prop.setConstraints(&_transp_cstr);
            });
    _shadowRenderParam<App::PropertyBool>(view, "GroundBackFaceCull",
            ViewParams::docShadowGroundBackFaceCull(),
            ViewParams::getShadowGroundBackFaceCull());
    _shadowRenderParam<App::PropertyBool>(view, "GroundShading",
            ViewParams::docShadowGroundShading(),
            ViewParams::getShadowGroundShading());
    _shadowRenderParam<App::PropertyFileIncluded>(view, "GroundTexture",
            ViewParams::docShadowGroundTexture(),
            ViewParams::getShadowGroundTexture().c_str());
    _shadowRenderParam<App::PropertyFileIncluded>(view, "GroundBumpMap",
            ViewParams::docShadowGroundBumpMap(),
            ViewParams::getShadowGroundBumpMap().c_str());
    static const App::PropertyQuantityConstraint::Constraints _texture_cstr = {0,DBL_MAX,10.0};
    _shadowRenderParam<App::PropertyLength>(view, "GroundTextureSize",
            ViewParams::docShadowGroundTextureSize(),
            ViewParams::getShadowGroundTextureSize(),
            [](App::PropertyLength &prop) {
                if(prop.getConstraints() != &_texture_cstr)
                    prop.setConstraints(&_texture_cstr);
            });
    _shadowRenderParam<App::PropertyBool>(view, "GroundSizeAuto",
            "Auto adjust ground size based on the scene bounding box", true);
    _shadowRenderParam<App::PropertyFloat>(view, "GroundSizeScale",
            ViewParams::docShadowGroundScale(),
            ViewParams::getShadowGroundScale());
    _shadowRenderParam<App::PropertyLength>(view, "GroundSizeX", "", 100.0);
    _shadowRenderParam<App::PropertyLength>(view, "GroundSizeY", "", 100.0);
    _shadowRenderParam<App::PropertyBool>(view, "GroundAutoPosition",
            "Auto place the ground face at the Z bottom of the scene", true);
    _shadowRenderParam<App::PropertyPlacement>(view, "GroundPlacement",
            "Ground placement. If 'GroundAutoPosition' is on, this specifies an additional offset of the ground",
            Base::Placement());
    return res;
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

    // The scene light is the renderer's (stage 4a), and stage 4d made
    // this style read the same properties rather than a family of its
    // own: what it builds here is a Coin light node standing in for the
    // one the backend resolves from Render_Light*.
    bool spotlight = _renderParam<App::PropertyBool>(view, "LightSpot",
            RenderParams::docLightSpot(), RenderParams::getLightSpot());

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

        // The ground receiver is the render backend's
        // (Render::LightConfig, docs/CoinRetirement.md stage 4b). What
        // stood here was the Coin quad -- an SoFaceSet under its own
        // light model, shape hints, two texture coordinate nodes and an
        // SoPolygonOffset that existed only to match the one
        // PartGui::ViewProviderPartExt puts on nearly every shape.

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
    // The map's own settings, and the ground receiver's beside them:
    // one family, materialized in one place, read here and by the
    // backend's bridge alike.
    const auto shadowParams = Gui::materializeShadowRenderParams(view);
    pcShadowGroup->precision = shadowParams.precision;
    pcShadowGroup->epsilon = shadowParams.epsilon;
    pcShadowGroup->threshold = shadowParams.threshold;

    SoLight *light;
    auto _dir = _renderParam<App::PropertyVector>(
            view, "LightDirection", RenderParams::docLight(),
            Base::Vector3d(RenderParams::getLightDirectionX(),
                            RenderParams::getLightDirectionY(),
                            RenderParams::getLightDirectionZ()));
    _dir.Normalize();
    SbVec3f dir(_dir.x,_dir.y,_dir.z);

    SbBox3f bbox;
    owner->getSceneBoundBox(bbox);

    if(spotlight) {
        light = pcShadowSpotLight;
        pcShadowSpotLight->direction = dir;
        // Render_LightPosition, like the backend's spot (which is the
        // one that outlives this): a plain property, where the style's
        // own SpotLightPosition used to default to a point above the
        // scene bounding box.
        auto pos = _renderParam<App::PropertyVector>(
                view, "LightPosition", RenderParams::docLightSpot(),
                Base::Vector3d(RenderParams::getLightPositionX(),
                               RenderParams::getLightPositionY(),
                               RenderParams::getLightPositionZ()));
        pcShadowSpotLight->location = SbVec3f(pos.x,pos.y,pos.z);
        pcShadowSpotLight->dropOffRate = _renderParam<App::PropertyFloat>(
                view, "LightDropOffRate", RenderParams::docLightDropOffRate(),
                RenderParams::getLightDropOffRate());
        // Degrees in the property, radians in Coin -- the convention the
        // bridge states for the same property.
        pcShadowSpotLight->cutOffAngle = M_PI * _renderParam<App::PropertyFloat>(
                view, "LightCutOffAngle", RenderParams::docLightCutOffAngle(),
                RenderParams::getLightCutOffAngle()) / 180.0;
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

    light->intensity = _renderParam<App::PropertyFloat>(view, "LightIntensity",
            RenderParams::docLightIntensity(), RenderParams::getLightIntensity());

    App::Color color = _renderParam<App::PropertyColor>(view, "LightColor",
            RenderParams::docLightColor(),
            App::Color(uint32_t(RenderParams::getLightColor())));
    SbColor sbColor;
    float f;
    sbColor.setPackedValue(color.getPackedValue(),f);
    light->color = sbColor;

    if(isValidBBox(bbox))
        updateShadowExtent(bbox);

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

void View3DInventorViewer::setViewportCB(void* ud, SoAction* action)
{
    Q_UNUSED(ud)
    // Make sure to override the value set inside SoOffscreenRenderer::render()
    if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
        SoFCOffscreenRenderer& renderer = SoFCOffscreenRenderer::instance();
        const SbViewportRegion& vp = renderer.getViewportRegion();
        SoViewportRegionElement::set(action->getState(), vp);
        static_cast<SoGLRenderAction*>(action)->setViewportRegion(vp);  // NOLINT
    }
}

void View3DInventorViewer::clearBufferCB(void* ud, SoAction* action)
{
    Q_UNUSED(ud)
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

void View3DInventorViewer::handleEventCB(void* userdata, SoEventCallback* n)
{
    auto that = static_cast<View3DInventorViewer*>(userdata);
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

    if (mode < 0) {
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
            if (pcViewProviderRoot && setting != 1) {
                pcViewProviderRoot->renderCaching = SoSeparator::ON;
            }
            mode = 2;
        }
        else {
            if (pcViewProviderRoot) {
                pcViewProviderRoot->renderCaching = SoSeparator::AUTO;
            }
            mode = setting;
        }
    }

    if (canAutoCache < 0) {
        const char *env = coin_getenv("COIN_AUTO_CACHING");
        canAutoCache = env ? atoi(env) : 1;
    }

    // If coin auto cache is disabled, do not use 'Auto' render cache mode, but
    // fallback to 'Distributed' mode.
    if (!canAutoCache && mode != 2 && mode != 3) {
        mode = 1;
    }

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

void View3DInventorViewer::setNaviCubeCorner(int cc)
{
    if (naviCube) {
        naviCube->setCorner(static_cast<NaviCube::Corner>(cc));
    }
}

NaviCube* View3DInventorViewer::getNaviCube() const
{
    return naviCube;
}

void View3DInventorViewer::setAxisCross(bool on)
{
    SoNode* scene = getSoRenderManager()->getSceneGraph();
    auto sep = static_cast<SoSeparator*>(scene); // NOLINT

    if (on) {
        if (!axisGroup) {
            axisCross = new Gui::SoShapeScale;
            auto axisKit = new Gui::SoAxisCrossKit();
            axisKit->set("xAxis.appearance.drawStyle", "lineWidth 2");
            axisKit->set("yAxis.appearance.drawStyle", "lineWidth 2");
            axisKit->set("zAxis.appearance.drawStyle", "lineWidth 2");
            axisCross->setPart("shape", axisKit);
            axisCross->scaleFactor = 1.0F;
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

void View3DInventorViewer::showRotationCenter(bool show)
{
    SoNode* scene = getSceneGraph();
    if (!scene) {
        return;
    }

    auto sep = static_cast<SoSeparator*>(scene);  // NOLINT

    bool showEnabled = App::GetApplication()
                           .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
                           ->GetBool("ShowRotationCenter", true);

    if (show && showEnabled) {
        SbBool found{};
        SbVec3f center = navigation->getRotationCenter(found);

        if (!found) {
            return;
        }

        if (!rotationCenterGroup) {
            float size = App::GetApplication()
                             .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
                             ->GetFloat("RotationCenterSize", 5.0);  // NOLINT

            unsigned long rotationCenterColor =
                App::GetApplication()
                    .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
                    ->GetUnsigned("RotationCenterColor", 4278190131);  // NOLINT

            QColor color = App::Color::fromPackedRGBA<QColor>(rotationCenterColor);

            rotationCenterGroup = new SoSkipBoundingGroup();

            auto sphere = new SoSphere();

            // There needs to be a non-transparent object to ensure the transparent sphere works when opening an new empty document
            auto hidden = new SoSeparator();
            auto hiddenScale = new SoScale();
            hiddenScale->scaleFactor = SbVec3f(0, 0, 0);
            hidden->addChild(hiddenScale);
            hidden->addChild(sphere);

            auto complexity = new SoComplexity();
            complexity->value = 1;

            auto material = new SoMaterial();
            // Base color as well as emissive: with the flat (BASE_COLOR)
            // light model below the render-cache backend takes the diffuse
            // color as the unlit fill.
            material->diffuseColor = SbColor(float(color.redF()),
                                             float(color.greenF()),
                                             float(color.blueF()));
            material->emissiveColor = SbColor(float(color.redF()),
                                              float(color.greenF()),
                                              float(color.blueF()));
            material->transparency = 1.0F - float(color.alphaF());

            // The rotation-center sphere is a navigation gizmo, not scene
            // geometry: render it flat (unlit) and out of the shadow pass so
            // the render-cache backend never shades, shadows or tints it.
            // Drawn on-top (SoAnnotation) it already renders after the
            // screen-space effects (SSAO / volumetric / water / caustics);
            // this keeps the lighting and shadow passes off it too, so no
            // special effect touches the transparent sphere.
            auto lightModel = new SoLightModel();
            lightModel->model = SoLightModel::BASE_COLOR;

            auto shadowStyle = new SoShadowStyle();
            shadowStyle->style = SoShadowStyle::NO_SHADOWING;

            auto translation = new SoTranslation();
            translation->setName("translation");
            translation->translation.setValue(center);

            auto annotation = new SoAnnotation();
            annotation->addChild(complexity);
            annotation->addChild(lightModel);
            annotation->addChild(shadowStyle);
            annotation->addChild(material);
            annotation->addChild(sphere);

            auto scaledSphere = new SoShapeScale();
            scaledSphere->setPart("shape", annotation);
            scaledSphere->scaleFactor = size;

            rotationCenterGroup->addChild(translation);
            rotationCenterGroup->addChild(hidden);
            rotationCenterGroup->addChild(scaledSphere);

            sep->addChild(rotationCenterGroup);
        }
    }
    else {
        if (rotationCenterGroup) {
            sep->removeChild(rotationCenterGroup);
            rotationCenterGroup = nullptr;
        }
    }
}

// Changes the position of the rotation center indicator
void View3DInventorViewer::changeRotationCenterPosition(const SbVec3f& newCenter) {
    if (!rotationCenterGroup) {
        return;
    }

    SoTranslation* translation = dynamic_cast<SoTranslation*>(rotationCenterGroup->getByName("translation"));
    if (!translation) {
        return;
    }

    translation->translation = newCenter;
}

void View3DInventorViewer::setNavigationType(Base::Type type)
{
    if (this->navigation && this->navigation->getTypeId() == type) {
        return; // nothing to do
    }

    Base::Type navtype = Base::Type::getTypeIfDerivedFrom(type.getName(), NavigationStyle::getClassTypeId());
    auto ns = static_cast<NavigationStyle*>(navtype.createInstance());
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

void View3DInventorViewer::setBacklightEnabled(bool on)
{
    this->backlight->on = on;
}

bool View3DInventorViewer::isBacklightEnabled() const
{
    return this->backlight->on.getValue();
}

SoDirectionalLight* View3DInventorViewer::getFillLight() const
{
    return this->fillLight;
}

void View3DInventorViewer::setFillLightEnabled(bool on)
{
    this->fillLight->on = on;
}

bool View3DInventorViewer::isFillLightEnabled() const
{
    return this->fillLight->on.getValue();
}

SoEnvironment* View3DInventorViewer::getEnvironment() const
{
    return this->environment;
}

void View3DInventorViewer::setSceneGraph(SoNode* root)
{
    inherited::setSceneGraph(root);
    if (!root) {
        _ViewProviderSet.clear();
        editViewProvider = nullptr;
        return;
    }

    SoSearchAction sa;
    sa.setNode(this->backlight);
    //we want the rendered scene with all lights and cameras, viewer->getSceneGraph would return
    //the geometry scene only
    SoNode* scene = this->getSoRenderManager()->getSceneGraph();
    if (scene && scene->getTypeId().isDerivedFrom(SoSeparator::getClassTypeId())) {
        auto sceneroot = static_cast<SoSeparator*>(scene);
        sa.apply(scene);
        if (!sa.getPath()) {
            sceneroot->insertChild(this->backlight, 0);
        }
        // The fill light and the ambient environment go *after* the camera,
        // in world space, but still above the render-cache root so the
        // renderer backends see them too. The backlight and the headlight
        // sit before the camera, which is what makes them eye-fixed.
        sa.reset();
        sa.setNode(this->viewerLightingRoot);
        sa.apply(scene);
        if (!sa.getPath()) {
            // Anchored on the camera rather than on a fixed position: the
            // scene root itself is swapped out by index when shadows are
            // activated, so it is not something to count from.
            int index = sceneroot->findChild(getSoRenderManager()->getCamera());
            if (index >= 0) {
                ++index;
            }
            else {
                index = sceneroot->findChild(root);
            }
            sceneroot->insertChild(this->viewerLightingRoot,
                                   index < 0 ? sceneroot->getNumChildren() : index);
        }
    }

    _pimpl->addRendererBoundsNode();

    syncLightRotation();

    navigation->findBoundingSphere();
}

void View3DInventorViewer::savePicture(int width, int height, int sample, const QColor& bg, QImage& img) const
{
    // An external render backend draws the scene from its own feeds into
    // its own targets; the Coin scene graph it was fed from renders to
    // nothing. Every route below would therefore return the frame with the
    // model missing, so the capture goes through the backend's own one-shot
    // dump instead (docs/RenderDebug.md §4) -- which is the composed frame,
    // overlays and background included.
    if (getExternalRenderer()) {
        auto self = const_cast<View3DInventorViewer*>(this);  // NOLINT
        if (self->imageFromRenderer(width, height, bg, img))
            return;
        Base::Console().Warning("Render backend frame capture failed; "
                                "falling back to the plain GL capture\n");
    }

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
        auto self = const_cast<View3DInventorViewer*>(this);  // NOLINT
        self->imageFromFramebuffer(width, height, sample, bg, img);
        return;
    }

    if (useGrabFramebuffer) {
        auto self = const_cast<View3DInventorViewer*>(this);  // NOLINT
        img = self->grabFramebuffer();
        img = img.mirrored();
        img = img.scaledToWidth(width);
        return;
    }

    // if no valid color use the current background
    bool useBackground = false;
    SbViewportRegion vp(getSoRenderManager()->getViewportRegion());

    if (width > 0 && height > 0) {
        vp.setWindowSize(short(width), short(height));
    }

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

    // The same rig as the on-screen scene root, in the same order: the
    // eye-fixed lights before the camera, the world-space ones after it.
    root->addChild(getHeadlight());
    root->addChild(getBacklight());
    root->addChild(camera);
    root->addChild(viewerLightingRoot);
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
            renderer.setNumPasses(sample);
            renderer.setInternalTextureFormat(getInternalTextureFormat());
            if (bgColor.isValid()) {
                renderer.setBackgroundColor(SbColor4f(float(bgColor.redF()),
                                                      float(bgColor.greenF()),
                                                      float(bgColor.blueF()),
                                                      float(bgColor.alphaF())));
            }
            if (!renderer.render(root)) {
                THROWM(Base::RuntimeError, "Offscreen rendering failed")
            }

            renderer.writeToImage(img);
            root->unref();
        }
        else {
            SoFCOffscreenRenderer& renderer = SoFCOffscreenRenderer::instance();
            renderer.setViewportRegion(vp);
            renderer.getGLRenderAction()->setSmoothing(true);
            renderer.getGLRenderAction()->setNumPasses(sample);
            renderer.getGLRenderAction()->setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);
            if (bgColor.isValid()) {
                renderer.setBackgroundColor(SbColor(float(bgColor.redF()),
                                                    float(bgColor.greenF()),
                                                    float(bgColor.blueF())));
            }
            if (!renderer.render(root)) {
                THROWM(Base::RuntimeError, "Offscreen rendering failed")
            }

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
    if (bgcolor.isValid()) {
        va->setBackgroundColor(true, SbColor(float(bgcolor.redF()),
                                             float(bgcolor.greenF()),
                                             float(bgcolor.blueF())));
    }

    const float border = 10.0F;
    SbVec2s vpsize = this->getSoRenderManager()->getViewportRegion().getViewportSizePixels();
    float vpratio = ((float)vpsize[0]) / ((float)vpsize[1]);

    if (vpratio > 1.0F) {
        va->setOrientation(SoVectorizeAction::LANDSCAPE);
        vpratio = 1.0F / vpratio;
    }
    else {
        va->setOrientation(SoVectorizeAction::PORTRAIT);
    }

    va->beginStandardPage(SoVectorizeAction::PageSize(pagesize), border);

    // try to fill as much "paper" as possible
    SbVec2f size = va->getPageSize();

    float pageratio = size[0] / size[1];
    float xsize{};
    float ysize{};

    if (pageratio < vpratio) {
        xsize = size[0];
        ysize = xsize / vpratio;
    }
    else {
        ysize = size[1];
        xsize = ysize * vpratio;
    }

    float offx = border + (size[0]-xsize) * 0.5F;  // NOLINT
    float offy = border + (size[1]-ysize) * 0.5F;  // NOLINT

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

void View3DInventorViewer::setSelectionEnabled(bool enable)
{
    selectionRoot->selectionRole.setValue(enable);
}

bool View3DInventorViewer::isSelectionEnabled() const
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
    SbMatrix mat = gma.getMatrix().transpose();
    mat.multMatrixVec(imageCoords, imageCoords);

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
    int width = getGLWidget()->width();
    int height = getGLWidget()->height();

    if (width >= height) {
        // "Landscape" orientation, to square
        imageCoords[0] *= height;
        imageCoords[0] += (width-height) / 2.0;  // NOLINT
        imageCoords[1] *= height;

    }
    else {
        // "Portrait" orientation
        imageCoords[0] *= width;
        imageCoords[1] *= width;
        imageCoords[1] += (height-width) / 2.0;  // NOLINT
    }

    return {imageCoords[0], imageCoords[1]};
}

std::vector<SbVec2f> View3DInventorViewer::getGLPolygon(const std::vector<SbVec2s>& pnts) const
{
    const SbViewportRegion &vp = this->getSoRenderManager()->getViewportRegion();
    const SbVec2s &sp = vp.getViewportSizePixels();
    const SbVec2s &op = vp.getViewportOriginPixels();
    const SbVec2f &vpSize = vp.getViewportSize();
    float dX{};
    float dY{};
    vpSize.getValue(dX, dY);
    float fRatio = vp.getViewportAspectRatio();

    std::vector<SbVec2f> poly;
    for (const auto & pnt : pnts) {
        SbVec2s loc = pnt - op;
        SbVec2f pos((float)loc[0] / (float)sp[0], (float)loc[1] / (float)sp[1]);
        float pX{};
        float pY{};
        pos.getValue(pX, pY);

        // now calculate the real points respecting aspect ratio information
        //
        if (fRatio > 1.0F) {
            pX = (pX - 0.5F * dX) * fRatio + 0.5F * dX;  // NOLINT
            pos.setValue(pX, pY);
        }
        else if (fRatio < 1.0F) {
            pY = (pY - 0.5F * dY) / fRatio + 0.5F * dY;  // NOLINT
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

    if (fi.hasExtension({"idtf", "svg"})) {
        int ps = 4;
        QColor col = Qt::white;
        std::unique_ptr<SoVectorizeAction> vo;

        if (fi.hasExtension("svg")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeSVGAction());
        }
        else if (fi.hasExtension("idtf")) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoFCVectorizeU3DAction());
        }
        else if (fi.hasExtension({"ps", "eps"})) {
            vo = std::unique_ptr<SoVectorizeAction>(new SoVectorizePSAction());
        }
        else {
            THROWM(Base::ValueError, "Not supported vector graphic")
        }

        SoVectorOutput* out = vo->getOutput();
        if (!out || !out->openFile(filename)) {
            std::ostringstream a_out;
            a_out << "Cannot open file '" << filename << "'";
            THROWM(Base::FileSystemError, a_out.str())
        }

        saveGraphic(ps, col, vo.get());
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
void View3DInventorViewer::interactionStartCB(void* ud, SoQTQuarterAdaptor* viewer)
{
    Q_UNUSED(ud)
    SoGLRenderAction* glra = viewer->getSoRenderManager()->getGLRenderAction();
    SoFCInteractiveElement::set(glra->getState(), viewer->getSceneGraph(), true);
}

/**
 * Sets the SoFCInteractiveElement to \a false and forces a redraw.
 */
void View3DInventorViewer::interactionFinishCB(void* ud, SoQTQuarterAdaptor* viewer)
{
    Q_UNUSED(ud)
    SoGLRenderAction* glra = viewer->getSoRenderManager()->getGLRenderAction();
    SoFCInteractiveElement::set(glra->getState(), viewer->getSceneGraph(), false);
    viewer->redraw();
}

/**
 * Logs the type of the action that traverses the Inventor tree.
 */
void View3DInventorViewer::interactionLoggerCB(void* ud, SoAction* action)
{
    Q_UNUSED(ud)
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
    for (auto it : this->graphicsItems) {
        if (it->isDerivedFrom(type)) {
            items.push_back(it);
        }
    }

    return items;
}

void View3DInventorViewer::clearGraphicsItems()
{
    this->graphicsItems.clear();
}

int View3DInventorViewer::getNumSamples()
{
    long samples = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/View")->GetInt("AntiAliasing", 0);

    // NOLINTBEGIN
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
    // NOLINTEND
}

GLenum View3DInventorViewer::getInternalTextureFormat()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath
        ("User parameter:BaseApp/Preferences/View");
    std::string format = hGrp->GetASCII("InternalTextureFormat", "Default");

    // NOLINTBEGIN
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
    // NOLINTEND
}

void View3DInventorViewer::setRenderType(RenderType type)
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

            auto gl = static_cast<QtGLWidget*>(this->viewport());  // NOLINT
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
    auto gl = static_cast<QtGLWidget*>(this->viewport());  // NOLINT
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
    auto gl = static_cast<QtGLWidget*>(this->viewport());  // NOLINT
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

bool View3DInventorViewer::pumpFrameDump(Render::Renderer *renderer)
{
    if (!renderer)
        return false;
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        // Processing events can run scene/view scripts that destroy and
        // recreate the external renderer (e.g. a renderer-type or MSAA
        // parameter change) — re-validate the pointer every iteration
        // instead of touching a potentially dangling one.
        Render::Renderer *current = getExternalRenderer();
        if (!current || current != renderer)
            return false;
        if (!renderer->frameDumpPending())
            return true;
        if (timer.elapsed() >= 5000)
            return false;
        if (auto rm = getSoRenderManager())
            rm->scheduleRedraw();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

bool View3DInventorViewer::imageFromRenderer(int width, int height,
                                             const QColor& bgcolor, QImage& img)
{
    Render::Renderer *renderer = getExternalRenderer();
    if (!renderer)
        return false;

    // Raw PPM: the dump writes it without an encoder and Qt reads it back
    // directly, so a screenshot does not pay for a PNG round trip.
    QTemporaryFile tmp(QDir::temp().filePath(
                QStringLiteral("FreeCAD-capture-XXXXXX.ppm")));
    tmp.setAutoRemove(true);
    if (!tmp.open())
        return false;
    const QString path = tmp.fileName();
    tmp.close();

    // The backend paints the background itself, from what the viewer hands
    // it each frame — so an asked-for background is set for the captured
    // frame the same way the offscreen path sets it. It is painted opaque:
    // a capture asking for a transparent background gets a solid one, the
    // frame having been composed before it is read back.
    const QColor col = backgroundColor();
    auto grad = getGradientBackground();
    if (bgcolor.isValid()) {
        setBackgroundColor(bgcolor);
        setGradientBackground(Background::NoGradient);
    }

    Render::FrameDumpRequest req;
    req.path = path.toUtf8().constData();
    // An exported image is of the model: no navigation cube, no corner
    // axis cross, no on-screen text — which is what the Coin route this
    // stands in for produced.
    req.overlays = false;
    bool ok = renderer->requestFrameDump(req) && pumpFrameDump(renderer);

    if (bgcolor.isValid()) {
        setBackgroundColor(col);
        setGradientBackground(grad);
    }
    // The captured frame went to the screen too, without the chrome the
    // capture left out and with the capture's background: put the view back.
    if (auto rm = getSoRenderManager())
        rm->scheduleRedraw();
    if (!ok)
        return false;

    QImage captured(path);
    if (captured.isNull())
        return false;

    // The backend renders at the size of the view it belongs to. A capture
    // asked for at another size is scaled to it -- the frame itself cannot
    // be re-staged at an arbitrary resolution from here.
    if (width > 0 && height > 0
            && (captured.width() != width || captured.height() != height)) {
        captured = captured.scaled(width, height, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
    }
    img = captured;
    return true;
}

void View3DInventorViewer::renderToFramebuffer(QtGLFramebufferObject* fbo)
{
    static_cast<QtGLWidget*>(this->viewport())->makeCurrent();  // NOLINT
    fbo->bind();
    int width = fbo->size().width();
    int height = fbo->size().height();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);

    const QColor col = this->backgroundColor();
    glViewport(0, 0, width, height);

    // With a backend active the geometry is its to draw: the Coin
    // traversal below skips it (SoFCRenderer::render returns early while
    // canSkipInternal()), so without this an offscreen capture is a
    // blank image. Same call as the on-screen frame, only rendered at
    // the capture's size and into the framebuffer bound above.
    bool externalRendered = false;
    if (SoCamera* cam = _pimpl->renderer ? getSoRenderManager()->getCamera()
                                         : nullptr) {
        SbMatrix viewMat, projMat;
        SbViewportRegion capvp {short(width), short(height)};
        SbViewVolume vol = cam->getViewVolume(capvp.getViewportAspectRatio());
        vol.getMatrices(viewMat, projMat);
        _pimpl->renderer->setBackground(_pimpl->backgroundFeed(col));
        externalRendered = _pimpl->renderer->renderOffscreen(
                col, &viewMat.getValue(), &projMat.getValue(), width, height);
    }
    if (!externalRendered) {
        glClearColor(float(col.redF()), float(col.greenF()), float(col.blueF()),
                     float(col.alphaF()));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    SoBoxSelectionRenderAction gl(SbViewportRegion(width, height));
    // When creating a new GL render action we have to copy over the cache context id
    // For further details see init().
    uint32_t id = this->getSoRenderManager()->getGLRenderAction()->getCacheContext();
    gl.setCacheContext(id);
    gl.setTransparencyType(SoGLRenderAction::SORTED_OBJECT_SORTED_TRIANGLE_BLEND);

    // The backend's output already carries the background, and repainting
    // the gradient would erase it — the same suppression the on-screen
    // frame does, for the same reason.
    pcBackGround->setSuppressed(externalRendered);
    gl.apply(this->backgroundroot);
    pcBackGround->setSuppressed(false);
    // The render action of the render manager has set the depth function to GL_LESS
    // while creating a new render action has it set to GL_LEQUAL. So, in order to get
    // the exact same result set it explicitly to GL_LESS.
    glDepthFunc(GL_LESS);
    SoDatumLabel::SuppressGLRender =
        externalRendered && _pimpl->editingBackendFed;
    SoFCRenderCacheManager::SuppressImageGLRender =
        SoDatumLabel::SuppressGLRender;
    gl.apply(this->getSoRenderManager()->getSceneGraph());
    SoDatumLabel::SuppressGLRender = false;
    SoFCRenderCacheManager::SuppressImageGLRender = false;

    // Foreground superimposition and the corner axis cross come from the
    // backend's overlay feeds on a backend frame, like on screen.
    if (!externalRendered) {
        gl.apply(this->foregroundroot);

        if (this->axiscrossEnabled) {
            this->drawAxisCross();
        }
    }

    fbo->release();
}

void View3DInventorViewer::actualRedraw()
{
    QElapsedTimer frameTimer;
    frameTimer.start();

    // An on-top entry whose Coin path went stale stops rendering and reports
    // itself here. Re-resolving runs an SoAction, so it must not happen inside
    // the frame: hand it to the event loop.
    if (selectionRoot) {
        if (auto manager = selectionRoot->getRenderManager()) {
            std::vector<std::string> invalid;
            if (manager->takeInvalidSelections(invalid))
                QTimer::singleShot(0, this, [this]() { refreshGroupOnTop(); });
        }
    }

    // The stage timers live deep in the publish pipeline, which knows
    // nothing of views; the drawing view applies the global switches
    // here. Global on purpose: these are measurement state, and their
    // old per-view copies saved inside documents shadowed the globals
    // (a saved Timing=false once blanked a whole measurement run).
    RenderTiming::setEnabled(RenderParams::getDebugTiming());
    ScenePublishDelta::setLogging(RenderParams::getDebugDelta());

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

    // What a frame costs is what the redraw throttle budgets against. A
    // sub-millisecond result means nothing was drawn (the Recomputing case
    // above), and feeding that in would tell the throttle the scene is cheap.
    const double ms = double(frameTimer.nsecsElapsed()) / 1e6;
    if (ms >= 1.0)
        _pimpl->noteFrameCost(ms);

    RenderTiming::frameDone();
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
        glTexCoord2f(0.0F, 0.0F);
        glVertex2f(-1.0, -1.0F);
        glTexCoord2f(1.0F, 0.0F);
        glVertex2f(1.0F, -1.0F);
        glTexCoord2f(1.0F, 1.0F);
        glVertex2f(1.0F, 1.0F);
        glTexCoord2f(0.0F, 1.0F);
        glVertex2f(-1.0F, 1.0F);
    glEnd();

    printDimension();
    navigation->redraw();

    for (auto it : this->graphicsItems) {
        it->paintGL();
    }

    if (naviCubeEnabled) {
        naviCube->drawNaviCube();
    }

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
    glOrtho(0, size[0], 0, size[1], 0, 100);  // NOLINT
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    glRasterPos2f(0,0);
    glDrawPixels(glImage.width(), glImage.height(), GL_BGRA,GL_UNSIGNED_BYTE, glImage.bits());

    printDimension();
    navigation->redraw();

    for (auto it : this->graphicsItems) {
        it->paintGL();
    }

    if (naviCubeEnabled) {
        naviCube->drawNaviCube();
    }

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

void View3DInventorViewer::Private::rendererBoundsCB(void *ud, SoAction *action)
{
    if (!action->isOfType(SoGetBoundingBoxAction::getClassTypeId()))
        return;
    auto self = static_cast<View3DInventorViewer::Private *>(ud);
    // The same report SoFCUnifiedSelection makes, from a place a cache
    // cannot answer for. Reaching both is a union of one box with itself.
    self->owner->onGetBoundingBox(static_cast<SoGetBoundingBoxAction *>(action));
}

void View3DInventorViewer::Private::addRendererBoundsNode()
{
    auto scene = owner->getSoRenderManager()->getSceneGraph();
    if (!scene || !scene->isOfType(SoSeparator::getClassTypeId()))
        return;
    auto super = static_cast<SoSeparator *>(scene);
    if (!rendererBoundsNode) {
        rendererBoundsNode = new SoCallback;
        rendererBoundsNode->setName("RendererBounds");
        rendererBoundsNode->setCallback(&Private::rendererBoundsCB, this);
    }
    // Appended, not inserted: activateShadow() swaps the scene root by
    // index, and a node at the front would shift every one of them.
    if (super->findChild(rendererBoundsNode) < 0)
        super->addChild(rendererBoundsNode);
}

bool View3DInventorViewer::hasExternalRenderer() const
{
    return _pimpl->renderer != nullptr;
}

Render::Renderer *View3DInventorViewer::getExternalRenderer() const
{
    return _pimpl->renderer.get();
}

bool View3DInventorViewer::applyRendererAntiAliasing()
{
    if (!_pimpl->renderer)
        return false;
    // The backend renders into its own offscreen target and resolves before
    // compositing, so the sample count is applied directly — no need to clone
    // the view to obtain a multisampled GL context (which would tear down and
    // recreate the backend). The change takes effect on the next frame.
    _pimpl->renderer->setMSAASamples(getNumSamples());
    if (auto rm = getSoRenderManager())
        rm->scheduleRedraw();
    return true;
}

void View3DInventorViewer::setRendererType(const std::string &type)
{
    // An empty or 'Default' type selects the plain GL pipeline. A failed
    // RendererFactory::create() also returns null, falling back to plain GL.
    if (type.empty() || type == "Default") {
        if (_pimpl->renderer) {
            _pimpl->clearOverlayCaptures();
            if (selectionRoot)
                selectionRoot->setExternalRenderer(nullptr);
            ObjectMetaFeed::instance().forget(_pimpl->renderer.get());
            _pimpl->renderer.reset();
            getSoRenderManager()->scheduleRedraw();
        }
    }
    else if (!_pimpl->renderer || _pimpl->renderer->type() != type) {
        _pimpl->clearOverlayCaptures();
        if (selectionRoot)
            selectionRoot->setExternalRenderer(nullptr);
        if (_pimpl->renderer)
            ObjectMetaFeed::instance().forget(_pimpl->renderer.get());
        _pimpl->renderer = RendererFactory::create(
                type, qobject_cast<QOpenGLWidget*>(getGLWidget()));
        if (_pimpl->renderer && selectionRoot) {
            selectionRoot->setExternalRenderer(_pimpl->renderer.get(), _pimpl->view);
            Gui::initRenderProperties(_pimpl->view);
            // Seed the renderer-layer knobs it cannot read itself
            // before the backend can start serving (RenderParams
            // changes re-push through onRenderParamChanged).
            Render::SceneStreamServer::setLevelThreadCap(
                int(RenderParams::getLevelThreads()));
            // Apply the persisted AntiAliasing preference to the backend's
            // offscreen target now: at startup the sample count otherwise
            // only reaches the Qt surface-format REQUEST (not granted on
            // e.g. WSLg/Wayland), and the preference handler only fires on
            // a change — so a stored MSAA setting was silently ignored
            // until the user re-toggled it.
            applyRendererAntiAliasing();
        }
        // Remote-viewer click selection: while the backend serves the
        // scene stream (FC_BGFX_SERVE_SCENE), viewer clicks arrive as
        // world-ray pick requests on a server thread — marshal them to
        // the GUI thread and pick against this viewer's scene.
        if (_pimpl->renderer && getenv("FC_BGFX_SERVE_SCENE")) {
            QPointer<View3DInventorViewer> self(this);
            Render::SceneStreamServer::instance().setPickHandler(
                [self](const Render::ScenePickRequest &req) {
                    Render::ScenePickRequest r = req;
                    QMetaObject::invokeMethod(qApp, [self, r]() {
                        if (self)
                            self->pickAndSelect(
                                SbVec3f(r.origin[0], r.origin[1], r.origin[2]),
                                SbVec3f(r.dir[0], r.dir[1], r.dir[2]),
                                r.modifiers & 1);
                    }, Qt::QueuedConnection);
                });
            // The semantic control channel (docs/ThinClient.md §4.2):
            // property reads (and later edits) from remote viewers,
            // marshalled to the GUI thread inside.
            installSceneControlHandler();
            // A finished level-generation job (§7, phase 5c) is
            // announced by the next publish, and the publish poll
            // lives in the render path — so an idle backend would sit
            // on finished work forever. The notifier's whole job is a
            // frame.
            Render::SceneStreamServer::instance().setWorkNotifier(
                [self]() {
                    QMetaObject::invokeMethod(qApp, [self]() {
                        if (self)
                            self->getSoRenderManager()->scheduleRedraw();
                    }, Qt::QueuedConnection);
                });
        }
        getSoRenderManager()->scheduleRedraw();
    }
    // The Coin render mode, which Tessellation picks differently
    // with a backend present: it is sticky, so switching the backend on
    // or off under that style already applied has to re-decide it. Only
    // Tessellation -- applyOverrideMode() would re-enter activateShadow()
    // for the Shadow style, which is neither cheap nor wanted here.
    if (SoFCUnifiedSelection::DisplayModeTessellation == overrideMode.c_str()) {
        getSoRenderManager()->setRenderMode(
                _pimpl->renderer ? SoRenderManager::AS_IS
                                 : SoRenderManager::HIDDEN_LINE);
    }
}

void View3DInventorViewer::pickAndSelect(const SbVec3f &origin,
                                         const SbVec3f &dir,
                                         bool ctrl)
{
    SoRayPickAction rp(getSoRenderManager()->getViewportRegion());
    rp.setRay(origin, dir);
    rp.setRadius(getPickRadius());
    // setRay picking needs the camera as a traversed child (the
    // getPointOnRay() pattern), so pick a temporary root of camera +
    // scene instead of the scene graph directly.
    auto root = new SoSeparator;
    root->ref();
    if (SoCamera *cam = getSoRenderManager()->getCamera())
        root->addChild(cam);
    root->addChild(getSoRenderManager()->getSceneGraph());
    rp.apply(root);

    SoPickedPoint *pp = rp.getPickedPoint();
    ViewProviderDocumentObject *vpd = nullptr;
    std::string subname;
    if (pp && guiDocument) {
        vpd = guiDocument->getViewProviderByPathFromHead(
                static_cast<SoFullPath*>(pp->getPath()));
        if (vpd && (!vpd->getObject()
                    || !vpd->getObject()->isAttachedToDocument()
                    || !vpd->getElementPicked(pp, subname)))
            vpd = nullptr;
    }
    if (!vpd) {
        if (!ctrl)
            Gui::Selection().clearSelection();
        root->unref();
        return;
    }

    const char *docname = vpd->getObject()->getDocument()->getName();
    const char *objname = vpd->getObject()->getNameInDocument();
    const auto &pt = pp->getPoint();
    SelectionNoTopParentCheck guard;
    if (ctrl) {
        if (Gui::Selection().isSelected(docname, objname, subname.c_str(),
                                        ResolveMode::NoResolve))
            Gui::Selection().rmvSelection(docname, objname, subname.c_str());
        else
            Gui::Selection().addSelection(docname, objname, subname.c_str(),
                                          pt[0], pt[1], pt[2]);
    }
    else {
        Gui::Selection().clearSelection();
        Gui::Selection().addSelection(docname, objname, subname.c_str(),
                                      pt[0], pt[1], pt[2]);
    }
    root->unref();
}

/// Keep the embedded environment-image copy in step with the path and
/// the embed toggle. App::PropertyFileIncluded has no reference
/// counting — each property owns its own copy in the document's
/// transient directory — so the copy is only made while the toggle is
/// on, and dropped again when it goes off.
static void syncEnvImageEmbed(View3DInventor *view)
{
    // While restoring, the path and the embedded copy arrive as
    // separate properties in file order — acting on the first would
    // re-copy the image (and mark the document modified) just by
    // opening it.
    if (!view || view->isRestoring())
        return;
    auto data = Base::freecad_dynamic_cast<App::PropertyFileIncluded>(
            view->getPropertyByName("Render_PBREnvImageData"));
    auto path = Base::freecad_dynamic_cast<App::PropertyFile>(
            view->getPropertyByName("Render_PBREnvImage"));
    auto embed = Base::freecad_dynamic_cast<App::PropertyBool>(
            view->getPropertyByName("Render_PBREnvEmbed"));
    if (!data || !path || !embed)
        return;
    const char *src = path->getValue();
    if (!embed->getValue() || !src || !src[0]) {
        if (!data->isEmpty())
            data->setValue("");
        return;
    }
    // Keep the copy we already have when it is the same image, or when
    // the source is gone — a restored document holds the copy but not
    // the original path (the source name is runtime state, so after a
    // restore it reads empty).
    if (!data->isEmpty()
            && (data->getOriginalFileName() == src
                || !Base::FileInfo(src).exists()))
        return;
    try {
        data->setValue(src);
    }
    catch (const Base::Exception &e) {
        FC_WARN("cannot embed environment image " << src << ": "
                << e.what());
    }
}

void Gui::initRenderProperties(App::PropertyContainer *view)
{
    // Materialize the per-view render engine settings as Render_* dynamic
    // properties on the view object, like the Shadow draw style's Shadow_*
    // properties, so the user can override the global RenderParams
    // preferences per view/document. They are created here - when a
    // renderer backend is selected - not lazily by the per-frame config
    // feed, which only reads existing properties
    // (SoFCRendererBridge::translateAOConfig() etc.).
    if (!view)
        return;
    _renderParam<App::PropertyBool>(view, "AO",
            RenderParams::docAO(), RenderParams::getAO());
    // The AO method is an enumeration; _renderParam can't create it (the
    // generic helper sets the default value before any callback could
    // install the enum strings), so materialize it explicitly.
    if (!view->getPropertyByName("Render_AOMethod")) {
        static const char* _ssaoMethodEnums[] = {"SSAO", "GTAO", nullptr};
        auto prop = static_cast<App::PropertyEnumeration*>(
                view->addDynamicProperty("App::PropertyEnumeration",
                                         "Render_AOMethod", "Render",
                                         RenderParams::docAOMethod()));
        prop->setEnums(_ssaoMethodEnums);
        prop->setValue(long(RenderParams::getAOMethod()));
    }
    _localRenderParam<App::PropertyInteger>(view, "AOSlices",
            RenderParams::docAOSlices(), RenderParams::getAOSlices());
    _localRenderParam<App::PropertyInteger>(view, "AOSteps",
            RenderParams::docAOSteps(), RenderParams::getAOSteps());
    _renderParam<App::PropertyBool>(view, "Shadow",
            RenderParams::docShadow(), RenderParams::getShadow());
    _localRenderParam<App::PropertyFloat>(view, "EffectResolution",
            RenderParams::docEffectResolution(),
            RenderParams::getEffectResolution());
    _localRenderParam<App::PropertyInteger>(view, "CoarseTessellation",
            RenderParams::docCoarseTessellation(),
            RenderParams::getCoarseTessellation());
    _localRenderParam<App::PropertyFloat>(view, "LevelTolerance",
            RenderParams::docLevelTolerance(),
            RenderParams::getLevelTolerance());
    // No per-view GpuMemoryBudgetMB, LevelDebug or LevelCeilingSimulateMB:
    // those are machine-resource and measurement knobs, global RenderParams
    // only (see stripLegacyRenderProperties for the full ruling).
    _renderParam<App::PropertyFloat>(view, "AORadius",
            RenderParams::docAORadius(), RenderParams::getAORadius());
    _renderParam<App::PropertyFloat>(view, "AOIntensity",
            RenderParams::docAOIntensity(), RenderParams::getAOIntensity());
    _localRenderParam<App::PropertyFloat>(view, "AOResolution",
            RenderParams::docAOResolution(), RenderParams::getAOResolution());
    // Cavity composes with occlusion rather than replacing it, so it sits
    // with the AO block.
    _renderParam<App::PropertyBool>(view, "Cavity",
            RenderParams::docCavity(), RenderParams::getCavity());
    _renderParam<App::PropertyFloat>(view, "CavityValley",
            RenderParams::docCavityValley(), RenderParams::getCavityValley());
    _renderParam<App::PropertyFloat>(view, "CavityRidge",
            RenderParams::docCavityRidge(), RenderParams::getCavityRidge());
    _renderParam<App::PropertyFloat>(view, "CavityRadius",
            RenderParams::docCavityRadius(), RenderParams::getCavityRadius());
    _renderParam<App::PropertyBool>(view, "PBR",
            RenderParams::docPBR(), RenderParams::getPBR());
    static const App::PropertyFloatConstraint::Constraints _unit_cstr(0.0,1.0,0.1);
    auto applyUnitConstraint = [](App::PropertyFloatConstraint &prop) {
        if (!prop.getConstraints())
            prop.setConstraints(&_unit_cstr);
    };
    _renderParam<App::PropertyFloatConstraint>(view, "PBRMetallic",
            RenderParams::docPBRMetallic(), RenderParams::getPBRMetallic(),
            applyUnitConstraint);
    _renderParam<App::PropertyFloatConstraint>(view, "PBRRoughness",
            RenderParams::docPBRRoughness(), RenderParams::getPBRRoughness(),
            applyUnitConstraint);
    _renderParam<App::PropertyFloat>(view, "PBREnvIntensity",
            RenderParams::docPBREnvIntensity(), RenderParams::getPBREnvIntensity());
    // The environment image is a plain path; Render_PBREnvEmbed
    // optionally copies it into the document (Render_PBREnvImageData),
    // which then takes precedence — see syncEnvImageEmbed().
    _renderParam<App::PropertyFile>(view, "PBREnvImage",
            RenderParams::docPBREnvImage(),
            RenderParams::getPBREnvImage().c_str());
    _renderParam<App::PropertyBool>(view, "PBREnvEmbed",
            RenderParams::docPBREnvEmbed(), RenderParams::getPBREnvEmbed());
    if (!view->getPropertyByName("Render_PBREnvImageData")) {
        view->addDynamicProperty("App::PropertyFileIncluded",
                "Render_PBREnvImageData", "Render",
                "Copy of the environment image stored in the document "
                "(filled while Render_PBREnvEmbed is set).");
    }
    // No sync here on purpose: a restored document already carries its
    // copy, and re-embedding it would mark the document modified just
    // by opening it. The copy follows explicit property changes only
    // (View3DInventorViewer::onViewPropertyChanged).
    _renderParam<App::PropertyBool>(view, "PBREnvBackground",
            RenderParams::docPBREnvBackground(),
            RenderParams::getPBREnvBackground());
    // Matcap is the other shading model: it overrides PBR while on, so it
    // follows it here.
    _renderParam<App::PropertyBool>(view, "Matcap",
            RenderParams::docMatcap(), RenderParams::getMatcap());
    // An enumeration, like Render_AOMethod above: materialized by hand so
    // the names are installed before the value is set.
    if (!view->getPropertyByName("Render_MatcapPreset")) {
        static const char* _matcapPresetEnums[] =
            {"Studio", "Clay", "Metal", "Pearl", nullptr};
        auto prop = static_cast<App::PropertyEnumeration*>(
                view->addDynamicProperty("App::PropertyEnumeration",
                                         "Render_MatcapPreset", "Render",
                                         RenderParams::docMatcapPreset()));
        prop->setEnums(_matcapPresetEnums);
        prop->setValue(long(RenderParams::getMatcapPreset()));
    }
    _renderParam<App::PropertyFloatConstraint>(view, "MatcapTint",
            RenderParams::docMatcapTint(), RenderParams::getMatcapTint(),
            applyUnitConstraint);
    _renderParam<App::PropertyFloat>(view, "BumpScale",
            RenderParams::docBumpScale(), RenderParams::getBumpScale());
    _renderParam<App::PropertyBool>(view, "Parallax",
            RenderParams::docParallax(), RenderParams::getParallax());
    _renderParam<App::PropertyBool>(view, "Volumetric",
            RenderParams::docVolumetric(), RenderParams::getVolumetric());
    _renderParam<App::PropertyFloat>(view, "VolumetricIntensity",
            RenderParams::docVolumetricIntensity(),
            RenderParams::getVolumetricIntensity());
    _renderParam<App::PropertyFloat>(view, "VolumetricDensity",
            RenderParams::docVolumetricDensity(),
            RenderParams::getVolumetricDensity());
    _renderParam<App::PropertyBool>(view, "Bloom",
            RenderParams::docBloom(), RenderParams::getBloom());
    _renderParam<App::PropertyFloat>(view, "BloomThreshold",
            RenderParams::docBloomThreshold(),
            RenderParams::getBloomThreshold());
    _renderParam<App::PropertyFloat>(view, "BloomIntensity",
            RenderParams::docBloomIntensity(),
            RenderParams::getBloomIntensity());
    _renderParam<App::PropertyFloat>(view, "BloomRadius",
            RenderParams::docBloomRadius(),
            RenderParams::getBloomRadius());
    // The renderer's own scene light (docs/CoinRetirement.md stage 4a).
    // Off by default and inert while off: a light found in the Coin
    // traversal still wins, so the Shadow draw style is unaffected.
    // Direction and position are vectors here rather than the three
    // scalars the global parameters use, to match the Shadow_* shape a
    // later stage has to migrate from.
    _renderParam<App::PropertyBool>(view, "Light",
            RenderParams::docLight(), RenderParams::getLight());
    _renderParam<App::PropertyVector>(view, "LightDirection",
            RenderParams::docLight(),
            Base::Vector3d(RenderParams::getLightDirectionX(),
                           RenderParams::getLightDirectionY(),
                           RenderParams::getLightDirectionZ()));
    _renderParam<App::PropertyColor>(view, "LightColor",
            RenderParams::docLightColor(),
            App::Color(uint32_t(RenderParams::getLightColor())));
    _renderParam<App::PropertyFloat>(view, "LightIntensity",
            RenderParams::docLightIntensity(),
            RenderParams::getLightIntensity());
    _renderParam<App::PropertyBool>(view, "LightSpot",
            RenderParams::docLightSpot(), RenderParams::getLightSpot());
    _renderParam<App::PropertyVector>(view, "LightPosition",
            RenderParams::docLightSpot(),
            Base::Vector3d(RenderParams::getLightPositionX(),
                           RenderParams::getLightPositionY(),
                           RenderParams::getLightPositionZ()));
    _renderParam<App::PropertyFloat>(view, "LightCutOffAngle",
            RenderParams::docLightCutOffAngle(),
            RenderParams::getLightCutOffAngle());
    _renderParam<App::PropertyFloat>(view, "LightDropOffRate",
            RenderParams::docLightDropOffRate(),
            RenderParams::getLightDropOffRate());
    // The shadow map and the ground receiver it falls on: their own
    // group, materialized here so the surface exists wherever the
    // render properties do (stage 4d).
    materializeShadowRenderParams(view);
    _renderParam<App::PropertyBool>(view, "SunDisc",
            RenderParams::docSunDisc(), RenderParams::getSunDisc());
    _renderParam<App::PropertyFloat>(view, "SunDiscSize",
            RenderParams::docSunDiscSize(),
            RenderParams::getSunDiscSize());
    _renderParam<App::PropertyBool>(view, "Caustics",
            RenderParams::docCaustics(), RenderParams::getCaustics());
    _renderParam<App::PropertyFloat>(view, "CausticsIntensity",
            RenderParams::docCausticsIntensity(),
            RenderParams::getCausticsIntensity());
    _renderParam<App::PropertyFloat>(view, "CausticsScale",
            RenderParams::docCausticsScale(),
            RenderParams::getCausticsScale());
    _renderParam<App::PropertyFloat>(view, "CausticsSpeed",
            RenderParams::docCausticsSpeed(),
            RenderParams::getCausticsSpeed());
    _renderParam<App::PropertyBool>(view, "WaterSurface",
            RenderParams::docWaterSurface(),
            RenderParams::getWaterSurface());
    _renderParam<App::PropertyFloat>(view, "WaterWaveStrength",
            RenderParams::docWaterWaveStrength(),
            RenderParams::getWaterWaveStrength());
    _renderParam<App::PropertyFloat>(view, "WaterWaveScale",
            RenderParams::docWaterWaveScale(),
            RenderParams::getWaterWaveScale());
    _renderParam<App::PropertyFloat>(view, "WaterWaveSpeed",
            RenderParams::docWaterWaveSpeed(),
            RenderParams::getWaterWaveSpeed());
    _renderParam<App::PropertyFloat>(view, "WaterAbsorption",
            RenderParams::docWaterAbsorption(),
            RenderParams::getWaterAbsorption());
    _renderParam<App::PropertyFloat>(view, "WaterInscatter",
            RenderParams::docWaterInscatter(),
            RenderParams::getWaterInscatter());
    _renderParam<App::PropertyBool>(view, "WaterRefraction",
            RenderParams::docWaterRefraction(),
            RenderParams::getWaterRefraction());
    _renderParam<App::PropertyBool>(view, "WaterReflection",
            RenderParams::docWaterReflection(),
            RenderParams::getWaterReflection());
    _renderParam<App::PropertyBool>(view, "WaterPlanarReflection",
            RenderParams::docWaterPlanarReflection(),
            RenderParams::getWaterPlanarReflection());
    _renderParam<App::PropertyBool>(view, "WaterShadow",
            RenderParams::docWaterShadow(),
            RenderParams::getWaterShadow());
    _renderParam<App::PropertyFloat>(view, "WaterShadowWobble",
            RenderParams::docWaterShadowWobble(),
            RenderParams::getWaterShadowWobble());
    // Enumeration property: like Render_AOMethod, _renderParam can't
    // create it (the generic helper sets the default value before the
    // enum strings exist), so materialize it explicitly.
    if (!view->getPropertyByName("Render_WaterRippleType")) {
        static const char* _rippleTypeEnums[] = {"Waves", "Rain", "None",
                                                 nullptr};
        auto prop = static_cast<App::PropertyEnumeration*>(
                view->addDynamicProperty("App::PropertyEnumeration",
                                         "Render_WaterRippleType", "Render",
                                         RenderParams::docWaterRippleType()));
        prop->setEnums(_rippleTypeEnums);
        prop->setValue(long(RenderParams::getWaterRippleType()));
    }
    _renderParam<App::PropertyFloat>(view, "WaterRippleDensity",
            RenderParams::docWaterRippleDensity(),
            RenderParams::getWaterRippleDensity());
    _renderParam<App::PropertyFloat>(view, "WaterImpactStrength",
            RenderParams::docWaterImpactStrength(),
            RenderParams::getWaterImpactStrength());
    _renderParam<App::PropertyFloat>(view, "WaterImpactLife",
            RenderParams::docWaterImpactLife(),
            RenderParams::getWaterImpactLife());
    _renderParam<App::PropertyBool>(view, "GroundReflection",
            RenderParams::docGroundReflection(),
            RenderParams::getGroundReflection());
    _renderParam<App::PropertyFloat>(view, "GroundReflectionIntensity",
            RenderParams::docGroundReflectionIntensity(),
            RenderParams::getGroundReflectionIntensity());

    // No occlusion-culling and no RenderDebug_* switch properties: those
    // are performance mechanisms and measurement state, global
    // RenderParams only. They used to be materialized here (hidden), and
    // documents that saved them shadowed the globals every measurement
    // set -- see stripLegacyRenderProperties. Custom RenderDebug_<name>
    // shader parameters (docs/RenderDebug.md sec 2.5) remain per-view: they
    // are dynamically named, created by the user or a script, and no
    // global parameter could stand in for them.
}

const char * const *Gui::shadowRenderPropertyNames()
{
    // Everything materializeShadowRenderParams creates, i.e. everything
    // the shadow map and its ground receiver are configured with. The
    // list exists to be excluded: any OTHER RenderShadow_<name> property
    // is a custom shader parameter feeding u_<name>
    // (docs/RenderDebug.md sec 2.5), and a knob the engine reads itself
    // must not also upload a uniform nobody declares.
    static const char * const names[] = {
        "RenderShadow_Precision",
        "RenderShadow_Epsilon",
        "RenderShadow_Threshold",
        "RenderShadow_SmoothBorder",
        "RenderShadow_SpreadSize",
        "RenderShadow_SpreadSampleSize",
        "RenderShadow_ShowGround",
        "RenderShadow_GroundColor",
        "RenderShadow_GroundTransparency",
        "RenderShadow_GroundBackFaceCull",
        "RenderShadow_GroundShading",
        "RenderShadow_GroundTexture",
        "RenderShadow_GroundBumpMap",
        "RenderShadow_GroundTextureSize",
        "RenderShadow_GroundSizeAuto",
        "RenderShadow_GroundSizeScale",
        "RenderShadow_GroundSizeX",
        "RenderShadow_GroundSizeY",
        "RenderShadow_GroundAutoPosition",
        "RenderShadow_GroundPlacement",
        nullptr,
    };
    return names;
}

// Where each Shadow_* property went (docs/CoinRetirement.md stage 4d).
// The ground and the map's own quality knobs kept their names under the
// RenderShadow_ prefix; the light was already waiting for them as
// Render_Light* (stage 4a shaped it to match, defaults included).
static const struct { const char *from; const char *to; } _shadowPropertyMap[] = {
    {"Shadow_Precision",             "RenderShadow_Precision"},
    {"Shadow_Epsilon",               "RenderShadow_Epsilon"},
    {"Shadow_Threshold",             "RenderShadow_Threshold"},
    {"Shadow_SmoothBorder",          "RenderShadow_SmoothBorder"},
    {"Shadow_SpreadSize",            "RenderShadow_SpreadSize"},
    {"Shadow_SpreadSampleSize",      "RenderShadow_SpreadSampleSize"},
    {"Shadow_ShowGround",            "RenderShadow_ShowGround"},
    {"Shadow_GroundColor",           "RenderShadow_GroundColor"},
    {"Shadow_GroundTransparency",    "RenderShadow_GroundTransparency"},
    {"Shadow_GroundBackFaceCull",    "RenderShadow_GroundBackFaceCull"},
    {"Shadow_GroundShading",         "RenderShadow_GroundShading"},
    {"Shadow_GroundTexture",         "RenderShadow_GroundTexture"},
    {"Shadow_GroundBumpMap",         "RenderShadow_GroundBumpMap"},
    {"Shadow_GroundTextureSize",     "RenderShadow_GroundTextureSize"},
    {"Shadow_GroundSizeAuto",        "RenderShadow_GroundSizeAuto"},
    {"Shadow_GroundSizeScale",       "RenderShadow_GroundSizeScale"},
    {"Shadow_GroundSizeX",           "RenderShadow_GroundSizeX"},
    {"Shadow_GroundSizeY",           "RenderShadow_GroundSizeY"},
    {"Shadow_GroundAutoPosition",    "RenderShadow_GroundAutoPosition"},
    {"Shadow_GroundPlacement",       "RenderShadow_GroundPlacement"},
    {"Shadow_LightDirection",        "Render_LightDirection"},
    {"Shadow_LightColor",            "Render_LightColor"},
    {"Shadow_LightIntensity",        "Render_LightIntensity"},
    {"Shadow_SpotLight",             "Render_LightSpot"},
    {"Shadow_SpotLightPosition",     "Render_LightPosition"},
    {"Shadow_SpotLightCutOffAngle",  "Render_LightCutOffAngle"},
    {"Shadow_SpotLightDropOffRate",  "Render_LightDropOffRate"},
};

/// Move one value across, by type where the types agree and by number
/// where they do not. Three of the light pairs differ: the draw style
/// constrained its float, and stated its cone in an App::PropertyAngle;
/// the renderer's are plain floats of the same unit, so the value is
/// carried rather than the property. Both sides of every numeric pair
/// derive from PropertyFloat or PropertyInteger, which is what makes
/// that a two-line conversion instead of a table of casts.
static bool _copyPropertyValue(App::Property *from, App::Property *to)
{
    if (!from || !to)
        return false;
    if (from->getTypeId() == to->getTypeId()) {
        to->Paste(*from);
        return true;
    }
    auto ffrom = Base::freecad_dynamic_cast<App::PropertyFloat>(from);
    auto fto = Base::freecad_dynamic_cast<App::PropertyFloat>(to);
    if (ffrom && fto) {
        fto->setValue(ffrom->getValue());
        return true;
    }
    auto ifrom = Base::freecad_dynamic_cast<App::PropertyInteger>(from);
    auto ito = Base::freecad_dynamic_cast<App::PropertyInteger>(to);
    if (ifrom && ito) {
        ito->setValue(ifrom->getValue());
        return true;
    }
    // A float property standing in for an integer one, or the reverse.
    if (ffrom && ito) {
        ito->setValue(long(ffrom->getValue()));
        return true;
    }
    if (ifrom && fto) {
        fto->setValue(double(ifrom->getValue()));
        return true;
    }
    return false;
}

void Gui::applyLegacyShadowStyle(App::PropertyContainer *view)
{
    if (!view)
        return;
    // What the Shadow draw style was, in the terms that outlive it: the
    // renderer's own scene light -- off by default, and the thing a
    // Shadow-styled document was lit by -- with its shadow map, which
    // defaults on.
    _renderParam<App::PropertyBool>(view, "Light",
            RenderParams::docLight(), RenderParams::getLight(),
            [](App::PropertyBool &prop) { prop.setValue(true); });
    _renderParam<App::PropertyBool>(view, "Shadow",
            RenderParams::docShadow(), RenderParams::getShadow(),
            [](App::PropertyBool &prop) { prop.setValue(true); });
}

void Gui::migrateShadowProperties(App::PropertyContainer *view)
{
    if (!view)
        return;

    // The draw style first, because it is read from a property this
    // loop is about to take away. A document that says "Shadow" means
    // "the display style inside Shadow_DisplayMode, lit and shadowed by
    // the scene light" -- which is now Render_Light with Render_Shadow,
    // and an ordinary display style beside them.
    auto drawStyle = Base::freecad_dynamic_cast<App::PropertyEnumeration>(
            view->getPropertyByName("DrawStyle"));
    if (drawStyle && drawStyle->getEnum().isValid()
            && boost::equals(drawStyle->getValueAsString(), "Shadow")) {
        static const char *_subMode[] = {"Flat Lines", "Shaded", "As Is",
                                         "Hidden Line"};
        long sub = ViewParams::getShadowDisplayMode();
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyEnumeration>(
                    view->getPropertyByName("Shadow_DisplayMode")))
            sub = prop->getValue();
        if (sub < 0 || sub >= long(sizeof(_subMode)/sizeof(_subMode[0])))
            sub = 0;
        int index = drawStyleIndexFromName(_subMode[sub]);
        if (index >= 0)
            drawStyle->setValue(long(index));
        applyLegacyShadowStyle(view);
    }

    // Then the properties. Nothing is created for a name the document
    // did not carry: a property exists on a view only because somebody
    // chose it, and materializeShadowRenderParams answers for the rest.
    bool migrated = false;
    for (const auto &pair : _shadowPropertyMap) {
        auto from = view->getPropertyByName(pair.from);
        if (!from)
            continue;
        migrated = true;
        auto to = view->getPropertyByName(pair.to);
        if (!to) {
            // The target family is materialized when the render
            // properties are (initRenderProperties), which for a
            // restoring view may not have happened yet -- create it
            // with the same type, and let the materializer find it.
            const char *group = boost::starts_with(pair.to, "RenderShadow_")
                ? "Render Shadow" : "Render";
            to = view->addDynamicProperty(from->getTypeId().getName(),
                                          pair.to, group,
                                          from->getDocumentation());
        }
        _copyPropertyValue(from, to);
        view->removeDynamicProperty(pair.from);
    }
    // The rest of the Shadow group is the Coin shadow group's own: the
    // sub display mode just folded into the draw style, the light
    // camera's bounding-box scale and cut-off distance, and Coin's
    // transparent-shadow flag. They are removed with the draw style
    // that reads them, one stage from here (4e), not here -- until then
    // an override still has to work.
    if (migrated)
        FC_LOG("migrated the Shadow draw style's properties");
}

const char * const *Gui::legacyRenderPropertyNames()
{
    // The per-view render properties that were retired to global
    // RenderParams (2026-08-14): debug and measurement switches, the
    // ladder's tuning knobs, the occlusion-culling mechanism, and the
    // GPU budget -- none express per-view display intent, and saved
    // copies inside documents shadowed whatever the preferences or a
    // measurement harness set globally. Old documents still carry them
    // as saved dynamic properties; View3DInventor::Restore strips them
    // after reading, so loading stays compatible and the dead surface
    // does not linger in the property editor.
    static const char * const names[] = {
        "Render_GpuMemoryBudgetMB",
        "Render_LevelDebug",
        "Render_LevelCeilingSimulateMB",
        "Render_Occlusion",
        "Render_OcclusionSoftware",
        "Render_OcclusionSimd",
        "Render_OcclusionPerInstance",
        "Render_OcclusionCoarse",
        "Render_OcclusionVisibleTtl",
        "Render_OcclusionBudget",
        "Render_OcclusionMinSubtree",
        "Render_OcclusionMaxHidden",
        "Render_OcclusionDepthPad",
        "Render_OcclusionConfirm",
        "Render_OcclusionOccluderTris",
        "Render_OcclusionMinOccluder",
        "Render_OcclusionResolution",
        "Render_OcclusionThreads",
        "Render_OcclusionDemoteStreak",
        "Render_OcclusionCoarseLevel",
        "Render_OcclusionCoarseMinTris",
        "Render_OcclusionCoarseBuilds",
        "Render_OcclusionCoarseBias",
        "Render_OcclusionCoarseMemory",
        "Render_OcclusionBenefitProbe",
        "Render_LevelPressureRelease",
        "Render_DowngradeLedger",
        "Render_ClimbHardLimit",
        "Render_ClimbAdmitBatch",
        "Render_DescentOrderBatch",
        "Render_ShapeVertices",
        "Render_PressureDropEdges",
        "Render_LoadDropElements",
        "RenderDebug_ViewMode",
        "RenderDebug_FreezeFrame",
        "RenderDebug_Label",
        "RenderDebug_Timing",
        "RenderDebug_Delta",
        "RenderDebug_Coverage",
        "RenderDebug_Occlusion",
        "RenderDebug_ProxyCut",
        "RenderDebug_ProxyGen",
        "RenderDebug_CullAudit",
        "RenderDebug_CullBounds",
        nullptr,
    };
    return names;
}

void Gui::stripLegacyRenderProperties(App::PropertyContainer *view)
{
    if (!view)
        return;
    for (const char * const *name = legacyRenderPropertyNames(); *name;
         ++name) {
        if (view->getPropertyByName(*name))
            view->removeDynamicProperty(*name);
    }

}

// The render properties that state what the machine can afford rather than
// what the model should look like: how many samples the ambient occlusion
// traces and at what resolution, how much GPU memory the backend may take,
// how coarsely a shape may be tessellated, and the whole debug
// instrumentation. Everything else in the group describes a look, which is
// the author's and is meant to travel; these describe the desktop it was
// authored on, and arriving on a smaller machine as somebody else's budget
// is the opposite of what they are for. AOMethod is deliberately not here:
// SSAO against GTAO reads as an authoring choice, not a cost dial.
static const char *_localRenderProperties[] = {
    "Render_AOResolution",
    "Render_AOSlices",
    "Render_AOSteps",
    "Render_CoarseTessellation",
    "Render_EffectResolution",
    "Render_LevelTolerance",
    nullptr
};

bool Gui::isLocalRenderProperty(const char *name)
{
    if (!name)
        return false;
    if (boost::starts_with(name, "RenderDebug_"))
        return true;
    for (const char **local = _localRenderProperties; *local; ++local) {
        if (boost::equals(name, *local))
            return true;
    }
    return false;
}

// The section/clipping style, which a view can answer for with a Section_*
// property. Unlike the Render_* group nothing materializes these: a
// property here exists only because somebody chose an override -- a saved
// view restoring one, or a hand edit -- and the preference answers for
// every view that has none. That is what keeps a clipped presentation from
// moving the reader's own defaults when the document is opened.
template<class PropT, class ValueT>
static ValueT _sectionStyle(App::PropertyContainer *view, const char *name,
                            const ValueT &def)
{
    if (!view)
        return def;
    char full[128];
    snprintf(full, sizeof(full)-1, "Section_%s", name);
    auto prop = view->getPropertyByName(full);
    if (!prop || !prop->isDerivedFrom(PropT::getClassTypeId()))
        return def;
    return ValueT(static_cast<PropT*>(prop)->getValue());
}

bool Gui::sectionStyle(App::PropertyContainer *view, const char *name, bool def)
{
    return _sectionStyle<App::PropertyBool, bool>(view, name, def);
}

double Gui::sectionStyle(App::PropertyContainer *view, const char *name, double def)
{
    return _sectionStyle<App::PropertyFloat, double>(view, name, def);
}

std::string Gui::sectionStyle(App::PropertyContainer *view, const char *name,
                              const std::string &def)
{
    return _sectionStyle<App::PropertyString, std::string>(view, name, def);
}

void Gui::reseedLocalRenderProperties(App::PropertyContainer *view)
{
    if (!view)
        return;
    // Only for a view that already carries the render set. Rebuilding it
    // here on a view that has none would hand it the whole group it never
    // asked for -- and then save it.
    if (!view->getPropertyByName("Render_AO"))
        return;
    std::vector<std::string> stale;
    for (const auto &v : view->getDynamicPropertyNames()) {
        if (Gui::isLocalRenderProperty(v.c_str()))
            stale.push_back(v);
    }
    if (stale.empty())
        return;
    // A file written before these became local carries the author's values
    // in them, and restoring one overwrites what this installation seeded.
    // Drop those and let initRenderProperties put the local preference back
    // -- which also gives them the Prop_NoPersist they can only be born
    // with, so the next save no longer carries them.
    for (const auto &name : stale)
        view->removeDynamicProperty(name.c_str());
    initRenderProperties(view);
}

// The viewer's light rig. Every open viewer used to read these keys
// straight out of the View preference group, which made one rig for the
// whole application: a document that wanted its own lighting could not
// have it, and a saved view could only restage a lit look by moving
// everybody else's. Each key now lands in a Light_* view property, and the
// viewer takes its light from there.
//
// A property carries PropNoPersist for as long as it is only a copy of the
// preference: the preference still reaches every view that has not
// overridden it, and a merely seeded value is written to no file. The
// first edit clears the mark, and from then on the property is an override
// -- it lights this view alone, it travels with the document, and a saved
// view captures it. That one bit is what says "the author meant this", so
// it is also what decides whether the setting reaches somebody else's
// installation.
namespace {

enum LightTarget { HeadLight, BackLight, FillLight, SceneAmbient };
enum LightField { FieldEnable, FieldColor, FieldDirection, FieldIntensity };

struct LightPropertyDef {
    const char *key;        // the View preference key, and the Light_ suffix
    LightTarget target;
    LightField field;
    unsigned long def;      // bool, packed colour, or intensity per cent
    const char *docu;
};

const LightPropertyDef _lightProperties[] = {
    {"EnableHeadlight", HeadLight, FieldEnable, 1,
     "Light this view with the headlight"},
    {"HeadlightColor", HeadLight, FieldColor, 0xFFFFFFFF,
     "Colour of this view's headlight"},
    {"HeadlightDirection", HeadLight, FieldDirection, 0,
     "Direction of this view's headlight, in eye space"},
    {"HeadlightIntensity", HeadLight, FieldIntensity, 100,
     "Intensity of this view's headlight"},
    {"EnableBacklight", BackLight, FieldEnable, 0,
     "Light this view's back faces with the backlight"},
    {"BacklightColor", BackLight, FieldColor, 0xFFFFFFFF,
     "Colour of this view's backlight"},
    {"BacklightDirection", BackLight, FieldDirection, 0,
     "Direction of this view's backlight, in eye space"},
    {"BacklightIntensity", BackLight, FieldIntensity, 100,
     "Intensity of this view's backlight"},
    {"EnableFillLight", FillLight, FieldEnable, 0,
     "Light this view with the off-axis fill light"},
    {"FillLightColor", FillLight, FieldColor, 0xE6FAFFFF,
     "Colour of this view's fill light"},
    {"FillLightDirection", FillLight, FieldDirection, 0,
     "Direction of this view's fill light, relative to the camera"},
    {"FillLightIntensity", FillLight, FieldIntensity, 60,
     "Intensity of this view's fill light"},
    {"AmbientLightColor", SceneAmbient, FieldColor, 0xFFFFFFFF,
     "Colour of this view's scene ambient light"},
    {"AmbientLightIntensity", SceneAmbient, FieldIntensity, 20,
     "Intensity of this view's scene ambient light (Coin's own default is 20)"},
};

const LightPropertyDef *_findLightProperty(const char *key)
{
    if (!key)
        return nullptr;
    for (const auto &def : _lightProperties) {
        if (boost::equals(key, def.key))
            return &def;
    }
    return nullptr;
}

const char *_lightPropertyType(LightField field)
{
    switch (field) {
    case FieldEnable:
        return "App::PropertyBool";
    case FieldColor:
        return "App::PropertyColor";
    case FieldDirection:
        return "App::PropertyVector";
    default:
        return "App::PropertyFloat";
    }
}

std::string _lightPropertyName(const LightPropertyDef &def)
{
    return std::string("Light_") + def.key;
}

SoDirectionalLight *_lightNode(View3DInventorViewer &viewer, LightTarget target)
{
    switch (target) {
    case HeadLight:
        return viewer.getHeadlight();
    case BackLight:
        return viewer.getBacklight();
    case FillLight:
        return viewer.getFillLight();
    default:
        return nullptr;
    }
}

void _applyLight(View3DInventorViewer &viewer, const LightPropertyDef &def,
                 const App::Property &prop)
{
    if (!prop.isDerivedFrom(Base::Type::fromName(_lightPropertyType(def.field))))
        return;
    switch (def.field) {
    case FieldEnable: {
        bool on = static_cast<const App::PropertyBool &>(prop).getValue();
        switch (def.target) {
        case HeadLight:
            viewer.setHeadlightEnabled(on);
            break;
        case BackLight:
            viewer.setBacklightEnabled(on);
            break;
        case FillLight:
            viewer.setFillLightEnabled(on);
            break;
        default:
            break;
        }
        break;
    }
    case FieldColor: {
        const auto &c = static_cast<const App::PropertyColor &>(prop).getValue();
        SbColor col(c.r, c.g, c.b);
        if (def.target == SceneAmbient) {
            if (auto env = viewer.getEnvironment())
                env->ambientColor.setValue(col);
        }
        else if (auto node = _lightNode(viewer, def.target))
            node->color.setValue(col);
        break;
    }
    case FieldDirection: {
        const auto &v = static_cast<const App::PropertyVector &>(prop).getValue();
        if (auto node = _lightNode(viewer, def.target))
            node->direction.setValue(float(v.x), float(v.y), float(v.z));
        break;
    }
    case FieldIntensity: {
        auto v = float(static_cast<const App::PropertyFloat &>(prop).getValue());
        if (def.target == SceneAmbient) {
            if (auto env = viewer.getEnvironment())
                env->ambientIntensity.setValue(v);
        }
        else if (auto node = _lightNode(viewer, def.target))
            node->intensity.setValue(v);
        break;
    }
    }
}

// Read the preference into a property of the matching type. Returns false
// when the preference has nothing to say, which only a direction can do --
// there is no sensible default for one, so an unset direction leaves the
// light pointing where it already points.
bool _readPreference(const LightPropertyDef &def, ParameterGrp &grp, App::Property &prop)
{
    switch (def.field) {
    case FieldEnable:
        static_cast<App::PropertyBool &>(prop).setValue(
                grp.GetBool(def.key, def.def != 0));
        return true;
    case FieldColor:
        static_cast<App::PropertyColor &>(prop).setValue(
                uint32_t(grp.GetUnsigned(def.key, def.def)));
        return true;
    case FieldDirection: {
        std::string dir = grp.GetASCII(def.key);
        if (dir.empty())
            return false;
        try {
            Base::Vector3f v = Base::to_vector(dir);
            static_cast<App::PropertyVector &>(prop).setValue(
                    Base::Vector3d(v.x, v.y, v.z));
        }
        catch (const std::exception &) {
            return false;
        }
        return true;
    }
    default:
        static_cast<App::PropertyFloat &>(prop).setValue(
                grp.GetInt(def.key, long(def.def)) / 100.0);
        return true;
    }
}

ParameterGrp::handle _viewParameterGroup()
{
    return App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View");
}

// What lights a view that has not overridden this key -- and every view of
// a viewer with no view object of its own (a headless publisher, a preview
// widget), which has nowhere to keep an override.
void _applyPreference(View3DInventorViewer &viewer,
                      const LightPropertyDef &def, ParameterGrp &grp)
{
    switch (def.field) {
    case FieldEnable: {
        App::PropertyBool prop;
        if (_readPreference(def, grp, prop))
            _applyLight(viewer, def, prop);
        break;
    }
    case FieldColor: {
        App::PropertyColor prop;
        if (_readPreference(def, grp, prop))
            _applyLight(viewer, def, prop);
        break;
    }
    case FieldDirection: {
        App::PropertyVector prop;
        if (_readPreference(def, grp, prop))
            _applyLight(viewer, def, prop);
        break;
    }
    default: {
        App::PropertyFloat prop;
        if (_readPreference(def, grp, prop))
            _applyLight(viewer, def, prop);
        break;
    }
    }
}

} // anonymous namespace

bool View3DInventorViewer::isLightPreferenceKey(const char *key)
{
    return _findLightProperty(key) != nullptr;
}

void View3DInventorViewer::applyLightProperty(const App::Property &prop)
{
    const char *name = prop.getName();
    if (!name || !boost::starts_with(name, "Light_"))
        return;
    if (auto def = _findLightProperty(name + 6))
        _applyLight(*this, *def, prop);
}

bool View3DInventorViewer::applyLightPreference(const char *key)
{
    auto def = _findLightProperty(key);
    if (!def)
        return false;

    // A property here IS the override -- it exists only because somebody
    // chose it -- so the preference stops at a view that has one. There is
    // no seeded copy to keep in step, and nothing to decide about saving:
    // what the view carries is what its author meant, and that is exactly
    // what belongs in the document.
    if (auto view = _pimpl->view) {
        std::string name = _lightPropertyName(*def);
        if (auto prop = view->getPropertyByName(name.c_str())) {
            _applyLight(*this, *def, *prop);
            return true;
        }
    }
    auto grp = _viewParameterGroup();
    _applyPreference(*this, *def, *grp);
    return true;
}

void View3DInventorViewer::syncLightProperties()
{
    for (const auto &def : _lightProperties)
        applyLightPreference(def.key);
}

// #define ENABLE_GL_DEPTH_RANGE
// The calls of glDepthRange inside renderScene() causes problems with transparent objects
// so that's why it is disabled now: https://forum.freecad.org/viewtopic.php?f=3&t=6037&hilit=transparency

// Documented in superclass. Overrides this method to be able to draw
// the axis cross, if selected, and to keep a continuous animation
// upon spin.
void View3DInventorViewer::renderScene()
{
    // The frame line splits the frame's CPU into ours, bgfx's and
    // `outside` -- and `outside` is this function's other half plus Qt.
    // These scopes are what tell them apart; they cost a relaxed load
    // each while the timing switch is off (Render::FrameOutside).
    Render::FrameOutsideScope outPre(Render::FrameOutside::Pre);

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

    bool externalRendered = false;
    SoCamera* cam = getSoRenderManager()->getCamera();
    if (cam && _pimpl->renderer) {
        SbMatrix viewMat, projMat;
        const SbViewportRegion vp = getSoRenderManager()->getViewportRegion();
        SbViewVolume vol = cam->getViewVolume(vp.getViewportAspectRatio());
        vol.getMatrices(viewMat, projMat);
        _pimpl->renderer->setBackground(_pimpl->backgroundFeed(col));
        // render() publishes on the way past when something is listening
        // (docs/HeadlessServe.md §4), and a published object entry names
        // its object for the viewer. The names come from here rather
        // than from the publish path — nothing about a label describes a
        // mesh. Costs a lookup and an integer compare per frame, and
        // only a serving process builds the table at all.
        if (Render::SceneStreamServer::instance().running())
            ObjectMetaFeed::instance().feed(_pimpl->renderer.get());
        // Everything past here for this frame is the renderer's own
        // account, which it times itself.
        outPre.stop();
        externalRendered =
            _pimpl->renderer->render(col, &viewMat.getValue(), &projMat.getValue());
        // Time-animated backend content (e.g. water caustics) keeps
        // advancing by itself: schedule the follow-up frame.
        if (externalRendered && _pimpl->renderer->animating())
            getSoRenderManager()->scheduleRedraw();
        if (!externalRendered) {
            // Backend failed this frame. Clear like the plain GL path so the
            // fixed-function renderer (not skipped in this case) draws on a
            // clean buffer.
            glClearColor(col.redF(), col.greenF(), col.blueF(), 0.0F);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    } else {
        glClearColor(col.redF(), col.greenF(), col.blueF(), 0.0F);
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
    // The external renderer's output (color + depth) is already in the
    // framebuffer, background included; the flat background fill would
    // erase it, and the gradient background node would repaint every pixel
    // still at the far plane — including transparent geometry, which does
    // not write depth. Suppress both for backend-rendered frames (the
    // suppress flag is reset right after so offscreen renders sharing the
    // node keep their background).
    {
        Render::FrameOutsideScope outBg(Render::FrameOutside::Background);
        if (!externalRendered)
            drawSingleBackground(col);
        pcBackGround->setSuppressed(externalRendered);
        glra->apply(this->backgroundroot);
        pcBackGround->setSuppressed(false);
    }

    SoBoxSelectionRenderAction *glbra = nullptr;
    if(glra->isOfType(SoBoxSelectionRenderAction::getClassTypeId())) {
        glbra = static_cast<SoBoxSelectionRenderAction*>(glra);
        glbra->checkRootNode(this->getSoRenderManager()->getSceneGraph());
    }
    // With an active backend the foreground superimposition, corner axis cross
    // and in-scene datums are drawn by the backend from its captured feeds;
    // keep the GL drawing for the plain path and for FC_RENDERER_PARALLEL_GL
    // comparison frames.
    static const bool parallelgl =
        (std::getenv("FC_RENDERER_PARALLEL_GL") != nullptr);

    // When the backend already draws the editing overlay (datums, constraint
    // icons), suppress the raw-GL datum and screen-space image draws during
    // this Coin pass so they are not doubled.
    SoDatumLabel::SuppressGLRender =
        externalRendered && _pimpl->editingBackendFed && !parallelgl;
    SoFCRenderCacheManager::SuppressImageGLRender =
        SoDatumLabel::SuppressGLRender;
    // * The sharp one. At render-cache mode 3 the geometry has already
    // gone to the backend above, so this traversal should be compositing
    // overlays and nothing else. If it is a large share of the frame it
    // is walking the whole scene graph for no pixels -- a bug, not a
    // cost. (Both actualRedraw() calls are inside the span: the retry
    // after an out-of-memory is still time this frame spent.)
    Render::FrameOutsideScope outCoin(Render::FrameOutside::Coin);
    try {
        // Render normal scenegraph.
        inherited::actualRedraw();
    }
    catch (const Base::MemoryException&) {
        // FIXME: If this exception appears then the background and camera position get broken somehow. (Werner 2006-02-01)
        for (auto it : _ViewProviderSet) {
            it->hide();
        }

        inherited::actualRedraw();
        QMessageBox::warning(parentWidget(), QObject::tr("Out of memory"),
                             QObject::tr("Not enough memory available to display the data."));
    }
    outCoin.stop();
    SoDatumLabel::SuppressGLRender = false;
    SoFCRenderCacheManager::SuppressImageGLRender = false;
    if (glbra) {
        glbra->checkRootNode(nullptr);
    }

#if defined (ENABLE_GL_DEPTH_RANGE)
    // using 10% of the z-buffer for the foreground node
    glDepthRange(0.0,0.1);
#endif

    // Render overlay front scenegraph.
    {
        Render::FrameOutsideScope outFg(Render::FrameOutside::Foreground);
        if (!externalRendered || parallelgl)
            glra->apply(this->foregroundroot);
    }

    // Compose the fps/stats readout before the overlay captures run so
    // the backend feed carries the current frame's numbers.
    Render::FrameOutsideScope outFps(Render::FrameOutside::Chrome);
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
        _pimpl->fpsText = stream.str();
    }
    else {
        _pimpl->fpsText.clear();
    }

    outFps.stop();
    {
        Render::FrameOutsideScope outCaps(Render::FrameOutside::Captures);
        if (_pimpl->renderer)
            _pimpl->updateOverlayCaptures(glra);
    }

    // The rest of the function: chrome, and the alpha fixup at the end.
    Render::FrameOutsideScope outChrome(Render::FrameOutside::Chrome);
    if (this->axiscrossEnabled && (!externalRendered || parallelgl)) {
        this->drawAxisCross();
    }

#if defined (ENABLE_GL_DEPTH_RANGE)
    // using the main portion of z-buffer again (for frontbuffer highlighting)
    glDepthRange(0.1,1.0);
#endif

    // The scene feed to the external renderer happens during the Coin
    // traversal above; if new data arrived there, render one more frame so
    // the backend output (drawn before the traversal) catches up.
    if (_pimpl->renderer && _pimpl->renderer->needsRedraw())
        this->getSoRenderManager()->scheduleRedraw();

    // A publish that spent its capture budget left shapes a frame stale
    // (Render CaptureBudgetMS); keep publishing until none defer. Each
    // follow-up publish captures at least one more shape, so this
    // converges, and going through the normal redraw keeps the event
    // loop serviced between passes -- which is the entire point.
    if (selectionRoot) {
        if (auto manager = selectionRoot->getRenderManager()) {
            if (manager->getDeferredCaptureCount() > 0)
                this->getSoRenderManager()->scheduleRedraw();
        }
    }

    // Immediately reschedule to get continuous animation.
    if (this->isAnimating()) {
        this->getSoRenderManager()->scheduleRedraw();
    } else
        _pimpl->onRender();

    printDimension();
    navigation->redraw();

    for (auto it : this->graphicsItems) {
        // Items ported to the backend's overlay feed (rubber band,
        // polyline) skip the GL painting on backend frames; items
        // without an overlay graph keep the GL path.
        if (externalRendered && !parallelgl && it->getOverlaySceneGraph())
            continue;
        it->paintGL();
    }

    //fps rendering (fed through the backend overlay on backend frames)
    if (fpsEnabled && (!externalRendered || parallelgl)) {
        draw2DString(_pimpl->fpsText.c_str(), SbVec2s(10, 10), SbVec2f(0.1F, 0.1F));  // NOLINT
    }

    // On backend frames the NaviCube rides the overlay feed
    // (updateOverlayCaptures above); picking stays on the GL pick pass.
    if (naviCubeEnabled && (!externalRendered || parallelgl)) {
        naviCube->drawNaviCube();
    }

    // Force the frame fully opaque: blended transparent geometry leaves
    // alpha < 1 in the framebuffer, and Wayland compositors (e.g. WSLg)
    // honor destination alpha, blending the window with whatever is behind
    // it (X11 ignores it, which is why this never showed there).
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glPopAttrib();

    if(restoreGradient) {
        setGradientBackground(grad);
    }
}

void View3DInventorViewer::setSeekMode(bool on)
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
    if (!cam) {
        return {0. ,0. ,0. };
    }

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

void View3DInventorViewer::getDimensions(float& fHeight, float& fWidth) const
{
    SoCamera* camera = getSoRenderManager()->getCamera();
    if (!camera) {
        // no camera there
        return;
    }

    float aspectRatio = getViewportRegion().getViewportAspectRatio();

    SoType type = camera->getTypeId();
    if (type.isDerivedFrom(SoOrthographicCamera::getClassTypeId())) {
        // NOLINTBEGIN
        fHeight = static_cast<SoOrthographicCamera*>(camera)->height.getValue();
        fWidth = fHeight;
        // NOLINTEND
    }
    else if (type.isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        // NOLINTBEGIN
        float fHeightAngle = static_cast<SoPerspectiveCamera*>(camera)->heightAngle.getValue();
        fHeight = std::tan(fHeightAngle / 2.0) * 2.0 * camera->focalDistance.getValue();
        fWidth = fHeight;
        // NOLINTEND
    }

    if (aspectRatio > 1.0) {
        fWidth *= aspectRatio;
    }
    else {
        fHeight *= aspectRatio;
    }
}

void View3DInventorViewer::printDimension() const
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

    for (auto it : _ViewProviderSet) {
        if (it->isDerivedFrom<ViewProviderDocumentObject>()) {
            auto vp = static_cast<ViewProviderDocumentObject*>(it);  // NOLINT
            App::DocumentObject* obj = vp->getObject();

            if (obj) {
                objs.push_back(obj);
            }
        }
    }

    if (!objs.empty()) {
        Gui::Selection().setSelection(objs);
    }
}

bool View3DInventorViewer::processSoEvent(const SoEvent* ev)
{
    // Every event the view sees is a person interacting with it; the redraw
    // throttle gets out of their way for the next little while.
    _pimpl->noteInput();

    if (naviCubeEnabled && naviCube->processSoEvent(ev)) {
        return true;
    }
    if (isRedirectedToSceneGraph()) {
        bool processed = inherited::processSoEvent(ev);

        if (!processed) {
            processed = navigation->processEvent(ev);
        }

        return processed;
    }

    if (ev->getTypeId().isDerivedFrom(SoKeyboardEvent::getClassTypeId())) {
        // filter out 'Q' and 'ESC' keys
        const auto ke = static_cast<const SoKeyboardEvent*>(ev);  // NOLINT

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

bool View3DInventorViewer::processSoEventBase(const SoEvent* const ev)
{
    return inherited::processSoEvent(ev);
}

SbVec3f View3DInventorViewer::getViewDirection() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam) {
        // this is the default
        return {0,0,-1};
    }

    SbVec3f projDir = cam->getViewVolume().getProjectionDirection();
    return projDir;
}

void View3DInventorViewer::setViewDirection(SbVec3f dir)
{
    if (SoCamera* cam = this->getSoRenderManager()->getCamera()) {
        cam->orientation.setValue(SbRotation(SbVec3f(0, 0, -1), dir));
    }
}

SbVec3f View3DInventorViewer::getUpDirection() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam) {
        return {0,1,0};
    }

    SbRotation camrot = cam->orientation.getValue();
    SbVec3f upvec(0, 1, 0); // init to default up vector
    camrot.multVec(upvec, upvec);
    return upvec;
}

SbRotation View3DInventorViewer::getCameraOrientation() const
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (!cam) {
        // this is the default
        return {0,0,0,1};
    }

    return cam->orientation.getValue();
}

SbVec2f View3DInventorViewer::getNormalizedPosition(const SbVec2s& pnt) const
{
    const SbViewportRegion& vp = this->getSoRenderManager()->getViewportRegion();

    short xpos{};
    short ypos{};
    pnt.getValue(xpos, ypos);
    SbVec2f siz = vp.getViewportSize();
    float dX{};
    float dY{};
    siz.getValue(dX, dY);

    float fRatio = vp.getViewportAspectRatio();
    float pX = float(xpos) / float(vp.getViewportSizePixels()[0]);
    float pY = float(ypos) / float(vp.getViewportSizePixels()[1]);

    // now calculate the real points respecting aspect ratio information
    //
    // NOLINTBEGIN
    if (fRatio > 1.0F) {
        pX = (pX - 0.5F * dX) * fRatio + 0.5F * dX;
    }
    else if (fRatio < 1.0F) {
        pY = (pY - 0.5F * dY) / fRatio + 0.5F * dY;
    }
    // NOLINTEND

    return {pX, pY};
}

namespace
{
SbVec3f projectPointOntoPlane(const SbVec3f& point, const SbPlane& plane)
{
    SbVec3f planeNormal = plane.getNormal();
    float d = plane.getDistanceFromOrigin();
    float distance = planeNormal.dot(point) + d;
    return point - planeNormal * distance;
}

/// Project a line onto a plane
SbLine projectLineOntoPlane(const SbVec3f& p1, const SbVec3f& p2, const SbPlane& plane)
{
    SbVec3f projectedPoint1 = projectPointOntoPlane(p1, plane);
    SbVec3f projectedPoint2 = projectPointOntoPlane(p2, plane);
    return SbLine(projectedPoint1, projectedPoint2);
}

SbVec3f lineIntersection(const SbVec3f& p11, const SbVec3f& p12,
                         const SbVec3f& p21, const SbVec3f& p22)
{
    SbVec3f da = p12 - p11;
    SbVec3f db = p22 - p21;
    SbVec3f dc = p21 - p11;

    double s = (dc.cross(db)).dot(da.cross(db)) / da.cross(db).sqrLength();
    return p11 + da * static_cast<float>(s);
}
}  // namespace

SbVec3f View3DInventorViewer::getPointOnXYPlaneOfPlacement(const SbVec2s& pnt,
                                                           const Base::Placement& plc) const
{
    SbVec2f pnt2d = getNormalizedPosition(pnt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam)
        THROWM(Base::RuntimeError, "No camera node found")

    SbViewVolume vol = pCam->getViewVolume();
    SbLine line;
    vol.projectPointToLine(pnt2d, line);

    // The plane is the XY plane of the given placement
    Base::Vector3d normalVector = plc.getRotation().multVec(Base::Vector3d(0, 0, 1));
    SbVec3f planeNormal = Base::convertTo<SbVec3f>(normalVector);
    SbVec3f planePosition = Base::convertTo<SbVec3f>(plc.getPosition());
    SbPlane xyPlane(planeNormal, planePosition);

    SbVec3f pt;
    if (xyPlane.intersect(line, pt))
        return pt;

    THROWM(Base::RuntimeError, "No intersection found")
}

SbVec3f View3DInventorViewer::getPointOnLine(const SbVec2s& pnt,
                                             const SbVec3f& axisCenter,
                                             const SbVec3f& axis) const
{
    SbVec2f pnt2d = getNormalizedPosition(pnt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam)
        return {};  // return invalid point

    // First we get pnt projection on the focal plane
    SbViewVolume vol = pCam->getViewVolume();

    float nearDist = pCam->nearDistance.getValue();
    float farDist = pCam->farDistance.getValue();
    float focalDist = pCam->focalDistance.getValue();

    if (focalDist < nearDist || focalDist > farDist)
        focalDist = 0.5F * (nearDist + farDist);

    SbLine line;
    SbVec3f ptOnFocalPlaneAndOnLine, ptOnFocalPlane;
    SbPlane focalPlane = vol.getPlane(focalDist);
    vol.projectPointToLine(pnt2d, line);
    focalPlane.intersect(line, ptOnFocalPlane);

    // Check if line is orthogonal to the focal plane
    SbVec3f focalPlaneNormal = focalPlane.getNormal();
    float dotProduct = fabs(axis.dot(focalPlaneNormal));
    if (dotProduct > (1.0 - 1e-6))
        return ptOnFocalPlane;

    SbLine projectedLine = projectLineOntoPlane(axisCenter, axisCenter + axis, focalPlane);
    ptOnFocalPlaneAndOnLine = projectedLine.getClosestPoint(ptOnFocalPlane);

    // Intersection between the line normal to focalPlane through
    // ptOnFocalPlaneAndOnLine, and the line (axisCenter, axisCenter + axis)
    return lineIntersection(ptOnFocalPlaneAndOnLine,
                            ptOnFocalPlaneAndOnLine + focalPlaneNormal,
                            axisCenter,
                            axisCenter + axis);
}

SbVec3f View3DInventorViewer::getPointOnFocalPlane(const SbVec2s& pnt) const
{
    SbVec2f pnt2d = getNormalizedPosition(pnt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam) {
        // return invalid point
        return {};
    }

    SbViewVolume  vol = pCam->getViewVolume();

    float nearDist = pCam->nearDistance.getValue();
    float farDist = pCam->farDistance.getValue();
    float focalDist = pCam->focalDistance.getValue();

    if (focalDist < nearDist || focalDist > farDist) {
        focalDist = 0.5F * (nearDist + farDist);  // NOLINT
    }

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

    auto xpos = short(std::roundf(pt[0] * sp[0]));  // NOLINT
    auto ypos = short(std::roundf(pt[1] * sp[1]));  // NOLINT

    return {xpos, ypos};
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

    return {xpos, ypos};
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
    if (!pCam) {
        // just do nothing
        return;
    }

    SbViewVolume vol = pCam->getViewVolume();

    // get the normal of the front clipping plane
    SbPlane nearPlane = vol.getPlane(vol.nearDist);
    float dist = nearPlane.getDistanceFromOrigin();
    rcNormal = nearPlane.getNormal();
    rcNormal.normalize();
    float nx{};
    float ny{};
    float nz{};
    rcNormal.getValue(nx, ny, nz);
    rcPt.setValue(dist * rcNormal[0], dist * rcNormal[1], dist * rcNormal[2]);
}

void View3DInventorViewer::getFarPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const
{
    SoCamera* pCam = getSoRenderManager()->getCamera();
    if (!pCam) {
        // just do nothing
        return;
    }

    SbViewVolume vol = pCam->getViewVolume();

    // get the normal of the back clipping plane
    SbPlane farPlane = vol.getPlane(vol.nearDist+vol.nearToFar);
    float dist = farPlane.getDistanceFromOrigin();
    rcNormal = farPlane.getNormal();
    rcNormal.normalize();
    float nx{};
    float ny{};
    float nz{};
    rcNormal.getValue(nx, ny, nz);
    rcPt.setValue(dist * rcNormal[0], dist * rcNormal[1], dist * rcNormal[2]);
}

SbVec3f View3DInventorViewer::projectOnNearPlane(const SbVec2f& pt) const
{
    SbVec3f pt1;
    SbVec3f pt2;
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    // return invalid point
    if (!cam) {
        return {};
    }

    SbViewVolume vol = cam->getViewVolume();
    vol.projectPointToLine(pt, pt1, pt2);
    return pt1;
}

SbVec3f View3DInventorViewer::projectOnFarPlane(const SbVec2f& pt) const
{
    SbVec3f pt1;
    SbVec3f pt2;
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    // return invalid point
    if (!cam) {
        return {};
    }

    SbViewVolume vol = cam->getViewVolume();
    vol.projectPointToLine(pt, pt1, pt2);
    return pt2;
}

void View3DInventorViewer::projectPointToLine(const SbVec2s& pt, SbVec3f& pt1, SbVec3f& pt2) const
{
    SbVec2f pnt2d = getNormalizedPosition(pt);
    SoCamera* pCam = this->getSoRenderManager()->getCamera();

    if (!pCam) {
        return;
    }

    SbViewVolume vol = pCam->getViewVolume();
    vol.projectPointToLine(pnt2d, pt1, pt2);
}

void View3DInventorViewer::toggleClippingPlane(int toggle, bool beforeEditing,
        bool noManip, const Base::Placement &pla)
{
    if (pcClipPlane) {
        if (toggle <= 0) {
            pcViewProviderRoot->removeChild(pcClipPlane);
            pcClipPlane->unref();
            pcClipPlane = nullptr;
        }
        return;
    }

    if (toggle == 0) {
        return;
    }

    Base::Vector3d dir;
    pla.getRotation().multVec(Base::Vector3d(0, 0, -1), dir);
    Base::Vector3d base = pla.getPosition();

    if (!noManip) {
        auto clip = new SoClipPlaneManip;
        pcClipPlane = clip;
        SbBox3f box = getBoundingBox();

        if (isValidBBox(box)) {
            // adjust to overall bounding box of the scene
            clip->setValue(box, Base::convertTo<SbVec3f>(dir), 1.0F);
        }
    }
    else {
        pcClipPlane = new SoClipPlane;
    }

    pcClipPlane->plane.setValue(SbPlane(Base::convertTo<SbVec3f>(dir),
                                        Base::convertTo<SbVec3f>(base)));
    pcClipPlane->ref();
    if (beforeEditing) {
        pcViewProviderRoot->insertChild(pcClipPlane, 0);
    }
    else {
        pcViewProviderRoot->insertChild(pcClipPlane,
                                        pcViewProviderRoot->findChild(pcEditingRoot) + 1);
    }
}

bool View3DInventorViewer::hasClippingPlane() const
{
    return pcClipPlane != nullptr;
}

/**
 * This method picks the closest point to the camera in the underlying scenegraph
 * and returns its location and normal.
 * If no point was picked false is returned.
 */
bool View3DInventorViewer::pickPoint(const SbVec2s& pos, SbVec3f& point, SbVec3f& norm) const
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

bool View3DInventorViewer::pubSeekToPoint(const SbVec2s& pos)
{
    return this->seekToPoint(pos);
}

void View3DInventorViewer::pubSeekToPoint(const SbVec3f& pos)
{
    this->seekToPoint(pos);
}

void View3DInventorViewer::setCameraOrientation(const SbRotation& orientation, bool moveToCenter)
{
    navigation->setCameraOrientation(orientation, moveToCenter);
}

void View3DInventorViewer::setCameraType(SoType type)
{
    inherited::setCameraType(type);

    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (!cam) {
        return;
    }

    if (type.isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        // When doing a viewAll() for an orthographic camera and switching
        // to perspective the scene looks completely strange because of the
        // heightAngle. Setting it to 45 deg also causes an issue with a too
        // close camera but we don't have this other ugly effect.
        static_cast<SoPerspectiveCamera*>(cam)->heightAngle = (float)(M_PI / 4.0);  // NOLINT
    }

    // The camera node itself was just replaced, so the fill light's rotation
    // has to be slaved to the new one.
    syncLightRotation();
}

void View3DInventorViewer::syncLightRotation()
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (!cam) {
        return;
    }
    lightRotation->rotation.disconnect();
    lightRotation->rotation.connectFrom(&cam->orientation);
}

void View3DInventorViewer::moveCameraTo(const SbRotation& orientation, const SbVec3f& position, int duration)
{
    SoCamera* camera = getCamera();
    if (!camera) {
        return;
    }

    if (isAnimationEnabled()) {
        startAnimation(
            orientation, camera->position.getValue(), position - camera->position.getValue(), duration, true);
    }

    camera->orientation.setValue(orientation);
    camera->position.setValue(position);
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

    bool res = box.IsValid() ? true : false;

    // Coin3D camera seems stuck if zoomed to close because the boundbox is too
    // small. So, we limit the boundbox size
    const double minLength = 1e-7;
    const double margin = 0.01;
    if (std::fabs(box.MinX - box.MaxX) < minLength) {
        box.MinX -= margin;
        box.MaxX += margin;
    }
    if (std::fabs(box.MinY - box.MaxY) < minLength) {
        box.MinY -= margin;
        box.MaxY += margin;
    }
    if (std::fabs(box.MinZ - box.MaxZ) < minLength) {
        box.MinZ -= margin;
        box.MaxZ += margin;
    }

    return res;
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
    if (!cam) {
        return;
    }

    SbVec3f campos = cam->position.getValue();
    SbRotation camrot = cam->orientation.getValue();
    SbViewportRegion vp = this->getSoRenderManager()->getViewportRegion();

    float aspectRatio = vp.getViewportAspectRatio();

    if (box.isEmpty()) {
        return;
    }

    SbSphere sphere;
    sphere.circumscribe(box);
    if (sphere.getRadius() == 0) {
        return;
    }

    SbVec3f direction;
    SbVec3f pos(0.0F, 0.0F, 0.0F);
    camrot.multVec(SbVec3f(0, 0, -1), direction);

    bool isOrthographic = false;
    float height = 0;
    float diff = 0;

    if (cam->isOfType(SoOrthographicCamera::getClassTypeId())) {
        isOrthographic = true;
        height = static_cast<SoOrthographicCamera*>(cam)->height.getValue();  // NOLINT
        if (aspectRatio < 1.0F) {
            diff = sphere.getRadius() * 2 - height * aspectRatio;
        }
        else {
            diff = sphere.getRadius() * 2 - height;
        }
        pos = (box.getCenter() - direction * sphere.getRadius());
    }
    else if (cam->isOfType(SoPerspectiveCamera::getClassTypeId())) {
        // NOLINTBEGIN
        float movelength = sphere.getRadius()/float(tan(static_cast<SoPerspectiveCamera*>
            (cam)->heightAngle.getValue() / 2.0));
        // NOLINTEND
        pos = box.getCenter() - direction * movelength;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    Base::StateLocker guard(_pimpl->animating);
    for (int i=0; i<steps; i++) {
        float par = float(i)/float(steps);

        if (isOrthographic) {
            float camHeight = height + diff * par;
            static_cast<SoOrthographicCamera*>(cam)->height.setValue(camHeight);  // NOLINT
        }

        SbVec3f curpos = campos * (1.0F - par) + pos * par;
        cam->position.setValue(curpos);
        timer.start(Base::clamp<int>(ms, 0, ViewParams::getMaxCameraAnimatePeriod())); // NOLINT
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
    if(!getSceneBoundBox(box)) {
        return;
    }

    _pimpl->updateShadowExtent(box);

    // Set the height angle to 45 deg
    SoCamera* cam = this->getSoRenderManager()->getCamera();

    if (cam && cam->getTypeId().isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        static_cast<SoPerspectiveCamera*>(cam)->heightAngle = (float)(M_PI / 4.0);  // NOLINT
    }

    viewBoundBox(box);
}

/// Re-fit the Coin shadow light and its filter to the scene extent.
/// The ground quad used to be sized here too, and to widen the extent
/// the spot light's spread is scaled against; the backend owns it now
/// (stage 4b), and it sizes its own quad from the same bounding box.
void View3DInventorViewer::Private::updateShadowExtent(const SbBox3f &box)
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

    const auto shadowParams = Gui::materializeShadowRenderParams(view);
    double smoothBorder = double(shadowParams.smoothBorder);
    double spread = double(shadowParams.spreadSize);
    double sample = double(shadowParams.spreadSampleSize);

    float maxSize = std::max(size[0],std::max(size[1],size[2]));
    if (maxSize > 256.0 && pcShadowGroup->findChild(pcShadowSpotLight)>=0)
        spread *= 256.0/maxSize;
    pcShadowGroup->smoothBorder = smoothBorder/10.0f + sample/100.0f + spread/1000000.0f;
}

void View3DInventorViewer::viewAll(float factor)
{
    SoCamera* cam = this->getSoRenderManager()->getCamera();
    if (!cam) {
        return;
    }

    if (factor <= 0.0F) {
        return;
    }

    if (factor != 1.0F) {
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

/**
 * @brief Decide if it should be possible to start any animation
 *
 * If the enable flag is false and we're currently animating, the animation will be stopped
 */
void View3DInventorViewer::setAnimationEnabled(bool enable)
{
    navigation->setAnimationEnabled(enable);
}

/**
 * @brief Decide if it should be possible to start a spin animation of the model in the viewer by releasing the mouse button while dragging
 *
 * If the enable flag is false and we're currently animating, the spin animation will be stopped
 */
void View3DInventorViewer::setSpinningAnimationEnabled(bool enable)
{
    navigation->setSpinningAnimationEnabled(enable);
}

/**
 * @return Whether or not it is possible to start any animation
 */
bool View3DInventorViewer::isAnimationEnabled() const
{
    return navigation->isAnimationEnabled();
}

/**
 * @return Whether or not it is possible to start a spinning animation e.g. after dragging
 */
bool View3DInventorViewer::isSpinningAnimationEnabled() const
{
    return navigation->isSpinningAnimationEnabled();
}

/**
 * @return Whether or not any animation is currently active
 */
bool View3DInventorViewer::isAnimating() const
{
    return navigation->isAnimating();
}

/**
 * @return Whether or not a spinning animation is currently active e.g. after a user drag
 */
bool View3DInventorViewer::isSpinning() const
{
    return navigation->isSpinning();
}

/**
 * @brief Change the camera pose with an animation
 *
 * @param orientation The new orientation
 * @param rotationCenter The rotation center
 * @param translation An additional translation on top of the translation caused by the rotation around the rotation center
 * @param duration The duration in milliseconds
 * @param wait When false, start the animation and continue (asynchronous). When true, start the animation and wait for the animation to finish (synchronous)
 */
void View3DInventorViewer::startAnimation(const SbRotation& orientation,
                                          const SbVec3f& rotationCenter,
                                          const SbVec3f& translation,
                                          int duration,
                                          bool wait)
{
    // Currently starts a FixedTimeAnimation. If there is going to be an additional animation like
    // FixedVelocityAnimation, check the animation type from a parameter and start the right animation

    // Duration was not set or is invalid so use the AnimationDuration parameter as default
    if (duration < 0) {
        duration = App::GetApplication()
                       .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
                       ->GetInt("AnimationDuration", 250);
    }

    auto animation = std::make_shared<FixedTimeAnimation>(
        navigation, orientation, rotationCenter, translation, duration);

    navigation->startAnimating(animation, wait);
}

/**
 * @brief Start an infinite spin animation
 *
 * @param axis The rotation axis in screen coordinates
 * @param velocity The angular velocity in radians per second
 */
void View3DInventorViewer::startSpinningAnimation(const SbVec3f& axis, float velocity)
{
    auto animation = std::make_shared<SpinningAnimation>(navigation, axis, velocity);
    navigation->startAnimating(animation);
}

void View3DInventorViewer::stopAnimating()
{
    navigation->stopAnimating();
}

void View3DInventorViewer::setPopupMenuEnabled(bool on)
{
    navigation->setPopupMenuEnabled(on);
}

bool View3DInventorViewer::isPopupMenuEnabled() const
{
    return navigation->isPopupMenuEnabled();
}

/*!
  Set the flag deciding whether or not to show the axis cross.
*/

void
View3DInventorViewer::setFeedbackVisibility(bool enable)
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

bool
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
View3DInventorViewer::setFeedbackSize(int size)
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
void View3DInventorViewer::setCursorEnabled(bool /*enable*/)
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
void View3DInventorViewer::setViewing(bool enable)
{
    if (this->isViewing() == enable) {
        return;
    }

    navigation->setViewingMode(enable ? NavigationStyle::IDLE : NavigationStyle::INTERACT);
    inherited::setViewing(enable);
}

void View3DInventorViewer::drawAxisCross()
{
    // NOLINTBEGIN
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
    const int pixelarea = int(float(this->axiscrossSize)/100.0F * std::min(view[0], view[1]));
    SbVec2s origin(view[0] - pixelarea, 0);
    glViewport(origin[0], origin[1], pixelarea, pixelarea);

    // Set up the projection matrix.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float NEARVAL = 0.1F;
    const float FARVAL = 10.0F;
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
                    glColor3f(0.500F, 0.5F, 0.5F);
                else
                    glColor3f(0.500F, 0.125F, 0.125F);
            }
            else if (i == YAXIS) {                   // Y axis.
                glRotatef(90, 0, 0, 1);

                if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
                    glColor3f(0.400F, 0.4F, 0.4F);
                else
                    glColor3f(0.125F, 0.500F, 0.125F);
            }
            else {                                        // Z axis.
                glRotatef(-90, 0, 1, 0);

                if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO)
                    glColor3f(0.300F, 0.3F, 0.3F);
                else
                    glColor3f(0.125F, 0.125F, 0.500F);
            }

            drawArrow();
            glPopMatrix();
        }
    }

    // Render axis notation letters ("X", "Y", "Z").
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, view[0], 0, view[1], -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLint unpack{};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (stereoMode() != Quarter::SoQTQuarterAdaptor::MONO) {
        glColor3fv(SbVec3f(1.0F, 1.0F, 1.0F).getValue());
    }
    else {
        glColor3fv(SbVec3f(0.0F, 0.0F, 0.0F).getValue());
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelZoom((float)axiscrossSize / 30, (float)axiscrossSize / 30); // 30 = 3 (character pixmap ratio) * 10 (default axiscrossSize)
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
    // NOLINTEND
}

// Draw an arrow for the axis representation directly through OpenGL.
void View3DInventorViewer::drawArrow()
{
    // NOLINTBEGIN
    glDisable(GL_CULL_FACE);
    glBegin(GL_QUADS);
    glVertex3f(0.0F, -0.02F, 0.02F);
    glVertex3f(0.0F, 0.02F, 0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.02F, 0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.02F, 0.02F);

    glVertex3f(0.0F, -0.02F, -0.02F);
    glVertex3f(0.0F, 0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.02F, -0.02F);

    glVertex3f(0.0F, -0.02F, 0.02F);
    glVertex3f(0.0F, -0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.02F, 0.02F);

    glVertex3f(0.0F, 0.02F, 0.02F);
    glVertex3f(0.0F, 0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.02F, -0.02F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.02F, 0.02F);

    glVertex3f(0.0F, 0.02F, 0.02F);
    glVertex3f(0.0F, 0.02F, -0.02F);
    glVertex3f(0.0F, -0.02F, -0.02F);
    glVertex3f(0.0F, -0.02F, 0.02F);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex3f(1.0F, 0.0F, 0.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, +0.5F / 4.0F, 0.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.5F / 4.0F, 0.0F);
    glVertex3f(1.0F, 0.0F, 0.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.0F, +0.5F / 4.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.0F, -0.5F / 4.0F);
    glEnd();
    glBegin(GL_QUADS);
    glVertex3f(1.0F - 1.0F / 3.0F, +0.5F / 4.0F, 0.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.0F, +0.5F / 4.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, -0.5F / 4.0F, 0.0F);
    glVertex3f(1.0F - 1.0F / 3.0F, 0.0F, -0.5F / 4.0F);
    glEnd();
    // NOLINTEND
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
    glColor3f(float(col.redF()), float(col.greenF()), float(col.blueF()));
    glVertex2f(-1, 1);
    glColor3f(float(col.redF()), float(col.greenF()), float(col.blueF()));
    glVertex2f(-1, -1);
    glColor3f(float(col.redF()), float(col.greenF()), float(col.blueF()));
    glVertex2f(1, 1);
    glColor3f(float(col.redF()), float(col.greenF()), float(col.blueF()));
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
    if (glWindow) {
        glWindow->setAttribute(Qt::WA_SetCursor, false);
    }

    if (glWindow && glWindow->rect().contains(QCursor::pos())) {
        glWindow->setAttribute(Qt::WA_UnderMouse);
    }

    QWidget *widget = QWidget::mouseGrabber();
    if (!widget)
        widget = this->getWidget();

    switch (modearg) {
    case NavigationStyle::IDLE:
    case NavigationStyle::INTERACT:
        if (isEditing()) {
            widget->setCursor(this->editCursor);
        }
        else {
            widget->setCursor(QCursor(Qt::ArrowCursor));
        }
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

void View3DInventorViewer::setEditing(bool edit)
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
        std::string str = vp->getElement(pp->getDetail());
        vp->getSelectionShape(str.c_str());
        static char buf[513];
        snprintf(buf,
                 sizeof(buf),
                 "Hovered: %s (%f,%f,%f)"
                 , str.c_str()
                 , pp->getPoint()[0]
                 , pp->getPoint()[1]
                 , pp->getPoint()[2]);

        getMainWindow()->showMessage(QString::fromUtf8(buf), 3000);
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
    coinRemoveAllChildren(static_cast<SoSwitch*>(dimensionRoot->getChild(0)));  // NOLINT
    coinRemoveAllChildren(static_cast<SoSwitch*>(dimensionRoot->getChild(1)));  // NOLINT
}

void View3DInventorViewer::turn3dDimensionsOn()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->whichChild = SO_SWITCH_ALL;  // NOLINT
}

void View3DInventorViewer::turn3dDimensionsOff()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->whichChild = SO_SWITCH_NONE;  // NOLINT
}

void View3DInventorViewer::addDimension3d(SoNode* node)
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(0))->addChild(node);  // NOLINT
}

void View3DInventorViewer::addDimensionDelta(SoNode* node)
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->addChild(node);  // NOLINT
}

void View3DInventorViewer::turnDeltaDimensionsOn()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->whichChild = SO_SWITCH_ALL;  // NOLINT
}

void View3DInventorViewer::turnDeltaDimensionsOff()
{
    static_cast<SoSwitch*>(dimensionRoot->getChild(1))->whichChild = SO_SWITCH_NONE;  // NOLINT
}

PyObject *View3DInventorViewer::getPyObject()
{
    if (!_viewerPy) {
        _viewerPy = new View3DInventorViewerPy(this);
    }

    Py_INCREF(_viewerPy);
    return _viewerPy;
}

/**
 * Drops the event \a e and loads the files into the given document.
 */
void View3DInventorViewer::dropEvent (QDropEvent* ev)
{
    const QMimeData* data = ev->mimeData();
    if (data->hasUrls() && guiDocument) {
        getMainWindow()->loadUrls(guiDocument->getDocument(), data->urls());
    }
    else {
        inherited::dropEvent(ev);
    }
}

void View3DInventorViewer::dragEnterEvent (QDragEnterEvent* ev)
{
    // Here we must allow uri drags and check them in dropEvent
    const QMimeData* data = ev->mimeData();
    if (data->hasUrls()) {
        ev->accept();
    }
    else {
        inherited::dragEnterEvent(ev);
    }
}

void View3DInventorViewer::dragMoveEvent(QDragMoveEvent* ev)
{
    const QMimeData* data = ev->mimeData();
    if (data->hasUrls() && guiDocument) {
        ev->accept();
    }
    else {
        inherited::dragMoveEvent(ev);
    }
}

void View3DInventorViewer::dragLeaveEvent(QDragLeaveEvent* ev)
{
    inherited::dragLeaveEvent(ev);
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

bool View3DInventorViewer::Private::deferRedraw()
{
    const qint64 interval = ViewParams::getLiveImportRedrawInterval();
    if (interval <= 0)
        return false;

    // Only a document that a live operation is filling behind the user's back
    // produces frames nobody asked for. Everything else — including a normal
    // recompute, which redraws once at the end — renders as it always did.
    auto doc = owner->guiDocument ? owner->guiDocument->getDocument() : nullptr;
    if (!doc || !doc->testStatus(App::Document::LiveImport))
        return false;

    // The interval alone is a poor throttle, because it is not what grows
    // with the model: a frame does. Every new object invalidates the render
    // cache of the whole scene, so on a large import a single frame costs
    // ~100ms and the view already redraws at only a few frames per second —
    // a fixed 200ms floor removes almost nothing. Budgeting instead: let the
    // view spend at most `budget` percent of the time drawing, which means
    // waiting cost * (100 - budget) / budget between frames. The wait scales
    // itself, disappearing on scenes whose frames are cheap.
    const qint64 budget = ViewParams::getLiveImportRedrawBudget();
    qint64 gap = interval;
    if (budget > 0 && budget < 100 && frameCostMs > 0.0) {
        auto want = qint64(frameCostMs * double(100 - budget) / double(budget));
        // ...but never leave the view without a frame for an absurd stretch,
        // however expensive frames have become
        gap = std::max(gap, std::min(want, interval * 10));
    }

    const qint64 now = throttleClock.elapsed();

    // Someone is working the mouse: that person is waiting for this frame, so
    // it is not the one to drop. A drag keeps re-arming this window, so it
    // runs at full frame rate while the import grows the scene underneath it;
    // the window is the plain interval, not the budgeted gap, so one stray
    // mouse move does not lift the throttle for seconds.
    if (lastInputMs && now - lastInputMs < interval) {
        lastRedrawMs = now;
        return false;
    }

    if (now - lastRedrawMs >= gap) {
        lastRedrawMs = now;
        return false;
    }

    if (!throttleTimer.isActive())
        throttleTimer.start(int(gap - (now - lastRedrawMs)));
    return true;
}

void View3DInventorViewer::redraw(bool force)
{
    if (!force && _pimpl && _pimpl->deferRedraw())
        return;
    inherited::redraw(force);
}

void View3DInventorViewer::Private::redraw()
{
    if (animating) {
        timer.start(100);
        return;
    }
    timer.stop();
    SoCamera* cam = owner->getSoRenderManager()->getCamera();
    if(pcShadowGroup && cam) {
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
            updateShadowExtent(bbox);
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

void Gui::applySectionHatchTexture(SoFCRenderCacheManager &manager,
                                   App::PropertyContainer *view)
{
    QString path = QString::fromUtf8(
            Gui::sectionStyle(view, "HatchTexture",
                              ViewParams::getSectionHatchTexture()).c_str());
    QDateTime date;
    auto &entry = _HatchTextures[path];
    if (!path.startsWith(QLatin1Char(':'))) {
        QFileInfo finfo(path);
        if (!finfo.exists()) {
            manager.setHatchImage(nullptr,0,0,0);
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
    manager.setHatchImage(dataptr,nc,size[0],size[1]);
}

void View3DInventorViewer::updateHatchTexture()
{
    if (auto manager = selectionRoot->getRenderManager()) {
        Gui::applySectionHatchTexture(*manager, _pimpl->view);
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
