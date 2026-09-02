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
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>

#include <Inventor/nodes/SoComplexity.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoSwitch.h>
#endif

#include <algorithm>
#include <cmath>
#include <vector>

#include <App/Application.h>
#include <App/PropertyContainer.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Gui/Inventor/SoFCRenderMaterial.h>
#include <Gui/RenderParams.h>
#include <Gui/Renderer/Environment.h>
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
///
/// Stated here and pinned on the viewer (setNumSamples), NOT left to the
/// AntiAliasing preference, for the same reason as every other setting in
/// settings(): these pictures ship. The preference's default went from 4x
/// to off (View3DInventorViewer::getNumSamples), and the icons rendered
/// after that came out with a BINARY alpha -- 0 or 255, no coverage
/// ladder at all -- against the 0/64/128/191/255 of the ones already in
/// the binary. A sphere in Realistic shading is exactly the case that
/// default gives up: a bare limb with no edge drawn along it.
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
 * ABSOLUTELY -- as is the rest of the camera now, in IconScene::place()
 * -- rather than taken from viewAll(). Two reasons, and the second is
 * the one that bites:
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
// The checkerboard behind the sphere (IconScene::buildBackdrop): how
// far behind the sphere's centre it sits along the line of sight, and
// how many cells across the frame. It fills the frame exactly, so the
// cells come out whole at the icon's edges.
constexpr float BackdropDistance = 1.3F;
// Where the camera stands from the origin, and how far either side of
// it the clip planes reach: the shapes are within about 1.1 of the
// origin and the checkerboard 1.3 behind it.
constexpr float CameraDistance = 4.0F;
constexpr float CameraReach = 2.5F;
constexpr int BackdropCells = 6;
// Model units per unit of the icon's sphere, for the glass absorption
// density a card states per millimetre: the two-unit ball stands for a
// 20 mm one, which is about what a tinted-glass swatch is.
constexpr float IconGlassScale = 10.0F;
// The ball's diameter (SoSphere's default radius is one), and how deep
// the icon shows a coloured glass's tint: the optical depth its most
// absorbed channel reaches at the ball's centre, when the card's own
// density leaves it shallower (IconScene::apply).
constexpr float SphereDiameter = 2.0F;
constexpr float IconTintDepth = 0.20F;

/// The gloss of a finish icon before its own relief roughens it: a
/// polished metal, smoother than anything Phong data can express, since
/// a rough surface blurs away the very detail the icon exists to show.
///
/// Not as polished as it was, though. At 0.22 the billet mirrored the
/// environment nearly perfectly, so whatever it happened to point at
/// arrived undimmed: under a single hard key a tenth of the icon
/// clipped to flat white, and the pattern in the clipped part stopped
/// existing, which is the opposite of what the picture is for. A
/// reflection has to be able to land on a bright source and still have
/// somewhere above it to go.
constexpr float BaseRoughness = 0.32F;
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
        // Before the backend is created below: attaching the feed is what
        // hands it a sample count (applyRendererAntiAliasing).
        setNumSamples(IconSamples);
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
        lighting(shape);
        if (shape == Shape::Sphere) {
            setViewDirection(SbVec3f(0, 1, -0.35F));
        }
        else {
            // Down onto the rim at about 35 degrees above the billet's
            // equator: enough of the top face to read a turned lay as
            // circles rather than as an ellipse's worth of them, and
            // enough wall left for a knurl. Stated about Z, because the
            // billet stands about Z (see buildCylinder).
            //
            // aim() and not setViewDirection(), which derives the up
            // vector from the direction alone: the rotation it picks is
            // the shortest one from -Z, and for a direction tilted this
            // far down that lands the camera's up pointing BELOW the
            // horizon. The billet came out upside down, top face at the
            // bottom of the frame.
            aim(SbVec3f(0, -0.819F, -0.574F), SbVec3f(0, 0, 1));
        }
        place(shape == Shape::Sphere ? SphereFrame : CylinderFrame);
    }

    void apply(const App::MaterialAppearance& mat, const App::SurfaceFinish& finish,
               const App::MaterialRenderProperties& props)
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
        // A card can also state render features App::MaterialAppearance does not
        // carry. Glass is the one that reaches an icon: without it a
        // glass card renders as its Phong fallback, which for a
        // transparency near 1 is a featureless disc -- the four shipped
        // glass presets came out identical grey circles that way.
        float glassIOR = 0.0F;
        float glassDensity = 0.0F;
        float glassRoughness = 0.0F;
        bool glass = false;
        for (const auto& prop : props) {
            if (prop.name == "Render_Glass") {
                glass = prop.value != 0.0;
            }
            else if (prop.name == "Render_GlassIOR") {
                glassIOR = float(prop.value);
            }
            else if (prop.name == "Render_GlassDensity") {
                glassDensity = float(prop.value);
            }
            else if (prop.name == "Render_GlassRoughness") {
                glassRoughness = float(prop.value);
            }
        }

        // Something to see THROUGH. A see-through material over the
        // icon's transparent background is the theme colour tinted, which
        // at 32 px is not distinguishable from a dull opaque one, and a
        // glass one refracts the scene behind it, which was nothing: the
        // shipped glass presets rendered as black spheres that way. The
        // checkerboard is only shown where it can be seen through.
        _backdrop->whichChild = (glass || mat.transparency > 0.0F)
            ? SO_SWITCH_ALL : SO_SWITCH_NONE;

        // Absorption density is per model unit -- per millimetre, in a
        // document -- and the icon's ball is two units across, so a
        // card's density, taken as is, tints nothing. The ball stands
        // for a ball IconGlassScale times wider.
        //
        // That is still not enough for a faint colour. A card states its
        // density for real parts, where a 10 mm sheet of acrylic is
        // barely blue, and that is right; but on a 20 mm ball the same
        // statement shows no colour at all, and the colour is the one
        // thing that tells acrylic from glass at 32 px. So a COLOURED
        // glass is shown with its density raised until its most absorbed
        // channel reaches IconTintDepth at the ball's centre. A
        // colourless one -- no chroma in the diffuse -- has no colour to
        // show and keeps whatever darkness it states, and one already
        // deeper than that, like the tinted preset, is left as stated.
        float density = glassDensity * IconGlassScale;
        if (glass) {
            if (density <= 0.0F) {
                // The engine's automatic density: about one optical depth
                // across the body's diagonal (BGFXView::submitGlassSurface).
                density = 3.0F / (SphereDiameter * std::sqrt(3.0F));
            }
            // The engine absorbs with the LINEAR colour when it is
            // colour managed (submitGlassSurface decodes the authored
            // diffuse like the mesh pass, as Cycles does), so the depth
            // this rule predicts is taken from the same number -- read
            // off the picked value, a mid tint comes out 1.8x shallower
            // than the ball then shows it.
            const bool managed = Gui::RenderParams::getOutputTransform()
                != long(Render::OutputConfig::None);
            auto lin = [managed](float c) {
                return managed ? Render::srgbToLinear(c) : c;
            };
            const float cr = 1.0F - lin(d.r);
            const float cg = 1.0F - lin(d.g);
            const float cb = 1.0F - lin(d.b);
            const float most = std::max({cr, cg, cb});
            const float chroma = most - std::min({cr, cg, cb});
            const float depth = density * most * SphereDiameter;
            if (chroma > 1.0e-3F && depth > 0.0F && depth < IconTintDepth) {
                density *= IconTintDepth / depth;
            }
        }

        for (auto* render : _render) {
            render->metallic.setValue(mat.pbr ? mat.getMetallic() : -1.0F);
            render->roughness.setValue(mat.pbr ? mat.getRoughness() : -1.0F);
            render->finish.setValue(finish.pattern);
            render->finishPitch.setValue(finish.pitch);
            render->finishDepth.setValue(finish.depth);
            render->finishAngle.setValue(finish.angle);
            render->glass.setValue(glass);
            render->glassIOR.setValue(glassIOR);
            render->glassDensity.setValue(density);
            render->glassRoughness.setValue(glassRoughness);
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
    /// Point the camera along \a dir with \a up as the world up, which
    /// is the part setViewDirection() leaves to chance.
    void aim(const SbVec3f& dir, const SbVec3f& up)
    {
        SoCamera* camera = getSoRenderManager()->getCamera();
        if (!camera) {
            return;
        }
        // A camera looks down its own -Z with +Y up, so its basis in
        // world terms is (right, up, backwards) and the rotation that
        // carries it there has those three as its rows.
        SbVec3f back = -dir;
        back.normalize();
        SbVec3f right = up.cross(back);
        if (right.length() < 1.0e-6F) {
            right = SbVec3f(1, 0, 0);
        }
        right.normalize();
        const SbVec3f above = back.cross(right);
        SbMatrix basis;
        basis.makeIdentity();
        for (int i = 0; i < 3; ++i) {
            basis[0][i] = right[i];
            basis[1][i] = above[i];
            basis[2][i] = back[i];
        }
        camera->orientation.setValue(SbRotation(basis));
    }

    /// The rig, which is not the same question for the two shapes.
    ///
    /// A SPHERE presents every normal at once, so it always turns one
    /// of them toward whatever the brightest thing in the environment
    /// is: a single hard key is ideal, and gives the crisp highlight
    /// that is most of how a metal reads apart from a dielectric at 32
    /// pixels. Interior, and no exposure correction.
    ///
    /// A BILLET has two faces and they sample the environment in two
    /// places, so one hard key lights one of them and misses the other.
    /// Measured under Interior: the wall came out twice the top face
    /// and 16 per cent of the blasted icon was clipped to white, which
    /// is the sphere's virtue turned into the billet's problem. Studio
    /// spreads the same total radiance over four sources including one
    /// overhead, so both faces have something to return, and the
    /// exposure buys back the headroom a mirror-bright metal needs when
    /// it does find a source.
    void lighting(Shape shape)
    {
        const bool sphere = shape == Shape::Sphere;
        if (_envPreset) {
            _envPreset->setValue(long(sphere ? 4 : 5));
        }
        if (_exposure) {
            _exposure->setValue(sphere ? 1.0 : 0.80);
        }
    }

    /// Put the camera where it sees the shape whole: on the line it is
    /// already looking along, a stated distance from the origin the
    /// shapes are built about, taking in \a height. Stated, and not
    /// viewAll(): that fits the scene's bounding box -- the box of what
    /// was SHOWING, one card behind -- and after a glass preset the box
    /// still held that card's checkerboard, so the camera moved and
    /// the next icon came out 13 px high with its top clipped off.
    /// Only meaningful for the orthographic camera the icons use.
    void place(float height)
    {
        auto* camera = dynamic_cast<SoOrthographicCamera*>(
            getSoRenderManager()->getCamera());
        if (!camera) {
            return;
        }
        SbVec3f dir;
        camera->orientation.getValue().multVec(SbVec3f(0, 0, -1), dir);
        camera->position = -dir * CameraDistance;
        camera->focalDistance = CameraDistance;
        camera->nearDistance = CameraDistance - CameraReach;
        camera->farDistance = CameraDistance + CameraReach;
        camera->height = height;
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
        _backdrop = new SoSwitch;
        _backdrop->addChild(buildBackdrop());
        _backdrop->whichChild = SO_SWITCH_NONE;
        root->addChild(_backdrop);
        // No frame stated: a sphere is neither a plane nor a surface of
        // revolution about one axis, so it shades triplanarly, which is
        // all a sphere can honestly do and is why finishes are not shown
        // on one.
        branch(root, new SoSphere);
        return root;
    }

    /// A checkerboard filling the frame behind the sphere, facing the
    /// camera. Shown only for a material it can be seen through, so an
    /// opaque icon keeps its round outline on a transparent surround
    /// while a glass one is a ball on a checkerboard, with the cells
    /// behind it bent by the refraction. Cells, not lines: a line thin
    /// enough to be a grid at 256 px is gone at 32.
    SoNode* buildBackdrop()
    {
        // The direction the sphere is viewed along (setShape), which is
        // both the board's normal and the line it sits behind the ball on.
        SbVec3f dir(0, 1, -0.35F);
        dir.normalize();
        auto* root = new SoSeparator;
        auto* transform = new SoTransform;
        transform->translation = dir * BackdropDistance;
        transform->rotation = SbRotation(SbVec3f(0, 0, 1), -dir);
        root->addChild(transform);

        // The orthographic camera takes in SphereFrame units across a
        // square viewport (place()), so a board that size is the frame.
        const float half = SphereFrame * 0.5F;
        const float cell = SphereFrame / BackdropCells;
        std::vector<SbVec3f> points;
        std::vector<int32_t> light;
        std::vector<int32_t> dark;
        for (int i = 0; i < BackdropCells; ++i) {
            for (int j = 0; j < BackdropCells; ++j) {
                const float x0 = -half + float(i) * cell;
                const float y0 = -half + float(j) * cell;
                auto& indices = ((i + j) % 2 != 0) ? dark : light;
                const auto first = int32_t(points.size());
                points.emplace_back(x0, y0, 0.0F);
                points.emplace_back(x0 + cell, y0, 0.0F);
                points.emplace_back(x0 + cell, y0 + cell, 0.0F);
                points.emplace_back(x0, y0 + cell, 0.0F);
                for (int32_t k = 0; k < 4; ++k) {
                    indices.push_back(first + k);
                }
                indices.push_back(SO_END_FACE_INDEX);
            }
        }
        auto* coords = new SoCoordinate3;
        coords->point.setValues(0, int(points.size()), points.data());
        root->addChild(coords);

        // Two greys, UNLIT. Its own materials, not the card's: it is the
        // thing behind the material, not the material -- a reference
        // pattern, whose greys are the display values they read as.
        // Emissive is the one slot the pipeline passes through unchanged
        // (decoded on the way in, encoded on the way out); a diffuse grey
        // is lit by the environment first, and on a plane facing the
        // camera that is about twice over, so 0.72 came back as 244.
        // Specular 0 rather than merely dark: with Render_PBRFromSpecular
        // it IS the reflectance, so 0 is no environment reflection at
        // all, where a black rough dielectric would still add its 4%.
        auto cells = [root](const std::vector<int32_t>& indices, float grey) {
            auto* sep = new SoSeparator;
            auto* material = new SoMaterial;
            material->diffuseColor.setValue(0, 0, 0);
            material->ambientColor.setValue(0, 0, 0);
            material->specularColor.setValue(0, 0, 0);
            material->emissiveColor.setValue(grey, grey, grey);
            material->shininess = 0.0F;
            auto* faces = new SoIndexedFaceSet;
            faces->coordIndex.setValues(0, int(indices.size()), indices.data());
            sep->addChild(material);
            sep->addChild(faces);
            root->addChild(sep);
        };
        cells(light, 0.72F);
        cells(dark, 0.34F);
        return root;
    }

    SoNode* buildCylinder()
    {
        auto* root = new SoSeparator;
        // STAND IT UP. SoCylinder turns about Y and the environment is
        // Z-up, so without this the billet lies on its side in the only
        // frame that matters for image based lighting: the top face's
        // normal comes out HORIZONTAL, and the reflection of a camera
        // looking 55 degrees down off a horizontal face goes 55 degrees
        // DOWN -- into the ground. Measured, the two faces were exactly
        // inverted: the wall reflected the sky and clipped, the top face
        // reflected the floor and went dead, which is both of the things
        // that looked wrong about these icons.
        //
        // The finish frames below stay as they are, stated about Y. They
        // are read in object space (v_opos is a_position), which this
        // transform is above.
        auto* upright = new SoTransform;
        upright->rotation.setValue(SbVec3f(1, 0, 0), float(M_PI) * 0.5F);
        root->addChild(upright);
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
        // Its X runs OBLIQUE, and at 22.5 degrees rather than 45.
        //
        // A machined face has no canonical X -- it is whatever direction
        // the tool swept -- but the lighting cares. Tilting a normal
        // sideways to the view only swings the reflection in azimuth,
        // and an environment graded in elevation does not change along
        // that swing, so a lay square to the view shades as though the
        // face were plain. That is why the angle is oblique at all:
        // laid along an axis, brushed and the straight knurl vanished
        // from the top face.
        //
        // 45 degrees fixed those two and broke the DIAMOND knurl, which
        // is the one pattern that is not one train but two, crossing at
        // a right angle. Laid over by 45 its trains land at 0 and 90 --
        // both of the bad angles at once -- and the top face came out
        // as one set of stripes with its other set invisible. 22.5 is
        // oblique for a single train AND for a crossed pair, which no
        // multiple of 45 can be.
        constexpr float LayX = 0.92387953F;   // cos 22.5
        constexpr float LayZ = 0.38268343F;   // sin 22.5
        setFrame(branch(root, top),
                 SbVec3f(0, CylinderHeight * 0.5F, 0), FramePlanar,
                 SbVec3f(0, 1, 0), 0.0F,
                 SbVec3f(LayX, 0, LayZ));
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
        // Inert at today's default (off), and pinned for the same reason
        // the sample count is: it is a preference, and what it does to a
        // picture is refine it over ~32 jittered frames while the view sits
        // idle. An icon is grabbed on the second frame of two, so a user
        // who has it on would be handed whichever partial accumulation that
        // frame happened to hold.
        set("Render_TemporalAccum", false);

        // Which environment and how much of it, stated here rather
        // than inherited. These pictures ship, so they have to be
        // reproducible on a machine whose preference says something
        // else -- and the preference is exactly the kind of thing a
        // user changes. The VALUES are per shape and set in setShape():
        // a sphere and a flat-topped billet do not want the same rig.
        _envPreset = static_cast<App::PropertyEnumeration*>(
            _settings.addDynamicProperty("App::PropertyEnumeration",
                                         "Render_PBREnvPreset", "Render",
                                         nullptr, App::Prop_NoPersist));
        // Must match View3DInventorViewer's list entry for entry: this
        // is a second copy of the same enumeration, and setValue() on an
        // index past its end does nothing at all -- silently, which is
        // how the billet went on being lit by Interior through four
        // rounds of tuning an environment it was never using.
        static const char* kPresets[] =
            {"Studio", "Gradient", "Overcast", "Sunset", "Interior",
             "Light tent", nullptr};
        _envPreset->setEnums(kPresets);
        _exposure = static_cast<App::PropertyFloat*>(
            _settings.addDynamicProperty("App::PropertyFloat",
                                         "Render_Exposure", "Render",
                                         nullptr, App::Prop_NoPersist));
    }

    App::PropertyContainer _settings;
    App::PropertyEnumeration* _envPreset {nullptr};
    App::PropertyFloat* _exposure {nullptr};
    /// Every appearance node in the scene: one for the sphere, one per
    /// face of the cylinder. They differ only in the frame they state.
    std::vector<Gui::SoFCRenderMaterial*> _render;
    /// The checkerboard behind the sphere, shown only for a material it
    /// can be seen through.
    SoSwitch* _backdrop {nullptr};
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
    // instance() is a function-local static, so this object is destroyed
    // by the exit handlers -- after main() has returned and with it
    // QApplication, the platform integration and the display connection.
    // _scene is a View3DInventorViewer, a QWidget owning a GL context,
    // and destroying one THEN takes QOpenGLContext::destroy() into a
    // GLX/Mesa stack that has already been torn down: a segfault at
    // every exit. Qt's rule is that no widget may outlive QApplication,
    // so the viewer goes now, while there is still an application to
    // release it against.
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this] {
            _quitting = true;
            _scene.reset();
        });
    }
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

QString MaterialIcons::digestOf(const App::MaterialAppearance& material,
                                const App::SurfaceFinish& finish,
                                const App::MaterialRenderProperties& render)
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
    // The render properties are part of the picture too, so that a glass
    // card whose IOR moved is recognised as stale and a bundled icon is
    // not served for an appearance it no longer shows.
    for (const auto& prop : render) {
        out << QString::fromStdString(prop.name) << prop.value;
    }
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

QIcon MaterialIcons::icon(const QString& key, const App::MaterialAppearance& material,
                          const QString& name,
                          const App::MaterialRenderProperties& render)
{
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    const QString digest = digestOf(material, material.finish, render);
    // Bundled with the module, or supplied by the user in place of what
    // is bundled. Ahead of everything else because it costs no render at
    // all, because it is the same icon on every installation, and
    // because it is the only icon there is where nothing can be drawn.
    QIcon bundled = fromResource(key, resourceName(name), digest);
    if (bundled.isNull()) {
        // Nothing under this card's own name, so try the one the cards
        // that look like it share. The per-card name is tried first and
        // stays the way a single card is overridden; this is how the
        // other hundred get a bundled icon without shipping a hundred
        // copies of the same picture.
        bundled = fromResource(key, sharedResourceName(digest), digest);
    }
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
    _queue.push_back({key, material, material.finish, render});
    _timer->start();
    return {};
}

App::MaterialAppearance MaterialIcons::finishMaterial(uint8_t pattern)
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
    App::MaterialAppearance metal;
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
            // this is still the coarsest of the lot -- but its period
            // along the arc is the pitch times root two, so it draws
            // half again as big as the number suggests, and at 0.65 it
            // put barely three diamonds across the visible wall, which
            // reads as a pattern of the billet rather than of the
            // surface.
            finish.pitch = 0.450F;
            finish.depth = 0.115F;
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
    const App::MaterialAppearance neutral = finishMaterial(finish.pattern);
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
    return build(key, neutral, finish, {}, IconShape::Cylinder);
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

QIcon MaterialIcons::patternIcon(const QString& key, const QString& materialName)
{
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }
    // No digest: a swatch is drawn from the hatch definition, not from an
    // App::MaterialAppearance, so there is nothing for the staleness guard to
    // compare and the file is taken as it stands.
    return fromResource(key, patternResourceName(materialName), QString());
}

QString MaterialIcons::patternResourceName(const QString& materialName)
{
    if (materialName.isEmpty()) {
        return {};
    }
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]+"));
    QString safe = QString(materialName).replace(unsafe, QStringLiteral("_"));
    // The bundle holds two cards whose names differ only in case -- the PAT
    // "Square" and the SVG "square" -- and the icon directory is checked out
    // on filesystems that do not tell those apart. There the plain name is
    // ONE file, so whichever swatch git wrote last is the one both cards
    // show, and the other icon is silently wrong. A name carrying any
    // uppercase takes a digest of itself, which case folding cannot
    // collapse. Must agree with resource_name() in
    // scripts/pattern-icons.py, which draws the files this looks up.
    if (materialName != materialName.toLower()) {
        safe += QStringLiteral("-")
            + QString::fromLatin1(
                QCryptographicHash::hash(materialName.toUtf8(), QCryptographicHash::Sha1)
                    .toHex()
                    .left(6));
    }
    return QStringLiteral("Pattern_") + safe;
}

QString MaterialIcons::sharedResourceName(const QString& digest)
{
    if (digest.isEmpty()) {
        return {};
    }
    // The digest is already hex, so it is a legal file name as it
    // stands. Distinct prefix so that the generator can recognise its
    // own leftovers, and so that a reader can tell at a glance which
    // icons are per-card overrides and which are shared looks.
    return QStringLiteral("Look_") + digest;
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

bool MaterialIcons::renderToFile(const App::MaterialAppearance& material,
                                 const App::SurfaceFinish& finish, const QString& path,
                                 const App::MaterialRenderProperties& props,
                                 IconShape shape)
{
    QImage image = render(material, finish, props, shape);
    if (image.isNull()) {
        return false;
    }
    image.setText(QString::fromLatin1(DigestKey), digestOf(material, finish, props));
    return image.save(path, "PNG");
}

void MaterialIcons::drain()
{
    for (int i = 0; i < RenderPerTick && !_queue.empty(); ++i) {
        Request req = _queue.front();
        _queue.pop_front();
        QIcon icon = build(req.key, req.material, req.finish, req.render);
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

QImage MaterialIcons::render(const App::MaterialAppearance& material,
                             const App::SurfaceFinish& finish,
                             const App::MaterialRenderProperties& props,
                             IconShape shape)
{
    if (_failed || _quitting) {
        return {};
    }
    try {
        if (!_scene) {
            _scene = std::make_unique<IconScene>();
        }
        _scene->setShape(shape == IconShape::Cylinder ? IconScene::Shape::Cylinder
                                                       : IconScene::Shape::Sphere);
        _scene->apply(material, finish, props);
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

QIcon MaterialIcons::build(const QString& key, const App::MaterialAppearance& material,
                           const App::SurfaceFinish& finish,
                           const App::MaterialRenderProperties& props,
                           IconShape shape)
{
    QImage img = render(material, finish, props, shape);
    if (img.isNull()) {
        return {};
    }
    const QString path = cachePath(digestOf(material, finish, props));
    img.save(path, "PNG");
    QIcon icon = fromImage(img);
    _cache[key] = icon;
    _paths[key] = path;
    return icon;
}

#include "moc_MaterialIcons.cpp"
