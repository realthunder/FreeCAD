// SPDX-License-Identifier: LGPL-2.1-or-later

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

// The elements of a shape painted by name, merged by name
// (docs/TransactionLog.md sec 31.20). The names are the object's
// (ColoredElements) and the look of each its view provider's
// (MappedAppearance), one entry for each by its place: the two are one
// thing, and a merge that took one side's names and kept the other's looks
// would paint each face with another's.

#include "PreCompiled.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyStandard.h>
#include <App/TransactionValue.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>

#include "PartFeature.h"

using namespace Part;

namespace
{

const char* const Names = "ColoredElements";
/// A property of the view provider, as a merge unit names one
const char* const Looks = "view:MappedAppearance";

using State = App::DocumentObject::MergeUnitState;
using Note = App::DocumentObject::MergeUnitNote;

/// The value of an attribute of one saved tag. Attribute values are
/// written encoded, so there is no `"` inside one.
std::string attribute(const std::string& tag, const char* name)
{
    const std::string key = std::string(" ") + name + "=\"";
    const std::size_t at = tag.find(key);
    if (at == std::string::npos)
        return {};
    const std::size_t from = at + key.size();
    const std::size_t to = tag.find('"', from);
    return to == std::string::npos ? std::string() : tag.substr(from, to - from);
}

/// Field by field: App::MaterialAppearance::operator== calls two that name
/// one card the same whatever their colours say.
bool sameLook(const App::MaterialAppearance& a, const App::MaterialAppearance& b)
{
    return a.getType() == b.getType() && a.pbr == b.pbr && a.ambientColor == b.ambientColor
        && a.diffuseColor == b.diffuseColor && a.specularColor == b.specularColor
        && a.emissiveColor == b.emissiveColor && a.shininess == b.shininess
        && a.finish == b.finish && a.texture == b.texture && a.image == b.image
        && a.imagePath == b.imagePath && a.uuid == b.uuid && a.materialx == b.materialx;
}

/// One painted element.
struct Painted
{
    std::string tag;    ///< `<Sub .../>`, as saved
    std::string shown;  ///< the name a reader knows it by
    App::MaterialAppearance look;
};

/// The painted elements at one moment, by what each is known by on any
/// branch: its mapped name where it has one -- what a face is called does
/// not depend on how many faces come before it -- else the name it has.
struct Paint
{
    std::string target;
    std::vector<std::string> order;
    std::map<std::string, Painted> at;
    bool pbr {false};
    /// Colours and no more, each the object in that colour: the list's
    /// follow flag (docs/ShapeAppearanceDesign.md sec 13.7)
    bool coloursOnly {false};
    App::AppearanceList list;

    /// `object` is the object's own look where it can be had: a list of
    /// colours and no more is then read as what it stands for, the object
    /// in each colour.
    bool read(const State& state, const App::MaterialAppearance* object)
    {
        auto names = state.find(Names);
        auto looks = state.find(Looks);
        if (names == state.end() || looks == state.end())
            return false;
        App::Property::SavedElements subs;
        if (!App::Property::savedTags(names->second, "Sub", "value", subs))
            return false;
        const std::size_t head = names->second.find("<LinkSub ");
        if (head == std::string::npos)
            return false;
        target = attribute(names->second.substr(head, names->second.find('>', head) - head),
                           "value");
        App::PropertyAppearanceList held;
        try {
            App::CapturedValue value;
            value.fragment = looks->second;
            value.ok = true;
            App::restoreValue(held, value);
        }
        catch (const Base::Exception&) {
            return false;
        }
        catch (const std::exception&) {
            return false;
        }
        // The two are written one after the other; a state between them
        // is not one to merge.
        if (held.getSize() != static_cast<int>(subs.size()))
            return false;
        list = held.getList();
        pbr = list.isPBR();
        coloursOnly = !subs.empty() && list.isFollowingMaterial();
        for (std::size_t i = 0; i < subs.size(); ++i) {
            Painted p;
            p.tag = subs[i].second;
            p.shown = subs[i].first;
            p.look = list.getMaterial(static_cast<int>(i));
            if (coloursOnly && object) {
                const App::Color colour = p.look.diffuseColor;
                p.look = *object;
                p.look.diffuseColor = colour;
                p.look.transparency = colour.transparency();
            }
            std::string key = attribute(p.tag, "shadowed");
            if (key.empty())
                key = attribute(p.tag, "shadow");
            if (key.empty())
                key = p.shown;
            if (!at.emplace(key, std::move(p)).second)
                return false;   // one element named twice
            order.push_back(std::move(key));
        }
        return true;
    }
};

/// Where both branches gave one element another look: `ours`, `theirs`,
/// or `asked` -- the whole of the object's paint then one question.
std::string facePaint()
{
    return App::GetApplication()
        .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
        ->GetASCII("TransactionLogMergeFacePaint", "ours");
}

}  // namespace

std::vector<std::string> Feature::getMergeUnit(const char* prop) const
{
    static const std::vector<std::string> paint {Names, Looks};
    if (prop && (std::strcmp(prop, Names) == 0 || std::strcmp(prop, Looks) == 0))
        return paint;
    return inherited::getMergeUnit(prop);
}

bool Feature::mergeUnit(const MergeUnitState& baseAt, const MergeUnitSide& oursSide,
                        const MergeUnitSide& theirsSide, MergeUnitState& merged,
                        std::vector<MergeUnitNote>& notes) const
{
    if (!baseAt.count(Names))
        return inherited::mergeUnit(baseAt, oursSide, theirsSide, merged, notes);
    // The object's own look, which a list of colours and no more is read
    // over: its view provider's, as the document has it. Ours', that is,
    // for theirs' colours too -- it is ours' object they are merged onto.
    App::MaterialAppearance object;
    bool known = false;
    if (auto view = App::Document::viewOf(this)) {
        if (auto appearance = dynamic_cast<App::PropertyAppearanceList*>(
                    view->getPropertyByName("ShapeAppearance"))) {
            object = appearance->getBase();
            known = true;
        }
    }
    Paint base, ours, theirs;
    if (!base.read(baseAt, known ? &object : nullptr)
            || !ours.read(oursSide.at, known ? &object : nullptr)
            || !theirs.read(theirsSide.at, known ? &object : nullptr))
        return false;
    // One side's a list of colours and the other's of whole looks, and no
    // object's look to read the colours over.
    if (!known && !ours.at.empty() && !theirs.at.empty()
            && ours.coloursOnly != theirs.coloursOnly)
        return false;

    const std::string rule = facePaint();
    auto note = [&](const Painted& p, const char* change, const char* side) {
        Note n;
        n.prop = Names;
        n.key = p.shown;
        n.change = change;
        n.side = side;
        notes.push_back(std::move(n));
    };
    // What both changed, and differently: by the setting. False where it
    // says to ask.
    enum class Keep { Ours, Theirs, Ask };
    auto ruled = [&]() {
        return rule == "theirs" ? Keep::Theirs : rule == "asked" ? Keep::Ask : Keep::Ours;
    };

    std::vector<Painted> out;
    // Ours' order, then what theirs added.
    for (const auto& key : ours.order) {
        const Painted& o = ours.at.at(key);
        auto b = base.at.find(key);
        auto t = theirs.at.find(key);
        if (t == theirs.at.end()) {
            if (b == base.at.end()) {
                out.push_back(o);   // ours added it
                continue;
            }
            // Theirs took the name away.
            if (sameLook(o.look, b->second.look)) {
                note(o, "removed", "theirs");
                continue;
            }
            switch (ruled()) {   // ours changed it, theirs removed it
            case Keep::Ask:
                return false;
            case Keep::Theirs:
                note(o, "ruled", "theirs");
                break;
            case Keep::Ours:
                note(o, "ruled", "ours");
                out.push_back(o);
                break;
            }
            continue;
        }
        if (sameLook(o.look, t->second.look)) {
            out.push_back(o);
            continue;
        }
        const bool oursChanged = b == base.at.end() || !sameLook(o.look, b->second.look);
        const bool theirsChanged = b == base.at.end() || !sameLook(t->second.look, b->second.look);
        Painted taken = o;
        taken.look = t->second.look;
        if (!oursChanged) {
            note(o, "changed", "theirs");
            out.push_back(std::move(taken));
        }
        else if (!theirsChanged) {
            out.push_back(o);
        }
        else {
            switch (ruled()) {
            case Keep::Ask:
                return false;
            case Keep::Theirs:
                note(o, "ruled", "theirs");
                out.push_back(std::move(taken));
                break;
            case Keep::Ours:
                note(o, "ruled", "ours");
                out.push_back(o);
                break;
            }
        }
    }
    for (const auto& key : theirs.order) {
        if (ours.at.count(key))
            continue;
        const Painted& t = theirs.at.at(key);
        auto b = base.at.find(key);
        if (b == base.at.end()) {
            note(t, "added", "theirs");
            out.push_back(t);
            continue;
        }
        // Ours took the name away.
        if (sameLook(t.look, b->second.look))
            continue;
        switch (ruled()) {   // theirs changed it, ours removed it
        case Keep::Ask:
            return false;
        case Keep::Theirs:
            note(t, "ruled", "theirs");
            out.push_back(t);
            break;
        case Keep::Ours:
            note(t, "ruled", "ours");
            break;
        }
    }

    // The names. Ours' own text where nothing of it changed, so that what
    // is the same is seen to be.
    bool asOurs = out.size() == ours.order.size();
    for (std::size_t i = 0; asOurs && i < out.size(); ++i)
        asOurs = out[i].tag == ours.at.at(ours.order[i]).tag;
    if (asOurs) {
        merged[Names] = oursSide.at.at(Names);
    }
    else {
        const std::string target = out.empty() ? std::string()
                                 : !ours.target.empty() ? ours.target : theirs.target;
        std::string text = "<LinkSub value=\"" + target + "\" count=\""
                         + std::to_string(out.size()) + "\">\n";
        for (const auto& p : out)
            text += "    " + p.tag + "\n";
        text += "</LinkSub>\n";
        merged[Names] = std::move(text);
    }

    // The looks, in that order.
    // Colours and no more where neither side has given a look more than a
    // colour: the merged faces go on taking the object's finish.
    App::AppearanceList list;
    if (!out.empty()) {
        const bool coloursOnly = (ours.at.empty() || ours.coloursOnly)
                              && (theirs.at.empty() || theirs.coloursOnly);
        const int count = static_cast<int>(out.size());
        if (coloursOnly) {
            list.setSize(count);
            for (int i = 0; i < count; ++i)
                list.setDiffuseColor(i, App::AppearanceList::storedDiffuse(out[i].look));
        }
        else {
            list.setPBR(ours.at.empty() ? theirs.pbr : ours.pbr);
            list.setSize(count, out.front().look);
            for (int i = 0; i < count; ++i)
                list.set1Value(i, out[i].look);
            list.setFollowMaterial(false);
        }
    }
    if (asOurs && ours.list.isSame(list)) {
        merged[Looks] = oursSide.at.at(Looks);
        return true;
    }
    auto doc = getDocument();
    if (!doc)
        return false;
    App::PropertyAppearanceList held;
    held.setList(list);
    App::CaptureConfig config(*doc);
    config.forceXML = true;
    const App::CapturedValue value = App::captureValue(config, held);
    if (!value.ok || !value.attachments.empty())
        return false;
    merged[Looks] = value.fragment;
    return true;
}
