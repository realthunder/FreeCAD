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
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>

#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoSwitch.h>
#endif

#include <cmath>
#include <vector>

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

/** The PNG text key a bundled icon states its appearance digest under
 *
 * An icon that ships in the binary is a photograph of a material taken
 * at some earlier date, and both the material and the engine that drew
 * it go on changing. Carrying the digest lets a stale one be recognised
 * as stale and rendered afresh, so an edited card never keeps showing
 * the icon of what it used to be.
 *
 * A file that states NO digest is taken at face value. That is the user
 * override case: someone who drops their own artwork in is asserting it
 * is the icon for this material, and has no digest to state.
 */
const char* DigestKey = "FreeCAD.Appearance";

/// The billet a surface finish is shown on. Wide enough that the top
/// face still reads as a face at this viewing angle, short enough that
/// the wall does not become the whole icon.
constexpr float CylinderRadius = 1.0F;
constexpr float CylinderHeight = 1.1F;

/// SoFCRenderMaterial::framePalette kinds, matching fc_finish.sh's
/// FC_FRAME_PLANAR and FC_FRAME_RADIAL.
constexpr float FramePlanar = 1.0F;
constexpr float FrameRadial = 2.0F;

/** The orthographic height each shape is framed in
 *
 * Its own projected extent plus a few per cent of margin, stated
 * ABSOLUTELY rather than as a factor of what viewAll() chose. Two
 * reasons, and the second is the one that bites:
 *
 * - viewAll() frames the scene's bounding SPHERE, not its silhouette,
 *   so a shape two units across is fitted as though it were its own
 *   diagonal -- 2*sqrt(3) = 3.46 for the sphere -- and lands with a
 *   fifth of the frame empty on every side. A 64 pixel row was showing
 *   a 37 pixel sphere, which is most of why the rows read as too tall
 *   for what was in them.
 * - It also frames the shape that was showing BEFORE the switch
 *   changed, the same one-step lag the backend has. The first finish
 *   icon rendered after an appearance came out 12 per cent small --
 *   exactly the ratio of the two shapes' bounding diagonals, which is
 *   how it was identified.
 *
 * Measurements, not theory: re-take them from the alpha bounding box of
 * a rendered icon if a shape or the camera angle changes.
 */
constexpr float SphereFrame = 2.10F;
constexpr float CylinderFrame = 2.15F;

/// The gloss of a finish icon before its own relief roughens it: a
/// polished metal, smoother than anything Phong data can express, since
/// a rough surface blurs away the very detail the icon exists to show.
constexpr float BaseRoughness = 0.22F;
}  // namespace

/** The scene the icons are rendered from
 *
 * A viewer with no MDI view behind it, holding the two shapes an icon is
 * taken of. It owns a property container of its OWN
 * (View3DInventorViewer::setRenderSettings) rather than falling through
 * to the global render preferences: an icon has to look the same
 * whatever shading model the user is currently looking at, so this
 * states Realistic shading, no matcap, and the built-in studio
 * environment regardless.
 *
 * **Two shapes, because they answer different questions.** An appearance
 * is shown on a SPHERE, as every CAD tool shows one: a single unbroken
 * highlight, every grazing angle from head-on to the silhouette in one
 * picture, and no seam or corner for a colour to hide behind.
 *
 * A surface finish is shown on a CYLINDER, and has to be. The finish
 * shader lays its pattern out in a frame the geometry states -- planar,
 * or radial about an axis (fc_finish.sh) -- and a sphere is neither, so
 * every finish on one falls back to the triplanar projection, which is
 * three axis-aligned guesses blended together. A brushed lay simply
 * disappeared. A cylinder states both frames at once and is the shape
 * these processes are actually performed on: the wall is radial, so a
 * straight knurl runs along the axis and a diamond knurl closes at the
 * seam, and the top face is planar, so turning marks come out as the
 * concentric rings they are.
 */
class MatGui::IconScene: public Gui::View3DInventorViewer
{
public:
    /// Which shape the next grab() is of.
    enum class Shape
    {
        Sphere,
        Cylinder
    };

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

        _material = new SoMaterial;
        _material->ref();

        // Both shapes are analytic, so how round they come out is a
        // number rather than a property of any mesh -- and the default
        // 0.5 leaves a silhouette with visible corners at 256 pixels,
        // which is the one flaw an icon cannot hide behind being small.
        // A sphere and a cylinder at full complexity are a few thousand
        // triangles once, for a scene drawn twice per icon.
        auto* complexity = new SoComplexity;
        complexity->value = 1.0F;

        _shapes = new SoSwitch;
        _shapes->ref();
        _shapes->addChild(buildSphere());
        _shapes->addChild(buildCylinder());
        _shapes->whichChild = 0;

        auto* root = dynamic_cast<SoSeparator*>(getSceneGraph());
        root->addChild(complexity);
        root->addChild(_shapes);

        setCameraType(SoOrthographicCamera::getClassTypeId());
        setShape(Shape::Sphere);

        settings();
        setRenderSettings(&_settings);
        // Mode 3 is the path that feeds the engine; without it the
        // backend is never created and the icons are plain Coin.
        setRenderCache(3);
        // The backend this build has, NOT the one the user has their own
        // views set to. Same reason the render settings above are stated
        // rather than inherited: a material has to look like itself in
        // the tree whatever the user is currently looking at, and where
        // the preference still says "Default" it would otherwise be
        // Coin shading a material the appearance dialog shows in PBR.
        setRendererType(Gui::RenderParams::preferredType());
    }

    ~IconScene() override
    {
        if (_shapes) {
            _shapes->unref();
        }
        if (_material) {
            _material->unref();
        }
    }

    IconScene(const IconScene&) = delete;
    IconScene& operator=(const IconScene&) = delete;

    /// Show \a shape, and frame the camera on it. Two shapes want two
    /// viewpoints: a sphere looks the same from anywhere, a cylinder has
    /// to be seen from above its rim or its top face is an edge.
    void setShape(Shape shape)
    {
        _shapes->whichChild = shape == Shape::Sphere ? 0 : 1;
        setAnimationEnabled(false);
        if (shape == Shape::Sphere) {
            setViewDirection(SbVec3f(0, 1, -0.35F));
        }
        else {
            // Down onto the rim at about 35 degrees: enough of the top
            // face to read a turned lay as circles rather than as an
            // ellipse's worth of them, and enough wall left for a knurl.
            setViewDirection(SbVec3f(0, -0.57F, -0.82F));
        }
        viewAll();
        frame(shape == Shape::Sphere ? SphereFrame : CylinderFrame);
    }

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
        // Every draw in the scene takes the same appearance and the same
        // finish; what differs between them is the frame each states,
        // which is geometry and was set once when it was built.
        for (auto* render : _render) {
            render->metallic.setValue(mat.pbr ? mat.getMetallic() : -1.0F);
            render->roughness.setValue(mat.pbr ? mat.getRoughness() : -1.0F);
            render->finish.setValue(finish.pattern);
            render->finishPitch.setValue(finish.pitch);
            render->finishDepth.setValue(finish.depth);
            render->finishAngle.setValue(finish.angle);
        }
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
    /// State outright how much the camera takes in, overriding whatever
    /// viewAll() decided. Only meaningful for the orthographic camera
    /// the icons use, where that is one field.
    void frame(float height)
    {
        SoCamera* camera = getSoRenderManager()->getCamera();
        if (!camera
            || !camera->isOfType(SoOrthographicCamera::getClassTypeId())) {
            return;
        }
        static_cast<SoOrthographicCamera*>(camera)->height = height;
    }

    /// A shape under its own appearance node, recorded so apply() can
    /// reach it. The frame it states is set by the caller.
    Gui::SoFCRenderMaterial* branch(SoSeparator* parent, SoNode* shape)
    {
        auto* sep = new SoSeparator;
        auto* render = new Gui::SoFCRenderMaterial;
        sep->addChild(render);
        sep->addChild(_material);
        sep->addChild(shape);
        parent->addChild(sep);
        _render.push_back(render);
        return render;
    }

    SoNode* buildSphere()
    {
        auto* root = new SoSeparator;
        // No frame stated: a sphere is neither a plane nor a surface of
        // revolution about one axis, so it shades triplanarly, which is
        // all a sphere can honestly do and is why finishes are not shown
        // on one.
        branch(root, new SoSphere);
        return root;
    }

    SoNode* buildCylinder()
    {
        auto* root = new SoSeparator;
        auto* wall = new SoCylinder;
        wall->radius = CylinderRadius;
        wall->height = CylinderHeight;
        wall->parts = SoCylinder::SIDES;
        auto* top = new SoCylinder;
        top->radius = CylinderRadius;
        top->height = CylinderHeight;
        top->parts = SoCylinder::TOP;

        // Coin turns a cylinder about Y, centred on the origin. Three
        // SbVec4f make a frame -- (origin, kind), (axis, radius),
        // (xdir, spare) -- and with one entry and no per-face index
        // stream, every vertex of the draw reads entry 0.
        setFrame(branch(root, wall),
                 SbVec3f(0, 0, 0), FrameRadial,
                 SbVec3f(0, 1, 0), CylinderRadius,
                 SbVec3f(1, 0, 0));
        // The top face's frame origin is the CENTRE of that face, which
        // is what a turned lay draws its circles about.
        //
        // Its X runs at 45 degrees for a reason worth keeping. A machined
        // face has no canonical X -- it is whatever direction the tool
        // swept -- but the camera looks down the Y-Z plane and the light
        // follows the camera, so a one-directional lay laid along Z tilts
        // every normal in X alone, square to the only light there is, and
        // shades as though the face were plain. Brushed and the straight
        // knurl both vanished from the top face; the two-directional
        // patterns beside them did not. Laid oblique, all four show.
        constexpr float Diagonal = 0.70710678F;
        setFrame(branch(root, top),
                 SbVec3f(0, CylinderHeight * 0.5F, 0), FramePlanar,
                 SbVec3f(0, 1, 0), 0.0F,
                 SbVec3f(Diagonal, 0, Diagonal));
        return root;
    }

    static void setFrame(Gui::SoFCRenderMaterial* render, const SbVec3f& origin,
                         float kind, const SbVec3f& axis, float radius,
                         const SbVec3f& xdir)
    {
        render->framePalette.setNum(3);
        render->framePalette.set1Value(0, SbVec4f(origin[0], origin[1], origin[2], kind));
        render->framePalette.set1Value(1, SbVec4f(axis[0], axis[1], axis[2], radius));
        render->framePalette.set1Value(2, SbVec4f(xdir[0], xdir[1], xdir[2], 0.0F));
        // One index, and it has to be here: the render cache publishes a
        // frame palette only alongside an index array, and every reader
        // downstream resolves an out-of-range face to entry 0. So a
        // single 0 is how a draw says "all of me, this one frame" --
        // which is what each of these branches is. Without it the whole
        // palette is dropped and the shape shades triplanarly, which
        // looks like a plausible finish rather than like a failure: the
        // turned billet came out with concentric rings on its WALL.
        render->frameIndices.setNum(1);
        render->frameIndices.set1Value(0, 0);
    }

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
    /// Every appearance node in the scene: one for the sphere, one per
    /// face of the cylinder. They differ only in the frame they state.
    std::vector<Gui::SoFCRenderMaterial*> _render;
    SoSwitch* _shapes {nullptr};
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
    _paths.clear();
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

QIcon MaterialIcons::icon(const QString& key, const App::Material& material,
                          const QString& name)
{
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    const QString digest = digestOf(material, material.finish);
    // Bundled with the module, or supplied by the user in place of what
    // is bundled. Ahead of everything else because it costs no render at
    // all, because it is the same icon on every installation, and
    // because it is the only icon there is where nothing can be drawn.
    QIcon bundled = fromResource(key, resourceName(name), digest);
    if (!bundled.isNull()) {
        return bundled;
    }
    if (_failed) {
        return {};
    }
    // On disk from an earlier run.
    QImage cached;
    const QString path = cachePath(digest);
    if (cached.load(path) && !cached.isNull()) {
        QIcon icon = fromImage(cached);
        _cache[key] = icon;
        _paths[key] = path;
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

App::Material MaterialIcons::finishMaterial(uint8_t pattern)
{
    // One material for every pattern except in its GLOSS, so that what
    // differs between the icons is the finish and nothing else. A
    // POLISHED METAL, stated in PBR outright rather than left for the
    // engine to read out of Phong slots.
    //
    // Metal because a finish is only ever visible in a reflection. What
    // the shader does with a pattern is tilt the normal; on a diffuse
    // surface that barely changes how much light the surface returns, so
    // a grey dielectric showed the deeper patterns faintly and the
    // shallow ones not at all. A metal has no diffuse term to wash it
    // out -- every bit of what it shows is reflection, so the same tilt
    // moves the whole shading. It is also what these processes are
    // actually done to.
    //
    // Aluminium's measured reflectance for the base colour, LINEAR (see
    // docs/RenderEngine.md -- the BRDF is linear and nothing applies an
    // sRGB transform).
    App::Material metal;
    metal.setPBR(true);
    metal.diffuseColor.set(0.9130F, 0.9220F, 0.9240F);
    metal.ambientColor.set(0.0913F, 0.0922F, 0.0924F);
    metal.emissiveColor.set(0.0F, 0.0F, 0.0F);
    metal.transparency = 0.0F;
    metal.setMetallic(1.0F);
    metal.setRoughness(finishRoughness(pattern));
    return metal;
}

float MaterialIcons::finishRoughness(uint8_t pattern)
{
    // Matte is not one of the patterns, and could not be: it is a gloss
    // level, which is roughness, and roughness is what fine relief IS.
    // The shader already says so -- relief too fine to draw has its
    // slope variance added to the roughness instead (fc_finish.sh,
    // "Relief too fine to draw is not relief that stopped existing"):
    //
    //     lost  = (1 - visible) * 0.5 * (pi * depth / pitch)^2
    //     rough = sqrt(rough^2 + lost)
    //
    // which is exactly what turns a blasted surface matte at real scale.
    // The icons bypass it: defaultFinish() states a pitch coarse enough
    // to READ at 32 pixels, tens of pixels per feature, so nothing is
    // lost to roughness and every finish came out mirror bright --
    // blasted as hammered chrome, which no blasted part looks like.
    //
    // So the gloss is computed here from what the process really leaves,
    // by the shader's own formula, while the pattern goes on being drawn
    // at the coarse pitch. The icon then shows the shape of the process
    // at a size you can see and the gloss of it at true scale.
    struct Actual
    {
        float pitch;   ///< mm, as the process really leaves it
        float depth;   ///< mm
        bool resolved; ///< whether that relief is visible at all at the
                       ///< sizes a part is looked at -- a knurl is, a
                       ///< blast is not, and only unresolved relief
                       ///< becomes roughness
    };
    Actual actual {};
    switch (pattern) {
        case App::SurfaceFinish::Knurl:
        case App::SurfaceFinish::KnurlStraight:
            actual = {0.800F, 0.250F, true};
            break;
        case App::SurfaceFinish::Turned:
            actual = {0.200F, 0.008F, true};
            break;
        case App::SurfaceFinish::Brushed:
            actual = {0.040F, 0.004F, false};
            break;
        case App::SurfaceFinish::Blasted:
            actual = {0.060F, 0.015F, false};
            break;
        default:
            return BaseRoughness;
    }
    if (actual.resolved || actual.pitch <= 0.0F) {
        return BaseRoughness;
    }
    const float slope = float(M_PI) * actual.depth / actual.pitch;
    const float lost = 0.5F * slope * slope;
    return std::min(std::sqrt(BaseRoughness * BaseRoughness + lost), 1.0F);
}

App::SurfaceFinish MaterialIcons::defaultFinish(uint8_t pattern)
{
    // These are coarser than the processes they name really are, and
    // deliberately so. An icon is looked at around 24 to 32 pixels
    // across, where a truthful pitch puts several cycles inside one
    // pixel and every pattern averages to the same flat grey -- the
    // detail does not merely soften, it stops existing. So each states
    // the coarsest scale still recognisable as itself, which puts a
    // handful of features across the billet rather than a hundred.
    // Anyone who wants the real scale gets it: a finish with a pitch of
    // its own no longer matches the bundled digest and is rendered.
    App::SurfaceFinish finish;
    finish.pattern = pattern;
    switch (pattern) {
        case App::SurfaceFinish::Knurl:
            // A diamond has to be big enough to read AS a diamond, so
            // this is the coarsest of the lot.
            finish.pitch = 0.650F;
            finish.depth = 0.160F;
            break;
        case App::SurfaceFinish::KnurlStraight:
            finish.pitch = 0.450F;
            finish.depth = 0.110F;
            break;
        case App::SurfaceFinish::Brushed:
            finish.pitch = 0.200F;
            finish.depth = 0.080F;
            break;
        case App::SurfaceFinish::Blasted:
            finish.pitch = 0.240F;
            finish.depth = 0.064F;
            break;
        case App::SurfaceFinish::Turned:
            finish.pitch = 0.240F;
            finish.depth = 0.052F;
            break;
        default:
            return {};
    }
    finish.normalize();
    return finish;
}

QIcon MaterialIcons::finishIcon(const App::SurfaceFinish& finish)
{
    const App::Material neutral = finishMaterial(finish.pattern);
    const QString digest = digestOf(neutral, finish);
    const QString key = QStringLiteral("finish:%1").arg(digest);
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    // Bundled only for the pitch and depth defaultFinish() states; a
    // finish the user has given a scale of its own states a different
    // digest and falls through to a render, which is the whole point of
    // showing the scale rather than the pattern alone.
    QIcon bundled = fromResource(key, finishResourceName(finish.pattern), digest);
    if (!bundled.isNull()) {
        return bundled;
    }
    if (_failed) {
        return {};
    }
    return build(key, neutral, finish, IconShape::Cylinder);
}

QString MaterialIcons::resourceName(const QString& materialName)
{
    if (materialName.isEmpty()) {
        return {};
    }
    // An ordinary icon name, so it shares a namespace with every other
    // icon in FreeCAD -- hence the prefix -- and has to survive being a
    // file name, which "Shiny Plastic" does not as it stands.
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    return QStringLiteral("Appearance_") + QString(materialName).replace(unsafe,
                                                                         QStringLiteral("_"));
}

QString MaterialIcons::finishResourceName(uint8_t pattern)
{
    const char* name = App::SurfaceFinish::patternName(pattern);
    if (!name || !name[0]) {
        return {};
    }
    return QStringLiteral("Finish_") + QString::fromLatin1(name);
}

QString MaterialIcons::resourcePath(const QString& file)
{
    if (file.isEmpty()) {
        return {};
    }
    // The search path BitmapFactory maintains, read in order: the user's
    // own icon directory and any custom Bitmaps path come first and the
    // bundled resource last, which is the whole of how an icon is
    // overridden in FreeCAD. Resolved to a concrete path rather than
    // left as the "icons:" prefix, because a caller may need to name the
    // file to something that does not know about Qt search paths.
    const QString leaf = QStringLiteral("/") + file + QStringLiteral(".png");
    const QStringList dirs = QDir::searchPaths(QStringLiteral("icons"));
    for (const QString& dir : dirs) {
        QString path = dir;
        while (path.endsWith(QLatin1Char('/'))) {
            path.chop(1);
        }
        path += leaf;
        if (QFile::exists(path)) {
            return path;
        }
    }
    return {};
}

QIcon MaterialIcons::fromResource(const QString& key, const QString& file,
                                  const QString& digest)
{
    const QString path = resourcePath(file);
    if (path.isEmpty()) {
        return {};
    }
    // Loaded here rather than through BitmapFactory::pixmap() because
    // this needs the image at full size and needs its text chunks, both
    // of which a cached QPixmap has lost.
    QImage image;
    if (!image.load(path)) {
        return {};
    }
    const QString stated = image.text(QString::fromLatin1(DigestKey));
    if (!stated.isEmpty() && stated != digest) {
        return {};
    }
    QIcon icon = fromImage(image);
    _cache[key] = icon;
    _paths[key] = path;
    return icon;
}

QString MaterialIcons::iconPath(const QString& key) const
{
    auto it = _paths.find(key);
    return it == _paths.end() ? QString() : it->second;
}

bool MaterialIcons::renderToFile(const App::Material& material,
                                 const App::SurfaceFinish& finish, const QString& path,
                                 IconShape shape)
{
    QImage image = render(material, finish, shape);
    if (image.isNull()) {
        return false;
    }
    image.setText(QString::fromLatin1(DigestKey), digestOf(material, finish));
    return image.save(path, "PNG");
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

QImage MaterialIcons::render(const App::Material& material,
                             const App::SurfaceFinish& finish, IconShape shape)
{
    if (_failed) {
        return {};
    }
    try {
        if (!_scene) {
            _scene = std::make_unique<IconScene>();
        }
        _scene->setShape(shape == IconShape::Cylinder ? IconScene::Shape::Cylinder
                                                       : IconScene::Shape::Sphere);
        _scene->apply(material, finish);
        QImage img = _scene->grab(sizes().front());
        if (img.isNull()) {
            // No backend frame: nothing here is load bearing, so stop
            // asking rather than retry every card in the library.
            Base::Console().Log("MaterialIcons: no rendered frame, "
                                "material icons stay unavailable\n");
            _failed = true;
            return {};
        }
        return img.convertToFormat(QImage::Format_ARGB32);
    }
    catch (const Base::Exception& e) {
        Base::Console().Log("MaterialIcons: %s\n", e.what());
        _failed = true;
        return {};
    }
}

QIcon MaterialIcons::build(const QString& key, const App::Material& material,
                           const App::SurfaceFinish& finish, IconShape shape)
{
    QImage img = render(material, finish, shape);
    if (img.isNull()) {
        return {};
    }
    const QString path = cachePath(digestOf(material, finish));
    img.save(path, "PNG");
    QIcon icon = fromImage(img);
    _cache[key] = icon;
    _paths[key] = path;
    return icon;
}

#include "moc_MaterialIcons.cpp"
