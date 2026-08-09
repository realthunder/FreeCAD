/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef GUI_RENDERER_BGFXRENDERERP_H
#define GUI_RENDERER_BGFXRENDERERP_H

/// Private header of the bgfx renderer: shared by the BGFXRenderer
/// translation units only (BGFXRenderer.cpp and the BGFXView*/
/// per-feature files). Not an API -- nothing outside
/// src/Gui/Renderer may include it.
#include "FCConfig.h"
#include "BGFXRenderer.h"
#include "SceneDump.h"
#include "MeshSource.h"
#include "SceneLadder.h"
#ifndef FC_RENDERER_STANDALONE
#include "SceneServer.h"
#endif

#ifndef FC_OS_WIN32
# ifndef GL_GLEXT_PROTOTYPES
#   define GL_GLEXT_PROTOTYPES 1
# endif
#endif

#ifndef _PreComp_
# include <float.h>
# ifdef FC_OS_WIN32
#  ifndef NOMINMAX
#   define NOMINMAX
#  endif
#  include <windows.h>
// windows.h still defines the 16-bit memory-model keywords near/far as empty
// macros, which silently eats the type in declarations like "float near = ...".
// It also defines a couple of names this file uses as identifiers.
#  undef near
#  undef far
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# endif
#endif

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <set>

#undef GL_GLEXT_VERSION
#ifdef __EMSCRIPTEN__
#include <cstdlib>
#include <emscripten/html5.h>
#endif
#ifdef FC_RENDERER_STANDALONE
#include "StandalonePlatform.h"
#else
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QWindow>
#include <QDebug>
#include <Base/Console.h>
#endif

// #if !defined(FC_OS_MACOSX)
// # include <GL/gl.h>
// # include <GL/glu.h>
// # include <GL/glext.h>
// #endif

#include <bgfx/bgfx.h>
#include <bx/timer.h>
#include <bx/math.h>

#ifndef FC_RENDERER_STANDALONE
#include <bgfx_utils.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#   include <QtGui/qopenglcontext_platform.h>
#elif defined FC_OS_LINUX
#   include <QtPlatformHeaders/QGLXNativeContext>
typedef QGLXNativeContext OpenGLContext;
#elif defined FC_OS_WIN
#  include <QtPlatformHeaders/QWGLNativeContext>
typedef QWGLNativeContext OpenGLContext;
#elif defined FC_OS_MACOSX
#  include <QtPlatformHeaders/QCocoaNativeContext>
typedef QCocoaNativeContext OpenGLContext;
#endif
#endif // !FC_RENDERER_STANDALONE
#undef KeyPress
#undef Status
#undef None

using namespace Render;
using namespace bgfx;

#define _RENDER_LOG(_lvl, _line, _msg) \
    _lvl() << "bgfx (" << _line << "): " << _msg

#define _RENDER_ERR(_line, _msg) _RENDER_LOG(qCritical, _line, _msg)
#define RENDER_ERR(_msg) _RENDER_ERR(__LINE__, _msg)
#define _RENDER_WARN(_line, _msg) _RENDER_LOG(qWarn, _line, _msg)
#define RENDER_WARN(_msg) _RENDER_ERR(__LINE__, _msg)


////////////////////////////////////////////////////////

namespace
{
    /// True when float32 textures cannot be linearly filtered on this GPU.
    /// On WebGL that needs OES_texture_float_linear, which many mobile GPUs
    /// lack -- there a linear-filtered float32 (RG32F) texture is *incomplete*
    /// and every sample reads back (0,0), so a float32 variance shadow map
    /// makes the whole scene read fully shadowed. bgfx misses this on WebGL2
    /// (its per-format linear detection is gated on the WebGL1-era
    /// OES_texture_float extension, which WebGL2 reports as core). Detected
    /// once from the live context; desktop GL (and any WebGL that advertises
    /// the extension, e.g. desktop-GPU browsers) filters float32 natively.
    /// Half-float (RG16F/RGBA16F) linear filtering is separate and far more
    /// widely available, so the shadow moments drop to RG16F here rather than
    /// point-sampling RG32F (which speckles the self-shadowed terminator).
    inline bool shadowFloat32NotFilterable()
    {
        static const bool need = [] {
#ifdef __EMSCRIPTEN__
            char *exts = emscripten_webgl_get_supported_extensions();
            bool has = exts
                && std::strstr(exts, "OES_texture_float_linear") != nullptr;
            if (exts)
                std::free(exts);
            return !has;
#else
            return false;  // desktop GL: float32 linear filtering is core
#endif
        }();
        return need;
    }

#ifndef FC_RENDERER_STANDALONE
    inline void freeFramebufferFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteFramebuffers(1, &id);
    }

    /*
    void freeRenderbufferFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteRenderbuffers(1, &id);
    }

    void freeTextureFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteTextures(1, &id);
    }
    */

    inline bool _checkGLError(int line, const char *msg)
    {
        GLenum error = glGetError();
        if (error == GL_NO_ERROR)
            return false;
        unsigned clamped = qMin(unsigned(error - GL_INVALID_ENUM), 4U);
        const char *errors[] = { "GL_INVALID_ENUM", "GL_INVALID_VALUE", "GL_INVALID_OPERATION", "Unknown" };
        _RENDER_ERR(line, msg << " (" << errors[clamped] << ")");
        return true;
    }
    #define checkGLError(msg) _checkGLError(__LINE__, msg)

    inline bool _checkFramebufferStatus(int line)
    {
        auto *f = QOpenGLContext::currentContext()->extraFunctions();
        GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        switch(status) {
        case GL_NO_ERROR:
        case GL_FRAMEBUFFER_COMPLETE:
            return true;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            _RENDER_ERR(line, "Unsupported framebuffer format.");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete attachment.");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete, missing attachment.");
            break;
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT
        case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete, duplicate attachment.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS
        case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
            _RENDER_ERR(line, "Framebuffer incomplete, attached images must have same dimensions.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_FORMATS
        case GL_FRAMEBUFFER_INCOMPLETE_FORMATS:
            _RENDER_ERR(line, "Framebuffer incomplete, attached images must have same format.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            _RENDER_ERR(line, "Framebuffer incomplete, missing draw buffer.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            _RENDER_ERR(line, "Framebuffer incomplete, missing read buffer.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            _RENDER_ERR(line, "Framebuffer incomplete, attachments must have same number of samples per pixel.");
            break;
#endif
        default:
            _RENDER_ERR(line, "An undefined error has occurred: " << status);
            break;
        }
        return false;
    }
    #define checkFramebufferStatus() _checkFramebufferStatus(__LINE__)
#endif // !FC_RENDERER_STANDALONE
}

////////////////////////////////////////////////////////

class BGFXView;

/// Active backend's shaderc target (defined below): the profile string
/// is the label a viewer matches snapshot-shipped variants against.
bool shadercTarget(std::string &platform, std::string &profile,
                          std::string &apiDir);

/// Stock shader pack loaders (BGFXRenderer.cpp). They replace
/// bgfx_utils' loadShader/loadProgram, which assert on a missing .bin
/// and then hand createShader a null memory block -- a truncated or
/// stale pack would take the process down instead of degrading. These
/// report the missing stage and return an invalid handle (and never
/// leak the sibling shader of a half-loaded pair, which
/// bgfx::createProgram's early-out does); init() turns an invalid core
/// program into a Coin fallback.
bgfx::ShaderHandle fcLoadShader(const char *name, const char *path);
bgfx::ProgramHandle fcLoadProgram(const char *vsName, const char *fsName,
                                  const char *path);

namespace Render {

/// One line a second summarising what the camera can resolve
/// (docs/FarFieldProxies.md §9). Rate-limited here rather than at the
/// call site so the caller stays a plain "measure this frame".
inline void reportCoverage(const CoverageHistogram &hist)
{
    static int64_t lastReport = 0;
    const int64_t now = bx::getHPCounter();
    const int64_t freq = bx::getHPFrequency();
    if (lastReport && now - lastReport < freq)
        return;
    lastReport = now;
    if (!hist.total)
        return;
    std::string line;
    char buf[128];
    const float *edges = CoverageHistogram::kEdges;
    for (int i = 0; i < CoverageHistogram::kBuckets; ++i) {
        if (i == 0)
            snprintf(buf, sizeof(buf), "<=%gpx:%d", double(edges[0]),
                     hist.counts[0]);
        else if (i == CoverageHistogram::kBuckets - 1)
            snprintf(buf, sizeof(buf), " >%gpx:%d",
                     double(edges[CoverageHistogram::kBuckets - 2]),
                     hist.counts[i]);
        else
            snprintf(buf, sizeof(buf), " <=%gpx:%d", double(edges[i]),
                     hist.counts[i]);
        line += buf;
    }
    // The headline is the share a far-field cut could aggregate: objects
    // the camera resolves at a handful of pixels, each still paying for a
    // whole object.
    const int tiny = hist.atOrUnder(4.0f);
    const int onScreen = hist.total - hist.offScreen - hist.noBounds;
    snprintf(buf, sizeof(buf),
             " | on-screen:%d offscreen:%d nobounds:%d | <=4px %.1f%% of on-screen",
             onScreen, hist.offScreen, hist.noBounds,
             onScreen > 0 ? 100.0 * double(tiny) / double(onScreen) : 0.0);
    // Shared code: the standalone/WASM tier has no App layer to log
    // through, but the desktop's line belongs in the report view and
    // the console capture like every other coverage diagnostic.
#ifdef FC_RENDERER_STANDALONE
    std::printf("render coverage: objects:%d %s%s\n", hist.total,
                line.c_str(), buf);
#else
    Base::Console().Message("render coverage: objects:%d %s%s\n", hist.total,
                            line.c_str(), buf);
#endif
}

/// A user shader is animated when its source references the engine
/// clock uniform u_fcTime — no declared flag anywhere, the reference
/// itself is the opt-in (docs/RenderDebug.md §6).
inline bool userShaderAnimated(const UserShader &shader)
{
    return shader.vertexSource.find("u_fcTime") != std::string::npos
        || shader.fragmentSource.find("u_fcTime") != std::string::npos;
}

/// Assemble a user volume-stage medium splice (docs/RenderEngine.md
/// §5.11): a variant of one of the volumetric fragment bodies
/// (fc_volume_fs.sh / fc_volume_ext_fs.sh / fc_refl_media_fs.sh) with
/// the user medium functions dispatched for their fire (emissive) and
/// cloud (scatter) slots. The prelude defines FC_USER_FIRE_<slot> /
/// FC_USER_SCATTER_<slot> (fc_volume.sh prototypes and dispatches on
/// them); each user source is appended after the body include — so it
/// sees every helper — with its contract functions (fcMediumField/
/// fcMediumRamp on the fire channel, fcMediumScatter on the cloud
/// channel) macro-renamed to the slot's dispatch targets and
/// FC_MEDIUM_SLOT set to its slot index. Returns a synthetic
/// UserShader (stock vs_fc_comp vertex stage, merged parameter list)
/// for the shared compile cache, or null when no slot carries a user
/// medium.
/// Collect the volume-stage user medium of every fire / cloud slot
/// from a draw list, replicating exactly the slot assignment of the
/// frame loop's fire and cloud/fountain body scans (dedup by object
/// key in draw order, bodies beyond the slot count share slot 0 and
/// carry no user medium). Shared between the frame loop and the
/// snapshot serializer so the assembled splice sources match
/// byte-for-byte across tiers — the viewer adopts shipped binaries by
/// source equality.
inline void
collectMediumUsers(const Render::DrawCallList &scene,
                   std::shared_ptr<const Render::UserShader> *fireUsers,
                   std::shared_ptr<const Render::UserShader> *cloudUsers,
                   int nslots)
{
    std::unordered_set<uint64_t> fireSeen, cloudSeen;
    int nfire = 0, ncloud = 0;
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if (mat.ontop || mat.type != Render::Material::Triangle)
            continue;
        auto user = (mat.usershader && mat.usershader->stage == "volume"
                     && !mat.usershader->fragmentSource.empty())
            ? mat.usershader : nullptr;
        if (mat.fire && fireSeen.insert(draw.objectKey).second
                && nfire < nslots)
            fireUsers[nfire++] = user;
        if ((mat.cloud || mat.fountain)
                && cloudSeen.insert(draw.objectKey).second
                && ncloud < nslots)
            cloudUsers[ncloud++] = user;
    }
}

inline std::shared_ptr<const Render::UserShader>
assembleMediumVariant(const char *body,
                      const std::shared_ptr<const Render::UserShader> *fireUsers,
                      const std::shared_ptr<const Render::UserShader> *scatterUsers,
                      int nslots)
{
    bool any = false;
    for (int i = 0; i < nslots; ++i)
        any = any || fireUsers[i] || scatterUsers[i];
    if (!any)
        return nullptr;
    auto res = std::make_shared<Render::UserShader>();
    res->stage = "volume-splice";
    std::string &s = res->fragmentSource;
    s = "$input v_texcoord0\n\n#include <bgfx_shader.sh>\n";
    for (int i = 0; i < nslots; ++i) {
        if (fireUsers[i])
            s += "#define FC_USER_FIRE_" + std::to_string(i) + "\n";
        if (scatterUsers[i])
            s += "#define FC_USER_SCATTER_" + std::to_string(i) + "\n";
    }
    s += "#include \"";
    s += body;
    s += "\"\n";
    auto mergeParams = [&res](const Render::UserShader &u) {
        for (const auto &p : u.params) {
            if (p.name == "fc_state")
                continue;
            res->params.push_back(p);
        }
    };
    for (int i = 0; i < nslots; ++i) {
        if (!fireUsers[i])
            continue;
        std::string n = std::to_string(i);
        s += "#define fcMediumField fcUserField_" + n + "\n";
        s += "#define fcMediumRamp fcUserRamp_" + n + "\n";
        s += "#define FC_MEDIUM_SLOT " + n + "\n";
        s += fireUsers[i]->fragmentSource;
        s += "\n#undef fcMediumField\n#undef fcMediumRamp\n"
             "#undef FC_MEDIUM_SLOT\n";
        mergeParams(*fireUsers[i]);
    }
    for (int i = 0; i < nslots; ++i) {
        if (!scatterUsers[i])
            continue;
        std::string n = std::to_string(i);
        s += "#define fcMediumScatter fcUserScatter_" + n + "\n";
        s += "#define FC_MEDIUM_SLOT " + n + "\n";
        s += scatterUsers[i]->fragmentSource;
        s += "\n#undef fcMediumScatter\n#undef FC_MEDIUM_SLOT\n";
        mergeParams(*scatterUsers[i]);
    }
    return res;
}

class BGFXRendererLibP {
public:
    BGFXRendererLibP() {
        for (auto &v : typeMap)
            types.push_back(v.first);
    }

    ~BGFXRendererLibP();

    BGFXView *getView(QOpenGLWidget *widget, RendererType::Enum type);

    void removeView(QOpenGLWidget *widget);

    void shutdown();

#ifdef FC_RENDERER_STANDALONE
    /// Standalone (no Qt): bgfx owns the native window/canvas handed in
    /// through setWindowHandle() — under Emscripten the "#canvas" CSS
    /// selector — and creates its own GL context on it.
    bool prepare(QOpenGLWidget *, RendererType::Enum type)
    {
        if (currentType == RendererType::Noop) {
            currentType = type;
            bgfx::Init init;
            init.type = type;
            init.platformData.nwh = windowHandle;
            init.resolution.width = standaloneWidth;
            init.resolution.height = standaloneHeight;
            // MAXANISOTROPY is required for BGFX_SAMPLER_*_ANISOTROPIC to have
            // any effect: bgfx only raises its internal m_maxAnisotropy (and
            // thus honors the per-sampler anisotropic flags) when this reset
            // bit is set — otherwise the flags are silently ignored.
            init.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY;
            if (!bgfx::init(init)) {
                currentType = RendererType::Noop;
                RENDER_ERR("init failed");
                return false;
            }
        }
        return true;
    }

    void makeCurrent() {}
    void doneCurrent() {}
    void freeFBO(int) {}
#else
    bool prepare(QOpenGLWidget *widget, RendererType::Enum type)
    {
        if (!context) {
            context.reset(new QOpenGLContext);
            auto format = widget->format();
            context->setShareContext(QOpenGLContext::globalShareContext());
            context->setFormat(format);
            if (!context->create()) {
                RENDER_ERR("failed to create a GL context for bgfx");
                context.reset();
                return false;
            }
            offscreen.reset(new QOffscreenSurface);
            offscreen->setFormat(format);
            offscreen->create();
            if (!offscreen->isValid()) {
                RENDER_ERR("failed to create the offscreen surface for bgfx");
                offscreen.reset();
                context.reset();
                return false;
            }
            if (!quitHooked && QCoreApplication::instance() && !getenv("FC_NO_BGFX_QUITHOOK")) {
                quitHooked = true;
                // Tear down bgfx and its Qt GL objects while Qt is still
                // alive. This static _BGFXLib is otherwise destroyed at
                // library unload, after QApplication is gone, where deleting
                // the QOpenGLContext/QOffscreenSurface crashes.
                QObject::connect(QCoreApplication::instance(),
                                 &QCoreApplication::aboutToQuit,
                                 [this]() {
                                     views.clear();
                                     granules.clear();
                                     shutdown();
                                 });
            }
        }

        if (currentType == RendererType::Noop) {
            currentType = type;

            bgfx::renderFrame();
            bgfx::Init init;
            init.type = currentType;

            if (currentType == RendererType::OpenGL) {
                makeCurrent();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#   if defined(FC_OS_LINUX)
                if (auto *glx = context->nativeInterface<QNativeInterface::QGLXContext>())
                    init.platformData.context = glx->nativeContext();
                else if (auto *egl = context->nativeInterface<QNativeInterface::QEGLContext>())
                    init.platformData.context = egl->nativeContext();
#   elif defined(FC_OS_WIN32)
                if (auto *wgl = context->nativeInterface<QNativeInterface::QWGLContext>())
                    init.platformData.context = wgl->nativeContext();
#   elif defined(FC_OS_MACOSX)
                if (auto *cocoa = context->nativeInterface<QNativeInterface::QCocoaGLContext>())
                    init.platformData.context = cocoa->nativeContext();
#   endif
#else
                init.platformData.context = qvariant_cast<OpenGLContext>(
                    context->nativeHandle()).context();
#endif
            } else {
                window = new QWindow();
                window->setObjectName(QStringLiteral("bgfxScreenSurface"));
                // d->offscreenWindow->setSurfaceType(QWindow::OpenGLSurface);
                // d->offscreenWindow->setFormat(d->requestedFormat);
                window->setGeometry(0, 0, widget->width(), widget->height());
                window->create();
#if BX_PLATFORM_OSX
                init.platformData.nwh = get_nswindow_from_nsview(reinterpret_cast<void*>(window->winId()));
#else
                init.platformData.nwh = reinterpret_cast<void*>(window->winId());
#endif
            }
            // bgfx treats an all-null PlatformData as a request for a headless device, and
            // then rejects a non-zero resolution ("resolution of non-existing backbuffer
            // can't be larger than 0x0") - which surfaces only as init() returning false.
            // In the OpenGL path platformData.context is the sole field we set, so if the
            // native handle did not come through, say which step failed rather than letting
            // bgfx report a headless-mode error that has nothing to do with the real cause.
            if (currentType == RendererType::OpenGL && !init.platformData.context) {
                currentType = RendererType::Noop;
                RENDER_ERR("no native GL context handle; bgfx would fall back to headless");
                return false;
            }
            init.resolution.width = widget->width();
            init.resolution.height = widget->height();
            // MAXANISOTROPY is required for BGFX_SAMPLER_*_ANISOTROPIC to have
            // any effect: bgfx only raises its internal m_maxAnisotropy (and
            // thus honors the per-sampler anisotropic flags) when this reset
            // bit is set — otherwise the flags are silently ignored.
            init.resolution.reset = BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY;
            if (!bgfx::init(init)) {
                widget->makeCurrent();
                RENDER_ERR("init failed");
                return false;
            }
        }
        return true;
    }

    void makeCurrent()
    {
        context->makeCurrent(offscreen.get());
        for (auto &v : pendingRemoves)
            v.second(context->functions(), v.first);
        pendingRemoves.clear();
    }

    void doneCurrent()
    {
        context->doneCurrent();
    }

    void freeFBO(GLint fbo)
    {
        pendingRemoves.emplace_back(fbo, freeFramebufferFunc);
    }
#endif // !FC_RENDERER_STANDALONE

    std::string resource()
    {
        return RendererFactory::resourcePath() + "bgfx/assets/";
    }

    /// Asset root the shader programs load from: FC_BGFX_SHADER_DIR
    /// (a directory containing shaders/{glsl,essl,spirv}/) overrides
    /// the installed resource tree, so a developer can point at the
    /// source tree's compile.sh output and hot-reload from there
    /// (docs/RenderDebug.md §3).
    std::string shaderPath()
    {
        static const std::string overridePath = [] {
            const char *dir = std::getenv("FC_BGFX_SHADER_DIR");
            std::string s = dir ? dir : "";
            if (!s.empty() && s.back() != '/')
                s += '/';
            return s;
        }();
        return overridePath.empty() ? resource() : overridePath;
    }

    /// Shader hot-reload generation (docs/RenderDebug.md §3): bumped by
    /// BGFXRenderer::reloadShaders(); a view whose captured generation
    /// lags re-inits at the top of its next render(), reloading every
    /// program from shaderPath().
    int shaderGeneration = 0;

    /// Dynamically bound named uniforms (docs/RenderDebug.md §2.5):
    /// name -> handle + vec4 count, resolved lazily. bgfx registers
    /// uniforms by name, refcounted, and resizes a same-name uniform
    /// upward on re-create, so growth is just another createUniform
    /// (the old reference is released to keep the count balanced).
    /// The handle MUST default to BGFX_INVALID_HANDLE — a
    /// value-initialized handle is idx 0, a live uniform, and
    /// destroying it underflows someone else's refcount.
    struct UserUniform {
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        uint16_t num = 0;
    };
    std::map<std::string, UserUniform> userUniforms;

    /// Animation clock for user shaders (u_fcTime): x = seconds on the
    /// shared effect clock (0 while RenderDebug freeze-frame holds), y =
    /// 1 while the clock advances, z/w reserved. Stamped per frame by
    /// the renderer, recorded with every consuming user draw.
    bgfx::UniformHandle timeUniform = BGFX_INVALID_HANDLE;
    float userTime[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    /// The current frame submitted a live time-referencing user draw
    /// (reset each frame, folded into the renderer's animatedFrame so
    /// the viewer keeps scheduling redraws while a user shader
    /// animates). Lives here because the draw-submitting view code has
    /// no path back to the renderer pimpl.
    bool userAnimatedDraw = false;

    void setUserUniform(const std::string &name, const float *data,
                        uint16_t num)
    {
        if (!num)
            return;
        auto &entry = userUniforms[name];
        if (!bgfx::isValid(entry.handle) || entry.num < num) {
            bgfx::UniformHandle h = bgfx::createUniform(
                name.c_str(), bgfx::UniformType::Vec4, num);
            if (bgfx::isValid(entry.handle))
                bgfx::destroy(entry.handle);
            entry.handle = h;
            entry.num = num;
        }
        if (bgfx::isValid(entry.handle))
            bgfx::setUniform(entry.handle, data, num);
    }

    /// Record a user shader's parameters for its consuming draw
    /// (docs/RenderDebug.md §6.4), zeroing every other dynamically
    /// bound uniform: uniform values persist backend-side between
    /// frames, so a parameter dropped from the list would otherwise
    /// keep feeding its stale value to a program that declares it.
    /// Zeroing is safe for the frame's other consumers — every user
    /// draw and the debug lane re-record their own parameters with
    /// their own submits — and engine uniforms have their own handles
    /// outside this map, so they are never touched.
    void pushUserParams(const Render::UserShader &shader)
    {
        if (!bgfx::isValid(timeUniform))
            timeUniform = bgfx::createUniform("u_fcTime",
                                              bgfx::UniformType::Vec4);
        bgfx::setUniform(timeUniform, userTime);
        std::vector<float> zeros;
        for (auto &v : userUniforms) {
            if (!bgfx::isValid(v.second.handle))
                continue;
            bool inParams = false;
            for (const auto &p : shader.params) {
                if (p.name == v.first) {
                    inParams = true;
                    break;
                }
            }
            if (inParams)
                continue;
            if (zeros.size() < v.second.num * 4u)
                zeros.resize(v.second.num * 4u, 0.0f);
            bgfx::setUniform(v.second.handle, zeros.data(), v.second.num);
        }
        for (const auto &p : shader.params)
            setUserUniform(p.name, p.values.data(),
                           uint16_t(p.values.size() / 4));
    }

    /// Runtime user-shader program cache (docs/RenderDebug.md §6.3).
    /// Desktop builds: user .sc source compiles through the host
    /// shaderc into a disk cache keyed on SHA1(source × target), and
    /// the linked programs cache here by source identity. Compilation
    /// is ASYNCHRONOUS: render() only consults the caches (a QProcess
    /// vfork inside the widget paint path both hitches the frame and
    /// corrupts Qt's repaint bookkeeping — learned the hard way),
    /// missing bins schedule a compile on the event loop, and a
    /// finished compile bumps userCompileGeneration so idle-skip
    /// clients re-render. A failed compile is remembered and reported
    /// once — the pass just stays off.
    /// Standalone/WASM builds have no host compiler: programs load
    /// from the profile-matched precompiled variants the snapshot
    /// carries (UserShader::compiled, server-side compile), cached
    /// here keyed on the binary payloads.
    struct UserProgram {
        bgfx::ProgramHandle prog = BGFX_INVALID_HANDLE;
        bool failed = false;
    };
    std::map<std::string, UserProgram> userPrograms;
    /// Resolve a user program: user fragment stage + either a user
    /// vertex stage or the named stock vertex stage ("vs_fc_comp" for
    /// the post stage's full-screen triangle, "vs_fc_mesh" for the
    /// material stage). Invalid while a compile is pending (desktop)
    /// or while no shipped binary matches the active backend
    /// (standalone); entry.failed on real failure.
    /// simulate = resolve the shader's particle state step
    /// (UserShader::simulateSource) instead of its beauty fragment
    /// stage, always paired with the stock full-screen vertex shader.
    /// Same cache, same async compile, same viewer-tier binary lookup.
    bgfx::ProgramHandle getUserProgram(const Render::UserShader &shader,
                                       const char *stockVs,
                                       bool simulate = false);
#ifndef FC_RENDERER_STANDALONE
    /// Per-shader compile bookkeeping, keyed by SHA1(source×target×type).
    std::set<std::string> userShaderInflight;
    std::set<std::string> userShaderFailed;
    // include-tree content hash per source dir, part of the compile
    // cache key (shipped-helper edits must miss stale cached bins)
    std::map<std::string, QByteArray> userShaderSrcFingerprints;
    /// Bumped when an async compile finishes (either way); mirrored into
    /// each renderer's dirty state so the next frame retries the lookup
    /// (and the scene server republishes with the fresh bins).
    int userCompileGeneration = 0;

    /// Disk-cache / async-compile step for one user shader. The default
    /// target is the active bgfx backend; \a platform / \a profile
    /// override it for cross-compiles (the viewer tiers).
    /// Returns 0 with \a binPath set when the bin is ready, 1 while a
    /// compile is in flight, 2 when compilation failed (reported once).
    int ensureUserShaderBin(const std::string &source, bool fragment,
                            QString &binPath,
                            const char *platform = nullptr,
                            const char *profile = nullptr);
    /// Server-side compile for the viewer tiers (docs/RenderDebug.md
    /// §6.3): compile \a shader for each viewer target through the
    /// async disk cache and append every READY variant to \a out.
    /// Pending compiles republish on the next userCompileGeneration
    /// bump; failed ones are dropped (reported once by the compile).
    void viewerShaderBins(const Render::UserShader &shader,
                          std::vector<Render::UserShader::Compiled> &out);
#endif

#ifdef FC_RENDERER_STANDALONE
    /// Native window handle bgfx initializes on (Emscripten: the canvas
    /// CSS selector) and the current output size, fed by the host app
    /// through BGFXRenderer::setWindowHandle()/setWindowSize().
    void *windowHandle = nullptr;
    uint16_t standaloneWidth = 1024;
    uint16_t standaloneHeight = 768;
    // Scene render-target sample count (BGFXRenderer::setMSAASamples);
    // a change re-creates the view targets on the next render().
    int standaloneSamples = 4;
#else
    // Scene render-target sample count override (BGFXRenderer::setMSAASamples,
    // driven by the AntiAliasing preference). -1 = follow the host GL widget's
    // format; >= 0 overrides it. A change re-creates the view targets at the
    // top of the next render() — bgfx renders into its own offscreen FBO and
    // resolves before the blit, so the Qt context need not be multisampled.
    int desktopSamples = -1;
    // Offscreen capture size (BGFXRenderer::renderOffscreen): while
    // non-zero the desktop view renders at this size instead of the
    // host widget's. 0 = follow the widget, which is every on-screen
    // frame -- so the frame after a capture sees the mismatch and
    // sizes the view back on its own.
    uint16_t captureWidth = 0;
    uint16_t captureHeight = 0;
    /// The size a desktop view renders at: the host widget's, unless an
    /// offscreen capture is asking for its own.
    int viewWidth(QOpenGLWidget *widget) const
    { return captureWidth ? int(captureWidth) : widget->width(); }
    int viewHeight(QOpenGLWidget *widget) const
    { return captureHeight ? int(captureHeight) : widget->height(); }
    typedef void (*FreeResourceFunc)(QOpenGLFunctions *functions, GLuint id);
    std::vector<std::pair<GLuint, FreeResourceFunc>> pendingRemoves;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> offscreen;
#endif
    // Resolution scale of the expensive screen-space effect passes
    // (Render_EffectResolution; BGFXRenderer::setEffectResolution). A change
    // re-creates the view's scaled targets at the top of the next render().
    float effectResolution = 1.0f;
    // Resolution scale of the SSAO resolve targets (Render_SSAOResolution;
    // BGFXRenderer::setSSAOResolution) -- independent of effectResolution so
    // ambient occlusion stays sharp while the reflection re-render can scale.
    float ssaoResolution = 1.0f;
    std::unordered_map<QOpenGLWidget *, std::unique_ptr<BGFXView>> views;
    // View-id pool. A viewer holds a contiguous block sized to the
    // passes its frames actually use (BGFXView's pass map), which is
    // well under NUM_VIEWS for an ordinary scene, so the ids bgfx offers
    // stretch much further than one block per viewer would allow.
    //
    // Blocks are handed out in granules so that a scene picking up one
    // more pass -- a second overlay, a bulb that starts casting -- does
    // not repack the pool every time. A block only ever grows: it is the
    // high-water mark of what the viewer has needed, which keeps the
    // ids of a viewer that alternates between two configurations still.
    static const uint16_t kIdGranule = 8;
    std::vector<uint8_t> granules;   ///< 1 = taken
    // Whether the "out of bgfx view ids" refusal has already been
    // reported. A viewer asks once per frame, so without this the
    // message would repeat for as long as the extra viewer is open.
    // Cleared whenever a block is returned, so the next viewer that
    // does not fit says so again.
    bool warnedViewBudget = false;

    /// Ids in the pool (0 until the first reservation sizes it).
    uint16_t poolSize() const
    {
        return uint16_t(granules.size() * kIdGranule);
    }
    /// Free the view's block.
    void releaseBlock(BGFXView *view);
    /// Make sure the view's block holds at least `need` ids, moving it
    /// if it has to grow and cannot grow in place. False means the pool
    /// is full -- the caller falls back to Coin for this frame rather
    /// than submitting ids bgfx would abort on.
    bool reserveBlock(BGFXView *view, uint16_t need);

    std::map<std::string, RendererType::Enum> typeMap = {
#ifdef FC_RENDERER_STANDALONE
        // The standalone build renders through GLES — under Emscripten
        // that is WebGL2 on the canvas.
        {"bgfx - OpenGL", RendererType::OpenGLES},
#else
        {"bgfx - OpenGL", RendererType::OpenGL},
#endif
        // {"bgfx - Vulkan", RendererType::Vulkan},
#ifdef FC_OS_WIN32
        // {"bgfx - Direct3D9", RendererType::Direct3D9},
        // {"bgfx - Direct3D11", RendererType::Direct3D11},
        // {"bgfx - Direct3D12", RendererType::Direct3D12},
#elif defined(FC_OS_MACOSX)
        // {"bgfx - Metal", RendererType::Metal},
#endif
    };
    std::vector<std::string> types;
    RendererType::Enum currentType = RendererType::Noop;
    std::string name = "bgfx";
    std::set<BGFXRenderer::Private *> renderers;
#ifndef FC_RENDERER_STANDALONE
    QWindow *window = nullptr;
    bool quitHooked = false;
#endif
};

// Defined in BGFXRenderer.cpp; shared by every renderer TU.
extern BGFXRendererLibP _BGFXLib;
extern BGFXRendererLib BGFXLib;

} // namespace Renderer

// Geometry vertex stream built from the separate MeshData attribute
// arrays: position + normal (zero normal when the cache has none).
// Per-vertex colors ride their own stream (ColorVertex) so caches that
// differ only in baked colors — the PartGui color variants and the
// render cache's recolored copies — share one geometry buffer.
struct SceneVertex
{
    float px, py, pz;
    float nx, ny, nz;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Second vertex stream of the mesh/flat programs: per-vertex rgba8
// color. Meshes without baked colors bind a shared all-white buffer
// (grown to the largest vertex count seen) instead of carrying one each.
struct ColorVertex
{
    uint32_t rgba;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Interleaved position + normal + rgba8 vertex of the TRANSIENT draws
// (background gradient, shadow ground quad): one-off buffers where a
// separate color stream would buy nothing. The mesh programs source
// their attributes from this single stream.
struct TransientVertex
{
    float px, py, pz;
    float nx, ny, nz;
    uint32_t rgba;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Position-only stream of the particle impact splat: one vertex per
// particle whose x is that particle's index (docs/RenderEngine.md
// §5.8). Everything the point draws with is fetched from the state
// grid, so the buffer carries no payload of its own.
struct PointVertex
{
    float px, py, pz;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Texture-coordinate vertex stream of textured meshes: 2D texture
// coordinates, bound only by the textured mesh programs (texcoords live
// outside SceneVertex so untextured scenes don't pay for them).
struct TexCoordVertex
{
    float u, v;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Corner of the unit quad that vs_fc_line expands into a screen-space
// thick line segment: x = end of the segment (0/1), y = side (-1/+1).
struct LineQuadVertex
{
    float x, y, z;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();
        // Per-segment instance data consumed as i_data0-3: endpoint A,
        // endpoint B, color at A, color at B. Only the stride matters for
        // instance buffers; the attributes just shape the layout.
        ms_instLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .end();
        // Per-point instance data consumed as i_data0-1: position, color
        // (the point sprite path).
        ms_pointInstLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bgfx::VertexLayout ms_instLayout;
    static bgfx::VertexLayout ms_pointInstLayout;
    static bool ms_initialized;
};


// Section-cap quad vertex: a world-space position on the clip plane plus
// hatch texture coordinates (vs_fc_cap).
struct CapVertex
{
    float px, py, pz;
    float u, v;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Content identity of a mesh's geometry: every attribute and index array
// EXCEPT the per-vertex colors (they ride their own stream). Two caches
// with the same key — e.g. the color variants of one shared tessellation,
// or the render cache's recolored/reindexed copies — share one set of
// geometry buffers. The hash is FNV-1a over the raw array bytes,
// computed once per cache id.
struct GeomKey
{
    uint64_t hash = 0;
    int numVertices = 0;
    int numTri = 0;
    int numLine = 0;
    int numPoint = 0;
    uint8_t hasNormals = 0;
    uint8_t hasTexCoords = 0;

    bool operator==(const GeomKey &o) const
    {
        return hash == o.hash && numVertices == o.numVertices
            && numTri == o.numTri && numLine == o.numLine
            && numPoint == o.numPoint && hasNormals == o.hasNormals
            && hasTexCoords == o.hasTexCoords;
    }
};

struct GeomKeyHasher
{
    size_t operator()(const GeomKey &k) const { return size_t(k.hash); }
};

inline uint64_t fnv1a64(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline GeomKey computeGeomKey(const Render::MeshData &mesh)
{
    GeomKey key;
    key.numVertices = mesh.numVertices;
    key.numTri = mesh.numTriangleIndices;
    key.numLine = mesh.numLineIndices;
    key.numPoint = mesh.numPointIndices;
    key.hasNormals = mesh.normals ? 1 : 0;
    key.hasTexCoords = mesh.texCoords ? 1 : 0;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t nv = size_t(mesh.numVertices);
    h = fnv1a64(h, mesh.positions, nv * 12);
    if (mesh.normals)
        h = fnv1a64(h, mesh.normals, nv * 12);
    if (mesh.texCoords)
        h = fnv1a64(h, mesh.texCoords, nv * 16);
    if (mesh.numTriangleIndices > 0)
        h = fnv1a64(h, mesh.triangleIndices,
                    size_t(mesh.numTriangleIndices) * 4);
    if (mesh.numLineIndices > 0)
        h = fnv1a64(h, mesh.lineIndices, size_t(mesh.numLineIndices) * 4);
    if (mesh.numPointIndices > 0)
        h = fnv1a64(h, mesh.pointIndices,
                    size_t(mesh.numPointIndices) * 4);
    key.hash = h;
    return key;
}

// Colorless GPU buffers of one geometry (GeomKey): shared by every cache
// whose arrays match. Buffers are immutable; entries are dropped when no
// referencing mesh used them recently.
/// Geometry bytes currently uploaded through GpuGeometry/GpuMesh — the
/// GPU-budget estimate where the backend API reports no memory stats
/// (GL does not). Buffer sizes are added at creation and taken back in
/// destroy(); render targets, shadow maps and textures are outside it,
/// deliberately: the budget steers *geometry* residency (§13 step 3),
/// and the fixed passes cost what they cost.
inline std::atomic<size_t> s_gpuGeometryBytes {0};

struct GpuGeometry
{
    /// Position + normal vertex stream.
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle tri = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle line = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle point = BGFX_INVALID_HANDLE;
    /// Seam-filtered line index set (hidden-line hideSeam); only created
    /// when the mesh carries a no-seam index set.
    bgfx::IndexBufferHandle lineNoSeam = BGFX_INVALID_HANDLE;
    /// Triangle-edge segment / corner instance data feeding the stencil
    /// outline passes (constant white colors), built lazily on first
    /// outline use. Instance i maps 1:1 onto triangle index position i
    /// (the edge leaving that corner), so partial index ranges translate
    /// directly into instance ranges.
    bgfx::VertexBufferHandle triEdgeInst = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle triCornerInst = BGFX_INVALID_HANDLE;
    /// Texture-coordinate stream of textured draws, built lazily on
    /// first textured use.
    bgfx::VertexBufferHandle texcoord = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// Bytes this entry has uploaded (s_gpuGeometryBytes share).
    size_t bytes = 0;

    void track(size_t add)
    {
        bytes += add;
        s_gpuGeometryBytes += add;
    }

    void destroy()
    {
        for (auto ib : {&tri, &line, &point, &lineNoSeam}) {
            if (bgfx::isValid(*ib)) {
                bgfx::destroy(*ib);
                *ib = BGFX_INVALID_HANDLE;
            }
        }
        for (auto vb : {&vbh, &triEdgeInst, &triCornerInst, &texcoord}) {
            if (bgfx::isValid(*vb)) {
                bgfx::destroy(*vb);
                *vb = BGFX_INVALID_HANDLE;
            }
        }
        s_gpuGeometryBytes -= bytes;
        bytes = 0;
    }

    void upload(const Render::MeshData &mesh)
    {
        SceneVertex::init();
        const bgfx::Memory *vmem =
            bgfx::alloc(uint32_t(mesh.numVertices) * sizeof(SceneVertex));
        auto *verts = reinterpret_cast<SceneVertex *>(vmem->data);
        for (int i = 0; i < mesh.numVertices; ++i) {
            SceneVertex &v = verts[i];
            v.px = mesh.positions[i*3];
            v.py = mesh.positions[i*3 + 1];
            v.pz = mesh.positions[i*3 + 2];
            if (mesh.normals) {
                v.nx = mesh.normals[i*3];
                v.ny = mesh.normals[i*3 + 1];
                v.nz = mesh.normals[i*3 + 2];
            } else {
                v.nx = v.ny = 0.0f;
                v.nz = 1.0f;
            }
        }
        vbh = bgfx::createVertexBuffer(vmem, SceneVertex::ms_layout);
        track(vmem->size);

        if (mesh.numTriangleIndices > 0) {
            tri = bgfx::createIndexBuffer(
                bgfx::copy(mesh.triangleIndices, mesh.numTriangleIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numTriangleIndices) * 4);
        }
        if (mesh.numLineIndices > 0) {
            line = bgfx::createIndexBuffer(
                bgfx::copy(mesh.lineIndices, mesh.numLineIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numLineIndices) * 4);
        }
        if (mesh.numPointIndices > 0) {
            point = bgfx::createIndexBuffer(
                bgfx::copy(mesh.pointIndices, mesh.numPointIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numPointIndices) * 4);
        }
    }

    void ensureNoSeam(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(lineNoSeam) || mesh.numNoSeamLineIndices <= 1)
            return;
        lineNoSeam = bgfx::createIndexBuffer(
            bgfx::copy(mesh.noSeamLineIndices,
                       mesh.numNoSeamLineIndices * 4),
            BGFX_BUFFER_INDEX32);
        track(size_t(mesh.numNoSeamLineIndices) * 4);
    }

    /// Texture-coordinate stream (Coin xyzw texcoords collapsed to 2D,
    /// projective coordinates divided per vertex), built on first
    /// textured use of the mesh; bound only by the textured programs so
    /// untextured scenes don't pay for it.
    void ensureTexCoord(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(texcoord) || !mesh.texCoords
                || mesh.numVertices == 0)
            return;
        TexCoordVertex::init();
        const bgfx::Memory *tmem = bgfx::alloc(
            uint32_t(mesh.numVertices) * sizeof(TexCoordVertex));
        auto *tc = reinterpret_cast<TexCoordVertex *>(tmem->data);
        for (int i = 0; i < mesh.numVertices; ++i) {
            const float *t = mesh.texCoords + i*4;
            float q = t[3] != 0.0f ? t[3] : 1.0f;
            tc[i].u = t[0] / q;
            tc[i].v = t[1] / q;
        }
        texcoord = bgfx::createVertexBuffer(tmem,
                                            TexCoordVertex::ms_layout);
        track(tmem->size);
    }

    /// Triangle-edge segment + corner instance buffers for the stencil
    /// outline passes, one instance per triangle index position. Built on
    /// first use: whole-scene hidden-line outlines would otherwise
    /// re-upload mesh-sized transient buffers every frame.
    void ensureOutline(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(triEdgeInst)
                || mesh.numTriangleIndices < 3
                || !(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING))
            return;
        LineQuadVertex::init();
        int n = mesh.numTriangleIndices;

        const bgfx::Memory *emem =
            bgfx::alloc(uint32_t(n) * 16 * sizeof(float));
        float *d = reinterpret_cast<float *>(emem->data);
        for (int i = 0; i < n; ++i, d += 16) {
            int t = i / 3, e = i % 3;
            int32_t ia = mesh.triangleIndices[i];
            int32_t ib = mesh.triangleIndices[t*3 + (e + 1) % 3];
            d[0] = mesh.positions[ia*3];
            d[1] = mesh.positions[ia*3 + 1];
            d[2] = mesh.positions[ia*3 + 2];
            // No stipple run: a triangle edge stands alone, and these
            // are never drawn patterned anyway (a zero length tells
            // the shader to count from A).
            d[3] = 0.0f;
            d[4] = mesh.positions[ib*3];
            d[5] = mesh.positions[ib*3 + 1];
            d[6] = mesh.positions[ib*3 + 2];
            d[7] = 0.0f;
            for (int c = 8; c < 16; ++c)
                d[c] = 1.0f;
        }
        triEdgeInst = bgfx::createVertexBuffer(
            emem, LineQuadVertex::ms_instLayout);
        track(emem->size);

        const bgfx::Memory *cmem =
            bgfx::alloc(uint32_t(n) * 8 * sizeof(float));
        d = reinterpret_cast<float *>(cmem->data);
        for (int i = 0; i < n; ++i, d += 8) {
            int32_t ip = mesh.triangleIndices[i];
            d[0] = mesh.positions[ip*3];
            d[1] = mesh.positions[ip*3 + 1];
            d[2] = mesh.positions[ip*3 + 2];
            d[3] = 0.0f;
            for (int c = 4; c < 8; ++c)
                d[c] = 1.0f;
        }
        triCornerInst = bgfx::createVertexBuffer(
            cmem, LineQuadVertex::ms_pointInstLayout);
        track(cmem->size);
    }
};

// Per-cache GPU buffers of one MeshData, keyed by MeshData::cacheId: the
// baked per-vertex color stream and the color-carrying line/point
// instance data, plus a reference to the shared colorless geometry. A
// cache id always refers to identical content, so buffers are immutable
// and reused until the id disappears from the scene.
struct GpuMesh
{
    /// Shared geometry buffers (owned by the geometry table; guaranteed
    /// to outlive this entry — see collectMeshes).
    GpuGeometry *geom = nullptr;
    /// Per-vertex rgba8 color stream; invalid when the cache carries no
    /// baked colors (the shared white buffer is bound instead).
    bgfx::VertexBufferHandle color = BGFX_INVALID_HANDLE;
    /// Per-segment instance data (endpoints + colors) feeding the
    /// quad-expanded thick line path; invalid without instancing support.
    bgfx::VertexBufferHandle lineInst = BGFX_INVALID_HANDLE;
    /// Per-point instance data (position + color) feeding the point
    /// sprite path; invalid without instancing support.
    bgfx::VertexBufferHandle pointInst = BGFX_INVALID_HANDLE;
    /// Seam-filtered variant of lineInst (hidden-line hideSeam).
    bgfx::VertexBufferHandle lineNoSeamInst = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// MeshData::generation at upload — the arrays this entry was
    /// built from. A ladder refines its mesh in place under one
    /// cacheId, so the id alone no longer proves the upload current.
    uint32_t generation = 0;
    /// Bytes this entry has uploaded (s_gpuGeometryBytes share).
    size_t bytes = 0;

    void track(size_t add)
    {
        bytes += add;
        s_gpuGeometryBytes += add;
    }

    void destroy()
    {
        geom = nullptr;
        for (auto vb : {&color, &lineInst, &pointInst, &lineNoSeamInst}) {
            if (bgfx::isValid(*vb)) {
                bgfx::destroy(*vb);
                *vb = BGFX_INVALID_HANDLE;
            }
        }
        s_gpuGeometryBytes -= bytes;
        bytes = 0;
    }

    void upload(const Render::MeshData &mesh)
    {
        if (mesh.colors) {
            ColorVertex::init();
            color = bgfx::createVertexBuffer(
                bgfx::copy(mesh.colors, uint32_t(mesh.numVertices) * 4),
                ColorVertex::ms_layout);
            track(size_t(mesh.numVertices) * 4);
        }

        if (mesh.numLineIndices > 1
                && (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)) {
            lineInst = makeSegmentInstances(
                mesh, mesh.lineIndices, mesh.numLineIndices);
            track(size_t(mesh.numLineIndices / 2) * 16 * sizeof(float));
        }

        if (mesh.numPointIndices > 0
                && (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)) {
            LineQuadVertex::init();
            const bgfx::Memory *imem = bgfx::alloc(
                uint32_t(mesh.numPointIndices) * 8 * sizeof(float));
            float *d = reinterpret_cast<float *>(imem->data);
            for (int i = 0; i < mesh.numPointIndices; ++i, d += 8) {
                int32_t ip = mesh.pointIndices[i];
                d[0] = mesh.positions[ip*3];
                d[1] = mesh.positions[ip*3 + 1];
                d[2] = mesh.positions[ip*3 + 2];
                d[3] = 0.0f;
                if (mesh.colors) {
                    for (int c = 0; c < 4; ++c)
                        d[4 + c] = mesh.colors[ip*4 + c] / 255.0f;
                } else {
                    for (int c = 0; c < 4; ++c)
                        d[4 + c] = 1.0f;
                }
            }
            pointInst = bgfx::createVertexBuffer(
                imem, LineQuadVertex::ms_pointInstLayout);
            track(imem->size);
        }
    }

    /// One quad-expansion instance per line segment (endpoints + endpoint
    /// colors), from any GL_LINES style index set of the mesh.
    static bgfx::VertexBufferHandle makeSegmentInstances(
            const Render::MeshData &mesh, const int32_t *indices, int num)
    {
        LineQuadVertex::init();
        int nseg = num / 2;
        const bgfx::Memory *imem =
            bgfx::alloc(uint32_t(nseg) * 16 * sizeof(float));
        float *d = reinterpret_cast<float *>(imem->data);
        // Stipple phase, in the endpoints' spare w slots: how far along
        // its polyline this segment starts (model units) and how long
        // it is. The shader turns them into the pixel distance the
        // pattern counts. Without it the count restarts at every
        // segment and a dashed curve — which is many segments shorter
        // than one dash — draws solid.
        double run = 0.0;
        int32_t prevB = -1;
        for (int s = 0; s < nseg; ++s, d += 16) {
            int32_t ia = indices[s*2];
            int32_t ib = indices[s*2 + 1];
            // A polyline is what the feed flattened into pairs: as
            // long as a segment starts where the last one ended, the
            // run continues.
            if (ia != prevB)
                run = 0.0;
            prevB = ib;
            const float dx = mesh.positions[ib*3] - mesh.positions[ia*3];
            const float dy = mesh.positions[ib*3 + 1]
                - mesh.positions[ia*3 + 1];
            const float dz = mesh.positions[ib*3 + 2]
                - mesh.positions[ia*3 + 2];
            const float seglen = std::sqrt(dx*dx + dy*dy + dz*dz);
            d[0] = mesh.positions[ia*3];
            d[1] = mesh.positions[ia*3 + 1];
            d[2] = mesh.positions[ia*3 + 2];
            d[3] = float(run);
            d[4] = mesh.positions[ib*3];
            d[5] = mesh.positions[ib*3 + 1];
            d[6] = mesh.positions[ib*3 + 2];
            d[7] = seglen;
            run += seglen;
            if (mesh.colors) {
                for (int i = 0; i < 4; ++i) {
                    d[8 + i] = mesh.colors[ia*4 + i] / 255.0f;
                    d[12 + i] = mesh.colors[ib*4 + i] / 255.0f;
                }
            } else {
                for (int i = 0; i < 8; ++i)
                    d[8 + i] = 1.0f;
            }
        }
        return bgfx::createVertexBuffer(imem, LineQuadVertex::ms_instLayout);
    }

    /// Seam-filtered line buffers (hidden-line hideSeam), built on first
    /// use: a mesh may be uploaded before the hidden-line feed arrives
    /// (GpuMesh is keyed by cacheId), so upload() cannot see this data.
    /// The index set lands on the shared geometry, the color-carrying
    /// segment instances on this entry.
    void ensureNoSeam(const Render::MeshData &mesh)
    {
        geom->ensureNoSeam(mesh);
        if (bgfx::isValid(lineNoSeamInst)
                || mesh.numNoSeamLineIndices <= 1)
            return;
        if (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) {
            lineNoSeamInst = makeSegmentInstances(
                mesh, mesh.noSeamLineIndices, mesh.numNoSeamLineIndices);
            track(size_t(mesh.numNoSeamLineIndices / 2) * 16
                  * sizeof(float));
        }
    }
};

// GPU texture of one Render::TextureImage, keyed by
// TextureImage::textureId (a texture id always refers to identical
// content, so textures are immutable and reused until unreferenced).
struct GpuTexture
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// This handle is the stand-in for a texture whose pixels had not
    /// arrived, and must be replaced by the real one when they do.
    bool placeholder = false;

    void destroy()
    {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    void upload(const Render::TextureImage &tex)
    {
        // Expand to RGBA8 (1/2 components are luminance(+alpha), the GL
        // fixed-function texel expansion). Rows are uploaded in the
        // GL bottom-up order the Coin image comes in, matching the GL
        // renderer on the OpenGL backend (other backends may see the
        // image v-flipped, a known deviation until needed).
        const size_t n = size_t(tex.width) * tex.height;
        // A streamed texture arrives as its header first and its pixels
        // later (Renderer.h), so a draw can reach here naming an image
        // that has a size but no bytes — and the expansion below would
        // read every one of them off the end of an empty vector. White
        // is the answer the deferral already promises: a modulating
        // draw renders as if untextured until the payload lands.
        if (tex.pixels.size() < n * size_t(tex.numComponents)) {
            const uint8_t white[4] = {255, 255, 255, 255};
            handle = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                bgfx::copy(white, sizeof(white)));
            placeholder = true;
            return;
        }
        placeholder = false;
        std::vector<std::vector<uint8_t>> levels;
        levels.emplace_back(n * 4);
        {
            const uint8_t *src = tex.pixels.data();
            uint8_t *dst = levels.back().data();
            for (size_t i = 0; i < n; ++i) {
                const uint8_t *p = src + i * tex.numComponents;
                switch (tex.numComponents) {
                case 1: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = 255;
                        break;
                case 2: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = p[1];
                        break;
                case 3: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                        dst[3] = 255; break;
                default: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                         dst[3] = p[3]; break;
                }
                dst += 4;
            }
        }
        // Full mip chain, CPU 2x2 box filter (bgfx has no runtime mip
        // generation): closes the minification-speckle gap of the
        // texture rows — the GL renderer's fixed-function path stays
        // non-mipped, a known comparison-tolerance difference under
        // strong minification.
        int lw = tex.width, lh = tex.height;
        size_t total = levels.back().size();
        while (lw > 1 || lh > 1) {
            int nw = std::max(1, lw >> 1), nh = std::max(1, lh >> 1);
            const uint8_t *sp = levels.back().data();
            std::vector<uint8_t> lvl(size_t(nw) * nh * 4);
            for (int y = 0; y < nh; ++y) {
                int y0 = std::min(2 * y, lh - 1);
                int y1 = std::min(2 * y + 1, lh - 1);
                for (int x = 0; x < nw; ++x) {
                    int x0 = std::min(2 * x, lw - 1);
                    int x1 = std::min(2 * x + 1, lw - 1);
                    for (int c = 0; c < 4; ++c) {
                        int s = sp[(size_t(y0) * lw + x0) * 4 + c]
                            + sp[(size_t(y0) * lw + x1) * 4 + c]
                            + sp[(size_t(y1) * lw + x0) * 4 + c]
                            + sp[(size_t(y1) * lw + x1) * 4 + c];
                        lvl[(size_t(y) * nw + x) * 4 + c] =
                            uint8_t((s + 2) / 4);
                    }
                }
            }
            total += lvl.size();
            levels.push_back(std::move(lvl));
            lw = nw;
            lh = nh;
        }
        const bgfx::Memory *mem = bgfx::alloc(uint32_t(total));
        {
            uint8_t *dst = mem->data;
            for (const auto &lvl : levels) {
                std::memcpy(dst, lvl.data(), lvl.size());
                dst += lvl.size();
            }
        }
        // Anisotropic min/mag filtering: without it, textures viewed at a
        // grazing angle (the NaviCube face labels are the visible case) get
        // isotropically over-blurred along the foreshortened axis. The desktop
        // path sets setMaximumAnisotropy(4.0) for the same reason; match it
        // here. Harmless where the hardware lacks it (bgfx checks caps; WebGL
        // gates on EXT_texture_filter_anisotropic) and it also sharpens the
        // head-on minified case by taking higher-mip taps.
        uint64_t flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        if (tex.wrapS == Render::TextureImage::Clamp)
            flags |= BGFX_SAMPLER_U_CLAMP;
        if (tex.wrapT == Render::TextureImage::Clamp)
            flags |= BGFX_SAMPLER_V_CLAMP;
        handle = bgfx::createTexture2D(
            uint16_t(tex.width), uint16_t(tex.height), true, 1,
            bgfx::TextureFormat::RGBA8, flags, mem);
    }
};

// Set the effective model transform of a draw. Plain draws use
// DrawCall::model as-is; autozoom draws replay the material's autozoom
// chain per frame like the GL renderer's setupMatrix: accumulate each
// entry's matrix Coin-style (multLeft — bx::mtxMul(a, m) with the same
// row-vector convention and memory layout), then apply the entry's node
// (SoAutoZoomTranslation::doAction: keep the accumulated rotation and the
// world position of the local origin, substitute the scale with
// scaleFactor times the per-frame world-to-screen scale), and multiply
// the draw's own matrix in last. The scale substitution extracts the
// rotation by normalizing the upper 3x3 rows, which matches Coin's
// getTransform for the rotation + translation + uniform positive scale
// matrices autozoom is used with (no shear support).
inline void setDrawTransform(const Render::DrawCall &draw,
                             float autozoomScale,
                             const float *viewMatrix,
                             const float *projMatrix,
                             float viewportHeight,
                             const Render::OverlayAnchor *overlayAnchor = nullptr,
                             float overlayRectHeight = 0.f)
{
    const auto &autozoom = draw.material.autozoom;
    if (autozoom.empty()) {
        if (!draw.identity)
            bgfx::setTransform(draw.model);
        return;
    }

    float m[16];
    bx::mtxIdentity(m);
    for (const auto &entry : autozoom) {
        // Each entry.matrix is the FULL accumulated model matrix at its
        // SoAutoZoomTranslation node (SoFCRenderCache::addAutoZoom stores
        // SoModelMatrixElement as-is), so it is absolute, not incremental —
        // the deepest autozoom on the path wins, matching Coin's nested
        // doAction (each substitutes scale on the current matrix). Assigning
        // it (rather than multiplying) is byte-identical for a single entry
        // (m starts identity) and fixes the case of two autozooms on one path
        // — e.g. a SoTextImage glyph companion placed under a node that
        // already carries its own autozoom (ViewProviderDatumCS), where the
        // former mtxMul double-counted the shared prefix.
        if (entry.resetmatrix && entry.identity)
            bx::mtxIdentity(m);
        else if (!entry.identity)
            std::memcpy(m, entry.matrix, sizeof(m));
        float sf = entry.scaleFactor == 0.0f
            ? 1.0f : entry.scaleFactor * autozoomScale;
        if (entry.billboard && viewMatrix && projMatrix && viewportHeight > 0.f) {
            // Screen-align: substitute the upper 3x3 with the camera basis so
            // the geometry always faces the viewer (SoText2-style text). The
            // camera's world-space axes are the columns of the view matrix's
            // 3x3 (row-vector layout: p_view = p_world * V). Keep the
            // accumulated translation (m[12..14]) as the anchor.
            const float *V = viewMatrix;
            const float *P = projMatrix;
            const float right[3] = {V[0], V[4], V[8]};   // local X -> screen right
            const float up[3]    = {V[1], V[5], V[9]};   // local Y -> screen up
            const float fwd[3]   = {V[2], V[6], V[10]};  // local Z -> toward viewer

            // Size the glyph screen-constant from the ANCHOR's own view-space
            // depth (not the global autozoomScale, which uses the orbit-centre
            // distance and so drifts for an off-centre label as the camera
            // orbits). A world length L at view depth d projects to
            // L*P[5]/d * (H/2) pixels (perspective) or L*P[5]*(H/2) (ortho), so
            // to render kBillboard screen pixels per native glyph pixel:
            //   sf = kBillboard * 2 / (P[5]*H)   [* d for perspective]
            float sfb;
            if (overlayAnchor) {
                // Overlay text (corner axis cross, NaviCube axis labels): the
                // draw is submitted into the overlay's own mini view, so P/H
                // above (the main scene projection + full viewport) do not
                // apply. The orientation substitution still uses the main
                // viewMatrix — correct, because orientFromScene builds the
                // overlay view from that same matrix, so the two cancel and the
                // label faces the viewer. Only the SIZE needs the overlay's own
                // projection + the label's depth in it.
                //
                // Size = screen-CONSTANT pixels (on-screen height ~= N*kGlyph
                // for an N-pixel glyph), identical in every overlay regardless
                // of the overlay's own pixel size — so the small axis-cross and
                // the larger NaviCube render their labels at the same absolute
                // on-screen size (kGlyph=1 => the label's own font pixel size).
                // Depth-corrected against the label's ACTUAL depth in the
                // mini-perspective (not the fixed camera distance), so the size
                // stays constant as the label orbits — the fixed-distance form
                // let near-side labels swell.
                const float H = overlayRectHeight > 0.f ? overlayRectHeight
                                                        : viewportHeight;
                float p5o;
                bool ovPersp;
                if (overlayAnchor->fovDeg > 0.f) {
                    p5o = 1.0f / std::tan(overlayAnchor->fovDeg
                                          * float(M_PI) / 360.0f);
                    ovPersp = true;
                }
                else {
                    p5o = 2.0f / std::max(overlayAnchor->orthoHeight, 1e-4f);
                    ovPersp = false;
                }
                const float kOverlayGlyph = 1.0f; // on-screen px per glyph px
                sfb = kOverlayGlyph * 2.0f / (p5o * H);
                if (ovPersp) {
                    // Label depth in the overlay view: its 3x3 is the scene view
                    // rotation (orientFromScene) and its z translation is
                    // -cameraDistance, so zview = a.(V z-basis) - cameraDistance.
                    const float ax = m[12], ay = m[13], az = m[14];
                    const float zov = ax*V[2] + ay*V[6] + az*V[10]
                                    - overlayAnchor->cameraDistance;
                    const float depth = -zov;
                    sfb *= (depth > 1e-4f ? depth : 1e-4f);
                }
            }
            else {
                const bool persp = std::abs(P[15]) < 1e-6f;
                const float ax = m[12], ay = m[13], az = m[14];
                const float zview = ax*V[2] + ay*V[6] + az*V[10] + V[14];
                const float depth = -zview;  // in front of the camera => positive
                const float p5 = std::abs(P[5]) > 1e-8f ? P[5] : 1.0f;
                // On-screen px per emitted unit: the entry's own pixel scale
                // when set (image quads in native pixels, 1:1 with raw GL),
                // else the glyph-legibility text factor.
                const float kBillboard =
                    entry.pixelscale > 0.f ? entry.pixelscale : 1.35f;
                sfb = kBillboard * 2.0f / (p5 * viewportHeight);
                if (persp)
                    sfb *= (depth > 1e-4f ? depth : 1e-4f);
            }

            for (int k = 0; k < 3; ++k) {
                m[0 + k] = right[k] * sfb;
                m[4 + k] = up[k]    * sfb;
                m[8 + k] = fwd[k]   * sfb;
            }
            m[3] = m[7] = m[11] = 0.0f;
        }
        else {
            for (int r = 0; r < 3; ++r) {
                float *row = m + r * 4;
                float len = std::sqrt(
                    row[0]*row[0] + row[1]*row[1] + row[2]*row[2]);
                float s = len > 1e-20f ? sf / len : 0.0f;
                row[0] *= s;
                row[1] *= s;
                row[2] *= s;
                row[3] = 0.0f;
            }
            if (entry.datumFlip && viewMatrix) {
                // Keep the glyph in its dimension plane but mirror its local
                // X/Y so the number reads upright from the current viewpoint —
                // a per-frame port of SoDatumLabel::GLRender's projected-axis +
                // backfacing test. Row 0/1 of m are the glyph's local X/Y axes
                // in world space; project them onto the view axes to see which
                // way they point on screen (row-vector: dir_view = dir_world*V).
                const float *V = viewMatrix;
                const float projX = m[0]*V[0] + m[1]*V[4] + m[2]*V[8];   // screen-x of local +X
                const float projY = m[4]*V[1] + m[5]*V[5] + m[6]*V[9];   // screen-y of local +Y
                // reflection in the model matrix inverts the decision
                const float det =
                    m[0]*(m[5]*m[10] - m[6]*m[9])
                  - m[1]*(m[4]*m[10] - m[6]*m[8])
                  + m[2]*(m[4]*m[9]  - m[5]*m[8]);
                const float margin = det < 0.f ? -1e-3f : 1e-3f;
                float xfactor = (-2.0f*projX < margin) ? 0.5f : -0.5f;
                float yfactor = (-2.0f*projY < margin) ? 0.5f : -0.5f;
                // backfacing: does the plane normal point away from the camera?
                // camera "toward viewer" world axis = view-matrix z column.
                const float *N = entry.normal;
                const float ndotz = N[0]*V[2] + N[1]*V[6] + N[2]*V[10];
                const bool backfacing = ndotz < 0.f;
                bool flip = backfacing ? (xfactor*yfactor > 0.f)
                                       : (xfactor*yfactor < 0.f);
                if (det < 0.f)
                    flip = !flip;
                if (flip)
                    xfactor = -xfactor;
                if (xfactor < 0.f) { m[0] = -m[0]; m[1] = -m[1]; m[2] = -m[2]; }
                if (yfactor < 0.f) { m[4] = -m[4]; m[5] = -m[5]; m[6] = -m[6]; }
            }
        }
        m[15] = 1.0f;  // the translation row m[12..14] stays
    }
    if (!draw.identity) {
        float tmp[16];
        bx::mtxMul(tmp, draw.model, m);
        std::memcpy(m, tmp, sizeof(m));
    }
    bgfx::setTransform(m);
}

inline void unpackColor(uint32_t rgba, float *out)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    out[3] = (rgba & 0xff) / 255.0f;
}

// FNV-1a accumulation for the shadow-map caster-set hash.
inline void hashBytes(uint64_t &h, const void *data, size_t len)
{
    auto p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

// 0xRRGGBBAA to the TransientVertex byte order (r,g,b,a in memory, i.e. what
// GpuMesh::upload memcpy's from MeshData::colors).
inline uint32_t vertexColor(uint32_t rgba)
{
    return ((rgba >> 24) & 0xff)
        | (((rgba >> 16) & 0xff) << 8)
        | (((rgba >> 8) & 0xff) << 16)
        | ((rgba & 0xff) << 24);
}

////////////////////////////////////////////////////////

/// The reserved "fc_emitter" parameter of a stateful particle
/// program (docs/RenderEngine.md §5.8): x = particle count,
/// y = fixed steps per second, z = freeze-frame warm-up seconds,
/// w = travel headroom as a fraction of the seed box diagonal, and
/// the next vector's x = the rate of the emitter's clock against the
/// wall clock. Absent or malformed leaves the defaults, which still
/// run — including a sender too old to carry the fifth lane, whose
/// emitters simply play in real time.
inline void emitterParams(const Render::UserShader &shader,
                          int &count, float &rate, float &warmup,
                          float *margin = nullptr,
                          float *timeScale = nullptr)
{
    count = 0;
    rate = 60.0f;
    warmup = 0.0f;
    if (margin)
        *margin = 0.0f;
    if (timeScale)
        *timeScale = 1.0f;
    for (const auto &p : shader.params) {
        if (p.name != "fc_emitter" || p.values.size() < 3)
            continue;
        count = int(p.values[0]);
        if (p.values[1] > 0.0f)
            rate = p.values[1];
        warmup = std::max(0.0f, p.values[2]);
        if (margin && p.values.size() >= 4)
            *margin = std::max(0.0f, p.values[3]);
        if (timeScale && p.values.size() >= 5)
            *timeScale = std::max(0.0f, p.values[4]);
        break;
    }
}

/// How far past its own bounds a draw can put pixels on screen.
///
/// Only a particle emitter has any: its seed geometry is a cloud of
/// zero-area quads at the spawn anchors, and the vertex stage flies
/// the billboards away from them, so the drawn result leaves the
/// box the vertices describe. The headroom is deliberately absent
/// from the geometry (buildEmitterSeedNodes) — it belongs to what
/// is culled, not to what is framed, spawned in, or streamed
/// against — so it is added back here, at the one consumer that
/// would otherwise cull a visible emitter the moment its anchors
/// left the frustum.
inline float drawHeadroom(const Render::DrawCall &d)
{
    const auto &sh = d.material.usershader;
    if (!sh || sh->stage != "particle")
        return 0.0f;
    int count = 0;
    float rate = 0.0f, warmup = 0.0f, margin = 0.0f;
    emitterParams(*sh, count, rate, warmup, &margin);
    if (margin <= 0.0f)
        return 0.0f;
    const float dx = d.bboxMax[0] - d.bboxMin[0];
    const float dy = d.bboxMax[1] - d.bboxMin[1];
    const float dz = d.bboxMax[2] - d.bboxMin[2];
    return margin * std::sqrt(dx * dx + dy * dy + dz * dz);
}

class BGFXView
{
public:
    // Pass sequence reproducing (a simplified subset of) SoFCRenderer's
    // draw order. Each is a bgfx view sharing the same framebuffer.
    /// Stateful particle emitters simulated per view, and the fixed
    /// simulation steps each may take in one frame
    /// (docs/RenderEngine.md §5.8). Both are view-id budget: a viewer
    /// occupies NUM_VIEWS contiguous bgfx ids out of the 512 the build
    /// configures (src/3rdParty/CMakeLists.txt), so these numbers are
    /// part of what says how many viewers can be open at once --
    /// NUM_VIEWS is 87 today, which fits five. Past the budget
    /// BGFXRendererLibP::getView refuses the viewer and it falls back
    /// to Coin rendering; it does not crash, and it does not silently
    /// share ids. A
    /// frame that cannot afford every step lets the simulation fall
    /// behind the clock rather than stretching the step — a stretched
    /// step is a different simulation.
    enum {
        kParticleSlots = 3,
        kParticleSteps = 2,
        kParticleViews = kParticleSlots * kParticleSteps,
    };
    /// How far behind the clock a live emitter may fall before the
    /// missed time is written off instead of simulated (seconds).
    ///
    /// The target is wall-clock, so time that passes while frames are
    /// not being drawn still counts: a stalled view, a slow frame, or a
    /// backgrounded browser tab (where requestAnimationFrame stops
    /// entirely and the gap is unbounded) all leave the simulation owing
    /// time. Repaying it at the full per-frame step budget is visible as
    /// the fountain fast-forwarding when the view comes back.
    ///
    /// A few frames of debt is worth repaying — that is an ordinary
    /// hitch, and catching up keeps the motion continuous. Beyond that
    /// the view was not being watched, so treat it as a pause: the
    /// emitter keeps its state and resumes from the current instant.
    /// The alternative, stretching the step to cover the gap, is a
    /// different simulation (see kParticleSteps).
    static constexpr float kParticleMaxLag = 0.25f;

    enum PassView {
        // The particle state steps come first on purpose: bgfx submits
        // views in id order, and every later pass that draws or shades
        // particles reads the state these wrote this frame.
        ViewParticleSim0 = 0,
        ViewParticleSimLast = ViewParticleSim0 + kParticleViews - 1,
        ViewParticleImpact, // impacts the steps just reported, scattered
                            // into the water impact map (one point per
                            // particle, own framebuffer): every emitter
                            // and every step of this frame splat into
                            // the same map, so one view id carries the
                            // lot (docs/RenderEngine.md §5.8)
        ViewBackground,     // clear + gradient background quad (clip space)
        ViewSunDisc,        // visible sun (disc + limb glow) along the
                            // directional scene light, over the
                            // background before any geometry — the
                            // opaque pass overdraws it (occlusion for
                            // free) and the bloom bright pass picks it
                            // up. Perspective cameras only
        ViewShadow,         // variance shadow map moments of shadow
                            // casting scene triangles, rendered from the
                            // scene light (own framebuffer, light camera)
        ViewShadowBlurH,    // separable gaussian blur of the shadow
                            // moments (ShadowSmoothBorder): horizontal
                            // into the ping texture ...
        ViewShadowBlurV,    // ... and vertical back into the moments
                            // texture, so receivers and the volumetric
                            // raymarch keep sampling the same target
        ViewShadowTint,     // glass casters render their per-channel
                            // light transmittance into the shadow tint
                            // map (cleared to white, multiplicative);
                            // receivers multiply it into the direct
                            // scene-light term — glass shadows are
                            // softer, tinted when colored
        ViewShadowTintBlurH, // ShadowSmoothBorder blur of the tint map
                            // (same separable gaussian as the moments —
                            // an unblurred tint edge would stay hard
                            // inside the smoothed penumbra) ...
        ViewShadowTintBlurV, // ... and vertical back into the tint map
        ViewBulbShadow0,    // shadow-map tiles of shadow-casting
        ViewBulbShadow1,    // light-source bodies (Render_LightShadow):
        ViewBulbShadow2,    // plain VSM moments of a wide downward
        ViewBulbShadow3,    // cone from each bulb (one tile), or of
        ViewBulbShadow4,    // six world-axis cube faces around it when
        ViewBulbShadow5,    // Render_LightShadowExtended is on. Tiles
        ViewBulbShadow6,    // allocate sequentially from a 4x4 atlas
        ViewBulbShadow7,    // grid; each re-renders only when the
        ViewBulbShadow8,    // casters or the bulb change (cached like
        ViewBulbShadow9,    // the scene shadow map).
        ViewBulbShadow10,
        ViewBulbShadow11,
        ViewBulbShadow12,
        ViewBulbShadow13,
        ViewBulbShadow14,
        ViewBulbShadow15,
        ViewAOPrepass,      // SSAO depth+normal prepass of opaque scene
                            // triangles into a non-MSAA RGBA16F target
                            // (own framebuffer, own depth)
        ViewAODepthMip1,    // GTAO prefiltered depth pyramid (XeGTAO's
        ViewAODepthMip2,    // depth MIP chain): each level halves the
        ViewAODepthMip3,    // previous — fullscreen weighted-downsample
        ViewAODepthMip4,    // passes reading the prepass / prior level;
        ViewAODepthMip5,    // far horizon taps of the GTAO pass read the
        ViewAODepthMip6,    // coarse levels (long-range occlusion without
                            // sparse full-res taps, cache-coherent out to
                            // the pixel radius cap)
        ViewWaterFront,     // nearest front-face depths of water body
                            // draws (prepass shader family, own
                            // framebuffer): per-pixel entry of the
                            // volumetric water medium
        ViewWaterBack,      // farthest back-face depths of water body
                            // draws (depth GREATER, cleared to 0): the
                            // medium exit; no front but a back face
                            // means the camera is under water
        ViewGlassFront,     // nearest front-face depths of glass body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the absorption
                            // interval of the glass surface pass
        ViewGlassBack,      // farthest back-face depths of glass body
                            // draws (depth GREATER, cleared to 0): the
                            // absorption interval exit
        ViewCloudFront,     // nearest front-face depths of cloud body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the cloud medium
                            // interval of the volumetric raymarch
        ViewCloudBack,      // farthest back-face depths of cloud body
                            // draws (depth GREATER, cleared to 0): the
                            // cloud medium exit
        ViewFireFront,      // nearest front-face depths of fire body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the emissive flame
                            // interval of the volumetric raymarch
        ViewFireBack,       // farthest back-face depths of fire body
                            // draws (depth GREATER, cleared to 0): the
                            // flame interval exit
        ViewAOGen,          // fullscreen SSAO generation into the R8 AO
                            // target (hemisphere kernel over the prepass)
        ViewAOBlur,         // fullscreen 4x4 AO blur into a second R8
        ViewAOBlur2,        // GTAO only: second edge-aware denoise pass,
                            // ping-ponged back into the first R8 target
                            // (XeGTAO's DenoisePasses > 1)
        ViewVolGen,         // volumetric light shafts: half-res raymarch
                            // of the shadow map through the scattering
                            // medium, bounded by the prepass depth (own
                            // half-res framebuffer, scene transforms for
                            // position reconstruction)
        ViewVolAccum,       // temporal accumulation of the raymarch:
                            // current outputs blended into the history
                            // targets (constant-factor blend, reset to
                            // full replace on camera/scene change); the
                            // apply passes read the history
        ViewGroundRefl,     // ground reflection: the opaque scene
                            // re-rendered with the world mirrored about
                            // the shadow ground plane (own framebuffer,
                            // flipped culling, reflected shadow matrix);
                            // the overlay view blends it onto the ground
        ViewReflMedia,      // fountain/fire bodies composited into the
                            // mirrored-scene texture (analytic cylinder
                            // raymarch from the mirror camera) so the
                            // water/ground reflection shows the plume
                            // and flame, not what stands behind them
        ViewOpaque,         // opaque triangles, lines, points
        ViewSelection,      // opaque draws of non-on-top selections
                            // (SoFCRenderer's opaqueselections bucket):
                            // GL renders them painter-style right after
                            // the opaque scene, so coincident geometry
                            // (a selected sketch edge over its own scene
                            // line) resolves to the highlight color via
                            // LEQUAL. A Sequential view keeps that order
                            // where ViewOpaque's state sorting would not.
        ViewSectionCap,     // stencil section caps of clipped opaque
                            // solids (GL: _renderSection; before the
                            // outline/transparent passes like the GL
                            // opaque-loop caps, and the cap parity
                            // marking needs the stencil buffer before
                            // the outline passes leave their marks)
        ViewDebugScene,     // debug scene re-render (docs/RenderDebug.md
                            // modes 6/8): scene triangle fills
                            // re-rasterized into a dedicated full-res
                            // target — additive fragment counting for
                            // the overdraw heatmap, or depth-tested
                            // texcoord output for the UV mode; the
                            // ViewDebug blit samples the result.
                            // (Repurposes the retired AO-apply slot —
                            // the fullscreen AO multiply moved into the
                            // mesh shaders' ambient terms, aoMeshTex at
                            // unit 9.)
        ViewCavity,         // fullscreen cavity (curvature) multiply over
                            // the finished opaque scene: a one-pixel
                            // second derivative of the prepass normals
                            // darkening creases and ridges. After every
                            // opaque pass (section caps included) so it
                            // states the shape of everything solid, and
                            // before the outlines and the transparent
                            // bucket, which must stay crisp -- curvature
                            // read off a silhouette edge is meaningless,
                            // so the shader rejects depth steps rather
                            // than haloing them.
        ViewGroundReflApply, // ground reflection overlay: the mirrored
                            // scene blended onto the shadow ground quad
                            // (depth EQUAL against the ground's own
                            // depth); the reflection itself samples no
                            // screen AO, so it is not AO-darkened twice
        ViewOutline,        // hidden-line stencil outlines of scene draws
                            // (after all opaque geometry so the depth
                            // test sees the whole scene, before the
                            // transparent bucket blends over them)
        ViewCaustics,       // fullscreen additive water caustics splat
                            // over the prepass surfaces inside the water
                            // interval — before the volumetric apply so
                            // the extinction multiply absorbs the
                            // caustic light on its way to the eye
        ViewVolApply,       // fullscreen bilateral upsample of the
                            // half-res inscatter, composited onto the
                            // opaque scene before the transparent bucket
                            // (after the outlines so edges are fogged
                            // like the fills)
        ViewWaterCopy,      // fullscreen copy of the scene color into
                            // the sampleable refraction source (also
                            // forces the multisample resolve), after the
                            // volumetric composite so the refracted view
                            // carries the underwater tint and shafts
        ViewWaterSurface,   // water body draws re-rendered as the water
                            // surface: screen-space refraction from the
                            // copy, Fresnel environment reflection, sun
                            // glint; replaces their transparent-bucket
                            // rendering while the surface is enabled.
                            // The pass composites the raymarch's
                            // in-front-of-the-water inscatter itself —
                            // else a fountain plume / fire over the
                            // pool reads as behind the surface
        ViewGlassSurface,   // glass body draws re-rendered as glass:
                            // screen-space refraction (IOR + normal),
                            // per-channel thickness absorption from the
                            // glass front/back interval, Fresnel
                            // environment reflection; replaces their
                            // ordinary rendering
        ViewParticles,      // blended user particle draws (a "particle"
                            // stage program whose Blend is not Default).
                            // Their own view because the bucket they
                            // used to share, ViewOpaque, runs BEFORE the
                            // volumetric composite: a particle does not
                            // write depth, so the fog resolved the pixel
                            // it covered at the depth of whatever was
                            // behind it and multiplied the full
                            // background extinction over the sprite —
                            // every droplet of a fountain came out
                            // ringed with fog it is nowhere near. Here
                            // the sprites land after the inscatter, and
                            // after the water and glass surfaces, so
                            // spray over a pool reads as being in front
                            // of it (the same reason ViewWaterSurface
                            // composites its own in-front inscatter).
                            // Kept out of ViewTransparent so the sprites
                            // do not enter WBOIT: an emitter is
                            // thousands of tiny quads with no meaningful
                            // per-fragment depth, and the additive ones
                            // are commutative anyway.
        ViewTransparent,    // transparent triangles: WBOIT accumulation
                            // into the OIT targets, or blended
                            // back-to-front into the scene FBO when OIT
                            // is unavailable
        ViewOITComposite,   // fullscreen WBOIT resolve onto the scene FBO
        ViewSectionCapTransp, // section caps of clipped transparent
                            // solids, after the transparent bucket like
                            // GL's grouped section pass; stencil-cleared
                            // because the outline views left marks
        ViewBloomBright,    // bloom bright pass: the finished scene
                            // (opaque + water + transparent, before the
                            // on-top/UI buckets) box-downsampled to the
                            // quarter-res halo source, thresholded with
                            // a soft knee
        ViewBloomEmit,      // light-source bodies (Render_Light)
                            // re-rendered additively into the halo
                            // source at their HDR emission color
                            // (diffuse * intensity), depth-rejected
                            // against the prepass — the halo scales
                            // with the intensity even though the LDR
                            // scene clips their own pixels
        ViewBloomBlurH,     // separable gaussian of the halo source:
                            // horizontal into the ping target ...
        ViewBloomBlurV,     // ... and vertical back into the source
        ViewBloomApply,     // the blurred halo added onto the scene
                            // (blend ONE/ONE, scaled by the bloom
                            // intensity)
        ViewUserPostCopy,   // user "post" shader (docs/RenderDebug.md §6)
                            // input: resolve/copy of the composited scene
                            // color into userPostTex, so the user pass can
                            // read and write color without feedback
        ViewUserPost,       // the user post-stage program drawn fullscreen
                            // back into the scene target — after bloom, so
                            // it sees the final composited color, before
                            // ViewDebug/on-top/highlight/overlays so debug
                            // visualization and UI still draw on top
        ViewDebug,          // render debugging buffer visualization
                            // (docs/RenderDebug.md): when the RenderDebug
                            // view mode is active, a fullscreen blit
                            // overwrites the scene color with an
                            // intermediate target (prepass depth/normal,
                            // AO term, shadow term) — before the
                            // on-top/highlight/overlay passes so those
                            // still draw on top and the view stays
                            // navigable
        ViewOnTop,          // scene geometry with on-top materials
        ViewHighlight,      // selection-on-top and preselection highlight
        ViewOverlay0,       // overlay feeds (Renderer::setOverlay — the
                            // foreground superimposition, the corner axis
                            // cross): each active overlay slot renders
                            // late into its own view with an
                            // anchor-derived viewport + camera and a
                            // fresh depth inside its rect
        ViewOverlay1,
        ViewOverlay2,
        ViewOverlay3,
        ViewOverlay4,
        ViewOverlay5,
        ViewOverlay6,
        ViewOverlay7,
        ViewOverlay8,       // headroom: the overlay ids (foreground, axis,
                            // graphics-items, fps, navi-cube, navi-buttons,
                            // editing, dimensions) can all be active at once
        ViewPresent,        // standalone build only: fullscreen copy of
                            // the scene color onto the default backbuffer
                            // (the desktop build GL-blits into the Qt
                            // framebuffer instead)
        NUM_VIEWS
    };
    enum { NumOverlayViews = ViewPresent - ViewOverlay0 };

    /// One stateful emitter's particle state (docs/RenderEngine.md
    /// §5.8): two RGBA32F attachment pairs that ping-pong once per
    /// fixed step. `pos` holds xyz position + age, `vel` holds xyz
    /// velocity + lifetime. Sized by particle count, not by the
    /// window, so it survives a resize — which is why it lives outside
    /// BGFXView::destroy().
    struct ParticleState {
        bgfx::FrameBufferHandle fbo[2] = {BGFX_INVALID_HANDLE,
                                          BGFX_INVALID_HANDLE};
        bgfx::TextureHandle pos[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        bgfx::TextureHandle vel[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        /// What each step REPORTED rather than what it remembers:
        /// xyz = where this particle struck (model space), w = how
        /// hard (0 = it did not). Per step, so both buffers of a
        /// two-step frame hold a report the splat pass still owes the
        /// impact map — which is the reason it ping-pongs with the
        /// state instead of being one shared target.
        bgfx::TextureHandle imp[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        /// One quad per particle carrying its index and its corner,
        /// and nothing else: the impact splat's payload comes from
        /// `imp`, so its vertex buffer is a counter.
        bgfx::VertexBufferHandle idxVb = BGFX_INVALID_HANDLE;
        /// Buffers written by this frame's steps, oldest first, and
        /// how many — what the splat pass has to drain.
        int stepBuf[kParticleSteps] = {};
        int stepCount = 0;
        /// The clock this frame's impacts are stamped with. Live, that
        /// is the shared animation clock. Frozen, the animation clock
        /// stands still at zero while the emitter walks to its warm-up
        /// over several frames, so the emitter's own simulated time is
        /// the only reading under which a ring can age — and it makes
        /// the rings of a frozen frame a function of the warm-up, like
        /// everything else about it.
        float stamp = 0.0f;
        /// The emitter's model matrix this frame — how the splat pass
        /// turns a model-space impact into a place in the world.
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                           0, 0, 1, 0, 0, 0, 0, 1};
        bool modelIdentity = true;
        uint16_t gridW = 0;         ///< state texture size in texels
        uint16_t gridH = 0;
        int count = 0;              ///< particles (<= gridW * gridH)
        /// Identity of what produced this state: the step program
        /// source, the particle count and the emitter seed. A change
        /// is a different simulation, so the state resets.
        uint64_t identity = 0;
        int cur = 0;                ///< buffer holding the live state
        float simTime = 0.0f;       ///< seconds simulated since reset
        bool needInit = true;
        uint32_t lastFrame = 0;     ///< view frame of the last step
        int slot = -1;              ///< sim view slot this frame, or -1

        void destroy()
        {
            for (int i = 0; i < 2; ++i) {
                if (bgfx::isValid(fbo[i])) {
                    bgfx::destroy(fbo[i]);
                    fbo[i] = BGFX_INVALID_HANDLE;
                }
            }
            for (int i = 0; i < 2; ++i) {
                for (auto tex : {&pos[i], &vel[i], &imp[i]}) {
                    if (bgfx::isValid(*tex)) {
                        bgfx::destroy(*tex);
                        *tex = BGFX_INVALID_HANDLE;
                    }
                }
            }
            if (bgfx::isValid(idxVb)) {
                bgfx::destroy(idxVb);
                idxVb = BGFX_INVALID_HANDLE;
            }
            stepCount = 0;
        }
    };
    /// Live particle states keyed by the draw's objectKey — the stable
    /// per-occurrence identity, so two occurrences of one emitter
    /// object simulate independently and a moved emitter keeps its
    /// particles.
    std::map<uint64_t, ParticleState> particles;

    // -----------------------------------------------------------------
    // Handle registry. Every bgfx handle the view owns as a member
    // appears here EXACTLY ONCE, in destruction order (a framebuffer
    // strictly before the textures it references), tagged with its
    // lifetime:
    //
    //   LifeSized -- sized by the window / created by init(): released
    //                by destroy() and recreated by the next init().
    //   LifeView  -- created once per view and kept across the
    //                resize-driven destroy()/init() cycles (validity
    //                guards in init() skip re-creation): released only
    //                when the view ends.
    //
    // destroy() and the destructor both sweep this one list instead of
    // keeping hand-maintained copies -- a handle listed here cannot be
    // released by one of them and leaked by the other, which is the
    // bug class the old per-method hand lists produced. Handles a
    // framebuffer owns (created with destroyTextures=true, e.g. the
    // sink attachments) are NOT listed; their owner's release
    // invalidates them by hand.
    enum HandleLife : uint8_t { LifeSized, LifeView };
    template <typename Fn>
    void forEachHandle(Fn &&fn)
    {
        fn(whiteColorVb, LifeSized);
        // SSAO/debug-scene resources: framebuffers before the textures
        // they reference.
        fn(debugSceneFbo, LifeSized);
        fn(aoPrepassFbo, LifeSized);
        fn(aoGenFbo, LifeSized);
        fn(aoBlurFbo, LifeSized);
        for (auto &h : aoMipFbo)
            fn(h, LifeSized);
        fn(debugSceneTex, LifeSized);
        fn(debugSceneDepth, LifeSized);
        fn(aoNormalZ, LifeSized);
        fn(aoDepth, LifeSized);
        fn(aoTex, LifeSized);
        fn(aoBlurTex, LifeSized);
        fn(aoNoiseTex, LifeSized);
        for (auto &h : aoMipTex)
            fn(h, LifeSized);
        // Volumetric / medium / bloom resources: the framebuffers
        // before their textures.
        fn(volFbo, LifeSized);
        fn(volHistFbo, LifeSized);
        fn(bloomFbo, LifeSized);
        fn(bloomBlurFbo, LifeSized);
        fn(bulbShadowFbo, LifeSized);
        fn(waterFrontFbo, LifeSized);
        fn(waterBackFbo, LifeSized);
        fn(glassFrontFbo, LifeSized);
        fn(glassBackFbo, LifeSized);
        fn(cloudFrontFbo, LifeSized);
        fn(cloudBackFbo, LifeSized);
        fn(fireFrontFbo, LifeSized);
        fn(fireBackFbo, LifeSized);
        fn(sceneCopyFbo, LifeSized);
        fn(reflFbo, LifeSized);
        fn(volTex, LifeSized);
        fn(volFrontTex, LifeSized);
        fn(volHistTex, LifeSized);
        fn(volHistFrontTex, LifeSized);
        fn(bloomTex, LifeSized);
        fn(bloomBlurTex, LifeSized);
        fn(bulbShadowTex, LifeSized);
        fn(bulbShadowDepth, LifeSized);
        fn(waterFrontTex, LifeSized);
        fn(waterBackTex, LifeSized);
        fn(waterFrontDepth, LifeSized);
        fn(waterBackDepth, LifeSized);
        fn(glassFrontTex, LifeSized);
        fn(glassBackTex, LifeSized);
        fn(glassFrontDepth, LifeSized);
        fn(glassBackDepth, LifeSized);
        fn(cloudFrontTex, LifeSized);
        fn(cloudBackTex, LifeSized);
        fn(cloudFrontDepth, LifeSized);
        fn(cloudBackDepth, LifeSized);
        fn(fireFrontTex, LifeSized);
        fn(fireBackTex, LifeSized);
        fn(fireFrontDepth, LifeSized);
        fn(fireBackDepth, LifeSized);
        fn(sceneCopyTex, LifeSized);
        fn(reflTex, LifeSized);
        fn(reflDepth, LifeSized);
        fn(s_texVol, LifeSized);
        fn(s_texVolFront, LifeSized);
        fn(u_volParams, LifeSized);
        fn(u_volMedium, LifeSized);
        fn(u_volTexel, LifeSized);
        fn(s_texWaterFront, LifeSized);
        fn(s_texWaterBack, LifeSized);
        fn(u_waterSigma, LifeSized);
        fn(u_causticParams, LifeSized);
        fn(s_texScene, LifeSized);
        fn(s_texRefl, LifeSized);
        fn(u_waterSurf, LifeSized);
        fn(u_waterAbsorb, LifeSized);
        fn(u_waterRipple, LifeSized);
        fn(u_reflParams, LifeSized);
        fn(s_texGlassFront, LifeSized);
        fn(s_texGlassBack, LifeSized);
        fn(u_glassParams, LifeSized);
        fn(s_texCloudFront, LifeSized);
        fn(s_texCloudBack, LifeSized);
        fn(u_cloudParams, LifeSized);
        fn(s_texFireFront, LifeSized);
        fn(s_texFireBack, LifeSized);
        fn(u_fireParams, LifeSized);
        fn(u_fireParams2, LifeSized);
        fn(u_fireFrame, LifeSized);
        fn(u_fountainParams, LifeSized);
        fn(u_fountainFrame, LifeSized);
        fn(u_waterSplash, LifeSized);
        fn(u_mediumSlot, LifeSized);
        fn(m_progPrepass, LifeSized);
        fn(m_progPrepassClip, LifeSized);
        fn(m_progMedDepth, LifeSized);
        fn(m_progMedDepthClip, LifeSized);
        fn(m_progPrepassInst, LifeSized);
        fn(m_progSsao, LifeSized);
        fn(m_progGtao, LifeSized);
        fn(m_progGtaoBlur, LifeSized);
        fn(m_progGtaoDepth, LifeSized);
        fn(m_progSsaoBlur, LifeSized);
        fn(m_progCavity, LifeSized);
        fn(m_progVol, LifeSized);
        fn(m_progVolAccum, LifeSized);
        fn(m_progReflMedia, LifeSized);
        fn(m_progBloomBright, LifeSized);
        fn(m_progBloomEmit, LifeSized);
        fn(m_progBloomBlur, LifeSized);
        fn(m_progBloomApply, LifeSized);
        fn(m_progSun, LifeSized);
        fn(m_progEnvBg, LifeSized);
        fn(m_progVolApply, LifeSized);
        fn(m_progVolExt, LifeSized);
        fn(m_progCaustics, LifeSized);
        fn(m_progWaterCopy, LifeSized);
        fn(m_progWater, LifeSized);
        fn(m_progGlass, LifeSized);
        fn(m_progGroundRefl, LifeSized);
        // Shadow resources: the framebuffers before their textures.
        fn(shadowFbo, LifeSized);
        fn(shadowBlurFbo, LifeSized);
        fn(shadowBlurBackFbo, LifeSized);
        fn(shadowTintFbo, LifeSized);
        fn(shadowTintBlurFbo, LifeSized);
        fn(shadowTintBlurBackFbo, LifeSized);
        fn(shadowTex, LifeSized);
        fn(shadowDepth, LifeSized);
        fn(shadowBlurTex, LifeSized);
        fn(shadowTintTex, LifeSized);
        fn(shadowTintBlurTex, LifeSized);
        fn(m_progShadow, LifeSized);
        fn(m_progShadowClip, LifeSized);
        fn(m_progShadowInst, LifeSized);
        fn(m_progShadowBlur, LifeSized);
        fn(m_progShadowTint, LifeSized);
        fn(s_texShadow, LifeSized);
        fn(s_texShadowTint, LifeSized);
        fn(s_texAOScreen, LifeSized);
        fn(u_debugParams, LifeSized);
        fn(s_texDebugScene, LifeSized);
        fn(u_shadowParams, LifeSized);
        fn(u_lightDir, LifeSized);
        fn(u_lightPos, LifeSized);
        fn(u_lightColor, LifeSized);
        fn(u_shadowMatrix, LifeSized);
        fn(u_shadowBlur, LifeSized);
        fn(u_evsm, LifeSized);
        fn(u_localLight, LifeSized);
        fn(u_localLightColor, LifeSized);
        fn(s_texBloom, LifeSized);
        fn(u_bloomParams, LifeSized);
        fn(u_bloomTexel, LifeSized);
        fn(u_bloomBlur, LifeSized);
        fn(u_sunParams, LifeSized);
        fn(s_texBulbShadow, LifeSized);
        fn(u_bulbShadowMtx, LifeSized);
        fn(u_bulbShadowConf, LifeSized);
        fn(u_bulbShadowRot, LifeSized);
        // PBR environment resources.
        fn(m_envTex, LifeSized);
        fn(m_dummyEnvTex, LifeSized);
        fn(s_texNormalZ, LifeSized);
        fn(s_texAONoise, LifeSized);
        fn(s_texAO, LifeSized);
        for (auto &h : s_texAOMip)
            fn(h, LifeSized);
        fn(u_aoParams, LifeSized);
        fn(u_aoParams2, LifeSized);
        fn(u_cavityParams, LifeSized);
        fn(u_aoKernel, LifeSized);
        fn(s_texEnv, LifeSized);
        fn(u_pbrParams, LifeSized);
        fn(u_matcapParams, LifeSized);
        fn(u_envSH, LifeSized);
        fn(s_texBump, LifeSized);
        fn(u_bumpParams, LifeSized);
        fn(s_texEmissive, LifeSized);
        fn(s_texOcclusion, LifeSized);
        fn(s_texMetallicRoughness, LifeSized);
        // The OIT framebuffer references bgfxDepth (owned by bgfxFbo),
        // so it goes first; the sink framebuffer owns its attachments
        // (sinkColor/sinkDepth are invalidated by the sweep caller).
        fn(oitFbo, LifeSized);
        fn(oitAccum, LifeSized);
        fn(oitReveal, LifeSized);
        fn(bgfxFbo, LifeSized);
        fn(sinkFbo, LifeSized);
        fn(m_progMesh, LifeSized);
        fn(m_progMeshInst, LifeSized);
        fn(m_progMeshInstTex, LifeSized);
        fn(m_progMeshInstOit, LifeSized);
        fn(m_progMeshInstOitTex, LifeSized);
        fn(u_instParams, LifeSized);
        fn(m_progFlat, LifeSized);
        fn(m_progMeshClip, LifeSized);
        fn(m_progFlatClip, LifeSized);
        fn(m_progLine, LifeSized);
        fn(m_progLineClip, LifeSized);
        fn(m_progLinePat, LifeSized);
        fn(m_progLinePatClip, LifeSized);
        fn(m_progPoint, LifeSized);
        fn(m_progPointClip, LifeSized);
        fn(m_progMeshTex, LifeSized);
        fn(m_progMeshTexClip, LifeSized);
        fn(m_progMeshOitTex, LifeSized);
        fn(m_progMeshOitTexClip, LifeSized);
        fn(s_texColor, LifeSized);
        fn(u_texMatrix, LifeSized);
        fn(u_texParams, LifeSized);
        fn(u_texBlendColor, LifeSized);
        fn(m_progMeshOit, LifeSized);
        fn(m_progMeshOitClip, LifeSized);
        fn(m_progComp, LifeSized);
        fn(m_progDebug, LifeSized);
        fn(m_progDebugScene, LifeSized);
        fn(m_progDebugSceneClip, LifeSized);
        fn(m_progCap, LifeSized);
        fn(m_progCapClip, LifeSized);
        fn(s_texHatch, LifeSized);
        fn(m_whiteTex, LifeSized);
        fn(m_blackTex, LifeSized);
        fn(m_hatchTex, LifeSized);
        fn(s_texAccum, LifeSized);
        fn(s_texReveal, LifeSized);
        fn(m_lineQuadVb, LifeSized);
        fn(m_lineQuadIb, LifeSized);
        fn(u_matColor, LifeSized);
        fn(u_matEmissive, LifeSized);
        fn(u_matSpecular, LifeSized);
        fn(u_params, LifeSized);
        fn(u_polyOffset, LifeSized);
        fn(u_clipParams, LifeSized);
        fn(u_clipPlanes, LifeSized);
        fn(u_linePattern, LifeSized);
#ifdef FC_RENDERER_STANDALONE
        fn(m_progPresent, LifeSized);
#endif
        // Per-view-lifetime resources. The impact map is sized by the
        // map resolution, not by the window; the stateful-particle
        // programs/uniforms are created once per view and kept across
        // the resize-driven destroy()/init() cycles (see the validity
        // guard in init()).
        fn(impactFbo, LifeView);
        fn(impactTex, LifeView);
        fn(m_progPSimInit, LifeView);
        fn(m_progPImpact, LifeView);
        fn(s_pstate0, LifeView);
        fn(s_pstate1, LifeView);
        fn(u_pgrid, LifeView);
        fn(u_pboxMin, LifeView);
        fn(u_pboxMax, LifeView);
        fn(s_pimpsrc, LifeView);
        fn(u_impactFrame, LifeView);
        fn(u_impactNow, LifeView);
        fn(s_texImpact, LifeView);
        fn(u_waterImpact, LifeView);
        fn(u_waterImpactCfg, LifeView);
    }
    template <typename H>
    static void releaseHandle(H &h)
    {
        if (bgfx::isValid(h)) {
            bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        }
    }
    /// Release every registry handle of the given lifetime, in list
    /// (i.e. dependency) order.
    void sweepHandles(HandleLife life);

    ~BGFXView();

    void destroy();

    bgfx::TextureHandle createTexture(bgfx::TextureFormat::Enum format, uint64_t flags = 0,
                                      bool sampled = false);

    void init();

    /// Radiance of the environment for a world direction (Z up, unit
    /// length). With a user image (PBRConfig::envImage) that image is
    /// sampled — a 2:1 image as equirectangular (lat-long), anything
    /// squarer as a GL sphere map, the convention the Texture mapping
    /// dialog's Environment mode uses for the same file. Otherwise the
    /// fixed procedural studio environment: a vertical
    /// ground/horizon/sky gradient plus three broad light lobes
    /// (key/fill/rim), all values fixed so frames stay deterministic.
    /// Modest HDR range either way.
    void envRadiance(const float d[3], float out[3]) const;

    /// Bilinear lookup of a user environment image along a world
    /// direction. Coin's SoTextureCoordinateEnvironment (and fixed
    /// function GL_SPHERE_MAP) maps a reflection vector to the unit
    /// disc of a sphere/light-probe image, so a roughly square image is
    /// read that way; a 2:1 image is the usual lat-long panorama. The
    /// image is Y-up in world terms: Z is up in FreeCAD.
    static void sampleEnvImage(const Render::TextureImage &img,
                               const float d[3], float out[3]);

    static void envRadianceProcedural(const float d[3], float out[3]);

    // World direction of a cube face texel; standard GL/D3D face order
    // and orientation (+x, -x, +y, -y, +z, -z), u/v in [-1, 1].
    static void cubeDir(int face, float u, float v, float d[3]);

    // Build the image based lighting data once per view: a
    // GGX-prefiltered RGBA16F cubemap (shader lod = roughness * 5, the
    // 1-2 px tail mips stay at full roughness) and the cosine-convolved
    // irradiance SH of the same environment in Ramamoorthi's polynomial
    // form, basis and 1/pi constants folded so the shader evaluates
    // plain dot products. CPU cost is a one-off ~2M radiance samples.
    void ensureEnvironment();

    GpuMesh *getMesh(const Render::MeshData &data)
    {
        GpuMesh &mesh = meshes[data.cacheId];
        mesh.lastUsed = frame;
        // "A cache id always refers to identical content" stopped
        // being true when meshes grew rungs: every level of a ladder
        // fills ONE mesh object under one cacheId, in place
        // (docs/SceneStreaming.md §7), and a mesh being drawn is never
        // idle long enough for the two-frame purge to retire its
        // upload. Trusting the id alone kept the coarse buffers under
        // exact-sized index counts — "Insufficient buffer size" from
        // the driver, a scene whose books said exact while the screen
        // showed facets. The generation is bumped by every in-place
        // fill and release; when it moves, the upload is of a mesh
        // that no longer exists. The shared geometry entry stays for
        // whoever still matches it and ages out on its own.
        if (mesh.geom && mesh.generation != data.generation)
            mesh.destroy();
        if (!mesh.geom) {
            mesh.generation = data.generation;
            GeomKey key = computeGeomKey(data);
            auto res = geometries.emplace(key, GpuGeometry());
            GpuGeometry &geom = res.first->second;
            if (!bgfx::isValid(geom.vbh))
                geom.upload(data);
            if (!bgfx::isValid(geom.vbh)) {
                // handle pool exhausted: the draw sites skip an
                // invalid upload instead of fatally binding it
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    fprintf(stderr,
                            "bgfx: vertex buffer handle pool exhausted, "
                            "some meshes will not be drawn\n");
                }
            }
            mesh.geom = &geom;
            mesh.upload(data);
            static const bool dbgfeed =
                (getenv("FC_BGFX_DEBUG_FEED") != nullptr);
            if (dbgfeed)
                fprintf(stderr,
                        "bgfx mesh cache=%llx nv=%d geom=%llx%s\n",
                        (unsigned long long)data.cacheId,
                        data.numVertices,
                        (unsigned long long)key.hash,
                        res.second ? "" : " shared");
        }
        mesh.geom->lastUsed = frame;
        return &mesh;
    }

    /// Color stream fallback of meshes without baked per-vertex colors:
    /// one shared all-white buffer, regrown to the largest vertex count
    /// seen (attribute fetches are bounded by the draw's indices).
    bgfx::VertexBufferHandle whiteColors(int numVertices);

    /// Bind the two mesh vertex streams: the shared colorless geometry
    /// and the per-cache color stream (white fallback). Only for
    /// programs whose vertex stage reads a_color0 (mesh/flat families);
    /// depth-only programs bind gpu->geom->vbh alone.
    void setMeshVertexBuffers(GpuMesh *gpu, const Render::MeshData &mesh);

    GpuTexture *getTexture(const Render::TextureImage &data)
    {
        GpuTexture &tex = textures[data.textureId];
        tex.lastUsed = frame;
        // A placeholder is re-examined every frame: the pixels it
        // stands in for are in flight, and the id they will arrive
        // under is this one, so nothing else would ever replace it.
        if (tex.placeholder)
            tex.destroy();
        if (!bgfx::isValid(tex.handle))
            tex.upload(data);
        return &tex;
    }

    // Drop GPU buffers of caches/textures that no draw call referenced
    // recently.
    void collectMeshes();

    // Fullscreen gradient behind the scene, replicating
    // SoFCBackgroundGradient::GLRender vertex for vertex in clip space
    // (the background view has identity view/proj, so transparent scene
    // geometry blends against the real background colors). Depth is
    // neither tested nor written: the buffer keeps the far-plane clear
    // value, matching the GL path where the gradient sits at the far
    // plane.
    /// Visible sun disc + limb glow along the directional scene light,
    /// drawn additively over the background (its view precedes all
    /// geometry, which then overdraws it). u_proj on the view
    /// reconstructs the pixel direction; the shader outputs nothing for
    /// orthographic cameras.
    void submitSunDisc(float sizeDeg);

    /// PBR environment background (PBRConfig::envBackground): the IBL
    /// cubemap drawn as the visible background instead of the gradient
    /// quad. The background view keeps the scene view/proj for this
    /// (the fullscreen vertex shader ignores them; the fragment shader
    /// reconstructs each pixel's world direction from the predefined
    /// u_proj/u_invView). A soft cubemap lod keeps the studio lobes
    /// from reading as hard clipped discs.
    void submitEnvBackground();

    void submitBackground(const Render::Background &bg);

    /// The constant `units * r` half of glPolygonOffset(factor, units),
    /// as an NDC depth bias (u_params.w). Positive pushes away from the
    /// viewer. The `factor * m` slope half is per-vertex and lives in
    /// fc_mesh_vs.sh, fed by setPolygonOffsetUniform().
    static float polygonOffsetBias(const Render::Material &mat);

    /// Ceiling on the depth gradient the vertex stage's slope term
    /// tracks, in NDC depth per NDC screen unit: 1 would be a surface
    /// crossing the entire depth range within one screen width, so this
    /// only ever engages on a face within a few degrees of edge-on,
    /// where the true gradient runs to infinity and GL is saved by such
    /// a polygon covering no pixels.
    static constexpr float kPolyOffsetMaxSlope = 4.0f;

    /// The largest NDC depth bias the slope term can produce for this
    /// material at the current viewport size — what the stencil
    /// outline has to clear to stay behind the fill that owns it.
    float polygonOffsetMaxBias(const Render::Material &mat) const;

    /// Bind u_polyOffset for one draw: the slope factor and its
    /// ceiling. Call at every site submitting a vs_fc_mesh program;
    /// pass null (or a non-triangle material) to disable the term.
    void setPolygonOffsetUniform(const Render::Material *mat);

    /// Appearance/placement of one stencil outline.
    struct OutlineSpec {
        uint16_t view = ViewHighlight;  ///< target bgfx view
        uint32_t color = 0x000000ff;    ///< packed RGBA, forced opaque
        float width = 1.0f;             ///< edge/cap width in pixels
        /// Corner cap size in pixels; 0 = same as width. The whole-scene
        /// silhouette pass uses the unscaled outline width for its caps
        /// where the edges are 1.5x (GL: renderSceneOutline ~1544).
        float capWidth = 0.0f;
        /// LEQUAL depth test, like GL's renderOutline for scene
        /// (hidden-line) entries; false = draw on top (highlight).
        bool depthTest = false;
        /// Depth writes of the edge/cap passes (the stencil mark pass
        /// never writes depth — it must not depth-kill farther outlines
        /// or the transparent fills that dim them). Writing the edge
        /// depth stands in for GL's back-to-front entry order: a
        /// *farther* transparent fill drawn later depth-fails on a
        /// nearer outline instead of dimming it, while nearer fills
        /// still blend over hidden outlines.
        bool depthWrite = false;
        bool caps = true;               ///< corner point caps
        int start = 0;                  ///< triangle index range;
        int count = 0;                  ///< count 0 = the whole buffer
    };

    // Stencil outline of (part of) a triangle draw, porting the GL
    // renderer's renderOutline: mark the pixels in the stencil buffer,
    // then redraw the triangle edges as thick screen-space lines (GL
    // uses glPolygonMode(GL_LINE), which modern APIs lack) plus
    // point-sprite corner caps where the stencil does not match — only
    // the boundary survives. Each outline uses its own stencil
    // reference so no per-part stencil clear is needed (refs wrap at
    // 255; collisions between outlines that far apart are accepted).
    // Serves both the selection/preselection face outline and the
    // hidden-line draw style's per-object/per-part outlines; clipped
    // materials clip all three passes like the GL state machine does.
    void submitOutline(const Render::DrawCall &draw, uint32_t refCounter,
                       const OutlineSpec &spec);

    /// Does this draw need a discard-clipping shader variant: its own
    /// section planes, or the mirror pass's water/ground plane.
    bool clipActiveFor(const Render::Material &mat) const;

    /// The mirror pass clips the world to the half-space above its
    /// plane. Without it every surface BELOW the plane — the ground the
    /// basin stands on, the pool floor, the submerged half of the rim —
    /// is mirrored upward and lands in front of the camera, so the
    /// reflection target fills with the underside of the scene and the
    /// water reflects a slab instead of the sky and whatever stands in
    /// it. Concave (union-mode) section clipping is left alone: the
    /// shader has one mode for all planes, and intersecting a plane
    /// with a union needs a second one.
    bool reflClipActive(const Render::Material &mat) const;

    void setClipUniforms(const Render::Material &mat);

    /// Stencil-mark pass of an outline: rasterize (part of) the triangle
    /// draw into the stencil buffer with the given reference, no color
    /// output. Scene outlines keep the depth test (matching GL's
    /// color-masked fill pass; the mark still replaces on depth fail);
    /// highlight outlines draw on top. The whole-scene silhouette marks
    /// every scene draw under one shared reference before its edge
    /// passes. Never writes depth — it must not depth-kill farther
    /// outlines or the transparent fills that dim them.
    bool submitOutlineMark(const Render::DrawCall &draw,
                           uint32_t refCounter, uint16_t view,
                           bool depthTest, int start = 0, int count = 0);

    /// Edge and corner-cap passes of an outline: redraw the triangle
    /// edges as instanced thick lines (plus point-sprite caps) where the
    /// stencil does not match the reference — only the boundary
    /// survives.
    void submitOutlineEdges(const Render::DrawCall &draw,
                            uint32_t refCounter, const OutlineSpec &spec);

    // Upload the section hatch texture when the CPU-side pixels changed
    // (version 0 = no hatch image).
    void updateHatchTexture(uint64_t version, const uint8_t *rgba,
                            int width, int height);

    /// Stencil parity mark of one section-cap pass (GL: _renderSection's
    /// color-masked renderSolids loop): rasterize the solid triangle
    /// ranges clipped by the single active section plane with a
    /// depth-independent stencil INVERT — pixels looking through the
    /// cut opening end up with an odd (non-zero) parity. Assumes the
    /// stencil is zero where the draw rasterizes (the cap view is
    /// stencil-cleared and every cap pass cleans up after itself).
    bool submitCapMark(const Render::DrawCall &draw,
                       const float plane[4], uint16_t view);

    /// Cap fill of one section-cap pass: a world-space quad in the
    /// section plane, drawn where the stencil parity is odd — the cross
    /// section of the marked solids — with depth LESS + write like GL's
    /// cap (LESS so the already-drawn fill keeps the shared rim pixels
    /// that GL's cap-before-fill order gives to the fill). Clipped by
    /// the remaining planes when there are any (never in concave mode,
    /// matching the GL clip state there).
    void submitCapQuad(const CapVertex verts[4], uint32_t color,
                       const float (*otherPlanes)[4], int numOther,
                       bool hatch, bool blend, uint16_t view);

    /// Stencil cleanup of one section-cap pass (the stand-in for GL's
    /// per-pass glClear(GL_STENCIL_BUFFER_BIT)): zero the stencil over
    /// the cap quad, which covers every pixel the parity mark can have
    /// touched (the cut cross section lies inside the plane/circumsphere
    /// intersection), unclipped so marks outside the other planes are
    /// cleaned too. No color or depth output.
    void submitCapCleanup(const CapVertex verts[4], uint16_t view);

    // Fullscreen WBOIT resolve: average the accumulated premultiplied
    // color and blend it onto the scene by coverage (1 - revealage in
    // the source alpha, blend INV_SRC_ALPHA / SRC_ALPHA).
    void submitComposite();

    /// Render debugging buffer visualization (docs/RenderDebug.md):
    /// overwrite the scene color with an intermediate target — prepass
    /// depth/normal (modes 1/2), the AO term (3), the shadow term
    /// re-evaluated from the prepass position (4), the shadow tile
    /// coverage (5), the overdraw/UV re-render (6/8), or the shadow
    /// filtering-precision probe (7). Runs before the
    /// on-top/highlight/overlay passes so those still draw on top.
    void submitDebug(const Render::RenderDebugConfig &conf, float maxDepth,
                     int aoMethod, bool shadowValid, float impactLife);

    /// SSAO depth+normal prepass of one opaque scene triangle draw:
    /// re-rasterize it into the non-MSAA prepass target with the
    /// encoding fragment shader, replicating the fill's transform, clip
    /// planes and culling so the AO sees exactly the visible geometry.
    // The shadow ground plane: a shadow receiving quad at the bottom of
    // the scene bounds (the Coin-side ground lives outside the captured
    // graph, so the backend draws its own). Lit by the scene light like
    // any receiver, classic shading regardless of the PBR mode.
    void submitShadowGround(const float bmin[3], const float bmax[3],
                            const Render::LightConfig &light,
                            bool prepass = false);

    // Rasterize a shadow casting triangle draw into the variance shadow
    // map under the light camera (the ViewShadow transform). Both faces
    // cast; section-clipped parts do not (clip shader variant).
    void submitShadowCaster(const Render::DrawCall &draw);

    /// Render a caster into one bulb shadow atlas tile (plain VSM
    /// moments — the tiles skip the smooth-border blur).
    void submitBulbShadowCaster(const Render::DrawCall &draw, int tile);

    /// Render a glass caster's light transmittance into the shadow
    /// tint map (multiplicative onto the white-cleared target; front
    /// faces only so one glass body multiplies once, no depth).
    void submitShadowTint(const Render::DrawCall &draw);

    /// Separable gaussian blur of the shadow moments (the Shadow draw
    /// style's SmoothBorder, 0..100): horizontal into the ping texture,
    /// vertical back into shadowTex, so the mesh receivers and the
    /// volumetric raymarch keep sampling the same target. The blur views
    /// sit right after the caster pass. Softens the VSM penumbra and
    /// curbs shimmer on razor-straight CAD edges.
    /// (Re)create the shadow map targets for the requested size
    /// (ShadowPrecision); the stored moments are lost, so the cached
    /// map re-renders.
    void ensureShadowTargets(uint16_t size);

    void submitShadowBlur(float smoothBorder);

    void submitPrepass(const Render::DrawCall &draw);

    /// Debug scene re-render target (docs/RenderDebug.md modes 6/8),
    /// created on first use at viewport size; every reset/resize path
    /// destroys it with the other offscreen targets.
    bool ensureDebugScene();

    /// Rasterize one scene triangle draw into the debug scene target
    /// (docs/RenderDebug.md): mode 6 accumulates a fragment count with
    /// the depth test off (additive blend — the overdraw heatmap
    /// source); mode 8 writes the depth-tested texcoords (the UV view).
    /// Transform, clip planes and culling replicate the color fill like
    /// the AO prepass does.
    void submitDebugScene(const Render::DrawCall &draw, int mode);

    /// Rasterize a water/glass/cloud body draw into one of its interval
    /// depth targets (the meddepth programs write the linear view depth
    /// in .z and the body's appearance slot in .x): front faces with
    /// the nearest depth = interval entry, back faces with the farthest
    /// = interval exit. Culling is forced by face side whatever the
    /// material's two-sidedness. kind: 0 = water, 1 = glass, 2 =
    /// cloud, 3 = fire.
    void submitWaterDepth(const Render::DrawCall &draw, bool back,
                          int kind = 0, int slot = 0);


    /// Advance every stateful particle emitter in this frame's scene by
    /// whole fixed steps (docs/RenderEngine.md §5.8), then leave each
    /// one's live state bound for the beauty draws that follow. The
    /// simulation is a fragment pass over a ping-pong pair of RGBA32F
    /// targets — no compute shaders, so the browser tier runs the same
    /// program the desktop does.
    ///
    /// Returns true while some emitter still owes steps, which keeps
    /// the viewer scheduling redraws: that is how a frozen frame
    /// reaches its warm-up state (the steps are the same length as the
    /// live ones, so a warmed-up frozen frame and a live frame are the
    /// same simulation, and both are reproducible from the reset).
    bool stepParticles(const Render::DrawCallList &scene,
                       float animTime, bool freeze);

    /// Scatter the impacts this frame's steps reported into the water
    /// impact map (docs/RenderEngine.md §5.8), so the water surface can
    /// raise its rings where particles actually struck it.
    ///
    /// `foot` is the world xy footprint the map covers — the bounds of
    /// the water bodies in the scene — as {xmin, ymin, xmax, ymax}, or
    /// null when there is no water to disturb. The map is framed on a
    /// square covering it, so a cell is square and a ring is round.
    ///
    /// Nothing here clears the map: a record is a standing statement
    /// that something struck this place at this time, and it stops
    /// mattering when the surface ages it past the ring lifetime, not
    /// when the frame that wrote it ends.
    void splatImpacts(float animTime, bool freeze, const float *foot);

    /// One clip-space triangle covering the viewport, submitted to a
    /// fullscreen resolve pass (uniforms/textures are set by the caller).
    void fullscreen(uint16_t pass, bgfx::ProgramHandle prog,
                    uint64_t state, uint32_t blendFactor = 0);

    /// Fullscreen AO resolve chain: occlusion from the prepass into the
    /// R8 target — hemisphere-kernel SSAO (method 0) or XeGTAO-style
    /// horizon-integral GTAO (method 1) — and a 4x4 box blur / GTAO
    /// denoise. The result (aoTex for GTAO, aoBlurTex classic) is not
    /// composited here: the mesh programs sample it at unit 9 and fold
    /// it into their ambient/headlight/IBL terms only (aoMeshTex), so
    /// direct scene/bulb light is not AO-darkened.
    void submitAOResolve(float radius, float intensity, int method,
                         bool fast, int slices, int steps);

    /// Cavity (curvature) shading: one fullscreen multiply of the
    /// finished opaque scene by a curvature term read from the prepass
    /// normals. Unlike the AO term above this one *is* composited here
    /// — it darkens the final color rather than an ambient sub-term, so
    /// it states shape independently of how the surface is lit.
    void submitCavity(float valley, float ridge);

    /// Volumetric light shaft resolve: raymarch the shadow map through
    /// the media at half resolution (ray ends at the prepass depth; a
    /// water body interval carries its own per-channel extinction and
    /// scattering), then composite onto the opaque scene in the
    /// sequential apply view — first the analytic per-channel
    /// transmittance multiply, then the bilateral-upsampled inscatter
    /// add: dst = inscatter + transmittance * scene.
    void submitVolumetric(float density, float intensity, float maxDist,
                          const float medium[4], bool water,
                          bool surfaceSplit, float accum,
                          const float waterSigma[][4],
                          const float cloudParams[][4],
                          const float fireParams[][4],
                          const float fireParams2[][4],
                          const float fireFrames[][16],
                          const float fountainParams[][4],
                          const float fountainFrames[][16]);

    /// Water caustics splat: additive fullscreen pass over the prepass
    /// surfaces inside the water body interval, in its own view before
    /// the volumetric apply (the extinction multiply then absorbs the
    /// caustic light over the eye-ward underwater path). The light /
    /// shadow / water uniforms match the raymarch; u_volParams.w flags
    /// the water span helper active.
    void submitCaustics(const float causticParams[][4],
                        const float waterSigma[][4]);

    /// Copy the scene color into the sampleable refraction source (its
    /// view sits after the volumetric composite; the framebuffer switch
    /// also resolves a multisampled scene attachment).
    void submitWaterCopy();

    /// User "post" stage (docs/RenderDebug.md §6): resolve the composited
    /// scene color into the water-refraction copy target (safe to share —
    /// this view runs after every reader of the mid-frame water copy),
    /// then draw the user program fullscreen back over the scene reading
    /// the copy. Its uniforms ride the dynamic name binding shared with
    /// the RenderDebug parameters, and like there the updates must be
    /// recorded with the consuming draw (see submitDebug).
    void submitUserPost(const Render::UserShader &shader,
                        bgfx::ProgramHandle prog);

    /// Bloom (glow) chain: bright-pass downsample of the finished scene
    /// into the quarter-res halo source, the light-source bodies added
    /// on top at their HDR emission color, a separable gaussian, and
    /// the additive composite back onto the scene.
    void submitBloom(float threshold, float intensity, float radius,
                     const std::vector<const Render::DrawCall *> &bulbs,
                     bool prepassCurrent);

    /// Re-render a water body draw as the animated water surface:
    /// screen-space refraction from the scene copy, Fresnel-blended
    /// environment reflection and a sun glint (fs_fc_water). Draws
    /// opaquely with depth write — the refraction replaces the
    /// transparent-bucket blending of the body.
    void submitWaterSurface(const Render::DrawCall &draw,
                            float waveStrength, float waveScale,
                            float time, bool depthReject, int reflMode,
                            bool refraction, bool absorb, float absorption,
                            float inscatter, bool shadow,
                            float shadowWobble,
                            int rippleType, float rippleDensity,
                            float impactStrength, float impactLife,
                            const float (*splash)[4], bool volFront);

    /// Re-render a glass body draw as glass: screen-space refraction of
    /// the scene copy (offset from the IOR-refracted view direction and
    /// the front/back thickness), per-channel Beer-Lambert absorption
    /// tinted by the material diffuse, Fresnel-blended environment
    /// reflection (fs_fc_glass). Draws opaquely with depth write like
    /// the water surface.
    void submitGlassSurface(const Render::DrawCall &draw, bool depthReject);

    /// Composite the fountain/fire media into the mirrored-scene
    /// reflection texture (premultiplied over): the analytic cylinder
    /// raymarch runs with the mirror-view transforms bound.
    void submitReflMedia(const float cloudParams[][4],
                         const float fireParams[][4],
                         const float fireParams2[][4],
                         const float fireFrames[][16],
                         const float fountainParams[][4],
                         const float fountainFrames[][16]);

    /// Blend the mirrored-scene render onto the shadow ground quad:
    /// the same quad geometry and vertex shader as the ground draw, so
    /// the EQUAL depth test hits exactly the ground pixels still
    /// visible; the reflection texture's alpha (0 = nothing mirrored)
    /// scales the blend with the intensity.
    void submitGroundReflOverlay(const float bmin[3], const float bmax[3],
                                 const Render::LightConfig &light);

    // Submission passes mirroring SoFCRenderer's delayed render loop.
    enum SubmitPass {
        PassNormal = 0,
        PassDepthOnly,   // depth-write-only prepass of on-top fills
        PassLineHidden,  // on-top lines/points, no depth test, dimmed
        PassLineSolid,   // on-top lines/points, depth LEQUAL, full color
    };

    /// Per-frame PBR/shadow uniform + sampler state shared by every
    /// triangle draw (the branches are uniform-selected in fc_mesh_fs.sh,
    /// so every mesh program consumes them).
    /// Bind the samplers and uniforms of a textured triangle draw
    /// (shared by the per-draw and the instanced submit paths).
    void bindTextureStage(const Render::Material &mat, bool bumped,
                          bool mapped);

    void setTriangleFrameState(const Render::Material &mat, int pass,
                               bool mapped, bool aoDraw);

    /// True when the cross-object instanced mesh path can run this frame.
    bool instancingActive() const
    {
        return m_instancing && bgfx::isValid(m_progMeshInst);
    }

    /// Stride of one instance: the model matrix columns (i_data0-3,
    /// DrawCall::model layout) followed by the diffuse color (i_data4).
    static constexpr uint32_t InstanceStride = 20 * sizeof(float);

    /// Submit one instanced draw of `count` placements of the prototype's
    /// mesh/material — the eligibility rules live in the producer
    /// (instancableDraw / buildInstanceGroups): an unclipped, non-on-top,
    /// non-water triangle draw of the normal scene pass. Textured draws
    /// batch too (the texture handles are part of the group key); a
    /// transparent group only batches while WBOIT runs — its blending is
    /// order-independent, unlike the sorted fallback. `data` holds
    /// count * InstanceStride bytes. Returns false when the program or
    /// the transient instance space is missing (or the group is
    /// transparent on a sorted-transparency frame); the caller falls
    /// back to per-draw submits.
    bool submitInstanced(const Render::DrawCall &draw, const float *data,
                         uint32_t count);

    /// Instanced counterpart of submitShadowCaster: one caster submit of
    /// `count` placements of the prototype's mesh. `data` uses the same
    /// {model, diffuse} InstanceStride layout as the color pass (the
    /// diffuse rides along unused). Instancable draws are never
    /// section-clipped, so only the unclipped program exists. Returns
    /// false when the program or transient instance space is missing;
    /// the caller falls back to per-draw caster submits.
    bool submitShadowCasterInstanced(const Render::DrawCall &draw,
                                     const float *data, uint32_t count);

    /// Instanced counterpart of submitPrepass (SSAO/volumetric
    /// depth+normal): same instance-data contract and fallback rules as
    /// submitShadowCasterInstanced.
    bool submitPrepassInstanced(const Render::DrawCall &draw,
                                const float *data, uint32_t count);

    void submit(const Render::DrawCall &draw, const float *viewMatrix,
                int pass = PassNormal, bool noseam = false);

    /// Does this user shader's beauty draw blend? Reads the same
    /// reserved "fc_state" parameter applyUserState below acts on
    /// (0 = Default, 1 = Alpha, 2 = Additive), because that is the only
    /// place App::ShaderProgram::Blend reaches the backend — the draw's
    /// Material carries the geometry's transparency, not the program's.
    static bool userDrawBlends(const Render::UserShader &shader);

    /// Reserved "fc_state" parameter = render-state override of a user
    /// shader's beauty draw (App::ShaderProgram Blend / DepthWrite,
    /// docs/RenderDebug.md §6.2): re-record the state — bgfx keeps the
    /// last setState before submit.
    /// The alpha channel means different things in the two targets a
    /// user draw lands in, so the blend cannot be the same in both.
    ///
    /// In the mirror target alpha is COVERAGE: the pass clears it to 0
    /// meaning "nothing reflected", and the water surface keeps only
    /// what covers. A sprite that blends colour without alpha there is
    /// discarded, and the pool reflects the scene but never the spray.
    ///
    /// In the scene target alpha is what the present composite may
    /// consume — the WASM viewer's does, the desktop GL blit does not.
    /// A sprite that blends alpha there punches its own shape out of
    /// the frame and the viewer resolves it toward black, which is why
    /// the browser drew grey spray while the desktop drew white from
    /// the same state.
    ///
    /// So: write coverage into the mirror, leave the scene's alpha
    /// alone.
    static void applyUserState(const Render::UserShader &shader,
                               uint64_t state, uint32_t blendRt,
                               bool coverage);

    static uint64_t depthFuncState(uint8_t func);

#ifdef FC_RENDERER_STANDALONE
    /// Standalone present: fullscreen copy of the scene color onto the
    /// default backbuffer (ViewPresent targets the invalid framebuffer).
    /// Submitted before bgfx::frame(), replacing the desktop GL blit.
    void present();
#else
    /// Write \a color (tightly packed RGBA8, glReadPixels bottom-up
    /// rows) to \a path: raw PPM for a .ppm extension, else through
    /// Qt's image writers (PNG etc.).
    static bool writeDumpImage(const std::string &path,
                               const unsigned char *color,
                               int width, int height);

    void blit(const Render::FrameDumpRequest *dump,
              Render::RenderStats *stats);
#endif // !FC_RENDERER_STANDALONE

    QOpenGLWidget *widget = nullptr;

    /////////////////////////////////////////////////////////
    // Pass -> bgfx view id mapping (the view-id budget)
    //
    // The pass sequence is NUM_VIEWS wide, but a frame draws only a
    // fraction of it: no stateful emitters, no bulb shadow tiles, no
    // media interval passes, two or three overlay slots out of nine.
    // Reserving the full width for every viewer is what limited the
    // renderer to a handful of 3D views at once.
    //
    // So each frame declares which passes it will use (markPass, from
    // the same flags that configure the views) and mapPasses() hands
    // those -- in enum order, which is draw order, which is bgfx's
    // submission order -- the consecutive ids of a block whose size is
    // the high-water mark of what this view has needed, not NUM_VIEWS.
    //
    // A pass that was not marked maps to `sinkView`: a real, configured
    // id that renders into a 1x1 scratch target. Mispredicting liveness
    // therefore costs the pass's pixels and a warning naming it, never a
    // draw into a neighbouring pass and never bgfx's "invalid view id"
    // abort. sinkHits/sinkPasses report it.
    static const uint16_t kNoPass = 0xffff;
    uint16_t viewId = 0;         ///< block base
    uint16_t viewSpan = 0;       ///< ids reserved for the block
    uint16_t viewLive = 0;       ///< ids the current frame mapped (+ sink)
    uint16_t sinkView = 0;       ///< id of the discard view (last of the block)
    uint16_t idMap[NUM_VIEWS] = {};
    bool passMark[NUM_VIEWS] = {};
    mutable uint32_t sinkHits = 0;
    mutable uint64_t sinkPasses[(NUM_VIEWS + 63) / 64] = {};
    bool sinkReported = false;
    bgfx::FrameBufferHandle sinkFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sinkColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sinkDepth = BGFX_INVALID_HANDLE;

    /// Start a frame's declaration: nothing is live until marked.
    void beginPasses()
    {
        std::memset(passMark, 0, sizeof(passMark));
        sinkHits = 0;
        std::memset(sinkPasses, 0, sizeof(sinkPasses));
    }
    /// Declare pass `p` (or the inclusive range `p`..`last`) live this frame.
    void markPass(int p, bool live = true)
    {
        if (live && p >= 0 && p < NUM_VIEWS)
            passMark[p] = true;
    }
    void markPasses(int p, int last, bool live = true)
    {
        for (; live && p <= last; ++p)
            markPass(p);
    }
    bool passLive(int p) const
    {
        return p >= 0 && p < NUM_VIEWS && passMark[p];
    }
    /// Ids the frame needs: the marked passes plus the sink.
    uint16_t passesNeeded() const
    {
        uint16_t n = 1;
        for (int i = 0; i < NUM_VIEWS; ++i)
            n += passMark[i] ? 1 : 0;
        return n;
    }
    /// Assign the marked passes consecutive ids from the block base.
    /// Must run before anything submits, and after the block is sized.
    void mapPasses()
    {
        uint16_t next = viewId;
        for (int i = 0; i < NUM_VIEWS; ++i)
            idMap[i] = passMark[i] ? next++ : kNoPass;
        sinkView = next++;
        viewLive = uint16_t(next - viewId);
        for (int i = 0; i < NUM_VIEWS; ++i) {
            if (idMap[i] == kNoPass)
                idMap[i] = sinkView;
        }
    }
    /// The bgfx view id of a pass, or the sink if the frame did not
    /// declare it. Never returns an id outside the block.
    uint16_t vid(int p) const
    {
        if (p < 0 || p >= NUM_VIEWS)
            return sinkView;
        if (!passMark[p]) {
            ++sinkHits;
            sinkPasses[p / 64] |= uint64_t(1) << (p % 64);
        }
        return idMap[p];
    }

    uint16_t width;
    uint16_t height;
    // Reduced resolution of the expensive screen-space effect passes
    // (planar/ground reflection re-render, SSAO resolve) -- effectScale of
    // the view resolution, clamped in init(). The main scene, the geometry
    // prepass and the water depth prepass stay full-res.
    float effectScale = 1.0f;
    uint16_t effW = 0;
    uint16_t effH = 0;
    // SSAO resolve resolution (Render_SSAOResolution), independent of
    // effectScale; the AO gen/blur targets and view rects use it.
    float ssaoScale = 1.0f;
    uint16_t ssaoW = 0;
    uint16_t ssaoH = 0;
    bgfx::FrameBufferHandle bgfxFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMesh = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstOit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstOitTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progFlat = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progFlatClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLine = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLineClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLinePat = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLinePatClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPoint = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPointClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshTexClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitTexClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texBlendColor = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_lineQuadVb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_lineQuadIb = BGFX_INVALID_HANDLE;
    bool m_instancing = false;
    bgfx::UniformHandle u_matColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matSpecular = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_polyOffset = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_instParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clipParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clipPlanes = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_linePattern = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle oitAccum = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle oitReveal = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle oitFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progComp = BGFX_INVALID_HANDLE;
    /// Debug buffer-visualization blit (docs/RenderDebug.md).
    bgfx::ProgramHandle m_progDebug = BGFX_INVALID_HANDLE;
    /// x = view mode, y = 1/max linear view depth (depth normalization),
    /// z = shadow-state valid this frame, w = scene shadow map size in
    /// texels (the mode-7 filtering probe).
    bgfx::UniformHandle u_debugParams = BGFX_INVALID_HANDLE;
    /// Debug scene re-render (modes 6/8): the counting/UV rasterization
    /// of the scene fills into their own full-res target, read back by
    /// the visualization blit.
    bgfx::ProgramHandle m_progDebugScene = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progDebugSceneClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texDebugScene = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle debugSceneTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle debugSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle debugSceneFbo = BGFX_INVALID_HANDLE;
    uint16_t debugSceneW = 0;
    uint16_t debugSceneH = 0;
    bgfx::UniformHandle s_texAccum = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texReveal = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCap = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCapClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texHatch = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_blackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_hatchTex = BGFX_INVALID_HANDLE;
    uint64_t m_hatchVersion = 0;   // Private's hatch pixel generation
    static constexpr int kAOSamples = 16;
    bgfx::TextureHandle aoNormalZ = BGFX_INVALID_HANDLE;
    bool aoNormalZFp16 = true;   // prepass depth precision (see init)
    bgfx::TextureHandle aoDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoBlurTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoNoiseTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoPrepassFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoGenFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoBlurFbo = BGFX_INVALID_HANDLE;
    // GTAO prefiltered depth pyramid (XeGTAO depth MIP chain): six
    // successively halved single-channel viewZ levels below full res,
    // each its own texture+framebuffer (rendering into mip N of one
    // texture while sampling mip N-1 is a GL feedback hazard). Six
    // levels keep the per-tap level pick (one level per octave beyond
    // ~10px) cache-coherent out to the pixel radius cap — with fewer,
    // taps past the coarsest level stride sparsely through it and the
    // AO pass turns memory-bound as the camera zooms in.
    static constexpr int kAOMipLevels = 6;
    bgfx::TextureHandle aoMipTex[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle aoMipFbo[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::UniformHandle s_texAOMip[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle m_progGtaoDepth = BGFX_INVALID_HANDLE;
    int aoMipCount = 0;   // 0 = no pyramid (R32F/R16F not renderable)
    bgfx::ProgramHandle m_progPrepass = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPrepassClip = BGFX_INVALID_HANDLE;
    // Medium (water/glass/cloud/fire) interval depth writers: the
    // prepass layout with the body's appearance slot stamped into .x
    // (fs_fc_meddepth); u_mediumSlot.x carries the slot per submit.
    bgfx::ProgramHandle m_progMedDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMedDepthClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mediumSlot = BGFX_INVALID_HANDLE;
    // Appearance slots per medium kind: distinct bodies get their own
    // parameter slot (uniform array entry), looked up per pixel via
    // the slot index in the interval targets. Must match MEDIUM_SLOTS
    // in fc_volume.sh.
    static constexpr int kMediumSlots = 4;
    // Local effect lights (LOCAL_LIGHTS in fc_mesh_fs.sh): the first
    // kMediumSlots entries carry the fire body flame lights, the rest
    // the light-source bodies (Render_Light bulbs).
    static constexpr int kLocalLights = 2 * kMediumSlots;
    bgfx::ProgramHandle m_progPrepassInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGtao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGtaoBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsaoBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormalZ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAONoise = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAO = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoKernel = BGFX_INVALID_HANDLE;
    // Screen-space cavity (curvature) shading: one fullscreen multiply
    // reading the same prepass the AO chain reads. u_cavityParams:
    // x = valley strength, y = ridge strength, zw = prepass texel size.
    bgfx::ProgramHandle m_progCavity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cavityParams = BGFX_INVALID_HANDLE;
    // PBR image based lighting: a fixed procedural studio environment
    // built once on demand — a GGX-prefiltered cubemap mip chain for the
    // specular part and its irradiance SH for the diffuse part.
    static constexpr int kEnvSH = 9;
    static constexpr uint16_t kEnvSize = 64;
    bgfx::TextureHandle m_envTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_dummyEnvTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEnv = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pbrParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matcapParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envSH = BGFX_INVALID_HANDLE;
    float envSH[kEnvSH][4];
    /// User environment image the built cubemap came from (null = the
    /// procedural studio environment); a change invalidates the build.
    std::shared_ptr<const Render::TextureImage> m_envImage;
    bool m_envBuilt = false;   // build attempted (m_envTex may still be
                               // invalid when the caps disallow it)
    bool pbrFrame = false;     // PBR active for the frame being submitted
    // Matcap shading for the frame being submitted: a global shading
    // mode, so it rides the view rather than the per-draw material.
    bool matcapFrame = false;
    int matcapPreset = 0;
    float matcapTint = 0.0f;
    float pbrMetallic = 0.0f;
    float pbrRoughness = 0.0f; // <= 0: derive from the material shininess
    float pbrEnvIntensity = 1.0f;
    bgfx::UniformHandle s_texBump = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bumpParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texOcclusion = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texMetallicRoughness = BGFX_INVALID_HANDLE;
    float bumpScale = 1.0f;    // bump/normal map strength (BumpConfig)
    bool bumpParallax = true;  // parallax-occlusion map height maps
    // Variance shadow map of the Shadow draw style's scene light.
    // Coin sizes its shadow map as precision * min(2048, max texture
    // size) (ShadowPrecision, default 1.0 -> 2048); the targets are
    // (re)created for the requested size by ensureShadowTargets.
    static constexpr uint16_t kShadowMaxSize = 2048;
    uint16_t shadowSize = 0;
    bgfx::TextureFormat::Enum shadowFormat = bgfx::TextureFormat::RG32F;
    bool m_shadow = false;     // shadow resources exist (caps allow it)
    // The reduced RG16F moment path (float32 not linearly filterable) always
    // runs the EVSM warp -- fp16 plain (z, z^2) moments are too imprecise.
    bool m_shadowForceWarp = false;
    float shadowWarp = 42.0f;  // EVSM exponent (by moments format)
    // Per-frame shadow lookup state: warp 0 = plain VSM (Coin
    // parity, SmoothBorder 0), else the EVSM exponent above;
    // epsilon/threshold are Coin's VsmLookup parameters.
    float shadowWarpFrame = 0.0f;
    float shadowEpsilon = 1.0e-5f;
    float shadowThreshold = 0.0f;
    // Coin's N-tap receiver spread kernel (ShadowSpreadSize /
    // SpreadSampleSize), packed into u_evsm.zw: tap spacing in shadow
    // map uv and the kernel mode (0 off, 1 dithered 4-tap, N >= 3 an
    // N x N grid).
    float shadowSpreadUv = 0.0f;
    float shadowSpreadMode = 0.0f;
    bool shadowFrame = false;  // shadows active this frame
    bgfx::TextureHandle shadowTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowTintTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowTintBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintBlurBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowTint = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texShadowTint = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowFbo = BGFX_INVALID_HANDLE;
    // ShadowSmoothBorder blur ping texture and the two color-only
    // framebuffers of the separable passes (the back one re-targets
    // shadowTex).
    bgfx::TextureHandle shadowBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowBlurBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadow = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texShadow = BGFX_INVALID_HANDLE;
    // Screen-space AO sampler of the mesh programs (unit 9) and the
    // frame's AO chain result to bind there — invalid when AO is off
    // this frame (the white stand-in binds instead). Set by render()
    // before the scene submits; the AO views run before ViewOpaque, so
    // the opaque pass samples this frame's result.
    bgfx::UniformHandle s_texAOScreen = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoMeshTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightPos = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_evsm = BGFX_INVALID_HANDLE;
    float lightDirView[3] = {0.0f, 0.0f, -1.0f}; // view space
    // rgb = color * intensity; w = the spot falloff exponent
    // (dropOffRate * 128; 0 for directional lights, pow(x, 0) = 1).
    float lightColorI[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    // Spot light position in view space; w = cos(cutOffAngle) for a
    // spot light, -1 for a directional one (the shader switch).
    float lightPosView[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    // Fire body effect lights (unshadowed flickering point lights at
    // the flame centroids, one entry per fire appearance slot): xyz =
    // position in view space, w = 1 / range^2 (0 = slot inactive this
    // frame). The colors arrive premultiplied by intensity and the
    // animation-clock flicker.
    bgfx::UniformHandle u_localLight = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_localLightColor = BGFX_INVALID_HANDLE;
    float localLightView[kLocalLights][4] = {};
    float localLightColorI[kLocalLights][4] = {};
    float shadowMtx[16];       // camera view space -> shadow uv/depth
    // Cached shadow map: hash of the light camera + caster set of the
    // moments currently in shadowTex; the caster pass (and blur) only
    // re-runs when it changes. 0 = nothing rendered yet.
    uint64_t shadowMapHash = 0;
    // AO/prepass cache key: camera + viewport + AO params + prepass draw
    // set (see the aoRender hash in render()); 0 = never cached.
    uint64_t aoMapHash = 0;
    // Camera+viewport of the previous frame (medium-interval and
    // planar-reflection frame cache — see staticFrame in render()).
    uint64_t camFrameHash = 0;
    bool m_ssao = false;     // SSAO resources exist (caps allow it)
    // Volumetric light shafts: half-res raymarch of the shadow map
    // bounded by the prepass depth, so both resource sets must exist.
    bool m_vol = false;      // volumetric resources exist
    bgfx::TextureHandle volTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volHistTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volHistFrontTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle volFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle volHistFbo = BGFX_INVALID_HANDLE;
    /// Consecutive static frames feeding the volumetric temporal
    /// accumulation (0 = replace history this frame).
    int volAccumFrames = 0;
    /// User volume-stage medium splice (docs/RenderEngine.md §5.11):
    /// synthetic shaders assembled per frame from the fire and cloud
    /// slots bound to a user medium function — variants of the
    /// volumetric raymarch / extinction / reflection-media programs
    /// with the user field/ramp (fire channel) and scatter density
    /// (cloud channel) dispatched for those slots. Null = all media
    /// stock. volUserKey tracks the slot-source tuple (fire slots then
    /// cloud slots) so the strings only reassemble when a binding
    /// changes.
    std::shared_ptr<const Render::UserShader> volUserVol;
    std::shared_ptr<const Render::UserShader> volUserExt;
    std::shared_ptr<const Render::UserShader> volUserRefl;
    std::array<const void *, 8> volUserKey {};
    /// Compiler-less tier only: fingerprint of the splice table the
    /// current variants were adopted from, and whether that adoption
    /// found binaries for all of them. The server compiles the viewer
    /// binaries asynchronously, so the snapshot that first carries a
    /// binding can carry no binaries with it; a half-adopted variant
    /// is retried when a republished table arrives rather than on
    /// every frame, which would reassemble the sources forever if the
    /// compile never succeeds.
    size_t volSpliceFingerprint = 0;
    bool volUserAdopted = true;
    bgfx::ProgramHandle m_progVol = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolApply = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolAccum = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progReflMedia = BGFX_INVALID_HANDLE;
    // Bloom (glow) chain: quarter-res halo source + blur ping target.
    bgfx::TextureHandle bloomTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bloomBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomBright = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomEmit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomApply = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texBloom = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomTexel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSun = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sunParams = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progEnvBg = BGFX_INVALID_HANDLE;
    // Bulb (Render_LightShadow) shadow atlas: 2x2 tiles of plain VSM
    // moments, one per bulb light slot; a tile re-renders only when its
    // hash (bulb + casters) changes.
    /// Atlas tiles (4x4 grid): a plain-shadow bulb uses one downward
    /// tile, an extended (Render_LightShadowExtended) bulb six cube
    /// faces; tiles allocate sequentially per frame.
    static constexpr int kBulbShadowTiles = 16;
    static constexpr int kBulbShadowGrid = 4;
    static constexpr uint16_t kBulbShadowTileSize = 512;
    bgfx::TextureHandle bulbShadowTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bulbShadowDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bulbShadowFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texBulbShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bulbShadowMtx = BGFX_INVALID_HANDLE;
    /// Camera-view-space -> atlas uv/depth per tile, refreshed every
    /// frame (the camera moves); identity-w row zeroed when inactive.
    float bulbShadowMtx[kBulbShadowTiles][16] = {};
    /// Light camera per tile for the caster views (world space).
    float bulbShadowViewMtx[kBulbShadowTiles][16];
    float bulbShadowProjMtx[kBulbShadowTiles][16];
    uint64_t bulbShadowHash[kBulbShadowTiles] = {};
    bool bulbShadowValid[kBulbShadowTiles] = {};
    /// Per bulb slot: x = first atlas tile, y = tile (face) count —
    /// 1 = plain downward cone, 6 = cube faces.
    float bulbShadowConf[kLocalLights - kMediumSlots][4] = {};
    /// Camera view -> world rotation for the shader's cube-face pick
    /// (directions only, translation ignored via w = 0).
    float bulbShadowRotMtx[16] = {};
    bgfx::UniformHandle u_bulbShadowConf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bulbShadowRot = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texVolFront = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolExt = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCaustics = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texVol = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volMedium = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_causticParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volTexel = BGFX_INVALID_HANDLE;
    // Water medium: front/back depth targets of water body draws
    // (prepass shader family) bounding the underwater ray stretch.
    bgfx::TextureHandle waterFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle waterFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle waterBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texWaterFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texWaterBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSigma = BGFX_INVALID_HANDLE;
    // Glass body: front/back depth targets bounding the absorption
    // interval of the glass surface pass.
    bgfx::TextureHandle glassFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle glassFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle glassBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGlass = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texGlassFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texGlassBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_glassParams = BGFX_INVALID_HANDLE;
    // Cloud body: front/back depth targets bounding the FBM medium
    // interval of the volumetric raymarch.
    bgfx::TextureHandle cloudFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle cloudFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle cloudBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texCloudFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texCloudBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cloudParams = BGFX_INVALID_HANDLE;
    // Fire body: front/back depth targets bounding the emissive flame
    // interval of the volumetric raymarch.
    bgfx::TextureHandle fireFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fireFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fireBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texFireFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texFireBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireFrame = BGFX_INVALID_HANDLE;
    // Water surface refraction (scene copy) + ground reflection targets.
    bgfx::TextureHandle sceneCopyTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle sceneCopyFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle reflTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle reflDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle reflFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progWaterCopy = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progWater = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGroundRefl = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texScene = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texRefl = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSurf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterAbsorb = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterRipple = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSplash = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fountainParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fountainFrame = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_reflParams = BGFX_INVALID_HANDLE;
    // Stateful particles (docs/RenderEngine.md §5.8): the state
    // samplers and the step uniforms, plus the stock reset program.
    // Created once with the view, not with the size-dependent targets.
    bgfx::ProgramHandle m_progPSimInit = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pstate0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pstate1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pgrid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pboxMin = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pboxMax = BGFX_INVALID_HANDLE;
    /// RGBA32F is renderable on this backend — WebGL2 only has it with
    /// EXT_color_buffer_float, which bgfx reports through the format
    /// caps. Without it there is nowhere to keep particle state and
    /// stateful emitters fall back to their stateless vertex stage.
    bool particleStateOk = false;
    /// Particle impact map (docs/RenderEngine.md §5.8): one RGBA32F
    /// texel per cell of the water's world footprint holding the most
    /// recent hit there — xy = where, z = when, w = how hard. Written
    /// by the splat pass, read by the water surface, and deliberately
    /// never cleared: a ring outlives by far the step that started it,
    /// and an expired record ages out on its own.
    bgfx::ProgramHandle m_progPImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pimpsrc = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_impactFrame = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_impactNow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterImpactCfg = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impactTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle impactFbo = BGFX_INVALID_HANDLE;
    /// xy = world min corner of the map's footprint, zw = 1 / extent.
    float impactFrame[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    /// The clock the map's records are stamped against — the raw
    /// animation seconds, not the wave clock, so the wave speed can be
    /// tuned without ageing every ring that is already travelling.
    float impactNow = 0.0f;
    /// The map holds this frame's impacts and the water surface may
    /// read it.
    bool impactActive = false;
    /// An emitter reset this frame, so its history is being replayed
    /// from the start and what it struck on the way there is no longer
    /// about anything. Clearing on that is what keeps a frozen frame a
    /// pure function of the warm-up with water in the scene, the same
    /// as it is without.
    bool impactReset = false;
    /// Cells across the map. Rings die two cells out (the water
    /// surface's 5x5 neighbourhood), so this is what sets their size
    /// relative to the pool: coarser cells make bigger, longer rings
    /// and lose more of them to hits sharing a cell.
    static constexpr uint16_t kImpactRes = 24;
    /// A stateful emitter drew this frame and its simulation is running
    /// (not frozen). Unlike the step budget's "still owes steps", this
    /// stays true once an emitter has caught up with the clock, which
    /// is exactly when its sprites keep moving — what the mirrored and
    /// cached passes have to follow.
    bool particlesLive = false;
    // Redirect submit() into the ground reflection view (mirrored
    // camera, flipped culling).
    bool reflPass = false;
    /// Clip the mirror pass to the half-space above its plane, and the
    /// world-space plane equation to clip against (dot(pos, p) >= 0
    /// keeps the fragment, so z >= planeZ is (0,0,1,-planeZ)).
    bool reflClip = false;
    float reflClipPlane[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    bool m_oit = false;      // OIT resources exist (caps allow it)
    bool oitFrame = false;   // OIT active for the frame being submitted
    /// Whether the water surface was last seen with nothing for its
    /// wave normals to act on, so the report below is made when that
    /// state is entered rather than on every frame it persists.
    bool waterNoResponse = false;
    std::unordered_map<uint64_t, GpuMesh> meshes;
    /// Shared colorless geometry buffers keyed by content; GpuMesh::geom
    /// points into this map (values are node-stable).
    std::unordered_map<GeomKey, GpuGeometry, GeomKeyHasher> geometries;
    /// Shared all-white color stream of meshes without baked colors.
    bgfx::VertexBufferHandle whiteColorVb = BGFX_INVALID_HANDLE;
    int whiteColorCount = 0;
    std::unordered_map<uint64_t, GpuTexture> textures;
    uint64_t frame = 0;
    int drawcount = 0;
    // Standalone (WebGL2) one-shot warmup: the very first MSAA scene
    // framebuffer created while bgfx's async WebGL2 init is still settling
    // renders the overlay views wrong (a stale-target artifact — the corner
    // overlay draws the main scene). Re-creating the target once, a few frames
    // after the scene is first present, clears it for good: counts frames with
    // a scene on screen, then sets -1 once the rebuild has fired, so it costs
    // one re-create per view and not one per streamed arrival. (Regression:
    // 6065ad06ed dropped the interaction-triggered rebuild that masked this.)
    int warmup = 0;
    bool ontop = false;   // route submits to the highlight pass
    bool selPass = false; // route opaque-view submits into ViewSelection
                          // (non-on-top selection draws follow the opaque
                          // scene in submission order, GL pass parity)
    int overlayView = -1; // >= 0: route submits into this overlay view
    // Anchor + rect pixel height of the overlay currently being submitted (set
    // alongside overlayView); billboard text sizes itself against the overlay's
    // own mini projection + rect instead of the main scene view.
    const Render::OverlayAnchor *overlayAnchor = nullptr;
    float overlayRectHeight = 0.f;
    int msaaSamples = 0;  // sample count the current targets were built with
    int shaderGen = 0;    // _BGFXLib.shaderGeneration the programs were
                          // loaded at; a lag re-inits the view (hot-reload)
    // The last init() could not load a program the view cannot draw
    // without (a missing or stale stock shader pack). The view stays
    // torn down and every frame reports failure -- on the desktop that
    // is the Coin fallback -- until the generation moves, so a
    // reloadShaders() over a repaired pack recovers.
    bool shaderFailed = false;
    // Set with it: the abandoned init's queued bgfx commands still
    // need one frame to execute (see the frame path's bail).
    bool shaderFailedDrain = false;
    // Per-frame world-to-screen scale consumed by autozoom draws
    // (Renderer::setAutoZoomScale).
    float autozoomScale = 1.0f;
    // Current frame's view/projection matrices (GL-layout), for billboard
    // autozoom draws (SoTextImage): the screen-aligned basis is read from the
    // view matrix and the screen-constant size from the anchor's own view
    // depth + projection in setDrawTransform. Null outside a frame -> billboard
    // falls back to plain scaling.
    const float *viewMatrix = nullptr;
    const float *projMatrix = nullptr;
#ifdef FC_RENDERER_STANDALONE
    bgfx::ProgramHandle m_progPresent = BGFX_INVALID_HANDLE;
#else
    GLuint fbo = 0;
    GLuint fboDepth = 0;
    GLuint blitColorId = 0;
    bool hasFBO = false;
#endif
};

class BGFXRenderer::Private
{
public:
    Private(QOpenGLWidget *widget, bool publishOnly = false)
        :widget(widget), publishOnly(publishOnly)
    {
        _BGFXLib.renderers.insert(this);
    }

    ~Private()
    {
        _BGFXLib.renderers.erase(this);
        deinit();
    }

    void deinit()
    {
        _deinit = true;
        // A publish-only renderer never asked for a view, so there is
        // none to remove -- and asking would be the one call that
        // brings the graphics device into a process that has none.
        if (!publishOnly)
            _BGFXLib.removeView(widget);
    }

    /// Assemble the CPU-side snapshot of everything the feeds hold:
    /// the draws, the selections and highlight, the overlays and every
    /// per-frame config, plus the camera and viewport the frame was
    /// asked for. Touches no GPU state -- the frame dump and the scene
    /// publish are both built from this, and a publish-only process
    /// (docs/HeadlessServe.md) never gets past it.
    void makeSnapshot(Render::SceneSnapshot &snap,
                      const void *viewMatrix,
                      const void *projMatrix,
                      uint16_t width,
                      uint16_t height,
                      uint32_t clearColor);

    /// A cheap identity of what the feeds currently hold — which draws,
    /// of what, over which geometry. Two consecutive frames with the
    /// same one are a scene that has stopped arriving; that is the
    /// condition a dump meant for comparison waits on
    /// (FC_BGFX_DUMP_SCENE_SETTLE). It deliberately reads identity
    /// (cacheId, objectKey) rather than content: the point is to notice
    /// that something changed, and a counter that moves on every
    /// re-tessellation notices more, not less.
    uint64_t sceneFingerprint() const;

    /// FC_BGFX_DUMP_SCENE=<path>: snapshot the first non-empty scene
    /// feed with all per-frame configs and the camera for the
    /// standalone/wasm viewer (SceneDump.h).
    /// FC_BGFX_DUMP_SCENE_DELAY=<n> skips the first n non-empty
    /// frames, and FC_BGFX_DUMP_SCENE_SEL=1 additionally waits for a
    /// non-empty selection feed, so later state (a selection made by
    /// a script) is in the capture.
    /// FC_BGFX_DUMP_SCENE_SETTLE=<n> waits for the feeds to stop
    /// changing for n consecutive frames first, which is what a dump
    /// meant for comparison wants: a document does not arrive all at
    /// once (objects are added, tessellated, recoloured, and a
    /// coarse-first load refines rungs afterwards), so a dump taken a
    /// fixed number of frames in captures how far the build had got.
    /// Measured on a 40-object scene: three runs of one script, three
    /// different scenes -- 2159580, 2310620 and 2454636 bytes. Nothing
    /// downstream can tell that from a real difference, because a
    /// bundled dump carries no level information at all. Not for an
    /// animated scene, which never settles by construction.
    ///
    /// Written with no chunk/mesh/texture sinks installed, so the file
    /// is monolithic and self-contained — unlike a published manifest,
    /// which is content-keyed and delta-encoded per viewer. That makes
    /// it the comparable form of a scene, and so the way one publisher
    /// is diffed against another (docs/HeadlessServe.md §4, stage 2c).
    /// Hence it hangs off the snapshot, not off the frame: a
    /// publish-only process draws nothing and must still be able to
    /// produce one.
    void maybeDumpScene(const void *viewMatrix,
                        const void *projMatrix,
                        uint16_t width,
                        uint16_t height,
                        uint32_t clearColor,
                        bool dirtyChanged);

#ifndef FC_RENDERER_STANDALONE
    /// Publish the feeds to the scene-stream server when they have
    /// changed (docs/SceneStreaming.md). Starts the server on first
    /// call. Like makeSnapshot() this is CPU work alone: it is
    /// deliberately separable from the frame it currently rides on, so
    /// a source with no 3D view can drive it (docs/HeadlessServe.md
    /// §3.1).
    void publishScene(const void *viewMatrix,
                      const void *projMatrix,
                      uint16_t width,
                      uint16_t height,
                      uint32_t clearColor,
                      bool dirtyChanged);

    /// The whole of what a publish-only renderer does
    /// (docs/HeadlessServe.md §3.1): consume the pending feed change
    /// and put the snapshot on the wire. Everything render() does
    /// besides this needs a graphics device, and this process has
    /// none -- so unlike render(), which reaches the same publish on
    /// its way past a live view, there is nothing here to skip.
    bool publishNoDraw(const QColor &col,
                       const void *viewMatrix,
                       const void *projMatrix,
                       uint16_t width,
                       uint16_t height);
#endif

    bool render(const QColor &col,
                const void * viewMatrix,
                const void * projMatrix);

    /// GL's SectionFillInvert color transform (_renderSection ~1838):
    /// invert each channel, mapping the mid-gray band to 180 and
    /// near-black results to 50; the alpha stays.
    static uint32_t invertCapColor(uint32_t col);

    /// Build the world-space cap quad of one section plane over the
    /// given bounds, replicating _renderSection's geometry: a square of
    /// the bounding sphere's radius centered on the bbox center
    /// projected onto the plane, oriented by Coin's z-to-normal
    /// rotation, with the hatch texture coordinates scaled from the
    /// world-to-pixel ratio at mid view depth.
    void buildCapQuad(const float plane[4], const float bmin[3],
                      const float bmax[3], const float *projMat,
                      int vpWidth, CapVertex verts[4]) const;

    /// Stencil section caps of clipped solids (GL: renderSection /
    /// renderSectionGrouped / _renderSection). For every eligible draw
    /// (whole solid triangle geometry with clip planes) and every one of
    /// its section planes: parity-mark the solid clipped by that plane
    /// alone, fill the cap quad where marked (clipped by the remaining
    /// planes outside concave mode), and clean the stencil up again.
    /// SectionFillGroup caps runs of same-colored draws together like
    /// GL, turning intersecting same-material solids into one boolean
    /// cut.
    void submitSectionCaps(BGFXView *view, const float *projMat);

    void updateBBox();

    QOpenGLWidget *widget;
    /// This renderer will never draw (docs/HeadlessServe.md §3.1): no
    /// bgfx view, no graphics device, no display. render() refuses;
    /// publishNoDraw() is the whole of what it does.
    bool publishOnly = false;
    /// The server document group this renderer publishes into; empty =
    /// the default group (Renderer::setPublishGroup).
    std::string publishGroup;
    bool _deinit = false;
    RendererType::Enum type;
    std::string typeName;

    // CPU-side scene data fed through Render::Renderer's scene API. GPU
    // upload happens lazily during render(), so the feed may arrive before
    // bgfx is initialized.
    Render::DrawCallList scene;
    /// Draw identity resolved by the producer (setObjectInfo); consulted
    /// by the snapshot writer for the published object entries.
    Render::ObjectInfoMap objectInfo;
    // Cross-object instance groups of the scene feed: draws sharing one
    // geometry content (by hash — a shared cache OR coincidentally
    // identical flattened caches), index range and material (diffuse
    // aside — links may override colors) collapse into one instanced
    // submit per frame. Rebuilt with the scene; the per-frame loop still
    // skips hidden members and falls back to per-draw submits when a
    // group thins out.
    struct InstGroup { std::vector<int> members; };
    std::vector<InstGroup> instGroups;
    std::vector<int> instGroupOf;   ///< scene index -> group index or -1

    /// Content identity of one mesh cache: the colorless geometry hash
    /// (computeGeomKey) plus a hash of the per-vertex color stream.
    /// Computed once per (cache id, refine generation) — so scene
    /// rebuilds only hash caches they have not seen before, and an
    /// in-place ladder refine re-hashes. Two caches with equal hashes
    /// render identically
    /// through one prototype: the geometry table already shares their
    /// GPU buffers, and equal color hashes mean the baked color streams
    /// match byte for byte.
    struct MeshContent {
        uint64_t geomHash;
        uint64_t colorHash;
        int numVertices;
        int numTri;
        uint32_t generation;
        uint64_t stamp;
    };
    std::unordered_map<uint64_t, MeshContent> meshContents;
    uint64_t meshContentStamp = 0;

    const MeshContent &meshContent(const Render::MeshData &mesh);

    /// A draw the instanced mesh path can express: an unclipped,
    /// non-on-top, non-water triangle draw without autozoom. Textured
    /// draws qualify (the texture identity joins the group key);
    /// transparent draws qualify too but only batch on WBOIT frames
    /// (submitInstanced falls back otherwise — sorted transparency
    /// needs per-draw depth keys). (Hidden-line frames disable
    /// instancing wholesale, so the outline material flag stays out of
    /// the picture.)
    static bool instancableDraw(const Render::DrawCall &d);

    void buildInstanceGroups();

    Render::Background background;
    std::map<int, Render::DrawCallList> selections;
    // Overlay feeds keyed by producer id (Renderer::setOverlay); map
    // order assigns the (limited) overlay view slots deterministically.
    struct OverlayFeed {
        Render::DrawCallList draws;
        Render::OverlayAnchor anchor;
    };
    std::map<int, OverlayFeed> overlays;
    Render::DrawCallList highlight;
    std::unordered_set<uint64_t> hiddenKeys;
    std::unordered_set<const Render::DrawCall *> dupDraws;
    Render::HiddenLineConfig hlconfig;
    Render::SectionConfig secconf;
    Render::AOConfig aoconf;
    Render::CavityConfig cavityconf;
    Render::MatcapConfig matcapconf;
    Render::PBRConfig pbrconf;
    Render::BumpConfig bumpconf;
    Render::LightConfig lightconf;
    Render::VolumetricConfig volconf;
    Render::WaterConfig waterconf;
    Render::BloomConfig bloomconf;
    Render::RenderDebugConfig debugconf;
    Render::UserShaderConfig usershaderconf;
    /// Snapshot of _BGFXLib.userCompileGeneration taken by render();
    /// isSceneDirty() reports dirty while they differ (async compile).
    int userShaderGen = 0;
    Render::PreselHighlightConfig preselconf;
    Render::PreselHighlightConfig selconf;
    float autozoomScale = 1.0f;
    // CPU copy of the section hatch texture, expanded to RGBA8; the
    // version stamps GPU re-uploads (null = no image). Held as a
    // TextureImage so the stream can carry it in the texture table,
    // content-keyed and served out of band (SceneDump.h, v27) — the
    // key is then hashed once per hatch change, not per publish.
    std::shared_ptr<Render::TextureImage> hatchTex;
    const void *hatchKey = nullptr;
    /// Streamed mesh chunks by cacheId: content key and chunk size, for
    /// the publish in flight and the one before it (SceneDump.h,
    /// MeshBlobSink). Sound because a cacheId names one content for the
    /// life of the process, so a hit needs no re-hash; bounded because
    /// an entry only survives while the blob it names is still served.
    std::map<uint64_t, std::pair<std::string, uint32_t>> meshKeys;
    std::map<uint64_t, std::pair<std::string, uint32_t>> meshKeysPrev;
    /// The object list of the last publish, ordered by objectKey. The
    /// difference against it is what the server remembers per publish,
    /// and it is the publisher that has to keep it: the server holds
    /// bytes, not scenes.
    std::vector<Render::SceneSnapshot::ObjectEntry> publishedObjects;
    uint64_t hatchVersion = 0;
    bool hlWholeOnTop = false;
    /// One-shot frame capture (requestFrameDump), consumed by the next
    /// frame's blit; lastStats keeps that readback's statistics.
    Render::FrameDumpRequest pendingDump;
    bool dumpPending = false;
    Render::RenderStats lastStats;
    bool sceneDumped = false;   ///< FC_BGFX_DUMP_SCENE fired
    /// Frames since the feeds last changed, and the fingerprint that is
    /// measured against, for FC_BGFX_DUMP_SCENE_SETTLE.
    int dumpQuietFrames = 0;
    uint64_t dumpFingerprint = 0;
    int dumpFrames = 0;         ///< non-empty frames seen (dump delay)
    bool serveStarted = false;  ///< FC_BGFX_SERVE_SCENE start attempted
    bool scenePublished = false;///< at least one payload published
    /// SceneStreamServer::levelsBuilt() as of the last publish. The
    /// counter moving is what a finished level-generation job looks
    /// like from here, and the response is a republish — that is the
    /// announcement (§7, phase 5c), there is no other channel.
    size_t publishedLevelsBuilt = 0;
    bool sceneDirty = false;
    // Whether the last rendered frame contained time-animated content
    // (fire/cloud/water/caustics) — see the assignment in render().
    // Defaults to true so a client's idle skip never engages before the
    // first frame has classified the scene.
    bool sceneAnimated = true;
    // Distinct from sceneDirty: set ONLY when the scene geometry feed itself
    // changes (setScene), not on cheap per-frame state (AO toggle, selection
    // /preselect highlight, effect resolution). The STANDALONE warmup target
    // rebuild keys off this — re-arming it on every sceneDirty made every
    // highlight/AO toggle trigger a full view->init() (a hi-DPI multisampled
    // target realloc = a WebGL stall), felt as a hitch at drag start and on
    // any preselection.
    bool feedDirty = false;
    bool hasScene = false;
    bool renderOk = false;

    /// Does anything read the pixels this view draws?
    ///
    /// For a serving process (FC_BGFX_SERVE_SCENE): no, and not just
    /// while no viewer is connected. What it puts on the wire is a
    /// scene *snapshot*, re-sent when the scene changes rather than per
    /// frame, and its viewers rasterize and animate it themselves off
    /// their own clock. Its own window is never in that path, connected
    /// viewers or not -- so every pixel it rasterizes is read by
    /// nobody, and under Xvfb (llvmpipe) read by nobody at the cost of
    /// several cores.
    ///
    /// Same view of the local window the level planner already takes
    /// ("a serving process's own window never plans -- its viewers'
    /// cameras decide"). Off the serving path this is always true:
    /// there the window IS the audience.
    ///
    /// FC_BGFX_SERVE_DRAW=1 puts the window back in the picture, for
    /// serving from a desktop session where somebody is in fact
    /// watching it.
    ///
    /// Two things follow: an animated scene stops asking for frames
    /// (BGFXRenderer::animating), and the frames that do happen -- a
    /// scene change, a viewer's hello -- stop at the publish (the
    /// publish-only exit in render()).
    bool localAudience() const
    {
#ifndef FC_RENDERER_STANDALONE
        static const char *servePort = getenv("FC_BGFX_SERVE_SCENE");
        static const bool serveDraw =
            getenv("FC_BGFX_SERVE_DRAW") != nullptr;
        if (servePort && *servePort && !serveDraw)
            return false;
#endif
        return true;
    }
    bool bboxValid = false;
    // The last rendered frame splatted animated water caustics: the
    // viewer keeps redrawing while set so the animation advances.
    bool animatedFrame = false;
    float bboxMin[3], bboxMax[3];

#ifndef FC_RENDERER_STANDALONE
    // The desktop level plan's event half (§13 step 2): fed the camera
    // every rendered frame, fires the plan pass ~300ms after it
    // settles somewhere new. The policy half is planMeshRefines.
    Render::MeshLevelPlanner levelPlanner;
    // The last memory-ceiling epoch this view replanned for (§13
    // step 3) — a new observation marks the planner dirty.
    uint64_t levelCeilingSeen = 0;
    // GPU geometry budget (setGpuMemoryBudget); 0 = automatic.
    size_t gpuBudget = 0;
    // Whether the last rendered frame stood over the GPU budget — the
    // crossing is what wakes the planner.
    bool gpuOverBudget = false;

    /// GPU geometry bytes in use: the API's own number where it
    /// reports one, else the upload accounting.
    static size_t gpuUsedBytes()
    {
        const bgfx::Stats *stats = bgfx::getStats();
        if (stats && stats->gpuMemoryUsed > 0)
            return size_t(stats->gpuMemoryUsed);
        return s_gpuGeometryBytes.load();
    }

    /// The effective GPU budget: the explicit one, else the API's
    /// reported maximum, else none.
    size_t gpuBudgetBytes() const
    {
        if (gpuBudget)
            return gpuBudget;
        const bgfx::Stats *stats = bgfx::getStats();
        if (stats && stats->gpuMemoryMax > 0)
            return size_t(stats->gpuMemoryMax);
        return 0;
    }
#endif
};

#endif // GUI_RENDERER_BGFXRENDERERP_H
