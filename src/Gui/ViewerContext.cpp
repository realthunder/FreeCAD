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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <cmath>

#include <QApplication>

#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoEventManager.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/events/SoEvent.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#endif

#include <Base/Console.h>
#include <Base/Matrix.h>
#include <Base/PyObjectBase.h>

#include "Document.h"
#include "InventorBase.h"
#include "OnViewEntry.h"
#include "Selection.h"
#include "ViewProvider.h"
#include "ViewProviderLink.h"
#include "ViewVisibility.h"
#include "ViewerContext.h"
#include "Renderer/Renderer.h"

using namespace Gui;

FC_LOG_LEVEL_INIT("3DViewer", true, true)

// ---- EditingRoot ------------------------------------------------------------

SO_NODE_SOURCE(SoFCEditingRoot)

bool SoFCEditingRoot::SuppressGLRender = false;

void SoFCEditingRoot::initClass()
{
    SO_NODE_INIT_CLASS(SoFCEditingRoot, SoSeparator, "Separator");
}

SoFCEditingRoot::SoFCEditingRoot()
{
    SO_NODE_CONSTRUCTOR(SoFCEditingRoot);
}

// All four: an SoAnnotation's content is drawn late through the path Coin
// stored for it, which enters here in-path rather than through GLRender.
void SoFCEditingRoot::GLRender(SoGLRenderAction* action)
{
    if (!SuppressGLRender)
        inherited::GLRender(action);
}

void SoFCEditingRoot::GLRenderBelowPath(SoGLRenderAction* action)
{
    if (!SuppressGLRender)
        inherited::GLRenderBelowPath(action);
}

void SoFCEditingRoot::GLRenderInPath(SoGLRenderAction* action)
{
    if (!SuppressGLRender)
        inherited::GLRenderInPath(action);
}

void SoFCEditingRoot::GLRenderOffPath(SoGLRenderAction* action)
{
    if (!SuppressGLRender)
        inherited::GLRenderOffPath(action);
}

EditingRoot::EditingRoot(Gui::Document* document)
    : doc(document)
{
    // SoFCDB::init registers the class; a context built without it (the
    // unit harness's mirror) would otherwise make a node of a bad type.
    if (SoFCEditingRoot::getClassTypeId().isBad())
        SoFCEditingRoot::initClass();
    root = new SoFCEditingRoot;
    root->ref();
    root->setName("EditingRoot");
    transform = new SoTransform;
    transform->ref();
    transform->setName("EditingTransform");
    root->addChild(transform);
    onView = new SoSeparator;
    onView->ref();
    onView->setName("EditingOnViewRoot");
    // Labels move with the camera and the pointer; a cache here would be
    // rebuilt on every frame that matters
    onView->renderCaching = SoSeparator::OFF;
    publish = new SoGroup;
    publish->ref();
    publish->setName("EditingPublishRoot");
    publish->addChild(root);
    publish->addChild(onView);
}

EditingRoot::~EditingRoot()
{
    // Not reset(): whoever moved a view provider's children here gives
    // them back before letting go of this (the initiating view, in its
    // own destructor at the latest). A root destroyed with content would
    // take that content with it, which is the one thing owning the nodes
    // must never do silently.
    if (restore) {
        FC_ERR("editing root destroyed while still holding an edit's geometry");
    }
    publish->unref();
    onView->unref();
    transform->unref();
    root->unref();
}

bool EditingRoot::hasContent() const
{
    return root->getNumChildren() > 1;
}

bool EditingRoot::hasEditGeometry() const
{
    return root->getNumChildren() > 1 + int(sessionNodes.size());
}

bool EditingRoot::hasOnViewContent() const
{
    return onView->getNumChildren() > 0;
}

void EditingRoot::hangUnder(SoGroup* parent, int index)
{
    if (!parent) {
        return;
    }
    int& count = parents[parent];
    if (count++ == 0 && parent->findChild(root) < 0) {
        if (index < 0 || index > parent->getNumChildren()) {
            parent->addChild(root);
        }
        else {
            parent->insertChild(root, index);
        }
    }
}

void EditingRoot::unhangFrom(SoGroup* parent)
{
    auto it = parents.find(parent);
    if (it == parents.end()) {
        return;
    }
    if (--it->second > 0) {
        return;
    }
    parents.erase(it);
    const int index = parent->findChild(root);
    if (index >= 0) {
        parent->removeChild(index);
    }
}

int EditingRoot::hangCount(SoGroup* parent) const
{
    auto it = parents.find(parent);
    return it == parents.end() ? 0 : it->second;
}

bool EditingRoot::admitInput(ViewerContext* view, const SoEvent* event, const ViewProvider* vp)
{
    if (!view || !event) {
        return true;
    }
    if (holder && holder != view) {
        // Another view's gesture: a button still down, or the tool between
        // the clicks of a sequence that view's press started.
        if (held != 0 || (vp && vp->isGestureInProgress())) {
            return false;
        }
        // The hold outlived its gesture -- ended by the panel, an undo, a
        // tool change -- and this event is the first to notice.
        holder = nullptr;
    }
    if (event->isOfType(SoMouseButtonEvent::getClassTypeId())) {
        const auto* button = static_cast<const SoMouseButtonEvent*>(event);
        const unsigned bit = 1U << unsigned(button->getButton());
        if (button->getState() == SoButtonEvent::DOWN) {
            holder = view;
            held |= bit;
        }
        else {
            held &= ~bit;
        }
    }
    return true;
}

void EditingRoot::releaseGesture(ViewerContext* view)
{
    if (holder == view) {
        holder = nullptr;
        held = 0;
    }
}

void EditingRoot::attachView(ViewerContext* view)
{
    if (view && std::find(viewList.begin(), viewList.end(), view) == viewList.end()) {
        viewList.push_back(view);
        if (editHide || !swaps.empty()) {
            view->setEditVisibilities(transientEntries());
        }
    }
}

void EditingRoot::detachView(ViewerContext* view)
{
    auto it = std::find(viewList.begin(), viewList.end(), view);
    if (it == viewList.end()) {
        return;
    }
    viewList.erase(it);
    if (editHide || !swaps.empty()) {
        view->setEditVisibilities({});
    }
}

std::vector<VisibilityEntry> EditingRoot::transientEntries() const
{
    std::vector<VisibilityEntry> entries;
    if (editHide) {
        entries.push_back(*editHide);
    }
    for (const auto& owned : swaps) {
        entries.insert(entries.end(), owned.second.begin(), owned.second.end());
    }
    return entries;
}

bool EditingRoot::applyVisibility()
{
    const std::vector<VisibilityEntry> entries = transientEntries();
    bool ok = true;
    for (ViewerContext* view : viewList) {
        if (!view->setEditVisibilities(entries)) {
            ok = false;
        }
    }
    return ok;
}

bool EditingRoot::hideEdited(App::DocumentObject* parent, const char* subname)
{
    showEdited();
    // The occurrence as a path entry: resolved to its node key by each
    // view's table, which also resolves it again should the scene move.
    std::vector<Render::ObjectRef> path;
    if (!parent || !resolveObjectPath(parent, subname, path)) {
        return false;
    }
    auto hide = std::make_unique<VisibilityEntry>();
    hide->doc = path.front().doc;
    hide->obj = path.front().obj;
    hide->subname = subname ? subname : "";
    hide->rooted = true;
    hide->visible = false;
    editHide = std::move(hide);
    if (!applyVisibility()) {
        showEdited();
        return false;
    }
    return true;
}

void EditingRoot::showEdited()
{
    if (!editHide) {
        return;
    }
    editHide.reset();
    applyVisibility();
}

bool EditingRoot::setVisibilitySwaps(const std::string& owner,
                                     std::vector<VisibilityEntry>&& entries)
{
    if (entries.empty()) {
        clearVisibilitySwaps(owner);
        return true;
    }
    swaps[owner] = std::move(entries);
    if (!applyVisibility()) {
        clearVisibilitySwaps(owner);
        return false;
    }
    return true;
}

void EditingRoot::clearVisibilitySwaps(const std::string& owner)
{
    if (swaps.erase(owner) == 0) {
        return;
    }
    applyVisibility();
}

const std::vector<VisibilityEntry>* EditingRoot::visibilitySwaps(const std::string& owner) const
{
    auto it = swaps.find(owner);
    return it == swaps.end() ? nullptr : &it->second;
}

namespace
{
SbMatrix toSbMatrix(const Base::Matrix4D& mat)
{
    // NOLINTBEGIN
    double dMtrx[16];
    mat.getGLMatrix(dMtrx);
    return SbMatrix(
                dMtrx[0], dMtrx[1], dMtrx[2],  dMtrx[3],
                dMtrx[4], dMtrx[5], dMtrx[6],  dMtrx[7],
                dMtrx[8], dMtrx[9], dMtrx[10], dMtrx[11],
                dMtrx[12],dMtrx[13],dMtrx[14], dMtrx[15]);
    // NOLINTEND
}
}  // namespace

void EditingRoot::setTransform(const Base::Matrix4D& mat)
{
    editMatrix = mat;
    transform->setMatrix(toSbMatrix(mat));
    placeSessionNodes();
}

void EditingRoot::placeSessionNodes()
{
    if (sessionNodes.empty()) {
        return;
    }
    // Under the editing transform like everything else here, so each
    // holder undoes it first: a second root beside this one would have to
    // be hung, captured and published by every kind of view all over
    // again.
    Base::Matrix4D undo = editMatrix;
    undo.inverseGauss();
    for (const SessionNode& entry : sessionNodes) {
        entry.place->setMatrix(toSbMatrix(undo * entry.world));
    }
}

void EditingRoot::addSessionNode(SoNode* node, const Base::Matrix4D& world)
{
    if (!node) {
        return;
    }
    if (setSessionNodeTransform(node, world)) {
        return;
    }
    SessionNode entry;
    entry.node = node;
    entry.world = world;
    entry.holder = new SoSeparator;
    entry.holder->setName("EditingSessionNode");
    entry.place = new SoTransform;
    entry.holder->addChild(entry.place);
    entry.holder->addChild(node);
    // After the transform and the session nodes already here, ahead of
    // the edit's own geometry: reset() gives back or drops what follows.
    root->insertChild(entry.holder, 1 + int(sessionNodes.size()));
    sessionNodes.push_back(entry);
    placeSessionNodes();
}

bool EditingRoot::setSessionNodeTransform(SoNode* node, const Base::Matrix4D& world)
{
    for (SessionNode& entry : sessionNodes) {
        if (entry.node == node) {
            entry.world = world;
            placeSessionNodes();
            return true;
        }
    }
    return false;
}

void EditingRoot::removeSessionNode(SoNode* node)
{
    for (auto it = sessionNodes.begin(); it != sessionNodes.end(); ++it) {
        if (it->node != node) {
            continue;
        }
        const int index = root->findChild(it->holder);
        if (index >= 0) {
            root->removeChild(index);
        }
        sessionNodes.erase(it);
        return;
    }
}

bool EditingRoot::hasSessionNode(SoNode* node) const
{
    return std::any_of(sessionNodes.begin(), sessionNodes.end(),
                       [node](const SessionNode& entry) { return entry.node == node; });
}

void EditingRoot::endSession()
{
    while (!sessionNodes.empty()) {
        removeSessionNode(sessionNodes.back().node);
    }
    const bool told = editHide || !swaps.empty();
    editHide.reset();
    swaps.clear();
    if (told) {
        applyVisibility();
    }
}

void EditingRoot::setup(Gui::ViewProvider* vp, SoNode* node, const Base::Matrix4D* mat)
{
    if (!vp) {
        return;
    }
    reset(vp, false);
    if (mat) {
        setTransform(*mat);
    }
    else if (doc) {
        setTransform(doc->getEditingTransform());
    }
    if (node) {
        restore = false;
        root->addChild(node);
        return;
    }

    restore = true;
    auto vpRoot = vp->getRoot();
    // Appended, so after the session nodes: reset() counts on that.
    for (int i = 0, count = vpRoot->getNumChildren(); i < count; ++i) {
        SoNode* child = vpRoot->getChild(i);
        if (child != vp->getTransformNode()) {
            root->addChild(child);
        }
    }
    coinRemoveAllChildren(vpRoot);
    ViewProviderLink::updateLinks(vp);
}

void EditingRoot::reset(Gui::ViewProvider* vp, bool updateLinks)
{
    if (!vp || !hasEditGeometry()) {
        return;
    }
    // The transform and the session nodes stay: only what the edit mode
    // hung is dropped or given back.
    const int first = 1 + int(sessionNodes.size());
    if (!restore) {
        root->getChildren()->truncate(first);
        return;
    }
    restore = false;
    auto vpRoot = vp->getRoot();
    if (vpRoot->getNumChildren()) {
        FC_ERR("WARNING!!! Editing view provider root node is tampered");
    }
    vpRoot->addChild(vp->getTransformNode());
    for (int i = first, count = root->getNumChildren(); i < count; ++i) {
        vpRoot->addChild(root->getChild(i));
    }
    root->getChildren()->truncate(first);

    // handle exceptions eventually raised by ViewProviderLink
    try {
        if (updateLinks) {
            ViewProviderLink::updateLinks(vp);
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
            Base::Console().Error("Unexpected exception raised in EditingRoot::reset\n");
        }
    }
}

// ---- ViewerContext ----------------------------------------------------------

ViewerContext::ViewerContext()
{
    // Idle, a view shows through a private root of its own: what a session
    // binds is the document's, and what differs between a desktop viewer
    // and a client's mirror is only where either is hung, which each of
    // them does for itself (hangEditingRoot).
    ownEditRoot = std::make_unique<EditingRoot>();
    editRoot = ownEditRoot.get();
    pcEditingRoot = editRoot->node();
    pcEditingTransform = editRoot->transformNode();
}

// Out of line so the class has one key function, and with it one vtable and
// one typeinfo, rather than a copy in every translation unit that sees the
// header.
ViewerContext::~ViewerContext()
{
    // First, so that nothing reached from below takes this view for one
    // that is still there.
    life.reset();
    // Not resetEditingViewProvider(): that reaches virtuals which by here
    // have no override left to reach. An implementation that can still be
    // in a session ends its half of it in its OWN destructor, while it is
    // still itself. The session's view list is plain data, so a view that
    // did not is at least not left in it.
    if (editRoot && editRoot != ownEditRoot.get()) {
        editRoot->detachView(this);
    }
    // Whatever the implementation did not release: forgotten, not taken
    // out, which would reach a virtual
    for (OnViewEntry* entry : std::vector<OnViewEntry*>(onViewEntries)) {
        entry->forgetViewer(false);
    }
}

void ViewerContext::releaseOnViewParameters()
{
    for (OnViewEntry* entry : std::vector<OnViewEntry*>(onViewEntries)) {
        entry->forgetViewer(true);
    }
}

void ViewerContext::trackOnViewEntry(OnViewEntry* entry)
{
    if (entry && std::find(onViewEntries.begin(), onViewEntries.end(), entry)
            == onViewEntries.end()) {
        onViewEntries.push_back(entry);
    }
}

void ViewerContext::untrackOnViewEntry(OnViewEntry* entry)
{
    onViewEntries.erase(std::remove(onViewEntries.begin(), onViewEntries.end(), entry),
                        onViewEntries.end());
}

SoCamera* ViewerContext::getCamera() const
{
    SoRenderManager* manager = getSoRenderManager();
    return manager ? manager->getCamera() : nullptr;
}

SoGroup* ViewerContext::getOnViewParameterRoot() const
{
    // The served root a mirror answers with is a group but not always a
    // separator, and it is the node the publish traversal walks
    SoNode* scene = getSceneGraph();
    return scene && scene->isOfType(SoGroup::getClassTypeId()) ? static_cast<SoGroup*>(scene)
                                                                : nullptr;
}

SoNode* ViewerContext::getPickRoot() const
{
    SoRenderManager* manager = getSoRenderManager();
    return manager ? manager->getSceneGraph() : nullptr;
}

Qt::KeyboardModifiers ViewerContext::keyboardModifiers() const
{
    return QApplication::queryKeyboardModifiers();
}

Qt::KeyboardModifiers ViewerContext::currentKeyboardModifiers()
{
    if (ViewerContext* view = current()) {
        return view->keyboardModifiers();
    }
    return QApplication::queryKeyboardModifiers();
}

void ViewerContext::getDimensions(float& fHeight, float& fWidth) const
{
    SoCamera* camera = getCamera();
    if (!camera) {
        // A mirror before its client has stated a camera. The caller's
        // -1 sentinels stand, which is what the desktop answers with no
        // camera either.
        return;
    }

    const float aspectRatio = getViewportRegion().getViewportAspectRatio();

    SoType type = camera->getTypeId();
    if (type.isDerivedFrom(SoOrthographicCamera::getClassTypeId())) {
        fHeight = static_cast<SoOrthographicCamera*>(camera)->height.getValue();
        fWidth = fHeight;
    }
    else if (type.isDerivedFrom(SoPerspectiveCamera::getClassTypeId())) {
        const float heightAngle =
            static_cast<SoPerspectiveCamera*>(camera)->heightAngle.getValue();
        fHeight = std::tan(heightAngle / 2.0F) * 2.0F * camera->focalDistance.getValue();
        fWidth = fHeight;
    }

    if (aspectRatio > 1.0) {
        fWidth *= aspectRatio;
    }
    else {
        fHeight *= aspectRatio;
    }
}

QPoint ViewerContext::toQPoint(const SbVec2s& pnt) const
{
    const SbVec2s& vps = getViewportRegion().getViewportSizePixels();
    int xpos = pnt[0];
    int ypos = vps[1] - pnt[1] - 1;

    const qreal ratio = devicePixelRatio();
    if (ratio > 0.0) {
        xpos = int(std::roundf(float(xpos / ratio)));
        ypos = int(std::roundf(float(ypos / ratio)));
    }

    return {xpos, ypos};
}

void ViewerContext::hangEditingRoot(EditingRoot*, bool)
{
}

void ViewerContext::bindEditingRoot(EditingRoot* root)
{
    if (!root) {
        root = ownEditRoot.get();
    }
    if (root == editRoot) {
        return;
    }
    if (editRoot) {
        // Leaving mid-gesture -- a client dropping with its button down --
        // must not leave every other view of the session locked out.
        editRoot->releaseGesture(this);
        editRoot->detachView(this);
    }
    if (editRoot != ownEditRoot.get()) {
        hangEditingRoot(editRoot, false);
    }
    editRoot = root;
    pcEditingRoot = editRoot->node();
    pcEditingTransform = editRoot->transformNode();
    editRoot->attachView(this);
    if (editRoot != ownEditRoot.get()) {
        hangEditingRoot(editRoot, true);
    }
}

void ViewerContext::unbindEditingRoot()
{
    bindEditingRoot(nullptr);
}

void ViewerContext::setEditingViewProvider(Gui::ViewProvider* vp, int ModNum, EditingRoot* root)
{
    if (editViewProvider && joinedEditing) {
        // Promoted from joiner to initiator: not a case anything asks for,
        // and one the base cannot do right (the geometry is already
        // moved). Leave first, then start clean.
        leaveEditing();
    }
    editViewProvider = vp;
    joinedEditing = false;
    if (!editViewProvider) {
        return;
    }
    // A private root is hung by no one, so the session's is what the
    // other views can find; bound before setEditViewer, which is what
    // fills it, so that the implementation that hangs it sees the fill.
    bindEditingRoot(root);
    // Recorded here rather than inside setEditViewer below, which is
    // virtual and which ViewProviderDragger overrides without chaining
    // to the base -- so every geometry object would have answered null.
    editViewProvider->_editViewer = this;
    editViewProvider->setEditViewer(this, ModNum);
    addEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,
                     editViewProvider);
    sessionSelectionChanged();
}

void ViewerContext::resetEditingViewProvider()
{
    if (!editViewProvider) {
        return;
    }
    if (joinedEditing) {
        leaveEditing();
        return;
    }

    // In case the event action still has grabbed a node when leaving edit
    // mode force to release it now. A view with no event manager -- a
    // mirror before its events are streamed -- has nothing to release.
    if (SoEventManager* mgr = getSoEventManager()) {
        SoHandleEventAction* heaction = mgr->getHandleEventAction();
        if (heaction && heaction->getGrabber()) {
            heaction->releaseGrabber();
        }
    }

    resetEditingRoot();

    editViewProvider->unsetEditViewer(this);
    editViewProvider->_editViewer = nullptr;
    removeEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,
                        editViewProvider);
    editViewProvider = nullptr;
    // The initiator leaving is the session ending, and the joiners are
    // still attached: whatever the session alone held -- a preview node,
    // the swaps of its views' visibility -- goes now, in all of them.
    editRoot->endSession();
    // After the restore, so that an implementation whose graph has to see
    // the children leave (a mirror's publish traversal) still has the root
    // in it when they do.
    unbindEditingRoot();
    sessionSelectionChanged();
}

void ViewerContext::joinEditing(Gui::ViewProvider* vp, EditingRoot* root)
{
    if (!vp || !root) {
        return;
    }
    if (editViewProvider) {
        // Already in a session: the initiator, or a joiner of this same
        // one. A joiner of another session is a document bug, not a
        // state this view can repair by itself.
        if (editViewProvider != vp || editRoot != root) {
            FC_WARN("view asked to join an edit while in another");
        }
        return;
    }
    editViewProvider = vp;
    joinedEditing = true;
    bindEditingRoot(root);
    // What every implementation of setEditViewer does for the initiating
    // view, and what a joiner needs for the same reason: the view's own
    // selection stands aside so a click reaches the tool, and navigation
    // shows the edit cursor.
    setEditing(true);
    setSelectionEnabled(false);
    addEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,
                     editViewProvider);
    sessionSelectionChanged();
}

void ViewerContext::leaveEditing()
{
    if (!editViewProvider || !joinedEditing) {
        return;
    }
    if (SoEventManager* mgr = getSoEventManager()) {
        SoHandleEventAction* heaction = mgr->getHandleEventAction();
        if (heaction && heaction->getGrabber()) {
            heaction->releaseGrabber();
        }
    }
    removeEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,
                        editViewProvider);
    setSelectionEnabled(true);
    setEditing(false);
    editViewProvider = nullptr;
    joinedEditing = false;
    unbindEditingRoot();
    sessionSelectionChanged();
}

SoNode* ViewerContext::getEditRootNode() const
{
    return pcEditingRoot;
}

SelectionSingleton* ViewerContext::sessionSelectionInstance() const
{
    if (editViewProvider && joinedEditing && editRoot && editRoot->document()) {
        ViewerContext* initiator = editRoot->document()->editingViewer();
        if (initiator && initiator != this) {
            return initiator->selectionInstance();
        }
    }
    return selectionInstance();
}

void ViewerContext::setEditingTransform(const Base::Matrix4D& mat)
{
    editRoot->setTransform(mat);
}

void ViewerContext::setupEditingRoot(SoNode* node, const Base::Matrix4D* mat)
{
    // The initiator owns the move: a joiner asked to set the root up (the
    // Python API on the wrong view) would pull the geometry out from
    // under the view running the tool.
    if (!editViewProvider || joinedEditing) {
        return;
    }
    editRoot->setup(editViewProvider, node, mat);
}

void ViewerContext::resetEditingRoot(bool updateLinks)
{
    if (!editViewProvider || joinedEditing) {
        return;
    }
    editRoot->showEdited();
    editRoot->reset(editViewProvider, updateLinks);
}

bool ViewerContext::hideEditedObject()
{
    if (!editViewProvider || joinedEditing) {
        return false;
    }
    auto vp = Base::freecad_dynamic_cast<ViewProviderDocumentObject>(editViewProvider);
    if (!vp) {
        return false;
    }
    App::DocumentObject* parent = vp->getObject();
    std::string subname;
    if (Gui::Document* doc = editRoot->document()) {
        ViewProviderDocumentObject* parentVp = nullptr;
        doc->getInEdit(&parentVp, &subname);
        if (parentVp) {
            parent = parentVp->getObject();
        }
        else {
            subname.clear();
        }
    }
    return editRoot->hideEdited(parent, subname.c_str());
}

bool ViewerContext::setEditVisibilities(const std::vector<VisibilityEntry>&)
{
    return false;
}

bool ViewerContext::canSetEditVisibilities() const
{
    return false;
}

PyObject* ViewerContext::getPyObject()
{
    Py_INCREF(Py_None);
    return Py_None;
}

namespace
{
/// The innermost open ViewerScope's context, or null.
///
/// Thread-local because a scope is opened around the handling of one
/// event and events are handled on whatever thread the view lives on --
/// the GUI thread for every view there is today, but a global would make
/// that an assumption rather than an observation.
thread_local ViewerContext* s_current = nullptr;
}  // namespace

ViewerContext* ViewerContext::current()
{
    return s_current;
}

ViewerScope::ViewerScope(ViewerContext* context)
    : previous(s_current)
{
    s_current = context;
    // And the view's selection with it, ALWAYS -- the room when this view
    // has none of its own, and the room again when the scope names no
    // view at all. Pushing nothing in those cases would leave whatever
    // was current underneath standing, so a scope that says "no view is
    // being handled here" would still be selecting in the last client's
    // instance: the two stacks out of step, which is the one thing this
    // shape exists to make impossible.
    // The SESSION's instance, which is the view's own outside an edit and
    // the initiator's inside one (docs/ThinClient.md 8.11): a joiner's
    // replayed click selects where the tool state machine is listening.
    SelectionSingleton* sel = context ? context->sessionSelectionInstance() : nullptr;
    selection = std::make_unique<SelectionScope>(sel ? *sel : SelectionRoom());
}

ViewerScope::~ViewerScope()
{
    // The selection first, so the two stacks unwind in the order they
    // were pushed.
    selection.reset();
    s_current = previous;
}
