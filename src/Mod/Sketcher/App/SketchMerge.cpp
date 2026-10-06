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
    /// The external geometry there is: each id with what it is of.
    std::set<std::pair<long, std::string>> extSet;
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
        if (l != at.end())
            links = l->second;
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
            extSet.emplace(id, ExternalGeometryFacade::getFacade(exts[i])->getRef());
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

    // External geometry is what the references make it, and a place in it
    // is what a constraint goes by: one side's, whole. Both having changed
    // which there is, is the whole sketch's question still.
    const bool oursExt = ours.extSet != base.extSet || ours.links != base.links;
    const bool theirsExt = theirs.extSet != base.extSet || theirs.links != base.links;
    if (oursExt && theirsExt && (ours.extSet != theirs.extSet || ours.links != theirs.links))
        return false;
    const Lists& extOf = theirsExt ? theirs : ours;
    const State& extFrom = theirsExt ? theirsSide.at : oursSide.at;

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
                auto to = extOf.extAt.find(GeometryFacade::getId(k.from->ext.getValues()[-at - 1]));
                if (to == extOf.extAt.end())
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
        const auto& exts = extOf.ext.getValues();
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
    for (const char* name : {"ExternalGeo", "ExternalGeometry"}) {
        auto it = extFrom.find(name);
        if (it != extFrom.end())
            merged[name] = it->second;
    }
    if (theirsExt)
        notes.push_back({"ExternalGeo", "external geometry", "changed", "theirs", false});
    return true;
}

bool SketchObject::getMergePlaces(const MergeUnitState& at, std::string& prop,
                                  std::vector<std::string>& names) const
{
    Lists lists;
    if (!lists.read(at))
        return false;
    prop = "Constraints";
    names.clear();
    const auto& list = lists.cons.getValuesForce();
    for (std::size_t i = 0; i < list.size(); ++i)
        names.push_back(list[i]->Name.empty() ? "?" + lists.conKey[i] : list[i]->Name);
    return true;
}
