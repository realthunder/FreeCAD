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

#include "CyclesMaterialXP.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <MaterialXCore/Geom.h>
#include <MaterialXCore/Material.h>
#include <MaterialXCore/Node.h>
#include <MaterialXGenShader/ShaderTranslator.h>

#include "MaterialXSupportP.h"

#include "scene/shader_graph.h"
#include "util/colorspace.h"
#include "scene/shader_nodes.h"
#include "util/transform.h"

namespace Render::Cycles
{
namespace
{

using Out = ccl::ShaderOutput *;

/// One MaterialX value on the way into the graph. Everything is a
/// node: Cycles constant-folds a subexpression at graph build, so
/// stating a constant as a ValueNode costs nothing at render and saves
/// the interpreter a second, constant-only evaluation path.
///
/// `dim` is the MaterialX type's component count -- 1 float/integer/
/// boolean, 2 vector2, 3 color3/vector3, 4 color4/vector4 -- because
/// Cycles has only float and float3 sockets and the fourth component
/// has to ride beside the triple. A vector2 is the triple with z = 0,
/// which is what every Cycles texture-coordinate consumer expects.
struct Val
{
    Out out = nullptr;  ///< float when dim == 1, else the xyz triple
    Out w = nullptr;    ///< the fourth component, dim == 4 only
    int dim = 1;
};

/// Where an input's value is looked up when it names an interface
/// rather than a node: the call site of the nodedef implementation
/// being expanded. Null for the document's own nodegraphs, whose
/// interface inputs are the graph's own.
struct Frame
{
    mx::NodePtr caller;
    /// What the cache keys an evaluation inside this expansion by. The
    /// CALLER's path, not a counter: a translation node is asked for
    /// one output at a time, and a counter would give each of them its
    /// own frame and rebuild every shared subexpression of the
    /// implementation graph per output.
    std::string key;
};

int typeDim(const std::string &type)
{
    if (type == "float" || type == "integer" || type == "boolean")
        return 1;
    if (type == "vector2")
        return 2;
    if (type == "color3" || type == "vector3")
        return 3;
    if (type == "color4" || type == "vector4")
        return 4;
    return 0;  // surfaceshader, material, filename, string, ...
}

/// The interpreter proper: one instance per document translated, so
/// its caches are the graph's.
class Interpreter
{
public:
    Interpreter(ccl::ShaderGraph *graph, const mx::DocumentPtr &doc,
                const std::string &surfaceName)
        : graph(graph)
        , doc(doc)
        , surfaceName(surfaceName)
    {}

    MaterialXResult run();

private:
    ccl::ShaderGraph *graph;
    mx::DocumentPtr doc;
    /// Which surface of the document is interpreted, empty for its
    /// first (docs/MaterialStorage.md sec 17.13)
    std::string surfaceName;
    std::vector<Frame> frames;
    std::map<std::string, Val> cache;
    std::set<std::string> reported;
    MaterialXResult result;

    // -- graph construction helpers ------------------------------------

    Out konst(float v)
    {
        auto *n = graph->create_node<ccl::ValueNode>();
        n->set_value(v);
        return n->output("Value");
    }
    Out konst3(float x, float y, float z)
    {
        auto *n = graph->create_node<ccl::ColorNode>();
        n->set_value(ccl::make_float3(x, y, z));
        return n->output("Color");
    }
    Val scalar(Out o)
    {
        return Val {o, nullptr, 1};
    }
    Val vector(Out o, int dim = 3)
    {
        return Val {o, nullptr, dim};
    }
    /// The value as one float: the scalar itself, or the x of a
    /// triple. MaterialX is strictly typed, so this only runs where a
    /// node's own definition says the component is what is wanted.
    Out asFloat(const Val &v)
    {
        if (v.dim <= 1)
            return v.out ? v.out : konst(0.0f);
        return separate(v.out)->output("X");
    }
    /// The value as a triple: broadcast from a scalar, or as it is.
    Out asVec(const Val &v)
    {
        if (v.dim <= 1) {
            auto *n = graph->create_node<ccl::CombineXYZNode>();
            Out s = v.out ? v.out : konst(0.0f);
            graph->connect(s, n->input("X"));
            graph->connect(s, n->input("Y"));
            graph->connect(s, n->input("Z"));
            return n->output("Vector");
        }
        return v.out;
    }
    /// The fourth component: the value's own, or 1 (an opaque colour4
    /// out of a colour3 source).
    Out asW(const Val &v)
    {
        if (v.w)
            return v.w;
        if (v.dim <= 1)
            return v.out ? v.out : konst(0.0f);
        return konst(1.0f);
    }
    ccl::SeparateXYZNode *separate(Out v)
    {
        auto *n = graph->create_node<ccl::SeparateXYZNode>();
        graph->connect(v, n->input("Vector"));
        return n;
    }
    Out combine(Out x, Out y, Out z)
    {
        auto *n = graph->create_node<ccl::CombineXYZNode>();
        graph->connect(x, n->input("X"));
        graph->connect(y, n->input("Y"));
        graph->connect(z, n->input("Z"));
        return n->output("Vector");
    }
    Out math(ccl::NodeMathType type, Out a, Out b = nullptr, Out c = nullptr)
    {
        auto *n = graph->create_node<ccl::MathNode>();
        n->set_math_type(type);
        graph->connect(a, n->input("Value1"));
        if (b)
            graph->connect(b, n->input("Value2"));
        if (c)
            graph->connect(c, n->input("Value3"));
        return n->output("Value");
    }
    Out vmath(ccl::NodeVectorMathType type, Out a, Out b = nullptr, Out c = nullptr)
    {
        auto *n = graph->create_node<ccl::VectorMathNode>();
        n->set_math_type(type);
        graph->connect(a, n->input("Vector1"));
        if (b)
            graph->connect(b, n->input("Vector2"));
        if (c)
            graph->connect(c, n->input("Vector3"));
        return n->output("Vector");
    }
    Out vmathScalar(ccl::NodeVectorMathType type, Out a, Out b)
    {
        auto *n = graph->create_node<ccl::VectorMathNode>();
        n->set_math_type(type);
        graph->connect(a, n->input("Vector1"));
        if (b)
            graph->connect(b, n->input("Vector2"));
        return n->output("Value");
    }
    /// The same arithmetic on a scalar or componentwise on a triple,
    /// which is what every MaterialX math node does.
    Val componentwise(ccl::NodeMathType s, ccl::NodeVectorMathType v, const Val &a, const Val &b);

    void report(const std::string &key, const std::string &message)
    {
        if (reported.insert(key).second)
            result.warnings.push_back(message);
    }

    // -- document walking ----------------------------------------------

    Val evalInput(const mx::InputPtr &input, const mx::NodePtr &owner);
    Val evalNode(const mx::NodePtr &node, const std::string &output);
    Val evalOutputElement(const mx::OutputPtr &output);
    /// A node whose nodedef is implemented as a nodegraph: evaluate
    /// that graph with this node bound as its interface.
    bool evalImplementation(const mx::NodePtr &node, const std::string &output, Val &out);
    Val defaultValue(const mx::NodePtr &node, const std::string &name);
    /// One of MaterialX's geometric properties -- "texcoord",
    /// "position", "normal", "tangent", "bitangent", "geomcolor" --
    /// as the corresponding Cycles source.
    Val geomProp(const std::string &prop, int dim);
    /// The value of one of a node's inputs, falling back to the
    /// nodedef's default when the node does not state it.
    Val in(const mx::NodePtr &node, const char *name);
    Val fromValueString(const std::string &type, const std::string &text);

    Val buildNode(const mx::NodePtr &node, const std::string &category, const std::string &output);
    Out buildOpenPbr(const mx::NodePtr &node);
};

// ---------------------------------------------------------------------
// values

Val Interpreter::fromValueString(const std::string &type, const std::string &text)
{
    const int dim = typeDim(type);
    float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (type == "boolean") {
        c[0] = (text == "true" || text == "1") ? 1.0f : 0.0f;
    }
    else {
        // MaterialX writes a vector as comma-separated decimals.
        size_t pos = 0;
        int i = 0;
        while (i < 4 && pos <= text.size()) {
            size_t comma = text.find(',', pos);
            std::string piece = text.substr(pos, comma == std::string::npos ? comma : comma - pos);
            try {
                c[i] = std::stof(piece);
            }
            catch (const std::exception &) {
                c[i] = 0.0f;
            }
            ++i;
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }
    if (dim <= 1)
        return scalar(konst(c[0]));
    Val v = vector(konst3(c[0], c[1], c[2]), dim);
    if (dim == 4)
        v.w = konst(c[3]);
    return v;
}

Val Interpreter::geomProp(const std::string &prop, int dim)
{
    if (prop == "texcoord") {
        auto *n = graph->create_node<ccl::TextureCoordinateNode>();
        return vector(n->output("UV"), dim > 0 ? dim : 2);
    }
    if (prop == "geomcolor") {
        auto *n = graph->create_node<ccl::VertexColorNode>();
        Val v = vector(n->output("Color"), dim > 0 ? dim : 3);
        if (v.dim == 4)
            v.w = n->output("Alpha");
        return v;
    }
    auto *n = graph->create_node<ccl::GeometryNode>();
    if (prop == "position")
        return vector(n->output("Position"), 3);
    if (prop == "normal")
        return vector(n->output("Normal"), 3);
    if (prop == "tangent")
        return vector(n->output("Tangent"), 3);
    if (prop == "bitangent")
        return vector(vmath(ccl::NODE_VECTOR_MATH_CROSS_PRODUCT,
                            n->output("Normal"),
                            n->output("Tangent")),
                      3);
    report("geomprop:" + prop, "geometric property '" + prop
               + "' has no equivalent here; read as zero");
    return dim <= 1 ? scalar(konst(0.0f)) : vector(konst3(0.0f, 0.0f, 0.0f), dim);
}

Val Interpreter::defaultValue(const mx::NodePtr &node, const std::string &name)
{
    mx::NodeDefPtr def = node ? node->getNodeDef() : nullptr;
    mx::InputPtr in = def ? def->getActiveInput(name) : nullptr;
    if (!in)
        return scalar(konst(0.0f));
    // An input the caller left alone may still not be a constant: a
    // texture coordinate, a normal, a position. MaterialX states that
    // as defaultgeomprop, and reading it as the zero value instead is
    // the flat-colour bug in its purest form -- every texel sampled at
    // (0, 0), which is a plausible-looking material and the wrong one.
    if (mx::GeomPropDefPtr prop = in->getDefaultGeomProp())
        return geomProp(prop->getGeomProp(), typeDim(in->getType()));
    if (in->hasValueString())
        return fromValueString(in->getType(), in->getValueString());
    return scalar(konst(0.0f));
}

Val Interpreter::in(const mx::NodePtr &node, const char *name)
{
    mx::InputPtr input = node->getInput(name);
    if (input)
        return evalInput(input, node);
    return defaultValue(node, name);
}

Val Interpreter::evalInput(const mx::InputPtr &input, const mx::NodePtr &owner)
{
    // An input names its source in one of four ways, and they are
    // tried in MaterialX's own order of precedence.
    if (input->hasInterfaceName()) {
        const std::string &name = input->getInterfaceName();
        // Inside an expanded nodedef implementation the interface is
        // the CALLING node's inputs; inside a plain document nodegraph
        // it is the graph's own.
        if (!frames.empty() && frames.back().caller) {
            // The caller's input is evaluated in the CALLER's frame,
            // which is this one popped -- not in an empty stack, which
            // would lose an enclosing expansion two levels up.
            Frame top = frames.back();
            frames.pop_back();
            mx::InputPtr bound = top.caller->getInput(name);
            Val v = bound ? evalInput(bound, top.caller) : defaultValue(top.caller, name);
            frames.push_back(top);
            return v;
        }
        if (mx::InputPtr iface = input->getInterfaceInput())
            return evalInput(iface, owner);
    }
    if (input->hasNodeGraphString()) {
        if (mx::OutputPtr out = input->getConnectedOutput())
            return evalOutputElement(out);
    }
    if (input->hasNodeName()) {
        if (mx::NodePtr node = input->getConnectedNode())
            return evalNode(node, input->getOutputString());
    }
    if (input->hasValueString())
        return fromValueString(input->getType(), input->getValueString());
    return defaultValue(owner, input->getName());
}

Val Interpreter::evalOutputElement(const mx::OutputPtr &output)
{
    if (mx::NodePtr node = output->getConnectedNode())
        return evalNode(node, output->getOutputString());
    if (output->hasValueString())
        return fromValueString(output->getType(), output->getValueString());
    return scalar(konst(0.0f));
}

Val Interpreter::evalNode(const mx::NodePtr &node, const std::string &output)
{
    static const std::string kNoFrame;
    const std::string &frame = frames.empty() ? kNoFrame : frames.back().key;
    const std::string key = frame + '|' + node->getNamePath() + '|' + output;
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;
    // A cycle would recurse forever; MaterialX's validate() does not
    // reject one, so the guard is here.
    cache[key] = scalar(konst(0.0f));
    Val v = buildNode(node, node->getCategory(), output);
    cache[key] = v;
    return v;
}

bool Interpreter::evalImplementation(const mx::NodePtr &node,
                                     const std::string &output,
                                     Val &out)
{
    mx::NodeDefPtr def = node->getNodeDef();
    if (!def)
        return false;
    mx::InterfaceElementPtr impl = def->getImplementation();
    mx::NodeGraphPtr nodegraph = impl ? impl->asA<mx::NodeGraph>() : nullptr;
    if (!nodegraph)
        return false;
    // A nodedef whose implementation is itself a nodegraph -- the
    // shape most of the pattern library and every one of the shading
    // model TRANSLATIONS take -- is interpreted by descending into it
    // with this node bound as its interface. That is what makes the
    // table below a floor rather than the whole vocabulary: anything
    // built out of nodes the table knows is known too.
    if (frames.size() > 32) {
        report("recursion", "node graph nested deeper than 32 levels; stopped");
        return false;
    }
    std::vector<mx::OutputPtr> outputs = nodegraph->getOutputs();
    if (outputs.empty())
        return false;
    mx::OutputPtr target = output.empty() ? outputs.front() : nodegraph->getOutput(output);
    if (!target)
        target = outputs.front();
    frames.push_back(Frame {node, (frames.empty() ? std::string() : frames.back().key)
                                      + '/' + node->getNamePath()});
    out = evalOutputElement(target);
    frames.pop_back();
    return true;
}

Val Interpreter::componentwise(ccl::NodeMathType s,
                               ccl::NodeVectorMathType v,
                               const Val &a,
                               const Val &b)
{
    if (a.dim <= 1 && b.dim <= 1)
        return scalar(math(s, asFloat(a), asFloat(b)));
    Val r = vector(vmath(v, asVec(a), asVec(b)), std::max(a.dim, b.dim));
    if (r.dim == 4)
        r.w = math(s, asW(a), asW(b));
    return r;
}

// ---------------------------------------------------------------------
// the node table

Val Interpreter::buildNode(const mx::NodePtr &node,
                           const std::string &category,
                           const std::string &output)
{
    const std::string type = node->getType();
    const int dim = typeDim(type);

    // -- pass-through and constants ------------------------------------
    if (category == "dot" || category == "convert")
    {
        Val v = in(node, "in");
        if (category == "convert") {
            // MaterialX's convert crosses the float/vector divide;
            // asFloat and asVec ARE that conversion.
            if (dim <= 1)
                return scalar(asFloat(v));
            Val r = vector(asVec(v), dim);
            if (dim == 4)
                r.w = asW(v);
            return r;
        }
        return v;
    }
    if (category == "constant")
        return in(node, "value");
    if (category == "switch") {
        // Whichever branch the constant selector names; a linked
        // selector would need a mix chain, which no example uses.
        mx::InputPtr which = node->getInput("which");
        int index = 0;
        if (which && which->hasValueString()) {
            try {
                index = int(std::stof(which->getValueString()));
            }
            catch (const std::exception &) {
                index = 0;
            }
        }
        return in(node, ("in" + std::to_string(index + 1)).c_str());
    }

    // -- geometry -------------------------------------------------------
    if (category == "texcoord")
        return geomProp("texcoord", dim ? dim : 2);
    if (category == "position" || category == "normal" || category == "tangent"
        || category == "bitangent" || category == "geomcolor")
    {
        return geomProp(category, dim);
    }

    // -- images ---------------------------------------------------------
    if (category == "image" || category == "tiledimage") {
        mx::InputPtr file = node->getInput("file");
        const std::string path = file ? file->getValueString() : std::string();
        auto *tex = graph->create_node<ccl::ImageTextureNode>();
        ++result.images;
        tex->set_filename(ccl::ustring(path));
        // What space the FILE is in. An input that states one is the
        // answer; otherwise the node's own type decides, because a
        // document's working colour space (lin_rec709 on the root, and
        // getActiveColorSpace inherits it) says nothing about a normal
        // or roughness map's pixels -- reading those as colour is the
        // silent, plausible mistake this avoids.
        //
        // A picture is declared `scene_linear_srgb` and NOT
        // `u_colorspace_srgb`, for the reason PixelImage states at
        // length in CyclesScene.cpp: the bytes stay bytes and the
        // kernel decodes them per sample. The two spellings differ
        // only in primaries -- `u_colorspace_srgb` is Rec.709 whatever
        // scene linear is -- and scene linear IS Rec.709 here, since
        // nothing sets an OpenColorIO config and every other colour
        // this engine is handed is already Rec.709.
        //
        // This path had the other spelling, and it does not merely
        // cost half floats: ColorSpaceManager::to_scene_linear leaves
        // the pixels sRGB-encoded for it (it forces compress_as_srgb)
        // while ImageMetaData::finalize never sets the
        // is_compressible_as_srgb that tells the kernel to decode
        // them, so the map arrived ENCODED TWICE -- 3.418x the albedo
        // it should have, against 3.413x predicted for a double
        // encode (fcad-probes/mtlximage_run.sh).
        const std::string space = file ? file->getColorSpace() : std::string();
        if (space == "srgb_texture")
            tex->set_colorspace(ccl::u_colorspace_scene_linear_srgb);
        else if (dim >= 3 && (type == "color3" || type == "color4"))
            tex->set_colorspace(ccl::u_colorspace_scene_linear);
        else
            tex->set_colorspace(ccl::u_colorspace_data);
        Val uv = in(node, "texcoord");
        // A tiled image repeats the coordinates and offsets them; a
        // plain image samples them as they are (and MaterialX's
        // default texcoord is the mesh's own, which is what an
        // unconnected input evaluates to below).
        Out coord = asVec(uv);
        if (category == "tiledimage") {
            Out tiling = asVec(in(node, "uvtiling"));
            Out offset = asVec(in(node, "uvoffset"));
            coord = vmath(ccl::NODE_VECTOR_MATH_MULTIPLY, coord, tiling);
            coord = vmath(ccl::NODE_VECTOR_MATH_ADD, coord, offset);
        }
        graph->connect(coord, tex->input("Vector"));
        if (dim <= 1)
            return scalar(separate(tex->output("Color"))->output("X"));
        Val v = vector(tex->output("Color"), dim);
        if (dim == 4)
            v.w = tex->output("Alpha");
        return v;
    }
    if (category == "normalmap") {
        auto *n = graph->create_node<ccl::NormalMapNode>();
        n->set_space(ccl::NODE_NORMAL_MAP_TANGENT);
        graph->connect(asVec(in(node, "in")), n->input("Color"));
        graph->connect(asFloat(in(node, "scale")), n->input("Strength"));
        return vector(n->output("Normal"), 3);
    }

    // -- arithmetic ------------------------------------------------------
    if (category == "add")
        return componentwise(ccl::NODE_MATH_ADD, ccl::NODE_VECTOR_MATH_ADD,
                             in(node, "in1"), in(node, "in2"));
    if (category == "subtract")
        return componentwise(ccl::NODE_MATH_SUBTRACT, ccl::NODE_VECTOR_MATH_SUBTRACT,
                             in(node, "in1"), in(node, "in2"));
    if (category == "multiply")
        return componentwise(ccl::NODE_MATH_MULTIPLY, ccl::NODE_VECTOR_MATH_MULTIPLY,
                             in(node, "in1"), in(node, "in2"));
    if (category == "divide")
        return componentwise(ccl::NODE_MATH_DIVIDE, ccl::NODE_VECTOR_MATH_DIVIDE,
                             in(node, "in1"), in(node, "in2"));
    if (category == "modulo")
        return componentwise(ccl::NODE_MATH_MODULO, ccl::NODE_VECTOR_MATH_MODULO,
                             in(node, "in1"), in(node, "in2"));
    if (category == "power")
        return componentwise(ccl::NODE_MATH_POWER, ccl::NODE_VECTOR_MATH_POWER,
                             in(node, "in1"), in(node, "in2"));
    if (category == "min")
        return componentwise(ccl::NODE_MATH_MINIMUM, ccl::NODE_VECTOR_MATH_MINIMUM,
                             in(node, "in1"), in(node, "in2"));
    if (category == "max")
        return componentwise(ccl::NODE_MATH_MAXIMUM, ccl::NODE_VECTOR_MATH_MAXIMUM,
                             in(node, "in1"), in(node, "in2"));
    if (category == "absval")
        return componentwise(ccl::NODE_MATH_ABSOLUTE, ccl::NODE_VECTOR_MATH_ABSOLUTE,
                             in(node, "in"), scalar(konst(0.0f)));
    if (category == "floor")
        return componentwise(ccl::NODE_MATH_FLOOR, ccl::NODE_VECTOR_MATH_FLOOR,
                             in(node, "in"), scalar(konst(0.0f)));
    if (category == "ceil")
        return componentwise(ccl::NODE_MATH_CEIL, ccl::NODE_VECTOR_MATH_CEIL,
                             in(node, "in"), scalar(konst(0.0f)));
    if (category == "round")
        return componentwise(ccl::NODE_MATH_ROUND, ccl::NODE_VECTOR_MATH_ROUND,
                             in(node, "in"), scalar(konst(0.0f)));
    if (category == "sign")
        return componentwise(ccl::NODE_MATH_SIGN, ccl::NODE_VECTOR_MATH_SIGN,
                             in(node, "in"), scalar(konst(0.0f)));
    if (category == "sin" || category == "cos" || category == "tan" || category == "asin"
        || category == "acos" || category == "ln" || category == "exp" || category == "sqrt")
    {
        static const std::map<std::string, ccl::NodeMathType> unary = {
            {"sin", ccl::NODE_MATH_SINE},         {"cos", ccl::NODE_MATH_COSINE},
            {"tan", ccl::NODE_MATH_TANGENT},      {"asin", ccl::NODE_MATH_ARCSINE},
            {"acos", ccl::NODE_MATH_ARCCOSINE},   {"ln", ccl::NODE_MATH_LOGARITHM},
            {"exp", ccl::NODE_MATH_EXPONENT},     {"sqrt", ccl::NODE_MATH_SQRT},
        };
        const ccl::NodeMathType op = unary.at(category);
        // Cycles' logarithm takes its base as a second operand; every
        // other one here is genuinely unary.
        Out base = category == "ln" ? konst(float(M_E)) : nullptr;
        Val v = in(node, "in");
        if (v.dim <= 1)
            return scalar(math(op, asFloat(v), base));
        // Cycles has no componentwise transcendental on a vector, so
        // the triple is split and rebuilt.
        auto *sp = separate(asVec(v));
        return vector(combine(math(op, sp->output("X"), base),
                              math(op, sp->output("Y"), base),
                              math(op, sp->output("Z"), base)),
                      v.dim);
    }
    if (category == "atan2") {
        Val y = in(node, "in1");
        Val x = in(node, "in2");
        if (y.dim <= 1 && x.dim <= 1)
            return scalar(math(ccl::NODE_MATH_ARCTAN2, asFloat(y), asFloat(x)));
        auto *sy = separate(asVec(y));
        auto *sx = separate(asVec(x));
        return vector(combine(math(ccl::NODE_MATH_ARCTAN2, sy->output("X"), sx->output("X")),
                              math(ccl::NODE_MATH_ARCTAN2, sy->output("Y"), sx->output("Y")),
                              math(ccl::NODE_MATH_ARCTAN2, sy->output("Z"), sx->output("Z"))),
                      std::max(y.dim, x.dim));
    }
    if (category == "clamp") {
        Val v = in(node, "in");
        Val lo = in(node, "low");
        Val hi = in(node, "high");
        if (v.dim <= 1) {
            auto *n = graph->create_node<ccl::ClampNode>();
            graph->connect(asFloat(v), n->input("Value"));
            graph->connect(asFloat(lo), n->input("Min"));
            graph->connect(asFloat(hi), n->input("Max"));
            return scalar(n->output("Result"));
        }
        Out r = vmath(ccl::NODE_VECTOR_MATH_MAXIMUM, asVec(v), asVec(lo));
        r = vmath(ccl::NODE_VECTOR_MATH_MINIMUM, r, asVec(hi));
        Val out2 = vector(r, v.dim);
        if (out2.dim == 4) {
            auto *n = graph->create_node<ccl::ClampNode>();
            graph->connect(asW(v), n->input("Value"));
            graph->connect(asW(lo), n->input("Min"));
            graph->connect(asW(hi), n->input("Max"));
            out2.w = n->output("Result");
        }
        return out2;
    }
    if (category == "remap" || category == "smoothstep") {
        Val v = in(node, "in");
        const bool smooth = category == "smoothstep";
        Val lowIn = in(node, smooth ? "low" : "inlow");
        Val highIn = in(node, smooth ? "high" : "inhigh");
        auto one = [&](Out x, Out lo, Out hi) {
            auto *n = graph->create_node<ccl::MapRangeNode>();
            n->set_range_type(smooth ? ccl::NODE_MAP_RANGE_SMOOTHSTEP
                                     : ccl::NODE_MAP_RANGE_LINEAR);
            n->set_clamp(smooth);
            graph->connect(x, n->input("Value"));
            graph->connect(lo, n->input("From Min"));
            graph->connect(hi, n->input("From Max"));
            return n->output("Result");
        };
        auto scaled = [&](Out mapped, Out lo, Out hi) {
            if (smooth)
                return mapped;
            // remap's output range, applied after the 0..1 mapping.
            Out span = math(ccl::NODE_MATH_SUBTRACT, hi, lo);
            return math(ccl::NODE_MATH_ADD, math(ccl::NODE_MATH_MULTIPLY, mapped, span), lo);
        };
        Val lowOut = smooth ? Val {} : in(node, "outlow");
        Val highOut = smooth ? Val {} : in(node, "outhigh");
        if (v.dim <= 1) {
            Out m = one(asFloat(v), asFloat(lowIn), asFloat(highIn));
            return scalar(smooth ? m : scaled(m, asFloat(lowOut), asFloat(highOut)));
        }
        auto *sv = separate(asVec(v));
        auto *sl = separate(asVec(lowIn));
        auto *sh = separate(asVec(highIn));
        auto *slo = smooth ? nullptr : separate(asVec(lowOut));
        auto *sho = smooth ? nullptr : separate(asVec(highOut));
        Out comps[3];
        const char *axes[3] = {"X", "Y", "Z"};
        for (int i = 0; i < 3; ++i) {
            Out m = one(sv->output(axes[i]), sl->output(axes[i]), sh->output(axes[i]));
            comps[i] = smooth ? m : scaled(m, slo->output(axes[i]), sho->output(axes[i]));
        }
        return vector(combine(comps[0], comps[1], comps[2]), v.dim);
    }
    if (category == "invert") {
        Val amount = in(node, "amount");
        Val v = in(node, "in");
        return componentwise(ccl::NODE_MATH_SUBTRACT, ccl::NODE_VECTOR_MATH_SUBTRACT, amount, v);
    }
    if (category == "normalize")
        return vector(vmath(ccl::NODE_VECTOR_MATH_NORMALIZE, asVec(in(node, "in"))), dim);
    if (category == "magnitude")
        return scalar(vmathScalar(ccl::NODE_VECTOR_MATH_LENGTH, asVec(in(node, "in")), nullptr));
    if (category == "dotproduct")
        return scalar(vmathScalar(ccl::NODE_VECTOR_MATH_DOT_PRODUCT,
                                  asVec(in(node, "in1")),
                                  asVec(in(node, "in2"))));
    if (category == "crossproduct")
        return vector(vmath(ccl::NODE_VECTOR_MATH_CROSS_PRODUCT,
                            asVec(in(node, "in1")),
                            asVec(in(node, "in2"))),
                      3);
    if (category == "luminance") {
        auto *n = graph->create_node<ccl::RGBToBWNode>();
        graph->connect(asVec(in(node, "in")), n->input("Color"));
        // luminance is a colour node: the grey goes to all three.
        Val v = vector(asVec(scalar(n->output("Val"))), dim ? dim : 3);
        if (v.dim == 4)
            v.w = asW(in(node, "in"));
        return v;
    }
    if (category == "rgbtohsv" || category == "hsvtorgb") {
        report(category,
               category + ": interpreted as a pass-through (Cycles has no "
                          "colour-space conversion node)");
        return in(node, "in");
    }

    // -- mixing and comparison -------------------------------------------
    if (category == "mix") {
        Val fg = in(node, "fg");
        Val bg = in(node, "bg");
        Val mixAmount = in(node, "mix");
        if (fg.dim <= 1 && bg.dim <= 1) {
            auto *n = graph->create_node<ccl::MixFloatNode>();
            graph->connect(asFloat(mixAmount), n->input("Factor"));
            graph->connect(asFloat(bg), n->input("A"));
            graph->connect(asFloat(fg), n->input("B"));
            return scalar(n->output("Result"));
        }
        auto *n = graph->create_node<ccl::MixColorNode>();
        n->set_blend_type(ccl::NODE_MIX_BLEND);
        graph->connect(asFloat(mixAmount), n->input("Factor"));
        graph->connect(asVec(bg), n->input("A"));
        graph->connect(asVec(fg), n->input("B"));
        Val v = vector(n->output("Result"), std::max(fg.dim, bg.dim));
        if (v.dim == 4) {
            auto *a = graph->create_node<ccl::MixFloatNode>();
            graph->connect(asFloat(mixAmount), a->input("Factor"));
            graph->connect(asW(bg), a->input("A"));
            graph->connect(asW(fg), a->input("B"));
            v.w = a->output("Result");
        }
        return v;
    }
    if (category == "ifgreater" || category == "ifgreatereq" || category == "ifequal") {
        // The comparison is a 0/1 factor and the branches are mixed by
        // it, which folds to one branch whenever the operands are
        // constant -- and in the shading-model translations they are.
        const ccl::NodeMathType op = category == "ifgreater" ? ccl::NODE_MATH_GREATER_THAN
            : category == "ifgreatereq"                      ? ccl::NODE_MATH_LESS_THAN
                                                             : ccl::NODE_MATH_COMPARE;
        Out v1 = asFloat(in(node, "value1"));
        Out v2 = asFloat(in(node, "value2"));
        Out factor = category == "ifgreatereq"
            ? math(ccl::NODE_MATH_SUBTRACT, konst(1.0f), math(op, v1, v2))
            : math(op, v1, v2);
        if (category == "ifequal") {
            auto *n = graph->create_node<ccl::MathNode>();
            n->set_math_type(ccl::NODE_MATH_COMPARE);
            graph->connect(v1, n->input("Value1"));
            graph->connect(v2, n->input("Value2"));
            n->set_value3(1e-6f);
            factor = n->output("Value");
        }
        Val t = in(node, "in1");
        Val f = in(node, "in2");
        if (t.dim <= 1 && f.dim <= 1) {
            auto *n = graph->create_node<ccl::MixFloatNode>();
            graph->connect(factor, n->input("Factor"));
            graph->connect(asFloat(f), n->input("A"));
            graph->connect(asFloat(t), n->input("B"));
            return scalar(n->output("Result"));
        }
        auto *n = graph->create_node<ccl::MixColorNode>();
        n->set_blend_type(ccl::NODE_MIX_BLEND);
        graph->connect(factor, n->input("Factor"));
        graph->connect(asVec(f), n->input("A"));
        graph->connect(asVec(t), n->input("B"));
        return vector(n->output("Result"), std::max(t.dim, f.dim));
    }

    // -- channels ---------------------------------------------------------
    if (category == "separate2" || category == "separate3" || category == "separate4") {
        Val v = in(node, "in");
        auto *sp = separate(asVec(v));
        if (output == "outr" || output == "outx" || output.empty())
            return scalar(sp->output("X"));
        if (output == "outg" || output == "outy")
            return scalar(sp->output("Y"));
        if (output == "outb" || output == "outz")
            return scalar(sp->output("Z"));
        return scalar(asW(v));
    }
    if (category == "combine2" || category == "combine3" || category == "combine4") {
        Out x = asFloat(in(node, "in1"));
        Out y = asFloat(in(node, "in2"));
        Out z = category == "combine2" ? konst(0.0f) : asFloat(in(node, "in3"));
        Val v = vector(combine(x, y, z), dim);
        if (dim == 4)
            v.w = asFloat(in(node, "in4"));
        return v;
    }
    if (category == "extract") {
        Val v = in(node, "in");
        mx::InputPtr index = node->getInput("index");
        int i = 0;
        if (index && index->hasValueString()) {
            try {
                i = int(std::stof(index->getValueString()));
            }
            catch (const std::exception &) {
                i = 0;
            }
        }
        if (i >= 3)
            return scalar(asW(v));
        auto *sp = separate(asVec(v));
        const char *axes[3] = {"X", "Y", "Z"};
        return scalar(sp->output(axes[std::max(0, i)]));
    }
    if (category == "swizzle") {
        // A channel string over the input's components. Rare, and
        // MaterialX 1.39 deprecates it in favour of separate/combine.
        report(category, "swizzle: interpreted as a pass-through (deprecated in "
                         "MaterialX 1.39; use separate/combine)");
        return in(node, "in");
    }

    // -- procedural patterns -----------------------------------------------
    if (category == "noise2d" || category == "noise3d" || category == "fractal3d"
        || category == "cellnoise2d" || category == "cellnoise3d")
    {
        const bool twoD = category == "noise2d" || category == "cellnoise2d";
        const bool cell = category == "cellnoise2d" || category == "cellnoise3d";
        auto *n = graph->create_node<ccl::NoiseTextureNode>();
        n->set_dimensions(twoD ? 2 : 3);
        if (category == "fractal3d")
            graph->connect(asFloat(in(node, "octaves")), n->input("Detail"));
        else
            n->set_detail(0.0f);
        // The sampling point is the node's own input, and where the
        // node does not state one the nodedef's defaultgeomprop is the
        // answer -- UV0 for the 2D forms, the object position for the
        // 3D ones. Reading a missing input as zero instead would give
        // one noise value for the whole surface.
        graph->connect(asVec(in(node, twoD ? "texcoord" : "position")), n->input("Vector"));
        Out fac = n->output("Fac");
        if (cell) {
            // Cell noise is already 0..1 and unscaled.
            if (dim <= 1)
                return scalar(fac);
            return vector(asVec(scalar(fac)), dim);
        }
        // MaterialX's noise is signed, amplitude scaled and pivoted;
        // Cycles' Fac is 0..1.
        Out signed11 = math(ccl::NODE_MATH_MULTIPLY,
                            math(ccl::NODE_MATH_SUBTRACT, fac, konst(0.5f)),
                            konst(2.0f));
        Val amplitude = in(node, "amplitude");
        Val pivot = in(node, "pivot");
        if (dim <= 1)
            return scalar(math(ccl::NODE_MATH_ADD,
                               math(ccl::NODE_MATH_MULTIPLY, signed11, asFloat(amplitude)),
                               asFloat(pivot)));
        Out v = vmath(ccl::NODE_VECTOR_MATH_MULTIPLY,
                      asVec(scalar(signed11)),
                      asVec(amplitude));
        return vector(vmath(ccl::NODE_VECTOR_MATH_ADD, v, asVec(pivot)), dim);
    }
    if (category == "ramplr" || category == "ramptb") {
        Val a = in(node, "valuel");
        Val b = in(node, "valuer");
        if (category == "ramptb") {
            a = in(node, "valuet");
            b = in(node, "valueb");
        }
        auto *coord = graph->create_node<ccl::TextureCoordinateNode>();
        auto *sp = separate(coord->output("UV"));
        Out factor = sp->output(category == "ramplr" ? "X" : "Y");
        auto *n = graph->create_node<ccl::MixColorNode>();
        n->set_blend_type(ccl::NODE_MIX_BLEND);
        graph->connect(factor, n->input("Factor"));
        graph->connect(asVec(a), n->input("A"));
        graph->connect(asVec(b), n->input("B"));
        return vector(n->output("Result"), dim ? dim : 3);
    }

    // -- anything else: the nodedef's own graph, then give up -------------
    Val expanded;
    if (evalImplementation(node, output, expanded))
        return expanded;
    report(category,
           category + ": no interpretation for this node; the material renders "
                      "with it evaluated as zero");
    if (dim <= 1)
        return scalar(konst(0.0f));
    return vector(konst3(0.0f, 0.0f, 0.0f), dim);
}

// ---------------------------------------------------------------------
// the surface

/// A float input the node states as a constant, else 0: for the inputs
/// whose VALUE decides the graph's shape (a depth that is or is not
/// there), which a connection cannot. Read FLAT through the translation
/// graph (Render::MaterialX::flatConstant): a translated
/// standard_surface carries every input as a connection, so a literal
/// read of the node -- what this did before -- said 0 for every depth
/// a standard_surface document stated.
static float statedFloat(const mx::NodePtr &node, const char *name)
{
    std::vector<float> value;
    if (!Render::MaterialX::flatConstant(node, name, value) || value.empty())
        return 0.0f;
    return value[0];
}

Out Interpreter::buildOpenPbr(const mx::NodePtr &node)
{
    auto *bsdf = graph->create_node<ccl::PrincipledBsdfNode>();

    // Base. OpenPBR's base_weight scales the diffuse albedo, which
    // Cycles has no socket for, so it is folded into the colour --
    // exactly what a weight of that kind means.
    Out base = vmath(ccl::NODE_VECTOR_MATH_MULTIPLY,
                     asVec(in(node, "base_color")),
                     asVec(in(node, "base_weight")));
    // OpenPBR gives transmission_color two meanings by the depth: over
    // a stated depth it is what survives that path length (the
    // absorption volume at the end of this function), and at depth 0
    // it is a tint applied ONCE at the surface. Principled tints its
    // refraction by Base Color and has no socket for the second, so
    // it is folded in there, weighted by the transmission -- exact for
    // a fully transmissive body, and for a partial one it tints the
    // diffuse share too, which is what Blender's own importers accept.
    // Without this the tracer dropped the colour of every depth-0
    // glass (the chess set's pawn heads) while the raster glass pass
    // applies it (docs/MaterialStorage.md sec 17.21).
    const float depthValue = statedFloat(node, "transmission_depth");
    if (depthValue <= 0.0f && node->getInput("transmission_color")) {
        auto *tint = graph->create_node<ccl::MixColorNode>();
        tint->set_blend_type(ccl::NODE_MIX_BLEND);
        graph->connect(asFloat(in(node, "transmission_weight")), tint->input("Factor"));
        graph->connect(konst3(1.0f, 1.0f, 1.0f), tint->input("A"));
        graph->connect(asVec(in(node, "transmission_color")), tint->input("B"));
        base = vmath(ccl::NODE_VECTOR_MATH_MULTIPLY, base, tint->output("Result"));
    }
    graph->connect(base, bsdf->input("Base Color"));
    graph->connect(asFloat(in(node, "base_metalness")), bsdf->input("Metallic"));
    graph->connect(asFloat(in(node, "base_diffuse_roughness")),
                   bsdf->input("Diffuse Roughness"));

    // Specular. Cycles' Specular IOR Level scales F0 by 2x, so 0.5 is
    // the neutral value OpenPBR's specular_weight = 1 means.
    graph->connect(asFloat(in(node, "specular_roughness")), bsdf->input("Roughness"));
    graph->connect(asFloat(in(node, "specular_ior")), bsdf->input("IOR"));
    graph->connect(math(ccl::NODE_MATH_MULTIPLY,
                        asFloat(in(node, "specular_weight")),
                        konst(0.5f)),
                   bsdf->input("Specular IOR Level"));
    graph->connect(asVec(in(node, "specular_color")), bsdf->input("Specular Tint"));
    graph->connect(asFloat(in(node, "specular_roughness_anisotropy")),
                   bsdf->input("Anisotropic"));

    // Transmission.
    graph->connect(asFloat(in(node, "transmission_weight")), bsdf->input("Transmission Weight"));

    // Subsurface. OpenPBR states a radius and a per-channel scale;
    // Cycles states a radius triple and one scale.
    graph->connect(asFloat(in(node, "subsurface_weight")), bsdf->input("Subsurface Weight"));
    graph->connect(asVec(in(node, "subsurface_radius_scale")), bsdf->input("Subsurface Radius"));
    graph->connect(asFloat(in(node, "subsurface_radius")), bsdf->input("Subsurface Scale"));
    graph->connect(asFloat(in(node, "subsurface_scatter_anisotropy")),
                   bsdf->input("Subsurface Anisotropy"));

    // Fuzz is Cycles' sheen.
    graph->connect(asFloat(in(node, "fuzz_weight")), bsdf->input("Sheen Weight"));
    graph->connect(asVec(in(node, "fuzz_color")), bsdf->input("Sheen Tint"));
    graph->connect(asFloat(in(node, "fuzz_roughness")), bsdf->input("Sheen Roughness"));

    // Coat.
    graph->connect(asFloat(in(node, "coat_weight")), bsdf->input("Coat Weight"));
    graph->connect(asFloat(in(node, "coat_roughness")), bsdf->input("Coat Roughness"));
    graph->connect(asFloat(in(node, "coat_ior")), bsdf->input("Coat IOR"));
    graph->connect(asVec(in(node, "coat_color")), bsdf->input("Coat Tint"));

    // Thin film. OpenPBR states the thickness in micrometres and gates
    // it with a weight; Cycles states nanometres and reads a zero
    // thickness as off.
    graph->connect(math(ccl::NODE_MATH_MULTIPLY,
                        math(ccl::NODE_MATH_MULTIPLY,
                             asFloat(in(node, "thin_film_weight")),
                             asFloat(in(node, "thin_film_thickness"))),
                        konst(1000.0f)),
                   bsdf->input("Thin Film Thickness"));
    graph->connect(asFloat(in(node, "thin_film_ior")), bsdf->input("Thin Film IOR"));

    // Emission. OpenPBR's emission_luminance is photometric (nits);
    // Cycles' strength is a plain multiplier, so this is a scale, not
    // a conversion -- an absolute-luminance material comes out bright
    // by whatever the scene's exposure says.
    graph->connect(asVec(in(node, "emission_color")), bsdf->input("Emission Color"));
    graph->connect(asFloat(in(node, "emission_luminance")), bsdf->input("Emission Strength"));

    // Geometry.
    graph->connect(asFloat(in(node, "geometry_opacity")), bsdf->input("Alpha"));
    if (node->getInput("geometry_normal"))
        graph->connect(asVec(in(node, "geometry_normal")), bsdf->input("Normal"));
    if (node->getInput("geometry_coat_normal"))
        graph->connect(asVec(in(node, "geometry_coat_normal")), bsdf->input("Coat Normal"));
    if (node->getInput("geometry_tangent"))
        graph->connect(asVec(in(node, "geometry_tangent")), bsdf->input("Tangent"));
    mx::InputPtr thin = node->getInput("geometry_thin_walled");
    if (thin && thin->hasValueString()
        && (thin->getValueString() == "true" || thin->getValueString() == "1"))
        bsdf->set_thin_wall(true);

    // The interior of a transmissive body absorbs over its depth --
    // Beer-Lambert, the same closure the fork's glass materials
    // already use, with the density read off the depth at which
    // transmission_color is reached.
    if (depthValue > 0.0f) {
        auto *absorb = graph->create_node<ccl::AbsorptionVolumeNode>();
        absorb->set_density(1.0f / depthValue);
        graph->connect(asVec(in(node, "transmission_color")), absorb->input("Color"));
        result.volume = absorb->output("Volume");
    }

    return bsdf->output("BSDF");
}

MaterialXResult Interpreter::run()
{
    // Which surface, the OpenPBR translation and the reason a document
    // states none are the raster path's answers too, so both consumers
    // read one implementation (Render::MaterialX::openPbrSurface): a
    // second copy is a second place for "the first material" to be
    // wrong.
    mx::NodePtr surface = Render::MaterialX::openPbrSurface(doc, surfaceName,
                                                            result.error,
                                                            result.warnings);
    if (!surface)
        return result;
    result.surface = buildOpenPbr(surface);
    return result;
}

}  // namespace

MaterialXResult buildMaterialXSurface(ccl::ShaderGraph *graph, const mx::DocumentPtr &doc,
                                      const std::string &surface)
{
    MaterialXResult result;
    if (!graph || !doc) {
        result.error = "no document";
        return result;
    }
    try {
        Interpreter interpreter(graph, doc, surface);
        result = interpreter.run();
    }
    catch (const std::exception &e) {
        result.surface = nullptr;
        result.error = e.what();
    }
    return result;
}

}  // namespace Render::Cycles
