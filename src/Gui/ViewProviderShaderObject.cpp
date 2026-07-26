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
# include <algorithm>
# include <set>
# include <QTimer>
# include <random>

# include <Inventor/actions/SoGetBoundingBoxAction.h>
# include <Inventor/nodes/SoCone.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoCylinder.h>
# include <Inventor/nodes/SoFragmentShader.h>
# include <Inventor/nodes/SoCoordinate3.h>
# include <Inventor/nodes/SoIndexedFaceSet.h>
# include <Inventor/nodes/SoMaterialBinding.h>
# include <Inventor/nodes/SoNormal.h>
# include <Inventor/nodes/SoNormalBinding.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoRotation.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShaderParameter.h>
# include <Inventor/nodes/SoShaderProgram.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoVertexShader.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "ViewProviderShaderObject.h"
#include "Application.h"
#include "Document.h"
#include "SoFCUnifiedSelection.h"
#include "Inventor/SoFCRenderCache.h"
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Inventor/SoFCRendererBridge.h"
#include "Renderer/Renderer.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"


FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

// Stage identifiers as interned SbNames: SbName's char* equality
// operators compare a program's Stage string against the one interned
// entry directly — no strcmp calls at the call sites, and no interning
// of arbitrary property values into the permanent name table.
namespace {
const SbName StageMaterial("material");
const SbName StageWater("water");
const SbName StageVolume("volume");
const SbName StageParticle("particle");
const SbName StagePost("post");
}

// Particle seed geometry (docs/RenderDebug.md §6.2, RenderEngine.md
// §5.8/§5.11): count degenerate quads whose 4 vertices coincide at a
// random anchor inside [bmin, bmax] — zero area, so stock rendering
// shows nothing; a particle vertex stage expands them into billboards
// from the seed attributes (a_normal.xy = corner ±1, a_normal.z = the
// particle's 0..1 index, a_color0 = the per-particle random seed).
// Deterministic per seed/count/box. Explicit element nodes, not
// SoVertexProperty — the render cache does not capture
// vertex-property-fed shapes. margin > 0 adds two INERT quads (zero
// billboard corners — invisible even expanded) at the margin-expanded
// box corners, so the geometry's own bounds cover the billboard travel
// and the auto near/far fit does not clip displaced particles.
static void buildEmitterSeedNodes(SoGroup *parent, int count,
                                  uint32_t seedval,
                                  const SbVec3f &bmin, const SbVec3f &bmax,
                                  float margin)
{
    count = std::max(1, count);
    int bounds = margin > 0.0f ? 2 : 0;
    int total = count + bounds;
    std::mt19937 gen(seedval);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    auto coords = new SoCoordinate3;
    auto norms = new SoNormal;
    auto pmat = new SoMaterial;
    coords->point.setNum(total * 4);
    norms->vector.setNum(total * 4);
    pmat->diffuseColor.setNum(total * 4);
    SbVec3f *verts = coords->point.startEditing();
    SbVec3f *normals = norms->vector.startEditing();
    SbColor *colors = pmat->diffuseColor.startEditing();
    auto ifs = new SoIndexedFaceSet;
    ifs->coordIndex.setNum(total * 5);
    int32_t *idx = ifs->coordIndex.startEditing();
    static const float corner[4][2] = {{-1, -1}, {1, -1},
                                       {1, 1}, {-1, 1}};
    SbVec3f ext = bmax - bmin;
    for (int i = 0; i < count; ++i) {
        SbVec3f anchor(bmin[0] + uni(gen) * ext[0],
                       bmin[1] + uni(gen) * ext[1],
                       bmin[2] + uni(gen) * ext[2]);
        uint32_t seed = uint32_t(gen());
        SbColor seedc(float((seed >> 24) & 0xff) / 255.0f,
                      float((seed >> 16) & 0xff) / 255.0f,
                      float((seed >> 8) & 0xff) / 255.0f);
        float f = count > 1 ? float(i) / float(count - 1) : 0.0f;
        for (int c = 0; c < 4; ++c) {
            verts[i * 4 + c] = anchor;
            normals[i * 4 + c] =
                SbVec3f(corner[c][0], corner[c][1], f);
            colors[i * 4 + c] = seedc;
            idx[i * 5 + c] = i * 4 + c;
        }
        idx[i * 5 + 4] = -1;
    }
    if (bounds) {
        float pad = margin * ext.length();
        SbVec3f cmin = bmin - SbVec3f(pad, pad, pad);
        SbVec3f cmax = bmax + SbVec3f(pad, pad, pad);
        for (int b = 0; b < 2; ++b) {
            int i = count + b;
            for (int c = 0; c < 4; ++c) {
                verts[i * 4 + c] = b ? cmax : cmin;
                normals[i * 4 + c] = SbVec3f(0.0f, 0.0f, 0.0f);
                colors[i * 4 + c] = SbColor(0.0f, 0.0f, 0.0f);
                idx[i * 5 + c] = i * 4 + c;
            }
            idx[i * 5 + 4] = -1;
        }
    }
    coords->point.finishEditing();
    norms->vector.finishEditing();
    pmat->diffuseColor.finishEditing();
    ifs->coordIndex.finishEditing();
    auto nbind = new SoNormalBinding;
    nbind->value = SoNormalBinding::PER_VERTEX_INDEXED;
    auto mbind = new SoMaterialBinding;
    mbind->value = SoMaterialBinding::PER_VERTEX_INDEXED;
    parent->addChild(coords);
    parent->addChild(norms);
    parent->addChild(nbind);
    parent->addChild(pmat);
    parent->addChild(mbind);
    parent->addChild(ifs);
}

// Appearance bindings hold a translated copy of the shader (not the Coin
// node), so program/effect edits must re-resolve the bindings of every
// Appearance referencing the given App::Shader.
static void pokeAppearancesOfShader(App::DocumentObject *shaderObj)
{
    std::set<App::Document*> docs;
    for (auto parent : shaderObj->getInList()) {
        if (parent && parent->isDerivedFrom(App::Appearance::getClassTypeId()))
            docs.insert(parent->getDocument());
    }
    for (auto doc : docs)
        ViewProviderAppearance::rebuildAllBindings(doc);
}

// Enumerate a shader-parameter carrier's Param_Name dynamic properties
// (docs/RenderDebug.md §6.4) as (uniform name, vec4-padded values),
// sorted by uniform name. Only the "Param" group binds — other dynamic
// properties stay ordinary properties.
static std::vector<std::pair<std::string, std::vector<float>>>
collectParamProps(App::DocumentObject *obj)
{
    static const char prefix[] = "Param_";
    static const size_t prefixLen = sizeof(prefix) - 1;
    std::vector<std::pair<std::string, std::vector<float>>> res;
    for (const auto &name : obj->getDynamicPropertyNames()) {
        if (name.compare(0, prefixLen, prefix) != 0
                || name.size() <= prefixLen)
            continue;
        auto prop = obj->getDynamicPropertyByName(name.c_str());
        if (!prop)
            continue;
        std::vector<float> values;
        if (!RendererBridge::translateShaderParamValues(prop, values)) {
            static std::set<std::string> warned;
            if (warned.insert(obj->getFullName() + "." + name).second)
                FC_WARN("shader parameter " << obj->getFullName() << "."
                        << name << ": unsupported property type "
                        << prop->getTypeId().getName());
            continue;
        }
        res.emplace_back(
                RendererBridge::shaderParamUniformName(name.c_str()),
                std::move(values));
    }
    std::sort(res.begin(), res.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    return res;
}

namespace {
// Dynamic-property add/remove only surfaces through the application
// signals (there is no view-provider hook), and the remove signal fires
// while the property still exists — resync from the event loop instead.
void deferShaderParamResync(App::DocumentObject *dynObj)
{
    App::DocumentObjectT ref(dynObj);
    QTimer::singleShot(0, [ref]() {
        auto obj = ref.getObject();
        if (!obj)
            return;
        if (obj->isDerivedFrom(App::ShaderProgram::getClassTypeId())) {
            auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                    Application::Instance->getViewProvider(obj));
            if (!vp)
                return;
            vp->syncParameters();
            for (auto parent : obj->getInList()) {
                if (parent
                        && parent->isDerivedFrom(App::Shader::getClassTypeId()))
                    pokeAppearancesOfShader(parent);
            }
        }
        else
            ViewProviderAppearance::rebuildAllBindings(obj->getDocument());
    });
}

void ensureDynPropConnections()
{
    static bool connected;
    if (connected)
        return;
    connected = true;
    auto handler = [](const App::Property &prop) {
        auto obj = dynamic_cast<App::DocumentObject*>(prop.getContainer());
        if (obj && (obj->isDerivedFrom(App::ShaderProgram::getClassTypeId())
                    || obj->isDerivedFrom(App::Appearance::getClassTypeId())))
            deferShaderParamResync(obj);
    };
    App::GetApplication().signalAppendDynamicProperty.connect(handler);
    App::GetApplication().signalRemoveDynamicProperty.connect(handler);
}
} // namespace

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderShaderProgram, Gui::ViewProviderDocumentObject)

ViewProviderShaderProgram::ViewProviderShaderProgram()
{
    pcShaderProgram = new SoShaderProgram;
    pcVertexShader = new SoVertexShader;
    pcFragmentShader = new SoFragmentShader;
}

ViewProviderShaderProgram::~ViewProviderShaderProgram() = default;

SoShaderProgram *ViewProviderShaderProgram::getShaderNode() const
{
    return pcShaderProgram;
}

void ViewProviderShaderProgram::attach(App::DocumentObject *obj)
{
    ViewProviderDocumentObject::attach(obj);
    ensureDynPropConnections();
    updateShaderNode();
    // The owning App::Shader's view provider may have attached before this
    // one existed (document restore order) — let it pick up the node now.
    for (auto parent : obj->getInList()) {
        if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
            continue;
        auto vp = dynamic_cast<ViewProviderShader*>(
                Application::Instance->getViewProvider(parent));
        if (vp)
            vp->updateDemo();
    }
}

void ViewProviderShaderProgram::updateData(const App::Property *prop)
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    // A dynamic property is a shader parameter (§6.4)
    bool dynParam = obj && prop && prop->getName()
            && obj->getDynamicPropertyByName(prop->getName()) == prop;
    if (obj && (dynParam
                || prop == &obj->Stage
                || prop == &obj->Dialect
                || prop == &obj->VertexProgram
                || prop == &obj->FragmentProgram
                || prop == &obj->Blend
                || prop == &obj->DepthWrite
                || prop == &obj->Enabled
                || prop == &obj->EmitterCount
                || prop == &obj->EmitterSeed
                || prop == &obj->EmitterSpread
                || prop == &obj->EmitterOffset
                || prop == &obj->EmitterMargin)) {
        if (dynParam)
            syncParameters();
        else
            updateShaderNode();
        for (auto parent : obj->getInList()) {
            if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
                continue;
            if (prop == &obj->Stage || prop == &obj->Enabled) {
                // stage / enablement decide whether the demo preview
                // includes the program
                auto vp = dynamic_cast<ViewProviderShader*>(
                        Application::Instance->getViewProvider(parent));
                if (vp)
                    vp->updateDemo();
            }
            pokeAppearancesOfShader(parent);
        }
    }
    ViewProviderDocumentObject::updateData(prop);
}

// Materialize an App::ShaderProgram plus resolved parameter values onto a
// Coin shader-node triple. Shared between the program view provider's own
// (library) node and the per-binding clones an Appearance builds to bake
// in its parameter overrides. All field writes are diffed so a no-op sync
// does not touch the nodes.
static void syncShaderNodes(App::ShaderProgram *obj,
        SoShaderProgram *program,
        SoVertexShader *vshader,
        SoFragmentShader *fshader,
        const std::vector<std::pair<std::string, std::vector<float>>> &params,
        std::map<std::string, CoinPtr<SoShaderParameterArray1f>> &paramNodes)
{
    SbName stage(obj->Stage.getValue());
    if (program->stage.getValue() != stage)
        program->stage = stage;

    int32_t sourcetype = obj->Dialect.getValue() == 0 ? SoShaderObject::BGFX_SC
                                                      : SoShaderObject::GLSL_PROGRAM;
    const char *vs = obj->VertexProgram.getValue();
    const char *fs = obj->FragmentProgram.getValue();

    if (vshader->sourceType.getValue() != sourcetype)
        vshader->sourceType = sourcetype;
    if (vshader->sourceProgram.getValue() != vs)
        vshader->sourceProgram = vs;
    if (fshader->sourceType.getValue() != sourcetype)
        fshader->sourceType = sourcetype;
    if (fshader->sourceProgram.getValue() != fs)
        fshader->sourceProgram = fs;

    SoNode *nodes[2];
    int num = 0;
    if (vs && vs[0])
        nodes[num++] = vshader;
    if (fs && fs[0])
        nodes[num++] = fshader;
    bool changed = program->shaderObject.getNum() != num;
    for (int i = 0; !changed && i < num; ++i)
        changed = program->shaderObject[i] != nodes[i];
    if (changed) {
        program->shaderObject.setNum(num);
        for (int i = 0; i < num; ++i)
            program->shaderObject.set1Value(i, nodes[i]);
    }

    // One SoShaderParameterArray1f per parameter (the values are already
    // vec4-padded floats, so one node type covers every property type),
    // updated in place so a value edit notifies the enclosing render
    // caches without relisting the parameter field.
    // A non-default render state (Blend/DepthWrite) rides the same
    // channel as a reserved "fc_state" parameter — no uniform prefix, the
    // backend consumes it as draw state at the user-draw submit instead
    // (docs/RenderDebug.md §6.2) — so it reaches every consumer of the
    // node (Appearance clones, snapshot transport) with no new fields.
    auto allParams = params;
    if (obj->Blend.getValue() != 0 || !obj->DepthWrite.getValue())
        allParams.emplace_back("fc_state", std::vector<float>{
                float(obj->Blend.getValue()),
                obj->DepthWrite.getValue() ? 1.0f : 0.0f, 0.0f, 0.0f});
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> next;
    for (const auto &v : allParams) {
        auto &node = next[v.first];
        if (!node) {
            auto it = paramNodes.find(v.first);
            if (it != paramNodes.end())
                node = it->second;
            else {
                node = new SoShaderParameterArray1f;
                node->name = v.first.c_str();
            }
        }
        int num = int(v.second.size());
        if (node->value.getNum() != num
                || memcmp(node->value.getValues(0), v.second.data(),
                          num * sizeof(float)) != 0) {
            node->value.setValues(0, num, v.second.data());
            if (node->value.getNum() != num)
                node->value.setNum(num);
        }
    }
    paramNodes = std::move(next);

    // The parameters ride the fragment shader object (always present for
    // a consumable program); a vertex-only program keeps them on the
    // vertex shader object.
    SoShaderObject *target = fshader;
    SoShaderObject *other = vshader;
    if (!fs || !fs[0])
        std::swap(target, other);
    if (other->parameter.getNum())
        other->parameter.setNum(0);
    bool relist = target->parameter.getNum() != int(paramNodes.size());
    int i = 0;
    for (auto &v : paramNodes) {
        if (relist)
            break;
        relist = target->parameter[i++] != v.second;
    }
    if (relist) {
        target->parameter.setNum(int(paramNodes.size()));
        i = 0;
        for (auto &v : paramNodes)
            target->parameter.set1Value(i++, v.second);
    }
}

void ViewProviderShaderProgram::updateShaderNode()
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;
    syncShaderNodes(obj, pcShaderProgram, pcVertexShader, pcFragmentShader,
                    collectParamProps(obj), paramNodes);
}

void ViewProviderShaderProgram::syncParameters()
{
    // field writes are diffed, so a full sync is the parameter sync
    updateShaderNode();
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderShader, Gui::ViewProviderDocumentObject)

ViewProviderShader::ViewProviderShader() = default;

ViewProviderShader::~ViewProviderShader() = default;

void ViewProviderShader::attach(App::DocumentObject *obj)
{
    ViewProviderDocumentObject::attach(obj);
    pcDemoRoot = new SoSeparator;
    addDisplayMaskMode(pcDemoRoot, "Demo");
    updateDemo();
}

std::vector<std::string> ViewProviderShader::getDisplayModes() const
{
    return {"Demo"};
}

void ViewProviderShader::setDisplayMode(const char *ModeName)
{
    if (strcmp(ModeName, "Demo") == 0)
        setDisplayMaskMode("Demo");
    ViewProviderDocumentObject::setDisplayMode(ModeName);
}

void ViewProviderShader::updateData(const App::Property *prop)
{
    auto obj = dynamic_cast<App::Shader*>(getObject());
    if (obj && (prop == &obj->Programs
                || prop == &obj->Demo
                || prop == &obj->DemoSize
                || prop == &obj->DemoRadius
                || prop == &obj->DemoHeight
                || prop == &obj->EmitterCount
                || prop == &obj->EmitterSeed))
        updateDemo();
    if (obj && prop == &obj->Programs)
        pokeAppearancesOfShader(obj);
    ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderShader::updateDemo()
{
    auto obj = dynamic_cast<App::Shader*>(getObject());
    if (!obj || !pcDemoRoot)
        return;

    pcDemoRoot->removeAllChildren();

    long demo = obj->Demo.getValue();
    if (demo == 0) // None
        return;

    // Shader programs ahead of the shape: the SoFCRenderMaterial placement
    // rules — they apply to the shapes captured after them in this cache.
    // Scene-level ("post") programs are skipped: activating those is the
    // Appearance object's job, and nested placement would be unreliable
    // anyway (pruned from recapture inside a valid cached separator).
    for (auto prog : obj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || progObj->Stage.getValue() == StagePost
                || !progObj->Enabled.getValue())
            continue;
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (vp && vp->getShaderNode())
            pcDemoRoot->addChild(vp->getShaderNode());
    }

    // Demo shapes carry no view-provider material chain; without an
    // explicit material the captured diffuse goes dark.
    auto mat = new SoMaterial;
    mat->diffuseColor = SbColor(0.8f, 0.8f, 0.8f);
    pcDemoRoot->addChild(mat);

    switch (demo) {
    case 1: { // Box
        auto cube = new SoCube;
        const auto &size = obj->DemoSize.getValue();
        cube->width = (float)size.x;
        cube->height = (float)size.y;
        cube->depth = (float)size.z;
        pcDemoRoot->addChild(cube);
        break;
    }
    case 2: { // Sphere
        auto sphere = new SoSphere;
        sphere->radius = (float)obj->DemoRadius.getValue();
        pcDemoRoot->addChild(sphere);
        break;
    }
    case 3: case 4: { // Cylinder / Cone: Coin's axis is +Y, rotate to Z-up
        auto rot = new SoRotation;
        rot->rotation = SbRotation(SbVec3f(1, 0, 0), (float)(M_PI / 2));
        pcDemoRoot->addChild(rot);
        if (demo == 3) {
            auto cyl = new SoCylinder;
            cyl->radius = (float)obj->DemoRadius.getValue();
            cyl->height = (float)obj->DemoHeight.getValue();
            pcDemoRoot->addChild(cyl);
        }
        else {
            auto cone = new SoCone;
            cone->bottomRadius = (float)obj->DemoRadius.getValue();
            cone->height = (float)obj->DemoHeight.getValue();
            pcDemoRoot->addChild(cone);
        }
        break;
    }
    case 5: { // Emitter: N degenerate seed quads (particle groundwork)
        // Shared seed builder (buildEmitterSeedNodes above); anchors
        // spread over the DemoSize box centered on the origin, with
        // the standard travel-headroom bounds quads.
        const auto &size = obj->DemoSize.getValue();
        SbVec3f half(float(size.x) * 0.5f, float(size.y) * 0.5f,
                     float(size.z) * 0.5f);
        buildEmitterSeedNodes(pcDemoRoot,
                              int(std::max(1L, obj->EmitterCount.getValue())),
                              uint32_t(obj->EmitterSeed.getValue()),
                              -half, half, 0.5f);
        break;
    }
    default:
        break;
    }
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderAppearance, Gui::ViewProviderLink)

namespace {
// Active Appearance view providers per document, for precedence
std::map<App::Document*, std::set<ViewProviderAppearance*>> _AppearanceRegistry;
bool _RebuildingBindings;

// Scope=Instance bindings depend on the whole document's structure — any
// object edit can create or remove an occurrence of a registered chain —
// so documents with active Appearances get their signals hooked, with
// rebuilds coalesced through the event loop.
struct DocumentHooks {
    boost::signals2::scoped_connection newObj;
    boost::signals2::scoped_connection delObj;
    boost::signals2::scoped_connection changedObj;
    // whether the last rebuild registered any chains: without chains a
    // link-property edit elsewhere cannot affect the bindings, and the
    // rebuild churn (node reinsertion → recapture) is not worth it
    bool hasChains = false;
};
std::map<App::Document*, DocumentHooks> _DocumentHooks;
std::set<std::string> _PendingRebuilds;

void scheduleRebuild(App::Document *doc)
{
    if (!doc || _RebuildingBindings)
        return;
    std::string name = doc->getName();
    if (!_PendingRebuilds.insert(name).second)
        return;
    QTimer::singleShot(0, [name]() {
        _PendingRebuilds.erase(name);
        if (auto doc = App::GetApplication().getDocument(name.c_str()))
            ViewProviderAppearance::rebuildAllBindings(doc);
    });
}

void ensureDocumentHooks(App::Document *doc)
{
    if (_DocumentHooks.count(doc))
        return;
    auto &hooks = _DocumentHooks[doc];
    hooks.newObj = doc->signalNewObject.connect(
        [doc](const App::DocumentObject &) { scheduleRebuild(doc); });
    hooks.delObj = doc->signalDeletedObject.connect(
        [doc](const App::DocumentObject &) { scheduleRebuild(doc); });
    hooks.changedObj = doc->signalChangedObject.connect(
        [doc](const App::DocumentObject &, const App::Property &prop) {
            auto it = _DocumentHooks.find(doc);
            if (it == _DocumentHooks.end() || !it->second.hasChains)
                return;
            // only link topology can change the occurrence set
            if (prop.isDerivedFrom(App::PropertyLinkBase::getClassTypeId()))
                scheduleRebuild(doc);
        });
}

App::DocumentObject *canonObject(App::DocumentObject *obj)
{
    auto linked = obj->getLinkedObject(true);
    return linked ? linked : obj;
}

bool isShaderFamily(App::DocumentObject *obj)
{
    return obj->isDerivedFrom(App::Shader::getClassTypeId())
        || obj->isDerivedFrom(App::ShaderProgram::getClassTypeId());
}

// The resolved object sequence a scene child contributes to occurrence
// matching: a subname link expands to the objects along its resolved path
// — skipping the link's own root, the suffix-anchored matching rule —
// anything else to its final linked object. A non-null element out
// parameter (Scope=Element target registration) splits a trailing
// element reference (e.g. Face3) off the subname before expansion.
void appendExpansion(App::DocumentObject *obj,
                     std::vector<App::DocumentObject*> &seq,
                     std::string *element = nullptr)
{
    if (element)
        element->clear();
    if (auto ext = obj->getExtensionByType<App::LinkBaseExtension>(true)) {
        if (auto prop = Base::freecad_dynamic_cast<App::PropertyXLink>(
                    ext->getLinkedObjectProperty())) {
            auto linked = prop->getValue();
            const char *sub = prop->getSubName();
            if (linked && sub && sub[0]) {
                std::string subNoElement = sub;
                if (element) {
                    App::SubObjectT ref(linked, sub);
                    *element = ref.getOldElementName();
                    subNoElement = ref.getSubNameNoElement();
                }
                if (!subNoElement.empty()) {
                    for (auto o : linked->getSubObjectList(subNoElement.c_str()))
                        seq.push_back(canonObject(o));
                    return;
                }
                // element-only subname: the chain is the linked object
                seq.push_back(canonObject(linked));
                return;
            }
        }
    }
    seq.push_back(canonObject(obj));
}

// A Scope=Instance/Element target child's registered chain
struct ChainBinding {
    std::vector<App::DocumentObject*> chain;
    // Scope=Element: the face element (e.g. "Face3") of the matched
    // occurrence the override is restricted to; empty = whole occurrence
    std::string element;
    ViewProviderAppearance *vp;
    App::Appearance *obj;
};

// Longest chain wins, then TreeRank (higher wins), then name
bool betterBinding(const ChainBinding &a, const ChainBinding &b)
{
    if (a.chain.size() != b.chain.size())
        return a.chain.size() > b.chain.size();
    long ra = a.obj->TreeRank.getValue();
    long rb = b.obj->TreeRank.getValue();
    if (ra != rb)
        return ra > rb;
    return strcmp(a.obj->getNameInDocument(),
                  b.obj->getNameInDocument()) > 0;
}

/** Depth-first scan of the document's logical scene occurrences.
 *
 * Every document object is a base (each object owns a top-level scene
 * instance; claimed or hidden instances capture empty and cost nothing at
 * render). Descent expands each object into its resolved sequence, so an
 * occurrence path "under" a subname link — whose scene graph shows only
 * the resolved tail with a baked transform — still matches the chain of
 * intermediate objects it resolves through. A match stops the descent:
 * the override applies to the whole subtree (outer binding wins over any
 * deeper match, the settled nested-precedence rule).
 */
struct OccurrenceScan {
    const std::vector<ChainBinding> &chains;
    std::map<const ChainBinding*,
             std::vector<std::pair<App::DocumentObject*, std::string>>> matches;
    std::vector<App::DocumentObject*> seq;
    std::set<App::DocumentObject*> onPath;
    size_t visited = 0;
    bool capped = false;
    static constexpr size_t maxVisited = 100000;
    static constexpr int maxDepth = 24;

    explicit OccurrenceScan(const std::vector<ChainBinding> &chains)
        : chains(chains)
    {}

    // Whole-occurrence bindings compete for one winner; element-scoped
    // bindings coexist with it and with each other, one winner per
    // element (a face override refines a whole-occurrence shader).
    void matchesAt(std::vector<const ChainBinding*> &out) const {
        const ChainBinding *whole = nullptr;
        std::map<std::string, const ChainBinding*> byElement;
        for (const auto &cb : chains) {
            const auto &c = cb.chain;
            if (c.empty() || c.size() > seq.size())
                continue;
            if (!std::equal(c.begin(), c.end(), seq.end() - c.size()))
                continue;
            if (cb.element.empty()) {
                if (!whole || betterBinding(cb, *whole))
                    whole = &cb;
            }
            else {
                auto &slot = byElement[cb.element];
                if (!slot || betterBinding(cb, *slot))
                    slot = &cb;
            }
        }
        if (whole)
            out.push_back(whole);
        for (const auto &v : byElement)
            out.push_back(v.second);
    }

    void visit(App::DocumentObject *base, App::DocumentObject *obj,
               std::string &subname, int depth)
    {
        if (++visited > maxVisited) {
            capped = true;
            return;
        }
        size_t seqlen = seq.size();
        appendExpansion(obj, seq);
        std::vector<const ChainBinding*> found;
        matchesAt(found);
        if (!found.empty()) {
            for (auto m : found)
                matches[m].emplace_back(base, subname);
        }
        else if (depth < maxDepth && !onPath.count(obj)) {
            onPath.insert(obj);
            for (const auto &s : obj->getSubObjects()) {
                if (s.empty())
                    continue;
                auto child = obj->getSubObject(s.c_str());
                if (!child || isShaderFamily(canonObject(child)))
                    continue;
                size_t sublen = subname.size();
                subname += s;
                visit(base, child, subname, depth + 1);
                subname.resize(sublen);
            }
            onPath.erase(obj);
        }
        seq.resize(seqlen);
    }
};
} // namespace

ViewProviderAppearance::ViewProviderAppearance() = default;

ViewProviderAppearance::~ViewProviderAppearance() = default;

// Like-named (Param_*) dynamic properties on the Appearance override the
// program's parameter values for this binding only (§6.4); a parameter
// the program does not carry is added, so a binding can set uniforms the
// shader source declares without a matching property on the program
// object.
static void applyParamOverrides(App::Appearance *obj,
                                Render::UserShader &shader)
{
    for (auto &v : collectParamProps(obj)) {
        auto it = std::find_if(shader.params.begin(), shader.params.end(),
                               [&](const auto &p) {
                                   return p.name == v.first;
                               });
        if (it != shader.params.end())
            it->values = std::move(v.second);
        else
            shader.params.push_back({std::move(v.first),
                                     std::move(v.second)});
    }
}

// The effect's object-scoped shader: the first non-"post" program of the
// bound App::Shader, translated off its view provider's Coin node.
static std::shared_ptr<const Render::UserShader>
resolveUserShader(App::Appearance *obj)
{
    auto shobj = obj->resolveShader();
    if (!shobj)
        return nullptr;
    for (auto prog : shobj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || progObj->Stage.getValue() == StagePost
                || !progObj->Enabled.getValue()
                || progObj->EmitterCount.getValue() > 0)
            continue;   // particle companions are never the main program
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (!vp || !vp->getShaderNode())
            continue;
        Render::UserShader shader;
        if (!RendererBridge::translateShaderProgram(vp->getShaderNode(),
                                                    shader))
            continue;
        applyParamOverrides(obj, shader);
        return std::make_shared<Render::UserShader>(std::move(shader));
    }
    return nullptr;
}

// The effect's scene-level programs: every "post"-stage program of the
// bound App::Shader in Programs order, with this Appearance's parameter
// overrides applied. Activated by an Appearance with no target children
// (§6.5).
static std::vector<Render::UserShader>
resolvePostShaders(App::Appearance *obj)
{
    std::vector<Render::UserShader> res;
    auto shobj = obj->resolveShader();
    if (!shobj)
        return res;
    for (auto prog : shobj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || progObj->Stage.getValue() != StagePost
                || !progObj->Enabled.getValue())
            continue;
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (!vp || !vp->getShaderNode())
            continue;
        Render::UserShader shader;
        if (!RendererBridge::translateShaderProgram(vp->getShaderNode(),
                                                    shader))
            continue;
        applyParamOverrides(obj, shader);
        res.push_back(std::move(shader));
    }
    return res;
}

void ViewProviderAppearance::attach(App::DocumentObject *obj)
{
    ViewProviderLink::attach(obj);
    ensureDynPropConnections();
    ensureDocumentHooks(obj->getDocument());
    _AppearanceRegistry[obj->getDocument()].insert(this);
    if (!obj->isRestoring())
        rebuildAllBindings(obj->getDocument());
}

void ViewProviderAppearance::finishRestoring()
{
    ViewProviderLink::finishRestoring();
    if (auto obj = getObject())
        rebuildAllBindings(obj->getDocument());
}

void ViewProviderAppearance::beforeDelete()
{
    auto obj = getObject();
    App::Document *doc = obj ? obj->getDocument() : nullptr;
    clearBindings();
    if (doc) {
        auto it = _AppearanceRegistry.find(doc);
        if (it != _AppearanceRegistry.end()) {
            it->second.erase(this);
            if (it->second.empty()) {
                _AppearanceRegistry.erase(it);
                _DocumentHooks.erase(doc);
            }
            // Runs on the emptied registry too — the views' scene-level
            // shader list must clear with the last Appearance.
            rebuildAllBindings(doc);
        }
    }
    ViewProviderLink::beforeDelete();
}

void ViewProviderAppearance::updateData(const App::Property *prop)
{
    ViewProviderLink::updateData(prop);
    auto obj = dynamic_cast<App::Appearance*>(getObject());
    // dynamic properties are per-binding parameter overrides (§6.4)
    if (obj && !obj->isRestoring()
            && (prop == &obj->ElementList
                || prop == &obj->Scope
                || prop == &obj->TreeRank
                || (prop && prop->getName()
                    && obj->getDynamicPropertyByName(prop->getName())
                        == prop)))
        rebuildAllBindings(obj->getDocument());
}

void ViewProviderAppearance::onChanged(const App::Property *prop)
{
    ViewProviderLink::onChanged(prop);
    // Hiding an Appearance deactivates its bindings (the next-ranked
    // binding on the same target takes over).
    if (prop == &Visibility && getObject())
        rebuildAllBindings(getObject()->getDocument());
}

void ViewProviderAppearance::clearBindings()
{
    for (auto &v : bound) {
        if (!v.first)
            continue;
        if (auto mgr = v.first->getRenderCacheManager())
            mgr->removeShaderOverride(v.second);
    }
    bound.clear();
    for (auto &v : attached) {
        int idx = v.first->findChild(v.second);
        if (idx >= 0)
            v.first->removeChild(idx);
    }
    attached.clear();
}

void ViewProviderAppearance::applyPathBindings(
        const std::vector<std::pair<App::DocumentObject*,
                                    std::string>> &targets,
        const std::string &element)
{
    auto obj = dynamic_cast<App::Appearance*>(getObject());
    if (!obj || targets.empty())
        return;
    auto shader = resolveUserShader(obj);
    if (!shader)
        return;
    auto gdoc = Application::Instance->getDocument(obj->getDocument());
    if (!gdoc)
        return;
    for (auto view : gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
        auto viewer = static_cast<View3DInventor*>(view)->getViewer();
        auto mgr = viewer ? viewer->getRenderCacheManager() : nullptr;
        if (!mgr)
            continue;
        for (const auto &t : targets) {
            auto vpd = dynamic_cast<ViewProviderDocumentObject*>(
                    Application::Instance->getViewProvider(t.first));
            if (!vpd)
                continue;
            // Scope=Element: the element ref appended to the occurrence
            // subname resolves through to the tail shape's SoDetail
            std::string sub = t.second;
            sub += element;
            CoinPtr<SoPath> path = new SoPath(10);
            viewer->appendDetailPath(path, vpd);
            SoDetail *det = nullptr;
            bool ok = vpd->getDetailPath(sub.c_str(),
                    static_cast<SoFullPath*>(path.get()), true, det);
            if (!ok || !path->getLength()) {
                delete det;
                continue;
            }
            if (!element.empty() && !det) {
                FC_WARN("Appearance " << obj->getFullName()
                        << ": no detail for element "
                        << t.first->getFullName() << "." << sub);
                continue;
            }
            std::string key = obj->getFullName();
            key += '|';
            key += t.first->getFullName();
            key += '.';
            key += sub;
            FC_LOG("AP bind " << key << " pathlen " << path->getLength());
            mgr->addShaderOverride(key, path, shader, det);
            delete det;
            bound.emplace_back(viewer, std::move(key));
        }
    }
}

SoShaderProgram *ViewProviderAppearance::ownProgramNode()
{
    auto obj = dynamic_cast<App::Appearance*>(getObject());
    auto shobj = obj ? obj->resolveShader() : nullptr;
    App::ShaderProgram *progObj = nullptr;
    if (shobj) {
        // Direct attachment inserts a real node into target graphs, and
        // the capture callback routes only "material"/"water"-stage
        // programs into the enclosing cache — anything else would leak
        // into the scene-level list, so the stage filter is strict here.
        for (auto prog : shobj->Programs.getValues()) {
            auto p = dynamic_cast<App::ShaderProgram*>(prog);
            if (!p || !p->Enabled.getValue()
                   || p->EmitterCount.getValue() > 0)
                continue;   // particle companions are never the main program
            const char *st = p->Stage.getValue();
            if (st == StageMaterial || st == StageWater
                || st == StageVolume) {
                progObj = p;
                break;
            }
        }
    }
    if (!progObj)
        return nullptr;

    if (!pcOwnProgram) {
        pcOwnProgram = new SoShaderProgram;
        pcOwnVertexShader = new SoVertexShader;
        pcOwnFragmentShader = new SoFragmentShader;
    }

    // The program's parameters overridden per binding by this
    // Appearance's like-named Param_* dynamic properties (§6.4); an
    // override the program does not declare is appended.
    auto params = collectParamProps(progObj);
    for (auto &v : collectParamProps(obj)) {
        auto it = std::find_if(params.begin(), params.end(),
                               [&](const auto &p) {
                                   return p.first == v.first;
                               });
        if (it != params.end())
            it->second = std::move(v.second);
        else
            params.push_back(std::move(v));
    }
    std::sort(params.begin(), params.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    syncShaderNodes(progObj, pcOwnProgram, pcOwnVertexShader,
                    pcOwnFragmentShader, params, ownParamNodes);
    return pcOwnProgram;
}

void ViewProviderAppearance::applyDirectBindings(
        const std::vector<App::DocumentObject*> &targets)
{
    if (targets.empty())
        return;
    auto node = ownProgramNode();
    // Particle companion programs of the effect (EmitterCount > 0,
    // docs/RenderEngine.md §5.11): each gets seed geometry generated
    // per target below, fit to the target's bounding box.
    auto obj = dynamic_cast<App::Appearance*>(getObject());
    auto shobj = obj ? obj->resolveShader() : nullptr;
    std::vector<App::ShaderProgram*> emitters;
    if (shobj) {
        for (auto prog : shobj->Programs.getValues()) {
            auto p = dynamic_cast<App::ShaderProgram*>(prog);
            if (p && p->Enabled.getValue()
                  && p->EmitterCount.getValue() > 0
                  && p->Stage.getValue() == StageParticle)
                emitters.push_back(p);
        }
    }
    if (!node && emitters.empty())
        return;
    for (auto t : targets) {
        auto vpd = dynamic_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(t));
        if (!vpd || !vpd->getRoot())
            continue;
        SoGroup *root = vpd->getRoot();
        if (node && root->findChild(node) < 0) {
            // Child 0: captured into the target's own render cache ahead
            // of everything, then merged down through all child caches and
            // every instance (the shared-snapshot + mergeMaterial path).
            FC_LOG("AP attach " << t->getFullName());
            root->insertChild(node, 0);
            attached.emplace_back(root, node);
        }
        if (emitters.empty())
            continue;
        // Target bounds in the root frame (the same frame a child-0
        // separator's coordinates live in — before the placement
        // transform child applies to later siblings, so the
        // placement-inclusive view-provider bbox matches). A bare
        // SoGetBoundingBoxAction on the root would come back empty:
        // the geometry hides inside the display-mode SoFCSwitch.
        Base::BoundBox3d bb = vpd->getBoundingBox();
        if (!bb.IsValid()) {
            FC_WARN("particle emitter: empty bounds on "
                    << t->getFullName() << ", seeds skipped");
            continue;
        }
        SbBox3f box(float(bb.MinX), float(bb.MinY), float(bb.MinZ),
                    float(bb.MaxX), float(bb.MaxY), float(bb.MaxZ));
        FC_LOG("AP emitter bounds " << t->getFullName() << " ("
               << box.getMin()[0] << "," << box.getMin()[1] << ","
               << box.getMin()[2] << ")-(" << box.getMax()[0] << ","
               << box.getMax()[1] << "," << box.getMax()[2] << ")");
        for (auto p : emitters) {
            SbVec3f bmin = box.getMin(), bmax = box.getMax();
            SbVec3f c = (bmin + bmax) * 0.5f, ext = bmax - bmin;
            const auto &spread = p->EmitterSpread.getValue();
            const auto &offset = p->EmitterOffset.getValue();
            c += SbVec3f(float(offset.x) * ext[0],
                         float(offset.y) * ext[1],
                         float(offset.z) * ext[2]);
            SbVec3f half(0.5f * float(spread.x) * ext[0],
                         0.5f * float(spread.y) * ext[1],
                         0.5f * float(spread.z) * ext[2]);
            auto pvp = dynamic_cast<ViewProviderShaderProgram*>(
                    Application::Instance->getViewProvider(p));
            if (!pvp || !pvp->getShaderNode())
                continue;
            // Own SoFCSelectionRoot: setUserShader stamps the CACHE
            // material (last-set wins within a cache), so the seeds
            // need their own render cache or the effect's main program
            // recaptures them; the "particle" stage then survives the
            // parent merge-down (SoFCRenderCache mergeMaterial).
            // Inserted at child 0 (root frame, matching the bounds).
            auto sep = new SoFCSelectionRoot;
            sep->addChild(pvp->getShaderNode());
            buildEmitterSeedNodes(
                sep, int(p->EmitterCount.getValue()),
                uint32_t(p->EmitterSeed.getValue()),
                c - half, c + half,
                float(p->EmitterMargin.getValue()));
            root->insertChild(sep, 0);
            attached.emplace_back(root, sep);
        }
    }
}

void ViewProviderAppearance::onViewCreated(App::Document *doc)
{
    // Deferred + coalesced: the caller is still constructing the view,
    // and a restore may open several views at once.
    if (doc && _AppearanceRegistry.count(doc))
        scheduleRebuild(doc);
}

void ViewProviderAppearance::rebuildAllBindings(App::Document *doc)
{
    if (!doc || _RebuildingBindings)
        return;
    Base::StateLocker guard(_RebuildingBindings);
    // A missing registry entry (last Appearance deleted) still runs the
    // tail: the views' scene-level shader list must clear too.
    auto it = _AppearanceRegistry.find(doc);
    std::vector<ViewProviderAppearance*> vps;
    if (it != _AppearanceRegistry.end())
        vps.assign(it->second.begin(), it->second.end());

    for (auto vp : vps)
        vp->clearBindings();

    // Shader-only Appearances activate scene-level post programs (§6.5)
    std::vector<App::Appearance*> sceneObjs;
    // Scope=Object: winner per resolved final target object
    std::map<App::DocumentObject*, ViewProviderAppearance*> directWinners;
    // Scope=Instance: registered chains, matched over occurrences below
    std::vector<ChainBinding> chains;

    for (auto vp : vps) {
        auto obj = dynamic_cast<App::Appearance*>(vp->getObject());
        if (!obj || !vp->Visibility.getValue())
            continue;
        if (!obj->resolveShader())
            continue;
        auto targets = obj->getTargets();
        if (targets.empty()) {
            sceneObjs.push_back(obj);
            continue;
        }
        if (obj->scopeMode() == App::Appearance::ScopeMode::Object) {
            for (auto t : targets) {
                auto resolved = canonObject(t);
                if (isShaderFamily(resolved))
                    continue;
                auto r = directWinners.emplace(resolved, vp);
                if (r.second)
                    continue;
                auto other = dynamic_cast<App::Appearance*>(
                        r.first->second->getObject());
                long rank = obj->TreeRank.getValue();
                long otherrank = other ? other->TreeRank.getValue() : 0;
                if (rank > otherrank
                        || (rank == otherrank && other
                            && strcmp(obj->getNameInDocument(),
                                      other->getNameInDocument()) > 0))
                    r.first->second = vp;
            }
        }
        else {
            bool elementScope =
                obj->scopeMode() == App::Appearance::ScopeMode::Element;
            for (auto t : targets) {
                ChainBinding cb;
                appendExpansion(t, cb.chain,
                                elementScope ? &cb.element : nullptr);
                if (cb.chain.empty()
                        || isShaderFamily(cb.chain.back()))
                    continue;
                cb.vp = vp;
                cb.obj = obj;
                chains.push_back(std::move(cb));
            }
        }
    }

    {
        std::map<ViewProviderAppearance*,
                 std::vector<App::DocumentObject*>> directTargets;
        for (const auto &v : directWinners)
            directTargets[v.second].push_back(v.first);
        for (auto &v : directTargets)
            v.first->applyDirectBindings(v.second);
    }

    if (!chains.empty()) {
        OccurrenceScan scan(chains);
        for (auto obj : doc->getObjects()) {
            if (isShaderFamily(canonObject(obj)))
                continue;
            std::string subname;
            scan.visit(obj, obj, subname, 0);
        }
        if (scan.capped)
            FC_WARN("Appearance occurrence scan of " << doc->getName()
                    << " truncated at " << OccurrenceScan::maxVisited
                    << " nodes — some occurrences may be unbound");
        // Whole-occurrence bindings before element ones: element entries
        // then register later selection ids, so a face override draws
        // after (over) a whole-occurrence shader on the same occurrence.
        for (int round = 0; round < 2; ++round) {
            for (auto &m : scan.matches) {
                if (m.first->element.empty() != (round == 0))
                    continue;
                for (auto &t : m.second)
                    FC_LOG("AP occurrence " << m.first->obj->getNameInDocument()
                            << " -> " << t.first->getFullName() << " . "
                            << t.second << m.first->element);
                m.first->vp->applyPathBindings(m.second, m.first->element);
            }
        }
    }

    if (auto h = _DocumentHooks.find(doc); h != _DocumentHooks.end())
        h->second.hasChains = !chains.empty();

    // Scene-level activation: the shader-only Appearances' post-stage
    // programs, ordered ascending by TreeRank (name fallback) so the
    // highest-ranked Appearance lands last — the winning slot of the
    // backend's "last shader on a stage wins" rule, matching the
    // per-target precedence direction.
    std::sort(sceneObjs.begin(), sceneObjs.end(),
              [](App::Appearance *a, App::Appearance *b) {
                  long ra = a->TreeRank.getValue();
                  long rb = b->TreeRank.getValue();
                  if (ra != rb)
                      return ra < rb;
                  return strcmp(a->getNameInDocument(),
                                b->getNameInDocument()) < 0;
              });
    std::vector<Render::UserShader> sceneShaders;
    for (auto obj : sceneObjs) {
        auto post = resolvePostShaders(obj);
        sceneShaders.insert(sceneShaders.end(),
                            std::make_move_iterator(post.begin()),
                            std::make_move_iterator(post.end()));
    }
    if (auto gdoc = Application::Instance->getDocument(doc)) {
        for (auto view :
                gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
            auto viewer = static_cast<View3DInventor*>(view)->getViewer();
            auto mgr = viewer ? viewer->getRenderCacheManager() : nullptr;
            if (!mgr)
                continue;
            auto copy = sceneShaders;
            mgr->setAppearanceShaders(std::move(copy));
            viewer->getSoRenderManager()->scheduleRedraw();
        }
    }
}

// Python features ------------------------------------------------------------

namespace Gui {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderShaderProgramPython, Gui::ViewProviderShaderProgram)
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderShaderPython, Gui::ViewProviderShader)
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderAppearancePython, Gui::ViewProviderAppearance)
/// @endcond

// explicit template instantiation
template class GuiExport ViewProviderPythonFeatureT<ViewProviderShaderProgram>;
template class GuiExport ViewProviderPythonFeatureT<ViewProviderShader>;
template class GuiExport ViewProviderPythonFeatureT<ViewProviderAppearance>;
}
