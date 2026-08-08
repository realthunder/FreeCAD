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
        for (int s = 0; s < nseg; ++s, d += 16) {
            int32_t ia = indices[s*2];
            int32_t ib = indices[s*2 + 1];
            d[0] = mesh.positions[ia*3];
            d[1] = mesh.positions[ia*3 + 1];
            d[2] = mesh.positions[ia*3 + 2];
            d[3] = 0.0f;
            d[4] = mesh.positions[ib*3];
            d[5] = mesh.positions[ib*3 + 1];
            d[6] = mesh.positions[ib*3 + 2];
            d[7] = 0.0f;
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
        fn(m_progSsaoApply, LifeSized);
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
        fn(u_aoKernel, LifeSized);
        fn(s_texEnv, LifeSized);
        fn(u_pbrParams, LifeSized);
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

    /// glPolygonOffset(factor, units) approximated as a constant NDC
    /// depth bias (no per-pixel slope term): one offset unit is 2 (NDC
    /// range) * 16 LSB headroom for the unevaluated slope factor / 2^24
    /// depth bits. Positive pushes away from the viewer.
    static float polygonOffsetBias(const Render::Material &mat);

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
    bgfx::ProgramHandle m_progSsaoApply = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormalZ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAONoise = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAO = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoKernel = BGFX_INVALID_HANDLE;
    // PBR image based lighting: a fixed procedural studio environment
    // built once on demand — a GGX-prefiltered cubemap mip chain for the
    // specular part and its irradiance SH for the diffuse part.
    static constexpr int kEnvSH = 9;
    static constexpr uint16_t kEnvSize = 64;
    bgfx::TextureHandle m_envTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_dummyEnvTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEnv = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pbrParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envSH = BGFX_INVALID_HANDLE;
    float envSH[kEnvSH][4];
    /// User environment image the built cubemap came from (null = the
    /// procedural studio environment); a change invalidates the build.
    std::shared_ptr<const Render::TextureImage> m_envImage;
    bool m_envBuilt = false;   // build attempted (m_envTex may still be
                               // invalid when the caps disallow it)
    bool pbrFrame = false;     // PBR active for the frame being submitted
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
                const void * projMatrix)
    {
        // A publish-only renderer has no view, no target and no device
        // (docs/HeadlessServe.md §3.1). Nothing below can run, and
        // getView() would try to create the very thing this mode
        // exists to avoid.
        if (publishOnly)
            return false;

        // The pending scene data (whatever its age) is consumed by this
        // frame; needsRedraw() reports false until new data arrives.
        // Two distinct "dirty" signals:
        //  - feedChanged (setScene only) gates the STANDALONE warmup target
        //    rebuild, so cheap per-frame state (AO toggle on drag, selection/
        //    preselect highlight) does NOT force a view->init() stall.
        //  - dirtyChanged (any change, incl. selection/highlight/config) gates
        //    the serve republish, so a click-selection round trip still
        //    streams to the viewer.
        const bool feedChanged = feedDirty;
        bool dirtyChanged = sceneDirty;
#ifndef FC_RENDERER_STANDALONE
        // A finished async user-shader compile is a change too: the
        // frame caches must refresh and — with the scene server up —
        // the snapshot must republish so viewers receive the freshly
        // compiled binaries (userShaderGen re-snapshots below, in the
        // post-pass setup).
        if (userShaderGen != _BGFXLib.userCompileGeneration)
            dirtyChanged = true;
#endif
        (void)feedChanged;
        feedDirty = false;
        sceneDirty = false;
        renderOk = false;

        if (_deinit)
            return false;

        auto view = _BGFXLib.getView(widget, type);
        if (!view)
            return false;

#ifdef FC_RENDERER_STANDALONE
        // Warmup rebuild: a few frames after the scene first appears,
        // force a single target re-create to clear the bad
        // first-target overlay artifact (see BGFXView::warmup).
        //
        // **Once per view, not once per feed change.** It was written
        // when a streamed scene was set exactly once, so re-arming on
        // every feed change cost one rebuild per load. A progressively
        // assembled scene sets the feed on every arrival
        // (docs/SceneStreaming.md §6), and a rebuild re-creates every
        // target the view owns — the MSAA scene buffer, the AO
        // pyramid, the shadow maps — so the same rule turned into
        // tens of full pipeline re-creates during a load, each one a
        // stall on the frame that was supposed to be showing progress.
        //
        // What the artifact needs is one rebuild once bgfx's async
        // WebGL2 init has settled, and a scene that has been on screen
        // for a few frames is exactly that. It cannot be the very
        // first frame with a scene, which is what "too early to
        // matter" meant for the bundled snapshot.
        bool warmupReinit = false;
        if (view->warmup >= 0 && !scene.empty() && ++view->warmup >= 3) {
            warmupReinit = true;
            view->warmup = -1;  // fired; never again for this view
        }
        if (_BGFXLib.standaloneWidth != view->width
                || _BGFXLib.standaloneHeight != view->height
                || _BGFXLib.standaloneSamples != view->msaaSamples
                || _BGFXLib.effectResolution != view->effectScale
                || _BGFXLib.ssaoResolution != view->ssaoScale
                || _BGFXLib.shaderGeneration != view->shaderGen
                || warmupReinit) {
            if (_BGFXLib.standaloneWidth != view->width
                    || _BGFXLib.standaloneHeight != view->height)
                bgfx::reset(_BGFXLib.standaloneWidth,
                            _BGFXLib.standaloneHeight,
                            BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY);
            view->init();
        }

        if (!bgfx::isValid(view->bgfxFbo))
            return false;
#else
        if (widget->width() != int(view->width)
                || widget->height() != int(view->height)
                || !bgfx::isValid(view->bgfxFbo)
                || _BGFXLib.effectResolution != view->effectScale
                || _BGFXLib.ssaoResolution != view->ssaoScale
                || _BGFXLib.shaderGeneration != view->shaderGen
                || (_BGFXLib.desktopSamples >= 0
                    && _BGFXLib.desktopSamples != view->msaaSamples))
            view->init();

        if (!bgfx::isValid(view->bgfxFbo)) {
            widget->makeCurrent();
            return false;
        }
#endif

        uint16_t width = view->width;
        uint16_t height = view->height;
        uint32_t clearColor = (uint32_t(col.red()) << 24)
            | (uint32_t(col.green()) << 16)
            | (uint32_t(col.blue()) << 8)
            | 0xff;
        if (getenv("FC_BGFX_DEBUG_CLEAR"))
            clearColor = 0xff0000ff;

        maybeDumpScene(viewMatrix, projMatrix, width, height, clearColor,
                       dirtyChanged);

#ifndef FC_RENDERER_STANDALONE
        // Desktop level plan (§13 step 2): feed the settle detector
        // this frame's camera; ~300ms after it stops somewhere new the
        // callback asks the registry to refine every coarse-first
        // source erring more than the tolerance on screen. A serving
        // process's own window never plans — its viewers' cameras
        // decide, and the registry arms no refine there anyway.
        if (!getenv("FC_BGFX_SERVE_SCENE") && viewMatrix && projMatrix) {
            // A new memory-ceiling observation replans promptly — and
            // stickily: from the first one on, every plan also demotes
            // what the camera would not miss (§13 step 3).
            const uint64_t ceiling = Render::MeshSourceRegistry::
                instance().memoryCeilingEpoch();
            if (ceiling != levelCeilingSeen) {
                levelCeilingSeen = ceiling;
                levelPlanner.markDirty();
            }
            // Crossing the GPU budget wakes the planner too (§13
            // step 3): the sweep itself runs in the plan callback.
            if (const size_t budget = gpuBudgetBytes()) {
                const bool over = gpuUsedBytes() > budget;
                if (over && !gpuOverBudget)
                    levelPlanner.markDirty();
                gpuOverBudget = over;
            }
            levelPlanner.observe(
                reinterpret_cast<const float *>(viewMatrix),
                reinterpret_cast<const float *>(projMatrix),
                [this]() {
                    const float h = float(widget->height()
                                          * widget->devicePixelRatioF());
                    auto tags = Render::planMeshRefines(
                        scene, levelPlanner.viewMatrix(),
                        levelPlanner.projMatrix(), h,
                        levelPlanner.tolerance());
                    auto &reg = Render::MeshSourceRegistry::instance();
                    // Demotions first (§13 step 3), and only ever under
                    // an observed CPU-memory ceiling: drop the hidden
                    // exact rungs outright (nothing on screen changes),
                    // then the displayed exact rungs the camera would
                    // not miss — off screen, or coarse within half the
                    // tolerance — before spending anything on new
                    // builds.
                    if (reg.memoryCeilingEpoch()) {
                        reg.dropHiddenLevels();
                        auto drops = Render::planMeshDemotes(
                            scene, levelPlanner.viewMatrix(),
                            levelPlanner.projMatrix(), h,
                            levelPlanner.tolerance(),
                            [&reg](const void *t) {
                                return reg.demoteError(t);
                            });
                        for (const void *tag : drops)
                            reg.requestDemote(tag);
                    }
                    // The GPU budget's half (§13 step 3): over it,
                    // downgrade the *displayed* rung of what the
                    // camera would not miss. The exact mesh stays in
                    // CPU RAM — the way back up is an instant
                    // re-activation through an ordinary refine.
                    const size_t gpuBudget = gpuBudgetBytes();
                    if (gpuBudget && gpuUsedBytes() > gpuBudget) {
                        auto drops = Render::planMeshDemotes(
                            scene, levelPlanner.viewMatrix(),
                            levelPlanner.projMatrix(), h,
                            levelPlanner.tolerance(),
                            [&reg](const void *t) {
                                return reg.downgradeError(t);
                            });
                        for (const void *tag : drops)
                            reg.requestDowngrade(tag);
                    }
                    // Cancels next (§13 step 4): every coarse source
                    // this plan does not want is de-wanted — a queued
                    // tessellation the camera moved away from is work,
                    // not a fetch to ignore. Before the requests, so a
                    // same-pass flip lands wanted. cancelRefine only
                    // fires where an ask actually stands.
                    std::set<const void *> wanted(tags.begin(),
                                                  tags.end());
                    for (const auto &draw : scene) {
                        if (!draw.mesh || !draw.mesh->sourceTag
                            || draw.mesh->levelError <= 0.0f)
                            continue;
                        if (!wanted.count(draw.mesh->sourceTag))
                            reg.cancelRefine(draw.mesh->sourceTag);
                    }
                    for (const void *tag : tags)
                        reg.requestRefine(tag);
                });

            // docs/FarFieldProxies.md §9: how much of the model this
            // camera cannot resolve. Reported on the plan's own schedule
            // rather than per frame — it is a property of where the
            // camera settled, and one line a second is what makes it
            // readable while orbiting a large assembly.
            if (debugconf.coverage) {
                const float h = float(widget->height()
                                      * widget->devicePixelRatioF());
                reportCoverage(Render::coverageHistogram(
                        scene, reinterpret_cast<const float *>(viewMatrix),
                        reinterpret_cast<const float *>(projMatrix), h));
            }
        }

        publishScene(viewMatrix, projMatrix, width, height, clearColor,
                     dirtyChanged);
#endif

        // Publish-only frame. A serving process with nobody at its own
        // window has, at this point, done the whole of what its frame is
        // for: the feeds are consumed and the snapshot is out. What
        // follows -- every pass, for a fountain several cores of
        // software rasterization under Xvfb -- draws a picture nothing
        // reads. The viewers render the scene themselves from the
        // snapshot; they never receive these pixels.
        //
        // `renderOk` AND `hasScene` are set on purpose, because
        // canSkipInternal() needs both to tell SoFCRenderer to skip its
        // own fixed-function GL pass: this frame IS accounted for, by
        // deliberately drawing nothing. Reporting failure instead would
        // hand the same scene to Coin to rasterize, which is no cheaper
        // -- and a process that has served from launch never reaches
        // the end-of-frame assignments below, so leaving `hasScene`
        // to them means it never turns true and Coin rasterizes every
        // frame anyway.
        //
        // Everything above this line is CPU-side feed work, so the
        // snapshot a connecting viewer receives is exactly the one it
        // would have received while the window was being drawn.
        // A pending local dump (saveRenderDump on this process) is the
        // exception: it asks for these pixels by name, so that frame
        // draws in full.
        if (!localAudience() && !dumpPending) {
            renderOk = true;
            hasScene = !scene.empty();
#ifndef FC_RENDERER_STANDALONE
            // The re-snapshot in the post-pass setup is unreachable
            // from here, and without it one finished async user-shader
            // compile leaves dirtyChanged latched: every later frame
            // would re-serialize and republish the whole snapshot.
            // The publish above already carried the fresh binaries, so
            // the generation is consumed exactly as publishNoDraw()
            // consumes it.
            userShaderGen = _BGFXLib.userCompileGeneration;
#endif
            return true;
        }

        // WBOIT runs when the resources exist and the scene has any
        // transparent (non-on-top) triangles this frame; otherwise the
        // transparent view stays a bbox-sorted alpha blend into the
        // scene framebuffer.
        bool oitActive = false;
        if (view->m_oit) {
            for (const auto &draw : scene) {
                if (!draw.material.ontop
                        && draw.material.type == Render::Material::Triangle
                        && (draw.material.transparent
                            || (draw.material.pervertexcolor && draw.mesh
                                && draw.mesh->hasTransparency))) {
                    oitActive = true;
                    break;
                }
            }
        }
        view->oitFrame = oitActive;

        // SSAO runs when the resources exist, the per-frame config asks
        // for it, and there is opaque scene geometry to occlude. The
        // hidden-line draw style disables it (a technical drawing mode;
        // its faces may not draw at all).
        bool ssaoActive = false;
        if (view->m_ssao && aoconf.enabled && !hlconfig.show) {
            for (const auto &draw : scene) {
                if (!draw.material.ontop
                        && draw.material.type == Render::Material::Triangle
                        && !draw.material.transparent
                        && !(draw.material.pervertexcolor && draw.mesh
                             && draw.mesh->hasTransparency)) {
                    ssaoActive = true;
                    break;
                }
            }
        }
        // PBR runs when the per-frame config asks for it and the
        // environment could be built (caps). The hidden-line draw style
        // disables it like SSAO (a technical drawing mode).
        bool pbrActive = false;
        if (pbrconf.enabled && !hlconfig.show) {
            // A changed environment image invalidates the built cubemap
            // (and its irradiance SH) — rebuild on the next ensure.
            if (view->m_envImage != pbrconf.envImage) {
                view->m_envImage = pbrconf.envImage;
                view->m_envBuilt = false;
            }
            view->ensureEnvironment();
            pbrActive = bgfx::isValid(view->m_envTex);
        }
        // The debug buffer visualization (docs/RenderDebug.md) reads the
        // prepass normal/depth and AO targets: force the SSAO chain on
        // while a mode that needs it is active, scene geometry
        // permitting (same opaque-triangle test as above). Modes 4/5/7
        // reconstruct positions from the prepass depth, so they force
        // it too (mode 4 previously relied on another prepass consumer
        // being active).
        if (!ssaoActive && view->m_ssao
                && ((debugconf.viewMode >= 1 && debugconf.viewMode <= 5)
                    || debugconf.viewMode == 7)) {
            for (const auto &draw : scene) {
                if (!draw.material.ontop
                        && draw.material.type == Render::Material::Triangle
                        && !draw.material.transparent
                        && !(draw.material.pervertexcolor && draw.mesh
                             && draw.mesh->hasTransparency)) {
                    ssaoActive = true;
                    break;
                }
            }
        }
        view->pbrFrame = pbrActive;
        view->pbrMetallic = pbrconf.metallic;
        view->pbrRoughness = pbrconf.roughness;
        view->pbrEnvIntensity = pbrconf.envIntensity;
        view->bumpScale = bumpconf.scale;
        view->bumpParallax = bumpconf.parallax;

        // Whole-object selection/highlight draws replace the object's
        // normal rendering (SoFCRenderer's selectionkeys/highlightkeys
        // skip): collect their object keys and hide matching scene draws.
        // Computed before the shadow setup — hidden draws don't cast, so
        // the cached-map caster hash needs them.
        hiddenKeys.clear();
        for (const auto &sel : selections) {
            for (const auto &draw : sel.second) {
                if (draw.wholeObject && draw.objectKey)
                    hiddenKeys.insert(draw.objectKey);
            }
        }
        if (hlWholeOnTop) {
            for (const auto &draw : highlight) {
                if (draw.wholeObject && draw.objectKey)
                    hiddenKeys.insert(draw.objectKey);
            }
        }
        auto isHidden = [this](const Render::DrawCall &d) {
            return d.objectKey && !hiddenKeys.empty()
                && hiddenKeys.count(d.objectKey);
        };

        // Shadow draw style: a light in the scene feed activates the
        // variance shadow map pass (only that style traverses one — the
        // viewer headlight lives outside the captured graph). The light
        // camera is an orthographic fit of the scene bounds along the
        // light direction; spot lights fall back to their direction
        // (a known first-cut deviation).
        bool shadowActive = false;
        float lightViewMtx[16], lightProjMtx[16];
        if (view->m_shadow && bboxValid && lightconf.valid
                && lightconf.shadow) {
            const Render::LightConfig &light = lightconf;
            shadowActive = true;
            {
                float dx = bboxMax[0] - bboxMin[0];
                float dy = bboxMax[1] - bboxMin[1];
                float dz = bboxMax[2] - bboxMin[2];
                float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (diag <= 0.0f) {
                    shadowActive = false;
                } else {
                    // Half diagonal with the GL ShadowBoundBoxScale-like
                    // margin.
                    float r = 0.6f * diag;
                    bx::Vec3 dir = bx::normalize(
                        bx::Vec3(light.direction[0], light.direction[1],
                                 light.direction[2]));
                    bx::Vec3 center((bboxMin[0] + bboxMax[0]) * 0.5f,
                                    (bboxMin[1] + bboxMax[1]) * 0.5f,
                                    (bboxMin[2] + bboxMax[2]) * 0.5f);
                    bx::Vec3 up = bx::abs(dir.z) > 0.99f
                        ? bx::Vec3(1.0f, 0.0f, 0.0f)
                        : bx::Vec3(0.0f, 0.0f, 1.0f);
                    const auto *caps = bgfx::getCaps();
                    if (light.spot) {
                        // Spot light: perspective camera at the light
                        // position along its direction, field of view
                        // from the cone cutoff, depth range fit to the
                        // scene bounding sphere.
                        bx::Vec3 eye(light.position[0],
                                     light.position[1],
                                     light.position[2]);
                        bx::mtxLookAt(lightViewMtx, eye,
                                      bx::add(eye, dir), up);
                        float d = bx::length(bx::sub(center, eye));
                        float far = d + r;
                        float near = bx::max(d - r, far * 1.0e-3f);
                        float fovy = bx::clamp(
                            2.0f * light.cutOffAngle, 0.02f, 3.1f)
                            * 180.0f / bx::kPi;
                        bx::mtxProj(lightProjMtx, fovy, 1.0f, near, far,
                                    caps->homogeneousDepth);
                    } else {
                        bx::Vec3 eye =
                            bx::sub(center, bx::mul(dir, 2.0f * r));
                        bx::mtxLookAt(lightViewMtx, eye, center, up);
                        bx::mtxOrtho(lightProjMtx, -r, r, -r, r,
                                     0.0f, 4.0f * r, 0.0f,
                                     caps->homogeneousDepth);
                    }
                    // Camera view space -> shadow map uv (xy) and light
                    // window depth (z), the matrix the mesh shaders use.
                    float invV[16], tmp[16], tmp2[16];
                    bx::mtxInverse(invV,
                        reinterpret_cast<const float *>(viewMatrix));
                    bx::mtxMul(tmp, invV, lightViewMtx);
                    bx::mtxMul(tmp2, tmp, lightProjMtx);
                    const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
                    const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
                    const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
                    const float crop[16] = {
                        0.5f, 0.0f, 0.0f, 0.0f,
                        0.0f, sy,   0.0f, 0.0f,
                        0.0f, 0.0f, sz,   0.0f,
                        0.5f, 0.5f, tz,   1.0f,
                    };
                    bx::mtxMul(view->shadowMtx, tmp2, crop);
                    // Light direction and color in camera view space.
                    const float *vm =
                        reinterpret_cast<const float *>(viewMatrix);
                    float lv[3];
                    for (int j = 0; j < 3; ++j)
                        lv[j] = dir.x * vm[j] + dir.y * vm[4 + j]
                            + dir.z * vm[8 + j];
                    float ll = std::sqrt(lv[0]*lv[0] + lv[1]*lv[1]
                                         + lv[2]*lv[2]);
                    for (int j = 0; j < 3; ++j)
                        view->lightDirView[j] = ll > 0.0f ? lv[j] / ll
                                                          : lv[j];
                    unpackColor(light.color, view->lightColorI);
                    for (int j = 0; j < 3; ++j)
                        view->lightColorI[j] *= light.intensity;
                    // Spot light: position in camera view space, cone
                    // cutoff cosine in w (-1 = directional), falloff
                    // exponent riding the light color's free channel.
                    if (light.spot) {
                        for (int j = 0; j < 3; ++j)
                            view->lightPosView[j] =
                                light.position[0] * vm[j]
                                + light.position[1] * vm[4 + j]
                                + light.position[2] * vm[8 + j]
                                + vm[12 + j];
                        view->lightPosView[3] =
                            std::cos(bx::clamp(light.cutOffAngle,
                                               0.01f, 1.55f));
                        view->lightColorI[3] =
                            bx::clamp(light.dropOffRate, 0.0f, 1.0f)
                            * 128.0f;
                    } else {
                        view->lightPosView[0] = 0.0f;
                        view->lightPosView[1] = 0.0f;
                        view->lightPosView[2] = 0.0f;
                        view->lightPosView[3] = -1.0f;
                        view->lightColorI[3] = 0.0f;
                    }
                }
            }
        }
        view->shadowFrame = shadowActive;
        // Shadow map size from ShadowPrecision (Coin: the next power of
        // two of precision * the 2048 cap); recreating the targets
        // resets the cached-map hash.
        if (shadowActive) {
            float prec = bx::clamp(lightconf.precision, 0.01f, 1.0f);
            uint16_t desired = 1;
            uint16_t want = uint16_t(prec * BGFXView::kShadowMaxSize);
            while (desired < want)
                desired = uint16_t(desired << 1);
            view->ensureShadowTargets(desired);
            shadowActive = bgfx::isValid(view->shadowFbo);
        }
        // On RG32F the map stores plain (z, z^2) moments and the
        // receivers run Coin's exact VsmLookup — the GL Shadow style's
        // soft penumbra — at every SmoothBorder setting.
        // The reduced RG16F moment path (float32 not linearly filterable)
        // always runs the EVSM warp -- fp16 plain (z, z^2) moments lose too
        // much precision on the self-shadowed terminator, and the warp is what
        // makes RG16F usable.
        // The warp is fundamentally at odds with the blur: even a
        // scaled-down exponent reconstructs a near-binary edge from a
        // widely blurred moments map — exp(c·z) at the receiver dwarfs
        // the variance the blur added within a texel or two, and the
        // penumbra the blur paid for disappears. RG32F therefore stays
        // plain VSM (warp 0) with the blur too: blurring plain (z, z²)
        // widens the variance across the edge, which is exactly the
        // penumbra gradient. Only the reduced-precision RG16F path
        // keeps its fixed warp (plain fp16 moments are unusable on the
        // self-shadowed terminator), so its smoothing stays tighter.
        view->shadowWarpFrame =
            view->m_shadowForceWarp ? view->shadowWarp : 0.0f;
        view->shadowEpsilon = lightconf.epsilon;
        view->shadowThreshold = lightconf.threshold;
        // Coin's N-tap receiver spread kernel (ShadowSpreadSize /
        // SpreadSampleSize). The viewer packs both into the Coin
        // smoothBorder field as spread * 1e-6 + sample * 1e-2 digits;
        // Coin decodes swidth = (packed % 100000) * 5e-5 — i.e. the
        // spread wraps every 10000 (replicated for parity) — and taps
        // at coord + offset * swidth * 0.001 (spot lights * 0.1). The
        // viewer also shrinks a spot light's spread by 256 / the scene
        // extent on large scenes before packing (the backend uses the
        // raw scene bbox where the viewer's box includes the ground).
        {
            float spread = std::max(lightconf.spreadSize, 0.0f);
            spread = std::fmod(spread * 10.0f, 100000.0f) * 0.1f;
            if (lightconf.spot && bboxValid) {
                float maxSize = std::max(
                    bboxMax[0] - bboxMin[0],
                    std::max(bboxMax[1] - bboxMin[1],
                             bboxMax[2] - bboxMin[2]));
                if (maxSize > 256.0f)
                    spread *= 256.0f / maxSize;
            }
            int sample = int(bx::clamp(lightconf.spreadSampleSize,
                                       0.0f, 7.0f) + 0.5f);
            float mode = 0.0f;
            if (spread > 0.0f)
                mode = sample >= 1
                    ? float(std::min(2 * sample + 1, 8))
                    : 1.0f;
            float sw = spread * 5.0e-4f * 0.001f;
            if (lightconf.spot)
                sw *= 0.1f;
            view->shadowSpreadUv = sw;
            view->shadowSpreadMode = mode;
        }
        static const bool dbgshadow =
            (getenv("FC_BGFX_DEBUG_SHADOW") != nullptr);
        if (dbgshadow)
            fprintf(stderr,
                    "bgfx shadow active=%d valid=%d dir=%g,%g,%g"
                    " ldirview=%g,%g,%g bbox=%d hd=%d obl=%d\n",
                    shadowActive, lightconf.valid,
                    lightconf.direction[0], lightconf.direction[1],
                    lightconf.direction[2],
                    view->lightDirView[0], view->lightDirView[1],
                    view->lightDirView[2], bboxValid,
                    bgfx::getCaps()->homogeneousDepth,
                    bgfx::getCaps()->originBottomLeft);
        if (dbgshadow && shadowActive) {
            // Cross-check the receiver chain on the scene bbox center:
            // world -> camera view -> shadowMtx should equal
            // world -> lightView -> lightProj -> crop.
            const float *vm = reinterpret_cast<const float *>(viewMatrix);
            float w[4] = {(bboxMin[0] + bboxMax[0]) * 0.5f,
                          (bboxMin[1] + bboxMax[1]) * 0.5f,
                          (bboxMin[2] + bboxMax[2]) * 0.5f, 1.0f};
            auto xform = [](const float *m, const float *v, float *o) {
                for (int j = 0; j < 4; ++j)
                    o[j] = v[0]*m[j] + v[1]*m[4+j] + v[2]*m[8+j]
                        + v[3]*m[12+j];
            };
            float vv[4], sp[4], lv[4], lp[4];
            xform(vm, w, vv);
            xform(view->shadowMtx, vv, sp);
            xform(lightViewMtx, w, lv);
            xform(lightProjMtx, lv, lp);
            fprintf(stderr,
                    "bgfx shadow chk sp=%g,%g,%g,%g direct ndc=%g,%g,%g"
                    " -> uvz=%g,%g,%g\n",
                    sp[0], sp[1], sp[2], sp[3], lp[0], lp[1], lp[2],
                    lp[0]*0.5f + 0.5f, lp[1]*0.5f + 0.5f,
                    lp[2]*0.5f + 0.5f);
        }

        // Zero radius = automatic: a fraction of the scene bounding
        // sphere, the scale-free default.
        float aoRadius = aoconf.radius;
        if (ssaoActive && aoRadius <= 0.0f) {
            if (bboxValid) {
                float dx = bboxMax[0] - bboxMin[0];
                float dy = bboxMax[1] - bboxMin[1];
                float dz = bboxMax[2] - bboxMin[2];
                aoRadius = 0.05f * std::sqrt(dx*dx + dy*dy + dz*dz);
            }
            if (aoRadius <= 0.0f)
                ssaoActive = false;
        }
        // The mesh programs fold the AO chain result into their
        // ambient/headlight/IBL terms (sampled at unit 9, bound per
        // draw in setTriangleFrameState). GTAO's denoise ping-pongs
        // its final result back into aoTex; the classic path ends in
        // aoBlurTex. Invalid = AO off, the white stand-in binds.
        view->aoMeshTex = BGFX_INVALID_HANDLE;
        if (ssaoActive)
            view->aoMeshTex = aoconf.method == 1
                    && bgfx::isValid(view->m_progGtao)
                    && bgfx::isValid(view->m_progGtaoBlur)
                ? view->aoTex : view->aoBlurTex;

        // Volumetric light shafts raymarch the shadow map with ray ends
        // from the SSAO prepass, so they need the shadow pass active
        // this frame; hidden-line mode disables them like the other
        // shading effects. The medium is a sphere around the scene
        // bounds — bounding it keeps the camera's stand-off distance
        // out of the optical depth.
        bool volActive = view->m_vol && volconf.enabled && shadowActive
            && !hlconfig.show;
        float volDensity = volconf.density;
        float volMaxDist = 0.0f;
        float volMedium[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (volActive) {
            float dx = bboxMax[0] - bboxMin[0];
            float dy = bboxMax[1] - bboxMin[1];
            float dz = bboxMax[2] - bboxMin[2];
            float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
            // Medium sphere: the scene bounding sphere with a margin
            // (shafts reach a bit of the surrounding ground plane).
            float r = 0.75f * diag;
            const float *vm = reinterpret_cast<const float *>(viewMatrix);
            float c[3] = {(bboxMin[0] + bboxMax[0]) * 0.5f,
                          (bboxMin[1] + bboxMax[1]) * 0.5f,
                          (bboxMin[2] + bboxMax[2]) * 0.5f};
            for (int j = 0; j < 3; ++j)
                volMedium[j] = c[0] * vm[j] + c[1] * vm[4 + j]
                    + c[2] * vm[8 + j] + vm[12 + j];
            volMedium[3] = r;
            volMaxDist = std::sqrt(volMedium[0] * volMedium[0]
                                   + volMedium[1] * volMedium[1]
                                   + volMedium[2] * volMedium[2]) + r;
            // Zero density = automatic: unit optical depth over the
            // medium radius, the scale-free default.
            if (volDensity <= 0.0f)
                volDensity = 1.0f / r;
            if (getenv("FC_BGFX_DEBUG_VOL"))
                fprintf(stderr,
                        "bgfx vol density=%g maxdist=%g medium="
                        "%g,%g,%g r=%g\n",
                        volDensity, volMaxDist, volMedium[0],
                        volMedium[1], volMedium[2], volMedium[3]);
        }
        // Water medium: scene draws flagged Material::water become
        // water bodies of the volumetric pass. Each body (object key)
        // gets an appearance slot — extinction sigma from its diffuse
        // color (absorption of the complement) plus a
        // wavelength-independent scattering term, density auto = from
        // its own bounds. The interval depth writer stamps the slot per
        // pixel; bodies beyond the slot count share slot 0's
        // appearance. slotOf resolves a draw's slot at submit time.
        constexpr int kSlots = BGFXView::kMediumSlots;
        bool waterActive = false;
        bool hasWaterBody = false;
        float waterSigma[kSlots][4] = {};
        float waterDiagSlot[kSlots] = {};
        float waterDiag = 0.0f;   // first body's, for the wave-scale auto
        float waterPlaneZ = 0.0f; // top of the water body/bodies, the plane
        bool waterPlaneSet = false; // the planar reflection mirrors about
        // World xy the water covers, which is the footprint the particle
        // impact map is framed on: {xmin, ymin, xmax, ymax}. Every body
        // counts, since a droplet may land in any of them.
        float waterFoot[4] = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
        std::unordered_map<uint64_t, int> waterSlots;
        auto slotOf = [](const std::unordered_map<uint64_t, int> &slots,
                         uint64_t key) {
            auto it = slots.find(key);
            return it == slots.end() ? 0 : it->second;
        };
        int waterSlotCount = 0;
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (!mat.water || mat.ontop
                    || mat.type != Render::Material::Triangle)
                continue;
            // Footprint before the slot bookkeeping: a body past the
            // slot budget, or a second draw of one already slotted,
            // still holds water a droplet can land in.
            waterFoot[0] = std::min(waterFoot[0], draw.bboxMin[0]);
            waterFoot[1] = std::min(waterFoot[1], draw.bboxMin[1]);
            waterFoot[2] = std::max(waterFoot[2], draw.bboxMax[0]);
            waterFoot[3] = std::max(waterFoot[3], draw.bboxMax[1]);
            if (waterSlots.count(draw.objectKey))
                continue;
            if (waterSlotCount >= kSlots) {
                waterSlots.emplace(draw.objectKey, 0);
                continue;
            }
            int slot = waterSlotCount++;
            waterSlots.emplace(draw.objectKey, slot);
            float diag;
            {
                float dx = draw.bboxMax[0] - draw.bboxMin[0];
                float dy = draw.bboxMax[1] - draw.bboxMin[1];
                float dz = draw.bboxMax[2] - draw.bboxMin[2];
                diag = std::sqrt(dx*dx + dy*dy + dz*dz);
            }
            waterDiagSlot[slot] = diag;
            if (waterDiag <= 0.0f)
                waterDiag = diag;
            if (!waterPlaneSet || draw.bboxMax[2] > waterPlaneZ) {
                waterPlaneZ = draw.bboxMax[2];
                waterPlaneSet = true;
            }
            hasWaterBody = true;
            float dens = mat.waterdensity;
            if (dens <= 0.0f) {
                if (diag <= 0.0f)
                    continue;   // degenerate body: slot stays a no-op
                dens = 3.0f / diag;
            }
            float color[4];
            unpackColor(mat.diffuse, color);
            float sigmaS = 0.35f * dens;
            for (int j = 0; j < 3; ++j)
                waterSigma[slot][j] = sigmaS + dens * (1.0f - color[j]);
            waterSigma[slot][3] = sigmaS;
            // The volumetric water medium needs the volumetric pass; the
            // body detection itself also serves the surface pass below.
            waterActive = volActive;
            if (volActive && getenv("FC_BGFX_DEBUG_VOL"))
                fprintf(stderr,
                        "bgfx water slot=%d dens=%g sigma=%g,%g,%g s=%g\n",
                        slot, dens, waterSigma[slot][0],
                        waterSigma[slot][1], waterSigma[slot][2],
                        waterSigma[slot][3]);
        }
        // Water surface (refraction/reflection) pass: independent of the
        // volumetric medium — it only needs a water body, the scene copy
        // resources, and the environment for the reflection. Hidden-line
        // mode disables it like the other shading effects.
        bool waterSurfActive = waterconf.enabled && hasWaterBody
            && !hlconfig.show
            && bgfx::isValid(view->m_progWater)
            && bgfx::isValid(view->sceneCopyFbo);
        if (waterSurfActive) {
            view->ensureEnvironment();
            if (!bgfx::isValid(view->m_envTex))
                waterSurfActive = bgfx::isValid(view->m_dummyEnvTex);
        }
        // Glass bodies (Material::glass): the draws leave the ordinary
        // path and re-render in the glass surface pass — screen-space
        // refraction, thickness absorption from the glass front/back
        // interval, environment reflection. Needs the scene copy, the
        // SSAO resource set (the interval targets and the prepass
        // depth-reject live there) and the environment; hidden-line
        // mode disables it like the other shading effects. Unlike
        // water the body's edge/vertex draws keep rendering (a glass
        // part keeps its CAD feature lines).
        bool hasGlassBody = false;
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (mat.glass && !mat.ontop && mat.numclipplanes == 0
                    && mat.type == Render::Material::Triangle) {
                hasGlassBody = true;
                break;
            }
        }
        bool glassActive = hasGlassBody && !hlconfig.show
            && view->m_ssao
            && bgfx::isValid(view->m_progGlass)
            && bgfx::isValid(view->glassFrontFbo)
            && bgfx::isValid(view->sceneCopyFbo);
        if (glassActive) {
            view->ensureEnvironment();
            if (!bgfx::isValid(view->m_envTex))
                glassActive = bgfx::isValid(view->m_dummyEnvTex);
        }
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr, "bgfx glass: body=%d active=%d\n",
                    hasGlassBody, glassActive);
        // Body-local "up" frame shared by the fire taper and fountain
        // flow frames: up = the model placement's local z axis (world z
        // for identity transforms), the model x axis Gram-Schmidt'd
        // into a lateral right vector. The world AABB corners projected
        // onto the basis give the bottom-center origin, the up-extent
        // and the max lateral half-extent (conservative for tilted
        // bodies -- a flow frame, not a fit). Row-vector convention
        // like the shadow matrix: lp = [wp, 1] * M with the frame axes
        // as columns.
        auto buildBodyFrame = [](const Render::DrawCall &draw,
                                 float frame[16], float base[3],
                                 float up[3], float &height,
                                 float &radius) -> bool {
            for (int j = 0; j < 3; ++j) {
                base[j] = 0.0f;
                up[j] = 0.0f;
            }
            height = 0.0f;
            radius = 0.0f;
            float u[3] = {0.0f, 0.0f, 1.0f};
            float r[3] = {1.0f, 0.0f, 0.0f};
            if (!draw.identity) {
                for (int j = 0; j < 3; ++j) {
                    u[j] = draw.model[8 + j];
                    r[j] = draw.model[j];
                }
                float ul = std::sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
                if (ul > 1.0e-6f)
                    for (int j = 0; j < 3; ++j)
                        u[j] /= ul;
                else
                    u[0] = 0.0f, u[1] = 0.0f, u[2] = 1.0f;
                float ru = r[0]*u[0] + r[1]*u[1] + r[2]*u[2];
                for (int j = 0; j < 3; ++j)
                    r[j] -= ru * u[j];
                float rl = std::sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
                if (rl > 1.0e-6f) {
                    for (int j = 0; j < 3; ++j)
                        r[j] /= rl;
                } else {
                    // up nearly parallel to the model x axis: any
                    // stable perpendicular does for the lateral frame.
                    r[0] = -u[2]; r[1] = 0.0f; r[2] = u[0];
                    rl = std::sqrt(r[0]*r[0] + r[2]*r[2]);
                    if (rl > 1.0e-6f) {
                        r[0] /= rl; r[2] /= rl;
                    } else {
                        r[0] = 1.0f; r[2] = 0.0f;
                    }
                }
            }
            float f[3] = {u[1]*r[2] - u[2]*r[1],
                          u[2]*r[0] - u[0]*r[2],
                          u[0]*r[1] - u[1]*r[0]};
            float pmin[3], pmax[3];
            for (int c = 0; c < 8; ++c) {
                float wp[3] = {
                    (c & 1) ? draw.bboxMax[0] : draw.bboxMin[0],
                    (c & 2) ? draw.bboxMax[1] : draw.bboxMin[1],
                    (c & 4) ? draw.bboxMax[2] : draw.bboxMin[2]};
                float pr[3] = {
                    wp[0]*r[0] + wp[1]*r[1] + wp[2]*r[2],
                    wp[0]*f[0] + wp[1]*f[1] + wp[2]*f[2],
                    wp[0]*u[0] + wp[1]*u[1] + wp[2]*u[2]};
                for (int j = 0; j < 3; ++j) {
                    if (c == 0 || pr[j] < pmin[j]) pmin[j] = pr[j];
                    if (c == 0 || pr[j] > pmax[j]) pmax[j] = pr[j];
                }
            }
            height = pmax[2] - pmin[2];
            radius = 0.5f * std::max(pmax[0] - pmin[0],
                                     pmax[1] - pmin[1]);
            // Bottom center of the body in the frame, back in world
            // coordinates (r/f/u are an orthonormal world basis).
            float rc = 0.5f * (pmin[0] + pmax[0]);
            float fc = 0.5f * (pmin[1] + pmax[1]);
            for (int j = 0; j < 3; ++j) {
                base[j] = rc * r[j] + fc * f[j] + pmin[2] * u[j];
                up[j] = u[j];
                frame[j * 4 + 0] = r[j];
                frame[j * 4 + 1] = f[j];
                frame[j * 4 + 2] = u[j];
                frame[j * 4 + 3] = 0.0f;
            }
            frame[12] = -rc;
            frame[13] = -fc;
            frame[14] = -pmin[2];
            frame[15] = 1.0f;
            return height > 1.0e-6f && radius > 1.0e-6f;
        };
        // Cloud bodies (Material::cloud): the closed volume raymarches
        // as a procedural-density medium of the volumetric pass and the
        // geometry itself is not rendered. Each body gets an appearance
        // slot like the water medium (bodies beyond the slot count
        // share slot 0); density/detail auto = from the body's own
        // bounds. Fountain bodies (Material::fountain) share the cloud
        // medium channel -- same interval targets and slots -- with the
        // slot's w flagging the fountain density field (2.0) instead of
        // the cloud FBM (1.0), plus a flow frame and geometry entry.
        bool hasCloudBody = false;
        // Per slot: x = density, y = detail, z = speed, w = flavor
        // (0 = inactive, 1 = cloud, 2 = fountain).
        float cloudSlot[kSlots][4] = {};
        float fountainFrame[kSlots][16] = {};
        // Per slot: x = 1/height, y = 1/lateral radius.
        float fountainGeom[kSlots][4] = {};
        // Per slot splash source for the water surface: xyz = world
        // base center, w = impact ring radius (0 = inactive).
        float fountainSplash[kSlots][4] = {};
        std::unordered_map<uint64_t, int> cloudSlots;
        int cloudSlotCount = 0;
        // Cloud slots bound to a user "volume"-stage scatter medium
        // (docs/RenderEngine.md §5.11) — feeds the spliced program
        // variants assembled with the fire-slot users below.
        std::shared_ptr<const Render::UserShader> cloudSlotUser[kSlots];
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if ((!mat.cloud && !mat.fountain) || mat.ontop
                    || mat.type != Render::Material::Triangle)
                continue;
            if (cloudSlots.count(draw.objectKey))
                continue;
            if (cloudSlotCount >= kSlots) {
                cloudSlots.emplace(draw.objectKey, 0);
                continue;
            }
            int slot = cloudSlotCount++;
            cloudSlots.emplace(draw.objectKey, slot);
            float dx = draw.bboxMax[0] - draw.bboxMin[0];
            float dy = draw.bboxMax[1] - draw.bboxMin[1];
            float dz = draw.bboxMax[2] - draw.bboxMin[2];
            float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
                ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
            if (mat.fountain) {
                // Fountain body: spray density field in the body's
                // flow frame; denser auto defaults than the cloud
                // (spray is a tight plume, not a room-filling puff).
                float height = 0.0f, radius = 0.0f;
                float base[3], up[3];
                if (!buildBodyFrame(draw, fountainFrame[slot], base,
                                    up, height, radius))
                    continue;   // degenerate: slot stays inactive
                float density = mat.fountaindensity;
                if (density <= 0.0f && diag > 0.0f)
                    density = 40.0f / diag;
                float detail = mat.fountaindetail;
                if (detail <= 0.0f && diag > 0.0f)
                    detail = 12.0f / diag;
                if (density <= 0.0f || detail <= 0.0f)
                    continue;
                cloudSlot[slot][0] = density;
                cloudSlot[slot][1] = detail;
                cloudSlot[slot][2] = mat.fountainspeed;
                cloudSlot[slot][3] = 2.0f;
                fountainGeom[slot][0] = 1.0f / height;
                fountainGeom[slot][1] = 1.0f / radius;
                fountainSplash[slot][0] = base[0];
                fountainSplash[slot][1] = base[1];
                fountainSplash[slot][2] = base[2];
                fountainSplash[slot][3] = radius * 0.85f;
                hasCloudBody = true;
                if (getenv("FC_BGFX_DEBUG_FEED"))
                    fprintf(stderr,
                            "bgfx fountain slot=%d dens=%g detail=%g"
                            " h=%g r=%g\n",
                            slot, density, detail, height, radius);
                continue;
            }
            float density = mat.clouddensity;
            if (density <= 0.0f && diag > 0.0f)
                density = 6.0f / diag;
            float detail = mat.clouddetail;
            if (detail <= 0.0f && diag > 0.0f)
                detail = 4.0f / diag;
            if (density <= 0.0f || detail <= 0.0f)
                continue;   // degenerate body: slot stays inactive
            cloudSlot[slot][0] = density;
            cloudSlot[slot][1] = detail;
            cloudSlot[slot][2] = mat.cloudspeed;
            cloudSlot[slot][3] = 1.0f;
            hasCloudBody = true;
            if (getenv("FC_BGFX_DEBUG_FEED"))
                fprintf(stderr,
                        "bgfx cloud slot=%d dens=%g detail=%g\n",
                        slot, density, detail);
        }
        bool cloudActive = hasCloudBody && volActive;
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr, "bgfx cloud: body=%d active=%d\n",
                    hasCloudBody, cloudActive);
        // The cloud body's own draws (fills and feature lines) are
        // suppressed entirely while the medium renders, matched by
        // object key like the water surface line suppression.
        std::unordered_set<uint64_t> cloudObjects;
        if (cloudActive) {
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if ((mat.cloud || mat.fountain) && !mat.ontop
                        && draw.objectKey
                        && mat.type == Render::Material::Triangle)
                    cloudObjects.insert(draw.objectKey);
            }
        }
        // Fire bodies (Material::fire): the closed volume raymarches as
        // an emissive flame medium of the volumetric pass and the
        // geometry itself is not rendered. Each body gets an appearance
        // slot like the water/cloud media (bodies beyond the slot count
        // share slot 0), carrying its own emission/detail/speed, taper
        // frame and effect-light anchor.
        struct FireSlot {
            float emission = 0.0f, detail = 0.0f, speed = 1.0f;
            float invHeight = 0.0f, invRadius = 0.0f, soot = 0.0f;
            float intensity = 1.0f, diag = 0.0f;
            // World -> fire-local frame (z = the body placement's up
            // axis, origin at the bottom center) and the effect-light
            // anchor.
            float frame[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, 1.0f};
            float lightWorld[3] = {0.0f, 0.0f, 0.0f};
            bool valid = false;
        };
        bool hasFireBody = false;
        FireSlot fireSlot[kSlots];
        std::unordered_map<uint64_t, int> fireSlots;
        int fireSlotCount = 0;
        // Fire slots bound to a user "volume"-stage medium function
        // (docs/RenderEngine.md §5.11) — feeds the spliced program
        // variants assembled after the loop.
        std::shared_ptr<const Render::UserShader> fireSlotUser[kSlots];
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (!mat.fire || mat.ontop
                    || mat.type != Render::Material::Triangle)
                continue;
            if (fireSlots.count(draw.objectKey))
                continue;
            if (fireSlotCount >= kSlots) {
                fireSlots.emplace(draw.objectKey, 0);
                continue;
            }
            FireSlot &fs = fireSlot[fireSlotCount];
            fireSlots.emplace(draw.objectKey, fireSlotCount++);
            float dx = draw.bboxMax[0] - draw.bboxMin[0];
            float dy = draw.bboxMax[1] - draw.bboxMin[1];
            float dz = draw.bboxMax[2] - draw.bboxMin[2];
            float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
                ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
            float intensity = mat.fireintensity > 0.0f
                ? mat.fireintensity : 1.0f;
            if (diag > 0.0f)
                fs.emission = intensity * 4.0f / diag;
            fs.detail = mat.firedetail;
            if (fs.detail <= 0.0f && diag > 0.0f)
                fs.detail = 5.0f / diag;
            fs.speed = mat.firespeed;
            fs.intensity = intensity;
            fs.diag = diag;
            // Mild soot absorption scaled like the other auto
            // densities; FC_BGFX_NO_FIRESOOT keeps the old purely
            // additive flame for A/B comparisons.
            static const bool noSoot =
                (getenv("FC_BGFX_NO_FIRESOOT") != nullptr);
            if (!noSoot && diag > 0.0f)
                fs.soot = 1.5f / diag;
            // The taper frame comes from the body's placement (shared
            // buildBodyFrame helper above).
            float base[3], up[3];
            float height = 0.0f, radius = 0.0f;
            if (buildBodyFrame(draw, fs.frame, base, up, height,
                               radius)) {
                fs.invHeight = 1.0f / height;
                fs.invRadius = 1.0f / radius;
            }
            // The effect light sits a third up the flame — the ramp's
            // bright zone — along the body's up axis.
            for (int j = 0; j < 3; ++j)
                fs.lightWorld[j] = base[j] + 0.35f * height * up[j];
            fs.valid = fs.emission > 0.0f && fs.detail > 0.0f
                && fs.invHeight > 0.0f;
            hasFireBody = hasFireBody || fs.valid;
            if (getenv("FC_BGFX_DEBUG_FEED"))
                fprintf(stderr,
                        "bgfx fire slot=%d valid=%d emit=%g detail=%g\n",
                        int(&fs - fireSlot), fs.valid, fs.emission,
                        fs.detail);
        }
        bool fireActive = hasFireBody && volActive;
        // Assemble (or drop) the user medium splice variants when the
        // slot→user-source tuple changed since the last frame; the
        // compile itself is async through the shared user-shader cache,
        // stock media stand in until the binaries land.
        {
            if (fireActive || cloudActive)
                collectMediumUsers(scene, fireSlotUser, cloudSlotUser,
                                   kSlots);
            if (!fireActive)
                for (int i = 0; i < kSlots; ++i)
                    fireSlotUser[i].reset();
            if (!cloudActive)
                for (int i = 0; i < kSlots; ++i)
                    cloudSlotUser[i].reset();
            std::array<const void *, 8> key {};
            for (int i = 0; i < kSlots && i < 4; ++i) {
                key[i] = fireSlotUser[i].get();
                key[i + 4] = cloudSlotUser[i].get();
            }
            // A republished splice table is a new chance to adopt what
            // an earlier, binary-less one could not.
            size_t fingerprint = usershaderconf.splices.size();
            for (const auto &sp : usershaderconf.splices)
                fingerprint = fingerprint * 131u + sp.compiled.size();
            const bool retry = !view->volUserAdopted
                && fingerprint != view->volSpliceFingerprint;
            view->volSpliceFingerprint = fingerprint;
            if (key != view->volUserKey || retry) {
                view->volUserKey = key;
                view->volUserVol = assembleMediumVariant(
                    "fc_volume_fs.sh", fireSlotUser, cloudSlotUser,
                    kSlots);
                view->volUserExt = assembleMediumVariant(
                    "fc_volume_ext_fs.sh", fireSlotUser, cloudSlotUser,
                    kSlots);
                view->volUserRefl = assembleMediumVariant(
                    "fc_refl_media_fs.sh", fireSlotUser, cloudSlotUser,
                    kSlots);
#ifdef FC_RENDERER_STANDALONE
                // Compiler-less tier: adopt the snapshot-shipped
                // assembled variant (server-compiled binaries) when its
                // source matches what was just assembled — source
                // equality guarantees the same slot binding
                // (docs/RenderEngine.md §5.11 transport).
                // The server compiles the viewer binaries asynchronously
                // and republishes when they land, so the first snapshot
                // after a binding — a cold shader cache above all — can
                // carry the splice with no binaries at all. Adopting
                // that empty variant must not be final: a binary-less
                // splice keeps the stock stand-in and marks the
                // adoption incomplete, so the next republished table
                // is tried again instead of leaving the medium stock
                // for the rest of the session.
                bool usable = true;
                // Usable means "carries a binary THIS tier can load":
                // the variants are compiled per target and the loader
                // matches on the profile label, so a table holding only
                // the other tier's binary is as unusable as an empty
                // one and must be retried the same way.
                std::string plat, prof, apiDir;
                const bool haveTarget = shadercTarget(plat, prof, apiDir);
                auto adopt =
                    [this, &usable, &prof, haveTarget](
                            std::shared_ptr<const Render::UserShader> &s,
                            const char *tag) {
                    if (!s)
                        return;
                    for (const auto &sp : usershaderconf.splices) {
                        if (sp.fragmentSource == s->fragmentSource) {
                            bool mine = false;
                            for (const auto &c : sp.compiled)
                                if (!haveTarget
                                        || (c.profile == prof
                                            && !c.fsBin.empty())) {
                                    mine = true;
                                    break;
                                }
                            std::printf("fcviewer: splice %s adopted "
                                        "(%zu bins, %s)\n",
                                        tag, sp.compiled.size(),
                                        mine ? "mine" : "none for this tier");
                            if (!mine)
                                usable = false;
                            else
                                s = std::make_shared<Render::UserShader>(sp);
                            return;
                        }
                    }
                    std::printf("fcviewer: splice %s NOT shipped "
                                "(%zu candidates)\n",
                                tag, usershaderconf.splices.size());
                    usable = false;
                };
                adopt(view->volUserVol, "vol");
                adopt(view->volUserExt, "ext");
                adopt(view->volUserRefl, "refl");
                view->volUserAdopted = usable;
#endif
            }
        }
        // Time-animated content (fire flicker, cloud drift, water waves,
        // caustics): a client's idle frame skip must keep rendering
        // while any of these replay per frame; everything else in the
        // frame is camera/scene-driven and a repeat frame is identical.
        sceneAnimated = fireActive || cloudActive || waterSurfActive
            || (volActive && waterActive);
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr, "bgfx fire: body=%d active=%d\n",
                    hasFireBody, fireActive);
        // The fire body's own draws (fills and feature lines) are
        // suppressed entirely while the medium renders, matched by
        // object key like the cloud body.
        std::unordered_set<uint64_t> fireObjects;
        if (fireActive) {
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if (mat.fire && !mat.ontop && draw.objectKey
                        && mat.type == Render::Material::Triangle)
                    fireObjects.insert(draw.objectKey);
            }
        }
        // The water body's edge/vertex draws are suppressed while the
        // surface renders (a water surface has no CAD feature lines, and
        // the black edges would smear through the screen-space
        // refraction). The line/point draws don't reliably carry the
        // material's water flag (node-order dependent capture), so they
        // are matched by the body's object key.
        std::unordered_set<uint64_t> waterSurfObjects;
        if (waterSurfActive) {
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if (mat.water && !mat.ontop && draw.objectKey
                        && mat.type == Render::Material::Triangle
                        && mat.numclipplanes == 0)
                    waterSurfObjects.insert(draw.objectKey);
            }
        }
        // Shared animation clock of the water effects (caustics, surface
        // waves); FC_BGFX_CAUSTIC_TIME freezes it for deterministic
        // comparisons. animating() reports live animation so the viewer
        // keeps scheduling redraws.
        animatedFrame = false;
        float animTime = 0.0f;
        bool animLive = false;
        static const char *fixedTime = getenv("FC_BGFX_CAUSTIC_TIME");
        if (fixedTime) {
            animTime = float(atof(fixedTime));
        } else {
            using animclock = std::chrono::steady_clock;
            static const animclock::time_point start = animclock::now();
            animTime = std::chrono::duration<float>(
                           animclock::now() - start).count();
            animLive = true;
        }
        // The RenderDebug freeze-frame determinism switch
        // (docs/RenderDebug.md): a frozen clock renders every
        // time-animated effect (water waves, fire, caustics, splashes)
        // at t = 0, and animLive stays false so idle viewers stop
        // re-rendering — two frames of the same scene/camera/params
        // are then identical.
        if (debugconf.freezeFrame) {
            animTime = 0.0f;
            animLive = false;
        }
        // Publish the clock to user shaders: u_fcTime is recorded with
        // every consuming user draw (pushUserParams), and a user shader
        // referencing it keeps the animation loop alive like the stock
        // timed effects below.
        _BGFXLib.userTime[0] = animTime;
        _BGFXLib.userTime[1] = animLive ? 1.0f : 0.0f;
        _BGFXLib.userAnimatedDraw = false;
        // Fire lights the scene: an unshadowed point light per fire
        // body slot at the flame centroid, its brightness flickered on
        // the shared animation clock (frozen clocks stay deterministic)
        // and its color the flame ramp's bright zone. The mesh FS adds
        // them on top of the frame's lighting model — no shadow maps
        // from them, the usual engine effect-light shortcut.
        static const bool noFireLight =
            (getenv("FC_BGFX_NO_FIRELIGHT") != nullptr);
        for (int slot = 0; slot < kSlots; ++slot) {
            const FireSlot &fs = fireSlot[slot];
            if (!(fireActive && !noFireLight && fs.valid
                  && fs.diag > 0.0f)) {
                view->localLightView[slot][3] = 0.0f;
                continue;
            }
            const float *vm = reinterpret_cast<const float *>(viewMatrix);
            // The light anchor (a third up the flame along the body's
            // up axis, the ramp's bright zone) came out of the fire
            // scan's taper frame.
            for (int j = 0; j < 3; ++j)
                view->localLightView[slot][j] = fs.lightWorld[0] * vm[j]
                    + fs.lightWorld[1] * vm[4 + j]
                    + fs.lightWorld[2] * vm[8 + j]
                    + vm[12 + j];
            float range = 2.5f * fs.diag;
            view->localLightView[slot][3] = 1.0f / (range * range);
            // Flicker: a few incommensurate sines on the flame clock
            // (same 2.0 rise rate as the noise scroll), amplitude kept
            // above zero so the fire never blacks out. The slot index
            // offsets the phases so several fires don't pulse in step.
            float t = animTime * fs.speed * 2.0f + 3.1f * float(slot);
            float flicker = 0.80f
                + 0.20f * (0.55f * std::sin(t * 11.7f)
                           + 0.33f * std::sin(t * 7.3f + 1.7f)
                           + 0.12f * std::sin(t * 23.9f + 0.5f));
            float glow = fs.intensity * flicker;
            // fireRamp(0.6) of the volume shader: the flame's dominant
            // orange.
            view->localLightColorI[slot][0] = 1.00f * glow;
            view->localLightColorI[slot][1] = 0.72f * glow;
            view->localLightColorI[slot][2] = 0.13f * glow;
            view->localLightColorI[slot][3] = 0.0f;
        }
        // Light-source bodies (Render_Light): the upper half of the
        // local-light array — an unshadowed point light at each body's
        // bounds center, color = diffuse * intensity, steady (no
        // flicker). The draws are also collected for the bloom emit
        // pass (their HDR halo source re-render).
        std::vector<const Render::DrawCall *> bulbDraws;
        constexpr int kBulbSlots = BGFXView::kLocalLights
            - BGFXView::kMediumSlots;
        float bulbPos[kBulbSlots][3] = {};
        float bulbRangeW[kBulbSlots] = {};
        float bulbDiagW[kBulbSlots] = {};
        bool bulbWantShadow[kBulbSlots] = {};
        bool bulbWantShadowExt[kBulbSlots] = {};
        int bulbCount = 0;
        {
            int slot = 0;
            std::unordered_set<uint64_t> bulbObjects;
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if (!mat.lightsource || mat.ontop
                        || mat.type != Render::Material::Triangle)
                    continue;
                bulbDraws.push_back(&draw);
                if (slot >= BGFXView::kLocalLights
                                - BGFXView::kMediumSlots)
                    continue;
                // One light per object (a body's face/edge draws share
                // the object key).
                if (draw.objectKey
                        && !bulbObjects.insert(draw.objectKey).second)
                    continue;
                float dx = draw.bboxMax[0] - draw.bboxMin[0];
                float dy = draw.bboxMax[1] - draw.bboxMin[1];
                float dz = draw.bboxMax[2] - draw.bboxMin[2];
                float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
                    ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
                if (diag <= 0.0f)
                    continue;
                float cx = 0.5f * (draw.bboxMin[0] + draw.bboxMax[0]);
                float cy = 0.5f * (draw.bboxMin[1] + draw.bboxMax[1]);
                float cz = 0.5f * (draw.bboxMin[2] + draw.bboxMax[2]);
                float intensity = mat.lightintensity > 0.0f
                    ? mat.lightintensity : 1.0f;
                float range = mat.lightrange > 0.0f ? mat.lightrange
                                                    : 8.0f * diag;
                bulbPos[slot][0] = cx;
                bulbPos[slot][1] = cy;
                bulbPos[slot][2] = cz;
                bulbRangeW[slot] = range;
                bulbDiagW[slot] = diag;
                bulbWantShadow[slot] = mat.lightshadow;
                bulbWantShadowExt[slot] = mat.lightshadowext;
                bulbCount = slot + 1;
                int li = BGFXView::kMediumSlots + slot++;
                const float *vm =
                    reinterpret_cast<const float *>(viewMatrix);
                for (int j = 0; j < 3; ++j)
                    view->localLightView[li][j] = cx * vm[j]
                        + cy * vm[4 + j] + cz * vm[8 + j] + vm[12 + j];
                view->localLightView[li][3] = 1.0f / (range * range);
                float color[4];
                unpackColor(mat.diffuse, color);
                for (int j = 0; j < 3; ++j)
                    view->localLightColorI[li][j] = color[j] * intensity;
                view->localLightColorI[li][3] = 0.0f;
            }
            for (; slot < BGFXView::kLocalLights - BGFXView::kMediumSlots;
                 ++slot)
                view->localLightView[BGFXView::kMediumSlots + slot][3]
                    = 0.0f;
        }
        bool bloomActive = bloomconf.enabled;
        float waterWaveStrength = waterconf.waveStrength;
        float waterWaveScale = waterconf.waveScale;
        if (waterWaveScale <= 0.0f)
            waterWaveScale = waterDiag > 0.0f ? 4.0f / waterDiag : 1.0f;
        float waterSurfTime = animTime * waterconf.waveSpeed;
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx water surf: conf=%d body=%d active=%d prog=%d"
                    " scale=%g strength=%g time=%g\n",
                    waterconf.enabled, hasWaterBody, waterSurfActive,
                    bgfx::isValid(view->m_progWater), waterWaveScale,
                    waterWaveStrength, waterSurfTime);
        // Cached shadow map: the moments in shadowTex stay valid while
        // the light camera, the smoothing, and the caster set (mesh
        // content, transforms, ranges, clipping) are unchanged — camera
        // moves don't touch them, so the caster pass and blur only
        // re-run on scene/light edits (the large-assembly policy of the
        // GL Shadow style's cached SoShadowGroup map). The hash loop
        // mirrors the caster predicate of the submit loop below; the
        // hidden-line style adds fill-hiding rules resolved later, so
        // it just disables the caching.
        static const bool shadowNoCache =
            (getenv("FC_BGFX_SHADOW_NOCACHE") != nullptr);
        // Water bodies neither cast shadows (the medium needs the light
        // inside) nor act as ordinary surfaces while either water mode
        // (volumetric medium or surface) is active. Glass bodies are
        // exempt the same way while the glass pass runs (a tinting
        // colored-shadow approximation is future work — light passes
        // through for now).
        bool waterExempt = waterActive || waterSurfActive;
        auto mediumExempt = [&](const Render::Material &mat) {
            return (waterExempt && mat.water)
                || (glassActive && mat.glass)
                || (cloudActive && (mat.cloud || mat.fountain))
                || (fireActive && mat.fire);
        };
        // Caster-set content hash, shared by the scene shadow map and
        // the bulb shadow tiles (both re-render only when it changes).
        auto hashCasters = [&](uint64_t &h) {
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                // On-top draws cast too: a selected-on-top object's
                // scene draws are hidden and re-rendered on top, so
                // excluding them dropped its whole shadow (GL keeps it).
                // Glass draws stay in the hash: they feed the tint
                // map beside the moments (with their color).
                if (mat.type != Render::Material::Triangle
                        || !(mat.shadowstyle & 1)
                        || (waterExempt && mat.water)
                        || (cloudActive && (mat.cloud || mat.fountain))
                        || (fireActive && mat.fire)
                        || !draw.mesh)
                    continue;
                if (glassActive && mat.glass)
                    hashBytes(h, &mat.diffuse, sizeof(mat.diffuse));
                hashBytes(h, &draw.mesh->cacheId,
                          sizeof(draw.mesh->cacheId));
                if (!draw.identity)
                    hashBytes(h, draw.model, sizeof(float) * 16);
                hashBytes(h, &draw.indexStart, sizeof(draw.indexStart));
                hashBytes(h, &draw.indexCount, sizeof(draw.indexCount));
                if (mat.numclipplanes) {
                    hashBytes(h, &mat.numclipplanes, 1);
                    hashBytes(h, &mat.clipconcave, 1);
                    hashBytes(h, mat.clipplanes,
                              sizeof(float) * 4 * mat.numclipplanes);
                }
                // Autozoom casters rebuild their transform from the
                // per-frame world-to-screen scale.
                if (!mat.autozoom.empty())
                    hashBytes(h, &autozoomScale, sizeof(autozoomScale));
            }
        };
        bool shadowRender = shadowActive;
        if (shadowActive && !hlconfig.show && !shadowNoCache) {
            uint64_t h = 1469598103934665603ULL;
            hashBytes(h, lightViewMtx, sizeof(float) * 16);
            hashBytes(h, lightProjMtx, sizeof(float) * 16);
            hashBytes(h, &lightconf.smoothBorder,
                      sizeof(lightconf.smoothBorder));
            hashCasters(h);
            shadowRender = h != view->shadowMapHash;
            if (shadowRender)
                view->shadowMapHash = h;
            if (dbgshadow && !shadowRender)
                fprintf(stderr, "bgfx shadow map cached (%llx)\n",
                        (unsigned long long)h);
        } else if (shadowActive) {
            view->shadowMapHash = 0;
        }

        // Bulb shadow tiles (Render_LightShadow): each shadow-casting
        // bulb renders either one wide downward-cone tile, or — with
        // Render_LightShadowExtended — six world-axis cube faces, into
        // sequentially allocated atlas tiles. The camera-space receiver
        // matrices refresh every frame; a tile itself re-renders only
        // when its hash (bulb pose + face + caster set) changes — on a
        // static scene the steady-state cost is zero.
        bool bulbShadowRender[BGFXView::kBulbShadowTiles] = {};
        bool anyBulbShadow = false;
        {
            uint64_t casterH = 0;
            const auto *caps = bgfx::getCaps();
            // Camera view -> world rotation for the shader's cube-face
            // pick (directions only; w = 0 drops the translation).
            bx::mtxInverse(view->bulbShadowRotMtx,
                reinterpret_cast<const float *>(viewMatrix));
            // World-axis cube faces, order matched by the shader's
            // dominant-axis pick: +X -X +Y -Y +Z -Z.
            static const float kFaceFwd[6][3] = {
                {1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
            static const float kFaceUp[6][3] = {
                {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
                {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
            float invV[16];
            bx::mtxInverse(invV,
                reinterpret_cast<const float *>(viewMatrix));
            int nextTile = 0;
            for (int sl = 0; sl < kBulbSlots; ++sl) {
                bool want = shadowActive && sl < bulbCount
                    && bulbWantShadow[sl] && bulbRangeW[sl] > 0.0f
                    && bgfx::isValid(view->bulbShadowFbo);
                // 6 cube faces when extended; fall back to the plain
                // downward tile when the atlas can't fit them all.
                int faces = want ? (bulbWantShadowExt[sl] ? 6 : 1) : 0;
                if (faces > BGFXView::kBulbShadowTiles - nextTile)
                    faces = BGFXView::kBulbShadowTiles - nextTile >= 1
                        ? 1 : 0;
                view->bulbShadowConf[sl][0] = float(nextTile);
                view->bulbShadowConf[sl][1] = float(faces);
                view->bulbShadowConf[sl][2] = 0.0f;
                // .w = the format-dependent variance floor of the
                // plain-VSM tap: fp16 moments quantize the z^2 moment
                // to ~2^-12 steps, so the RG16F fallback needs a floor
                // well above that noise or the Chebyshev test speckles
                // along contact terminators.
                view->bulbShadowConf[sl][3] =
                    view->shadowFormat == bgfx::TextureFormat::RG32F
                        ? 1.0e-5f : 3.0e-4f;
                if (!faces)
                    continue;
                // Light camera per face. Near starts outside the bulb
                // body so the emitter doesn't shadow itself.
                bx::Vec3 eye(bulbPos[sl][0], bulbPos[sl][1],
                             bulbPos[sl][2]);
                float near = bx::max(0.55f * bulbDiagW[sl],
                                     0.01f * bulbRangeW[sl]);
                float far = bx::max(1.5f * bulbRangeW[sl], near * 4.0f);
                // Cube faces cover 90 deg; 100 leaves margin past the
                // diagonal so the bounds guard never opens a seam.
                float fovy = faces == 6 ? 100.0f : 130.0f;
                for (int f = 0; f < faces; ++f) {
                    int t = nextTile + f;
                    bx::Vec3 fwd = faces == 6
                        ? bx::Vec3(kFaceFwd[f][0], kFaceFwd[f][1],
                                   kFaceFwd[f][2])
                        : bx::Vec3(0.0f, 0.0f, -1.0f);
                    bx::Vec3 up = faces == 6
                        ? bx::Vec3(kFaceUp[f][0], kFaceUp[f][1],
                                   kFaceUp[f][2])
                        : bx::Vec3(0.0f, 1.0f, 0.0f);
                    bx::mtxLookAt(view->bulbShadowViewMtx[t], eye,
                                  bx::add(eye, fwd), up);
                    bx::mtxProj(view->bulbShadowProjMtx[t], fovy, 1.0f,
                                near, far, caps->homogeneousDepth);
                    // Camera view space -> atlas tile uv/depth.
                    float tmp[16], tmp2[16];
                    bx::mtxMul(tmp, invV, view->bulbShadowViewMtx[t]);
                    bx::mtxMul(tmp2, tmp, view->bulbShadowProjMtx[t]);
                    const float sc = 0.5f / float(BGFXView::kBulbShadowGrid);
                    const float sy = caps->originBottomLeft ? sc : -sc;
                    const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
                    const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
                    const float tx = sc
                        + 2.0f * sc * float(t % BGFXView::kBulbShadowGrid);
                    const float ty = sc
                        + 2.0f * sc * float(t / BGFXView::kBulbShadowGrid);
                    const float crop[16] = {
                        sc,   0.0f, 0.0f, 0.0f,
                        0.0f, sy,   0.0f, 0.0f,
                        0.0f, 0.0f, sz,   0.0f,
                        tx,   ty,   tz,   1.0f,
                    };
                    bx::mtxMul(view->bulbShadowMtx[t], tmp2, crop);
                    // Tile cache: pose + range + face + caster set.
                    if (!casterH) {
                        casterH = 1469598103934665603ULL;
                        hashCasters(casterH);
                    }
                    uint64_t h = casterH;
                    hashBytes(h, bulbPos[sl], sizeof(bulbPos[sl]));
                    hashBytes(h, &bulbRangeW[sl], sizeof(bulbRangeW[sl]));
                    hashBytes(h, &f, sizeof(f));
                    hashBytes(h, &faces, sizeof(faces));
                    bulbShadowRender[t] = !view->bulbShadowValid[t]
                        || h != view->bulbShadowHash[t];
                    view->bulbShadowHash[t] = h;
                    view->bulbShadowValid[t] = true;
                    anyBulbShadow = anyBulbShadow || bulbShadowRender[t];
                }
                nextTile += faces;
                // Flag the mesh shader to tap this slot's tiles.
                view->localLightColorI[BGFXView::kMediumSlots + sl][3]
                    = 1.0f;
            }
            // Unused tiles: invalidate and zero so nothing taps them.
            for (int t = nextTile; t < BGFXView::kBulbShadowTiles; ++t) {
                view->bulbShadowValid[t] = false;
                view->bulbShadowHash[t] = 0;
                std::memset(view->bulbShadowMtx[t], 0,
                            sizeof(view->bulbShadowMtx[t]));
            }
        }

        // ShadowSmoothBorder > 0 runs the separable blur over the fresh
        // moments right after the caster pass.
        bool shadowBlurActive = shadowRender
            && lightconf.smoothBorder > 0.0f
            && bgfx::isValid(view->shadowBlurFbo);

        // The prepass rasterizes for SSAO and/or the volumetric ray
        // ends; the AO resolve chain itself stays SSAO-gated. The water
        // surface pass also reads its viewZ, to reject refraction
        // samples landing on geometry in front of the surface (the
        // not-submerged parts of protruding objects would smear).
        static const bool noWaterReject =
            (getenv("FC_BGFX_NO_WATER_REJECT") != nullptr);
        bool waterSurfReject = waterSurfActive && view->m_ssao
            && !noWaterReject;
        bool glassReject = glassActive && !noWaterReject;
        bool prepassActive = ssaoActive || volActive || waterSurfReject
            || glassReject;

        // AO/prepass cache (same pattern as the shadow-map hash above):
        // the depth/normal prepass and the whole AO resolve chain
        // (pyramid, gen, denoise) depend only on the camera, the
        // viewport, the AO parameters and the prepass draw set — never
        // on time. When none of those changed, skip them all and keep
        // last frame's targets: animated effects (water waves, fire)
        // re-render every frame but read the cached prepass/AO, so a
        // static camera pays only the cheap AO apply multiply. The
        // interaction fast path needs no special casing — aoconf.fast
        // flips the hash (and the camera moves anyway), and the first
        // idle frame recomputes at full quality and re-primes the
        // cache.
        static const bool aoNoCache =
            (getenv("FC_BGFX_NO_AO_CACHE") != nullptr);
        bool aoRender = prepassActive;
        if (prepassActive && !hlconfig.show && !aoNoCache) {
            uint64_t h = 1469598103934665603ULL;
            hashBytes(h, reinterpret_cast<const float *>(viewMatrix),
                      sizeof(float) * 16);
            hashBytes(h, reinterpret_cast<const float *>(projMatrix),
                      sizeof(float) * 16);
            hashBytes(h, &width, sizeof(width));
            hashBytes(h, &height, sizeof(height));
            hashBytes(h, &view->ssaoW, sizeof(view->ssaoW));
            hashBytes(h, &view->ssaoH, sizeof(view->ssaoH));
            hashBytes(h, &aoRadius, sizeof(aoRadius));
            hashBytes(h, &aoconf.intensity, sizeof(aoconf.intensity));
            hashBytes(h, &aoconf.method, sizeof(aoconf.method));
            hashBytes(h, &aoconf.fast, sizeof(aoconf.fast));
            hashBytes(h, &aoconf.slices, sizeof(aoconf.slices));
            hashBytes(h, &aoconf.steps, sizeof(aoconf.steps));
            const bool consumers[4] = {ssaoActive, volActive,
                                       waterSurfReject, glassReject};
            hashBytes(h, consumers, sizeof(consumers));
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                // The prepass draw filter (see the submit below):
                // opaque scene triangles, media bodies exempt.
                const bool transp = mat.transparent
                    || (mat.pervertexcolor && draw.mesh
                        && draw.mesh->hasTransparency);
                if (mat.type != Render::Material::Triangle
                        || transp || mediumExempt(mat)
                        || !draw.mesh)
                    continue;
                hashBytes(h, &draw.mesh->cacheId,
                          sizeof(draw.mesh->cacheId));
                if (!draw.identity)
                    hashBytes(h, draw.model, sizeof(float) * 16);
                hashBytes(h, &draw.indexStart, sizeof(draw.indexStart));
                hashBytes(h, &draw.indexCount, sizeof(draw.indexCount));
                if (mat.numclipplanes) {
                    hashBytes(h, &mat.numclipplanes, 1);
                    hashBytes(h, &mat.clipconcave, 1);
                    hashBytes(h, mat.clipplanes,
                              sizeof(float) * 4 * mat.numclipplanes);
                }
                if (!mat.autozoom.empty())
                    hashBytes(h, &autozoomScale, sizeof(autozoomScale));
            }
            aoRender = h != view->aoMapHash;
            if (aoRender)
                view->aoMapHash = h;
        } else if (prepassActive) {
            view->aoMapHash = 0;
        }
        // User "post" stage shader (docs/RenderDebug.md §6): the last
        // captured post-stage program wins; resolved through the
        // runtime shaderc compile cache on desktop builds and from the
        // snapshot-shipped server-compiled binaries on the viewer
        // tier. A failed compile keeps the pass off.
        const Render::UserShader *userPost = nullptr;
        for (const auto &s : usershaderconf.shaders) {
            if (s.stage == "post" && !s.fragmentSource.empty())
                userPost = &s;
        }
        bgfx::ProgramHandle userPostProg = BGFX_INVALID_HANDLE;
#ifndef FC_RENDERER_STANDALONE
        userShaderGen = _BGFXLib.userCompileGeneration;
#endif
        if (userPost)
            userPostProg = _BGFXLib.getUserProgram(*userPost,
                                                   "vs_fc_comp");
        const bool userPostActive = userPost
            && bgfx::isValid(userPostProg)
            && bgfx::isValid(view->sceneCopyFbo)
            && bgfx::isValid(view->m_progWaterCopy);

        const bool prepassRender = prepassActive && aoRender;

        // Debug scene re-render (docs/RenderDebug.md modes 6/8): the
        // counting/UV rasterization runs every frame while its mode is
        // active — debug-only work, no caching.
        const bool debugSceneRender = (debugconf.viewMode == 6
                                       || debugconf.viewMode == 8)
            && view->ensureDebugScene();

        // Ground reflection: mirror the world about the shadow ground
        // plane (z = scene bbox bottom, the plane the ground quad sits
        // on) and re-render the opaque scene with the original camera —
        // the ground then acts as a window into the mirrored world. The
        // shadow lookup of the mirrored draws needs the shadow matrix
        // rebased from the mirrored view space (mirrored-view -> world
        // -> original-view -> shadow uv).
        bool groundReflActive = shadowActive && lightconf.ground
            && lightconf.groundReflection && bboxValid && !hlconfig.show
            && bgfx::isValid(view->m_progGroundRefl)
            && bgfx::isValid(view->reflFbo);
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx ground refl: conf=%d ground=%d shadow=%d"
                    " active=%d intensity=%g\n",
                    lightconf.groundReflection, lightconf.ground,
                    shadowActive, groundReflActive,
                    lightconf.groundReflectionIntensity);
        float reflViewMtx[16], reflShadowMtx[16];
        if (groundReflActive) {
            const float *vm = reinterpret_cast<const float *>(viewMatrix);
            float S[16];
            bx::mtxIdentity(S);
            S[10] = -1.0f;
            S[14] = 2.0f * bboxMin[2];
            bx::mtxMul(reflViewMtx, S, vm);
            float invV[16], m1[16], m2[16];
            bx::mtxInverse(invV, vm);
            bx::mtxMul(m1, invV, S);
            bx::mtxMul(m2, m1, vm);
            bx::mtxMul(reflShadowMtx, m2, view->shadowMtx);
        }

        // Water planar reflection: mirror the world about the water body's
        // top plane and re-render the opaque scene into the same reflection
        // target, so the water surface shader samples a true reflection (no
        // SSR taper). Reuses the ground-reflection FBO/pass; only when the
        // ground reflection is not itself using them (single mirror plane
        // per frame). Assumes a horizontal water surface, like the ground.
        bool waterReflActive = waterSurfActive && waterconf.reflection
            && waterconf.planarReflection && waterPlaneSet
            && !groundReflActive && bboxValid && !hlconfig.show
            && bgfx::isValid(view->m_progGroundRefl)
            && bgfx::isValid(view->reflFbo);
        // Reflection mode fed to the water shader: 0 off, 1 environment
        // cubemap, 2 screen-space (march), 3 planar (mirror pass). Planar
        // requested but unavailable (e.g. ground reflection using the
        // shared target) degrades to the environment cubemap.
        int waterReflMode = 0;
        if (waterSurfActive && waterconf.reflection)
            waterReflMode = waterReflActive ? 3
                : (waterconf.planarReflection ? 1 : 2);
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx water refl: mode=%d active=%d surf=%d refl=%d "
                    "planar=%d planeSet=%d groundRefl=%d bbox=%d hl=%d "
                    "prog=%d fbo=%d\n",
                    waterReflMode, int(waterReflActive),
                    int(waterSurfActive), int(waterconf.reflection),
                    int(waterconf.planarReflection), int(waterPlaneSet),
                    int(groundReflActive), int(bboxValid),
                    int(hlconfig.show),
                    int(bgfx::isValid(view->m_progGroundRefl)),
                    int(bgfx::isValid(view->reflFbo)));

        // A water surface whose wave normals have nothing to act on.
        // Refraction bends the scene behind the surface by the normal,
        // reflection aims by it, the glint needs it against a light —
        // with all three gone the shader still runs, but every wave,
        // ripple and impact ring resolves to the same flat tinted
        // sheet. Nothing about that is an error (each switch documents
        // "off = flat water color"), yet turning up the wave strength
        // and seeing not one pixel move reads exactly like a broken
        // renderer, and has cost this project a misdiagnosis already.
        // So say it, once when the state is entered rather than every
        // frame, and name the switches that would give the waves
        // something to do.
        const bool waterMute = waterSurfActive
            && !waterconf.refraction && waterReflMode == 0
            && !lightconf.valid;
        if (waterMute != view->waterNoResponse) {
            view->waterNoResponse = waterMute;
            if (waterMute) {
                const char *msg = "water surface: refraction and "
                    "reflection are both off and no scene light is "
                    "active, so the surface shades flat and the wave, "
                    "ripple and impact settings cannot change a pixel "
                    "(WaterRefraction / WaterReflection / the Shadow "
                    "draw style)\n";
#ifdef FC_RENDERER_STANDALONE
                std::printf("%s", msg);
#else
                Base::Console().Warning("%s", msg);
#endif
            }
        }

        float waterReflViewMtx[16], waterReflShadowMtx[16];
        if (waterReflActive) {
            const float *vm = reinterpret_cast<const float *>(viewMatrix);
            float S[16];
            bx::mtxIdentity(S);
            S[10] = -1.0f;
            S[14] = 2.0f * waterPlaneZ;
            bx::mtxMul(waterReflViewMtx, S, vm);
            float invV[16], m1[16], m2[16];
            bx::mtxInverse(invV, vm);
            bx::mtxMul(m1, invV, S);
            bx::mtxMul(m2, m1, vm);
            bx::mtxMul(waterReflShadowMtx, m2, view->shadowMtx);
        }

        // Medium-interval / planar-reflection frame cache: those targets
        // depend only on the camera, the viewport and the scene draws.
        // The fire/cloud/water animation lives in the volume raymarch
        // and the surface shaders, not in the interval depths, and the
        // mirrored-scene render is static per camera (the water waves
        // only distort how the surface samples it). Re-render when the
        // camera or viewport changed or any scene/config change was
        // applied this frame (dirtyChanged covers every mutation path);
        // the mirrored scene additionally re-renders whenever the shadow
        // map did (its shading includes the shadow lookup) and while a
        // fire burns (the flickering fire effect light shades the
        // mirrored geometry per frame).
        static const bool staticNoCache =
            (getenv("FC_BGFX_NO_STATIC_CACHE") != nullptr);
        uint64_t camH = 1469598103934665603ULL;
        hashBytes(camH, reinterpret_cast<const float *>(viewMatrix),
                  sizeof(float) * 16);
        hashBytes(camH, reinterpret_cast<const float *>(projMatrix),
                  sizeof(float) * 16);
        hashBytes(camH, &width, sizeof(width));
        hashBytes(camH, &height, sizeof(height));
        const bool staticFrame = !dirtyChanged && !hlconfig.show
            && !staticNoCache && camH == view->camFrameHash;
        view->camFrameHash = camH;
        const bool mediumRender = !staticFrame;

        // Which passes this frame draws (BGFXView's pass map) and how
        // each one's bgfx view is configured, declared as ONE table.
        // Every pass states its liveness predicate exactly once, next
        // to the closure that configures its view: the mark phase and
        // the view-config phase (after the particle step below) both
        // iterate this table, so they cannot drift apart -- the
        // predicate that claims a pass's id is the predicate that
        // configures it, and a pass the table does not declare cannot
        // render at all (it reports itself once instead).
        //
        // Nothing may submit before mapPasses() below: the pass ->
        // bgfx-view-id mapping is decided there, and only the passes
        // declared live get an id of their own. Everything else maps
        // to the discard view, so a submit the table did not predict
        // costs that pass its pixels and prints the pass number -- it
        // cannot bleed into another pass.
        //
        // Passes whose use depends on the draws rather than on
        // configuration (which bucket a mesh lands in, whether anything
        // has an outline) are simply always claimed: one id each, and
        // the point of the exercise is the groups that come in sixes
        // and sixteens.
        using V = BGFXView;
        // Decided after the particle step (it reads the emitter
        // liveness the step updates); the reflection entry's config
        // closure reads it by reference at config time, which runs
        // after the assignment.
        bool reflRender = false;

        // The shared tail of every pass that renders into the scene
        // framebuffer (or falls back to it): viewport rect, camera
        // transforms and submission-order mode.
        auto configTail = [&](int i, uint16_t id) {
            // The SSAO generate/blur passes render into aoTex/aoBlurTex
            // at their own Render_SSAOResolution (ssaoW/ssaoH, default
            // full-res and independent of the reflection scale); the
            // mesh draws sample the result back at normalized uv. The
            // reduced-resolution reflection re-render sets its own rect
            // in its own closure.
            bool aoResolveView = i == V::ViewAOGen
                || i == V::ViewAOBlur
                || i == V::ViewAOBlur2;
            bgfx::setViewRect(id, 0, 0,
                aoResolveView ? view->ssaoW : width,
                aoResolveView ? view->ssaoH : height);
            // The background quad and the OIT composite triangle are
            // submitted in clip space; the AO generation pass keeps the
            // scene projection for its predefined u_proj (position
            // reconstruction).
            // The GTAO denoise passes (AOBlur/AOBlur2) keep the scene
            // projection like the gen pass: their plane-aware bilateral
            // weight reconstructs view positions from u_proj (the shared
            // fullscreen vertex shader ignores the matrices).
            // The environment background keeps the scene matrices on
            // the background view: its fragment shader reconstructs
            // per-pixel world directions from u_proj/u_invView.
            if ((i == V::ViewBackground
                    && !(pbrActive && pbrconf.envBackground))
                    || i == V::ViewOITComposite)
                bgfx::setViewTransform(id, nullptr, nullptr);
            else
                bgfx::setViewTransform(id, viewMatrix, projMatrix);
            // On-top and highlight draws are blended painter-style: keep
            // submission order (GL pass order) instead of state sorting.
            // With OIT the transparent blend is commutative, so no
            // depth sorting is needed there either. The outline and
            // section-cap views interleave stencil mark/fill/cleanup
            // passes per entry, so they must keep submission order too.
            // The volumetric apply view is sequential too: the
            // per-channel extinction multiply must land before the
            // inscatter add.
            // Blended sprites are painted back to front for the same
            // reason a non-OIT transparent bucket is: alpha blending is
            // not commutative (additive emitters do not care).
            bgfx::setViewMode(id,
                (i == V::ViewTransparent && !oitActive)
                        || i == V::ViewParticles
                    ? bgfx::ViewMode::DepthDescending
                    : i >= V::ViewOnTop
                            || i == V::ViewSelection
                            || i == V::ViewOutline
                            || i == V::ViewSectionCap
                            || i == V::ViewSectionCapTransp
                            || i == V::ViewVolApply
                        ? bgfx::ViewMode::Sequential
                        : bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        // Passes that render into the scene framebuffer -- and the
        // benign fallback for a claimed pass whose special target is
        // unavailable this frame (it draws nothing there). The
        // section-cap views clear the stencil: their parity marking
        // (INVERT) needs a zeroed base, and earlier outline passes
        // leave stale marks behind (relevant for the transparent cap
        // view, which runs after ViewOutline). The frame's one clear of
        // the scene framebuffer belongs to the first view that draws
        // into it -- named, not index 0: the particle state views
        // precede it.
        auto configScene = [&](int i, uint16_t id) {
            bgfx::setViewFrameBuffer(id, view->bgfxFbo);
            bgfx::setViewClear(id,
                i == V::ViewBackground
                    ? uint16_t(BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
                               |BGFX_CLEAR_STENCIL)
                : (i == V::ViewSectionCap
                   || i == V::ViewSectionCapTransp)
                    ? uint16_t(BGFX_CLEAR_STENCIL)
                    : uint16_t(BGFX_CLEAR_NONE),
                clearColor, 1.0f, 0);
            configTail(i, id);
        };
        auto configTransparent = [&](int i, uint16_t id) {
            if (!oitActive) {
                configScene(i, id);
                return;
            }
            // Accumulation targets: accum clears to 0, revealage
            // to 1; the shared depth attachment is not cleared.
            bgfx::setViewFrameBuffer(id, view->oitFbo);
            bgfx::setPaletteColor(0, 0.0f, 0.0f, 0.0f, 0.0f);
            bgfx::setPaletteColor(1, 1.0f, 1.0f, 1.0f, 1.0f);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_COLOR),
                               1.0f, 0, 0, 1);
            configTail(i, id);
        };
        auto configShadow = [&](int, uint16_t id) {
            // Moments clear to the warped far plane
            // (exp(c), exp(2c)) through the palette (the packed
            // clear color cannot exceed 1), own depth; the caster
            // pass renders under the light camera at the shadow
            // map size.
            float evsmClear[4] = {std::exp(view->shadowWarpFrame),
                                  std::exp(2.0f * view->shadowWarpFrame),
                                  0.0f, 0.0f};
            bgfx::setPaletteColor(2, evsmClear);
            bgfx::setViewFrameBuffer(id, view->shadowFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                1.0f, 0, 2);
            bgfx::setViewRect(id, 0, 0, view->shadowSize,
                              view->shadowSize);
            bgfx::setViewTransform(id, lightViewMtx, lightProjMtx);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configShadowTint = [&](int i, uint16_t id) {
            if (!bgfx::isValid(view->shadowTintFbo)) {
                configScene(i, id);
                return;
            }
            // Glass shadow tint map: cleared to white (no glass =
            // full transmittance) under the light camera; glass
            // casters multiply their transmittance in.
            bgfx::setViewFrameBuffer(id, view->shadowTintFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_COLOR),
                               0xffffffffu, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, view->shadowSize,
                              view->shadowSize);
            bgfx::setViewTransform(id, lightViewMtx, lightProjMtx);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configShadowBlur = [&](int i, uint16_t id) {
            // Fullscreen blur passes over the shadow map size; the
            // triangle overwrites every pixel, so no clear.
            bgfx::setViewFrameBuffer(id,
                i == V::ViewShadowBlurH
                    ? view->shadowBlurFbo
                : i == V::ViewShadowBlurV
                    ? view->shadowBlurBackFbo
                : i == V::ViewShadowTintBlurH
                    ? view->shadowTintBlurFbo
                    : view->shadowTintBlurBackFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, view->shadowSize,
                              view->shadowSize);
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configBulb = [&](int i, uint16_t id) {
            // Bulb shadow tile: plain VSM moments cleared to the
            // far plane (1, 1) through the palette, tile subrect of
            // the atlas, the bulb's light camera.
            int t = i - V::ViewBulbShadow0;
            float vsmClear[4] = {1.0f, 1.0f, 0.0f, 0.0f};
            bgfx::setPaletteColor(3, vsmClear);
            bgfx::setViewFrameBuffer(id, view->bulbShadowFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                1.0f, 0, 3);
            // The receiver crop matrix addresses tile row t/grid
            // from the bottom of the atlas. On bottom-left-origin
            // backends (GL) the view rect's top-left y is flipped
            // to a GL viewport row from the bottom, so mirror the
            // row for the sampled v to land on the tile.
            int grid = V::kBulbShadowGrid;
            int tileRow = bgfx::getCaps()->originBottomLeft
                ? grid - 1 - t / grid : t / grid;
            bgfx::setViewRect(id,
                uint16_t((t % grid) * V::kBulbShadowTileSize),
                uint16_t(tileRow * V::kBulbShadowTileSize),
                V::kBulbShadowTileSize,
                V::kBulbShadowTileSize);
            bgfx::setViewTransform(id, view->bulbShadowViewMtx[t],
                                   view->bulbShadowProjMtx[t]);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configAOPrepass = [&](int i, uint16_t id) {
            // Prepass target clears to 0 (.w = 0 marks background
            // in the AO pass), with its own depth buffer.
            bgfx::setViewFrameBuffer(id, view->aoPrepassFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                0x00000000u, 1.0f, 0);
            configTail(i, id);
        };
        auto configAOMip = [&](int i, uint16_t id) {
            // GTAO depth pyramid downsamples: each level renders a
            // clip-space fullscreen triangle into its own half-stepped
            // single-channel target (no-op views otherwise).
            // Only the levels the frame claimed are here (a level
            // past aoMipCount, or a frame with the AO chain off, is
            // simply not mapped).
            const int m = i - V::ViewAODepthMip1;
            bgfx::setViewFrameBuffer(id, view->aoMipFbo[m]);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0,
                uint16_t(std::max(1, width >> (m + 1))),
                uint16_t(std::max(1, height >> (m + 1))));
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configAOChain = [&](int i, uint16_t id) {
            // Fullscreen passes overwrite their whole target.
            // ViewAOBlur2 ping-pongs the denoise back into aoTex.
            bgfx::setViewFrameBuffer(id,
                i == V::ViewAOBlur ? view->aoBlurFbo
                                   : view->aoGenFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            configTail(i, id);
        };
        auto configMedium = [&](int i, uint16_t id) {
            // Water/glass/cloud/fire body interval depth targets:
            // color clears to 0 (.w = 0 = no medium on this pixel);
            // the back-face views keep the farthest depth, so their
            // depth buffer clears to 0 and tests GREATER.
            bool back = i == V::ViewWaterBack || i == V::ViewGlassBack
                || i == V::ViewCloudBack || i == V::ViewFireBack;
            bgfx::setViewFrameBuffer(id,
                i == V::ViewWaterFront ? view->waterFrontFbo
                : i == V::ViewWaterBack ? view->waterBackFbo
                : i == V::ViewGlassFront ? view->glassFrontFbo
                : i == V::ViewGlassBack ? view->glassBackFbo
                : i == V::ViewCloudFront ? view->cloudFrontFbo
                : i == V::ViewCloudBack ? view->cloudBackFbo
                : i == V::ViewFireFront ? view->fireFrontFbo
                                        : view->fireBackFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                0x00000000u, back ? 0.0f : 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, viewMatrix, projMatrix);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configVolGen = [&](int, uint16_t id) {
            // Half-res raymarch target; the fullscreen triangle
            // overwrites every pixel, and the scene transforms stay
            // bound for the predefined u_proj (ray reconstruction,
            // like the AO generation pass).
            bgfx::setViewFrameBuffer(id, view->volFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0,
                uint16_t(std::max(1, int(width) / 2)),
                uint16_t(std::max(1, int(height) / 2)));
            bgfx::setViewTransform(id, viewMatrix, projMatrix);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configVolAccum = [&](int i, uint16_t id) {
            if (!bgfx::isValid(view->volHistFbo)) {
                configScene(i, id);
                return;
            }
            // History accumulation target, same half-res rect as
            // the raymarch; the blended quad overwrites (or blends
            // into) every pixel, so no clear.
            bgfx::setViewFrameBuffer(id, view->volHistFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0,
                uint16_t(std::max(1, int(width) / 2)),
                uint16_t(std::max(1, int(height) / 2)));
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configRefl = [&](int i, uint16_t id) {
            // A cached mirror frame (reflRender false) re-renders
            // nothing: the target keeps its content and the claimed id
            // keeps the benign default configuration. The media
            // composite additionally needs an analytic medium to march.
            if (!reflRender
                    || (i == V::ViewReflMedia
                        && !(volActive && (cloudActive || fireActive)))) {
                configScene(i, id);
                return;
            }
            if (i == V::ViewReflMedia) {
                // Media composite over the just-rendered mirror scene:
                // same target/rect/transforms, no clear (premultiplied
                // over blend).
                bgfx::setViewFrameBuffer(id, view->reflFbo);
                bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                                   clearColor, 1.0f, 0);
            } else {
                // Mirrored-scene render: own color (cleared to alpha 0 =
                // nothing reflected) + depth, the original projection
                // over the mirrored view (ground plane or water plane).
                bgfx::setViewFrameBuffer(id, view->reflFbo);
                bgfx::setViewClear(id,
                    uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                    0x00000000u, 1.0f, 0);
            }
            // Reduced-resolution reflection re-render (matches reflFbo).
            bgfx::setViewRect(id, 0, 0, view->effW, view->effH);
            bgfx::setViewTransform(id,
                groundReflActive ? reflViewMtx : waterReflViewMtx,
                projMatrix);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configWaterCopy = [&](int, uint16_t id) {
            // Fullscreen copy of the scene color; the framebuffer
            // switch also resolves a multisampled scene attachment
            // before the surface pass samples the copy.
            bgfx::setViewFrameBuffer(id, view->sceneCopyFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configUserPostCopy = [&](int, uint16_t id) {
            // User post input: resolve/copy of the composited (post
            // bloom) scene color into the shared sceneCopy target;
            // like the water copy, the framebuffer switch resolves a
            // multisampled scene attachment before the copy samples.
            bgfx::setViewFrameBuffer(id, view->sceneCopyFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configUserPost = [&](int, uint16_t id) {
            // The user post program draws fullscreen back into the
            // scene target.
            bgfx::setViewFrameBuffer(id, view->bgfxFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configBloom = [&](int i, uint16_t id) {
            if (!bgfx::isValid(view->bloomFbo)) {
                configScene(i, id);
                return;
            }
            // Quarter-res bloom chain: bright/emit into the halo
            // source, blur ping-pongs through the second target.
            // The fullscreen passes overwrite every pixel but the
            // emit pass blends into the bright result, so only the
            // source clears (via the bright overwrite) -- no view
            // clear needed anywhere. The emit pass renders the
            // light bodies under the scene camera.
            bgfx::setViewFrameBuffer(id,
                i == V::ViewBloomBlurH
                    ? view->bloomBlurFbo : view->bloomFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0,
                uint16_t(std::max(1, int(width) / 4)),
                uint16_t(std::max(1, int(height) / 4)));
            if (i == V::ViewBloomEmit)
                bgfx::setViewTransform(id, viewMatrix, projMatrix);
            else
                bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };
        auto configDebugScene = [&](int i, uint16_t id) {
            // Fresh count/UV target every frame: the overdraw
            // counts accumulate from zero, .w = 0 marks pixels the
            // UV re-render did not cover.
            bgfx::setViewFrameBuffer(id, view->debugSceneFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                0x00000000u, 1.0f, 0);
            configTail(i, id);
        };
        auto configOverlay = [&](int i, uint16_t id) {
            // Overlay feed slot: derive the viewport rect and the
            // camera from the declarative anchor each frame, so
            // overlays re-anchor on resize and (orientFromScene)
            // follow the current camera -- including the WASM
            // viewer's own orbit camera.
            // Only the slots the frame's overlays fill are mapped.
            int slot = i - V::ViewOverlay0;
            auto ovIt = overlays.begin();
            std::advance(ovIt, slot);
            const Render::OverlayAnchor *anchor = &ovIt->second.anchor;
            if (anchor->sceneCamera) {
                // In-scene overlay (editing graph, dimensions): draw over
                // the whole viewport with the main scene camera so the
                // world-space geometry lines up with the finished scene,
                // on a fresh depth buffer so it sits on top but still
                // depth-tests within itself.
                bgfx::setViewFrameBuffer(id, view->bgfxFbo);
                bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_DEPTH),
                                   clearColor, 1.0f, 0);
                bgfx::setViewRect(id, 0, 0, width, height);
                bgfx::setViewTransform(id, viewMatrix, projMatrix);
                bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
                bgfx::touch(id);
                return;
            }
            uint16_t rx = 0, ry = 0, rw = width, rh = height;
            if (anchor->corner != Render::OverlayAnchor::FullViewport) {
                uint16_t edge = uint16_t(std::max(1.0f,
                    anchor->sizeFraction
                        * float(std::min(width, height))));
                rw = rh = edge;
                bool right =
                    anchor->corner == Render::OverlayAnchor::BottomRight
                    || anchor->corner == Render::OverlayAnchor::TopRight;
                bool top =
                    anchor->corner == Render::OverlayAnchor::TopLeft
                    || anchor->corner == Render::OverlayAnchor::TopRight;
                // bgfx view rects are top-left anchored.
                int mx = int(anchor->marginX);
                int my = int(anchor->marginY);
                rx = uint16_t(std::max(0,
                    right ? width - edge - mx : mx));
                ry = uint16_t(std::max(0,
                    top ? my : height - edge - my));
            }
            const auto *caps = bgfx::getCaps();
            float ovProj[16];
            float aspect = float(rw) / float(rh);
            // Right-handed like the GL view matrix convention the
            // anchor camera follows (bx defaults to left-handed).
            if (anchor->pixelSpace) {
                // One unit == one pixel, origin top-left, y down (Qt
                // widget coordinates); z=0 content sits mid-range.
                bx::mtxOrtho(ovProj, 0.0f, float(rw), float(rh), 0.0f,
                             -1.0f, 1.0f, 0.0f, caps->homogeneousDepth,
                             bx::Handedness::Right);
            } else if (anchor->fovDeg > 0.0f) {
                bx::mtxProj(ovProj, anchor->fovDeg, aspect,
                            std::max(anchor->nearPlane, 1.0e-3f),
                            anchor->farPlane, caps->homogeneousDepth,
                            bx::Handedness::Right);
            } else {
                float hh = 0.5f * anchor->orthoHeight;
                float hw = hh * aspect;
                bx::mtxOrtho(ovProj, -hw, hw, -hh, hh,
                             anchor->nearPlane, anchor->farPlane,
                             0.0f, caps->homogeneousDepth,
                             bx::Handedness::Right);
            }
            float ovView[16];
            bx::mtxIdentity(ovView);
            if (!anchor->pixelSpace
                && anchor->orientFromScene && viewMatrix) {
                // Rotation part of the scene view matrix (rigid:
                // upper-left 3x3), translation dropped -- the axis
                // cross tracks the camera orientation only.
                const float *v =
                    reinterpret_cast<const float *>(viewMatrix);
                for (int c = 0; c < 3; ++c)
                    for (int r = 0; r < 3; ++r)
                        ovView[c * 4 + r] = v[c * 4 + r];
            }
            if (!anchor->pixelSpace)
                ovView[14] = -anchor->cameraDistance;
            bgfx::setViewFrameBuffer(id, view->bgfxFbo);
            // Fresh depth inside the overlay rect: overlays draw on
            // top of the finished frame but depth-test within
            // themselves.
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_DEPTH),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, rx, ry, rw, rh);
            bgfx::setViewTransform(id, ovView, ovProj);
            bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
            bgfx::touch(id);
        };
        auto configPresent = [&](int, uint16_t id) {
            // Standalone present: the default backbuffer; the
            // fullscreen triangle overwrites every pixel.
            //
            // On desktop no present is drawn, but the empty view still
            // targets the default backbuffer ON PURPOSE: bgfx only
            // resolves an MSAA framebuffer (multisampled renderbuffer
            // -> resolve texture) when the frame transitions AWAY from
            // it, and every desktop content view targets bgfxFbo -- so
            // without this trailing view the resolve texture the
            // composite blit reads stayed stale under MSAA (an empty
            // viewport).
            bgfx::setViewFrameBuffer(id, BGFX_INVALID_HANDLE);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, nullptr, nullptr);
            bgfx::setViewMode(id, bgfx::ViewMode::Default);
            bgfx::touch(id);
        };

        // The table. One entry per pass (or per contiguous group that
        // shares a predicate and a config closure), in enum -- i.e.
        // draw -- order. A null config is a pass that configures its
        // own views (the particle steps).
        using ConfigFn = std::function<void(int, uint16_t)>;
        struct PassDecl {
            bool live;       ///< does this frame draw the pass?
            ConfigFn config; ///< configure the pass's bgfx view id
        };
        std::vector<PassDecl> passTable;
        passTable.reserve(V::NUM_VIEWS);
        int16_t declOf[V::NUM_VIEWS];
        std::memset(declOf, 0xff, sizeof(declOf));
        auto declPasses = [&](int first, int last, bool live,
                              ConfigFn config) {
            for (int p = first; p <= last; ++p)
                declOf[p] = int16_t(passTable.size());
            passTable.push_back({live, std::move(config)});
        };
        auto declPass = [&](int p, bool live, ConfigFn config) {
            declPasses(p, p, live, std::move(config));
        };

        // Stateful particle simulation: two step ids per emitter
        // slot, claimed as a group whenever the scene carries an
        // emitter that could hold state -- stepParticles picks the
        // slots itself, after the map is decided.
        bool statefulParticles = false;
        for (const auto &d : scene) {
            const auto &sh = d.material.usershader;
            if (sh && sh->stage == "particle"
                    && !sh->simulateSource.empty() && d.objectKey) {
                statefulParticles = true;
                break;
            }
        }
        // Non-on-top selections reroute their opaque draws into
        // ViewSelection (submit(), selPass) -- claim it whenever such
        // a feed exists or those draws land in the discard view.
        bool nonOntopSel = false;
        for (const auto &sel : selections) {
            if (sel.first <= 0 && !sel.second.empty()) {
                nonOntopSel = true;
                break;
            }
        }
        const bool reflActive = groundReflActive || waterReflActive;

        declPasses(V::ViewParticleSim0,
                   int(V::ViewParticleSim0) + int(V::kParticleViews) - 1,
                   statefulParticles, nullptr);
        declPass(V::ViewParticleImpact,
                 statefulParticles && hasWaterBody && waterSurfActive,
                 nullptr);
        declPass(V::ViewBackground, true, configScene);
        declPass(V::ViewSunDisc, true, configScene);
        declPass(V::ViewShadow, shadowRender, configShadow);
        declPasses(V::ViewShadowBlurH, V::ViewShadowBlurV,
                   shadowBlurActive, configShadowBlur);
        declPass(V::ViewShadowTint, shadowRender, configShadowTint);
        declPasses(V::ViewShadowTintBlurH, V::ViewShadowTintBlurV,
                   shadowBlurActive, configShadowBlur);
        for (int t = 0; t < V::kBulbShadowTiles; ++t)
            declPass(V::ViewBulbShadow0 + t, bulbShadowRender[t],
                     configBulb);
        declPass(V::ViewAOPrepass, prepassRender, configAOPrepass);
        for (int m = 0; m < 6; ++m)
            declPass(V::ViewAODepthMip1 + m,
                     ssaoActive && aoRender && m < view->aoMipCount,
                     configAOMip);
        declPasses(V::ViewWaterFront, V::ViewWaterBack,
                   waterActive && mediumRender, configMedium);
        declPasses(V::ViewGlassFront, V::ViewGlassBack,
                   glassActive && mediumRender, configMedium);
        declPasses(V::ViewCloudFront, V::ViewCloudBack,
                   cloudActive && mediumRender, configMedium);
        declPasses(V::ViewFireFront, V::ViewFireBack,
                   fireActive && mediumRender, configMedium);
        declPasses(V::ViewAOGen, V::ViewAOBlur2,
                   ssaoActive && aoRender, configAOChain);
        declPass(V::ViewVolGen, volActive, configVolGen);
        declPass(V::ViewVolAccum, volActive, configVolAccum);
        declPasses(V::ViewGroundRefl, V::ViewReflMedia, reflActive,
                   configRefl);
        declPass(V::ViewOpaque, true, configScene);
        declPass(V::ViewSelection, nonOntopSel, configScene);
        declPass(V::ViewSectionCap, true, configScene);
        declPass(V::ViewDebugScene, debugSceneRender, configDebugScene);
        declPass(V::ViewGroundReflApply, groundReflActive, configScene);
        declPass(V::ViewOutline, true, configScene);
        declPass(V::ViewCaustics, waterActive && volconf.caustics,
                 configScene);
        declPass(V::ViewVolApply, volActive, configScene);
        declPass(V::ViewWaterCopy, waterSurfActive || glassActive,
                 configWaterCopy);
        declPass(V::ViewWaterSurface, waterSurfActive, configScene);
        declPass(V::ViewGlassSurface, glassActive, configScene);
        declPass(V::ViewParticles, true, configScene);
        declPass(V::ViewTransparent, true, configTransparent);
        declPass(V::ViewOITComposite, oitActive, configScene);
        declPass(V::ViewSectionCapTransp, true, configScene);
        declPasses(V::ViewBloomBright, V::ViewBloomBlurV, bloomActive,
                   configBloom);
        declPass(V::ViewBloomApply, bloomActive, configScene);
        declPass(V::ViewUserPostCopy, userPostActive, configUserPostCopy);
        declPass(V::ViewUserPost, userPostActive, configUserPost);
        declPass(V::ViewDebug, true, configScene);
        declPass(V::ViewOnTop, true, configScene);
        declPass(V::ViewHighlight, true, configScene);
        for (int s = 0; s < int(V::NumOverlayViews); ++s)
            declPass(V::ViewOverlay0 + s, s < int(overlays.size()),
                     configOverlay);
        declPass(V::ViewPresent, true, configPresent);

        {
            view->beginPasses();
            for (int p = 0; p < V::NUM_VIEWS; ++p) {
                if (declOf[p] < 0) {
                    // A pass missing from the table can never render:
                    // nothing marks it, so every submit to it lands in
                    // the discard view. Say so once -- this catches a
                    // new PassView added without a declaration.
                    static bool warnedUndeclared = false;
                    if (!warnedUndeclared) {
                        warnedUndeclared = true;
                        RENDER_ERR("bgfx pass table: pass " << p
                                   << " has no declaration and cannot "
                                      "render");
                    }
                    continue;
                }
                view->markPass(p, passTable[size_t(declOf[p])].live);
            }

            if (!_BGFXLib.reserveBlock(view, view->passesNeeded())) {
                // The pool is full. Same answer as an unfittable viewer:
                // sit the frame out and let Coin composite the scene,
                // rather than submitting ids that belong to somebody
                // else or that bgfx would abort on.
                if (!_BGFXLib.warnedViewBudget) {
                    _BGFXLib.warnedViewBudget = true;
                    RENDER_ERR("Out of bgfx view ids: this 3D view needs "
                               << view->passesNeeded() << " of "
                               << _BGFXLib.poolSize()
                               << " and the open views hold the rest. It "
                                  "falls back to Coin rendering; close "
                                  "another 3D view to get it back.");
                }
                return false;
            }
            view->mapPasses();
        }

        // Stateful particle emitters advance before anything draws:
        // their views come first in id order and configure/submit
        // themselves, so the loop below leaves them alone. A frame
        // that still owes simulation steps keeps the view animating
        // (docs/RenderEngine.md §5.8) — that is how a frozen frame
        // reaches its warm-up state.
        if (view->stepParticles(scene, animTime, debugconf.freezeFrame))
            animatedFrame = true;
        // ... and immediately hand what they hit to the water, which is
        // the only consumer that has to see it before anything draws.
        view->splatImpacts(animTime, debugconf.freezeFrame,
                           hasWaterBody && waterSurfActive ? waterFoot
                                                           : nullptr);
        // The mirrored render is cached against a static frame, and a
        // live emitter changes the picture every frame without ever
        // touching the dirty flags — its motion is in state textures,
        // not in the scene. Without this the reflection keeps whatever
        // it was rendered from, which for a fountain means a pool
        // reflecting everything except the jet standing in it. Same
        // escape hatch the analytic media already take.
        reflRender = !staticFrame || shadowRender || fireActive
            || cloudActive || view->particlesLive;

        // Configure the ids the pass map handed out, through the same
        // table that declared them. A pass the frame did not claim has
        // no id to configure: it is not drawn. A null config is a pass
        // that configures its own views (the particle steps, in
        // stepParticles/splatImpacts above).
        for (int i = 0; i < V::NUM_VIEWS; ++i) {
            if (!view->passLive(i) || declOf[i] < 0)
                continue;
            const auto &decl = passTable[size_t(declOf[i])];
            if (decl.config)
                decl.config(i, view->vid(i));
        }

        // The discard view every unclaimed pass maps to: a 1x1 scratch
        // target, configured but not touched, so a draw that lands here
        // is thrown away instead of reaching the scene or an id that
        // belongs to another pass. Nothing should submit to it -- the
        // count is reported after the frame.
        bgfx::setViewFrameBuffer(view->sinkView, view->sinkFbo);
        bgfx::setViewClear(view->sinkView, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(view->sinkView, 0, 0, 1, 1);
        bgfx::setViewTransform(view->sinkView, nullptr, nullptr);
        bgfx::setViewMode(view->sinkView, bgfx::ViewMode::Default);

        ++view->frame;
        view->drawcount = 0;
        view->autozoomScale = autozoomScale;
        if (pbrActive && pbrconf.envBackground)
            view->submitEnvBackground();
        else
            view->submitBackground(background);
        // Visible sun along the directional scene light (needs the
        // view-space light state the shadow section filled above).
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx sun: valid=%d spot=%d disc=%d size=%g frame=%d"
                    " dir=%g,%g,%g\n",
                    lightconf.valid, lightconf.spot, lightconf.sunDisc,
                    lightconf.sunDiscSize, view->shadowFrame,
                    view->lightDirView[0], view->lightDirView[1],
                    view->lightDirView[2]);
        if (lightconf.valid && !lightconf.spot && lightconf.sunDisc
                && view->shadowFrame)
            view->submitSunDisc(lightconf.sunDiscSize);
        const float *viewMat = reinterpret_cast<const float *>(viewMatrix);
        view->viewMatrix = viewMat;
        view->projMatrix = reinterpret_cast<const float *>(projMatrix);

        // Frustum culling: world-space clip planes extracted from the
        // camera view-projection (Gribb-Hartmann; the fed matrices follow
        // Coin's GL clip conventions). A scene draw whose world bounds
        // lie fully outside any plane skips its color/water/prepass
        // submits below. Shadow casters are exempt — off-screen geometry
        // still casts into the view — as are autozoom draws (their model
        // matrix rebuilds per frame, so the fed bounds are stale) and
        // draws without bounds. Hidden-line frames skip culling like
        // instancing does (the outline passes rework the fill submits).
        static const bool noCulling =
            getenv("FC_BGFX_NO_CULLING") != nullptr;
        std::vector<uint8_t> sceneCulled;
        if (!noCulling && !hlconfig.show && !scene.empty()) {
            float vp[16];
            bx::mtxMul(vp, viewMat,
                       reinterpret_cast<const float *>(projMatrix));
            // Row-vector convention (v' = v * M): clip component i is
            // dot(v, column i); plane k folds column 3 with the column
            // of its axis. GL clip volume, so the near plane is w + z.
            float planes[6][4];
            for (int k = 0; k < 6; ++k) {
                int axis = k >> 1;
                float sign = (k & 1) ? -1.0f : 1.0f;
                for (int r = 0; r < 4; ++r)
                    planes[k][r] = vp[r * 4 + 3] + sign * vp[r * 4 + axis];
            }
            sceneCulled.assign(scene.size(), 0);
            size_t nculled = 0;
            for (size_t i = 0; i < scene.size(); ++i) {
                const auto &d = scene[i];
                if (d.bboxMin[0] > d.bboxMax[0]
                        || !d.material.autozoom.empty())
                    continue;
                // An emitter draws outside its own bounds: test the
                // box its billboards can actually reach, not the one
                // its anchors sit in.
                const float pad = drawHeadroom(d);
                const float lo[3] = {d.bboxMin[0] - pad,
                                     d.bboxMin[1] - pad,
                                     d.bboxMin[2] - pad};
                const float hi[3] = {d.bboxMax[0] + pad,
                                     d.bboxMax[1] + pad,
                                     d.bboxMax[2] + pad};
                for (const auto &pl : planes) {
                    // Positive-vertex test: the bbox corner farthest
                    // along the plane normal decides containment.
                    float dist = pl[3]
                        + pl[0] * (pl[0] >= 0.0f ? hi[0] : lo[0])
                        + pl[1] * (pl[1] >= 0.0f ? hi[1] : lo[1])
                        + pl[2] * (pl[2] >= 0.0f ? hi[2] : lo[2]);
                    if (dist < 0.0f) {
                        sceneCulled[i] = 1;
                        ++nculled;
                        break;
                    }
                }
            }
            if (getenv("FC_BGFX_DEBUG_CULL"))
                fprintf(stderr, "bgfx cull: %zu of %zu scene draws\n",
                        nculled, scene.size());
        }
        // Scene draws only — the argument must reference into `scene`.
        auto culled = [&](const Render::DrawCall &d) {
            if (sceneCulled.empty())
                return false;
            size_t idx = size_t(&d - scene.data());
            return idx < sceneCulled.size() && sceneCulled[idx] != 0;
        };

        auto isTriangle = [](const Render::DrawCall &d) {
            return d.material.type == Render::Material::Triangle;
        };
        auto isTransp = [](const Render::DrawCall &d) {
            return d.material.transparent
                || (d.material.pervertexcolor
                    && d.mesh && d.mesh->hasTransparency);
        };

        // Decide whether on-top lines/points need the two-pass hidden-line
        // rendering (SoFCRenderer's hassel/hasontop/hlwholeontop check).
        bool sceneOnTopTri = false, sceneOnTopLine = false;
        for (const auto &draw : scene) {
            if (!draw.material.ontop)
                continue;
            (isTriangle(draw) ? sceneOnTopTri : sceneOnTopLine) = true;
        }
        bool selOnTopLine = false;
        for (const auto &sel : selections) {
            if (sel.first <= 0)
                continue;
            for (const auto &draw : sel.second) {
                if (!isTriangle(draw)) {
                    selOnTopLine = true;
                    break;
                }
            }
        }
        bool sceneTwoPass = sceneOnTopTri && sceneOnTopLine;
        bool twoPass = sceneTwoPass || selOnTopLine || hlWholeOnTop;


        // The same object selected through several ids draws its
        // whole-object geometry only once (GL's renderkeys dedup in
        // SoFCRendererP::updateSelection): the first draw of a given
        // (objectKey, cacheId, primitive type, model matrix) wins, on-top
        // selections considered first like the GL loop order. The matrix
        // hash keeps distinct placements of one shared geometry cache
        // (TShape instancing) apart — only true duplicates collapse.
        dupDraws.clear();
        {
            auto matrixHash = [](const Render::DrawCall &d) -> uint64_t {
                if (d.identity)
                    return 0;
                uint64_t h = 1469598103934665603ull;
                const uint8_t *p =
                    reinterpret_cast<const uint8_t *>(d.model);
                for (size_t i = 0; i < sizeof(d.model); ++i) {
                    h ^= p[i];
                    h *= 1099511628211ull;
                }
                return h ? h : 1;
            };
            std::set<std::tuple<uint64_t, uint64_t, uint8_t, uint64_t>> seen;
            auto dedup = [&](const Render::DrawCallList &draws) {
                for (const auto &draw : draws) {
                    if (!draw.wholeObject || !draw.objectKey || !draw.mesh)
                        continue;
                    if (!seen.emplace(draw.objectKey, draw.mesh->cacheId,
                                      draw.material.type,
                                      matrixHash(draw)).second)
                        dupDraws.insert(&draw);
                }
            };
            // Explicitly colored selections (SelIdSelected) win over the
            // implicit whole-on-top companions an element selection adds
            // — both can carry whole-object draws of the same geometry
            // (e.g. the instanced whole-highlight degradation), and the
            // tinted one is the visible signal.
            for (const auto &sel : selections) {
                if (sel.first > 0 && (sel.first & Render::SelIdSelected))
                    dedup(sel.second);
            }
            for (const auto &sel : selections) {
                if (sel.first > 0 && !(sel.first & Render::SelIdSelected))
                    dedup(sel.second);
            }
            for (const auto &sel : selections) {
                if (sel.first <= 0)
                    dedup(sel.second);
            }
        }
        auto isDup = [this](const Render::DrawCall &d) {
            return !dupDraws.empty() && dupDraws.count(&d);
        };
        // A partial face whose outline replaces its fill (GL's
        // NoPreSelFaceHighlightWithOutline / NoSelFaceHighlightWith-
        // Outline skip in renderHighlight ~2159).
        auto outlineOnly = [](const Render::DrawCall &d) {
            return d.material.faceoutline && d.material.outlineonly
                && d.partIndex >= 0;
        };

        // Hidden-line draw style rules for scene draws (GL: renderOpaque
        // ~2012 / renderTransparency's notriangle + renderLines/
        // renderPoints + renderOutline with highlight=false). The
        // materials carry the outline flag; the per-frame config decides
        // face/seam/vertex hiding and the outline shape.
        const Render::HiddenLineConfig &hl = hlconfig;
        uint32_t outlineRef = 0;
        auto hideFill = [&](const Render::DrawCall &d) {
            // GL skips opaque fills of outline materials and, with the
            // transparency override, every transparent scene fill — and
            // with them their outlines.
            return hl.show && hl.hideFace && isTriangle(d)
                && (d.material.outline || isTransp(d));
        };
        auto hidePoints = [&](const Render::DrawCall &d) {
            return hl.hideVertex && d.material.outline
                && d.material.type == Render::Material::Point
                && d.partIndex < 0;
        };
        auto sceneNoSeam = [&](const Render::DrawCall &d) {
            return hl.hideSeam && d.material.outline
                && d.material.type == Render::Material::Line
                && d.partIndex < 0;
        };
        // Which face-part set a whole-cache outline splits into
        // (GL: renderOutline ~1400). Clipped geometry and the
        // perFaceOutline mode (with a positive outline width, unless the
        // whole-scene silhouette runs) outline every face part; the
        // remaining perFaceOutline combinations outline only the
        // non-flat (curved) parts, whose silhouette edges are not in the
        // line set. Null = one whole-cache outline. A cache without a
        // face-part table outlines nothing in the per-part modes,
        // like GL's zero-iteration loop.
        auto outlineParts = [&](const Render::DrawCall &d, bool highlight)
                -> const std::vector<std::pair<int, int>> * {
            if (!d.mesh)
                return nullptr;
            if (d.material.numclipplanes > 0
                    || (!highlight && hl.perFaceOutline && !hl.sceneOutline
                        && hl.outlineWidth > 0.0f))
                return &d.mesh->triangleParts;
            if (hl.perFaceOutline && !d.mesh->nonFlatParts.empty())
                return &d.mesh->nonFlatParts;
            return nullptr;
        };
        // Per-part outlines overlap their own object's fills (only the
        // part's interior is stencil-killed), so they skip the depth
        // write to let those fills dim them like GL's ordered draw does.
        auto submitOutlineOrParts = [&](const Render::DrawCall &d,
                                        BGFXView::OutlineSpec &spec,
                                        bool highlight) {
            if (const auto *parts = outlineParts(d, highlight)) {
                spec.depthWrite = false;
                for (const auto &part : *parts) {
                    spec.start = part.first;
                    spec.count = part.second;
                    view->submitOutline(d, ++outlineRef, spec);
                }
            } else {
                view->submitOutline(d, ++outlineRef, spec);
            }
        };
        // Per-entry stencil outline of a whole-cache triangle draw of the
        // scene or a selection (GL: renderOutline with highlight=false,
        // reached from renderOpaque/renderTransparency right after the
        // fill). The whole-scene outline mode (sceneOutline without
        // perFaceOutline) suppresses these; its single-silhouette pass
        // runs at the end of the frame instead. viewOverride routes
        // on-top selection outlines into the highlight view where their
        // fills draw.
        auto submitSceneOutline = [&](const Render::DrawCall &d,
                                      int viewOverride = -1) {
            if (!hl.show || !d.material.outline || !isTriangle(d)
                    || d.partIndex >= 0
                    || (hl.sceneOutline && !hl.perFaceOutline))
                return;
            BGFXView::OutlineSpec spec;
            bool ontop = d.material.ontop
                || viewOverride == BGFXView::ViewHighlight;
            spec.view = viewOverride >= 0 ? uint16_t(viewOverride)
                : d.material.ontop ? BGFXView::ViewOnTop
                                   : BGFXView::ViewOutline;
            spec.depthTest = !ontop;
            spec.depthWrite = spec.depthTest;
            spec.caps = hl.hideVertex;
            spec.color = d.material.linecolor ? d.material.linecolor
                                              : d.material.diffuse;
            // GL renderOutline ~1458: raise the line width to the
            // configured outline width, then thicken.
            float lw = qMax(d.material.linewidth, hl.outlineWidth);
            spec.width = qMax(lw * 1.5f,
                              d.material.linewidth * hl.outlineThicken);
            submitOutlineOrParts(d, spec, false);
        };
        // Whole-object outline of the preselection highlight in
        // hidden-line mode (GL: renderOutline with highlight=true and
        // partidx < 0): drawn on top with the selection-thickened width
        // the bridge resolved into Material::outlinewidth, raised to the
        // configured outline width like GL's in-place formula.
        auto submitHighlightOutline = [&](const Render::DrawCall &d) {
            if (!hl.show || !d.material.outline || !isTriangle(d)
                    || d.partIndex >= 0)
                return;
            BGFXView::OutlineSpec spec;
            spec.view = BGFXView::ViewHighlight;
            spec.depthTest = false;
            spec.depthWrite = false;
            spec.caps = true;
            spec.color = d.material.linecolor ? d.material.linecolor
                                              : d.material.diffuse;
            spec.width = qMax(d.material.outlinewidth,
                              hl.outlineWidth * 1.5f);
            submitOutlineOrParts(d, spec, true);
        };

        // Cross-object instancing: each precomputed group of identical
        // draws (matching geometry content and material bar the diffuse)
        // collapses into one instanced submit of the opaque — or, on
        // WBOIT frames, transparent — pass, carrying {model matrix,
        // diffuse} per instance. The
        // depth-only side submissions batch too, over the same instance
        // layout: the SSAO/volumetric prepass with the visible members,
        // the shadow caster pass with visible, frustum-culled AND
        // selection-hidden members (a hidden scene draw still casts —
        // its geometry re-renders in the on-top pass). Hidden-line
        // frames stay
        // per-draw (the stencil outline pass reworks the fill submits
        // wholesale).
        static const bool noInstancing =
            getenv("FC_BGFX_NO_INSTANCING") != nullptr;
        std::vector<uint8_t> drawInstanced;
        std::vector<uint8_t> prepassInstanced;
        std::vector<uint8_t> casterInstanced;
        if (!noInstancing && !hl.show && !instGroups.empty()
                && view->instancingActive()) {
            drawInstanced.assign(scene.size(), 0);
            prepassInstanced.assign(scene.size(), 0);
            casterInstanced.assign(scene.size(), 0);
            std::vector<float> instData;
            std::vector<int> vis, visOut, hidden;
            auto appendInstance = [&](int i) {
                const auto &d = scene[i];
                if (d.identity) {
                    static const float ident[16] = {
                        1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f};
                    instData.insert(instData.end(), ident, ident + 16);
                } else {
                    instData.insert(instData.end(), d.model, d.model + 16);
                }
                float c[4];
                unpackColor(d.material.diffuse, c);
                instData.insert(instData.end(), c, c + 4);
            };
            for (const auto &group : instGroups) {
                if (group.members.size() < 2)
                    continue;
                vis.clear();
                visOut.clear();
                hidden.clear();
                for (int i : group.members) {
                    if (isHidden(scene[i]))
                        hidden.push_back(i);
                    else if (culled(scene[i]))
                        visOut.push_back(i);
                    else
                        vis.push_back(i);
                }
                instData.clear();
                instData.reserve(group.members.size() * 20);
                for (int i : vis)
                    appendInstance(i);
                if (vis.size() >= 2) {
                    const auto &proto = scene[vis[0]];
                    if (view->submitInstanced(proto, instData.data(),
                                              uint32_t(vis.size()))) {
                        for (int i : vis)
                            drawInstanced[i] = 1;
                    }
                    // Transparent geometry neither occludes nor receives
                    // AO — it stays out of the prepass like the per-draw
                    // path.
                    if (prepassRender && !isTransp(proto)
                            && view->submitPrepassInstanced(
                                proto, instData.data(),
                                uint32_t(vis.size()))) {
                        for (int i : vis)
                            prepassInstanced[i] = 1;
                    }
                }
                // Casters append the frustum-culled and hidden members
                // after the visible ones — off-screen geometry still
                // casts, and the instance order does not matter for a
                // depth pass. Group members share the material bar the
                // diffuse, so one shadowstyle check covers them all.
                if (shadowRender
                        && (scene[group.members[0]].material.shadowstyle
                            & 1)) {
                    for (int i : visOut)
                        appendInstance(i);
                    for (int i : hidden)
                        appendInstance(i);
                    uint32_t total = uint32_t(vis.size() + visOut.size()
                                              + hidden.size());
                    if (total >= 2
                            && view->submitShadowCasterInstanced(
                                scene[group.members[0]], instData.data(),
                                total)) {
                        for (int i : group.members)
                            casterInstanced[i] = 1;
                    }
                }
            }
        }
        auto instancedThisFrame = [&](int i) {
            return !drawInstanced.empty() && drawInstanced[i];
        };
        auto prepassInstancedThisFrame = [&](int i) {
            return !prepassInstanced.empty() && prepassInstanced[i];
        };
        auto casterInstancedThisFrame = [&](int i) {
            return !casterInstanced.empty() && casterInstanced[i];
        };

        // 1. Normal scene draws, then on-top triangle fills (opaque before
        // transparent), mirroring the GL delayed-pass order. On-top lines
        // are deferred below so they draw over the selection fills.
        // Hidden-line entries get their stencil outline right after the
        // fill and honor the face/seam/vertex hiding rules.
        view->ontop = false;
        for (int drawIdx = 0; drawIdx < int(scene.size()); ++drawIdx) {
            const auto &draw = scene[drawIdx];
            if (draw.material.ontop || isHidden(draw)) {
                // A selection-hidden scene draw still casts its shadow —
                // the same geometry re-renders in the on-top pass, and
                // GL's SoShadowGroup shadow map render is oblivious to
                // the selection re-render. This also keeps the cached
                // shadow map valid across select/deselect.
                if (shadowRender && !draw.material.ontop
                        && isTriangle(draw)
                        && (draw.material.shadowstyle & 1)
                        && !mediumExempt(draw.material)
                        && !casterInstancedThisFrame(drawIdx))
                    view->submitShadowCaster(draw);
                // Bulb shadow tiles re-rendering this frame take every
                // caster individually (no instanced caster path there).
                if (anyBulbShadow && !draw.material.ontop
                        && isTriangle(draw)
                        && (draw.material.shadowstyle & 1)
                        && !mediumExempt(draw.material)) {
                    for (int t = 0; t < BGFXView::kBulbShadowTiles; ++t)
                        if (bulbShadowRender[t])
                            view->submitBulbShadowCaster(draw, t);
                }
                if (shadowRender && !draw.material.ontop
                        && isTriangle(draw)
                        && (draw.material.shadowstyle & 1)
                        && glassActive && draw.material.glass)
                    view->submitShadowTint(draw);
                // Likewise the depth+normal prepass and the debug scene
                // re-render: the replacing draw (e.g. a shader override,
                // docs/RenderDebug.md §6.5) only substitutes the beauty
                // fill — depth, AO, volumetrics and the debug modes must
                // keep seeing the geometry.
                if (!draw.material.ontop && isTriangle(draw)
                        && !isTransp(draw)
                        && !mediumExempt(draw.material)) {
                    if (prepassRender && !culled(draw)
                            && !prepassInstancedThisFrame(drawIdx))
                        view->submitPrepass(draw);
                    if (debugSceneRender && !culled(draw))
                        view->submitDebugScene(draw, debugconf.viewMode);
                }
                continue;
            }
            if (hideFill(draw) || hidePoints(draw))
                continue;
            // A frustum-culled draw skips its color/water/prepass
            // submits but still casts its shadow below.
            bool cullDraw = culled(draw);
            // Water surface pass: the water body's triangles leave the
            // ordinary (transparent-bucket) path and re-render in the
            // dedicated surface view; clipped bodies keep the normal
            // path (no clip variant of the surface shader). The body's
            // edge/vertex draws are dropped entirely — a water surface
            // has no CAD feature lines, and the black edges would also
            // smear through the screen-space refraction.
            bool surfWater = waterSurfActive && isTriangle(draw)
                && draw.material.water
                && draw.material.numclipplanes == 0;
            bool surfWaterLine = waterSurfActive && !isTriangle(draw)
                && draw.objectKey
                && waterSurfObjects.count(draw.objectKey);
            // Glass pass: the body's triangles re-render as glass
            // (their front/back depths bound the absorption interval);
            // clipped bodies keep the normal path like water. The
            // edge/vertex draws keep rendering — a glass part keeps
            // its CAD feature lines.
            bool surfGlass = glassActive && isTriangle(draw)
                && draw.material.glass
                && draw.material.numclipplanes == 0;
            // Cloud body: neither the fills nor the feature lines
            // render — the volume raymarches as a medium instead. The
            // triangles rasterize the interval depth targets only.
            bool cloudFill = cloudActive && isTriangle(draw)
                && (draw.material.cloud || draw.material.fountain);
            bool cloudPart = cloudActive
                && (cloudFill
                    || (!isTriangle(draw) && draw.objectKey
                        && cloudObjects.count(draw.objectKey)));
            // Fire body: like the cloud, only the interval depth
            // targets see the triangles.
            bool fireFill = fireActive && isTriangle(draw)
                && draw.material.fire;
            bool firePart = fireActive
                && (fireFill
                    || (!isTriangle(draw) && draw.objectKey
                        && fireObjects.count(draw.objectKey)));
            if (!cullDraw && !instancedThisFrame(drawIdx) && !surfWater
                    && !surfWaterLine && !surfGlass && !cloudPart
                    && !firePart)
                view->submit(draw, viewMat, BGFXView::PassNormal,
                             sceneNoSeam(draw));
            if (cloudFill && !cullDraw && mediumRender) {
                int slot = slotOf(cloudSlots, draw.objectKey);
                view->submitWaterDepth(draw, false, 2, slot);
                view->submitWaterDepth(draw, true, 2, slot);
            }
            if (fireFill && !cullDraw && mediumRender) {
                int slot = slotOf(fireSlots, draw.objectKey);
                view->submitWaterDepth(draw, false, 3, slot);
                view->submitWaterDepth(draw, true, 3, slot);
            }
            if (surfWater && !cullDraw)
                view->submitWaterSurface(draw, waterWaveStrength,
                                         waterWaveScale, waterSurfTime,
                                         waterSurfReject, waterReflMode,
                                         waterconf.refraction, waterActive,
                                         waterconf.absorption,
                                         waterconf.inscatter,
                                         waterconf.shadow,
                                         waterconf.shadowWobble,
                                         waterconf.rippleType,
                                         waterconf.rippleDensity,
                                         waterconf.impactStrength,
                                         waterconf.impactLife,
                                         volActive && cloudActive
                                             ? fountainSplash : nullptr,
                                         volActive && waterActive);
            if (surfGlass && !cullDraw) {
                // The interval depths cache with the medium targets; the
                // surface pass reads the per-frame scene copy, so it
                // always re-renders.
                if (mediumRender) {
                    view->submitWaterDepth(draw, false, 1);
                    view->submitWaterDepth(draw, true, 1);
                }
                view->submitGlassSurface(draw, glassReject);
            }
            // Water body draws bound the medium instead of acting as
            // ordinary surfaces: their front/back depths rasterize into
            // the water targets (whatever their transparency), and they
            // are excluded from the volumetric ray ends and from shadow
            // casting so light and shafts enter the water.
            bool isWater = waterActive && isTriangle(draw)
                && draw.material.water;
            if (isWater && !cullDraw && mediumRender) {
                int slot = slotOf(waterSlots, draw.objectKey);
                view->submitWaterDepth(draw, false, 0, slot);
                view->submitWaterDepth(draw, true, 0, slot);
            }
            // The SSAO prepass re-rasterizes the opaque fills into the
            // depth+normal target (transparent geometry neither occludes
            // nor receives AO; the multiply pass runs before the
            // transparent bucket). The volumetric raymarch shares it as
            // its ray-end depth source.
            if (prepassRender && isTriangle(draw) && !isTransp(draw)
                    && !mediumExempt(draw.material) && !cullDraw
                    && !prepassInstancedThisFrame(drawIdx))
                view->submitPrepass(draw);
            // Debug scene re-render (modes 6/8): every triangle fill
            // that rasterizes in the main color passes — opaque,
            // transparent and the water/glass surface re-renders alike;
            // cloud/fire bodies raymarch instead of rasterizing, so
            // they stay out. Instanced members count per-draw (same
            // fragments either way).
            if (debugSceneRender && isTriangle(draw) && !cullDraw
                    && !cloudFill && !fireFill)
                view->submitDebugScene(draw, debugconf.viewMode);
            // Shadow casters — transparent geometry casts like an opaque
            // one, matching Coin's SoShadowGroup (its depth-map pass
            // ignores alpha); water is the one exception (light must
            // enter the medium). Skipped while the cached map is valid.
            if (shadowRender && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material)
                    && !casterInstancedThisFrame(drawIdx))
                view->submitShadowCaster(draw);
            // Bulb shadow tiles re-rendering this frame take every
            // caster individually (no instanced caster path there).
            if (anyBulbShadow && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material)) {
                for (int t = 0; t < BGFXView::kBulbShadowTiles; ++t)
                    if (bulbShadowRender[t])
                        view->submitBulbShadowCaster(draw, t);
            }
            // Glass casts through the tint map instead of the moments:
            // a softer shadow, tinted when the glass is colored.
            if (shadowRender && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && glassActive && draw.material.glass)
                view->submitShadowTint(draw);
            if (!cullDraw)
                submitSceneOutline(draw);
        }
        if (view->passLive(V::ViewShadowBlurH))
            view->submitShadowBlur(lightconf.smoothBorder);
        if (shadowActive && lightconf.ground && bboxValid) {
            view->submitShadowGround(bboxMin, bboxMax, lightconf,
                                     volActive && aoRender);
        }
        // Ground reflection: the opaque scene triangles re-submit into
        // the mirrored view through the ordinary submit path (redirected
        // by reflPass, culling flipped, shadow matrix rebased), then the
        // overlay quad blends the result onto the ground. Frustum
        // culling is skipped — the mirrored camera sees a different
        // volume; hidden and on-top draws stay out like the water
        // bodies and transparent geometry (single-bounce opaque only).
        // Marshalled per-slot medium parameters: consumed by the
        // reflection media pass right below and by the volumetric
        // raymarch submit later in the frame.
        float cloudParams[kSlots][4] = {};
        float fireParams[kSlots][4] = {};
        float fireParams2[kSlots][4] = {};
        float fireFrames[kSlots][16] = {};
        if (volActive) {
            for (int s = 0; s < kSlots; ++s) {
                if (cloudActive && cloudSlot[s][3] > 0.0f) {
                    cloudParams[s][0] = cloudSlot[s][0];
                    cloudParams[s][1] = cloudSlot[s][1];
                    cloudParams[s][2] = animTime * cloudSlot[s][2];
                    // w keeps the flavor: 1 = cloud FBM, 2 = fountain.
                    cloudParams[s][3] = cloudSlot[s][3];
                    animatedFrame = animatedFrame
                        || (animLive && cloudSlot[s][2] != 0.0f);
                }
                const FireSlot &fsl = fireSlot[s];
                if (fireActive && fsl.valid) {
                    // The 2.0 rise rate makes the flame climb a couple
                    // of noise cells per second at the default speed.
                    fireParams[s][0] = fsl.emission;
                    fireParams[s][1] = fsl.detail;
                    fireParams[s][2] = animTime * fsl.speed * 2.0f;
                    fireParams[s][3] = 1.0f;
                    fireParams2[s][0] = fsl.invRadius;
                    fireParams2[s][1] = fsl.invHeight;
                    fireParams2[s][2] = fsl.soot;
                    animatedFrame = animatedFrame
                        || (animLive && fsl.speed != 0.0f);
                }
                std::memcpy(fireFrames[s], fsl.frame,
                            sizeof(fsl.frame));
            }
        }
        if (view->passLive(V::ViewGroundRefl) && reflRender) {
            view->reflPass = true;
            // Keep only what is above the mirror plane; everything
            // under it would otherwise fold up over the reflection.
            view->reflClip = true;
            view->reflClipPlane[0] = 0.0f;
            view->reflClipPlane[1] = 0.0f;
            view->reflClipPlane[2] = 1.0f;
            view->reflClipPlane[3] = -(groundReflActive ? bboxMin[2]
                                                        : waterPlaneZ);
            float savedShadowMtx[16];
            std::memcpy(savedShadowMtx, view->shadowMtx,
                        sizeof(savedShadowMtx));
            std::memcpy(view->shadowMtx,
                        groundReflActive ? reflShadowMtx : waterReflShadowMtx,
                        sizeof(savedShadowMtx));
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if (!isTriangle(draw) || mat.ontop || isHidden(draw)
                        || hideFill(draw) || isTransp(draw)
                        || mediumExempt(mat))
                    continue;
                view->submit(draw, viewMat);
            }
            // Non-on-top whole-object selection fills replace hidden
            // scene draws (shader overrides, §6.5) — mirror them into
            // the reflection so the object doesn't vanish from it.
            for (const auto &sel : selections) {
                if (sel.first > 0)
                    continue;
                for (const auto &draw : sel.second) {
                    if (!isTriangle(draw) || draw.partIndex >= 0
                            || !draw.wholeObject || isTransp(draw)
                            || hideFill(draw)
                            || mediumExempt(draw.material)
                            || isDup(draw))
                        continue;
                    view->submit(draw, viewMat);
                }
            }
            std::memcpy(view->shadowMtx, savedShadowMtx,
                        sizeof(savedShadowMtx));
            view->reflPass = false;
            view->reflClip = false;
            // The reflection shows the fountain plume / flame too: an
            // analytic media march composited over the mirrored scene.
            if (volActive && (cloudActive || fireActive))
                view->submitReflMedia(cloudParams, fireParams,
                                      fireParams2, fireFrames,
                                      fountainGeom, fountainFrame);
        }
        // Ground blends its (possibly cached) reflection with a quad
        // every frame; the water surface pass samples reflTex itself
        // (s_texRefl) below.
        if (view->passLive(V::ViewGroundReflApply))
            view->submitGroundReflOverlay(bboxMin, bboxMax, lightconf);
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && !isTransp(draw)
                    && !isHidden(draw) && !hideFill(draw)) {
                bool cullDraw = culled(draw);
                if (!cullDraw)
                    view->submit(draw, viewMat);
                // On-top geometry keeps casting its shadow (the view
                // order still lands these in the caster pass).
                if (shadowRender && (draw.material.shadowstyle & 1)
                        && !mediumExempt(draw.material))
                    view->submitShadowCaster(draw);
                if (!cullDraw)
                    submitSceneOutline(draw);
            }
        }
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && isTransp(draw)
                    && !isHidden(draw) && !hideFill(draw)) {
                bool cullDraw = culled(draw);
                if (!cullDraw)
                    view->submit(draw, viewMat);
                if (shadowRender && (draw.material.shadowstyle & 1)
                        && !mediumExempt(draw.material))
                    view->submitShadowCaster(draw);
                if (!cullDraw)
                    submitSceneOutline(draw);
            }
        }

        // 1b. Stencil section caps of clipped solids, in their own
        // sequential views (opaque caps between the opaque and outline
        // passes, transparent caps after the OIT composite — GL's
        // grouped section pass order).
        view->updateHatchTexture(hatchVersion,
                                 hatchTex ? hatchTex->pixels.data()
                                          : nullptr,
                                 hatchTex ? hatchTex->width : 0,
                                 hatchTex ? hatchTex->height : 0);
        submitSectionCaps(view, reinterpret_cast<const float *>(projMatrix));

        // 1c. SSAO resolve: generate and blur the AO (the gen/blur
        // views run before ViewOpaque, whose mesh draws sample the
        // result into their ambient terms — see aoMeshTex). A cached
        // frame (aoRender false) skips the chain outright: the targets
        // still hold this camera/scene's result.
        if (view->passLive(V::ViewAOGen))
            view->submitAOResolve(aoRadius, aoconf.intensity,
                                  aoconf.method, aoconf.fast,
                                  aoconf.slices, aoconf.steps);

        // 1d. Volumetric light shafts: half-res raymarch of the shadow
        // map, bilateral-upsampled and composited onto the opaque scene
        // after the outlines, before the transparent bucket.
        if (view->passLive(V::ViewVolGen)) {
            // Temporal accumulation factor: while the camera holds
            // still, successive jittered marches blend into the
            // history (k = 1/frames, floored so animated media keep
            // ~1/16 of fresh signal per frame); any camera or scene
            // change replaces the history outright — no reprojection,
            // no ghosting, interaction just returns to single-frame
            // noise.
            if (!staticFrame)
                view->volAccumFrames = 0;
            else if (view->volAccumFrames < 1024)
                ++view->volAccumFrames;
            // The freeze-frame determinism switch (docs/RenderDebug.md)
            // replaces the history outright every frame: k = 1 also
            // zeroes the golden-ratio jitter phase (accum < 1 gates it),
            // so a repeat frame raymarches identically.
            float volAccum = debugconf.freezeFrame ? 1.0f
                : view->volAccumFrames == 0
                ? 1.0f
                : std::max(1.0f / float(view->volAccumFrames + 1),
                           1.0f / 16.0f);
            view->submitVolumetric(volDensity, volconf.intensity,
                                   volMaxDist, volMedium,
                                   waterActive, waterSurfActive,
                                   volAccum,
                                   waterSigma, cloudParams,
                                   fireParams, fireParams2, fireFrames,
                                   fountainGeom, fountainFrame);
        }

        // 1e. Water caustics: additive light-space pattern splat over
        // the prepass surfaces inside the water interval, before the
        // extinction multiply of the volumetric apply.
        if (view->passLive(V::ViewCaustics)) {
            float causticParams[kSlots][4] = {};
            bool anyCaustics = false;
            for (int s = 0; s < waterSlotCount; ++s) {
                float scale = volconf.causticsScale;
                if (scale <= 0.0f && waterDiagSlot[s] > 0.0f)
                    scale = 6.0f / waterDiagSlot[s];
                if (scale <= 0.0f)
                    continue;
                causticParams[s][0] = volconf.causticsIntensity;
                causticParams[s][1] = scale;
                causticParams[s][2] = animTime * volconf.causticsSpeed;
                anyCaustics = true;
            }
            if (anyCaustics) {
                view->submitCaustics(causticParams, waterSigma);
                animatedFrame = animatedFrame
                    || (animLive && volconf.causticsSpeed != 0.0f);
            }
        }

        // 1f. Water surface / glass: copy the scene color (post
        // volumetric composite) into the refraction source; the
        // per-draw surface submits happened in the scene loop above
        // (their views render after the copy).
        if (view->passLive(V::ViewWaterCopy))
            view->submitWaterCopy();
        if (waterSurfActive) {
            animatedFrame = animatedFrame
                || (animLive && waterconf.waveSpeed != 0.0f);
        }

        // 1g. Bloom: bright-pass + light-source emit + blur + additive
        // composite, over the finished scene (its views sit after the
        // transparent/water buckets, before the on-top/UI passes).
        if (view->passLive(V::ViewBloomBright))
            view->submitBloom(bloomconf.threshold, bloomconf.intensity,
                              bloomconf.radius, bulbDraws,
                              // Rendered this frame, or the AO cache
                              // matched — either way the prepass
                              // targets describe the current camera.
                              prepassActive);

        // 1g'. User post-stage shader (docs/RenderDebug.md §6): copy the
        // composited color, then the user program draws fullscreen over
        // the scene reading the copy — before the debug visualization
        // and the on-top/highlight/overlay passes.
        if (view->passLive(V::ViewUserPost))
            view->submitUserPost(*userPost, userPostProg);

        // 1h. Render debugging buffer visualization (docs/RenderDebug.md):
        // overwrite the scene color with the selected intermediate target.
        // Depth (mode 1) normalizes by the farthest scene-bbox corner in
        // view space so the whole model spans the visible ramp.
        if (debugconf.viewMode > 0) {
            float maxDepth = 0.0f;
            if (bboxValid) {
                for (int c = 0; c < 8; ++c) {
                    float x = (c & 1) ? bboxMax[0] : bboxMin[0];
                    float y = (c & 2) ? bboxMax[1] : bboxMin[1];
                    float z = (c & 4) ? bboxMax[2] : bboxMin[2];
                    float viewZ = viewMat[2] * x + viewMat[6] * y
                        + viewMat[10] * z + viewMat[14];
                    maxDepth = std::max(maxDepth, -viewZ);
                }
            }
            view->submitDebug(debugconf, maxDepth, aoconf.method,
                              shadowActive && bgfx::isValid(view->shadowTex),
                              waterconf.impactLife);
        }

        // 2. Selection whole-object fills; positive ids are on-top
        // selections (SoFCRenderer::addSelection). Their lines/points are
        // deferred to the two-pass loop when it runs; single-part (e.g.
        // selected face) triangle draws come last of all.
        for (const auto &sel : selections) {
            view->ontop = sel.first > 0;
            view->selPass = sel.first <= 0;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0)
                    continue;
                if (twoPass && sel.first > 0 && !isTriangle(draw))
                    continue;
                if (isDup(draw))
                    continue;
                view->submit(draw, viewMat);
                // Hidden-line outline of a whole-object selection fill
                // (GL: renderOutline from renderOpaque/renderTransparency
                // over slentries). On-top selections outline in the
                // highlight view where their fills draw.
                if (isTriangle(draw))
                    submitSceneOutline(draw, sel.first > 0
                            ? int(BGFXView::ViewHighlight) : -1);
            }
        }
        view->selPass = false;

        // 3. Whole-object preselection fills before the depth prepass.
        view->ontop = true;
        if (hlWholeOnTop) {
            for (const auto &draw : highlight) {
                if (isTriangle(draw) && !outlineOnly(draw)) {
                    view->submit(draw, viewMat);
                    submitHighlightOutline(draw);
                }
            }
        }

        if (twoPass) {
            // 4. Depth-write-only prepass of on-top fills: on-top draws
            // render without depth test and thus never write depth, so the
            // solid line pass below needs this to tell hidden from visible.
            if (sceneTwoPass) {
                for (const auto &draw : scene) {
                    if (draw.material.ontop && isTriangle(draw)
                            && !isHidden(draw) && !hideFill(draw)
                            && !culled(draw))
                        view->submit(draw, viewMat, BGFXView::PassDepthOnly);
                }
            }
            if (selOnTopLine) {
                for (const auto &sel : selections) {
                    if (sel.first <= 0)
                        continue;
                    for (const auto &draw : sel.second) {
                        if (isTriangle(draw) && draw.partIndex < 0
                                && !isDup(draw))
                            view->submit(draw, viewMat,
                                         BGFXView::PassDepthOnly);
                    }
                }
            }
            if (hlWholeOnTop) {
                for (const auto &draw : highlight) {
                    if (isTriangle(draw))
                        view->submit(draw, viewMat, BGFXView::PassDepthOnly);
                }
            }

            // 5. On-top lines/points, dimmed where depth-occluded then
            // solid where visible (GL's RenderPassLinePattern/LineSolid).
            for (int pass : {int(BGFXView::PassLineHidden),
                             int(BGFXView::PassLineSolid)}) {
                for (const auto &draw : scene) {
                    if (draw.material.ontop && !isTriangle(draw)
                            && !isHidden(draw) && !hidePoints(draw)
                            && !culled(draw))
                        view->submit(draw, viewMat, pass,
                                     sceneNoSeam(draw));
                }
                // GL bucket order within each pass: the uncolored
                // whole-on-top companions (selsontop) draw before the
                // colored highlight lines (selslineontop) — the
                // highlight must paint last or a companion coincident
                // with it (a selected sketch edge over its own object
                // lines) covers it back with white.
                for (int hlphase = 0; hlphase < 2; ++hlphase) {
                    for (const auto &sel : selections) {
                        if (sel.first <= 0)
                            continue;
                        for (const auto &draw : sel.second) {
                            if (!isTriangle(draw) && !isDup(draw)
                                    && draw.material.highlightline
                                        == (hlphase == 1))
                                view->submit(draw, viewMat, pass);
                        }
                    }
                }
                if (hlWholeOnTop) {
                    for (const auto &draw : highlight) {
                        if (!isTriangle(draw) && draw.partIndex < 0)
                            view->submit(draw, viewMat, pass);
                    }
                }
            }
        } else {
            // No fills on top: scene on-top lines draw in a single pass.
            for (const auto &draw : scene) {
                if (draw.material.ontop && !isTriangle(draw)
                        && !isHidden(draw) && !hidePoints(draw)
                        && !culled(draw))
                    view->submit(draw, viewMat, BGFXView::PassNormal,
                                 sceneNoSeam(draw));
            }
        }

        // 6. Single-part selection fills (e.g. the selected face), matching
        // GL's transpselectionsfaceontop position after the line passes.
        for (const auto &sel : selections) {
            view->ontop = sel.first > 0;
            view->selPass = sel.first <= 0;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0
                        && !outlineOnly(draw))
                    view->submit(draw, viewMat);
            }
        }
        view->selPass = false;

        // 7. Preselection highlight: whole-on-top fills/lines were handled
        // above, only its single-part lines/points remain; otherwise draw
        // everything here, fills first.
        view->ontop = true;
        if (hlWholeOnTop) {
            for (const auto &draw : highlight) {
                if (!isTriangle(draw) && draw.partIndex >= 0)
                    view->submit(draw, viewMat);
            }
        } else {
            for (const auto &draw : highlight) {
                if (isTriangle(draw) && !outlineOnly(draw)) {
                    view->submit(draw, viewMat);
                    submitHighlightOutline(draw);
                }
            }
            for (const auto &draw : highlight) {
                if (!isTriangle(draw))
                    view->submit(draw, viewMat);
            }
        }

        // 8. Whole-scene hidden-line silhouette (GL: renderSceneOutline,
        // issued after all line/highlight passes and before the face
        // outlines): every scene triangle draw stencil-marks under one
        // shared reference — depth-independent, so hidden geometry still
        // counts — then each one's edges redraw where the stencil
        // differs, leaving a single outline around the union of the
        // scene. Deviation from GL: the edge passes apply each entry's
        // own clip planes, where GL leaves whatever clip state its mark
        // loop applied last.
        if (hl.show && hl.sceneOutline) {
            uint32_t silhouetteRef = ++outlineRef;
            bool marked = false;
            for (const auto &draw : scene) {
                if (!isTriangle(draw) || isHidden(draw))
                    continue;
                marked |= view->submitOutlineMark(draw, silhouetteRef,
                        BGFXView::ViewHighlight, true);
            }
            if (marked) {
                BGFXView::OutlineSpec spec;
                spec.view = BGFXView::ViewHighlight;
                spec.color = hl.lineColor;
                float lw = qMax(1.0f, hl.outlineWidth);
                spec.width = lw * 1.5f;
                spec.capWidth = lw;
                spec.depthTest = true;
                spec.depthWrite = false;
                spec.caps = hl.hideVertex;
                for (const auto &draw : scene) {
                    if (!isTriangle(draw) || isHidden(draw))
                        continue;
                    view->submitOutlineEdges(draw, silhouetteRef, spec);
                }
            }
        }

        // 9. Selected/preselected face outlines, last of all (GL draws
        // them at the very end of the frame under
        // RenderPassSelectionOutline; selections before preselection).
        auto faceOutlineSpec = [](const Render::DrawCall &draw) {
            BGFXView::OutlineSpec spec;
            spec.view = BGFXView::ViewHighlight;
            spec.color = draw.material.emissive;
            spec.width = draw.material.outlinewidth;
            spec.depthTest = false;
            spec.caps = true;
            spec.start = draw.indexStart;
            spec.count = draw.indexCount;
            return spec;
        };
        for (const auto &sel : selections) {
            if (sel.first <= 0)
                continue;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0
                        && draw.material.faceoutline)
                    view->submitOutline(draw, ++outlineRef,
                                        faceOutlineSpec(draw));
            }
        }
        for (const auto &draw : highlight) {
            if (isTriangle(draw) && draw.partIndex >= 0
                    && draw.material.faceoutline)
                view->submitOutline(draw, ++outlineRef,
                                    faceOutlineSpec(draw));
        }
        view->ontop = false;

        // 10. Overlay feeds (foreground superimposition, corner axis
        // cross): each slot draws late into its own view — anchor-derived
        // camera and viewport, fresh depth — through the normal submit
        // path with the target view overridden.
        {
            int slot = 0;
            for (const auto &ov : overlays) {
                if (slot >= BGFXView::NumOverlayViews) {
                    static bool warned = false;
                    if (!warned) {
                        warned = true;
                        fprintf(stderr,
                                "bgfx: overlay feed %d dropped (only %d"
                                " overlay views)\n", ov.first,
                                int(BGFXView::NumOverlayViews));
                    }
                    break;
                }
                view->overlayView = int(BGFXView::ViewOverlay0) + slot;
                // Billboard text in an overlay with its OWN mini camera (corner
                // axis cross, NaviCube axis labels) must size against that
                // camera. sceneCamera overlays (editing / dimension feeds) draw
                // with the main view+proj, so their billboard text keeps the
                // main-scene sizing (overlayAnchor stays null); pixelSpace
                // overlays carry no billboard text.
                const Render::OverlayAnchor &anchor = ov.second.anchor;
                if (!anchor.sceneCamera && !anchor.pixelSpace) {
                    view->overlayAnchor = &anchor;
                    // Rect pixel height the overlay renders into (mirrors the
                    // view-config loop), so fixed-pixel glyph sizing lands.
                    view->overlayRectHeight =
                        anchor.corner == Render::OverlayAnchor::FullViewport
                        ? float(height)
                        : std::max(1.0f, anchor.sizeFraction
                                         * float(std::min(width, height)));
                }
                else {
                    view->overlayAnchor = nullptr;
                    view->overlayRectHeight = 0.f;
                }
                for (const auto &draw : ov.second.draws)
                    view->submit(draw, viewMat);
                ++slot;
            }
            view->overlayView = -1;
            view->overlayAnchor = nullptr;
            view->overlayRectHeight = 0.f;
        }

        if (view->passLive(V::ViewOITComposite))
            view->submitComposite();

        view->collectMeshes();

        // Anything that reached the discard view drew nothing: the pass
        // declaration above missed a case the submission side takes.
        // Name the passes -- once per view, since a mis-declared pass
        // repeats every frame -- because the symptom on its own (a
        // missing shadow, an overlay that stopped appearing) says
        // nothing about view ids.
        if (view->sinkHits && !view->sinkReported) {
            view->sinkReported = true;
            std::string passes;
            for (int p = 0; p < BGFXView::NUM_VIEWS; ++p) {
                if (view->sinkPasses[p / 64] & (uint64_t(1) << (p % 64)))
                    passes += " " + std::to_string(p);
            }
            RENDER_ERR("bgfx pass map: " << view->sinkHits
                       << " draw(s) went to the discard view from pass(es)"
                       << passes.c_str()
                       << " -- those passes were not declared for this "
                          "frame and did not render");
        }

#ifdef FC_RENDERER_STANDALONE
        view->present();
        bgfx::frame();
#else
        widget->doneCurrent();
        _BGFXLib.makeCurrent();
        bgfx::frame();
        widget->makeCurrent();
        view->blit(dumpPending ? &pendingDump : nullptr, &lastStats);
        dumpPending = false;
#endif

        if (!hasScene && !scene.empty())
            qDebug() << "bgfx: scene consumed:" << view->drawcount
                     << "draws," << view->meshes.size() << "meshes";

        if (getenv("FC_BGFX_DEBUG_READBACK"))
            fprintf(stderr, "bgfx frame %llu: scene=%zu sel=%zu hl=%zu"
                    " dups=%zu draws=%d\n",
                    (unsigned long long)view->frame, scene.size(),
                    selections.size(), highlight.size(),
                    dupDraws.size(), view->drawcount);

        // A submitted live time-referencing user draw keeps the
        // animation loop alive like the stock timed effects.
        animatedFrame = animatedFrame || _BGFXLib.userAnimatedDraw;

        renderOk = true;
        hasScene = !scene.empty();
        return true;
    }

    /// GL's SectionFillInvert color transform (_renderSection ~1838):
    /// invert each channel, mapping the mid-gray band to 180 and
    /// near-black results to 50; the alpha stays.
    static uint32_t invertCapColor(uint32_t col)
    {
        auto inv = [](uint32_t c) -> uint32_t {
            return (c > 120 && c < 140) ? 180 : 255 - c;
        };
        uint32_t r = inv((col >> 24) & 0xff);
        uint32_t g = inv((col >> 16) & 0xff);
        uint32_t b = inv((col >> 8) & 0xff);
        if (r + g + b < 10)
            r = g = b = 50;
        return (r << 24) | (g << 16) | (b << 8) | (col & 0xff);
    }

    /// Build the world-space cap quad of one section plane over the
    /// given bounds, replicating _renderSection's geometry: a square of
    /// the bounding sphere's radius centered on the bbox center
    /// projected onto the plane, oriented by Coin's z-to-normal
    /// rotation, with the hatch texture coordinates scaled from the
    /// world-to-pixel ratio at mid view depth.
    void buildCapQuad(const float plane[4], const float bmin[3],
                      const float bmax[3], const float *projMat,
                      int vpWidth, CapVertex verts[4]) const
    {
        float n[3] = {plane[0], plane[1], plane[2]};
        float nlen = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (nlen < 1e-12f)
            nlen = 1.0f;
        n[0] /= nlen; n[1] /= nlen; n[2] /= nlen;

        float center[3], ext[3];
        for (int i = 0; i < 3; ++i) {
            center[i] = 0.5f * (bmin[i] + bmax[i]);
            ext[i] = bmax[i] - bmin[i];
        }
        float radius = 0.5f * std::sqrt(
            ext[0]*ext[0] + ext[1]*ext[1] + ext[2]*ext[2]);

        // Project the center onto the plane (GL: center += -normal *
        // plane.getDistance(center)).
        float dist = center[0]*n[0] + center[1]*n[1] + center[2]*n[2]
            + plane[3] / nlen;
        for (int i = 0; i < 3; ++i)
            center[i] -= n[i] * dist;

        // Coin SbRotation(z-axis -> normal): quaternion from the cross
        // product, with the antiparallel fallback about the y axis.
        float q[4];  // x, y, z, w
        float dot = n[2];
        float cx = -n[1], cy = n[0], cz = 0.0f;  // cross(z, n)
        float crosslen = std::sqrt(cx*cx + cy*cy);
        if (crosslen < 1e-12f) {
            if (dot > 0.0f) {
                q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f;
            } else {
                q[0] = 0.0f; q[1] = 1.0f; q[2] = 0.0f; q[3] = 0.0f;
            }
        } else {
            float s = std::sqrt(0.5f * std::fabs(1.0f - dot)) / crosslen;
            q[0] = cx * s; q[1] = cy * s; q[2] = cz * s;
            q[3] = std::sqrt(0.5f * std::fabs(1.0f + dot));
        }
        // u = q * x-axis * radius, v = q * y-axis * radius.
        auto rotate = [&q](const float in[3], float out[3]) {
            // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
            float t[3] = {
                q[1]*in[2] - q[2]*in[1] + q[3]*in[0],
                q[2]*in[0] - q[0]*in[2] + q[3]*in[1],
                q[0]*in[1] - q[1]*in[0] + q[3]*in[2],
            };
            out[0] = in[0] + 2.0f * (q[1]*t[2] - q[2]*t[1]);
            out[1] = in[1] + 2.0f * (q[2]*t[0] - q[0]*t[2]);
            out[2] = in[2] + 2.0f * (q[0]*t[1] - q[1]*t[0]);
        };
        static const float xaxis[3] = {1.0f, 0.0f, 0.0f};
        static const float yaxis[3] = {0.0f, 1.0f, 0.0f};
        float u[3], v[3];
        rotate(xaxis, u);
        rotate(yaxis, v);
        for (int i = 0; i < 3; ++i) {
            u[i] *= radius;
            v[i] *= radius;
        }

        // Hatch texture scale (GL: _renderSection ~1852): pixels per
        // world unit at mid view depth from the projection matrix (the
        // stand-in for Coin's getWorldToScreenScale at the sight point),
        // times the bounding radius, over the texture width.
        float texscale = 0.0f;
        if (secconf.hatchEnable && hatchTex) {
            float hs = std::max(1e-4f, 0.3f * secconf.hatchScale);
            float worldPerVp;
            if (projMat[15] == 1.0f) {  // orthographic
                worldPerVp = 2.0f / projMat[0];
            } else {
                float near_ = projMat[14] / (projMat[10] - 1.0f);
                float far_ = projMat[14] / (projMat[10] + 1.0f);
                float wmid = near_ + 0.5f * (far_ - near_);
                worldPerVp = 2.0f * wmid / projMat[0];
            }
            float pixelsize = float(vpWidth) / (hs * worldPerVp);
            texscale = std::max(1e-3f, radius * pixelsize
                                           / float(hatchTex->width));
        }

        // GL vertex/texcoord assignment: v1=(0,s) v2=(0,0) v3=(s,0)
        // v4=(s,s) around center +/- u/v.
        auto set = [&](CapVertex &vert, float su, float sv,
                       float tu, float tv) {
            vert.px = center[0] + sv*v[0] + su*u[0];
            vert.py = center[1] + sv*v[1] + su*u[1];
            vert.pz = center[2] + sv*v[2] + su*u[2];
            vert.u = tu;
            vert.v = tv;
        };
        set(verts[0], -1.0f,  1.0f, 0.0f, texscale);
        set(verts[1],  1.0f,  1.0f, 0.0f, 0.0f);
        set(verts[2],  1.0f, -1.0f, texscale, 0.0f);
        set(verts[3], -1.0f, -1.0f, texscale, texscale);
    }

    /// Stencil section caps of clipped solids (GL: renderSection /
    /// renderSectionGrouped / _renderSection). For every eligible draw
    /// (whole solid triangle geometry with clip planes) and every one of
    /// its section planes: parity-mark the solid clipped by that plane
    /// alone, fill the cap quad where marked (clipped by the remaining
    /// planes outside concave mode), and clean the stencil up again.
    /// SectionFillGroup caps runs of same-colored draws together like
    /// GL, turning intersecting same-material solids into one boolean
    /// cut.
    void submitSectionCaps(BGFXView *view, const float *projMat)
    {
        auto isTransp = [](const Render::DrawCall &d) {
            return d.material.transparent
                || (d.material.pervertexcolor
                    && d.mesh && d.mesh->hasTransparency);
        };
        auto eligible = [this](const Render::DrawCall &d) {
            return d.material.type == Render::Material::Triangle
                && d.partIndex < 0
                && d.material.numclipplanes > 0
                && !d.material.ontop
                && d.mesh
                && (d.material.solidshape || d.mesh->hasSolid)
                && (secconf.fill || d.material.clipconcave);
        };

        // Opaque and transparent cap sources, following the buckets the
        // fills render in (GL collects section entries in renderOpaque
        // and renderTransparency): the scene minus draws replaced by
        // whole-object selections, plus the (deduplicated) whole-object
        // selection draws themselves.
        std::vector<const Render::DrawCall *> items[2];
        for (const auto &draw : scene) {
            if (eligible(draw)
                    && !(draw.objectKey && !hiddenKeys.empty()
                         && hiddenKeys.count(draw.objectKey)))
                items[isTransp(draw) ? 1 : 0].push_back(&draw);
        }
        for (const auto &sel : selections) {
            for (const auto &draw : sel.second) {
                if (eligible(draw)
                        && !(!dupDraws.empty() && dupDraws.count(&draw)))
                    items[isTransp(draw) ? 1 : 0].push_back(&draw);
            }
        }

        auto samePlanes = [](const Render::Material &a,
                             const Render::Material &b) {
            if (a.numclipplanes != b.numclipplanes)
                return false;
            return std::memcmp(a.clipplanes, b.clipplanes,
                    sizeof(a.clipplanes[0]) * a.numclipplanes) == 0;
        };

        for (int bucket = 0; bucket < 2; ++bucket) {
            const auto &list = items[bucket];
            uint16_t capView = bucket ? BGFXView::ViewSectionCapTransp
                                      : BGFXView::ViewSectionCap;
            for (size_t head = 0; head < list.size();) {
                const Render::Material &mat = list[head]->material;
                // Consecutive same-color same-planes run (GL groups on
                // diffuse + clippers; autozoom is not translated).
                size_t tail = head + 1;
                if (secconf.fillGroup && !mat.clipconcave) {
                    while (tail < list.size()
                            && list[tail]->material.diffuse == mat.diffuse
                            && samePlanes(list[tail]->material, mat))
                        ++tail;
                }

                // Union world bbox -> cap quad placement.
                float bmin[3], bmax[3];
                bool bvalid = false;
                for (size_t k = head; k < tail; ++k) {
                    const auto &d = *list[k];
                    if (d.bboxMin[0] > d.bboxMax[0])
                        continue;
                    if (!bvalid) {
                        bvalid = true;
                        for (int i = 0; i < 3; ++i) {
                            bmin[i] = d.bboxMin[i];
                            bmax[i] = d.bboxMax[i];
                        }
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            bmin[i] = qMin(bmin[i], d.bboxMin[i]);
                            bmax[i] = qMax(bmax[i], d.bboxMax[i]);
                        }
                    }
                }
                if (!bvalid) {
                    head = tail;
                    continue;
                }

                uint32_t color = secconf.fillInvert
                    ? invertCapColor(mat.diffuse) : mat.diffuse;
                for (int i = 0; i < mat.numclipplanes; ++i) {
                    bool marked = false;
                    for (size_t k = head; k < tail; ++k)
                        marked |= view->submitCapMark(
                            *list[k], mat.clipplanes[i], capView);
                    if (!marked)
                        continue;
                    CapVertex verts[4];
                    buildCapQuad(mat.clipplanes[i], bmin, bmax, projMat,
                                 view->width, verts);
                    // The cap of one plane is clipped by the remaining
                    // planes; concave mode leaves it unclipped (GL's
                    // clip state there).
                    float others[Render::Material::MaxClipPlanes][4];
                    int numother = 0;
                    if (!mat.clipconcave) {
                        for (int j = 0; j < mat.numclipplanes; ++j) {
                            if (j != i)
                                std::memcpy(others[numother++],
                                            mat.clipplanes[j],
                                            sizeof(others[0]));
                        }
                    }
                    view->submitCapQuad(verts, color, others, numother,
                                        secconf.hatchEnable
                                            && hatchTex != nullptr,
                                        bucket == 1, capView);
                    view->submitCapCleanup(verts, capView);
                }
                head = tail;
            }
        }
    }

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
