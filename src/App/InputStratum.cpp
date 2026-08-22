/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef _PreComp_
#include <algorithm>
#include <deque>
#include <sstream>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>

#include "Document.h"
#include "DocumentObject.h"
#include "Expression.h"
#include "ExpressionParser.h"
#include "InputStratum.h"
#include "ObjectIdentifier.h"
#include "Property.h"
#include "PropertyExpressionEngine.h"

FC_LOG_LEVEL_INIT("App", true);

using namespace App;

namespace
{

constexpr std::size_t NotANode = static_cast<std::size_t>(-1);

// One binding whose target property is marked Property::Input.
struct Node
{
    DocumentObject* obj = nullptr;
    ObjectIdentifier path;
    Property* prop = nullptr;
    // Nodes that must be evaluated before this one.
    std::vector<std::size_t> deps;
    // The input properties this binding reads, whether or not anything writes
    // them. Seeds the dirty set, and is the walk used by the cycle check.
    std::vector<InputStratum::PropRef> reads;
    bool dirty = false;
};

const char* propNameOf(const Property* prop)
{
    const char* name = prop ? prop->getName() : nullptr;
    return name ? name : "";
}

// Is this property marked input, wherever it lives? The closure rule of
// section 4 asks this much and no more: what makes a reference safe to read
// from an input property is the enforced promise, not which document it is in.
// Which document it is in decides only whether the ordering edge may go, and
// that is what isInputRef() adds.
bool isInputProp(const InputStratum::PropRef& ref)
{
    if (!ref.first || ref.second.empty()) {
        return false;
    }
    return ref.first->isInputProperty(ref.second);
}

// A double binding is a cycle by construction -- A reads B and B is written
// back from A -- so a topologically ordered stratum has nowhere to put it. It
// keeps the eager signal driven fixed point it has today. See section 9.
//
// This cannot be told apart at the identifier level: FunctionExpression and
// CallableExpression both mark HREF, HIDDEN_REF and DBIND arguments with the
// same hidden flag. The binding as a whole is what answers it.
bool isDoubleBinding(const Expression* expr)
{
    return VariableExpression::isDoubleBinding(expr) != nullptr;
}

}  // namespace

class InputStratum::Private
{
public:
    explicit Private(Document* document)
        : doc(document)
    {}

    bool reaches(const PropRef& from, const PropRef& target, std::set<PropRef>& seen) const;

    Document* doc;
    std::vector<Node> nodes;
    // Topological order over nodes, filled by build().
    std::vector<std::size_t> order;
    // Input property -> the nodes that write it. An input property with no
    // binding is a source and does not appear here at all.
    std::map<PropRef, std::vector<std::size_t>> nodeOf;
    // Input property -> the nodes that read it.
    std::map<PropRef, std::vector<std::size_t>> inRefs;
    // Input property -> the objects that read it from outside the stratum.
    // These stand in for the dependency edge that was dropped.
    std::map<PropRef, std::set<DocumentObject*>> outRefs;
    // The union of the two key sets above, so the seeding pass has one thing
    // to walk.
    std::set<PropRef> readInputs;
};

bool InputStratum::Private::reaches(const PropRef& from,
                                    const PropRef& target,
                                    std::set<PropRef>& seen) const
{
    if (from == target) {
        return true;
    }
    if (!seen.insert(from).second) {
        return false;
    }
    auto it = nodeOf.find(from);
    if (it == nodeOf.end()) {
        return false;
    }
    for (auto n : it->second) {
        for (const auto& r : nodes[n].reads) {
            if (reaches(r, target, seen)) {
                return true;
            }
        }
    }
    return false;
}

InputStratum::InputStratum(Document* doc)
    : d(new Private(doc))
{}

InputStratum::~InputStratum()
{
    delete d;
}

bool InputStratum::empty() const
{
    return d->nodes.empty() && d->readInputs.empty();
}

void InputStratum::collectDeps(const Expression* expr,
                               std::map<DocumentObject*, bool>& deps,
                               std::vector<std::string>* labels,
                               std::map<PropRef, bool>* refs)
{
    if (!expr) {
        return;
    }
    for (const auto& v : expr->getIdentifiers()) {
        const bool hidden = v.second;
        std::vector<std::string> strings;
        for (const auto& dep : v.first.getDep(true, labels ? &strings : nullptr)) {
            auto obj = dep.first;
            if (obj && !obj->testStatus(ObjectStatus::Remove)) {
                if (labels) {
                    std::copy(strings.begin(), strings.end(), std::back_inserter(*labels));
                }
                // An object referenced normally anywhere is a normal
                // dependency, whatever else references it.
                auto res = deps.insert(std::make_pair(obj, hidden));
                if (!hidden || res.second) {
                    res.first->second = hidden;
                }
                if (refs) {
                    if (dep.second.empty()) {
                        // A whole object reference. No input property can
                        // answer it, but record it so the closure check can
                        // say so.
                        auto r = refs->emplace(PropRef(obj, std::string()), hidden);
                        if (!hidden) {
                            r.first->second = false;
                        }
                    }
                    for (const auto& propName : dep.second) {
                        auto r = refs->emplace(PropRef(obj, propName), hidden);
                        if (!hidden) {
                            r.first->second = false;
                        }
                    }
                }
            }
            strings.clear();
        }
    }
}

void InputStratum::collectPropRefs(const Expression* expr, std::map<PropRef, bool>& refs)
{
    std::map<DocumentObject*, bool> deps;
    collectDeps(expr, deps, nullptr, &refs);
}

bool InputStratum::isInputRef(const DocumentObject* reader, const PropRef& ref)
{
    if (!reader || !isInputProp(ref)) {
        return false;
    }
    // Cross document references keep the ordinary edge and with it the
    // revision based staleness check -- see docs/InputProperties.md section 8.
    return reader->getDocument() == ref.first->getDocument();
}

bool InputStratum::build(std::string& error, DocumentObject** culprit)
{
    d->nodes.clear();
    d->order.clear();
    d->nodeOf.clear();
    d->inRefs.clear();
    d->outRefs.clear();
    d->readInputs.clear();

    if (!d->doc) {
        return true;
    }

    const auto& objects = d->doc->getObjects();

    // Pass one: every binding whose target is an input property is a node.
    // Keyed by expression pointer, which is what pass two has in hand.
    std::map<const Expression*, std::size_t> nodeByExpr;
    for (auto obj : objects) {
        if (!obj || !obj->isAttachedToDocument() || obj->ExpressionEngine.numExpressions() == 0) {
            continue;
        }
        for (const auto& v : obj->ExpressionEngine.getExpressions()) {
            if (!v.second || isDoubleBinding(v.second)) {
                continue;
            }
            auto prop = v.first.getProperty();
            if (!prop || !prop->testStatus(Property::Input)) {
                continue;
            }
            Node node;
            node.obj = obj;
            node.path = v.first;
            node.prop = prop;
            nodeByExpr[v.second] = d->nodes.size();
            d->nodeOf[PropRef(obj, propNameOf(prop))].push_back(d->nodes.size());
            d->nodes.push_back(std::move(node));
        }
    }

    // Pass two: edges between nodes, and the propagation record for every
    // binding in the document that reads an input property.
    for (auto obj : objects) {
        if (!obj || !obj->isAttachedToDocument() || obj->ExpressionEngine.numExpressions() == 0) {
            continue;
        }
        for (const auto& v : obj->ExpressionEngine.getExpressions()) {
            if (!v.second || isDoubleBinding(v.second)) {
                continue;
            }
            auto nit = nodeByExpr.find(v.second);
            const std::size_t self = nit == nodeByExpr.end() ? NotANode : nit->second;

            std::map<PropRef, bool> refs;
            collectPropRefs(v.second, refs);

            for (const auto& r : refs) {
                if (self != NotANode && !isInputProp(r.first)) {
                    // Section 4: input-ness is closed under expression
                    // composition. Reaching this means the closure was broken
                    // after the fact, by clearing the status on a property the
                    // binding reads. Loud, because the value can now move
                    // during the object phase.
                    FC_WARN("Input property " << d->nodes[self].prop->getFullName()
                                              << " reads non-input "
                                              << r.first.first->getFullName() << "."
                                              << r.first.second);
                }
                // The propagation record stands in for a dropped edge, so it
                // covers exactly the references whose edge was dropped.
                if (!isInputRef(obj, r.first)) {
                    continue;
                }
                d->readInputs.insert(r.first);
                if (self == NotANode) {
                    d->outRefs[r.first].insert(obj);
                    continue;
                }
                d->nodes[self].reads.push_back(r.first);
                d->inRefs[r.first].push_back(self);
                auto wit = d->nodeOf.find(r.first);
                if (wit == d->nodeOf.end()) {
                    continue;
                }
                for (auto n : wit->second) {
                    if (n != self) {
                        d->nodes[self].deps.push_back(n);
                    }
                }
            }
        }
    }

    // Topological sort. Kahn, so that a cycle leaves the unordered remainder
    // behind for the error message to name.
    std::vector<std::size_t> indegree(d->nodes.size(), 0);
    std::vector<std::vector<std::size_t>> successors(d->nodes.size());
    for (std::size_t i = 0; i < d->nodes.size(); ++i) {
        auto& deps = d->nodes[i].deps;
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
        indegree[i] = deps.size();
        for (auto dep : deps) {
            successors[dep].push_back(i);
        }
    }

    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < d->nodes.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push_back(i);
        }
    }
    while (!ready.empty()) {
        auto i = ready.front();
        ready.pop_front();
        d->order.push_back(i);
        for (auto s : successors[i]) {
            if (--indegree[s] == 0) {
                ready.push_back(s);
            }
        }
    }

    if (d->order.size() != d->nodes.size()) {
        std::ostringstream ss;
        ss << "cyclic dependency among input properties:";
        for (std::size_t i = 0; i < d->nodes.size(); ++i) {
            if (indegree[i] != 0) {
                ss << " " << d->nodes[i].prop->getFullName();
                if (culprit && !*culprit) {
                    *culprit = d->nodes[i].obj;
                }
            }
        }
        error = ss.str();
        d->order.clear();
        return false;
    }

    return true;
}

bool InputStratum::evaluate(bool force,
                            std::vector<DocumentObject*>& referrers,
                            std::string& error,
                            DocumentObject** culprit)
{
    if (d->nodes.empty() && d->readInputs.empty()) {
        return true;
    }

    std::set<DocumentObject*> pushed(referrers.begin(), referrers.end());

    auto markReaders = [&](const PropRef& key) {
        auto it = d->inRefs.find(key);
        if (it != d->inRefs.end()) {
            for (auto n : it->second) {
                d->nodes[n].dirty = true;
            }
        }
        auto ot = d->outRefs.find(key);
        if (ot != d->outRefs.end()) {
            for (auto obj : ot->second) {
                if (pushed.insert(obj).second) {
                    referrers.push_back(obj);
                }
            }
        }
    };

    // An input property carrying a pending change pushes its readers before
    // anything is evaluated. This is precisely what the dropped ordering edge
    // no longer does.
    for (const auto& key : d->readInputs) {
        auto prop = key.first->getPropertyByName(key.second.c_str());
        if (prop && prop->isTouched()) {
            markReaders(key);
        }
    }

    // A node whose owner is up for recompute is evaluated regardless. Before
    // this phase existed those bindings ran from _recomputeFeature() on every
    // pass over the object, so this is the conservative side of the old
    // behaviour, and it is what makes a restore re-evaluate the stratum.
    for (auto& node : d->nodes) {
        if (force || node.obj->isTouched() || node.obj->testStatus(ObjectStatus::Enforce)
            || (node.prop && node.prop->isTouched())) {
            node.dirty = true;
        }
    }

    for (auto idx : d->order) {
        auto& node = d->nodes[idx];
        if (!node.dirty) {
            continue;
        }
        bool failed = false;
        try {
            node.obj->ExpressionEngine.executeBinding(node.path);
        }
        catch (Base::Exception& e) {
            // Base::Exception is not a std::exception here, hence the pair.
            error = e.what();
            failed = true;
        }
        catch (std::exception& e) {
            error = e.what();
            failed = true;
        }
        if (failed) {
            std::ostringstream ss;
            ss << "Failed to evaluate input binding " << node.path.toString() << " on "
               << node.obj->getFullName() << ": " << error;
            error = ss.str();
            if (culprit) {
                *culprit = node.obj;
            }
            return false;
        }
        if (node.prop && node.prop->isTouched()) {
            markReaders(PropRef(node.obj, propNameOf(node.prop)));
        }
    }

    return true;
}

std::vector<InputStratum::PropRef> InputStratum::referrersOf(DocumentObject* obj,
                                                             const std::string& propName) const
{
    std::vector<PropRef> res;
    PropRef key(obj, propName);
    auto it = d->inRefs.find(key);
    if (it != d->inRefs.end()) {
        for (auto n : it->second) {
            res.emplace_back(d->nodes[n].obj, propNameOf(d->nodes[n].prop));
        }
    }
    auto ot = d->outRefs.find(key);
    if (ot != d->outRefs.end()) {
        for (auto referrer : ot->second) {
            res.emplace_back(referrer, std::string());
        }
    }
    return res;
}

std::vector<InputStratum::PropRef> InputStratum::findReferrers(DocumentObject* obj,
                                                               const std::string& propName)
{
    std::vector<PropRef> res;
    if (!obj || propName.empty() || !obj->getDocument()) {
        return res;
    }

    // Same document only. A cross document reference keeps its ordinary edge
    // whatever the status says -- section 8 -- so nothing over there can go
    // stale when the status changes.
    const PropRef target(obj, propName);
    for (auto reader : obj->getDocument()->getObjects()) {
        if (!reader || !reader->isAttachedToDocument()
            || reader->ExpressionEngine.numExpressions() == 0) {
            continue;
        }
        for (const auto& v : reader->ExpressionEngine.getExpressions()) {
            if (!v.second) {
                continue;
            }
            std::map<PropRef, bool> refs;
            collectPropRefs(v.second, refs);
            if (refs.find(target) != refs.end()) {
                res.emplace_back(reader, propNameOf(v.first.getProperty()));
            }
        }
    }
    return res;
}

void InputStratum::refreshReferrers(DocumentObject* obj, const std::string& propName)
{
    std::set<DocumentObject*> readers;
    for (const auto& ref : findReferrers(obj, propName)) {
        readers.insert(ref.first);
    }
    for (auto reader : readers) {
        // Recomputing the dependency set is all this needs; the values have
        // not moved, so nothing is touched and no recompute is provoked.
        reader->ExpressionEngine.refreshDependencies();
    }
}

std::string InputStratum::checkBinding(DocumentObject* owner,
                                       const ObjectIdentifier& path,
                                       const Expression* expr)
{
    if (!owner || !expr || !owner->getDocument()) {
        return {};
    }
    auto prop = path.getProperty();
    if (!prop || !prop->testStatus(Property::Input) || isDoubleBinding(expr)) {
        // Not a stratum node. Nothing in this section applies, and in
        // particular a plain binding that merely reads an input property is
        // free to do so, as is a double binding, which stays outside the
        // stratum entirely.
        return {};
    }

    std::map<PropRef, bool> refs;
    collectPropRefs(expr, refs);
    if (refs.empty()) {
        return {};
    }

    const PropRef target(owner, propNameOf(prop));

    // Section 4, the closure rule: an input property may only read input
    // properties, or its value could move during the object phase and the
    // stratification would not hold. A hiddenref() to an input property counts
    // -- the wrapper is redundant there -- and a hiddenref() to a computed one
    // does not, which is why the hidden flag is not consulted here.
    for (const auto& r : refs) {
        if (isInputProp(r.first)) {
            continue;
        }
        std::ostringstream ss;
        ss << "input property " << prop->getFullName() << " may only reference input properties, "
           << "but this binding reads ";
        if (r.first.second.empty()) {
            ss << "the whole object " << r.first.first->getFullName();
        }
        else {
            ss << r.first.first->getFullName() << "." << r.first.second;
        }
        return ss.str();
    }

    // Section 5.1: these references have had their object level edge removed,
    // which is the point, so the cycle check in validateExpression() cannot
    // see them. Walk the sub-DAG instead.
    InputStratum stratum(owner->getDocument());
    std::string error;
    if (!stratum.build(error)) {
        return error;
    }
    for (const auto& r : refs) {
        std::set<PropRef> seen;
        if (stratum.d->reaches(r.first, target, seen)) {
            std::ostringstream ss;
            ss << "cyclic reference among input properties: " << prop->getFullName()
               << " reaches itself through " << r.first.first->getFullName() << "."
               << r.first.second;
            return ss.str();
        }
    }

    return {};
}
