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
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Writer.h>

#include "ComplexGeoData.h"
#include "Document.h"
#include "DocumentObject.h"
#include "ElementAppearancePy.h"
#include "ElementNamingUtils.h"
#include "GeoFeature.h"
#include "MaterialPy.h"
#include "PropertyElementAppearance.h"
#include "PropertyGeo.h"
#include "PropertyStandard.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

TYPESYSTEM_SOURCE(App::PropertyElementAppearance, App::PropertyLinkSubHidden)

namespace
{

const char *const KindNames[PropertyElementAppearance::KindCount] = {"Face", "Edge", "Vertex"};

/// What a held list's file is named after, beside the property's own name
const char *const SlotNames[] = {"Named",      "Face",       "Edge",         "Vertex",  "Faces",
                                 "Edges",      "Vertices",   "DrawnFaces",   "DrawnEdges",
                                 "DrawnVertices"};

struct OwnName
{
    uint16_t bit;
    const char *name;
};
const OwnName OwnNames[] = {
    {PropertyElementAppearance::OwnDiffuse, "DiffuseColor"},
    {PropertyElementAppearance::OwnAmbient, "AmbientColor"},
    {PropertyElementAppearance::OwnSpecular, "SpecularColor"},
    {PropertyElementAppearance::OwnEmissive, "EmissiveColor"},
    {PropertyElementAppearance::OwnShininess, "Shininess"},
    {PropertyElementAppearance::OwnFinish, "Finish"},
    {PropertyElementAppearance::OwnTexture, "Texture"},
    {PropertyElementAppearance::OwnImage, "Image"},
    {PropertyElementAppearance::OwnCard, "UUID"},
    {PropertyElementAppearance::OwnMaterialX, "MaterialX"},
    {PropertyElementAppearance::OwnType, "Type"},
};

/// How many elements a call may give a look by number and still write them
/// where they are: more are written to a list made again (setStated())
const std::size_t FewElements = 16;

/// A material's type, written without MaterialAppearance::setType() taking
/// the preset's colours with it
void setTypeOnly(MaterialAppearance &mat, MaterialAppearance::MaterialType type)
{
    if (mat.getType() == type) {
        return;
    }
    const MaterialAppearance saved = mat;
    mat.setType(type);
    mat.ambientColor = saved.ambientColor;
    mat.diffuseColor = saved.diffuseColor;
    mat.specularColor = saved.specularColor;
    mat.emissiveColor = saved.emissiveColor;
    mat.shininess = saved.shininess;
    mat.transparency = saved.transparency;
}

/// The part of a name that says which element, as the shape counts it
const char *indexedPart(const char *name)
{
    if (!name) {
        return nullptr;
    }
    const char *element = Data::findElementName(name);
    if (!element) {
        return nullptr;
    }
    if (Data::isMappedElement(element)) {
        // A mapped name carries the element it was last seen as behind a dot
        const char *dot = std::strrchr(element, '.');
        if (!dot) {
            return nullptr;
        }
        element = dot + 1;
    }
    const std::string &missing = Data::missingPrefix();
    if (!missing.empty() && std::strncmp(element, missing.c_str(), missing.size()) == 0) {
        element += missing.size();
    }
    return element;
}

}  // namespace

struct PropertyElementAppearance::Held
{
    PropertyAppearanceList list;
    std::string name;
};

//**************************************************************************
// Construction

PropertyElementAppearance::PropertyElementAppearance() = default;

PropertyElementAppearance::~PropertyElementAppearance()
{
    // A view of a property that is gone is invalid, which every access to
    // it checks. Moved out first: nothing may unregister while this walks.
    const std::vector<ElementAppearancePy *> views = std::move(_views);
    _views.clear();
    for (auto *view : views) {
        view->setInvalid();
    }
}

//**************************************************************************
// Names of elements

const char *PropertyElementAppearance::kindName(Kind kind)
{
    return kind >= 0 && kind < KindCount ? KindNames[kind] : "";
}

bool PropertyElementAppearance::parseElement(const char *name, Kind &kind, int &index)
{
    const char *element = indexedPart(name);
    if (!element || !element[0]) {
        return false;
    }
    for (int k = 0; k < KindCount; ++k) {
        const std::size_t len = std::strlen(KindNames[k]);
        if (std::strncmp(element, KindNames[k], len) != 0) {
            continue;
        }
        const char *digits = element + len;
        if (!digits[0]) {
            kind = static_cast<Kind>(k);
            index = -1;
            return true;
        }
        char *end = nullptr;
        const long number = std::strtol(digits, &end, 10);
        if (end == digits || *end || number <= 0 || digits[0] == '+' || digits[0] == '-') {
            return false;
        }
        kind = static_cast<Kind>(k);
        index = static_cast<int>(number - 1);
        return true;
    }
    return false;
}

std::string PropertyElementAppearance::elementName(Kind kind, int index)
{
    std::string name(kindName(kind));
    if (index >= 0) {
        name += std::to_string(index + 1);
    }
    return name;
}

std::vector<std::string> PropertyElementAppearance::ownNames(uint16_t own)
{
    std::vector<std::string> names;
    for (const auto &entry : OwnNames) {
        if (own & entry.bit) {
            names.emplace_back(entry.name);
        }
    }
    return names;
}

uint16_t PropertyElementAppearance::ownFromNames(const std::vector<std::string> &names)
{
    uint16_t own = OwnNone;
    for (const auto &name : names) {
        bool found = false;
        for (const auto &entry : OwnNames) {
            if (name == entry.name) {
                own |= entry.bit;
                found = true;
                break;
            }
        }
        if (!found) {
            FC_THROWM(Base::ValueError, "'" << name << "' is no field of a look");
        }
    }
    return own;
}

uint16_t PropertyElementAppearance::differingFields(const MaterialAppearance &look,
                                                    const MaterialAppearance &from)
{
    uint16_t own = OwnNone;
    // As a list stores them: the diffuse alpha carries the transparency, and
    // a finish or a texture is clamped on its way in
    if (!(AppearanceList::storedDiffuse(look) == AppearanceList::storedDiffuse(from))) {
        own |= OwnDiffuse;
    }
    if (!(look.ambientColor == from.ambientColor)) {
        own |= OwnAmbient;
    }
    if (!(look.specularColor == from.specularColor)) {
        own |= OwnSpecular;
    }
    if (!(look.emissiveColor == from.emissiveColor)) {
        own |= OwnEmissive;
    }
    if (look.shininess != from.shininess) {
        own |= OwnShininess;
    }
    if (!(AppearanceList::storedFinish(look.finish) == AppearanceList::storedFinish(from.finish))) {
        own |= OwnFinish;
    }
    if (!(AppearanceList::storedTexture(look.texture)
          == AppearanceList::storedTexture(from.texture))) {
        own |= OwnTexture;
    }
    if (look.image != from.image || look.imagePath != from.imagePath) {
        own |= OwnImage;
    }
    if (look.uuid != from.uuid) {
        own |= OwnCard;
    }
    if (look.materialx != from.materialx) {
        own |= OwnMaterialX;
    }
    if (look.getType() != from.getType()) {
        own |= OwnType;
    }
    return own;
}

MaterialAppearance PropertyElementAppearance::layOver(const MaterialAppearance &base,
                                                      const MaterialAppearance &look,
                                                      uint16_t own)
{
    if ((own & OwnAll) == OwnAll) {
        return look;
    }
    MaterialAppearance out = base;
    if (own & OwnType) {
        setTypeOnly(out, look.getType());
    }
    if (own & OwnDiffuse) {
        out.diffuseColor = look.diffuseColor;
        out.transparency = look.transparency;
    }
    if (own & OwnAmbient) {
        out.ambientColor = look.ambientColor;
    }
    if (own & OwnSpecular) {
        out.specularColor = look.specularColor;
    }
    if (own & OwnEmissive) {
        out.emissiveColor = look.emissiveColor;
    }
    if (own & OwnShininess) {
        out.shininess = look.shininess;
    }
    if (own & OwnFinish) {
        out.finish = look.finish;
    }
    if (own & OwnTexture) {
        out.texture = look.texture;
    }
    if (own & OwnImage) {
        out.image = look.image;
        out.imagePath = look.imagePath;
    }
    if (own & OwnCard) {
        out.uuid = look.uuid;
    }
    if (own & OwnMaterialX) {
        out.materialx = look.materialx;
    }
    return out;
}

void PropertyElementAppearance::applyFields(AppearanceList &list, int idx,
                                            const MaterialAppearance &look, uint16_t own)
{
    if ((own & OwnAll) == OwnAll) {
        list.set1Value(idx, look);
        return;
    }
    list.set1Value(idx, layOver(list.getMaterial(idx), look, own));
}

//**************************************************************************
// The change, announced once

PropertyElementAppearance::Edit::Edit(PropertyElementAppearance &prop)
    : prop(prop)
{
    if (prop._editing++ == 0) {
        prop.PropertyLinkSubHidden::aboutToSetValue();
    }
}

PropertyElementAppearance::Edit::~Edit()
{
    if (prop._editing != 1) {
        --prop._editing;
        return;
    }
    // Still inside the edit while the names are given to the link, which
    // announces a change of its own
    try {
        prop.flushSubs();
        if (!prop._restoring) {
            prop.pruneHeld();
        }
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (...) {
    }
    prop._editing = 0;
    prop._places.reset();
    try {
        prop.PropertyLinkSubHidden::hasSetValue();
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (...) {
    }
}

void PropertyElementAppearance::aboutToSetValue()
{
    if (_editing == 0) {
        PropertyLinkSubHidden::aboutToSetValue();
    }
}

void PropertyElementAppearance::hasSetValue()
{
    if (_editing != 0) {
        return;
    }
    // The names were changed as a link's are, by something that knows
    // nothing of the looks
    conformNames();
    _places.reset();
    PropertyLinkSubHidden::hasSetValue();
}

DocumentObject *PropertyElementAppearance::owner() const
{
    return Base::freecad_dynamic_cast<DocumentObject>(getContainer());
}

const Data::ComplexGeoData *PropertyElementAppearance::geometry() const
{
    auto geo = Base::freecad_dynamic_cast<GeoFeature>(getContainer());
    if (!geo) {
        return nullptr;
    }
    auto prop = geo->getPropertyOfGeometry();
    return prop ? prop->getComplexData() : nullptr;
}

//**************************************************************************
// The held lists

const AppearanceList &PropertyElementAppearance::listAt(int slot) const
{
    static const AppearanceList nothing;
    return _held[slot] ? _held[slot]->list.getList() : nothing;
}

AppearanceList &PropertyElementAppearance::editList(int slot)
{
    auto &held = _held[slot];
    if (!held) {
        held = std::make_unique<Held>();
        const char *name = getName();
        held->name = std::string(name && name[0] ? name : "ElementAppearance") + "."
            + SlotNames[slot];
        held->list.setHolder(this, held->name.c_str());
    }
    return held->list.heldList();
}

void PropertyElementAppearance::assign(int slot, const AppearanceList &list, bool hold)
{
    if (list.getSize() == 0) {
        _held[slot].reset();
        return;
    }
    // The old value outlives the assignment: what it holds of stored
    // content is what the new one takes hold of
    const AppearanceList before = listAt(slot);
    editList(slot) = list;
    auto &held = *_held[slot];
    held.list.setHolder(this, held.name.c_str());
    if (hold && (list.hasTexture() || list.hasMaterialX())) {
        held.list.holdStoredBlobs();
    }
}

void PropertyElementAppearance::syncHeld() const
{
    auto self = const_cast<PropertyElementAppearance *>(this);
    for (int slot = 0; slot < SlotCount; ++slot) {
        auto &held = self->_held[slot];
        if (!held) {
            continue;
        }
        const char *own = getName();
        std::string name = std::string(own && own[0] ? own : "ElementAppearance") + "."
            + SlotNames[slot];
        if (name != held->name) {
            held->name = std::move(name);
        }
        held->list.setHolder(self, held->name.c_str());
    }
}

void PropertyElementAppearance::pruneHeld()
{
    for (int k = 0; k < KindCount; ++k) {
        // A numbered list in which no element differs from the kind's own
        // look says nothing
        auto &held = _held[SlotNumbered + k];
        if (!held) {
            continue;
        }
        const AppearanceList &list = held->list.getList();
        if (list.getSize() > 0 && !list.hasOverrides()
            && differingFields(list.getBase(), getBase(static_cast<Kind>(k))) == OwnNone) {
            held.reset();
            continue;
        }
        // Whether the kind's own look is the card's is said by the numbered
        // list as by the own look, so that what is drawn of it -- the same
        // storage, where nothing more is laid in -- says it too
        const bool following = isFollowingMaterial(static_cast<Kind>(k));
        if (list.isFollowingMaterial() != following) {
            held->list.heldList().setFollowMaterial(following);
        }
    }
    for (auto &held : _held) {
        if (held && held->list.getList().getSize() == 0) {
            held.reset();
        }
    }
    if (std::all_of(_own.begin(), _own.end(), [](uint16_t own) { return own == OwnAll; })) {
        _own.clear();
    }
}

void PropertyElementAppearance::conformNames(bool keepHeld)
{
    const std::size_t count = _cSubList.size();
    if (!_own.empty() && _own.size() != count) {
        // A name that came without a look states nothing
        _own.resize(count, OwnNone);
    }
    const int looks = listAt(SlotNamed).getSize();
    if (looks == static_cast<int>(count)) {
        return;
    }
    if (looks < static_cast<int>(count) && _own.empty() && looks > 0) {
        _own.assign(static_cast<std::size_t>(looks), OwnAll);
        _own.resize(count, OwnNone);
    }
    else if (looks == 0 && count > 0 && _own.empty() && !keepHeld) {
        _own.assign(count, OwnNone);
    }
    if (count == 0) {
        _own.clear();
        if (!keepHeld) {
            _held[SlotNamed].reset();
        }
        return;
    }
    if (keepHeld && looks == 0) {
        // Its file has not been read yet
        return;
    }
    editList(SlotNamed).setSize(static_cast<int>(count));
}

//**************************************************************************
// A kind's own look

bool PropertyElementAppearance::hasBase(Kind kind) const
{
    return listAt(SlotBase + kind).getSize() > 0;
}

const MaterialAppearance &PropertyElementAppearance::getBase(Kind kind) const
{
    const AppearanceList &list = listAt(SlotBase + kind);
    return list.getSize() > 0 ? list.getBase() : AppearanceList::defaultMaterial();
}

void PropertyElementAppearance::setBase(Kind kind, const MaterialAppearance &look)
{
    AppearanceList list = listAt(SlotBase + kind);
    if (list.getSize() == 0) {
        list.setValue(look);
    }
    else {
        list.setBase(look);
    }
    if (list.isSameData(listAt(SlotBase + kind))) {
        return;
    }
    assignBase(kind, list);
}

void PropertyElementAppearance::clearBase(Kind kind)
{
    if (hasBase(kind)) {
        assignBase(kind, AppearanceList());
    }
}

void PropertyElementAppearance::assignBase(Kind kind, const AppearanceList &list)
{
    const MaterialAppearance before = getBase(kind);
    Edit edit(*this);
    assign(SlotBase + kind, list);
    if (_held[SlotNumbered + kind]) {
        rebaseNumbered(kind, before, getBase(kind));
    }
}

void PropertyElementAppearance::setBaseList(Kind kind, const AppearanceList &own)
{
    if (own.getSize() != 1) {
        throw Base::ValueError("a kind's own look is a list of one");
    }
    if (own.isSame(listAt(SlotBase + kind))) {
        return;
    }
    assignBase(kind, own);
}

const AppearanceList &PropertyElementAppearance::getBaseList(Kind kind) const
{
    return listAt(SlotBase + kind);
}

void PropertyElementAppearance::setDetachedNamed(const AppearanceList &looks,
                                                 std::vector<uint16_t> &&own)
{
    if (owner()) {
        throw Base::RuntimeError("the names of a property on an object are the object's elements");
    }
    _detached = true;
    const auto count = static_cast<std::size_t>(looks.getSize());
    if (!own.empty() && own.size() != count) {
        throw Base::ValueError("the own fields are given for each named element, or for none");
    }
    Edit edit(*this);
    for (auto &bits : own) {
        bits &= OwnAll;
    }
    _cSubList.assign(count, std::string());
    _ShadowSubList.assign(count, ShadowSub());
    _own = std::move(own);
    assign(SlotNamed, looks, false);
    _places.reset();
}

bool PropertyElementAppearance::isFollowingMaterial(Kind kind) const
{
    const AppearanceList &list = listAt(SlotBase + kind);
    return list.getSize() == 0 || list.isFollowingMaterial();
}

void PropertyElementAppearance::followMaterial(Kind kind, const MaterialAppearance &card)
{
    AppearanceList list = listAt(SlotBase + kind);
    if (list.getSize() == 0) {
        list.setValue(card);
        list.setFollowMaterial(true);
    }
    else {
        list.followMaterial(card);
    }
    // The flag with the look: a look that was chosen, given back to the
    // card, is the same look and a change all the same
    if (list.isSame(listAt(SlotBase + kind))) {
        return;
    }
    assignBase(kind, list);
}

//**************************************************************************
// By number

const AppearanceList &PropertyElementAppearance::getNumbered(Kind kind) const
{
    return listAt(SlotNumbered + kind);
}

void PropertyElementAppearance::setNumbered(Kind kind, const AppearanceList &list)
{
    if (list.isSameData(getNumbered(kind))) {
        return;
    }
    Edit edit(*this);
    assign(SlotNumbered + kind, list);
}

uint16_t PropertyElementAppearance::getNumberedOwn(Kind kind, int index) const
{
    const AppearanceList &list = getNumbered(kind);
    if (index < 0 || index >= list.getSize()) {
        return OwnNone;
    }
    const MaterialAppearance &base = getBase(kind);
    if (!list.isOverride(index)) {
        // The list's base, which is the kind's own look until every
        // element was given the same by number
        return differingFields(list.getBase(), base);
    }
    return differingFields(list.getMaterial(index), base);
}

AppearanceList &PropertyElementAppearance::editNumbered(Kind kind, int index)
{
    const AppearanceList own = listAt(SlotBase + kind);
    AppearanceList &list = editList(SlotNumbered + kind);
    const int count = std::max(countElements(kind), index + 1);
    if (list.getSize() == 0) {
        // Every element the kind's own look, until one is given another
        if (own.getSize() > 0) {
            list = own;
            list.setSize(count);
        }
        else {
            list.setSize(count, getBase(kind));
        }
        list.setFollowMaterial(false);
    }
    else if (list.getSize() < count) {
        list.setSize(count);
    }
    return list;
}

void PropertyElementAppearance::rebaseNumbered(Kind kind, const MaterialAppearance &from,
                                               const MaterialAppearance &to)
{
    AppearanceList &list = editList(SlotNumbered + kind);
    if (list.getSize() == 0) {
        return;
    }
    const MaterialAppearance listBase = list.getBase();
    const uint16_t agreed = differingFields(listBase, from);
    if (agreed == OwnNone && list.variesOnlyInDiffuse()) {
        // The entries state colours and no more, and the list stores of an
        // entry only what it states: the rest follows of itself
        list.setBase(to);
        return;
    }
    // An entry holds a field for every field any entry states, so which of
    // them are its own is read off the look it was laid over
    const std::vector<uint32_t> overrides = list.getOverrides();
    std::vector<std::pair<int, MaterialAppearance>> kept;
    kept.reserve(overrides.size());
    for (uint32_t idx : overrides) {
        const MaterialAppearance look = list.getMaterial(static_cast<int>(idx));
        kept.emplace_back(static_cast<int>(idx), layOver(to, look, differingFields(look, from)));
    }
    // What every entry agreed on is in the list's base, and is theirs
    list.setBase(layOver(to, listBase, agreed));
    for (const auto &v : kept) {
        list.set1Value(v.first, v.second);
    }
}

//**************************************************************************
// By name

const std::vector<std::string> &PropertyElementAppearance::subs() const
{
    return _pendingSubs ? *_pendingSubs : _cSubList;
}

std::vector<std::string> &PropertyElementAppearance::editSubs()
{
    if (!_pendingSubs) {
        _pendingSubs = std::make_unique<std::vector<std::string>>(_cSubList);
    }
    return *_pendingSubs;
}

void PropertyElementAppearance::flushSubs()
{
    if (!_pendingSubs) {
        return;
    }
    std::vector<std::string> names = std::move(*_pendingSubs);
    _pendingSubs.reset();
    if (names == _cSubList) {
        return;
    }
    if (names.empty()) {
        PropertyLinkSub::setValue(nullptr);
    }
    else {
        PropertyLinkSub::setValue(owner(), std::move(names));
    }
}

const AppearanceList &PropertyElementAppearance::getNamedLooks() const
{
    return listAt(SlotNamed);
}

uint16_t PropertyElementAppearance::getNamedOwn(int pos) const
{
    if (pos < 0 || pos >= static_cast<int>(subs().size())) {
        return OwnNone;
    }
    if (_own.empty()) {
        return OwnAll;
    }
    return pos < static_cast<int>(_own.size()) ? _own[static_cast<std::size_t>(pos)] : OwnNone;
}

PropertyElementAppearance::Kind PropertyElementAppearance::getNamedKind(int pos) const
{
    if (pos < 0 || pos >= static_cast<int>(subs().size())) {
        return KindCount;
    }
    Kind kind = KindCount;
    int index = -1;
    const auto at = static_cast<std::size_t>(pos);
    // The name the shape counts it by, where the link has looked it up
    if (!_pendingSubs && at < _ShadowSubList.size() && !_ShadowSubList[at].second.empty()
        && parseElement(_ShadowSubList[at].second.c_str(), kind, index)) {
        return kind;
    }
    if (parseElement(subs()[at].c_str(), kind, index)) {
        return kind;
    }
    return KindCount;
}

MaterialAppearance PropertyElementAppearance::getNamedLook(int pos) const
{
    const Kind kind = getNamedKind(pos);
    const MaterialAppearance &base = kind == KindCount ? AppearanceList::defaultMaterial()
                                                       : getBase(kind);
    const AppearanceList &looks = getNamedLooks();
    if (pos < 0 || pos >= looks.getSize()) {
        return base;
    }
    const uint16_t own = getNamedOwn(pos);
    if (own == OwnNone) {
        return base;
    }
    return layOver(base, looks.getMaterial(pos), own);
}

int PropertyElementAppearance::findNamed(const char *element) const
{
    if (!element || !element[0] || subs().empty()) {
        return -1;
    }
    if (!_places) {
        _places = std::make_unique<std::unordered_map<std::string, int>>();
        const auto &names = subs();
        _places->reserve(names.size() * 2);
        auto note = [this](const std::string &name, int pos) {
            if (name.empty()) {
                return;
            }
            _places->emplace(name, pos);
            if (_pathNames) {
                // A path is itself, and not the element at its end
                return;
            }
            Kind kind = KindCount;
            int index = -1;
            if (parseElement(name.c_str(), kind, index) && index >= 0) {
                _places->emplace(elementName(kind, index), pos);
            }
        };
        for (std::size_t i = 0; i < names.size(); ++i) {
            const int pos = static_cast<int>(i);
            // What the link made of a name outranks the name as it was given
            if (!_pendingSubs && i < _ShadowSubList.size()) {
                note(_ShadowSubList[i].second, pos);
                note(_ShadowSubList[i].first, pos);
            }
            note(names[i], pos);
        }
    }
    auto it = _places->find(element);
    if (it != _places->end()) {
        return it->second;
    }
    if (_pathNames) {
        return -1;
    }
    Kind kind = KindCount;
    int index = -1;
    if (parseElement(element, kind, index) && index >= 0) {
        it = _places->find(elementName(kind, index));
        if (it != _places->end()) {
            return it->second;
        }
    }
    return -1;
}

void PropertyElementAppearance::setNamed(std::vector<std::string> &&names,
                                         const AppearanceList &looks,
                                         std::vector<uint16_t> &&own)
{
    if (looks.getSize() != static_cast<int>(names.size())) {
        throw Base::ValueError("one look is needed for each named element");
    }
    if (!own.empty() && own.size() != names.size()) {
        throw Base::ValueError("the own fields are given for each named element, or for none");
    }
    Edit edit(*this);
    for (auto &bits : own) {
        bits &= OwnAll;
    }
    _own = std::move(own);
    assign(SlotNamed, looks);
    editSubs() = std::move(names);
    _places.reset();
}

void PropertyElementAppearance::eraseNamed(int pos)
{
    auto &names = editSubs();
    const int count = static_cast<int>(names.size());
    if (pos < 0 || pos >= count) {
        return;
    }
    names.erase(names.begin() + pos);
    if (!_own.empty() && pos < static_cast<int>(_own.size())) {
        _own.erase(_own.begin() + pos);
    }
    _places.reset();
    const AppearanceList before = getNamedLooks();
    if (count == 1 || before.getSize() == 0) {
        _held[SlotNamed].reset();
        return;
    }
    AppearanceList next;
    next.setPBR(before.isPBR());
    next.setSize(before.getSize() - 1, before.getBase());
    for (int i = 0, j = 0; i < before.getSize(); ++i) {
        if (i == pos) {
            continue;
        }
        next.set1Value(j++, before.getMaterial(i));
    }
    next.setFollowMaterial(false);
    assign(SlotNamed, next);
}

//**************************************************************************
// Names that are paths

void PropertyElementAppearance::setPathNames(bool on)
{
    _pathNames = on;
    _places.reset();
}

bool PropertyElementAppearance::isPathName(const char *name) const
{
    if (!_pathNames || !name || !name[0]) {
        return false;
    }
    for (const char *kind : KindNames) {
        if (std::strcmp(name, kind) == 0) {
            return false;
        }
    }
    return true;
}

void PropertyElementAppearance::setNamedLook(const char *name, const MaterialAppearance &look,
                                             uint16_t own)
{
    if (!name || !name[0]) {
        throw Base::ValueError("a look is given to a name");
    }
    own &= OwnAll;
    std::vector<std::string> names = subs();
    const AppearanceList &before = getNamedLooks();
    const int count = static_cast<int>(names.size());
    int pos = findNamed(name);
    if (pos >= 0 && pos < before.getSize() && getNamedOwn(pos) == own
        && differingFields(before.getMaterial(pos), look) == OwnNone) {
        return;
    }
    AppearanceList looks;
    looks.setPBR(before.isPBR());
    looks.setSize(count + (pos < 0 ? 1 : 0));
    std::vector<uint16_t> owns(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (i < before.getSize()) {
            looks.set1Value(i, before.getMaterial(i));
        }
        owns[static_cast<std::size_t>(i)] = getNamedOwn(i);
    }
    if (pos < 0) {
        pos = count;
        names.emplace_back(name);
        owns.push_back(own);
    }
    else {
        owns[static_cast<std::size_t>(pos)] = own;
    }
    looks.set1Value(pos, look);
    looks.setFollowMaterial(false);
    setNamed(std::move(names), looks, std::move(owns));
}

//**************************************************************************
// What is drawn

const AppearanceList &PropertyElementAppearance::getDrawn(Kind kind) const
{
    if (_held[SlotDrawn + kind]) {
        return listAt(SlotDrawn + kind);
    }
    const AppearanceList &numbered = getNumbered(kind);
    return numbered.getSize() > 0 ? numbered : listAt(SlotBase + kind);
}

void PropertyElementAppearance::setDrawn(Kind kind, const AppearanceList &list)
{
    const int slot = SlotDrawn + kind;
    const AppearanceList &numbered = getNumbered(kind);
    const AppearanceList &stated = numbered.getSize() > 0 ? numbered : listAt(SlotBase + kind);
    // Every element the kind's own look and no more is what the kind's own
    // look says already, however many of them the list was made for
    const bool plain = numbered.getSize() == 0 && !list.hasOverrides()
        && differingFields(list.getBase(), getBase(kind)) == OwnNone;
    if (list.getSize() == 0 || plain || list.isSameData(stated) || list.isSame(stated)) {
        // What is stated is what is drawn, and is not kept twice
        if (!_held[slot]) {
            return;
        }
        _held[slot].reset();
    }
    else {
        if (_held[slot] && listAt(slot).isSame(list)) {
            return;
        }
        assign(slot, list);
    }
    // Made of what is, so there is nothing for an undo to take back: the
    // owner is told, and no change is recorded
    touch();
}

AppearanceList PropertyElementAppearance::compose(
    Kind kind,
    int count,
    const std::vector<std::pair<int, int>> &named,
    const std::map<int, MaterialAppearance> *handedOn) const
{
    AppearanceList out = getNumbered(kind);
    // Whether the kind's own look is the card's is said of what is drawn of
    // it too: a list drawn from is asked that as the own look would be
    const bool following = isFollowingMaterial(kind);
    if (out.getSize() == 0 || (count >= 0 && out.getSize() != count)) {
        // No element given a look by number -- or they were counted for
        // another shape, and say nothing of this one's
        out = listAt(SlotBase + kind);
    }
    if (count >= 0 && out.getSize() != count) {
        if (out.getSize() == 0 && count > 0) {
            out.setSize(count, getBase(kind));
        }
        else {
            out.setSize(count);
        }
    }
    if (handedOn) {
        for (const auto &v : *handedOn) {
            if (v.first >= 0 && v.first < out.getSize()) {
                out.set1Value(v.first, v.second);
            }
        }
    }
    for (const auto &v : named) {
        if (v.first >= 0 && v.first < out.getSize() && v.second >= 0
            && v.second < getNamedCount()) {
            out.set1Value(v.first, getNamedLook(v.second));
        }
    }
    out.setFollowMaterial(following);
    return out;
}

//**************************************************************************
// Smart indexing

bool PropertyElementAppearance::hasMappedName(Kind kind, int index) const
{
    auto data = geometry();
    if (!data || index < 0) {
        return false;
    }
    return !data->getMappedName(Data::IndexedName::fromConst(kindName(kind), index + 1)).empty();
}

int PropertyElementAppearance::countElements(Kind kind) const
{
    auto data = geometry();
    return data ? static_cast<int>(data->countSubElements(kindName(kind))) : -1;
}

bool PropertyElementAppearance::resolveElement(const char *element, Kind &kind, int &index) const
{
    if (_pathNames) {
        // A kind's own name and nothing else: the rest are paths, and no
        // element of the owner
        return element && !isPathName(element) && parseElement(element, kind, index)
            && index < 0;
    }
    if (parseElement(element, kind, index)) {
        return true;
    }
    const int pos = findNamed(element);
    if (pos >= 0) {
        const auto at = static_cast<std::size_t>(pos);
        if (!_pendingSubs && at < _ShadowSubList.size()
            && parseElement(_ShadowSubList[at].second.c_str(), kind, index)) {
            return true;
        }
    }
    auto data = geometry();
    if (!data || !element || !element[0]) {
        return false;
    }
    const Data::IndexedName indexed = data->getElementName(element).index;
    if (!indexed) {
        return false;
    }
    std::string name;
    indexed.appendToStringBuffer(name);
    return parseElement(name.c_str(), kind, index);
}

MaterialAppearance PropertyElementAppearance::getLook(Kind kind, int index) const
{
    if (kind < 0 || kind >= KindCount) {
        throw Base::ValueError("no such kind of element");
    }
    if (index < 0) {
        return getBase(kind);
    }
    const int pos = findNamed(elementName(kind, index).c_str());
    if (pos >= 0) {
        return getNamedLook(pos);
    }
    const AppearanceList &drawn = getDrawn(kind);
    return index < drawn.getSize() ? drawn.getMaterial(index) : getBase(kind);
}

MaterialAppearance PropertyElementAppearance::getLook(const char *element) const
{
    const int pos = findNamed(element);
    if (pos >= 0) {
        return getNamedLook(pos);
    }
    Kind kind = KindCount;
    int index = -1;
    if (!resolveElement(element, kind, index)) {
        FC_THROWM(Base::ValueError, "'" << (element ? element : "") << "' names no element");
    }
    return getLook(kind, index);
}

void PropertyElementAppearance::setLook(Kind kind, int index, const MaterialAppearance &look,
                                        uint16_t own)
{
    if (kind < 0 || kind >= KindCount) {
        throw Base::ValueError("no such kind of element");
    }
    if (index < 0) {
        setBase(kind, look);
        return;
    }
    if (_pathNames) {
        throw Base::ValueError("an element is named by its path here, not by its number");
    }
    own &= OwnAll;
    if (own == OwnNone) {
        return;
    }
    const std::string name = elementName(kind, index);
    int pos = findNamed(name.c_str());
    if (pos < 0 && !hasMappedName(kind, index)) {
        // No name to hold it by: the number
        const AppearanceList &numbered = getNumbered(kind);
        const MaterialAppearance current = index < numbered.getSize()
            ? numbered.getMaterial(index)
            : getBase(kind);
        const MaterialAppearance next = layOver(current, look, own);
        if (differingFields(next, current) == OwnNone) {
            return;
        }
        Edit edit(*this);
        editNumbered(kind, index).set1Value(index, next);
        if (look.texture.isSet() || !look.materialx.empty()) {
            _held[SlotNumbered + kind]->list.holdStoredBlobs();
        }
        return;
    }
    Edit edit(*this);
    AppearanceList &looks = editList(SlotNamed);
    const int count = static_cast<int>(subs().size());
    if (looks.getSize() != count) {
        if (count > 0 && looks.getSize() < count && _own.empty() && looks.getSize() > 0) {
            _own.assign(static_cast<std::size_t>(looks.getSize()), OwnAll);
        }
        if (count == 0) {
            looks = AppearanceList();
        }
        else {
            looks.setSize(count);
        }
    }
    if (pos < 0) {
        pos = count;
        if (count == 0) {
            looks.setPBR(listAt(SlotBase + Face).isPBR());
        }
        // What the name does not state is kept as the object has it now,
        // which is what keeps the list to the fields that are stated
        looks.set1Value(pos, layOver(getBase(kind), look, own));
        looks.setFollowMaterial(false);
        if (_own.empty() && own != OwnAll) {
            _own.assign(static_cast<std::size_t>(count), OwnAll);
        }
        if (!_own.empty() || own != OwnAll) {
            _own.resize(static_cast<std::size_t>(count), OwnNone);
            _own.push_back(own);
        }
        editSubs().push_back(name);
        if (_places) {
            _places->emplace(name, pos);
        }
    }
    else {
        const uint16_t before = getNamedOwn(pos);
        looks.set1Value(pos, layOver(looks.getMaterial(pos), look, own));
        const uint16_t merged = before | own;
        if (merged != before) {
            if (_own.empty()) {
                _own.assign(static_cast<std::size_t>(count), OwnAll);
            }
            _own.resize(static_cast<std::size_t>(count), OwnNone);
            _own[static_cast<std::size_t>(pos)] = merged;
        }
    }
    if (look.texture.isSet() || !look.materialx.empty()) {
        _held[SlotNamed]->list.holdStoredBlobs();
    }
}

void PropertyElementAppearance::setLook(const char *element, const MaterialAppearance &look,
                                        uint16_t own)
{
    if (isPathName(element)) {
        setNamedLook(element, look, own);
        return;
    }
    Kind kind = KindCount;
    int index = -1;
    if (!resolveElement(element, kind, index)) {
        FC_THROWM(Base::ValueError, "'" << (element ? element : "") << "' names no element");
    }
    setLook(kind, index, look, own);
}

void PropertyElementAppearance::setColor(Kind kind, int index, const Color &color)
{
    MaterialAppearance look = getLook(kind, index);
    look.diffuseColor = color;
    look.transparency = color.transparency();
    if (index < 0) {
        setBase(kind, look);
        return;
    }
    setLook(kind, index, look, OwnDiffuse);
}

void PropertyElementAppearance::setColor(const char *element, const Color &color)
{
    if (isPathName(element)) {
        const int pos = findNamed(element);
        MaterialAppearance look = pos >= 0 && pos < getNamedLooks().getSize()
            ? getNamedLooks().getMaterial(pos)
            : MaterialAppearance(AppearanceList::defaultMaterial());
        look.diffuseColor = color;
        look.transparency = color.transparency();
        setNamedLook(element, look, (pos >= 0 ? getNamedOwn(pos) : OwnNone) | OwnDiffuse);
        return;
    }
    Kind kind = KindCount;
    int index = -1;
    if (!resolveElement(element, kind, index)) {
        FC_THROWM(Base::ValueError, "'" << (element ? element : "") << "' names no element");
    }
    setColor(kind, index, color);
}

uint16_t PropertyElementAppearance::getOwn(Kind kind, int index) const
{
    if (kind < 0 || kind >= KindCount || index < 0) {
        return OwnNone;
    }
    const int pos = findNamed(elementName(kind, index).c_str());
    return pos >= 0 ? getNamedOwn(pos) : getNumberedOwn(kind, index);
}

bool PropertyElementAppearance::isStated(Kind kind, int index) const
{
    if (kind < 0 || kind >= KindCount || index < 0) {
        return false;
    }
    return findNamed(elementName(kind, index).c_str()) >= 0
        || getNumberedOwn(kind, index) != OwnNone;
}

bool PropertyElementAppearance::removeLook(Kind kind, int index)
{
    if (kind < 0 || kind >= KindCount || index < 0) {
        return false;
    }
    const int pos = findNamed(elementName(kind, index).c_str());
    const bool numbered = getNumberedOwn(kind, index) != OwnNone;
    if (pos < 0 && !numbered) {
        return false;
    }
    Edit edit(*this);
    if (pos >= 0) {
        eraseNamed(pos);
    }
    if (numbered) {
        editList(SlotNumbered + kind).set1Value(index, getBase(kind));
    }
    return true;
}

bool PropertyElementAppearance::removeLook(const char *element)
{
    Kind kind = KindCount;
    int index = -1;
    if (resolveElement(element, kind, index)) {
        return removeLook(kind, index);
    }
    // A name the shape no longer has: by its place among the names
    const int pos = findNamed(element);
    if (pos < 0) {
        return false;
    }
    Edit edit(*this);
    eraseNamed(pos);
    return true;
}

void PropertyElementAppearance::clear()
{
    if (isEmpty()) {
        return;
    }
    Edit edit(*this);
    for (auto &held : _held) {
        held.reset();
    }
    _own.clear();
    editSubs().clear();
    _places.reset();
}

bool PropertyElementAppearance::isEmpty() const
{
    if (!subs().empty()) {
        return false;
    }
    return std::all_of(_held.begin(), _held.end(), [](const std::unique_ptr<Held> &held) {
        return !held;
    });
}

const std::string &PropertyElementAppearance::statedName(std::size_t pos) const
{
    if (!_pendingSubs && pos < _ShadowSubList.size() && !_ShadowSubList[pos].second.empty()) {
        return _ShadowSubList[pos].second;
    }
    return subs()[pos];
}

std::vector<int> PropertyElementAppearance::statedNumbers(Kind kind) const
{
    std::vector<int> res;
    const AppearanceList &list = getNumbered(kind);
    if (list.getSize() == 0) {
        return res;
    }
    if (differingFields(list.getBase(), getBase(kind)) == OwnNone) {
        for (uint32_t idx : list.getOverrides()) {
            if (getNumberedOwn(kind, static_cast<int>(idx)) != OwnNone) {
                res.push_back(static_cast<int>(idx));
            }
        }
    }
    else {
        // Every element was given the same by number, and the list keeps
        // that as its base
        for (int idx = 0; idx < list.getSize(); ++idx) {
            if (getNumberedOwn(kind, idx) != OwnNone) {
                res.push_back(idx);
            }
        }
    }
    return res;
}

std::vector<std::pair<std::string, MaterialAppearance>>
PropertyElementAppearance::getStatedLooks() const
{
    std::vector<std::pair<std::string, MaterialAppearance>> res;
    const auto &names = subs();
    for (std::size_t i = 0; i < names.size(); ++i) {
        res.emplace_back(statedName(i), getNamedLook(static_cast<int>(i)));
    }
    for (int k = 0; k < KindCount; ++k) {
        const auto kind = static_cast<Kind>(k);
        const AppearanceList &list = getNumbered(kind);
        for (int idx : statedNumbers(kind)) {
            const std::string name = elementName(kind, idx);
            if (findNamed(name.c_str()) < 0) {
                res.emplace_back(name, list.getMaterial(idx));
            }
        }
    }
    return res;
}

//**************************************************************************
// Many elements at once

struct PropertyElementAppearance::Stated
{
    const std::string *name;
    const MaterialAppearance *look;
    const Color *color;

    uint16_t own() const
    {
        return look ? OwnAll : OwnDiffuse;
    }
    /// What an element that is \a current comes to be
    MaterialAppearance over(const MaterialAppearance &current) const
    {
        if (look) {
            return *look;
        }
        MaterialAppearance out = current;
        out.diffuseColor = *color;
        out.transparency = color->transparency();
        return out;
    }
};

void PropertyElementAppearance::setStatedColors(const std::map<std::string, Color> &colors,
                                                std::vector<std::string> *unknown)
{
    std::vector<Stated> stated;
    stated.reserve(colors.size());
    for (const auto &v : colors) {
        stated.push_back({&v.first, nullptr, &v.second});
    }
    setStated(stated, unknown);
}

void PropertyElementAppearance::setStatedLooks(
    const std::map<std::string, MaterialAppearance> &looks,
    std::vector<std::string> *unknown)
{
    std::vector<Stated> stated;
    stated.reserve(looks.size());
    for (const auto &v : looks) {
        stated.push_back({&v.first, &v.second, nullptr});
    }
    setStated(stated, unknown);
}

void PropertyElementAppearance::setStated(const std::vector<Stated> &stated,
                                          std::vector<std::string> *unknown)
{
    // In the order of their names, as a map has them
    auto given = [&stated](const std::string &name) {
        const auto it = std::lower_bound(stated.begin(), stated.end(), name,
                                         [](const Stated &s, const std::string &value) {
                                             return *s.name < value;
                                         });
        return it != stated.end() && *it->name == name;
    };
    Edit edit(*this);
    if (_pathNames) {
        // A path is found by its string and by nothing else, one at a time
        for (const auto &v : getStatedLooks()) {
            if (!given(v.first)) {
                removeLook(v.first.c_str());
            }
        }
        for (const Stated &s : stated) {
            if (!isPathName(s.name->c_str())) {
                continue;
            }
            if (s.look) {
                setLook(s.name->c_str(), *s.look);
            }
            else {
                setColor(s.name->c_str(), *s.color);
            }
        }
        return;
    }

    // Which element each name is. Of two names of one element the later
    // says, as it did when they were given one after the other.
    struct Target
    {
        Kind kind;
        int index;
        const Stated *what;
    };
    std::vector<Target> targets;
    targets.reserve(stated.size());
    for (const Stated &s : stated) {
        Kind kind = KindCount;
        int index = -1;
        if (!resolveElement(s.name->c_str(), kind, index)) {
            if (unknown) {
                unknown->push_back(*s.name);
            }
            continue;
        }
        if (index >= 0) {
            targets.push_back({kind, index, &s});
        }
    }
    auto before = [](const Target &a, const Target &b) {
        return a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
    };
    std::stable_sort(targets.begin(), targets.end(), before);
    {
        std::size_t out = 0;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (out > 0 && targets[out - 1].kind == targets[i].kind
                && targets[out - 1].index == targets[i].index) {
                targets[out - 1] = targets[i];
            }
            else {
                targets[out++] = targets[i];
            }
        }
        targets.resize(out);
    }
    auto isTarget = [&](Kind kind, int index) {
        return std::binary_search(targets.begin(), targets.end(), Target {kind, index, nullptr},
                                  before);
    };

    // Each to the name the element has, to a name the shape gives it, or to
    // its number
    const int count = static_cast<int>(subs().size());
    const AppearanceList named = getNamedLooks();
    std::vector<const Stated *> toName(static_cast<std::size_t>(count), nullptr);
    std::vector<Target> added;
    std::array<std::vector<std::pair<int, const Stated *>>, KindCount> toNumber;
    for (const Target &t : targets) {
        const int pos = findNamed(elementName(t.kind, t.index).c_str());
        if (pos >= 0 && pos < count) {
            toName[static_cast<std::size_t>(pos)] = t.what;
        }
        else if (hasMappedName(t.kind, t.index)) {
            added.push_back(t);
        }
        else {
            toNumber[t.kind].emplace_back(t.index, t.what);
        }
    }

    // The names: those the call leaves out go, in one pass
    std::vector<char> keep(static_cast<std::size_t>(count), 0);
    bool renamed = !added.empty();
    for (std::size_t pos = 0; pos < keep.size(); ++pos) {
        keep[pos] = toName[pos] || given(statedName(pos));
        renamed = renamed || !keep[pos] || toName[pos];
    }

    // The numbers, a kind at a time. Decided before the names are changed:
    // an element with both goes with its name.
    for (int k = 0; k < KindCount; ++k) {
        const auto kind = static_cast<Kind>(k);
        // The list as it was, which is what is read: the copy is written
        const AppearanceList numbered = getNumbered(kind);
        const MaterialAppearance base = getBase(kind);
        // What is given and what is taken away, in the order of the entries
        std::vector<std::pair<int, const Stated *>> plan;
        {
            const auto &gives = toNumber[kind];
            const std::vector<int> had = statedNumbers(kind);
            plan.reserve(gives.size() + had.size());
            std::size_t g = 0;
            for (int idx : had) {
                while (g < gives.size() && gives[g].first < idx) {
                    plan.push_back(gives[g++]);
                }
                if (g < gives.size() && gives[g].first == idx) {
                    plan.push_back(gives[g++]);
                    continue;
                }
                if (isTarget(kind, idx)) {
                    continue;
                }
                const int pos = findNamed(elementName(kind, idx).c_str());
                if (pos < 0 || pos >= count || !keep[static_cast<std::size_t>(pos)]) {
                    plan.emplace_back(idx, nullptr);
                }
            }
            plan.insert(plan.end(), gives.begin() + static_cast<std::ptrdiff_t>(g), gives.end());
        }
        if (plan.empty()) {
            continue;
        }
        const int size =
            std::max({countElements(kind), plan.back().first + 1, numbered.getSize()});
        AppearanceList work = numbered;
        if (work.getSize() == 0) {
            // Every element the kind's own look, until one is given another
            work = listAt(SlotBase + kind);
            if (work.getSize() > 0) {
                work.setSize(size);
            }
            else {
                work.setSize(size, base);
            }
            work.setFollowMaterial(false);
        }
        else if (work.getSize() < size) {
            work.setSize(size);
        }
        auto current = [&numbered, &base](int idx) {
            return idx < numbered.getSize() ? numbered.getMaterial(idx) : base;
        };
        auto next = [&base](const Stated *what, const MaterialAppearance &now) {
            return what ? what->over(now) : base;
        };
        bool any = false;
        if (plan.size() <= FewElements) {
            for (const auto &v : plan) {
                const MaterialAppearance now = current(v.first);
                const MaterialAppearance then = next(v.second, now);
                if (differingFields(then, now) != OwnNone) {
                    work.set1Value(v.first, then);
                    any = true;
                }
            }
        }
        else {
            // An entry put among those a list holds moves every one behind
            // it. Made again from the first to the last, each is put at the
            // end.
            const std::vector<uint32_t> overrides = numbered.getOverrides();
            work.clearOverrides();
            std::size_t p = 0;
            for (uint32_t held : overrides) {
                const int idx = static_cast<int>(held);
                for (; p < plan.size() && plan[p].first < idx; ++p) {
                    const MaterialAppearance now = current(plan[p].first);
                    const MaterialAppearance then = next(plan[p].second, now);
                    any = any || differingFields(then, now) != OwnNone;
                    work.set1Value(plan[p].first, then);
                }
                const MaterialAppearance now = numbered.getMaterial(idx);
                if (p < plan.size() && plan[p].first == idx) {
                    const MaterialAppearance then = next(plan[p++].second, now);
                    any = any || differingFields(then, now) != OwnNone;
                    work.set1Value(idx, then);
                }
                else {
                    work.set1Value(idx, now);
                }
            }
            for (; p < plan.size(); ++p) {
                const MaterialAppearance now = current(plan[p].first);
                const MaterialAppearance then = next(plan[p].second, now);
                any = any || differingFields(then, now) != OwnNone;
                work.set1Value(plan[p].first, then);
            }
        }
        if (any) {
            assign(SlotNumbered + kind, work);
        }
    }

    if (!renamed) {
        return;
    }
    const std::vector<std::string> &names = subs();
    std::vector<std::string> nextNames;
    std::vector<uint16_t> nextOwn;
    AppearanceList looks;
    looks.setPBR(named.getSize() > 0 ? named.isPBR() : listAt(SlotBase + Face).isPBR());
    looks.setSize(static_cast<int>(std::count(keep.begin(), keep.end(), 1))
                  + static_cast<int>(added.size()));
    nextNames.reserve(static_cast<std::size_t>(looks.getSize()));
    nextOwn.reserve(static_cast<std::size_t>(looks.getSize()));
    for (std::size_t pos = 0; pos < keep.size(); ++pos) {
        if (!keep[pos]) {
            continue;
        }
        MaterialAppearance look = named.getMaterial(static_cast<int>(pos));
        uint16_t own = getNamedOwn(static_cast<int>(pos));
        if (const Stated *what = toName[pos]) {
            look = layOver(look, what->over(look), what->own());
            own |= what->own();
        }
        looks.set1Value(static_cast<int>(nextNames.size()), look);
        nextNames.push_back(names[pos]);
        nextOwn.push_back(own);
    }
    for (const Target &t : added) {
        // What the name does not state is kept as the object has it now,
        // which is what keeps the list to the fields that are stated
        const MaterialAppearance &base = getBase(t.kind);
        looks.set1Value(static_cast<int>(nextNames.size()),
                        layOver(base, t.what->over(base), t.what->own()));
        nextNames.push_back(elementName(t.kind, t.index));
        nextOwn.push_back(t.what->own());
    }
    looks.setFollowMaterial(false);
    setNamed(std::move(nextNames), looks, std::move(nextOwn));
}

//**************************************************************************
// The property

void PropertyElementAppearance::updateElementReference(DocumentObject *feature, bool reverse,
                                                       bool notify)
{
    PropertyLinkSubHidden::updateElementReference(feature, reverse, notify);
    _places.reset();
}

Property *PropertyElementAppearance::Copy() const
{
    auto *p = new PropertyElementAppearance();
    p->_pathNames = _pathNames;
    p->_pcLinkSub = _pcLinkSub;
    p->_cSubList = _cSubList;
    p->_ShadowSubList = _ShadowSubList;
    p->_own = _own;
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (_held[slot]) {
            p->assign(slot, _held[slot]->list.getList(), false);
        }
    }
    return p;
}

void PropertyElementAppearance::Paste(const Property &from)
{
    if (auto other = Base::freecad_dynamic_cast<const PropertyElementAppearance>(&from)) {
        if (other == this) {
            return;
        }
        Edit edit(*this);
        _pendingSubs.reset();
        for (int slot = 0; slot < SlotCount; ++slot) {
            assign(slot, other->listAt(slot));
        }
        _own = other->_own;
        // The link is to the object the property is on, whichever that is
        DocumentObject *linked = other->_pcLinkSub;
        if (linked && linked == other->owner() && owner()) {
            linked = owner();
        }
        PropertyLinkSub::setValue(linked, other->_cSubList,
                                  std::vector<ShadowSub>(other->_ShadowSubList));
        return;
    }
    if (from.isDerivedFrom(PropertyLinkSub::getClassTypeId())) {
        // Names and nothing else: the looks there are stay with the names
        // they were given to, by their place
        Edit edit(*this);
        _pendingSubs.reset();
        PropertyLinkSubHidden::Paste(from);
        conformNames();
        return;
    }
    THROWM(Base::TypeError, "Incompatible property to paste to")
}

bool PropertyElementAppearance::isSame(const Property &other) const
{
    if (&other == this) {
        return true;
    }
    auto o = Base::freecad_dynamic_cast<const PropertyElementAppearance>(&other);
    if (!o) {
        return false;
    }
    if (_pcLinkSub != o->_pcLinkSub || _cSubList != o->_cSubList) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(_cSubList.size()); ++i) {
        if (getNamedOwn(i) != o->getNamedOwn(i)) {
            return false;
        }
    }
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (!listAt(slot).isSame(o->listAt(slot))) {
            return false;
        }
    }
    return true;
}

bool PropertyElementAppearance::isSameStated(const PropertyElementAppearance &other) const
{
    if (&other == this) {
        return true;
    }
    if (subs() != other.subs()) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(subs().size()); ++i) {
        if (getNamedOwn(i) != other.getNamedOwn(i)) {
            return false;
        }
    }
    for (int slot = 0; slot < SlotDrawn; ++slot) {
        if (!listAt(slot).isSame(other.listAt(slot))) {
            return false;
        }
    }
    return true;
}

unsigned int PropertyElementAppearance::getMemSize() const
{
    unsigned int size = PropertyLinkSubHidden::getMemSize();
    size += static_cast<unsigned int>(_own.size() * sizeof(uint16_t));
    for (const auto &held : _held) {
        if (held) {
            size += held->list.getMemSize();
        }
    }
    return size;
}

/** The link as a link is written, and then the looks
 *
 * In that order so that a reader that knows the names and nothing else -- a
 * plain link, which is what this property's names were before it -- reads
 * them and steps over the rest.
 *
 * The own fields are written run by run, "3*1 7ff": most names of a shape
 * state the same.
 */
void PropertyElementAppearance::Save(Base::Writer &writer) const
{
    // On no object there are no names to write: the looks alone
    if (!isDetached()) {
        PropertyLinkSubHidden::Save(writer);
    }
    syncHeld();
    unsigned mask = 0;
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (_held[slot]) {
            mask |= 1U << slot;
        }
    }
    writer.Stream() << writer.ind() << "<ElementAppearance lists=\"" << mask << "\"";
    if (!_own.empty()) {
        writer.Stream() << " own=\"" << std::hex;
        for (std::size_t i = 0; i < _own.size();) {
            std::size_t j = i + 1;
            while (j < _own.size() && _own[j] == _own[i]) {
                ++j;
            }
            if (i) {
                writer.Stream() << ' ';
            }
            if (j - i > 1) {
                writer.Stream() << (j - i) << '*';
            }
            writer.Stream() << _own[i];
            i = j;
        }
        writer.Stream() << std::dec << "\"";
    }
    writer.Stream() << "/>\n";
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (_held[slot]) {
            _held[slot]->list.Save(writer);
        }
    }
}

void PropertyElementAppearance::Restore(Base::XMLReader &reader)
{
    // A list whose file is still to be read has no entries yet, and is not
    // one that holds nothing
    Base::StateLocker restoring(_restoring);
    Edit edit(*this);
    _pendingSubs.reset();
    if (!owner()) {
        // No object to look the names up in: how many there were, so that
        // the looks that follow are counted as theirs
        _detached = true;
        reader.readElement("LinkSub");
        const long count = reader.getAttributeAsInteger("count", "0");
        for (long i = 0; i < count; ++i) {
            reader.readElement("Sub");
        }
        reader.readEndElement("LinkSub");
        _cSubList.assign(static_cast<std::size_t>(std::max(count, 0L)), std::string());
        _ShadowSubList.assign(_cSubList.size(), ShadowSub());
    }
    else {
        PropertyLinkSubHidden::Restore(reader);
    }
    for (auto &held : _held) {
        held.reset();
    }
    _own.clear();
    _places.reset();
    // Names and no looks is a link as it was written before this property,
    // or by something that knows no more: what follows is the end of the
    // property, which is left where it is for whoever reads on
    if (!reader.readNextElement() || std::strcmp(reader.localName(), "ElementAppearance") != 0) {
        conformNames();
        return;
    }
    // The looks are the file's: names alone are a link's, and say nothing
    // of what the object looked like (wasRestored())
    _wasRestored = true;
    const unsigned mask = static_cast<unsigned>(reader.getAttributeAsInteger("lists", "0"));
    if (reader.hasAttribute("own")) {
        std::istringstream str(reader.getAttribute("own"));
        std::string token;
        while (str >> token) {
            unsigned long run = 1;
            const std::size_t star = token.find('*');
            const std::string value = star == std::string::npos ? token : token.substr(star + 1);
            if (star != std::string::npos) {
                run = std::strtoul(token.substr(0, star).c_str(), nullptr, 16);
            }
            const auto bits = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, 16) & OwnAll);
            if (run > _cSubList.size() - std::min(_cSubList.size(), _own.size())) {
                run = _cSubList.size() - std::min(_cSubList.size(), _own.size());
            }
            _own.insert(_own.end(), run, bits);
        }
    }
    for (int slot = 0; slot < SlotCount; ++slot) {
        if (!(mask & (1U << slot))) {
            continue;
        }
        editList(slot);
        _held[slot]->list.Restore(reader);
    }
    conformNames(true);
}

//**************************************************************************
// The content a look names

void PropertyElementAppearance::collectBlobs(FileBlobManager &manager,
                                             const DocumentObject *object) const
{
    syncHeld();
    for (const auto &held : _held) {
        if (held) {
            held->list.collectBlobs(manager, object);
        }
    }
}

void PropertyElementAppearance::assignRestoredBlob(const FileBlobHandle &blob)
{
    // Each list asks for its own content and is answered itself; this is
    // for whoever hands content to the property as a whole
    for (auto &held : _held) {
        if (held) {
            held->list.assignRestoredBlob(blob);
        }
    }
}

bool PropertyElementAppearance::blobContentNeedsStore() const
{
    return std::any_of(_held.begin(), _held.end(), [](const std::unique_ptr<Held> &held) {
        return held && held->list.blobContentNeedsStore();
    });
}

//**************************************************************************
// Python

void PropertyElementAppearance::registerView(ElementAppearancePy *view)
{
    _views.push_back(view);
}

void PropertyElementAppearance::unregisterView(ElementAppearancePy *view)
{
    _views.erase(std::remove(_views.begin(), _views.end(), view), _views.end());
}

PyObject *PropertyElementAppearance::getPyObject()
{
    // A view, not a copy: it reads this property and writes to it. Fresh
    // each time, and registered so that it does not outlive what it shows.
    auto *view = new ElementAppearancePy(this);
    registerView(view);
    return view;
}

void PropertyElementAppearance::setPyObject(PyObject *value)
{
    if (value == Py_None) {
        clear();
        return;
    }
    if (PyObject_TypeCheck(value, &(ElementAppearancePy::Type))) {
        auto *view = static_cast<ElementAppearancePy *>(value);
        if (!view->isValid()) {
            throw Base::RuntimeError("the element appearance is no longer valid");
        }
        Paste(*view->getPropertyElementAppearancePtr());
        return;
    }
    if (PyDict_Check(value)) {
        // Everything stated, replaced by what the dict states
        Edit edit(*this);
        clear();
        ElementAppearancePy::update(*this, value);
        return;
    }
    // (object, [names]): the names, as a link takes them
    Edit edit(*this);
    _pendingSubs.reset();
    PropertyLinkSubHidden::setPyObject(value);
    conformNames();
}
