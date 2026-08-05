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
#include <Inventor/SbBox3f.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <QColor>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "SceneServeSource.h"

#include "Document.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Renderer/Renderer.h"
#include "RenderParams.h"
#include "SoFCUnifiedSelection.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;

namespace
{
/// The viewport a synthesized camera is built for. Only the framing a
/// joining viewer adopts before it frames the scene itself, so this has
/// to be sane rather than right.
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

/// The background viewers draw behind the scene. A real viewer builds
/// this from its own Coin background node, but that node is itself only
/// a mirror of these preferences (View3DSettings.cpp) — and the
/// background is a property of how the document is presented, not of a
/// window. Same keys, same defaults, so both publishers describe it
/// identically.
Render::Background backgroundFromPreferences()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");

    // Stored 0xRRGGBBAA; the packed form the renderer wants is the same
    // with an opaque alpha.
    auto opaque = [](unsigned long packed) {
        return uint32_t((packed & 0xffffff00UL) | 0xffUL);
    };

    Render::Background bg;
    if (hGrp->GetBool("Gradient", true))
        bg.type = Render::Background::LinearGradient;
    else if (hGrp->GetBool("RadialGradient", false))
        bg.type = Render::Background::RadialGradient;
    else {
        bg.type = Render::Background::Flat;
        bg.fromColor = opaque(hGrp->GetUnsigned("BackgroundColor", 3940932863UL));
        return bg;
    }

    bg.fromColor = opaque(hGrp->GetUnsigned("BackgroundColor2", 859006463UL));
    bg.toColor = opaque(hGrp->GetUnsigned("BackgroundColor3", 2880160255UL));
    bg.hasMid = hGrp->GetBool("UseBackgroundColorMid", false);
    if (bg.hasMid)
        bg.midColor = opaque(hGrp->GetUnsigned("BackgroundColor4", 1869583359UL));
    return bg;
}
}  // namespace

class SceneServeSource::Private
{
public:
    Document *doc = nullptr;
    SoFCUnifiedSelection *root = nullptr;
    std::unique_ptr<Render::Renderer> renderer;
    QTimer timer;
    std::vector<boost::signals2::scoped_connection> connections;

    ~Private()
    {
        // Detach the backend before the graph goes: the cache manager
        // lives in the root and would otherwise push into a renderer
        // that is already being destroyed.
        if (root) {
            root->setExternalRenderer(nullptr);
            root->unref();
        }
    }

    /// Hang every view provider of the document under the root, the way
    /// View3DInventorViewer::addViewProvider does for a real view.
    void attachViewProviders()
    {
        if (!doc || !root)
            return;
        for (auto *vp : doc->getViewProvidersOfType(
                 ViewProviderDocumentObject::getClassTypeId())) {
            auto *vpd = static_cast<ViewProviderDocumentObject *>(vp);
            SoSeparator *vproot = vpd->getRoot();
            if (!vproot || doc->isClaimed3D(vpd) || !vpd->canAddToSceneGraph())
                continue;
            // Only the physical objects. A real viewer puts the rest in
            // its nonObjectGroup, which is view furniture — the same
            // reason the overlays are not published either.
            if (!vpd->isPartOfPhysicalObject())
                continue;
            if (root->findChild(vproot) < 0)
                root->addChild(vproot);
        }
    }

    /// A synthetic camera, since there is no real one to report. Fitted
    /// to the scene bounds at a fixed orientation: every joining viewer
    /// re-frames with its own fitAll, so this only has to be sane.
    void cameraMatrices(float *viewMatrix, float *projMatrix)
    {
        SbBox3f box;
        if (root) {
            SbViewportRegion viewport{short(kDefaultWidth),
                                      short(kDefaultHeight)};
            SoGetBoundingBoxAction bboxaction(viewport);
            bboxaction.apply(root);
            box = bboxaction.getBoundingBox();
        }
        SbVec3f center(0.0f, 0.0f, 0.0f);
        float radius = 1.0f;
        if (!box.isEmpty()) {
            center = box.getCenter();
            SbVec3f span = box.getMax() - box.getMin();
            radius = std::max({span[0], span[1], span[2], 1e-3f}) * 0.5f;
        }

        // An isometric-ish eye, far enough out to hold the bounds.
        const float dist = radius * 4.0f;
        const SbVec3f dir = SbVec3f(-0.577f, -0.577f, -0.577f);
        const SbVec3f eye = center - dir * dist;

        SbVec3f zaxis = -dir;
        zaxis.normalize();
        SbVec3f up(0.0f, 0.0f, 1.0f);
        if (std::fabs(zaxis.dot(up)) > 0.99f)
            up = SbVec3f(0.0f, 1.0f, 0.0f);
        SbVec3f xaxis = up.cross(zaxis);
        xaxis.normalize();
        SbVec3f yaxis = zaxis.cross(xaxis);

        // GL-style column-major view matrix.
        const float view[16] = {
            xaxis[0], yaxis[0], zaxis[0], 0.0f,
            xaxis[1], yaxis[1], zaxis[1], 0.0f,
            xaxis[2], yaxis[2], zaxis[2], 0.0f,
            -xaxis.dot(eye), -yaxis.dot(eye), -zaxis.dot(eye), 1.0f};
        std::memcpy(viewMatrix, view, sizeof(view));

        // Orthographic, sized to the bounds — a parametric model has no
        // natural perspective and a wrong near/far plane is the one way
        // this can hide the whole scene.
        const float aspect = float(kDefaultWidth) / float(kDefaultHeight);
        const float halfh = radius * 1.2f;
        const float halfw = halfh * aspect;
        const float zn = 0.0f;
        const float zf = dist + radius * 4.0f;
        const float proj[16] = {
            1.0f / halfw, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f / halfh, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f / (zf - zn), 0.0f,
            0.0f, 0.0f, -zn / (zf - zn), 1.0f};
        std::memcpy(projMatrix, proj, sizeof(proj));
    }
};

SceneServeSource::SceneServeSource(Document *doc)
    : pimpl(new Private)
{
    pimpl->doc = doc;

    const std::string &type = RenderParams::getType();
    if (type.empty() || type == "Default") {
        Base::Console().Warning(
            "SceneServeSource: no render engine configured; nothing to "
            "publish through\n");
        return;
    }
    pimpl->renderer = Render::RendererFactory::create(type, nullptr, true);
    if (!pimpl->renderer) {
        Base::Console().Warning(
            "SceneServeSource: render engine '%s' cannot publish without a "
            "graphics device\n", type.c_str());
        return;
    }

    pimpl->root = new SoFCUnifiedSelection();
    pimpl->root->ref();
    pimpl->root->applySettings();
    pimpl->root->setDocument(doc);
    // No view: the per-view property overrides simply do not apply, and
    // the backend falls back to the global render parameters.
    pimpl->root->setExternalRenderer(pimpl->renderer.get(), nullptr);
    pimpl->attachViewProviders();

    // Coalesce: a recompute or a load changes many objects, and each one
    // would otherwise be a full traversal. Zero-timer, so the publish
    // happens once the current batch of changes has been applied.
    pimpl->timer.setSingleShot(true);
    pimpl->timer.setInterval(0);
    connect(&pimpl->timer, &QTimer::timeout,
            this, &SceneServeSource::onPublishTimeout);

    if (doc) {
        pimpl->connections.emplace_back(doc->signalNewObject.connect(
            [this](const ViewProviderDocumentObject &) {
                pimpl->attachViewProviders();
                schedulePublish();
            }));
        pimpl->connections.emplace_back(doc->signalDeletedObject.connect(
            [this](const ViewProviderDocumentObject &) { schedulePublish(); }));
        pimpl->connections.emplace_back(doc->signalChangedObject.connect(
            [this](const ViewProviderDocumentObject &, const App::Property &) {
                schedulePublish();
            }));
    }

    schedulePublish();
}

SceneServeSource::~SceneServeSource() = default;

namespace
{
/// The served documents. A function-local static so the sources are torn
/// down at exit rather than at static-destruction time in some other
/// translation unit, which would race the Coin graph they hold.
std::map<Document *, std::unique_ptr<SceneServeSource>> &servedDocuments()
{
    static std::map<Document *, std::unique_ptr<SceneServeSource>> sources;
    return sources;
}
}  // namespace

SceneServeSource *SceneServeSource::serve(Document *doc)
{
    if (!doc)
        return nullptr;
    auto &sources = servedDocuments();
    auto it = sources.find(doc);
    if (it != sources.end())
        return it->second.get();

    auto source = std::make_unique<SceneServeSource>(doc);
    if (!source->isValid())
        return nullptr;
    auto *raw = source.get();
    sources[doc] = std::move(source);
    return raw;
}

void SceneServeSource::unserve(Document *doc)
{
    servedDocuments().erase(doc);
}

bool SceneServeSource::isValid() const
{
    return pimpl->renderer && pimpl->root;
}

Document *SceneServeSource::document() const
{
    return pimpl->doc;
}

void SceneServeSource::schedulePublish()
{
    if (isValid() && !pimpl->timer.isActive())
        pimpl->timer.start();
}

void SceneServeSource::onPublishTimeout()
{
    publishNow();
}

bool SceneServeSource::publishNow()
{
    if (!isValid())
        return false;

    auto *manager = pimpl->root->getRenderManager();
    if (!manager)
        return false;

    // The two feeds a viewer supplies from outside its scene graph.
    pimpl->renderer->setBackground(backgroundFromPreferences());
    applySectionHatchTexture(*manager);

    // Build the caches without a frame. This is the whole of what a
    // drawing viewer's render path did for the feed.
    SbViewportRegion viewport{short(kDefaultWidth), short(kDefaultHeight)};
    manager->traverse(pimpl->root, viewport);

    float viewMatrix[16];
    float projMatrix[16];
    pimpl->cameraMatrices(viewMatrix, projMatrix);

    return pimpl->renderer->publish(QColor(0x33, 0x33, 0x33), viewMatrix,
                                    projMatrix, kDefaultWidth,
                                    kDefaultHeight);
}

#include "moc_SceneServeSource.cpp"
