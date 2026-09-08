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


#ifndef GUI_VIEW3DINVENTORVIEWER_H
#define GUI_VIEW3DINVENTORVIEWER_H

#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <unordered_map>

#include <QCursor>
#include <QImage>

#include <Inventor/SbRotation.h>
#include <Inventor/SbTime.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoSwitch.h>

#include <App/DocumentObserver.h>
#include <Base/BoundBox.h>
#include <Base/Placement.h>

#include "Namespace.h"
#include "Selection.h"
#include "InventorBase.h"
#include "Inventor/SoFCDisplayModeElement.h"
#include "View3DInventorSelection.h"
#include "ViewerContext.h"
#include "Quarter/SoQTQuarterAdaptor.h"

class SoTranslation;
class SoTransform;
class SoText2;
class SoGetBoundingBoxAction;
class SoFCRenderCacheManager;
namespace Render::Cycles { struct RenderReport; struct ViewportOptions; struct ViewportStatus; struct SceneInput; }

class SoSeparator;
class SoDetail;
class SoShapeHints;
class SoMaterial;
class SoRotationXYZ;
class SoRotation;
class SoEnvironment;
class SbSphereSheetProjector;
class SoEventCallback;  // NOLINT
class SbBox2s;
class SoVectorizeAction;
class QImage;
class SoGroup;  // NOLINT
class SoPickStyle;
class NaviCube;
class SoClipPlane;
class SoTimerSensor;
class SoSensor;
class SbBox3f;
class SoFCSwitch;
class SoFCDisplayMode;

namespace Quarter = SIM::Coin3D::Quarter;

namespace Render {
class Renderer;
struct StyleOverrideTable;
struct CaptureInterestTable;
}

namespace App {
class PropertyContainer;
}

namespace Gui {

/// Whether a render property states what the machine can afford rather
/// than what the model should look like - the sampling and budget dials
/// and the debug instrumentation. Those are never saved with a document,
/// and a saved view must not carry them either: it would hand its reader
/// somebody else's hardware.
GuiExport bool isLocalRenderProperty(const char *name);

class ViewProvider;
class SoFCBackgroundGradient;
class NavigationStyle;
class SoFCUnifiedSelection;
class SoFCSelectionRoot;
class SoFCPathAnnotation;
class SoSelectionElementAction;
class SoHighlightElementAction;
class SoFCSelectionAction;
class SoFCHighlightAction;
class SoFCPathAnnotation;
class Document;
class GLGraphicsItem;
class SoShapeScale;
class ViewerEventFilter;
class AbstractMouseSelection;

/** GUI view into a 3D scene provided by View3DInventor
 *
 */
class GuiExport View3DInventorViewer : public Quarter::SoQTQuarterAdaptor,
                                      public SelectionObserver,
                                      public ViewerContext
{
    using inherited = Quarter::SoQTQuarterAdaptor;
    Q_OBJECT

public:
    /// Pick modes for picking points in the scene
    enum SelectionMode {
        Lasso       = 0,  /**< Select objects using a lasso. */
        Rectangle   = 1,  /**< Select objects using a rectangle. */
        Rubberband  = 2,  /**< Select objects using a rubberband. */
        BoxZoom     = 3,  /**< Perform a box zoom. */
        Clip        = 4,  /**< Clip objects using a lasso. */
    };
    /** @name Modus handling of the viewer
      * Here you can switch several features on/off
      * and modes of the Viewer
      */
    //@{
    enum ViewerMod {
        ShowCoord=1,       /**< Enables the Coordinate system in the corner. */
        ShowFPS  =2,       /**< Enables the Frames Per Second counter. */
        SimpleBackground=4,/**< switch to a simple background. */
        DisallowRotation=8,/**< switch off the rotation. */
        DisallowPanning=16,/**< switch off the panning. */
        DisallowZooming=32,/**< switch off the zooming. */
    };
    //@}

    /** @name Anti-Aliasing modes of the rendered 3D scene
      * Specifies Anti-Aliasing (AA) method
      * - Smoothing enables OpenGL line and vertex smoothing (basically deprecated)
      * - MSAA is hardware multi sampling (with 2, 4 or 8 passes), a quite common and efficient AA technique
      */
    //@{
    enum AntiAliasing {
        None,
        Smoothing,
        MSAA2x,
        MSAA4x,
        MSAA8x
    };
    //@}

    /** @name Render mode
      */
    //@{
    enum RenderType {
        Native,
        Framebuffer,
        Image
    };
    //@}

    /** @name Background
      */
    //@{
    enum Background {
        NoGradient,
        LinearGradient,
        RadialGradient
    };
    //@}

    explicit View3DInventorViewer (QWidget *parent, const QtGLWidget* sharewidget = nullptr);
    View3DInventorViewer (const QtGLFormat& format, QWidget *parent, const QtGLWidget* sharewidget = nullptr);
    ~View3DInventorViewer() override;

    void init();

    /** @name ViewerContext rows that Quarter already answers
     *
     * These exist on QuarterWidget and SoQTQuarterAdaptor as plain members.
     * Redeclaring them here is what makes them overrides of the ViewerContext
     * virtuals -- and what keeps the name unambiguous, since it is now
     * reachable through two bases. The bodies forward; there is no behaviour
     * here.
     */
    //@{
    SoNode* getSceneGraph() const override;
    SoRenderManager* getSoRenderManager() const override;
    SoEventManager* getSoEventManager() const override;
    const SbViewportRegion& getViewportRegion() const override;
    float getPickRadius() const override;
    double devicePixelRatio() const override;
    QWidget* getWidget() const override;
    QWidget* getGLWidget() const override;
    /// Non-const overloads Quarter offers, kept reachable past the redeclaration above.
    QWidget* getWidget();
    QWidget* getGLWidget();
    /// Whether a mouse button is down, from this view's own event handling.
    bool isMouseButtonDown() const override;
    void setFocusToView() override;
    static View3DInventorViewer* fromEventCallback(const SoEventCallback* node);
    //@}

    /// Observer message from the Selection
    void onSelectionChanged(const SelectionChanges &Reason) override;

    void checkGroupOnTop(const SelectionChanges &Reason, bool alt=false);
    void refreshGroupOnTop();
    void clearGroupOnTop(bool alt=false);

    bool isInGroupOnTop(const App::SubObjectT &objT, bool altOnly=true) const;
    bool hasOnTopObject() const;
    const std::set<App::SubObjectT> &getObjectsOnTop() const;

    SoDirectionalLight* getBacklight() const;
    void setBacklightEnabled(bool on);
    bool isBacklightEnabled() const;
    /// The third of the three-point rig: a fill light that tracks the camera.
    SoDirectionalLight* getFillLight() const;
    void setFillLightEnabled(bool on);
    bool isFillLightEnabled() const;
    /// Carries the scene's ambient light (GL's LIGHT_MODEL_AMBIENT).
    SoEnvironment* getEnvironment() const;
    /// Slave the fill light's rotation to the current camera's orientation.
    void syncLightRotation();
    void setSceneGraph (SoNode *root) override;
    bool searchNode(SoNode*) const;

    void setAnimationEnabled(bool enable);
    void setSpinningAnimationEnabled(bool enable);
    bool isAnimationEnabled() const;
    bool isSpinningAnimationEnabled() const;
    bool isAnimating() const;
    bool isSpinning() const;
    void startAnimation(const SbRotation& orientation, const SbVec3f& rotationCenter,
                        const SbVec3f& translation, int duration = -1, bool wait = false);
    void startSpinningAnimation(const SbVec3f& axis, float velocity);
    void stopAnimating();

    void setPopupMenuEnabled(bool on);
    bool isPopupMenuEnabled() const;

    void setFeedbackVisibility(bool enable);
    bool isFeedbackVisible() const;

    void setFeedbackSize(int size);
    int getFeedbackSize() const;

    /// Get the preferred samples from the user settings
    static int getNumSamples();
    /// The multisampling THIS viewer draws with: the user preference,
    /// unless the viewer has been given a count of its own.
    int numSamples() const;
    /// Fix this viewer's multisampling, whatever the preference says.
    ///
    /// For a viewer whose picture is an artifact rather than a view of
    /// the user's model: the material icons are rendered by one and ship
    /// in the binary, so what it draws must not move when the
    /// AntiAliasing default does. A negative value gives the preference
    /// back.
    void setNumSamples(int samples);
    void setRenderType(RenderType type);
    RenderType getRenderType() const;
    void renderToFramebuffer(QtGLFramebufferObject*);
    QImage grabFramebuffer();
    void imageFromFramebuffer(int width, int height, int samples,
                              const QColor& bgcolor, QImage& img);
    /// Pump paint events until an armed one-shot frame dump has been
    /// consumed by a rendered frame (docs/RenderDebug.md §4.2); false on
    /// timeout, or if the renderer was replaced while pumping. The
    /// backend holds the dump while a user shader is still compiling,
    /// or the scene is still arriving under the capture budget, and
    /// the timeout waits with it (5 s quiet, 120 s in all).
    bool pumpFrameDump(Render::Renderer *renderer);
    /** Pump frames until the external backend has rendered a COMPLETE
     * one since this call (Render::Renderer::frameComplete: every user
     * shader compiled, every deferred shape arrived, a frozen frame's
     * particle warm-up reached, no mesh refine outstanding) -- the
     * picture, rather than a number of frames. The built-in signal a
     * test settles on; frameCompleted() is the same event as a Qt
     * signal. The quiet timeout (5 s) restarts on every frame rendered
     * and every pending compile, under \a timeoutMs in all. Without a
     * backend one frame is rendered and that is the picture. False on
     * timeout, or if the renderer was replaced while pumping.
     */
    bool waitFrameComplete(int timeoutMs = 120000);
    /// Capture the frame through the render backend's own one-shot dump
    /// rather than an offscreen Coin render, which cannot see what the
    /// backend drew. False when there is no backend or it has no capture
    /// path, and the caller falls back to the Coin route.
    /// \a waitComplete: the dump is consumed only by a complete frame
    /// (Render::FrameDumpRequest::waitComplete); false takes the next
    /// frame as it stands.
    bool imageFromRenderer(int width, int height, const QColor& bgcolor,
                           QImage& img, bool waitComplete = true);

    void setViewing(bool enable) override;
    virtual void setCursorEnabled(bool enable);

    void addGraphicsItem(GLGraphicsItem*);
    void removeGraphicsItem(GLGraphicsItem*);
    std::list<GLGraphicsItem*> getGraphicsItems() const;
    std::list<GLGraphicsItem*> getGraphicsItemsOfType(const Base::Type&) const;
    void clearGraphicsItems();

    /** @name Handling of view providers */
    //@{
    /// Checks if the view provider is a top-level object of the scene
    bool hasViewProvider(ViewProvider*) const;
    /// Checks if the view provider is part of the scene.
    /// In contrast to hasViewProvider() this method also checks if the view
    /// provider is a child of another view provider
    bool containsViewProvider(const ViewProvider*) const;
    /// adds an ViewProvider to the view, e.g. from a feature
    void addViewProvider(ViewProvider*);
    /// remove a ViewProvider
    void removeViewProvider(ViewProvider*);
    /// Check viewprovider to see if it should be added to or removed from scene graph root
    void toggleViewProvider(ViewProvider*);
    /// get view provider by path
    ViewProvider* getViewProviderByPath(SoPath*) const;
    ViewProvider* getViewProviderByPathFromTail(SoPath*) const;
    /// get all view providers of given type
    std::vector<ViewProvider*> getViewProvidersOfType(const Base::Type& typeId) const;
    /// set the ViewProvider in special edit mode
    void setEditingViewProvider(Gui::ViewProvider* vp, int ModNum);
    /// return whether a view provider is edited
    bool isEditingViewProvider() const;
    /// reset from edit mode
    void resetEditingViewProvider();
    void setupEditingRoot(SoNode *node=nullptr, const Base::Matrix4D *mat=nullptr);
    void resetEditingRoot(bool updateLinks=true);
    void setEditingTransform(const Base::Matrix4D &mat);
    SoSeparator * getEditRootNode() const { return pcEditingRoot; }
    /** Helper method to get picked entities while editing.
     * It's in the responsibility of the caller to delete the returned instance.
     */
    SoPickedPoint* getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const;
    /** Helper method to get picked entities while editing.
     * It's in the responsibility of the caller to delete the returned instance.
     */
    SoPickedPoint* getPointOnRay(const SbVec3f& pos, const SbVec3f& dir, const ViewProvider* vp) const;
    /// display override mode
    void setOverrideMode(const std::string &mode);
    void applyOverrideMode();
    std::string getOverrideMode() const {return overrideMode;}
    /// This viewer's display style as a Render::DrawStyleMask, or
    /// Render::StyleAsIs when the style is not one the backend can draw
    /// by filtering (docs/CoinRetirement.md 5.7). What a unified canvas
    /// puts on its SubViewFrame so each cell draws the shared scene in
    /// its own style.
    unsigned char drawStyleMask() const
    { return drawStyleMaskFromName(overrideMode.c_str()); }
    /// The mask a display style name filters with. Only the four
    /// Class-A styles have one: Hidden Line, No Shading and
    /// Tessellation are not bucket selections, they are extra traversal
    /// state, so they stay with the Coin traversal and report
    /// StyleAsIs.
    static unsigned char drawStyleMaskFromName(const char *mode);
    /// This viewer's display style as a StyleNameBit, for the backend
    /// to tell whether an object's display-mode switch has a child of
    /// that style's name (docs/CoinRetirement.md 5.8).
    unsigned char drawStyleNameBit() const;
    /// This viewer's display style as an interned mode id
    /// (Render::internModeName), for the backend to resolve the style
    /// from the mode's own ADDITIVELY captured draws rather than from a
    /// mask over the superset child (docs/CoinRetirement.md 5.11).
    /// Zero unless the capture IS a superset capture and the style is
    /// one of the four Class-A names -- outside those the traversal
    /// applies the style itself and there is nothing to resolve.
    uint16_t drawStyleModeId() const;
    /// The display mode name this viewer's traversal CAPTURES with:
    /// its own style, none, or the superset child -- see
    /// setCanvasStyleMode().
    const char *captureOverrideMode() const;
    /// What a unified canvas needs this viewer's traversal to capture,
    /// because one canvas draws ONE resident scene through ONE
    /// traversal and its cells may be in different display styles
    /// (docs/CoinRetirement.md 5.7, 5.8; docs/SplitViews.md sec 17).
    enum CanvasStyleMode : unsigned char {
        /// Not on a canvas, or on one whose claimed cells all share a
        /// style: capture with that style, exactly as a plain view.
        CanvasStyleOff,
        /// Capture every object in its OWN display mode; the cells
        /// filter buckets out of it. Only usable where a filter and an
        /// override cannot be told apart.
        CanvasStyleFilter,
        /// Capture the SUPERSET display-mode child, so a cell can be
        /// served a style that ADDS geometry the object's own mode does
        /// not draw. The cells resolve their style per object.
        CanvasStyleSuperset,
    };
    /// Re-applies the override, which is what dirties the capture.
    void setCanvasStyleMode(CanvasStyleMode mode);
    CanvasStyleMode canvasStyleMode() const;
    /// This view's per-object display mode overrides
    /// (docs/CoinRetirement.md 5.9), parsed from View3DInventor's
    /// ObjectDisplayModes property into backend entries. Takes
    /// ownership, bumps the table's version, and re-applies the
    /// override mode: a view with overrides captures the SUPERSET
    /// child even outside a canvas, because an override can ADD
    /// geometry the object's own mode does not draw.
    void setObjectStyleOverrides(Render::StyleOverrideTable &&table);
    /// The table above, or null when it is empty. The pointer stays
    /// valid for the viewer's lifetime; a unified canvas puts it on
    /// its SubViewFrame, the plain frame states it through
    /// Renderer::setMainViewStyle.
    const Render::StyleOverrideTable *objectStyleOverrides() const;
    /// Whether overrides exist AND this viewer's style can host them:
    /// the backend resolves them over a superset capture, which only
    /// an "As Is" or Class-A styled view produces (Hidden Line, No
    /// Shading and Tessellation are traversal state, not bucket
    /// selections, and keep their plain capture).
    bool hasObjectStyleOverrides() const;
    /// Additive-mode interest imposed by a unified canvas
    /// (docs/CoinRetirement.md 5.9 "Non-standard modes", 5.10): the
    /// UNION of every cell's override modes AND of the cells' own
    /// display STYLE names, as interned ids
    /// (Render::internModeName). The shared capture must traverse the
    /// union whichever cell feeds it, so the canvas imposes it on
    /// every claimed viewer; an empty vector (the default, and what a
    /// cell leaving the canvas is reset to) leaves the viewer's own
    /// overrides as the only interest source.
    void setImposedCaptureInterest(std::vector<uint16_t> ids);
    /// The capture's interest list handed to the backend
    /// (Renderer::setCaptureInterest), or null when empty. Built from
    /// this view's own non-standard override modes plus the imposed
    /// set, in the SAME order as the list pushed to the traversal --
    /// the order is the interestBits bit assignment.
    const Render::CaptureInterestTable *captureInterestTable() const;
    const SoFCDisplayModeElement::HiddenLineConfig &getHiddenLineConfig() const;
    //@}

    /** @name Making pictures */
    //@{
    /**
     * Creates an image with width \a width and height \a height of the current scene graph
     * using a multi-sampling of \a sample and exports the rendered scenegraph to an image.
     */
    void savePicture(int width, int height, int sample, const QColor& bg, QImage& img,
                     bool waitComplete = true) const;
    void saveGraphic(int pagesize, const QColor&, SoVectorizeAction* va) const;
    //@}
    /**
     * Writes the current scenegraph to an Inventor file, either in ascii or binary.
     */
    bool dumpToFile(SoNode* node, const char* filename, bool binary) const;

    void dump(const char *filename, bool onlyVisible) const;

    /** @name Selection methods */
    //@{
    AbstractMouseSelection *startSelection(SelectionMode = Lasso);
    void abortSelection();
    void stopSelection();
    bool isSelecting() const;
    std::vector<SbVec2f> getGLPolygon(SelectionRole* role=nullptr) const;
    std::vector<SbVec2f> getGLPolygon(const std::vector<SbVec2s>&) const;
    const std::vector<SbVec2s>& getPolygon(SelectionRole* role=nullptr) const;
    void setSelectionEnabled(bool enable);
    bool isSelectionEnabled() const;
    //@}

    /// Returns the screen coordinates of the origin of the path's tail object
    /*! Return value is in floating-point pixels, origin at bottom-left. */
    SbVec2f screenCoordsOfPath(SoPath *path) const;

    /** @name Edit methods */
    //@{
    void setEditing(bool edit);
    bool isEditing() const { return this->editing; }
    void setEditingCursor (const QCursor& cursor);
    void setComponentCursor(const QCursor& cursor);
    void setRedirectToSceneGraph(bool redirect) { this->redirected = redirect; }
    bool isRedirectedToSceneGraph() const { return this->redirected; }
    void setRedirectToSceneGraphEnabled(bool enable) { this->allowredir = enable; }
    bool isRedirectToSceneGraphEnabled() const { return this->allowredir; }
    //@}

    /** @name Pick actions */
    //@{
    // calls a PickAction on the scene graph
    bool pickPoint(const SbVec2s& pos,SbVec3f &point,SbVec3f &norm) const;
    SoPickedPoint* pickPoint(const SbVec2s& pos) const;
    SoPickedPoint* getPickedPoint(SoEventCallback * n) const;
    bool pubSeekToPoint(const SbVec2s& pos);
    void pubSeekToPoint(const SbVec3f& pos);

    std::vector<App::SubObjectT> getPickedList(bool singlePick=false) const;

    std::vector<App::SubObjectT> getPickedList(const SbVec2s &pos,
                                               bool singlePick = false,
                                               bool mapCoords = false) const;

    std::vector<App::SubObjectT> getPickedList(const std::vector<SbVec2f> &points,
                                               bool center = false,
                                               bool pickElement = true,
                                               bool backfaceCull = true,
                                               bool currentSelection = false,
                                               bool unselect = false,
                                               bool mapCoords = true) const;
    //@}

    /**
     * Set up a callback function \a cb which will be invoked for the given eventtype.
     * \a userdata will be given as the first argument to the callback function.
     */
    void addEventCallback(SoType eventtype, SoEventCallbackCB * cb, void* userdata = nullptr);
    /**
     * Unregister the given callback function \a cb.
     */
    void removeEventCallback(SoType eventtype, SoEventCallbackCB * cb, void* userdata = nullptr);

    /** @name Clipping plane, near and far plane */
    //@{
    /** Returns the view direction from the user's eye point in direction to the
     * viewport which is actually the negative normal of the near plane.
     * The vector is normalized to length of 1.
     */
    SbVec3f getViewDirection() const;
    void    setViewDirection(SbVec3f);
    /** Returns the up direction */
    SbVec3f getUpDirection() const;

    /** Returns the orientation of the camera. */
    SbRotation getCameraOrientation() const;

    /** Returns the 3d point on the focal plane to the given 2d point. */
    SbVec3f getPointOnFocalPlane(const SbVec2s&) const;
    /// Point under the cursor projected onto the XY plane of \a plc
    SbVec3f getPointOnXYPlaneOfPlacement(const SbVec2s& pnt, const Base::Placement& plc) const;
    /// Point under the cursor projected onto the line through \a axisCenter along \a axis
    SbVec3f getPointOnLine(const SbVec2s& pnt, const SbVec3f& axisCenter, const SbVec3f& axis) const;

    /** Returns the 2d coordinates on the viewport to the given 3d point. */
    SbVec2s getPointOnViewport(const SbVec3f&) const;

    /** Converts Inventor coordinates into Qt coordinates.
     * The conversion takes the device pixel ratio into account.
     */
    QPoint toQPoint(const SbVec2s&) const;

    /** Converts Qt coordinates into Inventor coordinates.
     * The conversion takes the device pixel ratio into account.
     */
    SbVec2s fromQPoint(const QPoint&) const;

    /** Returns the near plane represented by its normal and base point. */
    void getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const;

    /** Returns the far plane represented by its normal and base point. */
    void getFarPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const;

    /** Adds or remove a manipulator to/from the scenegraph. */
    void toggleClippingPlane(int toggle=-1, bool beforeEditing=false,
            bool noManip=false, const Base::Placement &pla = Base::Placement());

    /** Checks whether a clipping plane is set or not. */
    bool hasClippingPlane() const;

    /** Project the given normalized 2d point onto the near plane */
    SbVec3f projectOnNearPlane(const SbVec2f&) const;

    /** Project the given normalized 2d point onto the far plane */
    SbVec3f projectOnFarPlane(const SbVec2f&) const;

    /** Project the given 2d point to a line */
    void projectPointToLine(const SbVec2s&, SbVec3f& pt1, SbVec3f& pt2) const;

    /** Get the normalized position of the 2d point. */
    SbVec2f getNormalizedPosition(const SbVec2s&) const;
    //@}

    /** @name Dimension controls
     * the "turn*" functions are wired up to parameter groups through view3dinventor.
     * don't call them directly. instead set the parameter groups.
     * @see TaskDimension
     */
    //@{
    void turnAllDimensionsOn();
    void turnAllDimensionsOff();
    void turn3dDimensionsOn();
    void turn3dDimensionsOff();
    void turnDeltaDimensionsOn();
    void turnDeltaDimensionsOff();
    void eraseAllDimensions();
    void addDimension3d(SoNode *node);
    void addDimensionDelta(SoNode *node);
    //@}
    
    SoGroup * getAuxSceneGraph() const;

    /**
     * Set the camera's orientation. If isAnimationEnabled() returns
     * \a true the reorientation is animated, otherwise its directly
     * set.
     */
    void setCameraOrientation(const SbRotation& orientation, bool moveToCenter = false);
    /// Apply a camera saved as an Inventor node string (the form
    /// SoFCDB::writeNodesToString produces): switches the camera type
    /// when it differs and copies the camera fields, animating the move
    /// when \a animateDuration (ms) is nonzero. Throws on a string that
    /// does not parse to a camera. The view-level wrapper
    /// View3DInventor::setCamera adds the draw-style tail on top.
    bool setCamera(const char* pCamera, int animateDuration = 0);
    void setCameraType(SoType type) override;
    void moveCameraTo(const SbRotation& orientation, const SbVec3f& position, int duration = -1);
    /**
     * Zooms the viewport to the size of the bounding box.
     */
    void boxZoom(const SbBox2s&);
    /**
     * Reposition the current camera so we can see the complete scene.
     */
    void viewAll() override;
    void viewAll(float factor);
    void viewBoundBox(const SbBox3f &box);

    /// Breaks out a VR window for a Rift
    void viewVR();

    /**
     * Returns the bounding box of the scene graph.
     */
    SbBox3f getBoundingBox() const;

    /**
     * Reposition the current camera so we can see all selected objects.
     *
     * @param extend: Whether to extend the current view (zoom out if
     * necessary) to include the selection, or zoom in the camera to view only
     * the selection.
     */
    void viewSelection(bool extend = false);

    /** Reposition the current camera so we can see the given objects
     *
     * @param objs: viewing objects
     *
     * @param extend: Whether to extend the current view (zoom out if
     * necessary) to include the objects, or zoom in the camera to view only
     * the given objects.
     */
    void viewObjects(const std::vector<App::SubObjectT> &objs, bool extend = false);

    /** Reposition the current camera to see in the normal direction of the selected geometry elements
     *
     * @param backFacing: whether to see the back facing side
     */
    void viewSelectionNormal(bool backFacing);

    void setRotationCenterSelection();

    void setGradientBackground(Background);
    bool hasGradientBackground() const;
    Background getGradientBackground() const;
    void setGradientBackgroundColor(const SbColor& fromColor,
                                    const SbColor& toColor);
    void setGradientBackgroundColor(const SbColor& fromColor,
                                    const SbColor& toColor,
                                    const SbColor& midColor);
    void setNavigationType(Base::Type);

    void setAxisCross(bool on);
    bool hasAxisCross();

    void showRotationCenter(bool show);
    void changeRotationCenterPosition(const SbVec3f& newCenter);

    void setEnabledFPSCounter(bool on);
    void setEnabledNaviCube(bool on);
    bool isEnabledNaviCube() const;
    void setNaviCubeCorner(int);
    NaviCube* getNaviCube() const;
    void setEnabledVBO(bool on);
    bool isEnabledVBO() const;
    void setRenderCache(int);
    void setRendererType(const std::string &);
    /** Read the Render_* settings from \a container instead of the MDI view
     *
     * A viewer that belongs to no View3DInventor -- a material preview, the
     * material icon renderer -- has no property container of its own, so
     * every render setting falls through to the global RenderParams
     * preference and it cannot be given a shading model independent of
     * whatever the user is looking at. Hand it a container here (the caller
     * owns it, and it must outlive the viewer) and Gui::initRenderProperties
     * populates that instead, so the viewer can state its own PBR,
     * environment and matcap settings.
     *
     * Call before selecting the backend with setRendererType(): the
     * container is bound when the backend is created.
     */
    void setRenderSettings(App::PropertyContainer *container);
    /// Whether an external render backend (Renderer/) is active on this
    /// viewer. Interaction paths that would bypass renderScene() (e.g.
    /// the cached-image rubber-band optimization) must keep Native
    /// rendering so backend frames — and their overlay feeds — stay live.
    bool hasExternalRenderer() const;
    /// The active external render backend, null when none — the
    /// frame-capture Python API (saveRenderDump/getRenderStats) arms
    /// one-shot readbacks on it (docs/RenderDebug.md §4).
    Render::Renderer *getExternalRenderer() const;
    /// The backend as a shared handle, so that a ViewArea unified canvas
    /// can hand the same instance to every 3D cell it hosts
    /// (docs/SplitViews.md sec 13). Null when there is no backend.
    std::shared_ptr<Render::Renderer> sharedRenderer() const;
    /// State this viewer's background (flat or gradient) to the backend
    /// it feeds and return the flat colour behind it -- what a unified
    /// canvas frame needs before rendering its sub-views, and what it
    /// clears with if the backend pass fails.
    QColor feedRendererBackground();
    /** Use \a renderer instead of creating a backend of this viewer's
     * own -- what a unified canvas does to its cells, so that N cells
     * cost ONE backend instance and one copy of the GPU scene
     * (docs/SplitViews.md sec 13).
     *
     * \a feed selects whether this viewer's render-cache manager is the
     * one that states the scene to it. Exactly one cell of a canvas may
     * feed: two managers pushing setScene() at one backend would
     * overwrite each other's scene. The rest supply only a camera, and
     * draw the same resident scene through it. Passing a null \a
     * renderer detaches and puts the viewer back on its own backend
     * (setRendererType).
     */
    void adoptRenderer(const std::shared_ptr<Render::Renderer> &renderer,
                       bool feed, int subView = 0);
    /** Feed this viewer's overlay captures -- its NaviCube, its corner
     * axis cross, its foreground root -- into the adopted backend,
     * scoped to the sub-view it was adopted as.
     *
     * renderScene() does this for a viewer that paints. A canvas cell
     * that is not the feeder never paints, so without this its chrome
     * would never reach the backend and every cell would show the
     * FEEDING cell's cube, turned by the feeding cell's camera
     * (docs/SplitViews.md sec 16.3). Cheap: the capture traversals are
     * over the chrome graphs alone, not the scene.
     *
     * Must be called with the canvas's GL context current.
     */
    void updateCanvasOverlays();
    /// Whether this viewer is a cell of a unified canvas (adoptRenderer
    /// with a live renderer).
    bool hasAdoptedRenderer() const;
    /** Composite this viewer's Coin residue into one cell of a unified
     * canvas (docs/SplitViews.md sec 13).
     *
     * The canvas has already drawn the backend pass for every cell into
     * the framebuffer it has bound; this draws what the backend does not
     * claim -- draggers, uncached custom nodes -- into \a origin / \a
     * size, a rect of the canvas in GL device pixels (origin bottom
     * left). \a backendDrawn says whether the backend pass succeeded, so
     * a failed one still gets its background clear. Must be called with
     * the canvas's GL context current.
     */
    void renderCanvasResidue(const SbVec2s &origin, const SbVec2s &size,
                             bool backendDrawn);
    /// Apply the current AntiAliasing preference to the external render
    /// backend (if any) and schedule a redraw. Returns true when a backend
    /// handled it — the caller then skips the Coin view-clone that a plain-GL
    /// view needs to change its multisampled context. Returns false (no-op)
    /// when there is no external renderer.
    bool applyRendererAntiAliasing();
    /// Hand the backend's render targets back now, without waiting out the
    /// grace period. They come back with the next frame this view draws.
    void releaseRenderTargets();
    /// Start (or cancel) the wait after which a view nobody is looking at
    /// releases its render targets, according to what
    /// View3DInventor::isBackgroundView() says right now. Called from
    /// every signal that could have changed that answer -- the MDI
    /// window state, a hide or show, and a change to
    /// Render_BackgroundReleaseDelay itself, since a view already in the
    /// background is then waiting out a stale deadline.
    void armBackgroundRelease();
    /// The grace period in milliseconds: the per-view override where the
    /// view carries one, the Render/BackgroundReleaseDelay preference
    /// otherwise. 0 keeps the targets for as long as the view lives.
    int backgroundReleaseDelay() const;
    /// Materialize the per-view Render_* dynamic properties on the view
    /// object (RenderParams defaults), called when a renderer backend is
    /// selected.
    /// Pick along a world-space ray and select the hit element —
    /// remote-viewer click selection forwarded by the scene-streaming
    /// server (SceneServer.h). ctrl toggles like a Ctrl-click; a miss
    /// without ctrl clears the selection. GUI thread only.
    void pickAndSelect(const SbVec3f &origin, const SbVec3f &dir, bool ctrl);

    void updateHatchTexture();
    void refreshRenderCache();

    void getDimensions(float& fHeight, float& fWidth) const;
    float getMaxDimension() const;
    SbVec3f getCenterPointOnFocalPlane() const;

    NavigationStyle* navigationStyle() const;

    void setDocument(Gui::Document *pcDocument);
    Gui::Document* getDocument();

    virtual PyObject *getPyObject();

    const SoPath *getGroupOnTopPath();

    const SoPath *getRootPath();

    bool getSceneBoundBox(SbBox3f &box) const;
    bool getSceneBoundBox(Base::BoundBox3d &box) const;

    void setTransparencyOnTop(float t);

    void onGetBoundingBox(SoGetBoundingBoxAction *);

    void onViewPropertyChanged(const App::Property &);

    /// Whether a View preference key belongs to the viewer's light rig,
    /// which a Light_* view property can override.
    static bool isLightPreferenceKey(const char *key);
    /// Light this view from the effective value of one rig key: its
    /// Light_* property if it has one, and the preference otherwise.
    /// Returns false for a key that is not part of the rig.
    bool applyLightPreference(const char *key);
    /// The same for the whole rig. Done after a restore, so that the
    /// overrides the document carried reach the light nodes.
    void syncLightProperties();

    const SoPathList *getLatePickPaths() const;

    void appendDetailPath(SoPath *path, ViewProvider *vp);

    /// The render cache manager of this view's selection root (null
    /// when the render-cache bridge is inactive)
    SoFCRenderCacheManager *getRenderCacheManager() const;

    /** Path trace this view with Cycles to a PNG (docs/CyclesIntegration.md
     * phase 3).
     *
     * Snapshots what the view's render cache holds -- the same translated
     * draw list the bgfx backend is fed -- with the view's PBR, output,
     * light and background settings and its current camera, framed for
     * \a width x \a height, and renders it with \a samples per pixel on
     * the device of the given type ("CPU", "CUDA", ...). Blocks until
     * the render is done. Requires the render-cache bridge (render cache
     * mode 3). Returns false with \a error set on failure; \a report, if
     * given, receives what the translation made of the scene.
     */
    bool renderWithCycles(const std::string &path, int width, int height, int samples,
                          const std::string &device, std::string *error,
                          Render::Cycles::RenderReport *report = nullptr);

    /** Path trace this view live with Cycles (docs/CyclesIntegration.md
     * phase 4, the viewport).
     *
     * With \a options, a Cycles session on the named device follows this
     * view: it is fed the render-cache scene whenever the backend's feed
     * restates it (or a render setting changes), the camera whenever it
     * moves, and its progressively refined frame is blitted over the
     * backend's scene as the base layer of every frame. Null \a options
     * turns it off. Requires the render-cache bridge (render cache mode
     * 3) with a backend attached. Returns false with \a error set.
     */
    bool setCyclesViewport(const Render::Cycles::ViewportOptions *options,
                           std::string *error);
    /// What the live Cycles session is doing; false when there is none.
    bool cyclesViewportStatus(Render::Cycles::ViewportStatus &status) const;
    /** The Cycles options in effect for this view: the view's Cycles_*
     * properties where materialized, the preferences underneath where
     * not -- the effective-value rule every Render_* setting follows.
     * What syncExternalShading starts a session with, and what a
     * consumer rendering on this view's behalf (the shader graph
     * editor's preview, docs/ShaderGraphEditor.md sec 15) starts its
     * own with.
     */
    Render::Cycles::ViewportOptions cyclesViewportOptions() const;
    /** Fill the per-frame configs of a Cycles scene from this view's
     * render settings: PBR (enabled -- a Cycles session IS external
     * shading, see the feed), bump, output, light, section, the
     * background from \a col (or the view's gradient), and the debug
     * view mode. The draws and the camera are the caller's.
     */
    void cyclesSceneConfig(Render::Cycles::SceneInput &input, const QColor &col) const;
    /** Make the live external session agree with the view's shading
     * choice (the External value of View3DInventor::ShadingType).
     *
     * Reads the choice and the Cycles_* options off the view and
     * starts, restarts or stops the session through setCyclesViewport
     * accordingly -- a restart only when the effective options actually
     * changed, so the property writes a refresh loop re-issues do not
     * throw the refining frame away. Called from every direction the
     * inputs change: the declared properties (View3DInventor::
     * onChanged), the Cycles_* options (onViewPropertyChanged), restore
     * (View3DInventor::Restore) and backend selection
     * (setRendererType). Failure lands on the console: the caller is a
     * property change with nobody to hand an error to.
     */
    void syncExternalShading();
    /** Feed this viewer's live Cycles session as one cell of a unified
     * canvas (docs/CyclesIntegration.md sec 5.11).
     *
     * The canvas calls this for every cell before its renderSubViews
     * frame, with the cell's camera matrices at its \a width x \a
     * height and the cell that feeds the shared backend: the session
     * takes its scene from \a feeder's render cache -- the one
     * traversal a canvas runs -- and its camera from here, and its
     * consumer is registered under this cell's sub-view id so it draws
     * into this cell alone. Returns false, doing nothing, when this
     * viewer has no Cycles session or is not a canvas cell.
     */
    bool feedCanvasCyclesViewport(const QColor &col, const SbMatrix &view,
                                  const SbMatrix &proj, int width, int height,
                                  View3DInventorViewer *feeder);

    struct Private;
    friend struct Private;

Q_SIGNALS:
    /// The external backend has just rendered a complete frame (see
    /// waitFrameComplete): emitted from renderScene, on the frame that
    /// advanced Render::Renderer::completeFrames. Nothing listens by
    /// default; an unconnected emit is a connection-list check.
    void frameCompleted();

public Q_SLOTS:
    /** Redraw the view, subject to the live-operation redraw throttle.
     *
     * While a progressive import fills the document, every new object makes
     * the next frame rebuild the whole render cache, so unthrottled redraws
     * cost more than the import itself. Requests are then held to a share of
     * the wall clock (ViewParams::LiveImportRedrawBudget, floored at
     * LiveImportRedrawInterval); anything the user does with the mouse
     * renders immediately, and \a force always renders.
     */
    void redraw(bool force = false) override;

protected:
    static GLenum getInternalTextureFormat();
    void renderScene();
    void renderFramebuffer();
    void renderGLImage();
    void animatedViewAll(const SbBox3f &bbox, int steps, int ms);
    void actualRedraw() override;
    void setSeekMode(bool on) override;
    void afterRealizeHook() override;
    bool processSoEvent(const SoEvent * ev) override;
    void dropEvent (QDropEvent * ev) override;
    void dragEnterEvent (QDragEnterEvent * ev) override;
    void dragMoveEvent(QDragMoveEvent* ev) override;
    void dragLeaveEvent(QDragLeaveEvent* ev) override;
    /// Two of the signals that can change whether anyone is looking at
    /// this view; the MDI one arrives as View3DInventor::
    /// windowStateChanged instead. None of them can be noticed from the
    /// frame path, which a view nobody is looking at never reaches.
    void hideEvent(QHideEvent * ev) override;
    void showEvent(QShowEvent * ev) override;
    bool processSoEventBase(const SoEvent * const ev);
    void printDimension() const;
    void selectAll();

private:
    static void setViewportCB(void * userdata, SoAction * action);
    static void clearBufferCB(void * userdata, SoAction * action);
    static void setGLWidgetCB(void * userdata, SoAction * action);
    static void handleEventCB(void * userdata, SoEventCallback * n);
    static void interactionStartCB(void * data, Quarter::SoQTQuarterAdaptor * viewer);
    static void interactionFinishCB(void * data, Quarter::SoQTQuarterAdaptor * viewer);
    static void interactionLoggerCB(void * ud, SoAction* action);

private:
    /// Push one Light_* property into the Coin light or environment node
    /// it stands for.
    void applyLightProperty(const App::Property &prop);
    static void selectCB(void * viewer, SoPath * path);
    static void deselectCB(void * viewer, SoPath * path);
    static SoPath * pickFilterCB(void * viewer, const SoPickedPoint * pp);
    void initialize();
    /// Rebuild the additive-mode interest (own overrides + imposed),
    /// push it to the selection root, and schedule the re-capture.
    void rebuildCaptureInterest();
    void drawAxisCross();
    static void drawArrow();
    static void drawSingleBackground(const QColor&);
    void setCursorRepresentation(int mode);
    void aboutToDestroyGLContext() override;
    void createStandardCursors(double);

private:
    /// Multisampling of this viewer alone; < 0 defers to the preference.
    int _numSamples {-1};
    NaviCube* naviCube;
    std::set<ViewProvider*> _ViewProviderSet;
    std::list<GLGraphicsItem*> graphicsItems;
    ViewProvider* editViewProvider;
    SoFCBackgroundGradient *pcBackGround;
    SoSwitch               *pcBackGroundSwitch;
    SoSeparator * backgroundroot;
    SoSeparator * foregroundroot;
    SoDirectionalLight* backlight;
    SoDirectionalLight* fillLight;
    SoEnvironment* environment;
    // Sits between the camera and the scene root: the fill light (under a
    // rotation slaved to the camera, so the rig tracks the view) and the
    // ambient environment.
    SoGroup* viewerLightingRoot;
    SoRotation* lightRotation;

    // Scene graph root
    SoSeparator * pcViewProviderRoot;
    // Child group in the scene graph that contains view providers related to the physical object
    SoGroup* nonObjectGroup;

    mutable std::unique_ptr<Private> _pimpl;

    std::unique_ptr<View3DInventorSelection> inventorSelection;

    std::unique_ptr<SoFCSelectionAction> selectionAction;
    std::unique_ptr<SoFCHighlightAction> highlightAction;

    SoSeparator * pcEditingRoot;
    SoTransform * pcEditingTransform;
    bool restoreEditingRoot;
    SoEventCallback* pEventCallback;
    NavigationStyle* navigation;
    SoFCUnifiedSelection* selectionRoot;

    SoClipPlane *pcClipPlane;

    RenderType renderType;
    QtGLFramebufferObject* framebuffer;
    QImage glImage;
    bool shading;
    SoSwitch *dimensionRoot;

    // small axis cross in the corner
    bool axiscrossEnabled;
    int axiscrossSize;
    // big one in the middle
    SoShapeScale* axisCross;
    SoGroup* axisGroup;

    SoGroup* rotationCenterGroup;

    //stuff needed to draw the fps counter
    bool fpsEnabled;
    bool vboEnabled;
    bool naviCubeEnabled;

    bool editing;
    QCursor editCursor, zoomCursor, panCursor, spinCursor;
    bool redirected;
    bool allowredir;

    std::string overrideMode;
    uint32_t overrideBGColor = 0;
    Gui::Document* guiDocument = nullptr;

    ViewerEventFilter* viewerEventFilter;

    PyObject *_viewerPy;

    bool _applyingOverride = false;

    // friends
    friend class NavigationStyle;
    friend class GLPainter;
    friend class ViewerEventFilter;
};

} // namespace Gui

#endif  // GUI_VIEW3DINVENTORVIEWER_H
