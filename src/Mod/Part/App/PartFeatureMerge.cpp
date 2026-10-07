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

// The looks of a shape's elements, merged by what they are given to
// (docs/ShapeAppearanceDesign.md sec 14.6.5, docs/TransactionLog.md sec
// 31.20). ElementAppearance is one value: the names with a look each and
// which of it is the name's own, the looks given by number, and the own look
// of the faces, the edges and the vertices. Where two branches both wrote it
// the names are merged by name -- by the mapped name where there is one,
// which does not depend on how many faces come before -- the numbers by
// number, and the own looks each as a value.
//
// A branch's value is its saved text and there is no object to look its
// names up in, so they are kept as the text they are; the looks are read
// into a property that is on no object. What is drawn is left out: it is
// made of the rest, and made again when the merged value lands.

#include "PreCompiled.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyElementAppearance.h>
#include <App/PropertyStandard.h>
#include <App/TransactionValue.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>

#include "PartFeature.h"

using namespace Part;

namespace
{

const char* const Looks = "ElementAppearance";
const std::string EndOfNames("</LinkSub>");

using Store = App::PropertyElementAppearance;
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

/// One element given a look by name.
struct Painted
{
    std::string tag;    ///< `<Sub .../>`, as saved
    std::string shown;  ///< the name a reader knows it by
    App::MaterialAppearance look;
    uint16_t own {Store::OwnAll};  ///< which of the look is the name's
};

/// Whether two say the same of an element: the same fields, the same in
/// each. What a name does not state is the object's, whatever is stored.
bool sameStated(const Painted& a, const Painted& b)
{
    return a.own == b.own && (Store::differingFields(a.look, b.look) & a.own) == 0;
}

/// A branch's value at one moment.
struct Paint
{
    std::string target;
    std::vector<std::string> order;
    std::map<std::string, Painted> at;
    /// The looks, in a property on no object
    Store value;

    bool read(const State& state)
    {
        auto found = state.find(Looks);
        if (found == state.end())
            return false;
        const std::string& text = found->second;
        const std::size_t end = text.find(EndOfNames);
        const std::size_t head = text.find("<LinkSub ");
        if (end == std::string::npos || head == std::string::npos || head > end)
            return false;
        const std::string names = text.substr(0, end);
        target = attribute(names.substr(head, names.find('>', head) - head), "value");
        App::Property::SavedElements subs;
        if (names.find("<Sub ") != std::string::npos
                && !App::Property::savedTags(names, "Sub", "value", subs))
            return false;
        try {
            App::CapturedValue saved;
            saved.fragment = text;
            saved.ok = true;
            App::restoreValue(value, saved);
        }
        catch (const Base::Exception&) {
            return false;
        }
        catch (const std::exception&) {
            return false;
        }
        if (value.getNamedCount() != static_cast<int>(subs.size()))
            return false;
        const App::AppearanceList& looks = value.getNamedLooks();
        for (std::size_t i = 0; i < subs.size(); ++i) {
            const int pos = static_cast<int>(i);
            Painted p;
            p.tag = subs[i].second;
            p.shown = subs[i].first;
            p.own = value.getNamedOwn(pos);
            if (pos < looks.getSize())
                p.look = looks.getMaterial(pos);
            else
                p.own = Store::OwnNone;
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
/// or `asked` -- the whole of the object's looks then one question.
std::string facePaint()
{
    return App::GetApplication()
        .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
        ->GetASCII("TransactionLogMergeFacePaint", "ours");
}

enum class Keep { Ours, Theirs, Ask };

/** One list three ways: the side that changed it, or the rule where both did
 *
 * `fill` is given for the looks by number: what an element is that a list
 * says nothing of, the kind's own look -- a list nobody wrote is every
 * element that. Those are then merged an element at a time where both
 * sides wrote, so that what one side alone gave a look keeps it. `taken` is
 * set where the merge is not ours'. False where the rule says to ask.
 */
bool mergeList(const App::AppearanceList& base, const App::AppearanceList& ours,
               const App::AppearanceList& theirs, Keep rule, App::AppearanceList& out,
               bool& taken, bool& ruled, const App::MaterialAppearance* fill = nullptr)
{
    out = ours;
    if (ours.isSame(theirs) || theirs.isSame(base))
        return true;
    if (ours.isSame(base)) {
        out = theirs;
        taken = true;
        return true;
    }
    // Both wrote it.
    const int count = std::max(ours.getSize(), theirs.getSize());
    auto counts = [count](const App::AppearanceList& list) {
        return list.getSize() == 0 || list.getSize() == count;
    };
    if (fill && count > 0 && counts(base) && counts(ours) && counts(theirs)) {
        auto entry = [fill](const App::AppearanceList& list, int i) {
            return list.getSize() == 0 ? *fill : list.getMaterial(i);
        };
        if (out.getSize() == 0)
            out.setSize(count, *fill);
        for (int i = 0; i < count; ++i) {
            const App::MaterialAppearance b = entry(base, i);
            const App::MaterialAppearance o = entry(ours, i);
            const App::MaterialAppearance t = entry(theirs, i);
            const bool oursWrote = Store::differingFields(o, b) != Store::OwnNone;
            const bool theirsWrote = Store::differingFields(t, b) != Store::OwnNone;
            if (!theirsWrote || Store::differingFields(o, t) == Store::OwnNone)
                continue;
            if (oursWrote) {
                ruled = true;
                if (rule == Keep::Ask)
                    return false;
                if (rule == Keep::Ours)
                    continue;
            }
            out.set1Value(i, t);
            taken = true;
        }
        return true;
    }
    ruled = true;
    if (rule == Keep::Ask)
        return false;
    if (rule == Keep::Theirs) {
        out = theirs;
        taken = true;
    }
    return true;
}

}  // namespace

std::vector<std::string> Feature::getMergeUnit(const char* prop) const
{
    static const std::vector<std::string> looks {Looks};
    if (prop && std::strcmp(prop, Looks) == 0)
        return looks;
    return inherited::getMergeUnit(prop);
}

bool Feature::mergeUnit(const MergeUnitState& baseAt, const MergeUnitSide& oursSide,
                        const MergeUnitSide& theirsSide, MergeUnitState& merged,
                        std::vector<MergeUnitNote>& notes) const
{
    if (!baseAt.count(Looks))
        return inherited::mergeUnit(baseAt, oursSide, theirsSide, merged, notes);
    Paint base, ours, theirs;
    if (!base.read(baseAt) || !ours.read(oursSide.at) || !theirs.read(theirsSide.at))
        return false;

    const std::string setting = facePaint();
    const Keep rule = setting == "theirs" ? Keep::Theirs
                    : setting == "asked" ? Keep::Ask : Keep::Ours;
    auto note = [&](const std::string& key, const char* change, const char* side) {
        Note n;
        n.prop = Looks;
        n.key = key;
        n.change = change;
        n.side = side;
        notes.push_back(std::move(n));
    };

    // The names. Ours' order, then what theirs added.
    std::vector<Painted> out;
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
            if (sameStated(o, b->second)) {
                note(o.shown, "removed", "theirs");
                continue;
            }
            switch (rule) {   // ours changed it, theirs removed it
            case Keep::Ask:
                return false;
            case Keep::Theirs:
                note(o.shown, "ruled", "theirs");
                break;
            case Keep::Ours:
                note(o.shown, "ruled", "ours");
                out.push_back(o);
                break;
            }
            continue;
        }
        if (sameStated(o, t->second)) {
            out.push_back(o);
            continue;
        }
        const bool oursChanged = b == base.at.end() || !sameStated(o, b->second);
        const bool theirsChanged = b == base.at.end() || !sameStated(t->second, b->second);
        Painted taken = o;
        taken.look = t->second.look;
        taken.own = t->second.own;
        if (!oursChanged) {
            note(o.shown, "changed", "theirs");
            out.push_back(std::move(taken));
        }
        else if (!theirsChanged) {
            out.push_back(o);
        }
        else {
            switch (rule) {
            case Keep::Ask:
                return false;
            case Keep::Theirs:
                note(o.shown, "ruled", "theirs");
                out.push_back(std::move(taken));
                break;
            case Keep::Ours:
                note(o.shown, "ruled", "ours");
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
            note(t.shown, "added", "theirs");
            out.push_back(t);
            continue;
        }
        // Ours took the name away.
        if (sameStated(t, b->second))
            continue;
        switch (rule) {   // theirs changed it, ours removed it
        case Keep::Ask:
            return false;
        case Keep::Theirs:
            note(t.shown, "ruled", "theirs");
            out.push_back(t);
            break;
        case Keep::Ours:
            note(t.shown, "ruled", "ours");
            break;
        }
    }
    bool asOurs = out.size() == ours.order.size();
    for (std::size_t i = 0; asOurs && i < out.size(); ++i) {
        const Painted& o = ours.at.at(ours.order[i]);
        asOurs = out[i].tag == o.tag && sameStated(out[i], o);
    }

    // The own looks and the looks by number, a kind at a time.
    std::array<App::AppearanceList, Store::KindCount> own, numbered;
    for (int k = 0; k < Store::KindCount; ++k) {
        const auto kind = static_cast<Store::Kind>(k);
        bool taken = false;
        bool ruled = false;
        if (!mergeList(base.value.getBaseList(kind), ours.value.getBaseList(kind),
                       theirs.value.getBaseList(kind), rule, own[k], taken, ruled))
            return false;
        if (taken || ruled)
            note(Store::kindName(kind), ruled ? "ruled" : "changed",
                 taken ? "theirs" : "ours");
        asOurs = asOurs && !taken;
        taken = ruled = false;
        const App::MaterialAppearance fill = own[k].getSize() == 1
            ? own[k].getBase() : App::AppearanceList::defaultMaterial();
        if (!mergeList(base.value.getNumbered(kind), ours.value.getNumbered(kind),
                       theirs.value.getNumbered(kind), rule, numbered[k], taken, ruled, &fill))
            return false;
        if (taken || ruled)
            note(std::string(Store::kindName(kind)) + "*", ruled ? "ruled" : "changed",
                 taken ? "theirs" : "ours");
        asOurs = asOurs && !taken;
    }

    // Ours' own text where nothing of it changed, so that what is the same
    // is seen to be.
    if (asOurs) {
        merged[Looks] = oursSide.at.at(Looks);
        return true;
    }
    auto doc = getDocument();
    if (!doc)
        return false;
    Store made;
    try {
        for (int k = 0; k < Store::KindCount; ++k) {
            const auto kind = static_cast<Store::Kind>(k);
            if (own[k].getSize() == 1)
                made.setBaseList(kind, own[k]);
            if (numbered[k].getSize() > 0)
                made.setNumbered(kind, numbered[k]);
        }
        // On no object, with or without names: the looks alone are written
        made.setDetachedNamed(App::AppearanceList());
        if (!out.empty()) {
            const int count = static_cast<int>(out.size());
            App::AppearanceList list;
            list.setPBR(ours.at.empty() ? theirs.value.getNamedLooks().isPBR()
                                        : ours.value.getNamedLooks().isPBR());
            list.setSize(count, out.front().look);
            std::vector<uint16_t> bits;
            bits.reserve(out.size());
            for (int i = 0; i < count; ++i) {
                list.set1Value(i, out[static_cast<std::size_t>(i)].look);
                bits.push_back(out[static_cast<std::size_t>(i)].own);
            }
            list.setFollowMaterial(false);
            made.setDetachedNamed(list, std::move(bits));
        }
    }
    catch (const Base::Exception&) {
        return false;
    }
    App::CaptureConfig config(*doc);
    config.forceXML = true;
    const App::CapturedValue value = App::captureValue(config, made);
    if (!value.ok || !value.attachments.empty())
        return false;
    const std::string target = out.empty() ? std::string()
                             : !ours.target.empty() ? ours.target : theirs.target;
    std::string text = "<LinkSub value=\"" + target + "\" count=\"" + std::to_string(out.size())
                     + "\">\n";
    for (const auto& p : out)
        text += "    " + p.tag + "\n";
    text += "</LinkSub>\n";
    merged[Looks] = text + value.fragment;
    return true;
}
