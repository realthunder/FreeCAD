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

#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include <tuple>
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

// Second vertex stream of textured meshes: 2D texture coordinates,
// bound only by the textured mesh programs (texcoords live outside
// SceneVertex so untextured scenes don't pay for them).
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

bgfx::VertexLayout TexCoordVertex::ms_layout;
bool TexCoordVertex::ms_initialized = false;

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

bgfx::VertexLayout LineQuadVertex::ms_layout;
bgfx::VertexLayout LineQuadVertex::ms_instLayout;
bgfx::VertexLayout LineQuadVertex::ms_pointInstLayout;
bool LineQuadVertex::ms_initialized = false;

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

bgfx::VertexLayout CapVertex::ms_layout;
bool CapVertex::ms_initialized = false;

// GPU buffers of one MeshData, keyed by MeshData::cacheId. A cache id
// always refers to identical content, so buffers are immutable and reused
// until the id disappears from the scene.
struct GpuMesh
{
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle tri = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle line = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle point = BGFX_INVALID_HANDLE;
    /// Per-segment instance data (endpoints + colors) feeding the
    /// quad-expanded thick line path; invalid without instancing support.
    bgfx::VertexBufferHandle lineInst = BGFX_INVALID_HANDLE;
    /// Per-point instance data (position + color) feeding the point
    /// sprite path; invalid without instancing support.
    bgfx::VertexBufferHandle pointInst = BGFX_INVALID_HANDLE;
    /// Seam-filtered variants of line/lineInst (hidden-line hideSeam);
    /// only created when the mesh carries a no-seam index set.
    bgfx::IndexBufferHandle lineNoSeam = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle lineNoSeamInst = BGFX_INVALID_HANDLE;
    /// Triangle-edge segment / corner instance data feeding the stencil
    /// outline passes, built lazily on first outline use of the mesh.
    /// Instance i maps 1:1 onto triangle index position i (the edge
    /// leaving that corner), so partial index ranges translate directly
    /// into instance ranges.
    bgfx::VertexBufferHandle triEdgeInst = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle triCornerInst = BGFX_INVALID_HANDLE;
    /// Texture-coordinate stream of textured draws (second vertex
    /// stream), built lazily on first textured use.
    bgfx::VertexBufferHandle texcoord = BGFX_INVALID_HANDLE;
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
        if (bgfx::isValid(lineInst)) {
            bgfx::destroy(lineInst);
            lineInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(pointInst)) {
            bgfx::destroy(pointInst);
            pointInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(lineNoSeam)) {
            bgfx::destroy(lineNoSeam);
            lineNoSeam = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(lineNoSeamInst)) {
            bgfx::destroy(lineNoSeamInst);
            lineNoSeamInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(triEdgeInst)) {
            bgfx::destroy(triEdgeInst);
            triEdgeInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(triCornerInst)) {
            bgfx::destroy(triCornerInst);
            triCornerInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(texcoord)) {
            bgfx::destroy(texcoord);
            texcoord = BGFX_INVALID_HANDLE;
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

        if (mesh.numLineIndices > 1
                && (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING))
            lineInst = makeSegmentInstances(
                mesh, mesh.lineIndices, mesh.numLineIndices);

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
    void ensureNoSeam(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(lineNoSeam) || mesh.numNoSeamLineIndices <= 1)
            return;
        lineNoSeam = bgfx::createIndexBuffer(
            bgfx::copy(mesh.noSeamLineIndices,
                       mesh.numNoSeamLineIndices * 4),
            BGFX_BUFFER_INDEX32);
        if (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)
            lineNoSeamInst = makeSegmentInstances(
                mesh, mesh.noSeamLineIndices, mesh.numNoSeamLineIndices);
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
    }
};

// GPU texture of one Render::TextureImage, keyed by
// TextureImage::textureId (a texture id always refers to identical
// content, so textures are immutable and reused until unreferenced).
struct GpuTexture
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;

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
        uint64_t flags = 0;
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
static void setDrawTransform(const Render::DrawCall &draw,
                             float autozoomScale)
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
        if (entry.resetmatrix) {
            if (entry.identity)
                bx::mtxIdentity(m);
            else
                std::memcpy(m, entry.matrix, sizeof(m));
        }
        else if (!entry.identity) {
            float tmp[16];
            bx::mtxMul(tmp, entry.matrix, m);
            std::memcpy(m, tmp, sizeof(m));
        }
        float sf = entry.scaleFactor == 0.0f
            ? 1.0f : entry.scaleFactor * autozoomScale;
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
        m[15] = 1.0f;  // the translation row m[12..14] stays
    }
    if (!draw.identity) {
        float tmp[16];
        bx::mtxMul(tmp, draw.model, m);
        std::memcpy(m, tmp, sizeof(m));
    }
    bgfx::setTransform(m);
}

static inline void unpackColor(uint32_t rgba, float *out)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    out[3] = (rgba & 0xff) / 255.0f;
}

// FNV-1a accumulation for the shadow-map caster-set hash.
static inline void hashBytes(uint64_t &h, const void *data, size_t len)
{
    auto p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
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
        ViewShadow,         // variance shadow map moments of shadow
                            // casting scene triangles, rendered from the
                            // scene light (own framebuffer, light camera)
        ViewShadowBlurH,    // separable gaussian blur of the shadow
                            // moments (ShadowSmoothBorder): horizontal
                            // into the ping texture ...
        ViewShadowBlurV,    // ... and vertical back into the moments
                            // texture, so receivers and the volumetric
                            // raymarch keep sampling the same target
        ViewAOPrepass,      // SSAO depth+normal prepass of opaque scene
                            // triangles into a non-MSAA RGBA16F target
                            // (own framebuffer, own depth)
        ViewWaterFront,     // nearest front-face depths of water body
                            // draws (prepass shader family, own
                            // framebuffer): per-pixel entry of the
                            // volumetric water medium
        ViewWaterBack,      // farthest back-face depths of water body
                            // draws (depth GREATER, cleared to 0): the
                            // medium exit; no front but a back face
                            // means the camera is under water
        ViewAOGen,          // fullscreen SSAO generation into the R8 AO
                            // target (hemisphere kernel over the prepass)
        ViewAOBlur,         // fullscreen 4x4 AO blur into a second R8
        ViewVolGen,         // volumetric light shafts: half-res raymarch
                            // of the shadow map through the scattering
                            // medium, bounded by the prepass depth (own
                            // half-res framebuffer, scene transforms for
                            // position reconstruction)
        ViewOpaque,         // opaque triangles, lines, points
        ViewSectionCap,     // stencil section caps of clipped opaque
                            // solids (GL: _renderSection; before the
                            // outline/transparent passes like the GL
                            // opaque-loop caps, and the cap parity
                            // marking needs the stencil buffer before
                            // the outline passes leave their marks)
        ViewAOApply,        // fullscreen multiply of the blurred AO onto
                            // the opaque scene color (after the caps,
                            // before outlines/transparency)
        ViewOutline,        // hidden-line stencil outlines of scene draws
                            // (after all opaque geometry so the depth
                            // test sees the whole scene, before the
                            // transparent bucket blends over them)
        ViewVolApply,       // fullscreen bilateral upsample of the
                            // half-res inscatter, composited onto the
                            // opaque scene before the transparent bucket
                            // (after the outlines so edges are fogged
                            // like the fills)
        ViewTransparent,    // transparent triangles: WBOIT accumulation
                            // into the OIT targets, or blended
                            // back-to-front into the scene FBO when OIT
                            // is unavailable
        ViewOITComposite,   // fullscreen WBOIT resolve onto the scene FBO
        ViewSectionCapTransp, // section caps of clipped transparent
                            // solids, after the transparent bucket like
                            // GL's grouped section pass; stencil-cleared
                            // because the outline views left marks
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
        for (auto &v : textures)
            v.second.destroy();
        textures.clear();
        // SSAO resources: framebuffers before the textures they reference.
        for (auto fb : {&aoPrepassFbo, &aoGenFbo, &aoBlurFbo}) {
            if (bgfx::isValid(*fb)) {
                bgfx::destroy(*fb);
                *fb = BGFX_INVALID_HANDLE;
            }
        }
        for (auto tex : {&aoNormalZ, &aoDepth, &aoTex, &aoBlurTex,
                         &aoNoiseTex}) {
            if (bgfx::isValid(*tex)) {
                bgfx::destroy(*tex);
                *tex = BGFX_INVALID_HANDLE;
            }
        }
        // Volumetric resources: the framebuffers before their textures.
        for (auto fb : {&volFbo, &waterFrontFbo, &waterBackFbo}) {
            if (bgfx::isValid(*fb)) {
                bgfx::destroy(*fb);
                *fb = BGFX_INVALID_HANDLE;
            }
        }
        for (auto tex : {&volTex, &waterFrontTex, &waterBackTex,
                         &waterFrontDepth, &waterBackDepth}) {
            if (bgfx::isValid(*tex)) {
                bgfx::destroy(*tex);
                *tex = BGFX_INVALID_HANDLE;
            }
        }
        for (auto uni : {&s_texVol, &u_volParams, &u_volMedium,
                         &u_volTexel, &s_texWaterFront, &s_texWaterBack,
                         &u_waterSigma}) {
            if (bgfx::isValid(*uni)) {
                bgfx::destroy(*uni);
                *uni = BGFX_INVALID_HANDLE;
            }
        }
        for (auto prog : {&m_progPrepass, &m_progPrepassClip, &m_progSsao,
                          &m_progSsaoBlur, &m_progSsaoApply,
                          &m_progVol, &m_progVolApply, &m_progVolExt}) {
            if (bgfx::isValid(*prog)) {
                bgfx::destroy(*prog);
                *prog = BGFX_INVALID_HANDLE;
            }
        }
        // Shadow resources: the framebuffers before their textures. The
        // recreated moments texture starts empty, so the cached-map hash
        // resets with it.
        shadowMapHash = 0;
        for (auto fb : {&shadowFbo, &shadowBlurFbo, &shadowBlurBackFbo}) {
            if (bgfx::isValid(*fb)) {
                bgfx::destroy(*fb);
                *fb = BGFX_INVALID_HANDLE;
            }
        }
        for (auto tex : {&shadowTex, &shadowDepth, &shadowBlurTex}) {
            if (bgfx::isValid(*tex)) {
                bgfx::destroy(*tex);
                *tex = BGFX_INVALID_HANDLE;
            }
        }
        for (auto prog : {&m_progShadow, &m_progShadowClip,
                          &m_progShadowBlur}) {
            if (bgfx::isValid(*prog)) {
                bgfx::destroy(*prog);
                *prog = BGFX_INVALID_HANDLE;
            }
        }
        for (auto uni : {&s_texShadow, &u_shadowParams, &u_lightDir,
                         &u_lightPos, &u_lightColor, &u_shadowMatrix,
                         &u_shadowBlur, &u_evsm}) {
            if (bgfx::isValid(*uni)) {
                bgfx::destroy(*uni);
                *uni = BGFX_INVALID_HANDLE;
            }
        }
        // PBR environment resources.
        for (auto tex : {&m_envTex, &m_dummyEnvTex}) {
            if (bgfx::isValid(*tex)) {
                bgfx::destroy(*tex);
                *tex = BGFX_INVALID_HANDLE;
            }
        }
        m_envBuilt = false;
        for (auto uni : {&s_texNormalZ, &s_texAONoise, &s_texAO,
                         &u_aoParams, &u_aoKernel,
                         &s_texEnv, &u_pbrParams, &u_envSH,
                         &s_texBump, &u_bumpParams,
                         &s_texEmissive, &s_texOcclusion,
                         &s_texMetallicRoughness}) {
            if (bgfx::isValid(*uni)) {
                bgfx::destroy(*uni);
                *uni = BGFX_INVALID_HANDLE;
            }
        }
        // The OIT framebuffer references bgfxDepth (owned by bgfxFbo),
        // so it goes first.
        if (bgfx::isValid(oitFbo)) {
            bgfx::destroy(oitFbo);
            oitFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(oitAccum)) {
            bgfx::destroy(oitAccum);
            oitAccum = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(oitReveal)) {
            bgfx::destroy(oitReveal);
            oitReveal = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(bgfxFbo)) {
            bgfx::destroy(bgfxFbo);
            bgfxFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMesh)) {
            bgfx::destroy(m_progMesh);
            m_progMesh = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshInst)) {
            bgfx::destroy(m_progMeshInst);
            m_progMeshInst = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_instParams)) {
            bgfx::destroy(u_instParams);
            u_instParams = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progFlat)) {
            bgfx::destroy(m_progFlat);
            m_progFlat = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshClip)) {
            bgfx::destroy(m_progMeshClip);
            m_progMeshClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progFlatClip)) {
            bgfx::destroy(m_progFlatClip);
            m_progFlatClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progLine)) {
            bgfx::destroy(m_progLine);
            m_progLine = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progLineClip)) {
            bgfx::destroy(m_progLineClip);
            m_progLineClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progLinePat)) {
            bgfx::destroy(m_progLinePat);
            m_progLinePat = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progLinePatClip)) {
            bgfx::destroy(m_progLinePatClip);
            m_progLinePatClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progPoint)) {
            bgfx::destroy(m_progPoint);
            m_progPoint = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progPointClip)) {
            bgfx::destroy(m_progPointClip);
            m_progPointClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshTex)) {
            bgfx::destroy(m_progMeshTex);
            m_progMeshTex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshTexClip)) {
            bgfx::destroy(m_progMeshTexClip);
            m_progMeshTexClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshOitTex)) {
            bgfx::destroy(m_progMeshOitTex);
            m_progMeshOitTex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshOitTexClip)) {
            bgfx::destroy(m_progMeshOitTexClip);
            m_progMeshOitTexClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(s_texColor)) {
            bgfx::destroy(s_texColor);
            s_texColor = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_texMatrix)) {
            bgfx::destroy(u_texMatrix);
            u_texMatrix = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_texParams)) {
            bgfx::destroy(u_texParams);
            u_texParams = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_texBlendColor)) {
            bgfx::destroy(u_texBlendColor);
            u_texBlendColor = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshOit)) {
            bgfx::destroy(m_progMeshOit);
            m_progMeshOit = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progMeshOitClip)) {
            bgfx::destroy(m_progMeshOitClip);
            m_progMeshOitClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progComp)) {
            bgfx::destroy(m_progComp);
            m_progComp = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progCap)) {
            bgfx::destroy(m_progCap);
            m_progCap = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_progCapClip)) {
            bgfx::destroy(m_progCapClip);
            m_progCapClip = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(s_texHatch)) {
            bgfx::destroy(s_texHatch);
            s_texHatch = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_whiteTex)) {
            bgfx::destroy(m_whiteTex);
            m_whiteTex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_hatchTex)) {
            bgfx::destroy(m_hatchTex);
            m_hatchTex = BGFX_INVALID_HANDLE;
        }
        m_hatchVersion = 0;
        if (bgfx::isValid(s_texAccum)) {
            bgfx::destroy(s_texAccum);
            s_texAccum = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(s_texReveal)) {
            bgfx::destroy(s_texReveal);
            s_texReveal = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_lineQuadVb)) {
            bgfx::destroy(m_lineQuadVb);
            m_lineQuadVb = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_lineQuadIb)) {
            bgfx::destroy(m_lineQuadIb);
            m_lineQuadIb = BGFX_INVALID_HANDLE;
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
        if (bgfx::isValid(u_clipParams)) {
            bgfx::destroy(u_clipParams);
            u_clipParams = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_clipPlanes)) {
            bgfx::destroy(u_clipPlanes);
            u_clipPlanes = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(u_linePattern)) {
            bgfx::destroy(u_linePattern);
            u_linePattern = BGFX_INVALID_HANDLE;
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
        // NOTE: the MSAA levels are an enum in the RT flag nibble, not
        // orthogonal bits — masking BGFX_TEXTURE_RT out of them would
        // turn MSAA_X4 (0x3) into MSAA_X2 (0x2) and desync the depth
        // sample count from the color attachment's.
        bgfxDepth = createTexture(bgfx::TextureFormat::D24S8, flags);
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
        m_progMeshClip = loadProgram("vs_fc_mesh_clip", "fs_fc_mesh_clip",
                                     _BGFXLib.resource().c_str());
        m_progFlatClip = loadProgram("vs_fc_flat_clip", "fs_fc_flat_clip",
                                     _BGFXLib.resource().c_str());
        m_progMeshTex = loadProgram("vs_fc_mesh_tex", "fs_fc_mesh_tex",
                                    _BGFXLib.resource().c_str());
        m_progMeshTexClip = loadProgram("vs_fc_mesh_tex_clip",
                                        "fs_fc_mesh_tex_clip",
                                        _BGFXLib.resource().c_str());
        s_texColor = bgfx::createUniform("s_texColor",
                                         bgfx::UniformType::Sampler);
        u_texMatrix = bgfx::createUniform("u_texMatrix",
                                          bgfx::UniformType::Mat4);
        u_texParams = bgfx::createUniform("u_texParams",
                                          bgfx::UniformType::Vec4);
        u_texBlendColor = bgfx::createUniform("u_texBlendColor",
                                              bgfx::UniformType::Vec4);

        // Thick lines: instanced screen-space quad expansion (there is no
        // fixed-function line width in modern APIs). Without instancing
        // support every line falls back to 1px primitives.
        m_instancing =
            (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) != 0;
        // Publish the capability so geometry producers (Part tessellation)
        // know whether shared-instance scene structure will actually batch.
        Render::Renderer::setInstancingHint(m_instancing);
        if (m_instancing) {
            // Cross-object instancing: identical placements of one shared
            // geometry cache (Link arrays) collapse into a single
            // instanced submit carrying {model matrix, diffuse} per
            // instance.
            m_progMeshInst = loadProgram("vs_fc_mesh_inst", "fs_fc_mesh",
                                         _BGFXLib.resource().c_str());
            u_instParams = bgfx::createUniform("u_instParams",
                                               bgfx::UniformType::Vec4);
            m_progLine = loadProgram("vs_fc_line", "fs_fc_flat",
                                     _BGFXLib.resource().c_str());
            m_progLineClip = loadProgram("vs_fc_line_clip", "fs_fc_flat_clip",
                                         _BGFXLib.resource().c_str());
            m_progLinePat = loadProgram("vs_fc_line_pat", "fs_fc_line_pat",
                                        _BGFXLib.resource().c_str());
            m_progLinePatClip = loadProgram("vs_fc_line_pat_clip",
                                            "fs_fc_line_pat_clip",
                                            _BGFXLib.resource().c_str());
            m_progPoint = loadProgram("vs_fc_point", "fs_fc_flat",
                                      _BGFXLib.resource().c_str());
            m_progPointClip = loadProgram("vs_fc_point_clip",
                                          "fs_fc_flat_clip",
                                          _BGFXLib.resource().c_str());
            LineQuadVertex::init();
            static const LineQuadVertex quad[4] = {
                {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
            };
            static const uint16_t quadIndices[6] = {0, 1, 2, 1, 3, 2};
            m_lineQuadVb = bgfx::createVertexBuffer(
                bgfx::makeRef(quad, sizeof(quad)),
                LineQuadVertex::ms_layout);
            m_lineQuadIb = bgfx::createIndexBuffer(
                bgfx::makeRef(quadIndices, sizeof(quadIndices)));
        }

        // Section caps: the cap quad fill with hatch texture modulation.
        // The stencil mark and cleanup passes reuse the flat programs.
        m_progCap = loadProgram("vs_fc_cap", "fs_fc_cap",
                                _BGFXLib.resource().c_str());
        m_progCapClip = loadProgram("vs_fc_cap_clip", "fs_fc_cap_clip",
                                    _BGFXLib.resource().c_str());
        s_texHatch = bgfx::createUniform("s_texHatch",
                                         bgfx::UniformType::Sampler);
        // 1x1 white stand-in so the cap program samples neutrally when
        // hatching is disabled (and for the stencil cleanup pass).
        static const uint32_t white = 0xffffffff;
        m_whiteTex = bgfx::createTexture2D(1, 1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(&white, sizeof(white)));
        CapVertex::init();

        // PBR: the mesh programs always carry the environment sampler
        // (the branch is uniform-selected); a 1x1 black cube stands in
        // while PBR is off or unavailable. The real environment is built
        // on demand (ensureEnvironment).
        s_texEnv = bgfx::createUniform("s_texEnv",
                                       bgfx::UniformType::Sampler);
        u_pbrParams = bgfx::createUniform("u_pbrParams",
                                          bgfx::UniformType::Vec4);
        u_envSH = bgfx::createUniform("u_envSH",
                                      bgfx::UniformType::Vec4, kEnvSH);
        // Bump mapping of the textured mesh programs (unit 2; the 1x1
        // white cap texture stands in when a draw has no bump map).
        s_texBump = bgfx::createUniform("s_texBump",
                                        bgfx::UniformType::Sampler);
        u_bumpParams = bgfx::createUniform("u_bumpParams",
                                           bgfx::UniformType::Vec4);
        // Emissive/occlusion material maps of the textured mesh programs
        // (units 4/5; u_texParams.zw flag their presence, the white
        // stand-in is never sampled).
        s_texEmissive = bgfx::createUniform("s_texEmissive",
                                            bgfx::UniformType::Sampler);
        s_texOcclusion = bgfx::createUniform("s_texOcclusion",
                                             bgfx::UniformType::Sampler);
        // Metallic-roughness map at unit 6; u_pbrParams.x = 2 flags it.
        s_texMetallicRoughness =
            bgfx::createUniform("s_texMetallicRoughness",
                                bgfx::UniformType::Sampler);

        // Shadows: variance moments rendered from the scene light of the
        // Shadow draw style (unit 3 of the mesh programs; the white
        // stand-in reads as fully lit). Needs a renderable two-channel
        // float format.
        s_texShadow = bgfx::createUniform("s_texShadow",
                                          bgfx::UniformType::Sampler);
        u_shadowParams = bgfx::createUniform("u_shadowParams",
                                             bgfx::UniformType::Vec4);
        u_lightDir = bgfx::createUniform("u_lightDir",
                                         bgfx::UniformType::Vec4);
        u_lightPos = bgfx::createUniform("u_lightPos",
                                         bgfx::UniformType::Vec4);
        u_lightColor = bgfx::createUniform("u_lightColor",
                                           bgfx::UniformType::Vec4);
        u_shadowMatrix = bgfx::createUniform("u_shadowMatrix",
                                             bgfx::UniformType::Mat4);
        u_evsm = bgfx::createUniform("u_evsm", bgfx::UniformType::Vec4);
        // EVSM: the moments store an exponential warp of the light
        // window depth (exp(c z), exp(c z)^2), which curbs VSM's light
        // bleeding at overlapping occluders. RG32F carries the classic
        // c = 42 warp; the RG16F fallback must keep exp(2 c) inside
        // half-float range, so its warp is much weaker (c = 5 — better
        // than plain VSM, worse than fp32). Float-linear filtering of
        // 32-bit targets is not guaranteed on WebGL2 — revisit the
        // preference when the WASM build lands.
        auto shadowFormat = bgfx::TextureFormat::RG32F;
        shadowWarp = 42.0f;
        m_shadow = (bgfx::getCaps()->formats[shadowFormat]
                    & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        if (!m_shadow) {
            shadowFormat = bgfx::TextureFormat::RG16F;
            shadowWarp = 5.0f;
            m_shadow = (bgfx::getCaps()->formats[shadowFormat]
                        & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        }
        if (m_shadow) {
            this->shadowFormat = shadowFormat;
            shadowSize = 0;  // targets created on first use
            m_progShadow = loadProgram("vs_fc_shadow", "fs_fc_shadow",
                                       _BGFXLib.resource().c_str());
            m_progShadowClip = loadProgram("vs_fc_shadow_clip",
                                           "fs_fc_shadow_clip",
                                           _BGFXLib.resource().c_str());
            m_progShadowBlur = loadProgram("vs_fc_comp",
                                           "fs_fc_shadow_blur",
                                           _BGFXLib.resource().c_str());
            u_shadowBlur = bgfx::createUniform("u_shadowBlur",
                                               bgfx::UniformType::Vec4);
        }
        static const uint32_t blackCube[6] = {0, 0, 0, 0, 0, 0};
        m_dummyEnvTex = bgfx::createTextureCube(1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(blackCube, sizeof(blackCube)));

        u_matColor = bgfx::createUniform("u_matColor", bgfx::UniformType::Vec4);
        u_matEmissive = bgfx::createUniform("u_matEmissive", bgfx::UniformType::Vec4);
        u_matSpecular = bgfx::createUniform("u_matSpecular", bgfx::UniformType::Vec4);
        u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
        u_clipParams = bgfx::createUniform("u_clipParams", bgfx::UniformType::Vec4);
        u_clipPlanes = bgfx::createUniform("u_clipPlanes", bgfx::UniformType::Vec4,
                                           Render::Material::MaxClipPlanes);
        u_linePattern = bgfx::createUniform("u_linePattern", bgfx::UniformType::Vec4);

        // Weighted-blended OIT for the transparent bucket. Needs
        // independent per-target blending and half-float render targets
        // (the WebGL2-compatible set); with MSAA the formats must also
        // be multisample-framebuffer capable. Unavailable -> the
        // transparent view falls back to bbox-sorted alpha blending as
        // before.
        const auto *caps = bgfx::getCaps();
        const uint32_t oitFmtCaps = samples > 1
            ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA
            : BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
        m_oit = (caps->supported & BGFX_CAPS_BLEND_INDEPENDENT)
            && (caps->formats[bgfx::TextureFormat::RGBA16F] & oitFmtCaps)
            && (caps->formats[bgfx::TextureFormat::R16F] & oitFmtCaps)
            && !getenv("FC_BGFX_DEBUG_NO_OIT");
        if (m_oit) {
            // With MSAA the accum/reveal targets carry the scene's
            // sample count (all attachments of the OIT framebuffer must
            // match the shared multisampled depth). Created *without*
            // BGFX_TEXTURE_RT_WRITE_ONLY they get both a multisampled
            // renderbuffer and a single-sample resolve texture; bgfx
            // resolves automatically when the transparent view's
            // framebuffer is switched away (before the composite view),
            // so the composite pass samples the resolved images.
            const uint64_t oitFlags = 0
                | flags
                | BGFX_SAMPLER_MIN_POINT
                | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT
                | BGFX_SAMPLER_U_CLAMP
                | BGFX_SAMPLER_V_CLAMP;
            oitAccum = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, oitFlags);
            oitReveal = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::R16F, oitFlags);
            // Accumulation renders against the scene depth (test only,
            // no write), so the OIT framebuffer shares bgfxDepth.
            bgfx::Attachment att[3];
            att[0].init(oitAccum, bgfx::Access::Write, 0, 1, 0,
                        BGFX_RESOLVE_NONE);
            att[1].init(oitReveal, bgfx::Access::Write, 0, 1, 0,
                        BGFX_RESOLVE_NONE);
            att[2].init(bgfxDepth, bgfx::Access::Write, 0, 1, 0,
                        BGFX_RESOLVE_NONE);
            oitFbo = bgfx::createFrameBuffer(3, att, false);
            m_progMeshOit = loadProgram("vs_fc_mesh", "fs_fc_mesh_oit",
                                        _BGFXLib.resource().c_str());
            m_progMeshOitClip = loadProgram("vs_fc_mesh_clip",
                                            "fs_fc_mesh_oit_clip",
                                            _BGFXLib.resource().c_str());
            m_progMeshOitTex = loadProgram("vs_fc_mesh_tex",
                                           "fs_fc_mesh_oit_tex",
                                           _BGFXLib.resource().c_str());
            m_progMeshOitTexClip = loadProgram("vs_fc_mesh_tex_clip",
                                               "fs_fc_mesh_oit_tex_clip",
                                               _BGFXLib.resource().c_str());
            m_progComp = loadProgram("vs_fc_comp", "fs_fc_comp",
                                     _BGFXLib.resource().c_str());
            s_texAccum = bgfx::createUniform("s_texAccum",
                                             bgfx::UniformType::Sampler);
            s_texReveal = bgfx::createUniform("s_texReveal",
                                              bgfx::UniformType::Sampler);
        }

        // SSAO: depth+normal prepass + AO generation/blur targets, all
        // non-MSAA at viewport size (the multiply pass samples at pixel
        // centers under MSAA). Needs renderable-and-samplable RGBA16F
        // (the WebGL2 float-buffer set, like OIT) and R8.
        m_ssao = (caps->formats[bgfx::TextureFormat::RGBA16F]
                      & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
            && (caps->formats[bgfx::TextureFormat::R8]
                    & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        if (m_ssao) {
            const uint64_t aoFlags = 0
                | BGFX_TEXTURE_RT
                | BGFX_SAMPLER_MIN_POINT
                | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT
                | BGFX_SAMPLER_U_CLAMP
                | BGFX_SAMPLER_V_CLAMP;
            aoNormalZ = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, aoFlags);
            aoDepth = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::D24S8,
                aoFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
            aoTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::R8, aoFlags);
            aoBlurTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::R8, aoFlags);
            bgfx::TextureHandle preatt[2] = {aoNormalZ, aoDepth};
            aoPrepassFbo = bgfx::createFrameBuffer(2, preatt, false);
            aoGenFbo = bgfx::createFrameBuffer(1, &aoTex, false);
            aoBlurFbo = bgfx::createFrameBuffer(1, &aoBlurTex, false);

            m_progPrepass = loadProgram("vs_fc_prepass", "fs_fc_prepass",
                                        _BGFXLib.resource().c_str());
            m_progPrepassClip = loadProgram("vs_fc_prepass_clip",
                                            "fs_fc_prepass_clip",
                                            _BGFXLib.resource().c_str());
            m_progSsao = loadProgram("vs_fc_comp", "fs_fc_ssao",
                                     _BGFXLib.resource().c_str());
            m_progSsaoBlur = loadProgram("vs_fc_comp", "fs_fc_ssao_blur",
                                         _BGFXLib.resource().c_str());
            m_progSsaoApply = loadProgram("vs_fc_comp", "fs_fc_ssao_apply",
                                          _BGFXLib.resource().c_str());
            s_texNormalZ = bgfx::createUniform("s_texNormalZ",
                                               bgfx::UniformType::Sampler);
            s_texAONoise = bgfx::createUniform("s_texAONoise",
                                               bgfx::UniformType::Sampler);
            s_texAO = bgfx::createUniform("s_texAO",
                                          bgfx::UniformType::Sampler);
            u_aoParams = bgfx::createUniform("u_aoParams",
                                             bgfx::UniformType::Vec4);
            u_aoKernel = bgfx::createUniform("u_aoKernel",
                                             bgfx::UniformType::Vec4,
                                             kAOSamples);
            // 4x4 tiled random rotation vectors (xy packed *0.5+0.5),
            // fixed values so frames are deterministic.
            static const uint8_t noise[64] = {
                0xa2, 0x05, 0x00, 0xff, 0x11, 0xc0, 0x00, 0xff,
                0xee, 0xc0, 0x00, 0xff, 0x25, 0xd9, 0x00, 0xff,
                0x27, 0x23, 0x00, 0xff, 0x63, 0x03, 0x00, 0xff,
                0x20, 0xd4, 0x00, 0xff, 0x2a, 0xde, 0x00, 0xff,
                0x90, 0xfe, 0x00, 0xff, 0x9b, 0xfc, 0x00, 0xff,
                0xae, 0x09, 0x00, 0xff, 0xef, 0xbe, 0x00, 0xff,
                0xd8, 0x23, 0x00, 0xff, 0x3a, 0x15, 0x00, 0xff,
                0x00, 0x87, 0x00, 0xff, 0xe8, 0xc9, 0x00, 0xff,
            };
            aoNoiseTex = bgfx::createTexture2D(4, 4, false, 1,
                bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT,
                bgfx::copy(noise, sizeof(noise)));
        }

        // Volumetric light shafts: the half-res raymarch reads the SSAO
        // prepass depth (ray end) and the shadow moments (light
        // visibility), so it needs both resource sets. Point-sampled —
        // the apply pass does its own bilateral 4-tap upsample.
        m_vol = m_ssao && m_shadow;
        if (m_vol) {
            uint16_t hw = std::max<uint16_t>(1, width / 2);
            uint16_t hh = std::max<uint16_t>(1, height / 2);
            volTex = bgfx::createTexture2D(hw, hh, false, 1,
                bgfx::TextureFormat::RGBA16F,
                BGFX_TEXTURE_RT
                | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT
                | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            volFbo = bgfx::createFrameBuffer(1, &volTex, false);
            m_progVol = loadProgram("vs_fc_comp", "fs_fc_volume",
                                    _BGFXLib.resource().c_str());
            m_progVolApply = loadProgram("vs_fc_comp",
                                         "fs_fc_volume_apply",
                                         _BGFXLib.resource().c_str());
            s_texVol = bgfx::createUniform("s_texVol",
                                           bgfx::UniformType::Sampler);
            u_volParams = bgfx::createUniform("u_volParams",
                                              bgfx::UniformType::Vec4);
            u_volMedium = bgfx::createUniform("u_volMedium",
                                              bgfx::UniformType::Vec4);
            u_volTexel = bgfx::createUniform("u_volTexel",
                                             bgfx::UniformType::Vec4);

            // Water medium: full-res front/back depths of water body
            // draws, written by the prepass programs (only .z viewZ and
            // .w validity are consumed).
            const uint64_t waterFlags = 0
                | BGFX_TEXTURE_RT
                | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                | BGFX_SAMPLER_MIP_POINT
                | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            waterFrontTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, waterFlags);
            waterBackTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, waterFlags);
            waterFrontDepth = bgfx::createTexture2D(width, height, false,
                1, bgfx::TextureFormat::D24S8,
                waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
            waterBackDepth = bgfx::createTexture2D(width, height, false,
                1, bgfx::TextureFormat::D24S8,
                waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
            bgfx::TextureHandle fatt[2] = {waterFrontTex, waterFrontDepth};
            waterFrontFbo = bgfx::createFrameBuffer(2, fatt, false);
            bgfx::TextureHandle batt[2] = {waterBackTex, waterBackDepth};
            waterBackFbo = bgfx::createFrameBuffer(2, batt, false);
            m_progVolExt = loadProgram("vs_fc_comp", "fs_fc_volume_ext",
                                       _BGFXLib.resource().c_str());
            s_texWaterFront = bgfx::createUniform(
                "s_texWaterFront", bgfx::UniformType::Sampler);
            s_texWaterBack = bgfx::createUniform(
                "s_texWaterBack", bgfx::UniformType::Sampler);
            u_waterSigma = bgfx::createUniform("u_waterSigma",
                                               bgfx::UniformType::Vec4);
        }
    }

    // Radiance of the fixed procedural studio environment for a world
    // direction (Z up, unit length): a vertical ground/horizon/sky
    // gradient plus three broad light lobes (key/fill/rim). All values
    // are fixed — frames stay deterministic. Modest HDR range.
    static void envRadiance(const float d[3], float out[3])
    {
        // The ground stays fairly bright: metals reflect the lower
        // hemisphere over most of a model's side faces, and a dark
        // floor reads as black plastic in a CAD view.
        static const float ground[3] = {0.30f, 0.30f, 0.32f};
        static const float horizon[3] = {0.45f, 0.46f, 0.48f};
        static const float sky[3] = {0.60f, 0.66f, 0.76f};
        float z = d[2];
        float t = std::sqrt(std::fabs(z));
        for (int i = 0; i < 3; ++i)
            out[i] = z < 0.0f ? horizon[i] + (ground[i] - horizon[i]) * t
                              : horizon[i] + (sky[i] - horizon[i]) * t;

        struct Lobe {
            float dir[3];       // not normalized
            float power;
            float intensity;
            float color[3];
        };
        static const Lobe lobes[3] = {
            {{0.45f, -0.35f, 0.82f}, 40.0f, 3.0f, {1.0f, 0.98f, 0.92f}},
            {{-0.75f, -0.25f, 0.35f}, 12.0f, 1.0f, {0.75f, 0.8f, 0.9f}},
            {{0.15f, 0.85f, 0.25f}, 25.0f, 1.5f, {0.9f, 0.93f, 1.0f}},
        };
        for (const auto &lobe : lobes) {
            float len = std::sqrt(lobe.dir[0]*lobe.dir[0]
                                  + lobe.dir[1]*lobe.dir[1]
                                  + lobe.dir[2]*lobe.dir[2]);
            float dot = (d[0]*lobe.dir[0] + d[1]*lobe.dir[1]
                         + d[2]*lobe.dir[2]) / len;
            if (dot <= 0.0f)
                continue;
            float s = std::pow(dot, lobe.power) * lobe.intensity;
            for (int i = 0; i < 3; ++i)
                out[i] += lobe.color[i] * s;
        }
    }

    // World direction of a cube face texel; standard GL/D3D face order
    // and orientation (+x, -x, +y, -y, +z, -z), u/v in [-1, 1].
    static void cubeDir(int face, float u, float v, float d[3])
    {
        switch (face) {
        case 0:  d[0] =  1.0f; d[1] = -v; d[2] = -u; break;
        case 1:  d[0] = -1.0f; d[1] = -v; d[2] =  u; break;
        case 2:  d[0] =  u; d[1] =  1.0f; d[2] =  v; break;
        case 3:  d[0] =  u; d[1] = -1.0f; d[2] = -v; break;
        case 4:  d[0] =  u; d[1] = -v; d[2] =  1.0f; break;
        default: d[0] = -u; d[1] = -v; d[2] = -1.0f; break;
        }
        float len = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        d[0] /= len; d[1] /= len; d[2] /= len;
    }

    // Build the image based lighting data once per view: a
    // GGX-prefiltered RGBA16F cubemap (shader lod = roughness * 5, the
    // 1-2 px tail mips stay at full roughness) and the cosine-convolved
    // irradiance SH of the same environment in Ramamoorthi's polynomial
    // form, basis and 1/pi constants folded so the shader evaluates
    // plain dot products. CPU cost is a one-off ~2M radiance samples.
    void ensureEnvironment()
    {
        if (m_envBuilt)
            return;
        m_envBuilt = true;
        const auto *caps = bgfx::getCaps();
        if (!(caps->formats[bgfx::TextureFormat::RGBA16F]
                & BGFX_CAPS_FORMAT_TEXTURE_CUBE))
            return;
        m_envTex = bgfx::createTextureCube(kEnvSize, true, 1,
            bgfx::TextureFormat::RGBA16F, 0, nullptr);
        if (!bgfx::isValid(m_envTex))
            return;

        int numMips = 1;
        while (kEnvSize >> numMips)
            ++numMips;
        const uint16_t halfOne = bx::halfFromFloat(1.0f);
        std::vector<uint16_t> texels;
        for (int mip = 0; mip < numMips; ++mip) {
            int size = int(kEnvSize) >> mip;
            float rough = std::min(1.0f, float(mip) / 5.0f);
            float a = rough * rough;
            for (int face = 0; face < 6; ++face) {
                texels.assign(size_t(size) * size * 4, halfOne);
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        float u = 2.0f * (x + 0.5f) / size - 1.0f;
                        float v = 2.0f * (y + 0.5f) / size - 1.0f;
                        float d[3];
                        cubeDir(face, u, v, d);
                        float col[3];
                        if (mip == 0) {
                            envRadiance(d, col);
                        } else {
                            // GGX importance sampling with the usual
                            // N = V = R approximation; Hammersley set.
                            float up[3] = {0.0f, 0.0f, 1.0f};
                            if (std::fabs(d[2]) > 0.999f) {
                                up[0] = 1.0f; up[2] = 0.0f;
                            }
                            float tx[3] = {
                                up[1]*d[2] - up[2]*d[1],
                                up[2]*d[0] - up[0]*d[2],
                                up[0]*d[1] - up[1]*d[0]};
                            float tl = std::sqrt(tx[0]*tx[0]
                                + tx[1]*tx[1] + tx[2]*tx[2]);
                            tx[0] /= tl; tx[1] /= tl; tx[2] /= tl;
                            float bt[3] = {
                                d[1]*tx[2] - d[2]*tx[1],
                                d[2]*tx[0] - d[0]*tx[2],
                                d[0]*tx[1] - d[1]*tx[0]};
                            constexpr int kSamples = 64;
                            float sum[3] = {0.0f, 0.0f, 0.0f};
                            float wsum = 0.0f;
                            for (int i = 0; i < kSamples; ++i) {
                                float u1 = (i + 0.5f) / kSamples;
                                uint32_t bits = uint32_t(i);
                                bits = (bits << 16) | (bits >> 16);
                                bits = ((bits & 0x55555555u) << 1)
                                    | ((bits & 0xAAAAAAAAu) >> 1);
                                bits = ((bits & 0x33333333u) << 2)
                                    | ((bits & 0xCCCCCCCCu) >> 2);
                                bits = ((bits & 0x0F0F0F0Fu) << 4)
                                    | ((bits & 0xF0F0F0F0u) >> 4);
                                bits = ((bits & 0x00FF00FFu) << 8)
                                    | ((bits & 0xFF00FF00u) >> 8);
                                float u2 = float(bits)
                                    * 2.3283064365386963e-10f;
                                float phi = 2.0f * bx::kPi * u1;
                                float ct = std::sqrt((1.0f - u2)
                                    / (1.0f + (a*a - 1.0f) * u2));
                                float st = std::sqrt(
                                    std::max(0.0f, 1.0f - ct*ct));
                                float cp = std::cos(phi) * st;
                                float sp = std::sin(phi) * st;
                                float h[3], l[3];
                                for (int k = 0; k < 3; ++k)
                                    h[k] = tx[k]*cp + bt[k]*sp + d[k]*ct;
                                for (int k = 0; k < 3; ++k)
                                    l[k] = 2.0f*ct*h[k] - d[k];
                                float ndl = d[0]*l[0] + d[1]*l[1]
                                    + d[2]*l[2];
                                if (ndl <= 0.0f)
                                    continue;
                                float r[3];
                                envRadiance(l, r);
                                for (int k = 0; k < 3; ++k)
                                    sum[k] += r[k] * ndl;
                                wsum += ndl;
                            }
                            for (int k = 0; k < 3; ++k)
                                col[k] = sum[k]
                                    / std::max(wsum, 1.0e-4f);
                        }
                        uint16_t *t = &texels[(size_t(y)*size + x) * 4];
                        t[0] = bx::halfFromFloat(col[0]);
                        t[1] = bx::halfFromFloat(col[1]);
                        t[2] = bx::halfFromFloat(col[2]);
                    }
                }
                bgfx::updateTextureCube(m_envTex, 0, uint8_t(face),
                    uint8_t(mip), 0, 0, uint16_t(size), uint16_t(size),
                    bgfx::copy(texels.data(),
                        uint32_t(texels.size() * sizeof(uint16_t))));
            }
        }

        // Project the base environment onto the SH basis (per-texel
        // solid-angle weights), then fold the cosine convolution,
        // polynomial constants and the Lambertian 1/pi (Ramamoorthi:
        // the constant L20 terms c3/3 and -c5 cancel).
        double L[kEnvSH][3] = {};
        for (int face = 0; face < 6; ++face) {
            for (int y = 0; y < kEnvSize; ++y) {
                for (int x = 0; x < kEnvSize; ++x) {
                    float u = 2.0f * (x + 0.5f) / kEnvSize - 1.0f;
                    float v = 2.0f * (y + 0.5f) / kEnvSize - 1.0f;
                    float d[3];
                    cubeDir(face, u, v, d);
                    double r2 = 1.0 + double(u)*u + double(v)*v;
                    double w = 4.0 / (kEnvSize * double(kEnvSize)
                                      * r2 * std::sqrt(r2));
                    float col[3];
                    envRadiance(d, col);
                    double Y[kEnvSH] = {
                        0.282095,
                        0.488603 * d[1],
                        0.488603 * d[2],
                        0.488603 * d[0],
                        1.092548 * d[0] * d[1],
                        1.092548 * d[1] * d[2],
                        0.315392 * (3.0 * d[2] * d[2] - 1.0),
                        1.092548 * d[0] * d[2],
                        0.546274 * (d[0] * d[0] - d[1] * d[1]),
                    };
                    for (int k = 0; k < kEnvSH; ++k)
                        for (int c = 0; c < 3; ++c)
                            L[k][c] += col[c] * Y[k] * w;
                }
            }
        }
        constexpr double c1 = 0.429043, c2 = 0.511664;
        constexpr double c3 = 0.743125, c4 = 0.886227;
        constexpr double invPi = 0.3183098861837907;
        const double fold[kEnvSH] = {
            c4, 2.0*c2, 2.0*c2, 2.0*c2,
            2.0*c1, 2.0*c1, c3/3.0, 2.0*c1, c1,
        };
        for (int k = 0; k < kEnvSH; ++k) {
            for (int c = 0; c < 3; ++c)
                envSH[k][c] = float(L[k][c] * fold[k] * invPi);
            envSH[k][3] = 0.0f;
        }
    }

    GpuMesh *getMesh(const Render::MeshData &data)
    {
        GpuMesh &mesh = meshes[data.cacheId];
        mesh.lastUsed = frame;
        if (!bgfx::isValid(mesh.vbh))
            mesh.upload(data);
        return &mesh;
    }

    GpuTexture *getTexture(const Render::TextureImage &data)
    {
        GpuTexture &tex = textures[data.textureId];
        tex.lastUsed = frame;
        if (!bgfx::isValid(tex.handle))
            tex.upload(data);
        return &tex;
    }

    // Drop GPU buffers of caches/textures that no draw call referenced
    // recently.
    void collectMeshes()
    {
        for (auto it = meshes.begin(); it != meshes.end();) {
            if (it->second.lastUsed + 2 < frame) {
                it->second.destroy();
                it = meshes.erase(it);
            } else
                ++it;
        }
        for (auto it = textures.begin(); it != textures.end();) {
            if (it->second.lastUsed + 2 < frame) {
                it->second.destroy();
                it = textures.erase(it);
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

    /// glPolygonOffset(factor, units) approximated as a constant NDC
    /// depth bias (no per-pixel slope term): one offset unit is 2 (NDC
    /// range) * 16 LSB headroom for the unevaluated slope factor / 2^24
    /// depth bits. Positive pushes away from the viewer.
    static float polygonOffsetBias(const Render::Material &mat)
    {
        constexpr float kDepthBiasUnit = 2.0f * 16.0f / 16777216.0f;
        return mat.polygonoffset
            ? (mat.polygonoffsetfactor + mat.polygonoffsetunits)
                * kDepthBiasUnit
            : 0.0f;
    }

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
                       const OutlineSpec &spec)
    {
        if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
            return;
        // Validate the edge passes up front so a mesh that cannot draw
        // them leaves no stray stencil marks.
        GpuMesh *gpu = getMesh(*draw.mesh);
        if (!bgfx::isValid(gpu->tri))
            return;
        gpu->ensureOutline(*draw.mesh);
        if (!bgfx::isValid(gpu->triEdgeInst))
            return;
        if (!submitOutlineMark(draw, refCounter, spec.view, spec.depthTest,
                               spec.start, spec.count))
            return;
        submitOutlineEdges(draw, refCounter, spec);
    }

    void setClipUniforms(const Render::Material &mat)
    {
        if (mat.numclipplanes == 0)
            return;
        float clipParams[4] = {float(mat.numclipplanes),
                               mat.clipconcave ? 1.0f : 0.0f,
                               0.0f, 0.0f};
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, mat.clipplanes, mat.numclipplanes);
    }

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
                           bool depthTest, int start = 0, int count = 0)
    {
        if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
            return false;
        const Render::MeshData &mesh = *draw.mesh;
        if (count <= 0) {
            start = 0;
            count = mesh.numTriangleIndices;
        }
        if (count < 3)
            return false;
        GpuMesh *gpu = getMesh(mesh);
        if (!bgfx::isValid(gpu->vbh) || !bgfx::isValid(gpu->tri))
            return false;

        const Render::Material &mat = draw.material;
        bool clipped = mat.numclipplanes > 0;
        uint32_t ref = ((refCounter - 1) % 255) + 1;
        float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bgfx::setUniform(u_matColor, zero);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, gpu->vbh);
        bgfx::setIndexBuffer(gpu->tri, uint32_t(start), uint32_t(count));
        bgfx::setState(BGFX_STATE_MSAA
            | (depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0));
        bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
            | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
            | BGFX_STENCIL_OP_FAIL_S_KEEP
            | BGFX_STENCIL_OP_FAIL_Z_REPLACE
            | BGFX_STENCIL_OP_PASS_Z_REPLACE);
        bgfx::submit(viewId + view,
                     clipped ? m_progFlatClip : m_progFlat);
        ++drawcount;
        return true;
    }

    /// Edge and corner-cap passes of an outline: redraw the triangle
    /// edges as instanced thick lines (plus point-sprite caps) where the
    /// stencil does not match the reference — only the boundary
    /// survives.
    void submitOutlineEdges(const Render::DrawCall &draw,
                            uint32_t refCounter, const OutlineSpec &spec)
    {
        if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
            return;
        const Render::MeshData &mesh = *draw.mesh;
        int start = spec.start;
        int count = spec.count;
        if (count <= 0) {
            start = 0;
            count = mesh.numTriangleIndices;
        }
        if (count < 3)
            return;
        GpuMesh *gpu = getMesh(mesh);
        if (!bgfx::isValid(gpu->vbh))
            return;
        gpu->ensureOutline(mesh);
        if (!bgfx::isValid(gpu->triEdgeInst))
            return;

        const Render::Material &mat = draw.material;
        bool clipped = mat.numclipplanes > 0;
        uint32_t ref = ((refCounter - 1) % 255) + 1;
        const uint64_t depthtest =
            spec.depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0;
        const uint64_t depthstate = depthtest
            | (spec.depthWrite ? BGFX_STATE_WRITE_Z : 0);
        float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        // Shared uniforms of the edge and corner passes: flat outline
        // color, forced opaque. When the edges write depth, they get
        // twice the fill's polygon-offset bias so the owning fill
        // (biased away from the viewer) still passes LEQUAL and blends
        // over its outline like GL's ordered draw does, while fills of
        // objects genuinely behind the outline stay depth-killed.
        float color[4];
        unpackColor((spec.color & 0xffffff00) | 0xff, color);
        params[1] = qMax(1.0f, std::floor(spec.width + 0.5f));
        params[2] = spec.depthWrite
            ? 2.0f * polygonOffsetBias(draw.material) : 0.0f;
        const uint64_t outlinestate = BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | depthstate;
        const uint32_t outlinestencil = BGFX_STENCIL_TEST_NOTEQUAL
            | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
            | BGFX_STENCIL_OP_FAIL_S_KEEP
            | BGFX_STENCIL_OP_FAIL_Z_KEEP
            | BGFX_STENCIL_OP_PASS_Z_KEEP;
        LineQuadVertex::init();

        // Pass 2: the triangle edges as instanced thick lines where the
        // stencil differs — the boundary outline. Instances map 1:1 onto
        // triangle index positions, so the index range is the instance
        // range.
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, m_lineQuadVb);
        bgfx::setIndexBuffer(m_lineQuadIb);
        bgfx::setInstanceDataBuffer(gpu->triEdgeInst, uint32_t(start),
                                    uint32_t(count));
        bgfx::setState(outlinestate);
        bgfx::setStencil(outlinestencil);
        bgfx::submit(viewId + spec.view,
                     clipped ? m_progLineClip : m_progLine);
        ++drawcount;

        // Pass 3: point-sprite corner caps (GL's GL_POINT polygon-mode
        // pass), patching the notches thick quads leave at corners.
        if (!spec.caps)
            return;
        if (spec.capWidth > 0.0f)
            params[1] = qMax(1.0f, std::floor(spec.capWidth + 0.5f));
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, m_lineQuadVb);
        bgfx::setIndexBuffer(m_lineQuadIb);
        bgfx::setInstanceDataBuffer(gpu->triCornerInst, uint32_t(start),
                                    uint32_t(count));
        bgfx::setState(outlinestate);
        bgfx::setStencil(outlinestencil);
        bgfx::submit(viewId + spec.view,
                     clipped ? m_progPointClip : m_progPoint);
        ++drawcount;
    }

    // Upload the section hatch texture when the CPU-side pixels changed
    // (version 0 = no hatch image).
    void updateHatchTexture(uint64_t version, const uint8_t *rgba,
                            int width, int height)
    {
        if (version == m_hatchVersion)
            return;
        m_hatchVersion = version;
        if (bgfx::isValid(m_hatchTex)) {
            bgfx::destroy(m_hatchTex);
            m_hatchTex = BGFX_INVALID_HANDLE;
        }
        if (!rgba || width <= 0 || height <= 0)
            return;
        m_hatchTex = bgfx::createTexture2D(uint16_t(width), uint16_t(height),
            false, 1, bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(rgba, uint32_t(width) * uint32_t(height) * 4));
    }

    /// Stencil parity mark of one section-cap pass (GL: _renderSection's
    /// color-masked renderSolids loop): rasterize the solid triangle
    /// ranges clipped by the single active section plane with a
    /// depth-independent stencil INVERT — pixels looking through the
    /// cut opening end up with an odd (non-zero) parity. Assumes the
    /// stencil is zero where the draw rasterizes (the cap view is
    /// stencil-cleared and every cap pass cleans up after itself).
    bool submitCapMark(const Render::DrawCall &draw,
                       const float plane[4], uint16_t view)
    {
        if (!draw.mesh || !draw.mesh->triangleIndices)
            return false;
        const Render::MeshData &mesh = *draw.mesh;
        GpuMesh *gpu = getMesh(mesh);
        if (!bgfx::isValid(gpu->vbh) || !bgfx::isValid(gpu->tri))
            return false;

        float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float clipParams[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        const uint32_t markstencil = BGFX_STENCIL_TEST_ALWAYS
            | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff)
            | BGFX_STENCIL_OP_FAIL_S_KEEP
            | BGFX_STENCIL_OP_FAIL_Z_KEEP
            | BGFX_STENCIL_OP_PASS_Z_INVERT;

        auto submitRange = [&](int start, int count) {
            bgfx::setUniform(u_matColor, zero);
            bgfx::setUniform(u_matEmissive, zero);
            bgfx::setUniform(u_matSpecular, zero);
            bgfx::setUniform(u_params, params);
            bgfx::setUniform(u_clipParams, clipParams);
            bgfx::setUniform(u_clipPlanes, plane, 1);
            setDrawTransform(draw, autozoomScale);
            bgfx::setVertexBuffer(0, gpu->vbh);
            if (count > 0)
                bgfx::setIndexBuffer(gpu->tri, uint32_t(start),
                                     uint32_t(count));
            else
                bgfx::setIndexBuffer(gpu->tri);
            bgfx::setState(BGFX_STATE_MSAA);
            bgfx::setStencil(markstencil);
            bgfx::submit(viewId + view, m_progFlatClip);
            ++drawcount;
        };

        if (mesh.solidParts.empty())
            submitRange(0, 0);
        else
            for (const auto &part : mesh.solidParts)
                submitRange(part.first, part.second);
        return true;
    }

    /// Cap fill of one section-cap pass: a world-space quad in the
    /// section plane, drawn where the stencil parity is odd — the cross
    /// section of the marked solids — with depth LESS + write like GL's
    /// cap (LESS so the already-drawn fill keeps the shared rim pixels
    /// that GL's cap-before-fill order gives to the fill). Clipped by
    /// the remaining planes when there are any (never in concave mode,
    /// matching the GL clip state there).
    void submitCapQuad(const CapVertex verts[4], uint32_t color,
                       const float (*otherPlanes)[4], int numOther,
                       bool hatch, bool blend, uint16_t view)
    {
        if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
        auto *v = reinterpret_cast<CapVertex *>(tvb.data);
        v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
        v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

        float col[4];
        unpackColor(color, col);
        bgfx::setUniform(u_matColor, col);
        if (numOther > 0) {
            float clipParams[4] = {float(numOther), 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_clipParams, clipParams);
            bgfx::setUniform(u_clipPlanes, otherPlanes, numOther);
        }
        // GL only textures the cap when hatching is enabled; the white
        // stand-in keeps the shader uniform otherwise.
        bgfx::setTexture(0, s_texHatch,
            hatch && bgfx::isValid(m_hatchTex) ? m_hatchTex : m_whiteTex);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_MSAA
            | (blend ? BGFX_STATE_BLEND_ALPHA : 0));
        bgfx::setStencil(BGFX_STENCIL_TEST_EQUAL
            | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0x01)
            | BGFX_STENCIL_OP_FAIL_S_KEEP
            | BGFX_STENCIL_OP_FAIL_Z_KEEP
            | BGFX_STENCIL_OP_PASS_Z_KEEP);
        bgfx::submit(viewId + view,
                     numOther > 0 ? m_progCapClip : m_progCap);
        ++drawcount;
    }

    /// Stencil cleanup of one section-cap pass (the stand-in for GL's
    /// per-pass glClear(GL_STENCIL_BUFFER_BIT)): zero the stencil over
    /// the cap quad, which covers every pixel the parity mark can have
    /// touched (the cut cross section lies inside the plane/circumsphere
    /// intersection), unclipped so marks outside the other planes are
    /// cleaned too. No color or depth output.
    void submitCapCleanup(const CapVertex verts[4], uint16_t view)
    {
        if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
        auto *v = reinterpret_cast<CapVertex *>(tvb.data);
        v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
        v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

        float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_matColor, zero);
        bgfx::setTexture(0, s_texHatch, m_whiteTex);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_MSAA);
        bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
            | BGFX_STENCIL_FUNC_REF(0) | BGFX_STENCIL_FUNC_RMASK(0xff)
            | BGFX_STENCIL_OP_FAIL_S_KEEP
            | BGFX_STENCIL_OP_FAIL_Z_REPLACE
            | BGFX_STENCIL_OP_PASS_Z_REPLACE);
        bgfx::submit(viewId + view, m_progCap);
        ++drawcount;
    }

    // Fullscreen WBOIT resolve: average the accumulated premultiplied
    // color and blend it onto the scene by coverage (1 - revealage in
    // the source alpha, blend INV_SRC_ALPHA / SRC_ALPHA).
    void submitComposite()
    {
        SceneVertex::init();
        if (bgfx::getAvailTransientVertexBuffer(3, SceneVertex::ms_layout)
                < 3)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 3, SceneVertex::ms_layout);
        auto *v = reinterpret_cast<SceneVertex *>(tvb.data);
        // Clip-space triangle covering the viewport.
        v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        bgfx::setTexture(0, s_texAccum, oitAccum);
        bgfx::setTexture(1, s_texReveal, oitReveal);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                    BGFX_STATE_BLEND_SRC_ALPHA));
        bgfx::submit(viewId + ViewOITComposite, m_progComp);
        ++drawcount;
    }

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
                            bool prepass = false)
    {
        // Fully transparent = invisible ground (Coin then switches to a
        // shadow-only ground rendering, not ported).
        if (light.groundTransparency >= 1.0f)
            return;
        uint32_t colorPacked = light.groundColor;
        float cx = (bmin[0] + bmax[0]) * 0.5f;
        float cy = (bmin[1] + bmax[1]) * 0.5f;
        float z = bmin[2];
        // Coin parity (updateShadowGround): the ground half-extent is
        // GroundSizeScale times the largest scene dimension (z included).
        float half = light.groundScale
            * std::max(bmax[0] - bmin[0],
                       std::max(bmax[1] - bmin[1], bmax[2] - bmin[2]));
        if (half <= 0.0f)
            return;

        SceneVertex::init();
        if (bgfx::getAvailTransientVertexBuffer(6, SceneVertex::ms_layout)
                < 6)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 6, SceneVertex::ms_layout);
        auto verts = reinterpret_cast<SceneVertex *>(tvb.data);
        const float xs[6] = {-1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
        const float ys[6] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f};
        for (int i = 0; i < 6; ++i) {
            verts[i].px = cx + xs[i] * half;
            verts[i].py = cy + ys[i] * half;
            verts[i].pz = z;
            verts[i].nx = verts[i].ny = 0.0f;
            verts[i].nz = 1.0f;
            verts[i].rgba = 0xffffffffu;
        }

        float color[4], zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        unpackColor(colorPacked, color);
        // Ground transparency (ShadowGroundTransparency): plain alpha
        // blend over whatever lies behind in the depth order (the
        // background; the quad still writes depth like Coin's ground).
        color[3] = 1.0f - light.groundTransparency;
        float params[4] = {0.0f, 1.0f, 1.0f, 0.0f};  // lit, two-sided
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        float pbrOff[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_pbrParams, pbrOff);
        bgfx::setTexture(1, s_texEnv, m_dummyEnvTex);
        static const bool dbgvis =
            getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
        float shadowParams[4] = {1.0f, shadowEpsilon, 0.003f,
                                 dbgvis ? 1.0f : 0.0f};
        float lightDir[4] = {lightDirView[0], lightDirView[1],
                             lightDirView[2], 1.0f};
        bgfx::setUniform(u_shadowParams, shadowParams);
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                         shadowSpreadUv, shadowSpreadMode};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::setUniform(u_lightDir, lightDir);
        bgfx::setUniform(u_lightPos, lightPosView);
        bgfx::setUniform(u_lightColor, lightColorI);
        bgfx::setUniform(u_shadowMatrix, shadowMtx);
        bgfx::setTexture(3, s_texShadow, shadowTex);

        // Ground texture (ShadowGroundTexture): tiled every
        // groundTextureSize world units, modulated by the ground color
        // like Coin's default SoTexture2 model on the ground material.
        // A ground bump map (ShadowGroundBumpMap) rides the same
        // textured program with the same tiled UVs (white color
        // stand-in when there is no ground texture, the scene's
        // lone-bump-map pattern).
        bool textured = (light.groundTexture || light.groundBumpMap)
            && bgfx::isValid(m_progMeshTex);
        bgfx::TransientVertexBuffer uvb;
        if (textured) {
            TexCoordVertex::init();
            if (bgfx::getAvailTransientVertexBuffer(
                        6, TexCoordVertex::ms_layout) < 6) {
                textured = false;
            }
            else {
                bgfx::allocTransientVertexBuffer(
                    &uvb, 6, TexCoordVertex::ms_layout);
                auto *uv = reinterpret_cast<TexCoordVertex *>(uvb.data);
                float span = light.groundTextureSize > 1.0e-5f
                    ? 2.0f * half / light.groundTextureSize : 1.0f;
                for (int i = 0; i < 6; ++i) {
                    uv[i].u = (xs[i] + 1.0f) * 0.5f * span;
                    uv[i].v = (ys[i] + 1.0f) * 0.5f * span;
                }
            }
        }
        if (textured) {
            float texParams[4] = {
                float(Render::TextureImage::Modulate),
                0.0f, 0.0f, 0.0f};
            bgfx::TextureHandle colorTex = m_whiteTex;
            if (light.groundTexture) {
                colorTex = getTexture(*light.groundTexture)->handle;
                texParams[1] =
                    light.groundTexture->numComponents == 4 ? 1.0f
                                                            : 0.0f;
            }
            float texmat[16];
            bx::mtxIdentity(texmat);
            bgfx::setTexture(0, s_texColor, colorTex);
            bgfx::setUniform(u_texParams, texParams);
            bgfx::setUniform(u_texBlendColor, zero);
            bgfx::setUniform(u_texMatrix, texmat);
            float bumpParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::TextureHandle bump = m_whiteTex;
            if (light.groundBumpMap) {
                const auto &bm = *light.groundBumpMap;
                bool heightmap = bm.numComponents <= 2;
                bumpParams[0] = heightmap
                    ? (bumpParallax ? 3.0f : 2.0f) : 1.0f;
                bumpParams[1] = heightmap
                    ? 0.04f * bumpScale : bumpScale;
                bumpParams[2] = 1.0f / float(bm.width);
                bumpParams[3] = 1.0f / float(bm.height);
                bump = getTexture(bm)->handle;
            }
            bgfx::setUniform(u_bumpParams, bumpParams);
            bgfx::setTexture(2, s_texBump, bump);
            bgfx::setTexture(4, s_texEmissive, m_whiteTex);
            bgfx::setTexture(5, s_texOcclusion, m_whiteTex);
        }

        float identity[16];
        bx::mtxIdentity(identity);
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, &tvb);
        if (textured)
            bgfx::setVertexBuffer(1, &uvb);
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_MSAA;
        if (light.groundTransparency > 0.0f)
            state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::submit(viewId + ViewOpaque,
                     textured ? m_progMeshTex : m_progMesh);
        ++drawcount;

        // The volumetric raymarch ends rays at the prepass depth, so the
        // ground must be a prepass source too or shafts would continue
        // through it (SSAO alone keeps the ground out of the prepass —
        // it neither receives nor casts AO, preserved behavior).
        if (prepass && bgfx::isValid(m_progPrepass)) {
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                           | BGFX_STATE_WRITE_Z
                           | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(viewId + ViewAOPrepass, m_progPrepass);
            ++drawcount;
        }
    }

    // Rasterize a shadow casting triangle draw into the variance shadow
    // map under the light camera (the ViewShadow transform). Both faces
    // cast; section-clipped parts do not (clip shader variant).
    void submitShadowCaster(const Render::DrawCall &draw)
    {
        if (!draw.mesh || !draw.mesh->triangleIndices)
            return;
        GpuMesh *gpu = getMesh(*draw.mesh);
        if (!bgfx::isValid(gpu->vbh) || !bgfx::isValid(gpu->tri))
            return;

        const Render::Material &mat = draw.material;
        bool clipped = mat.numclipplanes > 0;
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, gpu->vbh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(gpu->tri, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(gpu->tri);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS);
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                         0.0f, 0.0f};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::submit(viewId + ViewShadow,
                     clipped ? m_progShadowClip : m_progShadow);
        ++drawcount;
    }

    /// Separable gaussian blur of the shadow moments (the Shadow draw
    /// style's SmoothBorder, 0..100): horizontal into the ping texture,
    /// vertical back into shadowTex, so the mesh receivers and the
    /// volumetric raymarch keep sampling the same target. The blur views
    /// sit right after the caster pass. Softens the VSM penumbra and
    /// curbs shimmer on razor-straight CAD edges.
    /// (Re)create the shadow map targets for the requested size
    /// (ShadowPrecision); the stored moments are lost, so the cached
    /// map re-renders.
    void ensureShadowTargets(uint16_t size)
    {
        if (!m_shadow || (size == shadowSize && bgfx::isValid(shadowFbo)))
            return;
        for (auto fb : {&shadowFbo, &shadowBlurFbo, &shadowBlurBackFbo}) {
            if (bgfx::isValid(*fb)) {
                bgfx::destroy(*fb);
                *fb = BGFX_INVALID_HANDLE;
            }
        }
        for (auto tex : {&shadowTex, &shadowDepth, &shadowBlurTex}) {
            if (bgfx::isValid(*tex)) {
                bgfx::destroy(*tex);
                *tex = BGFX_INVALID_HANDLE;
            }
        }
        shadowSize = size;
        shadowMapHash = 0;
        shadowTex = bgfx::createTexture2D(size, size,
            false, 1, shadowFormat,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP);
        shadowDepth = bgfx::createTexture2D(size, size,
            false, 1, bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle att[2] = {shadowTex, shadowDepth};
        shadowFbo = bgfx::createFrameBuffer(2, att, false);
        // ShadowSmoothBorder: separable gaussian blur of the moments,
        // horizontal into the ping texture and vertical back into
        // shadowTex (a second color-only framebuffer over the same
        // texture; no depth needed for fullscreen passes).
        shadowBlurTex = bgfx::createTexture2D(size, size,
            false, 1, shadowFormat,
            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP);
        shadowBlurFbo = bgfx::createFrameBuffer(1, &shadowBlurTex,
                                                false);
        shadowBlurBackFbo = bgfx::createFrameBuffer(1, &shadowTex,
                                                    false);
    }

    void submitShadowBlur(float smoothBorder)
    {
        if (!bgfx::isValid(m_progShadowBlur)
                || !bgfx::isValid(shadowBlurFbo)
                || !bgfx::isValid(shadowBlurBackFbo))
            return;
        // 100 -> 4 texel base step of the 9-tap kernel (an effective
        // radius of ~13 texels at full smoothing).
        float step = smoothBorder * 0.04f;
        float dirH[4] = {step, 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_shadowBlur, dirH);
        bgfx::setTexture(0, s_texShadow, shadowTex);
        fullscreen(ViewShadowBlurH, m_progShadowBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        float dirV[4] = {0.0f, step, 0.0f, 0.0f};
        bgfx::setUniform(u_shadowBlur, dirV);
        bgfx::setTexture(0, s_texShadow, shadowBlurTex);
        fullscreen(ViewShadowBlurV, m_progShadowBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    }

    void submitPrepass(const Render::DrawCall &draw)
    {
        if (!draw.mesh || !draw.mesh->triangleIndices)
            return;
        GpuMesh *gpu = getMesh(*draw.mesh);
        if (!bgfx::isValid(gpu->vbh) || !bgfx::isValid(gpu->tri))
            return;

        const Render::Material &mat = draw.material;
        bool clipped = mat.numclipplanes > 0;
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, gpu->vbh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(gpu->tri, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(gpu->tri);
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        if (mat.culling && !mat.twoside)
            state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
        bgfx::setState(state);
        bgfx::submit(viewId + ViewAOPrepass,
                     clipped ? m_progPrepassClip : m_progPrepass);
        ++drawcount;
    }

    /// Rasterize a water body draw into one of the water depth targets
    /// (the prepass programs write the linear view depth in .z): front
    /// faces with the nearest depth = medium entry, back faces with the
    /// farthest = medium exit. Culling is forced by face side whatever
    /// the material's two-sidedness.
    void submitWaterDepth(const Render::DrawCall &draw, bool back)
    {
        if (!draw.mesh || !draw.mesh->triangleIndices)
            return;
        GpuMesh *gpu = getMesh(*draw.mesh);
        if (!bgfx::isValid(gpu->vbh) || !bgfx::isValid(gpu->tri))
            return;

        const Render::Material &mat = draw.material;
        bool clipped = mat.numclipplanes > 0;
        setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale);
        bgfx::setVertexBuffer(0, gpu->vbh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(gpu->tri, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(gpu->tri);
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z;
        if (back) {
            state |= BGFX_STATE_DEPTH_TEST_GREATER
                | (mat.ccw ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW);
        } else {
            state |= BGFX_STATE_DEPTH_TEST_LESS
                | (mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW);
        }
        bgfx::setState(state);
        bgfx::submit(viewId + (back ? ViewWaterBack : ViewWaterFront),
                     clipped ? m_progPrepassClip : m_progPrepass);
        ++drawcount;
    }

    /// One clip-space triangle covering the viewport, submitted to a
    /// fullscreen resolve pass (uniforms/textures are set by the caller).
    void fullscreen(uint16_t pass, bgfx::ProgramHandle prog,
                    uint64_t state)
    {
        SceneVertex::init();
        if (bgfx::getAvailTransientVertexBuffer(
                    3, SceneVertex::ms_layout) < 3)
            return;
        bgfx::TransientVertexBuffer tvb;
        bgfx::allocTransientVertexBuffer(&tvb, 3,
                                         SceneVertex::ms_layout);
        auto *v = reinterpret_cast<SceneVertex *>(tvb.data);
        v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(state);
        bgfx::submit(viewId + pass, prog);
        ++drawcount;
    }

    /// Fullscreen SSAO resolve chain: hemisphere-kernel AO from the
    /// prepass into the R8 target, a 4x4 box blur, then the multiply
    /// onto the opaque scene color (dst *= src, alpha kept).
    void submitAOResolve(float radius, float intensity)
    {
        // Fixed hemisphere kernel (unit radius, z >= 0, clustered near
        // the origin), deterministic across frames like the noise.
        static const float kernel[kAOSamples][4] = {
            {-0.058091f, 0.018602f, 0.079242f, 0.0f},
            {-0.016977f, 0.100367f, 0.018809f, 0.0f},
            {-0.042287f, 0.079676f, 0.069813f, 0.0f},
            {0.010341f, 0.119322f, 0.054631f, 0.0f},
            {0.012528f, 0.147272f, 0.050676f, 0.0f},
            {-0.131686f, -0.100976f, 0.088122f, 0.0f},
            {0.120937f, 0.161185f, 0.103557f, 0.0f},
            {0.024414f, -0.112444f, 0.246757f, 0.0f},
            {-0.050206f, -0.180815f, 0.265349f, 0.0f},
            {0.057177f, 0.368457f, 0.094947f, 0.0f},
            {0.223564f, 0.320370f, 0.226476f, 0.0f},
            {0.173264f, -0.484121f, 0.107897f, 0.0f},
            {0.046909f, 0.076361f, 0.599589f, 0.0f},
            {0.263978f, 0.433148f, 0.473845f, 0.0f},
            {-0.768430f, 0.171215f, 0.053107f, 0.0f},
            {0.429038f, 0.201413f, 0.754499f, 0.0f},
        };
        float params[4] = {radius, intensity, 0.02f * radius, 0.0f};
        bgfx::setUniform(u_aoParams, params);
        bgfx::setUniform(u_aoKernel, kernel, kAOSamples);
        bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
        bgfx::setTexture(1, s_texAONoise, aoNoiseTex);
        fullscreen(ViewAOGen, m_progSsao,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

        bgfx::setTexture(0, s_texAO, aoTex);
        fullscreen(ViewAOBlur, m_progSsaoBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

        bgfx::setTexture(0, s_texAO, aoBlurTex);
        fullscreen(ViewAOApply, m_progSsaoApply,
                   BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_BLEND_FUNC_SEPARATE(
                       BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR,
                       BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE));
    }

    /// Volumetric light shaft resolve: raymarch the shadow map through
    /// the media at half resolution (ray ends at the prepass depth; a
    /// water body interval carries its own per-channel extinction and
    /// scattering), then composite onto the opaque scene in the
    /// sequential apply view — first the analytic per-channel
    /// transmittance multiply, then the bilateral-upsampled inscatter
    /// add: dst = inscatter + transmittance * scene.
    void submitVolumetric(float density, float intensity, float maxDist,
                          const float medium[4], bool water,
                          const float waterSigma[4])
    {
        static const float noSigma[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float params[4] = {density, intensity, maxDist,
                           water ? 1.0f : 0.0f};
        bgfx::setUniform(u_volParams, params);
        bgfx::setUniform(u_volMedium, medium);
        bgfx::setUniform(u_waterSigma, water ? waterSigma : noSigma);
        bgfx::setUniform(u_lightColor, lightColorI);
        float lightDir[4] = {lightDirView[0], lightDirView[1],
                             lightDirView[2], 1.0f};
        bgfx::setUniform(u_lightDir, lightDir);
        bgfx::setUniform(u_lightPos, lightPosView);
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                         0.0f, 0.0f};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::setUniform(u_shadowMatrix, shadowMtx);
        bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
        bgfx::setTexture(1, s_texShadow, shadowTex);
        bgfx::setTexture(2, s_texWaterFront, waterFrontTex);
        bgfx::setTexture(3, s_texWaterBack, waterBackTex);
        fullscreen(ViewVolGen, m_progVol,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

        // Analytic per-channel surface extinction (multiply; the
        // sequential apply view keeps it before the inscatter add).
        bgfx::setUniform(u_volParams, params);
        bgfx::setUniform(u_volMedium, medium);
        bgfx::setUniform(u_waterSigma, water ? waterSigma : noSigma);
        bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
        bgfx::setTexture(1, s_texWaterFront, waterFrontTex);
        bgfx::setTexture(2, s_texWaterBack, waterBackTex);
        fullscreen(ViewVolApply, m_progVolExt,
                   BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_BLEND_FUNC_SEPARATE(
                       BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR,
                       BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE));

        float hw = std::max(1.0f, std::floor(width / 2.0f));
        float hh = std::max(1.0f, std::floor(height / 2.0f));
        float texel[4] = {1.0f / hw, 1.0f / hh, hw, hh};
        bgfx::setUniform(u_volParams, params);
        bgfx::setUniform(u_volMedium, medium);
        bgfx::setUniform(u_volTexel, texel);
        bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
        bgfx::setTexture(1, s_texVol, volTex);
        fullscreen(ViewVolApply, m_progVolApply,
                   BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                           BGFX_STATE_BLEND_ONE));
    }

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
    void setTriangleFrameState(const Render::Material &mat, int pass,
                               bool mapped)
    {
        float pbrParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::TextureHandle env = m_dummyEnvTex;
        if (pbrFrame && mat.lighting && pass != PassDepthOnly) {
            // x = 2 flags a metallic-roughness map on top of the
            // branch (u_texParams has no free component; the map
            // only matters to the PBR path anyway).
            pbrParams[0] = mapped && mat.metallicroughnessmap
                ? 2.0f : 1.0f;
            // Per-object overrides (SoFCRenderMaterial, from
            // ViewProvider Render_* properties) beat the frame config.
            float metal = mat.metallic >= 0.0f ? mat.metallic
                                               : pbrMetallic;
            pbrParams[1] = bx::clamp(metal, 0.0f, 1.0f);
            float rough = mat.roughness >= 0.0f ? mat.roughness
                                                : pbrRoughness;
            if (rough <= 0.0f) {
                // Derive from the material shininess (Coin's 0..1
                // convention maps to a GL exponent of s * 128) with
                // the usual Blinn-Phong-to-GGX conversion.
                float exponent =
                    std::max(mat.shininess, 0.0f) * 128.0f;
                rough = std::sqrt(2.0f / (exponent + 2.0f));
            }
            pbrParams[2] = bx::clamp(rough, 0.02f, 1.0f);
            pbrParams[3] = std::max(pbrEnvIntensity, 0.0f);
            env = m_envTex;
            bgfx::setUniform(u_envSH, envSH, kEnvSH);
        }
        bgfx::setUniform(u_pbrParams, pbrParams);
        bgfx::setTexture(1, s_texEnv, env);

        // Shadow draw style: the scene light replaces the headlight
        // for every lit draw of the frame; the VSM lookup runs only
        // on receivers (the white stand-in reads as fully lit).
        float shadowParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float lightDir[4] = {0.0f, 0.0f, -1.0f, 0.0f};
        bgfx::TextureHandle shadow = m_whiteTex;
        if (shadowFrame) {
            lightDir[0] = lightDirView[0];
            lightDir[1] = lightDirView[1];
            lightDir[2] = lightDirView[2];
            lightDir[3] = 1.0f;
            bgfx::setUniform(u_lightColor, lightColorI);
            bgfx::setUniform(u_shadowMatrix, shadowMtx);
            if ((mat.shadowstyle & 2) && pass != PassDepthOnly) {
                shadowParams[0] = 1.0f;
                // Coin's epsilon (plain VSM adds it to the
                // variance; EVSM scales it by the warped moment)
                shadowParams[1] = shadowEpsilon;
                shadowParams[2] = 0.003f;   // depth bias
                static const bool dbgvis =
                    getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
                if (dbgvis)
                    shadowParams[3] = 1.0f;
                shadow = shadowTex;
            }
        }
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     shadowSpreadUv, shadowSpreadMode};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::setUniform(u_lightDir, lightDir);
        static const float noSpot[4] = {0.0f, 0.0f, 0.0f, -1.0f};
        bgfx::setUniform(u_lightPos,
                         shadowFrame ? lightPosView : noSpot);
        bgfx::setUniform(u_shadowParams, shadowParams);
        bgfx::setTexture(3, s_texShadow, shadow);
    }

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
    /// (instancableDraw / buildInstanceGroups): an opaque, untextured,
    /// unclipped, non-on-top triangle draw of the normal scene pass.
    /// `data` holds count * InstanceStride bytes. Returns false when the
    /// transient instance buffer cannot fit the group this frame; the
    /// caller falls back to per-draw submits.
    bool submitInstanced(const Render::DrawCall &draw, const float *data,
                         uint32_t count)
    {
        const Render::Material &mat = draw.material;
        GpuMesh *mesh = getMesh(*draw.mesh);
        if (!bgfx::isValid(mesh->vbh) || !bgfx::isValid(mesh->tri))
            return false;
        if (bgfx::getAvailInstanceDataBuffer(count, InstanceStride)
                < count)
            return false;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, count, InstanceStride);
        std::memcpy(idb.data, data, size_t(count) * InstanceStride);

        float color[4], emissive[4], specular[4], params[4];
        unpackColor(mat.diffuse, color);
        unpackColor(mat.emissive, emissive);
        unpackColor(mat.specular, specular);
        specular[3] = mat.shininess;
        // The fragment stage always reads v_color0 in the instanced
        // path — the vertex stage selects the per-vertex stream or the
        // per-instance color by u_instParams.x.
        params[0] = 1.0f;
        bool shaded = mat.lighting
            || (shadowFrame && (mat.shadowstyle & 2));
        params[1] = shaded ? 1.0f : 0.0f;
        bool twoside = mat.twoside;     // never transparent nor on-top
        params[2] = twoside ? 1.0f : 0.0f;
        params[3] = polygonOffsetBias(mat);
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, emissive);
        bgfx::setUniform(u_matSpecular, specular);
        bgfx::setUniform(u_params, params);
        float instParams[4] = {mat.pervertexcolor ? 1.0f : 0.0f,
                               0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_instParams, instParams);
        setTriangleFrameState(mat, PassNormal, false);

        uint64_t state = BGFX_STATE_MSAA
            | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (mat.depthtest)
            state |= depthFuncState(mat.depthfunc);
        if (mat.depthwrite)
            state |= BGFX_STATE_WRITE_Z;
        if (mat.culling && !twoside)
            state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;

        bgfx::setVertexBuffer(0, mesh->vbh);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(mesh->tri, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(mesh->tri);
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::setState(state);

        static const bool dbgsubmit =
            (getenv("FC_BGFX_DEBUG_SUBMIT") != nullptr);
        if (dbgsubmit)
            fprintf(stderr,
                    "bgfx submit instanced cache=%llx n=%u range=%d+%d"
                    " state=%llx pvc=%d\n",
                    (unsigned long long)draw.mesh->cacheId, count,
                    draw.indexStart, draw.indexCount,
                    (unsigned long long)state, mat.pervertexcolor);

        bgfx::submit(viewId + ViewOpaque, m_progMeshInst);
        return true;
    }

    void submit(const Render::DrawCall &draw, const float *viewMatrix,
                int pass = PassNormal, bool noseam = false)
    {
        const Render::Material &mat = draw.material;
        if (!draw.mesh || draw.mesh->numVertices == 0)
            return;

        GpuMesh *mesh = getMesh(*draw.mesh);
        // Hidden-line hideSeam: whole-cache line draws switch to the
        // seam-filtered index set (GL: renderLines' noseam argument).
        if (noseam && mat.type == Render::Material::Line)
            mesh->ensureNoSeam(*draw.mesh);
        if (noseam && mat.type == Render::Material::Line
                && getenv("FC_BGFX_DEBUG_SUBMIT"))
            fprintf(stderr, "bgfx noseam cache=%llx num=%d valid=%d\n",
                    (unsigned long long)draw.mesh->cacheId,
                    draw.mesh->numNoSeamLineIndices,
                    bgfx::isValid(mesh->lineNoSeam));
        noseam = noseam && mat.type == Render::Material::Line
            && bgfx::isValid(mesh->lineNoSeam);
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        switch (mat.type) {
        case Render::Material::Triangle: ibh = mesh->tri; break;
        case Render::Material::Line:
            ibh = noseam ? mesh->lineNoSeam : mesh->line;
            break;
        case Render::Material::Point: ibh = mesh->point; break;
        }
        if (!bgfx::isValid(ibh))
            return;

        // Textured triangle fill (unit-0 SoTexture2 fed by the bridge):
        // sampled only when the mesh carries texture coordinates; the
        // depth prepass stays untextured like GL's depthwriteonly path.
        // A bump map routes the draw through the textured programs too
        // (they carry the texcoord stream and the bump sampler); a 1x1
        // white texture stands in at unit 0 when there is no color
        // texture (modulate by white = unchanged).
        bool bumped = mat.type == Render::Material::Triangle
            && mat.bumpmap && mat.lighting && draw.mesh->texCoords
            && pass != PassDepthOnly;
        // Emissive/occlusion/metallic-roughness material maps ride the
        // textured programs too, with the same white unit-0 stand-in as
        // a lone bump map.
        bool mapped = mat.type == Render::Material::Triangle
            && (mat.emissivemap || mat.occlusionmap
                || mat.metallicroughnessmap)
            && draw.mesh->texCoords && pass != PassDepthOnly;
        bool textured = (mat.type == Render::Material::Triangle
            && mat.texture && draw.mesh->texCoords
            && pass != PassDepthOnly) || bumped || mapped;
        if (textured) {
            mesh->ensureTexCoord(*draw.mesh);
            textured = bgfx::isValid(mesh->texcoord);
            bumped = bumped && textured;
            mapped = mapped && textured;
        }

        // Line stipple: the hidden (dimmed) pass of on-top lines uses the
        // pattern resolved by the bridge (material's own or the selection
        // fallback, GL's RenderPassLinePattern); every other pass uses the
        // material's own pattern.
        uint32_t linepattern = pass == PassLineHidden
            ? mat.hiddenlinepattern : mat.linepattern;
        bool patterned = mat.type == Render::Material::Line
            && (linepattern & 0xffff) != 0xffff;

        // Lines wider than 1px — and patterned lines of any width, the
        // stipple lives in the quad fragment shader — render as instanced
        // screen-space quads (vs_fc_line*); plain line primitives have no
        // width or pattern in modern APIs. Without instancing support
        // patterned lines fall back to solid 1px primitives.
        bool thickline = mat.type == Render::Material::Line
            && (mat.linewidth > 1.001f || patterned)
            && m_instancing
            && bgfx::isValid(noseam ? mesh->lineNoSeamInst
                                    : mesh->lineInst);
        patterned = patterned && thickline;

        // Points larger than 1px render as instanced screen-space quads
        // as well: BGFX_STATE_POINT_SIZE only exists on the OpenGL
        // backend, the quad path is the portable one.
        bool thickpoint = mat.type == Render::Material::Point
            && mat.pointsize > 1.001f
            && m_instancing && bgfx::isValid(mesh->pointInst);

        bool transparent = mat.transparent
            || (mat.pervertexcolor && draw.mesh->hasTransparency);

        // GL parity (applyMaterial ~745): transparent and on-top draws
        // are lit on both faces, transparent draws are never culled —
        // back faces are visible layers of a transparent solid.
        bool twoside = mat.twoside || transparent || mat.ontop;
        bool culling = mat.culling && !transparent;

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
        // GL quirk (renderHighlight ~2187): a selected face drawn with
        // its outline keeps the depth test at LEQUAL so the outline
        // remains readable where the fill is hidden.
        if (mat.faceoutline && !mat.outlineonly && draw.partIndex >= 0
                && mat.type == Render::Material::Triangle
                && pass == PassNormal) {
            depthtest = true;
            depthfunc = Render::Material::LEqual;
        }
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

        // Weighted-blended OIT accumulation: RT0 sums the depth-weighted
        // premultiplied color, RT1 multiplies up the revealage. Draw
        // order becomes irrelevant (commutative blending).
        bool oitDraw = oitFrame && passView == ViewTransparent;

        uint64_t state = BGFX_STATE_MSAA;
        uint32_t blendRt = 0;
        if (pass != PassDepthOnly)
            state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (depthtest)
            state |= depthFuncState(depthfunc);
        if (depthwrite)
            state |= BGFX_STATE_WRITE_Z;
        if (oitDraw) {
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                           BGFX_STATE_BLEND_ONE)
                | BGFX_STATE_BLEND_INDEPENDENT;
            blendRt = uint32_t(
                BGFX_STATE_BLEND_FUNC_RT_1(BGFX_STATE_BLEND_ZERO,
                                           BGFX_STATE_BLEND_INV_SRC_COLOR));
        }
        else if (blend)
            state |= BGFX_STATE_BLEND_ALPHA;
        if (culling && !twoside
                        && mat.type == Render::Material::Triangle)
            state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
        if (mat.type == Render::Material::Line) {
            if (!thickline)
                state |= BGFX_STATE_PT_LINES;
        }
        else if (mat.type == Render::Material::Point) {
            if (!thickpoint)
                state |= BGFX_STATE_PT_POINTS
                    | BGFX_STATE_POINT_SIZE(
                        uint32_t(qMax(mat.pointsize, 1.0f)));
        }

        float color[4], emissive[4], specular[4], params[4];
        unpackColor(mat.diffuse, color);
        unpackColor(mat.emissive, emissive);
        unpackColor(mat.specular, specular);
        specular[3] = mat.shininess;
        params[0] = mat.pervertexcolor ? 1.0f : 0.0f;
        // u_params.y: mesh program = lighting flag; line program = line
        // width in pixels; point program = point size in pixels (unused
        // by the flat program). Widths/sizes round to the nearest integer
        // like GL's non-antialiased line/point rasterization does (a
        // 1.5px quad would otherwise cover its second pixel row only
        // partially and drop it without MSAA).
        // While the scene light is on, unlit receivers (the Shadow draw
        // style's BASE_COLOR ground) light up too — Coin's SoShadowGroup
        // shades and shadows the ground with its own shaders regardless
        // of the light model.
        bool shaded = mat.lighting
            || (shadowFrame && (mat.shadowstyle & 2));
        params[1] = mat.type == Render::Material::Line
            ? qMax(1.0f, std::floor(mat.linewidth + 0.5f))
            : mat.type == Render::Material::Point
                ? qMax(1.0f, std::floor(mat.pointsize + 0.5f))
                : shaded ? 1.0f : 0.0f;
        // u_params.z: mesh program = two-sided lighting; line/point
        // programs = NDC depth bias (only the outline passes bias).
        params[2] = mat.type == Render::Material::Triangle && twoside
            ? 1.0f : 0.0f;
        // u_params.w: mesh program = NDC depth bias (polygon offset
        // approximation, no per-pixel slope term); flat program = alpha
        // ceiling used to dim depth-occluded on-top lines.
        if (mat.type == Render::Material::Triangle) {
            params[3] = polygonOffsetBias(mat);
        } else {
            params[3] = dimalpha;
        }
        bgfx::setUniform(u_matColor, color);
        bgfx::setUniform(u_matEmissive, emissive);
        bgfx::setUniform(u_matSpecular, specular);
        bgfx::setUniform(u_params, params);

        // PBR branch of the mesh programs: every one of them carries the
        // environment sampler (the branch is uniform-selected), so bind
        // the dummy cube whenever the branch is off for this draw.
        if (mat.type == Render::Material::Triangle)
            setTriangleFrameState(mat, pass, mapped);

        // Clipped draws use the discard shader variants; the unclipped
        // programs contain no discard so the rest of the scene keeps
        // early-Z. The depth prepass clips too (unlike the stateful GL
        // path, which leaves whatever planes happen to be enabled).
        bool clipped = mat.numclipplanes > 0;
        if (clipped) {
            float clipParams[4] = {float(mat.numclipplanes),
                                   mat.clipconcave ? 1.0f : 0.0f,
                                   0.0f, 0.0f};
            bgfx::setUniform(u_clipParams, clipParams);
            bgfx::setUniform(u_clipPlanes, mat.clipplanes,
                             mat.numclipplanes);
        }

        if (textured) {
            // u_texParams: x = texture environment (TextureImage::Model),
            // y = the source format carries alpha (GL's REPLACE keeps the
            // fragment alpha for alpha-less formats; the RGBA8 expansion
            // hides that distinction from the sampler), z = emissive map
            // present, w = occlusion map present. The bump-only
            // stand-in modulates by opaque white.
            float texParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float blend[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::TextureHandle color = m_whiteTex;
            if (mat.texture) {
                color = getTexture(*mat.texture)->handle;
                texParams[0] = float(mat.texture->model);
                texParams[1] = mat.texture->numComponents == 2
                        || mat.texture->numComponents == 4
                    ? 1.0f : 0.0f;
                unpackColor(mat.texture->blendColor, blend);
            }
            if (mapped && mat.emissivemap)
                texParams[2] = 1.0f;
            if (mapped && mat.occlusionmap)
                texParams[3] = 1.0f;
            bool mrmapped = mapped && mat.metallicroughnessmap;
            bgfx::setTexture(0, s_texColor, color);
            bgfx::setUniform(u_texParams, texParams);
            bgfx::setUniform(u_texBlendColor, blend);
            float texmat[16];
            if (mat.texidentity)
                bx::mtxIdentity(texmat);
            else
                std::memcpy(texmat, mat.texmatrix, sizeof(texmat));
            bgfx::setUniform(u_texMatrix, texmat);

            // Bump map at unit 2: x = mode (1 normal map, 2 height,
            // 3 height + parallax), y = strength (normal-map slope
            // multiplier / height amplitude in UV units), zw = one
            // texel in UV. White stand-in when off (branch not taken).
            float bumpParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::TextureHandle bump = m_whiteTex;
            if (bumped) {
                const auto &bm = *mat.bumpmap;
                bool heightmap = bm.numComponents <= 2;
                bumpParams[0] = heightmap
                    ? (bumpParallax ? 3.0f : 2.0f) : 1.0f;
                bumpParams[1] = heightmap
                    ? 0.04f * bumpScale : bumpScale;
                bumpParams[2] = 1.0f / float(bm.width);
                bumpParams[3] = 1.0f / float(bm.height);
                bump = getTexture(bm)->handle;
            }
            bgfx::setUniform(u_bumpParams, bumpParams);
            bgfx::setTexture(2, s_texBump, bump);

            // Emissive/occlusion maps at units 4/5 (flagged in
            // u_texParams.zw above; white stand-ins are never sampled).
            bgfx::setTexture(4, s_texEmissive,
                             texParams[2] > 0.5f
                                 ? getTexture(*mat.emissivemap)->handle
                                 : m_whiteTex);
            bgfx::setTexture(5, s_texOcclusion,
                             texParams[3] > 0.5f
                                 ? getTexture(*mat.occlusionmap)->handle
                                 : m_whiteTex);

            // Metallic-roughness map at unit 6 (flagged via
            // u_pbrParams.x = 2 above; the white stand-in is never
            // sampled).
            bgfx::setTexture(
                6, s_texMetallicRoughness,
                mrmapped ? getTexture(*mat.metallicroughnessmap)->handle
                         : m_whiteTex);
        }

        if (patterned) {
            // glLineStipple clamps the repeat factor to [1, 256].
            uint32_t factor = linepattern >> 16;
            factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
            float patParams[4] = {float(linepattern & 0xffff),
                                  float(factor), 0.0f, 0.0f};
            bgfx::setUniform(u_linePattern, patParams);
        }

        setDrawTransform(draw, autozoomScale);
        if (thickline) {
            // One quad per line segment; a partial (per-edge) index range
            // maps 1:1 onto an instance range (two indices per segment).
            uint32_t startSeg = 0;
            uint32_t numSeg = uint32_t(noseam
                ? draw.mesh->numNoSeamLineIndices
                : draw.mesh->numLineIndices) / 2;
            if (draw.indexCount > 0) {
                startSeg = uint32_t(draw.indexStart) / 2;
                numSeg = uint32_t(draw.indexCount) / 2;
            }
            bgfx::setVertexBuffer(0, m_lineQuadVb);
            bgfx::setIndexBuffer(m_lineQuadIb);
            bgfx::setInstanceDataBuffer(
                noseam ? mesh->lineNoSeamInst : mesh->lineInst,
                startSeg, numSeg);
        }
        else if (thickpoint) {
            // One quad per point; a partial index range maps 1:1 onto an
            // instance range (one index per point).
            uint32_t startPt = 0;
            uint32_t numPt = uint32_t(draw.mesh->numPointIndices);
            if (draw.indexCount > 0) {
                startPt = uint32_t(draw.indexStart);
                numPt = uint32_t(draw.indexCount);
            }
            bgfx::setVertexBuffer(0, m_lineQuadVb);
            bgfx::setIndexBuffer(m_lineQuadIb);
            bgfx::setInstanceDataBuffer(mesh->pointInst, startPt, numPt);
        }
        else {
            bgfx::setVertexBuffer(0, mesh->vbh);
            if (textured)
                bgfx::setVertexBuffer(1, mesh->texcoord);
            if (draw.indexCount > 0)
                bgfx::setIndexBuffer(ibh, uint32_t(draw.indexStart),
                                     uint32_t(draw.indexCount));
            else
                bgfx::setIndexBuffer(ibh);
        }
        bgfx::setState(state, blendRt);

        static const bool dbgsubmit =
            (getenv("FC_BGFX_DEBUG_SUBMIT") != nullptr);
        if (dbgsubmit)
            fprintf(stderr,
                    "bgfx submit view=%d pass=%d type=%d part=%d state=%llx"
                    " color=%.2f,%.2f,%.2f,%.2f params=%g,%g,%g,%g"
                    " clip=%d%s lp=%08x\n",
                    passView, pass, mat.type, draw.partIndex,
                    (unsigned long long)state,
                    color[0], color[1], color[2], color[3],
                    params[0], params[1], params[2], params[3],
                    mat.numclipplanes, mat.clipconcave ? " concave" : "",
                    patterned ? linepattern : 0xffffu);

        uint32_t depth = 0;
        if (passView == ViewTransparent && !oitDraw
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
                         ? (oitDraw
                             ? (clipped
                                 ? (textured ? m_progMeshOitTexClip
                                             : m_progMeshOitClip)
                                 : (textured ? m_progMeshOitTex
                                             : m_progMeshOit))
                             : (clipped
                                 ? (textured ? m_progMeshTexClip
                                             : m_progMeshClip)
                                 : (textured ? m_progMeshTex
                                             : m_progMesh)))
                         : thickline
                             ? (patterned
                                 ? (clipped ? m_progLinePatClip
                                            : m_progLinePat)
                                 : (clipped ? m_progLineClip : m_progLine))
                             : thickpoint
                                 ? (clipped ? m_progPointClip : m_progPoint)
                                 : (clipped ? m_progFlatClip : m_progFlat),
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
    bgfx::ProgramHandle m_progMeshInst = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle s_texAccum = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texReveal = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCap = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCapClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texHatch = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_hatchTex = BGFX_INVALID_HANDLE;
    uint64_t m_hatchVersion = 0;   // Private's hatch pixel generation
    static constexpr int kAOSamples = 16;
    bgfx::TextureHandle aoNormalZ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoBlurTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoNoiseTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoPrepassFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoGenFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPrepass = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPrepassClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsaoBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsaoApply = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormalZ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAONoise = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAO = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams = BGFX_INVALID_HANDLE;
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
    bgfx::ProgramHandle m_progShadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texShadow = BGFX_INVALID_HANDLE;
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
    float shadowMtx[16];       // camera view space -> shadow uv/depth
    // Cached shadow map: hash of the light camera + caster set of the
    // moments currently in shadowTex; the caster pass (and blur) only
    // re-runs when it changes. 0 = nothing rendered yet.
    uint64_t shadowMapHash = 0;
    bool m_ssao = false;     // SSAO resources exist (caps allow it)
    // Volumetric light shafts: half-res raymarch of the shadow map
    // bounded by the prepass depth, so both resource sets must exist.
    bool m_vol = false;      // volumetric resources exist
    bgfx::TextureHandle volTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle volFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVol = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolApply = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolExt = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texVol = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volMedium = BGFX_INVALID_HANDLE;
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
    bool m_oit = false;      // OIT resources exist (caps allow it)
    bool oitFrame = false;   // OIT active for the frame being submitted
    std::unordered_map<uint64_t, GpuMesh> meshes;
    std::unordered_map<uint64_t, GpuTexture> textures;
    uint64_t frame = 0;
    int drawcount = 0;
    bool ontop = false;   // route submits to the highlight pass
    // Per-frame world-to-screen scale consumed by autozoom draws
    // (Renderer::setAutoZoomScale).
    float autozoomScale = 1.0f;
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
            view->ensureEnvironment();
            pbrActive = bgfx::isValid(view->m_envTex);
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
        if (view->m_shadow && bboxValid && lightconf.valid) {
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
        // At SmoothBorder 0 the map stores plain (z, z^2) moments and
        // the receivers run Coin's exact VsmLookup — the GL Shadow
        // style's soft default penumbra. The exponential warp (and its
        // tighter penumbra) only engages with the blur.
        view->shadowWarpFrame =
            lightconf.smoothBorder > 0.0f ? view->shadowWarp : 0.0f;
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
        // Water medium: scene draws flagged Material::water become the
        // water body of the volumetric pass. The first flagged draw
        // supplies the shared appearance — extinction sigma from its
        // diffuse color (absorption of the complement) plus a
        // wavelength-independent scattering term, density auto = from
        // its bounds (a known single-appearance limitation for multiple
        // differing bodies).
        bool waterActive = false;
        float waterSigma[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (volActive) {
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                if (!mat.water || mat.ontop
                        || mat.type != Render::Material::Triangle)
                    continue;
                float dens = mat.waterdensity;
                if (dens <= 0.0f) {
                    float dx = draw.bboxMax[0] - draw.bboxMin[0];
                    float dy = draw.bboxMax[1] - draw.bboxMin[1];
                    float dz = draw.bboxMax[2] - draw.bboxMin[2];
                    float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
                    if (diag <= 0.0f)
                        continue;
                    dens = 3.0f / diag;
                }
                float color[4];
                unpackColor(mat.diffuse, color);
                float sigmaS = 0.35f * dens;
                for (int j = 0; j < 3; ++j)
                    waterSigma[j] = sigmaS + dens * (1.0f - color[j]);
                waterSigma[3] = sigmaS;
                waterActive = true;
                if (getenv("FC_BGFX_DEBUG_VOL"))
                    fprintf(stderr,
                            "bgfx water dens=%g sigma=%g,%g,%g s=%g\n",
                            dens, waterSigma[0], waterSigma[1],
                            waterSigma[2], waterSigma[3]);
                break;
            }
        }
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
        bool shadowRender = shadowActive;
        if (shadowActive && !hlconfig.show && !shadowNoCache) {
            uint64_t h = 1469598103934665603ULL;
            hashBytes(h, lightViewMtx, sizeof(float) * 16);
            hashBytes(h, lightProjMtx, sizeof(float) * 16);
            hashBytes(h, &lightconf.smoothBorder,
                      sizeof(lightconf.smoothBorder));
            for (const auto &draw : scene) {
                const auto &mat = draw.material;
                // On-top draws cast too: a selected-on-top object's
                // scene draws are hidden and re-rendered on top, so
                // excluding them dropped its whole shadow (GL keeps it).
                if (mat.type != Render::Material::Triangle
                        || !(mat.shadowstyle & 1)
                        || (waterActive && mat.water)
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
                // Autozoom casters rebuild their transform from the
                // per-frame world-to-screen scale.
                if (!mat.autozoom.empty())
                    hashBytes(h, &autozoomScale, sizeof(autozoomScale));
            }
            shadowRender = h != view->shadowMapHash;
            if (shadowRender)
                view->shadowMapHash = h;
            if (dbgshadow && !shadowRender)
                fprintf(stderr, "bgfx shadow map cached (%llx)\n",
                        (unsigned long long)h);
        } else if (shadowActive) {
            view->shadowMapHash = 0;
        }
        // ShadowSmoothBorder > 0 runs the separable blur over the fresh
        // moments right after the caster pass.
        bool shadowBlurActive = shadowRender
            && lightconf.smoothBorder > 0.0f
            && bgfx::isValid(view->shadowBlurFbo);

        // The prepass rasterizes for SSAO and/or the volumetric ray
        // ends; the AO resolve chain itself stays SSAO-gated.
        bool prepassActive = ssaoActive || volActive;

        for (uint16_t i = 0; i < BGFXView::NUM_VIEWS; ++i) {
            uint16_t id = base + i;
            if (i == BGFXView::ViewTransparent && oitActive) {
                // Accumulation targets: accum clears to 0, revealage
                // to 1; the shared depth attachment is not cleared.
                bgfx::setViewFrameBuffer(id, view->oitFbo);
                bgfx::setPaletteColor(0, 0.0f, 0.0f, 0.0f, 0.0f);
                bgfx::setPaletteColor(1, 1.0f, 1.0f, 1.0f, 1.0f);
                bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_COLOR),
                                   1.0f, 0, 0, 1);
            } else if (shadowRender && i == BGFXView::ViewShadow) {
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
                continue;
            } else if (shadowBlurActive
                       && (i == BGFXView::ViewShadowBlurH
                           || i == BGFXView::ViewShadowBlurV)) {
                // Fullscreen blur passes over the shadow map size; the
                // triangle overwrites every pixel, so no clear.
                bgfx::setViewFrameBuffer(id,
                    i == BGFXView::ViewShadowBlurH
                        ? view->shadowBlurFbo : view->shadowBlurBackFbo);
                bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                                   clearColor, 1.0f, 0);
                bgfx::setViewRect(id, 0, 0, view->shadowSize,
                                  view->shadowSize);
                bgfx::setViewTransform(id, nullptr, nullptr);
                bgfx::setViewMode(id, bgfx::ViewMode::Default);
                bgfx::touch(id);
                continue;
            } else if (volActive && i == BGFXView::ViewVolGen) {
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
                continue;
            } else if (waterActive && (i == BGFXView::ViewWaterFront
                                       || i == BGFXView::ViewWaterBack)) {
                // Water body depth targets: color clears to 0 (.w = 0 =
                // no water on this pixel); the back-face view keeps the
                // farthest depth, so its depth buffer clears to 0 and
                // tests GREATER.
                bool back = i == BGFXView::ViewWaterBack;
                bgfx::setViewFrameBuffer(id, back ? view->waterBackFbo
                                                  : view->waterFrontFbo);
                bgfx::setViewClear(id,
                    uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                    0x00000000u, back ? 0.0f : 1.0f, 0);
                bgfx::setViewRect(id, 0, 0, width, height);
                bgfx::setViewTransform(id, viewMatrix, projMatrix);
                bgfx::setViewMode(id, bgfx::ViewMode::Default);
                bgfx::touch(id);
                continue;
            } else if (prepassActive && i == BGFXView::ViewAOPrepass) {
                // Prepass target clears to 0 (.w = 0 marks background
                // in the AO pass), with its own depth buffer.
                bgfx::setViewFrameBuffer(id, view->aoPrepassFbo);
                bgfx::setViewClear(id,
                    uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                    0x00000000u, 1.0f, 0);
            } else if (ssaoActive && (i == BGFXView::ViewAOGen
                                      || i == BGFXView::ViewAOBlur)) {
                // Fullscreen passes overwrite their whole target.
                bgfx::setViewFrameBuffer(id,
                    i == BGFXView::ViewAOGen ? view->aoGenFbo
                                             : view->aoBlurFbo);
                bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                                   clearColor, 1.0f, 0);
            } else {
                bgfx::setViewFrameBuffer(id, view->bgfxFbo);
                // The section-cap views clear the stencil: their parity
                // marking (INVERT) needs a zeroed base, and earlier
                // outline passes leave stale marks behind (relevant for
                // the transparent cap view, which runs after ViewOutline).
                bgfx::setViewClear(id,
                    i == 0 ? uint16_t(BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
                                      |BGFX_CLEAR_STENCIL)
                    : (i == BGFXView::ViewSectionCap
                       || i == BGFXView::ViewSectionCapTransp)
                        ? uint16_t(BGFX_CLEAR_STENCIL)
                        : uint16_t(BGFX_CLEAR_NONE),
                    clearColor, 1.0f, 0);
            }
            bgfx::setViewRect(id, 0, 0, width, height);
            // The background quad, the OIT composite triangle and the
            // fullscreen AO blur/apply triangles are submitted in clip
            // space; the AO generation pass keeps the scene projection
            // for its predefined u_proj (position reconstruction).
            if (i == BGFXView::ViewBackground
                    || i == BGFXView::ViewOITComposite
                    || i == BGFXView::ViewAOBlur
                    || i == BGFXView::ViewAOApply)
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
            bgfx::setViewMode(id,
                i == BGFXView::ViewTransparent && !oitActive
                    ? bgfx::ViewMode::DepthDescending
                    : i >= BGFXView::ViewOnTop
                            || i == BGFXView::ViewOutline
                            || i == BGFXView::ViewSectionCap
                            || i == BGFXView::ViewSectionCapTransp
                            || i == BGFXView::ViewVolApply
                        ? bgfx::ViewMode::Sequential
                        : bgfx::ViewMode::Default);
            bgfx::touch(id);
        }

        ++view->frame;
        view->drawcount = 0;
        view->autozoomScale = autozoomScale;
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
        // draws (shared geometry cache, matching material bar the
        // diffuse) collapses into one instanced submit of the opaque
        // pass, carrying {model matrix, diffuse} per instance. Members
        // keep their per-draw side submissions (prepass, shadow caster,
        // hidden-line outline) — only the main color submit batches.
        // Hidden-line frames stay per-draw (the stencil outline pass
        // reworks the fill submits wholesale).
        static const bool noInstancing =
            getenv("FC_BGFX_NO_INSTANCING") != nullptr;
        std::vector<uint8_t> drawInstanced;
        if (!noInstancing && !hl.show && !instGroups.empty()
                && view->instancingActive()) {
            drawInstanced.assign(scene.size(), 0);
            std::vector<float> instData;
            std::vector<int> vis;
            for (const auto &group : instGroups) {
                if (group.members.size() < 2)
                    continue;
                vis.clear();
                for (int i : group.members) {
                    if (!isHidden(scene[i]))
                        vis.push_back(i);
                }
                if (vis.size() < 2)
                    continue;
                instData.clear();
                instData.reserve(vis.size() * 20);
                for (int i : vis) {
                    const auto &d = scene[i];
                    if (d.identity) {
                        static const float ident[16] = {
                            1.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 1.0f};
                        instData.insert(instData.end(), ident, ident + 16);
                    } else {
                        instData.insert(instData.end(), d.model,
                                        d.model + 16);
                    }
                    float c[4];
                    unpackColor(d.material.diffuse, c);
                    instData.insert(instData.end(), c, c + 4);
                }
                if (view->submitInstanced(scene[vis[0]], instData.data(),
                                          uint32_t(vis.size()))) {
                    for (int i : vis)
                        drawInstanced[i] = 1;
                }
            }
        }
        auto instancedThisFrame = [&](int i) {
            return !drawInstanced.empty() && drawInstanced[i];
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
                        && !(waterActive && draw.material.water))
                    view->submitShadowCaster(draw);
                continue;
            }
            if (hideFill(draw) || hidePoints(draw))
                continue;
            if (!instancedThisFrame(drawIdx))
                view->submit(draw, viewMat, BGFXView::PassNormal,
                             sceneNoSeam(draw));
            // Water body draws bound the medium instead of acting as
            // ordinary surfaces: their front/back depths rasterize into
            // the water targets (whatever their transparency), and they
            // are excluded from the volumetric ray ends and from shadow
            // casting so light and shafts enter the water.
            bool isWater = waterActive && isTriangle(draw)
                && draw.material.water;
            if (isWater) {
                view->submitWaterDepth(draw, false);
                view->submitWaterDepth(draw, true);
            }
            // The SSAO prepass re-rasterizes the opaque fills into the
            // depth+normal target (transparent geometry neither occludes
            // nor receives AO; the multiply pass runs before the
            // transparent bucket). The volumetric raymarch shares it as
            // its ray-end depth source.
            if (prepassActive && isTriangle(draw) && !isTransp(draw)
                    && !isWater)
                view->submitPrepass(draw);
            // Shadow casters — transparent geometry casts like an opaque
            // one, matching Coin's SoShadowGroup (its depth-map pass
            // ignores alpha); water is the one exception (light must
            // enter the medium). Skipped while the cached map is valid.
            if (shadowRender && isTriangle(draw)
                    && (draw.material.shadowstyle & 1) && !isWater)
                view->submitShadowCaster(draw);
            submitSceneOutline(draw);
        }
        if (shadowBlurActive)
            view->submitShadowBlur(lightconf.smoothBorder);
        if (shadowActive && lightconf.ground && bboxValid) {
            view->submitShadowGround(bboxMin, bboxMax, lightconf,
                                     volActive);
        }
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && !isTransp(draw)
                    && !isHidden(draw) && !hideFill(draw)) {
                view->submit(draw, viewMat);
                // On-top geometry keeps casting its shadow (the view
                // order still lands these in the caster pass).
                if (shadowRender && (draw.material.shadowstyle & 1)
                        && !(waterActive && draw.material.water))
                    view->submitShadowCaster(draw);
                submitSceneOutline(draw);
            }
        }
        for (const auto &draw : scene) {
            if (draw.material.ontop && isTriangle(draw) && isTransp(draw)
                    && !isHidden(draw) && !hideFill(draw)) {
                view->submit(draw, viewMat);
                if (shadowRender && (draw.material.shadowstyle & 1)
                        && !(waterActive && draw.material.water))
                    view->submitShadowCaster(draw);
                submitSceneOutline(draw);
            }
        }

        // 1b. Stencil section caps of clipped solids, in their own
        // sequential views (opaque caps between the opaque and outline
        // passes, transparent caps after the OIT composite — GL's
        // grouped section pass order).
        view->updateHatchTexture(hatchVersion,
                                 hatchRGBA.empty() ? nullptr
                                                   : hatchRGBA.data(),
                                 hatchWidth, hatchHeight);
        submitSectionCaps(view, reinterpret_cast<const float *>(projMatrix));

        // 1c. SSAO resolve: generate, blur, and multiply the AO onto the
        // opaque scene (the AO views sit between the caps and the
        // outline/transparent passes).
        if (ssaoActive)
            view->submitAOResolve(aoRadius, aoconf.intensity);

        // 1d. Volumetric light shafts: half-res raymarch of the shadow
        // map, bilateral-upsampled and composited onto the opaque scene
        // after the outlines, before the transparent bucket.
        if (volActive)
            view->submitVolumetric(volDensity, volconf.intensity,
                                   volMaxDist, volMedium,
                                   waterActive, waterSigma);

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
                            && !isHidden(draw) && !hideFill(draw))
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
                            && !isHidden(draw) && !hidePoints(draw))
                        view->submit(draw, viewMat, pass,
                                     sceneNoSeam(draw));
                }
                for (const auto &sel : selections) {
                    if (sel.first <= 0)
                        continue;
                    for (const auto &draw : sel.second) {
                        if (!isTriangle(draw) && !isDup(draw))
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
                        && !isHidden(draw) && !hidePoints(draw))
                    view->submit(draw, viewMat, BGFXView::PassNormal,
                                 sceneNoSeam(draw));
            }
        }

        // 6. Single-part selection fills (e.g. the selected face), matching
        // GL's transpselectionsfaceontop position after the line passes.
        for (const auto &sel : selections) {
            view->ontop = sel.first > 0;
            for (const auto &draw : sel.second) {
                if (isTriangle(draw) && draw.partIndex >= 0
                        && !outlineOnly(draw))
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

        if (oitActive)
            view->submitComposite();

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
                    " dups=%zu draws=%d\n",
                    (unsigned long long)view->frame, scene.size(),
                    selections.size(), highlight.size(),
                    dupDraws.size(), view->drawcount);

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
        if (secconf.hatchEnable && hatchWidth > 0) {
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
                                           / float(hatchWidth));
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
                                        secconf.hatchEnable && hatchWidth > 0,
                                        bucket == 1, capView);
                    view->submitCapCleanup(verts, capView);
                }
                head = tail;
            }
        }
    }

    void updateBBox()
    {
        static const bool dbg = getenv("FC_BGFX_DEBUG_BBOX") != nullptr;
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
        if (dbg && bboxValid)
            fprintf(stderr, "bgfx bbox: %g,%g,%g - %g,%g,%g\n",
                    bboxMin[0], bboxMin[1], bboxMin[2],
                    bboxMax[0], bboxMax[1], bboxMax[2]);
    }

    QOpenGLWidget *widget;
    bool _deinit = false;
    RendererType::Enum type;
    std::string typeName;

    // CPU-side scene data fed through Render::Renderer's scene API. GPU
    // upload happens lazily during render(), so the feed may arrive before
    // bgfx is initialized.
    Render::DrawCallList scene;
    // Cross-object instance groups of the scene feed: draws sharing one
    // geometry cache, index range and material (diffuse aside — links
    // may override colors) collapse into one instanced submit per frame.
    // Rebuilt with the scene; the per-frame loop still skips hidden
    // members and falls back to per-draw submits when a group thins out.
    struct InstGroup { std::vector<int> members; };
    std::vector<InstGroup> instGroups;
    std::vector<int> instGroupOf;   ///< scene index -> group index or -1

    /// A draw the instanced mesh path can express: an opaque, untextured,
    /// unclipped, non-on-top, non-water triangle draw without autozoom.
    /// (Hidden-line frames disable instancing wholesale, so the outline
    /// material flag stays out of the picture.)
    static bool instancableDraw(const Render::DrawCall &d)
    {
        const Render::Material &m = d.material;
        return d.mesh && d.mesh->numTriangleIndices > 0
            && m.type == Render::Material::Triangle
            && !m.ontop
            && !m.transparent
            && !(m.pervertexcolor && d.mesh->hasTransparency)
            && m.autozoom.empty()
            && m.numclipplanes == 0
            && !m.texture && !m.bumpmap && !m.emissivemap
            && !m.occlusionmap && !m.metallicroughnessmap
            && !m.water
            && !m.faceoutline;
    }

    void buildInstanceGroups()
    {
        instGroups.clear();
        instGroupOf.assign(scene.size(), -1);

        // Group key: every material field the instanced submit consumes
        // besides the diffuse color (which rides the instance data).
        // Byte-compared, so the struct is zeroed first (padding).
        struct InstKey {
            const void *mesh;
            int start, count, part;
            float shininess, pofactor, pounits, metallic, roughness;
            uint32_t emissive, specular, ambient;
            uint8_t depthfunc, shadowstyle;
            uint8_t depthtest, depthwrite, pervertexcolor, lighting,
                twoside, culling, ccw, polygonoffset, solidshape;
        };
        struct KeyLess {
            bool operator()(const InstKey &a, const InstKey &b) const
            { return std::memcmp(&a, &b, sizeof(InstKey)) < 0; }
        };
        std::map<InstKey, int, KeyLess> groups;
        for (int i = 0; i < int(scene.size()); ++i) {
            const auto &d = scene[i];
            if (!instancableDraw(d))
                continue;
            const Render::Material &m = d.material;
            InstKey k;
            std::memset(&k, 0, sizeof(k));
            k.mesh = d.mesh.get();
            k.start = d.indexStart;
            k.count = d.indexCount;
            k.part = d.partIndex;
            k.shininess = m.shininess;
            k.pofactor = m.polygonoffsetfactor;
            k.pounits = m.polygonoffsetunits;
            k.metallic = m.metallic;
            k.roughness = m.roughness;
            k.emissive = m.emissive;
            k.specular = m.specular;
            k.ambient = m.ambient;
            k.depthfunc = m.depthfunc;
            k.shadowstyle = m.shadowstyle;
            k.depthtest = m.depthtest;
            k.depthwrite = m.depthwrite;
            k.pervertexcolor = m.pervertexcolor;
            k.lighting = m.lighting;
            k.twoside = m.twoside;
            k.culling = m.culling;
            k.ccw = m.ccw;
            k.polygonoffset = m.polygonoffset;
            k.solidshape = m.solidshape;
            auto res = groups.emplace(k, int(instGroups.size()));
            if (res.second)
                instGroups.emplace_back();
            instGroups[res.first->second].members.push_back(i);
            instGroupOf[i] = res.first->second;
        }
        // Singletons gain nothing; keep them on the per-draw path.
        for (auto &g : instGroups) {
            if (g.members.size() < 2) {
                for (int i : g.members)
                    instGroupOf[i] = -1;
                g.members.clear();
            }
        }
        while (!instGroups.empty() && instGroups.back().members.empty())
            instGroups.pop_back();
        if (getenv("FC_BGFX_DEBUG_FEED")) {
            size_t n = 0, draws = 0;
            for (const auto &g : instGroups) {
                if (!g.members.empty()) {
                    ++n;
                    draws += g.members.size();
                }
            }
            fprintf(stderr,
                    "bgfx feed instancing: %zu groups covering %zu of %zu"
                    " draws\n", n, draws, scene.size());
        }
    }

    Render::Background background;
    std::map<int, Render::DrawCallList> selections;
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
    float autozoomScale = 1.0f;
    // CPU copy of the section hatch texture, expanded to RGBA8; the
    // version stamps GPU re-uploads (0 = no image).
    std::vector<uint8_t> hatchRGBA;
    int hatchWidth = 0;
    int hatchHeight = 0;
    const void *hatchKey = nullptr;
    uint64_t hatchVersion = 0;
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
    // The shadow ground quad is backend geometry outside the scene
    // draws; include it so the viewer's camera auto-clipping covers it
    // (the Coin GL ground used to do this through the scene graph).
    const Render::LightConfig &light = pimpl->lightconf;
    if (light.valid && light.ground && light.groundTransparency < 1.0f) {
        float half = light.groundScale
            * std::max(xmax - xmin,
                       std::max(ymax - ymin, zmax - zmin));
        float cx = (xmin + xmax) * 0.5f;
        float cy = (ymin + ymax) * 0.5f;
        xmin = std::min(xmin, cx - half);
        xmax = std::max(xmax, cx + half);
        ymin = std::min(ymin, cy - half);
        ymax = std::max(ymax, cy + half);
    }
    return true;
}

static void dumpFeed(const char *tag, int id,
                     const Render::DrawCallList &draws);

void BGFXRenderer::setScene(DrawCallList &&draws)
{
    dumpFeed("scene", 0, draws);
    pimpl->scene = std::move(draws);
    pimpl->buildInstanceGroups();
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
                "  cache=%llx type=%d part=%d range=%d+%d diffuse=%08x emissive=%08x"
                " pvc=%d light=%d transp=%d ontop=%d dtest=%d dwrite=%d"
                " dfunc=%d lw=%.1f po=%d/%.1f/%.1f hla=%.2f lp=%08x/%08x"
                " ol=%d lc=%08x tex=%d bump=%d em=%d occ=%d mr=%d uv=%d"
                " ss=%d\n",
                d.mesh ? (unsigned long long)d.mesh->cacheId : 0ull,
                m.type, d.partIndex, d.indexStart, d.indexCount,
                m.diffuse, m.emissive, m.pervertexcolor, m.lighting,
                m.transparent, m.ontop, m.depthtest, m.depthwrite,
                m.depthfunc, m.linewidth, m.polygonoffset,
                m.polygonoffsetfactor, m.polygonoffsetunits,
                m.hiddenlinealpha, m.linepattern, m.hiddenlinepattern,
                m.outline, m.linecolor,
                m.texture ? m.texture->numComponents : 0,
                m.bumpmap ? m.bumpmap->numComponents : 0,
                m.emissivemap ? m.emissivemap->numComponents : 0,
                m.occlusionmap ? m.occlusionmap->numComponents : 0,
                m.metallicroughnessmap
                    ? m.metallicroughnessmap->numComponents : 0,
                d.mesh && d.mesh->texCoords ? 1 : 0,
                m.shadowstyle);
        for (int i = 0; i < m.numclipplanes; ++i)
            fprintf(stderr, "  clip%s %d: %g,%g,%g,%g\n",
                    m.clipconcave ? " (concave)" : "", i,
                    m.clipplanes[i][0], m.clipplanes[i][1],
                    m.clipplanes[i][2], m.clipplanes[i][3]);
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

void BGFXRenderer::setHiddenLineConfig(const HiddenLineConfig &config)
{
    if (pimpl->hlconfig != config) {
        pimpl->hlconfig = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setSectionConfig(const SectionConfig &config)
{
    if (pimpl->secconf != config) {
        pimpl->secconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setAOConfig(const AOConfig &config)
{
    if (pimpl->aoconf != config) {
        pimpl->aoconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setPBRConfig(const PBRConfig &config)
{
    if (pimpl->pbrconf != config) {
        pimpl->pbrconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setBumpConfig(const BumpConfig &config)
{
    if (pimpl->bumpconf != config) {
        pimpl->bumpconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setLightConfig(const LightConfig &config)
{
    if (pimpl->lightconf != config) {
        pimpl->lightconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setVolumetricConfig(const VolumetricConfig &config)
{
    if (pimpl->volconf != config) {
        pimpl->volconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setAutoZoomScale(float scale)
{
    if (pimpl->autozoomScale == scale)
        return;
    pimpl->autozoomScale = scale;
    // Only autozoom draws depend on the scale; they converge one frame
    // late on camera changes like the rest of the feed.
    auto hasAutoZoom = [](const Render::DrawCallList &draws) {
        for (const auto &draw : draws) {
            if (!draw.material.autozoom.empty())
                return true;
        }
        return false;
    };
    if (hasAutoZoom(pimpl->scene) || hasAutoZoom(pimpl->highlight)) {
        pimpl->sceneDirty = true;
        return;
    }
    for (const auto &sel : pimpl->selections) {
        if (hasAutoZoom(sel.second)) {
            pimpl->sceneDirty = true;
            return;
        }
    }
}

void BGFXRenderer::setHatchImage(const void *data, int nc,
                                 int width, int height)
{
    if (data == pimpl->hatchKey && width == pimpl->hatchWidth
            && height == pimpl->hatchHeight)
        return;
    pimpl->hatchKey = data;
    pimpl->hatchRGBA.clear();
    pimpl->hatchWidth = 0;
    pimpl->hatchHeight = 0;
    if (data && nc > 0 && width > 0 && height > 0) {
        // Expand to RGBA8 (the image comes as tightly packed
        // nc-component rows; 1/2 components are luminance(+alpha)).
        const uint8_t *src = static_cast<const uint8_t *>(data);
        pimpl->hatchRGBA.resize(size_t(width) * height * 4);
        uint8_t *dst = pimpl->hatchRGBA.data();
        for (size_t i = 0, n = size_t(width) * height; i < n; ++i) {
            const uint8_t *p = src + i * nc;
            switch (nc) {
            case 1: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = 255; break;
            case 2: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = p[1]; break;
            case 3: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                    dst[3] = 255; break;
            default: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                     dst[3] = p[3]; break;
            }
            dst += 4;
        }
        pimpl->hatchWidth = width;
        pimpl->hatchHeight = height;
    }
    ++pimpl->hatchVersion;
    pimpl->sceneDirty = true;
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
