/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <BOPAlgo_CellsBuilder.hxx>
#include <BRep_Builder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <gp_Pnt.hxx>
#include <gp_Quaternion.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <unordered_map>
#endif

#include <App/Document.h>
#include <App/FileBlobManager.h>
#include <App/StringHasher.h>
#include <App/TransactionLog.h>
#include <App/TransactionValue.h>
#include <Base/Exception.h>

#include "PropertyTopoShape.h"
#include "ShapeCongruence.h"
#include "ShapeDiff.h"
#include "TopoShape.h"

namespace Part
{

namespace
{

const TopAbs_ShapeEnum Types[3] = {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX};
const char* const TypeNames[3] = {"Face", "Edge", "Vertex"};

/// Adds its lifetime to a running total
class Stopwatch
{
public:
    explicit Stopwatch(double& into)
        : _into(into)
        , _start(std::chrono::steady_clock::now())
    {}
    ~Stopwatch()
    {
        _into += std::chrono::duration<double>(std::chrono::steady_clock::now() - _start).count();
    }

private:
    double& _into;
    std::chrono::steady_clock::time_point _start;
};

int slotOf(TopAbs_ShapeEnum type)
{
    switch (type) {
        case TopAbs_FACE:
            return 0;
        case TopAbs_EDGE:
            return 1;
        case TopAbs_VERTEX:
            return 2;
        default:
            return -1;
    }
}

int slotOf(const char* typeName)
{
    for (int t = 0; t < 3; ++t) {
        if (std::strcmp(typeName, TypeNames[t]) == 0) {
            return t;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Names: spelling out, and reading as steps

/// A name spelled out past this is left to the stages after the names. A
/// name grows with every feature its element passed and with every name an
/// operation lists, and a diff is not worth a gigabyte.
const std::size_t MaxSpelled = 1 << 16;
const int MaxDepth = 200;

bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

int hexValue(char c)
{
    return c <= '9' ? c - '0' : c - 'a' + 10;
}

/// "#id" or "#id:index" at \a pos, as StringID::toString() writes it
bool parseId(const std::string& text, std::size_t pos, long& id, int& index, std::size_t& end)
{
    std::size_t i = pos + 1;
    std::size_t digits = i;
    id = 0;
    while (i < text.size() && isHex(text[i])) {
        id = id * 16 + hexValue(text[i]);
        ++i;
    }
    if (i == digits) {
        return false;
    }
    index = 0;
    if (i + 1 < text.size() && text[i] == ':' && isHex(text[i + 1])) {
        std::size_t j = i + 1;
        int value = 0;
        while (j < text.size() && isHex(text[j])) {
            value = value * 16 + hexValue(text[j]);
            ++j;
        }
        index = value;
        i = j;
    }
    end = i;
    return true;
}

/// One step's end: ";:H" then the tag, the length of the operation before it
/// and the element's type -- ";:H4eb:7,F", ";:H-4f9,F", ";:H,F"
struct Hop
{
    std::size_t end = 0;
    bool negative = false;
    bool tagged = false;
    long tag = 0;
    char type = 0;
};

bool parseHop(const std::string& text, std::size_t pos, Hop& hop)
{
    std::size_t i = pos + 3;
    hop.negative = i < text.size() && text[i] == '-';
    if (hop.negative) {
        ++i;
    }
    std::size_t digits = i;
    long tag = 0;
    while (i < text.size() && isHex(text[i])) {
        tag = tag * 16 + hexValue(text[i]);
        ++i;
    }
    hop.tagged = i > digits;
    hop.tag = tag;
    if (i < text.size() && text[i] == ':') {
        ++i;
        std::size_t length = i;
        while (i < text.size() && isHex(text[i])) {
            ++i;
        }
        if (i == length) {
            return false;
        }
    }
    if (i + 1 >= text.size() || text[i] != ',' || text[i + 1] < 'A' || text[i + 1] > 'Z') {
        return false;
    }
    hop.type = text[i + 1];
    hop.end = i + 2;
    return true;
}

bool isHopAt(const std::string& text, std::size_t pos)
{
    return text.compare(pos, 3, ";:H") == 0;
}

/// Reads names through one string table
class Speller
{
public:
    explicit Speller(const App::StringHasherRef& hasher)
        : _hasher(hasher)
    {}

    /// The name with every id in it replaced by what it stands for. False
    /// where that is longer than a diff should carry.
    bool spell(const std::string& text, std::string& out, int depth = 0) const
    {
        if (depth > MaxDepth) {
            return false;
        }
        std::size_t i = 0;
        while (i < text.size()) {
            std::size_t at = text.find('#', i);
            if (at == std::string::npos) {
                out.append(text, i, std::string::npos);
                break;
            }
            out.append(text, i, at - i);
            long id = 0;
            int index = 0;
            std::size_t end = 0;
            if (!_hasher || !parseId(text, at, id, index, end)) {
                out += '#';
                i = at + 1;
                continue;
            }
            App::StringIDRef sid = _hasher->getID(id, index);
            if (!sid || sid.isHashed() || sid.isBinary()) {
                out.append(text, at, end - at);
            }
            else if (!spell(sid.dataToText(), out, depth + 1)) {
                return false;
            }
            if (out.size() > MaxSpelled) {
                return false;
            }
            i = end;
        }
        return out.size() <= MaxSpelled;
    }

    /// The objects whose shapes the element of a name was in: the tags of its
    /// steps, without spelling it out
    void tags(const std::string& text, std::set<long>& out, int depth = 0)
    {
        if (depth > MaxDepth) {
            return;
        }
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == ';' && isHopAt(text, i)) {
                Hop hop;
                if (parseHop(text, i, hop)) {
                    if (hop.tagged && hop.tag) {
                        out.insert(hop.tag);
                    }
                    i = hop.end;
                    continue;
                }
            }
            else if (text[i] == '#' && _hasher) {
                long id = 0;
                int index = 0;
                std::size_t end = 0;
                if (parseId(text, i, id, index, end)) {
                    const std::vector<long>& inner = tagsOfId(id, depth);
                    out.insert(inner.begin(), inner.end());
                    i = end;
                    continue;
                }
            }
            ++i;
        }
    }

private:
    const std::vector<long>& tagsOfId(long id, int depth)
    {
        auto it = _tags.find(id);
        if (it != _tags.end()) {
            return it->second;
        }
        // In before the walk, so that a table that refers to itself ends
        _tags[id];
        std::set<long> found;
        App::StringIDRef sid = _hasher->getID(id);
        if (sid && !sid.isHashed() && !sid.isBinary()) {
            tags(sid.dataToText(), found, depth + 1);
        }
        auto& slot = _tags[id];
        slot.assign(found.begin(), found.end());
        return slot;
    }

    App::StringHasherRef _hasher;
    std::unordered_map<long, std::vector<long>> _tags;
};

std::string withoutSteps(const std::string& text, const std::set<long>& tags, int depth);

/// An operation's text: the names it lists are read as names, and sorted
std::string withoutStepsIn(const std::string& op, const std::set<long>& tags, int depth)
{
    std::size_t open = op.find('(');
    if (open == std::string::npos) {
        return op;
    }
    std::size_t close = std::string::npos;
    int level = 0;
    for (std::size_t i = open; i < op.size(); ++i) {
        if (op[i] == '(') {
            ++level;
        }
        else if (op[i] == ')' && --level == 0) {
            close = i;
            break;
        }
    }
    if (close == std::string::npos) {
        return op;
    }
    std::vector<std::string> listed;
    level = 0;
    std::size_t from = open + 1;
    for (std::size_t i = open + 1; i < close; ++i) {
        if (op[i] == '(') {
            ++level;
        }
        else if (op[i] == ')') {
            --level;
        }
        else if (op[i] == '|' && level == 0) {
            listed.push_back(withoutSteps(op.substr(from, i - from), tags, depth + 1));
            from = i + 1;
        }
    }
    listed.push_back(withoutSteps(op.substr(from, close - from), tags, depth + 1));
    std::sort(listed.begin(), listed.end());

    std::string out = op.substr(0, open);
    out += '(';
    for (std::size_t i = 0; i < listed.size(); ++i) {
        if (i) {
            out += '|';
        }
        out += listed[i];
    }
    out += ')';
    out += withoutStepsIn(op.substr(close + 1), tags, depth);
    return out;
}

std::string withoutSteps(const std::string& text, const std::set<long>& tags, int depth)
{
    if (depth > 64) {
        return text;
    }
    struct Found
    {
        std::size_t start;
        Hop hop;
    };
    std::vector<Found> hops;
    int level = 0;
    for (std::size_t i = 0; i < text.size();) {
        char c = text[i];
        if (c == '(') {
            ++level;
        }
        else if (c == ')') {
            if (level) {
                --level;
            }
        }
        else if (level == 0 && c == ';' && isHopAt(text, i)) {
            Hop hop;
            if (parseHop(text, i, hop)) {
                hops.push_back({i, hop});
                i = hop.end;
                continue;
            }
        }
        ++i;
    }
    if (hops.empty()) {
        return text;
    }

    // What is before the first step is the root and what was done to it
    std::string first = text.substr(0, hops.front().start);
    std::size_t semi = first.find(';');
    std::string root = semi == std::string::npos ? first : first.substr(0, semi);

    // An element the object made has its root there, and nothing before to
    // fall back on: its steps stay, and the name is the one side's alone.
    const bool bornThere = hops.front().hop.tagged && tags.count(hops.front().hop.tag);

    struct Step
    {
        std::string op;
        Hop hop;
    };
    std::vector<Step> steps;
    for (std::size_t k = 0; k < hops.size(); ++k) {
        std::string op;
        if (k == 0) {
            op = semi == std::string::npos ? std::string() : first.substr(semi);
        }
        else {
            op = text.substr(hops[k - 1].hop.end, hops[k].start - hops[k - 1].hop.end);
        }
        const Hop& hop = hops[k].hop;
        // The object going out did the operation of the step before, and the
        // one written here was done to what it gave: that one stays, with the
        // tag of the step before.
        if (!bornThere && hop.tagged && !hop.negative && tags.count(hop.tag) && !steps.empty()) {
            steps.back().op = op;
        }
        else {
            steps.push_back({op, hop});
        }
    }

    static const char digits[] = "0123456789abcdef";
    std::string out = root;
    for (const auto& step : steps) {
        out += withoutStepsIn(step.op, tags, depth);
        out += ";:H";
        if (step.hop.negative) {
            out += '-';
        }
        if (step.hop.tagged) {
            std::string hex;
            long tag = step.hop.tag;
            do {
                hex += digits[tag & 0xf];
                tag >>= 4;
            } while (tag);
            out.append(hex.rbegin(), hex.rend());
        }
        out += ',';
        out += step.hop.type;
    }
    out += withoutStepsIn(text.substr(hops.back().hop.end), tags, depth);
    return out;
}

// ---------------------------------------------------------------------------
// One shape, its elements and their names

struct Side
{
    explicit Side(const TopoShape& s)
        : shape(s)
        , speller(s.Hasher)
    {
        for (int t = 0; t < 3; ++t) {
            TopExp::MapShapes(s.getShape(), Types[t], subs[t]);
            names[t].resize(subs[t].Extent() + 1);
        }
        for (const auto& element : s.getElementMap()) {
            int t = slotOf(element.index.getType());
            int i = element.index.getIndex();
            if (t < 0 || i <= 0 || i >= static_cast<int>(names[t].size())) {
                continue;
            }
            if (names[t][i].empty()) {
                names[t][i] = element.name.toString();
            }
        }
    }

    int count(int t) const
    {
        return subs[t].Extent();
    }

    /// The faces around each element: for a face those it shares an edge
    /// with, for an edge or a vertex those it is on
    const std::vector<std::vector<int>>& around(int t)
    {
        auto& out = _around[t];
        if (!out.empty()) {
            return out;
        }
        out.resize(count(t) + 1);
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(shape.getShape(), t == 0 ? TopAbs_EDGE : Types[t],
                                      TopAbs_FACE, faces);
        for (int k = 1; k <= faces.Extent(); ++k) {
            std::vector<int> on;
            for (const TopoDS_Shape& ancestor : faces(k)) {
                if (int face = subs[0].FindIndex(ancestor)) {
                    on.push_back(face);
                }
            }
            if (t == 0) {
                for (int x : on) {
                    for (int y : on) {
                        if (x != y) {
                            out[x].push_back(y);
                        }
                    }
                }
            }
            else if (int x = subs[t].FindIndex(faces.FindKey(k))) {
                out[x].insert(out[x].end(), on.begin(), on.end());
            }
        }
        for (auto& list : out) {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }
        return out;
    }

    const TopoShape& shape;
    Speller speller;
    TopTools_IndexedMapOfShape subs[3];
    /// By index, as stored; empty where the element has no name
    std::vector<std::string> names[3];

private:
    std::vector<std::vector<int>> _around[3];
};

/// Which element of A is which of B, for one type
struct Pairing
{
    Pairing(int na, int nb)
        : ab(na + 1, 0)
        , ba(nb + 1, 0)
        , by(na + 1, ShapeDiff::By::None)
    {}

    void pair(int a, int b, ShapeDiff::By how)
    {
        ab[a] = b;
        ba[b] = a;
        by[a] = how;
    }

    std::vector<int> ab;
    std::vector<int> ba;
    std::vector<ShapeDiff::By> by;
};

/// Pairs what has one key on each side and no more
void pairByKey(Pairing& pairing,
               const std::vector<std::string>& keysA,
               const std::vector<std::string>& keysB,
               ShapeDiff::By how)
{
    std::unordered_map<std::string, std::pair<int, int>> inB;  // key -> an index, how many
    for (std::size_t j = 1; j < keysB.size(); ++j) {
        if (keysB[j].empty() || pairing.ba[j]) {
            continue;
        }
        auto& slot = inB[keysB[j]];
        slot.first = static_cast<int>(j);
        ++slot.second;
    }
    std::unordered_map<std::string, int> inA;
    for (std::size_t i = 1; i < keysA.size(); ++i) {
        if (!keysA[i].empty() && !pairing.ab[i]) {
            ++inA[keysA[i]];
        }
    }
    for (std::size_t i = 1; i < keysA.size(); ++i) {
        if (keysA[i].empty() || pairing.ab[i] || inA[keysA[i]] != 1) {
            continue;
        }
        auto it = inB.find(keysA[i]);
        if (it != inB.end() && it->second.second == 1) {
            pairing.pair(static_cast<int>(i), it->second.first, how);
        }
    }
}

// ---------------------------------------------------------------------------
// Whether a pair is as it was

/// What of an element is compared: its corners, a point of each of its
/// edges -- three of an edge's own, so that an arc is not a chord -- and how
/// many edges it has
struct Measure
{
    std::vector<gp_Pnt> points;
    std::vector<gp_Pnt> mids;
    int edges = 0;
};

void sample(const TopoDS_Edge& edge, int count, std::vector<gp_Pnt>& out)
{
    if (BRep_Tool::Degenerated(edge)) {
        return;
    }
    try {
        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        for (int i = 1; i <= count; ++i) {
            out.push_back(curve.Value(first + (last - first) * i / (count + 1)));
        }
    }
    catch (const Standard_Failure&) {
        // An edge with no curve has its ends to say it
    }
}

Measure measure(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    Measure out;
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    out.points.reserve(vertices.Extent());
    for (int i = 1; i <= vertices.Extent(); ++i) {
        out.points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));
    }
    if (type == TopAbs_EDGE) {
        out.edges = 1;
        sample(TopoDS::Edge(shape), 3, out.mids);
    }
    else if (type == TopAbs_FACE) {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        out.edges = edges.Extent();
        out.mids.reserve(edges.Extent());
        for (int i = 1; i <= edges.Extent(); ++i) {
            sample(TopoDS::Edge(edges(i)), 1, out.mids);
        }
    }
    return out;
}

/// Every point of \a from, moved by \a by, is a point of \a to
bool pointsAgree(const std::vector<gp_Pnt>& from,
                 const std::vector<gp_Pnt>& to,
                 const gp_Trsf& by,
                 double tolerance)
{
    if (from.size() != to.size()) {
        return false;
    }
    const double limit = tolerance * tolerance;
    bool inOrder = true;
    for (std::size_t i = 0; i < from.size() && inOrder; ++i) {
        inOrder = from[i].Transformed(by).SquareDistance(to[i]) <= limit;
    }
    if (inOrder) {
        return true;
    }
    std::vector<bool> used(to.size(), false);
    for (const auto& point : from) {
        const gp_Pnt moved = point.Transformed(by);
        bool found = false;
        for (std::size_t j = 0; j < to.size(); ++j) {
            if (!used[j] && moved.SquareDistance(to[j]) <= limit) {
                used[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool boundaryAgrees(const Measure& a, const Measure& b, const gp_Trsf& by, double tolerance)
{
    return a.edges == b.edges && pointsAgree(a.points, b.points, by, tolerance)
        && pointsAgree(a.mids, b.mids, by, tolerance);
}

bool isNoMotion(const gp_Trsf& motion, double tolerance)
{
    if (motion.TranslationPart().Modulus() > tolerance) {
        return false;
    }
    return std::abs(motion.GetRotation().GetRotationAngle()) <= 1e-9;
}

double areaOf(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}

/** Two faces with one boundary, up to a motion, are on one surface.
 *
 * A plane is its boundary. A cylinder, a cone, a sphere and a torus are their
 * boundary and a radius or two. Anything else is asked for its area, which is
 * the one thing here that costs: 0.4 ms a pair on the model of
 * docs/TransactionLog.md sec 32.6, where every face asked it.
 */
bool surfaceAgrees(const TopoDS_Shape& a, const TopoDS_Shape& b, double tolerance)
{
    try {
        BRepAdaptor_Surface sa(TopoDS::Face(a), Standard_False);
        BRepAdaptor_Surface sb(TopoDS::Face(b), Standard_False);
        if (sa.GetType() != sb.GetType()) {
            return false;
        }
        auto close = [tolerance](double x, double y) {
            return std::abs(x - y) <= tolerance * std::max({1.0, std::abs(x), std::abs(y)});
        };
        switch (sa.GetType()) {
            case GeomAbs_Plane:
                return true;
            case GeomAbs_Cylinder:
                return close(sa.Cylinder().Radius(), sb.Cylinder().Radius());
            case GeomAbs_Cone:
                return close(sa.Cone().SemiAngle(), sb.Cone().SemiAngle());
            case GeomAbs_Sphere:
                return close(sa.Sphere().Radius(), sb.Sphere().Radius());
            case GeomAbs_Torus:
                return close(sa.Torus().MajorRadius(), sb.Torus().MajorRadius())
                    && close(sa.Torus().MinorRadius(), sb.Torus().MinorRadius());
            default:
                return close(areaOf(a), areaOf(b));
        }
    }
    catch (const Standard_Failure&) {
        return false;
    }
}

ShapeDiff::What whatBecameOf(const TopoDS_Shape& a,
                             const TopoDS_Shape& b,
                             TopAbs_ShapeEnum type,
                             double tolerance)
{
    if (a.IsSame(b)) {
        return ShapeDiff::What::Same;
    }
    const Measure ma = measure(a, type);
    const Measure mb = measure(b, type);
    if (ma.points.size() != mb.points.size() || ma.edges != mb.edges
        || ma.mids.size() != mb.mids.size()) {
        return ShapeDiff::What::Changed;
    }
    auto onOneSurface = [&]() {
        return type != TopAbs_FACE || surfaceAgrees(a, b, tolerance);
    };
    if (boundaryAgrees(ma, mb, gp_Trsf(), tolerance)) {
        return onOneSurface() ? ShapeDiff::What::Same : ShapeDiff::What::Changed;
    }
    gp_Trsf motion;
    if (recoverShapeMotion(a, b, motion)) {
        if (!boundaryAgrees(ma, mb, motion, tolerance) || !onOneSurface()) {
            return ShapeDiff::What::Changed;
        }
        return isNoMotion(motion, tolerance) ? ShapeDiff::What::Same : ShapeDiff::What::Moved;
    }
    // Too few corners to say how it turned: a disc, a cylinder's wall, an
    // edge. Slid is all that can be seen of those.
    const auto& from = ma.points.empty() ? ma.mids : ma.points;
    const auto& to = mb.points.empty() ? mb.mids : mb.points;
    if (from.empty()) {
        return ShapeDiff::What::Changed;
    }
    gp_XYZ sumFrom(0, 0, 0);
    gp_XYZ sumTo(0, 0, 0);
    for (const auto& point : from) {
        sumFrom += point.XYZ();
    }
    for (const auto& point : to) {
        sumTo += point.XYZ();
    }
    gp_Trsf slide;
    slide.SetTranslation(gp_Vec((sumTo - sumFrom) / static_cast<double>(from.size())));
    if (boundaryAgrees(ma, mb, slide, tolerance) && onOneSurface()) {
        return ShapeDiff::What::Moved;
    }
    return ShapeDiff::What::Changed;
}

/** Whether \a b is \a a moved as a whole, and how.
 *
 * The motion is the one every corner and every edge's middle agrees with,
 * element for element (recoverShapeMotion); then each face is asked whether
 * it is on the surface it was. The congruence index asks the area and the
 * volume of both instead, which is right for what it guards -- one shape
 * stored in place of another -- and cost 60 ms here where this costs one.
 */
bool wholeMoved(const Side& a, const Side& b, gp_Trsf& motion, double tolerance)
{
    if (a.count(0) != b.count(0) || a.count(1) != b.count(1) || a.count(2) != b.count(2)) {
        return false;
    }
    if (!recoverShapeMotion(a.shape.getShape(), b.shape.getShape(), motion)) {
        return false;
    }
    for (int i = 1; i <= a.count(0); ++i) {
        const Measure ma = measure(a.subs[0](i), TopAbs_FACE);
        const Measure mb = measure(b.subs[0](i), TopAbs_FACE);
        if (!boundaryAgrees(ma, mb, motion, tolerance)
            || !surfaceAgrees(a.subs[0](i), b.subs[0](i), tolerance)) {
            return false;
        }
    }
    return true;
}

int indexIn(const std::string& name, const char* typeName)
{
    const std::size_t length = std::strlen(typeName);
    if (name.compare(0, length, typeName) != 0) {
        return 0;
    }
    return std::atoi(name.c_str() + length);
}

}  // namespace

std::string ShapeDiff::nameWithoutSteps(const std::string& spelled, const std::set<long>& tags)
{
    return withoutSteps(spelled, tags, 0);
}

const char* ShapeDiff::name(By by)
{
    switch (by) {
        case By::Shape:
            return "shape";
        case By::Name:
            return "name";
        case By::Steps:
            return "steps";
        case By::Geometry:
            return "geometry";
        case By::Around:
            return "around";
        default:
            return "";
    }
}

const char* ShapeDiff::name(What what)
{
    switch (what) {
        case What::Same:
            return "same";
        case What::Moved:
            return "moved";
        case What::Changed:
            return "changed";
        case What::Added:
            return "added";
        default:
            return "removed";
    }
}

const std::vector<ShapeDiff::Element>& ShapeDiff::elements(TopAbs_ShapeEnum type) const
{
    static const std::vector<Element> none;
    int t = slotOf(type);
    return t < 0 ? none : _elements[t];
}

int ShapeDiff::count(TopAbs_ShapeEnum type, What what) const
{
    const auto& list = elements(type);
    return static_cast<int>(std::count_if(list.begin(), list.end(), [what](const Element& e) {
        return e.what == what;
    }));
}

int ShapeDiff::count(TopAbs_ShapeEnum type, By by) const
{
    const auto& list = elements(type);
    return static_cast<int>(std::count_if(list.begin(), list.end(), [by](const Element& e) {
        return e.by == by;
    }));
}

ShapeDiff::ShapeDiff(const TopoShape& a, const TopoShape& b)
    : ShapeDiff(a, b, Options())
{}

ShapeDiff::ShapeDiff(const TopoShape& shapeA, const TopoShape& shapeB, const Options& options)
{
    const bool wanted[3] = {options.faces, options.edges, options.vertices};
    const TopoDS_Shape& sa = shapeA.getShape();
    const TopoDS_Shape& sb = shapeB.getShape();
    if (sa.IsNull() && sb.IsNull()) {
        _same = true;
        return;
    }

    std::unique_ptr<Side> sideA;
    std::unique_ptr<Side> sideB;
    {
        Stopwatch watch(_cost.read);
        sideA = std::make_unique<Side>(shapeA);
        sideB = std::make_unique<Side>(shapeB);
    }
    Side& a = *sideA;
    Side& b = *sideB;

    // The whole first: one shape, or one moved, is said so and not as every
    // face of it changed.
    if (!sa.IsNull() && !sb.IsNull()) {
        Stopwatch watch(_cost.whole);
        if (sa.IsSame(sb)) {
            _same = true;
        }
        else {
            gp_Trsf motion;
            if (wholeMoved(a, b, motion, options.tolerance)) {
                if (isNoMotion(motion, options.tolerance)) {
                    _same = true;
                }
                else {
                    _moved = true;
                    _motion = motion;
                }
            }
        }
    }
    if (_same || _moved) {
        for (int t = 0; t < 3; ++t) {
            if (!wanted[t] || a.count(t) != b.count(t)) {
                continue;
            }
            for (int i = 1; i <= a.count(t); ++i) {
                _elements[t].push_back({i, i, By::Shape, _same ? What::Same : What::Moved});
            }
        }
        return;
    }

    // The objects only one of the two has a step of, in any of its names
    std::set<long> tagsA;
    std::set<long> tagsB;
    for (int t = 0; t < 3; ++t) {
        Stopwatch watch(_cost.steps);
        for (const auto& name : a.names[t]) {
            a.speller.tags(name, tagsA);
        }
        for (const auto& name : b.names[t]) {
            b.speller.tags(name, tagsB);
        }
    }
    std::set<long> oneSided;
    std::set_symmetric_difference(tagsA.begin(), tagsA.end(), tagsB.begin(), tagsB.end(),
                                  std::inserter(oneSided, oneSided.end()));

    // One table: a name is the same text in both. Two: only spelled out.
    const bool oneTable = shapeA.Hasher == shapeB.Hasher;

    std::vector<Pairing> pairings;
    for (int t = 0; t < 3; ++t) {
        pairings.emplace_back(a.count(t), b.count(t));
    }
    const bool needFaces = wanted[0] || ((wanted[1] || wanted[2]) && options.around);

    for (int t = 0; t < 3; ++t) {
        if (!(t == 0 ? needFaces : wanted[t])) {
            continue;
        }
        Pairing& pairing = pairings[t];
        const int na = a.count(t);
        const int nb = b.count(t);

        // 1. One name
        auto spelled = [](Side& side, int slot, const Pairing* skip, bool sideA) {
            std::vector<std::string> out(side.names[slot].size());
            for (std::size_t i = 1; i < out.size(); ++i) {
                if (side.names[slot][i].empty()) {
                    continue;
                }
                if (skip && (sideA ? skip->ab[i] : skip->ba[i])) {
                    continue;
                }
                if (!side.speller.spell(side.names[slot][i], out[i])) {
                    out[i].clear();
                }
            }
            return out;
        };
        {
            Stopwatch watch(_cost.names);
            if (oneTable) {
                pairByKey(pairing, a.names[t], b.names[t], By::Name);
            }
            else {
                pairByKey(pairing, spelled(a, t, nullptr, true), spelled(b, t, nullptr, false),
                          By::Name);
            }
        }

        // 2. One name without the steps only one side has
        if (!oneSided.empty()) {
            Stopwatch watch(_cost.steps);
            auto keysA = spelled(a, t, &pairing, true);
            auto keysB = spelled(b, t, &pairing, false);
            for (auto& key : keysA) {
                if (!key.empty()) {
                    key = withoutSteps(key, oneSided, 0);
                }
            }
            for (auto& key : keysB) {
                if (!key.empty()) {
                    key = withoutSteps(key, oneSided, 0);
                }
            }
            pairByKey(pairing, keysA, keysB, By::Steps);
        }

        // 3. One geometry
        if (options.geometry) {
            Stopwatch watch(_cost.geometry);
            for (int i = 1; i <= na; ++i) {
                if (pairing.ab[i]) {
                    continue;
                }
                std::vector<std::string> found;
                try {
                    shapeB.searchSubShape(TopoShape(a.subs[t](i)), &found,
                                          Data::SearchOption::CheckGeometry, options.tolerance);
                }
                catch (const Standard_Failure&) {
                    continue;
                }
                catch (const Base::Exception&) {
                    continue;
                }
                int only = 0;
                int candidates = 0;
                for (const auto& name : found) {
                    int j = indexIn(name, TypeNames[t]);
                    if (j > 0 && j <= nb && !pairing.ba[j]) {
                        only = j;
                        ++candidates;
                    }
                }
                if (candidates == 1) {
                    pairing.pair(i, only, By::Geometry);
                }
            }
        }

        // 4. The same paired faces around. A face is paired by its
        // neighbours, which pairs more neighbours: again until none.
        if (options.around) {
            Stopwatch watch(_cost.around);
            const Pairing& faces = pairings[0];
            const auto& aroundA = a.around(t);
            const auto& aroundB = b.around(t);
            for (;;) {
                std::map<std::vector<int>, std::vector<int>> byA;
                std::map<std::vector<int>, std::vector<int>> byB;
                for (int i = 1; i <= na; ++i) {
                    if (pairing.ab[i]) {
                        continue;
                    }
                    std::vector<int> key;
                    for (int face : aroundA[i]) {
                        if (faces.ab[face]) {
                            key.push_back(faces.ab[face]);
                        }
                    }
                    if (key.size() >= 2) {
                        std::sort(key.begin(), key.end());
                        byA[key].push_back(i);
                    }
                }
                for (int j = 1; j <= nb; ++j) {
                    if (pairing.ba[j]) {
                        continue;
                    }
                    std::vector<int> key;
                    for (int face : aroundB[j]) {
                        if (faces.ba[face]) {
                            key.push_back(face);
                        }
                    }
                    if (key.size() >= 2) {
                        byB[key].push_back(j);
                    }
                }
                bool any = false;
                for (const auto& entry : byA) {
                    if (entry.second.size() != 1) {
                        continue;
                    }
                    auto it = byB.find(entry.first);
                    if (it != byB.end() && it->second.size() == 1) {
                        pairing.pair(entry.second.front(), it->second.front(), By::Around);
                        any = true;
                    }
                }
                if (!any || t != 0) {
                    break;
                }
            }
        }

        if (!wanted[t]) {
            continue;
        }
        Stopwatch watch(_cost.what);
        const TopAbs_ShapeEnum type = Types[t];
        auto& out = _elements[t];
        out.reserve(std::max(na, nb));
        for (int i = 1; i <= na; ++i) {
            if (int j = pairing.ab[i]) {
                out.push_back({i, j, pairing.by[i],
                               whatBecameOf(a.subs[t](i), b.subs[t](j), type, options.tolerance)});
            }
            else {
                out.push_back({i, 0, By::None, What::Removed});
            }
        }
        for (int j = 1; j <= nb; ++j) {
            if (!pairing.ba[j]) {
                out.push_back({0, j, By::None, What::Added});
            }
        }
    }
}

bool ShapeDiff::material(const TopoShape& a, const TopoShape& b, Material& out, double fuzzy,
                         const Message_ProgressRange& progress)
{
    out = Material();
    const auto start = std::chrono::steady_clock::now();
    const TopoDS_Shape& sa = a.getShape();
    const TopoDS_Shape& sb = b.getShape();
    auto volumeOf = [](const TopoDS_Shape& shape) {
        double volume = 0.;
        for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
            GProp_GProps props;
            BRepGProp::VolumeProperties(it.Current(), props);
            volume += props.Mass();
        }
        return volume;
    };
    auto solid = [](const TopoDS_Shape& shape) {
        return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
    };
    if (!solid(sa) || !solid(sb))
        return false;
    try {
        const double least = 1e-9 * std::max(volumeOf(sa), volumeOf(sb));
        // The solids of a result that are more than what two faces meant to
        // be one leave between them
        auto solids = [&](const TopoDS_Shape& from, TopoDS_Shape& shape, double& volume) {
            BRep_Builder builder;
            TopoDS_Compound all;
            builder.MakeCompound(all);
            int count = 0;
            for (TopExp_Explorer it(from, TopAbs_SOLID); it.More(); it.Next()) {
                GProp_GProps props;
                BRepGProp::VolumeProperties(it.Current(), props);
                if (props.Mass() <= least)
                    continue;
                builder.Add(all, it.Current());
                volume += props.Mass();
                ++count;
            }
            if (count)
                shape = all;
        };
        if (sa.IsEqual(sb)) {
            // One shape in one place: all of it is of both
            solids(sa, out.common, out.commonVolume);
        }
        else {
            BOPAlgo_CellsBuilder cells;
            cells.AddArgument(sa);
            cells.AddArgument(sb);
            cells.SetNonDestructive(true);
            // All of what this takes is here, and it is what the kernel's
            // other fuses are given (sec 32.16)
            cells.SetRunParallel(true);
            if (fuzzy > 0.)
                cells.SetFuzzyValue(fuzzy);
            cells.Perform(progress);
            // Told to stop is an error of the fuse
            if (cells.HasErrors())
                return false;
            auto part = [&](bool inA, bool inB, TopoDS_Shape& shape, double& volume) {
                TopTools_ListOfShape take;
                TopTools_ListOfShape avoid;
                (inA ? take : avoid).Append(sa);
                (inB ? take : avoid).Append(sb);
                cells.RemoveAllFromResult();
                // One material: the walls between its cells are taken out
                cells.AddToResult(take, avoid, 1);
                cells.RemoveInternalBoundaries();
                solids(cells.Shape(), shape, volume);
            };
            part(true, false, out.removed, out.removedVolume);
            part(false, true, out.added, out.addedVolume);
            part(true, true, out.common, out.commonVolume);
        }
    }
    catch (const Standard_Failure&) {
        return false;
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

bool ShapeDiff::valueShape(const App::Document& doc, const std::string& ref, long tag,
                           TopoShape& shape)
{
    auto log = doc.getTransactionLog();
    if (ref.empty() || !log)
        return false;
    App::CapturedValue value;
    if (!log->readValue(ref, value) || !value.ok)
        return false;
    // Only a shape: the fragment of one opens with its Part element
    if (value.fragment.compare(0, 6, "<Part ") != 0)
        return false;
    // A property on no object reads for no document. The geometry it names
    // by hash is found in the store given here, and its names are read with
    // the string table the value it holds has -- so it is given both.
    App::BlobRecorder store(&doc.getFileBlobManager());
    PropertyPartShape held;
    held.setValue(TopoShape(tag, doc.getStringHasher()));
    try {
        App::restoreValue(held, value);
        shape = held.getShape();
    }
    catch (const Base::Exception&) {
        return false;
    }
    catch (const Standard_Failure&) {
        return false;
    }
    catch (const std::exception&) {
        return false;
    }
    shape.Tag = tag;
    return true;
}

}  // namespace Part
