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

#ifndef RENDERER_RENDERER_H
#define RENDERER_RENDERER_H

#include <FCConfig.h>

#ifdef FreeCADRenderer_STATIC
#   define RendererExport
#elif defined(FreeCADRenderer_EXPORTS)
#   define RendererExport   FREECAD_DECL_EXPORT
#else
#   define RendererExport   FREECAD_DECL_IMPORT
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QOpenGLWidget;
class QColor;

namespace Render {

class RenderLib;

/// CPU-side snapshot of one geometry cache (SoFCVertexCache on the Gui side).
/// All array pointers stay valid for as long as `owner` is held. Backends key
/// GPU uploads on `cacheId`: the same id always refers to identical content.
struct MeshData {
    uint64_t cacheId = 0;
    std::shared_ptr<const void> owner;

    int numVertices = 0;
    const float *positions = nullptr;   ///< xyz per vertex, never null
    const float *normals = nullptr;     ///< xyz per vertex, may be null
    const uint8_t *colors = nullptr;    ///< rgba8 per vertex, may be null

    const int32_t *triangleIndices = nullptr;
    int numTriangleIndices = 0;
    const int32_t *lineIndices = nullptr;   ///< GL_LINES style vertex pairs
    int numLineIndices = 0;
    const int32_t *pointIndices = nullptr;
    int numPointIndices = 0;

    bool hasTransparency = false;   ///< some per-vertex colors are transparent
    bool hasOpaqueParts = false;    ///< some per-vertex colors are opaque
};

/// Flattened per-draw render state, translated from the Coin-side material
/// (SoFCRenderCache::Material). Colors are packed 0xRRGGBBAA.
struct Material {
    enum Type : uint8_t { Triangle, Line, Point };
    enum DepthFunc : uint8_t {
        Never, Always, Less, LEqual, Equal, GEqual, Greater, NotEqual
    };

    uint8_t type = Triangle;
    uint8_t depthfunc = LEqual;
    bool depthtest = true;
    bool depthwrite = true;
    bool pervertexcolor = false;
    bool lighting = true;        ///< false = flat base color (no light model)
    bool twoside = false;
    bool culling = false;
    bool ccw = true;             ///< front face vertex ordering
    bool transparent = false;    ///< uniform-color / texture transparency
    bool ontop = false;          ///< render after (over) the normal scene
    uint32_t diffuse = 0xCCCCCCFF;
    uint32_t emissive = 0;
    uint32_t specular = 0;
    uint32_t ambient = 0;
    float shininess = 0.0f;
    float linewidth = 1.0f;
    float pointsize = 1.0f;
};

/// One draw of (a part of) a mesh with a material and model transform.
struct DrawCall {
    Material material;
    std::shared_ptr<const MeshData> mesh;
    float model[16];        ///< GL-style layout, valid when !identity
    bool identity = true;
    int partIndex = -1;     ///< -1 = whole mesh, >= 0 = single face/edge part
    float bboxMin[3] = {0.0f, 0.0f, 0.0f};  ///< world space bounds,
    float bboxMax[3] = {0.0f, 0.0f, 0.0f};  ///< empty if min > max
};

typedef std::vector<DrawCall> DrawCallList;

class RendererExport Renderer
{
public:
    virtual ~Renderer() {}
    virtual const std::string &type() const = 0;
    virtual bool render(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix) = 0;
    virtual bool boundBox(float &xmin, float &ymin, float &zmin,
                          float &xmax, float &ymax, float &zmax) = 0;

    /// \name Scene API
    /// Mirrors SoFCRenderer's feed. Backends that don't consume scene data
    /// keep the default no-ops and canSkipInternal() == false, so the
    /// existing GL pipeline continues to draw the scene.
    //@{
    /// Replace the whole scene. An empty list clears it.
    virtual void setScene(DrawCallList &&draws) { (void)draws; }
    /// Add/replace one selection identified by id (SoFCRenderer::SelIdBits).
    virtual void addSelection(int id, DrawCallList &&draws)
    { (void)id; (void)draws; }
    virtual void removeSelection(int id) { (void)id; }
    /// Set the preselection highlight geometry.
    virtual void setHighlight(DrawCallList &&draws, bool wholeOnTop)
    { (void)draws; (void)wholeOnTop; }
    virtual void clearHighlight() {}
    /// True if scene data changed after the last render() and another
    /// frame should be scheduled.
    virtual bool needsRedraw() const { return false; }
    /// True when this backend has rendered the current scene and the
    /// internal fixed-function GL pass can be skipped.
    virtual bool canSkipInternal() const { return false; }
    //@}
};

class RendererLib
{
public:
    virtual ~RendererLib() {}
    virtual const std::string &name() const = 0;
    virtual const std::vector<std::string> &types() const = 0;
    virtual std::unique_ptr<Renderer> create(
            const std::string &type, QOpenGLWidget *widget) const = 0;
};

class RendererExport RendererFactory
{
public:
    static std::vector<std::string> types();
    static std::unique_ptr<Renderer> create(const std::string &type, QOpenGLWidget *widget);
    static void registerLib(RendererLib *);
    static void setResourcePath(const std::string &path);
    static const std::string &resourcePath();
};

} // namespace Render

#endif // RENDERER_RENDERER_H
