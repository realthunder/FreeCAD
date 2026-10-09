/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2012 Luke Parry <l.parry@warwick.ac.uk>                 *
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
#include <fastsignals/signal.h>
#include <fastsignals/signal.h>
#include <QApplication>
#include <QTimer>
#endif

#include <climits>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>

#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawView.h>
#include <Mod/TechDraw/App/Preferences.h>

#include "ViewProviderDrawingView.h"
#include "ViewProviderDrawingViewExtension.h"
#include "MDIViewPage.h"
#include "QGIView.h"
#include "QGSPage.h"
#include "ViewProviderPage.h"

using namespace TechDrawGui;
namespace sp = std::placeholders;

PROPERTY_SOURCE(TechDrawGui::ViewProviderDrawingView, Gui::ViewProviderDocumentObject)

ViewProviderDrawingView::ViewProviderDrawingView() :
    m_myName(std::string())
{
//    Base::Console().Message("VPDV::VPDV\n");
    initExtension(this);

    sPixmap = "TechDraw_TreeView";
    static const char *group = "Base";

    ADD_PROPERTY_TYPE(KeepLabel ,(false), group, App::Prop_None, "Keep Label on Page even if toggled off");
    ADD_PROPERTY_TYPE(StackOrder,(0),group,App::Prop_None,"Over or under lap relative to other views");

    // Do not show in property editor   why? wf  WF: because DisplayMode applies only to coin and we
    // don't use coin.
    DisplayMode.setStatus(App::Property::Hidden, true);
}

ViewProviderDrawingView::~ViewProviderDrawingView()
{
}

void ViewProviderDrawingView::attach(App::DocumentObject *pcFeat)
{
//    Base::Console().Message("VPDV::attach(%s)\n", pcFeat->getNameInDocument());
    ViewProviderDocumentObject::attach(pcFeat);

    //NOLINTBEGIN
    auto bnd = std::bind(&ViewProviderDrawingView::onGuiRepaint, this, sp::_1);
    //NOLINTEND
    auto feature = getViewObject();
    if (feature) {
        if (feature->isAttachedToDocument()) {
            // it could happen that feature is not completely in the document yet and getNameInDocument returns
            // nullptr, so we only update m_myName if we got a valid string.
            m_myName = feature->getNameInDocument();
        }
        connectGuiRepaint = feature->signalGuiPaint.connect(bnd);
        //TODO: would be good to start the QGIV creation process here, but no guarantee we actually have
        //      MDIVP or QGVP yet.
        // but parent page might.  we may not be part of the document yet though!
        // :( we're not part of the page yet either!
    } else {
        Base::Console().Warning("VPDV::attach has no Feature!\n");
    }
}

void ViewProviderDrawingView::onChanged(const App::Property *prop)
{
    App::DocumentObject* obj = getObject();
    if (!obj || obj->isRestoring()) {
            Gui::ViewProviderDocumentObject::onChanged(prop);
            return;
    }

    if (prop == &Visibility) {
        //handled by ViewProviderDocumentObject
    } else if (prop == &KeepLabel) {
        QGIView* qgiv = getQView();
        if (qgiv) {
            qgiv->updateView(true);
        }
    }

    if (prop == &StackOrder) {
        QGIView* qgiv = getQView();
        if (qgiv) {
            qgiv->setStack(StackOrder.getValue());
        }
    }

    Gui::ViewProviderDocumentObject::onChanged(prop);
}

void ViewProviderDrawingView::show()
{
    TechDraw::DrawView* obj = getViewObject();
    if (!obj || obj->isRestoring())
        return;

    if (obj->isDerivedFrom<TechDraw::DrawView>()) {
        QGIView* qView = getQView();
        if (qView) {
            qView->draw();
            qView->show();
        }
    }
    ViewProviderDocumentObject::show();
}

void ViewProviderDrawingView::hide()
{
    TechDraw::DrawView* obj = getViewObject();
    if (!obj || obj->isRestoring())
        return;

    if (obj->isDerivedFrom<TechDraw::DrawView>()) {
        QGIView* qView = getQView();
        if (qView) {
            //note: hiding an item in the scene clears its selection status
            //      this confuses Gui::Selection.
            //      So we block selection changes while we are hiding the qgiv
            //      in FC Tree hiding does not change selection state.
            //      block/unblock selection protects against crash in Gui::SelectionSingleton::setVisible
            MDIViewPage* mdi = getMDIViewPage();
            if (mdi) {                  //if there is no mdivp, there is nothing to hide!
                mdi->blockSceneSelection(true);
                qView->hide();
                ViewProviderDocumentObject::hide();
                mdi->blockSceneSelection(false);
            }
        }
    }
}

QGIView* ViewProviderDrawingView::getQView()
{
    TechDraw::DrawView* dv = getViewObject();
    if (!dv) {
        return nullptr;
    }

    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(dv->getDocument());
    if (!guiDoc) {
        return nullptr;
    }

    ViewProviderPage* vpp = getViewProviderPage();
    if (!vpp) {
        return nullptr;
    }

    if (vpp->getQGSPage()) {
        return dynamic_cast<QGIView *>(vpp->getQGSPage()->findQViewForDocObj(getViewObject()));
    }

    return nullptr;
}

bool ViewProviderDrawingView::isShow() const
{
    return Visibility.getValue();
}

void ViewProviderDrawingView::dropObject(App::DocumentObject* docObj)
{
    getViewProviderPage()->dropObject(docObj);
}

void ViewProviderDrawingView::startRestoring()
{
    Gui::ViewProviderDocumentObject::startRestoring();
}

//! convert old style transparency values in PropertyColor to new style alpha
//! channel values (upstream's, with the fork's own test for who wrote the file).
//!
//! The fourth component of a colour was a transparency nothing in TechDraw
//! looked at, and documents hold it as 0. It is an opacity since the colour
//! conversion started carrying it (189e3b629a), so a dimension, a balloon or a
//! hatch restored from such a document was drawn with no opacity at all: there,
//! selectable, and invisible.
void ViewProviderDrawingView::fixColorAlphaValues()
{
    fixColorAlphaValues(this);
}

void ViewProviderDrawingView::fixColorAlphaValues(Gui::ViewProviderDocumentObject* vp)
{
    if (!vp || !TechDraw::Preferences::fixColorAlphaOnLoad()) {
        return;
    }
    // Upstream from 1.1 on writes the opacity it means, and such a file is
    // left alone. The fork's files are not among them whatever their number
    // says: its releases are dated ("2025.1020"), its dev builds say 0.22,
    // and both wrote the old form.
    if (App::DocumentObject* obj = vp->getObject()) {
        if (App::Document* doc = obj->getDocument()) {
            int major = 0;
            int minor = 0;
            const bool dated = sscanf(doc->getProgramVersion(), "%d.%d", &major, &minor) == 2
                && major >= 2000;
            if (!dated && (major > 1 || (major == 1 && minor >= 1))) {
                return;
            }
        }
    }

    std::vector<App::Property*> allProperties;
    vp->getPropertyList(allProperties);
    for (App::Property* prop : allProperties) {
        auto colorProp = Base::freecad_dynamic_cast<App::PropertyColor>(prop);
        if (!colorProp) {
            continue;
        }
        // As upstream: a colour with no opacity at all is taken for the old
        // form, on the assumption that nobody draws an invisible dimension
        // on purpose. The preference is the way out for whoever does.
        App::Color color = colorProp->getValue();
        if (color.a == 0.0F) {
            color.a = 1.0F;
            colorProp->setValue(color);
        }
    }
}

void ViewProviderDrawingView::finishRestoring()
{
    fixColorAlphaValues();

    if (Visibility.getValue()) {
        show();
    } else {
        hide();
    }
    Gui::ViewProviderDocumentObject::finishRestoring();

    // A page can be on screen before this view provider exists. A
    // progressive load builds the view providers in slices after the
    // document has opened, and a page that comes back with the window
    // layout is drawn in between: its item for this view found no view
    // provider and drew nothing (QGIViewPart::drawViewPart), the
    // backend's page layer drew the view by its fallback widths
    // (PageFeed::Style), and whatever the view asked to have painted
    // meanwhile had nobody listening -- that connection is made in
    // attach(). Nothing asked again until a recompute did, so a document
    // opened and not recomputed kept such a page, a different one at each
    // open.
    //
    // Asked now that the view provider is whole -- but a turn of the event
    // loop later. The slice that builds the view providers flags the
    // document as restoring while it works, and a page item does not draw
    // a view of a restoring document (QGIViewPart::draw); that is also why
    // the show() above, which does ask the item to draw, drew nothing. By
    // name: the view may be gone by then. Only where the page already has
    // an item for the view; a load that builds its view providers before
    // any page is shown asks nothing.
    TechDraw::DrawView* view = getViewObject();
    if (view && view->isAttachedToDocument() && getQView()) {
        const std::string docName = view->getDocument()->getName();
        const std::string viewName = view->getNameInDocument();
        QTimer::singleShot(0, qApp, [docName, viewName]() {
            App::Document* doc = App::GetApplication().getDocument(docName.c_str());
            auto late = doc
                ? dynamic_cast<TechDraw::DrawView*>(doc->getObject(viewName.c_str()))
                : nullptr;
            if (late) {
                late->requestPaint();
            }
        });
    }
}

void ViewProviderDrawingView::updateData(const App::Property* prop)
{
    //only move the view on X, Y change
    if (prop == &(getViewObject()->X)  ||
        prop == &(getViewObject()->Y) ){
        QGIView* qgiv = getQView();
        if (qgiv) {
            qgiv->QGIView::updateView(true);
        }
    }

    Gui::ViewProviderDocumentObject::updateData(prop);
}

ViewProviderPage* ViewProviderDrawingView::getViewProviderPage() const
{
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(getViewObject()->getDocument());
    if (guiDoc) {
        Gui::ViewProvider* vp = guiDoc->getViewProvider(getViewObject()->findParentPage());
        return dynamic_cast<ViewProviderPage*>(vp);
    }
    return nullptr;
}

MDIViewPage* ViewProviderDrawingView::getMDIViewPage() const
{
    ViewProviderPage* vpp = getViewProviderPage();
    if (vpp) {
        return vpp->getMDIViewPage();
    }
    return nullptr;
}

Gui::MDIView *ViewProviderDrawingView::getMDIView() const
{
    return getMDIViewPage();
}

void ViewProviderDrawingView::onGuiRepaint(const TechDraw::DrawView* dv)
{
//    Base::Console().Message("VPDV::onGuiRepaint(%s) - this: %x\n", dv->getNameInDocument(), this);
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(getViewObject()->getDocument());
    if (!guiDoc)
        return;

    std::vector<TechDraw::DrawPage*> pages = getViewObject()->findAllParentPages();
    if (pages.size() > 1) {
        multiParentPaint(pages);
    } else if (dv == getViewObject()) {
        singleParentPaint(dv);
    }
}

void ViewProviderDrawingView::multiParentPaint(std::vector<TechDraw::DrawPage*>& pages)
{
    for (auto& p : pages) {
        std::vector<App::DocumentObject*> views = p->Views.getValues();
        for (auto& v: views) {
            if (v != getViewObject()) {  //should this be dv from onGuiRepaint?
                continue;
            }
            //view v belongs to this page p
            ViewProviderPage* vpPage = getViewProviderPage();
            if (!vpPage) {
                continue;
            }
            if (vpPage->getQGSPage()) {
                QGIView* qView = dynamic_cast<QGIView *>(vpPage->getQGSPage()->findQViewForDocObj(v));
                if (qView) {
                    qView->updateView(true);
                }
            }
        }
    }
}

void ViewProviderDrawingView::singleParentPaint(const TechDraw::DrawView* dv)
{
    //original logic for 1 view on 1 page
    if (dv->isRemoving() ||
        dv->isRestoring()) {
        return;
    }
    QGIView* qgiv = getQView();
    if (qgiv) {
        qgiv->updateView(true);
    } else {       //we are not part of the Gui page yet. ask page to add us.
        ViewProviderPage* vpPage = getViewProviderPage();
        if (vpPage) {
            if (vpPage->getQGSPage()) {
                vpPage->getQGSPage()->addView(dv);
            }
        }
    }
}

void ViewProviderDrawingView::stackUp()
{
    QGIView* v = getQView();
    if (v) {
        int z = StackOrder.getValue();
        z++;
        StackOrder.setValue(z);
        v->setStack(z);
    }
}

void ViewProviderDrawingView::stackDown()
{
    QGIView* v = getQView();
    if (v) {
        int z = StackOrder.getValue();
        z--;
        StackOrder.setValue(z);
        v->setStack(z);
    }
}

void ViewProviderDrawingView::stackTop()
{
    QGIView* qView = getQView();
    if (!qView || !getViewProviderPage()) {
        //no view, nothing to stack
        return;
    }
    int maxZ = INT_MIN;
    auto parent = qView->parentItem();
    if (parent) {
        //if we have a parentItem, we have to stack within the parentItem, not within the page
        auto siblings = parent->childItems();
        for (auto& child : siblings) {
            if (child->zValue() > maxZ) {
                maxZ = child->zValue();
            }
        }
    } else {
        //if we have no parentItem, we are a top level QGIView and we need to stack
        //with respect to the other top level views on this page
        std::vector<App::DocumentObject*> peerObjects = getViewProviderPage()->claimChildren();
        Gui::Document* gDoc = getDocument();
        for (auto& peer: peerObjects) {
            auto vpPeer = gDoc->getViewProvider(peer);
            ViewProviderDrawingView* vpdv = static_cast<ViewProviderDrawingView*>(vpPeer);
            int z = vpdv->StackOrder.getValue();
            if (z > maxZ) {
                maxZ = z;
            }
        }
    }
    StackOrder.setValue(maxZ + 1);
    qView->setStack(maxZ + 1);
}

void ViewProviderDrawingView::stackBottom()
{
    QGIView* qView = getQView();
    if (!qView || !getViewProviderPage()) {
        //no view, nothing to stack
        return;
    }
    int minZ = INT_MAX;
    auto parent = qView->parentItem();
    if (parent) {
        //if we have a parentItem, we have to stack within the parentItem, not within the page
        auto siblings = parent->childItems();
        for (auto& child : siblings) {
            if (child->zValue() < minZ) {
                minZ = child->zValue();
            }
        }
    } else {
        //TODO: need to special case DPGI or any other member of a collection
        //if we have no parentItem, we are a top level QGIView and we need to stack
        //with respect to the other top level views on this page
        std::vector<App::DocumentObject*> peerObjects = getViewProviderPage()->claimChildren();
        Gui::Document* gDoc = getDocument();
        for (auto& peer: peerObjects) {
            auto vpPeer = gDoc->getViewProvider(peer);
            ViewProviderDrawingView* vpdv = static_cast<ViewProviderDrawingView*>(vpPeer);
            int z = vpdv->StackOrder.getValue();
            if (z < minZ) {
                minZ = z;
            }
        }
    }
    StackOrder.setValue(minZ - 1);
    qView->setStack(minZ - 1);
}

const char*  ViewProviderDrawingView::whoAmI() const
{
    return m_myName.c_str();
}

TechDraw::DrawView* ViewProviderDrawingView::getViewObject() const
{
    return dynamic_cast<TechDraw::DrawView*>(pcObject);
}
