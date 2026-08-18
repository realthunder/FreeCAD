// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#endif

#include <App/Application.h>
#include <App/PropertyContainer.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Gui/Inventor/SoFCRenderMaterial.h>
#include <Gui/RenderParams.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewParams.h>

#include "MaterialIcons.h"

using namespace MatGui;

namespace
{
/// How many queued icons one event loop turn renders. Each is a full
/// engine frame, so this trades first-paint latency for how long the GUI
/// thread is held: four 256px spheres is a few milliseconds.
constexpr int RenderPerTick = 4;
/// Multisampling of the rendered frame. The downsample to the smaller
/// sizes does most of the anti-aliasing work; this cleans up the largest.
constexpr int IconSamples = 4;
}  // namespace

/** The scene the icons are rendered from
 *
 * A viewer with no MDI view behind it, holding one sphere. It owns a
 * property container of its OWN (View3DInventorViewer::setRenderSettings)
 * rather than falling through to the global render preferences: an icon
 * has to look the same whatever shading model the user is currently
 * looking at, so this states Realistic shading, no matcap, and the
 * built-in studio environment regardless.
 */
class MatGui::IconScene: public Gui::View3DInventorViewer
{
public:
    IconScene()
        : Gui::View3DInventorViewer(nullptr)
    {
        setPopupMenuEnabled(false);
        setEnabledNaviCube(false);
        // Never on screen, but shown as far as Qt is concerned -- which is
        // what gives the widget a real GL context to render into.
        setAttribute(Qt::WA_DontShowOnScreen);
        const int size = MaterialIcons::sizes().front();
        resize(size, size);
        show();

        auto* root = dynamic_cast<SoSeparator*>(getSceneGraph());
        _render = new Gui::SoFCRenderMaterial;
        _render->ref();
        _material = new SoMaterial;
        _material->ref();
        root->addChild(_render);
        root->addChild(_material);
        root->addChild(new SoSphere);

        setCameraType(SoOrthographicCamera::getClassTypeId());
        setViewDirection(SbVec3f(0, 1, -0.35F));
        viewAll();

        settings();
        setRenderSettings(&_settings);
        // Mode 3 is the path that feeds the engine; without it the
        // backend is never created and the icons are plain Coin.
        setRenderCache(3);
        setRendererType(Gui::RenderParams::getType());
    }

    ~IconScene() override
    {
        if (_render) {
            _render->unref();
        }
        if (_material) {
            _material->unref();
        }
    }

    IconScene(const IconScene&) = delete;
    IconScene& operator=(const IconScene&) = delete;

    void apply(const App::Material& mat, const App::SurfaceFinish& finish)
    {
        const Base::Color& d = mat.diffuseColor;
        const Base::Color& s = mat.specularColor;
        const Base::Color& a = mat.ambientColor;
        const Base::Color& e = mat.emissiveColor;
        _material->diffuseColor.setValue(d.r, d.g, d.b);
        _material->specularColor.setValue(s.r, s.g, s.b);
        _material->ambientColor.setValue(a.r, a.g, a.b);
        _material->emissiveColor.setValue(e.r, e.g, e.b);
        _material->shininess.setValue(mat.shininess);
        _material->transparency.setValue(mat.transparency);

        // A PBR-mode appearance states its factor pair outright; a classic
        // one leaves both unset and the engine derives them from the Phong
        // slots (docs/RenderEngine.md, "Reading a Phong appearance").
        _render->metallic.setValue(mat.pbr ? mat.getMetallic() : -1.0F);
        _render->roughness.setValue(mat.pbr ? mat.getRoughness() : -1.0F);
        _render->finish.setValue(finish.pattern);
        _render->finishPitch.setValue(finish.pitch);
        _render->finishDepth.setValue(finish.depth);
        _render->finishAngle.setValue(finish.angle);
    }

    /// The frame, or a null image when the backend declined to draw one.
    QImage grab(int size)
    {
        auto* gl = static_cast<QtGLWidget*>(viewport());
        gl->makeCurrent();
        if (!QtGLContext::currentContext()) {
            return {};
        }
        // NOT savePicture(): with a backend active that routes through the
        // frame dump, which is raw PPM -- a format with no alpha channel at
        // all, so a transparent background comes back solid black. This
        // renders into an FBO we give an alpha channel ourselves, which is
        // also why the internal format is stated here rather than taken
        // from the InternalTextureFormat preference (GL_RGB is a legal
        // value of it, and would lose the alpha again).
        QtGLFramebufferObjectFormat format;
        format.setSamples(IconSamples);
        format.setAttachment(QtGLFramebufferObject::CombinedDepthStencil);
        format.setInternalTextureFormat(GL_RGBA8);
        QtGLFramebufferObject fbo(size, size, format);

        const QColor previous = backgroundColor();
        setBackgroundColor(QColor(0, 0, 0, 0));
        setGradientBackground(Background::NoGradient);
        // TWICE, and the second one is the icon. renderScene() hands the
        // backend its frame BEFORE Coin's traversal refreshes the feed the
        // backend draws from, so the first render after a material change
        // still draws the previous material. Rendered once per icon, the
        // whole library came out shifted by one card -- Gold wearing
        // Emerald's green.
        renderToFramebuffer(&fbo);
        renderToFramebuffer(&fbo);
        setBackgroundColor(previous);

        return fbo.toImage();
    }

private:
    /// The render settings an icon is always drawn with, independent of
    /// the user's preferences.
    void settings()
    {
        auto set = [this](const char* name, bool value) {
            auto* prop = static_cast<App::PropertyBool*>(_settings.addDynamicProperty(
                "App::PropertyBool", name, "Render", nullptr, App::Prop_NoPersist));
            prop->setValue(value);
        };
        set("Render_PBR", true);
        set("Render_PBRFromSpecular", true);
        set("Render_PBREnvBackground", false);
        set("Render_Matcap", false);
        set("Render_AO", false);
    }

    App::PropertyContainer _settings;
    Gui::SoFCRenderMaterial* _render {nullptr};
    SoMaterial* _material {nullptr};
};

MaterialIcons& MaterialIcons::instance()
{
    static MaterialIcons inst;
    return inst;
}

MaterialIcons::MaterialIcons()
{
    _timer = new QTimer(this);
    _timer->setSingleShot(true);
    _timer->setInterval(0);
    connect(_timer, &QTimer::timeout, this, &MaterialIcons::drain);
}

MaterialIcons::~MaterialIcons() = default;

const QList<int>& MaterialIcons::sizes()
{
    // Largest first: the first entry is rendered, the rest downsampled.
    static const QList<int> value {256, 128, 64, 32, 16};
    return value;
}

bool MaterialIcons::available() const
{
    return !_failed;
}

void MaterialIcons::invalidate()
{
    _cache.clear();
}

QString MaterialIcons::digestOf(const App::Material& material,
                                const App::SurfaceFinish& finish)
{
    // Everything the render actually depends on, so an edited appearance
    // misses the cache and an untouched one hits it.
    QByteArray raw;
    QDataStream out(&raw, QIODevice::WriteOnly);
    for (const Base::Color* c : {&material.diffuseColor,
                                 &material.specularColor,
                                 &material.ambientColor,
                                 &material.emissiveColor}) {
        out << c->r << c->g << c->b << c->a;
    }
    out << material.shininess << material.transparency
        << static_cast<int>(material.pbr) << finish.pattern << finish.pitch
        << finish.depth << finish.angle;
    return QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex());
}

QString MaterialIcons::cachePath(const QString& digest)
{
    QString dir = QString::fromStdString(App::Application::getUserCachePath())
        + QStringLiteral("MaterialIcons");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + digest + QStringLiteral(".png");
}

QIcon MaterialIcons::fromImage(const QImage& image) const
{
    QIcon icon;
    for (int size : sizes()) {
        QImage scaled = image.size() == QSize(size, size)
            ? image
            : image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        icon.addPixmap(QPixmap::fromImage(scaled));
    }
    return icon;
}

QIcon MaterialIcons::icon(const QString& key, const App::Material& material)
{
    if (_failed) {
        return {};
    }
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    // On disk from an earlier run or from the build's pre-warm.
    const QString digest = digestOf(material, material.finish);
    QImage cached;
    if (cached.load(cachePath(digest)) && !cached.isNull()) {
        QIcon icon = fromImage(cached);
        _cache[key] = icon;
        return icon;
    }
    for (const auto& queued : _queue) {
        if (queued.key == key) {
            return {};
        }
    }
    _queue.push_back({key, material, material.finish});
    _timer->start();
    return {};
}

QIcon MaterialIcons::finishIcon(const App::SurfaceFinish& finish)
{
    // One neutral material for every pattern, so what differs between the
    // icons is the finish and not the colour.
    App::Material neutral;
    neutral.setType(App::Material::DEFAULT);
    neutral.diffuseColor.set(0.62F, 0.63F, 0.66F);
    neutral.specularColor.set(0.30F, 0.30F, 0.31F);
    neutral.shininess = 0.75F;
    const QString key = QStringLiteral("finish:%1").arg(digestOf(neutral, finish));
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    if (_failed) {
        return {};
    }
    QIcon icon = build(key, neutral, finish);
    return icon;
}

void MaterialIcons::drain()
{
    for (int i = 0; i < RenderPerTick && !_queue.empty(); ++i) {
        Request req = _queue.front();
        _queue.pop_front();
        QIcon icon = build(req.key, req.material, req.finish);
        if (!icon.isNull()) {
            Q_EMIT iconReady(req.key);
        }
        if (_failed) {
            _queue.clear();
            return;
        }
    }
    if (!_queue.empty()) {
        _timer->start();
    }
}

QIcon MaterialIcons::build(const QString& key, const App::Material& material,
                           const App::SurfaceFinish& finish)
{
    if (_failed) {
        return {};
    }
    try {
        if (!_scene) {
            _scene = std::make_unique<IconScene>();
        }
        _scene->apply(material, finish);
        const int size = sizes().front();
        QImage img = _scene->grab(size);
        if (img.isNull()) {
            // No backend frame: nothing here is load bearing, so stop
            // asking rather than retry every card in the library.
            Base::Console().Log("MaterialIcons: no rendered frame, "
                                "material icons stay unavailable\n");
            _failed = true;
            return {};
        }
        img = img.convertToFormat(QImage::Format_ARGB32);
        img.save(cachePath(digestOf(material, finish)), "PNG");
        QIcon icon = fromImage(img);
        _cache[key] = icon;
        return icon;
    }
    catch (const Base::Exception& e) {
        Base::Console().Log("MaterialIcons: %s\n", e.what());
        _failed = true;
        return {};
    }
}

#include "moc_MaterialIcons.cpp"
