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
# include <cctype>
# include <map>
# include <set>
# include <sstream>
# include <cstring>
# include <QAction>
# include <QMenu>
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
# include <Inventor/nodes/SoTransform.h>
# include <Inventor/nodes/SoShaderParameter.h>
# include <Inventor/nodes/SoShaderProgram.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoVertexShader.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObserver.h>
#include <App/MaterialXDocument.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Tools.h>

#include "ViewProviderShaderObject.h"
#include "Application.h"
#include "ViewProviderGeometryObject.h"
#include "Renderer/MaterialXSupport.h"
#ifdef FC_SHADER_GRAPH_EDITOR
# include "ShaderGraphView.h"
#endif
#include "ActionFunction.h"
#include "MainWindow.h"
#include "ViewPlacement.h"
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
// vertex-property-fed shapes.
//
// The travel headroom (EmitterMargin) is deliberately NOT geometry
// here. It used to be two inert quads at the margin-expanded corners,
// which did cover the billboard travel — but a bounding box has more
// than one consumer, and they do not want the same box. Framing wants
// the seed box: expanded, a 7-wide emitter reports 24 and "fit all"
// backs off over a box nothing ever fills, so any scene carrying an
// effect opened badly framed. Culling wants the expansion, because a
// particle displaced out of the seed box is still on screen. The
// spawn box wants the seed box too — fcParticleSpawn is documented as
// a uniform anchor in the SEED box, and the expanded bounds silently
// made it spawn over the headroom as well. So the geometry stays
// tight and the margin travels as data (the reserved fc_emitter
// parameter), applied by the renderer at the one consumer that wants
// it.
static void buildEmitterSeedNodes(SoGroup *parent, int count,
                                  uint32_t seedval,
                                  const SbVec3f &bmin, const SbVec3f &bmax)
{
    count = std::max(1, count);
    int total = count;
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

// ShaderBinding objects hold a translated copy of the shader (not the Coin
// node), so program/effect edits must re-resolve the bindings of every
// ShaderBinding referencing the given App::Shader.
static void pokeAppearancesOfShader(App::DocumentObject *shaderObj)
{
    std::set<App::Document*> docs;
    for (auto parent : shaderObj->getInList()) {
        if (parent && parent->isDerivedFrom(App::ShaderBinding::getClassTypeId()))
            docs.insert(parent->getDocument());
    }
    for (auto doc : docs)
        ViewProviderShaderBinding::rebuildAllBindings(doc);
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
            ViewProviderShaderBinding::rebuildAllBindings(obj->getDocument());
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
                    || obj->isDerivedFrom(App::ShaderBinding::getClassTypeId())))
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
    pcSimulateShader = new SoFragmentShader;
}

static void forgetCarriedDocument(const App::ShaderProgram *obj);

ViewProviderShaderProgram::~ViewProviderShaderProgram()
{
    if (auto obj = dynamic_cast<App::ShaderProgram*>(getObject()))
        forgetCarriedDocument(obj);
}

SoShaderProgram *ViewProviderShaderProgram::getShaderNode() const
{
    return pcShaderProgram;
}

// ---- the shader graph editor (docs/ShaderGraphEditor.md sec 4.4) ----

bool ViewProviderShaderProgram::hasGraphEditor() const
{
#ifdef FC_SHADER_GRAPH_EDITOR
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return false;
    const char *dialect = obj->Dialect.getValueAsString();
    return dialect && std::strcmp(dialect, "MATERIALX") == 0;
#else
    return false;
#endif
}

MDIView *ViewProviderShaderProgram::getMDIView() const
{
#ifdef FC_SHADER_GRAPH_EDITOR
    auto doc = getDocument();
    if (!doc)
        return nullptr;
    for (auto v : doc->getMDIViewsOfType(ShaderGraphView::getClassTypeId())) {
        auto view = static_cast<ShaderGraphView*>(v);
        if (view->getProgram() == getObject())
            return view;
    }
#endif
    return nullptr;
}

bool ViewProviderShaderProgram::activateView() const
{
    if (auto view = getMDIView()) {
        // An already-open view: the reveal half of rule 0, so Alt
        // relocates it (docs/ViewPlacement.md sec 4.2).
        ViewPlacement::reveal(view, getDocument(), true);
        return true;
    }
    return false;
}

void ViewProviderShaderProgram::show()
{
#ifdef FC_SHADER_GRAPH_EDITOR
    // Restore of an O:<name> split cell (Document.cpp) calls show()
    // and then asks getMDIView() for the view it made.
    if (hasGraphEditor() && !getMDIView()) {
        auto obj = static_cast<App::ShaderProgram*>(getObject());
        auto view = new ShaderGraphView(obj, getMainWindow());
        ViewPlacement::place(view, ViewPlacement::Category::DocView,
                             getDocument());
    }
#endif
    ViewProviderDocumentObject::show();
}

bool ViewProviderShaderProgram::doubleClicked()
{
    if (!hasGraphEditor())
        return ViewProviderDocumentObject::doubleClicked();
    // Reveal-if-open first, never a second view of one program.
    if (!activateView())
        show();
    return true;
}

void ViewProviderShaderProgram::setupContextMenu(QMenu *menu, QObject *receiver,
                                                 const char *member)
{
    if (hasGraphEditor()) {
        auto func = new Gui::ActionFunction(menu);
        QAction *act = menu->addAction(QObject::tr("Edit shader graph"));
        func->trigger(act, [this]() { this->doubleClicked(); });
    }
    ViewProviderDocumentObject::setupContextMenu(menu, receiver, member);
}

void ViewProviderShaderProgram::beforeDelete()
{
    // The view is a view over this object's property; it goes with
    // the object.
    if (auto view = getMDIView())
        view->close();
    ViewProviderDocumentObject::beforeDelete();
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

// The program sources are stored as shared files (App::PropertyStringIncluded),
// and blob content is handed to its properties only once the archive entries
// have been drained -- which happens AFTER the view document has been read and
// this view provider attached. The node attach() built is therefore built from
// text that had not arrived yet, so it is built again here, and everything that
// consumes it is told.
void ViewProviderShaderProgram::finishRestoring()
{
    ViewProviderDocumentObject::finishRestoring();
    updateShaderNode();
    auto obj = getObject();
    if (!obj)
        return;
    for (auto parent : obj->getInList()) {
        if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
            continue;
        if (auto vp = dynamic_cast<ViewProviderShader*>(
                    Application::Instance->getViewProvider(parent)))
            vp->updateDemo();
        pokeAppearancesOfShader(parent);
    }
}

void ViewProviderShaderProgram::updateData(const App::Property *prop)
{
#ifdef FC_SHADER_GRAPH_EDITOR
    if (auto obj = getObject(); obj && prop == &obj->Label) {
        if (auto view = static_cast<ShaderGraphView*>(getMDIView()))
            view->labelChanged();
    }
#endif
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    // A dynamic property is a shader parameter (§6.4)
    bool dynParam = obj && prop && prop->getName()
            && obj->getDynamicPropertyByName(prop->getName()) == prop;
    if (obj && (dynParam
                || prop == &obj->Stage
                || prop == &obj->Dialect
                || prop == &obj->VertexProgram
                || prop == &obj->FragmentProgram
                || prop == &obj->Surface
                || prop == &obj->SimulateProgram
                || prop == &obj->Blend
                || prop == &obj->DepthWrite
                || prop == &obj->Enabled
                || prop == &obj->EmitterCount
                || prop == &obj->EmitterSeed
                || prop == &obj->EmitterSpread
                || prop == &obj->EmitterOffset
                || prop == &obj->EmitterMargin
                || prop == &obj->EmitterRate
                || prop == &obj->EmitterTimeScale
                || prop == &obj->EmitterWarmup)) {
        if (dynParam)
            syncParameters();
        else
            updateShaderNode();
        for (auto parent : obj->getInList()) {
            if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
                continue;
            if (prop == &obj->Stage || prop == &obj->Enabled
                    || prop == &obj->EmitterCount
                    || prop == &obj->EmitterSeed
                    || prop == &obj->EmitterSpread
                    || prop == &obj->EmitterOffset
                    || prop == &obj->EmitterMargin) {
                // stage / enablement decide whether the demo preview
                // includes the program; the emitter props shape its
                // demo seed sub-root
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

// Whether a MATERIALX-dialect program's text is a PATH to a document
// rather than the document itself. A MaterialX document is XML, so its
// first non-blank character is always '<'.
static bool isDocumentPath(const char *text)
{
    if (!text)
        return false;
    while (*text && std::isspace(static_cast<unsigned char>(*text)))
        ++text;
    return *text && *text != '<';
}

// The document with every image reference it carries replaced by the
// path the stored file is at.
//
// Memoized, because this parses the document and a binding rebuild syncs
// every clone of it, while the answer changes only when the text or the
// stored files do -- which is exactly what the key is made of. ONE slot
// per program: the answer for a program's previous text is never asked
// for again, and a cache keyed by the text alone kept every version of
// every document edited in a session. The slot goes with the view
// provider (forgetCarriedDocument).
namespace {
struct CarriedDocument
{
    std::string key;
    std::string text;
};
std::map<const App::ShaderProgram*, CarriedDocument> _carriedDocuments;
}  // namespace

static void forgetCarriedDocument(const App::ShaderProgram *obj)
{
    _carriedDocuments.erase(obj);
}

static std::string documentWithStoredImages(App::ShaderProgram *obj, const char *xml)
{
    if (!xml || !xml[0])
        return {};
    std::string key(xml);
    std::vector<Render::MaterialX::ImageReference> files;
    for (const auto &file : obj->Images.getValues()) {
        std::string path = obj->Images.filePath(file.name.c_str());
        if (path.empty()) {
            continue;   // content not arrived; the document keeps its own name
        }
        key += '\0';
        key += file.name;
        key += '\0';
        key += path;
        files.push_back({file.name, std::move(path)});
    }
    if (files.empty())
        return xml;

    CarriedDocument &slot = _carriedDocuments[obj];
    if (slot.key != key) {
        slot.text = Render::MaterialX::substituteImages(xml, files);
        slot.key = std::move(key);
    }
    return slot.text;
}

// Materialize an App::ShaderProgram plus resolved parameter values onto a
// Coin shader-node triple. Shared between the program view provider's own
// (library) node and the per-binding clones a ShaderBinding builds to bake
// in its parameter overrides. All field writes are diffed so a no-op sync
// does not touch the nodes.
static void syncShaderNodes(App::ShaderProgram *obj,
        SoShaderProgram *program,
        SoVertexShader *vshader,
        SoFragmentShader *fshader,
        SoFragmentShader *simshader,
        const std::vector<std::pair<std::string, std::vector<float>>> &params,
        std::map<std::string, CoinPtr<SoShaderParameterArray1f>> &paramNodes)
{
    SbName stage(obj->Stage.getValue());
    if (program->stage.getValue() != stage)
        program->stage = stage;

    // App::ShaderProgram::DialectEnums order
    int32_t sourcetype = SoShaderObject::GLSL_PROGRAM;
    switch (obj->Dialect.getValue()) {
    case 0: sourcetype = SoShaderObject::BGFX_SC; break;
    case 2: sourcetype = SoShaderObject::MATERIALX; break;
    default: break;
    }
    const char *vs = obj->VertexProgram.getValue();
    const char *fs = obj->FragmentProgram.getValue();
    // A MaterialX document may be stated as a PATH instead of inline
    // (docs/CyclesIntegration.md sec 8 item 15): a real material names
    // its images relative to its own document, and only the file route
    // gives the consumer something to resolve them against. Coin's
    // FILENAME source type resolves a .mtlx suffix back to MATERIALX,
    // so this needs no second field -- and a document, being XML,
    // never looks like a path.
    if (sourcetype == SoShaderObject::MATERIALX && isDocumentPath(fs)) {
        sourcetype = SoShaderObject::FILENAME;
        vs = "";
    }

    // A document that carries its images is handed over naming them
    // where they are on THIS machine: the stored files live in the
    // transient directory, so what goes into the node is the document
    // with each reference replaced by that path
    // (docs/MaterialStorage.md sec 16). Everything downstream -- the
    // capture, the generator, the path tracer -- goes on opening files
    // and none of them has to learn what a blob is.
    //
    // Only for the inline route: a document stated as a PATH is read
    // by the consumer from a file that is there, and resolves its own
    // images against it.
    std::string carried;
    if (sourcetype == SoShaderObject::MATERIALX && !obj->Images.isEmpty()) {
        carried = documentWithStoredImages(obj, fs);
        fs = carried.c_str();
    }
    // The state step of a stateful emitter rides as the program's
    // second fragment object (docs/RenderEngine.md §5.8); it is only
    // meaningful with a beauty fragment stage ahead of it, which is
    // what tells the two apart on the way out.
    const char *ss = obj->SimulateProgram.getValue();
    if (!fs || !fs[0])
        ss = "";

    if (vshader->sourceType.getValue() != sourcetype)
        vshader->sourceType = sourcetype;
    if (vshader->sourceProgram.getValue() != vs)
        vshader->sourceProgram = vs;
    if (fshader->sourceType.getValue() != sourcetype)
        fshader->sourceType = sourcetype;
    if (fshader->sourceProgram.getValue() != fs)
        fshader->sourceProgram = fs;
    // Which surface of the document the program wears (sec 17.13). On
    // the fragment object alone: that is where the document is, and the
    // vertex object of a MATERIALX program carries nothing.
    const char *surface = obj->Surface.getValue();
    if (fshader->sourceSurface.getValue() != surface)
        fshader->sourceSurface = surface;
    if (simshader->sourceType.getValue() != sourcetype)
        simshader->sourceType = sourcetype;
    if (simshader->sourceProgram.getValue() != ss)
        simshader->sourceProgram = ss;

    SoNode *nodes[3];
    int num = 0;
    if (vs && vs[0])
        nodes[num++] = vshader;
    if (fs && fs[0])
        nodes[num++] = fshader;
    if (ss && ss[0])
        nodes[num++] = simshader;
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
    // node (ShaderBinding clones, snapshot transport) with no new fields.
    auto allParams = params;
    if (obj->Blend.getValue() != 0 || !obj->DepthWrite.getValue())
        allParams.emplace_back("fc_state", std::vector<float>{
                float(obj->Blend.getValue()),
                obj->DepthWrite.getValue() ? 1.0f : 0.0f, 0.0f, 0.0f});
    // Reserved "fc_emitter" = what the backend must know to run a
    // stateful emitter's state grid (docs/RenderEngine.md §5.8): how
    // many particles the seed geometry encodes, the fixed step rate
    // and warm-up of the simulation, and the travel headroom the seed
    // geometry deliberately does not carry (buildEmitterSeedNodes) so
    // the renderer can widen what it culls against without widening
    // what anything frames. Same no-new-fields channel as fc_state —
    // it reaches ShaderBinding clones and the snapshot transport for free.
    // The fifth lane (the time scale) is why this is two vec4s rather
    // than one: the values are zero-padded to the vector width on the
    // way to the backend, and a reader older than the lane simply
    // stops at four, which is the default it would have used anyway.
    if (ss && ss[0])
        allParams.emplace_back("fc_emitter", std::vector<float>{
                float(std::max(1L, obj->EmitterCount.getValue())),
                float(obj->EmitterRate.getValue()),
                float(obj->EmitterWarmup.getValue()),
                float(std::max(0.0, obj->EmitterMargin.getValue())),
                float(std::max(0.0, obj->EmitterTimeScale.getValue()))});
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
                    pcSimulateShader, collectParamProps(obj), paramNodes);
    validateDocument();
}

void ViewProviderShaderProgram::validateDocument()
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    // App::ShaderProgram::DialectEnums: 2 = MATERIALX
    if (!obj || obj->Dialect.getValue() != 2) {
        validatedSource.clear();
        return;
    }
    std::string xml = obj->FragmentProgram.getValue();
    // Which surface is worn is part of what was validated: the same
    // document says different things about a different surface, and its
    // interface is a different set of properties.
    std::string validated = xml + '\0' + obj->Surface.getValue();
    if (validated == validatedSource)
        return;
    validatedSource = std::move(validated);
    if (xml.empty())
        return;
    // Stated as a path, the document is read from disk and its images
    // resolve against it; stated inline, there is nothing to resolve
    // against.
    std::string sourcePath;
    if (isDocumentPath(xml.c_str())) {
        sourcePath = xml;
        Base::FileInfo fi(sourcePath);
        Base::ifstream file(fi);
        if (!file) {
            Base::Console().Error("%s: MaterialX document not found: %s\n",
                                  obj->Label.getValue(), sourcePath.c_str());
            return;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        xml = ss.str();
    }

    std::string label = obj->Label.getValue();
    if (!Render::MaterialX::available()) {
        Base::Console().Warning(
                "%s: the MATERIALX dialect needs a build with MaterialX "
                "(BUILD_MATERIALX); the program is inert\n", label.c_str());
        return;
    }
    // A MaterialX document is not a stage of the material stage's
    // choosing: it describes the whole surface, which is the only
    // thing any backend can do with it.
    const char *stage = obj->Stage.getValue();
    if (stage && stage[0] && strcmp(stage, "material") != 0) {
        Base::Console().Warning(
                "%s: the MATERIALX dialect applies to the 'material' stage; "
                "stage '%s' will ignore it\n", label.c_str(), stage);
    }
    // Store what the document refers to before reading it, while the
    // files can still be reached; then read the document AS THE
    // CONSUMERS WILL GET IT, naming its carried files where they are.
    // Read raw instead and a document opened on a machine that never had
    // the originals reports every travelled image as missing.
    syncDocumentImages(xml, sourcePath);
    xml = documentWithStoredImages(obj, xml.c_str());

    auto info = Render::MaterialX::inspect(xml, sourcePath, obj->Surface.getValue());
    // A warning is printed when it APPEARS, not on every parse: the
    // graph editor writes the text per gesture, and a note that holds
    // across every gesture (a standard_surface document is translated
    // to OpenPBR, say) would print, and raise the report view, on each.
    for (const auto &w : info.warnings) {
        if (std::find(reportedWarnings.begin(), reportedWarnings.end(), w)
                == reportedWarnings.end())
            Base::Console().Warning("%s: %s\n", label.c_str(), w.c_str());
    }
    reportedWarnings = info.warnings;
    if (!info.valid) {
        // The parameters are NOT withdrawn here. A document is edited
        // in place, so it spends time unparsable on the way from one
        // valid state to the next, and taking the properties away over
        // that would take the user's values with them.
        Base::Console().Error("%s: not a usable MaterialX document: %s\n",
                              label.c_str(), info.error.c_str());
        return;
    }
    FC_LOG(label << ": MaterialX document, " << info.materials.size()
                 << " surface(s), wearing '" << info.material << "', model "
                 << info.surface);
    // A document with more than one surface renders exactly one, and
    // which one is the Surface property's to say. Saying so once, on
    // the change that made it so, is what keeps "it draws the wrong
    // piece" from being a mystery (sec 17.13).
    if (info.materials.size() > 1 && !obj->Surface.getValue()[0]) {
        std::string known;
        for (const auto &name : info.materials)
            known += (known.empty() ? "" : ", ") + name;
        Base::Console().Warning(
                "%s: the shader graph states %d surfaces and this program "
                "wears the first, '%s'; set Surface to one of: %s\n",
                label.c_str(), int(info.materials.size()),
                info.material.c_str(), known.c_str());
    }
    syncDocumentInterface(info.inputs);
}

void ViewProviderShaderProgram::syncDocumentImages(const std::string &xml,
                                                   const std::string &sourcePath)
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;
    // Never while the document is being read. What the property holds
    // then is what the archive gave it, which is the answer; importing
    // over that would touch the document just for being opened, and on
    // a machine that happens to hold the same paths it would quietly
    // replace the travelled bytes with local ones.
    if (auto doc = obj->getDocument()) {
        if (doc->testStatus(App::Document::Restoring)
                || doc->testStatus(App::Document::Importing)) {
            return;
        }
    }

    auto refs = Render::MaterialX::imageReferences(xml, sourcePath);
    std::set<std::string> wanted;
    for (const auto &ref : refs) {
        wanted.insert(ref.name);
        if (obj->Images.find(ref.name.c_str())) {
            continue;   // already carried; its bytes are the stored ones
        }
        if (ref.path.empty()) {
            // The document names a file this machine does not have and
            // the property does not carry. inspect() has already warned
            // about it as a missing image; there is nothing to store.
            continue;
        }
        obj->Images.setFile(ref.name.c_str(), ref.path.c_str());
        FC_LOG(obj->Label.getValue() << ": stored image " << ref.name
                                     << " from " << ref.path);
    }
    // What the document no longer refers to stops being carried, the
    // same rule the declared interface follows above. Named first and
    // removed after: removeFile edits the vector getValues() hands out.
    std::vector<std::string> stale;
    for (const auto &file : obj->Images.getValues()) {
        if (!wanted.count(file.name)) {
            stale.push_back(file.name);
        }
    }
    for (const auto &name : stale) {
        obj->Images.removeFile(name.c_str());
    }
}

// The App::Property a MaterialX type is carried by, and the value
// written into it. Nothing else in the tree maps this way round --
// everywhere else a property exists and a shader reads it -- which is
// the whole point of the reverse direction (docs/CyclesIntegration.md
// sec 6.11): the document declares the interface and the properties
// follow. A type with no property here (a string, a matrix, an image
// file) never reaches this point: the enumeration leaves it out.
static const char *propertyTypeFor(const std::string &type)
{
    if (type == "float")
        return "App::PropertyFloat";
    if (type == "integer")
        return "App::PropertyInteger";
    if (type == "boolean")
        return "App::PropertyBool";
    if (type == "color3" || type == "color4")
        return "App::PropertyColor";
    if (type == "vector2" || type == "vector3")
        return "App::PropertyVector";
    if (type == "vector4")
        return "App::PropertyFloatList";
    return nullptr;
}

static void writeDefault(App::Property *prop, const std::vector<float> &v)
{
    auto at = [&v](size_t i) { return i < v.size() ? v[i] : 0.0f; };
    if (auto p = dynamic_cast<App::PropertyFloat*>(prop))
        p->setValue(at(0));
    else if (auto p = dynamic_cast<App::PropertyInteger*>(prop))
        p->setValue(long(at(0)));
    else if (auto p = dynamic_cast<App::PropertyBool*>(prop))
        p->setValue(at(0) != 0.0f);
    else if (auto p = dynamic_cast<App::PropertyColor*>(prop))
        p->setValue(App::Color(at(0), at(1), at(2),
                               v.size() > 3 ? at(3) : 1.0f));
    else if (auto p = dynamic_cast<App::PropertyVector*>(prop))
        p->setValue(Base::Vector3d(at(0), at(1), at(2)));
    else if (auto p = dynamic_cast<App::PropertyFloatList*>(prop)) {
        std::vector<double> values;
        for (size_t i = 0; i < 4; ++i)
            values.push_back(at(i));
        p->setValues(values);
    }
}

void ViewProviderShaderProgram::syncDocumentInterface(
        const std::vector<Render::MaterialX::MaterialInput> &inputs)
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;
    static const std::string prefix = "Param_";
    std::set<std::string> wanted;
    for (const auto &input : inputs) {
        const char *type = propertyTypeFor(input.type);
        if (!type)
            continue;
        const std::string name = prefix + input.name;
        wanted.insert(name);
        App::Property *prop = obj->getDynamicPropertyByName(name.c_str());
        if (prop && strcmp(prop->getTypeId().getName(), type) != 0) {
            // The document changed what the input IS. The value cannot
            // survive that, and keeping a property of the wrong type
            // would feed the shader lanes it does not mean.
            obj->removeDynamicProperty(name.c_str());
            prop = nullptr;
        }
        if (prop)
            continue;   // an existing value is the user's, not ours
        std::string doc = input.label.empty() ? input.name : input.label;
        if (!input.folder.empty())
            doc += " (" + input.folder + ")";
        if (!input.help.empty())
            doc += ": " + input.help;
        prop = obj->addDynamicProperty(type, name.c_str(), "Param", doc.c_str());
        if (prop)
            writeDefault(prop, input.value);
    }
    // What the document no longer declares stops being a parameter.
    // Only for a MATERIALX program: everywhere else Param_* properties
    // are the author's own and nothing may take them away.
    for (const auto &name : obj->getDynamicPropertyNames()) {
        if (name.compare(0, prefix.size(), prefix) == 0
                && name.size() > prefix.size() && !wanted.count(name))
            obj->removeDynamicProperty(name.c_str());
    }
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
                || prop == &obj->DemoPlacement
                || prop == &obj->DemoColor
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

    // Demo placement first, so several standalone demo shaders can
    // compose a scene; applies to the shape and the particle emitter
    // sub-roots alike.
    const auto &plc = obj->DemoPlacement.getValue();
    if (!plc.isIdentity()) {
        auto xf = new SoTransform;
        const auto &pos = plc.getPosition();
        const auto &rot = plc.getRotation();
        double q0, q1, q2, q3;
        rot.getValue(q0, q1, q2, q3);
        xf->translation = SbVec3f(float(pos.x), float(pos.y),
                                  float(pos.z));
        xf->rotation = SbRotation(float(q0), float(q1), float(q2),
                                  float(q3));
        pcDemoRoot->addChild(xf);
    }

    // Shader programs ahead of the shape: the SoFCRenderMaterial placement
    // rules — they apply to the shapes captured after them in this cache.
    // Scene-level ("post") programs are skipped: activating those is the
    // ShaderBinding object's job, and nested placement would be unreliable
    // anyway (pruned from recapture inside a valid cached separator).
    // Particle companions are skipped too on the real demo shapes:
    // setUserShader stamps the cache material (last-set wins), so they
    // would clobber the main program — they get their own emitter
    // sub-roots fit to the demo bounds below. The Emitter demo IS the
    // seed geometry, there they stay in the main chain.
    for (auto prog : obj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || progObj->Stage.getValue() == StagePost
                || !progObj->Enabled.getValue())
            continue;
        if (demo != 5 && progObj->Stage.getValue() == StageParticle
                && progObj->EmitterCount.getValue() > 0)
            continue;
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (vp && vp->getShaderNode())
            pcDemoRoot->addChild(vp->getShaderNode());
    }

    // Demo shapes carry no view-provider material chain; without an
    // explicit material the captured diffuse goes dark.
    auto mat = new SoMaterial;
    const auto &col = obj->DemoColor.getValue();
    mat->diffuseColor = SbColor(col.r, col.g, col.b);
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
        // Own separator so the rotation does not leak into the particle
        // emitter sub-roots appended after the shape.
        auto sep = new SoSeparator;
        auto rot = new SoRotation;
        rot->rotation = SbRotation(SbVec3f(1, 0, 0), (float)(M_PI / 2));
        sep->addChild(rot);
        if (demo == 3) {
            auto cyl = new SoCylinder;
            cyl->radius = (float)obj->DemoRadius.getValue();
            cyl->height = (float)obj->DemoHeight.getValue();
            sep->addChild(cyl);
        }
        else {
            auto cone = new SoCone;
            cone->bottomRadius = (float)obj->DemoRadius.getValue();
            cone->height = (float)obj->DemoHeight.getValue();
            sep->addChild(cone);
        }
        pcDemoRoot->addChild(sep);
        break;
    }
    case 5: { // Emitter: N degenerate seed quads (particle groundwork)
        // Shared seed builder (buildEmitterSeedNodes above); anchors
        // spread over the DemoSize box centered on the origin.
        const auto &size = obj->DemoSize.getValue();
        SbVec3f half(float(size.x) * 0.5f, float(size.y) * 0.5f,
                     float(size.z) * 0.5f);
        buildEmitterSeedNodes(pcDemoRoot,
                              int(std::max(1L, obj->EmitterCount.getValue())),
                              uint32_t(obj->EmitterSeed.getValue()),
                              -half, half);
        break;
    }
    default:
        break;
    }

    if (demo == 5)
        return;

    // Enabled particle companions on a real demo shape: each gets its
    // own SoFCSelectionRoot (own render cache — the main program owns
    // this cache's material slot) with seed quads fit to the analytic
    // demo bounds, honoring the program's spread/offset/margin exactly
    // like a ShaderBinding fits them to a target's bounding box.
    SbVec3f half(0, 0, 0);
    switch (demo) {
    case 1: { // Box
        const auto &size = obj->DemoSize.getValue();
        half = SbVec3f(float(size.x) * 0.5f, float(size.y) * 0.5f,
                       float(size.z) * 0.5f);
        break;
    }
    case 2: { // Sphere
        float r = float(obj->DemoRadius.getValue());
        half = SbVec3f(r, r, r);
        break;
    }
    case 3: case 4: { // Cylinder / Cone (Z-up after the rotation)
        float r = float(obj->DemoRadius.getValue());
        half = SbVec3f(r, r, float(obj->DemoHeight.getValue()) * 0.5f);
        break;
    }
    }
    for (auto prog : obj->Programs.getValues()) {
        auto p = dynamic_cast<App::ShaderProgram*>(prog);
        if (!p || !p->Enabled.getValue()
               || p->EmitterCount.getValue() <= 0
               || p->Stage.getValue() != StageParticle)
            continue;
        auto pvp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(p));
        if (!pvp || !pvp->getShaderNode())
            continue;
        const auto &spread = p->EmitterSpread.getValue();
        const auto &offset = p->EmitterOffset.getValue();
        SbVec3f ext = half * 2.0f;
        SbVec3f c(float(offset.x) * ext[0], float(offset.y) * ext[1],
                  float(offset.z) * ext[2]);
        SbVec3f h(0.5f * float(spread.x) * ext[0],
                  0.5f * float(spread.y) * ext[1],
                  0.5f * float(spread.z) * ext[2]);
        auto sep = new SoFCSelectionRoot;
        sep->addChild(pvp->getShaderNode());
        buildEmitterSeedNodes(sep, int(p->EmitterCount.getValue()),
                              uint32_t(p->EmitterSeed.getValue()),
                              c - h, c + h);
        pcDemoRoot->addChild(sep);
    }
}

// ----------------------------------------------------------------------------
PROPERTY_SOURCE(Gui::ViewProviderShaderBinding, Gui::ViewProviderLink)

namespace {
/// One card-carried document set, built once per document and shared by
/// every object wearing the card (docs/CyclesIntegration.md 6.13, 1a)
struct MaterialXNode {
    CoinPtr<SoShaderProgram> program;
    CoinPtr<SoFragmentShader> fragment;
    int users = 0;
};
std::map<App::Document*, std::map<std::string, MaterialXNode>> _MaterialXNodes;
}  // namespace

SoShaderProgram *ViewProviderShaderBinding::acquireMaterialXNode(
        App::Document *doc, const std::string &manifestHash)
{
    if (!doc || manifestHash.empty())
        return nullptr;
    auto &nodes = _MaterialXNodes[doc];
    auto it = nodes.find(manifestHash);
    if (it != nodes.end()) {
        ++it->second.users;
        return it->second.program;
    }
    // Everything the node is built from has to be IN the store. A
    // restore hands the blobs over as the archive yields them, so a miss
    // here is "not yet", and the caller comes back (finishRestoring).
    auto &manager = doc->getFileBlobManager();
    auto blob = manager.find(manifestHash);
    if (!blob)
        return nullptr;
    App::MaterialXDocument manifest;
    if (!App::MaterialXDocument::readFile(blob->path(), manifest) || !manifest.isSet()) {
        FC_WARN("MaterialX manifest " << manifestHash << " in "
                << doc->getName() << " cannot be read");
        return nullptr;
    }
    std::string text;
    std::vector<Render::MaterialX::ImageReference> files;
    for (const auto &file : manifest.files) {
        auto child = manager.find(file.hash);
        if (!child)
            return nullptr;   // still on its way
        if (file.name == manifest.document) {
            Base::ifstream in(Base::FileInfo(child->path()), std::ios::in | std::ios::binary);
            if (!in) {
                FC_WARN("MaterialX document " << file.name << " of " << manifestHash
                        << " cannot be read from " << child->path());
                return nullptr;
            }
            std::ostringstream buf;
            buf << in.rdbuf();
            text = buf.str();
        }
        else {
            files.push_back({file.name, child->path()});
        }
    }
    if (text.empty())
        return nullptr;
    // The document names its maps by the names the manifest keeps them
    // under; what goes into the node names them where the stored files
    // are on THIS machine, so the generator and the path tracer go on
    // opening files (docs/MaterialStorage.md sec 16.6).
    if (!files.empty())
        text = Render::MaterialX::substituteImages(text, files);

    MaterialXNode &node = nodes[manifestHash];
    node.program = new SoShaderProgram;
    node.fragment = new SoFragmentShader;
    node.fragment->sourceType = SoShaderObject::MATERIALX;
    node.fragment->sourceProgram = text.c_str();
    // Which of the graph's surfaces the card wears. Part of the manifest,
    // so two cards over one shared file set are two nodes here, keyed
    // apart by the manifest hash already (sec 17.13).
    node.fragment->sourceSurface = manifest.surface.c_str();
    node.program->stage = SbName(StageMaterial);
    node.program->shaderObject.setNum(1);
    node.program->shaderObject.set1Value(0, node.fragment);
    node.users = 1;
    FC_LOG("MaterialX card node built for " << manifestHash << " in " << doc->getName());
    return node.program;
}

void ViewProviderShaderBinding::releaseMaterialXNode(App::Document *doc,
                                                     const std::string &manifestHash)
{
    auto docIt = _MaterialXNodes.find(doc);
    if (docIt == _MaterialXNodes.end())
        return;
    auto it = docIt->second.find(manifestHash);
    if (it == docIt->second.end())
        return;
    if (--it->second.users <= 0)
        docIt->second.erase(it);
    if (docIt->second.empty())
        _MaterialXNodes.erase(docIt);
}

namespace {
// Active ShaderBinding view providers per document, for precedence
std::map<App::Document*, std::set<ViewProviderShaderBinding*>> _BindingRegistry;
bool _RebuildingBindings;

// Scope=Instance bindings depend on the whole document's structure — any
// object edit can create or remove an occurrence of a registered chain —
// so documents with active Appearances get their signals hooked, with
// rebuilds coalesced through the event loop.
struct DocumentHooks {
    fastsignals::scoped_connection newObj;
    fastsignals::scoped_connection delObj;
    fastsignals::scoped_connection changedObj;
    // whether the last rebuild registered any chains: without chains a
    // link-property edit elsewhere cannot affect the bindings, and the
    // rebuild churn (node reinsertion → recapture) is not worth it
    bool hasChains = false;
    // whether any registered chain carries particle emitter programs:
    // their occurrence-fit seeds are real top-of-root geometry that
    // must follow visibility, so Visibility edits rebuild too
    bool hasEmitterChains = false;
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
            ViewProviderShaderBinding::rebuildAllBindings(doc);
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
            // only link topology can change the occurrence set —
            // except occurrence-fit emitter seeds, which follow
            // visibility
            if (prop.isDerivedFrom(App::PropertyLinkBase::getClassTypeId())
                    || (it->second.hasEmitterChains && prop.getName()
                        && strcmp(prop.getName(), "Visibility") == 0))
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
    ViewProviderShaderBinding *vp;
    App::ShaderBinding *obj;
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

ViewProviderShaderBinding::ViewProviderShaderBinding() = default;

ViewProviderShaderBinding::~ViewProviderShaderBinding() = default;

// Like-named (Param_*) dynamic properties on the ShaderBinding override the
// program's parameter values for this binding only (§6.4); a parameter
// the program does not carry is added, so a binding can set uniforms the
// shader source declares without a matching property on the program
// object.
static void applyParamOverrides(App::ShaderBinding *obj,
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
resolveUserShader(App::ShaderBinding *obj)
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
// bound App::Shader in Programs order, with this ShaderBinding's parameter
// overrides applied. Activated by a ShaderBinding with no target children
// (§6.5).
static std::vector<Render::UserShader>
resolvePostShaders(App::ShaderBinding *obj)
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

void ViewProviderShaderBinding::attach(App::DocumentObject *obj)
{
    ViewProviderLink::attach(obj);
    ensureDynPropConnections();
    ensureDocumentHooks(obj->getDocument());
    _BindingRegistry[obj->getDocument()].insert(this);
    if (!obj->isRestoring())
        rebuildAllBindings(obj->getDocument());
}

void ViewProviderShaderBinding::finishRestoring()
{
    ViewProviderLink::finishRestoring();
    if (auto obj = getObject())
        rebuildAllBindings(obj->getDocument());
}

void ViewProviderShaderBinding::beforeDelete()
{
    auto obj = getObject();
    App::Document *doc = obj ? obj->getDocument() : nullptr;
    clearBindings();
    if (doc) {
        auto it = _BindingRegistry.find(doc);
        if (it != _BindingRegistry.end()) {
            it->second.erase(this);
            if (it->second.empty()) {
                _BindingRegistry.erase(it);
                _DocumentHooks.erase(doc);
            }
            // Runs on the emptied registry too — the views' scene-level
            // shader list must clear with the last ShaderBinding.
            rebuildAllBindings(doc);
        }
    }
    ViewProviderLink::beforeDelete();
}

void ViewProviderShaderBinding::updateData(const App::Property *prop)
{
    ViewProviderLink::updateData(prop);
    auto obj = dynamic_cast<App::ShaderBinding*>(getObject());
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

void ViewProviderShaderBinding::onChanged(const App::Property *prop)
{
    ViewProviderLink::onChanged(prop);
    // Hiding a ShaderBinding deactivates its bindings (the next-ranked
    // binding on the same target takes over).
    if (prop == &Visibility && getObject())
        rebuildAllBindings(getObject()->getDocument());
}

void ViewProviderShaderBinding::clearBindings()
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

// The effect's enabled particle companion programs (EmitterCount > 0,
// stage "particle", docs/RenderEngine.md §5.11) — never the main
// program, each gets seed geometry fit to a target's bounds.
static std::vector<App::ShaderProgram*>
collectEmitterPrograms(App::ShaderBinding *obj)
{
    std::vector<App::ShaderProgram*> res;
    auto shobj = obj ? obj->resolveShader() : nullptr;
    if (!shobj)
        return res;
    for (auto prog : shobj->Programs.getValues()) {
        auto p = dynamic_cast<App::ShaderProgram*>(prog);
        if (p && p->Enabled.getValue()
              && p->EmitterCount.getValue() > 0
              && p->Stage.getValue() == StageParticle)
            res.push_back(p);
    }
    return res;
}

// One emitter program's seed sub-root fit to the given target bounds.
// Own SoFCSelectionRoot: setUserShader stamps the CACHE material
// (last-set wins within a cache), so the seeds need their own render
// cache or the effect's main program recaptures them; the "particle"
// stage then survives the parent merge-down (SoFCRenderCache
// mergeMaterial).
static SoFCSelectionRoot *buildEmitterRoot(App::ShaderProgram *p,
                                           const SbBox3f &box)
{
    auto pvp = dynamic_cast<ViewProviderShaderProgram*>(
            Application::Instance->getViewProvider(p));
    if (!pvp || !pvp->getShaderNode())
        return nullptr;
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
    auto sep = new SoFCSelectionRoot;
    sep->addChild(pvp->getShaderNode());
    buildEmitterSeedNodes(sep, int(p->EmitterCount.getValue()),
                          uint32_t(p->EmitterSeed.getValue()),
                          c - half, c + half);
    return sep;
}

void ViewProviderShaderBinding::applyPathBindings(
        const std::vector<std::pair<App::DocumentObject*,
                                    std::string>> &targets,
        const std::string &element)
{
    auto obj = dynamic_cast<App::ShaderBinding*>(getObject());
    if (!obj || targets.empty())
        return;
    auto shader = resolveUserShader(obj);
    // Particle companions fit per occurrence; an element-scoped binding
    // restricts to a face — no emitters there. A particle-only effect
    // (no main program, e.g. the bundled rain) is a valid binding.
    std::vector<App::ShaderProgram*> emitters;
    if (element.empty())
        emitters = collectEmitterPrograms(obj);
    if (!shader && emitters.empty())
        return;
    auto gdoc = Application::Instance->getDocument(obj->getDocument());
    if (!gdoc)
        return;
    // Emitter seeds are scene nodes, view-independent: one sub-root per
    // occurrence × program at the BASE object's root child 0. The base
    // is the occurrence's top-level scene instance, so the seeds render
    // once per occurrence, and hide with the base's root (a claimed
    // base's root is not in the scene at all). Bounds by App-side
    // resolution: getSubObject accumulates every placement along the
    // occurrence subname (including the final object's), getLinkedObject
    // then follows link tails (a subname link's baked path transform is
    // accumulated too), and the tail view provider's placement-free bbox
    // under that matrix is the occurrence's doc-frame bounds — the same
    // frame a base-root child-0 separator's coordinates live in. (Not an
    // SoGetBoundingBoxAction over the bound path: IN_PATH traversal
    // unions every earlier sibling of each path node — whole-scene
    // bounds, not the occurrence's.)
    for (const auto &t : targets) {
        if (emitters.empty())
            break;
        auto vpd = dynamic_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(t.first));
        if (!vpd || !vpd->getRoot())
            continue;
        // A hidden occurrence draws no shader override either — its
        // caches capture empty — but the seeds are real top-of-root
        // geometry, so visibility must be checked here (rebuilds on
        // Visibility edits come from the document hooks).
        if (gdoc->isClaimed3D(vpd) || !vpd->isVisible())
            continue;
        if (!t.second.empty()
                && t.first->isElementVisibleEx(t.second.c_str()) == 0)
            continue;
        Base::Matrix4D mat;
        auto sobj = t.first->getSubObject(t.second.c_str(), nullptr,
                                          &mat, true);
        Base::BoundBox3d bb;
        if (sobj) {
            if (auto linked = sobj->getLinkedObject(true, &mat, false))
                sobj = linked;
            if (auto svp = dynamic_cast<ViewProviderDocumentObject*>(
                        Application::Instance->getViewProvider(sobj)))
                bb = svp->getBoundingBox(nullptr, &mat, false);
        }
        if (!bb.IsValid()) {
            FC_WARN("particle emitter: empty bounds on "
                    << t.first->getFullName() << "." << t.second
                    << ", seeds skipped");
            continue;
        }
        SbBox3f box(float(bb.MinX), float(bb.MinY), float(bb.MinZ),
                    float(bb.MaxX), float(bb.MaxY), float(bb.MaxZ));
        SoGroup *root = vpd->getRoot();
        for (auto p : emitters) {
            if (auto sep = buildEmitterRoot(p, box)) {
                FC_LOG("AP emitter occurrence " << t.first->getFullName()
                       << "." << t.second << " for " << p->getFullName()
                       << " box (" << bb.MinX << "," << bb.MinY << ","
                       << bb.MinZ << ")-(" << bb.MaxX << "," << bb.MaxY
                       << "," << bb.MaxZ << ")");
                root->insertChild(sep, 0);
                attached.emplace_back(root, sep);
            }
        }
    }
    if (!shader)
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
                FC_WARN("ShaderBinding " << obj->getFullName()
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

SoShaderProgram *ViewProviderShaderBinding::ownProgramNode()
{
    auto obj = dynamic_cast<App::ShaderBinding*>(getObject());
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
        pcOwnSimulateShader = new SoFragmentShader;
    }

    // The program's parameters overridden per binding by this
    // ShaderBinding's like-named Param_* dynamic properties (sec 6.4); an
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
                    pcOwnFragmentShader, pcOwnSimulateShader, params,
                    ownParamNodes);
    return pcOwnProgram;
}

void ViewProviderShaderBinding::applyDirectBindings(
        const std::vector<App::DocumentObject*> &targets)
{
    if (targets.empty())
        return;
    auto node = ownProgramNode();
    // Particle companion programs of the effect: each gets seed
    // geometry generated per target below, fit to the target's
    // bounding box.
    auto obj = dynamic_cast<App::ShaderBinding*>(getObject());
    auto emitters = collectEmitterPrograms(obj);
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
            //
            // Except when the target also WEARS a MaterialX card: that
            // node sits at the head too, and the cache's setUserShader
            // keeps the LAST material-stage program traversed. An
            // explicit binding beats the worn card, so go in behind it.
            int at = 0;
            if (auto vpg = dynamic_cast<ViewProviderGeometryObject*>(vpd)) {
                if (auto card = vpg->getMaterialXNode()) {
                    int idx = root->findChild(card);
                    if (idx >= 0)
                        at = idx + 1;
                }
            }
            FC_LOG("AP attach " << t->getFullName() << " at " << at);
            root->insertChild(node, at);
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
        // Inserted at child 0 (root frame, matching the bounds).
        for (auto p : emitters) {
            if (auto sep = buildEmitterRoot(p, box)) {
                root->insertChild(sep, 0);
                attached.emplace_back(root, sep);
            }
        }
    }
}

void ViewProviderShaderBinding::onViewCreated(App::Document *doc)
{
    // Deferred + coalesced: the caller is still constructing the view,
    // and a restore may open several views at once.
    if (doc && _BindingRegistry.count(doc))
        scheduleRebuild(doc);
}

void ViewProviderShaderBinding::rebuildAllBindings(App::Document *doc)
{
    if (!doc || _RebuildingBindings)
        return;
    Base::StateLocker guard(_RebuildingBindings);
    // A missing registry entry (last ShaderBinding deleted) still runs the
    // tail: the views' scene-level shader list must clear too.
    auto it = _BindingRegistry.find(doc);
    std::vector<ViewProviderShaderBinding*> vps;
    if (it != _BindingRegistry.end())
        vps.assign(it->second.begin(), it->second.end());

    for (auto vp : vps)
        vp->clearBindings();

    // Shader-only Appearances activate scene-level post programs (§6.5)
    std::vector<App::ShaderBinding*> sceneObjs;
    // Scope=Object: winner per resolved final target object
    std::map<App::DocumentObject*, ViewProviderShaderBinding*> directWinners;
    // Scope=Instance: registered chains, matched over occurrences below
    std::vector<ChainBinding> chains;

    for (auto vp : vps) {
        auto obj = dynamic_cast<App::ShaderBinding*>(vp->getObject());
        if (!obj || !vp->Visibility.getValue())
            continue;
        if (!obj->resolveShader())
            continue;
        auto targets = obj->getTargets();
        if (targets.empty()) {
            sceneObjs.push_back(obj);
            continue;
        }
        if (obj->scopeMode() == App::ShaderBinding::ScopeMode::Object) {
            for (auto t : targets) {
                auto resolved = canonObject(t);
                if (isShaderFamily(resolved))
                    continue;
                auto r = directWinners.emplace(resolved, vp);
                if (r.second)
                    continue;
                auto other = dynamic_cast<App::ShaderBinding*>(
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
                obj->scopeMode() == App::ShaderBinding::ScopeMode::Element;
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
        std::map<ViewProviderShaderBinding*,
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
            FC_WARN("ShaderBinding occurrence scan of " << doc->getName()
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

    if (auto h = _DocumentHooks.find(doc); h != _DocumentHooks.end()) {
        h->second.hasChains = !chains.empty();
        h->second.hasEmitterChains = std::any_of(
            chains.begin(), chains.end(), [](const ChainBinding &cb) {
                return cb.element.empty()
                    && !collectEmitterPrograms(cb.obj).empty();
            });
    }

    // Scene-level activation: the shader-only Appearances' post-stage
    // programs, ordered ascending by TreeRank (name fallback) so the
    // highest-ranked ShaderBinding lands last -- the winning slot of the
    // backend's "last shader on a stage wins" rule, matching the
    // per-target precedence direction.
    std::sort(sceneObjs.begin(), sceneObjs.end(),
              [](App::ShaderBinding *a, App::ShaderBinding *b) {
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
PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderShaderBindingPython, Gui::ViewProviderShaderBinding)
/// @endcond

// explicit template instantiation
template class GuiExport ViewProviderFeaturePythonT<ViewProviderShaderProgram>;
template class GuiExport ViewProviderFeaturePythonT<ViewProviderShader>;
template class GuiExport ViewProviderFeaturePythonT<ViewProviderShaderBinding>;
}
