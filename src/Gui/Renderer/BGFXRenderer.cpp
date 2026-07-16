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

#include "FCConfig.h"
#include "BGFXRenderer.h"

#ifndef FC_OS_WIN32
# ifndef GL_GLEXT_PROTOTYPES
#   define GL_GLEXT_PROTOTYPES 1
# endif
#endif

#ifndef _PreComp_
# include <float.h>
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# endif
#endif

#include <map>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

#undef GL_GLEXT_VERSION
#include <QColor>
#include <QCoreApplication>
#include <QVariant>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QWindow>
#include <QDebug>

// #if !defined(FC_OS_MACOSX)
// # include <GL/gl.h>
// # include <GL/glu.h>
// # include <GL/glext.h>
// #endif

#include <bgfx/bgfx.h>
#include <bx/timer.h>
#include <bx/math.h>
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

extern "C" int _main_(int, char**) {
    return 0;
}

////////////////////////////////////////////////////////

namespace
{
    void freeFramebufferFunc(QOpenGLFunctions *funcs, GLuint id)
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

    bool _checkGLError(int line, const char *msg)
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

    bool _checkFramebufferStatus(int line)
    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
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
}

////////////////////////////////////////////////////////

class BGFXView;

namespace Render {

class BGFXRendererLibP {
public:
    BGFXRendererLibP() {
        for (auto &v : typeMap)
            types.push_back(v.first);
    }

    ~BGFXRendererLibP();

    BGFXView *getView(QOpenGLWidget *widget, RendererType::Enum type);

    void removeView(QOpenGLWidget *widget);

    bool prepare(QOpenGLWidget *widget, RendererType::Enum type)
    {
        if (!context) {
            context.reset(new QOpenGLContext);
            auto format = widget->format();
            context->setShareContext(QOpenGLContext::globalShareContext());
            context->setFormat(format);
            context->create();
            offscreen.reset(new QOffscreenSurface);
            offscreen->setFormat(format);
            offscreen->create();
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
                                     viewIds.clear();
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
#   elif defined(FC_OS_WIN)
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
            init.resolution.width = widget->width();
            init.resolution.height = widget->height();
            init.resolution.reset = BGFX_RESET_VSYNC;
            if (!bgfx::init(init)) {
                widget->makeCurrent();
                RENDER_ERR("init failed");
                return false;
            }
        }
        return true;
    }

    void shutdown();

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

    std::string resource()
    {
        return RendererFactory::resourcePath() + "bgfx/assets/";
    }

    typedef void (*FreeResourceFunc)(QOpenGLFunctions *functions, GLuint id);
    std::vector<std::pair<GLuint, FreeResourceFunc>> pendingRemoves;
    std::unordered_map<QOpenGLWidget *, std::unique_ptr<BGFXView>> views;
    std::set<uint16_t> viewIds;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> offscreen;

    std::map<std::string, RendererType::Enum> typeMap = {
        {"bgfx - OpenGL", RendererType::OpenGL},
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
    QWindow *window = nullptr;
    bool quitHooked = false;
};

BGFXRendererLibP _BGFXLib;
BGFXRendererLib BGFXLib;

} // namespace Renderer

// Interleaved vertex format built from the separate MeshData attribute
// arrays: position + normal + rgba8 color (white when the cache has no
// per-vertex colors, zero normal when it has no normals).
struct SceneVertex
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

bgfx::VertexLayout SceneVertex::ms_layout;
bool SceneVertex::ms_initialized = false;

// GPU buffers of one MeshData, keyed by MeshData::cacheId. A cache id
// always refers to identical content, so buffers are immutable and reused
// until the id disappears from the scene.
struct GpuMesh
{
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle tri = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle line = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle point = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;

    void destroy()
    {
        if (bgfx::isValid(vbh)) {
            bgfx::destroy(vbh);
            vbh = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(tri)) {
            bgfx::destroy(tri);
            tri = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(line)) {
            bgfx::destroy(line);
            line = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(point)) {
            bgfx::destroy(point);
            point = BGFX_INVALID_HANDLE;
        }
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
            if (mesh.colors)
                memcpy(&v.rgba, mesh.colors + i*4, 4);
            else
                v.rgba = 0xffffffff;
        }
        vbh = bgfx::createVertexBuffer(vmem, SceneVertex::ms_layout);

        if (mesh.numTriangleIndices > 0)
            tri = bgfx::createIndexBuffer(
                bgfx::copy(mesh.triangleIndices, mesh.numTriangleIndices * 4),
                BGFX_BUFFER_INDEX32);
        if (mesh.numLineIndices > 0)
            line = bgfx::createIndexBuffer(
                bgfx::copy(mesh.lineIndices, mesh.numLineIndices * 4),
                BGFX_BUFFER_INDEX32);
        if (mesh.numPointIndices > 0)
            point = bgfx::createIndexBuffer(
                bgfx::copy(mesh.pointIndices, mesh.numPointIndices * 4),
                BGFX_BUFFER_INDEX32);
    }
};

static inline void unpackColor(uint32_t rgba, float *out)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    out[3] = (rgba & 0xff) / 255.0f;
}

// 0xRRGGBBAA to the SceneVertex byte order (r,g,b,a in memory, i.e. what
// GpuMesh::upload memcpy's from MeshData::colors).
static inline uint32_t vertexColor(uint32_t rgba)
{
    return ((rgba >> 24) & 0xff)
        | (((rgba >> 16) & 0xff) << 8)
        | (((rgba >> 8) & 0xff) << 16)
        | ((rgba & 0xff) << 24);
}

////////////////////////////////////////////////////////

class BGFXView
{
public:
    // Pass sequence reproducing (a simplified subset of) SoFCRenderer's
    // draw order. Each is a bgfx view sharing the same framebuffer.
    enum PassView {
        ViewBackground = 0, // clear + gradient background quad (clip space)
        ViewOpaque,         // opaque triangles, lines, points
        ViewTransparent,    // blended, depth read-only, back-to-front
        ViewOnTop,          // scene geometry with on-top materials
        ViewHighlight,      // selection-on-top and preselection highlight
        NUM_VIEWS
    };

    ~BGFXView()
    {
        destroy();
    }

    void destroy()
    {
        for (auto &v : meshes)
            v.second.destroy();
        meshes.clear();
        if (bgfx::isValid(bgfxFbo)) {
            bgfx::destroy(bgfxFbo);
            bgfxFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMesh)) {
            bgfx::destroy(m_progMesh);
            m_progMesh = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progFlat)) {
            bgfx::destroy(m_progFlat);
            m_progFlat = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_matColor)) {
            bgfx::destroy(u_matColor);
            u_matColor = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_matEmissive)) {
            bgfx::destroy(u_matEmissive);
            u_matEmissive = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_matSpecular)) {
            bgfx::destroy(u_matSpecular);
            u_matSpecular = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_params)) {
            bgfx::destroy(u_params);
            u_params = BGFX_INVALID_HANDLE;
        }
        if (hasFBO) {
            _BGFXLib.freeFBO(fbo);
            hasFBO = false;
        }
    }

    bgfx::TextureHandle createTexture(bgfx::TextureFormat::Enum format, uint64_t flags = 0)
    {
        const uint64_t tsFlags = 0
            | BGFX_SAMPLER_MIN_POINT
            | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP
            | BGFX_TEXTURE_RT_WRITE_ONLY;
        return bgfx::createTexture2D(width, height, false, 1, format, tsFlags | flags);
    }

    void init()
    {
        destroy();
        width = uint16_t(widget->width());
        height = uint16_t(widget->height());

        int samples = widget->format().samples();
        uint64_t flags = 0;
        if (samples >= 8)
            flags = BGFX_TEXTURE_RT_MSAA_X8;
        else if (samples >= 4)
            flags = BGFX_TEXTURE_RT_MSAA_X4;
        else if (samples >= 2)
            flags = BGFX_TEXTURE_RT_MSAA_X2;
        else
            flags = BGFX_TEXTURE_RT;

        bgfxColor = createTexture(bgfx::TextureFormat::RGBA8, flags);
        //GL_DEPTH24_STENCIL8
        bgfxDepth = createTexture(bgfx::TextureFormat::D24S8, flags & ~BGFX_TEXTURE_RT);
        bgfx::Attachment attachment[2];
        // No mip chain on these render targets; the default resolve flag
        // (BGFX_RESOLVE_AUTO_GEN_MIPS) is also rejected for depth attachments.
        attachment[0].init(bgfxColor, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        attachment[1].init(bgfxDepth, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
        bgfxFbo = bgfx::createFrameBuffer(2, attachment, true);

        for (uint16_t i = 0; i < NUM_VIEWS; ++i)
            bgfx::setViewFrameBuffer(viewId + i, bgfxFbo);

        m_progMesh = loadProgram("vs_fc_mesh", "fs_fc_mesh",
                                 _BGFXLib.resource().c_str());
        m_progFlat = loadProgram("vs_fc_flat", "fs_fc_flat",
                                 _BGFXLib.resource().c_str());

        u_matColor = bgfx::createUniform("u_matColor", bgfx::UniformType::Vec4);
        u_matEmissive = bgfx::createUniform("u_matEmissive", bgfx::UniformType::Vec4);
        u_matSpecular = bgfx::createUniform("u_matSpecular", bgfx::UniformType::Vec4);
        u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    }

    GpuMesh *getMesh(const Render::MeshData &data)
    {
        GpuMesh &mesh = meshes[data.cacheId];
        mesh.lastUsed = frame;
        if (!bgfx::isValid(mesh.vbh))
            mesh.upload(data);
        return &mesh;
    }

    // Drop GPU buffers of caches that no draw call referenced recently.
    void collectMeshes()
    {
        for (auto it = meshes.begin(); it != meshes.end();) {
            if (it->second.lastUsed + 2 < frame) {
                it->second.destroy();
                it = meshes.erase(it);
            } else
                ++it;
        }
    }

    // Fullscreen gradient behind the scene, replicating
    // SoFCBackgroundGradient::GLRender vertex for vertex in clip space
    // (the background view has identity view/proj, so transparent scene
    // geometry blends against the real background colors). Depth is
    // neither tested nor written: the buffer keeps the far-plane clear
    // value, matching the GL path where the gradient sits at the far
    // plane.
    void submitBackground(const Render::Background &bg)
    {
        if (bg.type == Render::Background::Flat)
            return;

        uint32_t fcol = vertexColor(bg.fromColor);
        uint32_t tcol = vertexColor(bg.toColor);
        uint32_t mcol = vertexColor(bg.midColor);

        std::vector<SceneVertex> verts;
        auto vert = [](float x, float y, uint32_t rgba) {
            SceneVertex v;
            v.px = x; v.py = y; v.pz = 1.0f;
            v.nx = v.ny = 0.0f; v.nz = 1.0f;
            v.rgba = rgba;
            return v;
        };
        // Triangle-list expansion of a strip/fan given as a vertex list;
        // winding is irrelevant (the background draw does not cull).
        auto strip = [&verts](const std::vector<SceneVertex> &vs) {
            for (size_t i = 2; i < vs.size(); ++i) {
                verts.push_back(vs[i - 2]);
                verts.push_back(vs[i - 1]);
                verts.push_back(vs[i]);
            }
        };
        auto fan = [&verts](const std::vector<SceneVertex> &vs) {
            for (size_t i = 2; i < vs.size(); ++i) {
                verts.push_back(vs[0]);
                verts.push_back(vs[i - 1]);
                verts.push_back(vs[i]);
            }
        };

        if (bg.type == Render::Background::LinearGradient) {
            if (!bg.hasMid)
                strip({vert(-1.0f, 1.0f, fcol), vert(-1.0f, -1.0f, tcol),
                       vert(1.0f, 1.0f, fcol), vert(1.0f, -1.0f, tcol)});
            else {
                strip({vert(-1.0f, 1.0f, fcol), vert(-1.0f, 0.0f, mcol),
                       vert(1.0f, 1.0f, fcol), vert(1.0f, 0.0f, mcol)});
                strip({vert(-1.0f, 0.0f, mcol), vert(-1.0f, -1.0f, tcol),
                       vert(1.0f, 0.0f, mcol), vert(1.0f, -1.0f, tcol)});
            }
        } else {
            // Same 32-segment circle/oval tessellation as the Coin node.
            constexpr int kSegments = 32;
            constexpr float kStep = 2.0f * bx::kPi / kSegments;
            float circle[kSegments][2], oval[kSegments][2];
            for (int i = 0; i < kSegments; ++i) {
                float c = bx::cos(i * kStep), s = bx::sin(i * kStep);
                circle[i][0] = bx::kSqrt2 * c;
                circle[i][1] = bx::kSqrt2 * s;
                oval[i][0] = 0.3f * bx::kSqrt2 * c;
                oval[i][1] = s / bx::kSqrt2;
            }
            if (!bg.hasMid) {
                std::vector<SceneVertex> vs;
                vs.push_back(vert(0.0f, 0.0f, fcol));
                for (auto &p : circle)
                    vs.push_back(vert(p[0], p[1], tcol));
                vs.push_back(vs[1]);
                fan(vs);
            } else {
                std::vector<SceneVertex> vs;
                vs.push_back(vert(0.0f, 0.0f, fcol));
                for (auto &p : oval)
                    vs.push_back(vert(p[0], p[1], mcol));
                vs.push_back(vs[1]);
                fan(vs);
                vs.clear();
                for (int i = 0; i < kSegments; ++i) {
                    vs.push_back(vert(oval[i][0], oval[i][1], mcol));
                    vs.push_back(vert(circle[i][0], circle[i][1], tcol));
                }
                vs.push_back(vs[0]);
                vs.push_back(vs[1]);
                strip(vs);
            }
        }

        SceneVertex::init();
        uint32_t num = uint32_t(verts.size());
        if (bgfx::getAvailTransientVertexBuffer(num, SceneVertex::ms_layout)
                < num)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, num, SceneVertex::ms_layout);
        memcpy(tvb.data, verts.data(), num * sizeof(SceneVertex));

        float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float params[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // per-vertex color
        bgfx::setUniform(u_matColor, zero);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_MSAA);
        bgfx::submit(viewId + ViewBackground, m_progFlat);
        ++drawcount;
    }

    // Submission passes mirroring SoFCRenderer's delayed render loop.
    enum SubmitPass {
        PassNormal = 0,
        PassDepthOnly,   // depth-write-only prepass of on-top fills
        PassLineHidden,  // on-top lines/points, no depth test, dimmed
        PassLineSolid,   // on-top lines/points, depth LEQUAL, full color
    };

    void submit(const Render::DrawCall &draw, const float *viewMatrix,
                int pass = PassNormal)
    {
        const Render::Material &mat = draw.material;
        if (!draw.mesh || draw.mesh->numVertices == 0)
            return;

        GpuMesh *mesh = getMesh(*draw.mesh);
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        switch (mat.type) {
        case Render::Material::Triangle: ibh = mesh->tri; break;
        case Render::Material::Line: ibh = mesh->line; break;
        case Render::Material::Point: ibh = mesh->point; break;
        }
        if (!bgfx::isValid(ibh))
            return;

        bool transparent = mat.transparent
            || (mat.pervertexcolor && draw.mesh->hasTransparency);

        uint16_t passView = ontop ? ViewHighlight
            : mat.ontop ? ViewOnTop
            : transparent && mat.type == Render::Material::Triangle
                ? ViewTransparent
                : ViewOpaque;

        // GL parity (SoFCRenderer::applyMaterial ~520): on-top draws ignore
        // the depth test, only non-on-top transparent draws drop the depth
        // write. Disabling the depth test also disables depth writes (in GL
        // and every bgfx backend alike), which is why on-top fills need the
        // PassDepthOnly prepass before the line passes.
        bool depthtest = mat.ontop ? false : mat.depthtest;
        bool depthwrite = (!mat.ontop && transparent) ? false : mat.depthwrite;
        uint8_t depthfunc = mat.depthfunc;
        bool blend = transparent;
        float dimalpha = 1.0f;
        switch (pass) {
        case PassDepthOnly:
            depthtest = true;
            depthwrite = true;
            depthfunc = Render::Material::Less;
            blend = false;
            break;
        case PassLineHidden:
            depthtest = false;
            blend = true;
            dimalpha = mat.hiddenlinealpha;
            break;
        case PassLineSolid:
            depthtest = true;
            depthfunc = Render::Material::LEqual;
            depthwrite = false;
            blend = true;
            break;
        default:
            break;
        }

        uint64_t state = BGFX_STATE_MSAA;
        if (pass != PassDepthOnly)
            state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (depthtest)
            state |= depthFuncState(depthfunc);
        if (depthwrite)
            state |= BGFX_STATE_WRITE_Z;
        if (blend)
            state |= BGFX_STATE_BLEND_ALPHA;
        if (mat.culling && !mat.twoside
                        && mat.type == Render::Material::Triangle)
            state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
        if (mat.type == Render::Material::Line)
            state |= BGFX_STATE_PT_LINES;
        else if (mat.type == Render::Material::Point)
            state |= BGFX_STATE_PT_POINTS
                | BGFX_STATE_POINT_SIZE(uint32_t(qMax(mat.pointsize, 1.0f)));

        float color[4], emissive[4], specular[4], params[4];
        unpackColor(mat.diffuse, color);
        unpackColor(mat.emissive, emissive);
        unpackColor(mat.specular, specular);
        specular[3] = mat.shininess;
        params[0] = mat.pervertexcolor ? 1.0f : 0.0f;
        params[1] = (mat.lighting
                     && mat.type == Render::Material::Triangle) ? 1.0f : 0.0f;
        params[2] = mat.twoside ? 1.0f : 0.0f;
        // u_params.w: mesh program = NDC depth bias (polygon offset
        // approximation, no per-pixel slope term); flat program = alpha
        // ceiling used to dim depth-occluded on-top lines.
        if (mat.type == Render::Material::Triangle) {
            // One glPolygonOffset unit in NDC z: 2 (NDC range) * 16 LSB
            // headroom for the unevaluated slope factor / 2^24 depth bits.
            constexpr float kDepthBiasUnit = 2.0f * 16.0f / 16777216.0f;
            params[3] = mat.polygonoffset
                ? (mat.polygonoffsetfactor + mat.polygonoffsetunits)
                    * kDepthBiasUnit
                : 0.0f;
        } else {
            params[3] = dimalpha;
        }
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, emissive);
        bgfx::setUniform(u_matSpecular, specular);
        bgfx::setUniform(u_params, params);

        if (!draw.identity)
            bgfx::setTransform(draw.model);
        bgfx::setVertexBuffer(0, mesh->vbh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(ibh, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(ibh);
        bgfx::setState(state);

        static const bool dbgsubmit =
            (getenv("FC_BGFX_DEBUG_SUBMIT") != nullptr);
        if (dbgsubmit)
            fprintf(stderr,
                    "bgfx submit view=%d pass=%d type=%d part=%d state=%llx"
                    " color=%.2f,%.2f,%.2f,%.2f params=%g,%g,%g,%g\n",
                    passView, pass, mat.type, draw.partIndex,
                    (unsigned long long)state,
                    color[0], color[1], color[2], color[3],
                    params[0], params[1], params[2], params[3]);

        uint32_t depth = 0;
        if (passView == ViewTransparent
                 && draw.bboxMin[0] <= draw.bboxMax[0]) {
            // Sort key: distance of the world-space bbox center along the
            // view axis; the view is in DepthDescending mode (far first).
            float cx = (draw.bboxMin[0] + draw.bboxMax[0]) * 0.5f;
            float cy = (draw.bboxMin[1] + draw.bboxMax[1]) * 0.5f;
            float cz = (draw.bboxMin[2] + draw.bboxMax[2]) * 0.5f;
            const float *m = viewMatrix;
            float eyez = m[2]*cx + m[6]*cy + m[10]*cz + m[14];
            depth = bx::floatToBits(bx::max(-eyez, 0.0f));
        }

        bgfx::submit(viewId + passView,
                     mat.type == Render::Material::Triangle
                         ? m_progMesh : m_progFlat,
                     depth);
        ++drawcount;
    }

    static uint64_t depthFuncState(uint8_t func)
    {
        switch (func) {
        case Render::Material::Never:    return BGFX_STATE_DEPTH_TEST_NEVER;
        case Render::Material::Always:   return BGFX_STATE_DEPTH_TEST_ALWAYS;
        case Render::Material::Less:     return BGFX_STATE_DEPTH_TEST_LESS;
        case Render::Material::Equal:    return BGFX_STATE_DEPTH_TEST_EQUAL;
        case Render::Material::GEqual:   return BGFX_STATE_DEPTH_TEST_GEQUAL;
        case Render::Material::Greater:  return BGFX_STATE_DEPTH_TEST_GREATER;
        case Render::Material::NotEqual: return BGFX_STATE_DEPTH_TEST_NOTEQUAL;
        default:                         return BGFX_STATE_DEPTH_TEST_LEQUAL;
        }
    }

    void blit()
    {
        GLint prevFbo;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *) &prevFbo);
        if (!hasFBO) {
            hasFBO = true;
            GLuint colorBuffer = bgfx::getInternal(bgfxColor);
            GLuint depthBuffer = bgfx::getInternal(bgfxDepth);
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                        GL_RENDERBUFFER, colorBuffer);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        GL_RENDERBUFFER, depthBuffer);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depthBuffer);
            if (!checkFramebufferStatus()) {
                glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
                destroy();
                return;
            }
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBlitFramebuffer(0, 0, width, height,
                          0, 0, width, height,
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);
        checkGLError("blit");

        static const bool readback = (getenv("FC_BGFX_DEBUG_READBACK") != nullptr);
        if (readback) {
            std::vector<float> depth(width * height);
            std::vector<unsigned char> color(width * height * 4);
            glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT,
                         depth.data());
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                         color.data());
            long n = 0, r = 0, g = 0, b = 0;
            for (int i = 0; i < width * height; ++i) {
                if (depth[i] < 0.999f) {
                    ++n;
                    r += color[i*4];
                    g += color[i*4 + 1];
                    b += color[i*4 + 2];
                }
            }
            fprintf(stderr,
                    "bgfx fbo %dx%d: %ld geometry pixels, avg color %ld,%ld,%ld\n",
                    width, height, n,
                    n ? r/n : -1, n ? g/n : -1, n ? b/n : -1);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    }

    QOpenGLWidget *widget = nullptr;
    uint16_t viewId = 0;
    uint16_t width;
    uint16_t height;
    bgfx::FrameBufferHandle bgfxFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMesh = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progFlat = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matSpecular = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params = BGFX_INVALID_HANDLE;
    std::unordered_map<uint64_t, GpuMesh> meshes;
    uint64_t frame = 0;
    int drawcount = 0;
    bool ontop = false;   // route submits to the highlight pass
    GLuint fbo = 0;
    bool hasFBO = false;
};

class BGFXRenderer::Private
{
public:
    Private(QOpenGLWidget *widget)
        :widget(widget)
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
        _BGFXLib.removeView(widget);
    }

    bool render(const QColor &col,
                const void * viewMatrix,
                const void * projMatrix)
    {
        // The pending scene data (whatever its age) is consumed by this
        // frame; needsRedraw() reports false until new data arrives.
        sceneDirty = false;
        renderOk = false;

        if (_deinit)
            return false;

        auto view = _BGFXLib.getView(widget, type);
        if (!view)
            return false;

        if (widget->width() != int(view->width)
                || widget->height() != int(view->height))
            view->init();

        if (!bgfx::isValid(view->bgfxFbo)) {
            widget->makeCurrent();
            return false;
        }

        uint16_t base = view->viewId;
        uint16_t width = view->width;
        uint16_t height = view->height;
        uint32_t clearColor = (uint32_t(col.red()) << 24)
            | (uint32_t(col.green()) << 16)
            | (uint32_t(col.blue()) << 8)
            | 0xff;
        if (getenv("FC_BGFX_DEBUG_CLEAR"))
            clearColor = 0xff0000ff;

        for (uint16_t i = 0; i < BGFXView::NUM_VIEWS; ++i) {
            uint16_t id = base + i;
            bgfx::setViewClear(id,
                i == 0 ? uint16_t(BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH)
                       : uint16_t(BGFX_CLEAR_NONE),
                clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            // The background quad is submitted in clip space.
            if (i == BGFXView::ViewBackground)
                bgfx::setViewTransform(id, nullptr, nullptr);
            else
                bgfx::setViewTransform(id, viewMatrix, projMatrix);
            // On-top and highlight draws are blended painter-style: keep
            // submission order (GL pass order) instead of state sorting.
            bgfx::setViewMode(id, i == BGFXView::ViewTransparent
                    ? bgfx::ViewMode::DepthDescending
                    : i >= BGFXView::ViewOnTop
                        ? bgfx::ViewMode::Sequential
                        : bgfx::ViewMode::Default);
            bgfx::touch(id);
        }

        ++view->frame;
        view->drawcount = 0;
        view->submitBackground(background);
        const float *viewMat = reinterpret_cast<const float *>(viewMatrix);

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

        // Whole-object selection/highlight draws replace the object's
        // normal rendering (SoFCRenderer's selectionkeys/highlightkeys
        // skip): collect their object keys and hide matching scene draws.
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

        // 1. Normal scene draws, then on-top triangle fills (opaque before
        // transparent), mirroring the GL delayed-pass order. On-top lines
        // are deferred below so they draw over the selection fills.
        view->ontop = false;
        for (const auto &draw : scene) {
            if (!draw.material.ontop && !isHidden(draw))
                view->submit(draw, viewMat);
        }
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && !isTransp(draw)
                    && !isHidden(draw))
                view->submit(draw, viewMat);
        }
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && isTransp(draw)
                    && !isHidden(draw))
                view->submit(draw, viewMat);
        }

        // 2. Selection whole-object fills; positive ids are on-top
        // selections (SoFCRenderer::addSelection). Their lines/points are
        // deferred to the two-pass loop when it runs; single-part (e.g.
        // selected face) triangle draws come last of all.
        for (const auto &sel : selections) {
            view->ontop = sel.first > 0;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0)
                    continue;
                if (twoPass && sel.first > 0 && !isTriangle(draw))
                    continue;
                view->submit(draw, viewMat);
            }
        }

        // 3. Whole-object preselection fills before the depth prepass.
        view->ontop = true;
        if (hlWholeOnTop) {
            for (const auto &draw : highlight) {
                if (isTriangle(draw))
                    view->submit(draw, viewMat);
            }
        }

        if (twoPass) {
            // 4. Depth-write-only prepass of on-top fills: on-top draws
            // render without depth test and thus never write depth, so the
            // solid line pass below needs this to tell hidden from visible.
            if (sceneTwoPass) {
                for (const auto &draw : scene) {
                    if (draw.material.ontop && isTriangle(draw)
                            && !isHidden(draw))
                        view->submit(draw, viewMat, BGFXView::PassDepthOnly);
                }
            }
            if (selOnTopLine) {
                for (const auto &sel : selections) {
                    if (sel.first <= 0)
                        continue;
                    for (const auto &draw : sel.second) {
                        if (isTriangle(draw) && draw.partIndex < 0)
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
                            && !isHidden(draw))
                        view->submit(draw, viewMat, pass);
                }
                for (const auto &sel : selections) {
                    if (sel.first <= 0)
                        continue;
                    for (const auto &draw : sel.second) {
                        if (!isTriangle(draw))
                            view->submit(draw, viewMat, pass);
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
                        && !isHidden(draw))
                    view->submit(draw, viewMat);
            }
        }

        // 6. Single-part selection fills (e.g. the selected face), matching
        // GL's transpselectionsfaceontop position after the line passes.
        for (const auto &sel : selections) {
            view->ontop = sel.first > 0;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0)
                    view->submit(draw, viewMat);
            }
        }

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
                if (isTriangle(draw))
                    view->submit(draw, viewMat);
            }
            for (const auto &draw : highlight) {
                if (!isTriangle(draw))
                    view->submit(draw, viewMat);
            }
        }
        view->ontop = false;

        view->collectMeshes();

        widget->doneCurrent();
        _BGFXLib.makeCurrent();
        bgfx::frame();
        widget->makeCurrent();
        view->blit();

        if (!hasScene && !scene.empty())
            qDebug() << "bgfx: scene consumed:" << view->drawcount
                     << "draws," << view->meshes.size() << "meshes";

        if (getenv("FC_BGFX_DEBUG_READBACK"))
            fprintf(stderr, "bgfx frame %llu: scene=%zu sel=%zu hl=%zu"
                    " draws=%d\n",
                    (unsigned long long)view->frame, scene.size(),
                    selections.size(), highlight.size(),
                    view->drawcount);

        renderOk = true;
        hasScene = !scene.empty();
        return true;
    }

    void updateBBox()
    {
        bboxValid = false;
        for (const auto &draw : scene) {
            if (draw.bboxMin[0] > draw.bboxMax[0])
                continue;
            if (!bboxValid) {
                bboxValid = true;
                for (int i = 0; i < 3; ++i) {
                    bboxMin[i] = draw.bboxMin[i];
                    bboxMax[i] = draw.bboxMax[i];
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    bboxMin[i] = qMin(bboxMin[i], draw.bboxMin[i]);
                    bboxMax[i] = qMax(bboxMax[i], draw.bboxMax[i]);
                }
            }
        }
    }

    QOpenGLWidget *widget;
    bool _deinit = false;
    RendererType::Enum type;
    std::string typeName;

    // CPU-side scene data fed through Render::Renderer's scene API. GPU
    // upload happens lazily during render(), so the feed may arrive before
    // bgfx is initialized.
    Render::DrawCallList scene;
    Render::Background background;
    std::map<int, Render::DrawCallList> selections;
    Render::DrawCallList highlight;
    std::unordered_set<uint64_t> hiddenKeys;
    bool hlWholeOnTop = false;
    bool sceneDirty = false;
    bool hasScene = false;
    bool renderOk = false;
    bool bboxValid = false;
    float bboxMin[3], bboxMax[3];
};

BGFXRenderer::BGFXRenderer(QOpenGLWidget *widget)
    :pimpl(new Private(widget))
{
}

BGFXRenderer::~BGFXRenderer()
{
}

bool BGFXRenderer::render(const QColor &col,
                          const void * viewMatrix,
                          const void * projMatrix)
{
    return pimpl->render(col, viewMatrix, projMatrix);
}

bool BGFXRenderer::boundBox(float &xmin, float &ymin, float &zmin,
                            float &xmax, float &ymax, float &zmax)
{
    if (!pimpl->bboxValid)
        return false;
    xmin = pimpl->bboxMin[0];
    ymin = pimpl->bboxMin[1];
    zmin = pimpl->bboxMin[2];
    xmax = pimpl->bboxMax[0];
    ymax = pimpl->bboxMax[1];
    zmax = pimpl->bboxMax[2];
    return true;
}

void BGFXRenderer::setScene(DrawCallList &&draws)
{
    pimpl->scene = std::move(draws);
    pimpl->sceneDirty = true;
    pimpl->updateBBox();
}

void BGFXRenderer::setBackground(const Background &bg)
{
    pimpl->background = bg;
}

static void dumpFeed(const char *tag, int id, const Render::DrawCallList &draws)
{
    if (!getenv("FC_BGFX_DEBUG_FEED"))
        return;
    fprintf(stderr, "bgfx feed %s id=%d: %zu draws\n", tag, id, draws.size());
    for (const auto &d : draws) {
        const auto &m = d.material;
        fprintf(stderr,
                "  type=%d part=%d range=%d+%d diffuse=%08x emissive=%08x"
                " pvc=%d light=%d transp=%d ontop=%d dtest=%d dwrite=%d"
                " dfunc=%d lw=%.1f po=%d/%.1f/%.1f hla=%.2f\n",
                m.type, d.partIndex, d.indexStart, d.indexCount,
                m.diffuse, m.emissive, m.pervertexcolor, m.lighting,
                m.transparent, m.ontop, m.depthtest, m.depthwrite,
                m.depthfunc, m.linewidth, m.polygonoffset,
                m.polygonoffsetfactor, m.polygonoffsetunits,
                m.hiddenlinealpha);
    }
}

void BGFXRenderer::addSelection(int id, DrawCallList &&draws)
{
    dumpFeed("sel", id, draws);
    pimpl->selections[id] = std::move(draws);
    pimpl->sceneDirty = true;
}

void BGFXRenderer::removeSelection(int id)
{
    if (pimpl->selections.erase(id))
        pimpl->sceneDirty = true;
}

void BGFXRenderer::setHighlight(DrawCallList &&draws, bool wholeOnTop)
{
    dumpFeed("hl", wholeOnTop, draws);
    pimpl->highlight = std::move(draws);
    pimpl->hlWholeOnTop = wholeOnTop;
    pimpl->sceneDirty = true;
}

void BGFXRenderer::clearHighlight()
{
    if (!pimpl->highlight.empty())
        pimpl->sceneDirty = true;
    pimpl->highlight.clear();
    pimpl->hlWholeOnTop = false;
}

bool BGFXRenderer::needsRedraw() const
{
    return pimpl->sceneDirty;
}

bool BGFXRenderer::canSkipInternal() const
{
    return pimpl->renderOk && pimpl->hasScene && !pimpl->_deinit;
}

const std::string &BGFXRenderer::type() const
{
    return pimpl->typeName;
}

//////////////////////////////////////////////////////////////////////

BGFXRendererLib::BGFXRendererLib()
{
    RendererFactory::registerLib(this);
}

const std::string &BGFXRendererLib::name() const
{
    return _BGFXLib.name;
}

const std::vector<std::string> &BGFXRendererLib::types() const
{
    return _BGFXLib.types;
}

std::unique_ptr<Renderer> BGFXRendererLib::create(
        const std::string &type, QOpenGLWidget *widget) const
{
    std::unique_ptr<Renderer> res;
    auto it = _BGFXLib.typeMap.find(type);
    if (it == _BGFXLib.typeMap.end()) {
        RENDER_WARN("Unsupported renderer type " << type.c_str());
        return res;
    }
    if (_BGFXLib.currentType != it->second) {
        for (auto renderer : _BGFXLib.renderers) {
            if (renderer->type != it->second)
                renderer->deinit();
        }
        _BGFXLib.shutdown();
    }
    auto renderer = new BGFXRenderer(widget);
    res.reset(renderer);
    renderer->pimpl->typeName = it->first;
    renderer->pimpl->type = it->second;
    return res;
}

/////////////////////////////////////////////////////////
void BGFXRendererLibP::removeView(QOpenGLWidget *widget)
{
    auto it = views.find(widget);
    if (it != views.end()) {
        viewIds.erase(it->second->viewId);
        views.erase(it);
        if (views.empty())
            _BGFXLib.shutdown();
    }
}

BGFXView *BGFXRendererLibP::getView(QOpenGLWidget *widget, RendererType::Enum type)
{
    if (!prepare(widget, type))
        return nullptr;

    auto &view = views[widget];
    if (!view) {
        view.reset(new BGFXView);
        view->widget = widget;
        // Each viewer consumes a contiguous block of NUM_VIEWS bgfx view
        // ids; viewIds stores the block base ids.
        view->viewId = 0;
        for (int id : viewIds) {
            if (view->viewId == id)
                view->viewId += BGFXView::NUM_VIEWS;
            else
                break;
        }
        viewIds.insert(view->viewId);
    }
    return view.get();
}

BGFXRendererLibP::~BGFXRendererLibP()
{
    // This destructor runs at library unload, when Qt is partially or fully
    // torn down; deleting the QOpenGLContext/QOffscreenSurface (or views
    // holding bgfx resources) here crashes inside Qt. If shutdown() ran the
    // pointers are already null; otherwise leak them, the process is exiting.
    for (auto &v : views)
        v.second.release();
    views.clear();
    context.release();
    offscreen.release();
}

void BGFXRendererLibP::shutdown()
{
    if (currentType == RendererType::Noop)
        return;
    // Must make the context current before shutdown bgfx, or else it seems to
    // mess up with the other context that is currently active
    makeCurrent();
    bgfx::shutdown();
    if (window) {
        window->deleteLater();
        window = nullptr;
    }
    context.reset();
    offscreen.reset();
    currentType = RendererType::Noop;
}
