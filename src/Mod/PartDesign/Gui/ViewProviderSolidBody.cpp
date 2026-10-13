/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QAction>
# include <QMenu>
# include <QMessageBox>
# include <QTimer>
# include <Inventor/SoFullPath.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/OwnNodesPath.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Tree.h>
#include <Gui/ViewParams.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Mod/PartDesign/App/SolidBody.h>

#include "ViewProviderSolidBody.h"
#include "ViewProviderBody.h"
#include "ViewProvider.h"

using namespace PartDesignGui;

const QByteArray &PartDesignGui::solidTipIconTag()
{
    static QByteArray tag("PartDesign::SolidTip");
    return tag;
}

const QByteArray &PartDesignGui::bodyTipIconTag()
{
    static QByteArray tag("PartDesign::BodyTip");
    return tag;
}

const QByteArray &PartDesignGui::featureTagIconTag()
{
    static QByteArray tag("PartDesign::Tag");
    return tag;
}

bool PartDesignGui::isSolidTip(const PartDesign::Body *body, const App::DocumentObject *feature)
{
    if (!body || !feature)
        return false;
    if (auto solid = body->getSolidBody(feature))
        return solid->Tip.getValue() == feature;
    return body->Tip.getValue() == feature;
}

bool PartDesignGui::canDropTipIcon(App::DocumentObject *source,
                                   const QByteArray &iconTag,
                                   App::DocumentObject *target)
{
    if (!source || !target || source == target)
        return false;
    auto body = PartDesign::Body::findBodyOf(target);
    if (!body || PartDesign::Body::findBodyOf(source) != body)
        return false;
    auto targetSolid = Base::freecad_dynamic_cast<PartDesign::SolidBody>(target);

    if (iconTag == bodyTipIconTag()) {
        // Onto another solid, or onto a feature of any solid: that solid
        // becomes the body's tip, rolled back to the feature
        auto sourceSolid = Base::freecad_dynamic_cast<PartDesign::SolidBody>(source);
        if (!sourceSolid)
            return false;
        if (targetSolid)
            return targetSolid->Group.getSize() > 0;
        return body->isSolidFeature(target) && body->Tip.getValue() != target;
    }

    if (targetSolid || !body->isSolidFeature(target))
        return false;

    if (iconTag == solidTipIconTag()) {
        // Within the solid only: another solid has a tip of its own
        if (auto solid = body->getSolidBody(source))
            return solid->hasFeature(target);
        return body->isSibling(source, target);
    }

    if (iconTag == featureTagIconTag()) {
        // The one tip of a body without solid bodies goes anywhere in it
        return !body->AddSolidBody.getValue() && body->Tip.getValue() == source;
    }
    return false;
}

void PartDesignGui::dropTipIcon(App::DocumentObject *source,
                                const QByteArray &iconTag,
                                App::DocumentObject *target)
{
    if (!canDropTipIcon(source, iconTag, target))
        return;
    auto body = PartDesign::Body::findBodyOf(target);

    App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Move tip"));
    try {
        auto solid = body->getSolidBody(source);
        if (iconTag == solidTipIconTag() && solid)
            FCMD_OBJ_CMD(solid, "Tip = " << Gui::Command::getObjectCmd(target));
        else
            FCMD_OBJ_CMD(body, "Tip = " << Gui::Command::getObjectCmd(target));

        // The solid is now seen through the feature it is rolled back to
        App::DocumentObject *shown = target;
        if (auto targetSolid = Base::freecad_dynamic_cast<PartDesign::SolidBody>(target))
            shown = targetSolid->Tip.getValue();
        if (shown)
            FCMD_OBJ_SHOW(shown);
        Gui::Command::updateActive();
    }
    catch (Base::Exception &e) {
        e.ReportException();
        committer.close(true);
    }
}

SolidBodyHighlight::SolidBodyHighlight(const App::DocumentObject *feature)
{
    if (!feature || !feature->isAttachedToDocument() || feature->isRestoring())
        return;
    auto body = PartDesign::Body::findBodyOf(feature);
    auto solid = body ? body->getSolidBody(feature) : nullptr;
    if (!solid || !Gui::Selection().isSelected(
                solid, "", Gui::ResolveMode::OldStyleElement))
        return;

    // The solid body or an element of it, by whichever path it was selected.
    // Taken off while the path still ends where the highlight was put.
    for (auto &sel : Gui::Selection().getSelectionT(
                solid->getDocument()->getName(), Gui::ResolveMode::NoResolve)) {
        if (sel.getSubObject() != solid)
            continue;
        Gui::Selection().updateSelection(false, sel.getDocumentName().c_str(),
                sel.getObjectName().c_str(), sel.getSubName().c_str());
        selections.push_back(std::move(sel));
    }
}

SolidBodyHighlight::~SolidBodyHighlight()
{
    // As a shown object gets its own highlight back
    // (ViewProviderDocumentObject::show())
    if (selections.empty() || !Gui::ViewParams::getUpdateSelectionVisual())
        return;
    for (auto &sel : selections) {
        Gui::Selection().updateSelection(true, sel.getDocumentName().c_str(),
                sel.getObjectName().c_str(), sel.getSubName().c_str());
    }
}

namespace {

/** Puts back a highlight that went with the one of another selection
 *
 * A solid body and the feature it is seen through are highlighted on the same
 * nodes. Under the render type "Legacy" a highlight is taken off the nodes
 * themselves, so with both selected, deselecting one clears what the other
 * still has to show. The render engine keeps a highlight by its selection,
 * and needs none of this.
 */
class SolidBodySelectionObserver : public Gui::SelectionObserver
{
public:
    SolidBodySelectionObserver()
        : Gui::SelectionObserver(true, Gui::ResolveMode::NoResolve)
    {}

    void onSelectionChanged(const Gui::SelectionChanges &msg) override
    {
        if (msg.Type != Gui::SelectionChanges::RmvSelection
                || Gui::ViewParams::isUsingRenderer())
            return;
        auto removed = msg.Object.getSubObject();
        if (!removed)
            return;

        // What shares the nodes with it: the features of a solid body, the
        // solid body of a feature
        std::vector<App::DocumentObject*> others;
        if (auto solid = Base::freecad_dynamic_cast<PartDesign::SolidBody>(removed))
            others = solid->Group.getValues();
        else {
            auto body = PartDesign::Body::findBodyOf(removed);
            if (auto solid = body ? body->getSolidBody(removed) : nullptr)
                others.push_back(solid);
        }
        bool selected = false;
        for (auto obj : others) {
            if (obj && Gui::Selection().isSelected(
                        obj, "", Gui::ResolveMode::OldStyleElement)) {
                selected = true;
                break;
            }
        }
        if (!selected)
            return;

        std::vector<App::SubObjectT> again;
        for (auto &sel : Gui::Selection().getSelectionT(
                    msg.Object.getDocumentName().c_str(), Gui::ResolveMode::NoResolve)) {
            if (std::find(others.begin(), others.end(), sel.getSubObject()) != others.end())
                again.push_back(std::move(sel));
        }
        if (again.empty())
            return;
        // Once every view has taken the highlight off. Hidden first: a view
        // still holds the selection as shown, and shows it again only after
        // it was told otherwise. One no longer selected by then is left out
        // by the selection itself.
        QTimer::singleShot(0, QCoreApplication::instance(), [again]() {
            for (auto &sel : again) {
                for (bool show : {false, true}) {
                    Gui::Selection().updateSelection(show, sel.getDocumentName().c_str(),
                            sel.getObjectName().c_str(), sel.getSubName().c_str());
                }
            }
        });
    }
};

/** What a solid body is seen through: its features, drawn beside it
 *
 * A view's own hide of the solid body hides the solid whichever feature
 * shows it, and a shader bound to the solid body dresses it likewise. Shown
 * by a view, the solid body shows its own shape, which is the solid: of the
 * features only one is to be seen at a time, and which is not a view's to
 * say. One face of it is a face of its tip, whose shape it has, and of no
 * earlier feature.
 */
std::vector<App::DocumentObject*> seenThroughFeatures(const App::DocumentObject *obj,
                                                      Gui::SeenThrough::For what)
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(obj);
    if (!owner)
        return {};
    if (what == Gui::SeenThrough::For::Show)
        return {const_cast<App::DocumentObject*>(obj)};
    if (what == Gui::SeenThrough::For::Element) {
        if (auto tip = owner->Tip.getValue())
            return {tip};
        return {};
    }
    return owner->Group.getValues();
}

} // anonymous namespace

PROPERTY_SOURCE(PartDesignGui::ViewProviderSolidBody, PartGui::ViewProviderPart)

ViewProviderSolidBody::ViewProviderSolidBody()
{
    sPixmap = "PartDesign_SolidBody.svg";

    // Lives as long as the application: the selection it listens to is gone
    // before a static of this library would be
    static auto observer = new SolidBodySelectionObserver;
    (void)observer;

    static bool registered;
    if (!registered) {
        registered = true;
        Gui::SeenThrough::add(PartDesign::SolidBody::getClassTypeId(), &seenThroughFeatures);
    }
}

ViewProviderSolidBody::~ViewProviderSolidBody() = default;

std::vector<App::DocumentObject*> ViewProviderSolidBody::claimChildren() const
{
    auto res = inherited::claimChildren();
    if (auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject())) {
        const auto &features = owner->Group.getValues();
        res.insert(res.end(), features.begin(), features.end());
    }
    return res;
}

void ViewProviderSolidBody::updateData(const App::Property *prop)
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
    if (owner && (prop == &owner->Group || prop == &owner->Tip)) {
        // The arrows of the features follow: which one is the tip, and
        // whether a feature is under a solid body at all
        std::set<App::DocumentObject*> features(owner->Group.getValues().begin(),
                                                owner->Group.getValues().end());
        features.insert(owner->Tip.getValue());
        auto body = owner->getBody();
        if (body) {
            for (auto obj : body->Group.getValues()) {
                if (obj && obj->isDerivedFrom<PartDesign::Feature>())
                    features.insert(obj);
            }
        }
        for (auto obj : features) {
            if (auto vp = Gui::Application::Instance->getViewProvider(obj))
                vp->signalChangeIcon();
        }
        signalChangeIcon();

        auto doc = owner->getDocument();
        if (prop == &owner->Group && body && doc
                && !doc->isPerformingTransaction()
                && !doc->testStatus(App::Document::Restoring)) {
            // What the body shows at its top level depends on what is
            // claimed here
            body->_GroupTouched.touch();
        }
    }
    inherited::updateData(prop);
}

void ViewProviderSolidBody::getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &icons) const
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
    auto body = owner ? owner->getBody() : nullptr;
    if (body && body->getTipSolidBody() == owner) {
        icons.emplace_back(bodyTipIconTag(),
                Gui::BitmapFactory().pixmapFromSvg("PartDesign_BodyTipArrow.svg", QSizeF(48, 64)));
    }
    inherited::getExtraIcons(icons);
}

QString ViewProviderSolidBody::getToolTip(const QByteArray &iconTag) const
{
    if (iconTag == bodyTipIconTag() && !Gui::isTreeViewDragging()) {
        return tr("Body tip: this solid is the tip of the body. The body has its shape,\n"
                  "and a new feature is added to it, after the solid's own tip.\n\n"
                  "Drag this arrow onto another solid to make that the tip of the body,\n"
                  "or onto a feature of any solid to also roll that solid back to it.");
    }
    return inherited::getToolTip(iconTag);
}

bool ViewProviderSolidBody::canDragIcon(const QByteArray &iconTag) const
{
    return iconTag == bodyTipIconTag();
}

bool ViewProviderSolidBody::canDropIcon(App::DocumentObject *source, const QByteArray &iconTag) const
{
    return canDropTipIcon(source, iconTag, getObject());
}

void ViewProviderSolidBody::dropIcon(App::DocumentObject *source, const QByteArray &iconTag)
{
    dropTipIcon(source, iconTag, getObject());
}

static Gui::ViewProvider *bodyViewProvider(const App::DocumentObject *obj)
{
    return Gui::Application::Instance->getViewProvider(PartDesign::Body::findBodyOf(obj));
}

bool ViewProviderSolidBody::canReplaceObject(App::DocumentObject *oldObj, App::DocumentObject *newObj)
{
    auto vp = bodyViewProvider(getObject());
    return vp && vp->canReplaceObject(oldObj, newObj);
}

int ViewProviderSolidBody::replaceObject(App::DocumentObject *oldObj, App::DocumentObject *newObj)
{
    auto vp = bodyViewProvider(getObject());
    return vp ? vp->replaceObject(oldObj, newObj) : 0;
}

bool ViewProviderSolidBody::canReorderObject(App::DocumentObject *obj, App::DocumentObject *before)
{
    auto vp = bodyViewProvider(getObject());
    return vp && vp->canReorderObject(obj, before);
}

bool ViewProviderSolidBody::reorderObjects(const std::vector<App::DocumentObject *> &objs,
                                           App::DocumentObject *before)
{
    // The features are listed here in the body's own order, so 'before'
    // means what it means to the body
    auto vp = bodyViewProvider(getObject());
    return vp && vp->reorderObjects(objs, before);
}

Gui::ViewProviderDocumentObject *ViewProviderSolidBody::shownFeature() const
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
    if (!owner)
        return nullptr;

    // The tip where it is shown: it has the shape the solid body has. Else
    // the latest of the features that are.
    Gui::ViewProviderDocumentObject *shown = nullptr;
    for (auto obj : owner->Group.getValues()) {
        auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(obj));
        if (!vp || !vp->isShow())
            continue;
        if (obj == owner->Tip.getValue())
            return vp;
        shown = vp;
    }
    return shown;
}

bool ViewProviderSolidBody::getDetailPath(
        const char *subname, SoFullPath *path, bool append, SoDetail *&det) const
{
    if (!subname)
        subname = "";

    // Only the solid itself and an element of it: a path going on into a
    // feature is followed by the base class. And only where the features are
    // drawn beside this object, under the group the path has come to. A Link
    // to the solid body draws a copy of the nodes of this object alone, with
    // a visibility of its own, and is left with those. So is a caller that
    // keeps something on the path and asks for this object's own nodes.
    if (append && !isShow() && path->getLength()
            && !Gui::OwnNodesPath::wanted()
            && (!*subname || Data::isElementName(subname))
            && !Data::hasMissingElement(subname)) {
        auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
        auto vp = shownFeature();
        SoNode *tail = path->getTail();
        if (owner && vp && tail->isOfType(SoGroup::getClassTypeId())
                && static_cast<SoGroup*>(tail)->findChild(vp->getRoot()) >= 0) {
            std::string element;
            if (*subname && vp->getObject() == owner->Tip.getValue()) {
                // The shape here is that of the tip, element for element.
                // A mapped name is one of this shape, so it is turned into
                // the indexed one here.
                auto indexed = owner->Shape.getShape().getElementName(subname).index;
                if (indexed)
                    element = indexed.toString();
            }
            // An element of the solid on a feature that is not its tip is
            // not to be found there: the solid, whole
            return vp->getDetailPath(element.c_str(), path, true, det);
        }
    }
    return inherited::getDetailPath(subname, path, append, det);
}

bool ViewProviderSolidBody::canDelete(App::DocumentObject *) const
{
    // A feature leaving its solid is nothing to warn about: the solid body
    // only lists it
    return true;
}

bool ViewProviderSolidBody::onDelete(const std::vector<std::string> &)
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
    auto body = owner ? owner->getBody() : nullptr;
    if (!owner || !body)
        return true;

    // The body's tip moves to another solid first, where there is one
    if (body->getTipSolidBody() == owner) {
        PartDesign::SolidBody *other = nullptr;
        for (auto solid : body->getSolidBodies()) {
            if (solid != owner && solid->Group.getSize()) {
                other = solid;
                break;
            }
        }
        if (other)
            FCMD_OBJ_CMD(body, "Tip = " << Gui::Command::getObjectCmd(other));
        else
            FCMD_OBJ_CMD(body, "Tip = None");
    }

    // Then the features go, latest first. Taken from the document, not out
    // of the body one by one: nothing of this solid is left to be given a
    // new base, and the solid body itself has to last until the caller
    // removes it. The body catches up at the recompute that follows.
    auto features = owner->Group.getValues();
    for (auto rit = features.rbegin(); rit != features.rend(); ++rit) {
        auto obj = *rit;
        if (obj && obj->isAttachedToDocument())
            Gui::Command::doCommand(Gui::Command::Doc,
                    "App.getDocument('%s').removeObject('%s')",
                    obj->getDocument()->getName(), obj->getNameInDocument());
    }
    return true;
}

void ViewProviderSolidBody::setupContextMenu(QMenu *menu, QObject *receiver, const char *member)
{
    auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
    auto body = owner ? owner->getBody() : nullptr;
    if (owner && body) {
        auto func = new Gui::ActionFunction(menu);

        if (body->getTipSolidBody() != owner && owner->Group.getSize()) {
            QAction *act = menu->addAction(tr("Set as body tip"));
            act->setToolTip(tr("Make this solid the tip of the body: the body takes its shape,\n"
                               "and new features are added to it"));
            func->trigger(act, [this]() {
                auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
                auto body = owner ? owner->getBody() : nullptr;
                if (!body)
                    return;
                App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Move tip"));
                try {
                    FCMD_OBJ_CMD(body, "Tip = " << Gui::Command::getObjectCmd(owner));
                    if (auto tip = owner->Tip.getValue())
                        FCMD_OBJ_SHOW(tip);
                    Gui::Command::updateActive();
                }
                catch (Base::Exception &e) {
                    e.ReportException();
                    committer.close(true);
                }
            });
        }

        QAction *act = menu->addAction(tr("Show solid shape"));
        act->setToolTip(tr("Show the shape of the solid body itself in place of its features,\n"
                           "to pick a face or an edge that stays with the solid whichever\n"
                           "feature is its tip"));
        act->setCheckable(true);
        act->setChecked(owner->Visibility.getValue());
        func->trigger(act, [this]() {
            auto owner = Base::freecad_dynamic_cast<PartDesign::SolidBody>(getObject());
            if (!owner)
                return;
            App::AutoTransaction committer(QT_TRANSLATE_NOOP("Command", "Show solid shape"));
            try {
                if (owner->Visibility.getValue()) {
                    FCMD_OBJ_HIDE(owner);
                    if (auto tip = owner->Tip.getValue())
                        FCMD_OBJ_SHOW(tip);
                }
                else
                    FCMD_OBJ_SHOW(owner);
            }
            catch (Base::Exception &e) {
                e.ReportException();
                committer.close(true);
            }
        });
    }
    inherited::setupContextMenu(menu, receiver, member);
}

bool ViewProviderSolidBody::setEdit(int ModNum)
{
    // Nothing of a solid body is edited: its features are, one by one
    if (ModNum == ViewProvider::Default)
        return false;
    return inherited::setEdit(ModNum);
}
