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
#include <Inventor/SoEventManager.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/events/SoEvent.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#endif

#include <Base/Console.h>
#include <Base/Matrix.h>
#include <Base/PyObjectBase.h>

#include "Document.h"
#include "InventorBase.h"
#include "Selection.h"
#include "ViewProvider.h"
#include "ViewProviderLink.h"
#include "ViewerContext.h"

using namespace Gui;

FC_LOG_LEVEL_INIT("3DViewer", true, true)

ViewerContext::ViewerContext()
{
    // The editing root belongs to the context rather than to any view: what
    // differs between a desktop viewer and a client's mirror is only where
    // it is hung, which each of them does for itself.
    pcEditingRoot = new SoSeparator;
    pcEditingRoot->ref();
    pcEditingRoot->setName("EditingRoot");
    pcEditingTransform = new SoTransform;
    pcEditingTransform->ref();
    pcEditingTransform->setName("EditingTransform");
    pcEditingRoot->addChild(pcEditingTransform);
}

// Out of line so the class has one key function, and with it one vtable and
// one typeinfo, rather than a copy in every translation unit that sees the
// header.
ViewerContext::~ViewerContext()
{
    // Not resetEditingRoot(): that reaches getDocument(), which by here has
    // no override left to reach. An implementation that can still be
    // holding a view provider's children gives them back in its OWN
    // destructor, while it is still itself.
    if (pcEditingTransform) {
        pcEditingTransform->unref();
    }
    if (pcEditingRoot) {
        pcEditingRoot->unref();
    }
}

void ViewerContext::setEditingViewProvider(Gui::ViewProvider* vp, int ModNum)
{
    editViewProvider = vp;
    if (!editViewProvider) {
        return;
    }
    // Recorded here rather than inside setEditViewer below, which is
    // virtual and which ViewProviderDragger overrides without chaining
    // to the base -- so every geometry object would have answered null.
    editViewProvider->_editViewer = this;
    editViewProvider->setEditViewer(this, ModNum);
    addEventCallback(SoEvent::getClassTypeId(), Gui::ViewProvider::eventCallback,
                     editViewProvider);
}

void ViewerContext::resetEditingViewProvider()
{
    if (!editViewProvider) {
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
}

SoNode* ViewerContext::getEditRootNode() const
{
    return pcEditingRoot;
}

void ViewerContext::setEditingTransform(const Base::Matrix4D& mat)
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

void ViewerContext::setupEditingRoot(SoNode* node, const Base::Matrix4D* mat)
{
    if (!editViewProvider) {
        return;
    }

    resetEditingRoot(false);
    if (mat) {
        setEditingTransform(*mat);
    }
    else if (Gui::Document* doc = getDocument()) {
        setEditingTransform(doc->getEditingTransform());
    }
    if (node) {
        restoreEditingRoot = false;
        pcEditingRoot->addChild(node);
        return;
    }

    restoreEditingRoot = true;
    auto root = editViewProvider->getRoot();
    for (int i = 0, count = root->getNumChildren(); i < count; ++i) {
        SoNode* child = root->getChild(i);
        if (child != editViewProvider->getTransformNode()) {
            pcEditingRoot->addChild(child);
        }
    }
    coinRemoveAllChildren(root);
    ViewProviderLink::updateLinks(editViewProvider);
}

void ViewerContext::resetEditingRoot(bool updateLinks)
{
    if (!editViewProvider || pcEditingRoot->getNumChildren() <= 1) {
        return;
    }
    if (!restoreEditingRoot) {
        pcEditingRoot->getChildren()->truncate(1);
        return;
    }
    restoreEditingRoot = false;
    auto root = editViewProvider->getRoot();
    if (root->getNumChildren()) {
        FC_ERR("WARNING!!! Editing view provider root node is tampered");
    }
    root->addChild(editViewProvider->getTransformNode());
    for (int i = 1, count = pcEditingRoot->getNumChildren(); i < count; ++i) {
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
            Base::Console().Error("Unexpected exception raised in ViewerContext::resetEditingRoot\n");
        }
    }
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
    SelectionSingleton* sel = context ? context->selectionInstance() : nullptr;
    selection = std::make_unique<SelectionScope>(sel ? *sel : SelectionRoom());
}

ViewerScope::~ViewerScope()
{
    // The selection first, so the two stacks unwind in the order they
    // were pushed.
    selection.reset();
    s_current = previous;
}
