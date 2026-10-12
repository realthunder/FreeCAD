// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

// A sketch two branches both changed, merged by what it holds
// (docs/TransactionLog.md sec 31.10): geometry by its id, constraints by
// what they say of which geometry, and the solver as the judge of the
// result.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Standard_Failure.hxx>

#include <App/Document.h>
#include <App/TransactionValue.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Writer.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/PropertyGeometryList.h>

#include "Constraint.h"
#include "ExternalGeometryFacade.h"
#include "GeoEnum.h"
#include "GeometryFacade.h"
#include "PropertyConstraintList.h"
#include "Sketch.h"
#include "SketchObject.h"

FC_LOG_LEVEL_INIT("Sketch", true, true)

using namespace Sketcher;

namespace
{

using State = App::DocumentObject::MergeUnitState;
using UnitSide = App::DocumentObject::MergeUnitSide;
using Note = App::DocumentObject::MergeUnitNote;

/// Two places a solver left one point are one place: what the kernel calls
/// one point. Two solves of one fully constrained sketch end 1e-14 apart
/// (sec 31.10); nothing a hand moved is this near where it was.
constexpr double SamePlace = 1e-7;

/** Two saved texts, the same but for numbers no further apart than
 * `SamePlace`: a geometry as it was and as a solve of something else left
 * it. Anything that is not a number -- a type, a flag, an id -- is compared
 * as it stands.
 */
bool sameSaved(const std::string& a, const std::string& b)
{
    if (a == b)
        return true;
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const std::size_t ia = a.find('"', i);
        const std::size_t jb = b.find('"', j);
        if (ia == std::string::npos || jb == std::string::npos)
            return a.compare(i, std::string::npos, b, j, std::string::npos) == 0;
        if (a.compare(i, ia - i, b, j, jb - j) != 0)
            return false;
        const std::size_t ea = a.find('"', ia + 1);
        const std::size_t eb = b.find('"', jb + 1);
        if (ea == std::string::npos || eb == std::string::npos)
            return false;
        const std::string va = a.substr(ia + 1, ea - ia - 1);
        const std::string vb = b.substr(jb + 1, eb - jb - 1);
        if (va != vb) {
            char* enda = nullptr;
            char* endb = nullptr;
            const double da = std::strtod(va.c_str(), &enda);
            const double db = std::strtod(vb.c_str(), &endb);
            if (va.empty() || vb.empty() || *enda || *endb)
                return false;
            if (std::fabs(da - db) > SamePlace * std::max(1.0, std::fabs(da)))
                return false;
        }
        i = ea + 1;
        j = eb + 1;
    }
    return i >= a.size() && j >= b.size();
}

bool readSaved(App::Property& prop, const State& at, const char* name)
{
    auto it = at.find(name);
    if (it == at.end())
        return false;
    try {
        App::CapturedValue value;
        value.fragment = it->second;
        value.ok = true;
        App::restoreValue(prop, value);
        return true;
    }
    catch (const Base::Exception&) {
        return false;
    }
    catch (const std::exception&) {
        return false;
    }
}

/// One state of a sketch, read into what it holds.
struct Lists
{
    Part::PropertyGeometryList geo;
    Part::PropertyGeometryList ext;
    PropertyConstraintList cons;
    std::string links;

    /// A geometry's id to its place, and what it is saved as.
    std::map<long, int> geoAt;
    std::map<long, int> extAt;
    std::map<long, std::string> geoText;
    /// External geometry by id: what it is of, with its flags. Where it
    /// lies is its source's doing, and a recompute's.
    std::map<long, std::string> extText;
    /// The references external geometry is made from, each line as saved
    /// and known by its object and sub-element.
    std::string linksHead;
    std::vector<std::pair<std::string, std::string>> linkLines;
    /// A constraint's key and its content with the places out, by place.
    std::vector<std::string> conKey;
    std::vector<std::string> conText;
    std::map<std::string, int> conAt;

    bool read(const State& at)
    {
        try {
            return readAll(at);
        }
        catch (const Base::Exception&) {
            return false;   // geometry that is not a sketch's
        }
    }

    bool readAll(const State& at)
    {
        if (!readSaved(geo, at, "Geometry") || !readSaved(ext, at, "ExternalGeo")
                || !readSaved(cons, at, "Constraints"))
            return false;
        auto l = at.find("ExternalGeometry");
        if (l != at.end()) {
            links = l->second;
            if (!splitLinks())
                return false;
        }
        const auto& geos = geo.getValues();
        for (int i = 0; i < static_cast<int>(geos.size()); ++i) {
            const long id = GeometryFacade::getId(geos[i]);
            if (!geoAt.emplace(id, i).second)
                return false;   // an id twice: not geometry known one by one
            Base::StringWriter writer;
            geos[i]->Save(writer);
            geoText[id] = std::string(geos[i]->getTypeId().getName()) + writer.getString();
        }
        const auto& exts = ext.getValues();
        for (int i = 0; i < static_cast<int>(exts.size()); ++i) {
            const long id = GeometryFacade::getId(exts[i]);
            if (!extAt.emplace(id, i).second)
                return false;
            auto facade = ExternalGeometryFacade::getFacade(exts[i]);
            extText[id] = facade->getRef() + " " + std::to_string(facade->getFlags());
        }
        std::map<std::string, int> seen;
        const auto& list = cons.getValuesForce();
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            std::string key;
            if (!keyOf(*list[i], key))
                return false;
            const int nth = ++seen[key];
            if (nth > 1)
                key += "#" + std::to_string(nth);
            conKey.push_back(key);
            conAt[key] = i;
            // What it says with the places out: they are in the key, and
            // they move when a line before them goes. A reference
            // constraint's value is the solver's.
            std::unique_ptr<Constraint> plain(list[i]->clone());
            for (std::size_t e = 0; e < plain->getElementsSize(); ++e)
                plain->setGeoId(e, 0);
            if (!plain->isDriving)
                plain->setValue(0.0);
            Base::StringWriter writer;
            plain->Save(writer);
            conText.push_back(writer.getString());
        }
        return true;
    }

    /// `<LinkSubList count="2">`, then a line for each link, then the end.
    bool splitLinks()
    {
        std::size_t at = 0;
        while (at < links.size()) {
            std::size_t end = links.find('\n', at);
            end = end == std::string::npos ? links.size() : end + 1;
            const std::string line = links.substr(at, end - at);
            at = end;
            if (line.find("<LinkSubList") != std::string::npos) {
                linksHead = line;
                continue;
            }
            if (line.find("<Link ") == std::string::npos)
                continue;
            auto value = [&](const char* name) {
                const std::string open = std::string(name) + "=\"";
                const std::size_t a = line.find(open);
                const std::size_t b = a == std::string::npos
                    ? a : line.find('"', a + open.size());
                return b == std::string::npos ? std::string()
                                              : line.substr(a, b + 1 - a);
            };
            const std::string key = value("obj") + " " + value("sub");
            if (key.size() < 3)
                return false;
            for (const auto& had : linkLines) {
                if (had.first == key)
                    return false;   // one reference twice: not known one by one
            }
            linkLines.emplace_back(key, line);
        }
        return !linksHead.empty() || linkLines.empty();
    }

    bool hasLink(const std::string& key) const
    {
        for (const auto& l : linkLines) {
            if (l.first == key)
                return true;
        }
        return false;
    }

    /// What a constraint is known by (sec 31.3 P2 a): its type and the
    /// geometry it is on, each by its id, with the point of it.
    bool keyOf(const Constraint& c, std::string& key) const
    {
        key = c.typeToString();
        if (c.Type == InternalAlignment) {
            key += ":" + c.internalAlignmentTypeToString() + ":"
                + std::to_string(c.InternalAlignmentIndex);
        }
        const auto& geos = geo.getValues();
        const auto& exts = ext.getValues();
        for (std::size_t e = 0; e < c.getElementsSize(); ++e) {
            const int at = c.getGeoId(e);
            key += ",";
            if (at == GeoEnum::GeoUndef)
                continue;
            if (at >= 0) {
                if (at >= static_cast<int>(geos.size()))
                    return false;
                key += "g" + std::to_string(GeometryFacade::getId(geos[at]));
            }
            else {
                if (-at - 1 >= static_cast<int>(exts.size()))
                    return false;
                key += "e" + std::to_string(GeometryFacade::getId(exts[-at - 1]));
            }
            key += "." + std::to_string(c.getPosIdAsInt(e));
        }
        return true;
    }
};

/// When a side last wrote each thing of the sketch: the newest of its rows
/// that left a geometry somewhere else, or a constraint saying something
/// else. `G<id>` and `C<key>`.
std::map<std::string, double> writtenBy(const State& base, const UnitSide& side)
{
    std::map<std::string, double> at;
    if (!side.rows)
        return at;
    auto was = std::make_unique<Lists>();
    if (!was->read(base))
        return at;
    for (const auto& row : side.rows()) {
        auto is = std::make_unique<Lists>();
        if (!is->read(row.second))
            continue;
        auto wrote = [&](const std::string& key) {
            double& t = at[key];
            t = std::max(t, row.first);
        };
        for (const auto& g : is->geoText) {
            auto old = was->geoText.find(g.first);
            if (old == was->geoText.end() || !sameSaved(old->second, g.second))
                wrote("G" + std::to_string(g.first));
        }
        for (const auto& g : was->geoText) {
            if (!is->geoText.count(g.first))
                wrote("G" + std::to_string(g.first));
        }
        for (const auto& c : is->conAt) {
            auto old = was->conAt.find(c.first);
            if (old == was->conAt.end() || was->conText[old->second] != is->conText[c.second])
                wrote("C" + c.first);
        }
        for (const auto& c : was->conAt) {
            if (!is->conAt.count(c.first))
                wrote("C" + c.first);
        }
        was = std::move(is);
    }
    return at;
}

}  // namespace

bool SketchObject::mergeUnit(const MergeUnitState& baseAt, const MergeUnitSide& oursSide,
                             const MergeUnitSide& theirsSide, MergeUnitState& merged,
                             std::vector<MergeUnitNote>& notes) const
{
    // Another unit of this object's: the faces painted by name, which are
    // any shape's (Part::Feature).
    if (!baseAt.count("Geometry") && !baseAt.count("Constraints"))
        return Part::Part2DObject::mergeUnit(baseAt, oursSide, theirsSide, merged, notes);

    Lists base, ours, theirs;
    if (!base.read(baseAt) || !ours.read(oursSide.at) || !theirs.read(theirsSide.at))
        return false;

    // Who wrote a thing last, asked only where both wrote it.
    std::map<std::string, double> oursAt, theirsAt;
    bool timed = false;
    auto theirsLast = [&](const std::string& key) {
        if (!timed) {
            oursAt = writtenBy(baseAt, oursSide);
            theirsAt = writtenBy(baseAt, theirsSide);
            timed = true;
        }
        auto o = oursAt.find(key);
        auto t = theirsAt.find(key);
        return t != theirsAt.end() && (o == oursAt.end() || t->second > o->second);
    };
    auto note = [&](const char* prop, const std::string& key, const char* change,
                    const char* side, bool byTime) {
        notes.push_back({prop, key, change, side, byTime});
    };

    // External geometry, by id, and the references it is made from, each
    // by its object and sub-element: what one side added is there, what
    // one side removed is gone. Where one lies is its source's doing and a
    // recompute's, so of one both have, ours' is as good -- but for what
    // it is of and its flags, which are theirs' where only theirs changed
    // them. Ours' order, then what theirs added.
    std::vector<const Part::Geometry*> exts;
    std::map<long, int> extPlace;
    bool theirsExt = false;
    for (const Part::Geometry* g : ours.ext.getValues()) {
        const long id = GeometryFacade::getId(g);
        const bool inBase = base.extText.count(id) != 0;
        const bool inTheirs = theirs.extText.count(id) != 0;
        if (!inBase && inTheirs)
            return false;   // one id, two geometries: a copy's (sec 31.3 P6)
        if (inBase && !inTheirs) {
            note("ExternalGeo", "e" + std::to_string(id), "removed", "theirs", false);
            theirsExt = true;
            continue;
        }
        const bool theirsOnly = inBase && inTheirs && base.extText.at(id) == ours.extText.at(id)
            && base.extText.at(id) != theirs.extText.at(id);
        extPlace[id] = static_cast<int>(exts.size());
        exts.push_back(theirsOnly ? theirs.ext.getValues()[theirs.extAt.at(id)] : g);
        if (theirsOnly) {
            note("ExternalGeo", "e" + std::to_string(id), "changed", "theirs", false);
            theirsExt = true;
        }
    }
    for (const Part::Geometry* g : theirs.ext.getValues()) {
        const long id = GeometryFacade::getId(g);
        if (ours.extText.count(id) || base.extText.count(id))
            continue;
        note("ExternalGeo", "e" + std::to_string(id), "added", "theirs", false);
        theirsExt = true;
        extPlace[id] = static_cast<int>(exts.size());
        exts.push_back(g);
    }
    std::string links = ours.linksHead;
    {
        std::vector<std::string> lines;
        for (const auto& l : ours.linkLines) {
            if (base.hasLink(l.first) && !theirs.hasLink(l.first))
                continue;
            lines.push_back(l.second);
        }
        for (const auto& l : theirs.linkLines) {
            if (!ours.hasLink(l.first) && !base.hasLink(l.first))
                lines.push_back(l.second);
        }
        const std::size_t count = links.find("count=\"");
        const std::size_t close =
            count == std::string::npos ? count : links.find('"', count + 7);
        if (close == std::string::npos) {
            if (!lines.empty())
                return false;
            links = ours.links;
        }
        else {
            links.replace(count + 7, close - count - 7, std::to_string(lines.size()));
            for (const auto& line : lines)
                links += line;
            const std::size_t tail = ours.links.rfind("</LinkSubList>");
            const std::size_t lead = tail == std::string::npos ? tail
                                                               : ours.links.rfind('\n', tail);
            links += tail == std::string::npos
                ? std::string("</LinkSubList>\n")
                : ours.links.substr(lead == std::string::npos ? tail : lead + 1);
        }
    }

    // Geometry, by id. What one side alone moved, added or removed is that
    // side's; what both moved, to two places, is where the one that moved
    // it last left it; what one side removed is gone, whatever the other
    // did to it. Ours' order, then what theirs added.
    std::vector<const Part::Geometry*> geos;
    std::map<long, int> geoAt;
    auto put = [&](const Lists& from, long id) {
        geoAt[id] = static_cast<int>(geos.size());
        geos.push_back(from.geo.getValues()[from.geoAt.at(id)]);
    };
    for (const Part::Geometry* g : ours.geo.getValues()) {
        const long id = GeometryFacade::getId(g);
        const std::string gid = "g" + std::to_string(id);
        const std::string& mine = ours.geoText.at(id);
        auto b = base.geoText.find(id);
        auto t = theirs.geoText.find(id);
        if (b == base.geoText.end()) {
            if (t != theirs.geoText.end())
                return false;   // one id, two geometries: a copy's (sec 31.3 P6)
            put(ours, id);
            continue;
        }
        if (t == theirs.geoText.end()) {
            note("Geometry", gid, "removed", "theirs", false);
            continue;
        }
        const bool oursMoved = !sameSaved(b->second, mine);
        const bool theirsMoved = !sameSaved(b->second, t->second);
        if (!theirsMoved || sameSaved(mine, t->second)) {
            put(ours, id);
            continue;
        }
        const bool theirsWins = !oursMoved || theirsLast("G" + std::to_string(id));
        note("Geometry", gid, "changed", theirsWins ? "theirs" : "ours", oursMoved);
        put(theirsWins ? theirs : ours, id);
    }
    for (const Part::Geometry* g : theirs.geo.getValues()) {
        const long id = GeometryFacade::getId(g);
        if (ours.geoText.count(id) || base.geoText.count(id))
            continue;   // ours has it, or ours removed it
        note("Geometry", "g" + std::to_string(id), "added", "theirs", false);
        put(theirs, id);
    }

    // Constraints, by what they say of which geometry. One both sides
    // changed, or one removed and the other changed, is the later's.
    struct Kept
    {
        const Lists* from;
        int at;
        std::string key;
    };
    std::vector<Kept> kept;
    for (int i = 0; i < static_cast<int>(ours.conKey.size()); ++i) {
        const std::string& key = ours.conKey[i];
        auto b = base.conAt.find(key);
        auto t = theirs.conAt.find(key);
        const bool inBase = b != base.conAt.end();
        const bool inTheirs = t != theirs.conAt.end();
        const bool oursChanged = !inBase || base.conText[b->second] != ours.conText[i];
        const bool theirsChanged = inBase != inTheirs
            || (inTheirs && theirs.conText[t->second] != base.conText[b->second]);
        if (!theirsChanged || (inTheirs && theirs.conText[t->second] == ours.conText[i])) {
            kept.push_back({&ours, i, key});
            continue;
        }
        const char* change = inTheirs ? (inBase ? "changed" : "added") : "removed";
        const bool theirsWins = !oursChanged || theirsLast("C" + key);
        note("Constraints", key, change, theirsWins ? "theirs" : "ours", oursChanged);
        if (!theirsWins)
            kept.push_back({&ours, i, key});
        else if (inTheirs)
            kept.push_back({&theirs, t->second, key});
    }
    for (int i = 0; i < static_cast<int>(theirs.conKey.size()); ++i) {
        const std::string& key = theirs.conKey[i];
        if (ours.conAt.count(key))
            continue;
        auto b = base.conAt.find(key);
        if (b == base.conAt.end()) {
            note("Constraints", key, "added", "theirs", false);
            kept.push_back({&theirs, i, key});
        }
        else if (base.conText[b->second] != theirs.conText[i]) {
            // Ours removed what theirs changed.
            const bool theirsWins = theirsLast("C" + key);
            note("Constraints", key, "changed", theirsWins ? "theirs" : "ours", true);
            if (theirsWins)
                kept.push_back({&theirs, i, key});
        }
    }

    // Each back on the place its geometry has now. One on geometry that is
    // gone goes with it (a ruling: the removal stands, and nothing is
    // brought back for a constraint's sake).
    std::vector<std::unique_ptr<Constraint>> owned;
    std::vector<Constraint*> list;
    for (const Kept& k : kept) {
        const Constraint* was = k.from->cons.getValuesForce()[k.at];
        std::unique_ptr<Constraint> c(was->clone());
        bool gone = false;
        for (std::size_t e = 0; e < c->getElementsSize() && !gone; ++e) {
            const int at = was->getGeoId(e);
            if (at == GeoEnum::GeoUndef)
                continue;
            if (at >= 0) {
                auto to = geoAt.find(GeometryFacade::getId(k.from->geo.getValues()[at]));
                if (to == geoAt.end())
                    gone = true;
                else
                    c->setGeoId(e, to->second);
            }
            else {
                auto to = extPlace.find(GeometryFacade::getId(k.from->ext.getValues()[-at - 1]));
                if (to == extPlace.end())
                    gone = true;
                else
                    c->setGeoId(e, -to->second - 1);
            }
        }
        if (gone) {
            // Said once: not as added, or changed, and then as left out.
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [&](const Note& n) {
                                           return n.prop == "Constraints" && n.key == k.key;
                                       }),
                        notes.end());
            note("Constraints", k.key, "dropped", k.from == &theirs ? "theirs" : "ours", false);
            continue;
        }
        list.push_back(c.get());
        owned.push_back(std::move(c));
    }

    // The solver judges it, as a recompute would: a sketch that does not
    // solve is not written; the whole of it is asked instead.
    try {
        std::vector<const Part::Geometry*> all(geos);
        all.insert(all.end(), exts.rbegin(), exts.rend());
        Sketch solver;
        const int dofs = solver.setUpSketch(all, list, static_cast<int>(exts.size()));
        if (dofs < 0 || solver.hasConflicts() || solver.hasRedundancies()
                || solver.hasMalformedConstraints() || solver.solve() != GCS::SolveStatus::Success) {
            FC_LOG(getFullName() << ": merged by its geometry and constraints it does not solve");
            return false;
        }
    }
    catch (const Base::Exception&) {
        return false;
    }
    catch (const Standard_Failure&) {
        return false;
    }

    App::Document* doc = getDocument();
    if (!doc)
        return false;
    Part::PropertyGeometryList geoOut;
    geoOut.setValues(geos);
    PropertyConstraintList consOut;
    consOut.setValues(list);
    const App::CapturedValue geoSaved = App::captureValue(*doc, geoOut);
    const App::CapturedValue consSaved = App::captureValue(*doc, consOut);
    if (!geoSaved.ok || !consSaved.ok || !geoSaved.attachments.empty()
            || !consSaved.attachments.empty())
        return false;
    merged = oursSide.at;
    merged["Geometry"] = geoSaved.fragment;
    merged["Constraints"] = consSaved.fragment;
    if (theirsExt) {
        Part::PropertyGeometryList extOut;
        extOut.setValues(exts);
        const App::CapturedValue extSaved = App::captureValue(*doc, extOut);
        if (!extSaved.ok || !extSaved.attachments.empty())
            return false;
        merged["ExternalGeo"] = extSaved.fragment;
    }
    if (merged.count("ExternalGeometry") && links != ours.links)
        merged["ExternalGeometry"] = links;
    return true;
}

bool SketchObject::getMergePlaces(const MergeUnitState& at, std::string& prop,
                                  std::vector<MergePlace>& places) const
{
    Lists lists;
    if (!lists.read(at))
        return false;
    prop = "Constraints";
    places.clear();
    const auto& list = lists.cons.getValuesForce();
    for (std::size_t i = 0; i < list.size(); ++i) {
        // `DistanceX,g4.1,g4.2,` is `DistanceX_g4p1_g4p2`: a name an
        // expression can say, and the same on any branch that has the
        // constraint.
        std::string made;
        for (char c : lists.conKey[i]) {
            if (c == ',' || c == ':' || c == '#') {
                if (!made.empty() && made.back() != '_')
                    made += '_';
            }
            else if (c == '.') {
                made += 'p';
            }
            else if (c == '-') {
                made += 'n';
            }
            else {
                made += c;
            }
        }
        while (!made.empty() && made.back() == '_')
            made.pop_back();
        places.push_back({lists.conKey[i], list[i]->Name, made});
    }
    return true;
}

bool SketchObject::nameMergePlaces(MergeUnitState& at, const std::vector<std::string>& names) const
{
    App::Document* doc = getDocument();
    PropertyConstraintList cons;
    if (!doc || !readSaved(cons, at, "Constraints"))
        return false;
    const auto& list = cons.getValuesForce();
    if (list.size() != names.size())
        return false;
    std::vector<std::unique_ptr<Constraint>> owned;
    std::vector<Constraint*> named;
    for (std::size_t i = 0; i < list.size(); ++i) {
        owned.emplace_back(list[i]->clone());
        owned.back()->Name = names[i];
        named.push_back(owned.back().get());
    }
    PropertyConstraintList out;
    out.setValues(named);
    const App::CapturedValue saved = App::captureValue(*doc, out);
    if (!saved.ok || !saved.attachments.empty())
        return false;
    at["Constraints"] = saved.fragment;
    return true;
}

// Another copy of the file numbers its new geometry on from the counter
// this file numbers its own from (docs/TransactionLog.md sec 31.14): the
// fifth line is `g5` in each, and they are two lines. What an import reads
// of the copy's says this file's ids.

namespace
{

/// The value of ` id="..."` in the opening tag that starts at `tag` of
/// `text` and ends before `end`: where its digits are, or npos.
std::size_t idDigits(const std::string& text, std::size_t tag, std::size_t end, std::size_t& size)
{
    static const std::string attr = " id=\"";
    const std::size_t at = text.find(attr, tag);
    if (at == std::string::npos || at >= end)
        return std::string::npos;
    const std::size_t from = at + attr.size();
    const std::size_t to = text.find('"', from);
    if (to == std::string::npos || to > end)
        return std::string::npos;
    size = to - from;
    return from;
}

/// Each id a saved list of geometry says, in the order it says them. A
/// geometry says its id twice: in its own tag, and in the tag of the
/// sketch's extension of it. The axes, which are nobody's to number, are
/// below one and left out. `each(tag, end, digits, size, id)`: the tag
/// from `tag` to `end`, the id's digits at `digits`, `size` of them.
template<typename Each>
void eachSavedId(const std::string& fragment, Each each)
{
    static const std::string geometry = "<Geometry ";
    static const std::string extension = "<GeoExtension type=\"Sketcher::SketchGeometryExtension\"";
    std::size_t at = 0;
    while (true) {
        const std::size_t g = fragment.find(geometry, at);
        const std::size_t e = fragment.find(extension, at);
        const std::size_t tag = std::min(g, e);
        if (tag == std::string::npos)
            break;
        const std::size_t end = fragment.find('>', tag);
        if (end == std::string::npos)
            break;
        at = end;
        std::size_t size = 0;
        const std::size_t digits = idDigits(fragment, tag, end, size);
        if (digits == std::string::npos)
            continue;
        char* stop = nullptr;
        const std::string said = fragment.substr(digits, size);
        const long id = std::strtol(said.c_str(), &stop, 10);
        if (said.empty() || *stop || id <= 0)
            continue;
        each(tag, end, digits, size, id);
    }
}

}  // namespace

bool SketchObject::importMintedIds(const char* prop, std::string& fragment,
                                   std::map<long, long>& ids, bool seed)
{
    if (seed) {
        // Not a number the map says is another of the copy's (sec 31.15):
        // a copy that took this geometry from this file holds it under a
        // number of its own, and its number of this one is another thing.
        std::set<long> given;
        for (const auto& kv : ids)
            given.insert(kv.second);
        for (const auto* list : {&Geometry.getValues(), &ExternalGeo.getValues()}) {
            for (const Part::Geometry* g : *list) {
                const long id = GeometryFacade::getId(g);
                if (id > 0 && !given.count(id))
                    ids.emplace(id, id);
            }
        }
    }
    if (!prop || (strcmp(prop, "Geometry") != 0 && strcmp(prop, "ExternalGeo") != 0))
        return false;
    // Both ids of a geometry by the same map.
    // A number this file has not given a thing of this sketch stays the
    // number it is -- all of them, for a sketch the copy made; one it has
    // given is another thing here, and the copy's gets the next.
    auto mint = [this](long id) {
        App::Document* doc = getDocument();
        const long floor = std::max(geoLastId, id - 1);
        geoLastId = doc ? doc->nextGeoId(*this, floor) : floor + 1;
        return geoLastId;
    };
    std::string out;
    out.reserve(fragment.size() + 16);
    std::size_t last = 0;
    eachSavedId(fragment,
                [&](std::size_t, std::size_t, std::size_t digits, std::size_t size, long id) {
                    auto known = ids.find(id);
                    if (known == ids.end())
                        known = ids.emplace(id, mint(id)).first;
                    out.append(fragment, last, digits - last);
                    out += std::to_string(known->second);
                    last = digits + size;
                });
    out.append(fragment, last, std::string::npos);
    fragment = std::move(out);
    return true;
}

bool SketchObject::pairMintedIds(const char* prop, const std::string& theirs,
                                 const std::string& ours, std::map<long, long>& ids) const
{
    if (!prop || (strcmp(prop, "Geometry") != 0 && strcmp(prop, "ExternalGeo") != 0))
        return false;
    // A row replayed puts the list back as it was saved: the same things
    // in the same order, each under the number its file has for it. So
    // the two are read side by side -- and are not one list where they
    // differ in how many they hold or in what kind of thing holds a place.
    auto said = [](const std::string& fragment) {
        std::vector<std::pair<std::string, long>> out;
        eachSavedId(fragment,
                    [&](std::size_t tag, std::size_t end, std::size_t, std::size_t, long id) {
                        static const std::string type = " type=\"";
                        std::string kind;
                        const std::size_t at = fragment.find(type, tag);
                        if (at != std::string::npos && at < end) {
                            const std::size_t from = at + type.size();
                            const std::size_t to = fragment.find('"', from);
                            if (to != std::string::npos && to < end)
                                kind = fragment.substr(from, to - from);
                        }
                        out.emplace_back(std::move(kind), id);
                    });
        return out;
    };
    const auto there = said(theirs);
    const auto here = said(ours);
    if (there.size() != here.size())
        return false;
    std::map<long, long> found;
    std::map<long, long> back;
    for (std::size_t i = 0; i < there.size(); ++i) {
        if (there[i].first != here[i].first)
            return false;
        if (found.emplace(there[i].second, here[i].second).first->second != here[i].second
                || back.emplace(here[i].second, there[i].second).first->second != there[i].second)
            return false;
    }
    for (const auto& kv : found)
        ids.emplace(kv.first, kv.second);
    return true;
}

bool SketchObject::importMintedName(std::string& name, const std::map<long, long>& ids) const
{
    // `g5`, `g5v1`, `e7`, with or without what follows it: `;SKT`.
    if (name.size() < 2 || (name[0] != 'g' && name[0] != 'e'))
        return false;
    std::size_t end = 1;
    while (end < name.size() && name[end] >= '0' && name[end] <= '9')
        ++end;
    if (end == 1)
        return false;
    std::size_t rest = end;
    if (rest < name.size() && name[rest] == 'v') {
        ++rest;
        const std::size_t point = rest;
        while (rest < name.size() && name[rest] >= '0' && name[rest] <= '9')
            ++rest;
        if (rest == point)
            return false;
    }
    if (rest < name.size() && name[rest] != ';')
        return false;
    auto to = ids.find(std::strtol(name.substr(1, end - 1).c_str(), nullptr, 10));
    if (to == ids.end() || to->first == to->second)
        return false;
    name.replace(1, end - 1, std::to_string(to->second));
    return true;
}
