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

#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Standard_Failure.hxx>

#include <App/AppearanceList.h>
#include <App/AppearanceUpdater.h>
#include <App/Application.h>
#include <App/Document.h>
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
        if (shape.isNull() || (_linkLook && _linkLook(original, obj, shown, color)) || !obj) {
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
void namedElements(const Feature *obj,
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

        for (auto &names : Feature::getRelatedElements(const_cast<Feature *>(obj),
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

void Feature::updateAppearance(App::Document *sourceDoc, bool forceMap)
{
    App::Document *doc = getDocument();
    if (_updatingAppearance || !doc || !getNameInDocument()
        || doc->testStatus(App::Document::Restoring)) {
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

    std::array<std::map<int, int>, TopAbs_SHAPE> named;
    namedElements(this, shape, store.getShadowSubs(), named);

    Caches caches;
    bool changed = false;
    for (int k = 0; k < Store::KindCount; ++k) {
        const Kind kind = static_cast<Kind>(k);
        const TopAbs_ShapeEnum type = KindTypes[k];
        const int count = static_cast<int>(shape.countSubShapes(type));
        const std::map<int, int> &names = named[type];

        // What a name does not paint is, with the kind's Map* property, what
        // its source makes it and no more
        std::map<int, App::MaterialAppearance> handedOn;
        if (!noMap && mapKind[k]) {
            const App::MaterialAppearance own = store.getBase(kind);
            const App::Color ownColor = colorOf(own);
            const char *typeName = Store::kindName(kind);
            for (int i = 0; i < count; ++i) {
                if (names.count(i)) {
                    continue;
                }
                Data::MappedName mapped =
                    shape.getMappedName(Data::IndexedName::fromConst(typeName, i + 1));
                if (!mapped) {
                    continue;
                }
                App::Document *from = sourceDoc;
                if (auto owner = getElementOwner(mapped)) {
                    from = owner->getDocument();
                }
                App::MaterialAppearance look;
                bool whole = false;
                App::Color color =
                    sourceColor(ownColor, shape, from, kind, mapped, caches, look, whole);
                // An edge and a vertex are a colour: what a face is seen
                // through says nothing of them
                if (kind != Store::Face || !MapTransparency.getValue()) {
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
}

void Feature::onAppearanceChanged(const App::Property *prop)
{
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
