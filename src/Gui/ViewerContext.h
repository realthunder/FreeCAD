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
#include <Base/Matrix.h>
#include <Inventor/nodes/SoSeparator.h>

#include <memory>
#include <string>
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

#include <fastsignals/signal.h>

#include "Inventor/SoFCVisibilityElement.h"
#include "Inventor/SoFCRenderCacheManager.h"

class SoNode;
class SoPath;
class SoPickedPoint;
class SoRenderManager;
class SoEventManager;
class SoEventCallback;  // NOLINT
class SoEvent;
class SoFCRenderCacheManager;
class SoGroup;
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
struct VisibilitySet;
}

namespace App {
class DocumentObject;
}

namespace Gui {

struct VisibilityEntry;

class Document;
class ViewProvider;
class ViewerContext;
class GLGraphicsItem;
class SelectionScope;
class SelectionSingleton;
class EditableDatumLabel;
class OnViewEntry;

/** The node an edit session's geometry hangs under (EditingRoot::node()).
 *
 * A separator that can stand out of a GL render. In render cache mode 3 the
 * external backend draws the edit graph from its own capture of it (the
 * viewer's editing overlay feed), and Coin's GL pass, which composites what
 * the backend does not draw, traversed it again on top: every sketch line,
 * point marker and grid line was drawn twice, the GL copy last, over the
 * datums the backend had drawn on top of them. The viewer sets
 * SuppressGLRender around its GL pass while the backend is fed; picking,
 * bounding boxes and the capture itself (a callback action) are untouched.
 */
class GuiExport SoFCEditingRoot : public SoSeparator
{
    using inherited = SoSeparator;
    SO_NODE_HEADER(SoFCEditingRoot);

public:
    static void initClass();
    SoFCEditingRoot();

    static bool SuppressGLRender;

    void GLRender(SoGLRenderAction* action) override;
    void GLRenderBelowPath(SoGLRenderAction* action) override;
    void GLRenderInPath(SoGLRenderAction* action) override;
    void GLRenderOffPath(SoGLRenderAction* action) override;

protected:
    ~SoFCEditingRoot() override = default;
};

/** The one editing root of an edit session (docs/ThinClient.md 8.11).
 *
 * A separator whose first child is always the editing transform, and the
 * rule that an edit mode's geometry is moved out of the document's graph
 * and under it, and back again when the session ends. There is one per
 * session because a served document is one session with N views: the
 * desktop's windows and every client's mirror all show the same edit, so
 * they all hang the same node -- a Coin node takes several parents -- and
 * each of them decides only WHERE it hangs (under the aux root on the
 * desktop, outside the render-cache feed; beside the served scene in a
 * mirror's own event graph). Gui::Document owns its session's root; a ViewerContext with no
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
    /** Where the session's views that have no on-view root of their own
     * hang their on-view parameters (a mirror's getOnViewParameterRoot()
     * while it is in the session): dimension lines and pattern markers,
     * in WORLD coordinates, so not under the editing transform.
     *
     * It is the session's, like node(), and for the same reason: the
     * serving source publishes both as the session's overlay, tagged with
     * it (docs/ThinClient.md 8.12 item J), where a mirror's labels used to
     * go into the served root every client shares -- seen by viewers
     * outside the session, and a moving label spoiling the shared scene's
     * caches on every drag. The desktop's views keep a root of their own
     * (View3DInventorViewer's on-view feed), so nothing here is drawn
     * twice there.
     */
    SoSeparator* onViewNode() const
    {
        return onView;
    }
    /// node() then onViewNode(): what the serving source captures.
    SoGroup* publishNode() const
    {
        return publish;
    }
    /// Whether a view has hung an on-view parameter in onViewNode().
    bool hasOnViewContent() const;
    /// The document whose session this is, or null for a private root.
    Gui::Document* document() const
    {
        return doc;
    }
    /// Whether an edit has hung anything here: more than the transform.
    /// A session node (addSessionNode) counts -- it is drawn, captured
    /// and published like the rest.
    bool hasContent() const;
    /// Whether the EDIT MODE's own geometry is here -- a node handed to
    /// setup, or the view provider's moved children -- as opposed to a
    /// session node beside it. What "pick the edited object under this
    /// root" has to ask: a feature whose preview hangs here still has its
    /// own geometry in the document's graph.
    bool hasEditGeometry() const;
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
     * N views may share one parent, so the first to hang it inserts it
     * and the last to unhang it takes it out. Today every view hangs it
     * under a parent of its own -- a desktop view's aux root, a mirror's
     * event root -- and each counts to one.
     */
    void hangUnder(SoGroup* parent, int index = -1);
    void unhangFrom(SoGroup* parent);
    /// How many views hang this under \a parent (a test's question).
    int hangCount(SoGroup* parent) const;

    /** @name Whose session it is
     *
     * SHARED, the session of docs/ThinClient.md 8.11: every view of the
     * document joins it -- the desktop's other windows, every served
     * client's mirror -- shows the edit and can work in it. Or the
     * initiating view's ALONE (ViewParams PerViewEdit; 8.12): no other
     * view joins, so none hangs this root, takes the session's hides and
     * swaps, or routes its input to the tool, and each goes on showing
     * the document as it is.
     *
     * The plumbing is the same either way -- a view shows an edit only
     * through the root it has bound -- so this is policy, asked at the
     * places that would join a view: Gui::Document for its windows, the
     * serving source for its mirrors. It is decided when a session starts
     * (Gui::Document::setEdit) and kept until the next one does, so the
     * preference changing halfway neither pulls a view in nor strands
     * one, and the session's end can still tell who was in it.
     */
    //@{
    bool isShared() const
    {
        return shared;
    }
    void setShared(bool on)
    {
        shared = on;
    }
    //@}

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

    /** @name The views of the session
     *
     * Every view showing through this root, initiator and joiners alike,
     * in the order they bound it (ViewerContext::bindEditingRoot keeps
     * it). A tool that changes what a VIEW does -- the External and
     * CarbonCopy tools turn the view's own selection back on so a click
     * reaches the object under the pointer -- has to do it to every view
     * of the session, and this is the only list of them: the document
     * knows its windows and the serving source its mirrors, and the tool
     * state machine is shared by both.
     */
    //@{
    const std::vector<ViewerContext*>& views() const
    {
        return viewList;
    }
    void attachView(ViewerContext* view);
    void detachView(ViewerContext* view);
    //@}

    /** @name The edited occurrence's own hide
     *
     * An edit that draws its object itself -- a sketch hands its edit
     * graph rather than moving the view provider's children -- leaves the
     * object's own geometry where it was, in every view that shows it. In
     * the views of the session that geometry is hidden: the ONE occurrence
     * being edited, \a subname under \a parent (Gui::Document::getInEdit),
     * as a path entry of each view's own visibility table, TRANSIENT
     * (ViewerContext::setEditVisibilities) -- never written into a view's
     * map, and gone when the edit ends. A view joining later hides it on
     * attach and shows it again on detach; a view outside the session --
     * another document's showing the object through a link -- keeps it.
     */
    //@{
    /** Hide the edited occurrence in every view of the session.
     *
     * False, and nothing hidden anywhere, when the path does not resolve or
     * some view cannot hide one (render-cache modes 0-2, which have no
     * per-view table): the caller falls back to moving the children.
     */
    bool hideEdited(App::DocumentObject* parent, const char* subname);
    /// Undo hideEdited. Idempotent.
    void showEdited();
    /// Whether hideEdited is in force.
    bool isEditedHidden() const
    {
        return editHide != nullptr;
    }
    //@}

    /** @name What else the edit swaps in its views
     *
     * An edit that shows something OTHER than the document's state while
     * it runs -- a PartDesign feature's preview draws the feature's base
     * with the tool over it, where the document shows the feature -- used
     * to write Visibility, which is document state: every view changed,
     * and every served client's. These are the same swaps as entries of
     * each session view's own visibility table, transient like the hide
     * above and after it in the table: entries, a hide or a show each. A
     * view joining later takes them on attach and drops them on detach;
     * they end with the session (endSession) at the latest.
     *
     * Kept per OWNER, a name: the PartDesign preview and TempoVis each
     * swap what is theirs and take back only that. Between two entries
     * that both name a draw the table's own rule decides, whoever owns
     * them: the longer key -- one occurrence ahead of the object wherever
     * it is drawn -- and, between equals, the earlier, the owners' going
     * in by the order of their names.
     */
    //@{
    /** Replace \a owner's swaps, in every view of the session.
     *
     * False, and none of that owner's in force anywhere, when some view
     * cannot take them (render-cache modes 0-2, which have no per-view
     * table): the caller falls back to document Visibility. With no view
     * attached yet -- an edit mode swapping while it starts, before the
     * session's views bind -- they are kept for the views that attach;
     * whether those will take them is ViewerContext::
     * canSetEditVisibilities, which the caller asks first.
     */
    bool setVisibilitySwaps(const std::string& owner, std::vector<VisibilityEntry>&& entries);
    /// Drop \a owner's. Idempotent.
    void clearVisibilitySwaps(const std::string& owner);
    /// \a owner's swaps, or null when it has none.
    const std::vector<VisibilityEntry>* visibilitySwaps(const std::string& owner) const;
    bool hasVisibilitySwaps() const
    {
        return !swaps.empty();
    }
    //@}

    /** @name What an edit shows beside its own geometry
     *
     * A node of the session's that is neither the edit mode's own graph
     * (setup with a node) nor the view provider's moved children: a
     * PartDesign feature's preview, the tinted tool shape. Hung here it
     * is drawn by the views of the session and by no other, where it used
     * to be a child of the base feature's switch -- in the one scene every
     * view and every served client draws.
     *
     * \a world is the node's own frame in WORLD coordinates. The editing
     * transform does not apply to it and may change under it -- a gizmo
     * hands setup a transform of its own, the edited object moves -- and
     * the node stays where it was put. setup() and reset() leave these
     * alone; they go with removeSessionNode, or with the session.
     */
    //@{
    void addSessionNode(SoNode* node, const Base::Matrix4D& world);
    /// Move a node added by addSessionNode; false when it is not here.
    bool setSessionNodeTransform(SoNode* node, const Base::Matrix4D& world);
    /// Take it out. Idempotent.
    void removeSessionNode(SoNode* node);
    bool hasSessionNode(SoNode* node) const;
    //@}

    /** The session is over: drop what only a session holds.
     *
     * The session nodes and the visibility swaps, and the edited
     * occurrence's hide. Called by the initiating view as it leaves
     * (ViewerContext::resetEditingViewProvider), while the joiners are
     * still attached, so every view of the session is told. Whoever added
     * a node or a swap usually takes it back first; this is what keeps one
     * that did not from reaching the next session.
     */
    void endSession();

private:
    /// The session's transient entries, as each view takes them: the
    /// edited occurrence's hide, then the swaps.
    std::vector<VisibilityEntry> transientEntries() const;
    /// Hand every view of the session the entries; false when one of them
    /// cannot take them.
    bool applyVisibility();
    /// Each session node's holder transform from its world frame and the
    /// editing transform above it.
    void placeSessionNodes();

    struct SessionNode
    {
        SoNode* node {nullptr};
        SoSeparator* holder {nullptr};
        SoTransform* place {nullptr};
        Base::Matrix4D world;
    };
    SoSeparator* root {nullptr};
    SoTransform* transform {nullptr};
    SoSeparator* onView {nullptr};
    SoGroup* publish {nullptr};
    Gui::Document* doc {nullptr};
    std::map<SoGroup*, int> parents;
    std::vector<ViewerContext*> viewList;
    /// Whether reset has children to give back to a view provider, as
    /// opposed to a node someone handed setup.
    bool restore {false};
    ViewerContext* holder {nullptr};
    unsigned held {0};
    bool shared {true};
    std::unique_ptr<VisibilityEntry> editHide;
    std::map<std::string, std::vector<VisibilityEntry>> swaps;
    /// Children 1..N of the root, after the transform and ahead of the
    /// edit's own geometry.
    std::vector<SessionNode> sessionNodes;
    /// What setTransform last set: the transform the session nodes undo.
    Base::Matrix4D editMatrix;
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
    /** This view's own object visibility as SoFCVisibilityElement
     * carries it (docs/CoinRetirement.md 5.18), or null for none. A
     * selection root shared by several views -- the served root, one per
     * document for every client -- sets the element for the view whose
     * traversal it is, so each client's picks follow its own table.
     */
    virtual const SoFCVisibilityElement::Table *visibilityElementTable() const
    {
        return nullptr;
    }
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
    /** The keyboard modifiers this view last saw.
     *
     * The application's for a desktop view. A mirror answers from its
     * client's last replayed event, because a replayed event carries its
     * own modifiers and the keyboard at the machine is somebody else's --
     * the same rule as mouseButtons(). Code that decides something from
     * a modifier while a pick is being resolved (a selection gate asking
     * whether Alt is held) asks currentKeyboardModifiers().
     */
    virtual Qt::KeyboardModifiers keyboardModifiers() const;
    /// The modifiers of the view whose input is being handled now, or the
    /// application's when no view is (the desktop with no scope open).
    static Qt::KeyboardModifiers currentKeyboardModifiers();
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
    /** A viewport pixel as the point of the camera's normalized [0, 1]
     * frustum that the projection math takes.
     *
     * The two viewers answer this differently, and have to: a desktop
     * viewer leaves SoCamera::aspectRatio at 1 and corrects for the
     * aspect here, while a mirror's camera states the client's real one
     * and must not correct again (see MirrorViewer). So this is asked
     * of the viewer and never worked out by a caller.
     */
    virtual SbVec2f getNormalizedPosition(const SbVec2s& pnt) const = 0;
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
    /** The graph a pick through this view is applied to.
     *
     * A ray pick has a view volume only after it has traversed a camera,
     * so this is the graph with the camera in it, not the bare scene:
     * the desktop's is its render manager's scene graph, a mirror's the
     * per-client event root it replays through (its render manager holds
     * no graph on purpose). What the unified selection root resolves a
     * replayed click against (docs/ThinClient.md 8.11 item 3); null when
     * the view has nothing to pick over.
     */
    virtual SoNode* getPickRoot() const;
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
    /// Give the view provider its geometry back, and show the edited
    /// occurrence again if hideEditedObject hid it.
    void resetEditingRoot(bool updateLinks = true);
    /** Highlight elements of the editing root in this view alone.
     *
     * For an edit mode that tracks its own preselection (the sketcher):
     * the items are drawn over the whole editing graph, in this view and
     * no other, and replace whatever the last call stated. An empty list
     * clears them. Returns false when this view cannot -- nothing here
     * captures the editing root for a backend: a view outside render
     * cache mode 3, and a served mirror -- and the caller then colours
     * the shared edit graph itself, which every view shows.
     */
    virtual bool setEditingHighlight(
        const std::vector<SoFCRenderCacheManager::HighlightItem>& items)
    {
        (void)items;
        return false;
    }
    /// Whether setEditingHighlight() would take a highlight now: asked
    /// before a caller decides what to colour in the shared graph.
    virtual bool canEditingHighlight() const
    {
        return false;
    }
    /** Hide the occurrence being edited in every view of the session.
     *
     * For an edit mode that hands setupEditingRoot a node of its own and
     * leaves the view provider's geometry where it is; see
     * EditingRoot::hideEdited. The occurrence is the document's
     * (Gui::Document::getInEdit), else the edited object itself. Only the
     * initiator may ask. False when it could not be hidden: the edit mode
     * moves the children instead (setupEditingRoot with no node).
     */
    bool hideEditedObject();
    /** Replace what the edit session hides and shows in this view.
     *
     * The session's own, transient entries of the view's visibility
     * table -- the edited occurrence's hide (EditingRoot::hideEdited) and
     * the edit's swaps (EditingRoot::setVisibilitySwaps) -- ahead of the
     * view's persisted map and never written into it; empty takes them
     * all back. False when the view has no per-view table to put them in
     * -- no render-cache manager, modes 0-2 -- which is what the base
     * answers.
     */
    virtual bool setEditVisibilities(const std::vector<VisibilityEntry>& entries);
    /// Whether setEditVisibilities would take entries: whether this view
    /// has a visibility table of its own. Asked before a session's views
    /// are bound, when there is nobody yet to refuse.
    virtual bool canSetEditVisibilities() const;
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
    virtual void addOnViewParameter(OnViewEntry*)
    {}
    virtual void removeOnViewParameter(OnViewEntry*)
    {}
    /** Which box takes the keys.
     *
     * Focus is the view's state, not the box's -- on the desktop it is Qt's
     * and this is ignored, and a widget that is never shown is never focused
     * by Qt at all, so a mirror keeps the record here.
     */
    virtual void onViewParameterFocused(OnViewEntry*)
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
    /** An entry made for this view, and gone: it is told when the view goes
     * (OnViewEntry::forgetViewer), whether or not it is on screen. Not
     * virtual, unlike the set above -- the view's going is what it is for.
     */
    void trackOnViewEntry(OnViewEntry* entry);
    void untrackOnViewEntry(OnViewEntry* entry);
    /** Where an on-view parameter's dimension hangs, in world coordinates.
     *
     * It has to reach whatever draws the view. A mirror's scene graph is
     * the served root, which the publish traversal walks to the client. The
     * desktop's is not drawn by an external backend during an edit -- only
     * the selection root and the overlay feeds are -- so it answers a root
     * of its own, fed as one (Blender's overlay engine, in effect: labels
     * and dimensions are the viewport's, drawn over the scene, never the
     * renderer's).
     */
    virtual SoGroup* getOnViewParameterRoot() const;
    //@}

    /** The camera NODE was replaced, as a change of projection does: what
     * watches the fields of the old one hears nothing more (upstream's
     * View3DInventorViewer::cameraChanged, 6fa9125919, on the view-less base
     * so that a mirror says it too).
     */
    fastsignals::signal<void()> signalCameraReplaced;

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

    /** A handle that expires when this view is destroyed.
     *
     * For whatever must name a view past its end without dangling: a
     * client's mirror goes when its client does, and it is no QObject to
     * point a QPointer at. Two handles of one view share ownership, so
     * they also say "the same view" after it is gone, which an address
     * cannot (Gui::TaskOwner).
     */
    std::weak_ptr<const void> lifetime() const
    {
        return life;
    }

protected:
    ViewerContext();

    /** Where this view hangs the editing root, and unhangs it.
     *
     * Called with the root just bound (\a hang true) and with the root
     * about to be unbound (false), always in pairs, never twice in a row.
     * The desktop puts it under the aux root; a mirror beside the served
     * scene in its own event graph. The base does nothing, for a view
     * that only picks.
     */
    virtual void hangEditingRoot(EditingRoot* root, bool hang);

    /** The editing root this view currently shows through.
     *
     * The session's root while one runs here, else this view's own
     * private one; pcEditingRoot and pcEditingTransform are that root's
     * nodes, cached raw so the implementations read them as they always
     * did. Idle, the root has one child, the transform, which is why a
     * test for "is anything hung here" reads getNumChildren() > 1; one
     * for "is the edited object's geometry here" asks
     * EditingRoot::hasEditGeometry, since a session node is not.
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

    /** Tell the on-view entries made for this view that it is going.
     *
     * A panel that holds entries can outlive the view they were made for: a
     * closed document takes its views first and the dialog is deleted
     * after. Taking a label's dimension out of the view reaches
     * getOnViewParameterRoot(), so an implementation calls this from its
     * OWN destructor, while it is still itself; the base destructor only
     * makes the entries forget it.
     */
    void releaseOnViewParameters();

private:
    /// This view's private root, built on first need.
    std::unique_ptr<EditingRoot> ownEditRoot;
    /// Every entry made for this view and not yet gone (trackOnViewEntry)
    std::vector<OnViewEntry*> onViewEntries;
    /// What lifetime() hands out.
    std::shared_ptr<const void> life {std::make_shared<char>()};
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
