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

#include "PreCompiled.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

#include <boost/algorithm/string/predicate.hpp>

#include <Base/Tools.h>

#include "AppearanceList.h"
#include "AppearanceUpdater.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "ElementNamingUtils.h"
#include "GeoFeature.h"
#include "Link.h"
#include "LinkAppearance.h"
#include "MappedName.h"
#include "Part.h"
#include "PropertyStandard.h"

using namespace App;

namespace
{

using Store = PropertyElementAppearance;

/// One name of a store, with what it is given
struct Entry
{
    std::string name;
    MaterialAppearance look;
    uint16_t own {Store::OwnNone};
};

std::vector<Entry> entriesOf(const Store &store)
{
    const std::vector<std::string> &names = store.getSubValues();
    const AppearanceList &looks = store.getNamedLooks();
    std::vector<Entry> res;
    res.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        Entry entry;
        entry.name = names[i];
        const int pos = static_cast<int>(i);
        if (pos < looks.getSize()) {
            entry.look = looks.getMaterial(pos);
            entry.own = store.getNamedOwn(pos);
        }
        res.push_back(std::move(entry));
    }
    return res;
}

bool sameEntries(const std::vector<Entry> &a, const std::vector<Entry> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].own != b[i].own) {
            return false;
        }
        if (a[i].own != Store::OwnNone
            && Store::differingFields(a[i].look, b[i].look) != Store::OwnNone) {
            return false;
        }
    }
    return true;
}

void assign(Store &store, std::vector<Entry> &&entries)
{
    if (sameEntries(entries, entriesOf(store))) {
        return;
    }
    std::vector<std::string> names;
    std::vector<uint16_t> own;
    AppearanceList looks;
    names.reserve(entries.size());
    own.reserve(entries.size());
    if (!entries.empty()) {
        looks.setSize(static_cast<int>(entries.size()));
        looks.setFollowMaterial(false);
    }
    int pos = 0;
    for (Entry &entry : entries) {
        looks.set1Value(pos++, entry.own == Store::OwnNone
                                   ? MaterialAppearance(AppearanceList::defaultMaterial())
                                   : entry.look);
        names.push_back(std::move(entry.name));
        own.push_back(entry.own);
    }
    store.setNamed(std::move(names), looks, std::move(own));
}

Color colorOf(const MaterialAppearance &look)
{
    return AppearanceList::storedDiffuse(look);
}

MaterialAppearance lookOf(const Color &color)
{
    MaterialAppearance look(AppearanceList::defaultMaterial());
    look.diffuseColor = color;
    look.transparency = color.transparency();
    return look;
}

}  // namespace

LinkAppearance::Names LinkAppearance::namesOf(const DocumentObject *obj)
{
    Names names;
    if (!obj) {
        return names;
    }
    auto object = const_cast<DocumentObject *>(obj);
    if (auto link = object->getExtensionByType<LinkBaseExtension>(true)) {
        names.store = link->getElementAppearanceProperty();
        names.colored = link->getColoredElementsProperty();
        names.overrideMaterial = link->getOverrideMaterialProperty();
        names.shapeAppearance = link->getShapeAppearanceProperty();
    }
    else if (auto part = Base::freecad_dynamic_cast<App::Part>(object)) {
        names.store = &part->ElementAppearance;
        names.colored = &part->ColoredElements;
        names.overrideMaterial = &part->OverrideMaterial;
        names.shapeAppearance = &part->ShapeAppearance;
    }
    if (names.store && !names.store->hasPathNames()) {
        // A Part::Feature's own looks, which a link made in Python of one
        // may have given the slot: not what it lays over what it shows
        names = Names();
    }
    return names;
}

bool LinkAppearance::isArrayName(const std::string &name, int *index)
{
    if (name.size() < 2 || name.back() != '.') {
        return false;
    }
    for (std::size_t i = 0; i + 1 < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    if (index) {
        *index = std::atoi(name.c_str());
    }
    return true;
}

bool LinkAppearance::isHiddenName(const std::string &name)
{
    return boost::ends_with(name, DocumentObject::hiddenMarker());
}

bool LinkAppearance::hasOverride(const Store &store)
{
    return store.hasBase(Store::Face);
}

void LinkAppearance::setOverride(Store &store, bool on, const MaterialAppearance &look)
{
    const bool has = store.hasBase(Store::Face);
    if (!on) {
        if (!has) {
            return;
        }
        // The look it gave is the one it would give
        Store::Edit edit(store);
        store.setKept(store.getBase(Store::Face));
        store.clearBase(Store::Face);
        return;
    }
    const bool same =
        has && Store::differingFields(store.getBase(Store::Face), look) == Store::OwnNone;
    if (same && !store.hasKept()) {
        return;
    }
    Store::Edit edit(store);
    if (!same) {
        store.setBase(Store::Face, look);
    }
    store.clearKept();
}

void LinkAppearance::keepLook(Store &store, const MaterialAppearance &look)
{
    if (!store.hasBase(Store::Face)) {
        store.setKept(look);
    }
}

MaterialAppearance LinkAppearance::defaultLook(bool ofPart)
{
    MaterialAppearance look(MaterialAppearance::DEFAULT);
    look.diffuseColor.setPackedValue(static_cast<uint32_t>(
        GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/View")
            ->GetUnsigned(ofPart ? "DefaultShapeColor" : "DefaultLinkColor",
                          ofPart ? 0xCCCCE6FFUL : 0x66FFFFFFUL)));
    return look;
}

void LinkAppearance::getColored(const Store &store, std::vector<std::string> &names,
                                std::vector<Color> &colors)
{
    names.clear();
    colors.clear();
    for (const Entry &entry : entriesOf(store)) {
        if (isArrayName(entry.name)) {
            continue;
        }
        names.push_back(entry.name);
        colors.push_back(entry.own == Store::OwnNone ? Color() : colorOf(entry.look));
    }
}

void LinkAppearance::setColored(Store &store, const std::vector<std::string> &names,
                                const std::vector<Color> &colors)
{
    const std::vector<Entry> before = entriesOf(store);
    std::vector<Entry> next;
    next.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty() || isArrayName(names[i])) {
            continue;
        }
        Entry entry;
        entry.name = names[i];
        auto had = std::find_if(before.begin(), before.end(), [&](const Entry &e) {
            return e.name == names[i];
        });
        if (isHiddenName(names[i])) {
            // Not shown: nothing to state of it
        }
        else if (i >= colors.size()) {
            if (had != before.end()) {
                entry = *had;
            }
        }
        else if (had != before.end() && had->own != Store::OwnNone
                 && colorOf(had->look) == colors[i]) {
            // What it has: with whatever more of a look it was given
            entry = *had;
        }
        else {
            entry.look = lookOf(colors[i]);
            entry.own = Store::OwnDiffuse;
        }
        next.push_back(std::move(entry));
    }
    for (const Entry &entry : before) {
        if (isArrayName(entry.name)) {
            next.push_back(entry);
        }
    }
    assign(store, std::move(next));
}

void LinkAppearance::getArrayLooks(const Store &store, std::vector<MaterialAppearance> &looks,
                                   boost::dynamic_bitset<> &given)
{
    looks.clear();
    given.clear();
    for (const Entry &entry : entriesOf(store)) {
        int index = -1;
        if (!isArrayName(entry.name, &index) || index < 0 || entry.own == Store::OwnNone) {
            continue;
        }
        const auto at = static_cast<std::size_t>(index);
        if (looks.size() <= at) {
            looks.resize(at + 1, MaterialAppearance(AppearanceList::defaultMaterial()));
            given.resize(at + 1, false);
        }
        looks[at] = entry.look;
        given[at] = true;
    }
}

void LinkAppearance::setArrayLooks(Store &store, const std::vector<MaterialAppearance> &looks,
                                   const boost::dynamic_bitset<> &given)
{
    std::vector<Entry> next;
    for (Entry &entry : entriesOf(store)) {
        if (!isArrayName(entry.name)) {
            next.push_back(std::move(entry));
        }
    }
    for (std::size_t i = 0; i < looks.size() && i < given.size(); ++i) {
        if (!given[i]) {
            continue;
        }
        Entry entry;
        entry.name = std::to_string(i) + ".";
        entry.look = looks[i];
        entry.own = Store::OwnAll;
        next.push_back(std::move(entry));
    }
    assign(store, std::move(next));
}

namespace
{

/// The object's own names given what the store has
void mirror(const LinkAppearance::Names &names)
{
    const Store &store = *names.store;
    if (names.colored) {
        std::vector<std::string> subs;
        std::vector<Color> colors;
        LinkAppearance::getColored(store, subs, colors);
        if (subs != names.colored->getSubValues()) {
            auto owner = Base::freecad_dynamic_cast<DocumentObject>(store.getContainer());
            if (subs.empty() || !owner) {
                names.colored->setValue(nullptr);
            }
            else {
                names.colored->setValue(owner, subs);
            }
        }
    }
    const bool on = LinkAppearance::hasOverride(store);
    if (names.overrideMaterial && names.overrideMaterial->getValue() != on) {
        names.overrideMaterial->setValue(on);
    }
    if (names.shapeAppearance && on) {
        names.shapeAppearance->mirrorList(store.getBaseList(Store::Face));
    }
    else if (names.shapeAppearance) {
        // It gives none: the look it would give, which is the one kept, or
        // the one nobody chose
        const MaterialAppearance look = store.hasKept()
            ? store.getKept()
            : LinkAppearance::defaultLook(
                  Base::freecad_dynamic_cast<App::Part>(store.getContainer()) != nullptr);
        const AppearanceList &now = names.shapeAppearance->getList();
        if (now.getSize() != 1
            || Store::differingFields(now.getBase(), look) != Store::OwnNone) {
            AppearanceList kept;
            kept.setValue(look);
            names.shapeAppearance->mirrorList(kept);
        }
    }
}

/** The store's names following the shape, before the object's are taken
 *
 * ColoredElements is a link to elements as the store is, and is told of a
 * shape that counts its faces another way as the store is -- in no order
 * anybody chose. Told first, it says "Face3" where the store still says
 * "Face6", which read as a write is one name taken away and another, with
 * no look, added: the look went with the renumbering. So the store is told
 * of the shapes the new names are of before they are compared with its own.
 */
void followShapes(const LinkAppearance::Names &names, DocumentObject *owner)
{
    const std::vector<std::string> &given = names.colored->getSubValues();
    const std::vector<std::string> &held = names.store->getSubValues();
    std::set<GeoFeature *> shapes;
    for (const std::string &name : given) {
        if (name.empty() || std::find(held.begin(), held.end(), name) != held.end()) {
            continue;
        }
        std::pair<std::string, std::string> element;
        GeoFeature *geo = nullptr;
        GeoFeature::resolveElement(owner, name.c_str(), element, true,
                                   GeoFeature::ElementNameType::Export, nullptr, nullptr, &geo);
        if (geo) {
            shapes.insert(geo);
        }
    }
    for (GeoFeature *geo : shapes) {
        names.store->updateElementReference(geo, false, true);
    }
}

bool settled(const LinkAppearance::Names &names)
{
    auto owner = Base::freecad_dynamic_cast<DocumentObject>(names.store->getContainer());
    Document *doc = owner ? owner->getDocument() : nullptr;
    return doc && owner->getNameInDocument() && !doc->testStatus(Document::Restoring);
}

}  // namespace

void LinkAppearance::onChanged(const Names &names, const Property *prop, bool &busy)
{
    if (!names.store || !prop) {
        return;
    }
    if (prop != names.store && prop != names.colored && prop != names.overrideMaterial) {
        return;
    }
    auto owner = Base::freecad_dynamic_cast<DocumentObject>(names.store->getContainer());
    if (!owner || !owner->getDocument() || !owner->getNameInDocument()) {
        return;
    }
    // While a document is read a name is the file's, or in no file, and no
    // write to the store; the store read, or given what an older file's
    // view provider has, is still what the names say
    if (prop != names.store && !settled(names)) {
        return;
    }
    // What was made from what the link shows takes what the link lays over
    // it: told once the change that set this off is done, whoever made it
    AppearanceUpdater updater;
    if (prop == names.store) {
        AppearanceUpdater::addObject(owner);
    }
    if (busy) {
        return;
    }
    Base::StateLocker guard(busy);
    // An undo puts the store back, and the names follow it
    if (prop != names.store && !owner->getDocument()->isPerformingTransaction()) {
        if (prop == names.colored) {
            followShapes(names, owner);
            setColored(*names.store, names.colored->getSubValues(), {});
        }
        else {
            const MaterialAppearance look = names.shapeAppearance
                ? names.shapeAppearance->getList().getBase()
                : MaterialAppearance(AppearanceList::defaultMaterial());
            setOverride(*names.store, names.overrideMaterial->getValue(), look);
        }
    }
    mirror(names);
}

void LinkAppearance::onRestored(const Names &names, bool &busy)
{
    if (!names.store) {
        return;
    }
    Base::StateLocker guard(busy);
    if (!names.store->wasRestored() && names.colored
        && !names.colored->getSubValues().empty()) {
        setColored(*names.store, names.colored->getSubValues(), {});
    }
    mirror(names);
}

void LinkAppearance::writeAppearance(const Names &names, const AppearanceList &after,
                                     bool &busy)
{
    if (busy || !names.store || !names.shapeAppearance) {
        return;
    }
    Base::StateLocker guard(busy);
    if (hasOverride(*names.store)) {
        setOverride(*names.store, true, after.getBase());
        mirror(names);
    }
    else {
        // No look of its own: kept as the one it would have
        keepLook(*names.store, after.getBase());
        mirror(names);
    }
}

bool LinkAppearance::getLinkColor(const Data::MappedName &mapped, DocumentObject *&obj,
                                  Color &color)
{
    if (!obj) {
        return false;
    }
    bool found = false;
    for (int depth = 0;; ++depth) {
        auto link = obj->getExtensionByType<LinkBaseExtension>(true);
        const Store *store = storeOf(obj);
        if (store && hasOverride(*store)) {
            found = true;
            color = colorOf(store->getBase(Store::Face));
            if (!link || !link->getElementCountValue()) {
                return true;
            }
        }
        // One of an array: its own look, or what the element shows
        if (link && !link->getShowElementValue() && link->getElementCountValue()) {
            const int pos = mapped.rfind(Data::indexPostfix());
            if (pos >= 0) {
                const int offset = pos + static_cast<int>(Data::indexPostfix().size());
                std::istringstream iss(mapped.toRawBytes(offset).toStdString());
                int index = 0;
                char sep = 0;
                iss >> index >> sep;
                if (sep == ';' && store && index >= 0) {
                    const int at = store->findNamed((std::to_string(index) + ".").c_str());
                    if (at >= 0 && store->getNamedOwn(at) != Store::OwnNone
                        && at < store->getNamedLooks().getSize()) {
                        color = colorOf(store->getNamedLooks().getMaterial(at));
                        return true;
                    }
                }
                if (found) {
                    return found;
                }
                obj = obj->getSubObject((std::to_string(index) + ".").c_str());
                return false;
            }
        }
        auto linked = obj->getLinkedObject(false, nullptr, false, depth);
        if (!linked || linked == obj) {
            break;
        }
        obj = linked;
    }
    return found;
}
