/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

/** @file
 * What a Part::Feature's faces, edges and vertices look like, made by the
 * object (docs/ShapeAppearanceDesign.md sec 14.6.2).
 *
 * ElementAppearance states the object's own look, the looks given to
 * elements by number and the looks given by name. What is drawn is made of
 * those here -- each name at the elements it is now, and, where the Map*
 * properties say so, a face no name paints in the look of the face it was
 * made from -- and kept in the same property.
 *
 * This is PartGui::ViewProviderPartExt::updateColors() and what is below
 * it, moved to where it runs without a view provider. It reads the objects
 * the shape was made from and never their view providers.
 */

#include "PreCompiled.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Standard_Failure.hxx>

#include <App/AppearanceList.h>
#include <App/AppearanceUpdater.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/LinkAppearance.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/Tools.h>
#include <Mod/Material/App/MaterialManager.h>

#include "PartFeature.h"
#include "TopoShape.h"

FC_LOG_LEVEL_INIT("Part", true, true)

using namespace Part;

namespace
{

using Store = App::PropertyElementAppearance;
using Kind = Store::Kind;

const TopAbs_ShapeEnum KindTypes[Store::KindCount] = {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX};

Feature::LinkLookFunc _linkLook;

/// The colour a look is drawn in, its transparency in the alpha
App::Color colorOf(const App::MaterialAppearance &look)
{
    return App::AppearanceList::storedDiffuse(look);
}

/// \a look in another colour
App::MaterialAppearance inColor(App::MaterialAppearance look, const App::Color &color)
{
    look.diffuseColor = color;
    look.transparency = color.transparency();
    return look;
}

/** A face's look, where it has more of its own than a colour
 *
 * What a face made from it takes whole (docs/ShapeAppearanceDesign.md sec
 * 13.4 Q5). A face that was only painted hands on its colour, as it always
 * has, and the face made from it keeps its own object's gloss.
 */
bool ownLook(const App::AppearanceList &list, int index, App::MaterialAppearance &look)
{
    if (index < 0 || index >= list.getSize() || !list.isOverride(index)) {
        return false;
    }
    look = list.getMaterial(index);
    return (Store::differingFields(look, list.getBase()) & ~Store::OwnDiffuse) != 0;
}

/// An object the shape was made from
struct Source
{
    TopoShape shape;
    App::GeoFeature *geo {nullptr};
    bool inited {false};
};

/// The looks of the elements of one source, asked for once a kind
struct Looks
{
    /// 0 not asked, 1 kept, -1 the source keeps none
    std::array<signed char, Store::KindCount> asked {};
    std::array<App::AppearanceList, Store::KindCount> lists;
};

struct Caches
{
    std::map<App::DocumentObject *, Source> sources;
    /// By the object, or by the view provider a link draws through
    std::map<const void *, Looks> looks;
};

/** The looks a link draws through a view provider of its own
 *
 * By the names that view provider has them under. This goes with
 * Part::Feature::setLinkLookFunc(), at docs/ShapeAppearanceDesign.md sec
 * 14.6.9 step C.
 */
bool shownLooks(const App::PropertyContainer *shown, Kind kind, App::AppearanceList &list)
{
    if (kind == Store::Face) {
        auto prop = Base::freecad_dynamic_cast<App::PropertyAppearanceList>(
            shown->getPropertyByName("ShapeAppearance"));
        if (!prop) {
            return false;
        }
        list = prop->getList();
        return true;
    }
    auto prop = Base::freecad_dynamic_cast<App::PropertyColorList>(
        shown->getPropertyByName(kind == Store::Edge ? "LineColorArray" : "PointColorArray"));
    if (!prop) {
        return false;
    }
    const std::vector<App::Color> &colors = prop->getValues();
    list = App::AppearanceList();
    list.setSize(static_cast<int>(colors.size()));
    for (std::size_t i = 0; i < colors.size(); ++i) {
        list.setDiffuseColor(static_cast<int>(i), colors[i]);
    }
    return true;
}

const App::AppearanceList *sourceLooks(Caches &caches, const Source &source,
                                       const App::PropertyContainer *shown, Kind kind)
{
    const void *key = shown ? static_cast<const void *>(shown)
                            : static_cast<const void *>(source.geo);
    if (!key) {
        return nullptr;
    }
    Looks &looks = caches.looks[key];
    if (!looks.asked[kind]) {
        const bool kept = shown ? shownLooks(shown, kind, looks.lists[kind])
                                : source.geo->getDrawnAppearance(kind, looks.lists[kind]);
        looks.asked[kind] = kept ? 1 : -1;
    }
    return looks.asked[kind] > 0 ? &looks.lists[kind] : nullptr;
}

/** The colour an element has from where it came
 *
 * \a color is what it is where nothing is found. A face's whole look too
 * where its source face has one (ownLook()): \a whole says so, \a look is
 * it, and the colour returned is that look's.
 */
App::Color sourceColor(App::Color color,
                       const TopoShape &shape,
                       App::Document *doc,
                       Kind kind,
                       const Data::MappedName &name,
                       Caches &caches,
                       App::MaterialAppearance &look,
                       bool &whole)
{
    if (!name) {
        return color;
    }
    const TopAbs_ShapeEnum type = KindTypes[kind];
    Data::MappedName mapped(name);
    std::vector<Data::MappedName> history;
    std::vector<Data::MappedName> prevHistory;
    Data::MappedName original;
    long tag = shape.getElementHistory(mapped, &original, &prevHistory);
    while (true) {
        if (!tag || !doc) {
            return color;
        }
        auto obj = doc->getObjectByID(std::abs(tag));
        if (!obj || !obj->getNameInDocument()) {
            return color;
        }
        Source &source = caches.sources[obj];
        if (!source.inited) {
            source.inited = true;
            source.shape = Feature::getTopoShape(obj);
            source.geo = Base::freecad_dynamic_cast<App::GeoFeature>(obj);
        }
        const TopoShape &shape = source.shape;
        const App::PropertyContainer *shown = nullptr;
        if (shape.isNull()) {
            return color;
        }
        // What a link in the way lays over the element: the link holds it
        // (sec 14.6.4). The Gui answers where it is there, for a link that
        // draws through a view provider of its own.
        const bool laid = _linkLook ? _linkLook(original, obj, shown, color)
                                    : App::LinkAppearance::getLinkColor(original, obj, color);
        if (laid || !obj) {
            return color;
        }
        const App::AppearanceList *list = sourceLooks(caches, source, shown, kind);
        if (!list) {
            // Not an object that keeps looks. No problem, just trace deeper
            // into the history until we find one.
            doc = obj->getDocument();
            mapped = original;
            prevHistory.clear();
            tag = shape.getElementHistory(mapped, &original, &prevHistory);
            continue;
        }
        if (list->getSize() == 0) {
            return color;
        }

        mapped = original;
        // Normally, TopoShape::getElementHistory() returns the mapped element
        // name of the previous step (in 'original'), and tag is the ID of the
        // previous feature. 'mapped' is the mapped element name of the next
        // modeling step in shape history.
        //
        // However, if there are intermediate modeling steps, things get a bit
        // tricky. getElementHistory() returns intermediate element names in
        // 'history', but the last entry of 'history' may actually contain the real
        // mapped element name of the 'current' modeling step. That's why we are
        // calling getElementHistory() for previous modeling step now, before
        // retrieving the element for the current step using getElementName(),
        // because we need to check intermediate history names of the previous
        // model step. The 'original' returned by getElementHistory() here may
        // or may not contain a valid element name for the previous step. We can
        // only decide after another loop hits here.
        std::swap(history, prevHistory);
        prevHistory.clear();
        tag = shape.getElementHistory(mapped, &original, &prevHistory);
        Data::IndexedName indexedName = shape.getIndexedName(mapped);
        if (!indexedName && !history.empty()) {
            indexedName = shape.getIndexedName(history.back());
        }
        auto idx = TopoShape::shapeTypeAndIndex(indexedName);
        if (idx.second <= 0 || idx.second > static_cast<int>(shape.countSubShapes(idx.first))) {
            return color;
        }
        int at = idx.second;
        if (idx.first != type) {
            // This means the element is generated from a different type of
            // source element, e.g. face generated by an edge.
            at = shape.findAncestor(shape.findShape(idx.first, idx.second), type);
            if (at <= 0) {
                return color;
            }
        }
        if (list->getSize() == 1) {
            // Every element of the source its own look: the colour of that
            return colorOf(list->getBase());
        }
        if (at <= list->getSize()) {
            if (kind == Store::Face) {
                whole = ownLook(*list, at - 1, look);
            }
            return colorOf(list->getMaterial(at - 1));
        }
        return color;
    }
}

/** Which elements of a shape the names paint
 *
 * For each type of element, its index from 0 to the place of its name in
 * \a subs. A name the shape has not as it is -- its face split, or cut -- is
 * looked up through what it became.
 */
void namedElements(const App::DocumentObject *obj,
                   const TopoShape &shape,
                   const std::vector<App::PropertyLinkBase::ShadowSub> &subs,
                   std::array<std::map<int, int>, TopAbs_SHAPE> &named)
{
    if (subs.empty()) {
        return;
    }
    std::set<Data::MappedName> subMap;
    for (auto &v : subs) {
        if (v.first.size()) {
            subMap.insert(shape.getElementName(v.first.c_str()).name);
        }
    }
    int i = -1;
    for (auto &v : subs) {
        ++i;
        Data::IndexedName element;
        if (v.first.size()) {
            element = shape.getElementName(v.first.c_str()).index;
        }
        else {
            element = Data::IndexedName(v.second.c_str());
        }
        auto idx = shape.shapeTypeAndIndex(element);
        if (idx.second) {
            named[idx.first][idx.second - 1] = i;
            continue;
        }
        else if (v.first.empty()) {
            continue;
        }

        // Seen through a link the shape's names are not the ones the
        // reference has: they are the linked object's with its tag after
        // them, "Top;:H2a,F" for its "Top". The element is the one the
        // reference leads to in the object that has it, which counts its
        // elements as what is seen of it does.
        {
            std::pair<std::string, std::string> resolved;
            App::GeoFeature *geo = nullptr;
            if (App::GeoFeature::resolveElement(const_cast<App::DocumentObject *>(obj),
                                                v.first.c_str(), resolved, false,
                                                App::GeoFeature::Normal, nullptr, nullptr, &geo)
                && geo && geo != obj && !resolved.first.empty()
                && !App::GeoFeature::hasMissingElement(resolved.second.c_str())) {
                const char *found = Data::findElementName(resolved.second.c_str());
                auto at = shape.shapeTypeAndIndex(Data::IndexedName(found ? found : ""));
                if (at.second) {
                    named[at.first][at.second - 1] = i;
                    continue;
                }
            }
        }

        for (auto &names : Feature::getRelatedElements(const_cast<App::DocumentObject *>(obj),
                                                       v.first.c_str())) {
            if (!subMap.insert(names.name).second) {
                continue;
            }
            auto idx = TopoShape::shapeTypeAndIndex(names.index);
            if (idx.second > 0) {
                named[idx.first][idx.second - 1] = i;
            }
        }
    }
}

}  // namespace

void Feature::setLinkLookFunc(LinkLookFunc func)
{
    _linkLook = func;
}

bool Feature::hasBaseFeature() const
{
    for (auto obj : getOutList(OutListNoHidden | OutListNoExpression)) {
        if (obj != this) {
            return true;
        }
    }
    return false;
}

bool Feature::getDrawnAppearance(int kind, App::AppearanceList &list) const
{
    if (kind < 0 || kind >= Store::KindCount) {
        return false;
    }
    list = ElementAppearance.getDrawn(static_cast<Kind>(kind));
    if (list.getSize() == 0) {
        // Nobody gave the object a look: every element is what one is then
        list.setValue(ElementAppearance.getBase(static_cast<Kind>(kind)));
    }
    return true;
}

void Feature::onSourceAppearanceChanged()
{
    if (MapFaceColor.getValue() || MapLineColor.getValue() || MapPointColor.getValue()
        || MapTransparency.getValue()) {
        updateAppearance();
    }
}

void Feature::updateAppearance(App::Document *sourceDoc, bool forceMap, bool whileRestoring)
{
    App::Document *doc = getDocument();
    if (_updatingAppearance || !doc || !getNameInDocument()
        || (!whileRestoring && doc->testStatus(App::Document::Restoring))) {
        return;
    }
    const TopoShape shape = Shape.getShape();
    if (shape.isNull()) {
        return;
    }
    Base::FlagToggler<> guard(_updatingAppearance);
    if (!sourceDoc) {
        sourceDoc = doc;
    }

    Store &store = ElementAppearance;
    const bool noMap = !ForceMapColors.getValue() && !forceMap && !hasBaseFeature();
    const bool mapKind[Store::KindCount] = {MapFaceColor.getValue(),
                                            MapLineColor.getValue(),
                                            MapPointColor.getValue()};

    ElementLooks looks;
    for (int k = 0; k < Store::KindCount; ++k) {
        looks.own[k] = store.getBase(static_cast<Kind>(k));
        looks.fromSources[k] = !noMap && mapKind[k];
    }
    looks.sourceTransparency = MapTransparency.getValue();
    mapElementLooks(this, shape, store.getShadowSubs(), sourceDoc, looks);

    bool changed = false;
    for (int k = 0; k < Store::KindCount; ++k) {
        const Kind kind = static_cast<Kind>(k);
        const int count = static_cast<int>(shape.countSubShapes(KindTypes[k]));
        const std::map<int, int> &names = looks.named[k];
        std::map<int, App::MaterialAppearance> &handedOn = looks.handedOn[k];

        if (forceMap && !handedOn.empty()) {
            // A copy of another object's shape, made once: nothing makes
            // what it takes again, so it is stated, as an import's is
            store.setNumbered(kind, store.compose(kind, count, {}, &handedOn));
            handedOn.clear();
        }

        const App::AppearanceList before = store.getDrawn(kind);
        store.setDrawn(kind,
                       store.compose(kind,
                                     count,
                                     std::vector<std::pair<int, int>>(names.begin(), names.end()),
                                     handedOn.empty() ? nullptr : &handedOn));
        if (!changed && !before.isSame(store.getDrawn(kind))) {
            changed = true;
        }
    }
    if (changed) {
        App::AppearanceUpdater::addObject(this);
    }
    mirrorLooks();
}

void Feature::mapElementLooks(const App::DocumentObject *owner,
                              const TopoShape &shape,
                              const std::vector<App::PropertyLinkBase::ShadowSub> &names,
                              App::Document *sourceDoc,
                              ElementLooks &looks)
{
    std::array<std::map<int, int>, TopAbs_SHAPE> named;
    namedElements(owner, shape, names, named);
    auto geo = Base::freecad_dynamic_cast<const App::GeoFeature>(owner);

    Caches caches;
    for (int k = 0; k < Store::KindCount; ++k) {
        const Kind kind = static_cast<Kind>(k);
        const TopAbs_ShapeEnum type = KindTypes[k];
        looks.named[k] = std::move(named[type]);
        std::map<int, App::MaterialAppearance> &handedOn = looks.handedOn[k];
        handedOn.clear();
        if (!looks.fromSources[k]) {
            continue;
        }
        // What a name does not paint is, with the kind's Map* property, what
        // its source makes it and no more
        const int count = static_cast<int>(shape.countSubShapes(type));
        const App::MaterialAppearance &own = looks.own[k];
        const App::Color ownColor = colorOf(own);
        const char *typeName = Store::kindName(kind);
        for (int i = 0; i < count; ++i) {
            if (looks.named[k].count(i)) {
                continue;
            }
            Data::MappedName mapped =
                shape.getMappedName(Data::IndexedName::fromConst(typeName, i + 1));
            if (!mapped) {
                continue;
            }
            App::Document *from = sourceDoc;
            if (geo) {
                if (auto elementOwner = geo->getElementOwner(mapped)) {
                    from = elementOwner->getDocument();
                }
            }
            App::MaterialAppearance look;
            bool whole = false;
            App::Color color =
                sourceColor(ownColor, shape, from, kind, mapped, caches, look, whole);
            // An edge and a vertex are a colour: what a face is seen
            // through says nothing of them
            if (kind != Store::Face || !looks.sourceTransparency) {
                color.setTransparency(ownColor.transparency());
            }
            if (whole) {
                handedOn.emplace(i, inColor(look, color));
            }
            else if (color != ownColor) {
                handedOn.emplace(i, inColor(own, color));
            }
        }
    }
}

void Feature::onAppearanceChanged(const App::Property *prop)
{
    if (isLookName(prop)) {
        // A write to a name is a write to what it names. Not what the name
        // is given of that, and nothing while a document is read: a name is
        // in no file, and one a file has all the same is not this
        App::Document *doc = getDocument();
        if (_mirroringLooks || !doc || !getNameInDocument()
            || doc->testStatus(App::Document::Restoring) || doc->isPerformingTransaction()) {
            return;
        }
        try {
            if (prop == &ShapeColor) {
                writeOwnColor(ShapeColor.getValue());
            }
            else if (prop == &Transparency) {
                writeOwnTransparency(Transparency.getValue());
            }
            else if (prop == &LineColor) {
                writeColors(Store::Edge, {LineColor.getValue()});
            }
            else if (prop == &PointColor) {
                writeColors(Store::Vertex, {PointColor.getValue()});
            }
        }
        catch (Base::Exception &e) {
            FC_ERR(getFullName() << ": " << prop->getName() << " was not taken: " << e.what());
        }
        // Whatever was made of the write, the name says what is
        mirrorLooks();
        return;
    }
    if (prop != &Shape && prop != &ElementAppearance && prop != &ShapeMaterial
        && prop != &MapFaceColor && prop != &MapLineColor && prop != &MapPointColor
        && prop != &MapTransparency && prop != &ForceMapColors) {
        return;
    }
    // What is drawn is made of what is, and failing to make it is no reason
    // for whatever changed the property to fail
    try {
        if (prop == &ShapeMaterial) {
            applyMaterialAppearance();
        }
        else if (!_updatingAppearance) {
            updateAppearance();
            // A look stated anew is drawn where nothing more is laid over it
            // without anything kept changing: what was made from this
            // object is told all the same
            if (prop == &ElementAppearance) {
                App::AppearanceUpdater::addObject(this);
            }
        }
        if (prop == &ElementAppearance && !_updatingAppearance) {
            mirrorLooks();
        }
    }
    catch (Base::Exception &e) {
        FC_ERR(getFullName() << ": the looks of the elements were not made: " << e.what());
    }
    catch (Standard_Failure &e) {
        FC_ERR(getFullName() << ": the looks of the elements were not made: "
                             << e.GetMessageString());
    }
}

namespace
{

/** The own look of the edges, or of the vertices, of a new object
 *
 * The preference's colour on the material a view provider has always given
 * them. One list for every object while the preference stays what it is: a
 * list is a shared value.
 */
const App::AppearanceList &defaultLineLook(const char *parameter)
{
    static std::map<std::string, std::pair<uint32_t, App::AppearanceList>> looks;
    static ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");
    const auto packed = static_cast<uint32_t>(hGrp->GetUnsigned(parameter, 0x191919FFUL));
    auto &held = looks[parameter];
    if (held.second.getSize() == 0 || held.first != packed) {
        App::MaterialAppearance mat;
        mat.ambientColor.set(0.2F, 0.2F, 0.2F);
        mat.diffuseColor.setPackedValue(packed);
        mat.diffuseColor.a = 1.0F;
        mat.specularColor.set(0.0F, 0.0F, 0.0F);
        mat.emissiveColor.set(0.0F, 0.0F, 0.0F);
        mat.shininess = 1.0F;
        mat.transparency = 0.0F;
        App::AppearanceList list;
        list.setValue(mat);
        // A look nobody chose
        list.setFollowMaterial(true);
        held.first = packed;
        held.second = list;
    }
    return held.second;
}

}  // namespace

void Feature::handleChangedPropertyName(Base::XMLReader &reader, const char *TypeName,
                                        const char *PropName)
{
    // The names of the elements given a colour were a link of their own,
    // their colours the view provider's. The names are read here, as names
    // that state nothing; the colours are given to them when the view
    // provider has read its own (docs/ShapeAppearanceDesign.md sec 14.6.6).
    // Not over looks the file has: a file that has both has them there.
    if (PropName && std::strcmp(PropName, "ColoredElements") == 0
        && Base::Type::fromName(TypeName).isDerivedFrom(App::PropertyLinkSub::getClassTypeId())) {
        if (!ElementAppearance.wasRestored()) {
            ElementAppearance.Restore(reader);
        }
        return;
    }
    inherited::handleChangedPropertyName(reader, TypeName, PropName);
}

void Feature::setupObject()
{
    inherited::setupObject();
    try {
        giveDefaultAppearance();
    }
    catch (Base::Exception &e) {
        FC_ERR(getFullName() << ": no look was given: " << e.what());
    }
}

void Feature::giveDefaultAppearance()
{
    Store &store = ElementAppearance;
    Store::Edit edit(store);
    if (!store.hasBase(Store::Face)) {
        applyMaterialAppearance();
        if (!store.hasBase(Store::Face)) {
            // A card that says nothing of a look: the preference's
            store.followMaterial(Store::Face, *Materials::MaterialManager::defaultAppearance());
        }
    }
    if (!store.hasBase(Store::Edge)) {
        store.setBaseList(Store::Edge, defaultLineLook("DefaultShapeLineColor"));
    }
    if (!store.hasBase(Store::Vertex)) {
        store.setBaseList(Store::Vertex, defaultLineLook("DefaultShapeVertexColor"));
    }
}

void Feature::applyMaterialAppearance()
{
    // The follow gates the moment a card is SET, and nothing else. A restore
    // is the file's own record landing: the look it states is what this
    // object looks like, and taking the card over it would write over a look
    // somebody chose and saved (docs/MaterialStorage.md 15.3).
    if (App::Document::isAnyRestoring()) {
        return;
    }
    // An undo puts the card back and the look with it, each as it was
    if (getDocument() && getDocument()->isPerformingTransaction()) {
        return;
    }
    const App::MaterialAppearance card = getMaterialAppearance();
    const App::MaterialAppearance none;
    if (card == none) {
        return;   // no card, or a card with nothing to say about the look
    }
    // A look somebody chose outranks the card, and choosing one is what
    // ends the follow
    if (!ElementAppearance.isFollowingMaterial(Store::Face)) {
        return;
    }
    // The object's own look only: the faces holding one of their own keep it
    ElementAppearance.followMaterial(Store::Face, card);
}

bool Feature::canResetAppearanceToMaterial() const
{
    if (ElementAppearance.isFollowingMaterial(Store::Face)) {
        return false;
    }
    return getMaterialAppearance() != App::MaterialAppearance();
}

bool Feature::resetAppearanceToMaterial()
{
    const App::MaterialAppearance card = getMaterialAppearance();
    if (card == App::MaterialAppearance()) {
        return false;   // nothing to go back to
    }
    ElementAppearance.followMaterial(Store::Face, card);
    return true;
}

/** @name The looks by the names they have always had
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.1, 14.6.3. ShapeAppearance,
 * ShapeColor, Transparency, LineColor and PointColor are names over
 * ElementAppearance: they take what it has (mirrorLooks()), and a write to
 * one is taken apart and stated there. A view provider's names end in the
 * same functions.
 */
//@{

namespace
{

/// Field by field: App::MaterialAppearance::operator== calls two that name
/// one card the same whatever their colours say
bool sameLook(const App::MaterialAppearance &a, const App::MaterialAppearance &b)
{
    return Store::differingFields(a, b) == Store::OwnNone;
}

bool sameRGB(const App::Color &a, const App::Color &b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

}  // namespace

bool Feature::isLookName(const App::Property *prop) const
{
    return prop == &ShapeAppearance || prop == &ShapeColor || prop == &Transparency
        || prop == &LineColor || prop == &PointColor;
}

void Feature::mirrorLooks()
{
    if (_mirroringLooks) {
        return;
    }
    Base::FlagToggler<> guard(_mirroringLooks);
    const Store &store = ElementAppearance;

    // The faces: the same storage, not a copy
    App::AppearanceList faces;
    getDrawnAppearance(Store::Face, faces);
    ShapeAppearance.mirrorList(faces);

    // The object's own look, and not what the list of the faces keeps as
    // its base: where every face states the same look that is the base, and
    // it is not the object's (sec 14.2)
    const App::MaterialAppearance own = store.getBase(Store::Face);
    const App::Color color = App::AppearanceList::storedDiffuse(own);
    if (ShapeColor.getValue() != color) {
        ShapeColor.setValue(color);
    }
    const long percent = std::lround(own.transparency * 100.0F);
    if (Transparency.getValue() != percent) {
        Transparency.setValue(percent);
    }
    auto colour = [&store](Kind kind, App::PropertyColor &name) {
        App::Color rgb = store.getBase(kind).diffuseColor;
        rgb.a = name.getValue().a;
        if (name.getValue() != rgb) {
            name.setValue(rgb);
        }
    };
    colour(Store::Edge, LineColor);
    colour(Store::Vertex, PointColor);
}

namespace {

/// Whether every element of the shape is held by its number: no name is
/// stated, and the shape gives none
bool allByNumber(const App::PropertyElementAppearance &store, const TopoShape &shape)
{
    return store.getNamedCount() == 0 && shape.getElementMapSize() == 0;
}

/** The looks given by number, to write many of in one go
 *
 * One at a time (PropertyElementAppearance::setLook()) each is read out of
 * the list the one before was written to, and a list written to puts itself
 * in order before it answers: every entry gone over, for every entry. A
 * colour for each of 120,000 faces of an import took a minute. Read from
 * the list as it was, written to a copy, given back once.
 */
App::AppearanceList numberedToWrite(const App::PropertyElementAppearance &store,
                                    App::PropertyElementAppearance::Kind kind, int count,
                                    const App::MaterialAppearance &base)
{
    App::AppearanceList work = store.getNumbered(kind);
    if (work.getSize() == 0) {
        // Every element the kind's own look, until one is given another
        work = store.getBaseList(kind);
        if (work.getSize() > 0) {
            work.setSize(count);
        }
        else {
            work.setSize(count, base);
        }
        work.setFollowMaterial(false);
    }
    else if (work.getSize() < count) {
        work.setSize(count);
    }
    return work;
}

}  // namespace

void Feature::writeFaces(const App::AppearanceList &before, const App::AppearanceList &after)
{
    if (_mirroringLooks) {
        return;
    }
    Store &store = ElementAppearance;
    Store::Edit edit(store);

    // The object's own look. A list assigned a face at a time has no base
    // chosen, and says nothing of the object.
    const bool hasBase = after.hasDerivedBase() || after.getSize() <= 1;
    // A write that moves the object's own look is a write to the object:
    // of its faces, those that state a look are given what it makes of
    // them -- a transparency is every face's -- and no face comes to state
    // one by it. What is drawn of the rest is made again.
    bool toObject = false;
    if (hasBase) {
        const bool moved =
            !sameLook(after.getBase(), before.getBase()) || after.isPBR() != before.isPBR();
        toObject = moved;
        const bool follows = after.isFollowingMaterial();
        const bool followed = before.isFollowingMaterial();
        if (follows && (moved || !followed)) {
            // The card's look, or the look given back to the card: which
            // the object then takes
            store.followMaterial(Store::Face, after.getBase());
            if (!moved) {
                applyMaterialAppearance();
            }
        }
        else if (!follows && moved) {
            store.setBase(Store::Face, after.getBase());
        }
        else if (!follows && followed) {
            // The follow ended with the look as it is
            App::AppearanceList own = store.getBaseList(Store::Face);
            if (own.getSize() == 1) {
                own.setFollowMaterial(false);
                store.setBaseList(Store::Face, own);
            }
        }
    }
    const App::MaterialAppearance base = store.getBase(Store::Face);

    // The faces the write changed, each given what it says: to its name
    // where the shape has one, to its number where it has not, with the
    // fields that changed as its own
    auto entry = [](const App::AppearanceList &list, int i) {
        if (list.getSize() <= 1 || i >= list.getSize()) {
            return list.getBase();
        }
        return list.getMaterial(i);
    };
    std::set<int> faces;
    if (before.getSize() <= 1 && after.getSize() <= 1) {
        // A write to the object and no more
    }
    else if (hasBase && (before.hasDerivedBase() || before.getSize() <= 1)) {
        for (const App::AppearanceList *list : {&before, &after}) {
            if (list->getSize() > 1) {
                faces.insert(list->getOverrides().begin(), list->getOverrides().end());
            }
        }
    }
    else {
        for (int i = 0; i < std::max(before.getSize(), after.getSize()); ++i) {
            faces.insert(i);
        }
    }
    const int count = store.countElements(Store::Face);
    bool plain = faces.size() > 1 && count > 0 && allByNumber(store, Shape.getShape());
    for (auto it = faces.begin(); plain && it != faces.end(); ++it) {
        // What names stored content is held as it is written, one at a time
        const App::MaterialAppearance look = entry(after, *it);
        plain = !look.texture.isSet() && look.materialx.empty();
    }
    if (plain) {
        // Every face by its number: together (numberedToWrite())
        const App::AppearanceList numbered = store.getNumbered(Store::Face);
        App::AppearanceList work = numberedToWrite(store, Store::Face, count, base);
        bool any = false;
        for (int i : faces) {
            if (i >= count) {
                break;
            }
            const App::MaterialAppearance current =
                i < numbered.getSize() ? numbered.getMaterial(i) : base;
            const uint16_t own = Store::differingFields(current, base);
            if (toObject && own == Store::OwnNone) {
                continue;
            }
            const App::MaterialAppearance now = entry(after, i);
            uint16_t changed = Store::differingFields(now, entry(before, i));
            if (toObject) {
                changed &= own;
            }
            if (changed == Store::OwnNone) {
                continue;
            }
            const App::MaterialAppearance next =
                sameLook(now, base) ? base : Store::layOver(current, now, changed);
            if (Store::differingFields(next, current) == Store::OwnNone) {
                continue;
            }
            work.set1Value(i, next);
            any = true;
        }
        if (any) {
            store.setNumbered(Store::Face, work);
        }
        return;
    }
    for (int i : faces) {
        if (count >= 0 && i >= count) {
            break;
        }
        if (toObject && !store.isStated(Store::Face, i)) {
            continue;
        }
        const App::MaterialAppearance now = entry(after, i);
        uint16_t changed = Store::differingFields(now, entry(before, i));
        // Of a write to the object, only what the face states itself: the
        // rest of it is the object's, and moved with the object
        if (toObject) {
            changed &= store.getOwn(Store::Face, i);
        }
        if (changed == Store::OwnNone) {
            continue;
        }
        if (sameLook(now, base) && store.removeLook(Store::Face, i)) {
            continue;
        }
        store.setLook(Store::Face, i, now, changed);
    }
}

void Feature::writeOwnColor(const App::Color &value)
{
    Store &store = ElementAppearance;
    App::MaterialAppearance own = store.getBase(Store::Face);
    // The colour and no more: what is seen through it is Transparency's
    App::Color color = value;
    color.a = own.diffuseColor.a;
    if (sameRGB(color, own.diffuseColor)) {
        return;
    }
    own.diffuseColor = color;
    store.setBase(Store::Face, own);
}

void Feature::writeOwnTransparency(long percent)
{
    Store &store = ElementAppearance;
    App::MaterialAppearance own = store.getBase(Store::Face);
    if (std::lround(own.transparency * 100.0F) == percent) {
        return;
    }
    Store::Edit edit(store);
    const float trans = static_cast<float>(percent) / 100.0F;
    own.transparency = trans;
    own.diffuseColor.setTransparency(trans);
    store.setBase(Store::Face, own);
    // A transparency is every face's: of those that state a colour too
    for (const auto &v : store.getStatedLooks()) {
        Store::Kind kind = Store::KindCount;
        int index = -1;
        if (!store.resolveElement(v.first.c_str(), kind, index) || kind != Store::Face
            || index < 0 || !(store.getOwn(kind, index) & Store::OwnDiffuse)) {
            continue;
        }
        App::MaterialAppearance look = v.second;
        look.transparency = trans;
        look.diffuseColor.setTransparency(trans);
        store.setLook(kind, index, look, Store::OwnDiffuse);
    }
}

void Feature::writeOwnMaterial(const App::MaterialAppearance &value)
{
    Store &store = ElementAppearance;
    const App::MaterialAppearance own = store.getBase(Store::Face);
    // A plain material: it states no shading model, finish or texture, and
    // leaves the object's as they are
    App::MaterialAppearance look = value;
    look.pbr = own.pbr;
    look.finish = own.finish;
    look.texture = own.texture;
    if (!sameLook(look, own)) {
        store.setBase(Store::Face, look);
    }
}

void Feature::writeColors(int which, const std::vector<App::Color> &values)
{
    if (which != Store::Edge && which != Store::Vertex) {
        return;
    }
    const auto kind = static_cast<Kind>(which);
    Store &store = ElementAppearance;
    Store::Edit edit(store);
    const App::MaterialAppearance own = store.getBase(kind);
    if (values.size() <= 1) {
        // One colour is every element's: the kind's own
        if (values.size() == 1 && !sameRGB(values[0], own.diffuseColor)) {
            App::MaterialAppearance look = own;
            look.diffuseColor = values[0];
            look.diffuseColor.a = own.diffuseColor.a;
            store.setBase(kind, look);
        }
        return;
    }
    const App::AppearanceList drawn = store.getDrawn(kind);
    const int count = store.countElements(kind);
    if (count > 0 && allByNumber(store, Shape.getShape())) {
        // Every element by its number: together (numberedToWrite())
        const App::AppearanceList numbered = store.getNumbered(kind);
        App::AppearanceList work = numberedToWrite(store, kind, count, own);
        bool any = false;
        for (int i = 0; i < static_cast<int>(values.size()) && i < count; ++i) {
            const App::Color &value = values[static_cast<std::size_t>(i)];
            const App::Color now = drawn.getSize() > 1 && i < drawn.getSize()
                ? drawn.getDiffuseColor(i)
                : own.diffuseColor;
            if (sameRGB(value, now)) {
                continue;
            }
            const App::MaterialAppearance current =
                i < numbered.getSize() ? numbered.getMaterial(i) : own;
            App::MaterialAppearance next = own;
            if (!sameRGB(value, own.diffuseColor)) {
                App::MaterialAppearance look = current;
                look.diffuseColor = value;
                look.diffuseColor.a = own.diffuseColor.a;
                look.transparency = look.diffuseColor.transparency();
                next = Store::layOver(current, look, Store::OwnDiffuse);
            }
            if (Store::differingFields(next, current) == Store::OwnNone) {
                continue;
            }
            work.set1Value(i, next);
            any = true;
        }
        if (any) {
            store.setNumbered(kind, work);
        }
        return;
    }
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        if (count >= 0 && i >= count) {
            break;
        }
        const App::Color &value = values[static_cast<std::size_t>(i)];
        const App::Color now = drawn.getSize() > 1 && i < drawn.getSize()
            ? drawn.getDiffuseColor(i)
            : own.diffuseColor;
        if (sameRGB(value, now)) {
            continue;
        }
        if (sameRGB(value, own.diffuseColor) && store.removeLook(kind, i)) {
            continue;
        }
        App::Color color = value;
        color.a = own.diffuseColor.a;
        store.setColor(kind, i, color);
    }
}

//@}
