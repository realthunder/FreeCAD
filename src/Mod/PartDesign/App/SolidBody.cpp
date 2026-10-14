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

#include <memory>
#include <set>
#include <sstream>

#include <Standard_Failure.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <App/MappedElement.h>
#include <App/PropertyLinks.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "Body.h"
#include "Feature.h"
#include "SolidBody.h"

FC_LOG_LEVEL_INIT("PartDesign", true, true)

using namespace PartDesign;

namespace {

/// A link of something in a body changed: what the tip of one of its solid
/// bodies is made of may now refer to that solid body
void slotChangedObject(const App::DocumentObject &obj, const App::Property &prop)
{
    static bool busy;
    if (busy || !prop.isDerivedFrom(App::PropertyLinkBase::getClassTypeId()))
        return;
    if (static_cast<const App::PropertyLinkBase&>(prop).getScope() == App::LinkScope::Hidden)
        return;
    if (obj.isDerivedFrom(SolidBody::getClassTypeId())
            || obj.isDerivedFrom(Body::getClassTypeId()))
        return;
    auto doc = obj.getDocument();
    if (!doc || !obj.isAttachedToDocument() || obj.isRestoring()
            || doc->testStatus(App::Document::Restoring)
            || doc->testStatus(App::Document::Recomputing)
            || doc->isPerformingTransaction())
        return;
    auto body = Body::findBodyOf(&obj);
    if (!body)
        return;
    Base::StateLocker guard(busy);
    for (auto solid : body->getSolidBodies())
        solid->keepOutOfItself();
}

/** The element of \a before that the element \a sub of \a shown comes from
 *
 * By the history of the element: a face the features after \a before left
 * alone, or made their own version of, has the one it comes from in every
 * feature before them. False for an element those features made.
 */
bool elementBefore(App::DocumentObject *shown, const App::DocumentObject *before,
                   std::string &sub)
{
    // The feature the names are of: name for name
    if (sub.empty() || shown == before)
        return true;
    if (!shown || sub.find('.') != std::string::npos)
        return false;
    try {
        // Not by asking for a history of one type: that one ends at the
        // first name it cannot find in the shape it started from
        Data::IndexedName picked(sub.c_str());
        if (!picked)
            return false;
        for (auto &item : Part::Feature::getElementHistory(shown, sub.c_str(), true, false)) {
            if (item.obj == before && item.index
                    && strcmp(item.index.getType(), picked.getType()) == 0) {
                sub = item.index.toString();
                return true;
            }
        }
    }
    catch (Base::Exception &) {
    }
    catch (Standard_Failure &) {
    }
    return false;
}

/** Turn what \a link has of \a self, named on the shape of \a shown, into
 * the same of \a before
 *
 * All of it or nothing: a link with one element that comes from nothing of
 * \a before is left as it is. False for a link that names no elements,
 * which is turned as it is.
 */
bool turnElements(App::PropertyLinkBase *link, const App::DocumentObject *self,
                  App::DocumentObject *shown, App::DocumentObject *before)
{
    using SubSet = App::PropertyLinkSubList::SubSet;
    auto one = Base::freecad_dynamic_cast<App::PropertyLinkSub>(link);
    auto list = Base::freecad_dynamic_cast<App::PropertyLinkSubList>(link);
    auto xone = Base::freecad_dynamic_cast<App::PropertyXLink>(link);
    auto xlist = Base::freecad_dynamic_cast<App::PropertyXLinkSubList>(link);
    std::vector<SubSet> sets;
    if (one)
        sets.emplace_back(one->getValue(), one->getSubValues(false));
    else if (list)
        sets = list->getSubListValues(false);
    else if (xone)
        sets.emplace_back(xone->getValue(), xone->getSubValues(false));
    else if (xlist) {
        for (auto obj : xlist->getValues())
            sets.emplace_back(obj, xlist->getSubValues(obj, false));
    }
    else
        return false;

    bool found = false;
    for (auto &set : sets) {
        if (set.first != self)
            continue;
        for (auto &sub : set.second) {
            if (!elementBefore(shown, before, sub))
                return true;
        }
        set.first = before;
        found = true;
    }
    if (!found)
        return true;
    if (one)
        one->setValue(sets.front().first, sets.front().second);
    else if (list)
        list->setSubListValues(sets);
    else if (xone)
        xone->setValue(sets.front().first, sets.front().second);
    else
        xlist->setSubListValues(sets);
    return true;
}

} // anonymous namespace

PROPERTY_SOURCE(PartDesign::SolidBody, Part::Feature)

SolidBody::SolidBody()
{
    // For as long as the application runs: there is nothing to watch until
    // there is a solid body
    static bool connected;
    if (!connected) {
        connected = true;
        App::GetApplication().signalChangedObject.connect(&slotChangedObject);
    }

    ADD_PROPERTY_TYPE(Tip, (nullptr), "Base", App::Prop_None,
            "The feature this solid is rolled back to. The solid has its shape,\n"
            "and a new feature of the solid is inserted after it");
    ADD_PROPERTY_TYPE(Group, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output),
            "The features of this solid");
    ADD_PROPERTY_TYPE(_Body, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    ADD_PROPERTY_TYPE(_Shown, (nullptr), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    ADD_PROPERTY_TYPE(_ShownShape, (false), "Base",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly|App::Prop_Output), 0);
    // View state, as Visibility is: no undo step and no modified document
    // for it
    _Shown.setStatus(App::Property::NoModify, true);
    _ShownShape.setStatus(App::Property::NoModify, true);

    // The shape is in the body's coordinates, as the body's own is
    Placement.setStatus(App::Property::Hidden, true);
    Placement.setStatus(App::Property::Immutable, true);
}

Body *SolidBody::getBody() const
{
    return Base::freecad_dynamic_cast<Body>(_Body.getValue());
}

bool SolidBody::hasFeature(const App::DocumentObject *obj) const
{
    return obj && obj->isAttachedToDocument()
        && Group.find(obj->getNameInDocument()) == obj;
}

bool SolidBody::isSolidVisible() const
{
    if (Visibility.getValue())
        return true;
    for (auto obj : Group.getValues()) {
        if (obj && obj->Visibility.getValue())
            return true;
    }
    return false;
}

App::DocumentObject *SolidBody::getShowTarget() const
{
    if (_ShownShape.getValue())
        return const_cast<SolidBody*>(this);
    auto shown = _Shown.getValue();
    if (shown && hasFeature(shown))
        return shown;
    if (auto tip = Tip.getValue())
        return tip;
    return Group.getSize() ? Group.getValues().back() : nullptr;
}

void SolidBody::rememberShown(const App::DocumentObject *obj)
{
    // A solid body just made has no features yet, and is hidden to leave the
    // drawing to them: nothing was seen through it
    if (!obj || !Group.getSize() || isSolidVisible())
        return;
    // A pinned version takes a change of what is visible, and no other
    auto doc = getDocument();
    if (!doc || doc->testStatus(App::Document::FrozenVersion))
        return;

    bool own = obj == this;
    if (!own && !hasFeature(obj))
        return;
    if (_ShownShape.getValue() != own)
        _ShownShape.setValue(own);
    if (!own) {
        // The tip is not held: a solid last seen through its tip is shown
        // through whichever feature is its tip by then
        auto shown = obj == Tip.getValue() ? nullptr : const_cast<App::DocumentObject*>(obj);
        if (_Shown.getValue() != shown)
            _Shown.setValue(shown);
    }
}

short SolidBody::mustExecute() const
{
    if (Tip.isTouched())
        return 1;
    return inherited::mustExecute();
}

App::DocumentObjectExecReturn *SolidBody::execute()
{
    return updateShape();
}

App::DocumentObjectExecReturn *SolidBody::updateShape()
{
    auto tip = Tip.getValue();
    if (!tip) {
        // A solid that lost every feature is taken away by its body; until
        // then it is empty, which is no error of its own
        Shape.setValue(Part::TopoShape());
        return App::DocumentObject::StdReturn;
    }
    if (!hasFeature(tip))
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip is not a feature of this solid"));
    if (!tip->isDerivedFrom<Part::Feature>())
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Linked object is not a PartDesign feature"));

    Part::TopoShape shape = static_cast<Part::Feature*>(tip)->Shape.getShape();
    if (shape.isNull())
        return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip shape is empty"));

    // The tip's own placement becomes part of the geometry, as in
    // Body::execute()
    shape.transformShape(shape.getTransform(), true);
    Shape.setValue(shape);
    shapeOf = tip->getNameInDocument();
    return App::DocumentObject::StdReturn;
}

void SolidBody::keepOutOfItself()
{
    auto doc = getDocument();
    auto tip = Tip.getValue();
    if (!doc || !isAttachedToDocument() || !tip || !hasFeature(tip))
        return;

    // Nothing refers to it but its body, most of the time
    auto body = _Body.getValue();
    bool referred = false;
    for (auto obj : getInList()) {
        if (obj != body) {
            referred = true;
            break;
        }
    }
    if (!referred) {
        reported.clear();
        return;
    }

    // The feature the shape is of, and with it the names of its elements
    auto shown = shapeOf.empty() ? nullptr : doc->getObject(shapeOf.c_str());

    // Feature by feature up to the tip, each with what it is made of that an
    // earlier one is not: what refers to this object there means the solid
    // before that feature. What comes after the tip may refer to the solid
    // as it is rolled back.
    std::set<App::DocumentObject*> visited;
    visited.insert(this);
    std::string wrong;
    std::ostringstream message;
    for (auto obj : Group.getValues()) {
        auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
        if (feature) {
            auto before = feature->BaseFeature.getValue();
            std::vector<App::DocumentObject*> stack(1, feature);
            while (!stack.empty()) {
                auto cur = stack.back();
                stack.pop_back();
                if (!cur || !cur->isAttachedToDocument() || !visited.insert(cur).second)
                    continue;
                bool refers = false;
                for (auto out : cur->getOutList()) {
                    if (out == this)
                        refers = true;
                    else
                        stack.push_back(out);
                }
                if (!refers)
                    continue;
                if (before && hasFeature(before)) {
                    std::vector<App::Property*> props;
                    cur->getPropertyList(props);
                    for (auto prop : props) {
                        auto link = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop);
                        if (!link || link->getScope() == App::LinkScope::Hidden)
                            continue;
                        std::vector<App::DocumentObject*> links;
                        link->getLinks(links, true);
                        if (std::find(links.begin(), links.end(), this) == links.end())
                            continue;
                        // The names of the elements are those of the feature
                        // this object has the shape of. A later one than the
                        // feature before: each by what it comes from. That
                        // feature itself and linked beside this object, as
                        // by a second face picked after the first was
                        // turned: set in place, where a replacement would
                        // swap the two
                        const bool beside
                            = std::find(links.begin(), links.end(), before) != links.end();
                        if ((before != shown || beside)
                                && turnElements(link, this, shown, before))
                            continue;
                        // No elements to go by
                        if (beside)
                            continue;
                        std::unique_ptr<App::Property> copy(
                                link->CopyOnLinkReplace(cur, this, before));
                        if (copy)
                            link->Paste(*copy);
                    }
                }
                auto left = cur->getOutList();
                if (std::find(left.begin(), left.end(), this) != left.end()) {
                    wrong += cur->getNameInDocument();
                    wrong += ' ';
                    message << cur->getFullName() << " refers to " << getFullName()
                            << ", and " << feature->getFullName() << " of that solid"
                               " is made of it: a solid cannot be made of what"
                               " refers to it, and what is referred to is nothing the"
                               " solid had before that feature. Refer to a feature of"
                               " the solid before "
                            << feature->getNameInDocument() << " instead.\n";
                }
            }
        }
        if (obj == tip)
            break;
    }
    // Said once, not with every link that changes while it stands
    if (wrong != reported) {
        reported = wrong;
        if (!wrong.empty())
            FC_ERR(message.str());
    }
}

App::DocumentObject *SolidBody::referenceFrom(const App::DocumentObject *user,
                                              std::string &sub) const
{
    auto self = const_cast<SolidBody*>(this);
    auto doc = getDocument();
    auto tip = Tip.getValue();
    if (!user || !doc || !isAttachedToDocument() || !tip || !hasFeature(tip))
        return self;

    // As keepOutOfItself() goes: a feature has what no earlier one has
    std::set<App::DocumentObject*> visited;
    visited.insert(self);
    for (auto obj : Group.getValues()) {
        if (auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(obj)) {
            std::vector<App::DocumentObject*> stack(1, feature);
            while (!stack.empty()) {
                auto cur = stack.back();
                stack.pop_back();
                if (!cur || !cur->isAttachedToDocument() || !visited.insert(cur).second)
                    continue;
                if (cur == user) {
                    auto before = feature->BaseFeature.getValue();
                    if (!before || !hasFeature(before))
                        return nullptr;
                    auto shown = shapeOf.empty() ? nullptr : doc->getObject(shapeOf.c_str());
                    return elementBefore(shown, before, sub) ? before : nullptr;
                }
                for (auto out : cur->getOutList())
                    stack.push_back(out);
            }
        }
        if (obj == tip)
            break;
    }
    return self;
}

void SolidBody::onChanged(const App::Property* prop)
{
    auto doc = getDocument();
    if (doc && !isRestoring()
            && !doc->testStatus(App::Document::Restoring)
            && !doc->isPerformingTransaction()) {
        if (prop == &Tip) {
            // Body.Tip stays the tip feature of the solid that is the body's
            // tip, which is all an older build knows of either
            auto tip = Tip.getValue();
            auto body = getBody();
            if (tip && body && hasFeature(tip)) {
                auto bodyTip = body->Tip.getValue();
                if (bodyTip && bodyTip != tip && hasFeature(bodyTip))
                    body->Tip.setValue(tip);
            }
            keepOutOfItself();
        }
        else if (prop == &Visibility) {
            if (Visibility.getValue()) {
                // The solid is shown either by one of its features or by this
                // object, which has the shape of its tip
                for (auto obj : Group.getValues()) {
                    if (obj && obj->Visibility.getValue())
                        obj->Visibility.setValue(false);
                }
            }
            else
                rememberShown(this);
        }
    }
    inherited::onChanged(prop);

    if (prop == &Group)
        handOn(true, true);
    else if (prop == &ShapeMaterial)
        handOn(true, false);
    else if (prop == &ElementAppearance)
        handOn(false, true);
}

void SolidBody::onDocumentRestored()
{
    // One made before a solid body was given a look when it is made
    if (!ElementAppearance.hasBase(App::PropertyElementAppearance::Face)) {
        try {
            giveDefaultAppearance();
        }
        catch (Base::Exception &e) {
            e.ReportException();
        }
    }
    // And had no material of its own: it is of its features', which were
    // of the body's, and would give them the default one otherwise
    if (auto tip = Base::freecad_dynamic_cast<Part::Feature>(Tip.getValue())) {
        if (hasFeature(tip) && tip->ShapeMaterial.getValue().getUUID()
                                    != ShapeMaterial.getValue().getUUID())
            ShapeMaterial.setValue(tip->ShapeMaterial.getValue());
    }
    lookGiven = isLookGiven();
    // Saved with the shape of its tip
    if (auto tip = Tip.getValue()) {
        if (hasFeature(tip))
            shapeOf = tip->getNameInDocument();
    }
    inherited::onDocumentRestored();
}

bool SolidBody::isLookGiven() const
{
    using Store = App::PropertyElementAppearance;
    for (auto kind : {Store::Face, Store::Edge, Store::Vertex}) {
        if (ElementAppearance.hasBase(kind) && !ElementAppearance.isFollowingMaterial(kind))
            return true;
    }
    return false;
}

void SolidBody::handOn(bool card, bool look)
{
    // Given, or given until now: its features go back to what it has instead
    bool handLook = false;
    if (look) {
        bool given = isLookGiven();
        handLook = given || lookGiven;
        lookGiven = given;
    }

    auto doc = getDocument();
    if (!doc || !isAttachedToDocument() || isRestoring()
            || doc->testStatus(App::Document::Restoring)
            || doc->isPerformingTransaction())
        return;

    for (auto obj : Group.getValues()) {
        auto feature = Base::freecad_dynamic_cast<PartDesign::Feature>(obj);
        if (!feature)
            continue;
        if (card && feature->ShapeMaterial.getValue().getUUID()
                        != ShapeMaterial.getValue().getUUID())
            feature->ShapeMaterial.setValue(ShapeMaterial.getValue());
        if (handLook)
            feature->followSolidBodyLook(true);
    }
}

App::DocumentObject *SolidBody::getSubObject(const char *subname,
        PyObject **pyObj, Base::Matrix4D *pmat, bool transform, int depth) const
{
    // A path entering here names a member of the body, as one entering a
    // feature does (Feature::getSubObject()). Placement is identity, so there
    // is nothing of this object's to cancel.
    if (subname && subname != Data::findElementName(subname)) {
        const char *dot = strchr(subname, '.');
        if (dot) {
            if (auto body = getBody()) {
                if (auto feat = body->Group.find(std::string(subname, dot)))
                    return feat->getSubObject(dot+1, pyObj, pmat, true, depth+1);
            }
        }
    }
    return inherited::getSubObject(subname, pyObj, pmat, transform, depth);
}
