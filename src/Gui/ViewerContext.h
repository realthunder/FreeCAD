/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef GUI_VIEWERCONTEXT_H
#define GUI_VIEWERCONTEXT_H

#include <FCGlobal.h>

#include <memory>
#include <vector>

#include <map>
#include <memory>

#include <QtCore/qnamespace.h>
#include <QtCore/QPoint>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec2f.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoType.h>
#include <Inventor/nodes/SoEventCallback.h>

class SoNode;
class SoPath;
class SoPickedPoint;
class SoRenderManager;
class SoEventManager;
class SoEventCallback;  // NOLINT
class SoEvent;
class SoFCRenderCacheManager;
class SoGroup;
class SoSeparator;
class SoTransform;

class QWidget;
class QCursor;
class QKeyEvent;
class SoCamera;

namespace Base {
class Matrix4D;
class Placement;
}  // namespace Base

namespace Render {
class Renderer;
}

namespace Gui {

class Document;
class ViewProvider;
class ViewerContext;
class GLGraphicsItem;
class SelectionScope;
class SelectionSingleton;
class EditableDatumLabel;

/** The one editing root of an edit session (docs/ThinClient.md 8.11).
 *
 * A separator whose first child is always the editing transform, and the
 * rule that an edit mode's geometry is moved out of the document's graph
 * and under it, and back again when the session ends. There is one per
 * session because a served document is one session with N views: the
 * desktop's windows and every client's mirror all show the same edit, so
 * they all hang the same node -- a Coin node takes several parents -- and
 * each of them decides only WHERE it hangs (under the aux root on the
 * desktop, outside the render-cache feed; inside the served graph for a
 * mirror). Gui::Document owns its session's root; a ViewerContext with no
 * document (the unit harness) builds a private one.
 *
 * Not a view. Nothing here reads a camera or a viewport; the bodies used to
 * live on ViewerContext, where a second view of the same edit would have
 * been a second root and a second move.
 */
class GuiExport EditingRoot
{
public:
    explicit EditingRoot(Gui::Document* doc = nullptr);
    ~EditingRoot();
    EditingRoot(const EditingRoot&) = delete;
    EditingRoot& operator=(const EditingRoot&) = delete;

    SoSeparator* node() const
    {
        return root;
    }
    SoTransform* transformNode() const
    {
        return transform;
    }
    /// The document whose session this is, or null for a private root.
    Gui::Document* document() const
    {
        return doc;
    }
    /// Whether an edit has hung anything here: more than the transform.
    bool hasContent() const;
    void setTransform(const Base::Matrix4D& mat);
    /** Hang an edit mode's geometry here.
     *
     * With \a node, that node. Without, \a vp's own children are *moved*
     * out of its root and under this one, so that the edit renders through
     * the editing transform and picks as one thing; reset() puts them
     * back. \a mat overrides the document's editing transform.
     */
    void setup(Gui::ViewProvider* vp, SoNode* node, const Base::Matrix4D* mat);
    /// Give \a vp its children back, if they were moved; drop a handed
    /// node. Idempotent: a second view leaving after the first has
    /// nothing left to do.
    void reset(Gui::ViewProvider* vp, bool updateLinks);
    /** Put the node under \a parent (at \a index; -1 appends), counted.
     *
     * N views may share one parent -- every client's mirror hangs the
     * root in the one served graph -- so the first to hang it inserts it
     * and the last to unhang it takes it out; the desktop's aux roots are
     * one parent per view and count to one.
     */
    void hangUnder(SoGroup* parent, int index = -1);
    void unhangFrom(SoGroup* parent);
    /// How many views hang this under \a parent (a test's question).
    int hangCount(SoGroup* parent) const;

    /** @name Gesture arbitration (docs/ThinClient.md 8.11)
     *
     * Two mice, one state machine. Every view of the session delivers its
     * events to the one view provider, and the rule that keeps its state
     * machine coherent is stated here, once, because this is the object
     * every view of the session shares: while a gesture is in progress
     * from view A -- a button held, or the tool between the clicks of a
     * multi-click sequence -- input from view B is dropped. A press takes
     * the hold for the view it came from; the hold ends when that view
     * holds no button and the tool reports no sequence. Lazily: the next
     * event from another view finds the hold stale and releases it, so a
     * sequence ended by the panel's Escape, an undo or a tool change never
     * leaves a view locked out. This is the second expansion seam of 8.11
     * -- per-client sessions would fork here.
     */
    //@{
    /** Whether \a event from \a view may reach the view provider now.
     *
     * Records the press or release it admits. \a vp is asked
     * ViewProvider::isGestureInProgress for the sequence half of the rule;
     * null means no sequence.
     */
    bool admitInput(ViewerContext* view, const SoEvent* event, const ViewProvider* vp);
    /// The view holding a gesture, or null.
    ViewerContext* gestureHolder() const
    {
        return holder;
    }
    /// The buttons the holder still holds, as SoMouseButtonEvent bits.
    unsigned heldButtons() const
    {
        return held;
    }
    /// Drop the hold if \a view has it: the view is leaving the session,
    /// or being destroyed, with a button down.
    void releaseGesture(ViewerContext* view);
    //@}

private:
    SoSeparator* root {nullptr};
    SoTransform* transform {nullptr};
    Gui::Document* doc {nullptr};
    std::map<SoGroup*, int> parents;
    /// Whether reset has children to give back to a view provider, as
    /// opposed to a node someone handed setup.
    bool restore {false};
    ViewerContext* holder {nullptr};
    unsigned held {0};
};

/** What an edit mode is allowed to ask of the view it is running in.
 *
 * The interaction code -- the sketcher's tools, the draggers, every
 * ViewProvider edit mode -- was written against View3DInventorViewer, which
 * is a QuarterWidget: a Qt widget owning a GL context. Nothing an edit mode
 * actually needs from it requires either. It needs a camera, a viewport, a
 * scene graph to pick against, the editing root to hang its geometry on, and
 * a few numbers the input device supplies. Coin already separates those from
 * drawing: SoRenderManager holds the camera and the viewport region and
 * touches GL only inside render(), and SoEventManager runs
 * SoHandleEventAction with no framebuffer anywhere.
 *
 * So this is that subset, named, with View3DInventorViewer as its desktop
 * implementation and a per-client offscreen mirror as the other one
 * (docs/ThinClient.md section 8.3). The edit entry points take this type,
 * which is the whole point: an edit mode that runs here runs in a server
 * process with no display.
 *
 * The last group is the residue -- the widget and GL calls the edit path
 * still makes. They are declared here rather than left out so that call sites
 * compile unchanged, and they answer null or do nothing by default. An edit
 * mode that cannot work without a widget is then found by that null, in a
 * place that can say so, rather than by a crash in a headless process.
 */
class GuiExport ViewerContext
{
public:
    virtual ~ViewerContext();

    /** @name Scene, camera and viewport */
    //@{
    virtual SoNode* getSceneGraph() const = 0;
    virtual SoRenderManager* getSoRenderManager() const = 0;
    virtual SoEventManager* getSoEventManager() const = 0;
    virtual const SbViewportRegion& getViewportRegion() const = 0;
    virtual Gui::Document* getDocument() = 0;
    virtual SoFCRenderCacheManager* getRenderCacheManager() const = 0;
    virtual Render::Renderer* getExternalRenderer() const = 0;
    //@}

    /** @name Values the input device supplies
     *
     * Not global state: a touch client wants a larger pick radius than a
     * mouse, and the device pixel ratio is the client's, so both are asked of
     * the context the event arrived through.
     */
    //@{
    virtual float getPickRadius() const = 0;
    virtual double devicePixelRatio() const = 0;
    /// The mouse buttons this view last saw held down.
    virtual Qt::MouseButtons mouseButtons() const = 0;
    /// Screen density, for the edit modes that size things in millimetres.
    virtual double logicalDotsPerInchX() const = 0;
    /// Whether any of them is.
    bool isMouseButtonDown() const
    {
        return mouseButtons() != Qt::NoButton;
    }
    //@}

    /** @name Camera math -- all of it view-less */
    //@{
    virtual SbVec3f getViewDirection() const = 0;
    virtual SbVec3f getCenterPointOnFocalPlane() const = 0;
    virtual SbVec3f getPointOnFocalPlane(const SbVec2s& pnt) const = 0;
    virtual SbVec3f getPointOnXYPlaneOfPlacement(const SbVec2s& pnt,
                                                 const Base::Placement& plc) const = 0;
    virtual SbVec3f getPointOnLine(const SbVec2s& pnt, const SbVec3f& axisCenter,
                                   const SbVec3f& axis) const = 0;
    virtual SbVec2s getPointOnViewport(const SbVec3f& pnt) const = 0;
    virtual SbVec2f screenCoordsOfPath(SoPath* path) const = 0;
    virtual void getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const = 0;
    virtual float getMaxDimension() const = 0;
    virtual bool getSceneBoundBox(SbBox3f& box) const = 0;
    virtual void setCameraOrientation(const SbRotation& orientation,
                                      bool moveToCenter = false) = 0;
    /** Viewport points as dimensionless [0 1] screen coordinates.
     *
     * Named for GL by history only -- it is the viewport region and an aspect
     * ratio, and no context of any kind is needed to compute it.
     */
    virtual std::vector<SbVec2f> getGLPolygon(const std::vector<SbVec2s>& pnts) const = 0;
    /// The camera this view looks through, or null before one is stated.
    SoCamera* getCamera() const;
    /** Whether the camera is a client's, stated over the wire.
     *
     * A desktop view owns its camera and an edit mode may turn it -- the
     * sketcher faces the sketch plane on entry. A mirror's camera is what
     * its client last stated and will state again on its next frame
     * (docs/ThinClient.md 8.11: the camera stays the client's), so an
     * adjustment here would only disagree with the picture the client
     * draws until then, and every pixel resolved in between would land
     * somewhere the client cannot see.
     */
    virtual bool cameraIsRemote() const
    {
        return false;
    }
    /** The world-space size of what the viewport shows.
     *
     * Camera arithmetic and an aspect ratio, so it is the same answer with
     * or without a widget -- which matters because the on-view parameters
     * size their dimension lines by it and would otherwise pin themselves
     * to the desktop.
     */
    void getDimensions(float& fHeight, float& fWidth) const;
    /** A Coin viewport point as a widget point.
     *
     * Flips the origin to the top left and divides the device pixels out,
     * because a widget is placed in logical ones. Arithmetic over the
     * viewport region and the client's pixel ratio, so a mirror answers it
     * as meaningfully as a desktop view does.
     */
    QPoint toQPoint(const SbVec2s& pnt) const;
    //@}

    /** @name Picking */
    //@{
    virtual SoPickedPoint* getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const = 0;
    virtual SoPickedPoint* getPointOnRay(const SbVec3f& pos, const SbVec3f& dir,
                                         const ViewProvider* vp) const = 0;
    virtual void appendDetailPath(SoPath* path, ViewProvider* vp) = 0;
    //@}

    /** @name Edit mode
     *
     * The editing root and everything done to it is implemented once, here,
     * because it is not view work: it is a separator, a transform, and the
     * rule that an edit mode's geometry is moved out of the document's graph
     * and back again. What a view supplies is *where* that root hangs, which
     * is the one line each implementation writes for itself -- under the aux
     * root on the desktop, under the served graph for a mirror.
     */
    //@{
    /// Whether an edit mode is running here. What it means is the view's:
    /// the desktop stops its navigation from claiming events.
    virtual void setEditing(bool edit) = 0;
    virtual bool isEditing() const = 0;
    /** Start an edit session of \a vp in this view: the INITIATOR.
     *
     * Binds \a root (the document's; null builds a private one, for a view
     * with no document), hangs it, gives the view provider this view
     * (ViewProvider::setEditViewer, which is what moves its geometry under
     * the root), and routes this view's events to
     * ViewProvider::eventCallback.
     */
    virtual void setEditingViewProvider(Gui::ViewProvider* vp, int ModNum,
                                        EditingRoot* root = nullptr);
    /// End the session here: a joiner leaves, the initiator gives the
    /// view provider its geometry back and unsets itself from it.
    virtual void resetEditingViewProvider();
    /** Join a session another view started (docs/ThinClient.md 8.11).
     *
     * The same root is hung here, this view's events are routed to the
     * same view provider, and the view's own selection and navigation
     * stand aside as they do for the initiator. What is NOT done is
     * ViewProvider::setEditViewer: the initiator owns the move of the
     * geometry, the camera it adjusted, and the tool state's own view.
     * Idempotent; a no-op on the initiator itself.
     */
    void joinEditing(Gui::ViewProvider* vp, EditingRoot* root);
    /// Undo joinEditing. A no-op on a view that is not a joiner.
    void leaveEditing();
    bool isEditingViewProvider() const
    {
        return editViewProvider != nullptr;
    }
    /// Whether this view started the session it is in (as opposed to
    /// having joined it).
    bool isEditingInitiator() const
    {
        return editViewProvider != nullptr && !joinedEditing;
    }
    /// Hang an edit mode's geometry under the session's root; see
    /// EditingRoot::setup. A no-op with no editing view provider.
    void setupEditingRoot(SoNode* node = nullptr, const Base::Matrix4D* mat = nullptr);
    void resetEditingRoot(bool updateLinks = true);
    void setEditingTransform(const Base::Matrix4D& mat);
    /** The root this view shows the edit through.
     *
     * The session's while one is running here, else this view's private
     * one -- so it is never null, and idle it has one child, the
     * transform. Gui::Document remembers the session's so that a
     * traversal arriving at that node knows it is looking at the edit,
     * not at the document.
     */
    virtual SoNode* getEditRootNode() const;
    EditingRoot* editingRoot() const
    {
        return editRoot;
    }
    /// The view provider being edited here, or null.
    Gui::ViewProvider* getEditingViewProvider() const
    {
        return editViewProvider;
    }
    /** The selection an edit session's events select into.
     *
     * The initiator's (docs/ThinClient.md 8.11): the room when the desktop
     * started the edit, a client's own when its browser did -- for every
     * view of the session, so that the one tool state machine hears one
     * selection whichever view the click came through. Outside a session,
     * or for the initiator, this view's own (selectionInstance).
     */
    SelectionSingleton* sessionSelectionInstance() const;
    //@}

    /** @name Event delivery and selection mode */
    //@{
    virtual void addEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                                  void* userdata = nullptr) = 0;
    virtual void removeEventCallback(SoType eventtype, SoEventCallbackCB* cb,
                                     void* userdata = nullptr) = 0;
    virtual void setRedirectToSceneGraph(bool redirect) = 0;
    virtual void setSelectionEnabled(bool enable) = 0;
    virtual bool isSelectionEnabled() const = 0;
    virtual bool isSelecting() const = 0;
    //@}

    /** @name The widget residue
     *
     * No answer without a widget, and none needed: these draw or focus, and a
     * mirror does neither. The DOM layer takes over these surfaces
     * (docs/ThinClient.md section 8.7).
     */
    //@{
    virtual QWidget* getWidget() const
    {
        return nullptr;
    }
    virtual QWidget* getGLWidget() const
    {
        return nullptr;
    }
    virtual void redraw(bool force = false)
    {
        (void)force;
    }
    virtual void addGraphicsItem(GLGraphicsItem*)
    {}
    virtual void removeGraphicsItem(GLGraphicsItem*)
    {}
    virtual void setEditingCursor(const QCursor&)
    {}
    virtual void setFocusToView()
    {}
    /** How the view gets its pixels onto the screen.
     *
     * Lives here rather than on the viewer only so that the edit modes that
     * switch it while dragging keep naming one type. Without a framebuffer it
     * means nothing, so the default does nothing.
     */
    enum RenderType
    {
        Native,
        Framebuffer,
        Image
    };
    virtual void setRenderType(RenderType type)
    {
        (void)type;
    }
    /// The Python face of this view. None where there is not one.
    virtual PyObject* getPyObject();
    //@}

    /** @name On-view parameters (docs/ThinClient.md section 8.7)
     *
     * The small entry boxes an edit mode places next to the cursor. Both
     * halves of one are here because both halves are the view's: where the
     * box is shown, and how a keystroke reaches it.
     *
     * The editor itself stays a QuantitySpinBox on either tier. A mirror's
     * is simply never given a parent and never shown -- it is a text model
     * driven by replayed key events, exactly as the sketcher's geometry is
     * driven by replayed pointer events, and its text is streamed to the
     * client instead of painted. That is what keeps the parsing, the units
     * and DrawSketchKeyboardManager's rule for which keys an entry box
     * claims in one place: the client is told what to display and forwards
     * keystrokes, and knows none of it.
     */
    //@{
    /// Where a new entry box is parented, and null where it is not shown.
    virtual QWidget* datumEditorParent() const
    {
        return nullptr;
    }
    /** Hand a key back to the scene when the entry box does not claim it.
     *
     * The desktop posts it to the viewer widget; a mirror replays it as the
     * Coin event it arrived as. Answers whether it was handled.
     */
    virtual bool sendKeyEvent(QKeyEvent* event)
    {
        (void)event;
        return false;
    }
    /// Track the set this view is showing, in the order it was built.
    virtual void addOnViewParameter(EditableDatumLabel*)
    {}
    virtual void removeOnViewParameter(EditableDatumLabel*)
    {}
    /** Which box takes the keys.
     *
     * Focus is the view's state, not the box's -- on the desktop it is Qt's
     * and this is ignored, and a widget that is never shown is never focused
     * by Qt at all, so a mirror keeps the record here.
     */
    virtual void onViewParameterFocused(EditableDatumLabel*)
    {}
    /** Something about that set changed: a value, the focus, a position.
     *
     * Deliberately one undifferentiated notice rather than a signal per
     * field. What the desktop does with it is nothing -- the widgets have
     * already moved themselves -- and what a mirror does is re-serialize
     * the set, which is cheap and cannot go out of step with itself.
     */
    virtual void onViewParametersChanged()
    {}
    //@}

    /** The context an event callback node was installed by.
     *
     * The node's user data is always a ViewerContext, never a derived
     * pointer: a pointer to a multiply-inherited object is not the same
     * address as a pointer to its base, so storing one and casting to the
     * other through void* reads the wrong bytes. Everything that wants the
     * desktop viewer back asks View3DInventorViewer::fromEventCallback,
     * which is a checked cast from this one.
     */
    static ViewerContext* fromEventCallback(const SoEventCallback* node)
    {
        return node ? static_cast<ViewerContext*>(node->getUserData()) : nullptr;
    }

    /** The view whose input is being handled right now, if one said so.
     *
     * An edit mode is entered from wherever the click that entered it
     * landed, and the code that starts one is nowhere near the code that
     * knows which view that was: Gui::Document::setEdit reaches
     * MainWindow::activeWindow() for it, which in a serving process names
     * either nothing or somebody else's window. So a replayed client event
     * says which view it is (ViewerScope), and setEdit asks here first.
     *
     * Null on the desktop, where nothing opens a scope, so every caller
     * falls back to the active window exactly as it did before. This is the
     * same shape as Gui::SelectionScope (docs/ThinClient.md sec 8.4): one
     * replayed event, one dynamic extent, and no call site retyped.
     */
    static ViewerContext* current();

    /** This view's own selection, or null when it selects into the room.
     *
     * The desktop's views share the room instance and answer null. A
     * client's mirror answers its own, because what a browser picks while
     * it is editing is that browser's: the room is what every viewer and
     * every panel agrees on, and one client's sketch elements are not
     * that (docs/ThinClient.md section 8.4).
     *
     * Opening a ViewerScope on a view makes this instance current, so
     * every one of the call sites behind Gui::Selection() lands in the
     * right one without being touched.
     */
    virtual SelectionSingleton* selectionInstance() const
    {
        return nullptr;
    }

protected:
    ViewerContext();

    /** Where this view hangs the editing root, and unhangs it.
     *
     * Called with the root just bound (\a hang true) and with the root
     * about to be unbound (false), always in pairs, never twice in a row.
     * The desktop puts it under the aux root; a mirror inserts it into the
     * served graph. The base does nothing, for a view that only picks.
     */
    virtual void hangEditingRoot(EditingRoot* root, bool hang);

    /** The editing root this view currently shows through.
     *
     * The session's root while one runs here, else this view's own
     * private one; pcEditingRoot and pcEditingTransform are that root's
     * nodes, cached raw so the implementations read them as they always
     * did. Idle, the root has one child, the transform, which is why every
     * test for "is anything being edited" reads getNumChildren() > 1.
     */
    EditingRoot* editRoot {nullptr};
    SoSeparator* pcEditingRoot {nullptr};
    SoTransform* pcEditingTransform {nullptr};
    /// The view provider in edit here.
    Gui::ViewProvider* editViewProvider {nullptr};
    /// Whether this view joined the session rather than started it.
    bool joinedEditing {false};
    /// Make \a root (or the private one) the root shown here. An
    /// implementation going away mid-session unbinds in its own
    /// destructor, while its graph is still there to unhang from.
    void bindEditingRoot(EditingRoot* root);
    void unbindEditingRoot();

private:
    /// This view's private root, built on first need.
    std::unique_ptr<EditingRoot> ownEditRoot;
};

/** Make \a context the current view for this scope's dynamic extent.
 *
 * Scopes nest and unwind innermost first. The scope does not own the
 * context and must not outlive it -- it is opened around the handling of
 * one event by the view that received it.
 *
 * It carries the view's selection with it: for the same extent
 * Gui::Selection() is that view's instance (ViewerContext::
 * selectionInstance), or the room when the view has none of its own and
 * when the scope names no view at all. So the two are never out of step
 * -- a replayed event cannot be handled in one client's view while
 * selecting in another's, and a scope that says "no view here" selects in
 * the room rather than in whoever was current outside it. On the desktop
 * this scope is never opened, so Gui::Selection() is the room as it has
 * always been.
 */
class GuiExport ViewerScope
{
public:
    explicit ViewerScope(ViewerContext* context);
    ~ViewerScope();
    ViewerScope(const ViewerScope&) = delete;
    ViewerScope& operator=(const ViewerScope&) = delete;

private:
    ViewerContext* previous;
    /// The selection this scope pushed, if the context had one to push.
    std::unique_ptr<SelectionScope> selection;
};

}  // namespace Gui

#endif  // GUI_VIEWERCONTEXT_H
