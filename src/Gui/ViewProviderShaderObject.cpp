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
# include <Inventor/nodes/SoCone.h>
# include <Inventor/nodes/SoCube.h>
# include <Inventor/nodes/SoCylinder.h>
# include <Inventor/nodes/SoFragmentShader.h>
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
#include "Inventor/SoFCRenderCache.h"
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Inventor/SoFCRendererBridge.h"
#include "Renderer/Renderer.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"


FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

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
                || prop == &obj->FragmentProgram)) {
        if (dynParam)
            syncParameters();
        else
            updateShaderNode();
        for (auto parent : obj->getInList()) {
            if (!parent->isDerivedFrom(App::Shader::getClassTypeId()))
                continue;
            if (prop == &obj->Stage) {
                // stage decides whether the demo preview includes the program
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

void ViewProviderShaderProgram::updateShaderNode()
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;

    SbName stage(obj->Stage.getValue());
    if (pcShaderProgram->stage.getValue() != stage)
        pcShaderProgram->stage = stage;

    int32_t sourcetype = obj->Dialect.getValue() == 0 ? SoShaderObject::BGFX_SC
                                                      : SoShaderObject::GLSL_PROGRAM;
    const char *vs = obj->VertexProgram.getValue();
    const char *fs = obj->FragmentProgram.getValue();

    if (pcVertexShader->sourceType.getValue() != sourcetype)
        pcVertexShader->sourceType = sourcetype;
    if (pcVertexShader->sourceProgram.getValue() != vs)
        pcVertexShader->sourceProgram = vs;
    if (pcFragmentShader->sourceType.getValue() != sourcetype)
        pcFragmentShader->sourceType = sourcetype;
    if (pcFragmentShader->sourceProgram.getValue() != fs)
        pcFragmentShader->sourceProgram = fs;

    SoNode *nodes[2];
    int num = 0;
    if (vs && vs[0])
        nodes[num++] = pcVertexShader;
    if (fs && fs[0])
        nodes[num++] = pcFragmentShader;
    bool changed = pcShaderProgram->shaderObject.getNum() != num;
    for (int i = 0; !changed && i < num; ++i)
        changed = pcShaderProgram->shaderObject[i] != nodes[i];
    if (changed) {
        pcShaderProgram->shaderObject.setNum(num);
        for (int i = 0; i < num; ++i)
            pcShaderProgram->shaderObject.set1Value(i, nodes[i]);
    }

    // which shader object carries the parameters depends on the sources
    syncParameters();
}

void ViewProviderShaderProgram::syncParameters()
{
    auto obj = dynamic_cast<App::ShaderProgram*>(getObject());
    if (!obj)
        return;

    // One SoShaderParameterArray1f per parameter property (the values are
    // already vec4-padded floats, so one node type covers every property
    // type), updated in place so a value edit notifies the enclosing
    // render caches without relisting the parameter field.
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> next;
    for (auto &v : collectParamProps(obj)) {
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
    SoShaderObject *target = pcFragmentShader;
    SoShaderObject *other = pcVertexShader;
    const char *fs = obj->FragmentProgram.getValue();
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
                || prop == &obj->DemoHeight))
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
        if (!progObj || strcmp(progObj->Stage.getValue(), "post") == 0)
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
    default:
        break;
    }
}

// ----------------------------------------------------------------------------

PROPERTY_SOURCE(Gui::ViewProviderAppearance, Gui::ViewProviderDocumentObject)

namespace {
// Active Appearance view providers per document, for TreeRank precedence
std::map<App::Document*, std::set<ViewProviderAppearance*>> _AppearanceRegistry;
bool _RebuildingBindings;
}

ViewProviderAppearance::ViewProviderAppearance() = default;

ViewProviderAppearance::~ViewProviderAppearance() = default;

// The effect's object-scoped shader: the first non-"post" program of the
// bound App::Shader, translated off its view provider's Coin node.
// (Scene-level "post" activation is a follow-up slice.)
static std::shared_ptr<const Render::UserShader>
resolveUserShader(App::Appearance *obj)
{
    auto shobj = dynamic_cast<App::Shader*>(obj->Shader.getValue());
    if (!shobj)
        return nullptr;
    for (auto prog : shobj->Programs.getValues()) {
        auto progObj = dynamic_cast<App::ShaderProgram*>(prog);
        if (!progObj || strcmp(progObj->Stage.getValue(), "post") == 0)
            continue;
        auto vp = dynamic_cast<ViewProviderShaderProgram*>(
                Application::Instance->getViewProvider(progObj));
        if (!vp || !vp->getShaderNode())
            continue;
        Render::UserShader shader;
        if (!RendererBridge::translateShaderProgram(vp->getShaderNode(),
                                                    shader))
            continue;
        // Like-named dynamic properties on the Appearance override the
        // program's parameter values for this binding only (§6.4); a
        // parameter the program does not carry is added, so a binding can
        // set uniforms the shader source declares without a matching
        // property on the program object.
        for (auto &v : collectParamProps(obj)) {
            auto it = std::find_if(shader.params.begin(),
                                   shader.params.end(),
                                   [&](const auto &p) {
                                       return p.name == v.first;
                                   });
            if (it != shader.params.end())
                it->values = std::move(v.second);
            else
                shader.params.push_back({std::move(v.first),
                                         std::move(v.second)});
        }
        return std::make_shared<Render::UserShader>(std::move(shader));
    }
    return nullptr;
}

// Flatten Targets into (object, subname-without-element) pairs; element
// scope is an explicit follow-up.
static std::vector<std::pair<App::DocumentObject*, std::string>>
collectTargets(App::Appearance *obj)
{
    std::vector<std::pair<App::DocumentObject*, std::string>> res;
    for (auto &link : obj->Targets.getSubListValues()) {
        auto target = link.getValue();
        if (!target || !target->getNameInDocument())
            continue;
        const auto &subs = link.getSubValues();
        if (subs.empty()) {
            res.emplace_back(target, std::string());
            continue;
        }
        for (const auto &sub : subs)
            res.emplace_back(target,
                App::SubObjectT(target, sub.c_str()).getSubNameNoElement());
    }
    return res;
}

void ViewProviderAppearance::attach(App::DocumentObject *obj)
{
    ViewProviderDocumentObject::attach(obj);
    ensureDynPropConnections();
    _AppearanceRegistry[obj->getDocument()].insert(this);
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
            if (it->second.empty())
                _AppearanceRegistry.erase(it);
            else
                rebuildAllBindings(doc);
        }
    }
    ViewProviderDocumentObject::beforeDelete();
}

void ViewProviderAppearance::updateData(const App::Property *prop)
{
    auto obj = dynamic_cast<App::Appearance*>(getObject());
    // dynamic properties are per-binding parameter overrides (§6.4)
    if (obj && (prop == &obj->Targets
                || prop == &obj->Shader
                || prop == &obj->TreeRank
                || (prop && prop->getName()
                    && obj->getDynamicPropertyByName(prop->getName())
                        == prop)))
        rebuildAllBindings(obj->getDocument());
    ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderAppearance::onChanged(const App::Property *prop)
{
    ViewProviderDocumentObject::onChanged(prop);
    // Hiding an Appearance deactivates its bindings (the next-ranked
    // Appearance on the same target takes over).
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
}

void ViewProviderAppearance::applyBindings(
        const std::vector<std::pair<App::DocumentObject*,
                                    std::string>> &targets)
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
            CoinPtr<SoPath> path = new SoPath(10);
            viewer->appendDetailPath(path, vpd);
            SoDetail *det = nullptr;
            bool ok = vpd->getDetailPath(t.second.c_str(),
                    static_cast<SoFullPath*>(path.get()), true, det);
            delete det;
            if (!ok || !path->getLength())
                continue;
            std::string key = obj->getFullName();
            key += '|';
            key += t.first->getFullName();
            key += '.';
            key += t.second;
            mgr->addShaderOverride(key, path, shader);
            bound.emplace_back(viewer, std::move(key));
        }
    }
}

void ViewProviderAppearance::rebuildAllBindings(App::Document *doc)
{
    if (!doc || _RebuildingBindings)
        return;
    Base::StateLocker guard(_RebuildingBindings);
    auto it = _AppearanceRegistry.find(doc);
    if (it == _AppearanceRegistry.end())
        return;

    for (auto vp : it->second)
        vp->clearBindings();

    // Winner per target: highest TreeRank; equal ranks fall back to the
    // object name for determinism.
    std::map<std::string, ViewProviderAppearance*> winners;
    std::map<ViewProviderAppearance*,
             std::vector<std::pair<App::DocumentObject*, std::string>>> targets;
    for (auto vp : it->second) {
        // Not isVisible(): that resolves through isShow(), which these
        // view providers pin to true for the tree.
        auto obj = dynamic_cast<App::Appearance*>(vp->getObject());
        if (!obj || !vp->Visibility.getValue())
            continue;
        auto tgts = collectTargets(obj);
        for (const auto &t : tgts) {
            std::string key = t.first->getFullName() + "." + t.second;
            auto r = winners.emplace(key, vp);
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
        targets[vp] = std::move(tgts);
    }

    for (auto &v : targets) {
        std::vector<std::pair<App::DocumentObject*, std::string>> winning;
        for (const auto &t : v.second) {
            std::string key = t.first->getFullName() + "." + t.second;
            if (winners[key] == v.first)
                winning.push_back(t);
        }
        v.first->applyBindings(winning);
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
