/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


/*!
\defgroup Document Document
\ingroup APP
\brief The Base class of the FreeCAD Document

This (besides the App::Application class) is the most important class in FreeCAD.
It contains all the data of the opened, saved, or newly created FreeCAD Document.
The App::Document manages the Undo and Redo mechanism and the linking of documents.

\namespace App \class App::Document
This is besides the Application class the most important class in FreeCAD
It contains all the data of the opened, saved or newly created FreeCAD Document.
The Document manage the Undo and Redo mechanism and the linking of documents.

Note: the documents are not free objects. They are completely handled by the
App::Application. Only the Application can Open or destroy a document.

\section Exception Exception handling
As the document is the main data structure of FreeCAD we have to take a close
look at how Exceptions affect the integrity of the App::Document.

\section UndoRedo Undo Redo an Transactions
Undo Redo handling is one of the major mechanism of a document in terms of
user friendliness and speed (no one will wait for Undo too long).

\section Dependency Graph and dependency handling
The FreeCAD document handles the dependencies of its DocumentObjects with
an adjacence list. This gives the opportunity to calculate the shortest
recompute path. Also, it enables more complicated dependencies beyond trees.

@see App::Application
@see App::DocumentObject
*/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <optional>
# include <bitset>
# include <set>
# include <sstream>
# include <stack>
# include <tuple>
# include <boost/filesystem.hpp>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/bimap.hpp>
#include <boost/graph/strong_components.hpp>

#ifdef USE_OLD_DAG
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/depth_first_search.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/visitors.hpp>
#endif //USE_OLD_DAG

#include <boost/regex.hpp>
#include <algorithm>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <QMap>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCoreApplication>

#include <App/DocumentPy.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/FileInfo.h>
#include <Base/TimeInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Tools.h>
#include <Base/Uuid.h>
#include <Base/Sequencer.h>
#include <Base/Stream.h>
#include <Base/UnitsApi.h>

#include "Document.h"
#include "Actor.h"
#include "ElementNamingUtils.h"
#include "private/DocumentP.h"
#include "Application.h"
#include "AutoTransaction.h"
#include "DocumentObserver.h"
#include "DocumentObject.h"
#include "DocumentParams.h"
#include "ExpressionParser.h"
#include "ExpressionSecurityRuntime.h"
#ifdef FC_EXPR_IMAGE_HOST
#include "ExpressionGuestProxy.h"
#endif
#include "GeoFeature.h"
#include "InputStratum.h"
#include "License.h"
#include "Link.h"
#include "MergeDocuments.h"
#include "StringHasher.h"
#include "Transactions.h"
#include "TransactionMeasure.h"
#include "PropertyHistory.h"
#include "TransactionLog.h"

#include <nlohmann/json.hpp>

#ifdef _MSC_VER
#include <zipios++/zipios-config.h>
#endif
#include <config.h>
#if defined(HAVE_BACKTRACE_SYMBOLS)
# include <execinfo.h>
#endif
#include <zipios++/zipfile.h>
#include <zipios++/zipinputstream.h>
#include <zipios++/zipoutputstream.h>
#include <zipios++/meta-iostreams.h>

FC_LOG_LEVEL_INIT("App", true, 2, true)

using Base::Console;
using Base::streq;
using Base::Writer;
using namespace App;
using namespace std;
using namespace boost;
using namespace zipios;

#if FC_DEBUG
#  define FC_LOGFEATUREUPDATE
#endif

namespace fs = boost::filesystem;

namespace App {

static bool globalIsRestoring;

DocumentP::DocumentP()
{
    Hasher = new StringHasher;
#ifndef FC_DEBUG
    static std::random_device _RD;
    static std::mt19937 _RGEN(_RD());
    static std::uniform_int_distribution<> _RDIST(10, 5000);
    // Set some random offset to reduce likelihood of ID collision when
    // copying shape from other document. It is probably better to randomize
    // on each object ID.
    lastObjectId = _RDIST(_RGEN);
#else
    lastObjectId = 10;
#endif
    activeObject = nullptr;
    activeUndoTransaction = nullptr;
    iTransactionMode = 0;
    rollback = false;
    undoing = false;
    committing = false;
    opentransaction = false;
    StatusBits.set((size_t)Document::Closable, true);
    StatusBits.set((size_t)Document::KeepTrailingDigits, true);
    StatusBits.set((size_t)Document::Restoring, false);
    iUndoMode = 0;
    UndoMemSize = 0;
    UndoMaxStackSize = 20;
}


} // namespace App

PROPERTY_SOURCE(App::Document, App::PropertyContainer)

bool Document::testStatus(Status pos) const
{
    return d->StatusBits.test((size_t)pos);
}

void Document::setStatus(Status pos, bool on)
{
    d->StatusBits.set((size_t)pos, on);
}

//bool _has_cycle_dfs(const DependencyList & g, vertex_t u, default_color_type * color)
//{
//  color[u] = gray_color;
//  graph_traits < DependencyList >::adjacency_iterator vi, vi_end;
//  for (tie(vi, vi_end) = adjacent_vertices(u, g); vi != vi_end; ++vi)
//    if (color[*vi] == white_color)
//      if (has_cycle_dfs(g, *vi, color))
//        return true;            // cycle detected, return immediately
//      else if (color[*vi] == gray_color)        // *vi is an ancestor!
//        return true;
//  color[u] = black_color;
//  return false;
//}

bool Document::checkOnCycle()
{
#if 0
  std::vector < default_color_type > color(num_vertices(_DepList), white_color);
  graph_traits < DependencyList >::vertex_iterator vi, vi_end;
  for (tie(vi, vi_end) = vertices(_DepList); vi != vi_end; ++vi)
    if (color[*vi] == white_color)
      if (_has_cycle_dfs(_DepList, *vi, &color[0]))
        return true;
#endif
    return false;
}

struct Document::ColdRevert
{
    TransactionLog::Revert log;
    /// The ops in the order they are applied: the log's, reversed.
    std::vector<const LogOp*> ops;
    /// (container id, property) of every derived op, for the inverse.
    std::set<std::pair<long, std::string>> derived;
    /// Undo of a row that is not the tip (sec 24.4): checked against the
    /// ops after it, and derived values left to recompute.
    bool selective {false};
    /// Revert the row's view ops too (sec 24.9); a restore to a version
    /// does only under ViewObjectTransaction.
    bool views {true};
    /// A refusal is not an error but a reason to take another path (sec
    /// 27.34): logged, not reported.
    bool quiet {false};
    /// Objects touched because a derived value is not in the log.
    std::set<long> touched;
};

namespace {

/// The container an op names, in `doc` as it stands; null when gone.
PropertyContainer* opContainer(Document& doc, const LogOp& op)
{
    if (op.ckind == "doc")
        return &doc;
    if (op.ckind == "obj")
        return doc.getObjectByID(op.cid);
    if (op.ckind == "view" && op.cid > 0)
        return Document::viewOf(doc.getObjectByID(op.cid));   // sec 24.9
    return nullptr;
}

Document::ViewResolver& viewResolver()
{
    static Document::ViewResolver resolver;
    return resolver;
}

std::function<void()>& implicitCloser()
{
    static std::function<void()> closer;
    return closer;
}

/// The dynamic-property metadata of an addprop/delprop op, as
/// TransactionLog writes it: group, doc, then "attr[ ro][ hidden]" and
/// " status=N" (sec 27.67; absent from rows written before it).
struct DynamicMeta
{
    std::string group;
    std::string doc;
    short attr {0};
    bool readonly {false};
    bool hidden {false};
    bool hasStatus {false};
    unsigned long status {0};
};

DynamicMeta parseMeta(const std::string& meta)
{
    DynamicMeta m;
    auto first = meta.find('\n');
    auto last = meta.rfind('\n');
    if (first == std::string::npos)
        return m;
    m.group = meta.substr(0, first);
    if (last > first)
        m.doc = meta.substr(first + 1, last - first - 1);
    std::istringstream flags(meta.substr(last + 1));
    int attr = 0;
    flags >> attr;
    m.attr = static_cast<short>(attr);
    std::string word;
    while (flags >> word) {
        if (word == "ro")
            m.readonly = true;
        else if (word == "hidden")
            m.hidden = true;
        else if (word.compare(0, 7, "status=") == 0) {
            m.hasStatus = true;
            m.status = std::stoul(word.substr(7));
        }
    }
    return m;
}

/// Add back a dynamic property an op's metadata describes, with the status
/// bits it had (as PropertyContainer::Restore applies a saved status: the
/// User bits are the session's, and the touched state is the rows').
Property* addLoggedProperty(PropertyContainer& container, const std::string& ptype,
                            const std::string& name, const std::string& meta)
{
    DynamicMeta m = parseMeta(meta);
    Property* prop = container.addDynamicProperty(ptype.c_str(), name.c_str(), m.group.c_str(),
                                                  m.doc.c_str(), m.attr, m.readonly, m.hidden);
    if (prop && m.hasStatus) {
        Property::StatusBits status(m.status);
        status.reset(Property::User1);
        status.reset(Property::User2);
        status.reset(Property::User3);
        status.set(Property::Touched, prop->testStatus(Property::Touched));
        prop->setStatusValue(status.to_ulong());
    }
    return prop;
}

/// An object's touched state as the log records it (docs/TransactionLog.md
/// sec 27.58): its bits and the names of its touched properties -- each
/// property's Touched bit, not isTouched(): a link also reads as touched
/// once its target's revision moved (PropertyLink::isTouched), which is the
/// target's and moves with any write to it (sec 27.63).
struct TouchedState
{
    int bits {0};
    std::vector<std::string> props;

    bool operator==(const TouchedState& other) const
    {
        return bits == other.bits && props == other.props;
    }
};

TouchedState touchedStateOf(const DocumentObject& obj)
{
    TouchedState state;
    state.bits = obj.getLogTouchedBits();
    std::vector<Property*> props;
    obj.getPropertyList(props);
    for (auto prop : props) {
        if (prop->hasTouchedBit() && prop->getName())
            state.props.emplace_back(prop->getName());
    }
    std::sort(state.props.begin(), state.props.end());
    return state;
}

/// `prop`'s Touched bit set or cleared, the bit alone: setStatus(Touched)
/// would touch() and signal a change. Cleared through purgeTouched(), which
/// for a link also takes its target's revision as seen.
void setPropertyTouched(Property& prop, bool touched)
{
    if (prop.hasTouchedBit() == touched)
        return;
    if (!touched) {
        prop.purgeTouched();
        return;
    }
    Property::StatusBits bit;
    bit.set(Property::Touched);
    prop.setStatus(bit, true);
}

/// The touched state a run of log rows leaves (docs/TransactionLog.md sec
/// 27.58), folded as the rows are crossed in either direction: a set taken
/// back leaves the state it recorded before it, a set done leaves the
/// property touched and its object too unless the property is an output, a
/// recompute record leaves the state it recorded before or after it. What
/// no crossed row says keeps the state the document had.
struct TouchedFold
{
    struct Flags
    {
        int bits {-1};                       ///< the object's, -1 when no row said
        std::map<std::string, bool> props;   ///< touched or not, as the rows said
        bool exact {false};                  ///< a property not in `props` is clean
        std::set<std::string> written;       ///< set since `bits`: touches unless an output
    };
    std::map<long, Flags> objects;

    void forget(long cid)
    {
        objects.erase(cid);
    }

    void back(const LogOp& o)
    {
        if (o.op != "set" || o.ckind != "obj" || o.touched < 0)
            return;
        auto& f = objects[o.cid];
        f.bits = o.touched & ~DocumentObject::LogPropTouched;
        f.written.clear();
        f.props[o.prop] = (o.touched & DocumentObject::LogPropTouched) != 0;
    }

    void forward(const LogOp& o)
    {
        // A derived write's state is the recompute's, in its record.
        if (o.op != "set" || o.ckind != "obj" || o.derived)
            return;
        auto& f = objects[o.cid];
        f.props[o.prop] = true;
        f.written.insert(o.prop);
    }

    /// A recompute record's entries: {"id", "b", "p", "a", "q"}, sec 27.58;
    /// an undo's, a redo's or a restore's the same (sec 27.63). Crossed
    /// after the row's ops: it is exact.
    void record(const std::string& script, bool back)
    {
        if (script.empty() || script[0] != '{')
            return;
        auto j = nlohmann::json::parse(script, nullptr, false);
        if (!j.is_object() || !j.contains("objects") || !j["objects"].is_array())
            return;
        for (const auto& e : j["objects"]) {
            if (!e.is_object() || !e.contains("id") || !e["id"].is_number_integer())
                continue;
            const long cid = e["id"].get<long>();
            if (back && !e.contains("b"))
                continue;   // made by the recompute, or recorded before sec 27.58
            const char* bits = back ? "b" : "a";
            const char* props = back ? "p" : "q";
            if (e.contains(bits) && !e[bits].is_number_integer())
                continue;
            auto& f = objects[cid];
            f.bits = e.contains(bits) ? e[bits].get<int>() : 0;
            f.written.clear();
            f.props.clear();
            f.exact = true;
            if (e.contains(props) && e[props].is_array()) {
                for (const auto& name : e[props]) {
                    if (name.is_string())
                        f.props[name.get<std::string>()] = true;
                }
            }
        }
    }

    /// The document's state for each object the fold names, taken before
    /// the values are written back, which touch what they write.
    std::map<long, TouchedState> save(Document& doc, const std::set<long>& also) const
    {
        std::map<long, TouchedState> saved;
        auto take = [&](long cid) {
            if (auto obj = doc.getObjectByID(cid))
                saved.emplace(cid, touchedStateOf(*obj));
        };
        for (const auto& kv : objects)
            take(kv.first);
        for (long cid : also)
            take(cid);
        return saved;
    }

    /// Each saved object back to its saved state with what the rows said on
    /// top.
    void apply(Document& doc, const std::map<long, TouchedState>& saved) const
    {
        for (const auto& kv : saved) {
            auto obj = doc.getObjectByID(kv.first);
            if (!obj)
                continue;
            auto it = objects.find(kv.first);
            const Flags* f = it == objects.end() ? nullptr : &it->second;
            int bits = f && f->bits >= 0 ? f->bits : kv.second.bits;
            std::vector<Property*> props;
            obj->getPropertyList(props);
            for (auto prop : props) {
                const char* name = prop->getName();
                if (!name)
                    continue;
                bool touched = std::binary_search(kv.second.props.begin(),
                                                  kv.second.props.end(), std::string(name));
                if (f) {
                    auto p = f->props.find(name);
                    if (p != f->props.end())
                        touched = p->second;
                    else if (f->exact)
                        touched = false;
                    if (f->written.count(name) && !obj->testStatus(ObjectStatus::NoTouch)
                            && !(prop->getType() & Prop_Output)
                            && !prop->testStatus(Property::Output))
                        bits |= DocumentObject::LogTouch;
                }
                setPropertyTouched(*prop, touched);
            }
            obj->setLogTouchedBits(bits);
        }
    }
};

/// `obj` set to `state`, the bits alone: nothing is signalled as changed.
void setTouchedState(DocumentObject& obj, const TouchedState& state)
{
    std::vector<Property*> props;
    obj.getPropertyList(props);
    for (auto prop : props) {
        const char* name = prop->getName();
        if (!name)
            continue;
        setPropertyTouched(
            *prop, std::binary_search(state.props.begin(), state.props.end(), std::string(name)));
    }
    obj.setLogTouchedBits(state.bits);
}

/// The record a row carries of the touched state it left (sec 27.63), in
/// a recompute record's form: every object of `after`, with its state in
/// `before` when it had one -- an object the row made has none.
std::string touchedRecord(const std::map<long, TouchedState>& before,
                          const std::map<long, TouchedState>& after)
{
    auto list = nlohmann::json::array();
    for (const auto& kv : after) {
        nlohmann::json e;
        e["id"] = kv.first;
        auto it = before.find(kv.first);
        if (it != before.end()) {
            e["b"] = it->second.bits;
            if (!it->second.props.empty())
                e["p"] = it->second.props;
        }
        if (kv.second.bits || !kv.second.props.empty()) {
            e["a"] = kv.second.bits;
            if (!kv.second.props.empty())
                e["q"] = kv.second.props;
        }
        list.push_back(std::move(e));
    }
    if (list.empty())
        return {};
    nlohmann::json j;
    j["objects"] = std::move(list);
    return j.dump();
}

/** An undo or redo of a logged step (docs/TransactionLog.md sec 27.63)
 * leaves the touched state its rows record, as the walk does (sec 27.59):
 * the rows from the head back to the step's own, crossed back -- a redo's
 * step is the undo's row, so a redo is the undo taken back. Folded before
 * the writes, applied after them, and set again once the transaction guard
 * has touched what they wrote; an object whose derived value could not be
 * written back is touched last.
 */
struct StepTouched
{
    TouchedFold fold;
    std::map<long, TouchedState> before;   ///< each object that may change
    std::map<long, TouchedState> after;    ///< the same, as the step leaves it
    bool active {false};

    /// Before the step is applied.
    void begin(Document& doc, TransactionLog& log, const Transaction& step)
    {
        std::vector<TransactionLog::TouchedRow> rows;
        if (step.LogSeq <= 0 || !log.rowsBackTo(step.LogSeq, rows))
            return;
        std::set<long> named;
        for (const auto& r : rows) {   // newest first
            for (auto it = r.ops.rbegin(); it != r.ops.rend(); ++it)
                fold.back(*it);
            fold.record(r.script, true);
        }
        for (const auto& o : rows.back().ops)
            named.insert(o.cid);
        std::set<long> ids;
        for (auto obj : doc.getObjects()) {
            const long id = obj->getID();
            if (fold.objects.count(id) || named.count(id) || step.hasObject(obj))
                ids.insert(id);
        }
        before = fold.save(doc, ids);
        active = true;
    }

    /// After the writes, before the guard's touches: the fold on the state
    /// before, each of `touch` touched, and the record for the inverse row.
    std::string end(Document& doc, const Transaction& inverse, const std::set<long>& touch)
    {
        std::map<long, TouchedState> base;
        for (auto obj : doc.getObjects()) {
            const long id = obj->getID();
            auto it = before.find(id);
            if (it != before.end())
                base.emplace(id, it->second);
            else if (fold.objects.count(id) || inverse.hasObject(obj) || touch.count(id))
                // Made by the step: as it comes back -- a hot redo's is the
                // object itself -- with what the rows say on top.
                base.emplace(id, touchedStateOf(*obj));
        }
        fold.apply(doc, base);
        for (long cid : touch) {
            if (auto obj = doc.getObjectByID(cid))
                obj->touch();
        }
        for (const auto& kv : base) {
            if (auto obj = doc.getObjectByID(kv.first))
                after.emplace(kv.first, touchedStateOf(*obj));
        }
        return touchedRecord(before, after);
    }

    /// After the writes: the end state, recorded on `inverse`'s row and set
    /// again once the guard has touched what the step wrote.
    void leave(Document& doc, Transaction& inverse, const std::set<long>& touch)
    {
        inverse.LogScript = end(doc, inverse, touch);
        TransactionGuard::afterTouches([&doc, after = after]() {
            for (const auto& kv : after) {
                auto obj = doc.getObjectByID(kv.first);
                if (obj && !(touchedStateOf(*obj) == kv.second))
                    setTouchedState(*obj, kv.second);
            }
        });
    }
};

/** Section 24.4's refuse rule for undoing row `seq` when it is not the tip:
 * every property it left in a state must still be in that state -- the
 * newest op on it after `seq`, if any, left it the same -- so the undo
 * overwrites nothing done since. Derived properties are not checked: the
 * selective undo leaves them to recompute. The log's pending afters must
 * be resolved. Returns the conflicts, empty when there are none.
 */
std::string checkSelective(TransactionLog& log, int64_t seq, const std::vector<LogOp>& ops)
{
    // The state each property is left in by the row: its last op's after,
    // or "gone" for a dynamic property it removed.
    std::map<std::tuple<std::string, long, std::string>, std::string> left;
    for (const auto& o : ops) {
        // A view op the undo cannot apply (sec 24.9) is not checked either.
        if ((o.ckind == "view" && (o.cid <= 0 || !viewResolver())) || o.derived
                || o.prop.empty())
            continue;
        if (o.op == "set")
            left[{o.ckind, o.cid, o.prop}] = o.vafter;
        else if (o.op == "delprop")
            left[{o.ckind, o.cid, o.prop}] = "-";
    }
    std::ostringstream why;
    // Only this branch's history counts (sec 26): a row on another branch
    // is not in this document's past, and what came after it here is
    // what its chain says.
    const auto rows = log.store().chain(log.head(), seq);
    if (rows.empty() || rows.front().seq != seq) {
        why << " row " << seq << " is not on this branch;";
        return why.str();
    }
    for (const auto& kv : left) {
        LogOp later;
        if (!log.store().lastOpOn(std::get<0>(kv.first), std::get<1>(kv.first),
                                  std::get<2>(kv.first), seq, log.head(), later))
            continue;
        std::string now = later.op == "delprop" ? std::string("-")
                        : later.op == "set"     ? later.vafter
                                                : std::string("+");
        if (now != kv.second)
            why << " " << std::get<2>(kv.first) << " of id " << std::get<1>(kv.first)
                << " was changed since, by row " << later.txn << ";";
    }
    return why.str();
}

} // namespace

void Document::setViewResolver(ViewResolver resolver)
{
    viewResolver() = std::move(resolver);
}

PropertyContainer* Document::viewOf(const DocumentObject* obj)
{
    auto& resolver = viewResolver();
    return obj && resolver ? resolver(obj) : nullptr;
}

bool Document::_prepareRevert(int64_t seq, const std::string& name, ColdRevert& revert)
{
    auto log = getTransactionLog();
    std::ostringstream why;
    if (!log || seq <= 0)
        why << "no log row";
    else if (!log->readRevert(seq, revert.log))
        why << "row " << seq << " is not in the log";
    if (why.str().empty()) {
        for (auto it = revert.log.ops.rbegin(); it != revert.log.ops.rend(); ++it) {
            if (!revert.views && it->ckind == "view")
                continue;
            revert.ops.push_back(&*it);
        }
        // Objects the revert recreates: their sets need no live container.
        std::set<long> recreated;
        for (const LogOp* o : revert.ops) {
            if (o->op == "remove" && o->ckind == "obj")
                recreated.insert(o->cid);
        }
        // A view provider's ops (sec 24.9) are applied through the Gui's
        // resolver; with none, or for an op logged before its owner was
        // (cid -1), they are left alone. Its create and remove follow its
        // object's.
        const bool haveViews = bool(viewResolver());
        size_t views = 0;
        for (const LogOp* o : revert.ops) {
            if (o->derived && o->ckind != "view")
                revert.derived.emplace(o->cid, o->prop);
            if (o->ckind == "view") {
                if (!haveViews || o->cid <= 0) {
                    ++views;
                    continue;
                }
                if (o->op == "create" || o->op == "remove")
                    continue;
                if (!recreated.count(o->cid) && !getObjectByID(o->cid))
                    why << " the object of view property " << o->prop << " (id " << o->cid
                        << ") is gone;";
                else if (!o->vbefore.empty() && !revert.log.values.count(o->vbefore))
                    why << " the value of view property " << o->prop << " (" << o->vbefore
                        << ") is not in the log;";
                continue;
            }
            if (o->op == "remove") {
                if (getObjectByID(o->cid))
                    why << " object id " << o->cid << " (" << o->cname << ") is in use;";
                else if (getObject(o->cname.c_str()))
                    why << " name " << o->cname << " is in use;";
                else if (Base::Type::getTypeIfDerivedFrom(o->ctype.c_str(),
                                                          DocumentObject::getClassTypeId(), true)
                             .isBad())
                    why << " type " << o->ctype << " is unknown;";
            }
            else if (o->op == "create") {
                if (!getObjectByID(o->cid))
                    why << " object " << o->cname << " (id " << o->cid << ") is gone;";
            }
            else if (o->op == "set" || o->op == "delprop") {
                if (o->ckind == "obj" && !recreated.count(o->cid) && !getObjectByID(o->cid))
                    why << " the owner of " << o->prop << " (id " << o->cid << ") is gone;";
                else if (!o->derived && !o->vbefore.empty()
                         && !revert.log.values.count(o->vbefore))
                    why << " the value of " << o->prop << " (" << o->vbefore
                        << ") is not in the log;";
            }
        }
        if (revert.selective)
            why << checkSelective(*log, seq, revert.log.ops);
        if (views)
            FC_LOG("cold " << name << ": " << views << " view op(s) not reverted");
    }
    if (!why.str().empty()) {
        d->undoRefusal = why.str();
        if (revert.quiet)
            FC_LOG("Cannot revert '" << name << "' of " << getName() << " from the log:"
                   << why.str());
        else
            FC_ERR("Cannot undo '" << name << "' of " << getName() << " from the log:"
                   << why.str());
        return false;
    }
    return true;
}

void Document::_applyRevert(ColdRevert& revert)
{
    // docs/TransactionLog.md sec 24.3. In passes, so a value naming an
    // object -- a link -- never restores before the object exists: first
    // the objects the row removed come back, then the dynamic properties it
    // removed, then every value, then what the row added goes.
    auto guarded = [&](const LogOp& o, const std::function<void()>& fn) {
        try {
            fn();
        }
        catch (Base::Exception& e) {
            FC_ERR("cold undo, " << o.op << " " << o.cname << " " << o.prop << ": "
                   << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("cold undo, " << o.op << " " << o.cname << " " << o.prop << ": "
                   << e.what());
        }
    };
    const auto& values = revert.log.values;
    // What a removed object's set needs to add a dynamic property back.
    std::map<std::pair<long, std::string>, const LogOp*> dynamicSets;
    for (const LogOp* o : revert.ops) {
        if (o->op == "set" && !o->meta.empty())
            dynamicSets.emplace(std::make_pair(o->cid, o->prop), o);
    }

    // 1. Recreate what the row removed, under its id and name.
    for (const LogOp* o : revert.ops) {
        if (o->op != "remove" || o->ckind != "obj")
            continue;
        guarded(*o, [&]() {
            Base::Type type = Base::Type::getTypeIfDerivedFrom(
                o->ctype.c_str(), DocumentObject::getClassTypeId(), true);
            auto obj = static_cast<DocumentObject*>(type.createInstance());
            if (!obj)
                throw Base::RuntimeError("cannot create " + o->ctype);
            obj->_Id = o->cid;
            addObject(obj, o->cname.c_str(), false);
            for (auto& kv : dynamicSets) {
                if (kv.first.first != o->cid || obj->getPropertyByName(kv.first.second.c_str()))
                    continue;
                addLoggedProperty(*obj, kv.second->ptype, kv.first.second, kv.second->meta);
            }
        });
    }
    // 2. The dynamic properties the row removed.
    for (const LogOp* o : revert.ops) {
        if (o->op != "delprop")
            continue;
        guarded(*o, [&]() {
            auto container = opContainer(*this, *o);
            if (!container || container->getPropertyByName(o->prop.c_str()))
                return;
            addLoggedProperty(*container, o->ptype, o->prop, o->meta);
        });
    }
    // 3. Every value the row replaced, each afterRestore() once all are in.
    RestoreBatch batch;
    for (const LogOp* o : revert.ops) {
        if (o->op != "set" && o->op != "delprop")
            continue;
        guarded(*o, [&]() {
            auto container = opContainer(*this, *o);
            if (!container)
                return;
            Property* prop = container->getPropertyByName(o->prop.c_str());
            auto it = o->vbefore.empty() || (revert.selective && o->derived)
                ? values.end() : values.find(o->vbefore);
            if (it == values.end()) {
                // A derived value gone from the cache (sec 24.1), or a set
                // that holds none (a create's, or the `none` policy): the
                // owner recomputes it.
                if (o->derived) {
                    if (auto obj = Base::freecad_dynamic_cast<DocumentObject>(container)) {
                        obj->touch();
                        revert.touched.insert(obj->getID());
                    }
                }
                return;
            }
            if (!prop)
                throw Base::RuntimeError("no property " + o->prop);
            restoreValue(*prop, it->second);
        });
    }
    batch.finish();
    // 4. The dynamic properties the row added, where the object stays.
    std::set<long> created;
    for (const LogOp* o : revert.ops) {
        if (o->op == "create")
            created.insert(o->cid);
    }
    for (const LogOp* o : revert.ops) {
        if (o->op != "addprop" || created.count(o->cid))
            continue;
        guarded(*o, [&]() {
            if (auto container = opContainer(*this, *o))
                container->removeDynamicProperty(o->prop.c_str());
        });
    }
    // 5. The objects the row created.
    for (const LogOp* o : revert.ops) {
        if (o->op != "create" || o->ckind != "obj")
            continue;
        guarded(*o, [&]() {
            if (auto obj = getObjectByID(o->cid))
                removeObject(obj->getNameInDocument());
        });
    }
}

void Document::_deleteTransaction(Transaction* t)
{
    if (!t)
        return;
    // The log's worker serialises copies after the commit that made them;
    // a copy of a link to an object this deletion destroys must be written
    // first. Only a step that removed objects owns any, so this rarely waits.
    if (d->transactionLog && t->destroysObjects())
        d->transactionLog->flush();
    delete t;
}

void Document::_trimHotWindow(std::list<Transaction*>& stack, std::map<int, Transaction*>& map)
{
    auto log = getTransactionLog();
    size_t hot = 0;
    for (auto t : stack)
        hot += t->Cold ? 0 : 1;
    // Oldest first, as clearUndos() deletes: an object a later step
    // references can be owned by an earlier one.
    for (auto it = stack.begin(); it != stack.end() && hot > d->UndoMaxStackSize;) {
        Transaction* t = *it;
        if (t->Cold) {
            ++it;
            continue;
        }
        --hot;
        if (log && t->LogSeq > 0) {
            Transaction* stub = Transaction::coldCopy(*t);
            map[t->getID()] = stub;
            *it = stub;
            _deleteTransaction(t);
            ++it;
        }
        else {
            map.erase(t->getID());
            _deleteTransaction(t);
            it = stack.erase(it);
        }
    }
}

bool Document::undoLogged(int64_t seq)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 24.4: a new transaction applying row
    // `seq` reversed, refused when anything since changed what it touched.
    if (isPerformingTransaction() || d->committing)
        return false;
    auto log = getTransactionLog();
    if (!log)
        return false;
    if (d->activeUndoTransaction)
        _commitTransaction(true);
    log->resolvePending();
    std::string name;
    for (const auto& t : log->store().transactions(seq, 1)) {
        if (t.seq == seq)
            name = t.name;
    }
    ColdRevert revert;
    revert.selective = true;
    if (!_prepareRevert(seq, name, revert))
        return false;

    _clearMyRedos();
    d->activeUndoTransaction = new Transaction(0);
    d->activeUndoTransaction->Name = "Undo " + name;
    d->activeUndoTransaction->LogKind = "undo";
    d->activeUndoTransaction->Inverts = seq;
    mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
    _applyRevert(revert);
    _commitTransaction(false);
    return true;
}

namespace
{

bool isDesktop(const Actor* actor)
{
    return !actor || actor->kind == Actor::Local;
}

/// Sec 30.10 item 1: one user, whichever login -- by kind and name; no
/// actor is the desktop user.
bool sameUser(const Actor* a, const Actor* b)
{
    if (isDesktop(a) || isDesktop(b))
        return isDesktop(a) && isDesktop(b);
    return a->kind == b->kind && a->name == b->name;
}

}  // namespace

Transaction* Document::_myStep(bool undo) const
{
    const auto& stack = undo ? mUndoTransactions : mRedoTransactions;
    // With no log there are no authors: one stack, its back (item 7).
    if (!d->transactionLog)
        return stack.empty() ? nullptr : stack.back();
    const auto actor = ActorScope::current();
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (sameUser((*it)->Author.get(), actor.get()))
            return *it;
    }
    return nullptr;
}

bool Document::_atState(const Transaction* step) const
{
    return !d->transactionLog || step->StateAfter == d->stateToken;
}

bool Document::stepNeedsLog(bool undo) const
{
    if (!d->iUndoMode)
        return false;
    const Transaction* step = _myStep(undo);
    // An open transaction is committed first. The actor's own is then the
    // step, where the document stands; another's is a write past the
    // actor's newest.
    if (d->activeUndoTransaction) {
        if (!d->transactionLog)
            return false;
        if (undo && sameUser(d->activeUndoTransaction->Author.get(),
                             ActorScope::current().get()))
            return false;
        return step != nullptr;
    }
    return step && !_atState(step);
}

const std::string& Document::undoRefusal() const
{
    return d->undoRefusal;
}

bool Document::_revertStep(bool undo, Transaction* step)
{
    // docs/TransactionLog.md sec 30.10 item 3: someone has written since
    // the step, so its copies are of a state the document is no longer in.
    // It is taken back as a selective undo is (sec 24.4) -- the values read
    // from the log, refused when what the step left has changed since --
    // in a transaction of its own, so the recompute of what the revert
    // leaves to one is in the same row.
    auto log = getTransactionLog();
    auto& from = undo ? mUndoTransactions : mRedoTransactions;
    auto& fromMap = undo ? mUndoMap : mRedoMap;
    auto& to = undo ? mRedoTransactions : mUndoTransactions;
    auto& toMap = undo ? mRedoMap : mUndoMap;
    if (!log || isPerformingTransaction() || d->committing) {
        d->undoRefusal = "a transaction is being applied";
        return false;
    }
    log->resolvePending();
    ColdRevert revert;
    revert.selective = true;
    if (!_prepareRevert(step->LogSeq, step->Name, revert))
        return false;

    auto inverse = new Transaction(step->getID());
    inverse->Name = step->Name;
    d->activeUndoTransaction = inverse;
    _applyRevert(revert);
    if (!revert.touched.empty()) {
        if (Transaction::isApplying())
            FC_WARN((undo ? "undo" : "redo") << " of '" << step->Name << "' in " << getName()
                    << " left objects to recompute");
        else
            recompute();
    }
    inverse->StateBefore = d->stateToken;
    d->stateToken = ++d->stateCounter;
    inverse->StateAfter = d->stateToken;
    logInverse(*step, undo ? "undo" : "redo", &revert);
    // What waited on the row: the recompute's record (sec 27.59).
    auto after = std::move(inverse->AfterLogRow);
    for (auto& write : after)
        write(*log);
    d->activeUndoTransaction = nullptr;

    // The step it leaves is cold: what the revert made and removed is not
    // what the copies of any step name, so nothing is applied from copies
    // across it.
    Transaction* stub = Transaction::coldCopy(*inverse);
    _deleteTransaction(inverse);
    toMap[stub->getID()] = stub;
    to.push_back(stub);
    fromMap.erase(step->getID());
    from.remove(step);
    _deleteTransaction(step);

    if (undo)
        signalUndo(*this);
    else
        signalRedo(*this);
    if (!Transaction::isApplying()) {
        if (undo)
            GetApplication().signalUndo();
        else
            GetApplication().signalRedo();
    }
    return true;
}

bool Document::undo(int id)
{
    OperationScope scope;   // sec 27.38
    if (d->iUndoMode) {
        d->undoRefusal.clear();
        if(id) {
            auto it = mUndoMap.find(id);
            if(it == mUndoMap.end())
                return false;
            if(it->second != d->activeUndoTransaction) {
                // The actor's steps down to that one (sec 30.10): another's
                // on the way are not in its stack.
                std::optional<TransactionGuard> guard;
                while (true) {
                    Transaction* top = _myStep(true);
                    if (!top || top == it->second)
                        break;
                    // One guard for the run, let go of around a step that
                    // goes through the log: it recomputes, and a guard's
                    // deferred touches would starve that.
                    if (!_atState(top))
                        guard.reset();
                    else if (!guard)
                        guard.emplace(TransactionGuard::Undo);
                    if (!undo(0))
                        return false;   // a step refused (sec 24.3, 30.10)
                }
            }
        }

        if (d->activeUndoTransaction)
            _commitTransaction(true);
        Transaction* step = _myStep(true);
        if (!step)
            return false;
        if (!_atState(step))
            return _revertStep(true, step);

        ColdRevert cold;
        if (step->Cold && !_prepareRevert(step->LogSeq, step->Name, cold))
            return false;
        StepTouched touched;   // sec 27.63
        if (auto log = getTransactionLog())
            touched.begin(*this, *log, *step);

        TransactionGuard guard(TransactionGuard::Undo);

        // redo
        d->activeUndoTransaction = new Transaction(step->getID());
        d->activeUndoTransaction->Name = step->Name;
        // Back in the state the step was made in; the redo step is applied
        // from there.
        d->activeUndoTransaction->StateBefore = step->StateAfter;
        d->activeUndoTransaction->StateAfter = step->StateBefore;
        d->stateToken = step->StateBefore ? step->StateBefore : ++d->stateCounter;

        Base::FlagToggler<bool> flag(d->undoing);
        // applying the undo
        if (step->Cold)
            _applyRevert(cold);
        else
            step->apply(*this,false);
        if (touched.active)
            touched.leave(*this, *d->activeUndoTransaction, cold.touched);
        logInverse(*step, "undo", step->Cold ? &cold : nullptr);

        // save the redo
        mRedoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
        mRedoTransactions.push_back(d->activeUndoTransaction);
        d->activeUndoTransaction = nullptr;

        mUndoMap.erase(step->getID());
        // The actor's newest, which another's steps may lie above.
        mUndoTransactions.remove(step);
        delete step;
        return true;
    }

    return false;
}

bool Document::redo(int id)
{
    OperationScope scope;   // sec 27.38
    if (d->iUndoMode) {
        d->undoRefusal.clear();
        if(id) {
            auto it = mRedoMap.find(id);
            if(it == mRedoMap.end())
                return false;
            std::optional<TransactionGuard> guard;
            while (true) {
                Transaction* top = _myStep(false);
                if (!top || top == it->second)
                    break;
                if (!_atState(top))
                    guard.reset();
                else if (!guard)
                    guard.emplace(TransactionGuard::Redo);
                if (!redo(0))
                    return false;   // a step refused (sec 24.3, 30.10)
            }
        }

        if (d->activeUndoTransaction)
            _commitTransaction(true);

        Transaction* step = _myStep(false);
        if (!step)
            return false;
        if (!_atState(step))
            return _revertStep(false, step);

        ColdRevert cold;
        if (step->Cold && !_prepareRevert(step->LogSeq, step->Name, cold))
            return false;
        StepTouched touched;   // sec 27.63
        if (auto log = getTransactionLog())
            touched.begin(*this, *log, *step);

        TransactionGuard guard(TransactionGuard::Redo);

        // undo
        d->activeUndoTransaction = new Transaction(step->getID());
        d->activeUndoTransaction->Name = step->Name;
        d->activeUndoTransaction->StateBefore = step->StateAfter;
        d->activeUndoTransaction->StateAfter = step->StateBefore;
        d->stateToken = step->StateBefore ? step->StateBefore : ++d->stateCounter;

        // do the redo
        Base::FlagToggler<bool> flag(d->undoing);
        if (step->Cold)
            _applyRevert(cold);
        else
            step->apply(*this,true);
        if (touched.active)
            touched.leave(*this, *d->activeUndoTransaction, cold.touched);
        logInverse(*step, "redo", step->Cold ? &cold : nullptr);

        mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
        mUndoTransactions.push_back(d->activeUndoTransaction);
        d->activeUndoTransaction = nullptr;

        mRedoMap.erase(step->getID());
        mRedoTransactions.remove(step);
        delete step;
        if (getTransactionLog())
            _trimHotWindow(mUndoTransactions, mUndoMap);
        return true;
    }

    return false;
}

void Document::logInverse(const Transaction& applied, const char* kind, const ColdRevert* cold)
{
    // docs/TransactionLog.md sec 24.2: the record the undo (or redo) opened
    // before applying collected the writes it made, which are the inverse
    // of what it applied. It is logged as a transaction of its own, naming
    // the row it inverts, before it becomes the redo (or undo) step.
    auto log = getTransactionLog();
    if (!log || !d->activeUndoTransaction)
        return;
    if (cold) {
        d->activeUndoTransaction->inheritDerived(
            [this, cold](const TransactionalObject* tobj, const Property* prop) {
                long cid = 0;
                const PropertyContainer* container = this;
                if (auto obj = Base::freecad_dynamic_cast<const DocumentObject>(tobj)) {
                    cid = obj->getID();
                    container = obj;
                }
                else if (tobj) {
                    return false;   // a view provider: its ops are not reverted
                }
                // Safe on a property already destroyed (a dynamic one removed).
                const char* name = container->getPropertyName(prop);
                return name && cold->derived.count({cid, name}) != 0;
            });
    }
    else
        d->activeUndoTransaction->inheritDerived(applied);
    d->activeUndoTransaction->LogSeq =
        log->onCommit(*d->activeUndoTransaction, kind, Application::InvocationScope::current(),
                      applied.LogSeq);
}

App::Property* Document::addDynamicProperty(
    const char* type, const char* name, const char* group, const char* doc,
    short attr, bool ro, bool hidden)
{
    auto prop = PropertyContainer::addDynamicProperty(type,name,group,doc,attr,ro,hidden);
    if(prop)
        _addOrRemoveProperty(nullptr, prop, true);
    return prop;
}

bool Document::removeDynamicProperty(const char* name)
{
    Property* prop = getDynamicPropertyByName(name);
    if(!prop || prop->testStatus(App::Property::LockDynamic))
        return false;

    _addOrRemoveProperty(nullptr, prop, false);
    return PropertyContainer::removeDynamicProperty(name);
}

void Document::addOrRemovePropertyOfObject(TransactionalObject* obj, Property *prop, bool add)
{
    if (!prop || !obj || !obj->isAttachedToDocument())
        return;
    _addOrRemoveProperty(obj, prop, add);
}

void Document::_addOrRemoveProperty(TransactionalObject* obj, Property *prop, bool add)
{
    if (d->bookkeeping)
        return;
    // A property never saved -- a cache like Part's shape cache, added when
    // a command's isActive() builds a link's shape -- is no change to the
    // document: it opens no transaction, where it opened the running
    // command's, which then committed as an undo step with nothing in it
    // (docs/TransactionLog.md sec 27.17). One open already still records it.
    // Prop_Transient is not one: its name and type are saved.
    const bool unsaved = prop && (prop->getType() & Prop_NoPersist);
    if(transactionsWanted() && !isPerformingTransaction() && !d->activeUndoTransaction
            && !unsaved) {
        if(!testStatus(Restoring) || testStatus(Importing)) {
            int tid=0;
            const char *name = GetApplication().getActiveTransaction(&tid);
            if(name && tid>0)
                _openTransaction(name,tid);
            else
                _openImplicitTransaction();
        }
    }
    if (d->activeUndoTransaction && !d->rollback)
        d->activeUndoTransaction->addOrRemoveProperty(obj, prop, add);
}

bool Document::isPerformingTransaction() const
{
    // A switch or a crash recovery replaying rows puts the document back
    // into a recorded state, as an undo does, and object code must not
    // react to it any more than to an undo (docs/TransactionLog.md sec
    // 27.68). A replay records nothing already (transactionsWanted()). A
    // restore to a version is not one: it is a recorded step.
    return d->undoing || d->rollback || d->replaying || Transaction::isApplying();
}

bool Document::isReplaying() const
{
    return d->replaying || d->checkingOut;
}

namespace
{

/// Sec 30.10 item 2: the steps of `stack` that are the actor's, newest
/// first -- every one when the log is off, which keeps no authors. An open
/// transaction is the newest undo step of whoever opened it.
std::vector<const Transaction*> stepsOf(const std::list<Transaction*>& stack,
                                        const Transaction* open, bool authors)
{
    std::vector<const Transaction*> mine;
    const auto actor = ActorScope::current();
    auto take = [&](const Transaction* step) {
        if (!authors || sameUser(step->Author.get(), actor.get()))
            mine.push_back(step);
    };
    if (open)
        take(open);
    for (auto it = stack.rbegin(); it != stack.rend(); ++it)
        take(*it);
    return mine;
}

}  // namespace

std::vector<std::string> Document::getAvailableUndoNames() const
{
    std::vector<std::string> vList;
    for (auto step : stepsOf(mUndoTransactions, d->activeUndoTransaction,
                             d->transactionLog != nullptr))
        vList.push_back(step->Name);
    return vList;
}

std::vector<std::string> Document::getAvailableRedoNames() const
{
    std::vector<std::string> vList;
    for (auto step : stepsOf(mRedoTransactions, nullptr, d->transactionLog != nullptr))
        vList.push_back(step->Name);
    return vList;
}

void Document::openTransaction(const char* name) {
    if(isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Cannot open transaction while transacting");
        return;
    }

    GetApplication().setActiveTransaction(name?name:"<empty>");
}

int Document::_openTransaction(const char* name, int id, bool implicit)
{
    if(isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Cannot open transaction while transacting");
        return 0;
    }

    if (transactionsWanted()) {
        // Avoid recursive calls that is possible while
        // clearing the redo transactions and will cause
        // a double deletion of some transaction and thus
        // a segmentation fault
        if (d->opentransaction)
            return 0;
        Base::FlagToggler<> flag(d->opentransaction);

        if(id && mUndoMap.find(id)!=mUndoMap.end())
            THROWM(Base::RuntimeError, "invalid transaction id")
        if (d->activeUndoTransaction)
            _commitTransaction(true);
        _clearMyRedos();

        d->activeUndoTransaction = new Transaction(id);
        if (!name)
            name = "<empty>";
        d->activeUndoTransaction->Name = name;
        d->activeUndoTransaction->Implicit = implicit;
        mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
        id = d->activeUndoTransaction->getID();

        signalOpenTransaction(*this, name);

        // An implicit transaction is not mirrored into the active document:
        // it stays out of the application's transaction as it does at commit,
        // and a mirror, not implicit itself, was never closed with it
        // (docs/TransactionLog.md sec 24.13).
        auto &app = GetApplication();
        auto activeDoc = app.getActiveDocument();
        if(!implicit &&
           activeDoc &&
           activeDoc!=this &&
           !activeDoc->hasPendingTransaction())
        {
            std::string aname("-> ");
            aname += d->activeUndoTransaction->Name;
            FC_LOG("auto transaction " << getName() << " -> " << activeDoc->getName());
            activeDoc->_openTransaction(aname.c_str(),id);
        }
        return id;
    }
    return 0;
}

void Document::renameTransaction(const char *name, int id) {
    if(name && d->activeUndoTransaction && d->activeUndoTransaction->getID()==id) {
        if(boost::starts_with(d->activeUndoTransaction->Name, "-> "))
            d->activeUndoTransaction->Name.resize(3);
        else
            d->activeUndoTransaction->Name.clear();
        d->activeUndoTransaction->Name += name;
    }
}

bool Document::transactionsWanted() const
{
    // A document being made joins no transaction: its creation is not a
    // step of whatever command made it. With the log on, its Label joined
    // the command's transaction, and the restore that followed committed
    // that transaction in every document (docs/TransactionLog.md sec 27.15).
    if (d->replaying || testStatus(Initializing))
        return false;
    return d->iUndoMode || DocumentParams::getTransactionLog() != 0;
}

void Document::_openImplicitTransaction()
{
    // docs/TransactionLog.md sec 9.1: with the log on, a write that arrives
    // with no transaction active opens one of its own, named after the
    // invocation it happens in, and closed when that returns.
    if (DocumentParams::getTransactionLog() == 0 || testStatus(Initializing))
        return;
    const char* origin = Application::InvocationScope::current();
    std::string name = "<implicit";
    if (origin && origin[0]) {
        name += ' ';
        name += origin;
    }
    name += '>';
#if defined(HAVE_BACKTRACE_SYMBOLS)
    // FC_TXNLOG_TRACE_IMPLICIT: who writes outside a command. The stack of
    // every implicit transaction as it opens, on stderr; names are mangled
    // (c++filt).
    static const bool trace = std::getenv("FC_TXNLOG_TRACE_IMPLICIT") != nullptr;
    if (trace) {
        void* stack[48];
        const int frames = backtrace(stack, 48);
        std::fprintf(stderr, "== implicit transaction %s in %s\n", name.c_str(), getName());
        backtrace_symbols_fd(stack, frames, 2);
    }
#endif
    if (_openTransaction(name.c_str(), 0, true) && d->activeUndoTransaction) {
        d->activeUndoTransaction->Origin = origin ? origin : "";
        // Opened outside any invocation -- a GUI event that is not a
        // command: nothing returns to close it, so the Gui closes it when
        // control is back in its event loop (sec 24.10).
        if (Application::InvocationScope::depth() == 0) {
            auto& closer = implicitCloser();
            if (closer)
                closer();
        }
    }
}

void Document::setImplicitCloser(std::function<void()> closer)
{
    implicitCloser() = std::move(closer);
}

void Document::commitImplicitTransaction()
{
    if (d->activeUndoTransaction && d->activeUndoTransaction->Implicit
            && !isPerformingTransaction() && !d->committing)
        _commitTransaction(false);
}

void Document::_checkTransaction(DocumentObject* pcDelObj, const Property *What, int line)
{
    // if the undo is active but no transaction open, open one!
    if (transactionsWanted() && !isPerformingTransaction()) {
        if (!d->activeUndoTransaction) {
            if(!testStatus(Restoring) || testStatus(Importing)) {
                int tid=0;
                const char *name = GetApplication().getActiveTransaction(&tid);
                bool ignore = false;
                if(What) {
                    if(What->testStatus(Property::NoModify))
                        ignore = true;
                    else if(!Base::freecad_dynamic_cast<Document>(What->getContainer())
                            && !Base::freecad_dynamic_cast<DocumentObject>(What->getContainer())) {
                        // A view provider's property. With the log on, a
                        // saved one is document data like any other and is
                        // recorded whatever ViewObjectTransaction says
                        // (docs/TransactionLog.md sec 24.10); one that is
                        // never saved is not.
                        short type = What->getContainer()->getPropertyType(What);
                        if (DocumentParams::getTransactionLog() != 0)
                            ignore = (type & Prop_Transient) || (type & Prop_NoPersist)
                                || What->testStatus(Property::Transient)
                                || DerivedViewWrites::active();
                        else if (!DocumentParams::getViewObjectTransaction()
                                 && !AutoTransaction::recordViewObjectChange())
                            ignore = true;
                    }
                }
                if(name && tid>0) {
                    if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
                        if(What)
                            FC_LOG((ignore?"ignore":"auto") << " transaction ("
                                    << line << ") '" << What->getFullName());
                        else
                            FC_LOG((ignore?"ignore":"auto") <<" transaction ("
                                    << line << ") '" << name << "' in " << getName());
                    }
                    if(!ignore)
                        _openTransaction(name,tid);
                    return;
                }
                if(!ignore && !pcDelObj) {
                    _openImplicitTransaction();
                    return;
                }
            }
            if(!pcDelObj)
                return;
            // When the object is going to be deleted we have to check if it has already been added to
            // the undo transactions
            std::list<Transaction*>::iterator it;
            for (it = mUndoTransactions.begin(); it != mUndoTransactions.end(); ++it) {
                if ((*it)->hasObject(pcDelObj)) {
                    _openTransaction("Delete");
                    break;
                }
            }
        }
    }
}

void Document::_clearRedos()
{
    if(isPerformingTransaction() || d->committing) {
        FC_ERR("Cannot clear redo while transacting");
        return;
    }

    mRedoMap.clear();
    while (!mRedoTransactions.empty()) {
        _deleteTransaction(mRedoTransactions.back());
        mRedoTransactions.pop_back();
    }
}

void Document::_clearMyRedos()
{
    if (!d->transactionLog) {
        _clearRedos();
        return;
    }
    if(isPerformingTransaction() || d->committing) {
        FC_ERR("Cannot clear redo while transacting");
        return;
    }
    // Sec 30.10 item 5: a new step ends what its author could redo, and
    // nobody else's. Newest first, as _clearRedos() goes.
    const auto actor = ActorScope::current();
    for (auto it = mRedoTransactions.end(); it != mRedoTransactions.begin();) {
        --it;
        Transaction* step = *it;
        if (!sameUser(step->Author.get(), actor.get()))
            continue;
        mRedoMap.erase(step->getID());
        it = mRedoTransactions.erase(it);
        _deleteTransaction(step);
    }
}

void Document::commitTransaction() {
    OperationScope scope;   // sec 27.38
    if(isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Cannot commit transaction while transacting");
        return;
    }

    if (d->activeUndoTransaction && d->activeUndoTransaction->Implicit)
        _commitTransaction(false);
    else if (d->activeUndoTransaction)
        GetApplication().closeActiveTransaction(false,d->activeUndoTransaction->getID());
}

void Document::_commitTransaction(bool notify)
{
    if (d->activeUndoTransaction) {
        if(d->undoing || d->rollback || d->committing) {
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                FC_WARN("Cannot commit transaction while transacting");
            return;
        }
        Base::FlagToggler<> flag(d->committing);
        Application::TransactionSignaller signaller(false,true);
        int id = d->activeUndoTransaction->getID();
        // Phase 0 of the transaction log: measure what this commit would
        // write (docs/TransactionLog.md sec 15). A static bool test when off.
        TransactionMeasure::checkEnvironment();
        if (TransactionMeasure::enabled())
            TransactionMeasure::onCommit(*this, *d->activeUndoTransaction);
        const bool implicit = d->activeUndoTransaction->Implicit;
        bool snapshotDue = false;
        if (auto log = getTransactionLog()) {
            const std::string& kind = d->activeUndoTransaction->LogKind;
            d->activeUndoTransaction->LogSeq =
                log->onCommit(*d->activeUndoTransaction,
                              !kind.empty() ? kind.c_str() : implicit ? "implicit" : "user",
                              d->activeUndoTransaction->Origin.c_str(),
                              d->activeUndoTransaction->Inverts);
            // What waited on the row: a recompute record (sec 27.59).
            auto after = std::move(d->activeUndoTransaction->AfterLogRow);
            for (auto& write : after)
                write(*log);
            // The cadence of unnamed versions (docs/TransactionLog.md sec
            // 16.3): every N commits, or the first commit the autosave
            // interval after the last version (sec 25.3). Taken once the
            // commit is complete, below.
            ++d->commitsSinceVersion;
            const long every = DocumentParams::getTransactionLogSnapshotTransactions();
            const long secs = DocumentParams::getAutoSaveEnabled()
                ? 60L * DocumentParams::getAutoSaveTimeout() : 0L;
            if (every > 0 && d->commitsSinceVersion >= every)
                snapshotDue = true;
            if (secs > 0 && d->lastVersionTime > 0
                    && std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch()).count()
                       - d->lastVersionTime >= secs)
                snapshotDue = true;
        }
        // The state it was made in and the one it leaves (sec 30.10): a
        // new one, unless it changed nothing.
        d->activeUndoTransaction->StateBefore = d->stateToken;
        if (!d->activeUndoTransaction->isEmpty())
            d->stateToken = ++d->stateCounter;
        d->activeUndoTransaction->StateAfter = d->stateToken;
        if (d->iUndoMode) {
            mUndoTransactions.push_back(d->activeUndoTransaction);
        }
        else {
            // Recorded for the log only: no undo step is kept.
            mUndoMap.erase(id);
            _deleteTransaction(d->activeUndoTransaction);
        }
        d->activeUndoTransaction = nullptr;
        // check the stack for the limits: the hot window (sec 24.3)
        _trimHotWindow(mUndoTransactions, mUndoMap);
        signalCommitTransaction(*this);

        if (notify && !implicit)
            GetApplication().closeActiveTransaction(false,id);
        if (snapshotDue)
            snapshotToLog();
    }
}

void Document::abortTransaction() {
    OperationScope scope;   // sec 27.38
    if(isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Cannot abort transaction while transacting");
        return;
    }
    if (d->activeUndoTransaction)
        GetApplication().closeActiveTransaction(true,d->activeUndoTransaction->getID());
}

void Document::_abortTransaction()
{
    if (d->activeUndoTransaction) {
        if(d->undoing || d->rollback || d->committing) {
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
                FC_WARN("Cannot abort transaction while transacting");
            return;
        }
        Base::FlagToggler<bool> flag(d->rollback);
        Application::TransactionSignaller signaller(true,true);
        TransactionGuard guard(TransactionGuard::Abort);

        // applying the so far made changes
        d->activeUndoTransaction->apply(*this,false);

        // destroy the undo
        mUndoMap.erase(d->activeUndoTransaction->getID());
        _deleteTransaction(d->activeUndoTransaction);
        d->activeUndoTransaction = nullptr;
        // signalAbortTransaction is emitted by the caller, once the enclosing
        // TransactionGuard has flushed the property changes this rollback deferred
    }
}

bool Document::hasPendingTransaction() const
{
    if (d->activeUndoTransaction)
        return true;
    else
        return false;
}

int Document::getBookedTransactionID() const
{
    if (d->activeUndoTransaction)
        return d->activeUndoTransaction->getID();
    int tid = 0;
    GetApplication().getActiveTransaction(&tid);
    return tid;
}

int Document::getTransactionID(bool undo, unsigned pos) const {
    // The open transaction is the document's one, whoever opened it: the
    // application asks for it here to close it, from wherever that happens.
    if (undo && d->activeUndoTransaction) {
        if (pos == 0)
            return d->activeUndoTransaction->getID();
        --pos;
    }
    // Then the actor's own steps (sec 30.10), the pos-th from the newest.
    const auto steps = stepsOf(undo ? mUndoTransactions : mRedoTransactions, nullptr,
                               d->transactionLog != nullptr);
    return pos < steps.size() ? steps[pos]->getID() : 0;
}

bool Document::isTransactionEmpty() const
{
    if (d->activeUndoTransaction) {
        // Transactions are now only created when there are actual changes.
        // Empty transaction is now significant for marking external changes. It
        // is used to match ID with transactions in external documents and
        // trigger undo/redo there.

        // return d->activeUndoTransaction->isEmpty();

        return false;
    }

    return true;
}

void Document::clearDocument()
{
    d->activeObject = nullptr;

    if (!d->objectArray.empty()) {
        GetApplication().signalDeleteDocument(*this);
        d->clearDocument();
        GetApplication().signalNewDocument(*this,false);
    }

    Base::FlagToggler<> flag(globalIsRestoring, false);

    setStatus(Document::PartialDoc,false);

    d->clearRecomputeLog();
    d->objectArray.clear();
    d->objectMap.clear();
    d->objectIdMap.clear();
    d->lastObjectId = 0;
}


void Document::clearUndos()
{
    OperationScope scope;   // sec 27.38
    // Nothing to clear: a document opened while another applies a
    // transaction (Transaction::isApplying() is process-wide) -- a pinned
    // version reopened by the undo of an unpin (sec 27.38) -- is not
    // transacting itself.
    if (!d->activeUndoTransaction && mUndoTransactions.empty() && mRedoTransactions.empty()
            && mUndoMap.empty())
        return;
    if(isPerformingTransaction() || d->committing) {
        FC_ERR("Cannot clear undos while transacting");
        return;
    }

    if (d->activeUndoTransaction)
        _commitTransaction(true);

    mUndoMap.clear();

    // When cleaning up the undo stack we must delete the transactions from front
    // to back because a document object can appear in several transactions but
    // once removed from the document the object can never ever appear in any later
    // transaction. Since the document object may be also deleted when the transaction
    // is deleted we must make sure not access an object once it's destroyed. Thus, we
    // go from front to back and not the other way round.
    while (!mUndoTransactions.empty()) {
        _deleteTransaction(mUndoTransactions.front());
        mUndoTransactions.pop_front();
    }
    //while (!mUndoTransactions.empty()) {
    //    delete mUndoTransactions.back();
    //    mUndoTransactions.pop_back();
    //}

    _clearRedos();
}

int Document::getAvailableUndos(int id) const
{
    // The actor's own steps (sec 30.10): how many, or how many down to
    // and with the one of `id` -- 0 when that one is not theirs.
    const auto steps = stepsOf(mUndoTransactions, d->activeUndoTransaction,
                               d->transactionLog != nullptr);
    if(id) {
        for (size_t i = 0; i < steps.size(); ++i) {
            if (steps[i]->getID() == id)
                return static_cast<int>(i + 1);
        }
        return 0;
    }
    return static_cast<int>(steps.size());
}

int Document::getAvailableRedos(int id) const
{
    const auto steps = stepsOf(mRedoTransactions, nullptr, d->transactionLog != nullptr);
    if(id) {
        for (size_t i = 0; i < steps.size(); ++i) {
            if (steps[i]->getID() == id)
                return static_cast<int>(i + 1);
        }
        return 0;
    }
    return static_cast<int>(steps.size());
}

void Document::setUndoMode(int iMode)
{
    // An implicit transaction belongs to the mode it was opened in
    // (docs/TransactionLog.md sec 24.13): writes made with undo off must not
    // become an undo step because undo came on before the transaction closed.
    if (!d->iUndoMode != !iMode)
        commitImplicitTransaction();

    if (d->iUndoMode && !iMode)
        clearUndos();

    d->iUndoMode = iMode;
}

int Document::getUndoMode() const
{
    return d->iUndoMode;
}

unsigned int Document::getUndoMemSize () const
{
    return d->UndoMemSize;
}

void Document::setUndoLimit(unsigned int UndoMemSize)
{
    d->UndoMemSize = UndoMemSize;
}

void Document::setMaxUndoStackSize(unsigned int UndoMaxStackSize)
{
     d->UndoMaxStackSize = UndoMaxStackSize;
}

unsigned int Document::getMaxUndoStackSize()const
{
    return d->UndoMaxStackSize;
}

void Document::onBeforeChange(const Property* prop)
{
    if(!d->rollback && !d->bookkeeping) {
        _checkTransaction(0, prop, __LINE__);
        if (d->activeUndoTransaction)
            d->activeUndoTransaction->addObjectChange(nullptr, prop);
    }
    if(prop == &FileName)
        ExpressionBlocker::check();
    if(prop == &Label)
        oldLabel = Label.getValue();
    signalBeforeChange(*this, *prop);
}

void Document::onChanged(const Property* prop)
{
    signalChanged(*this, *prop);

    // Bookkeeping is no transaction, so the log never hears of it: what it
    // holds for the property is stale from here (docs/TransactionLog.md sec
    // 23.3), and the next snapshot serialises it afresh.
    if (d->bookkeeping && d->transactionLog)
        d->transactionLog->forgetValue(*prop);

    // What a crash recovery shows before it reads anything (sec 25.2).
    if ((prop == &Label || prop == &FileName) && d->transactionLog)
        d->transactionLog->noteIdentity();

    // the Name property is a label for display purposes
    if (prop == &Label) {
        App::GetApplication().signalRelabelDocument(*this);
    } else if(prop == &ShowHidden) {
        App::GetApplication().signalShowHidden(*this);
    } else if (prop == &TransientDir) {
        // The history lives in its home's transient directory and moves
        // with it (docs/TransactionLog.md sec 27.7).
        if (d->history && d->history->home() == this)
            d->history->setDirectory(TransientDir.getStrValue());
    } else if (prop == &FileName) {
        // A version document is named after its file (`<file>@v<num>`) but
        // is not what the file is registered as (sec 27.7).
        if (d->history && !testStatus(VersionDoc))
            d->history->setPath(FileName.getStrValue());
    } else if (prop == &Uid) {
        std::string new_dir = getTransientDirectoryName(this->Uid.getValueStr(),this->FileName.getStrValue());
        std::string old_dir = this->TransientDir.getStrValue();
        Base::FileInfo TransDirNew(new_dir);
        Base::FileInfo TransDirOld(old_dir);
        // this directory should not exist
        if (!TransDirNew.exists()) {
            if (TransDirOld.exists()) {
                // The transaction log's database lives in the directory
                // and is open there already on the restore path: closed
                // across the move, reopened at the new path below.
                if (d->transactionLog)
                    d->transactionLog->closeStore();
                // Windows refuses to rename a directory with a file open in
                // it: a segment of the blob store being read or written.
                // Writes are held across the move -- after the log is
                // closed, whose worker writes here -- and a segment opens
                // again on its next read.
                std::unique_lock<std::mutex> blobWrites;
                FileBlobManager* blobs = d->history ? d->history->blobsIfMade() : nullptr;
                if (blobs) {
                    blobWrites = blobs->holdWrites();
                    blobs->closeArchives();
                }
                const bool renamed = TransDirOld.renameFile(new_dir.c_str());
                if (renamed)
                    this->TransientDir.setValue(new_dir);
                // Before the log reopens, which may wait on its worker.
                if (blobWrites)
                    blobWrites.unlock();
                if (!renamed) {
                    Base::Console().Warning("Failed to rename '%s' to '%s'\n", old_dir.c_str(), new_dir.c_str());
                    if (d->transactionLog && !d->transactionLog->reopenStore())
                        d->transactionLog.reset();
                }
                else {
                    if (d->transactionLog && !d->transactionLog->reopenStore())
                        d->transactionLog.reset();
                    // The stored files moved with the directory, so only their
                    // recorded paths are stale. Restoring an unpacked project
                    // arrives here with content already read: the blobs are
                    // copied in before Document.xml restores the Uid, and the
                    // Uid is what names the directory. Only an existing store
                    // needs this: creating one here, while the document is
                    // still being constructed, is what the manager's lazy
                    // construction avoids.
                    if (blobs)
                        blobs->relocate();
                }
            }
            else {
                if (!TransDirNew.createDirectories())
                    Base::Console().Warning("Failed to create '%s'\n", new_dir.c_str());
                else
                    this->TransientDir.setValue(new_dir);
            }
        }
        // when reloading an existing document the transient directory doesn't change
        // so we must avoid to generate a new uuid
        else if (TransDirNew.filePath() != TransDirOld.filePath()) {
            // make sure that the uuid is unique
            std::string uuid = this->Uid.getValueStr();
            Base::Uuid id;
            Base::Console().Warning("Document with the UUID '%s' already exists, change to '%s'\n",
                                    uuid.c_str(), id.getValue().c_str());
            // recursive call of onChanged()
            this->Uid.setValue(id);
        }
    } else if(prop == &UseHasher) {
        for(auto obj : d->objectArray) {
            auto geofeature = dynamic_cast<GeoFeature*>(obj);
            if(geofeature && geofeature->getPropertyOfGeometry())
                geofeature->enforceRecompute();
        }
    }
}

void Document::onBeforeChangeProperty(const TransactionalObject *Who, const Property *What)
{
    if(Who->isDerivedFrom(App::DocumentObject::getClassTypeId()))
        signalBeforeChangeObject(*static_cast<const App::DocumentObject*>(Who), *What);
    // A value served to an object under its own Restore status, in a
    // document that is not restoring: a deferred load (docs/DocumentLoad.md
    // sec 14), which runs when the value is first read. It is the restore's
    // value, not a change -- no transaction is opened for it, and one that
    // is open does not take the unserved value as what was there before
    // (docs/TransactionLog.md sec 30.8).
    if (!testStatus(Restoring)) {
        auto obj = Base::freecad_dynamic_cast<const DocumentObject>(Who);
        if (obj && obj->testStatus(ObjectStatus::Restore))
            return;
    }
    if(!d->rollback) {
        _checkTransaction(nullptr, What, __LINE__);
        if (d->activeUndoTransaction)
            d->activeUndoTransaction->addObjectChange(Who, What);
    }
}

void Document::onChangedProperty(const DocumentObject *Who, const Property *What)
{
    if (What == &Who->TreeRank) {
        if (d->treeRankRevision == d->revision) {
            long r = Who->TreeRank.getValue();
            if (r < d->treeRanks.first)
                d->treeRanks.first = r;
            else if (r > d->treeRanks.second)
                d->treeRanks.second = r;
        }
    }
    signalChangedObject(*Who, *What);
}

void Document::setTransactionMode(int iMode)
{
    d->iTransactionMode = iMode;
}

//--------------------------------------------------------------------------
// constructor
//--------------------------------------------------------------------------
Document::Document(const char* documentName)
    : myName(documentName)
{
    // Remark: In a constructor we should never increment a Python object as we cannot be sure
    // if the Python interpreter gets a reference of it. E.g. if we increment but Python don't
    // get a reference then the object wouldn't get deleted in the destructor.
    // So, we must increment only if the interpreter gets a reference.
    // Remark: We force the document Python object to own the DocumentPy instance, thus we don't
    // have to care about ref counting any more.
    d = new DocumentP;
    setStatus(Initializing, true);
    d->DocumentPythonObject = Py::Object(new DocumentPy(this), true);

#ifdef FC_LOGUPDATECHAIN
    Console().Log("+App::Document: %p\n", this);
#endif

    std::string CreationDateString = Base::TimeInfo::currentDateTimeString();
    std::string Author = DocumentParams::getprefAuthor();
    std::string AuthorComp = DocumentParams::getprefCompany();
    ADD_PROPERTY_TYPE(Label, ("Unnamed"), 0, Prop_None, "The name of the document");
    ADD_PROPERTY_TYPE(FileName,
                      (""),
                      0,
                      PropertyType(Prop_Transient | Prop_ReadOnly),
                      "The path to the file where the document is saved to");
    ADD_PROPERTY_TYPE(CreatedBy, (Author.c_str()), 0, Prop_None, "The creator of the document");
    ADD_PROPERTY_TYPE(
        CreationDate, (CreationDateString.c_str()), 0, Prop_ReadOnly, "Date of creation");
    ADD_PROPERTY_TYPE(LastModifiedBy, (""), 0, Prop_None, 0);
    ADD_PROPERTY_TYPE(LastModifiedDate, ("Unknown"), 0, Prop_ReadOnly, "Date of last modification");
    ADD_PROPERTY_TYPE(Company,
                      (AuthorComp.c_str()),
                      0,
                      Prop_None,
                      "Additional tag to save the name of the company");
    ADD_PROPERTY_TYPE(UnitSystem, (""), 0, Prop_None, "Unit system to use in this project");
    // Set up the possible enum values for the unit system
    int num = static_cast<int>(Base::UnitSystem::NumUnitSystemTypes);
    std::vector<std::string> enumValsAsVector;
    for (int i = 0; i < num; i++) {
        enumValsAsVector.emplace_back(
            Base::UnitsApi::getDescription(static_cast<Base::UnitSystem>(i)));
    }
    UnitSystem.setEnums(enumValsAsVector);
    // Get the preferences/General unit system as the default for a new document
    ParameterGrp::handle hGrpu = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Units");
    UnitSystem.setValue(hGrpu->GetInt("UserSchema", 0));
    ADD_PROPERTY_TYPE(Comment, (""), 0, Prop_None, "Additional tag to save a comment");
    ADD_PROPERTY_TYPE(Meta, (), 0, Prop_None, "Map with additional meta information");
    ADD_PROPERTY_TYPE(Material, (), 0, Prop_None, "Map with material properties");
    // create the uuid for the document
    Base::Uuid id;
    ADD_PROPERTY_TYPE(Id, (""), 0, Prop_None, "ID of the document");
    ADD_PROPERTY_TYPE(Uid, (id), 0, Prop_ReadOnly, "UUID of the document");

    ADD_PROPERTY_TYPE(SaveThumbnail, (DocumentParams::getSaveThumbnail()), 0, Prop_None,
            "Whether to auto update thumbnail on saving the document");
    ADD_PROPERTY_TYPE(ThumbnailFile, (""), 0, Prop_None,
            "User defined thumnail file. The thumnail will be saved into the\n"
            "document file. It will only be updated oncei when the user changes\n"
            "this property. An non-empty value of this property will also disable\n"
            "thumbnail auto update regardless of setting in SaveThumbnail.");
    ThumbnailFile.setFilter("Image files (*.jpg *.jpeg *.png *.bmp *.gif);;All files (*)");

    // license stuff
    auto index = static_cast<int>(DocumentParams::getprefLicenseType());
    const char* name = "";
    const char* url = "";
    std::string licenseUrl = "";
    if (index >= 0 && index < App::countOfLicenses) {
        name = App::licenseItems.at(index).at(App::posnOfFullName);
        url = App::licenseItems.at(index).at(App::posnOfUrl);
        if(DocumentParams::getprefLicenseUrl().empty()) {
            licenseUrl = DocumentParams::getprefLicenseUrl();
        } else if (url) {
            licenseUrl = url;
        }
    }
    ADD_PROPERTY_TYPE(License, (name), 0, Prop_None, "License string of the Item");
    ADD_PROPERTY_TYPE(
        LicenseURL, (licenseUrl.c_str()), 0, Prop_None, "URL to the license text/contract");
    ADD_PROPERTY_TYPE(ShowHidden,
                      (false),
                      0,
                      PropertyType(Prop_None),
                      "Whether to show hidden object items in the tree view");
    ADD_PROPERTY_TYPE(UseHasher,(true), 0,PropertyType(Prop_Hidden), 
                        "Whether to use hasher on topological naming");
    if(!DocumentParams::getUseHasher())
        UseHasher.setValue(false);

    // this creates and sets 'TransientDir' in onChanged()
    ADD_PROPERTY_TYPE(TransientDir,
                      (""),
                      0,
                      PropertyType(Prop_Transient | Prop_ReadOnly),
                      "Transient directory, where the files live while the document is open");
    ADD_PROPERTY_TYPE(
        Tip, (nullptr), 0, PropertyType(Prop_Transient), "Link of the tip object of the document");
    ADD_PROPERTY_TYPE(TipName,
                      (""),
                      0,
                      PropertyType(Prop_Hidden | Prop_ReadOnly),
                      "Link of the tip object of the document");
    Uid.touch();

    ADD_PROPERTY_TYPE(ForceXML,(3),"Format", Prop_None,
            "Preference of storing data as XML.\n"
            "Higher number means stronger preference.\n"
            "Only effective when saving document in directory.");
    ForceXML.setValue(DocumentParams::getForceXML());
    ADD_PROPERTY_TYPE(SplitXML,(true),"Format",Prop_None,
            "Save object data in separate XML file.\n"
            "Only effective when saving document in directory.");
    SplitXML.setValue(DocumentParams::getSplitXML());
    ADD_PROPERTY_TYPE(PreferBinary,(false),"Format",Prop_None,
            "Prefer binary format when saving object data.\n"
            "This can result in smaller file but bad for version control.");
    PreferBinary.setValue(DocumentParams::getPreferBinary());
    // getCurrentSchemaVersion(), i.e. 5: a document created here is this
    // fork's own format (user ruling 2026-08-20). What that costs is stated
    // rather than inherited -- Gui::Document warns explicitly, once, before
    // the first compact save of a file, and the choice is per document from
    // then on. A document restored from a file in an older format takes
    // this one too, and says so (sec 27.62 of docs/TransactionLog.md); see
    // Restore().
    ADD_PROPERTY_TYPE(SaveSchemaVersion,(getCurrentSchemaVersion()),"Format",Prop_None,
            "Document schema version to write.\n"
            "5 is this fork's compact format and the default for a new\n"
            "document: smaller and faster to load, but readable only by\n"
            "builds of this fork that know it -- no other FreeCAD, upstream\n"
            "included, will open the file. Lower it to 4, upstream's format,\n"
            "to keep the document readable everywhere. Only versions this\n"
            "build can still write are accepted.");
    {
        const auto &versions = getWritableSchemaVersions();
        static App::PropertyIntegerConstraint::Constraints schemaRange;
        schemaRange.LowerBound = versions.front();
        schemaRange.UpperBound = versions.back();
        schemaRange.StepSize = 1;
        SaveSchemaVersion.setConstraints(&schemaRange);
    }
}

Document::~Document()
{
#ifdef FC_LOGUPDATECHAIN
    Console().Log("-App::Document: %s %p\n",getName(), this);
#endif

    try {
        clearUndos();
    }
    catch (const boost::exception&) {
    }

#ifdef FC_LOGUPDATECHAIN
    Console().Log("-Delete Features of %s \n",getName());
#endif

    d->clearDocument();

    // Remark: The API of Py::Object has been changed to set whether the wrapper owns the passed
    // Python object or not. In the constructor we forced the wrapper to own the object so we need
    // not to dec'ref the Python object any more.
    // But we must still invalidate the Python object because it doesn't need to be
    // destructed right now because the interpreter can own several references to it.
    Base::PyGILStateLocker lock;
    Base::PyObjectBase* doc = static_cast<Base::PyObjectBase*>(d->DocumentPythonObject.ptr());
    // Call before decrementing the reference counter, otherwise a heap error can occur
    doc->setInvalid();

    // remove Transient directory
    try {
        // The log first: its worker writes into the store and the blob
        // segments until its queue is empty, which since every commit
        // writes its values (docs/TransactionLog.md sec 25.4) is often not
        // yet the case here. Nothing makes another one afterwards.
        d->noLog = true;
        d->transactionLog.reset();
        // The history removes its directory, this one's transient directory
        // when this is its home, once the last document of the file lets go
        // (docs/TransactionLog.md sec 27.7). A directory of this document's
        // own goes now.
        bool shared = false;
        if (d->history) {
            d->history->releaseHome(*this);
            shared = d->history->directory() == TransientDir.getStrValue();
            d->history.reset();
        }
        if (!shared) {
            Base::FileInfo TransDir(TransientDir.getValue());
            TransDir.deleteDirectoryRecursive();
        }
    }
    catch (const Base::Exception& e) {
        std::cerr << "Removing transient directory failed: " << e.what() << std::endl;
    }
    delete d;
}

std::string Document::getTransientDirectoryName(const std::string& uuid, const std::string& filename) const
{
    // Create a directory name of the form: {ExeName}_Doc_{UUID}_{HASH}_{PID}
    std::stringstream s;
    QCryptographicHash hash(QCryptographicHash::Sha1);
#if QT_VERSION < QT_VERSION_CHECK(6,3,0)
    hash.addData(filename.c_str(), filename.size());
#else
    hash.addData(QByteArrayView(filename.c_str(), filename.size()));
#endif
    s << App::Application::getUserCachePath() << App::Application::getExecutableName()
      << "_Doc_" << uuid
      << "_" << hash.result().toHex().left(6).constData()
      << "_" << QCoreApplication::applicationPid();
    return s.str();
}

//--------------------------------------------------------------------------
// Exported functions
//--------------------------------------------------------------------------

// Newest schema version this build writes. Every entry of
// getWritableSchemaVersions() is a shape the writer can still produce.
#define FC_DOC_SCHEMA_VER 5

// Root element of a document written at schema 5 or later -- one that may
// share class defaults. The new name is the format's incompatibility made
// loud: no released reader, this fork's or upstream's, checks a schema
// number before reading, but every one of them scans for <Document>, reaches
// the end of the stream without finding it, and throws. The alternative was
// each of them opening the file and silently reverting every elided property
// to its own build's defaults. Keep the name stable from here on -- the
// SchemaVersion attribute carries versioning, the name only says "not for
// readers that predate it".
#define FC_ELEM_FCDOCUMENT "FCDocument"

void Document::Save (Base::Writer &writer) const
{
    d->hashers.clear();
    addStringHasher(d->Hasher);

    // Not every caller comes through save(): the content dump streams a
    // document through a writer nothing has resolved a schema onto, and a
    // writer's own default is 0 -- which a reader would take for a
    // pre-schema file and restore no objects from. Whoever asks this
    // document to write itself gets the document's resolved answer.
    if (writer.getSchemaVersion() <= 0)
        writer.setSchemaVersion(resolveSchemaVersion(writer));

    // The writer's schema is the resolved outcome (resolveSchemaVersion),
    // and the root element states it twice: once as the attribute, and at 5
    // or later as its own name.
    writer.Stream() << '<'
                    << (writer.getSchemaVersion() >= 5 ? FC_ELEM_FCDOCUMENT : "Document")
                    << " SchemaVersion=\"" << writer.getSchemaVersion()
                    << "\" ProgramVersion=\""
                    << App::Application::Config()["BuildVersionMajor"] << "."
                    << App::Application::Config()["BuildVersionMinor"] << "R"
                    << App::Application::Config()["BuildRevision"]
                    << "\" FileVersion=\"" << writer.getFileVersion()
                    << "\" Uid=\"" << Uid.getValueStr()
                    << "\" StringHasher=\"1\"";
    if (d->lastObjectId > 0)
        writer.Stream() << " LastId=\"" << d->lastObjectId << "\"";
    // Announced the same way the string hasher is, because the reader has to
    // know whether the element is there before it can read past it.
    if (getFileBlobManager().hasInlineBlobs())
        writer.Stream() << " Blobs=\"1\"";
    writer.Stream() << ">\n";

    writer.incInd();

    // The included files, ahead of everything that can refer to them -- the
    // document's own properties included. Writes nothing unless this save was
    // asked for pure XML; otherwise they are archive entries of their own.
    getFileBlobManager().writeInlineBlobs(writer);

    // NOTE: DO NOT save the main string hasher as a file the reader serves
    // after the XML, because it is required by many objects, which assume
    // the string hasher is fully restored. The member of a schema-5 file
    // (docs/TransactionLog.md sec 27.50) is read at its element instead.
    d->Hasher->setPersistenceFileName(nullptr);

    // Two passes, and the order between them is load-bearing: every object
    // settles its own properties first (a shape parked by the deferred
    // restore is pulled back in here), and only then do the document's own
    // properties run -- which is where a document-wide store collects what
    // the objects have just made ready (docs/SharedShapeStorage.md).
    auto saveClock = std::chrono::steady_clock::now();
    auto saveSplit = [&saveClock]() {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - saveClock).count();
        saveClock = now;
        return secs;
    };
    for (auto o : d->objectArray) {
        if (d->saveSeq) {
            d->saveSeq->next();
        }
        o->beforeSave(writer);
    }
    beforeSave(writer);
    const double tBefore = saveSplit();

    if (d->tableAsMember)
        const_cast<Document*>(this)->_saveStringTable(writer);
    else
        d->Hasher->Save(writer);
    const double tHasher = saveSplit();

    writer.decInd();

    PropertyContainer::Save(writer);
    const double tOwn = saveSplit();
    FC_LOG("save " << getName() << ": beforeSave " << tBefore << "s, hasher "
            << tHasher << "s, document properties " << tOwn << "s");

    // writing the features types
    writeObjects(d->objectArray, writer);
}

void Document::Restore(Base::XMLReader &reader)
{
    int i,Cnt;
    d->hashers.clear();
    d->touchedObjs.clear();
    addStringHasher(d->Hasher);

    Base::ReaderContext rctx(getName());

    setStatus(Document::PartialDoc,false);

    // Either root: <Document> as ever, or the <FCDocument> a default-sharing
    // file announces itself with. The new name exists to be unreadable by
    // builds that predate it -- this build reads both in full, and anything
    // else is not a FreeCAD document.
    reader.readElement();
    if (strcmp(reader.localName(), "Document") != 0
            && strcmp(reader.localName(), FC_ELEM_FCDOCUMENT) != 0)
        THROWM(Base::XMLParseException, "Not a FreeCAD document")
    long scheme = reader.getAttributeAsInteger("SchemaVersion");
    reader.DocumentSchema = scheme;
    if (reader.hasAttribute("ProgramVersion")) {
        reader.ProgramVersion = reader.getAttribute("ProgramVersion");
    } else {
        reader.ProgramVersion = "pre-0.14";
    }
    if (reader.hasAttribute("FileVersion")) {
        reader.FileVersion = reader.getAttributeAsUnsigned("FileVersion");
    } else {
        reader.FileVersion = 0;
    }

    if (reader.hasAttribute("Uid"))
        Uid.setValue(reader.getAttribute("Uid"));

    // Both flags belong to the document element, and reading an element of
    // its own below replaces the attributes -- so take them while they are
    // still there.
    const bool hasInlineBlobs = reader.hasAttribute("Blobs");
    const bool hasStringHasher = reader.hasAttribute("StringHasher");
    // The last id the file handed out (sec 27.40 item 1), above every object
    // it holds when the newest ones were deleted: none is handed out again.
    const long savedLastId = reader.hasAttribute("LastId")
        ? static_cast<long>(reader.getAttributeAsInteger("LastId")) : 0;

    // Content carried inside the XML comes first, so everything parsed from
    // here on finds what it refers to already in the store. The Uid is set
    // above, which means the transient directory it names is already the
    // final one.
    if (hasInlineBlobs)
        getFileBlobManager().restoreInlineBlobs(reader);

    // The file's hasher, which other documents of the file may be using
    // (sec 27.40 item 2), is never cleared: the table read joins it, unless
    // an id there means something else -- history written before the
    // hasher was the file's -- and then the document keeps what it read.
    const bool shared = d->history && d->Hasher == d->history->hasher()
                        && d->Hasher->size() > 0;
    if (hasStringHasher) {
        Base::ReaderContext rctx("StringHasher");
        const auto hasherStart = std::chrono::steady_clock::now();
        struct HasherTime
        {
            const Document* doc;
            std::chrono::steady_clock::time_point start;
            ~HasherTime()
            {
                FC_LOG(doc->getName() << ": string table read in "
                       << std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                              .count()
                       << " s");
            }
        } hasherTime {this, hasherStart};
        StringHasherRef read = shared ? StringHasherRef(new StringHasher) : d->Hasher;
        read->Restore(reader);
        // Named, not held: the file's member, or the log's table (sec 27.50).
        if (!read->tableFile().empty())
            _restoreStringTable(reader, *read);
        if (shared && read->size() > 0) {
            std::size_t aliased = 0;
            if (d->Hasher->merge(*read, &aliased)) {
                if (aliased)
                    FC_LOG(getName() << ": " << aliased
                           << " string(s) under a second id in the file's hasher");
            }
            else {
                FC_LOG(getName() << ": string table disagrees with the file's; kept apart");
                d->Hasher = read;
            }
        }
    } else if (!shared) {
        d->Hasher->clear();
    }

    // When this document was created the FileName and Label properties
    // were set to the absolute path or file name, respectively. To save
    // the document to the file it was loaded from or to show the file name
    // in the tree view we must restore them after loading the file because
    // they will be overridden.
    // Note: This does not affect the internal name of the document in any way
    // that is kept in Application.
    std::string FilePath = FileName.getValue();
    std::string DocLabel = Label.getValue();

    // read the Document Properties, when reading in Uid the transient directory gets renamed automatically
    PropertyContainer::Restore(reader);

    // We must restore the correct 'FileName' property again because the stored
    // value could be invalid.
    FileName.setValue(FilePath.c_str());
    Label.setValue(DocLabel.c_str());

    // A file in an older format is this fork's own format from here on (user,
    // 2026-09-28, docs/TransactionLog.md sec 27.62): the next plain Save
    // writes the current schema, and the transaction log holds nothing else.
    // The user is told once, at the open; Save As with the standard format
    // still writes the old one for upstream. Whatever the file says it was
    // -- the schema it was written in, or a SaveSchemaVersion it states.
    const long current = getCurrentSchemaVersion();
    const long was = std::min<long>(scheme > 0 ? scheme : current, SaveSchemaVersion.getValue());
    if (was < current) {
        SaveSchemaVersion.setValue(current);
        if (!d->checkingOut && !testStatus(VersionDoc) && !testStatus(Importing))
            Base::Console().warning(DocLabel,
                "'%s' is in an older file format (schema %ld) and will be saved in the "
                "current one (schema %ld), which only this FreeCAD reads. To keep a copy "
                "other FreeCAD versions can open, use Save As and choose the standard "
                "format.\n",
                DocLabel.c_str(), was, current);
    }

    // SchemeVersion "2"
    if ( scheme == 2 ) {
        // read the feature types
        reader.readElement("Features");
        Cnt = reader.getAttributeAsInteger("Count");
        for (i=0 ;i<Cnt ;i++) {
            reader.readElement("Feature");
            string type = reader.getAttribute("type");
            string name = reader.getAttribute("name");
            try {
                addObject(type.c_str(), name.c_str(), /*isNew=*/ false);
            }
            catch ( Base::Exception& ) {
                Base::Console().Message("Cannot create object '%s'\n", name.c_str());
            }
        }
        reader.readEndElement("Features");

        // read the features itself
        reader.readElement("FeatureData");
        Cnt = reader.getAttributeAsInteger("Count");
        for (i=0 ;i<Cnt ;i++) {
            reader.readElement("Feature");
            string name = reader.getAttribute("name");
            Base::ReaderContext rctx(name);
            DocumentObject* pObj = getObject(name.c_str());
            if (pObj) { // check if this feature has been registered
                pObj->setStatus(ObjectStatus::Restore, true);
                pObj->Restore(reader);
                pObj->setStatus(ObjectStatus::Restore, false);
            }
            reader.readEndElement("Feature");
        }
        reader.readEndElement("FeatureData");
    } // SchemeVersion "3" or higher
    else if ( scheme >= 3 ) {
        // read the feature types
        readObjects(reader);
        d->noteObjectId(savedLastId);

        // tip object handling. First the whole document has to be read, then we
        // can restore the Tip link out of the TipName Property:
        Tip.setValue(getObject(TipName.getValue()));
    }

    // Nameless on purpose: the next end element is the root's own, whichever
    // of the two roots this file used.
    reader.readEndElement();
}

const char* Document::stringTableName()
{
    return "StringTable.txt";
}

namespace {

/// The bytes of the file's string table, as the writer asks for the member
/// after Document.xml (docs/TransactionLog.md sec 27.50 item 1).
class StringTableMember: public Base::Persistence
{
public:
    explicit StringTableMember(std::string bytes)
        : _bytes(std::move(bytes))
    {}
    unsigned int getMemSize() const override
    {
        return static_cast<unsigned int>(_bytes.size());
    }
    void Save(Base::Writer& /*writer*/) const override
    {}
    void Restore(Base::XMLReader& /*reader*/) override
    {}
    void SaveDocFile(Base::Writer& writer) const override
    {
        writer.Stream().write(_bytes.data(), static_cast<std::streamsize>(_bytes.size()));
    }

private:
    std::string _bytes;
};

/// The member `name` of the archive or directory `reader` reads, open for
/// reading ahead of its turn; null when there is none.
std::unique_ptr<std::istream> openMember(Base::XMLReader& reader, const std::string& name)
{
    Base::Reader* source = reader.getReader();
    if (!source)
        return {};
    if (auto zip = dynamic_cast<Base::ZipFileReader*>(source))
        return zip->hasEntry(name) ? zip->openEntry(name) : nullptr;
    const std::string dir = source->getDirectory();
    if (!dir.empty()) {
        Base::FileInfo fi(dir + "/" + name);
        if (!fi.exists())
            return {};
        return std::make_unique<Base::ifstream>(fi, std::ios::in | std::ios::binary);
    }
    // The forward-only reader: the same archive again, through its index.
    try {
        Base::ZipFileReader zip(source->getFileName());
        if (zip.hasEntry(name))
            return zip.openEntry(name);
    }
    catch (...) {
    }
    return {};
}

} // namespace

void Document::_saveStringTable(Base::Writer& writer)
{
    // docs/TransactionLog.md sec 27.50, 27.51. A save compacts first (Q3):
    // what memory holds, and every string a retained version or value of
    // the log uses, stays. A snapshot is no save and drops nothing.
    if (!d->snapshotting)
        _compactStrings();
    // The ids this version uses: the marks the pass before this one set
    // (item 4) -- for the log, which records the version, and in the file,
    // for the version a later open of it records.
    const std::vector<long> used = d->Hasher->markedIDs();
    if (TransactionLog* log = getTransactionLog())
        log->noteVersionStrings(used);
    if (d->snapshotting) {
        // A version the log keeps: the log's table, not a copy of it (item 2)
        // -- named as a save names it, so that its Document.xml is the bytes
        // a save writes (sec 23.3).
        d->Hasher->saveReference(writer, stringTableName(), d->Hasher->contentHash(),
                                 d->Hasher->size(), used);
        return;
    }
    std::string bytes = d->Hasher->saveTable();
    const std::string hash = d->Hasher->contentHash();
    auto member = std::make_unique<StringTableMember>(std::move(bytes));
    d->tableEntry = writer.addFile(stringTableName(), member.get());
    d->tableMember = std::move(member);
    d->Hasher->saveReference(writer, d->tableEntry, hash, d->Hasher->size(), used);
    // The saved file's table is in memory: a second open of it reads none.
    if (d->history && d->history->hasher() == d->Hasher)
        d->history->noteTable(hash);
}

void Document::_restoreStringTable(Base::XMLReader& reader, StringHasher& read)
{
    const std::string file = read.tableFile();
    const std::string hash = read.tableHash();
    const bool fileHasher = d->history && d->history->hasher() == d->Hasher;
    // From the log -- a checkout, a version, a recovery: no version carries
    // a table, and the log's is the file's (item 2).
    if (d->checkingOut && fileHasher && d->history->logCore()) {
        d->history->logCore()->loadStrings();
        return;
    }
    // A table this file's history has taken in already (item 3): the
    // second open of the same file.
    if (!hash.empty() && fileHasher && &read != d->Hasher.get() && d->history->tookTable(hash)) {
        FC_LOG(getName() << ": string table " << hash << " is in memory already");
        return;
    }
    std::unique_ptr<std::istream> in = openMember(reader, file);
    if (!in) {
        FC_ERR(getName() << ": the string table " << file << " is missing. It is strongly "
               "recommended to recompute the whole document.");
        reader.setPartialRestore(true);
        return;
    }
    try {
        read.restoreTable(*in);
    }
    catch (const Base::Exception& e) {
        e.ReportException();
        FC_ERR(getName() << ": the string table " << file << " cannot be read. It is strongly "
               "recommended to recompute the whole document.");
        reader.setPartialRestore(true);
        return;
    }
    if (!hash.empty() && d->history)
        d->history->noteTable(hash);
}

std::size_t Document::_compactStrings()
{
    // docs/TransactionLog.md sec 27.50 item 4, 27.51 Q3 and Q4: a string
    // stays while memory holds it, or a retained version or value uses it
    // (the ranges beside each in the store) -- and what a kept one is built
    // from stays with it. Only the file's hasher is the log's to judge.
    const StringHasherRef hasher = d->Hasher;
    if (!hasher || hasher->getSaveAll())
        return 0;
    std::vector<std::pair<long, long>> kept;
    TransactionLogCore* core = nullptr;
    // Whichever document of the file keeps the log: its versions and values
    // are this hasher's too.
    if (d->history && d->history->hasher() == hasher && d->history->logCore()) {
        core = d->history->logCore().get();
        kept = core->retainedStrings();
    }
    auto keep = [&kept](long id) {
        auto it = std::upper_bound(kept.begin(), kept.end(), id,
                                   [](long v, const std::pair<long, long>& r) { return v < r.first; });
        return it != kept.begin() && std::prev(it)->second >= id;
    };
    const std::vector<long> dropped = hasher->compact(keep);
    if (core)
        core->dropStrings(dropped);
    if (!dropped.empty())
        FC_LOG(getName() << ": " << dropped.size() << " string(s) dropped, " << hasher->size()
               << " kept");
    return dropped.size();
}

std::pair<bool,int> Document::addStringHasher(const StringHasherRef & hasher) const {
    if (!hasher)
        return std::make_pair(false, 0);
    auto ret = d->hashers.left.insert(HasherMap::left_map::value_type(hasher,(int)d->hashers.size()));
    if (ret.second)
        hasher->clearMarks();
    return std::make_pair(ret.second,ret.first->second);
}

StringHasherRef Document::getHasher() const {
    return d->Hasher;
}

std::string Document::externalTagPostfix(const StringHasherRef &hasher) const
{
    std::string res = Data::externalTagPostfix();
    if (hasher) {
        // Held by what the marker goes into (TopoShape::copyElementMap).
        res += hasher->getID(Uid.getValueStr().c_str()).toString();
    }
    return res;
}

StringHasherRef Document::getStringHasher(int idx) const {
    StringHasherRef hasher;
    if(idx<0) {
        if(UseHasher.getValue())
            return d->Hasher;
        return hasher;
    }

    auto it = d->hashers.right.find(idx);
    if(it == d->hashers.right.end()) {
        hasher = new StringHasher;
        d->hashers.right.insert(HasherMap::right_map::value_type(idx,hasher));
    }else
        hasher = it->second;
    return hasher;
}

struct DocExportStatus {
    Document::ExportStatus status;
    std::set<const App::DocumentObject*> objs;
};

static DocExportStatus _ExportStatus;

// Exception-safe exporting status setter
class DocumentExporting {
public:
    explicit DocumentExporting(const std::vector<App::DocumentObject*> &objs) {
        _ExportStatus.status = Document::Exporting;
        _ExportStatus.objs.insert(objs.begin(),objs.end());
    }

    ~DocumentExporting() {
        _ExportStatus.status = Document::NotExporting;
        _ExportStatus.objs.clear();
    }
};

// The current implementation choose to use a static variable for exporting
// status because we can be exporting multiple objects from multiple documents
// at the same time. I see no benefits in distinguish which documents are
// exporting, so just use a static variable for global status. But the
// implementation can easily be changed here if necessary.
Document::ExportStatus Document::isExporting(const App::DocumentObject *obj) const {
    if(_ExportStatus.status!=Document::NotExporting &&
       (!obj || _ExportStatus.objs.find(obj)!=_ExportStatus.objs.end()))
        return _ExportStatus.status;
    return Document::NotExporting;
}

void Document::exportObjects(const std::vector<App::DocumentObject*>& obj, std::ostream& out) {

    DocumentExporting exporting(obj);
    d->hashers.clear();

    if(FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
        for(auto o : obj) {
            if(o && o->isAttachedToDocument()) {
                FC_LOG("exporting " << o->getFullName());
                if (!o->getPropertyByName("_ObjectUUID")) {
                    auto prop = static_cast<PropertyUUID*>(o->addDynamicProperty(
                            "App::PropertyUUID", "_ObjectUUID", nullptr, nullptr,
                            Prop_Output | Prop_Hidden));
                    prop->setValue(Base::Uuid::createUuid());
                }
            }
        }
    }

    Base::ZipWriter writer(out);
    // An exported fragment never shares anything: it is small, it travels
    // (clipboard, merge), and a reader that merges it may be anything. Cap
    // at 4, which also keeps buildDefaults' schema gate closed and the
    // <Document> root a fragment has always had.
    writer.setSchemaVersion(std::min<long>(getSaveSchemaVersion(), 4));
    // Only the exported objects' files: a clipboard buffer has no business
    // carrying content belonging to the rest of the document.
    getFileBlobManager().beginSave(writer);
    collectFileBlobs(obj);

    writer.putNextEntry("Document.xml");
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n";
    writer.Stream() << R"(<Document SchemaVersion=")" << writer.getSchemaVersion()
                        << R"(" ProgramVersion=")"
                        << App::Application::Config()["BuildVersionMajor"] << "."
                        << App::Application::Config()["BuildVersionMinor"] << "R"
                        << App::Application::Config()["BuildRevision"]
                        << R"(" FileVersion="1">\n)";
    // Add this block to have the same layout as for normal documents
    writer.Stream() << "<Properties Count=\"0\">\n";
    writer.Stream() << "</Properties>\n";

    // writing the object types
    writeObjects(obj, writer);

    // The exported objects' included files, before anything the file channel
    // adds -- same layout as a document save.
    getFileBlobManager().writeBlobs(writer);

    // Hook for others to add further data.
    signalExportObjects(obj, writer);

    // write additional files
    writer.writeFiles();
    d->hashers.clear();
}

#define FC_ATTR_DEPENDENCIES "Dependencies"
#define FC_ELEMENT_OBJECT_DEPS "ObjectDeps"
#define FC_ATTR_DEP_COUNT "Count"
#define FC_ATTR_DEP_OBJ_NAME "Name"
#define FC_ATTR_DEP_ALLOW_PARTIAL "AllowPartial"
#define FC_ELEMENT_OBJECT_DEP "Dep"

namespace {

// The element names of the shared default block, and the attribute on
// <ObjectData> that says how many entries it has. A reader that finds no
// attribute never looks for the block, which is what lets a file written
// without one be read by the same code.
const char *FC_ELEM_DEFAULTS = "Defaults";
const char *FC_ELEM_DEFAULT = "Default";
const char *FC_ATTR_DEFAULTS = "Defaults";

/** Build an object of the given class outside any document.
 *
 * What a class treats as a default lives in its constructor and nowhere
 * else -- there is no metadata to ask -- so the only way to find out is to
 * build one and read its properties. Returns null for a class that cannot be
 * instantiated, and the caller then writes everything as before.
 */
std::unique_ptr<DocumentObject> makeDefaultObject(const char *typeName)
{
    auto type = Base::Type::fromName(typeName);
    if (type.isBad() || !type.isDerivedFrom(DocumentObject::getClassTypeId()))
        return {};
    std::unique_ptr<DocumentObject> res;
    try {
        res.reset(static_cast<DocumentObject*>(type.createInstance()));
    }
    catch (Base::Exception &e) {
        e.ReportException();
    }
    catch (const std::exception &e) {
        FC_ERR("Failed to build a default " << typeName << ": " << e.what());
    }
    if (!res)
        FC_LOG("No default object for " << typeName);

    return res;
}

// Raised while a <Defaults> block is being read into a stand-in, and
// consulted by Property::touch(). A counter rather than a bool so the App
// and Gui readers cannot un-say each other. See
// Document::isRestoringDefaults().
int _RestoringDefaults;

} // anonymous namespace

bool Document::isRestoringDefaults()
{
    return _RestoringDefaults > 0;
}

Document::RestoringDefaultsGuard::RestoringDefaultsGuard()
{
    ++_RestoringDefaults;
}

Document::RestoringDefaultsGuard::~RestoringDefaultsGuard()
{
    --_RestoringDefaults;
}

Document::RestoringScopeGuard::RestoringScopeGuard()
    : toggled(!globalIsRestoring)
{
    globalIsRestoring = true;
}

Document::RestoringScopeGuard::~RestoringScopeGuard()
{
    if (toggled)
        globalIsRestoring = false;
}

namespace
{
// Global rather than per document: a command is one action of the user, and
// what it may not do is change ANY document that is still filling -- not only
// the one that happened to be active when it started.
bool s_userEditing = false;
}  // namespace

Document::UserEditGuard::UserEditGuard()
    : toggled(!s_userEditing)
{
    if (toggled) {
        s_userEditing = true;
    }
}

Document::UserEditGuard::~UserEditGuard()
{
    if (toggled) {
        s_userEditing = false;
    }
}

bool Document::isUserEditing()
{
    return s_userEditing;
}

void Document::checkUserEdit(const Document *doc, const DocumentObject *obj,
                             const Property *prop)
{
    if (!s_userEditing || !doc || !doc->testStatus(Status::LiveImport)) {
        return;
    }
    // Undoing, redoing or rolling back is the machinery taking an edit AWAY,
    // not a command making one -- and refusing it is worse than useless: a
    // command that hits this check unwinds through its transaction's own
    // rollback, and a throw during stack unwinding terminates the process.
    if (doc->isPerformingTransaction()) {
        return;
    }
    // Showing and hiding is looking, not editing, and it is the one thing a
    // user reaches for while watching a model arrive. There are two
    // Visibility properties -- this one and the view provider's -- and the
    // view provider's write lands here too, because it mirrors itself onto
    // the object. Exempted by identity rather than by its Output status:
    // Shape carries Output as well, and assigning a shape IS an edit.
    if (obj && prop == &obj->Visibility) {
        return;
    }
    // The tree view's own ordering bookkeeping. TreeRank is written by
    // the tree as it populates, from its own timer, never by a command --
    // it reaches this check only when a command runs a nested event loop
    // (the animated view fit the import itself runs while the load is
    // still live, a modal dialog) and the timer fires inside that
    // command's scope. Refusing it unwinds the tree mid-populate and
    // leaves a root item that was never inserted, which the next tick
    // dereferences (SIGSEGV in DocumentObjectItem::getParentItem, the
    // chess-flat render golden under load, 2026-09-05).
    if (obj && prop == &obj->TreeRank) {
        return;
    }
    // The object's mirror of its view provider. A view provider property
    // write touches ViewObject so the document notices presentation
    // changing (ViewProviderDocumentObject::onChanged), and presentation
    // is exactly what the live view exists to keep usable: the origin
    // group's timer resizing its origin, a colour, a display mode. This is
    // the only route by which a view provider property reaches this
    // check, and it is exempted by identity like the two above (found by
    // tests/gui/live-import-nested-loop.py: the origin resize fired
    // inside the nested loop and aborted the command, 2026-09-06).
    if (obj && prop == &obj->ViewObject) {
        return;
    }
    // Named as precisely as the caller knew, because the whole point is that
    // the command did not say what it was going to do -- so the report has to.
    std::ostringstream str;
    str << "The document '" << doc->getName() << "' is still being filled in, and a"
           " command may not change it until that finishes. Stopped at ";
    if (obj && prop && prop->getName()) {
        str << obj->getNameInDocument() << '.' << prop->getName();
    }
    else if (obj) {
        str << obj->getNameInDocument();
    }
    else {
        str << "a change to the document";
    }
    str << ". Looking, selecting and moving the camera keep working.";
    // Said here, where the object and property are still known -- the command
    // itself could not have said it, which is the whole reason for this check.
    Base::Console().warning("%s\n", str.str().c_str());
    // AbortException rather than a plain error: Command::_invoke() already
    // treats it as "this operation stopped, and has explained itself", so it
    // unwinds without the modal dialog a Base::Exception would raise -- and a
    // modal dialog is exactly what a user clicking through a live load must
    // not be given.
    throw Base::AbortException(str.str().c_str());
}

Document::RestoreDrainGuard::RestoreDrainGuard(Document *doc)
    : doc(doc)
    , toggled(doc && !doc->testStatus(Status::RestoreDrain))
{
    if (toggled)
        doc->setStatus(Status::RestoreDrain, true);
}

Document::RestoreDrainGuard::~RestoreDrainGuard()
{
    if (toggled)
        doc->setStatus(Status::RestoreDrain, false);
}

void Document::reportRestoreDrainChange(const DocumentObject *obj, const Property *prop)
{
    // An object that is still restoring is not a finding: that is the load's
    // own work arriving late, and it already keeps this promise its own way
    // -- restoreDeferredFile() serves a parked archive entry with the owner's
    // touch saved and put back. What the report is for is the handler that
    // writes back on a *render*, and it is worth nothing if the expected
    // writes crowd the unexpected ones out of it.
    if (obj->isRestoring())
        return;
    ++d->drainReport.count;
    // The count is the measurement; the names are there to point at the
    // handler, and one of each is enough for that.
    if (d->drainReport.truncated)
        return;
    std::string name = obj->getFullName();
    name += '.';
    name += prop && prop->getName() ? prop->getName() : "touch()";
    if (std::find(d->drainReport.names.begin(), d->drainReport.names.end(), name)
            != d->drainReport.names.end())
        return;
    if (d->drainReport.names.size() >= 10)
        d->drainReport.truncated = true;
    else
        d->drainReport.names.push_back(std::move(name));
}

const Document::RestoreDrainReport &Document::getRestoreDrainReport() const
{
    return d->drainReport;
}

void Document::clearRestoreDrainReport()
{
    d->drainReport = RestoreDrainReport();
}

void Document::buildDefaults(Base::Writer &writer,
        const std::vector<App::DocumentObject*>& obj,
        std::map<std::string, SharedDefaults> &defaults) const
{
    // One gate, and it is the document's own: the resolved schema. Five is
    // the version that introduced the block (getWritableSchemaVersions),
    // the user chooses it per document in the save dialog, and a document
    // that resolved lower has asked to come out in a shape an older FreeCAD
    // reads in full. No preference outranks what a document promised.
    if (writer.getSchemaVersion() < 5)
        return;

    // A default block is one class's whole property set, so it only pays for
    // itself once enough objects can leave that set out. Two roughly break
    // even; below that a document would come out larger than if nothing had
    // been shared at all.
    const std::size_t minInstances = 3;

    std::map<std::string, std::size_t> counts;
    for (auto o : obj)
        ++counts[o->getTypeId().getName()];
    for (const auto &v : counts) {
        if (v.second < minInstances)
            continue;
        // The stand-in lives exactly as long as this recording. What the
        // objects compare against, and what the file will carry, are the
        // bytes SharedDefaults took down -- the object itself has nothing
        // more to say once they are recorded.
        if (auto proto = makeDefaultObject(v.first.c_str())) {
            SharedDefaults record;
            record.build(*proto, writer);
            if (!record.empty())
                defaults.emplace(v.first, std::move(record));
        }
    }
}

void Document::saveDefaults(Base::Writer &writer,
        const std::map<std::string, SharedDefaults> &defaults) const
{
    if (defaults.empty())
        return;

    writer.Stream() << writer.ind() << '<' << FC_ELEM_DEFAULTS << " Count=\""
                    << defaults.size() << "\">\n";
    writer.incInd();
    for (const auto &v : defaults) {
        writer.Stream() << writer.ind() << '<' << FC_ELEM_DEFAULT << " type=\""
                        << v.first << "\">\n";
        // Properties only, and only the recorded ones: eligibility was
        // settled when the record was built, and the bytes going out here
        // are the same bytes every elision was decided against.
        v.second.save(writer);
        writer.Stream() << writer.ind() << "</" << FC_ELEM_DEFAULT << ">\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</" << FC_ELEM_DEFAULTS << ">\n";
}

void Document::restoreDefaults(Base::XMLReader &reader, int count)
{
    d->restoreDefaults.clear();
    if (count <= 0)
        return;

    // The stand-in is in no document, and an object's reaction to its own
    // property changing is written for one that is: restoring an App::Link's
    // element list into a document-less stand-in walks straight into
    // LinkBaseExtension::update() dereferencing a null document. Nothing here
    // is a change to anything anyway -- there is no object behind these
    // values, only a record of what a class starts out holding.
    RestoringDefaultsGuard restoringGuard;

    reader.readElement(FC_ELEM_DEFAULTS);
    for (int i=0; i<count; ++i) {
        int guard;
        reader.readElement(FC_ELEM_DEFAULT, &guard);
        std::string type = reader.getAttribute("type");
        // Keyed by the type's CURRENT name, because applyDefaults() looks the
        // block up by getTypeId().getName(). A file written before a rename
        // states the former name (Base::Type::addLegacyName), and under that
        // key the block would never be found -- every elided default of
        // those objects silently not pasted.
        if (Base::Type resolved = Base::Type::fromName(type.c_str()); !resolved.isBad())
            type = resolved.getName();
        auto proto = makeDefaultObject(type.c_str());
        // What the record says against what this build produces. Only the
        // difference has to be pasted onto anything, and on the build that
        // wrote the file there is none.
        //
        // Two stand-ins, not one stand-in and a pile of Property::Copy().
        // A detached copy has no container, so link properties compare by a
        // scope they no longer know and enumerations by a list they no
        // longer have. Two live stand-ins of the same class, both serialized
        // the way the writer serialized, is the comparison the writer made.
        auto fresh = proto ? makeDefaultObject(type.c_str()) : nullptr;
        if (proto && fresh) {
            // Names before the restore, lookups after it. A block written by
            // a different build may create or replace properties on the way
            // in, so pointers collected here would not be trusted afterwards.
            std::vector<std::string> candidates;
            {
                std::vector<App::Property*> props;
                proto->getPropertyList(props);
                for (auto prop : props)
                    if (SharedDefaults::eligible(*proto, *prop))
                        candidates.emplace_back(prop->getName());
            }

            proto->App::PropertyContainer::Restore(reader);

            // The diff is decided the same way the writer decided the
            // elision: by the bytes each side serializes to, through
            // SharedDefaults::serializeForCompare on both. Serializing the
            // restored proto rather than trusting the file's literal text
            // cancels whatever formatting a parse-and-save round trip
            // applies, so a difference here is a difference in what was
            // recorded, not in how a float prints. Status counts too, mod
            // Touched -- a pasted default must carry the writer's status the
            // way a written property carries its status attribute.
            const unsigned long touchedMask = 1UL << Property::Touched;
            DocumentP::RestoreDefaults entry;
            std::string recorded, built;
            for (const auto &name : candidates) {
                auto prop = proto->getPropertyByName(name.c_str());
                auto other = fresh->getPropertyByName(name.c_str());
                if (!prop || !other || prop->getTypeId() != other->getTypeId())
                    continue;
                bool differs = (prop->getStatus() & ~touchedMask)
                        != (other->getStatus() & ~touchedMask);
                if (!differs) {
                    // A side that will not serialize is a side that cannot
                    // be compared, and a default nobody compared is not
                    // pasted over anything.
                    if (!SharedDefaults::serializeForCompare(reader.DocumentSchema,
                                reader.FileVersion, *prop, recorded)
                            || !SharedDefaults::serializeForCompare(reader.DocumentSchema,
                                reader.FileVersion, *other, built))
                        continue;
                    differs = (recorded != built);
                }
                if (differs)
                    entry.names.emplace_back(name);
            }
            if (!entry.names.empty() && FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
                // Named, not just counted. On the build that wrote the file
                // this list is empty, so anything in it is either a genuine
                // change of default between builds or a property that does
                // not survive its own round trip -- and only the name says
                // which.
                std::ostringstream ss;
                for (const auto &n : entry.names)
                    ss << ' ' << n;
                FC_LOG("Default object " << type << " differs in "
                        << entry.names.size() << " properties:" << ss.str());
            }
            // Kept even when nothing differs: a property in the block may
            // have registered an archive entry against this stand-in, and the
            // reader will come looking for its owner later.
            entry.proto = std::move(proto);
            d->restoreDefaults[type] = std::move(entry);
        }
        reader.readEndElement(FC_ELEM_DEFAULT, &guard);
    }
    reader.readEndElement(FC_ELEM_DEFAULTS);
}

void Document::applyDefaults(DocumentObject *obj)
{
    if (d->restoreDefaults.empty())
        return;
    auto it = d->restoreDefaults.find(obj->getTypeId().getName());
    if (it == d->restoreDefaults.end() || it->second.names.empty())
        return;
    for (const auto &name : it->second.names) {
        auto prop = obj->getPropertyByName(name.c_str());
        auto other = it->second.proto->getPropertyByName(name.c_str());
        if (!prop || !other || prop->getTypeId() != other->getTypeId())
            continue;
        // Status first and value second, the order Restore itself uses --
        // and the same masking of the reserved User bits. Behind the same
        // kind of per-property net, too: one recorded default that will not
        // paste must cost that property, not the whole document.
        try {
            Property::StatusBits status(other->getStatus());
            status.reset(Property::User1);
            status.reset(Property::User2);
            status.reset(Property::User3);
            prop->setStatusValue(status.to_ulong());
            prop->Paste(*other);
        }
        catch (Base::Exception &e) {
            e.ReportException();
            FC_ERR("Failed to apply default " << obj->getFullName()
                    << '.' << name);
        }
        catch (const std::exception &e) {
            FC_ERR("Failed to apply default " << obj->getFullName()
                    << '.' << name << ": " << e.what());
        }
        catch (...) {
            FC_ERR("Failed to apply default " << obj->getFullName()
                    << '.' << name);
        }
    }
}

void Document::writeObjects(const std::vector<App::DocumentObject*>& obj,
                            Base::Writer &writer) const
{
    auto passClock = std::chrono::steady_clock::now();
    auto passSplit = [&passClock]() {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - passClock).count();
        passClock = now;
        return secs;
    };
    // writing the features types
    writer.incInd(); // indentation for 'Objects count'
    writer.Stream() << writer.ind() << "<Objects Count=\"" << obj.size();
    if(!isExporting(nullptr))
        writer.Stream() << "\" " FC_ATTR_DEPENDENCIES "=\"1";
    writer.Stream() << "\">\n";

    writer.incInd(); // indentation for 'Object type'

    if(!isExporting(nullptr)) {
        for(auto o : obj) {
            // The dependency section is a full pass of its own, resolving
            // every object's out-list. Cheap on the reference document
            // (0.04s), but it is a pass and it is counted as one.
            if (d->saveSeq) {
                d->saveSeq->next();
            }
            const auto &outList = o->getOutList(DocumentObject::OutListNoHidden
                                                | DocumentObject::OutListNoXLinked);
            std::set<App::DocumentObject*> outSet(outList.begin(),outList.end());
            writer.Stream() << writer.ind() 
                << "<" FC_ELEMENT_OBJECT_DEPS " " FC_ATTR_DEP_OBJ_NAME "=\""
                << o->getNameInDocument() << "\" " FC_ATTR_DEP_COUNT "=\"" << outSet.size();
            if(outSet.empty()) {
                writer.Stream() << "\"/>\n";
                continue;
            }
            int partial = o->canLoadPartial();
            if(partial>0)
                writer.Stream() << "\" " FC_ATTR_DEP_ALLOW_PARTIAL << "=\"" << partial;
            writer.Stream() << "\">\n";
            writer.incInd();
            for(auto dep : outSet) {
                auto name = dep?dep->getNameInDocument():"";
                writer.Stream() << writer.ind() << "<" FC_ELEMENT_OBJECT_DEP " "
                    FC_ATTR_DEP_OBJ_NAME "=\"" << (name?name:"") << "\"/>\n";
            }
            writer.decInd();
            writer.Stream() << writer.ind() << "</" FC_ELEMENT_OBJECT_DEPS ">\n";
        }
    }

    const double tDeps = passSplit();
    // Rebuilt by the loop below, and read while this save writes its entries.
    d->splitXmlEntries.clear();
    std::vector<DocumentObject*>::const_iterator it;
    for (it = obj.begin(); it != obj.end(); ++it) {
        if (d->saveSeq) {
            d->saveSeq->next();
        }
        writer.Stream() << writer.ind() << "<Object "
        << "type=\"" << writer.typeName((*it)->getTypeId()) << "\" "
        << "name=\"" << (*it)->getExportName()       << "\" "
        << "id=\"" << (*it)->getID()       << "\" "
        << "revision=\"" << (*it)->getRevision() << "\" ";

        // Only write out custom view provider types
        std::string viewType = (*it)->getViewProviderNameStored();
        if (viewType != (*it)->getViewProviderName())
            writer.Stream() << "ViewType=\"" << viewType << "\" ";

        // See DocumentObjectPy::getState
        if ((*it)->testStatus(ObjectStatus::Touch))
            writer.Stream() << "Touched=\"1\" ";
        if ((*it)->testStatus(ObjectStatus::Error)) {
            writer.Stream() << "Invalid=\"1\" ";
            auto desc = getErrorDescription(*it);
            if(desc)
                writer.Stream() << "Error=\"" << Property::encodeAttribute(desc) << "\" ";
        }

        if(writer.isSplitXML()) {
            std::string name((*it)->getNameInDocument());
            if(name == "Document" || name == "GuiDocument")
                name += "-Obj";
            // The entry the writer settled on, which is not always the name
            // asked for: an object is named by whoever made it, and a file
            // system takes neither any length nor every name. Remembered
            // here because SaveDocFile is handed a file name and has to
            // answer with the object it is for -- reading the name back out
            // of it was what left a long-named object writing nothing.
            const std::string& entry = writer.addFile(name+".xml",this);
            d->splitXmlEntries[Base::FileInfo(entry).fileNamePure()] = *it;
            writer.Stream() << "file=\"" << entry << "\" ";
        }

        writer.Stream() << "/>\n";
    }

    writer.decInd();  // indentation for 'Object type'
    writer.Stream() << writer.ind() << "</Objects>\n";

    // Objects of one class are mostly what their constructor gave them: an
    // identity placement, an empty expression engine, a flag nobody touched.
    // Write that constructor state once per class and let each object save
    // only its difference from it -- on a large assembly that is most of
    // Document.xml, and the load has that many fewer properties to restore.
    // The defaults are written out, not implied, so a document opened by a
    // build whose constructors differ still holds what its author saved.
    //
    // No block in the split-XML layout, and not for want of a reader: the
    // block lands in Document.xml, restoreDefaults() holds it until
    // readFiles() has drained, and readObject() already applies it to an
    // object arriving from its own file. It is left out because that is the
    // layout someone picked to keep each object in a file of its own, and a
    // block would make every one of those files depend on a document-wide
    // record -- a third object of some class appearing would rewrite the
    // other two, which is the diff a directory layout exists to not have.
    const double tHeaders = passSplit();
    std::map<std::string, SharedDefaults> defaults;
    if (!writer.isSplitXML())
        buildDefaults(writer, obj, defaults);
    const double tDefaults = passSplit();

    // writing the features itself
    writer.Stream() << writer.ind() << "<ObjectData Count=\"";
    if(writer.isSplitXML())
        writer.Stream() << "0\">\n";
    else {
        writer.Stream() << obj.size() << '"';
        if (!defaults.empty())
            writer.Stream() << ' ' << FC_ATTR_DEFAULTS << "=\"" << defaults.size() << '"';
        writer.Stream() << ">\n";

        writer.incInd(); // indentation for 'Object name'
        saveDefaults(writer, defaults);
        auto elided = PropertyContainer::savedDefaults;
        auto unknown = PropertyContainer::savedDefaultsUnknown;
        auto byValue = PropertyContainer::savedDefaultsValue;
        auto byStatus = PropertyContainer::savedDefaultsStatus;
        // Point every object at its class record for the duration of the
        // write, and at nothing again after it -- the records do not
        // outlive this call.
        auto pointAtDefaults = [&](bool on) {
            for (auto o : obj) {
                auto def = defaults.find(o->getTypeId().getName());
                o->setSaveDefaults(
                        on && def != defaults.end() ? &def->second : nullptr);
            }
        };
        pointAtDefaults(true);
        try {
            for (it = obj.begin(); it != obj.end(); ++it) {
                if (d->saveSeq) {
                    d->saveSeq->next();
                }
                writeObject(writer,*it);
            }
        }
        catch (...) {
            pointAtDefaults(false);
            throw;
        }
        pointAtDefaults(false);
        if (!defaults.empty())
            FC_LOG("save " << getName() << ": deps " << tDeps << "s, headers "
                    << tHeaders << "s, defaults " << tDefaults << "s, data "
                    << passSplit() << "s; " << obj.size() << " objects, "
                    << defaults.size() << " class defaults, "
                    << (PropertyContainer::savedDefaults - elided)
                    << " properties left out, written anyway: "
                    << (PropertyContainer::savedDefaultsValue - byValue) << " by value, "
                    << (PropertyContainer::savedDefaultsStatus - byStatus) << " by status, "
                    << (PropertyContainer::savedDefaultsUnknown - unknown) << " not in the block");
        writer.decInd(); // indentation for 'Object name'
    }
    writer.Stream() << writer.ind() << "</ObjectData>\n";
    writer.decInd();  // indentation for 'Objects count'
    // Close whichever root Save() (or exportObjects, always <= 4) opened.
    writer.Stream() << "</"
                    << (writer.getSchemaVersion() >= 5 ? FC_ELEM_FCDOCUMENT : "Document")
                    << ">\n";
}

void Document::writeObject(Base::Writer &writer, DocumentObject *obj) const 
{
    writer.Stream() << writer.ind() << "<Object name=\"" << obj->getExportName() << "\"";
    if(obj->canSaveExtension())
        writer.Stream() << " Extensions=\"True\"";

    writer.Stream() << ">\n";
    obj->Save(writer);
    writer.Stream() << writer.ind() << "</Object>\n";
}

void Document::SaveDocFile(Base::Writer &writer) const {
    Base::FileInfo fi(writer.getCurrentFileName());
    // What the Objects section recorded for this entry, and only then the
    // name: the entry is a file name, which is a thing the save chose, while
    // the object's name is a thing the user chose. They agree in the ordinary
    // case and the lookup below still covers whoever writes an entry without
    // going through the section above.
    auto known = d->splitXmlEntries.find(fi.fileNamePure());
    auto obj = known != d->splitXmlEntries.end()
        ? known->second
        : getObject(fi.fileNamePure().c_str());
    if(!obj)
        FC_ERR("Cannot find object " << fi.fileNamePure());
    else {
        writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                        << "<!-- FreeCAD DocumentObject -->\n"
                        << "<Document SchemaVersion=\"" << writer.getSchemaVersion()
                        << "\" FileVersion=\"" << writer.getFileVersion()
                        << "\">\n";
        writeObject(writer,obj);
        writer.Stream() << "</Document>\n";
    }
}

void Document::RestoreDocFile(Base::Reader &reader) {
    Base::XMLReader xmlReader(reader);
    xmlReader.readElement("Document");
    xmlReader.DocumentSchema = xmlReader.getAttributeAsInteger("SchemaVersion","");
    if(!xmlReader.DocumentSchema)
        xmlReader.DocumentSchema = reader.getDocumentSchema();
    xmlReader.FileVersion = xmlReader.getAttributeAsInteger("FileVersion","");
    if(!xmlReader.FileVersion)
        xmlReader.FileVersion = reader.getFileVersion();
    xmlReader.readElement("Object");
    readObject(xmlReader);
}

void Document::readObject(Base::XMLReader &reader) {
    std::string name = reader.getName(reader.getAttribute("name"));
    Base::ReaderContext rctx(name);
    DocumentObject* pObj = getObject(name.c_str());
    if (pObj && !pObj->testStatus(App::PartialObject)) { // check if this feature has been registered
        pObj->setStatus(ObjectStatus::Restore, true);
        try {
            FC_TRACE("restoring " << pObj->getFullName());
            // Whatever the shared default block moved off this build's own
            // defaults has to be put back before the object's own properties,
            // so that what the file states for this object still wins.
            applyDefaults(pObj);
            pObj->Restore(reader);
        }
        // Try to continue only for certain exception types if not handled
        // by the feature type. For all other exception types abort the process.
        catch (const Base::UnicodeError &e) {
            e.ReportException();
        }
        catch (const Base::ValueError &e) {
            e.ReportException();
        }
        catch (const Base::IndexError &e) {
            e.ReportException();
        }
        catch (const Base::RuntimeError &e) {
            e.ReportException();
        }
        catch (const Base::XMLAttributeError &e) {
            e.ReportException();
        }

        pObj->setStatus(ObjectStatus::Restore, false);

        if (reader.testStatus(Base::XMLReader::ReaderStatus::PartialRestoreInDocumentObject)) {
            Base::Console().Error("Object \"%s\" was subject to a partial restore. As a result geometry may have changed or be incomplete.\n",name.c_str());
            reader.clearPartialRestoreDocumentObject();
        }
    }
}

struct DepInfo {
    std::unordered_set<std::string> deps;
    int canLoadPartial = 0;
};

static void _loadDeps(const std::string &name,
        std::unordered_map<std::string,bool> &objs,
        const std::unordered_map<std::string,DepInfo> &deps)
{
    auto it = deps.find(name);
    if(it == deps.end()) {
        objs.emplace(name,true);
        return;
    }
    if(it->second.canLoadPartial) {
        if(it->second.canLoadPartial == 1) {
            // canLoadPartial==1 means all its children will be created but not
            // restored, i.e. exists as if newly created object, and therefore no
            // need to load dependency of the children
            for(auto &dep : it->second.deps)
                objs.emplace(dep,false);
            objs.emplace(name,true);
        }else
            objs.emplace(name,false);
        return;
    }
    objs[name] = true;
    // If cannot load partial, then recurse to load all children dependency
    for(auto &dep : it->second.deps) {
        auto it = objs.find(dep);
        if(it!=objs.end() && it->second)
            continue;
        _loadDeps(dep,objs,deps);
    }
}

std::vector<App::DocumentObject*>
Document::readObjects(Base::XMLReader& reader)
{
    d->touchedObjs.clear();
    d->savedTouched.clear();
    bool keepDigits = testStatus(Document::KeepTrailingDigits);
    setStatus(Document::KeepTrailingDigits, !reader.doNameMapping());
    std::vector<App::DocumentObject*> objs;

    // read the object types
    reader.readElement("Objects");
    int Cnt = reader.getAttributeAsInteger("Count");

    if(!reader.hasAttribute(FC_ATTR_DEPENDENCIES))
        d->partialLoadObjects.clear();
    else if(!d->partialLoadObjects.empty()) {
        std::unordered_map<std::string,DepInfo> deps;
        for (int i=0 ;i<Cnt ;i++) {
            reader.readElement(FC_ELEMENT_OBJECT_DEPS);
            int dcount = reader.getAttributeAsInteger(FC_ATTR_DEP_COUNT);
            if(!dcount)
                continue;
            auto &info = deps[reader.getAttribute(FC_ATTR_DEP_OBJ_NAME)];
            if(reader.hasAttribute(FC_ATTR_DEP_ALLOW_PARTIAL))
                info.canLoadPartial = reader.getAttributeAsInteger(FC_ATTR_DEP_ALLOW_PARTIAL);
            for(int j=0;j<dcount;++j) {
                reader.readElement(FC_ELEMENT_OBJECT_DEP);
                const char *name = reader.getAttribute(FC_ATTR_DEP_OBJ_NAME);
                if(name && name[0])
                    info.deps.insert(name);
            }
            reader.readEndElement(FC_ELEMENT_OBJECT_DEPS);
        }
        std::vector<std::string> objs;
        objs.reserve(d->partialLoadObjects.size());
        for(auto &v : d->partialLoadObjects)
            objs.emplace_back(v.first.c_str());
        for(auto &name : objs)
            _loadDeps(name,d->partialLoadObjects,deps);
        if(Cnt > (int)d->partialLoadObjects.size())
            setStatus(Document::PartialDoc,true);
        else {
            for(auto &v : d->partialLoadObjects) {
                if(!v.second) {
                    setStatus(Document::PartialDoc,true);
                    break;
                }
            }
            if(!testStatus(Document::PartialDoc))
                d->partialLoadObjects.clear();
        }
    }

    long lastId = 0;
    FC_TIME_INIT(t);
    // The blocking open's own progress. Historically the archive file
    // loop ("Importing project files...") was what reported a load and
    // pumped the event loop; with deferred shape entries most of that
    // loop no longer runs at open, and a 5000-object document restored
    // for many seconds with a dead progress bar and a frozen window.
    // ONE launcher spanning both loops below, because only the TOP
    // launcher's next() reports and pumps at all -- a second launcher
    // started under a live blocking one is a silent no-op (measured:
    // the object-data loop ran unreported while a scoped creation
    // launcher was still alive above it). next() is throttled inside
    // the sequencer (one bar update and one event pump per 200ms), so
    // the per-object cost here is an integer increment.
    Base::SequencerLauncher seqRestore("Restoring document...",
                                       size_t(Cnt) * 2);
    for (int i=0 ;i<Cnt ;i++) {
        {
            FC_TIME_INIT(tSeq);
            seqRestore.next();
            FC_DURATION_PLUS(d->restoreTiming.createSeq, tSeq);
        }
        reader.readElement("Object");
        std::string type = reader.getAttribute("type");
        std::string name = reader.getAttribute("name");
        Base::ReaderContext rctx(name);
        std::string viewType = reader.hasAttribute("ViewType")?reader.getAttribute("ViewType"):"";
        int rev = reader.getAttributeAsInteger("revision", "");

        bool partial = false;
        if(!d->partialLoadObjects.empty()) {
            auto it = d->partialLoadObjects.find(name);
            if(it == d->partialLoadObjects.end())
                continue;
            partial = !it->second;
        }

        // if not importing, the following addObject() gives the object the
        // id it was saved with.
        d->restoringId = 0;
        if(!testStatus(Status::Importing) && reader.hasAttribute("id"))
            d->restoringId = reader.getAttributeAsInteger("id");

        // To prevent duplicate name when export/import of objects from
        // external documents, we append those external object name with
        // @<document name>. Before importing (here means we are called by
        // importObjects), we shall strip the postfix. What the caller
        // (MergeDocument) sees is still the unstripped name mapped to a new
        // internal name, and the rest of the link properties will be able to
        // correctly unmap the names.
        auto pos = name.find('@');
        std::string _obj_name;
        const char *obj_name;
        if(pos!=std::string::npos) {
            _obj_name = name.substr(0,pos);
            obj_name = _obj_name.c_str();
        }else
            obj_name = name.c_str();

        try {
            // Use name from XML as is and do NOT remove trailing digits because
            // otherwise we may cause a dependency to itself
            // Example: Object 'Cut001' references object 'Cut' and removing the
            // digits we make an object 'Cut' referencing itself.
            FC_TIME_INIT(tAdd);
            App::DocumentObject* obj = nullptr;
            {
                struct Reset {
                    long& id;
                    ~Reset() { id = 0; }
                } reset {d->restoringId};
                obj = addObject(type.c_str(), obj_name, /*isNew=*/ false, viewType.c_str(), partial);
            }
            FC_DURATION_PLUS(d->restoreTiming.createAdd, tAdd);
            if (obj) {
                if(lastId < obj->_Id)
                    lastId = obj->_Id;
                objs.push_back(obj);
                // use this name for the later access because an object with
                // the given name may already exist
                reader.addName(name.c_str(), obj->getNameInDocument());

                // restore touch/error status flags
                if (reader.hasAttribute("Touched")) {
                    if(reader.getAttributeAsInteger("Touched") != 0) {
                        d->touchedObjs.insert(obj);
                        d->savedTouched.insert(obj->getID());
                    }
                }
                if (reader.hasAttribute("Invalid")) {
                    obj->setStatus(ObjectStatus::Error, reader.getAttributeAsInteger("Invalid") != 0);
                    if(obj->isError() && reader.hasAttribute("Error"))
                        d->addRecomputeLog(reader.getAttribute("Error"),obj);
                }

                obj->_revision = rev;
            }

            const char *file = reader.getAttribute("file","");
            if(file && file[0])
                reader.addFile(file, this);
        }
        catch (const Base::Exception& e) {
            Base::Console().Error("Cannot create object '%s': (%s)\n", name.c_str(), e.what());
        }
    }
    if(!testStatus(Status::Importing))
        d->noteObjectId(lastId);

    reader.readEndElement("Objects");
    FC_DURATION_PLUS(d->restoreTiming.create, t);
    d->restoreTiming.objectCount += objs.size();
    setStatus(Document::KeepTrailingDigits, keepDigits);

    // read the features itself
    reader.clearPartialRestoreDocumentObject();

    reader.readElement("ObjectData");
    Cnt = reader.getAttributeAsInteger("Count");
    // Before any element of the block is read: readElement() replaces the
    // attributes of the element we are standing on.
    restoreDefaults(reader, reader.getAttributeAsInteger(FC_ATTR_DEFAULTS, "0"));
    std::string objName;
    _FC_TIME_INIT(t);
    auto propStats = PropertyContainer::restoreStats;
    try {
        // The property half of the open -- the bulk of its wall time,
        // reported through the same launcher as the creation loop.
        for (int i=0 ;i<Cnt ;i++) {
            seqRestore.next();
            int guard;
            reader.readElement("Object", &guard);
            objName = reader.getAttribute("name");
            readObject(reader);
            reader.readEndElement("Object",&guard);
        }
    } catch (Base::XMLParseException &e) {
        e.ReportException();
        FC_ERR("Exception while restoring " << getName() << '.' << objName);
        throw;
    }
    reader.readEndElement("ObjectData");
    FC_DURATION_PLUS(d->restoreTiming.data, t);
    d->restoreTiming.props = PropertyContainer::restoreStats - propStats;

    return objs;
}

void Document::addRecomputeObject(DocumentObject *obj) {
    if(testStatus(Status::Restoring) && obj) {
        setStatus(Status::RecomputeOnRestore, true);
        d->touchedObjs.insert(obj);
        obj->enforceRecompute();
    }
}

std::vector<App::DocumentObject*>
Document::importObjects(Base::XMLReader& reader)
{
    d->hashers.clear();
    Base::FlagToggler<> flag(globalIsRestoring, false);
    Base::ObjectStatusLocker<Status, Document> restoreBit(Status::Restoring, this);
    Base::ObjectStatusLocker<Status, Document> restoreBit2(Status::Importing, this);
    ExpressionParser::ExpressionImporter expImporter(reader);
    // Fragments are exported capped at 5 and rooted <Document>, but accept
    // both roots here too -- reading is cheap to keep symmetric, and a
    // future exporter may earn the other name.
    reader.readElement();
    if (strcmp(reader.localName(), "Document") != 0
            && strcmp(reader.localName(), FC_ELEM_FCDOCUMENT) != 0)
        THROWM(Base::XMLParseException, "Not a FreeCAD document")
    long scheme = reader.getAttributeAsInteger("SchemaVersion");
    reader.DocumentSchema = scheme;
    if (reader.hasAttribute("ProgramVersion")) {
        reader.ProgramVersion = reader.getAttribute("ProgramVersion");
    } else {
        reader.ProgramVersion = "pre-0.14";
    }
    if (reader.hasAttribute("FileVersion")) {
        reader.FileVersion = reader.getAttributeAsUnsigned("FileVersion");
    } else {
        reader.FileVersion = 0;
    }

    // The imported objects' included files come in the same archive; they
    // become this document's content, counted against this document's store.
    getFileBlobManager().beginRestore(reader);

    Base::ReaderContext rctx(getName());
    std::vector<App::DocumentObject*> objs = readObjects(reader);
    for(auto o : objs) {
        if(o && o->isAttachedToDocument()) {
            o->setStatus(App::ObjImporting,true);
            FC_LOG("importing " << o->getFullName());
            if (auto propUUID = Base::freecad_dynamic_cast<PropertyUUID>(
                        o->getPropertyByName("_ObjectUUID")))
            {
                auto propSource = Base::freecad_dynamic_cast<PropertyUUID>(
                        o->getPropertyByName("_SourceUUID"));
                if (!propSource)
                    propSource = static_cast<PropertyUUID*>(o->addDynamicProperty(
                                "App::PropertyUUID", "_SourceUUID", nullptr, nullptr,
                                Prop_Output | Prop_Hidden));
                if (propSource)
                    propSource->setValue(propUUID->getValue());
                propUUID->setValue(Base::Uuid::createUuid());
            }
        }
    }

    // Nameless on purpose: the next end element is the root's own, whichever
    // of the two roots this file used.
    reader.readEndElement();

    // readFiles() runs from this signal, so the content is in the store by
    // the time it returns and the importing properties can be served.
    signalImportObjects(objs, reader);
    getFileBlobManager().dispatchPending();

    // See restore(): nothing refers to the stand-ins once the files are in.
    d->restoreDefaults.clear();

    afterRestore(objs,true);

    signalFinishImportObjects(objs);
    // No later referrer can appear for an import: there is no view document
    // replay, so the hold has done its job.
    getFileBlobManager().endRestore();

    for(auto o : objs) {
        if(o && o->isAttachedToDocument())
            o->setStatus(App::ObjImporting,false);
    }

    d->hashers.clear();
    return objs;
}

unsigned int Document::getMemSize () const
{
    unsigned int size = 0;

    // size of the DocObjects in the document
    std::vector<DocumentObject*>::const_iterator it;
    for (it = d->objectArray.begin(); it != d->objectArray.end(); ++it)
        size += (*it)->getMemSize();

    size += d->Hasher->getMemSize();

    // size of the document properties...
    size += PropertyContainer::getMemSize();

    // Undo Redo size
    size += getUndoMemSize();

    return size;
}

static std::string checkFileName(const char *file) {
    Base::FileInfo fi(file);
    if(fi.isDir())
        return file;

    std::string fn(file);

    // Append extension if missing. This option is added for security reason, so
    // that the user won't accidentally overwrite other file that may be critical.
    if(DocumentParams::getCheckExtension())
    {
        const char *ext = strrchr(file,'.');
        if(!ext || !boost::iequals(ext+1,"fcstd")) {
            if(ext && ext[1] == 0)
                fn += "FCStd";
            else
                fn += ".FCStd";
        }
    }
    return fn;
}

bool Document::saveAs(const char* _file)
{
    std::string file = checkFileName(_file);
    // Naming a host file to write is fs.write (F1, docs/Sandbox.md 7.29).
    // save() writes the document's OWN file and stays the guest's to call
    // (S1); saveAs chooses a path, so it is gated -- and a path the user
    // picked in a dialog under this guest is blessed and passes.
    // checkFileName may have appended .FCStd: gate what will be written.
    ExpressionSecurity::checkHostPath(ExpressionSecurity::Permission::FsWrite, file);
    Base::FileInfo fi(file.c_str());
    if (this->FileName.getStrValue() != file) {
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        this->FileName.setValue(file);
        this->Label.setValue(fi.fileNamePure());
        this->Uid.touch(); // this forces a rename of the transient directory
    }

    return save();
}

bool Document::saveCopy(const char* _file, bool withHistory) const
{
    std::string file = checkFileName(_file);
    // a copy is a host file written by path, exactly as saveAs (7.29)
    ExpressionSecurity::checkHostPath(ExpressionSecurity::Permission::FsWrite, file);
    if (this->FileName.getStrValue() != file) {
        struct Guard
        {
            bool& flag;
            bool was;
            Guard(bool& f, bool v) : flag(f), was(f) { flag = v; }
            ~Guard() { flag = was; }
        } guard(d->savingWithoutHistory, !withHistory);
        bool result = saveToFile(file.c_str());
        return result;
    }
    return false;
}

// Save the document under the name it has been opened
bool Document::save ()
{
    if (testStatus(Document::VersionDoc)) {
        // Read-only as a partial document is (docs/TransactionLog.md sec
        // 27.5 ruling 4); the Gui offers to save it anyway, with a warning.
        FC_ERR("'" << Label.getValue() << "' is a version of a file and cannot be saved");
        return false;
    }
    if(testStatus(Document::PartialDoc)) {
        FC_ERR("Partial loaded document '" << Label.getValue() << "' cannot be saved");
        // TODO We don't make this a fatal error and return 'true' to make it possible to
        // save other documents that depends on this partial opened document. We need better
        // handling to avoid touching partial documents.
        return true;
    }

    if (*(FileName.getValue()) != '\0') {
        {
            Base::FlagToggler<> quiet(d->bookkeeping, false);
            // Save the name of the tip object in order to handle in Restore()
            if (Tip.getValue()) {
                TipName.setValue(Tip.getValue()->getNameInDocument());
            }

            std::string LastModifiedDateString = Base::TimeInfo::currentDateTimeString();
            LastModifiedDate.setValue(LastModifiedDateString.c_str());
            // set author if needed
            bool saveAuthor = DocumentParams::getprefSetAuthorOnSave();
            if (saveAuthor) {
                LastModifiedBy.setValue(DocumentParams::getprefAuthor().c_str());
            }
        }

        return saveToFile(FileName.getValue());
    }

    return false;
}

namespace App {
// Helper class to handle different backup policies
class BackupPolicy {
public:
    enum Policy {
        Standard,
        TimeStamp
    };
    BackupPolicy() {
        policy = Standard;
        numberOfFiles = 1;
        useFCBakExtension = true;
        saveBackupDateFormat = "%Y%m%d-%H%M%S";
    }
    ~BackupPolicy() = default;
    void setPolicy(Policy p) {
        policy = p;
    }
    void setNumberOfFiles(int count) {
        numberOfFiles = count;
    }
    void useBackupExtension(bool on) {
        useFCBakExtension = on;
    }
    void setDateFormat(const std::string& fmt) {
        saveBackupDateFormat = fmt;
    }
    void apply(const std::string& sourcename, const std::string& targetname) {
        switch (policy) {
        case Standard:
            applyStandard(sourcename, targetname);
            break;
        case TimeStamp:
            applyTimeStamp(sourcename, targetname);
            break;
        }
    }

private:
    void applyStandard(const std::string& sourcename, const std::string& targetname) {
        // if saving the project data succeeded rename to the actual file name
        Base::FileInfo fi(targetname);
        if (fi.exists()) {
            if (numberOfFiles > 0) {
                int nSuff = 0;
                std::string fn = fi.fileName();
                Base::FileInfo di(fi.dirPath());
                std::vector<Base::FileInfo> backup;
                std::vector<Base::FileInfo> files = di.getDirectoryContent();
                for (const Base::FileInfo& it : files) {
                    std::string file = it.fileName();
                    if (file.substr(0,fn.length()) == fn) {
                        // starts with the same file name
                        std::string suf(file.substr(fn.length()));
                        if (!suf.empty()) {
                            std::string::size_type nPos = suf.find_first_not_of("0123456789");
                            if (nPos==std::string::npos) {
                                // store all backup files
                                backup.push_back(it);
                                nSuff = std::max<int>(nSuff, std::atol(suf.c_str()));
                            }
                        }
                    }
                }

                if (!backup.empty() && (int)backup.size() >= numberOfFiles) {
                    // delete the oldest backup file we found
                    Base::FileInfo del = backup.front();
                    for (const Base::FileInfo& it : backup) {
                        if (it.lastModified() < del.lastModified())
                            del = it;
                    }

                    del.deleteFile();
                    fn = del.filePath();
                }
                else {
                    // create a new backup file
                    std::stringstream str;
                    str << fi.filePath() << (nSuff + 1);
                    fn = str.str();
                }

                if (!fi.renameFile(fn.c_str()))
                    Base::Console().Warning("Cannot rename project file to backup file\n");
            }
            else if (fi.isDir()) {
                fi.deleteDirectoryRecursive();
            }
            else {
                fi.deleteFile();
            }
        }

        Base::FileInfo tmp(sourcename);
        if (!tmp.renameFile(targetname.c_str())) {
            throw Base::FileException(
                "Cannot rename tmp save file to project file", Base::FileInfo(targetname));
        }
    }
    void applyTimeStamp(const std::string& sourcename, const std::string& targetname) {
        Base::FileInfo fi(targetname);

        std::string fn = sourcename;
        std::string ext = fi.extension();
        std::string bn; // full path with no extension but with "."
        std::string pbn; // base name of the project + "."
        if (!ext.empty()) {
            bn = fi.filePath().substr(0, fi.filePath().length() - ext.length());
            pbn = fi.fileName().substr(0, fi.fileName().length() - ext.length());
        }
        else {
            bn = fi.filePath() + ".";
            pbn = fi.fileName() + ".";
        }

        bool backupManagementError = false; // Note error and report at the end
        if (fi.exists()) {
            if (numberOfFiles > 0) {
                // replace . by - in format to avoid . between base name and extension
                boost::replace_all(saveBackupDateFormat, ".", "-");
                {
                    // Remove all extra backups
                    std::string fn = fi.fileName();
                    Base::FileInfo di(fi.dirPath());
                    std::vector<Base::FileInfo> backup;
                    std::vector<Base::FileInfo> files = di.getDirectoryContent();
                    for (const Base::FileInfo& it : files) {
                        if (it.isFile()) {
                            std::string file = it.fileName();
                            std::string fext = it.extension();
                            std::string fextUp = fext;
                            std::transform(fextUp.begin(), fextUp.end(), fextUp.begin(),(int (*)(int))toupper);
                            // re-enforcing identification of the backup file


                            // old case : the name starts with the full name of the project and follows with numbers
                            if ((startsWith(file, fn) &&
                                 (file.length() > fn.length()) &&
                                 checkDigits(file.substr(fn.length()))) ||
                                 // .FCBak case : The bame starts with the base name of the project + "."
                                 // + complement with no "." + ".FCBak"
                                 ((fextUp == "FCBAK") && startsWith(file, pbn) &&
                                 (checkValidComplement(file, pbn, fext)))) {
                                backup.push_back(it);
                            }
                        }
                    }

                    if (!backup.empty() && (int)backup.size() >= numberOfFiles) {
                        std::sort (backup.begin(), backup.end(), fileComparisonByDate);
                        // delete the oldest backup file we found
                        // Base::FileInfo del = backup.front();
                        int nb = 0;
                        for (Base::FileInfo& it : backup) {
                            nb++;
                            if (nb >= numberOfFiles) {
                                try {
                                    if (!it.deleteFile()) {
                                        backupManagementError = true;
                                        Base::Console().Warning("Cannot remove backup file : %s\n", it.fileName().c_str());
                                    }
                                }
                                catch (...) {
                                    backupManagementError = true;
                                    Base::Console().Warning("Cannot remove backup file : %s\n", it.fileName().c_str());
                                }
                            }
                        }

                    }
                }  //end remove backup

                // create a new backup file
                {
                    int ext = 1;
                    if (useFCBakExtension) {
                        std::stringstream str;
                        Base::TimeInfo ti = fi.lastModified();
                        time_t s =ti.getSeconds();
                        struct tm * timeinfo = localtime(& s);
                        char buffer[100];

                        strftime(buffer,sizeof(buffer),saveBackupDateFormat.c_str(),timeinfo);
                        str << bn << buffer ;

                        fn = str.str();
                        bool done = false;

                        if ((fn.empty()) || (fn[fn.length()-1] == ' ') || (fn[fn.length()-1] == '-')) {
                            if (fn[fn.length()-1] == ' ') {
                                fn = fn.substr(0,fn.length()-1);
                            }
                        }
                        else {
                            if (!renameFileNoErase(fi, fn+".FCBak")) {
                                fn = fn + "-";
                            }
                            else {
                                done = true;
                            }
                        }

                        if (!done) {
                            while (ext < numberOfFiles + 10) {
                                if (renameFileNoErase(fi, fn+std::to_string(ext)+".FCBak"))
                                    break;
                                ext++;
                            }
                        }
                    }
                    else {
                        // changed but simpler and solves also the delay sometimes introduced by google drive
                        while (ext < numberOfFiles + 10) {
                            // linux just replace the file if exists, and then the existence is to be tested before rename
                            if (renameFileNoErase(fi, fi.filePath()+std::to_string(ext)))
                                break;
                            ext++;
                        }
                    }

                    if (ext >= numberOfFiles + 10) {
                        Base::Console().Error("File not saved: Cannot rename project file to backup file\n");
                        //throw Base::FileException("File not saved: Cannot rename project file to backup file", fi);
                    }
                }
            }
            else {
                try {
                    fi.deleteFile();
                }
                catch (...) {
                    Base::Console().Warning("Cannot remove backup file: %s\n", fi.fileName().c_str());
                    backupManagementError = true;
                }
            }
        }

        Base::FileInfo tmp(sourcename);
        if (!tmp.renameFile(targetname.c_str())) {
            throw Base::FileException(
                "Save interrupted: Cannot rename temporary file to project file", tmp);
        }

        if (numberOfFiles <= 0) {
            try {
                if (fi.isDir())
                    fi.deleteDirectoryRecursive();
                else
                    fi.deleteFile();
            }
            catch (...) {
                Base::Console().Warning("Cannot remove backup file: %s\n", fi.fileName().c_str());
                backupManagementError = true;
           }
        }

        if (backupManagementError) {
            throw Base::FileException("Warning: Save complete, but error while managing backup history.", fi);
        }
    }
    static bool fileComparisonByDate(const Base::FileInfo& i,
                              const Base::FileInfo& j) {
        return (i.lastModified()>j.lastModified());
    }
    bool startsWith(const std::string& st1,
                    const std::string& st2) const {
        return st1.substr(0,st2.length()) == st2;
    }
    bool checkValidString (const std::string& cmpl, const boost::regex& e) const {
        boost::smatch what;
        bool res = boost::regex_search (cmpl,what,e);
        return res;
    }
    bool checkValidComplement(const std::string& file, const std::string& pbn, const std::string& ext) const {
        std::string cmpl = file.substr(pbn.length(),file.length()- pbn.length() - ext.length()-1);
        boost::regex e (R"(^[^.]*$)");
        return checkValidString(cmpl,e);
    }
    bool checkDigits (const std::string& cmpl) const {
        boost::regex e (R"(^[0-9]*$)");
        return checkValidString(cmpl,e);
    }
    bool renameFileNoErase(Base::FileInfo fi, const std::string& newName) {
        // linux just replaces the file if it exists, so the existence is to be tested before rename
        Base::FileInfo nf(newName);
        if (!nf.exists()) {
            return fi.renameFile(newName.c_str());
        }
        return false;
    }

private:
    Policy policy;
    int numberOfFiles;
    bool useFCBakExtension;
    std::string saveBackupDateFormat;
};
}

bool Document::saveToFile(const char* filename) const
{
    ExpressionBlocker::check();
    // What the file holds is what the log has committed.
    const_cast<Document*>(this)->commitImplicitTransaction();

    // Nothing may still be parked once this returns: the source archive is
    // renamed to a backup or deleted below, and an entry served afterwards
    // would seek a stale offset into whatever now carries that name. The
    // property accessors alone do not cover it -- a Transient or
    // non-persistent property is skipped by beforeSave()/Save() and would
    // keep its parked entry across the rename -- and on Windows the index's
    // own open handle can fail the rename outright. Faulting everything in
    // here costs what the save was going to read anyway.
    // ONE sequence for the whole save, started before the flush below because
    // that flush is most of it: on a document just opened, serving the parked
    // shapes is some 25 of a 30 second save, and it used to be reported by
    // whichever drain launcher happened to still be alive -- so the status
    // text came from the save and the bar's numbers and its remaining-time
    // estimate came from the drain, which is how a 30 second save advertised
    // three and a half minutes. Five passes over the objects are known now --
    // staging their blob content, letting each settle what it is about to
    // write, then the dependency, Objects and ObjectData sections -- plus
    // whatever is still parked; the later phases restate the total from their
    // own base as they learn their size.
    Base::SequencerLauncher seqSave(
            "Saving document...",
            d->deferredFiles.size() + d->objectArray.size() * 5);
    // A save that throws still has to put the document back as it found it.
    class SaveSeqGuard
    {
    public:
        SaveSeqGuard(DocumentP* p, Base::SequencerLauncher* seq)
            : d(p)
        {
            d->saveSeq = seq;
        }
        ~SaveSeqGuard()
        {
            d->saveSeq = nullptr;
        }
        SaveSeqGuard(const SaveSeqGuard&) = delete;
        SaveSeqGuard& operator=(const SaveSeqGuard&) = delete;

    private:
        DocumentP* d;
    } saveSeqGuard(d, &seqSave);
    // A save is its own job: whatever background fill or drain is still
    // running from the open must not be read as containing it.
    seqSave.setStandalone();

    const_cast<Document*>(this)->flushDeferredFiles();

    signalStartSave(*this, filename);

    int compression = DocumentParams::getCompressionLevel();
    compression = Base::clamp<int>(compression, Z_NO_COMPRESSION, Z_BEST_COMPRESSION);

    bool archive = !Base::FileInfo(filename).isDir();
    bool policy = archive?DocumentParams::getBackupPolicy():false;

    std::string _realfile;
    const char *realfile = filename;
    QFileInfo qfi(QString::fromUtf8(filename));
    if (qfi.isSymLink()) {
        _realfile = qfi.symLinkTarget().toUtf8().constData();
        realfile = _realfile.c_str();
    }

    auto canonical_path = [](const char* filename) {
        try {
#ifdef FC_OS_WIN32
            QString utf8Name = QString::fromUtf8(filename);
            auto realpath = fs::weakly_canonical(fs::absolute(fs::path(utf8Name.toStdWString())));
            std::string nativePath = QString::fromStdWString(realpath.native()).toStdString();
#else
            auto realpath = fs::weakly_canonical(fs::absolute(fs::path(filename)));
            std::string nativePath = realpath.native();
#endif
            // In case some folders in the path do not exist
            auto parentPath = realpath.parent_path();
            fs::create_directories(parentPath);

            return nativePath;
        }
        catch (const std::exception&) {
#ifdef FC_OS_WIN32
            QString utf8Name = QString::fromUtf8(filename);
            auto parentPath = fs::absolute(fs::path(utf8Name.toStdWString())).parent_path();
#else
            auto parentPath = fs::absolute(fs::path(filename)).parent_path();
#endif
            fs::create_directories(parentPath);

            return std::string(filename);
        }
    };

    //realpath is canonical filename i.e. without symlink
    std::string nativePath = canonical_path(realfile);

    // make a tmp. file where to save the project data first and then rename to
    // the actual file name. This may be useful if overwriting an existing file
    // fails so that the data of the work up to now isn't lost.
    std::string uuid = Base::Uuid::createUuid();
    std::string fn = nativePath;
    if (policy) {
        fn += ".";
        fn += uuid;
    }
    Base::FileInfo tmp(fn);


    std::vector<std::string> fileNames;

    // open extra scope to close ZipWriter properly
    {
        Base::ofstream file;
        std::unique_ptr<Base::Writer> _writer;
        if(archive) {
            file.open(tmp, std::ios::out | std::ios::binary);
            if (!file.is_open())
                throw Base::FileException("Failed to open file", tmp);
            auto zipwriter = new Base::ZipWriter(file);
            _writer.reset(zipwriter);
            zipwriter->setComment("FreeCAD Document");
            zipwriter->setLevel(compression);
        } else {
            _writer.reset(new Base::FileWriter(tmp.filePath().c_str()));
        }

        save(*_writer, archive);
        fileNames = _writer->getFilenames();
    }


    if (policy) {
        // if saving the project data succeeded rename to the actual file name
        int count_bak = DocumentParams::getCountBackupFiles();
        bool backup = DocumentParams::getCreateBackupFiles();
        if (!backup) {
            count_bak = -1;
        }
        bool useFCBakExtension = DocumentParams::getUseFCBakExtension();
        std::string	saveBackupDateFormat = DocumentParams::getSaveBackupDateFormat();

        BackupPolicy policy;
        if (useFCBakExtension) {
            policy.setPolicy(BackupPolicy::TimeStamp);
            policy.useBackupExtension(useFCBakExtension);
            policy.setDateFormat(saveBackupDateFormat);
        }
        else {
            policy.setPolicy(BackupPolicy::Standard);
        }
        policy.setNumberOfFiles(count_bak);
        policy.apply(fn, nativePath);
    }

    signalFinishSave(*this, filename);

    // A file written below the current schema -- for upstream, the user's
    // choice -- is not what the log keeps: the version of this save is the
    // document as it is, serialised at the current schema (sec 27.62).
    if (getSaveSchemaVersion() < getCurrentSchemaVersion() && getTransactionLog())
        const_cast<Document*>(this)->_snapshotToLog("save");

    if(!archive) {
        std::vector<std::pair<std::string,int> > files;
        for(const auto &f : fileNames) {
            auto it = d->files.find(f);
            if(it == d->files.end()) {
                FC_LOG("document " << getName() << " add " << f);
                files.emplace_back(f,1);
            } else {
                files.emplace_back(f,0);
                d->files.erase(it);
            }
        }
        for(const auto &f : d->files) {
            FC_LOG("document " << getName() << " remove " << f);
            files.emplace_back(f,-1);
        }
        d->files.clear();

        GetApplication().signalDocumentFilesSaved(*this, filename, files);

        bool remove = DocumentParams::getAutoRemoveFile();
        std::string path(filename);
        path += "/";
        for(auto &v : files) {
            if(v.second>=0)
                d->files.insert(std::move(v.first));
            else if(remove) 
                Base::FileInfo(path+v.first).deleteFile();
        }
    }

    return true;
}

void Document::save(Base::Writer &writer, bool archive) const {
    // Lend the save's sequence to the writer for the phases it owns. Null on
    // an export, which writes objects through here without a save around it.
    class WriterProgress
    {
    public:
        WriterProgress(Base::Writer& w, Base::SequencerLauncher* seq)
            : writer(w)
        {
            writer.setProgress(seq);
        }
        ~WriterProgress()
        {
            writer.setProgress(nullptr);
        }
        WriterProgress(const WriterProgress&) = delete;
        WriterProgress& operator=(const WriterProgress&) = delete;

    private:
        Base::Writer& writer;
    } writerProgress(writer, d->saveSeq);

    if(!archive) {
        writer.setFileVersion(2);
        writer.setForceXML(ForceXML.getValue());
        writer.setSplitXML(SplitXML.getValue());
    }

    // The property is the cap the user chose; what the writer carries from
    // here on is the outcome this save resolves it to, and every header
    // below states the outcome. The two are kept apart because a save may
    // leave out a half of the format it cannot carry without that changing
    // the version it is written under, and none of it touches the cap.
    writer.setSchemaVersion(resolveSchemaVersion(writer));
    // Collect before anything is written: the included files go into the
    // archive ahead of the objects and views that refer to them.
    auto phaseClock = std::chrono::steady_clock::now();
    auto phaseSplit = [&phaseClock]() {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - phaseClock).count();
        phaseClock = now;
        return secs;
    };
    getFileBlobManager().beginSave(writer);
    const double tBegin = phaseSplit();
    // The embedded history (sec 16.4) is a blob like the others and has to
    // be in place before the collect pass notes what the archive carries.
    // Below schema 5 there is no blob store to carry it (PropertyHistory
    // writes an empty element), so no copy is made (sec 27.56).
    const_cast<Document*>(this)->embedHistory(archive && writer.getSchemaVersion() >= 5);
    collectFileBlobs();
    const double tCollect = phaseSplit();

    writer.putNextEntry("Document.xml");

    if (PreferBinary.getValue()) {
        writer.setMode("BinaryBrep");
        writer.setPreferBinary(true);
    } else if(writer.getFileVersion() > 1)
        writer.setPreferBinary(false);

    // The log's save record wants every entry as written (sec 11, 23.3):
    // captured on the way into the archive, cut at each property body,
    // rather than serialised twice. Document.xml here, the other XML
    // entries the manifest holds (GuiDocument.xml, the split object files)
    // through the same sink as writeFiles() serves them. Record mode: the
    // sink claims nothing, the file is what it is. Not below the current
    // schema: the log holds no other, and saveToFile() records that save's
    // version as a snapshot of its own (sec 27.62).
    TransactionLog* log = writer.getSchemaVersion() >= getCurrentSchemaVersion()
        ? getTransactionLog() : nullptr;
    // The string table as a member of its own from schema 5 on, where the
    // content is archive entries (docs/TransactionLog.md sec 27.50 item 1).
    // It is the file's, not a version's: the log keeps it once (item 2).
    struct TableGuard
    {
        DocumentP* d;
        ~TableGuard()
        {
            d->tableAsMember = false;
            d->tableMember.reset();
            d->tableEntry.clear();
        }
    } tableGuard {d};
    d->tableAsMember = writer.getSchemaVersion() >= 5
        && getFileBlobManager().blobFormat() == FileBlobManager::BlobFormat::Entries;
    d->captures.clear();
    if (log) {
        writer.setPropertySink(log->beginSnapshot(false));
        writer.setEntrySink([this](const std::string& name, Base::EntryCapture capture) {
            if (name == d->tableEntry)
                return;
            d->captures.emplace_back(name, std::move(capture));
        });
        writer.beginCapture();
    }

    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<!--\n"
                    << " FreeCAD Document, see http://www.freecadweb.org for more information...\n"
                    << "-->\n";
    Document::Save(writer);
    writer.endCapture("Document.xml");

    // The included files, one entry per distinct content, straight behind
    // Document.xml and ahead of every entry the file channel will add.
    const double tObjects = phaseSplit();
    const size_t objectSteps = d->saveSeq ? d->saveSeq->progress() : 0;
    getFileBlobManager().writeBlobs(writer);
    const size_t blobSteps = (d->saveSeq ? d->saveSeq->progress() : 0) - objectSteps;

    // Special handling for Gui document.
    signalSaveDocument(writer);

    // write additional files
    const double tBlobs = phaseSplit();
    const size_t beforeFiles = d->saveSeq ? d->saveSeq->progress() : 0;
    writer.writeFiles();
    const double tFiles = phaseSplit();
    if (d->saveSeq) {
        FC_LOG("save " << getName() << ": " << objectSteps << " steps to the objects, "
                << blobSteps << " blob entries, "
                << (d->saveSeq->progress() - beforeFiles) << " file entries, of "
                << d->saveSeq->numberOfSteps() << " reported; begin " << tBegin
                << "s, collect " << tCollect << "s, objects " << tObjects
                << "s, blobs " << tBlobs << "s, files " << tFiles << "s");
    }

    if (writer.hasErrors()) {
        THROWM(Base::FileException, "Failed to write all data to file")
    }

    if (log) {
        writer.setEntrySink(nullptr);
        writer.setPropertySink(nullptr);
        // Named as the archive names them (docs/TransactionLog.md sec
        // 23.16), which is what pairs a property's file with its next one.
        TransactionLog::Blobs blobs = getFileBlobManager().versionEntries();
        TransactionLog::Captures entries = std::move(d->captures);
        d->captures.clear();
        if (log->onSave(FileName.getValue(), entries, blobs, writer.getSchemaVersion()))
            const_cast<Document*>(this)->noteVersionTaken();
    }

    if (d->restoreHistory) {
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        d->restoreHistory();
        d->restoreHistory = nullptr;
    }

    GetApplication().signalSaveDocument(*this);
}

bool Document::isAnyRestoring() {
    return globalIsRestoring;
}

// Open the document
void Document::restore (const char *filename,
        bool delaySignal, const std::vector<std::string> &objNames)
{
    if(!filename)
        filename = FileName.getValue();
    Base::FileInfo fi(filename);
    if(fi.isDir()) {
        fi.setFile(std::string(filename)+'/'+"Document.xml");
        if(!fi.exists()) 
            throw Base::FileException("Project file not found", fi);
    }

    std::unique_ptr<Base::Reader> _reader;
    std::shared_ptr<Base::ZipFileReader> zfreader;
    std::unique_ptr<Base::XMLReader> _xmlReader;
    std::unique_ptr<zipios::ZipInputStream> zipstream;
    std::string dirname;

    // Whatever was parked from a previous restore of this document is
    // gone with the objects it belonged to.
    d->deferredFiles.clear();
    d->archiveReader.reset();
    d->deferServeSeq.reset();

    // The log's version 1 is the file as found (docs/TransactionLog.md sec
    // 16.6): tap Document.xml on its way through the parser, which has to
    // be in place before the XML reader takes its first chunk. Ended by
    // restore(XMLReader&) once the element is parsed, and read there.
    d->restoreDocXml.clear();
    d->restoreTapped = false;
    d->fileEntries.clear();
    auto tap = [this, &objNames](Base::Reader& reader) {
        // A partial document is never snapshotted (sec 16.1), and a
        // checkout is a version already.
        if (!objNames.empty() || d->checkingOut || !getTransactionLog() || d->joinedHistory)
            return;
        reader.beginTap([this](const char* p, std::size_t n) { d->restoreDocXml.append(p, n); });
        d->restoreTapped = true;
    };
    // The other XML entries (GuiDocument.xml, split object files) as the
    // readers serve them, for the same manifest.
    auto sink = [this](Base::XMLReader& xmlReader) {
        if (!d->restoreTapped)
            return;
        xmlReader.setEntrySink([this](const std::string& name, std::string bytes) {
            d->fileEntries.emplace_back(name, std::move(bytes));
        });
    };

    if(fi.fileNamePure() == "Document" && fi.hasExtension("xml")) {
        Base::FileInfo di(fi.dirPath());
        _reader.reset(new Base::FileReader(fi,di.fileName()+"/Document.xml"));
        tap(*_reader);
        _xmlReader.reset(new Base::XMLReader(*_reader));
        sink(*_xmlReader);
    } else {
        if (DocumentParams::getArchiveRandomAccess()) {
            try {
                zfreader = std::make_shared<Base::ZipFileReader>(filename);
            } catch (Base::Exception &e) {
                // An archive the central-directory index cannot digest may
                // still open the old way (and if not, the forward walk
                // produces the error the user should see).
                FC_WARN("Archive random access unavailable for " << filename
                        << " (" << e.what() << "), falling back");
            }
        }
        Base::Reader *reader;
        if (zfreader) {
            reader = zfreader.get();
        } else {
            zipstream.reset(new zipios::ZipInputStream(filename));
            _reader.reset(new Base::ZipReader(*zipstream,filename));
            reader = _reader.get();
        }
        tap(*reader);
        _xmlReader.reset(new Base::XMLReader(*reader));
        sink(*_xmlReader);
        if (zfreader && DocumentParams::getDeferShapeLoad()) {
            // Park opted-in entries instead of serving them during the
            // walk; the index stays behind for restoreDeferredFile().
            d->archiveReader = zfreader;
            _xmlReader->setFileDeferrer(
                [this](const std::string &name, Base::Persistence *obj) {
                    auto prop = dynamic_cast<App::Property*>(obj);
                    if (!prop || !prop->canDeferRestore() || !prop->hasName())
                        return false;
                    auto owner = dynamic_cast<App::DocumentObject*>(prop->getContainer());
                    if (!owner || !owner->getNameInDocument()
                               || owner->getDocument() != this)
                        return false;
                    d->deferredFiles[std::make_pair(
                            std::string(owner->getNameInDocument()),
                            std::string(prop->getName()))] = name;
                    prop->setRestorePending(true);
                    return true;
                });
        }
    }

    restore(*_xmlReader, delaySignal, objNames);
}

bool Document::hasDeferredFile(const Base::Persistence *obj) const
{
    if (d->deferredFiles.empty())
        return false;
    auto prop = dynamic_cast<const App::Property*>(obj);
    auto owner = prop ? dynamic_cast<const App::DocumentObject*>(prop->getContainer()) : nullptr;
    if (!owner || !owner->getNameInDocument() || !prop->hasName())
        return false;
    return d->deferredFiles.count(std::make_pair(
                std::string(owner->getNameInDocument()),
                std::string(prop->getName()))) != 0;
}

bool Document::hasDeferredFiles() const
{
    return !d->deferredFiles.empty();
}

bool Document::restoreDeferredFile(Base::Persistence *obj)
{
    if (d->deferredFiles.empty())
        return false;
    auto prop = dynamic_cast<App::Property*>(obj);
    auto owner = prop ? dynamic_cast<App::DocumentObject*>(prop->getContainer()) : nullptr;
    if (!owner || !owner->getNameInDocument() || !prop->hasName())
        return false;
    auto it = d->deferredFiles.find(std::make_pair(
                std::string(owner->getNameInDocument()),
                std::string(prop->getName())));
    if (it == d->deferredFiles.end())
        return false;
    std::string name = std::move(it->second);
    // Erased before serving: a reentrant ask from inside RestoreDocFile
    // must find nothing rather than recurse.
    d->deferredFiles.erase(it);
    prop->setRestorePending(false);

    auto archive = d->archiveReader;
    if (!archive) {
        FC_ERR("Deferred entry " << name << " of " << prop->getFullName()
                << " lost: no archive index");
        return false;
    }
    FC_TIME_INIT(tServe);
    // Opening is as fallible as reading: an archive truncated, replaced or
    // deleted since the load throws from here, and this runs inside a
    // timer slice and inside arbitrary const accessors -- neither of which
    // may be left to unwind.
    std::unique_ptr<zipios::ZipInputStream> stream;
    try {
        stream = archive->openEntry(name);
    } catch (const std::exception &e) {
        FC_ERR("Deferred entry " << name << " of " << prop->getFullName()
                << " unreadable from " << archive->getFileName()
                << ": " << e.what());
        return false;
    }
    if (!stream) {
        FC_ERR("Deferred entry " << name << " of " << prop->getFullName()
                << " missing from " << archive->getFileName());
        return false;
    }
    FC_DURATION_PLUS(d->deferOpenTime, tServe);
    // Reproduce load-time conditions: observers see a restoring object,
    // and the owner does not come out touched by being served.
    bool wasTouched = owner->isTouched();
    {
        Base::ObjectStatusLocker<ObjectStatus, DocumentObject> guard(
                ObjectStatus::Restore, owner);
        try {
            Base::ZipReader zipreader(*stream, name);
            obj->RestoreDocFile(zipreader);
        } catch (Base::Exception &e) {
            e.ReportException();
            FC_ERR("Reading failed from deferred embedded file: " << name);
        } catch (...) {
            FC_ERR("Reading failed from deferred embedded file: " << name);
        }
    }
    FC_DURATION_PLUS(d->deferRestoreTime, tServe);
    // No change notification is replayed here: the serve runs before the
    // visual fill (runDeferredVisualSlice's pre-phase), so consumers pick
    // the shape up when they build -- and a per-serve signal costs
    // per-object GUI work (tree, property view) that multiplies into
    // minutes across a large document.
    if (!wasTouched)
        owner->purgeTouched();
    if (d->deferredFiles.empty())
        d->archiveReader.reset();
    return true;
}

void Document::cancelDeferredFile(Base::Persistence *obj)
{
    if (d->deferredFiles.empty())
        return;
    auto prop = dynamic_cast<App::Property*>(obj);
    auto owner = prop ? dynamic_cast<App::DocumentObject*>(prop->getContainer()) : nullptr;
    if (!owner || !owner->getNameInDocument() || !prop->hasName())
        return;
    if (d->deferredFiles.erase(std::make_pair(
                std::string(owner->getNameInDocument()),
                std::string(prop->getName())))) {
        prop->setRestorePending(false);
        if (d->deferredFiles.empty()) {
            d->archiveReader.reset();
            d->deferServeSeq.reset();
        }
    }
}

void Document::flushDeferredFiles()
{
    // The drain's own sequence, if a slice left one running, is about to have
    // its backlog taken away from under it -- and while it lives it is the
    // outermost sequence on this thread, so it, not the save, is what the
    // consolidated status bar reports. Retire it here and let the save's
    // sequence, which spans this loop, be the one the user sees.
    d->deferServeSeq.reset();
    while (!d->deferredFiles.empty()) {
        if (d->saveSeq) {
            d->saveSeq->next();
        }
        auto key = d->deferredFiles.begin()->first;
        auto oit = d->objectMap.find(key.first);
        App::Property *prop = oit == d->objectMap.end() ? nullptr
            : oit->second->getPropertyByName(key.second.c_str());
        if (!prop || !prop->canDeferRestore() || !restoreDeferredFile(prop)) {
            // Owner gone or renamed away -- nothing left to serve it to.
            d->deferredFiles.erase(key);
        }
    }
    d->archiveReader.reset();
    d->deferServeSeq.reset();
}

bool Document::serveDeferredFiles(double budgetSeconds)
{
    if (d->deferredFiles.empty())
        return false;
    if (!d->deferServeSeq)
        // This sequence outlives its slice: it is reported across every
        // return to the event loop until the last entry is served. An
        // ordinary main-thread sequence would have the indicator grab the
        // input for all of it -- wait cursor, clicks beeping, no
        // navigation -- which is exactly what the progressive load exists
        // to avoid. It reports; it does not take the window away.
        d->deferServeSeq = std::make_unique<Base::SequencerLauncher>(
                "Loading shapes...", d->deferredFiles.size(),
                Base::SequencerLauncher::KeepInteractive);
    auto start = std::chrono::steady_clock::now();
    std::size_t served = 0;
    while (!d->deferredFiles.empty()) {
        auto key = d->deferredFiles.begin()->first;
        auto oit = d->objectMap.find(key.first);
        App::Property *prop = oit == d->objectMap.end() ? nullptr
            : oit->second->getPropertyByName(key.second.c_str());
        if (!prop || !prop->canDeferRestore() || !restoreDeferredFile(prop))
            d->deferredFiles.erase(key);
        ++served;
        // Serving one entry can cancel another (a consumer overwriting a
        // parked value), and the cancel that empties the map drops this
        // sequence with it.
        if (d->deferServeSeq)
            d->deferServeSeq->next();
        if (std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count()
                >= budgetSeconds)
            break;
    }
    FC_LOG("deferred serve slice " << getName() << ": " << served << " in "
            << std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start).count()
            << "s (open " << d->deferOpenTime.count()
            << "s, restore " << d->deferRestoreTime.count()
            << "s cumulative), " << d->deferredFiles.size() << " pending");
    if (d->deferredFiles.empty()) {
        d->archiveReader.reset();
        d->deferServeSeq.reset();
        return false;
    }
    return true;
}

namespace {
void addUntappedMembers(const std::string& path,
                        std::vector<std::pair<std::string, std::string>>& entries);
}

void Document::restore(Base::XMLReader &reader,
        bool delaySignal, const std::vector<std::string> &objNames)
{
    if (!reader.isValid())
        throw Base::FileException("Error reading project file", FileName.getValue());

    clearUndos();
    d->files.clear();
    // Stale parked entries must never resolve against the new objects
    // (same names, different content). The archive index itself is
    // managed by the caller that installed it.
    d->deferredFiles.clear();
    bool signal = false;
    Document *activeDoc = GetApplication().getActiveDocument();
    if (!d->objectArray.empty()) {
        signal = true;
        GetApplication().signalDeleteDocument(*this);
        d->clearDocument();
    }

    Base::FlagToggler<> flag(globalIsRestoring, false);

    setStatus(Document::PartialDoc,false);

    d->clearRecomputeLog();
    d->objectArray.clear();
    d->objectMap.clear();
    d->objectIdMap.clear();
    d->lastObjectId = 0;

    if(signal) {
        GetApplication().signalNewDocument(*this,true);
        if(activeDoc == this)
            GetApplication().setActiveDocument(this);
    }


    GetApplication().signalStartRestoreDocument(*this);
    setStatus(Document::Restoring, true);

    d->restoreTiming.clear();
    FC_TIME_INIT(tRestore);

    // Claim the included-file entries out of the archive. The properties that
    // refer to them queue up as they are parsed and are served once the
    // entries have been drained, which readFiles() does before anything else.
    getFileBlobManager().beginRestore(reader);

    d->partialLoadObjects.clear();
    for(auto &name : objNames)
        d->partialLoadObjects.emplace(name,true);
    try {
        Document::Restore(reader);
    } catch (const Base::XMLParseException &) {
        endRestoreTap(reader);
        throw;
    } catch (const Base::Exception& e) {
        Base::Console().Error("Invalid Document.xml: %s\n", e.what());
        setStatus(Document::RestoreError, true);
    }
    // Before readFiles() moves a forward-only archive reader off the entry.
    endRestoreTap(reader);

    d->partialLoadObjects.clear();
    d->programVersion = reader.ProgramVersion;

    // Special handling for Gui document, the view representations must already
    // exist, what is done in Restore().
    // Note: This file doesn't need to be available if the document has been created
    // without GUI. But if available then follow after all data files of the App document.
    signalRestoreDocument(reader);

    FC_DURATION_DECL_INIT(dXml);
    FC_DURATION_PLUS(dXml, tRestore);

    FC_TIME_INIT(tFiles);
    reader.readFiles();
    FC_DURATION_PLUS(d->restoreTiming.files, tFiles);

    // Hand the restored content to the properties waiting for it. Referrers
    // that appear later -- a view document replayed from its embedded string
    // -- take theirs straight from the store, which holds it until
    // afterRestore() lets go.
    getFileBlobManager().dispatchPending();

    // The default stand-ins have served their purpose and nothing points at
    // them any more -- including the reader, which has just drained whatever
    // they registered.
    d->restoreDefaults.clear();

    for(auto &f : reader.getFilenames()) {
        FC_TRACE("document " << getName() << " file: " << f);
        d->files.insert(f);
    }

    if (reader.testStatus(Base::XMLReader::ReaderStatus::PartialRestore)) {
        setStatus(Document::PartialRestore, true);
        Base::Console().Error("There were errors while loading the file. Some data might have been modified or not recovered at all. Look above for more specific information about the objects involved.\n");
    }

    // The history initialised from the file (docs/TransactionLog.md sec
    // 16.6): version 1 is Document.xml as tapped plus every blob the file
    // carried, now that readFiles() has them in the store. Not for a
    // Document.xml the loader could not read -- that is no version to go
    // back to -- and never for a partial document, which was not tapped.
    if (d->restoreTapped) {
        d->restoreTapped = false;
        reader.setEntrySink(nullptr);
        // A history embedded in the file continues here when the guard
        // agrees; the snapshot below then becomes the version it expects.
        if (!testStatus(Document::RestoreError))
            adoptEmbeddedHistory();
        std::vector<std::pair<std::string, std::string>> entries;
        entries.emplace_back("Document.xml", std::move(d->restoreDocXml));
        d->restoreDocXml.clear();
        for (auto& e : d->fileEntries)
            entries.push_back(std::move(e));
        d->fileEntries.clear();
        TransactionLog* log = testStatus(Document::RestoreError) ? nullptr : getTransactionLog();
        if (log && reader.DocumentSchema < getCurrentSchemaVersion()) {
            // An older format is not what the log keeps (sec 27.62): the file
            // as found is recorded once the restore is over, serialised at
            // the current schema (afterRestore()).
            d->openVersionPending = true;
        }
        else if (log) {
            // Under the names the file gave them, which are the names the
            // next save gives them (sec 23.16).
            TransactionLog::Blobs blobs =
                TransactionLog::versionBlobs(entries, getFileBlobManager().restoredEntries());
            addUntappedMembers(FileName.getValue(), entries);
            if (log->onRestore(FileName.getValue(), entries, blobs, reader.DocumentSchema))
                noteVersionTaken();
        }
    }
    // Undo reaches back to here and no further, whatever branch the
    // document is later switched to (docs/TransactionLog.md sec 26.4).
    if (auto log = getTransactionLog())
        d->undoFloor = log->lastSeq();

    FC_DURATION_DECL_INIT(dAfter);
    if(!delaySignal) {
        FC_TIME_INIT(tAfter);
        afterRestore();
        FC_DURATION_PLUS(dAfter, tAfter);
    }

    // One line per load, split by stage. 'xml' is the whole Document.xml pass,
    // of which 'create' (the <Objects> pass that instantiates each object) and
    // 'data' (the <ObjectData> pass that restores properties) are the parts
    // worth separating; 'files' is the archive bulk -- shapes and anything
    // else that queued an addFile(); 'after' is the link/expression fixup, and
    // carries the Gui visual build with it when a Gui document is attached --
    // but only when this call ran it. Opening a document defers it, and
    // Application::openDocuments then times it as 'postprocess'.
    auto &rt = d->restoreTiming;
    FC_LOG("restore " << getName() << ": " << rt.objectCount << " objects, "
            << d->files.size() << " files"
            << ", xml " << dXml.count()
            << " (create " << rt.create.count()
            << " [addObject " << rt.createAdd.count() << "s, sequencer "
            << rt.createSeq.count() << "s]"
            << ", data " << rt.data.count()
            << " [" << rt.props.count << " properties, "
            << rt.props.total.count() << "s of which value "
            << rt.props.value.count() << "s]" << ')'
            << ", files " << rt.files.count()
            << ", after " << dAfter.count()
            << ", total " << (dXml + rt.files + dAfter).count() << 's');
}

void Document::embedHistory(bool archive)
{
    // Two document-level dynamic properties, NoModify so that setting them
    // here opens no transaction and touches nothing (sec 16.4): `History`
    // holds the copy and the retained manifests' blobs, `Version` the
    // number this save becomes and the save id the guard on open compares.
    // Written by the save, not by the user: no transaction opens for them,
    // and an open one does not record them.
    Base::FlagToggler<> quiet(d->bookkeeping, false);
    auto history = Base::freecad_dynamic_cast<PropertyHistory>(getPropertyByName("History"));
    auto version = Base::freecad_dynamic_cast<PropertyString>(getPropertyByName("Version"));
    // With the history when the preference says so, and always for a file
    // another document pins a version of (docs/TransactionLog.md sec 27.5
    // ruling 1): the pinned version must travel.
    TransactionLog* log = archive && !d->savingWithoutHistory ? getTransactionLog() : nullptr;
    if (log && DocumentParams::getTransactionLog() != 2 && !isPinned())
        log = nullptr;
    if (!log) {
        // Not embedding: a property left from an earlier embedded save is
        // emptied rather than carried on with stale content. For a copy
        // without history the emptying is for the copy only: the live
        // document keeps what it had.
        if (history && !history->isEmpty()) {
            if (d->savingWithoutHistory) {
                FileBlobHandle db = history->getDatabase();
                std::vector<FileBlobHandle> blobs = history->getBlobs();
                std::vector<std::string> exts;
                for (const auto& e : history->getEntries()) {
                    if (e.ext != ".db")
                        exts.push_back(e.ext);
                }
                history->setValue({}, {}, {});
                d->restoreHistory = [history, db, blobs, exts]() {
                    history->setValue(db, blobs, exts);
                };
            }
            else {
                history->setValue({}, {}, {});
            }
        }
        // A copy written without the history says which state of it the
        // copy is (sec 30.19 G2): a save id of its own, recorded at the row
        // the document is at, so the copy comes back against the state it
        // left with. For the copy only: the live document keeps its own.
        TransactionLog* mine = archive && d->savingWithoutHistory ? getTransactionLog() : nullptr;
        if (mine) {
            if (!version) {
                version = Base::freecad_dynamic_cast<PropertyString>(addDynamicProperty(
                    "App::PropertyString", "Version", "Base",
                    "The version of the transaction log this file is, and its save id",
                    Prop_Hidden | Prop_ReadOnly));
                if (version)
                    version->setStatus(Property::NoModify, true);
            }
            if (version) {
                const std::string was = version->getValue();
                version->setValue(mine->noteSave());
                auto restore = d->restoreHistory;
                d->restoreHistory = [restore, version, was]() {
                    if (restore)
                        restore();
                    version->setValue(was);
                };
            }
        }
        return;
    }
    try {
        TransactionLog::Embedded copy = log->embed(LastModifiedDate.getValue());
        auto& manager = getFileBlobManager();
        FileBlobHandle db = manager.adoptFile(copy.path.c_str(), "db");
        std::vector<FileBlobHandle> blobs;
        std::vector<std::string> exts;
        for (const auto& b : copy.blobs) {
            auto blob = manager.find(b.first);
            if (!blob) {
                FC_WARN("embedded history of " << getName() << ": blob " << b.first
                        << " of a named version is not in the store");
                continue;
            }
            blobs.push_back(blob);
            exts.push_back(b.second);
        }
        if (!history) {
            history = Base::freecad_dynamic_cast<PropertyHistory>(addDynamicProperty(
                "App::PropertyHistory", "History", "Base",
                "The embedded transaction log (docs/TransactionLog.md sec 16.4)",
                Prop_Hidden | Prop_ReadOnly));
            if (history)
                history->setStatus(Property::NoModify, true);
        }
        if (!version) {
            version = Base::freecad_dynamic_cast<PropertyString>(addDynamicProperty(
                "App::PropertyString", "Version", "Base",
                "The version of the transaction log this file is, and its save id",
                Prop_Hidden | Prop_ReadOnly));
            if (version)
                version->setStatus(Property::NoModify, true);
        }
        // `Branch` (sec 17.1, 26): the branch this file is. The copy's
        // `meta` already names it for the log; this is for anyone reading
        // the file, a FreeCAD that knows no log included.
        auto branch = Base::freecad_dynamic_cast<PropertyString>(getPropertyByName("Branch"));
        if (!branch) {
            branch = Base::freecad_dynamic_cast<PropertyString>(addDynamicProperty(
                "App::PropertyString", "Branch", "Base",
                "The branch of the transaction log this file is",
                Prop_Hidden | Prop_ReadOnly));
            if (branch)
                branch->setStatus(Property::NoModify, true);
        }
        if (!history || !version || !branch)
            THROWM(Base::RuntimeError, "cannot add the history properties");
        history->setValue(db, blobs, exts);
        version->setValue(std::to_string(copy.version) + " " + copy.saveId);
        LogBranch current;
        if (log->store().getBranch(log->branch(), current))
            branch->setValue(current.name);
    }
    catch (Base::Exception& e) {
        FC_ERR("embedding the history of " << getName() << " failed: " << e.what());
    }
}

bool Document::adoptEmbeddedHistory()
{
    // The guard of sec 16.4 (as planned in sec 21): the file carries a
    // history, and continues from it only if the save that wrote both
    // sides is the one the file is from -- the save id in `Version` and
    // the copy's meta agree, and so does the date the save stamped. A
    // FreeCAD that knows nothing of the log restamps the date.
    auto history = Base::freecad_dynamic_cast<PropertyHistory>(getPropertyByName("History"));
    auto version = Base::freecad_dynamic_cast<PropertyString>(getPropertyByName("Version"));
    if (!history || history->isEmpty() || !version)
        return false;
    TransactionLog* log = getTransactionLog();
    if (!log)
        return false;
    history->setStatus(Property::NoModify, true);
    version->setStatus(Property::NoModify, true);
    if (auto branch = getPropertyByName("Branch"))
        branch->setStatus(Property::NoModify, true);
    std::string saveId;
    {
        std::istringstream in(version->getValue());
        int64_t num = 0;
        in >> num >> saveId;
    }
    try {
        auto copy = TransactionStore::openSQLite(history->getDatabase()->path());
        const std::string id = copy->getMeta("save_id");
        const std::string date = copy->getMeta("save_date");
        copy.reset();
        if (id.empty() || id != saveId || date != LastModifiedDate.getValue()) {
            // Sec 16.6, 26.2 item 7: the file was edited elsewhere. The
            // history is kept, closed, and a new `main` starts from the
            // file as found, numbered on.
            FC_WARN("the embedded history of " << getName()
                    << " is not the file's (edited elsewhere?): kept as closed branches");
            return log->adoptClosed(history->getDatabase()->path());
        }
        return log->adoptStore(history->getDatabase()->path());
    }
    catch (Base::Exception& e) {
        FC_ERR("embedded history of " << getName() << ": " << e.what());
    }
    return false;
}

int64_t Document::snapshotToLog()
{
    return _snapshotToLog("snapshot");
}

int64_t Document::_snapshotToLog(const char* kind)
{
    TransactionLog* log = getTransactionLog();
    if (!log || d->snapshotting || testStatus(PartialDoc) || testStatus(Restoring)
            || isPerformingTransaction() || d->activeUndoTransaction)
        return 0;
    Base::FlagToggler<> guard(d->snapshotting);
    // A save's serialisation with the archive left out (what AutoSaver did
    // for recovery, sec 22.1): the blobs are made in the store, Document.xml
    // and the Gui entry stream through the taps, and the log gets the
    // version with the manifest a save would give it.
    try {
        // Configured as save() configures an archive's writer -- the
        // writer's defaults, not the directory layout's -- so that a part
        // composed here is the bytes a save writes (sec 23.3).
        // At the current schema, whatever the document is saved as: the log
        // holds no other (sec 27.62).
        Base::NullWriter writer;
        writer.setSchemaVersion(getCurrentSchemaVersion());
        if (PreferBinary.getValue()) {
            writer.setMode("BinaryBrep");
            writer.setPreferBinary(true);
        }
        else {
            writer.setPreferBinary(false);
        }
        getFileBlobManager().beginSave(writer);
        collectFileBlobs();

        // Compose mode (sec 23.3): the sink claims every property whose
        // value the log holds current and its Save is skipped; what is
        // captured is the skeleton, the misses, and under verification
        // the claimed bodies too.
        d->captures.clear();
        // The log's table stands for the one a save writes as a member
        // (docs/TransactionLog.md sec 27.50 item 2).
        Base::FlagToggler<> tableAsMember(d->tableAsMember, false);
        writer.setPropertySink(log->beginSnapshot(true));
        writer.setEntrySink([this](const std::string& name, Base::EntryCapture capture) {
            d->captures.emplace_back(name, std::move(capture));
        });
        writer.putNextEntry("Document.xml");
        writer.beginCapture();
        writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                        << "<!--\n"
                        << " FreeCAD Document, see http://www.freecadweb.org for more information...\n"
                        << "-->\n";
        Document::Save(writer);
        writer.endCapture("Document.xml");
        signalSaveDocument(writer);
        writer.writeFiles();
        writer.setEntrySink(nullptr);
        writer.setPropertySink(nullptr);

        TransactionLog::Blobs blobs = getFileBlobManager().versionEntries();
        TransactionLog::Captures entries = std::move(d->captures);
        d->captures.clear();
        int64_t num = log->onSnapshot(entries, blobs, writer.getSchemaVersion(), kind);
        if (num)
            noteVersionTaken();
        return num;
    }
    catch (Base::Exception& e) {
        FC_ERR("snapshot of " << getName() << " failed: " << e.what());
    }
    return 0;
}

std::string Document::_materialiseVersion(int64_t num, const std::string& where)
{
    if (!getTransactionLog())
        THROWM(Base::RuntimeError, "no such version");
    const std::string dir =
        where.empty() ? TransientDir.getStrValue() + "/history/checkout" : where;
    return materialiseVersion(TransactionLogCore::of(getFileHistory()), num, dir);
}

std::string Document::materialiseVersion(TransactionLogCore& log, int64_t num, const std::string& dir,
                                         bool blobsInStore)
{
    // An unpacked project: every entry from the log, the blobs under
    // blobs/, which a directory restore reads by content. Every entry is
    // read as bytes -- a blob the log holds as a file through read(), one
    // it keeps as a delta (sec 23.16) decoded -- never copied by path.
    LogVersion version;
    if (!log.store().getVersion(num, version))
        THROWM(Base::RuntimeError, "no such version");
    auto manifest = log.store().manifest(num);
    Base::FileInfo(dir).deleteDirectoryRecursive();
    if (!Base::FileInfo(dir + "/" + FileBlobManager::archivePrefix()).createDirectories())
        THROWM(Base::RuntimeError, "cannot create the checkout directory");
    bool haveDocXml = false;
    // A schema-5 version names its blobs by content, and a restore finds a
    // blob the store already has by its hash: no file written, none hashed
    // again on the way back in (sec 27.25 item 2). One that is not live is
    // made so by restoreBlob, which the log then holds.
    const bool inStore = blobsInStore && version.schema >= 5;
    size_t written = 0, fromStore = 0;
    if (inStore)
        log.flush();
    for (const auto& e : manifest) {
        LogEntity entity;
        if (!log.store().getEntity(e.hash, entity))
            THROWM(Base::RuntimeError, "version entry " + e.entry + " of version "
                                           + std::to_string(num) + " is not in the store");
        const bool isBlob = entity.kind == "blob";
        // Live: held by whoever holds it, which the restore does not touch.
        if (isBlob && inStore
                && (log.liveBlob(e.hash)
                    || log.restoreBlob(e.hash, Base::FileInfo(e.entry).extension()))) {
            ++fromStore;
            continue;
        }
        ++written;
        Base::FileInfo target(dir + "/" + (isBlob ? FileBlobManager::archivePrefix() : "")
                              + e.entry);
        CapturedValue v;
        if (!log.readValue(e.hash, v))
            THROWM(Base::RuntimeError, "version entry " + e.entry + " cannot be read");
        if (e.entry.find('/') != std::string::npos)
            Base::FileInfo(target.dirPath()).createDirectories();
        Base::ofstream out(target, std::ios::out | std::ios::binary);
        out.write(v.fragment.data(), static_cast<std::streamsize>(v.fragment.size()));
        if (!out)
            THROWM(Base::RuntimeError, "cannot write " + e.entry);
        if (e.entry == "Document.xml")
            haveDocXml = true;
    }
    if (!haveDocXml)
        THROWM(Base::RuntimeError, "version has no Document.xml");
    FC_LOG("version " << num << " materialised: " << written << " entries written, " << fromStore
                      << " blobs from the store");
    return dir;
}

namespace {

/// Document properties a restore to a version leaves alone (sec 24.5):
/// where the document lives and who it is, which a version of it does
/// not change, and what the log itself keeps there. The label is who it
/// is too: the document a version is read into has its own, and a switch or
/// a restore gave the document that name (sec 27.11).
bool keptOnRestore(const char* name)
{
    static const std::set<std::string> kept {"FileName", "TransientDir", "Uid", "Id", "Label",
        "History", "Version", "Branch", "LastModifiedBy", "LastModifiedDate", "CreatedBy",
        "CreationDate"};
    return kept.count(name) != 0;
}

/// Make `blob`, a file of another document's store, a file of `manager`
/// too, with every file it borrows from; by bytes, so wherever it lives.
/// The handles go into `held`: a blob nobody holds is gone at once, and the
/// referrer restored next finds it by hash.
void copyBlob(FileBlobManager& manager, const FileBlobManager& from, const FileBlobHandle& blob,
              std::vector<FileBlobHandle>& held, int depth = 0)
{
    if (!blob || depth > 1024)
        return;
    FileBlobHandle mine = manager.find(blob->hash());
    if (!mine) {
        std::string bytes;
        if (!blob->read(bytes))
            throw Base::RuntimeError("cannot read blob " + blob->hash());
        std::string ext = blob->extension();
        mine = manager.adoptBytes(bytes, ext.empty() ? nullptr : ext.c_str());
    }
    held.push_back(mine);
    for (const auto& hash : blob->sources())
        copyBlob(manager, from, from.find(hash), held, depth + 1);
}

} // namespace

void Document::_applyVersion(Document& version, bool views)
{
    // docs/TransactionLog.md sec 24.5, in the passes of 24.3: what the
    // version lacks goes, what it has comes (under its id and name), the
    // dynamic properties follow, then every value that differs.
    auto guarded = [&](const std::string& what, const std::function<void()>& fn) {
        try {
            fn();
        }
        catch (Base::Exception& e) {
            FC_ERR("restore to a version, " << what << ": " << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("restore to a version, " << what << ": " << e.what());
        }
    };
    std::map<long, DocumentObject*> target;
    for (auto obj : version.getObjects())
        target[obj->getID()] = obj;

    // 1. Objects the version does not have, or has as another type.
    std::vector<std::string> gone;
    for (auto obj : getObjects()) {
        auto it = target.find(obj->getID());
        if (it == target.end() || it->second->getTypeId() != obj->getTypeId()
                || strcmp(it->second->getNameInDocument(), obj->getNameInDocument()) != 0)
            gone.emplace_back(obj->getNameInDocument());
    }
    for (const auto& name : gone)
        guarded(name, [&]() {
            if (getObject(name.c_str()))
                removeObject(name.c_str());
        });

    // 2. Objects the version has, under its id and name.
    for (auto& kv : target) {
        if (getObjectByID(kv.first))
            continue;
        const char* name = kv.second->getNameInDocument();
        guarded(name, [&]() {
            if (getObject(name))
                throw Base::RuntimeError("name taken by another object");
            auto obj = static_cast<DocumentObject*>(kv.second->getTypeId().createInstance());
            if (!obj)
                throw Base::RuntimeError("cannot create the object");
            obj->_Id = kv.first;
            addObject(obj, name, false);
        });
    }

    // 3. The dynamic properties of every container, then 4. every value: a
    // value's restore can read another container's properties -- a
    // copy-on-change link restored onto its copy mirrors the copy's
    // CopyOnChange ones, and drops its own the copy does not have yet
    // (sec 27.68).
    CaptureConfig config(*this);
    auto& manager = getFileBlobManager();
    std::vector<FileBlobHandle> held;
    const auto& fromManager = version.getFileBlobManager();
    auto restoreContainer = [&](PropertyContainer& live, const PropertyContainer& from,
                                bool isDocument, bool values) {
        std::map<std::string, Property*> want, have;
        from.getPropertyMap(want);
        live.getPropertyMap(have);
        for (auto& kv : have) {
            if (values)
                break;
            if (want.count(kv.first) || live.getDynamicPropertyData(kv.second).name.empty())
                continue;
            guarded(kv.first, [&]() { live.removeDynamicProperty(kv.first.c_str()); });
        }
        for (auto& kv : want) {
            short type = from.getPropertyType(kv.second);
            if ((type & Prop_Transient) || (type & Prop_NoPersist))
                continue;
            if (isDocument && keptOnRestore(kv.first.c_str()))
                continue;
            guarded(kv.first, [&]() {
                Property* prop = live.getPropertyByName(kv.first.c_str());
                if (!values) {
                    if (prop)
                        return;
                    auto dyn = from.getDynamicPropertyData(kv.second);
                    if (dyn.name.empty())
                        return;
                    prop = live.addDynamicProperty(kv.second->getTypeId().getName(),
                                                   kv.first.c_str(), dyn.group.c_str(),
                                                   dyn.getDoc(), dyn.attr, dyn.readonly,
                                                   dyn.hidden);
                    if (!prop)
                        return;
                    // With its status, CopyOnChange among it (sec 27.67).
                    Property::StatusBits status(kv.second->getStatus());
                    status.reset(Property::User1);
                    status.reset(Property::User2);
                    status.reset(Property::User3);
                    status.set(Property::Touched, prop->testStatus(Property::Touched));
                    prop->setStatusValue(status.to_ulong());
                    return;
                }
                if (!prop)
                    return;
                CapturedValue want = captureValue(config, *kv.second);
                if (!want.ok)
                    throw Base::RuntimeError("cannot read the version's value");
                CapturedValue now = captureValue(config, *prop);
                if (now.ok && now.fragment == want.fragment
                        && now.attachments.size() == want.attachments.size()
                        && std::equal(now.attachments.begin(), now.attachments.end(),
                                      want.attachments.begin(),
                                      [](const auto& a, const auto& b) {
                                          return a.name == b.name && a.bytes == b.bytes;
                                      }))
                    return;
                for (const auto& blob : want.blobs)
                    copyBlob(manager, fromManager, blob, held);
                if (auto referrer = dynamic_cast<const BlobReferrerProperty*>(kv.second))
                    copyBlob(manager, fromManager, referrer->contentBlob(), held);
                restoreValue(*prop, want);
            });
        }
    };
    for (bool values : {false, true}) {
        RestoreBatch batch;
        restoreContainer(*this, version, true, values);
        for (auto& kv : target) {
            auto obj = getObjectByID(kv.first);
            if (obj)
                restoreContainer(*obj, *kv.second, false, values);
        }
    }
    // View providers (sec 24.9), where view state is undo state: under
    // ViewObjectTransaction, the setting that has a view provider's change
    // open a transaction of its own. The version document's are the
    // version's, restored by the Gui from its GuiDocument.xml.
    if (views || DocumentParams::getViewObjectTransaction()) {
        for (auto& kv : target) {
            auto live = viewOf(getObjectByID(kv.first));
            auto from = viewOf(kv.second);
            if (live && from) {
                restoreContainer(*live, *from, false, false);
                restoreContainer(*live, *from, false, true);
            }
        }
    }
    // 5. Touched as the version was saved (sec 27.60): its Document.xml
    // says which objects were, not which properties, and the restore of the
    // version document touched more of its own -- an object in error, a link
    // it had to fix, an expression. A clean one is clean; a touched one keeps
    // the properties the writes above touched.
    TouchedFold touched;
    std::set<long> ids;
    for (auto& kv : target) {
        auto& f = touched.objects[kv.first];
        const bool was = version.d->savedTouched.count(kv.first) != 0;
        f.bits = was ? DocumentObject::LogTouch : 0;
        f.exact = !was;
        ids.insert(kv.first);
    }
    touched.apply(*this, touched.save(*this, ids));
}

namespace {

/// The states a branch's history passes through (docs/TransactionLog.md
/// sec 26): 0, every row on the chain ending at `head`, and the parent of
/// its oldest -- where a trim (16.7) cut it, the version that anchors what
/// is left is at that row, which is gone.
std::set<int64_t> chainPoints(TransactionStore& store, int64_t head)
{
    std::set<int64_t> points {0};
    for (const auto& t : store.chain(head)) {
        points.insert(t.seq);
        points.insert(t.parent);
    }
    if (head > 0)
        points.insert(head);
    return points;
}

} // namespace

bool Document::recoverFromLog(const std::string& oldDir)
{
    // docs/TransactionLog.md sec 25.2.
    if (!d->objectArray.empty())
        THROWM(Base::RuntimeError, "recovery needs an empty document");
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "recovery needs the transaction log");
    TransactionLog::RecoverInfo info;
    if (!log->recover(oldDir, info))
        return false;
    auto& manager = getFileBlobManager();
    std::unique_ptr<void, std::function<void(void*)>> ending(
        &manager, [](void* m) { static_cast<FileBlobManager*>(m)->endRecovery(); });

    // The anchor: the newest version (16.3) on the branch the session was
    // on (sec 26), or nothing -- a document never saved or opened has every
    // object's create in the log.
    const std::set<int64_t> onChain = chainPoints(log->store(), log->head());
    LogVersion anchor;
    bool haveAnchor = false;
    for (const auto& v : log->store().versions()) {
        if (!onChain.count(v.seq))
            continue;
        if (!haveAnchor || v.num > anchor.num) {
            anchor = v;
            haveAnchor = true;
        }
    }

    size_t rows = 0;
    int64_t last = haveAnchor ? anchor.seq : 0;
    {
        Base::FlagToggler<> replaying(d->replaying);
        if (haveAnchor) {
            // Materialised outside this document's transient directory: the
            // restore takes the version's Uid and renames it.
            const std::string dir = _materialiseVersion(anchor.num, oldDir + "/checkout");
            Base::FlagToggler<> guard(d->checkingOut);
            restore(dir.c_str(), false);
            Base::FileInfo(dir).deleteDirectoryRecursive();
        }
        FileName.setValue(info.fileName);
        if (!info.label.empty())
            Label.setValue(info.label);
        rows = _replayLog(haveAnchor ? anchor.seq : 0, last);
    }
    _rebuildUndoFromLog();

    std::string from;
    for (char c : oldDir) {
        if (c == '"' || c == '\\')
            from += '\\';
        from += c;
    }
    std::ostringstream script;
    script << "{\"from\":\"" << from << "\",\"version\":"
           << (haveAnchor ? anchor.num : 0) << ",\"after\":" << (haveAnchor ? anchor.seq : 0)
           << ",\"rows\":" << rows << ",\"last\":" << last << ",\"sessions\":[";
    for (size_t i = 0; i < info.crashedSessions.size(); ++i)
        script << (i ? "," : "") << info.crashedSessions[i];
    script << "]}";
    log->recordRecovery(script.str());
    return true;
}

size_t Document::_replayLog(int64_t after, int64_t& last, int64_t head, bool* whole)
{
    // Folded, not applied row by row: the end state of every object,
    // dynamic property and value the tail touched, then applied in the
    // passes of a cold undo (sec 24.3), forward.
    TransactionLog* log = getTransactionLog();
    auto& store = log->store();
    using Key = std::tuple<std::string, long, std::string>;   // ckind, cid, prop
    struct Obj
    {
        bool exists {false};
        std::string name;
        std::string type;
    };
    std::map<long, Obj> objects;
    std::map<Key, std::string> values;
    std::map<Key, LogOp> added;
    std::set<Key> removed;
    std::set<long> touch;
    // The touched state the rows leave (sec 27.58).
    TouchedFold touched;
    auto forget = [&](long cid) {
        touched.forget(cid);
        for (auto it = values.begin(); it != values.end();)
            it = std::get<1>(it->first) == cid ? values.erase(it) : std::next(it);
        for (auto it = added.begin(); it != added.end();)
            it = std::get<1>(it->first) == cid ? added.erase(it) : std::next(it);
        for (auto it = removed.begin(); it != removed.end();)
            it = std::get<1>(*it) == cid ? removed.erase(it) : std::next(it);
    };

    size_t rows = 0;
    last = after;
    for (const auto& t : store.chain(head ? head : log->head(), after + 1)) {
        auto ops = store.ops(t.seq);
        if (t.kind == "recompute" && ops.empty()) {
            touched.record(t.script, false);
            ++rows;
            last = t.seq;
            continue;
        }
        // A clean prefix: a row whose value never reached the log -- a set
        // with no after that is not a removal's -- ends the replay, so the
        // document is a state the user had (sec 25.2 item 3).
        std::set<long> removes;
        for (const auto& o : ops) {
            if (o.op == "remove")
                removes.insert(o.cid);
        }
        bool complete = true;
        for (const auto& o : ops) {
            if (o.op == "set" && o.vafter.empty() && !o.derived && !removes.count(o.cid)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            if (whole) {
                *whole = false;
                return rows;
            }
            FC_WARN("recovery of " << getName() << ": row " << t.seq << " (" << t.name
                    << ") is incomplete; the replay ends before it");
            break;
        }
        for (const auto& o : ops) {
            Key key(o.ckind, o.cid, o.prop);
            if (o.op == "create" && o.ckind == "obj") {
                forget(o.cid);
                auto& obj = objects[o.cid];
                obj.exists = true;
                obj.name = o.cname;
                obj.type = o.ctype;
            }
            else if (o.op == "remove" && o.ckind == "obj") {
                forget(o.cid);
                auto& obj = objects[o.cid];
                obj.exists = false;
                obj.name = o.cname;
                obj.type = o.ctype;
            }
            else if (o.op == "addprop") {
                added[key] = o;
                removed.erase(key);
            }
            else if (o.op == "delprop") {
                added.erase(key);
                values.erase(key);
                removed.insert(key);
            }
            else if (o.op == "set") {
                touched.forward(o);
                if (!o.vafter.empty())
                    values[key] = o.vafter;
                else if (o.derived && o.ckind == "obj")
                    touch.insert(o.cid);
            }
        }
        touched.record(t.script, false);   // an undo's, a restore's (sec 27.63)
        ++rows;
        last = t.seq;
    }

    auto guarded = [&](const char* what, const std::string& name, const std::function<void()>& fn) {
        try {
            fn();
        }
        catch (Base::Exception& e) {
            FC_ERR("recovery, " << what << " " << name << ": " << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("recovery, " << what << " " << name << ": " << e.what());
        }
    };
    auto container = [&](const Key& key) -> PropertyContainer* {
        LogOp o;
        o.ckind = std::get<0>(key);
        o.cid = std::get<1>(key);
        return opContainer(*this, o);
    };
    // 1. The objects the tail made that are still there, under id and name.
    for (const auto& kv : objects) {
        if (!kv.second.exists || getObjectByID(kv.first))
            continue;
        guarded("create", kv.second.name, [&]() {
            Base::Type type = Base::Type::getTypeIfDerivedFrom(
                kv.second.type.c_str(), DocumentObject::getClassTypeId(), true);
            auto obj = type.isBad() ? nullptr : static_cast<DocumentObject*>(type.createInstance());
            if (!obj)
                throw Base::RuntimeError("cannot create " + kv.second.type);
            obj->_Id = kv.first;
            addObject(obj, kv.second.name.c_str(), false);
        });
    }
    // 2. Dynamic properties, as the tail left them.
    for (const auto& kv : added) {
        guarded("add property", std::get<2>(kv.first), [&]() {
            auto c = container(kv.first);
            if (!c || c->getPropertyByName(std::get<2>(kv.first).c_str()))
                return;
            addLoggedProperty(*c, kv.second.ptype, std::get<2>(kv.first), kv.second.meta);
        });
    }
    for (const auto& key : removed) {
        guarded("remove property", std::get<2>(key), [&]() {
            if (auto c = container(key))
                c->removeDynamicProperty(std::get<2>(key).c_str());
        });
    }
    // 3. Every value, the newest the tail wrote -- each write touches what it
    // writes, so the touched state is taken first.
    std::set<long> valued;
    for (const auto& kv : values) {
        if (std::get<0>(kv.first) == "obj")
            valued.insert(std::get<1>(kv.first));
    }
    const auto touchedNow = touched.save(*this, valued);
    RestoreBatch batch;   // each afterRestore() once all are in (sec 27.67)
    for (const auto& kv : values) {
        guarded("value of", std::get<2>(kv.first), [&]() {
            auto c = container(kv.first);
            if (!c)
                return;   // a view with no Gui, or an object gone
            Property* prop = c->getPropertyByName(std::get<2>(kv.first).c_str());
            if (!prop)
                throw Base::RuntimeError("no such property");
            CapturedValue v;
            if (!log->readValue(kv.second, v))
                throw Base::RuntimeError("value " + kv.second + " is not in the log");
            log->restoreBlobsOf(kv.second);
            restoreValue(*prop, v);
        });
    }
    batch.finish();
    // 4. The touched state the session had: the anchor's with what the rows
    // said on top (sec 27.58), and an object whose derived values the log
    // did not keep (sec 24.1) touched.
    touched.apply(*this, touchedNow);
    for (long cid : touch) {
        if (auto obj = getObjectByID(cid))
            obj->touch();
    }
    // 5. What the tail removed.
    for (const auto& kv : objects) {
        if (kv.second.exists)
            continue;
        if (auto obj = getObjectByID(kv.first)) {
            guarded("remove", kv.second.name,
                    [&]() { removeObject(obj->getNameInDocument()); });
        }
    }
    return rows;
}

void Document::_rebuildUndoFromLog(int64_t after)
{
    // The stacks the crashed session had, from its rows (sec 25.2): a step
    // pushes and ends what its author could redo; an `undo` row naming its
    // author's newest step moves that to redo, a `redo` row naming the
    // author's newest redo step moves it back -- each under the row that did
    // it, which is what a cold undo or redo reverts, and under the author of
    // its row (sec 30.10). An undo asked of a row by its number (sec 24.4),
    // named `Undo <name>`, is a step of its own.
    if (!d->iUndoMode)
        return;
    TransactionLog* log = getTransactionLog();
    auto& store = log->store();
    // A row's author is its session's user; the desktop's is no actor.
    std::map<int64_t, LogUser> users;
    for (auto& u : store.users())
        users[u.id] = u;
    std::map<int64_t, std::shared_ptr<const Actor>> authors;
    for (const auto& s : store.sessions()) {
        Actor actor;
        const LogUser& user = users[s.user];
        if (!Actor::kindFromName(user.kind, actor.kind) || actor.kind == Actor::Local)
            continue;
        actor.name = user.name;
        actor.access = s.access;
        authors[s.id] = std::make_shared<const Actor>(std::move(actor));
    }
    struct Step
    {
        int64_t seq;
        std::string name;
        bool implicit;
        std::shared_ptr<const Actor> author;
        int64_t before;
        int64_t after;
    };
    std::vector<Step> undo;
    std::vector<Step> redo;
    // The states as the rows went through them: a step applied where it
    // left the document gives back the state it was made in, anything else
    // makes a new one.
    int64_t state = ++d->stateCounter;
    auto newestOf = [](std::vector<Step>& stack, const Actor* author) {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (sameUser(it->author.get(), author))
                return std::prev(it.base());
        }
        return stack.end();
    };
    auto moved = [&](std::vector<Step>& from, std::vector<Step>& to, const LogTransaction& t,
                     const std::shared_ptr<const Actor>& author) {
        auto it = newestOf(from, author.get());
        if (it == from.end() || it->seq != t.inverts || it->name != t.name)
            return false;
        Step s = *it;
        from.erase(it);
        const bool atState = s.after == state;
        const int64_t madeIn = s.before;
        s.seq = t.seq;
        s.author = author;
        s.before = atState ? s.after : state;
        state = atState ? madeIn : ++d->stateCounter;
        s.after = state;
        to.push_back(std::move(s));
        return true;
    };
    for (const auto& t : store.chain(log->head(), after + 1)) {
        auto found = authors.find(t.session);
        const std::shared_ptr<const Actor> author =
            found == authors.end() ? nullptr : found->second;
        if (t.kind == "undo" && moved(undo, redo, t, author))
            continue;
        if (t.kind == "redo" && moved(redo, undo, t, author))
            continue;
        if (store.ops(t.seq).empty())
            continue;   // a record: save, snapshot, recompute, login
        const int64_t before = state;
        state = ++d->stateCounter;
        undo.push_back({t.seq, t.name, t.kind == "implicit", author, before, state});
        redo.erase(std::remove_if(redo.begin(), redo.end(),
                                  [&](const Step& s) {
                                      return sameUser(s.author.get(), author.get());
                                  }),
                   redo.end());
    }
    d->stateToken = state;
    auto stub = [](const Step& s) {
        auto t = new Transaction(0);
        t->Name = s.name;
        t->Implicit = s.implicit;
        t->LogSeq = s.seq;
        t->Author = s.author;
        t->StateBefore = s.before;
        t->StateAfter = s.after;
        t->Cold = true;
        return t;
    };
    for (const auto& s : undo) {
        auto t = stub(s);
        mUndoMap[t->getID()] = t;
        mUndoTransactions.push_back(t);
    }
    // The redo stack's top is its back, as the undo stack's.
    for (const auto& s : redo) {
        auto t = stub(s);
        mRedoMap[t->getID()] = t;
        mRedoTransactions.push_back(t);
    }
}

bool Document::restoreVersion(int64_t num)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 24.5: a restore to version N is one
    // forward transaction that makes the document what N was -- undoable,
    // logged, and the document never reloaded.
    checkNotFrozen("restore a version");
    TransactionLog* log = getTransactionLog();
    if (!log)
        return false;
    if (d->checkingOut || testStatus(Restoring) || isPerformingTransaction() || d->committing)
        THROWM(Base::RuntimeError, "cannot restore a version now");
    if (d->activeUndoTransaction)
        commitImplicitTransaction();
    if (d->activeUndoTransaction)
        THROWM(Base::RuntimeError, "cannot restore a version inside a transaction");
    LogVersion version;
    if (!log->store().getVersion(num, version))
        THROWM(Base::RuntimeError, "no such version");

    _clearMyRedos();
    d->activeUndoTransaction = new Transaction(0);
    d->activeUndoTransaction->Name = "Restore version " + std::to_string(num)
        + (version.name.empty() ? "" : " " + version.name);
    d->activeUndoTransaction->LogKind = "restore";
    mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
    // Through the rows between here and the version when the log has them
    // (sec 27.34): only what changed since is written. Else the version is
    // read whole into a document of its own and its difference applied --
    // which also puts right whatever the rows got part way through.
    // The touched state it changes goes on its row (sec 27.63): an object
    // whose flags alone changed is in no op, and a set's after, crossed
    // forward, is the write's, not the version's.
    std::map<long, TouchedState> before;
    for (auto obj : getObjects())
        before.emplace(obj->getID(), touchedStateOf(*obj));
    const int64_t head = log->head();
    if (!_moveAlongLog(head, version.seq, DocumentParams::getViewObjectTransaction())) {
        FC_LOG(getName() << ": version " << num << " restored by reading it whole");
        _readVersion(num, [&](Document& read) { _applyVersion(read); });
    }
    std::map<long, TouchedState> after;
    for (auto obj : getObjects()) {
        auto state = touchedStateOf(*obj);
        auto it = before.find(obj->getID());
        if (it == before.end() || !(it->second == state)
                || d->activeUndoTransaction->hasObject(obj))
            after.emplace(obj->getID(), std::move(state));
    }
    d->activeUndoTransaction->LogScript = touchedRecord(before, after);
    if (d->activeUndoTransaction->isEmpty()) {
        // Already what the version was: nothing to record.
        mUndoMap.erase(d->activeUndoTransaction->getID());
        delete d->activeUndoTransaction;
        d->activeUndoTransaction = nullptr;
        return true;
    }
    _commitTransaction(false);
    return true;
}

void Document::_restoreAsVersion(const std::shared_ptr<FileHistory>& history,
                                 const std::string& dir, const std::string& fileName)
{
    setStatus(VersionDoc, true);
    d->noLog = true;
    _joinHistory(history);
    // A version is read from the log, whose strings are the file's: its
    // hasher is the file's whatever this document hashed before it was one
    // -- with a view, something had, and the version then looked for a
    // table of its own that no version carries (sec 29.6).
    if (history->hasher())
        d->Hasher = history->hasher();
    // Who the document is, not a change to it (27.9): no transaction.
    if (!fileName.empty()) {
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        FileName.setValue(fileName);
    }
    Base::FlagToggler<> guard(d->checkingOut);
    restore(dir.c_str(), false);
}

void Document::_readVersion(int64_t num, const std::function<void(Document&)>& fn)
{
    // Sec 27.58, 27.60: read as openFileVersion() reads a version -- joined to
    // this file's history, so a blob is found by its hash in the store the
    // file already has and a shape already parsed is shared -- not into a
    // scratch document with a store of its own that every blob is written to
    // and every shape parsed again from. It keeps no log and has no view,
    // and it is not the version's document a user opens: that one may be
    // edited, and is left alone.
    getFileHistory();
    std::shared_ptr<FileHistory> history = d->history;
    TransactionLogCore& log = TransactionLogCore::of(*history);
    const std::string dir = materialiseVersion(
        log, num, history->directory() + "/history/read-v" + std::to_string(num), true);
    struct Cleanup
    {
        std::string dir;
        ~Cleanup() { Base::FileInfo(dir).deleteDirectoryRecursive(); }
    } cleanup {dir};

    // A new document becomes the active one; the active one is handed back.
    auto& app = GetApplication();
    Document* active = app.getActiveDocument();
    const std::string name = app.getUniqueDocumentName(
        (std::string(getName()) + "_read_v" + std::to_string(num)).c_str(), true);
    Document* version = app.newDocument(name.c_str(), name.c_str(), false, true);
    if (!version)
        THROWM(Base::RuntimeError, "cannot make the version's document");
    std::unique_ptr<void, std::function<void(void*)>> closer(
        version, [&app, name, active](void*) {
            app.closeDocument(name.c_str());
            if (active && app.getActiveDocument() != active)
                app.setActiveDocument(active);
        });
    version->setUndoMode(0);
    std::string file = FileName.getStrValue();
    FileHistory::splitVersion(file);
    version->_restoreAsVersion(history, dir,
                               file.empty() ? file : file + "@v" + std::to_string(num));
    fn(*version);
}

bool Document::isPinned() const
{
    TransactionLog* log = getTransactionLog();
    if (log && !log->store().getMeta("pins").empty())
        return true;
    return !FileName.getStrValue().empty() && !testStatus(VersionDoc)
        && !PropertyXLink::getPinsTo(FileName.getStrValue()).empty();
}

namespace {

/// The value of the document-level `<Property name="...">` of Document.xml
/// whose content is `<String value="..."/>`; empty if there is none.
std::string documentString(const std::string& xml, const char* name)
{
    const std::string open = std::string("<Property name=\"") + name + "\"";
    size_t at = xml.find(open);
    if (at == std::string::npos)
        return {};
    const size_t end = xml.find("</Property>", at);
    const size_t value = xml.find("<String value=\"", at);
    if (value == std::string::npos || value > end)
        return {};
    const size_t from = value + std::strlen("<String value=\"");
    const size_t to = xml.find('"', from);
    return to == std::string::npos ? std::string() : xml.substr(from, to - from);
}

/// Where the `<History .../>` or `<History ...>...</History>` element of the
/// document's History property is in Document.xml: false if it has none.
bool findHistoryElement(const std::string& xml, size_t& from, size_t& to)
{
    const size_t at = xml.find("<Property name=\"History\"");
    if (at == std::string::npos)
        return false;
    const size_t end = xml.find("</Property>", at);
    from = xml.find("<History", at);
    if (from == std::string::npos || from > end)
        return false;
    const size_t close = xml.find('>', from);
    if (close == std::string::npos)
        return false;
    if (xml[close - 1] == '/') {
        to = close + 1;
        return true;
    }
    const size_t closing = xml.find("</History>", close);
    if (closing == std::string::npos || closing > end)
        return false;
    to = closing + std::strlen("</History>");
    return true;
}

} // namespace

int64_t Document::saveToLog(const char* name)
{
    // docs/TransactionLog.md sec 27.22, 27.28.
    checkNotFrozen("save to the log");
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "no transaction log");
    std::string file = FileName.getStrValue();
    FileHistory::splitVersion(file);
    Base::FileInfo fi(file);
    if (file.empty() || !fi.isFile())
        THROWM(Base::RuntimeError,
               "'" + Label.getStrValue() + "' has no file to save its history into");
    if (DocumentParams::getTransactionLog() != 2 && log->store().getMeta("pins").empty()
            && PropertyXLink::getPinsTo(file).empty())
        THROWM(Base::RuntimeError,
               "the file does not carry its history: saving to the history alone would lose it");

    // The file's own guard, which it keeps: the save id and the date its
    // document says it was saved with (sec 16.4).
    std::string xml;
    if (!FileBlobManager::readArchiveMember(file, "Document.xml", xml))
        THROWM(Base::FileException, ("cannot read the document of '" + file + "'").c_str());
    std::string saveId;
    {
        std::istringstream in(documentString(xml, "Version"));
        int64_t num = 0;
        in >> num >> saveId;
    }
    const std::string saveDate = documentString(xml, "LastModifiedDate");
    size_t hFrom = 0, hTo = 0;
    if (saveId.empty() || !findHistoryElement(xml, hFrom, hTo))
        THROWM(Base::RuntimeError, "the file carries no history: save it with its history first");

    // Nothing of any document of the file may still be parked in the
    // archive being replaced (as for a save, saveToFile()).
    for (auto doc : log->documents())
        doc->flushDeferredFiles();
    commitImplicitTransaction();

    FC_TIME_INIT(tSnap);
    log->takeBranch();
    const int64_t num = _snapshotToLog("history");
    if (!num)
        THROWM(Base::RuntimeError, "'" + Label.getStrValue() + "' cannot be saved to the log now");
    // Kept: a version the file does not hold must travel in its history.
    log->store().nameVersion(num, name && name[0] ? name : "Saved to history");

    FC_TIME_INIT(tEmbed);
    // The history, as the file will carry it.
    TransactionLog::Embedded copy = log->embedForFile(saveDate, saveId);
    auto& manager = getFileBlobManager();
    FileBlobHandle db = manager.adoptFile(copy.path.c_str(), "db");
    if (!db)
        THROWM(Base::RuntimeError, "cannot store the history");
    std::ostringstream element;
    element << "<History db=\"" << db->hash() << "\" count=\"" << copy.blobs.size() << "\">\n";
    std::vector<FileBlobHandle> blobs;
    for (const auto& b : copy.blobs) {
        auto blob = manager.find(b.first);
        if (!blob) {
            FC_WARN("history of " << getName() << ": blob " << b.first << " is not in the store");
            continue;
        }
        blobs.push_back(blob);
        element << "  <Blob hash=\"" << b.first << "\" ext=\"" << Property::encodeAttribute(b.second)
                << "\"/>\n";
    }
    element << "</History>";
    const std::string oldElement = xml.substr(hFrom, hTo - hFrom);
    const std::string outside = xml.substr(0, hFrom) + xml.substr(hTo);
    xml.replace(hFrom, hTo - hFrom, element.str());

    // What the archive holds is known from its content index, not by
    // decoding every member (sec 27.37). The model's members are copied as
    // stored; a member only the old history referred to is left out unless
    // the new one keeps it (no version holds the history, sec 27.29); what
    // the archive lacks is added -- the new database, and the blobs the
    // history keeps -- and the index is written again to say so.
    FC_TIME_INIT(tIndex);
    bool hasIndex = false;
    std::map<std::string, BlobIndexEntry> members =
        FileBlobManager::archiveBlobIndex(file, &hasIndex);
    // A member the index does not list -- every member, in a file without
    // one (below schema 5 nothing but the history is under blobs/) -- is the
    // old history's when its element names it and nothing else in the
    // document does.
    std::string gui;
    FileBlobManager::readArchiveMember(file, "GuiDocument.xml", gui);
    for (auto& m : members) {
        const std::string& hash = m.second.hash;
        if (m.second.referrers.empty() && oldElement.find(hash) != std::string::npos
                && outside.find(hash) == std::string::npos && gui.find(hash) == std::string::npos)
            m.second.referrers.push_back(FileBlobManager::historyReferrer());
    }
    blobs.push_back(db);
    std::set<std::string> kept;
    for (const auto& blob : blobs)
        kept.insert(blob->hash());
    const std::string& historyRef = FileBlobManager::historyReferrer();
    const std::string prefix = FileBlobManager::archivePrefix();
    std::set<std::string> drop;
    std::set<std::string> held;
    for (auto it = members.begin(); it != members.end();) {
        const auto& refs = it->second.referrers;
        const bool onlyHistory = !refs.empty()
            && std::all_of(refs.begin(), refs.end(),
                           [&historyRef](const std::string& r) { return r == historyRef; });
        if (onlyHistory && !kept.count(it->second.hash)) {
            drop.insert(prefix + it->first);
            it = members.erase(it);
            continue;
        }
        held.insert(it->second.hash);
        ++it;
    }
    std::vector<std::pair<std::string, FileBlobHandle>> add;
    for (const auto& blob : blobs) {
        if (held.insert(blob->hash()).second) {
            std::string ext = blob->extension();
            std::string member = blob->hash() + (ext.empty() ? "" : "." + ext);
            members[member] = BlobIndexEntry {blob->hash(), {historyRef}};
            add.emplace_back(std::move(member), blob);
        }
    }
    std::map<std::string, std::string> replace {{"Document.xml", xml}};
    // In place, so it stays ahead of the content it describes (a restore
    // serves the whole archive when it reaches it). An archive without one
    // is read member by member and gets none.
    if (hasIndex)
        replace[prefix + FileBlobManager::indexName()] = FileBlobManager::indexText(members);

    FC_TIME_INIT(tWrite);
    const std::string tmp = file + "." + Base::Uuid::createUuid();
    try {
        manager.rewriteArchive(file, tmp, replace, drop, add);
    }
    catch (...) {
        Base::FileInfo(tmp).deleteFile();
        throw;
    }
    if (!fi.deleteFile() || !Base::FileInfo(tmp).renameFile(file.c_str()))
        THROWM(Base::FileException, ("cannot replace '" + file + "'").c_str());
    Base::FileInfo(copy.path).deleteFile();
    FC_LOG("saveToLog " << getName() << ": snapshot " << FC_DURATION(tEmbed - tSnap).count()
           << "s, history " << FC_DURATION(tIndex - tEmbed).count()
           << "s, index " << FC_DURATION(tWrite - tIndex).count() << "s (" << members.size()
           << " members, " << drop.size() << " dropped, " << add.size()
           << " added), rewrite " << Base::GetDuration(tWrite).count() << 's');

    noteVersionTaken();
    if (testStatus(VersionDoc)) {
        d->versionTail = num;
        refreshVersionNames();
    }
    return num;
}

int64_t Document::saveVersionAsFile()
{
    // docs/TransactionLog.md sec 27.5 ruling 4, 27.16.
    if (!testStatus(VersionDoc))
        THROWM(Base::RuntimeError, "'" + Label.getStrValue() + "' is not a version of a file");
    checkNotFrozen("save");
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "no transaction log");
    std::string file = FileName.getStrValue();
    if (!FileHistory::splitVersion(file) || file.empty())
        THROWM(Base::RuntimeError, "'" + Label.getStrValue() + "' names no file");
    // On a branch before the save names one: the embedded copy says which
    // branch the file reopens on.
    log->takeBranch();
    {
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        if (Tip.getValue())
            TipName.setValue(Tip.getValue()->getNameInDocument());
        LastModifiedDate.setValue(Base::TimeInfo::currentDateTimeString().c_str());
        if (DocumentParams::getprefSetAuthorOnSave())
            LastModifiedBy.setValue(DocumentParams::getprefAuthor().c_str());
    }
    // The file gets the label it is named after, not the version name
    // (27.23), which the document takes back after.
    bool written = false;
    {
        const std::string shown = Label.getStrValue();
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        if (!d->versionLabel.empty())
            Label.setValue(d->versionLabel);
        try {
            written = saveToFile(file.c_str());
        }
        catch (...) {
            Label.setValue(shown);
            throw;
        }
        Label.setValue(shown);
    }
    if (!written)
        THROWM(Base::FileException, ("saving '" + file + "' failed").c_str());
    auto version = Base::freecad_dynamic_cast<PropertyString>(getPropertyByName("Version"));
    const int64_t saved = version ? std::atoll(version->getValue()) : 0;
    // The name ends in the version it last saved (27.23).
    if (saved > 0) {
        d->versionTail = saved;
        refreshVersionNames();
    }
    return saved;
}

int64_t Document::pinLink(PropertyXLink& link, int64_t version)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 16.5, 27.6 Q3, 27.7.
    auto owner = Base::freecad_dynamic_cast<DocumentObject>(link.getContainer());
    if (!owner || !owner->isAttachedToDocument())
        THROWM(Base::RuntimeError, "the link has no owner");
    Document* linked = link.getValue() ? link.getValue()->getDocument() : link.getDocument();
    // A local link pins a version of its own file (sec 27.20).
    if (!linked && !link.getObjectName()[0])
        THROWM(Base::RuntimeError, "the link links to nothing");
    if (!linked)
        linked = owner->getDocument();
    const bool self = linked == owner->getDocument();
    std::string file = linked->FileName.getStrValue();
    FileHistory::splitVersion(file);
    if (file.empty())
        THROWM(Base::RuntimeError, "the linked document is not saved");
    std::string reason;
    auto history = FileHistory::openFile(file, &reason);
    if (!history)
        THROWM(Base::RuntimeError, "the linked file has no history: " + reason);
    TransactionLogCore& log = TransactionLogCore::of(*history);

    if (version <= 0) {
        // The version the linked document is: a version document's own, or
        // the one the file is on disk (27.6 Q3) -- its `Version`.
        if (auto vlog = linked->getTransactionLog(); vlog && vlog->detachedAt())
            version = vlog->detachedAt();
        else if (linked->testStatus(VersionDoc) && link.getPinVersion())
            version = link.getPinVersion();
        else if (linked->testStatus(VersionDoc) && linked->d->versionTail) {
            // An editable instance (27.23): the version its name ends in, if
            // nothing changed since; else what it shows has no number yet.
            LogVersion tail;
            if (log.store().getVersion(linked->d->versionTail, tail)
                    && log.documentAt(tail) == linked)
                version = linked->d->versionTail;
            else
                THROWM(Base::RuntimeError, "'" + linked->Label.getStrValue()
                           + "' has changed since its last version: save it first");
        }
        else if (auto prop = Base::freecad_dynamic_cast<PropertyString>(
                     linked->getPropertyByName("Version")))
            version = std::atoll(prop->getValue());
        else if (history->fileVersion())
            version = history->fileVersion();
        if (version <= 0)
            THROWM(Base::RuntimeError,
                   "the linked file has no version on disk: save it with its history first");
    }
    LogVersion v;
    if (!log.store().getVersion(version, v))
        THROWM(Base::ValueError, "the linked file has no version " + std::to_string(version));
    if (self) {
        // The version must have the object, or the pin would show nothing:
        // its frozen instance is opened now, as the pin would open it.
        const char* name = link.getValue() ? link.getValue()->getNameInDocument()
                                           : link.getObjectName();
        Document* frozen = openFileVersion(history, version, false, nullptr, true);
        if (frozen)
            frozen->setStatus(OpenedForPin, true);
        if (!frozen || !frozen->getObject(name))
            THROWM(Base::ValueError, std::string("version ") + std::to_string(version)
                                         + " of the file has no object '" + name + "'");
    }
    // Named, so the linked file's own eviction keeps it (16.3), and noted as
    // pinned, so the file is saved with its history from now on.
    if (v.kind != "named")
        log.store().nameVersion(version, "pinned");
    std::string pins = log.store().getMeta("pins");
    std::ostringstream entry;
    entry << version << '\t' << owner->getDocument()->FileName.getStrValue() << '\t'
          << owner->getDocument()->Uid.getValueStr();
    if (pins.find(entry.str()) == std::string::npos) {
        if (!pins.empty())
            pins += '\n';
        pins += entry.str();
        log.store().setMeta("pins", pins);
    }
    // The pin reaches the file with its next save: the file's own document
    // is marked modified (the convention DocInfo uses for a stamp change),
    // opened first when none is (27.6 Q3, 27.16) -- a store no save writes
    // would lose the row.
    auto fileDocs = [&log]() {
        std::vector<Document*> docs;
        for (auto doc : log.documents()) {
            if (!doc->testStatus(VersionDoc))
                docs.push_back(doc);
        }
        return docs;
    };
    std::vector<Document*> docs = fileDocs();
    if (docs.empty()) {
        auto& app = GetApplication();
        Document* active = app.getActiveDocument();
        app.openDocument(file.c_str());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        docs = fileDocs();
        if (docs.empty())
            THROWM(Base::RuntimeError, "the linked file '" + file + "' cannot be opened");
    }
    for (auto doc : docs)
        doc->Comment.touch();
    link.setPin(version, v.uuid);
    return version;
}

Document* Document::openFileBranch(const std::shared_ptr<FileHistory>& history,
                                   const std::string& branch, int64_t num, bool createView)
{
    // docs/TransactionLog.md sec 27.23, 27.24.
    if (!history)
        THROWM(Base::RuntimeError, "no file history");
    if (branch.empty())
        return openFileVersion(history, num, createView, nullptr, true);
    TransactionLogCore& log = TransactionLogCore::of(*history);
    auto& store = log.store();
    LogBranch b;
    if (!store.findBranch(branch, b))
        THROWM(Base::ValueError, "no branch '" + branch + "'");
    if (num > 0)
        return openFileVersion(history, num, createView, nullptr, false);
    // The tip: whoever holds the branch (27.12's worktree rule: one at
    // most), else its newest version opened and moved to the head in place.
    if (Document* holder = log.holderOf(b.id))
        return holder;
    const std::set<int64_t> onChain = chainPoints(store, b.head);
    LogVersion anchor;
    bool haveAnchor = false;
    for (const auto& v : store.versions()) {
        if (onChain.count(v.seq) && (!haveAnchor || v.num > anchor.num)) {
            anchor = v;
            haveAnchor = true;
        }
    }
    if (!haveAnchor)
        THROWM(Base::RuntimeError, "branch '" + branch + "' has no version to open");
    Document* doc = openFileVersion(history, anchor.num, createView, nullptr, false);
    TransactionLog* dlog = doc->getTransactionLog();
    if (dlog && dlog->branch() != b.id)
        doc->switchBranch(branch);
    return doc;
}

void Document::_joinHistory(const std::shared_ptr<FileHistory>& history)
{
    // A new document may have a log and a history of its own already: the
    // Gui's log panel asks the active document for its log, and a new one is
    // active before whoever made it is back (sec 27.7). Both go; the
    // history's directory was this document's transient directory, which
    // the document needs again.
    if (d->history == history)
        return;
    d->transactionLog.reset();
    if (d->history) {
        d->history->releaseHome(*this);
        d->history.reset();
        Base::FileInfo(TransientDir.getStrValue()).createDirectories();
    }
    d->history = history;
    d->joinedHistory = true;
    _noteObjectsInHistory();
}

Document* Document::openVersion(int64_t num, bool createView)
{
    // docs/TransactionLog.md sec 27.5 ruling 3, 27.7.
    if (!getTransactionLog())
        THROWM(Base::RuntimeError, "no transaction log");
    getFileHistory();
    return openFileVersion(d->history, num, createView, this);
}

void Document::refreshVersionNames()
{
    // docs/TransactionLog.md sec 27.23.
    TransactionLog* log = getTransactionLog();
    std::vector<Document*> docs = log ? log->documents() : std::vector<Document*> {this};
    for (auto doc : docs) {
        if (!doc->testStatus(VersionDoc) || doc->d->versionFile.empty()
                || !doc->d->versionTail)
            continue;
        std::string tail = "@v" + std::to_string(doc->d->versionTail);
        if (!doc->testStatus(FrozenVersion)) {
            TransactionLog* dlog = doc->getTransactionLog();
            std::string branch = dlog ? dlog->branchName() : std::string();
            if (branch.empty())
                continue;
            tail = "@" + branch + tail;
        }
        Base::FlagToggler<> quiet(doc->d->bookkeeping, false);
        const std::string fileName = doc->d->versionFile + tail;
        if (doc->FileName.getStrValue() != fileName)
            doc->FileName.setValue(fileName);
        const std::string label = doc->d->versionLabel + tail;
        if (doc->Label.getStrValue() != label)
            doc->Label.setValue(label);
    }
}

void Document::checkNotFrozen(const char* what) const
{
    // Sec 27.22: the instance a pin shows is the version, which never
    // changes. Its restore, and the bookkeeping that names it, are not
    // changes.
    if (!testStatus(FrozenVersion) || d->checkingOut || d->bookkeeping
            || testStatus(Restoring))
        return;
    THROWM(Base::RuntimeError, "'" + Label.getStrValue() + "' is a pinned version of a file and "
                                   "cannot be changed (" + what + ")");
}

Document* Document::openFileVersion(const std::shared_ptr<FileHistory>& history, int64_t num,
                                    bool createView, const Document* from, bool frozen)
{
    // Sec 27.7, 27.13: with or without a document of the file open; the
    // frozen instance or the editable one (27.22), at most one of each.
    if (!history)
        THROWM(Base::RuntimeError, "no file history");
    TransactionLogCore& log = TransactionLogCore::of(*history);
    LogVersion version;
    if (!log.store().getVersion(num, version))
        THROWM(Base::ValueError, "no version " + std::to_string(num));
    if (Document* open = log.documentAt(version, frozen))
        return open;
    return _openVersionDocument(history, version, createView, from, frozen);
}

Document* Document::_openVersionDocument(const std::shared_ptr<FileHistory>& history,
                                         const LogVersion& version, bool createView,
                                         const Document* from, bool frozen)
{
    TransactionLogCore& log = TransactionLogCore::of(*history);
    const int64_t num = version.num;
    const std::string dir = materialiseVersion(
        log, num, history->directory() + "/history/open-v" + std::to_string(num), true);
    struct Cleanup
    {
        std::string dir;
        ~Cleanup() { Base::FileInfo(dir).deleteDirectoryRecursive(); }
    } cleanup {dir};

    // Named after the document it came from, or else the file.
    const std::string fileName = from ? from->FileName.getStrValue() : history->path();
    std::string baseName = from ? std::string(from->getName())
                                : Base::FileInfo(fileName).fileNamePure();
    std::string label = from ? std::string(from->Label.getValue()) : history->fileLabel();
    if (from && from->testStatus(VersionDoc) && !from->d->versionLabel.empty())
        label = from->d->versionLabel;
    std::string file = fileName;
    FileHistory::splitVersion(file);
    if (label.empty())
        label = Base::FileInfo(file).fileNamePure();
    auto& app = GetApplication();
    Document* active = app.getActiveDocument();
    const std::string suffix = "@v" + std::to_string(num);
    const std::string name = app.getUniqueDocumentName(
        (baseName + "_v" + std::to_string(num) + (frozen ? "_pinned" : "")).c_str());
    Document* doc = app.newDocument(name.c_str(), name.c_str(), createView);
    if (!doc)
        THROWM(Base::RuntimeError, "cannot make the version's document");
    try {
        // Named for now as the frozen instance is: the restore reads
        // relative links against the file's directory either way, and the
        // editable one is named for its branch once it has a log (27.23).
        doc->_restoreAsVersion(history, dir, file.empty() ? file : file + suffix);
        doc->d->versionFile = file;
        doc->d->versionLabel = label;
        doc->d->versionTail = num;
        {
            Base::FlagToggler<> quiet(doc->d->bookkeeping, false);
            doc->Label.setValue(label + suffix);
        }
        if (from)
            doc->setUndoMode(from->getUndoMode());
        doc->d->noLog = false;
        doc->d->transactionLog = std::make_unique<TransactionLog>(*doc, &version);
        doc->d->undoFloor = doc->d->transactionLog->lastSeq();
        if (from)
            doc->d->lastVersionTime = from->d->lastVersionTime;
        if (frozen) {
            // Nothing to undo, ever; set last, the restore being over.
            doc->setUndoMode(0);
            doc->setStatus(FrozenVersion, true);
        }
        doc->refreshVersionNames();
    }
    catch (...) {
        app.closeDocument(name.c_str());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        throw;
    }
    return doc;
}

namespace {

/// The estimate of sec 27.48 as fields of a trim record.
std::string compactJson(const Document::CompactEstimate& e)
{
    std::ostringstream out;
    out << ",\"unreferenced\":" << e.objects << ",\"unreferenced_bytes\":" << e.bytes
        << ",\"unreferenced_bytes_total\":" << e.totalBytes
        << ",\"unheld_strings\":" << e.strings << ",\"unheld_string_bytes\":" << e.stringBytes
        << ",\"compacted\":" << (e.compacted ? "true" : "false");
    return out.str();
}

std::string jsonString(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "\"";
}

} // namespace

void Document::_checkBranchable(const char* what)
{
    if (d->checkingOut || testStatus(Restoring) || isPerformingTransaction() || d->committing
            || d->snapshotting || testStatus(PartialDoc))
        THROWM(Base::RuntimeError, std::string("cannot ") + what + " now");
    if (d->activeUndoTransaction)
        commitImplicitTransaction();
    if (d->activeUndoTransaction)
        THROWM(Base::RuntimeError, std::string("cannot ") + what + " inside a transaction");
}

void Document::_leaveBranch()
{
    // Sec 17.1: the tip left behind is snapshotted, unless it is a version
    // already, so that switching back checks it out with no replay; and the
    // branch keeps the last id it handed out.
    TransactionLog* log = getTransactionLog();
    // A version document not yet changed leaves nothing behind (sec 27.5).
    if (log->detached())
        return;
    log->resolvePending();
    // A version is the tip when nothing after it on the chain changed the
    // document: only records (save, snapshot, switch, branch) follow it.
    auto& store = log->store();
    const auto rows = store.chain(log->head());
    const std::set<int64_t> onChain = chainPoints(store, log->head());
    int64_t newest = -1;
    for (const auto& v : store.versions()) {
        if (onChain.count(v.seq))
            newest = std::max(newest, v.seq);
    }
    bool atVersion = newest >= 0;
    for (auto it = rows.rbegin(); atVersion && it != rows.rend() && it->seq > newest; ++it)
        atVersion = store.ops(it->seq).empty();
    if (!atVersion)
        snapshotToLog();
    LogBranch branch;
    if (log->store().getBranch(log->branch(), branch)) {
        branch.lastId = d->lastObjectId;
        log->store().updateBranch(branch);
    }
}

namespace {

/// The net effect of a run of log rows (docs/TransactionLog.md sec 27.34):
/// for every object, dynamic property and value they touched, where it ends.
struct LogFold
{
    using Key = std::tuple<std::string, long, std::string>;   // ckind, cid, prop
    struct Obj
    {
        bool exists {false};
        std::string name;
        std::string type;
    };
    std::map<long, Obj> objects;
    std::map<Key, std::string> values;   // the value it ends with, by hash
    std::map<Key, LogOp> added;          // there at the end, with what adds it
    std::set<Key> removed;               // gone at the end
    std::set<long> touch;                // its derived values the log did not keep
    TouchedFold touched;                 // the touched state, sec 27.58

    void forget(long cid)
    {
        touched.forget(cid);
        for (auto it = values.begin(); it != values.end();)
            it = std::get<1>(it->first) == cid ? values.erase(it) : std::next(it);
        for (auto it = added.begin(); it != added.end();)
            it = std::get<1>(it->first) == cid ? added.erase(it) : std::next(it);
        for (auto it = removed.begin(); it != removed.end();)
            it = std::get<1>(*it) == cid ? removed.erase(it) : std::next(it);
    }

    /// One row undone: its ops newest first, each taken back to its before.
    void back(const std::vector<LogOp>& ops, bool views)
    {
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            const LogOp& o = *it;
            if (o.ckind == "view" && !views)
                continue;
            Key key(o.ckind, o.cid, o.prop);
            if (o.op == "create" && o.ckind == "obj") {
                forget(o.cid);
                objects[o.cid] = Obj {false, o.cname, o.ctype};
            }
            else if (o.op == "remove" && o.ckind == "obj") {
                forget(o.cid);
                objects[o.cid] = Obj {true, o.cname, o.ctype};
            }
            else if (o.op == "addprop") {
                added.erase(key);
                values.erase(key);
                removed.insert(key);
            }
            else if (o.op == "delprop") {
                // Its value before the removal is the op's (sec 27.60): the
                // property comes back with it.
                removed.erase(key);
                added[key] = o;
                if (!o.vbefore.empty())
                    values[key] = o.vbefore;
            }
            else if (o.op == "set") {
                touched.back(o);
                if (!o.vbefore.empty()) {
                    values[key] = o.vbefore;
                    if (o.derived)
                        touch.erase(o.cid);
                }
                else if (o.derived && o.ckind == "obj") {
                    values.erase(key);
                    touch.insert(o.cid);
                }
                // A set's meta says how to add its dynamic property back: a
                // removed object's, which the walk back recreates, or one
                // the cascade removing it took first (sec 27.68).
                if (!o.meta.empty()) {
                    removed.erase(key);
                    added[key] = o;
                }
            }
        }
    }

    /// One row done: its ops in order, each to its after. False when a value
    /// never reached the log (sec 25.2 item 3).
    bool forward(const std::vector<LogOp>& ops, bool views)
    {
        std::set<long> removes;
        for (const auto& o : ops) {
            if (o.op == "remove")
                removes.insert(o.cid);
        }
        for (const auto& o : ops) {
            if (o.ckind == "view" && !views)
                continue;
            Key key(o.ckind, o.cid, o.prop);
            if (o.op == "create" && o.ckind == "obj") {
                forget(o.cid);
                objects[o.cid] = Obj {true, o.cname, o.ctype};
            }
            else if (o.op == "remove" && o.ckind == "obj") {
                forget(o.cid);
                objects[o.cid] = Obj {false, o.cname, o.ctype};
            }
            else if (o.op == "addprop") {
                added[key] = o;
                removed.erase(key);
            }
            else if (o.op == "delprop") {
                added.erase(key);
                values.erase(key);
                removed.insert(key);
            }
            else if (o.op == "set") {
                touched.forward(o);
                if (!o.vafter.empty()) {
                    values[key] = o.vafter;
                    if (o.derived)
                        touch.erase(o.cid);
                }
                else if (o.derived && o.ckind == "obj") {
                    values.erase(key);
                    touch.insert(o.cid);
                }
                else if (!removes.count(o.cid)) {
                    return false;
                }
            }
        }
        return true;
    }
};

} // namespace

namespace {

/// Whether `t`, an open's record on `chain`, made the document other than
/// the rows before it add up to: see _moveAlongLog().
bool openRecordJumps(TransactionStore& store, const std::vector<LogVersion>& versions,
                     const LogTransaction& t, const std::vector<LogTransaction>& chain)
{
    if (t.kind != "restore")
        return false;
    std::set<int64_t> on {0};
    for (const auto& c : chain)
        on.insert(c.seq);
    const LogVersion* at = nullptr;
    const LogVersion* before = nullptr;
    for (const auto& v : versions) {
        if (v.seq == t.parent) {
            if (!at || v.num > at->num)
                at = &v;
        }
        else if (v.seq < t.parent && on.count(v.seq) && (!before || v.seq > before->seq)) {
            before = &v;
        }
    }
    if (!at || !before || at->docxml_hash.empty() || at->docxml_hash != before->docxml_hash)
        return true;
    for (const auto& c : chain) {
        if (c.seq > before->seq && c.seq <= t.parent && !store.ops(c.seq).empty())
            return true;
    }
    return false;
}

} // namespace

bool Document::_moveAlongLog(int64_t fromHead, int64_t toSeq, bool views)
{
    // docs/TransactionLog.md sec 27.25 item 1, 27.34. From the state at row
    // `fromHead` to the state at `toSeq` by the rows between them: those on
    // `fromHead`'s chain after the two chains meet, taken back, then those on
    // `toSeq`'s chain after it, taken forward -- folded into where every
    // object, dynamic property and value ends, and only what differs from
    // the document as it is written. No version is read whole, no shape that
    // did not change is parsed, and a move that changes nothing records
    // nothing. False, with nothing done, when the chains do not meet in
    // stored rows (a trim cut one), a row cannot be folded (a value the log
    // does not have), or a row made the document other than its ops say --
    // an open's record of a file that is not its history's tip, which a
    // save to the log only leaves (sec 27.28).
    TransactionLog* log = getTransactionLog();
    if (!log || fromHead < 0 || toSeq < 0)
        return false;
    // The newest rows' values may still be pending (as for a cold undo).
    log->resolvePending();
    auto& store = log->store();
    const auto from = store.chain(fromHead);
    const auto to = store.chain(toSeq);
    // Where the chains meet: the newest point of `toSeq`'s chain -- itself,
    // or a row's parent -- that is also on `fromHead`'s.
    std::set<int64_t> onFrom {0};
    for (const auto& t : from)
        onFrom.insert(t.seq);
    int64_t meet = -1;
    if (onFrom.count(toSeq)) {
        meet = toSeq;
    }
    else {
        for (auto it = to.rbegin(); it != to.rend(); ++it) {
            if (onFrom.count(it->parent)) {
                meet = it->parent;
                break;
            }
        }
    }
    if (meet < 0)
        return false;
    auto tail = [meet](const std::vector<LogTransaction>& chain, int64_t end,
                       std::vector<const LogTransaction*>& rows) {
        for (const auto& t : chain) {
            if (t.seq > meet)
                rows.push_back(&t);
        }
        // Whole down to the meeting point: a chain a trim cut starts at a
        // row whose parent is gone.
        if (rows.empty())
            return end == meet || end == 0;
        return rows.front()->parent == meet && rows.back()->seq == end;
    };
    std::vector<const LogTransaction*> back, forward;
    if (!tail(from, fromHead, back) || (toSeq != meet && !tail(to, toSeq, forward)))
        return false;

    // An open's record is the file as found; it jumped the document when
    // the file is not what the rows before it add up to. The version the row
    // recorded is the state at the row's parent -- snapshot() stamps the
    // version with the head, then numbers the row (sec 27.57) -- and the
    // rows add up to it when the newest version before it on the chain has
    // the same Document.xml bytes and no row with ops lies between the two.
    const auto versions = store.versions();
    auto jumps = [&](const LogTransaction& t, const std::vector<LogTransaction>& chain) {
        return openRecordJumps(store, versions, t, chain);
    };

    LogFold fold;
    for (auto it = back.rbegin(); it != back.rend(); ++it) {
        auto ops = store.ops((*it)->seq);
        if (ops.empty()) {
            // Back to exactly the row's parent is back to the file as found,
            // which is the version the row recorded: a jump matters only
            // past it.
            if (meet < (*it)->parent && jumps(**it, from))
                return false;
            if ((*it)->kind == "recompute")
                fold.touched.record((*it)->script, true);
            continue;
        }
        fold.back(ops, views);
        fold.touched.record((*it)->script, true);   // an undo's, a restore's (sec 27.63)
    }
    for (const LogTransaction* t : forward) {
        auto ops = store.ops(t->seq);
        if (ops.empty()) {
            if (jumps(*t, to))
                return false;
            if (t->kind == "recompute")
                fold.touched.record(t->script, false);
            continue;
        }
        if (!fold.forward(ops, views))
            return false;
        fold.touched.record(t->script, false);
    }

    // Every value there, before anything is written.
    std::map<LogFold::Key, CapturedValue> want;
    for (const auto& kv : fold.values) {
        CapturedValue v;
        if (!log->readValue(kv.second, v))
            return false;
        want.emplace(kv.first, std::move(v));
    }

    auto guarded = [&](const char* what, const std::string& name, const std::function<void()>& fn) {
        try {
            fn();
        }
        catch (Base::Exception& e) {
            FC_ERR("move along the log, " << what << " " << name << ": " << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("move along the log, " << what << " " << name << ": " << e.what());
        }
    };
    auto container = [&](const LogFold::Key& key) -> PropertyContainer* {
        LogOp o;
        o.ckind = std::get<0>(key);
        o.cid = std::get<1>(key);
        return opContainer(*this, o);
    };
    // 1. The objects there at the end that are not, under id and name.
    for (const auto& kv : fold.objects) {
        if (!kv.second.exists || getObjectByID(kv.first))
            continue;
        guarded("create", kv.second.name, [&]() {
            if (getObject(kv.second.name.c_str()))
                throw Base::RuntimeError("name taken by another object");
            Base::Type type = Base::Type::getTypeIfDerivedFrom(
                kv.second.type.c_str(), DocumentObject::getClassTypeId(), true);
            auto obj = type.isBad() ? nullptr : static_cast<DocumentObject*>(type.createInstance());
            if (!obj)
                throw Base::RuntimeError("cannot create " + kv.second.type);
            obj->_Id = kv.first;
            addObject(obj, kv.second.name.c_str(), false);
        });
    }
    // 2. Dynamic properties as they end.
    for (const auto& kv : fold.added) {
        guarded("add property", std::get<2>(kv.first), [&]() {
            auto c = container(kv.first);
            if (!c || c->getPropertyByName(std::get<2>(kv.first).c_str()))
                return;
            addLoggedProperty(*c, kv.second.ptype, std::get<2>(kv.first), kv.second.meta);
        });
    }
    for (const auto& key : fold.removed) {
        guarded("remove property", std::get<2>(key), [&]() {
            auto c = container(key);
            if (c && c->getPropertyByName(std::get<2>(key).c_str()))
                c->removeDynamicProperty(std::get<2>(key).c_str());
        });
    }
    // 3. Every value that differs from the document's -- each write touches
    // what it writes, so the touched state is taken first.
    std::set<long> valued;
    for (const auto& kv : want) {
        if (std::get<0>(kv.first) == "obj")
            valued.insert(std::get<1>(kv.first));
    }
    const auto touchedNow = fold.touched.save(*this, valued);
    CaptureConfig config(*this);
    RestoreBatch batch;   // each afterRestore() once all are in (sec 27.67)
    for (auto& kv : want) {
        guarded("value of", std::get<2>(kv.first), [&]() {
            auto c = container(kv.first);
            if (!c)
                return;   // a view with no Gui, or an object gone
            Property* prop = c->getPropertyByName(std::get<2>(kv.first).c_str());
            if (!prop)
                throw Base::RuntimeError("no such property");
            CapturedValue now = captureValue(config, *prop);
            const CapturedValue& v = kv.second;
            if (now.ok && now.fragment == v.fragment
                    && now.attachments.size() == v.attachments.size()
                    && std::equal(now.attachments.begin(), now.attachments.end(),
                                  v.attachments.begin(), [](const auto& a, const auto& b) {
                                      return a.name == b.name && a.bytes == b.bytes;
                                  }))
                return;
            log->restoreBlobsOf(fold.values[kv.first]);
            restoreValue(*prop, v);
        });
    }
    batch.finish();
    // 4. Touched as the end state was: the state the document had with what
    // the crossed rows said on top (sec 27.58), and an object whose derived
    // values the log did not keep touched.
    fold.touched.apply(*this, touchedNow);
    for (long cid : fold.touch) {
        if (auto obj = getObjectByID(cid))
            obj->touch();
    }
    // 5. What is gone at the end.
    for (const auto& kv : fold.objects) {
        if (kv.second.exists)
            continue;
        if (auto obj = getObjectByID(kv.first)) {
            guarded("remove", kv.second.name, [&]() { removeObject(obj->getNameInDocument()); });
        }
    }
    return true;
}

void Document::_checkoutHead(int64_t fromHead)
{
    // Sec 26.2 item 4: this document made the state at the log's current
    // head with nothing recorded: the branch's content did not change, the
    // document moved to it. Through the rows between where it was and there
    // when the log has them (sec 27.34); else the newest version on the
    // chain, read whole and checked out in place, and the rows after it
    // replayed.
    TransactionLog* log = getTransactionLog();
    auto& store = log->store();
    {
        Base::FlagToggler<> replaying(d->replaying);
        if (_moveAlongLog(fromHead, log->head(), true))
            return;
    }
    FC_LOG(getName() << ": switched by reading a version whole");
    const std::set<int64_t> onChain = chainPoints(store, log->head());
    LogVersion anchor;
    bool haveAnchor = false;
    for (const auto& v : store.versions()) {
        if (onChain.count(v.seq) && (!haveAnchor || v.num > anchor.num)) {
            anchor = v;
            haveAnchor = true;
        }
    }
    Base::FlagToggler<> replaying(d->replaying);
    if (haveAnchor) {
        _readVersion(anchor.num, [this](Document& version) { _applyVersion(version, true); });
    }
    else {
        // No version on the chain: every object's create is in its rows.
        std::vector<std::string> names;
        for (auto obj : getObjects())
            names.emplace_back(obj->getNameInDocument());
        for (const auto& name : names) {
            if (getObject(name.c_str()))
                removeObject(name.c_str());
        }
    }
    int64_t last = 0;
    _replayLog(haveAnchor ? anchor.seq : 0, last);
}

void Document::_arriveOnBranch()
{
    _rebuildUndoFromLog(d->undoFloor);
}

int64_t Document::createBranch(const std::string& name, int64_t version, int64_t seq)
{
    // docs/TransactionLog.md sec 17.1, 26: a new branch from a version, a
    // row, or the current head, and the document switched to it.
    checkNotFrozen("create a branch");
    TransactionLog* log = getTransactionLog();
    if (!log)
        return 0;
    _checkBranchable("create a branch");
    auto& store = log->store();
    LogBranch taken;
    if (name.empty() || store.findBranch(name, taken))
        THROWM(Base::ValueError, "branch name '" + name + "' is empty or taken");

    LogVersion fork;
    int64_t forkSeq = log->head();
    if (version > 0) {
        if (!store.getVersion(version, fork))
            THROWM(Base::ValueError, "no such version");
        forkSeq = fork.seq;
    }
    else if (seq > 0) {
        bool found = false;
        for (const auto& t : store.transactions(seq, 1))
            found = t.seq == seq;
        if (!found)
            THROWM(Base::ValueError, "no such row");
        forkSeq = seq;
    }
    LogBranch left;
    store.getBranch(log->branch(), left);
    const int64_t leftHead = log->head();

    _leaveBranch();
    // The fork is a version (sec 17.1), which the tip snapshot just made
    // when the fork is the head; otherwise one at that row, if there is.
    if (!fork.num) {
        for (const auto& v : store.versions()) {
            if (v.seq == forkSeq)
                fork = v;
        }
    }

    LogBranch branch;
    branch.name = name;
    branch.fromVersion = fork.num;
    branch.fromSeq = forkSeq;
    branch.head = forkSeq;
    // The branch it is made from (sec 30.3 S.a): the one the fork row is
    // on, which is this document's unless an older row or version was named.
    branch.target = left.id;
    if (forkSeq != leftHead) {
        for (const auto& t : store.transactions(forkSeq, 1)) {
            if (t.seq == forkSeq && t.branch)
                branch.target = t.branch;
        }
    }
    branch.created = std::chrono::duration<double>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    store.addBranch(branch);

    if (forkSeq != leftHead) {
        // The steps on the stacks are the other branch's.
        clearUndos();
        _clearRedos();
        log->setBranch(branch.id);
        _checkoutHead(leftHead);
        log->forgetLiveValues();
    }
    else {
        // The document does not change, and neither do its steps: the
        // chain behind the new branch's head is the one they were made on.
        log->setBranch(branch.id);
    }
    if (!fork.num) {
        // A row with no version: the one the document now is, named below.
        snapshotToLog();
        for (const auto& v : store.versions()) {
            if (v.seq == forkSeq)
                fork = v;
        }
    }
    if (fork.num) {
        if (fork.kind != "named")
            store.nameVersion(fork.num, "branch " + name);
        branch.fromVersion = fork.num;
        store.updateBranch(branch);
    }
    if (forkSeq != leftHead)
        _arriveOnBranch();

    std::ostringstream script;
    script << "{\"from_branch\":" << jsonString(left.name) << ",\"from_version\":" << fork.num
           << ",\"from_seq\":" << forkSeq << "}";
    log->record("branch", "Branch " + name, script.str());
    refreshVersionNames();
    signalBranchesChanged(*this);
    return branch.id;
}

bool Document::switchBranch(const std::string& name)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 17.1, 26: the document becomes the head of
    // another branch, in place; not an undo step (26.4) -- each branch has
    // its own steps, since this document was opened.
    checkNotFrozen("switch branch");
    TransactionLog* log = getTransactionLog();
    if (!log)
        return false;
    _checkBranchable("switch branch");
    auto& store = log->store();
    LogBranch branch;
    if (!store.findBranch(name, branch))
        THROWM(Base::ValueError, "no branch '" + name + "'");
    if (branch.id == log->branch())
        return true;
    if (branch.closed != 0)
        THROWM(Base::ValueError, "branch '" + name + "' is closed; branch from one of its versions");
    LogBranch left;
    store.getBranch(log->branch(), left);

    const int64_t fromHead = log->head();
    _leaveBranch();
    clearUndos();
    _clearRedos();
    log->setBranch(branch.id);
    _checkoutHead(fromHead);
    log->forgetLiveValues();
    _arriveOnBranch();

    std::ostringstream script;
    script << "{\"from_branch\":" << jsonString(left.name) << ",\"from_head\":" << left.head
           << "}";
    log->record("switch", "Switch from " + left.name, script.str());
    refreshVersionNames();
    signalBranchesChanged(*this);
    return true;
}

namespace {

/// Every row another branch's history holds, and every version a branch
/// forked from: what trimming or deleting `except` must leave (sec 16.7).
struct Shared
{
    std::set<int64_t> rows;
    std::set<int64_t> forks;
};

/** The net change of a run of rows (docs/TransactionLog.md sec 16.7, squash;
 * sec 28.2 item 3, one side of a merge): where every object and property
 * the rows touched stood before the first and stands after the last. Rows
 * are added oldest first.
 */
struct NetChange
{
    using Key = std::tuple<std::string, long, std::string>;   // ckind, cid, prop
    struct Obj
    {
        bool born {false};
        bool alive {true};
        std::string cname;
        std::string ctype;
    };
    struct Val
    {
        bool atStart {false};
        bool atEnd {false};
        std::string before;
        std::string after;
        std::string ptype;
        std::string meta;
        bool derived {false};
    };
    std::map<long, Obj> objects;
    std::vector<long> objectOrder;
    std::map<Key, Val> values;
    std::vector<Key> valueOrder;
    Obj& object(long cid, bool born)
    {
        auto it = objects.find(cid);
        if (it == objects.end()) {
            objectOrder.push_back(cid);
            it = objects.emplace(cid, Obj()).first;
            it->second.born = born;
        }
        return it->second;
    }

    void add(const std::vector<LogOp>& rowOps)
    {
        for (const auto& o : rowOps) {
            if (o.op == "create" || o.op == "remove") {
                // A view provider's comes and goes with its object, under
                // the same id: it is not the object, and its type is not
                // the one to make again.
                if (o.ckind != "obj")
                    continue;
                Obj& obj = object(o.cid, o.op == "create");
                obj.alive = o.op == "create";
                obj.cname = o.cname;
                obj.ctype = o.ctype;
                continue;
            }
            if (o.ckind == "obj")
                object(o.cid, false);   // a set on it first: it was there
            Key key {o.ckind, o.cid, o.prop};
            auto it = values.find(key);
            if (it == values.end()) {
                valueOrder.push_back(key);
                Val v;
                v.atStart = o.op == "delprop" || (o.op == "set" && !o.vbefore.empty());
                v.before = v.atStart ? o.vbefore : std::string();
                it = values.emplace(key, v).first;
            }
            Val& v = it->second;
            v.ptype = o.ptype;
            if (!o.meta.empty())
                v.meta = o.meta;
            if (o.op == "addprop") {
                v.atEnd = true;
            }
            else if (o.op == "delprop" || o.vafter.empty()) {
                // Removed, or a remove's set: gone with its object. Or a
                // derived value the log did not keep (sec 10).
                v.atEnd = false;
                v.after.clear();
                v.derived = o.op == "set" && o.derived;
            }
            else {
                v.atEnd = true;
                v.after = o.vafter;
                v.derived = o.derived;
            }
        }
    }

    /** As the ops of one row: an object born in the rows and there at the
     * end is a create with a set per property; every other property its net
     * set, addprop or delprop; an object there at the start and gone at the
     * end a remove after its sets.
     */
    std::vector<LogOp> ops()
    {
        auto opOf = [](const std::string& op, const Key& key, const Val& v) {
            LogOp o;
            o.op = op;
            o.ckind = std::get<0>(key);
            o.cid = std::get<1>(key);
            o.prop = std::get<2>(key);
            o.ptype = v.ptype;
            return o;
        };
        std::vector<LogOp> ops;
        // Each object's values in their order, looked up once per object: a
        // trim's span (sec 27.71) is hundreds of rows and objects.
        std::map<long, std::vector<const Key*>> ofObject;
        for (const auto& key : valueOrder) {
            if (std::get<0>(key) == "obj")
                ofObject[std::get<1>(key)].push_back(&key);
        }
        // Objects born in the span and there at its end: the create, then a
        // set per property, a dynamic one's metadata before it.
        for (long cid : objectOrder) {
            const Obj& obj = objects[cid];
            if (!obj.born || !obj.alive)
                continue;
            LogOp c;
            c.op = "create";
            c.ckind = "obj";
            c.cid = cid;
            c.cname = obj.cname;
            c.ctype = obj.ctype;
            ops.push_back(c);
            for (const Key* k : ofObject[cid]) {
                const Key& key = *k;
                const Val& v = values[key];
                if (!v.atEnd)
                    continue;
                if (!v.meta.empty()) {
                    auto a = opOf("addprop", key, v);
                    a.meta = v.meta;
                    ops.push_back(a);
                }
                auto s = opOf("set", key, v);
                s.vafter = v.after;
                s.derived = v.derived;
                ops.push_back(s);
            }
        }
        // Everything else: the net change of each property.
        for (const auto& key : valueOrder) {
            const Val& v = values[key];
            const std::string& ckind = std::get<0>(key);
            const long cid = std::get<1>(key);
            const bool owned = ckind == "obj" || ckind == "view";
            const Obj* owner = owned && objects.count(cid) ? &objects[cid] : nullptr;
            if (owner && owner->born && (!owner->alive || ckind == "obj"))
                continue;   // never there at either end, or written with its create
            const bool ownerGone = owner && !owner->born && !owner->alive && ckind == "obj";
            if (v.atStart && v.atEnd) {
                if (v.before == v.after)
                    continue;
                auto s = opOf("set", key, v);
                s.vbefore = v.before;
                s.vafter = v.after;
                s.derived = v.derived;
                ops.push_back(s);
            }
            else if (!v.atStart && v.atEnd) {
                auto a = opOf("addprop", key, v);
                a.meta = v.meta;
                ops.push_back(a);
                auto s = opOf("set", key, v);
                s.vafter = v.after;
                s.derived = v.derived;
                ops.push_back(s);
            }
            else if (v.atStart && !v.atEnd) {
                // A removed object's value rides on a set, before the remove; a
                // dynamic property removed from a living one is a delprop.
                auto o = opOf(ownerGone ? "set" : "delprop", key, v);
                o.vbefore = v.before;
                o.meta = v.meta;
                ops.push_back(o);
            }
        }
        // Objects there at the start and gone at the end, after their sets.
        for (long cid : objectOrder) {
            const Obj& obj = objects[cid];
            if (obj.born || obj.alive)
                continue;
            LogOp r;
            r.op = "remove";
            r.ckind = "obj";
            r.cid = cid;
            r.cname = obj.cname;
            r.ctype = obj.ctype;
            ops.push_back(r);
        }
        return ops;
    }
};

/// The net change of the rows `path` (oldest first), as the ops of one row.
std::vector<LogOp> netOps(TransactionStore& store, const std::vector<LogTransaction>& path)
{
    NetChange net;
    for (const auto& t : path)
        net.add(store.ops(t.seq));
    return net.ops();
}

/// The second parent a row standing for `rows` keeps (sec 28.2 item 1): the
/// newest merge among them. A row has one; an older merge of another branch
/// folded with it is forgotten, and that branch's next merge starts from an
/// older base -- it finds what ours already has, and asks again only where
/// ours was kept in a conflict.
int64_t newestMergeFrom(const std::vector<LogTransaction>& rows)
{
    int64_t from = 0;
    for (const auto& t : rows) {
        if (t.mergeFrom)
            from = t.mergeFrom;
    }
    return from;
}

Shared sharedWith(TransactionStore& store, int64_t except)
{
    Shared shared;
    for (const auto& b : store.branches()) {
        if (b.fromVersion)
            shared.forks.insert(b.fromVersion);
        if (b.id == except)
            continue;
        for (const auto& t : store.chain(b.head))
            shared.rows.insert(t.seq);
    }
    return shared;
}

} // namespace

bool Document::renameBranch(const std::string& name, const std::string& newName)
{
    TransactionLog* log = getTransactionLog();
    if (!log)
        return false;
    auto& store = log->store();
    LogBranch branch;
    if (!store.findBranch(name, branch))
        THROWM(Base::ValueError, "no branch '" + name + "'");
    if (newName == name)
        return true;
    LogBranch taken;
    if (newName.empty() || store.findBranch(newName, taken))
        THROWM(Base::ValueError, "branch name '" + newName + "' is empty or taken");
    store.renameBranch(branch.id, newName);
    // The file says which branch it is (26.6): kept in step for the one the
    // document is on, as the next save would write it.
    if (branch.id == log->branch()) {
        Base::FlagToggler<> quiet(d->bookkeeping, false);
        if (auto prop = Base::freecad_dynamic_cast<PropertyString>(getPropertyByName("Branch")))
            prop->setValue(newName);
    }
    refreshVersionNames();
    signalBranchesChanged(*this);
    return true;
}

size_t Document::trimBranch(const std::string& name, int64_t version, bool bridge)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 16.7, "trim a branch".
    TransactionLog* log = getTransactionLog();
    if (!log)
        return 0;
    _checkBranchable("trim a branch");
    auto& store = log->store();
    LogBranch branch;
    if (!store.findBranch(name, branch))
        THROWM(Base::ValueError, "no branch '" + name + "'");
    const bool current = branch.id == log->branch();

    LogVersion keep;
    const std::set<int64_t> points = chainPoints(store, branch.head);
    if (version > 0) {
        if (!store.getVersion(version, keep) || !points.count(keep.seq))
            THROWM(Base::ValueError, "version " + std::to_string(version) + " is not on branch '"
                                         + name + "'");
    }
    else {
        // Up to the head: the version the head is, made now for the branch
        // the document is on; another branch's tip was taken when it was
        // left.
        if (current)
            _leaveBranch();
        for (const auto& v : store.versions()) {
            if (points.count(v.seq) && v.num > keep.num)
                keep = v;
        }
        bool atHead = keep.num > 0;
        for (const auto& t : store.chain(branch.head, keep.seq + 1))
            atHead = atHead && store.ops(t.seq).empty();
        if (!atHead)
            THROWM(Base::RuntimeError, "branch '" + name + "' has no version at its head");
    }
    if (keep.kind != "named")
        store.nameVersion(keep.num, "trim " + name);

    log->resolvePending();
    const Shared shared = sharedWith(store, branch.id);
    const auto chain = store.chain(branch.head);
    std::vector<int64_t> rows;
    std::set<int64_t> gone;
    for (const auto& t : chain) {
        if (t.seq <= keep.seq && !shared.rows.count(t.seq)) {
            rows.push_back(t.seq);
            gone.insert(t.seq);
        }
    }
    std::vector<int64_t> versions;
    for (const auto& v : store.versions()) {
        if (v.num != keep.num && v.kind != "named" && !shared.forks.count(v.num)
                && gone.count(v.seq))
            versions.push_back(v.num);
    }
    const std::set<long> named = _objectIdsOfRows(rows);

    // The bridge (sec 27.71, user): where another branch shares this one's
    // history, the rows from the newest shared one -- or from the start, for
    // a branch made from nothing -- up to the kept version are squashed into
    // the kept version's own row rather than removed, so the two histories
    // still meet in rows and a switch between them walks them instead of
    // reading a version whole. Below that row nothing is removed: it is the
    // other branch's history too.
    std::vector<LogTransaction> span;
    int64_t from = 0;
    if (bridge && !rows.empty() && store.branches().size() > 1) {
        for (const auto& t : chain) {
            if (t.seq <= keep.seq && shared.rows.count(t.seq))
                from = std::max(from, t.seq);
        }
        span = store.chain(keep.seq, from + 1);
        if (span.empty() || span.back().seq != keep.seq || span.front().parent != from)
            span.clear();
    }
    size_t squashed = 0;
    if (!span.empty()) {
        std::vector<LogOp> ops = netOps(store, span);
        LogTransaction t = span.back();
        t.parent = from;
        t.id = 0;
        t.kind = "squash";
        t.origin.clear();
        t.inverts = 0;
        t.mergeFrom = newestMergeFrom(span);
        t.name = "Trim " + name + " to version " + std::to_string(keep.num);
        std::ostringstream bridgeScript;
        bridgeScript << "{\"trim\":" << jsonString(name) << ",\"from\":" << from
                     << ",\"rows\":" << span.size() << ",\"ops\":" << ops.size() << "}";
        t.script = bridgeScript.str();
        std::set<int64_t> inSpan;
        std::vector<int64_t> drop;
        for (const auto& r : span) {
            inSpan.insert(r.seq);
            if (r.seq != keep.seq)
                drop.push_back(r.seq);
        }
        store.replaceTransactions(t, ops, drop);
        squashed = drop.size();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&inSpan](int64_t seq) { return inSpan.count(seq) != 0; }),
                   rows.end());
    }
    if (!rows.empty())
        store.removeTransactions(rows);
    if (!versions.empty())
        store.evictVersions(versions);
    if (current) {
        // The steps that named the rows gone are gone with them, and the
        // bridge is none: nothing is undone past the kept version (16.7).
        if (!span.empty())
            d->undoFloor = std::max<int64_t>(d->undoFloor, keep.seq);
        clearUndos();
        _clearRedos();
        _rebuildUndoFromLog(d->undoFloor);
    }
    const CompactEstimate estimate = _noteDroppedRows(named);
    const size_t removed = rows.size() + squashed;

    std::ostringstream script;
    script << "{\"branch\":" << jsonString(name) << ",\"version\":" << keep.num
           << ",\"rows\":" << removed << ",\"bridge\":" << (span.empty() ? 0 : span.size())
           << ",\"versions\":" << versions.size() << compactJson(estimate) << "}";
    log->record("trim", "Trim " + name + " to version " + std::to_string(keep.num), script.str());
    refreshVersionNames();
    signalBranchesChanged(*this);
    return removed;
}

size_t Document::deleteBranch(const std::string& name)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 16.7, "delete a branch".
    TransactionLog* log = getTransactionLog();
    if (!log)
        return 0;
    _checkBranchable("delete a branch");
    auto& store = log->store();
    LogBranch branch;
    if (!store.findBranch(name, branch))
        THROWM(Base::ValueError, "no branch '" + name + "'");
    if (branch.id == log->branch())
        THROWM(Base::ValueError, "branch '" + name + "' is the one the document is on");

    const Shared shared = sharedWith(store, branch.id);
    std::vector<int64_t> rows;
    std::set<int64_t> gone;
    for (const auto& t : store.chain(branch.head)) {
        if (!shared.rows.count(t.seq)) {
            rows.push_back(t.seq);
            gone.insert(t.seq);
        }
    }
    // Its versions, and any other taken at one of its own rows; a version
    // another branch forked from stays, named (sec 16.7).
    std::vector<int64_t> versions;
    size_t kept = 0;
    for (const auto& v : store.versions()) {
        if (v.branch != branch.id && !gone.count(v.seq))
            continue;
        if (shared.forks.count(v.num)) {
            ++kept;
            continue;
        }
        versions.push_back(v.num);
    }
    const std::set<long> named = _objectIdsOfRows(rows);
    store.removeTransactions(rows);
    if (!versions.empty())
        store.evictVersions(versions);
    store.removeBranch(branch.id);
    const CompactEstimate estimate = _noteDroppedRows(named);

    std::ostringstream script;
    script << "{\"deleted\":" << jsonString(name) << ",\"rows\":" << rows.size()
           << ",\"versions\":" << versions.size() << ",\"kept\":" << kept
           << compactJson(estimate) << "}";
    log->record("trim", "Delete branch " + name, script.str());
    refreshVersionNames();
    signalBranchesChanged(*this);
    return rows.size();
}

size_t Document::squashVersions(int64_t from, int64_t to)
{
    // docs/TransactionLog.md sec 16.7, "squash": the row `to` names is
    // rewritten as the net change since `from`, the rows between go, so
    // no seq changes -- the rows after it, and a branch forked at it,
    // still follow it.
    TransactionLog* log = getTransactionLog();
    if (!log)
        return 0;
    _checkBranchable("squash");
    log->resolvePending();
    auto& store = log->store();
    LogVersion first, last;
    if (!store.getVersion(from, first) || !store.getVersion(to, last))
        THROWM(Base::ValueError, "no such version");
    if (first.seq >= last.seq || !chainPoints(store, last.seq).count(first.seq))
        THROWM(Base::ValueError, "version " + std::to_string(from) + " is not behind version "
                                     + std::to_string(to) + " on one history");
    const auto path = store.chain(last.seq, first.seq + 1);
    if (path.empty() || path.back().seq != last.seq)
        THROWM(Base::ValueError, "the history between the versions is not in the log");
    std::set<int64_t> inside;
    for (const auto& t : path) {
        if (t.seq != last.seq)
            inside.insert(t.seq);
    }
    for (const auto& b : store.branches()) {
        bool through = false;
        bool into = false;
        for (const auto& t : store.chain(b.head)) {
            through = through || t.seq == last.seq;
            into = into || inside.count(t.seq);
        }
        if (into && !through)
            THROWM(Base::ValueError, "branch '" + b.name + "' forks between the versions");
    }
    std::vector<int64_t> evict;
    for (const auto& v : store.versions()) {
        if (!inside.count(v.seq))
            continue;
        if (v.kind == "named")
            THROWM(Base::ValueError, "named version " + std::to_string(v.num)
                                         + " sits between the versions");
        evict.push_back(v.num);
    }

    std::vector<LogOp> ops = netOps(store, path);

    LogTransaction t = path.back();
    t.parent = first.seq;
    t.id = 0;
    t.kind = "squash";
    t.origin.clear();
    t.inverts = 0;
    t.mergeFrom = newestMergeFrom(path);
    t.name = "Squash of versions " + std::to_string(from) + " to " + std::to_string(to);
    std::ostringstream script;
    script << "{\"from\":" << from << ",\"to\":" << to << ",\"rows\":" << path.size()
           << ",\"ops\":" << ops.size() << "}";
    t.script = script.str();
    std::vector<int64_t> drop(inside.begin(), inside.end());
    std::set<long> named = _objectIdsOfRows(drop);
    for (const auto& o : store.ops(last.seq)) {
        if (o.ckind == "obj")
            named.insert(o.cid);
    }
    store.replaceTransactions(t, ops, drop);
    if (!evict.empty())
        store.evictVersions(evict);
    // Steps of this branch's that named the rows gone, gone with them.
    if (chainPoints(store, log->head()).count(last.seq)) {
        clearUndos();
        _clearRedos();
        _rebuildUndoFromLog(d->undoFloor);
    }
    // The squash row is written already; the estimate goes to the file's
    // count only (sec 27.48).
    _noteDroppedRows(named);
    refreshVersionNames();
    signalBranchesChanged(*this);
    return path.size();
}

namespace {

/// A row's ops taken back -- newest first, each reversed -- so that a run
/// of rows crossed backward folds into a NetChange like one crossed forward.
std::vector<LogOp> invertedOps(const std::vector<LogOp>& ops)
{
    std::vector<LogOp> out;
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        LogOp o = *it;
        if (o.op == "create") {
            o.op = "remove";
        }
        else if (o.op == "remove") {
            o.op = "create";
        }
        else if (o.op == "addprop") {
            o.op = "delprop";
        }
        else if (o.op == "delprop") {
            // Back with the value it went with: the property, then its set.
            LogOp add = o;
            add.op = "addprop";
            add.vbefore.clear();
            out.push_back(std::move(add));
            o.op = "set";
            o.vafter = o.vbefore;
            o.vbefore.clear();
        }
        else {
            std::swap(o.vbefore, o.vafter);
        }
        out.push_back(std::move(o));
    }
    return out;
}

/// What the document was where `chain`, one that hangs off no row, starts:
/// the Document.xml hash of the version taken there on its oldest row's
/// branch -- the file as found -- or of the version that branch was made
/// from; empty for a document that started empty. Two chains that both
/// start at no row share a state only when these agree: the closed branches
/// of sec 16.6 start from another file than the `main` that replaced them.
std::string zeroStateOf(TransactionStore& store, const std::vector<LogVersion>& versions,
                        const std::vector<LogTransaction>& chain)
{
    if (chain.empty() || chain.front().parent != 0)
        return {};
    const int64_t branch = chain.front().branch;
    auto stateOf = [](const LogVersion& v) {
        return v.docxml_hash.empty() ? "v" + std::to_string(v.num) : v.docxml_hash;
    };
    const LogVersion* at = nullptr;
    for (const auto& v : versions) {
        if (v.seq == 0 && v.branch == branch && (!at || v.num > at->num))
            at = &v;
    }
    if (at)
        return stateOf(*at);
    LogBranch b;
    if (!store.getBranch(branch, b))
        return "branch " + std::to_string(branch);   // gone: nothing says
    LogVersion from;
    if (b.fromVersion && store.getVersion(b.fromVersion, from) && from.seq == 0)
        return stateOf(from);
    return {};
}

/// The rows between two points of the log: where the chains ending at
/// `from` and `to` meet, the rows of `from`'s after that (to be taken back)
/// and of `to`'s (to be done), each oldest first.
struct LogPath
{
    int64_t meet {-1};
    std::vector<LogTransaction> fromChain;
    std::vector<LogTransaction> toChain;
    std::vector<const LogTransaction*> back;
    std::vector<const LogTransaction*> forward;
};

bool pathBetween(TransactionStore& store, const std::vector<LogVersion>& versions, int64_t from,
                 int64_t to, LogPath& path, std::string& why)
{
    path.fromChain = store.chain(from);
    path.toChain = store.chain(to);
    std::set<int64_t> onFrom {0};
    for (const auto& t : path.fromChain)
        onFrom.insert(t.seq);
    if (onFrom.count(to)) {
        path.meet = to;
    }
    else {
        for (auto it = path.toChain.rbegin(); it != path.toChain.rend(); ++it) {
            if (onFrom.count(it->parent)) {
                path.meet = it->parent;
                break;
            }
        }
    }
    if (path.meet < 0) {
        why = "rows " + std::to_string(from) + " and " + std::to_string(to)
            + " share no history in the log's rows";
        return false;
    }
    const int64_t meet = path.meet;
    auto tail = [meet](const std::vector<LogTransaction>& chain, int64_t end,
                       std::vector<const LogTransaction*>& rows) {
        for (const auto& t : chain) {
            if (t.seq > meet)
                rows.push_back(&t);
        }
        if (rows.empty())
            return end == meet || end == 0;
        return rows.front()->parent == meet && rows.back()->seq == end;
    };
    if (!tail(path.fromChain, from, path.back)
            || (to != meet && !tail(path.toChain, to, path.forward))) {
        why = "the rows between " + std::to_string(from) + " and " + std::to_string(to)
            + " are not all in the log";
        return false;
    }
    if (meet == 0 && !path.fromChain.empty() && !path.toChain.empty()
            && zeroStateOf(store, versions, path.fromChain)
                   != zeroStateOf(store, versions, path.toChain)) {
        why = "rows " + std::to_string(from) + " and " + std::to_string(to)
            + " start from different files";
        return false;
    }
    return true;
}

/// The net change from the state at row `from` to the state at row `to`
/// (docs/TransactionLog.md sec 28.2 item 3), through the rows between them.
/// False, with `why`, when the log's rows do not say.
bool diffRows(TransactionStore& store, const std::vector<LogVersion>& versions, int64_t from,
              int64_t to, NetChange& net, std::string& why)
{
    LogPath path;
    if (!pathBetween(store, versions, from, to, path, why))
        return false;
    auto jumped = [&](const LogTransaction& t) {
        why = "row " + std::to_string(t.seq) + " opened a file that is not its history's";
        return false;
    };
    for (auto it = path.back.rbegin(); it != path.back.rend(); ++it) {
        const auto ops = store.ops((*it)->seq);
        if (ops.empty()) {
            if (path.meet < (*it)->parent && openRecordJumps(store, versions, **it, path.fromChain))
                return jumped(**it);
            continue;
        }
        net.add(invertedOps(ops));
    }
    for (const LogTransaction* t : path.forward) {
        const auto ops = store.ops(t->seq);
        if (ops.empty()) {
            // The open that starts a chain is the state the chain starts
            // from, which pathBetween() matched.
            const bool start = t->parent == 0 && path.meet == 0;
            if (!start && openRecordJumps(store, versions, *t, path.toChain))
                return jumped(*t);
            continue;
        }
        net.add(ops);
    }
    return true;
}

/// The base of a merge (sec 28.2 item 2): the newest row both histories
/// hold -- a row's ancestors all come before it, so the newest one in
/// common is an ancestor of no other. 0 when the two share no row but start
/// from one state at no row; -1 when they share nothing.
int64_t mergeBaseOf(TransactionStore& store, const std::vector<LogVersion>& versions,
                    int64_t ours, int64_t theirs)
{
    std::set<int64_t> mine;
    for (const auto& t : store.history(ours))
        mine.insert(t.seq);
    int64_t base = -1;
    for (const auto& t : store.history(theirs)) {
        if (mine.count(t.seq))
            base = std::max(base, t.seq);
    }
    if (base > 0)
        return base;
    const auto a = store.chain(ours);
    const auto b = store.chain(theirs);
    auto fromZero = [](const std::vector<LogTransaction>& chain, int64_t head) {
        return chain.empty() ? head == 0 : chain.front().parent == 0;
    };
    if (!fromZero(a, ours) || !fromZero(b, theirs))
        return -1;
    if (!a.empty() && !b.empty()
            && zeroStateOf(store, versions, a) != zeroStateOf(store, versions, b))
        return -1;
    return 0;
}

bool netChanged(const NetChange::Val& v)
{
    return v.atStart != v.atEnd || v.before != v.after;
}

/// A merge worked out (sec 28.2): what each side did since the base, and
/// theirs' changes classified against ours.
struct MergePlan
{
    Document::MergePreview preview;
    NetChange theirs;
    NetChange ours;
    std::string oursName;
    /// The two share no base (sec 30.22): theirs is a branch from nothing,
    /// and ours is weighed by what it holds.
    bool independent {false};
};

/** Ours, for a merge with no base (docs/TransactionLog.md sec 30.22). An
 * independent branch -- a file that shares no history with this one,
 * brought as what it is -- has made everything it holds, and there is no
 * state the two started from to say what ours did. So ours is what the
 * document holds: for every value theirs has of an object or a property
 * ours has too, ours' own, as if ours had made it as well. The same content
 * is then the same value -- one hash, the log's values being named by
 * their content -- and anything else is for a side to be picked. A value
 * ours' last write of was a recompute's is derived, as on any merge. What
 * ours has and theirs has not is left alone: with no base, not having a
 * thing is not having removed it.
 */
void independentOurs(Document& doc, TransactionLog& log, MergePlan& plan)
{
    auto& store = log.store();
    TransactionLogCore& core = TransactionLogCore::of(doc.getFileHistory());
    const CaptureConfig config(doc);
    const int64_t head = log.head();
    for (const auto& key : plan.theirs.valueOrder) {
        const NetChange::Val& v = plan.theirs.values[key];
        if (!v.atEnd || v.after.empty())
            continue;
        LogOp at;
        at.ckind = std::get<0>(key);
        at.cid = std::get<1>(key);
        PropertyContainer* container = opContainer(doc, at);
        Property* prop = container ? container->getPropertyByName(std::get<2>(key).c_str())
                                   : nullptr;
        if (!prop)
            continue;   // theirs alone has it: it comes
        const CapturedValue now = captureValue(config, *prop);
        if (!now.ok)
            continue;
        NetChange::Val mine;
        mine.atEnd = true;
        mine.ptype = v.ptype;
        LogOp last;
        const bool logged = store.lastOpOn(at.ckind, at.cid, std::get<2>(key), 0, head, last);
        mine.derived = (logged && last.derived)
            || (container->getPropertyType(prop) & Prop_Output) != 0;
        CapturedValue theirs;
        const bool same = log.readValue(v.after, theirs) && now.fragment == theirs.fragment
            && now.attachments.size() == theirs.attachments.size()
            && std::equal(now.attachments.begin(), now.attachments.end(),
                          theirs.attachments.begin(), [](const auto& a, const auto& b) {
                              return a.name == b.name && a.bytes == b.bytes;
                          });
        if (same) {
            mine.after = v.after;
        }
        else if (logged && last.op == "set" && !last.vafter.empty() && last.vafter != v.after) {
            mine.after = last.vafter;
        }
        else {
            // Never written in a row -- a value the file was opened with:
            // stored now, so the preview can show it.
            log.flush();
            mine.after = core.putValue(now, "durable");
        }
        plan.ours.valueOrder.push_back(key);
        plan.ours.values.emplace(key, std::move(mine));
    }
}

void planMerge(Document& doc, const std::string& name, int64_t version, MergePlan& plan)
{
    TransactionLog* log = doc.getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "the document has no transaction log");
    if (log->detached())
        THROWM(Base::RuntimeError, "the document is a version with no branch of its own yet");
    auto& store = log->store();
    LogBranch theirs;
    if (!store.findBranch(name, theirs))
        THROWM(Base::ValueError, "no branch '" + name + "'");
    if (theirs.id == log->branch())
        THROWM(Base::ValueError, "branch '" + name + "' is the one the document is on");
    // Theirs open in another document of the file (sec 17.1, 28.2 item 8):
    // what it has done is in rows before they are read.
    if (Document* holder = log->holderOf(theirs.id)) {
        if (holder != &doc) {
            holder->commitImplicitTransaction();
            if (holder->hasPendingTransaction())
                THROWM(Base::RuntimeError, "branch '" + name + "' has a transaction open in "
                                               + holder->Label.getStrValue());
            if (auto other = holder->getTransactionLog())
                other->resolvePending();
        }
    }
    log->resolvePending();
    store.getBranch(theirs.id, theirs);
    LogBranch mine;
    store.getBranch(log->branch(), mine);
    plan.oursName = mine.name;

    auto& pv = plan.preview;
    pv.branch = name;
    pv.ours = log->head();
    pv.theirs = theirs.head;
    if (version > 0) {
        LogVersion v;
        if (!store.getVersion(version, v) || !chainPoints(store, theirs.head).count(v.seq))
            THROWM(Base::ValueError, "version " + std::to_string(version) + " is not on branch '"
                                         + name + "'");
        pv.theirs = v.seq;
    }
    // Already ours: merged before, or ours was made from it.
    bool held = pv.theirs == pv.ours;
    for (const auto& t : store.history(pv.ours))
        held = held || t.seq == pv.theirs;
    if (held) {
        pv.base = pv.theirs;
        return;
    }
    const auto versions = store.versions();
    pv.base = mergeBaseOf(store, versions, pv.ours, pv.theirs);
    if (pv.base < 0) {
        // No row in common and no state both started from. A branch that
        // hangs off no row and started empty holds all it is in its rows
        // -- an independent branch (sec 30.22) -- and is merged by what the
        // two hold. Any other shares nothing a merge can read.
        const auto chain = store.chain(pv.theirs);
        if (chain.empty() || chain.front().parent != 0
                || !zeroStateOf(store, versions, chain).empty())
            THROWM(Base::ValueError, "branches '" + mine.name + "' and '" + name
                                         + "' share no history in the log");
        plan.independent = true;
        for (const auto& t : chain)
            plan.theirs.add(store.ops(t.seq));
        independentOurs(doc, *log, plan);
    }
    else {
        std::string why;
        if (!diffRows(store, versions, pv.base, pv.theirs, plan.theirs, why)
                || !diffRows(store, versions, pv.base, pv.ours, plan.ours, why))
            THROWM(Base::ValueError, "cannot merge branch '" + name + "': " + why);
    }

    // What ours changed: view state and what its own recomputes wrote are
    // not changes a merge weighs (sec 28.6 Q1, Q2).
    std::set<long> oursChanged;
    bool changed = false;
    for (const auto& kv : plan.ours.objects) {
        if (kv.second.born == kv.second.alive) {   // created, or removed
            changed = true;
            if (kv.second.alive)
                oursChanged.insert(kv.first);
        }
    }
    for (const auto& kv : plan.ours.values) {
        const std::string& ckind = std::get<0>(kv.first);
        if (ckind == "view" || kv.second.derived || !netChanged(kv.second))
            continue;
        if (ckind == "doc" && keptOnRestore(std::get<2>(kv.first).c_str()))
            continue;
        changed = true;
        if (ckind == "obj")
            oursChanged.insert(std::get<1>(kv.first));
    }
    // With no base nothing of theirs is ours moved on: it is all weighed.
    pv.fastForward = !changed && !plan.independent;
    // Sec 30.4 P1: ours has not moved since the base at all -- records, a
    // save or a snapshot, are all it has -- and the base is on both chains:
    // theirs' rows can be taken as they are.
    {
        const auto oursAfter = store.chain(pv.ours, pv.base + 1);
        const auto theirsAfter = store.chain(pv.theirs, pv.base + 1);
        bool direct = !theirsAfter.empty() && theirsAfter.front().parent == pv.base
                   && (oursAfter.empty() ? pv.ours == pv.base
                                         : oursAfter.front().parent == pv.base);
        std::vector<int64_t> records;
        for (const auto& t : oursAfter) {
            direct = direct && store.ops(t.seq).empty();
            records.push_back(t.seq);
        }
        // Those records leave the chain for its end (mergeBranch): not
        // when another branch, or another document of the file, stands on
        // one of them.
        if (direct && !records.empty()) {
            direct = !log->othersStandOn(records);
            for (const auto& b : store.branches()) {
                if (!direct || b.id == log->branch())
                    continue;
                for (const auto& t : store.chain(b.head, pv.base + 1)) {
                    if (std::find(records.begin(), records.end(), t.seq) != records.end())
                        direct = false;
                }
            }
        }
        bool any = false;
        if (direct) {
            for (const auto& t : theirsAfter)
                any = any || !store.ops(t.seq).empty();
        }
        if (any) {
            for (const auto& t : theirsAfter)
                pv.forward.push_back(t.seq);
        }
    }

    auto nameOf = [&](long cid) -> std::string {
        if (auto obj = doc.getObjectByID(cid))
            return obj->getNameInDocument();
        for (const NetChange* net : {&plan.theirs, &plan.ours}) {
            auto it = net->objects.find(cid);
            if (it != net->objects.end() && !it->second.cname.empty())
                return it->second.cname;
        }
        return "#" + std::to_string(cid);
    };
    auto add = [&](Document::MergeChange c) {
        if (c.kind == "conflict")
            ++pv.conflicts;
        pv.changes.push_back(std::move(c));
    };

    std::set<long> created;
    for (long cid : plan.theirs.objectOrder) {
        const NetChange::Obj& o = plan.theirs.objects[cid];
        DocumentObject* live = doc.getObjectByID(cid);
        Document::MergeChange c;
        c.ckind = "obj";
        c.cid = cid;
        c.object = live ? live->getNameInDocument() : o.cname;
        c.key = c.object;
        c.ptype = o.ctype;
        if (o.born && o.alive) {
            if (live)
                continue;   // ours has it: its values go by the property rule
            created.insert(cid);
            c.kind = "take";
            c.op = "create";
            add(std::move(c));
        }
        else if (!o.born && !o.alive) {
            c.op = "remove";
            c.kind = "take";
            if (!live) {
                c.kind = "same";
            }
            else if (oursChanged.count(cid)) {
                c.kind = "conflict";
                c.note = "changed here, removed there";
            }
            else {
                // What ours made or changed that still uses it.
                for (auto dep : live->getInList()) {
                    auto t = plan.theirs.objects.find(dep->getID());
                    if (t != plan.theirs.objects.end() && !t->second.alive)
                        continue;
                    if (oursChanged.count(dep->getID())) {
                        c.kind = "conflict";
                        c.note = std::string("removed there, used here by ")
                               + dep->getNameInDocument();
                        break;
                    }
                }
            }
            add(std::move(c));
        }
    }

    std::set<long> revived;
    for (const auto& key : plan.theirs.valueOrder) {
        const NetChange::Val& v = plan.theirs.values[key];
        const std::string& ckind = std::get<0>(key);
        const long cid = std::get<1>(key);
        const std::string& prop = std::get<2>(key);
        if (!v.derived && !netChanged(v))
            continue;
        if (ckind == "doc" && keptOnRestore(prop.c_str()))
            continue;
        Document::MergeChange c;
        c.ckind = ckind;
        c.cid = cid;
        c.prop = prop;
        c.ptype = v.ptype;
        c.base = v.atStart ? v.before : std::string();
        c.theirs = v.atEnd ? v.after : std::string();
        c.ours = c.base;
        c.op = v.atStart && v.atEnd ? "set" : (v.atEnd ? "addprop" : "delprop");
        if (ckind == "doc") {
            c.key = "." + prop;
        }
        else {
            if (created.count(cid))
                continue;   // goes in with its object
            auto t = plan.theirs.objects.find(cid);
            if (t != plan.theirs.objects.end() && !t->second.alive)
                continue;   // gone with its object
            c.object = nameOf(cid);
            c.key = (ckind == "view" ? "view:" : "") + c.object + "." + prop;
            if (!doc.getObjectByID(cid)) {
                // Not ours any more. One ours removed and theirs changed is
                // a conflict on the object; what theirs' recompute or its
                // view did to it is not a change to bring it back for.
                auto o = plan.ours.objects.find(cid);
                if (v.derived || ckind == "view" || o == plan.ours.objects.end()
                        || o->second.born || o->second.alive || !revived.insert(cid).second)
                    continue;
                Document::MergeChange r;
                r.kind = "conflict";
                r.op = "revive";
                r.ckind = "obj";
                r.cid = cid;
                r.object = c.object;
                r.key = c.object;
                r.ptype = o->second.ctype;
                r.note = "removed here, changed there";
                add(std::move(r));
                continue;
            }
        }
        if (v.derived) {
            c.derived = true;
            c.kind = pv.fastForward ? "take" : "derived";
            add(std::move(c));
            continue;
        }
        auto o = plan.ours.values.find(key);
        if (o == plan.ours.values.end() || o->second.derived || !netChanged(o->second)) {
            c.kind = "take";
        }
        else {
            c.ours = o->second.atEnd ? o->second.after : std::string();
            if (o->second.atEnd == v.atEnd && o->second.after == v.after)
                c.kind = "same";
            else
                c.kind = ckind == "view" ? "view" : "conflict";
        }
        add(std::move(c));
    }
}

} // namespace

Document::MergePreview Document::previewMerge(const std::string& branch, int64_t version)
{
    // docs/TransactionLog.md sec 28.2 item 9: read only, but for an implicit
    // transaction, which is committed so the rows say what the document is.
    if (d->activeUndoTransaction)
        commitImplicitTransaction();
    MergePlan plan;
    planMerge(*this, branch, version, plan);
    return plan.preview;
}

Document::MergeResult Document::mergeBranch(const std::string& branch,
                                            const std::map<std::string, std::string>& picks,
                                            const std::string& fallback, int64_t version)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 28: theirs' changes since the base, less
    // what ours changed too, as one transaction on ours.
    checkNotFrozen("merge a branch");
    MergeResult result;
    TransactionLog* log = getTransactionLog();
    if (!log)
        return result;
    _checkBranchable("merge a branch");
    auto sideOk = [](const std::string& side) { return side == "ours" || side == "theirs"; };
    if (!fallback.empty() && !sideOk(fallback))
        THROWM(Base::ValueError, "a side is 'ours' or 'theirs', not '" + fallback + "'");
    for (const auto& kv : picks) {
        if (!sideOk(kv.second))
            THROWM(Base::ValueError, "a side is 'ours' or 'theirs', not '" + kv.second + "'");
    }

    MergePlan plan;
    planMerge(*this, branch, version, plan);
    result.preview = plan.preview;
    const MergePreview& pv = plan.preview;
    // The version at the head merged in is what was merged (sec 17.3): it
    // is named, so it outlives eviction and says where a merge was.
    auto nameMerged = [&]() {
        const LogVersion* at = nullptr;
        const auto versions = log->store().versions();
        for (const auto& v : versions) {
            if (v.seq == pv.theirs && (!at || v.num > at->num))
                at = &v;
        }
        if (at && at->kind != "named")
            log->store().nameVersion(at->num, "merged into " + plan.oursName);
    };
    // Rows another copy of the file made, imported (sec 30.13 F2), carry
    // no derived values: where a merge takes rows as they are, what they
    // left out is computed here.
    auto imported = [&]() {
        for (const auto& t : log->store().chain(pv.theirs, pv.base + 1)) {
            if (t.script.find("\"imported\"") != std::string::npos)
                return true;
        }
        return false;
    };
    if (!pv.forward.empty()) {
        // Sec 30.4 P1, a fast-forward: ours has not moved since the base,
        // so theirs' rows are taken as they are -- each the row it was made
        // as, under its author -- and ours' head moves onto them. The merge
        // writes no row of its own.
        auto& store = log->store();
        LogBranch mine;
        LogBranch theirs;
        store.getBranch(log->branch(), mine);
        store.findBranch(branch, theirs);
        // Made from this branch, theirs gives its rows up: they are this
        // branch's now, and theirs starts again where they end. Any other
        // branch keeps its rows, and this one's head rests on them as a
        // new branch's rests on the rows it was made from.
        std::vector<int64_t> own;
        if (theirs.target == mine.id) {
            for (const auto& t : store.chain(pv.theirs, pv.base + 1)) {
                if (t.branch == theirs.id)
                    own.push_back(t.seq);
            }
        }
        // Records of ours since the base -- a save, a snapshot -- changed
        // nothing. A row is numbered after the row it follows, so they
        // cannot stay between the base and theirs' rows: the versions
        // taken at them, which are the state at the base, are the base's
        // now, and the records are written again once the rows are in.
        const std::vector<LogTransaction> records = store.chain(pv.ours, pv.base + 1);
        if (!records.empty()) {
            std::vector<int64_t> seqs;
            for (const auto& t : records)
                seqs.push_back(t.seq);
            store.anchorVersions(seqs, pv.base);
            store.removeTransactions(seqs);
        }
        store.forwardBranch(mine.id, pv.theirs, own);
        if (theirs.target == mine.id) {
            theirs.fromSeq = pv.theirs;
            store.updateBranch(theirs);
        }
        else if (mine.target == theirs.id) {
            mine.fromSeq = pv.theirs;
            store.updateBranch(mine);
        }
        // The document arrives as a switch does (sec 26.4): moved along the
        // rows with nothing recorded, its steps the rows of the chain.
        clearUndos();
        _clearRedos();
        log->moveHead(pv.theirs);
        _followHead(pv.base);
        const bool compute = imported();
        for (const auto& t : records)
            log->reappend(t);
        _arriveOnBranch();
        result.seq = pv.theirs;
        result.forwarded = pv.forward.size();
        if (compute) {
            std::set<std::string> failedBefore;
            for (auto obj : getObjects()) {
                if (obj->isError())
                    failedBefore.insert(obj->getNameInDocument());
            }
            try {
                recompute();
            }
            catch (Base::Exception& e) {
                FC_ERR("merge of " << branch << ", recompute: " << e.what());
            }
            for (auto obj : getObjects()) {
                if (obj->isError() && !failedBefore.count(obj->getNameInDocument()))
                    result.failed.emplace_back(obj->getNameInDocument());
            }
        }
        nameMerged();
        Document* holder = log->holderOf(theirs.id);
        if (holder && holder != this) {
            holder->refreshVersionNames();
            holder->signalBranchesChanged(*holder);
        }
        refreshVersionNames();
        signalBranchesChanged(*this);
        return result;
    }
    if (pv.changes.empty())
        return result;

    // A side for every conflict, or nothing moves (sec 28.6 Q3). A view
    // conflict keeps ours unless picked (Q2).
    std::map<std::string, std::string> side;
    for (const auto& c : pv.changes) {
        if (c.kind != "conflict" && c.kind != "view")
            continue;
        auto it = picks.find(c.key);
        const std::string s = it != picks.end() ? it->second
                            : c.kind == "view" ? std::string("ours") : fallback;
        if (s.empty())
            result.unresolved.push_back(c);
        else
            side[c.key] = s;
    }
    if (!result.unresolved.empty())
        return result;

    // What goes in: objects to make and to remove, dynamic properties to
    // add and to drop, values, and the owners of the derived values left to
    // the recompute.
    using Key = NetChange::Key;
    struct Made
    {
        long cid;
        std::string name;
        std::string type;
    };
    std::vector<Made> creates;
    std::vector<long> removes;
    std::map<Key, std::pair<std::string, std::string>> addprops;   // ptype, meta
    std::vector<Key> delprops;
    std::map<Key, std::string> sets;
    std::set<long> touch;
    auto take = [&](const Key& key, const NetChange::Val& v) {
        if (v.derived) {
            if (std::get<0>(key) == "obj")
                touch.insert(std::get<1>(key));
            return;
        }
        if (!v.atEnd) {
            if (v.atStart)
                delprops.push_back(key);
            return;
        }
        if (!v.meta.empty())
            addprops[key] = {v.ptype, v.meta};
        if (!v.after.empty())
            sets[key] = v.after;
    };
    for (const auto& c : pv.changes) {
        if (c.kind == "derived") {
            touch.insert(c.cid);
            continue;
        }
        const bool conflict = c.kind == "conflict" || c.kind == "view";
        if (c.kind != "take" && !(conflict && side[c.key] == "theirs"))
            continue;
        if (c.op == "create") {
            const NetChange::Obj& o = plan.theirs.objects[c.cid];
            creates.push_back({c.cid, o.cname, o.ctype});
            for (const auto& key : plan.theirs.valueOrder) {
                if (std::get<0>(key) != "doc" && std::get<1>(key) == c.cid)
                    take(key, plan.theirs.values[key]);
            }
        }
        else if (c.op == "remove") {
            removes.push_back(c.cid);
        }
        else if (c.op == "revive") {
            // Back as ours' removal recorded it -- each value's before is
            // its value at the base -- with what theirs did to it on top.
            const NetChange::Obj& o = plan.ours.objects[c.cid];
            creates.push_back({c.cid, o.cname, o.ctype});
            for (const auto& key : plan.ours.valueOrder) {
                if (std::get<0>(key) == "doc" || std::get<1>(key) != c.cid)
                    continue;
                const NetChange::Val& mine = plan.ours.values[key];
                if (!mine.atStart || plan.theirs.values.count(key))
                    continue;
                NetChange::Val was;
                was.atEnd = true;
                was.after = mine.before;
                was.ptype = mine.ptype;
                was.meta = mine.meta;
                was.derived = mine.derived;
                take(key, was);
            }
            for (const auto& key : plan.theirs.valueOrder) {
                if (std::get<0>(key) == "doc" || std::get<1>(key) != c.cid)
                    continue;
                NetChange::Val v = plan.theirs.values[key];
                auto mine = plan.ours.values.find(key);
                if (v.meta.empty() && mine != plan.ours.values.end())
                    v.meta = mine->second.meta;
                take(key, v);
            }
        }
        else {
            take(Key {c.ckind, c.cid, c.prop}, plan.theirs.values[Key {c.ckind, c.cid, c.prop}]);
        }
    }

    // Every value there, before anything moves.
    std::map<Key, CapturedValue> want;
    for (const auto& kv : sets) {
        CapturedValue v;
        if (!log->readValue(kv.second, v))
            THROWM(Base::RuntimeError, "cannot merge branch '" + branch + "': the value of "
                                           + std::get<2>(kv.first) + " is not in the log");
        want.emplace(kv.first, std::move(v));
    }

    std::set<std::string> failedBefore;
    std::map<long, TouchedState> before;
    for (auto obj : getObjects()) {
        before.emplace(obj->getID(), touchedStateOf(*obj));
        if (obj->isError())
            failedBefore.insert(obj->getNameInDocument());
    }

    _clearMyRedos();
    d->activeUndoTransaction = new Transaction(0);
    d->activeUndoTransaction->Name = "Merge " + branch;
    d->activeUndoTransaction->LogKind = "merge";
    d->activeUndoTransaction->MergeFrom = pv.theirs;
    mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;

    // Ours unchanged since the base: the document moved to theirs' state
    // through the rows, derived values and touched state with it, and no
    // recompute (sec 28.6 Q1). View state is left to the rule below.
    const bool moved = pv.fastForward && _moveAlongLog(pv.ours, pv.theirs, false);

    std::vector<std::string> relabelled;
    auto guarded = [&](const char* what, const std::string& name, const std::function<void()>& fn) {
        try {
            fn();
        }
        catch (Base::Exception& e) {
            FC_ERR("merge of " << branch << ", " << what << " " << name << ": " << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("merge of " << branch << ", " << what << " " << name << ": " << e.what());
        }
    };
    auto container = [&](const Key& key) -> PropertyContainer* {
        LogOp o;
        o.ckind = std::get<0>(key);
        o.cid = std::get<1>(key);
        return opContainer(*this, o);
    };
    auto skipped = [&](const Key& key) { return moved && std::get<0>(key) != "view"; };
    if (!moved) {
        for (const auto& made : creates) {
            if (getObjectByID(made.cid))
                continue;
            guarded("create", made.name, [&]() {
                if (getObject(made.name.c_str()))
                    throw Base::RuntimeError("name taken by another object");
                Base::Type type = Base::Type::getTypeIfDerivedFrom(
                    made.type.c_str(), DocumentObject::getClassTypeId(), true);
                auto obj = type.isBad() ? nullptr
                                        : static_cast<DocumentObject*>(type.createInstance());
                if (!obj)
                    throw Base::RuntimeError("cannot create " + made.type);
                obj->_Id = made.cid;
                addObject(obj, made.name.c_str(), false);
            });
        }
    }
    for (const auto& kv : addprops) {
        if (skipped(kv.first))
            continue;
        guarded("add property", std::get<2>(kv.first), [&]() {
            auto c = container(kv.first);
            if (!c || c->getPropertyByName(std::get<2>(kv.first).c_str()))
                return;
            addLoggedProperty(*c, kv.second.first, std::get<2>(kv.first), kv.second.second);
        });
    }
    for (const auto& key : delprops) {
        if (skipped(key))
            continue;
        guarded("remove property", std::get<2>(key), [&]() {
            auto c = container(key);
            if (c && c->getPropertyByName(std::get<2>(key).c_str()))
                c->removeDynamicProperty(std::get<2>(key).c_str());
        });
    }
    {
        CaptureConfig config(*this);
        RestoreBatch batch;
        for (auto& kv : want) {
            if (skipped(kv.first))
                continue;
            guarded("value of", std::get<2>(kv.first), [&]() {
                auto c = container(kv.first);
                if (!c)
                    return;   // a view with no Gui
                Property* prop = c->getPropertyByName(std::get<2>(kv.first).c_str());
                if (!prop)
                    throw Base::RuntimeError("no such property");
                // Only what differs is written, as a move along the log
                // does: a write touches its object, and view properties
                // that are one value under several names -- a colour, its
                // appearance, its material -- undo each other when all are
                // written back.
                const CapturedValue now = captureValue(config, *prop);
                const CapturedValue& v = kv.second;
                if (now.ok && now.fragment == v.fragment
                        && now.attachments.size() == v.attachments.size()
                        && std::equal(now.attachments.begin(), now.attachments.end(),
                                      v.attachments.begin(), [](const auto& x, const auto& y) {
                                          return x.name == y.name && x.bytes == y.bytes;
                                      }))
                    return;
                log->restoreBlobsOf(sets[kv.first]);
                restoreValue(*prop, kv.second);
                // A label a live object of ours has comes in suffixed, by
                // the property's own rule (sec 27.41 Q4 (a)).
                auto obj = Base::freecad_dynamic_cast<DocumentObject>(c);
                if (obj && prop == &obj->Label
                        && captureValue(config, *prop).fragment != kv.second.fragment)
                    relabelled.emplace_back(obj->getNameInDocument());
            });
        }
        batch.finish();
    }
    if (!moved) {
        for (long cid : removes) {
            if (auto obj = getObjectByID(cid)) {
                const std::string name = obj->getNameInDocument();
                guarded("remove", name, [&]() { removeObject(name.c_str()); });
            }
        }
        // Derived values are not merged (sec 17.3): the merged definition
        // is recomputed, inside the merge's own step (sec 24.1). Not when
        // nothing of theirs went in: ours is what it was.
        if (!d->activeUndoTransaction->isEmpty()) {
            for (long cid : touch) {
                if (auto obj = getObjectByID(cid))
                    obj->touch();
            }
            guarded("recompute", getName(), [&]() { recompute(); });
        }
    }
    else if (imported()) {
        guarded("recompute", getName(), [&]() { recompute(); });
    }
    for (auto obj : getObjects()) {
        if (obj->isError() && !failedBefore.count(obj->getNameInDocument()))
            result.failed.emplace_back(obj->getNameInDocument());
    }

    // The row's annotation: what was merged and how each conflict went,
    // and the touched state the merge left (sec 27.63), which is what an
    // undo of it puts back.
    std::map<long, TouchedState> after;
    for (auto obj : getObjects()) {
        auto state = touchedStateOf(*obj);
        auto it = before.find(obj->getID());
        if (it == before.end() || !(it->second == state)
                || d->activeUndoTransaction->hasObject(obj))
            after.emplace(obj->getID(), std::move(state));
    }
    nlohmann::json j;
    const std::string touchedJson = touchedRecord(before, after);
    if (!touchedJson.empty())
        j = nlohmann::json::parse(touchedJson, nullptr, false);
    if (!j.is_object())
        j = nlohmann::json::object();
    nlohmann::json m;
    m["branch"] = branch;
    m["from"] = pv.theirs;
    m["base"] = pv.base;
    m["fast_forward"] = moved;
    std::map<std::string, int> counts;
    auto conflicts = nlohmann::json::array();
    for (const auto& c : pv.changes) {
        ++counts[c.kind];
        if (c.kind == "conflict" || c.kind == "view")
            conflicts.push_back({{"key", c.key}, {"kind", c.kind}, {"side", side[c.key]}});
    }
    for (const auto& kv : counts)
        m[kv.first] = kv.second;
    if (!conflicts.empty())
        m["sides"] = std::move(conflicts);
    if (!relabelled.empty())
        m["relabelled"] = relabelled;
    if (!result.failed.empty())
        m["failed"] = result.failed;
    j["merge"] = std::move(m);
    const std::string script = j.dump();

    const std::string name = d->activeUndoTransaction->Name;
    const int64_t headBefore = log->head();
    if (d->activeUndoTransaction->isEmpty()) {
        // Nothing of theirs to write -- ours has it all, or kept its own in
        // every conflict: a record, so the base moves and nothing is asked
        // twice.
        mUndoMap.erase(d->activeUndoTransaction->getID());
        delete d->activeUndoTransaction;
        d->activeUndoTransaction = nullptr;
        result.seq = log->record("merge", name, script, pv.theirs);
    }
    else {
        d->activeUndoTransaction->LogScript = script;
        _commitTransaction(false);
        for (const auto& t : log->store().chain(log->head(), headBefore + 1)) {
            if (t.kind == "merge" && t.mergeFrom == pv.theirs)
                result.seq = t.seq;
        }
    }
    nameMerged();
    signalBranchesChanged(*this);
    return result;
}

namespace {

/// The copy of a file an import reads (docs/TransactionLog.md sec 30.13):
/// its history, opened without its document, and the branch asked for.
/// A file with no history has none of that, and `saved` -- what the file
/// says of the save it is from (sec 30.19).
struct ForkSource
{
    std::shared_ptr<FileHistory> history;
    TransactionLogCore* core {nullptr};
    std::string file;      ///< the file's name, as branches and users show it
    int64_t current {0};   ///< the branch its file reopens on
    FileHistory::Saved saved;
};

ForkSource openFork(const std::shared_ptr<FileHistory>& mine, const std::string& path)
{
    ForkSource fork;
    std::string reason;
    fork.file = Base::FileInfo(path).fileNamePure();
    if (!FileHistory::savedAs(path, fork.saved, &reason))
        THROWM(Base::RuntimeError, "cannot read '" + path + "': " + reason);
    if (!mine->path().empty()
            && FileHistory::canonicalPath(path) == FileHistory::canonicalPath(mine->path()))
        THROWM(Base::ValueError, "'" + path + "' is this document's own file");
    // No history: a state, and the save it names (sec 30.19).
    if (!fork.saved.history)
        return fork;
    fork.history = FileHistory::openFile(path, &reason);
    if (!fork.history)
        THROWM(Base::RuntimeError, "cannot read the history of '" + path + "': " + reason);
    if (fork.history == mine)
        THROWM(Base::ValueError, "'" + path + "' is this document's own file");
    fork.core = &TransactionLogCore::of(*fork.history);
    // What a document of the copy has done is in rows before they are read.
    for (Document* doc : fork.core->documents()) {
        doc->commitImplicitTransaction();
        if (auto log = doc->getTransactionLog())
            log->resolvePending();
    }
    // Every string its values name, in its table.
    fork.core->loadStrings();
    int64_t head = 0;
    fork.current = fork.core->metaBranch(head);
    return fork;
}

/// How many rows of `rows` hold an operation that is not a derived value.
size_t operationsIn(TransactionStore& store, const std::vector<LogTransaction>& rows)
{
    size_t n = 0;
    for (const auto& t : rows) {
        for (const auto& o : store.ops(t.seq)) {
            if (o.op != "set" || !o.derived) {
                ++n;
                break;
            }
        }
    }
    return n;
}

/// The key of the store's meta an import branch keeps its maps under.
std::string importKey(int64_t branch)
{
    return "import:" + std::to_string(branch);
}

/// The maps an import branch kept (sec 30.15): the copy's object ids and
/// names to the ones here.
void importMaps(const nlohmann::json& kept, std::map<long, long>& ids,
                std::map<std::string, std::string>& names)
{
    if (!kept.is_object())
        return;
    if (kept.contains("ids") && kept["ids"].is_array()) {
        for (const auto& e : kept["ids"]) {
            if (e.is_array() && e.size() == 2 && e[0].is_number_integer()
                    && e[1].is_number_integer())
                ids[e[0].get<long>()] = e[1].get<long>();
        }
    }
    if (kept.contains("names") && kept["names"].is_object()) {
        for (auto it = kept["names"].begin(); it != kept["names"].end(); ++it) {
            if (it.value().is_string())
                names[it.key()] = it.value().get<std::string>();
        }
    }
}

/// The import branch that holds a file as found (sec 30.19 G5): the one
/// made for `file` and `branch` of it whose maps name a state. False when
/// there is none; `kept` is its meta.
bool stateBranch(TransactionStore& store, const std::string& file, const std::string& branch,
                 LogBranch& mine, nlohmann::json& kept)
{
    for (const auto& b : store.branches()) {
        const std::string meta = store.getMeta(importKey(b.id));
        if (meta.empty())
            continue;
        auto was = nlohmann::json::parse(meta, nullptr, false);
        if (!was.is_object() || !was.contains("state")
                || was.value("file", std::string()) != file
                || was.value("branch", std::string()) != branch)
            continue;
        mine = b;
        kept = std::move(was);
        return true;
    }
    return false;
}

/// A copy whose history is there but whose file is not its tip (sec 16.6,
/// 30.19 G6): `closed` the branch that was the file's when it was edited
/// elsewhere, `found` the file as found -- the first version of the branch
/// the file reopens on. False for any other copy.
bool closedPart(TransactionStore& theirs, int64_t current, LogBranch& closed, LogVersion& found)
{
    const std::string was = theirs.getMeta("closed_branch");
    if (was.empty())
        return false;
    int64_t id = 0;
    std::istringstream(was) >> id;
    if (!id || id == current || !theirs.getBranch(id, closed))
        return false;
    bool have = false;
    for (const auto& v : theirs.versions()) {
        if (v.branch == current && (!have || v.num < found.num)) {
            found = v;
            have = true;
        }
    }
    return have;
}

} // namespace

std::vector<Document::ForkBranch> Document::forkBranches(const std::string& path)
{
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "no transaction log");
    getFileHistory();
    ForkSource fork = openFork(d->history, path);
    auto& store = log->store();
    TransactionLogCore& core = TransactionLogCore::of(*d->history);
    std::vector<ForkBranch> out;
    LogBranch held;
    nlohmann::json kept;
    if (!fork.history) {
        // One thing to bring, the file as it is, when the save it names is
        // one of this history's and the state is not here already.
        ForkBranch f;
        f.current = true;
        int64_t version = 0;
        if (TransactionLog::savedAt(store, fork.saved.saveId, version, f.base)) {
            bool reached = false;
            for (const auto& t : store.transactions(f.base, 1))
                reached = t.seq == f.base;
            if (!reached)
                f.base = 0;
        }
        f.independent = f.base == 0;
        const bool here = stateBranch(store, fork.file, std::string(), held, kept)
            && kept.value("state", std::string()) == fork.saved.hash;
        f.ahead = here ? 0 : 1;
        out.push_back(std::move(f));
        return out;
    }
    auto& theirs = fork.core->store();
    for (const auto& b : theirs.branches()) {
        ForkBranch f;
        f.name = b.name;
        f.current = b.id == fork.current;
        f.closed = b.closed > 0;
        const int64_t base = core.forkBase(theirs, b.head, f.base);
        if (base) {
            f.ahead = operationsIn(theirs, theirs.chain(b.head, base + 1));
        }
        else if (f.current) {
            // G6: what was the file's, the file as found, and what it has
            // done since.
            LogBranch closed;
            LogVersion found;
            if (closedPart(theirs, fork.current, closed, found)) {
                const int64_t shared = core.forkBase(theirs, closed.head, f.base);
                if (shared) {
                    const bool here = stateBranch(store, fork.file, b.name, held, kept)
                        && kept.value("state", std::string()) == found.docxml_hash;
                    f.ahead = operationsIn(theirs, theirs.chain(closed.head, shared + 1))
                        + operationsIn(theirs, theirs.chain(b.head)) + (here ? 0 : 1);
                }
            }
            if (!f.base) {
                // No row both hold: the file as it is, from nothing.
                f.independent = true;
                const bool here = stateBranch(store, fork.file, std::string(), held, kept)
                    && kept.value("state", std::string()) == fork.saved.hash;
                f.ahead = here ? 0 : 1;
            }
        }
        out.push_back(std::move(f));
    }
    return out;
}

Document* Document::_importReplay(int64_t base, const std::string& stem, LogBranch& mine,
                                  bool& scratch)
{
    // docs/TransactionLog.md sec 30.15 step 4: the document the rows are
    // replayed in -- the one holding the branch, or one of its own, at the
    // row both hold, closed when the import is done.
    TransactionLog* log = getTransactionLog();
    auto& store = log->store();
    TransactionLogCore& core = TransactionLogCore::of(*d->history);
    auto& app = GetApplication();
    Document* active = app.getActiveDocument();
    Document* replay = nullptr;
    scratch = false;
    if (mine.id) {
        replay = core.holderOf(mine.id);
        scratch = !replay;
        if (!replay)
            replay = openFileBranch(d->history, mine.name, 0, false);
        else
            replay->_checkBranchable("import a file's history");
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        return replay;
    }
    std::string name = stem;
    LogBranch taken;
    for (int i = 2; store.findBranch(name, taken); ++i)
        name = stem + "~" + std::to_string(i);
    if (base == 0) {
        // Sec 30.22: an independent branch hangs off no row. Its document
        // is an empty one of this file -- its history, its string table,
        // its blob store -- on a branch made from nothing.
        const std::string docName = app.getUniqueDocumentName(
            (std::string(getName()) + "_import").c_str());
        replay = app.newDocument(docName.c_str(), docName.c_str(), false);
        if (!replay)
            THROWM(Base::RuntimeError, "cannot make the import's document");
        scratch = true;
        try {
            replay->setStatus(VersionDoc, true);
            replay->d->noLog = true;
            replay->_joinHistory(d->history);
            if (d->history->hasher())
                replay->d->Hasher = d->history->hasher();
            replay->setUndoMode(getUndoMode());
            LogBranch made;
            made.name = name;
            made.target = log->branch();
            made.created = std::chrono::duration<double>(
                               std::chrono::system_clock::now().time_since_epoch()).count();
            log->flush();
            store.addBranch(made);
            replay->d->noLog = false;
            LogVersion nowhere;
            replay->d->transactionLog = std::make_unique<TransactionLog>(*replay, &nowhere);
            replay->d->transactionLog->setBranch(made.id);
            replay->d->undoFloor = replay->d->transactionLog->lastSeq();
            store.getBranch(made.id, mine);
        }
        catch (...) {
            app.closeDocument(replay->getName());
            if (active && app.getActiveDocument() != active)
                app.setActiveDocument(active);
            throw;
        }
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        return replay;
    }
    LogTransaction at;
    for (const auto& t : store.transactions(base, 1))
        at = t;
    const std::set<int64_t> onChain = chainPoints(store, base);
    LogVersion anchor;
    bool haveAnchor = false;
    for (const auto& v : store.versions()) {
        if (!onChain.count(v.seq))
            continue;
        if (!haveAnchor || v.seq > anchor.seq || (v.seq == anchor.seq && v.num > anchor.num)) {
            anchor = v;
            haveAnchor = true;
        }
    }
    if (!haveAnchor)
        THROWM(Base::RuntimeError, "no version to reach the row both files hold from");
    replay = _openVersionDocument(d->history, anchor, false, this, false);
    scratch = true;
    try {
        TransactionLog* rlog = replay->getTransactionLog();
        if (anchor.seq != base) {
            Base::FlagToggler<> replaying(replay->d->replaying);
            if (!replay->_moveAlongLog(anchor.seq, base, false))
                THROWM(Base::RuntimeError, "cannot reach the row both files hold");
        }
        rlog->moveHead(base);
        rlog->forgetLiveValues();
        rlog->forkHere(at.branch ? at.branch : log->branch(), name);
        replay->refreshVersionNames();
        store.getBranch(rlog->branch(), mine);
    }
    catch (...) {
        app.closeDocument(replay->getName());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        throw;
    }
    if (active && app.getActiveDocument() != active)
        app.setActiveDocument(active);
    return replay;
}

void Document::_finishImport(Document* replay, bool scratch, int64_t branch,
                             const std::string& file, const std::string& record,
                             const std::string& keep, ImportResult& result)
{
    // The import's record, and the maps a second import continues from.
    auto& app = GetApplication();
    Document* active = app.getActiveDocument() == replay ? this : app.getActiveDocument();
    TransactionLog* rlog = replay->getTransactionLog();
    result.seq = rlog->record("import", "Import " + file, record);
    rlog->flush();
    getTransactionLog()->store().setMeta(importKey(branch), keep);
    if (scratch) {
        // The tip left as a version, so the branch opens without a replay.
        try {
            replay->_leaveBranch();
        }
        catch (Base::Exception& e) {
            FC_WARN("import of " << file << ": " << e.what());
        }
        app.closeDocument(replay->getName());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
    }
    signalBranchesChanged(*this);
}

void Document::_applyForeignState(Document& from, std::map<long, long>& ids,
                                  std::map<std::string, std::string>& names,
                                  std::map<std::string, std::string>& renamed,
                                  const Document* kin)
{
    // docs/TransactionLog.md sec 30.19 G3, G7, G8: this document made what
    // `from` -- a document of another copy of the file -- is, recorded
    // into the open transaction. As _applyVersion() does for a version of
    // this file, but nothing here may be taken for the same by its number
    // alone: an object of theirs is the one here with its id only when its
    // type and its name say so too, and any other is the file's own, made
    // under an id and a name of this file. Values come through the maps and
    // out of the other table, as an imported row's do. A failure throws:
    // the caller rolls the whole row back.
    auto here = [&](long id) {
        auto it = ids.find(id);
        return it == ids.end() ? id : it->second;
    };
    std::vector<std::pair<DocumentObject*, DocumentObject*>> pairs;   // theirs, the one here
    std::set<long> kept;
    for (auto theirs : from.getObjects()) {
        const std::string name = theirs->getNameInDocument();
        DocumentObject* mine = getObjectByID(here(theirs->getID()));
        const std::string expect = names.count(name) ? names[name] : name;
        if (mine && (mine->getTypeId() != theirs->getTypeId() || kept.count(mine->getID())
                     || expect != mine->getNameInDocument()))
            mine = nullptr;
        if (!mine) {
            auto obj = static_cast<DocumentObject*>(theirs->getTypeId().createInstance());
            if (!obj)
                throw Base::RuntimeError(std::string("cannot create ")
                                         + theirs->getTypeId().getName());
            // On a branch from nothing (sec 30.22) there is no object to be
            // the same as, but the file may still be this file's kin: an
            // object whose id this file gave that very name is that object
            // -- unless `kin`, the document asked, has it as another type
            // -- and comes under both, as an object does on any branch of
            // its file. That is what lets a merge see the two as one.
            if (kin && !ids.count(theirs->getID()) && !names.count(name)) {
                const std::string* known = d->history->objectNameOfId(theirs->getID());
                const DocumentObject* same = kin->getObjectByID(theirs->getID());
                if (known && *known == name && !getObjectByID(theirs->getID())
                        && !getObject(name.c_str())
                        && (!same || same->getTypeId() == theirs->getTypeId()))
                    obj->_Id = theirs->getID();
            }
            addObject(obj, name.c_str(), false);
            if (obj->getID() != theirs->getID())
                ids[theirs->getID()] = obj->getID();
            const std::string made = obj->getNameInDocument();
            if (made != name) {
                names[name] = made;
                renamed[name] = made;
            }
            else {
                names.erase(name);
            }
            mine = obj;
        }
        kept.insert(mine->getID());
        pairs.emplace_back(theirs, mine);
    }

    CaptureConfig config(*this);
    CaptureConfig theirConfig(from);
    auto& manager = getFileBlobManager();
    const auto& fromManager = from.getFileBlobManager();
    std::vector<FileBlobHandle> held;
    // What the document is, not which file it is, when it was saved, or
    // what it carries of a log: none of that is the file's to change here,
    // by having it otherwise or by not having it.
    auto ofTheFile = [](const std::string& name) {
        return keptOnRestore(name.c_str()) || name == "Label" || name == "Uid";
    };
    auto container = [&](PropertyContainer& live, const PropertyContainer& other, bool isDocument,
                         bool values) {
        std::map<std::string, Property*> want, have;
        other.getPropertyMap(want);
        live.getPropertyMap(have);
        if (!values) {
            for (auto& kv : have) {
                if (isDocument && ofTheFile(kv.first))
                    continue;
                if (!want.count(kv.first) && !live.getDynamicPropertyData(kv.second).name.empty())
                    live.removeDynamicProperty(kv.first.c_str());
            }
        }
        for (auto& kv : want) {
            const short type = other.getPropertyType(kv.second);
            if ((type & Prop_Transient) || (type & Prop_NoPersist))
                continue;
            if (isDocument && ofTheFile(kv.first))
                continue;
            Property* prop = live.getPropertyByName(kv.first.c_str());
            if (!values) {
                if (prop)
                    continue;
                auto dyn = other.getDynamicPropertyData(kv.second);
                if (dyn.name.empty())
                    continue;
                prop = live.addDynamicProperty(kv.second->getTypeId().getName(), kv.first.c_str(),
                                               dyn.group.c_str(), dyn.getDoc(), dyn.attr,
                                               dyn.readonly, dyn.hidden);
                continue;
            }
            if (!prop)
                continue;
            CapturedValue theirs = captureValue(theirConfig, *kv.second);
            if (!theirs.ok)
                throw Base::RuntimeError("cannot read the value of " + kv.first);
            const CapturedValue now = captureValue(config, *prop);
            if (now.ok && now.fragment == theirs.fragment
                    && now.attachments.size() == theirs.attachments.size()
                    && std::equal(now.attachments.begin(), now.attachments.end(),
                                  theirs.attachments.begin(), [](const auto& a, const auto& b) {
                                      return a.name == b.name && a.bytes == b.bytes;
                                  }))
                continue;
            for (const auto& blob : theirs.blobs)
                copyBlob(manager, fromManager, blob, held);
            if (auto referrer = dynamic_cast<const BlobReferrerProperty*>(kv.second))
                copyBlob(manager, fromManager, referrer->contentBlob(), held);
            restoreValue(*prop, theirs);
        }
    };
    {
        RestoreNames through(names);
        StringHasher::ImportTags tags(ids);
        RestoreStrings strings(from.getStringHasher(), getStringHasher());
        for (bool values : {false, true}) {
            RestoreBatch batch;
            container(*this, from, true, values);
            for (auto& pair : pairs) {
                container(*pair.second, *pair.first, false, values);
                auto live = viewOf(pair.second);
                auto other = viewOf(pair.first);
                if (live && other)
                    container(*live, *other, false, values);
            }
            batch.finish();
        }
    }
    // What the file no longer has.
    std::vector<std::string> gone;
    for (auto obj : getObjects()) {
        if (!kept.count(obj->getID()))
            gone.emplace_back(obj->getNameInDocument());
    }
    for (const auto& name : gone) {
        if (getObject(name.c_str()))
            removeObject(name.c_str());
    }
}

bool Document::_importStateRow(Document& from, const std::string& file,
                               std::map<long, long>& ids,
                               std::map<std::string, std::string>& names, ImportResult& result,
                               const Document* kin)
{
    // docs/TransactionLog.md sec 30.19: one row, the difference between
    // this document and the file as found; one author, the file (G4).
    TransactionLog* rlog = getTransactionLog();
    const std::string who = from.LastModifiedBy.getValue();
    LogUser user;
    user.kind = Actor::kindName(Actor::Fork);
    user.name = who.empty() ? "(" + file + ")" : who + " (" + file + ")";
    LogSession session;
    session.uuid = Base::Uuid::createUuid();
    session.opened = std::chrono::duration<double>(
                         std::chrono::system_clock::now().time_since_epoch()).count();
    session.closed = session.opened;
    const int64_t here = rlog->importSession(session, user, "{}", file);
    Actor actor;
    actor.kind = Actor::Fork;
    actor.name = user.name;

    _clearMyRedos();
    {
        ActorScope as(actor);
        d->activeUndoTransaction = new Transaction(0);
    }
    Transaction* txn = d->activeUndoTransaction;
    txn->Name = "As found: " + file;
    mUndoMap[txn->getID()] = txn;
    const auto idsBefore = ids;
    const auto namesBefore = names;
    std::map<std::string, std::string> renamed;
    std::string why;
    bool failed = false;
    try {
        _applyForeignState(from, ids, names, renamed, kin);
    }
    catch (Base::Exception& e) {
        why = e.what();
        failed = true;
    }
    catch (std::exception& e) {
        why = e.what();
        failed = true;
    }
    if (failed) {
        // F8, for a state: nothing of it comes.
        _abortTransaction();
        ids = idsBefore;
        names = namesBefore;
        result.stoppedAt = -1;
        result.reason = "the file as found: " + why;
        FC_WARN("import of " << file << " stopped at " << result.reason);
        return false;
    }
    if (txn->isEmpty()) {
        mUndoMap.erase(txn->getID());
        delete txn;
        d->activeUndoTransaction = nullptr;
        ++result.skipped;
        return true;
    }
    nlohmann::json j;
    j["imported"] = {{"file", file}, {"state", true}};
    if (!renamed.empty())
        j["renamed"] = renamed;
    txn->LogScript = j.dump();
    TransactionLog::Stamp stamp;
    stamp.session = here;
    rlog->setStamp(&stamp);
    try {
        _commitTransaction(false);
    }
    catch (...) {
        rlog->setStamp(nullptr);
        throw;
    }
    rlog->setStamp(nullptr);
    if (stamp.seq) {
        ++result.rows;
        for (const auto& kv : renamed)
            result.renamed[kv.first] = kv.second;
    }
    else {
        ++result.skipped;
    }
    return true;
}

Document::ImportResult Document::_importState(const std::string& path, const std::string& file,
                                              const std::string& saveId, const std::string& hash)
{
    // docs/TransactionLog.md sec 30.19: a file with no history. Its
    // `Version` names the save it is from (G1); the state at that save's
    // row is the base, and the file as found comes as one row on a branch
    // from there -- or, brought before (G5), as one more on the branch it
    // made.
    TransactionLog* log = getTransactionLog();
    auto& store = log->store();
    ImportResult result;
    int64_t version = 0;
    // No save of this history named, or one its rows no longer reach: the
    // file is not refused (sec 30.22). It comes as an independent branch,
    // from nothing.
    if (TransactionLog::savedAt(store, saveId, version, result.base)) {
        bool reached = false;
        for (const auto& t : store.transactions(result.base, 1))
            reached = t.seq == result.base;
        if (!reached)
            result.base = 0;
    }
    result.independent = result.base == 0;
    LogBranch mine;
    nlohmann::json kept;
    if (stateBranch(store, file, std::string(), mine, kept)
            && kept.value("state", std::string()) == hash) {
        result.reason = "nothing new";
        return result;
    }
    result.extended = mine.id != 0;
    // Brought before: one more row on the branch it made, whatever that
    // branch hangs off.
    if (mine.id)
        result.independent = kept.value("independent", false);
    std::map<long, long> ids;
    std::map<std::string, std::string> names;
    importMaps(kept, ids, names);

    // The file as a document: the one open, or one opened for this.
    auto& app = GetApplication();
    Document* active = app.getActiveDocument();
    const std::string canonical = FileHistory::canonicalPath(path);
    Document* theirs = nullptr;
    for (auto doc : app.getDocuments()) {
        if (doc != this && !doc->testStatus(VersionDoc) && doc->FileName.getStrValue().size()
                && FileHistory::canonicalPath(doc->FileName.getStrValue()) == canonical)
            theirs = doc;
    }
    const bool opened = !theirs;
    if (theirs)
        theirs->commitImplicitTransaction();
    else
        theirs = app.openDocument(path.c_str(), false);
    if (!theirs || theirs == this)
        THROWM(Base::RuntimeError, "cannot open '" + path + "'");
    auto closeTheirs = [&]() {
        if (opened)
            app.closeDocument(theirs->getName());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
    };
    Document* replay = nullptr;
    bool scratch = false;
    try {
        replay = _importReplay(result.base, file, mine, scratch);
        result.branch = mine.name;
        replay->_importStateRow(*theirs, file, ids, names, result,
                                result.independent ? this : nullptr);
    }
    catch (...) {
        if (replay && scratch)
            app.closeDocument(replay->getName());
        closeTheirs();
        throw;
    }
    closeTheirs();

    nlohmann::json j;
    nlohmann::json m;
    m["file"] = file;
    m["state"] = hash;
    m["base"] = result.base;
    m["rows"] = result.rows;
    if (result.independent)
        m["independent"] = true;
    if (result.stoppedAt) {
        m["stopped_at"] = result.stoppedAt;
        m["reason"] = result.reason;
    }
    if (!result.renamed.empty())
        m["renamed"] = result.renamed;
    j["import"] = std::move(m);
    nlohmann::json keep;
    keep["file"] = file;
    keep["path"] = canonical;
    keep["branch"] = std::string();
    if (result.independent)
        keep["independent"] = true;
    // A state that did not come is asked for again.
    keep["state"] = result.stoppedAt ? kept.value("state", std::string()) : hash;
    auto list = nlohmann::json::array();
    for (const auto& kv : ids)
        list.push_back({kv.first, kv.second});
    keep["ids"] = std::move(list);
    keep["names"] = names;
    _finishImport(replay, scratch, mine.id, file, j.dump(), keep.dump(), result);
    return result;
}

Document::ImportResult Document::importFork(const std::string& path, const std::string& branch)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 30.13, 30.14: replay, always (F1).
    checkNotFrozen("import a file's history");
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "no transaction log");
    _checkBranchable("import a file's history");
    getFileHistory();
    log->resolvePending();
    auto& store = log->store();
    TransactionLogCore& core = TransactionLogCore::of(*d->history);

    ForkSource fork = openFork(d->history, path);
    if (!fork.history) {
        // No history: the file as it is, against the save it names.
        if (!branch.empty())
            THROWM(Base::ValueError, "'" + path + "' carries no history, and so no branch '"
                                         + branch + "'");
        return _importState(path, fork.file, fork.saved.saveId, fork.saved.hash);
    }
    auto& theirs = fork.core->store();
    // F6: the branch asked for, else the one the copy's file reopens on.
    LogBranch from;
    if (branch.empty() ? !theirs.getBranch(fork.current, from) : !theirs.findBranch(branch, from))
        THROWM(Base::ValueError, "'" + path + "' has no branch '" + branch + "'");

    ImportResult result;
    result.from = from.name;
    // The branch the file reopens on, asked for by name or not, is the file.
    const bool other = from.id != fork.current;
    int64_t base = core.forkBase(theirs, from.head, result.base);
    // G6: the branch the file reopens on has no ancestry when the file was
    // edited where there is no log. What it was made from is the branch
    // closed then; the edit is the file as found; and the branch's own rows
    // are what it has done since.
    LogBranch tail;
    LogVersion found;
    if (!base && from.id == fork.current) {
        LogBranch closed;
        if (closedPart(theirs, fork.current, closed, found)) {
            tail = from;
            from = closed;
            base = core.forkBase(theirs, from.head, result.base);
        }
    }
    const bool gap = tail.id != 0;
    if (!base) {
        // Another branch of the copy is that branch or nothing. The file
        // itself is not refused (sec 30.22): what it is now comes as an
        // independent branch.
        if (other)
            THROWM(Base::ValueError, "branch '" + result.from + "' of '" + path
                                         + "' shares no history with this file");
        return _importState(path, fork.file, std::string(), fork.saved.hash);
    }
    const auto rows = theirs.chain(from.head, base + 1);
    std::vector<LogTransaction> tailRows;
    if (gap)
        tailRows = theirs.chain(tail.head);

    // F7: the branch an earlier import of this copy made, when the row
    // both hold is the last thing that moved on it -- found by the row's
    // own identity (F3) -- and else a new one. A file as found is known by
    // what it is, not by a row (G5): the branch that holds one is the one.
    LogBranch mine;
    nlohmann::json kept;
    if (gap) {
        stateBranch(store, fork.file, result.from, mine, kept);
        if (mine.id && kept.value("state", std::string()) == found.docxml_hash
                && !operationsIn(theirs, rows) && !operationsIn(theirs, tailRows)) {
            result.reason = "nothing new";
            return result;
        }
    }
    else {
        if (!operationsIn(theirs, rows)) {
            result.reason = "nothing new";
            return result;
        }
        for (const auto& b : store.branches()) {
            const std::string meta = store.getMeta(importKey(b.id));
            if (meta.empty())
                continue;
            // Of this file's same branch: another branch of the copy is
            // another branch here (sec 30.14).
            const auto was = nlohmann::json::parse(meta, nullptr, false);
            if (!was.is_object() || was.value("file", std::string()) != fork.file
                    || was.value("branch", std::string()) != from.name)
                continue;
            const auto after = store.chain(b.head, result.base);
            if (after.empty() || after.front().seq != result.base)
                continue;
            bool moved = false;
            for (const auto& t : after)
                moved = moved || (t.seq != result.base && !store.ops(t.seq).empty());
            if (moved)
                continue;
            mine = b;
            kept = was;
            break;
        }
    }
    result.extended = mine.id != 0;

    // The maps (F1): the copy's object ids and names to the ones here.
    // What both had at the base is the same in both, and is not in them.
    std::map<long, long> ids;
    std::map<std::string, std::string> names;
    importMaps(kept, ids, names);

    // Named after the file, and after the copy's branch when that is not
    // its file's own (30.14).
    auto& app = GetApplication();
    Document* active = app.getActiveDocument();
    std::string name = fork.file;
    if (!gap && from.id != fork.current)
        name += "@" + from.name;
    bool scratch = false;
    Document* replay = _importReplay(result.base, name, mine, scratch);
    result.branch = mine.name;
    TransactionLog* rlog = replay->getTransactionLog();

    // Whose rows they are (F3, F4).
    std::map<int64_t, LogSession> sessions;
    for (const auto& s : theirs.sessions())
        sessions[s.id] = s;
    std::map<int64_t, LogUser> users;
    for (const auto& u : theirs.users())
        users[u.id] = u;
    std::map<int64_t, int64_t> sessionHere;
    std::map<int64_t, std::shared_ptr<const Actor>> authors;
    auto author = [&](int64_t session) {
        auto found = sessionHere.find(session);
        if (found != sessionHere.end())
            return found->second;
        const LogSession& s = sessions[session];
        const LogUser& u = users[s.user];
        const int64_t here =
            rlog->importSession(s, u, theirs.environmentJson(s.env), fork.file);
        sessionHere[session] = here;
        Actor actor;
        if (!Actor::kindFromName(u.kind, actor.kind) || actor.kind == Actor::Local) {
            actor.kind = Actor::Fork;
            actor.name = (u.name.empty() ? std::string("host") : u.name) + " (" + fork.file + ")";
        }
        else {
            actor.name = u.name;
        }
        actor.access = s.access;
        authors[session] = std::make_shared<const Actor>(std::move(actor));
        return here;
    };

    using Key = std::tuple<std::string, long, std::string>;   // ckind, cid here, prop
    // The copy's blobs a value names, made files of this file's store,
    // with the files each reads. The handles go into `held`: a blob nobody
    // holds is gone at once, and the value restored next finds it by hash.
    std::function<void(const std::string&, const std::string&, std::vector<FileBlobHandle>&, int)>
        bringBlob = [&](const std::string& hash, const std::string& ext,
                        std::vector<FileBlobHandle>& held, int depth) {
            if (depth > 1024)
                return;
            LogEntity e;
            if (theirs.getEntity(hash, e)) {
                for (const auto& r : e.refs) {
                    if (r.role == "blob")
                        bringBlob(r.target, r.name, held, depth + 1);
                }
            }
            FileBlobHandle mine = core.liveBlob(hash);
            if (!mine) {
                fork.core->flush();
                fork.core->restoreBlob(hash, ext);
                FileBlobHandle blob = fork.core->liveBlob(hash);
                std::string bytes;
                if (!blob || !blob->read(bytes))
                    throw Base::RuntimeError(
                        "a file the value names is not in the copy's history");
                mine = d->history->blobs().adoptBytes(bytes, ext.empty() ? nullptr : ext.c_str());
            }
            if (mine)
                held.push_back(std::move(mine));
        };

    std::map<int64_t, int64_t> seqs;   // the copy's rows, to the rows here
    // Dynamic properties left out for having no type, by object and name.
    std::set<std::pair<long, std::string>> untyped;
    auto replayRows = [&](const std::vector<LogTransaction>& list) {
        // F5: the copy's named versions past the base come as versions of the
        // branch, each taken when the replay has reached the row it is at.
        std::multimap<int64_t, std::string> named;
        {
            std::set<int64_t> past;
            for (const auto& t : list)
                past.insert(t.seq);
            for (const auto& v : theirs.versions()) {
                if (v.kind == "named" && past.count(v.seq))
                    named.emplace(v.seq, v.name);
            }
        }
        auto versionsAt = [&](int64_t seq) {
            const auto range = named.equal_range(seq);
            for (auto it = range.first; it != range.second; ++it) {
                const int64_t num = replay->snapshotToLog();
                if (!num)
                    continue;
                rlog->flush();
                store.nameVersion(num, it->second);
                ++result.versions;
            }
    };
        for (const auto& t : list) {
            const auto ops = theirs.ops(t.seq);
            if (ops.empty()) {
                ++result.skipped;   // a record: a save, a snapshot, a recompute
                versionsAt(t.seq);
                continue;
            }
            const int64_t session = author(t.session);
            std::map<std::string, std::string> renamed;
            std::string why;
            try {
                // Everything read, and every object's type known, before
                // anything moves.
                std::set<long> removed;
                for (const auto& o : ops) {
                    if (o.op == "remove" && o.ckind == "obj")
                        removed.insert(o.cid);
                }
                std::vector<std::pair<const LogOp*, CapturedValue>> values;
                std::vector<FileBlobHandle> held;
                for (const auto& o : ops) {
                    if (o.op == "create" && o.ckind == "obj") {
                        if (Base::Type::getTypeIfDerivedFrom(o.ctype.c_str(),
                                                             DocumentObject::getClassTypeId(), true)
                                .isBad())
                            throw Base::RuntimeError("no object type " + o.ctype
                                                     + " in this build");
                    }
                    if (o.op != "set" || o.derived || removed.count(o.cid))
                        continue;
                    if (o.vafter.empty())
                        throw Base::RuntimeError("the value of " + o.prop
                                                 + " never reached its log");
                    CapturedValue v;
                    if (!fork.core->readValue(o.vafter, v))
                        throw Base::RuntimeError("the value of " + o.prop + " is not in its log");
                    LogEntity e;
                    if (theirs.getEntity(o.vafter, e)) {
                        for (const auto& r : e.refs) {
                            if (r.role == "blob")
                                bringBlob(r.target, r.name, held, 0);
                        }
                    }
                    values.emplace_back(&o, std::move(v));
                }

                // The row, as a transaction of its author's. Only the
                // transaction is: what the commit records beside it -- a
                // snapshot -- is this process's.
                replay->_clearMyRedos();
                {
                    ActorScope as(authors[t.session]);
                    replay->d->activeUndoTransaction = new Transaction(0);
                }
                Transaction* txn = replay->d->activeUndoTransaction;
                txn->Name = t.name;
                txn->Origin = t.origin;
                if (t.kind != "user" && t.kind != "implicit")
                    txn->LogKind = t.kind == "merge" ? std::string("user") : t.kind;
                txn->Implicit = t.kind == "implicit";
                if (t.inverts) {
                    auto it = seqs.find(t.inverts);
                    LogRowId id;
                    if (it != seqs.end())
                        txn->Inverts = it->second;
                    else if (theirs.rowId(t.inverts, id))
                        txn->Inverts = store.findRow(id);
                }
                replay->mUndoMap[txn->getID()] = txn;
                try {
                    auto here = [&](long cid) {
                        auto it = ids.find(cid);
                        return it == ids.end() ? cid : it->second;
                    };
                    auto container = [&](const LogOp& o) -> PropertyContainer* {
                        LogOp mapped;
                        mapped.ckind = o.ckind;
                        mapped.cid = here(o.cid);
                        return opContainer(*replay, mapped);
                    };
                    // 1. What it made, under ids of this file, and under new
                    // names where its own are taken (sec 30.4 P4).
                    for (const auto& o : ops) {
                        if (o.op != "create" || o.ckind != "obj")
                            continue;
                        Base::Type type = Base::Type::getTypeIfDerivedFrom(
                            o.ctype.c_str(), DocumentObject::getClassTypeId(), true);
                        auto obj = static_cast<DocumentObject*>(type.createInstance());
                        if (!obj)
                            throw Base::RuntimeError("cannot create " + o.ctype);
                        replay->addObject(obj, o.cname.c_str(), false);
                        ids[o.cid] = obj->getID();
                        const std::string name = obj->getNameInDocument();
                        if (name != o.cname) {
                            names[o.cname] = name;
                            renamed[o.cname] = name;
                        }
                        else {
                            names.erase(o.cname);
                        }
                    }
                    // 2. Dynamic properties.
                    for (const auto& o : ops) {
                        if (o.op != "addprop" && o.op != "delprop")
                            continue;
                        auto c = container(o);
                        if (!c) {
                            if (o.ckind == "view")
                                continue;   // no Gui
                            throw Base::RuntimeError("no object for property " + o.prop);
                        }
                        const bool has = c->getPropertyByName(o.prop.c_str()) != nullptr;
                        if (o.op == "addprop" && !has) {
                            // A row written before the log named the type of
                            // a property added to an object that was there
                            // (sec 30.18) says `BadType`: nothing can be made
                            // of it. Most were a module's cache -- Part's
                            // shape cache -- and nothing of the document's.
                            if (o.ptype.find("::") == std::string::npos)
                                untyped.emplace(here(o.cid), o.prop);
                            else
                                addLoggedProperty(*c, o.ptype, o.prop, o.meta);
                        }
                        else if (o.op == "delprop" && has) {
                            c->removeDynamicProperty(o.prop.c_str());
                        }
                    }
                    // 3. The values, read through the names, and their
                    // strings out of the copy's table (sec 30.16).
                    {
                        RestoreNames through(names);
                        // An element name says which object made it by its
                        // id: the copy's, mapped like the ops'.
                        StringHasher::ImportTags tags(ids);
                        RestoreStrings strings(fork.history->hasher(), replay->getStringHasher());
                        RestoreBatch batch;
                        for (const auto& kv : values) {
                            const LogOp& o = *kv.first;
                            auto c = container(o);
                            if (!c) {
                                if (o.ckind == "view")
                                    continue;   // no Gui
                                throw Base::RuntimeError("no object for the value of " + o.prop);
                            }
                            Property* prop = c->getPropertyByName(o.prop.c_str());
                            if (!prop) {
                                if (untyped.count({here(o.cid), o.prop}))
                                    continue;
                                // A property of the object's type the copy's
                                // build had and this one has not.
                                throw Base::RuntimeError("no property " + o.prop);
                            }
                            restoreValue(*prop, kv.second);
                        }
                        batch.finish();
                    }
                    // 4. Derived values are left out (F2): their owners touched.
                    for (const auto& o : ops) {
                        if (o.op != "set" || !o.derived || o.ckind != "obj" || removed.count(o.cid))
                            continue;
                        if (auto obj = replay->getObjectByID(here(o.cid)))
                            obj->touch();
                    }
                    // 5. What it removed.
                    for (const auto& o : ops) {
                        if (o.op != "remove" || o.ckind != "obj")
                            continue;
                        if (auto obj = replay->getObjectByID(here(o.cid)))
                            replay->removeObject(obj->getNameInDocument());
                    }
                }
                catch (...) {
                    replay->_abortTransaction();
                    for (const auto& kv : renamed)
                        names.erase(kv.first);
                    for (const auto& o : ops) {
                        if (o.op == "create" && o.ckind == "obj")
                            ids.erase(o.cid);
                    }
                    throw;
                }

                if (txn->isEmpty()) {
                    // Nothing of it changes anything here.
                    replay->mUndoMap.erase(txn->getID());
                    delete txn;
                    replay->d->activeUndoTransaction = nullptr;
                    ++result.skipped;
                    versionsAt(t.seq);
                    continue;
                }
                nlohmann::json j;
                j["imported"] = {{"file", fork.file}, {"seq", t.seq}};
                if (!renamed.empty())
                    j["renamed"] = renamed;
                txn->LogScript = j.dump();
                TransactionLog::Stamp stamp;
                stamp.session = session;
                stamp.ordinal = t.ordinal;
                stamp.time = t.time;
                rlog->setStamp(&stamp);
                try {
                    replay->_commitTransaction(false);
                }
                catch (...) {
                    rlog->setStamp(nullptr);
                    throw;
                }
                rlog->setStamp(nullptr);
                if (stamp.seq) {
                    seqs[t.seq] = stamp.seq;
                    ++result.rows;
                    for (const auto& kv : renamed)
                        result.renamed[kv.first] = kv.second;
                }
                else {
                    ++result.skipped;
                }
                versionsAt(t.seq);
                continue;
            }
            catch (Base::Exception& e) {
                why = e.what();
            }
            catch (std::exception& e) {
                why = e.what();
            }
            // F8: the import ends here, the rows before it kept.
            result.stoppedAt = t.seq;
            result.reason = "row " + std::to_string(t.seq) + " (" + t.name + "): " + why;
            FC_WARN("import of " << path << " stopped at " << result.reason);
            return false;
        }
        return true;
    };

    bool whole = replayRows(rows);
    std::string state = kept.is_object() ? kept.value("state", std::string()) : std::string();
    if (whole && gap) {
        // The file as found, read as a document of the copy's history.
        Document* asFound = fork.core->documentAt(found);
        const bool opened = !asFound;
        try {
            if (opened)
                asFound = Document::openFileVersion(fork.history, found.num, false);
            whole = replay->_importStateRow(*asFound, fork.file, ids, names, result);
        }
        catch (...) {
            if (opened && asFound)
                app.closeDocument(asFound->getName());
            if (scratch)
                app.closeDocument(replay->getName());
            if (active && app.getActiveDocument() != active)
                app.setActiveDocument(active);
            throw;
        }
        if (opened)
            app.closeDocument(asFound->getName());
        if (active && app.getActiveDocument() != active)
            app.setActiveDocument(active);
        if (whole) {
            state = found.docxml_hash;
            whole = replayRows(tailRows);
        }
    }

    nlohmann::json j;
    nlohmann::json m;
    m["file"] = fork.file;
    m["branch"] = result.from;
    m["base"] = result.base;
    m["rows"] = result.rows;
    if (gap)
        m["state"] = state;
    if (result.stoppedAt) {
        m["stopped_at"] = result.stoppedAt;
        m["reason"] = result.reason;
    }
    if (!result.renamed.empty())
        m["renamed"] = result.renamed;
    if (result.versions)
        m["versions"] = result.versions;
    j["import"] = std::move(m);
    nlohmann::json keep;
    keep["file"] = fork.file;
    keep["path"] = FileHistory::canonicalPath(path);
    keep["branch"] = result.from;
    if (gap)
        keep["state"] = state;
    auto list = nlohmann::json::array();
    for (const auto& kv : ids)
        list.push_back({kv.first, kv.second});
    keep["ids"] = std::move(list);
    keep["names"] = names;
    _finishImport(replay, scratch, mine.id, fork.file, j.dump(), keep.dump(), result);
    return result;
}

void Document::_followHead(int64_t from)
{
    // As a switch arrives at a head (sec 26.2 item 4): in place, nothing
    // recorded, and what the log knew of the live values forgotten.
    _checkoutHead(from);
    getTransactionLog()->forgetLiveValues();
}

Document* Document::openNewBranch(const std::string& name, bool createView)
{
    OperationScope scope;   // sec 27.38
    // docs/TransactionLog.md sec 30.3 S.a: a branch made at this
    // document's head and opened in a second document of the file, this
    // one staying where it is. Neither follows the other: one is merged
    // into the other when that is asked for.
    checkNotFrozen("open a branch");
    TransactionLog* log = getTransactionLog();
    if (!log)
        THROWM(Base::RuntimeError, "no transaction log");
    _checkBranchable("open a branch");
    if (log->detached())
        THROWM(Base::RuntimeError, "the document is a version with no branch of its own yet");
    auto& store = log->store();
    LogBranch mine;
    store.getBranch(log->branch(), mine);
    std::string branchName = name;
    LogBranch taken;
    if (branchName.empty()) {
        for (int i = 1; branchName.empty() || store.findBranch(branchName, taken); ++i)
            branchName = mine.name + "~" + std::to_string(i);
    }
    else if (store.findBranch(branchName, taken)) {
        THROWM(Base::ValueError, "branch name '" + branchName + "' is taken");
    }

    // The branch's document is opened as the version at this branch's
    // head, which the tip snapshot makes when there is none.
    _leaveBranch();
    const std::set<int64_t> onChain = chainPoints(store, log->head());
    LogVersion tip;
    bool haveTip = false;
    for (const auto& v : store.versions()) {
        if (!onChain.count(v.seq))
            continue;
        if (!haveTip || v.seq > tip.seq || (v.seq == tip.seq && v.num > tip.num)) {
            tip = v;
            haveTip = true;
        }
    }
    if (!haveTip)
        THROWM(Base::RuntimeError, "no version to open a branch from");
    getFileHistory();
    Document* other = _openVersionDocument(d->history, tip, createView, this, false);
    TransactionLog* olog = other->getTransactionLog();
    // At this branch's head, the records after the version included.
    olog->moveHead(log->head());
    olog->forkHere(mine.id, branchName);
    other->refreshVersionNames();
    signalBranchesChanged(*this);
    return other;
}

Document::BranchState Document::branchState()
{
    BranchState state;
    TransactionLog* log = getTransactionLog();
    if (!log || log->detached())
        return state;
    auto& store = log->store();
    LogBranch mine;
    if (!store.getBranch(log->branch(), mine))
        return state;
    state.branch = mine.name;
    LogBranch target;
    if (!mine.target || !store.getBranch(mine.target, target))
        return state;
    state.target = target.name;
    // What each has that the other's history does not reach, in the rows
    // stored by now; records -- a save, a snapshot -- are not operations.
    const auto here = store.history(log->head());
    const auto there = store.history(target.head);
    std::set<int64_t> hereSeqs;
    std::set<int64_t> thereSeqs;
    for (const auto& t : here)
        hereSeqs.insert(t.seq);
    for (const auto& t : there)
        thereSeqs.insert(t.seq);
    for (const auto& t : here) {
        if (!thereSeqs.count(t.seq) && !store.ops(t.seq).empty())
            ++state.ahead;
    }
    for (const auto& t : there) {
        if (!hereSeqs.count(t.seq) && !store.ops(t.seq).empty())
            ++state.behind;
    }
    return state;
}

void Document::noteVersionTaken()
{
    d->commitsSinceVersion = 0;
    d->lastVersionTime = std::chrono::duration<double>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace {

/** The archive members no reader taps, for version 1 (docs/TransactionLog.md
 * sec 27.46): a file written before schema 5 keeps its shapes and other
 * attachments at the top of the archive, and without them the version's
 * Document.xml names files it does not carry. Everything but the entries
 * already in `entries`, the blob store's members -- the version holds
 * those as blobs -- and the thumbnails.
 */
void addUntappedMembers(const std::string& path,
                        std::vector<std::pair<std::string, std::string>>& entries)
{
    std::unique_ptr<Base::ZipFileReader> zip;
    try {
        zip = std::make_unique<Base::ZipFileReader>(path);
    }
    catch (...) {
        return;   // not an archive: a directory restore, which taps its files
    }
    const auto start = std::chrono::steady_clock::now();
    std::size_t count = 0, size = 0;
    std::set<std::string> have;
    for (const auto& e : entries)
        have.insert(e.first);
    const std::string blobs = FileBlobManager::archivePrefix();
    for (const auto& name : zip->entryNames()) {
        // The string table is the file's, which the log keeps once (sec
        // 27.50 item 2), not a version's.
        if (name.empty() || name.back() == '/' || have.count(name)
                || name == Document::stringTableName()
                || boost::starts_with(name, blobs) || boost::starts_with(name, "thumbnails/"))
            continue;
        auto in = zip->openEntry(name);
        if (!in)
            continue;
        std::string bytes((std::istreambuf_iterator<char>(*in)), std::istreambuf_iterator<char>());
        ++count;
        size += bytes.size();
        entries.emplace_back(name, std::move(bytes));
    }
    if (count)
        FC_LOG("version 1 of " << path << ": " << count << " untapped members, " << size
               << " bytes, read in "
               << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
               << " s");
}

} // namespace

void Document::endRestoreTap(Base::XMLReader& reader)
{
    if (!d->restoreTapped)
        return;
    if (auto breader = reader.getReader())
        breader->endTap();
}

bool Document::afterRestore(bool checkPartial) {
    Base::FlagToggler<> flag(globalIsRestoring, false);
    if(!afterRestore(d->objectArray,checkPartial)) {
        FC_WARN("Reload partial document " << getName());
        GetApplication().signalPendingReloadDocument(*this);
        return false;
    }
    setStatus(Document::Restoring, false);
    GetApplication().signalFinishRestoreDocument(*this);
    // The last point a referrer can turn up: the Gui document replays its
    // embedded view documents from this signal. Whatever is still unclaimed
    // belongs to nothing and goes. Deliberately not reached on the partial
    // reload path above, which still has a restore ahead of it.
    getFileBlobManager().endRestore();
    // The version of a file in an older format (sec 27.62): the document as
    // it was read, view providers included, at the current schema. Undo
    // reaches back to it and no further, as to the version of any open.
    if (d->openVersionPending) {
        d->openVersionPending = false;
        if (_snapshotToLog("restore")) {
            if (auto log = getTransactionLog())
                d->undoFloor = log->lastSeq();
        }
    }
    return true;
}

bool Document::afterRestore(const std::vector<DocumentObject *> &objArray, bool checkPartial)
{
    checkPartial = checkPartial && testStatus(Document::PartialDoc);
    if(checkPartial && !d->touchedObjs.empty())
        return false;

    // some link type property cannot restore link information until other
    // objects has been restored. For example, PropertyExpressionEngine and
    // PropertySheet with expression containing label reference. So we add the
    // Property::afterRestore() interface to let them sort it out. Note, this
    // API is not called in object dedpenency order, because the order
    // information is not ready yet.
    std::map<DocumentObject*, std::vector<App::Property*> > propMap;
    for(auto obj : objArray) {
        auto &props = propMap[obj];
        obj->getPropertyList(props);
        for(auto prop : props) {
            try {
                prop->afterRestore();
            } catch (const Base::Exception& e) {
                FC_ERR("Failed to restore " << obj->getFullName()
                        << '.' << prop->getName() << ": " << e.what());
                d->addRecomputeLog(e.what(), obj);
            }
        }
    }

    if(checkPartial && !d->touchedObjs.empty()) {
        // partial document touched, signal full reload
        return false;
    }

    std::set<DocumentObject*> objSet(objArray.begin(),objArray.end());
    auto objs = getDependencyList(objArray.empty()?d->objectArray:objArray,DepSort);
    for (auto obj : objs) {
        if(objSet.find(obj)==objSet.end())
            continue;
        try {
            for(auto prop : propMap[obj])
                prop->onContainerRestored();
            bool touched = false;
            auto returnCode = obj->ExpressionEngine.execute(
                    PropertyExpressionEngine::ExecuteOnRestore,&touched);
            if(returnCode!=DocumentObject::StdReturn) {
                FC_ERR("Expression engine failed to restore " << obj->getFullName() << ": " << returnCode->Why);
                d->addRecomputeLog(returnCode);
            }
            obj->onDocumentRestored();
            if(touched)
                d->touchedObjs.insert(obj);
        }
        catch (const Base::Exception& e) {
            d->addRecomputeLog(e.what(),obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << e.what());
        }
        catch (std::exception &e) {
            d->addRecomputeLog(e.what(),obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << e.what());
        }
        catch (...) {
            d->addRecomputeLog("Unknown exception on restore",obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << "unknown exception");
        }
        if(obj->isValid()) {
            auto &props = propMap[obj];
            props.clear();
            // refresh properties in case the object changes its property list
            obj->getPropertyList(props);
            for(auto prop : props) {
                auto link = Base::freecad_dynamic_cast<PropertyLinkBase>(prop);
                int res;
                std::string errMsg;
                if(link && (res=link->checkRestore(&errMsg))) {
                    d->touchedObjs.insert(obj);
                    if(res==1 || checkPartial) {
                        FC_WARN(obj->getFullName() << '.' << prop->getName() << ": " << errMsg);
                        setStatus(Document::LinkStampChanged, true);
                        if(checkPartial)
                            return false;
                    } else {
                        FC_ERR(obj->getFullName() << '.' << prop->getName() << ": " << errMsg);
                        d->addRecomputeLog(errMsg,obj);
                        setStatus(Document::PartialRestore, true);
                    }
                }
            }
        }

        if(checkPartial && !d->touchedObjs.empty()) {
            // partial document touched, signal full reload
            return false;
        } else if(!obj->isError() && !d->touchedObjs.count(obj))
            obj->purgeTouched();

        signalFinishRestoreObject(*obj);
    }

    d->touchedObjs.clear();
    return true;
}

bool Document::isSaved() const
{
    std::string name = FileName.getValue();
    return !name.empty();
}

/** Label is the visible name of a document shown e.g. in the windows title
 * or in the tree view. The label almost (but not always e.g. if you manually change it)
 * matches with the file name where the document is stored to.
 * In contrast to Label the method getName() returns the internal name of the document that only
 * matches with Label when loading or creating a document because then both are set to the same value.
 * Since the internal name cannot be changed during runtime it must differ from the Label after saving
 * the document the first time or saving it under a new file name.
 * @ note More than one document can have the same label name.
 * @ note The internal is always guaranteed to be unique because @ref Application::newDocument() checks
 * for a document with the same name and makes it unique if needed. Hence you cannot rely on that the
 * internal name matches with the name you passed to Application::newDoument(). You should use the
 * method getName() instead.
 */
const char* Document::getName() const
{
    // return GetApplication().getDocumentName(this);
    return myName.c_str();
}

std::string Document::getFullName(bool python) const {
    if(python) {
        std::ostringstream ss;
        ss << "FreeCAD.getDocument('" << myName << "')";
        return ss.str();
    }
    return myName;
}

App::Document *Document::getOwnerDocument() const {
    return const_cast<App::Document*>(this);
}

const char* Document::getProgramVersion() const
{
    return d->programVersion.c_str();
}

const std::vector<long>& Document::getWritableSchemaVersions()
{
    // Only what the writer can actually produce. Older versions the reader
    // still accepts are deliberately absent: offering to write a shape we
    // cannot build would fail silently at the worst moment.
    //
    // 4 is upstream's format and the only one anything but this fork reads:
    // one archive entry per PropertyFileIncluded, every container writing its
    // whole property set, a <Document> root.
    //
    // 5 is this fork's format, and it is one version because it is one
    // decision. An included file becomes one archive entry per distinct
    // content, shared by every property referring to it; a class may state
    // its defaults once and every container of that class be written as the
    // difference -- view providers in the view file, objects in Document.xml;
    // a document may carry one shared shape store. None of that is readable
    // anywhere else, so the root element becomes <FCDocument> and every
    // reader which cannot put the elided halves back refuses the file
    // outright instead of silently reverting them to its own defaults. The
    // blob half shipped first under a number of its own, but it was never
    // any more readable than the rest -- an older reader takes its hash
    // attributes for nothing and drops the content without a word -- so
    // there was never a middle version to keep, only one that lied.
    static const std::vector<long> versions {4, FC_DOC_SCHEMA_VER};
    return versions;
}

long Document::getCurrentSchemaVersion()
{
    return getWritableSchemaVersions().back();
}

long Document::getSaveSchemaVersion() const
{
    const long requested = SaveSchemaVersion.getValue();
    const auto &versions = getWritableSchemaVersions();
    if (std::find(versions.begin(), versions.end(), requested) != versions.end()) {
        return requested;
    }

    // Refuse quietly-wrong output: a version we cannot write must not be
    // approximated by writing a different one and calling it that.
    FC_WARN("Document " << getName() << ": cannot write schema version "
            << requested << ", using " << getCurrentSchemaVersion());
    return getCurrentSchemaVersion();
}

long Document::resolveSchemaVersion(const Base::Writer &writer) const
{
    (void)writer;
    // Nothing to resolve away any more. A split save writes what its cap
    // says: it shares no default block -- every object is its own file, and
    // a block would tie each of those files to a document-wide record, so
    // that gaining a third object of some class rewrites every file of that
    // class, which is exactly what a directory layout exists to avoid -- but
    // the rest of the format is the format, and skipping one half of it is
    // no reason to write the file under a version it is not.
    return getSaveSchemaVersion();
}

TransactionLog* Document::getTransactionLog() const
{
    // Mode 0 costs one bool test per commit. The log is made lazily so a
    // document opened before the preference was set still gets one on
    // its next commit -- and so the transient directory exists by then.
    if (!d->transactionLog) {
        if (DocumentParams::getTransactionLog() == 0 || d->noLog)
            return nullptr;
        if (TransientDir.getStrValue().empty())
            return nullptr;
        try {
            d->transactionLog = std::make_unique<TransactionLog>(*const_cast<Document*>(this));
            // The time rule of the cadence counts from the log's start, so a
            // document never saved or opened gets versions too (sec 25.3).
            if (d->lastVersionTime <= 0)
                const_cast<Document*>(this)->noteVersionTaken();
            d->transactionLog->noteIdentity();
        }
        catch (Base::Exception& e) {
            FC_ERR("cannot open the transaction log of " << getName() << ": " << e.what());
            return nullptr;
        }
    }
    return d->transactionLog.get();
}

FileBlobManager& Document::getFileBlobManager() const
{
    return getFileHistory().blobs();
}

FileHistory& Document::getFileHistory() const
{
    // Created on demand rather than in the constructor: the history lives in
    // TransientDir, which is not set up yet at that point. A file another
    // document of this process has open already has its history, which
    // this one joins: one log per file (docs/TransactionLog.md sec 27.5).
    if (!d->history) {
        if (!testStatus(VersionDoc) && !FileName.getStrValue().empty()) {
            if (auto existing = FileHistory::find(FileName.getStrValue())) {
                d->history = existing;
                d->joinedHistory = true;
                _noteObjectsInHistory();
                return *d->history;
            }
        }
        d->history = FileHistory::create(*const_cast<Document*>(this));
        d->history->setPath(FileName.getStrValue());
        _noteObjectsInHistory();
    }
    return *d->history;
}

void Document::_noteObjectsInHistory() const
{
    // The file's counter and name table (sec 27.40 items 1, 3) take in what
    // the document made before it had its history, and its string hasher is
    // the file's (item 2) unless it hashed already.
    d->history->noteObjectId(d->lastObjectId);
    d->Hasher = d->history->shareHasher(d->Hasher);
    for (auto obj : d->objectArray)
        d->history->noteObjectName(obj->getNameInDocument(), obj->getID());
    for (const auto& g : d->lastGeoIds)
        d->history->noteGeoId(g.first, g.second);
    d->lastGeoIds.clear();
}

void Document::collectFileBlobs(const std::vector<App::DocumentObject*>& objs) const
{
    auto &manager = getFileBlobManager();

    auto collect = [&manager](const PropertyContainer *container) {
        if (!container) {
            return;
        }
        std::vector<Property*> props;
        container->getPropertyList(props);
        for (auto prop : props) {
            // Any property that stores its value as blob content, not just
            // the one that did when this pass was written: a cast to a
            // concrete class is a list of what gets saved, and a property
            // left off it loses its content silently.
            if (auto owner = dynamic_cast<BlobReferrerProperty*>(prop)) {
                owner->collectBlobs(manager, nullptr);
            }
        }
    };

    collect(this);
    // Staging the blob content is a phase of its own, and on a large document
    // the longest one before anything is written: 21 of a 31 second save on
    // the 18142-object reference, during which the bar used to sit still. It
    // does not restate the total -- one pass per object is already part of
    // the three the save budgeted for, and restating here once shrank the
    // total to this phase alone and left the bar pinned at full for the rest.
    for (auto obj : objs.empty() ? d->objectArray : objs) {
        if (d->saveSeq) {
            d->saveSeq->next();
        }
        collect(obj);
    }

    // The view tier answers for itself: its properties are written later than
    // the content is, and a view's own properties are not reachable from here
    // at all.
    signalCollectFiles(manager, objs);
}

const char* Document::getFileName() const
{
    return testStatus(TempDoc) ? TransientDir.getValue()
                               : FileName.getValue();
}

/// Remove all modifications. After this call The document becomes valid again.
void Document::purgeTouched()
{
    for (auto It : d->objectArray)
        It->purgeTouched();
}

bool Document::isTouched() const
{
    for (auto It : d->objectArray) {
        if (It->isTouched()) {
            return true;
        }
    }
    return false;
}

vector<DocumentObject*> Document::getTouched() const
{
    vector<DocumentObject*> result;

    for (auto It : d->objectArray) {
        if (It->isTouched()) {
            result.push_back(It);
        }
    }

    return result;
}

void Document::setClosable(bool c)
{
    setStatus(Document::Closable, c);
}

bool Document::isClosable() const
{
    return testStatus(Document::Closable);
}

int Document::countObjects() const
{
   return static_cast<int>(d->objectArray.size());
}

void Document::getLinksTo(std::set<DocumentObject*> &links,
        const DocumentObject *obj, int options, int maxCount,
        const std::vector<DocumentObject*> &objs) const
{
    std::map<const App::DocumentObject*, std::vector<App::DocumentObject*> > linkMap;

    for(auto o : !objs.empty() ? objs : d->objectArray) {
        if (o == obj)
            continue;
        auto linked = o;
        if (options & GetLinkArrayElement) {
            linked = o->getLinkedObject(false);
        }
        else {
            auto ext = o->getExtensionByType<LinkBaseExtension>(true);
            if(ext)
                linked = ext->getTrueLinkedObject(false,nullptr,0,true);
            else
                linked = o->getLinkedObject(false);
        }

        if(linked && linked!=o) {
            if(options & GetLinkRecursive)
                linkMap[linked].push_back(o);
            else if(linked == obj || !obj) {
                if((options & GetLinkExternal)
                        && linked->getDocument()==o->getDocument())
                    continue;
                else if(options & GetLinkedObject)
                    links.insert(linked);
                else
                    links.insert(o);
                if(maxCount && maxCount<=(int)links.size())
                    return;
            }
        }
    }

    if(!(options & GetLinkRecursive))
        return;

    std::vector<const DocumentObject*> current(1,obj);
    for(int depth=0;!current.empty();++depth) {
        if(!GetApplication().checkLinkDepth(depth, MessageOption::Error))
            break;
        std::vector<const DocumentObject*> next;
        for(const App::DocumentObject *o : current) {
            auto iter = linkMap.find(o);
            if(iter==linkMap.end())
                continue;
            for (App::DocumentObject *link : iter->second) {
                if (links.insert(link).second) {
                    if(maxCount && maxCount<=(int)links.size())
                        return;
                    next.push_back(link);
                }
            }
        }
        current = std::move(next);
    }
    return;
}

bool Document::hasLinksTo(const DocumentObject *obj) const {
    std::set<DocumentObject *> links;
    getLinksTo(links,obj,0,1);
    return !links.empty();
}

std::vector<App::DocumentObject*> Document::getInList(const DocumentObject* me) const
{
    // result list
    std::vector<App::DocumentObject*> result;
    // go through all objects
    for (const auto & It : d->objectMap) {
        // get the outList and search if me is in that list
        std::vector<DocumentObject*> OutList = It.second->getOutList();
        for (auto obj : OutList) {
            if (obj && obj == me)
                // add the parent object
                result.push_back(It.second);
        }
    }
    return result;
}

// This function unifies the old _rebuildDependencyList() and
// getDependencyList().  The algorithm basically obtains the object dependency
// by recrusivly visiting the OutList of each object in the given object array.
// It makes sure to call getOutList() of each object once and only once, which
// makes it much more efficient than calling getRecursiveOutList() on each
// individual object.
//
// The problem with the original algorithm is that, it assumes the objects
// inside any OutList are all within the given object array, so it does not
// recursively call getOutList() on those dependent objects inside. This
// assumption is broken by the introduction of PropertyXLink which can link to
// external object.
//
static void _buildDependencyList(const std::vector<App::DocumentObject*> &objectArray,
        int options, std::vector<App::DocumentObject*> *depObjs,
        DependencyList *depList, std::map<DocumentObject*,Vertex> *objectMap,
        bool *touchCheck = nullptr)
{
    std::map<DocumentObject*, std::vector<DocumentObject*> > outLists;
    std::deque<DocumentObject*> objs;

    if(objectMap) objectMap->clear();
    if(depList) depList->clear();

    int op = (options & Document::DepNoXLinked)?DocumentObject::OutListNoXLinked:0;
    for (auto obj : objectArray) {
        objs.push_back(obj);
        while(!objs.empty()) {
            auto obj = objs.front();
            objs.pop_front();
            if(!obj || !obj->isAttachedToDocument())
                continue;

            auto it = outLists.find(obj);
            if(it!=outLists.end())
                continue;

            if(touchCheck) {
                if(obj->isTouched() || obj->mustExecute()) {
                    // early termination on touch check
                    *touchCheck = true;
                    return;
                }
            }
            if(depObjs) depObjs->push_back(obj);
            if(objectMap && depList)
                (*objectMap)[obj] = add_vertex(*depList);

            auto &outList = outLists[obj];
            outList = obj->getOutList(op);
            objs.insert(objs.end(),outList.begin(),outList.end());
        }
    }

    if(objectMap && depList) {
        for (const auto &v : outLists) {
            for(auto obj : v.second) {
                if(obj && obj->isAttachedToDocument())
                    add_edge((*objectMap)[v.first],(*objectMap)[obj],*depList);
            }
        }
    }
}

std::vector<App::DocumentObject*> Document::getDependencyList(
    const std::vector<App::DocumentObject*>& objectArray, int options)
{
    std::vector<App::DocumentObject*> ret;
    if(!(options & (DepSort | DepNoCycle))) {
        _buildDependencyList(objectArray,options,&ret,nullptr,nullptr);
        return ret;
    }

    DependencyList depList;
    std::map<DocumentObject*,Vertex> objectMap;
    std::map<Vertex,DocumentObject*> vertexMap;

    _buildDependencyList(objectArray,options,nullptr,&depList,&objectMap);

    for(auto &v : objectMap)
        vertexMap[v.second] = v.first;

    std::list<Vertex> make_order;
    try {
        boost::topological_sort(depList, std::front_inserter(make_order));
    } catch (const std::exception& e) {
        if(options & DepNoCycle) {
            // Use boost::strong_components to find cycles. It groups strongly
            // connected vertices as components, and therefore each component
            // forms a cycle.
            std::vector<int> c(vertexMap.size());
            std::map<int,std::vector<Vertex> > components;
            boost::strong_components(depList,boost::make_iterator_property_map(
                        c.begin(),boost::get(boost::vertex_index,depList),c[0]));
            for(size_t i=0;i<c.size();++i)
                components[c[i]].push_back(i);

            std::ostringstream ss;
            ss << "\nDependency cycles:";
            std::vector<Property*> props;
            std::vector<ObjectIdentifier> identifiers;
            auto findProperty = [&](DocumentObject *obj, DocumentObject *link) {
                props.clear();
                obj->getPropertyList(props);
                bool first = true;
                for (const auto *prop : props) {
                    if (!prop->getName() || prop->getContainer() != obj)
                        continue;
                    if (auto propLink = Base::freecad_dynamic_cast<PropertyLinkBase>(prop)) {
                        identifiers.clear();
                        propLink->getLinksTo(identifiers, link);
                        for (const auto &path : identifiers) {
                            ss << ", ";
                            if (first) {
                                first = false;
                                ss << "Property: ";
                            }
                            ss << path.canonicalPath().toString();
                        }
                    }
                }
            };
            for(auto &v : components) {
                if(v.second.size()==1) {
                    // For components with only one member, we still need to
                    // check if there is self looping.
                    auto it = vertexMap.find(v.second[0]);
                    if(it==vertexMap.end())
                        continue;
                    // Try search the object in its own out list
                    for(auto obj : it->second->getOutList()) {
                        if(obj == it->second) {
                            ss << '\n' << it->second->getFullName();
                            findProperty(obj, obj);
                            ss << '\n';
                            break;
                        }
                    }
                    continue;
                }
                // For components with more than one member, they form a loop together
                ss << '\n';
                DocumentObject *first = nullptr;
                DocumentObject *prev = nullptr;
                for(size_t i=0;i<v.second.size();++i) {
                    auto it = vertexMap.find(v.second[i]);
                    if(it==vertexMap.end())
                        continue;
                    if (prev) {
                        findProperty(prev, it->second);
                        ss << '\n';
                    } else
                        first = it->second;
                    ss << App::SubObjectT(it->second, "").getObjectFullName();
                    prev = it->second;
                }
                if (first != prev)
                    findProperty(prev, first);
                ss << '\n';
            }
            FC_ERR(ss.str());
            FC_THROWM(Base::RuntimeError,
                    "Cyclice dependency detected.\n"
                    "Please check Report View for more details.");
        }
        FC_ERR(e.what());
        ret = DocumentP::partialTopologicalSort(objectArray);
        std::reverse(ret.begin(),ret.end());
        return ret;
    }

    for (std::list<Vertex>::reverse_iterator i = make_order.rbegin();i != make_order.rend(); ++i)
        ret.push_back(vertexMap[*i]);
    return ret;
}

std::vector<App::Document*> Document::getDependentDocuments(bool sort) {
    return getDependentDocuments({this},sort);
}

std::vector<App::Document*> Document::getDependentDocuments(
        std::vector<App::Document*> pending, bool sort)
{
    DependencyList depList;
    std::map<Document*,Vertex> docMap;
    std::map<Vertex,Document*> vertexMap;

    std::vector<App::Document*> ret;
    if(pending.empty())
        return ret;

    auto outLists = PropertyXLink::getDocumentOutList();
    std::set<App::Document*> docs;
    docs.insert(pending.begin(),pending.end());
    if(sort) {
        for(auto doc : pending)
            docMap[doc] = add_vertex(depList);
    }
    while(!pending.empty()) {
        auto doc = pending.back();
        pending.pop_back();

        auto it = outLists.find(doc);
        if(it == outLists.end())
            continue;

        auto &vertex = docMap[doc];
        for(auto depDoc : it->second) {
            if(docs.insert(depDoc).second) {
                pending.push_back(depDoc);
                if(sort)
                    docMap[depDoc] = add_vertex(depList);
            }
            add_edge(vertex,docMap[depDoc],depList);
        }
    }

    if(!sort) {
        ret.insert(ret.end(),docs.begin(),docs.end());
        return ret;
    }

    for(auto &v : docMap)
        vertexMap[v.second] = v.first;

    std::list<Vertex> make_order;
    try {
        boost::topological_sort(depList, std::front_inserter(make_order));
    } catch (const std::exception& e) {
        // Use boost::strong_components to find cycles. It groups strongly
        // connected vertices as components, and therefore each component
        // forms a cycle.
        std::vector<int> c(vertexMap.size());
        std::map<int,std::vector<Vertex> > components;
        boost::strong_components(depList,boost::make_iterator_property_map(
                    c.begin(),boost::get(boost::vertex_index,depList),c[0]));
        for(size_t i=0;i<c.size();++i)
            components[c[i]].push_back(i);

        FC_ERR("Document dependency cycles: ");
        std::ostringstream ss;
        ss << '\n';
        for(auto &v : components) {
            if(v.second.size()<=1)
                continue;
            // For components with more than one member, they form a loop together
            for(size_t i=0;i<v.second.size();++i) {
                auto it = vertexMap.find(v.second[i]);
                if(it==vertexMap.end())
                    continue;
                if(i%6==0)
                    ss << '\n';
                ss << it->second->getName() << ", ";
            }
            ss << '\n';
        }
        FC_ERR(ss.str());
        FC_THROWM(Base::RuntimeError,
                "Cyclice depending documents detected.\n"
                "Please check Report View for more details.");
    }

    for (auto rIt=make_order.rbegin(); rIt!=make_order.rend(); ++rIt)
        ret.push_back(vertexMap[*rIt]);
    return ret;
}

void Document::_rebuildDependencyList(const std::vector<App::DocumentObject*> &objs)
{
#ifdef USE_OLD_DAG
    _buildDependencyList(objs.empty()?d->objectArray:objs,false,0,&d->DepList,&d->VertexObjectList);
#else
    (void)objs;
#endif
}

/**
 * @brief Signal that object identifiers, typically a property or document object has been renamed.
 *
 * This function iterates through all document object in the document, and calls its
 * renameObjectIdentifiers functions.
 *
 * @param paths Map with current and new names
 */

void Document::renameObjectIdentifiers(const std::map<App::ObjectIdentifier,
                                       App::ObjectIdentifier> &paths,
                                       const std::function<bool(const App::DocumentObject*)> & selector)
{
    std::map<App::ObjectIdentifier, App::ObjectIdentifier> extendedPaths;

    std::map<App::ObjectIdentifier, App::ObjectIdentifier>::const_iterator it = paths.begin();
    while (it != paths.end()) {
        extendedPaths[it->first.canonicalPath()] = it->second.canonicalPath();
        ++it;
    }

    for (auto it : d->objectArray) {
        if (selector(it)) {
            it->renameObjectIdentifiers(extendedPaths);
        }
    }
}

namespace {
int _Recomputing;
class RecomputeCounter {
public:
    RecomputeCounter()
    {
        ++_Recomputing;
    }
    ~RecomputeCounter()
    {
        --_Recomputing;
    }
};
} // anonymous namespace

bool Document::isAnyRecomputing()
{
    return _Recomputing != 0;
}

int Document::recompute(const std::vector<App::DocumentObject*> &objs, bool force, bool *hasError, int options)
{
    OperationScope operation;   // sec 27.38
    RecomputeCounter counter;
    // A recompute is an invocation of its own: writes made by execute()
    // with no transaction active group under one implicit transaction
    // that closes with the recompute (docs/TransactionLog.md sec 9.1).
    Application::InvocationScope scope("recompute");
    const auto recomputeClock = std::chrono::steady_clock::now();

    if (d->undoing || d->rollback) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG))
            FC_WARN("Ignore document recompute on undo/redo");
        return 0;
    }

    ExpressionParser::clearWarning();

    int objectCount = 0;
    if (testStatus(Document::PartialDoc)) {
        if(mustExecute())
            FC_WARN("Please reload partial document '" << Label.getValue() << "' for recomputation.");
        return 0;
    }
    // A pinned version is as it was saved (docs/TransactionLog.md sec 27.22).
    if (testStatus(Document::FrozenVersion)) {
        FC_LOG("pinned version '" << Label.getValue() << "' is not recomputed");
        return 0;
    }
    if (testStatus(Document::Recomputing)) {
        // this is clearly a bug in the calling instance
        FC_ERR("Recursive calling of recompute for document " << getName());
        return 0;
    }
    // The 'SkipRecompute' flag can be (tmp.) set to avoid too many
    // time expensive recomputes
    if(!force && testStatus(Document::SkipRecompute)) {
        signalSkipRecompute(*this,objs);
        return 0;
    }

    // delete recompute log
    d->clearRecomputeLog();

    d->skippedObjs.clear();

    FC_TIME_INIT(t);

    Base::ObjectStatusLocker<Document::Status, Document> exe(Document::Recomputing, this);
    // Every object's touched state before anything here changes it, for the
    // recompute record (docs/TransactionLog.md sec 27.58). Asked without
    // making the log, which is made where it always was, after the
    // recompute: made here, its end-of-process flush was registered before
    // the statics OCCT makes on first use in the recompute, and ran after
    // they were gone (Part_tests_run, FeaturePartCutTest.testMustExecute).
    std::map<long, TouchedState> touchedBefore;
    if (DocumentParams::getTransactionLog() != 0 && !d->noLog) {
        for (auto obj : d->objectArray)
            touchedBefore.emplace(obj->getID(), touchedStateOf(*obj));
    }
    signalBeforeRecompute(*this);

    // The input recompute stratum. Every parameter settles here, in its own
    // topological order, before a single execute() runs -- see
    // docs/InputProperties.md section 5. It has to precede getDependencyList()
    // below: the touches it raises are what put an object into the work list,
    // and a pure parameter edit would otherwise sort to an empty one.
    std::vector<DocumentObject*> inputReferrers;
    {
        InputStratum stratum(this);
        std::string error;
        DocumentObject *culprit = nullptr;
        if (!stratum.build(error, &culprit)
                || !stratum.evaluate(force || testStatus(Document::Restoring),
                                     inputReferrers, error, &culprit))
        {
            FC_ERR("Input stratum: " << error);
            if (culprit)
                d->addRecomputeLog(error, culprit);
            if (hasError)
                *hasError = true;
        }
    }
    for (auto obj : inputReferrers) {
        // The same marking the object phase uses for an inList object, and
        // deliberately not enforceRecompute(): the binding is re-evaluated
        // either way, and only a value that really moved goes any further.
        obj->StatusBits.set(ObjectStatus::Enforce);
        obj->StatusBits.set(ObjectStatus::Touch);
        signalTouchedObject(*obj);
    }

#if 0
    //////////////////////////////////////////////////////////////////////////
    // FIXME Comment by Realthunder:
    // the topologicalSrot() below cannot handle partial recompute, haven't got
    // time to figure out the code yet, simply use back boost::topological_sort
    // for now, that is, rely on getDependencyList() to do the sorting. The
    // downside is, it didn't take advantage of the ready built InList, nor will
    // it report for cyclic dependency.
    //////////////////////////////////////////////////////////////////////////

    // get the sorted vector of all dependent objects and go though it from the end
    auto depObjs = getDependencyList(objs.empty()?d->objectArray:objs);
    vector<DocumentObject*> topoSortedObjects = topologicalSort(depObjs);
    if (topoSortedObjects.size() != depObjs.size()){
        cerr << "App::Document::recompute(): cyclic dependency detected" << endl;
        topoSortedObjects = d->partialTopologicalSort(depObjs);
    }
    std::reverse(topoSortedObjects.begin(),topoSortedObjects.end());
#else
    // A referrer the stratum just touched has to be in the sorted list, or the
    // parameter change it is waiting on lands nowhere. Only a partial
    // recompute can miss it; the full list already holds everything.
    std::vector<DocumentObject*> partialObjs;
    if (!objs.empty() && !inputReferrers.empty()) {
        partialObjs = objs;
        partialObjs.insert(partialObjs.end(), inputReferrers.begin(), inputReferrers.end());
    }
    const auto &sortInput = !partialObjs.empty() ? partialObjs
                                                 : (objs.empty() ? d->objectArray : objs);
    auto topoSortedObjects = getDependencyList(sortInput,DepSort|options);
#endif
    for(auto obj : topoSortedObjects)
        obj->setStatus(ObjectStatus::PendingRecompute,true);

    bool canAbort = DocumentParams::getCanAbortRecompute();

    std::set<App::DocumentObject *> filter;
    size_t idx = 0;

    // For the recompute record (docs/TransactionLog.md sec 27.53): each
    // object this recompute made up to date or failed on, once, with the
    // seconds its execute() took -- not every object it looked at.
    std::vector<std::pair<App::DocumentObject*, double>> done;
    std::map<App::DocumentObject*, size_t> doneAt;
    auto noteDone = [&](App::DocumentObject* obj, double seconds) {
        auto res = doneAt.emplace(obj, done.size());
        if (res.second)
            done.emplace_back(obj, seconds);
        else
            done[res.first->second].second += seconds;
    };

    FC_TIME_INIT(t2);

    bool aborted = false;
    try {
        // maximum two passes to allow some form of dependency inversion
        for(int passes=0; passes<2 && idx<topoSortedObjects.size(); ++passes) {
            std::unique_ptr<Base::SequencerLauncher> seq;
            if(canAbort) {
                seq = std::make_unique<Base::SequencerLauncher>("Recompute...", topoSortedObjects.size());
            }
            FC_LOG("Recompute pass " << passes);
            for (; idx < topoSortedObjects.size(); ++idx) {
                auto obj = topoSortedObjects[idx];
                if(!obj->isAttachedToDocument() || filter.find(obj)!=filter.end())
                    continue;
                // Reached through a link from another document: a pinned
                // version is as it was saved, touched or not (sec 27.22).
                if (obj->getDocument()->testStatus(Document::FrozenVersion))
                    continue;
                // ask the object if it should be recomputed
                bool doRecompute = false;
                double took = 0;
                if (obj->mustRecompute()) {
                    doRecompute = true;
                    ++objectCount;
                    const auto start = std::chrono::steady_clock::now();
                    int res = _recomputeFeature(obj);
                    took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                               .count();
                    if(res) {
                        noteDone(obj, took);
                        if(hasError)
                            *hasError = true;
                        if(res < 0) {
                            passes = 2;
                            break;
                        }
                        // if something happened filter all object in its
                        // inListRecursive from the queue then proceed
                        obj->getInListEx(filter,true);
                        filter.insert(obj);
                        continue;
                    }
                }
                if(obj->isTouched() || doRecompute) {
                    noteDone(obj, took);
                    signalRecomputedObject(*obj);
                    GetApplication().signalRecomputedObject(*this, *obj);
                    obj->purgeTouched();
                    // Mark all dependent object with ObjectStatus::Enforce.
                    // Note that We don't call enforceRecompute() here in order
                    // to enable recomputation optimization (see
                    // _recomputeFeature())
                    for (auto inObjIt : obj->getInList()) {
                        inObjIt->StatusBits.set(ObjectStatus::Enforce);
                        inObjIt->StatusBits.set(ObjectStatus::Touch);
                        if (obj->getDocument())
                            obj->getDocument()->signalTouchedObject(*obj);
                    }

                    // give the object a chance to revert the above touching,
                    // because for example, new objects are created with
                    // object's execute(), and it will be safe to not touch
                    // those objects.
                    obj->afterRecompute();
                }
                if (seq)
                    seq->next(true);
            }
            // check if all objects are recomputed but still thouched
            for (size_t i=0;i<topoSortedObjects.size();++i) {
                auto obj = topoSortedObjects[i];
                obj->setStatus(ObjectStatus::Recompute2,false);
                if(!filter.count(obj) && obj->isTouched()) {
                    if(passes>0)
                        FC_ERR(obj->getFullName() << " still touched after recompute");
                    else{
                        FC_LOG(obj->getFullName() << " still touched after recompute");
                        if(idx>=topoSortedObjects.size()) {
                            // let's start the next pass on the first touched object
                            idx = i;
                        }
                        obj->setStatus(ObjectStatus::Recompute2,true);
                    }
                }
            }
        }
    }
    catch(Base::AbortException &e){
        aborted = true;
    }
    catch(Base::Exception &e) {
        e.ReportException();
    }

    FC_TIME_LOG(t2, "Recompute");

    for(auto obj : topoSortedObjects) {
        if(!obj->isAttachedToDocument())
            continue;
        obj->setStatus(ObjectStatus::PendingRecompute,false);
        obj->setStatus(ObjectStatus::Recompute2,false);
    }

    if (aborted)
        throw Base::AbortException();

    // The recompute record (docs/TransactionLog.md sec 11), read here, before
    // anything else runs: with undo off both the commit below and an observer
    // of signalRecomputed -- a Python one that clears the document, in
    // CAMTests.TestPathHelix -- delete objects topoSortedObjects still points
    // at. It is logged after the implicit transaction of the derived writes,
    // so it follows them.
    // With each object's touched state before and after (sec 27.58), and
    // any other object whose touched state the recompute changed.
    auto log = getTransactionLog();
    std::vector<TransactionLog::RecomputedObject> record;
    if (log) {
        record.reserve(done.size());
        auto withState = [&](TransactionLog::RecomputedObject& r, const TouchedState& after) {
            auto it = touchedBefore.find(r.id);
            if (it != touchedBefore.end()) {
                r.before = it->second.bits;
                r.beforeProps = it->second.props;
            }
            r.after = after.bits;
            r.afterProps = after.props;
        };
        for (const auto& d : done) {
            auto obj = d.first;
            if (!obj->isAttachedToDocument())
                continue;
            TransactionLog::RecomputedObject r;
            r.id = obj->getID();
            r.seconds = d.second;
            r.error = obj->isError();
            if (r.error) {
                const char* msg = getErrorDescription(obj);
                r.message = msg ? msg : "";
            }
            withState(r, touchedStateOf(*obj));
            record.push_back(std::move(r));
        }
        for (auto obj : d->objectArray) {
            if (doneAt.count(obj))
                continue;
            auto it = touchedBefore.find(obj->getID());
            if (it == touchedBefore.end())
                continue;
            TouchedState now = touchedStateOf(*obj);
            if (now == it->second)
                continue;
            TransactionLog::RecomputedObject r;
            r.id = obj->getID();
            r.seconds = 0;
            r.error = false;
            r.done = false;
            withState(r, now);
            record.push_back(std::move(r));
        }
    }

    signalRecomputed(*this,topoSortedObjects);

    if (log) {
        commitImplicitTransaction();
        // A transaction still open holds writes made before the recompute:
        // the record follows its row (sec 27.59).
        log->onRecompute(record,
                         std::chrono::duration<double>(std::chrono::steady_clock::now()
                                                       - recomputeClock).count(),
                         d->activeUndoTransaction);
    }

    if(!d->skippedObjs.empty())
        signalSkipRecompute(*this,d->skippedObjs);

    FC_TIME_LOG(t,"Recompute total");

    if (!d->_RecomputeLog.empty()) {
        if (!testStatus(Status::IgnoreErrorOnRecompute))
            Base::Console().Error("Recompute failed!\n");
    }

    clearPendingRemove();
    return objectCount;
}

void Document::clearPendingRemove()
{
    for (auto doc : GetApplication().getDocuments()) {
        decltype(doc->d->pendingRemove) objs;
        objs.swap(doc->d->pendingRemove);
        for(auto &o : objs) {
            try {
                auto obj = o.getObject();
                if(obj)
                    obj->getDocument()->removeObject(obj->getNameInDocument());
            } catch (Base::Exception & e) {
                FC_ERR("error when removing object " << o.getDocumentName() << '#' << o.getObjectName()
                        << ": " << e.what());
            }
        }
    }
}

/*!
  Does almost the same as topologicalSort() until no object with an input degree of zero
  can be found. It then searches for objects with an output degree of zero until neither
  an object with input or output degree can be found. The remaining objects form one or
  multiple cycles.
  An alternative to this method might be:
  https://en.wikipedia.org/wiki/Tarjan%E2%80%99s_strongly_connected_components_algorithm
 */
std::vector<App::DocumentObject*> DocumentP::partialTopologicalSort(
        const std::vector<App::DocumentObject*>& objects)
{
    vector < App::DocumentObject* > ret;
    ret.reserve(objects.size());
    // pairs of input and output degree
    map < App::DocumentObject*, std::pair<int, int> > countMap;

    for (auto objectIt : objects) {
        //we need inlist with unique entries
        auto in = objectIt->getInList();
        std::sort(in.begin(), in.end());
        in.erase(std::unique(in.begin(), in.end()), in.end());

        //we need outlist with unique entries
        auto out = objectIt->getOutList();
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());

        countMap[objectIt] = std::make_pair(in.size(), out.size());
    }

    std::list<App::DocumentObject*> degIn;
    std::list<App::DocumentObject*> degOut;

    bool removeVertex = true;
    while (removeVertex) {
        removeVertex = false;

        // try input degree
        auto degInIt = find_if(countMap.begin(), countMap.end(),
                               [](pair< App::DocumentObject*, pair<int, int> > vertex)->bool {
            return vertex.second.first == 0;
        });

        if (degInIt != countMap.end()) {
            removeVertex = true;
            degIn.push_back(degInIt->first);
            degInIt->second.first = degInIt->second.first - 1;

            //we need outlist with unique entries
            auto out = degInIt->first->getOutList();
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());

            for (auto outListIt : out) {
                auto outListMapIt = countMap.find(outListIt);
                if (outListMapIt != countMap.end())
                    outListMapIt->second.first = outListMapIt->second.first - 1;
            }
        }
    }

    // make the output degree negative if input degree is negative
    // to mark the vertex as processed
    for (auto& countIt : countMap) {
        if (countIt.second.first < 0) {
            countIt.second.second = -1;
        }
    }

    removeVertex = degIn.size() != objects.size();
    while (removeVertex) {
        removeVertex = false;

        auto degOutIt = find_if(countMap.begin(), countMap.end(),
                               [](pair< App::DocumentObject*, pair<int, int> > vertex)->bool {
            return vertex.second.second == 0;
        });

        if (degOutIt != countMap.end()) {
            removeVertex = true;
            degOut.push_front(degOutIt->first);
            degOutIt->second.second = degOutIt->second.second - 1;

            //we need inlist with unique entries
            auto in = degOutIt->first->getInList();
            std::sort(in.begin(), in.end());
            in.erase(std::unique(in.begin(), in.end()), in.end());

            for (auto inListIt : in) {
                auto inListMapIt = countMap.find(inListIt);
                if (inListMapIt != countMap.end())
                    inListMapIt->second.second = inListMapIt->second.second - 1;
            }
        }
    }

    // at this point we have no root object any more
    for (auto countIt : countMap) {
        if (countIt.second.first > 0 && countIt.second.second > 0) {
            degIn.push_back(countIt.first);
        }
    }

    ret.insert(ret.end(), degIn.begin(), degIn.end());
    ret.insert(ret.end(), degOut.begin(), degOut.end());

    return ret;
}

std::vector<App::DocumentObject*> DocumentP::topologicalSort(const std::vector<App::DocumentObject*>& objects) const
{
    // topological sort algorithm described here:
    // https://de.wikipedia.org/wiki/Topologische_Sortierung#Algorithmus_f.C3.BCr_das_Topologische_Sortieren
    vector < App::DocumentObject* > ret;
    ret.reserve(objects.size());
    map < App::DocumentObject*,int > countMap;

    for (auto objectIt : objects) {
        // We now support externally linked objects
        // if(!obj->isAttachedToDocument() || obj->getDocument()!=this)
        if(!objectIt->isAttachedToDocument())
            continue;
        //we need inlist with unique entries
        auto in = objectIt->getInList();
        std::sort(in.begin(), in.end());
        in.erase(std::unique(in.begin(), in.end()), in.end());

        countMap[objectIt] = in.size();
    }

    auto rootObjeIt = find_if(countMap.begin(), countMap.end(), [](pair < App::DocumentObject*, int > count)->bool {
        return count.second == 0;
    });

    if (rootObjeIt == countMap.end()){
        cerr << "Document::topologicalSort: cyclic dependency detected (no root object)" << endl;
        return ret;
    }

    while (rootObjeIt != countMap.end()){
        rootObjeIt->second = rootObjeIt->second - 1;

        //we need outlist with unique entries
        auto out = rootObjeIt->first->getOutList();
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());

        for (auto outListIt : out) {
            auto outListMapIt = countMap.find(outListIt);
            if (outListMapIt != countMap.end())
                outListMapIt->second = outListMapIt->second - 1;
        }
        ret.push_back(rootObjeIt->first);

        rootObjeIt = find_if(countMap.begin(), countMap.end(), [](pair < App::DocumentObject*, int > count)->bool {
            return count.second == 0;
        });
    }

    return ret;
}

std::vector<App::DocumentObject*> Document::topologicalSort() const
{
    return d->topologicalSort(d->objectArray);
}

const char * Document::getErrorDescription(const App::DocumentObject*Obj) const
{
    return d->findRecomputeLog(Obj);
}

void Document::setErrorDescription(App::DocumentObject *Obj, const char *msg)
{
    if (msg && msg[0] && Obj)
        d->addRecomputeLog(msg, Obj);
}

void Document::setErrorDescription(App::Property *Prop, const char *msg)
{
    if (msg && msg[0] && Prop) {
        auto obj = Base::freecad_dynamic_cast<DocumentObject>(Prop->getContainer());
        if (obj)
            d->addRecomputeLog(msg, obj);
    }
}

// call the recompute of the Feature and handle the exceptions and errors.
int Document::_recomputeFeature(DocumentObject* Feat)
{
    DocumentObjectExecReturn  *returnCode = DocumentObject::StdReturn;

    // delete recompute log
    d->clearRecomputeLog(Feat);

    try {
        returnCode = Feat->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteNonOutput);
        if (returnCode == DocumentObject::StdReturn) {
            bool doRecompute = Feat->isError() || Feat->_enforceRecompute
                                               || !DocumentParams::getOptimizeRecompute()
                                               || testStatus(Status::Restoring);
            if(!doRecompute) {
                static unsigned long long mask = (1<<Property::Output)
                                               | (1<<Property::PropOutput)
                                               | (1<<Property::NoRecompute)
                                               | (1<<Property::PropNoRecompute);
                auto prop = Feat->testPropertyStatus(Property::Touched, mask);
                if(prop) {
                    FC_LOG("recompute on touched " << prop->getFullName());
                    doRecompute = true;
                }
            }

            if(!doRecompute && Feat->skipRecompute()) {
                d->skippedObjs.push_back(Feat);
                FC_LOG("Skip recomputing " << Feat->getFullName());
            } else {
                Feat->_enforceRecompute = false;
                returnCode = Feat->recompute();
                // An input property changed inside execute(). Property::hasSetValue()
                // threw, but AtomicPropertyChange's destructor swallows exceptions,
                // so the latch is what makes this reliably fatal.
                // See docs/InputProperties.md section 6.
                auto violation = DocumentObject::takeInputViolation();
                if (!violation.empty()) {
                    std::string msg = "Input property " + violation
                        + " changed during recompute";
                    FC_ERR(msg << " in " << Feat->getFullName());
                    d->addRecomputeLog(msg, Feat);
                    return 1;
                }
            }

            if(returnCode == DocumentObject::StdReturn)
                returnCode = Feat->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteOutput);
        }
    }
    catch(Base::AbortException &e){
        FC_LOG("Failed to recompute " << Feat->getFullName() << ": " << e.what());
        d->addRecomputeLog("User abort",Feat);
        throw;
    }
    catch (const Base::MemoryException& e) {
        FC_ERR("Memory exception in " << Feat->getFullName() << " thrown: " << e.what());
        d->addRecomputeLog("Out of memory exception",Feat);
        return 1;
    }
    catch (Base::Exception &e) {
        e.ReportException();
        FC_LOG("Failed to recompute " << Feat->getFullName() << ": " << e.what());
        d->addRecomputeLog(e.what(),Feat);
        return 1;
    }
    catch (std::exception &e) {
        FC_ERR("exception in " << Feat->getFullName() << " thrown: " << e.what());
        d->addRecomputeLog(e.what(),Feat);
        return 1;
    }
#ifndef FC_DEBUG
    catch (...) {
        FC_ERR("Unknown exception in " << Feat->getFullName() << " thrown");
        d->addRecomputeLog("Unknown exception!",Feat);
        return 1;
    }
#endif

    if (returnCode == DocumentObject::StdReturn) {
        Feat->resetError();
    }
    else {
        returnCode->Which = Feat;
        d->addRecomputeLog(returnCode);
        FC_ERR("Failed to recompute " << Feat->getFullName() << ": " << returnCode->Why);
        return 1;
    }
    return 0;
}

bool Document::recomputeFeature(DocumentObject* Feat, bool recursive)
{
    // verify that the feature is (active) part of the document
    if (Feat->isAttachedToDocument()) {
        if(recursive) {
            bool hasError = false;
            recompute({Feat},true,&hasError);
            return !hasError;
        } else {
            if (testStatus(FrozenVersion))
                return Feat->isValid();
            _recomputeFeature(Feat);
            signalRecomputedObject(*Feat);
            GetApplication().signalRecomputedObject(*this, *Feat);
            return Feat->isValid();
        }
    }else
        return false;
}

DocumentObject * Document::addObject(const char* sType, const char* pObjectName,
                                     bool isNew, const char* viewType, bool isPartial)
{
    // Creating or deleting an object is a change this document may not take
    // while it is still filling itself in (Document::UserEditGuard). Neither
    // goes through DocumentObject::touch(), so each is asked here -- which
    // also covers the commands no name list would know about.
    checkUserEdit(this, nullptr, nullptr);
    checkNotFrozen("add an object");
    Base::Type type = Base::Type::getTypeIfDerivedFrom(sType, App::DocumentObject::getClassTypeId(), true);
    if (type.isBad()) {
        std::stringstream str;
        str << "'" << sType << "' is not a document object type";
        THROWM(Base::TypeError, str.str())
    }

    void* typeInstance = type.createInstance();
    if (!typeInstance)
        return nullptr;

    App::DocumentObject* pcObject = static_cast<App::DocumentObject*>(typeInstance);

    pcObject->setDocument(this);

    // do no transactions if we do a rollback!
    if (!d->rollback) {
        // Undo stuff
        _checkTransaction(nullptr,nullptr,__LINE__);
        if (d->activeUndoTransaction)
            d->activeUndoTransaction->addObjectDel(pcObject);
    }

    // get Unique name
    string ObjectName;

    if (pObjectName && pObjectName[0] != '\0')
        ObjectName = getUniqueObjectName(pObjectName, d->restoringId);
    else
        ObjectName = getUniqueObjectName(sType, d->restoringId);


    d->activeObject = pcObject;

    // insert in the name map
    d->objectMap[ObjectName] = pcObject;
    // generate object id and add to id map;
    pcObject->_Id = d->addObject(pcObject);
    d->noteObjectName(ObjectName, pcObject->_Id);
    // cache the pointer to the name string in the Object (for performance of DocumentObject::getNameInDocument())
    pcObject->pcNameInDocument = &(d->objectMap.find(ObjectName)->first);

    // If we are restoring, don't set the Label object now; it will be restored later. This is to avoid potential duplicate
    // label conflicts later.
    if (!d->StatusBits.test(Restoring))
        pcObject->Label.setValue( ObjectName );

    // Call the object-specific initialization
    if (!d->undoing && !d->rollback && isNew) {
        pcObject->TreeRank.setValue(treeRanks().second + 1);
        pcObject->setupObject ();
    }

    // mark the object as new (i.e. set status bit 2) and send the signal
    pcObject->setStatus(ObjectStatus::New, true);

    pcObject->setStatus(ObjectStatus::PartialObject, isPartial);

    if (!viewType || viewType[0] == '\0')
        viewType = pcObject->getViewProviderNameOverride();

    if (viewType && viewType[0] != '\0')
        pcObject->_pcViewProviderName = viewType;

    signalNewObject(*pcObject);

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        signalTransactionAppend(*pcObject, d->activeUndoTransaction);
    }

    // The new object may create object by itself, so set the active object
    // again, before signaling.
    d->activeObject = pcObject;
    signalActivatedObject(*pcObject);

    // return the Object
    return pcObject;
}

std::vector<DocumentObject *> Document::addObjects(const char* sType, const std::vector<std::string>& objectNames, bool isNew)
{
    checkNotFrozen("add objects");
    Base::Type type = Base::Type::getTypeIfDerivedFrom(sType, App::DocumentObject::getClassTypeId(), true);
    if (type.isBad()) {
        std::stringstream str;
        str << "'" << sType << "' is not a document object type";
        THROWM(Base::TypeError, str.str())
    }

    std::vector<DocumentObject *> objects;
    objects.resize(objectNames.size());
    std::generate(objects.begin(), objects.end(),
                  [&]{ return static_cast<App::DocumentObject*>(type.createInstance()); });
    // the type instance could be a null pointer, it is enough to check the first element
    if (!objects.empty() && !objects[0]) {
        objects.clear();
        return objects;
    }

    // get all existing object names
    std::vector<std::string> reservedNames;
    reservedNames.reserve(d->objectMap.size());
    for (const auto & pos : d->objectMap) {
        reservedNames.push_back(pos.first);
    }
    // And every name the file gave (sec 27.40 item 3): these are new.
    std::set<std::string> fileNames;
    if (d->history) {
        for (const auto& n : d->history->objectNames()) {
            if (!d->objectMap.count(n.first)) {
                reservedNames.push_back(n.first);
                fileNames.insert(n.first);
            }
        }
    }

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        auto index = std::distance(objects.begin(), it);
        App::DocumentObject* pcObject = *it;
        pcObject->setDocument(this);

        // do no transactions if we do a rollback!
        if (!d->rollback) {
            // Undo stuff
            _checkTransaction(nullptr,nullptr,__LINE__);
            if (d->activeUndoTransaction) {
                d->activeUndoTransaction->addObjectDel(pcObject);
            }
        }

        // get unique name
        std::string ObjectName = objectNames[index];
        if (ObjectName.empty())
            ObjectName = sType;
        ObjectName = Base::Tools::getIdentifier(ObjectName);
        if (d->objectMap.find(ObjectName) != d->objectMap.end() || fileNames.count(ObjectName)) {
            // remove also trailing digits from clean name which is to avoid to create lengthy names
            // like 'Box001001'
            if (!testStatus(KeepTrailingDigits)) {
                std::string::size_type index = ObjectName.find_last_not_of("0123456789");
                if (index+1 < ObjectName.size()) {
                    ObjectName = ObjectName.substr(0,index+1);
                }
            }

            ObjectName = Base::Tools::getUniqueName(ObjectName, reservedNames, 3);
        }

        reservedNames.push_back(ObjectName);

        // insert in the name map
        d->objectMap[ObjectName] = pcObject;
        // generate object id and add to id map;
        pcObject->_Id = d->addObject(pcObject);
        d->noteObjectName(ObjectName, pcObject->_Id);
        // cache the pointer to the name string in the Object (for performance of DocumentObject::getNameInDocument())
        pcObject->pcNameInDocument = &(d->objectMap.find(ObjectName)->first);

        pcObject->Label.setValue(ObjectName);

        // Call the object-specific initialization
        if (!d->undoing && !d->rollback && isNew) {
            pcObject->TreeRank.setValue(treeRanks().second + 1);
            pcObject->setupObject();
        }

        // mark the object as new (i.e. set status bit 2) and send the signal
        pcObject->setStatus(ObjectStatus::New, true);

        const char *viewType = pcObject->getViewProviderNameOverride();
        pcObject->_pcViewProviderName = viewType ? viewType : "";

        signalNewObject(*pcObject);

        // do no transactions if we do a rollback!
        if (!d->rollback && d->activeUndoTransaction) {
            signalTransactionAppend(*pcObject, d->activeUndoTransaction);
        }
    }

    if (!objects.empty()) {
        d->activeObject = objects.back();
        signalActivatedObject(*objects.back());
    }

    return objects;
}

void Document::addObject(DocumentObject* pcObject, const char* pObjectName, bool activate)
{
    // Creating or deleting an object is a change this document may not take
    // while it is still filling itself in (Document::UserEditGuard). Neither
    // goes through DocumentObject::touch(), so each is asked here -- which
    // also covers the commands no name list would know about.
    checkUserEdit(this, nullptr, nullptr);
    checkNotFrozen("add an object");
    if (pcObject->getDocument()) {
        THROWM(Base::RuntimeError, "Document object is already added to a document")
    }

    pcObject->setDocument(this);

    // do no transactions if we do a rollback!
    if (!d->rollback) {
        // Undo stuff
        _checkTransaction(nullptr,nullptr,__LINE__);
        if (d->activeUndoTransaction)
            d->activeUndoTransaction->addObjectDel(pcObject);
    }

    // get unique name
    string ObjectName;
    if (pObjectName && pObjectName[0] != '\0')
        ObjectName = getUniqueObjectName(pObjectName, pcObject->getID());
    else
        ObjectName = getUniqueObjectName(pcObject->getTypeId().getName(), pcObject->getID());

    if (activate)
        d->activeObject = pcObject;

    // insert in the name map
    d->objectMap[ObjectName] = pcObject;
    // generate object id and add to id map;
    pcObject->_Id = d->addObject(pcObject);
    d->noteObjectName(ObjectName, pcObject->_Id);
    // cache the pointer to the name string in the Object (for performance of DocumentObject::getNameInDocument())
    pcObject->pcNameInDocument = &(d->objectMap.find(ObjectName)->first);

    pcObject->Label.setValue( ObjectName );

    // mark the object as new (i.e. set status bit 2) and send the signal
    pcObject->setStatus(ObjectStatus::New, true);

    const char *viewType = pcObject->getViewProviderNameOverride();
    pcObject->_pcViewProviderName = viewType ? viewType : "";

    signalNewObject(*pcObject);

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        signalTransactionAppend(*pcObject, d->activeUndoTransaction);
    }

    if (activate)
        d->activeObject = pcObject;
    signalActivatedObject(*pcObject);
}

void Document::_addObject(DocumentObject* pcObject, const char* pObjectName)
{
    std::string ObjectName = getUniqueObjectName(pObjectName, pcObject->getID());
    d->objectMap[ObjectName] = pcObject;
    // generate object id and add to id map;
    pcObject->_Id = d->addObject(pcObject);
    d->noteObjectName(ObjectName, pcObject->_Id);
    // cache the pointer to the name string in the Object (for performance of DocumentObject::getNameInDocument())
    pcObject->pcNameInDocument = &(d->objectMap.find(ObjectName)->first);

    // do no transactions if we do a rollback!
    if (!d->rollback) {
        // Undo stuff
        _checkTransaction(nullptr,nullptr,__LINE__);
        if (d->activeUndoTransaction)
            d->activeUndoTransaction->addObjectDel(pcObject);
    }

    const char *viewType = pcObject->getViewProviderNameOverride();
    pcObject->_pcViewProviderName = viewType ? viewType : "";

    // send the signal
    signalNewObject(*pcObject);

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        signalTransactionAppend(*pcObject, d->activeUndoTransaction);
    }

    d->activeObject = pcObject;
    signalActivatedObject(*pcObject);
}

/// Remove an object out of the document
void Document::removeObject(const char* sName)
{
    OperationScope scope;   // sec 27.38
    // Creating or deleting an object is a change this document may not take
    // while it is still filling itself in (Document::UserEditGuard). Neither
    // goes through DocumentObject::touch(), so each is asked here -- which
    // also covers the commands no name list would know about.
    checkUserEdit(this, nullptr, nullptr);
    checkNotFrozen("remove an object");
    auto pos = d->objectMap.find(sName);

    // name not found?
    if (pos == d->objectMap.end())
        return;

    if (pos->second->testStatus(ObjectStatus::PendingRecompute)) {
        FC_LOG("pending remove of recomputing object " << pos->second->getFullName());
        d->pendingRemove.emplace_back(pos->second);
        return;
    }

    if (pos->second->testStatus(ObjectStatus::ObjEditing)) {
        FC_LOG("pending remove of editing object " << pos->second->getFullName());
        d->pendingRemove.emplace_back(pos->second);
        return;
    }

    TransactionLocker tlock;

    _checkTransaction(pos->second,nullptr,__LINE__);

    if (d->activeObject == pos->second)
        d->activeObject = nullptr;

    // Mark the object as about to be deleted
    pos->second->setStatus(ObjectStatus::Remove, true);
    if (!d->undoing && !d->rollback) {
        pos->second->unsetupObject();
    }

    signalDeletedObject(*(pos->second));

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        // in this case transaction delete or save the object
        signalTransactionRemove(*pos->second, d->activeUndoTransaction);
    }
    else {
        // if not saved in undo -> delete object
        signalTransactionRemove(*pos->second, 0);
    }

#ifdef USE_OLD_DAG
    if (!d->vertexMap.empty()) {
        // recompute of document is running
        for (std::map<Vertex,DocumentObject*>::iterator it = d->vertexMap.begin(); it != d->vertexMap.end(); ++it) {
            if (it->second == pos->second) {
                it->second = 0; // just nullify the pointer
                break;
            }
        }
    }
#endif //USE_OLD_DAG

    // Before deleting we must nullify all dependent objects
    breakDependency(pos->second, true);

    //and remove the tip if needed
    if (Tip.getValue() && strcmp(Tip.getValue()->getNameInDocument(), sName)==0) {
        Tip.setValue(nullptr);
        TipName.setValue("");
    }

    // remove the ID before possibly deleting the object
    d->objectIdMap.erase(pos->second->_Id);
    // Unset the bit to be on the safe side
    pos->second->setStatus(ObjectStatus::Remove, false);

    // do no transactions if we do a rollback!
    std::unique_ptr<DocumentObject> tobedestroyed;
    if (!d->rollback) {
        // Undo stuff
        if (d->activeUndoTransaction) {
            // in this case transaction delete or save the object
            d->activeUndoTransaction->addObjectNew(pos->second);
        }
        else {
            // if not saved in undo -> delete object later
            std::unique_ptr<DocumentObject> delobj(pos->second);
            tobedestroyed.swap(delobj);
            tobedestroyed->setStatus(ObjectStatus::Destroy, true);
        }
    }

    for (std::vector<DocumentObject*>::iterator obj = d->objectArray.begin(); obj != d->objectArray.end(); ++obj) {
        if (*obj == pos->second) {
            d->objectArray.erase(obj);
            break;
        }
    }

    // In case the object gets deleted the pointer must be nullified
    if (tobedestroyed) {
        tobedestroyed->pcNameInDocument = nullptr;
    }
    d->objectMap.erase(pos);
    ++d->revision;
}

/// Remove an object out of the document (internal)
void Document::_removeObject(DocumentObject* pcObject)
{
    if (pcObject->testStatus(ObjectStatus::PendingRecompute)) {
        FC_LOG("pending remove of recomputing object " << pcObject->getFullName());
        d->pendingRemove.emplace_back(pcObject);
        return;
    }

#if 0 // Allow deleting editing object when undo/redo

    if (pcObject->testStatus(ObjectStatus::ObjEditing)) {
        FC_LOG("pending remove of editing object " << pcObject->getFullName());
        d->pendingRemove.emplace_back(pcObject);
        return;
    }
#endif

    TransactionLocker tlock;

    // TODO Refactoring: share code with Document::removeObject() (2015-09-01, Fat-Zer)
    _checkTransaction(pcObject,nullptr,__LINE__);

    auto pos = d->objectMap.find(pcObject->getNameInDocument());

    if (d->activeObject == pcObject)
        d->activeObject = nullptr;

    // Mark the object as about to be removed
    pcObject->setStatus(ObjectStatus::Remove, true);
    if (!d->undoing && !d->rollback) {
        pcObject->unsetupObject();
    }
    signalDeletedObject(*pcObject);
    // TODO Check me if it's needed (2015-09-01, Fat-Zer)

    //remove the tip if needed
    if (Tip.getValue() == pcObject) {
        Tip.setValue(nullptr);
        TipName.setValue("");
    }

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        // Undo stuff
        signalTransactionRemove(*pcObject, d->activeUndoTransaction);
        breakDependency(pcObject, true);
        d->activeUndoTransaction->addObjectNew(pcObject);
    }
    else {
        // for a rollback delete the object
        signalTransactionRemove(*pcObject, 0);

        breakDependency(pcObject, true);
    }

    // remove from map
    pcObject->setStatus(ObjectStatus::Remove, false); // Unset the bit to be on the safe side
    d->objectIdMap.erase(pcObject->_Id);
    ++d->revision;
    for (std::vector<DocumentObject*>::iterator it = d->objectArray.begin(); it != d->objectArray.end(); ++it) {
        if (*it == pcObject) {
            d->objectArray.erase(it);
            break;
        }
    }

    // for a rollback delete the object
    if (d->rollback) {
        pcObject->setStatus(ObjectStatus::Destroy, true);
        if (!TransactionGuard::addPendingRemove(pcObject))
            delete pcObject;
    }

    // TransactionGuard::addPendingRemove() will call
    // DocumentObject::detachDocument() which will access its name in document.
    // So we have to delay removing object from objectMap, because the name is
    // stored in the map.
    d->objectMap.erase(pos);
}

static bool _RemovingObjects;
static int _RemovingObject;
static std::unordered_map<Property*, int> _PendingProps;
static int _PendingPropIndex;

bool Document::isRemoving(Property *prop)
{
    if (!prop || _RemovingObject == 0)
        return false;
    if (auto obj = Base::freecad_dynamic_cast<DocumentObject>(prop->getContainer())) {
        if (!obj->testStatus(App::Remove))
            _PendingProps.insert(std::make_pair(prop, _PendingPropIndex++));
    }
    return true;
}

void Document::removePendingProperty(Property *prop)
{
    _PendingProps.erase(prop);
}

void Document::removeObjects(const std::vector<std::string> &objs)
{
    // Creating or deleting an object is a change this document may not take
    // while it is still filling itself in (Document::UserEditGuard). Neither
    // goes through DocumentObject::touch(), so each is asked here -- which
    // also covers the commands no name list would know about.
    checkUserEdit(this, nullptr, nullptr);
    checkNotFrozen("remove objects");
    if (_RemovingObjects) {
        FC_ERR("recursive calling of Document.removeObjects()");
        return;
    }

    Base::StateLocker guard(_RemovingObjects);

    ++_RemovingObject;

    for (const auto &name : objs)
        removeObject(name.c_str());

    if (--_RemovingObject == 0) {
        std::vector<std::pair<Property*,int> > props;
        props.reserve(_PendingProps.size());
        props.insert(props.end(),_PendingProps.begin(),_PendingProps.end());
        std::sort(props.begin(), props.end(),
            [](const std::pair<Property*,int> &a, const std::pair<Property*,int> &b) {
                return a.second < b.second;
            });

        std::string errMsg;
        for(auto &v : props) {
            auto prop = v.first;
            // double check if the property exists, because it may be removed
            // while we are looping.
            if(_PendingProps.count(prop)) {
                Base::exceptionSafeCall(errMsg, [](Property *prop){prop->touch();}, prop);
                if(errMsg.size()) {
                    FC_ERR("Exception on post object removal "
                            << prop->getFullName() << ": " << errMsg);
                    errMsg.clear();
                }
            }
        }
        _PendingProps.clear();
        _PendingPropIndex = 0;
    }
}

void Document::breakDependency(DocumentObject* pcObject, bool clear)
{
    // Nullify all dependent objects
    PropertyLinkBase::breakLinks(pcObject,d->objectArray,clear);
}

std::vector<DocumentObject*> Document::copyObject(
    const std::vector<DocumentObject*> &objs, bool recursive, bool returnAll)
{
    std::vector<DocumentObject*> deps;
    if(!recursive)
        deps = objs;
    else
        deps = getDependencyList(objs,DepNoXLinked|DepSort);

    if (!testStatus(TempDoc) && !isSaved() && PropertyXLink::hasXLink(deps)) {
        throw Base::RuntimeError(
                "Document must be saved at least once before link to external objects");
    }

    MergeDocuments md(this);
    // if not copying recursively then suppress possible warnings
    md.setVerbose(recursive);

    unsigned int memsize=1000; // ~ for the meta-information
    for (auto it : deps)
        memsize += it->getMemSize();

    // if less than ~10 MB
    bool use_buffer=(memsize < 0xA00000);
    QByteArray res;
    try {
        res.reserve(memsize);
    }
    catch (const Base::MemoryException&) {
        use_buffer = false;
    }

    std::vector<App::DocumentObject*> imported;
    if (use_buffer) {
        Base::ByteArrayOStreambuf obuf(res);
        std::ostream ostr(&obuf);
        exportObjects(deps, ostr);

        Base::ByteArrayIStreambuf ibuf(res);
        std::istream istr(nullptr);
        istr.rdbuf(&ibuf);
        imported = md.importObjects(istr);
    } else {
        static Base::FileInfo fi(App::Application::getTempFileName());
        Base::ofstream ostr(fi, std::ios::out | std::ios::binary);
        exportObjects(deps, ostr);
        ostr.close();

        Base::ifstream istr(fi, std::ios::in | std::ios::binary);
        imported = md.importObjects(istr);
    }

    if (returnAll || imported.size()!=deps.size())
        return imported;

    std::unordered_map<App::DocumentObject*,size_t> indices;
    size_t i=0;
    for(auto o : deps)
        indices[o] = i++;
    std::vector<App::DocumentObject*> result;
    result.reserve(objs.size());
    for(auto o : objs)
        result.push_back(imported[indices[o]]);
    return result;
}

std::vector<App::DocumentObject*>
Document::importLinks(const std::vector<App::DocumentObject*> &objArray)
{
    std::set<App::DocumentObject*> links;
    getLinksTo(links,nullptr,GetLinkExternal,0,objArray);

    std::vector<App::DocumentObject*> objs;
    objs.insert(objs.end(),links.begin(),links.end());
    objs = App::Document::getDependencyList(objs);
    if(objs.empty()) {
        FC_ERR("nothing to import");
        return objs;
    }

    for(auto it=objs.begin();it!=objs.end();) {
        auto obj = *it;
        if(obj->getDocument() == this) {
            it = objs.erase(it);
            continue;
        }
        ++it;
        if(obj->testStatus(App::PartialObject)) {
            throw Base::RuntimeError(
                "Cannot import partial loaded object. Please reload the current document");
        }
    }

    Base::FileInfo fi(App::Application::getTempFileName());
    {
        // save stuff to temp file
        Base::ofstream str(fi, std::ios::out | std::ios::binary);
        MergeDocuments mimeView(this);
        exportObjects(objs, str);
        str.close();
    }
    Base::ifstream str(fi, std::ios::in | std::ios::binary);
    MergeDocuments mimeView(this);
    objs = mimeView.importObjects(str);
    str.close();
    fi.deleteFile();

    const auto &nameMap = mimeView.getNameMap();

    // First, find all link type properties that needs to be changed
    std::map<App::Property*,std::unique_ptr<App::Property> > propMap;
    std::vector<App::Property*> propList;
    for(auto obj : links) {
        propList.clear();
        obj->getPropertyList(propList);
        for(auto prop : propList) {
            auto linkProp = Base::freecad_dynamic_cast<PropertyLinkBase>(prop);
            if(linkProp && !prop->testStatus(Property::Immutable) && !obj->isReadOnly(prop)) {
                auto copy = linkProp->CopyOnImportExternal(nameMap);
                if(copy)
                    propMap[linkProp].reset(copy);
            }
        }
    }

    // Then change them in one go. Note that we don't make change in previous
    // loop, because a changed link property may break other depending link
    // properties, e.g. a link sub referring to some sub object of an xlink, If
    // that sub object is imported with a different name, and xlink is changed
    // before this link sub, it will break.
    for(auto &v : propMap)
        v.first->Paste(*v.second);

    return objs;
}

DocumentObject* Document::moveObject(DocumentObject* obj, bool recursive)
{
    if(!obj)
        return nullptr;
    Document* that = obj->getDocument();
    if (that == this) {
        auto ranks = treeRanks();
        if (obj->TreeRank.getValue() != ranks.second)
            obj->TreeRank.setValue(ranks.second+1);
        return nullptr; // nothing todo
    }

    // True object move without copy is only safe when undo is off on both
    // documents.
    if(!recursive && !d->iUndoMode && !that->d->iUndoMode && !that->d->rollback) {
        // all object of the other document that refer to this object must be nullified
        that->breakDependency(obj, false);
        std::string objname = getUniqueObjectName(obj->getNameInDocument());
        that->_removeObject(obj);
        this->_addObject(obj, objname.c_str());
        obj->setDocument(this);
        return obj;
    }

    std::vector<App::DocumentObject*> deps;
    if(recursive)
        deps = getDependencyList({obj},DepNoXLinked|DepSort);
    else
        deps.push_back(obj);

    auto objs = copyObject(deps,false);
    if(objs.empty())
        return nullptr;
    // Some object may delete its children if deleted, so we collect the IDs
    // or all depending objects for safety reason.
    std::vector<int> ids;
    ids.reserve(deps.size());
    for(auto o : deps)
        ids.push_back(o->getID());

    // We only remove object if it is the moving object or it has no
    // depending objects, i.e. an empty inList, which is why we need to
    // iterate the depending list backwards.
    for(auto iter=ids.rbegin();iter!=ids.rend();++iter) {
        auto o = that->getObjectByID(*iter);
        if(!o) continue;
        if(iter==ids.rbegin()
                || o->getInList().empty())
            that->removeObject(o->getNameInDocument());
    }
    return objs.back();
}

DocumentObject * Document::getActiveObject() const
{
    return d->activeObject;
}

DocumentObject * Document::getObject(const char *Name) const
{
    auto pos = d->objectMap.find(Name);

    if (pos != d->objectMap.end())
        return pos->second;
    else
        return nullptr;
}

DocumentObject * Document::getObjectByID(long id) const
{
    auto it = d->objectIdMap.find(id);
    if(it!=d->objectIdMap.end())
        return it->second;
    return nullptr;
}


// Note: This method is only used in Tree.cpp slotChangeObject(), see explanation there
bool Document::isIn(const DocumentObject *pFeat) const
{
    for (const auto & pos : d->objectMap) {
        if (pos.second == pFeat)
            return true;
    }

    return false;
}

const char * Document::getObjectName(DocumentObject *pFeat) const
{
    for (const auto & pos : d->objectMap) {
        if (pos.second == pFeat)
            return pos.first.c_str();
    }

    return nullptr;
}

std::string Document::getUniqueObjectName(const char *Name, long id) const
{
    if (!Name || *Name == '\0')
        return {};
    std::string CleanName = Base::Tools::getIdentifier(Name);

    // A new object takes no name the file gave another (sec 27.40 item 3);
    // one coming back under its id has its own.
    const FileHistory* history = id ? nullptr : d->history.get();

    // name in use?
    if (!d->objectMap.count(CleanName)
            && (!history || !history->objectIdOfName(CleanName))) {
        // if not, name is OK
        return CleanName;
    }
    else {
        // remove also trailing digits from clean name which is to avoid to create lengthy names
        // like 'Box001001'
        if (!testStatus(KeepTrailingDigits)) {
            std::string::size_type index = CleanName.find_last_not_of("0123456789");
            if (index+1 < CleanName.size()) {
                CleanName = CleanName.substr(0,index+1);
            }
        }

        // Hand the names over one at a time. Copying them into a vector
        // first is the whole cost of this call once a document holds
        // thousands of objects, and every one of those adds pays it: an
        // import of 13636 IFC products, all of them named 'Component', spent
        // most of addObject() here.
        auto it = d->objectMap.begin();
        using NameTable = std::unordered_map<std::string, long>;
        NameTable::const_iterator tableIt, tableEnd;
        if (history) {
            tableIt = history->objectNames().begin();
            tableEnd = history->objectNames().end();
        }
        auto next = [&]() -> const char * {
            if (it != d->objectMap.end())
                return (it++)->first.c_str();
            if (history && tableIt != tableEnd)
                return (tableIt++)->first.c_str();
            return nullptr;
        };
        return Base::Tools::getUniqueName(CleanName, next, 3);
    }
}

namespace {

/// The object ids a Document.xml's <Objects> list names (sec 27.47).
void collectObjectIds(const std::string& xml, std::unordered_set<long>& ids)
{
    std::size_t pos = xml.find("<Objects");
    const std::size_t end = xml.find("</Objects>", pos == std::string::npos ? 0 : pos);
    if (pos == std::string::npos || end == std::string::npos)
        return;
    while ((pos = xml.find("<Object ", pos)) != std::string::npos && pos < end) {
        const std::size_t close = xml.find('>', pos);
        const std::size_t id = xml.find(" id=\"", pos);
        if (id != std::string::npos && id < close)
            ids.insert(std::strtol(xml.c_str() + id + 5, nullptr, 10));
        pos = close;
    }
}

} // namespace

Document::CompactResult Document::compactFileState()
{
    // docs/TransactionLog.md sec 27.47. What the retained history can reach
    // is a version plus the ops after it, so an object alive anywhere in it
    // is in a version's object list or named by an op: those, and what the
    // documents of the file hold now, are the objects still referred to.
    CompactResult result;
    if (!getTransactionLog())
        return result;
    FileHistory& history = getFileHistory();
    TransactionLogCore& core = TransactionLogCore::of(history);
    TransactionStore& store = core.store();
    std::unordered_set<long> used;
    std::vector<Document*> docs = core.documents();
    if (std::find(docs.begin(), docs.end(), this) == docs.end())
        docs.push_back(this);
    for (auto doc : docs) {
        for (auto obj : doc->getObjects())
            used.insert(obj->getID());
    }
    for (long id : store.objectIdsInOps())
        used.insert(id);
    for (const auto& v : store.versions()) {
        for (const auto& e : store.manifest(v.num)) {
            if (e.entry != "Document.xml")
                continue;
            CapturedValue value;
            if (core.readValue(e.hash, value))
                collectObjectIds(value.fragment, used);
            else
                FC_WARN("compact: cannot read Document.xml of version " << v.num);
        }
    }
    const std::size_t geoBefore = history.lastGeoIds().size();
    const std::vector<long> gone = history.forgetObjects(used, result.names);
    result.geoIds = geoBefore - history.lastGeoIds().size();
    if (!gone.empty())
        store.removeObjectState(gone);
    history.setCompactEstimate(0);
    store.setMeta("compact_estimate", "0");

    // The strings nothing holds and no retained version or value uses
    // (sec 27.50 item 4): dropped, the counter kept, so no id is handed out
    // again.
    result.strings = _compactStrings();
    FC_LOG("compacted the file state of " << getName() << ": " << result.names << " names, "
           << result.geoIds << " geometry ids, " << result.strings << " strings");
    return result;
}

std::set<long> Document::_objectIdsOfRows(const std::vector<int64_t>& rows)
{
    std::set<long> ids;
    TransactionLog* log = getTransactionLog();
    if (!log)
        return ids;
    auto& store = log->store();
    for (int64_t seq : rows) {
        for (const auto& o : store.ops(seq)) {
            if (o.ckind == "obj")
                ids.insert(o.cid);
        }
    }
    return ids;
}

Document::CompactEstimate Document::_noteDroppedRows(const std::set<long>& named)
{
    // docs/TransactionLog.md sec 27.48.
    CompactEstimate estimate;
    TransactionLog* log = getTransactionLog();
    if (!log)
        return estimate;
    FileHistory& history = getFileHistory();
    TransactionLogCore& core = TransactionLogCore::of(history);
    TransactionStore& store = core.store();
    if (!named.empty()) {
        std::unordered_set<long> used;
        for (long id : store.objectIdsInOps())
            used.insert(id);
        std::vector<Document*> docs = core.documents();
        if (std::find(docs.begin(), docs.end(), this) == docs.end())
            docs.push_back(this);
        for (auto doc : docs) {
            for (auto obj : doc->getObjects())
                used.insert(obj->getID());
        }
        const auto& geo = history.lastGeoIds();
        for (long id : named) {
            if (used.count(id))
                continue;
            // Only what compaction could drop: an object the tables know.
            const std::string* name = history.objectNameOfId(id);
            const bool hasGeo = geo.count(id) != 0;
            if (!name && !hasGeo)
                continue;
            ++estimate.objects;
            // An entry's bytes: its text and its key, in either table -- a
            // key counted as 8 bytes everywhere, so the estimate is the same
            // on every platform.
            constexpr std::size_t key = 8;
            if (name)
                estimate.bytes += name->size() + key;
            if (hasGeo)
                estimate.bytes += 2 * key;
        }
    }
    estimate.totalBytes = history.compactEstimate() + estimate.bytes;
    history.setCompactEstimate(estimate.totalBytes);
    store.setMeta("compact_estimate", std::to_string(estimate.totalBytes));
    if (const StringHasherRef& hasher = history.hasher()) {
        estimate.strings = hasher->size() - hasher->count();
        const auto sizes = hasher->getStorageSize();
        estimate.stringBytes = sizes.total_size - sizes.referenced_size;
    }

    const long threshold = DocumentParams::getTransactionLogCompactSize();
    if (threshold > 0
            && estimate.totalBytes + estimate.stringBytes
                   >= static_cast<std::size_t>(threshold) * 1024) {
        compactFileState();
        estimate.compacted = true;
    }
    FC_LOG(getName() << ": " << estimate.objects << " object(s) left unreferenced ("
           << estimate.bytes << " bytes, " << estimate.totalBytes
           << " since the last compaction); " << estimate.strings << " strings unheld ("
           << estimate.stringBytes << " bytes)" << (estimate.compacted ? "; compacted" : ""));
    return estimate;
}

long Document::nextGeoId(const DocumentObject& obj, long floor) const
{
    if (obj.getID() <= 0)
        return floor + 1;
    if (d->history)
        return d->history->nextGeoId(obj.getID(), floor);
    long& last = d->lastGeoIds[obj.getID()];
    last = std::max(last, floor) + 1;
    return last;
}

std::string Document::getStandardObjectName(const char *Name, int d) const
{
    std::vector<App::DocumentObject*> mm = getObjects();
    std::vector<std::string> labels;
    labels.reserve(mm.size());

    for (auto it : mm) {
        std::string label = it->Label.getValue();
        labels.push_back(label);
    }
    return Base::Tools::getUniqueName(Name, labels, d);
}

std::vector<DocumentObject*> Document::getDependingObjects() const
{
    return getDependencyList(d->objectArray);
}

const std::vector<DocumentObject*> &Document::getObjects() const
{
    return d->objectArray;
}


std::vector<DocumentObject*> Document::getObjectsOfType(const Base::Type& typeId) const
{
    std::vector<DocumentObject*> Objects;
    for (auto it : d->objectArray) {
        if (it->getTypeId().isDerivedFrom(typeId))
            Objects.push_back(it);
    }
    return Objects;
}

std::vector< DocumentObject* > Document::getObjectsWithExtension(const Base::Type& typeId, bool derived) const {

    std::vector<DocumentObject*> Objects;
    for (auto it : d->objectArray) {
        if (it->hasExtension(typeId, derived))
            Objects.push_back(it);
    }
    return Objects;
}


std::vector<DocumentObject*> Document::findObjects(const Base::Type& typeId, const char* objname, const char* label) const
{
    boost::cmatch what;
    boost::regex rx_name, rx_label;

    if (objname)
        rx_name.set_expression(objname);

    if (label)
        rx_label.set_expression(label);

    std::vector<DocumentObject*> Objects;
    DocumentObject* found = nullptr;
    for (auto it : d->objectArray) {
        if (it->getTypeId().isDerivedFrom(typeId)) {
            found = it;

            if (!rx_name.empty() && !boost::regex_search(it->getNameInDocument(), what, rx_name))
                found = nullptr;

            if (!rx_label.empty() && !boost::regex_search(it->Label.getValue(), what, rx_label))
                found = nullptr;

            if (found)
                Objects.push_back(found);
        }
    }
    return Objects;
}

int Document::countObjectsOfType(const Base::Type& typeId) const
{
    int ct=0;
    for (const auto & it : d->objectMap) {
        if (it.second->getTypeId().isDerivedFrom(typeId))
            ct++;
    }

    return ct;
}

PyObject * Document::getPyObject()
{
    return Py::new_reference_to(d->DocumentPythonObject);
}

std::vector<App::DocumentObject*> Document::getRootObjects() const
{
    std::vector < App::DocumentObject* > ret;

    for (auto objectIt : d->objectArray) {
        if (objectIt->getInList().empty())
            ret.push_back(objectIt);
    }

    return ret;
}

std::vector<App::DocumentObject*> Document::getRootObjectsIgnoreLinks() const
{
    std::vector < App::DocumentObject* > ret;

    for (auto objectIt : d->objectArray) {
        const auto &inList = objectIt->getInList();
        bool noParents = inList.empty()
            || std::all_of(inList.begin(), inList.end(), [](DocumentObject *obj) {
                   return obj->isDerivedFrom<App::Link>();
               });
        if (noParents)
            ret.push_back(objectIt);
    }

    return ret;
}

void DocumentP::findAllPathsAt(const std::vector <Node> &all_nodes, size_t id,
                                std::vector <Path> &all_paths, Path tmp)
{
    if (std::find(tmp.begin(), tmp.end(), id) != tmp.end()) {
        Path tmp2(tmp);
        tmp2.push_back(id);
        all_paths.push_back(tmp2);
        return; // a cycle
    }

    tmp.push_back(id);
    if (all_nodes[id].empty()) {
        all_paths.push_back(tmp);
        return;
    }

    for (size_t i=0; i < all_nodes[id].size(); i++) {
        Path tmp2(tmp);
        findAllPathsAt(all_nodes, all_nodes[id][i], all_paths, tmp2);
    }
}

std::vector<std::list<App::DocumentObject*> >
Document::getPathsByOutList(const App::DocumentObject* from, const App::DocumentObject* to) const
{
    std::map<const DocumentObject*, size_t> indexMap;
    for (size_t i=0; i<d->objectArray.size(); ++i) {
        indexMap[d->objectArray[i]] = i;
    }

    std::vector <Node> all_nodes(d->objectArray.size());
    for (size_t i=0; i<d->objectArray.size(); ++i) {
        DocumentObject* obj = d->objectArray[i];
        std::vector<DocumentObject*> outList = obj->getOutList();
        for (auto it : outList) {
            all_nodes[i].push_back(indexMap[it]);
        }
    }

    std::vector<std::list<App::DocumentObject*> > array;
    if (from == to)
        return array;

    size_t index_from = indexMap[from];
    size_t index_to = indexMap[to];
    Path tmp;
    std::vector<Path> all_paths;
    DocumentP::findAllPathsAt(all_nodes, index_from, all_paths, tmp);

    for (const Path& it : all_paths) {
        Path::const_iterator jt = std::find(it.begin(), it.end(), index_to);
        if (jt != it.end()) {
            std::list<App::DocumentObject*> path;
            for (Path::const_iterator kt = it.begin(); kt != jt; ++kt) {
                path.push_back(d->objectArray[*kt]);
            }

            path.push_back(d->objectArray[*jt]);
            array.push_back(path);
        }
    }

    // remove duplicates
    std::sort(array.begin(), array.end());
    array.erase(std::unique(array.begin(), array.end()), array.end());

    return array;
}

bool Document::mustExecute() const
{
    if(PropertyXLink::hasXLink(this)) {
        bool touched = false;
        _buildDependencyList(d->objectArray,false,nullptr,nullptr,nullptr,&touched);
        return touched;
    }

    for (auto It : d->objectArray) {
        if (It->isTouched() || It->mustExecute()==1)
            return true;
    }
    return false;
}

long Document::getLastObjectId() const {
    return d->lastObjectId;
}

void Document::setLastObjectId(long id) {
    d->lastObjectId = id;
}

void Document::afterImport(App::DocumentObject *obj) {
    if (obj->Label.getStrValue() == "Unnamed")
        obj->Label.setValue(obj->getNameInDocument());
    obj->onDocumentRestored();
}

std::pair<long, long> Document::treeRanks() const
{
    if (d->objectArray.empty())
        return std::make_pair(0,0);
    if (d->treeRankRevision != d->revision) {
        d->treeRankRevision = d->revision;
        d->treeRanks.second = d->treeRanks.first = d->objectArray.front()->TreeRank.getValue();
        for (auto obj : d->objectArray) {
            long r = obj->TreeRank.getValue();
            if (r < d->treeRanks.first)
                d->treeRanks.first = r;
            else if (r > d->treeRanks.second)
                d->treeRanks.second = r;
        }
    }
    return d->treeRanks;
}

void Document::reorderObjects(const std::vector<DocumentObject*> &_objs, DocumentObject *before)
{
    const char *msg = "Object does not belong to this document";
    if (!before || before->getDocument() != this)
        THROWM(Base::RuntimeError, msg)
        
    for (auto obj : _objs) {
        if (!obj || obj->getDocument() != this)
            THROWM(Base::RuntimeError, msg)
    }
    auto objs = _objs;
    objs.erase(std::unique(objs.begin(), objs.end()), objs.end());
    long beforeRank = before->TreeRank.getValue();
    for (auto obj : d->objectArray) {
        long rank = obj->TreeRank.getValue();
        if (rank >= beforeRank)
            obj->TreeRank.setValue(rank + (long)objs.size());
    }
    for (auto obj : objs)
        obj->TreeRank.setValue(beforeRank++);
}
