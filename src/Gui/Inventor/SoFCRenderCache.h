/****************************************************************************
 *   Copyright (c) 2020 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef FC_RENDERCACHE_H
#define FC_RENDERCACHE_H

#include <cstdint>
#include <vector>
#include <map>
#include <memory>
#include <bitset>

#include <boost/container/flat_map.hpp>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/caches/SoCache.h>

#include "../SoFCUnifiedSelection.h"
#include "COWData.h"
#include "SoFCRenderCacheManager.h"
#include "SoAutoZoomTranslation.h"

namespace Render {
struct UserShader;
struct FinishPalette;
struct FramePalette;

/// The creation serial of one of the immutable, pointer-shared cache
/// objects, or 0 for a null one (Render::CacheSerial).
///
/// Out of line, and taking the pointer, so that the render cache can go
/// on holding these as an incomplete type -- which is the whole reason
/// they are standalone structs. Reached only when every earlier field
/// of a material ties.
GuiExport std::uint64_t cacheSerialOf(const FinishPalette *palette);
GuiExport std::uint64_t cacheSerialOf(const FramePalette *palette);
GuiExport std::uint64_t cacheSerialOf(const UserShader *shader);

/// The stable serial of a captured NODE (Render::CacheSerial::forNode),
/// which is what the texture and node infos below are ordered by.
GuiExport std::uint64_t cacheSerialOfNode(const void *node);
}

class SoFCVertexCache;
class SoFCRenderCacheP;
class SoState;
class SoTexture;
class SoLight;
class SoLightModel;
class SoMaterial;
class SoVRMLMaterial;
class SoDepthBuffer;
class SbBox3f;
class SoClipPlane;
class SoMFColor;

class GuiExport SoFCRenderCache : public SoCache {
  typedef SoCache inherited;
public:

  struct MatrixInfo {
    SbMatrix matrix;

    void combine(const MatrixInfo & other) {
      this->matrix = this->matrix.multLeft(other.matrix);
    }

    int compare(const MatrixInfo & other) const {
      for (int i=0; i<16; ++i) {
        if (this->matrix[i] < other.matrix[i]) return -1;
        if (this->matrix[i] > other.matrix[i]) return 1;
      }
      return 0;
    }

    bool operator==(const MatrixInfo &other) const {
      return compare(other) == 0;
    }

    bool operator!=(const MatrixInfo &other) const {
      return compare(other) != 0;
    }

    bool operator<(const MatrixInfo &other) const {
      return compare(other) < 0;
    }

    bool operator>(const MatrixInfo &other) const {
      return compare(other) > 0;
    }
  };

  typedef COWMap<int, MatrixInfo> TextureMatrixMap;

  struct TextureInfo {
    Gui::CoinPtr<SoNode> texture;
    SbMatrix matrix;
    bool identity = true;
    bool transparent = false;
    /// Memoized by setTexture(); see the note in compare().
    std::uint64_t serial = 0;

    void setTexture(SoNode * node) {
      this->texture = node;
      this->serial = Render::cacheSerialOfNode(node);
    }

    int compare(const TextureInfo & other) const {
    // ORDERED BY A STABLE SERIAL, not by the address and not by a Coin
    // node id.
    //
    // The address is different in every run, and a draw order that
    // follows it renders differently from run to run wherever two draws
    // contend for a pixel at equal depth. A node id is deterministic but
    // MOVES: Coin reassigns it on every notify(), so a material captured
    // before one and a material captured after it disagree about the same
    // node, and buildFromPrevious -- which merges a previous publish's
    // map with entries built now -- turns one light into two buckets that
    // draw identically. CacheSerial::forNode is both: fixed for the
    // node's whole life, and free of addresses.
      if (this->serial < other.serial) return -1;
      if (this->serial > other.serial) return 1;
      if (this->transparent < other.transparent) return -1;
      if (this->transparent > other.transparent) return 1;
      if (this->identity < other.identity) return -1;
      if (this->identity > other.identity) return 1;
      if (!this->identity) {
        for (int i=0; i<16; ++i) {
          if (this->matrix[i] < other.matrix[i]) return -1;
          if (this->matrix[i] > other.matrix[i]) return 1;
        }
      }
      return 0;
    }

    bool operator==(const TextureInfo &other) const {
      return compare(other) == 0;
    }

    bool operator!=(const TextureInfo &other) const {
      return compare(other) != 0;
    }

    bool operator<(const TextureInfo &other) const {
      return compare(other) < 0;
    }

    bool operator>(const TextureInfo &other) const {
      return compare(other) > 0;
    }
  };

  typedef COWMap<int, TextureInfo> TextureMap;

  struct NodeInfo {
    Gui::CoinPtr<SoNode> node;
    SbMatrix matrix;
    bool identity = true;
    bool resetmatrix = false;;
    /// Memoized by setNode(); see the note in compare().
    std::uint64_t serial = 0;

    void setNode(SoNode * n) {
      this->node = n;
      this->serial = Render::cacheSerialOfNode(n);
    }

    int compare(const NodeInfo & other) const {
    // ORDERED BY A STABLE SERIAL, not by the address and not by a Coin
    // node id.
    //
    // The address is different in every run, and a draw order that
    // follows it renders differently from run to run wherever two draws
    // contend for a pixel at equal depth. A node id is deterministic but
    // MOVES: Coin reassigns it on every notify(), so a material captured
    // before one and a material captured after it disagree about the same
    // node, and buildFromPrevious -- which merges a previous publish's
    // map with entries built now -- turns one light into two buckets that
    // draw identically. CacheSerial::forNode is both: fixed for the
    // node's whole life, and free of addresses.
      if (this->serial < other.serial) return -1;
      if (this->serial > other.serial) return 1;
      if (this->identity < other.identity) return -1;
      if (this->identity > other.identity) return 1;
      if (!this->identity) {
        for (int i=0; i<16; ++i) {
          if (this->matrix[i] < other.matrix[i]) return -1;
          if (this->matrix[i] > other.matrix[i]) return 1;
        }
      }
      return 0;
    }

    bool operator==(const NodeInfo &other) const {
      return compare(other) == 0;
    }

    bool operator!=(const NodeInfo &other) const {
      return compare(other) != 0;
    }

    bool operator<(const NodeInfo &other) const {
      return compare(other) < 0;
    }

    bool operator>(const NodeInfo &other) const {
      return compare(other) > 0;
    }

    template <class T>
    T *cast() {
      assert(node && node->isOfType(T::getClassTypeId()));
      return static_cast<T*>(node.get());
    }

    template <class T>
    const T *cast() const {
      assert(node && node->isOfType(T::getClassTypeId()));
      return static_cast<T*>(node.get());
    }
  };

  typedef COWVector<NodeInfo> NodeInfoArray;

  struct _Material {
    enum Type {
      Triangle,
      Line,
      Point,
    };
    enum SelectStyle {
      Full,
      Box,
      BoxFull, // override full selection by showing only bounding box
      Unpickable,
    };
    enum FlagBits {
      FLAG_AMBIENT = 0,
      FLAG_DIFFUSE = 1,
      FLAG_DRAW_STYLE = 2,
      FLAG_EMISSIVE = 3,
      FLAG_LIGHT_MODEL = 4,
      FLAG_LINE_PATTERN = 5,
      FLAG_LINE_WIDTH = 6,
      FLAG_LINE_COLOR = 7,
      FLAG_FACE_COLOR = 8,
      FLAG_FACE_TRANSPARENCY = 9,
      FLAG_MATERIAL_BINDING = 10,
      FLAG_POINT_SIZE = 11,
      FLAG_SHAPE_HINTS = 12,
      FLAG_SHININESS = 13,
      FLAG_SPECULAR = 14,
      FLAG_TRANSPARENCY = 15,
      FLAG_VERTEXORDERING = 16,
      FLAG_TWOSIDE = 17,
      FLAG_CULLING = 18,
      FLAG_SHADE_MODEL = 19,
      FLAG_SHADOW_STYLE = 20,
      FLAG_DEPTH_TEST = 21,
      FLAG_DEPTH_WRITE = 22,
      FLAG_DEPTH_FUNC = 23,
      FLAG_POLYGON_OFFSET = 24,
      FLAG_NO_TEXTURE = 25,
    };

    int32_t order;
    std::bitset<32> overrideflags;
    std::bitset<32> maskflags;
    uint32_t diffuse;
    uint32_t ambient;
    uint32_t emissive;
    uint32_t specular;
    /// Array form of ambient/emissive/specular/shininess, captured
    /// from the coin fork's extended lazy element when a material node
    /// holds them per face (packed colors, matching the scalars
    /// above; empty = scalar only, which is every object until
    /// something feeds per-face materials). Only external backends
    /// will consume them; the GL path keeps reading the scalars.
    COWVector<uint32_t> ambients;
    COWVector<uint32_t> emissives;
    COWVector<uint32_t> speculars;
    COWVector<float> shininesses;
    uint32_t linepattern;
    uint32_t linecolor;
    uint32_t facecolor;
    float linewidth;
    float pointsize;
    float shininess;
    /// Per-object PBR overrides captured from SoFCRenderMaterial
    /// (< 0 = unset); only external backends consume them.
    float metallic;
    float roughness;
    /// Per-face form of the pair, captured from the same node when a
    /// PBR appearance states one factor pair per face (empty
    /// otherwise). A whole-object draw shades from the mesh's baked
    /// stream, which carries the same values; these serve the draws
    /// that cannot -- a single-face draw resolves its face's pair into
    /// the scalars above. Indexed like the arrays above, but padded
    /// with entry 0 rather than clamped (see SoFCPbrElement).
    COWVector<float> metallics;
    COWVector<float> roughnesses;
    /// Machined surface finish captured from SoFCRenderMaterial (0 =
    /// none): the procedural pattern external backends shade over the
    /// surface, with the pitch and depth in millimetres of object space
    /// and the lay angle in degrees.
    uint8_t finish;
    float finishpitch;
    float finishdepth;
    float finishangle;
    /// Per-face form of the finish, captured from the same node: the
    /// distinct finishes of the appearance as a palette, and one index
    /// into it per face. A whole-object draw hands the palette to the
    /// backend and shades from the index the mesh's baked stream
    /// carries; a single-face draw, which has no stream, resolves its
    /// face's entry into the scalars above. Entry 0 is what those
    /// scalars repeat. Indexed like the PBR pair -- padded with entry 0
    /// rather than clamped (see SoFCFinishElement).
    std::shared_ptr<const Render::FinishPalette> finishpalette;
    COWVector<int32_t> finishindices;
    /// The projection frame the finish is laid out in, captured from the
    /// same node but derived from the GEOMETRY rather than from the
    /// appearance -- the plane's own axes, or the axis a cylinder was
    /// turned about (Render::SurfaceFrame). Unframed leaves the finish
    /// projected triplanarly, which is what every shape without an
    /// analytic surface, and every shape nobody finished, states.
    /// Palette and index array follow the finish's rules exactly, down
    /// to entry 0 being the whole draw's -- here the FIRST FACE's frame,
    /// which is what a mesh whose stream collapsed (every face framed
    /// alike) reads, and what a single-face draw resolves against.
    std::shared_ptr<const Render::FramePalette> framepalette;
    COWVector<int32_t> frameindices;
    /// Water body flag/density captured from SoFCRenderMaterial: the
    /// shapes' closed volume becomes a scattering medium of the
    /// volumetric lighting pass (only external backends consume this).
    bool water;
    float waterdensity;
    /// Glass body flag/parameters captured from SoFCRenderMaterial:
    /// external backends render the shapes with screen-space
    /// refraction, environment reflection and thickness absorption
    /// instead of the ordinary transparent path.
    bool glass;
    float glassior;
    float glassdensity;
    float glassroughness;
    /// Cloud body flag/parameters captured from SoFCRenderMaterial:
    /// external backends raymarch the shapes' closed volume as a
    /// procedural-density scattering medium instead of rendering the
    /// geometry.
    bool cloud;
    float clouddensity;
    float clouddetail;
    float cloudspeed;
    /// Fire body flag/parameters captured from SoFCRenderMaterial:
    /// external backends raymarch the shapes' closed volume as an
    /// emissive flame medium instead of rendering the geometry.
    bool fire;
    float fireintensity;
    float firedetail;
    float firespeed;
    /// Fountain body flag/parameters captured from SoFCRenderMaterial:
    /// external backends raymarch the shapes' closed volume as a
    /// water-spray scattering medium instead of rendering the geometry.
    bool fountain;
    float fountaindensity;
    float fountaindetail;
    float fountainspeed;
    /// Light-source body flag/parameters captured from
    /// SoFCRenderMaterial: external backends render the shapes unshaded
    /// at their diffuse color, feed them to the bloom pass and shine
    /// them as an unshadowed point light (bulb, sun disc).
    bool lightsource;
    float lightintensity;
    float lightrange;
    bool lightshadow;
    bool lightshadowext;
    /// User "material"-stage shader captured from a scene
    /// SoShaderProgram node in this cache (docs/RenderDebug.md §6);
    /// only external backends consume it. Pointer identity is the
    /// draw-batch key (a re-capture makes a new translation).
    std::shared_ptr<const Render::UserShader> usershader;
    float polygonoffsetunits;
    float polygonoffsetfactor;
    int16_t annotation;
    int8_t type;
    int8_t lightmodel;
    int8_t materialbinding;
    int8_t vertexordering;
    int8_t drawstyle;
    /// The display mode the OBJECT is in, and which Class-A style names
    /// its display-mode switch has a child for -- captured from
    /// SoFCOwnDisplayModeElement, which SoFCSwitch writes
    /// (docs/CoinRetirement.md 5.8). What lets the backend resolve a
    /// display style per object per view instead of the traversal
    /// baking one style into the capture. Part of the material because
    /// the material is the batching key: draws whose objects are in
    /// different modes must not merge, since a cell filters on this.
    uint8_t ownstyle;
    uint8_t registeredstyles;
    /// Additive-capture context (docs/CoinRetirement.md 5.9
    /// "Non-standard modes"), captured from SoFCCapturedModeElement
    /// and SoFCModeInterestElement beside the pair above: the interned
    /// mode id tagging an ADDITIVELY traversed subgraph's draws (0 in
    /// the normal flow), the id of the normally traversed mode child
    /// when the interest set names it, and which interest modes the
    /// switch has a child for. Part of the material because the
    /// material is the batching key: a tagged draw must not merge
    /// with the normal flow's.
    uint16_t capturedmode;
    uint16_t traversedmode;
    uint16_t interestbits;
    int8_t polygonoffsetstyle;
    int8_t shadowstyle;
    int8_t depthfunc;
    int8_t partialhighlight;
    int8_t selectstyle;
    int8_t shapetype;
    bool depthtest;
    bool depthwrite;
    bool depthclamp;
    bool transptexture;
    bool pervertexcolor;
    bool culling;
    bool twoside;
    bool outline;
    bool resetclip;
    /// Captured from Gui::SoSkipBoundingGroup: the draws made under one
    /// are a navigation gizmo (the axis cross, the rotation-centre
    /// sphere), not scene geometry, and Coin leaves them out of the
    /// scene bounding box whenever the caller asks for exclusion
    /// (SoSkipBoundingBoxElement, View3DInventorViewer::getSceneBoundBox).
    /// The flag carries that statement to the external backends, whose
    /// scene bounds are a min/max over the published draws and have no
    /// traversal left to read the group in.
    bool skipbounds;

    TextureMatrixMap texturematrices;
    TextureMap textures;
    /// Unit-0 SoBumpMap of triangle draws, kept out of `textures`: the
    /// GL renderer GLRenders every `textures` entry with its unit set,
    /// which is wrong for a bump map node (only external backends
    /// consume this).
    TextureMap bumpmaps;
    /// Unit-0 emissive/occlusion/metallic-roughness material maps
    /// (SoFCRenderTexture) of triangle draws, kept out of `textures`
    /// like the bump map (only external backends consume these).
    TextureMap emissivemaps;
    TextureMap occlusionmaps;
    TextureMap metallicroughnessmaps;
    /// Per-face texture PALETTE of triangle draws (SoFCRenderTexture
    /// nodes whose slot is FACE), keyed by the LAYER each one occupies
    /// rather than by a texture unit: they are not units, they are the
    /// images one draw puts on its individual faces, and the backend
    /// uploads them as the layers of a single array texture. Layer 0 is
    /// the untextured face and never has an entry. Only external
    /// backends consume these.
    TextureMap facetextures;
    /// One layer index per FACE, naming an entry of the map above
    /// (0 = untextured). Captured from SoFCRenderMaterial like the
    /// finish indices, and baked into the per-vertex material stream by
    /// the shape traversal.
    COWVector<int32_t> facetextureindices;
    /// Millimetres of object space per tile of those images, or <= 0 to
    /// lay them out on the mesh's own texture coordinates.
    float facetexscale;
    NodeInfoArray lights;
    NodeInfoArray clippers;
    NodeInfoArray autozoom;

    // Exported on its own: _Material is nested in SoFCRenderCache, and a
    // nested class is not carried out of the DLL by the enclosing class's
    // export, so RenderCacheMapBench_tests_run cannot link this without it.
    GuiExport void init(SoState * state = nullptr);

    bool isOnTop() const {
      return order > 0 || annotation > 0;
    }

    bool hasLinePattern() const {
      return (linepattern & 0xffff) != 0xffff;
    }

    /// The creation serial of a shared, immutable cache object, or 0
    /// for none -- which keeps a null ordering first, where the null
    /// pointer used to.
    template<class T>
    static inline std::uint64_t serialOf(const std::shared_ptr<const T> &p) {
      return Render::cacheSerialOf(p.get());
    }

    inline bool operator<(const _Material &other) const {
      if (order < other.order) return true;
      if (order > other.order) return false;
      if (annotation < other.annotation) return true;
      if (annotation > other.annotation) return false;
      if (clippers < other.clippers) return true;
      if (clippers > other.clippers) return false;
      if (autozoom < other.autozoom) return true;
      if (autozoom > other.autozoom) return false;
      if (type < other.type) return true;
      if (type > other.type) return false;
      if (depthtest < other.depthtest) return true;
      if (depthtest > other.depthtest) return false;
      if (depthclamp < other.depthclamp) return true;
      if (depthclamp > other.depthclamp) return false;
      if (depthfunc < other.depthfunc) return true;
      if (depthfunc > other.depthfunc) return false;
      if (depthwrite < other.depthwrite) return true;
      if (depthwrite > other.depthwrite) return false;
      if (selectstyle < other.selectstyle) return true;
      if (selectstyle > other.selectstyle) return false;
      // Compared out here, not in the Triangle branch: a gizmo is as
      // often lines (the axis cross) as it is faces.
      if (skipbounds < other.skipbounds) return true;
      if (skipbounds > other.skipbounds) return false;
      if (this->type == Triangle) {
        if (shapetype < other.shapetype) return true;
        if (shapetype > other.shapetype) return false;
        if (lights < other.lights) return true;
        if (lights > other.lights) return false;
        if (textures < other.textures) return true;
        if (textures > other.textures) return false;
        if (bumpmaps < other.bumpmaps) return true;
        if (bumpmaps > other.bumpmaps) return false;
        if (emissivemaps < other.emissivemaps) return true;
        if (emissivemaps > other.emissivemaps) return false;
        if (occlusionmaps < other.occlusionmaps) return true;
        if (occlusionmaps > other.occlusionmaps) return false;
        if (metallicroughnessmaps < other.metallicroughnessmaps) return true;
        if (metallicroughnessmaps > other.metallicroughnessmaps) return false;
        if (shadowstyle < other.shadowstyle) return true;
        if (shadowstyle > other.shadowstyle) return false;
        if (diffuse < other.diffuse) return true;
        if (diffuse > other.diffuse) return false;
        if (facecolor < other.facecolor) return true;
        if (facecolor > other.facecolor) return false;
        if (linecolor < other.linecolor) return true;
        if (linecolor > other.linecolor) return false;
        if (pervertexcolor < other.pervertexcolor) return true;
        if (pervertexcolor > other.pervertexcolor) return false;
        if (ambient < other.ambient) return true;
        if (ambient > other.ambient) return false;
        if (emissive < other.emissive) return true;
        if (emissive > other.emissive) return false;
        if (specular < other.specular) return true;
        if (specular > other.specular) return false;
        if (shininess < other.shininess) return true;
        if (shininess > other.shininess) return false;
        if (ambients < other.ambients) return true;
        if (ambients > other.ambients) return false;
        if (emissives < other.emissives) return true;
        if (emissives > other.emissives) return false;
        if (speculars < other.speculars) return true;
        if (speculars > other.speculars) return false;
        if (shininesses < other.shininesses) return true;
        if (shininesses > other.shininesses) return false;
        if (metallic < other.metallic) return true;
        if (metallic > other.metallic) return false;
        if (roughness < other.roughness) return true;
        if (roughness > other.roughness) return false;
        if (metallics < other.metallics) return true;
        if (metallics > other.metallics) return false;
        if (roughnesses < other.roughnesses) return true;
        if (roughnesses > other.roughnesses) return false;
        if (finish < other.finish) return true;
        if (finish > other.finish) return false;
        if (finishpitch < other.finishpitch) return true;
        if (finishpitch > other.finishpitch) return false;
        if (finishdepth < other.finishdepth) return true;
        if (finishdepth > other.finishdepth) return false;
        if (finishangle < other.finishangle) return true;
        if (finishangle > other.finishangle) return false;
        // Pointer identity is what says two draws SHARE a palette: it is
        // immutable once published and one node makes one. The ORDER,
        // though, is by creation serial and not by address -- an address
        // is different in every run, and a draw order that follows it
        // makes the picture differ wherever two draws contend for a
        // pixel at equal depth (Render::CacheSerial).
        if (serialOf(finishpalette) < serialOf(other.finishpalette)) return true;
        if (serialOf(finishpalette) > serialOf(other.finishpalette)) return false;
        if (finishindices < other.finishindices) return true;
        if (finishindices > other.finishindices) return false;
        if (serialOf(framepalette) < serialOf(other.framepalette)) return true;
        if (serialOf(framepalette) > serialOf(other.framepalette)) return false;
        if (frameindices < other.frameindices) return true;
        if (frameindices > other.frameindices) return false;
        if (facetextures < other.facetextures) return true;
        if (facetextures > other.facetextures) return false;
        if (facetextureindices < other.facetextureindices) return true;
        if (facetextureindices > other.facetextureindices) return false;
        if (facetexscale < other.facetexscale) return true;
        if (facetexscale > other.facetexscale) return false;
        if (water < other.water) return true;
        if (water > other.water) return false;
        if (waterdensity < other.waterdensity) return true;
        if (waterdensity > other.waterdensity) return false;
        if (glass < other.glass) return true;
        if (glass > other.glass) return false;
        if (glassior < other.glassior) return true;
        if (glassior > other.glassior) return false;
        if (glassdensity < other.glassdensity) return true;
        if (glassdensity > other.glassdensity) return false;
        if (glassroughness < other.glassroughness) return true;
        if (glassroughness > other.glassroughness) return false;
        if (cloud < other.cloud) return true;
        if (cloud > other.cloud) return false;
        if (clouddensity < other.clouddensity) return true;
        if (clouddensity > other.clouddensity) return false;
        if (clouddetail < other.clouddetail) return true;
        if (clouddetail > other.clouddetail) return false;
        if (cloudspeed < other.cloudspeed) return true;
        if (cloudspeed > other.cloudspeed) return false;
        if (fire < other.fire) return true;
        if (fire > other.fire) return false;
        if (fireintensity < other.fireintensity) return true;
        if (fireintensity > other.fireintensity) return false;
        if (firedetail < other.firedetail) return true;
        if (firedetail > other.firedetail) return false;
        if (firespeed < other.firespeed) return true;
        if (firespeed > other.firespeed) return false;
        if (fountain < other.fountain) return true;
        if (fountain > other.fountain) return false;
        if (fountaindensity < other.fountaindensity) return true;
        if (fountaindensity > other.fountaindensity) return false;
        if (fountaindetail < other.fountaindetail) return true;
        if (fountaindetail > other.fountaindetail) return false;
        if (fountainspeed < other.fountainspeed) return true;
        if (fountainspeed > other.fountainspeed) return false;
        if (lightsource < other.lightsource) return true;
        if (lightsource > other.lightsource) return false;
        if (lightintensity < other.lightintensity) return true;
        if (lightintensity > other.lightintensity) return false;
        if (lightrange < other.lightrange) return true;
        if (lightrange > other.lightrange) return false;
        if (lightshadow < other.lightshadow) return true;
        if (lightshadow > other.lightshadow) return false;
        if (lightshadowext < other.lightshadowext) return true;
        if (lightshadowext > other.lightshadowext) return false;
        if (serialOf(usershader) < serialOf(other.usershader)) return true;
        if (serialOf(usershader) > serialOf(other.usershader)) return false;
        if (lightmodel < other.lightmodel) return true;
        if (lightmodel > other.lightmodel) return false;
        if (vertexordering < other.vertexordering) return true;
        if (vertexordering > other.vertexordering) return false;
        if (outline < other.outline) return true;
        if (outline > other.outline) return false;
        if (culling < other.culling) return true;
        if (culling > other.culling) return false;
        if (twoside < other.twoside) return true;
        if (twoside > other.twoside) return false;
        if (drawstyle < other.drawstyle) return true;
        if (drawstyle > other.drawstyle) return false;
        if (ownstyle < other.ownstyle) return true;
        if (ownstyle > other.ownstyle) return false;
        if (registeredstyles < other.registeredstyles) return true;
        if (registeredstyles > other.registeredstyles) return false;
        if (capturedmode < other.capturedmode) return true;
        if (capturedmode > other.capturedmode) return false;
        if (traversedmode < other.traversedmode) return true;
        if (traversedmode > other.traversedmode) return false;
        if (interestbits < other.interestbits) return true;
        if (interestbits > other.interestbits) return false;
        if (polygonoffsetstyle < other.polygonoffsetstyle) return true;
        if (polygonoffsetstyle > other.polygonoffsetstyle) return false;
        if (polygonoffsetfactor < other.polygonoffsetfactor) return true;
        if (polygonoffsetfactor > other.polygonoffsetfactor) return false;
        if (polygonoffsetunits < other.polygonoffsetunits) return true;
        if (polygonoffsetunits > other.polygonoffsetunits) return false;
        if (outline) {
          if (linewidth < other.linewidth) return true;
          if (linewidth > other.linewidth) return false;
          if (pointsize < other.pointsize) return true;
          if (pointsize > other.pointsize) return false;
        }
        // no need to differentiate texture matrices. Its only used for merging to
        // upper hierarchy
        // if (texturematrices < other.texturematrices) return true;
        // if (texturematrices > other.texturematrices) return false;
      } else {
        if (diffuse < other.diffuse) return true;
        if (diffuse > other.diffuse) return false;
        if (pervertexcolor < other.pervertexcolor) return true;
        if (pervertexcolor > other.pervertexcolor) return false;
        if (this->type == Line) {
          if (linewidth < other.linewidth) return true;
          if (linewidth > other.linewidth) return false;
          if (linepattern < other.linepattern) return true;
          if (linepattern > other.linepattern) return false;
        } else {
          if (pointsize < other.pointsize) return true;
          if (pointsize > other.pointsize) return false;
        }
      }
      return overrideflags.to_ulong() < other.overrideflags.to_ulong();
    }
  };

  struct Material : _Material {
  };


  typedef Gui::SoFCSelectionRoot::NodeKey CacheKey;
  typedef std::shared_ptr<CacheKey> CacheKeyPtr;

  struct CacheKeyHasher {
    std::size_t operator()(const CacheKeyPtr &key) const {
      if (!key)
        return 0;
      return key->hash();
    }
    bool operator()(const CacheKeyPtr &a, const CacheKeyPtr &b) const {
      if (a == b)
        return true;
      if (!a || !b)
        return false;
      return *a == *b;
    }
  };

  typedef std::unordered_set<CacheKeyPtr, CacheKeyHasher, CacheKeyHasher> CacheKeySet;

  struct VertexCacheEntry {
    VertexCacheEntry()
      : mergecount(0)
      , skipcount(0)
      , partidx(-1)
      , identity(true)
      , resetmatrix(false)
    {}

    VertexCacheEntry(SoFCVertexCache * c,
                     const SbMatrix &m,
                     bool iden,
                     bool reset,
                     const CacheKeyPtr &k)
      : key(k)
      , cache(c)
      , mergecount(0)
      , skipcount(0)
      , partidx(-1)
      , identity(iden)
      , resetmatrix(reset)
    {
      if (!iden) matrix = m;
    }

    /// Takes \a other's placement but a different vertex cache, so it
    /// deliberately does not inherit the memoized bounding box: the box
    /// belongs to the cache it was measured from. The flatten also
    /// patches the matrix of an entry built this way, right after
    /// building it, which the same omission keeps sound.
    VertexCacheEntry(SoFCVertexCache * c,
                     const VertexCacheEntry & other,
                     const CacheKeyPtr &k)
      : key(k)
      , cache(c)
      , mergecount(other.mergecount)
      , skipcount(other.skipcount)
      , partidx(other.partidx)
      , identity(other.identity)
      , resetmatrix(other.resetmatrix)
      , incomplete(other.incomplete)
    {
      if (!other.identity)
        matrix = other.matrix;
    }

    VertexCacheEntry(const VertexCacheEntry &other)
      : key(other.key)
      , cache(other.cache)
      , mergecount(other.mergecount)
      , skipcount(other.skipcount)
      , partidx(other.partidx)
      , identity(other.identity)
      , resetmatrix(other.resetmatrix)
      , incomplete(other.incomplete)
      , bboxmemo(other.bboxmemo)
      , bboxfor(other.bboxfor)
    {
      if (!identity)
        this->matrix = other.matrix;
    }

    /** This entry's vertex cache, bounded under this entry's transform.
     *
     * A publish asks for it twice over: once building the draw entries,
     * and once translating them for the backend. It is a pure function
     * of the cache and the placement, so the second ask is answered from
     * the first, and an entry copied wholesale by the incremental
     * flatten carries the answer over from the publish that computed it.
     *
     * The memo is stamped with the cache it was measured from, so the
     * paths that swap a cache into an existing entry -- draw-call
     * merging, and the partial caches the highlight build makes -- get a
     * fresh measurement without having to remember to ask for one.
     */
    const SbBox3f & getBoundingBox() const;

    CacheKeyPtr key;
    Gui::CoinPtr<SoFCVertexCache> cache;
    int mergecount;
    int skipcount;
    int partidx;
    SbMatrix matrix;
    bool identity;
    bool resetmatrix;
    /// The cache this entry was collected from had a sibling drawable
    /// deferred by the publish's capture budget (isIncompleteHere).
    /// Element-gate input (docs/SceneStreaming.md #13b): it is what
    /// tells "this object's companion drawable has not arrived yet"
    /// apart from "the display mode legitimately omits it". Preserved
    /// verbatim by every entry copy up the merge/flatten/splice chain.
    bool incomplete = false;

  private:
    mutable SbBox3f bboxmemo;
    /// What bboxmemo was measured from, and so whether it describes the
    /// cache this entry holds now.
    mutable const SoFCVertexCache *bboxfor {nullptr};
  };

  typedef SbFCVector<VertexCacheEntry> VertexCacheArray;

#ifdef FC_COW_MEM_TRACE
  typedef boost::container::flat_map<Material,
                                     VertexCacheArray,
                                     std::less<Material>,
                                     SoFCAllocator<std::pair<Material, VertexCacheArray> > > VertexCacheMap;
#else
  typedef boost::container::flat_map<Material, VertexCacheArray> VertexCacheMap;
#endif

  /** One direct child of a render cache: either a nested cache or a
   * shape's vertex cache, together with the transform and material it was
   * captured under.
   *
   * This is the unit a publish changes. The traversal that rebuilds a
   * scene cache prunes at every separator whose cache is still valid and
   * hands the existing object back to the parent, so the children of two
   * consecutive scene caches are mostly the same objects
   * (docs/IncrementalPublish.md §3) -- which is what makes a per-child
   * slice of the flattened map the natural thing to splice (§5).
   *
   * Note that the transform and the material live here, in the parent,
   * and not in the child: the same child cache held under a moved
   * transform is a different contribution, so an unchanged child pointer
   * on its own does not mean an unchanged entry.
   */
  struct CacheEntry {
    Gui::CoinPtr<SoFCRenderCache> cache;
    Gui::CoinPtr<SoFCVertexCache> vcache;
    Material material;
    SbMatrix matrix;
    bool resetmatrix;
    bool identity;

    CacheEntry(const SbMatrix & m,
               bool iden, bool reset,
               SoFCRenderCache * c,
               SoFCVertexCache *vc)
      :cache(c), vcache(vc), resetmatrix(reset), identity(iden)
    {
      if (!identity) this->matrix = m;
    }
  };

  SoFCRenderCache(SoState * state, SoNode *node, SoFCRenderCache *prev = nullptr);
  virtual ~SoFCRenderCache();

  static void initClass();
  static void cleanup();

  static long getCacheEntryCount();

  SbFCUniqueId getNodeId() const;

  virtual SbBool isValid(const SoState * state) const;

  SbBool isEmpty() const;

  /** Whether a shape below this cache kept a stale vertex cache because
   * the publish's capture budget ran out (Render CaptureBudgetMS). An
   * incomplete cache renders fine -- every child entry is present, one
   * of them a publish old -- but it must not be reused by the next
   * traversal, or the pruning would freeze the stale child in for good:
   * the manager treats it like a node-id mismatch. Sticky by design;
   * the replacement cache built by the follow-up publish starts clean.
   */
  bool isIncomplete() const;
  void setIncomplete();

  /** Whether the deferral happened DIRECTLY under this cache: one of
   * this cache's own shape drawables kept a stale cache or stayed out
   * of the frame. Narrower than isIncomplete(), which the defer sets on
   * the whole open ancestor stack: this one names the innermost cache
   * only, so collecting it onto the cache's own entries
   * (VertexCacheEntry::incomplete) marks just the affected object's
   * drawables and not everything the publish walked.
   */
  bool isIncompleteHere() const;
  void setIncompleteHere();

  /// Whether this cache was opened at an SoFCSelectionRoot -- the
  /// object boundary the entry keys (and so DrawCall::objectKey) are
  /// built from. The defer walk marks incomplete-here up to and
  /// including the nearest such cache, no further: the mark must cover
  /// the whole object and only the object.
  bool isSelectionRoot() const;

  void resetActionStateStackDepth();

  void addTexture(SoState * state, const SoNode * texture);
  void addTextureTransform(SoState * state, const SoNode *);
  void addBumpMap(SoState * state, const SoNode * bumpmap);

  void addRenderMaterial(SoState * state, const SoNode * material);
  void addRenderTexture(SoState * state, const SoNode * texture);
  /// Attach a user "material"-stage shader program (a scene
  /// SoShaderProgram node translated by the cache manager,
  /// docs/RenderDebug.md §6) to the shapes captured after it in this
  /// cache — the SoFCRenderMaterial placement rules apply (same cache
  /// as the shapes, no parent-to-child merge).
  void setUserShader(SoState * state,
                     std::shared_ptr<const Render::UserShader> shader);

  void addClipPlane(SoState * state, const SoClipPlane * light);

  void addAutoZoom(SoState * state, const Gui::SoAutoZoomTranslation * node);

  void addLight(SoState * state, const SoNode * light);

  void setLightModel(SoState *state, const SoLightModel *);

  void setMaterial(SoState *state, const SoMaterial *);
  void setMaterial(SoState *state, const SoVRMLMaterial *);
  void setBaseColor(SoState *state, const SoNode *, const SoMFColor &);

  void setDepthBuffer(SoState *state, const SoDepthBuffer *);

  const VertexCacheMap & getVertexCaches(bool canmerge, int depth=0);

  /// The direct child caches, in the order the traversal added them.
  const SbFCVector<CacheEntry> & getChildCaches() const;

  /** The cache this one was built to replace, released to the caller.
   *
   * A rebuilt cache is the same node's cache one publish later, so the
   * two together are a diff of what that node changed
   * (docs/IncrementalPublish.md §5). The link is held only until someone
   * takes it — which a publish does as the cache closes and its children
   * are complete — so no cache keeps a previous generation alive beyond
   * the traversal that replaced it.
   */
  Gui::CoinPtr<SoFCRenderCache> takePreviousCache();

  /// Keep \a prev alive until this cache's flatten has taken its map to
  /// splice from (docs/IncrementalPublish.md §5), along with \a match:
  /// which child of \a prev each of this cache's children is, or -1 for
  /// one \a prev did not hold. The flatten copies a child's slice of the
  /// map only where \a match says the previous publish already produced
  /// it, and it drops both again, so they last one publish and not the
  /// life of the cache.
  ///
  /// The match is passed in rather than worked out here because the
  /// change set has just worked it out, for the same pair of caches and
  /// by the same test (ScenePublishDelta::lastMatch()).
  void setSpliceSource(SoFCRenderCache *prev, const SbFCVector<int> &match);

  enum HighlightFlag {
    PreselectHighlight = 1,
    CheckIndices = 2,
    WholeOnTop = 4,
    AltGroup = 8,
  };
  VertexCacheMap buildHighlightCache(SbFCMap<int, Gui::CoinPtr<SoFCVertexCache> > &sharedcache,
                                     int order,
                                     const SoDetail * detail,
                                     uint32_t color,
                                     int flags = 0);

  /** The user-shader-override variant of buildHighlightCache
   * (docs/RenderDebug.md §6.5): the whole object's original geometry and
   * materials untouched — no highlight-index cache substitution (those
   * carry no usable normals) and no color override. Only the draw order
   * and depth func change, so the entries can replace the key-suppressed
   * base draws in place.
   *
   * With a face detail (Scope=Element) the map holds just that face's
   * triangles out of the original cache (partial-index rendering, real
   * normals). These entries are partial, so the base draw stays; a small
   * negative polygon offset makes the face win the depth contest over
   * its coincident base copy on draw-order-agnostic backends.
   */
  VertexCacheMap buildWholeCacheMap(int order, const SoDetail * detail = nullptr);

  void open(SoState * state,
            int selectstyle = Material::Full,
            bool initmaterial = true);
  void close(SoState * state);

  void resetNode();

  void beginChildCaching(SoState * state, SoFCRenderCache * cache);
  void beginChildCaching(SoState * state, SoFCVertexCache * cache);

  void endChildCaching(SoState * state, SoFCRenderCache * cache);
  void endChildCaching(SoState * state, SoFCVertexCache * vcache);

  void addChildCache(SoState * state, SoFCRenderCache * cache);
  void addChildCache(SoState * state, SoFCVertexCache * cache);

  void increaseRenderingOrder(SoState *state, int priority=0);
  void decreaseRenderingOrder(SoState *state, int priority=0);

  /// Mark (or unmark) everything cached from here on as excluded from
  /// the scene bounding box -- Material::skipbounds. The manager owns
  /// the nesting count; this is the plain set the flag needs.
  void setSkipBounds(SoState *state, SbBool skip);

  const char * getRenderStatistics() const;

  void resetMatrix(SoState *state);

  const SbBox3f & getBoundingBox() const;

  void checkState(SoState *state);

private:
  friend class SoFCRenderCacheP;
  SoFCRenderCacheP * pimpl;

  SoFCRenderCache(const SoFCRenderCache & rhs); // N/A
  SoFCRenderCache & operator = (const SoFCRenderCache & rhs); // N/A

  static long CacheEntryCount;
  static long CacheEntryFreeCount;
};

// support for CoinPtr<SoFCRenderCache>
inline void intrusive_ptr_add_ref(SoFCRenderCache * obj) { obj->ref(); }
inline void intrusive_ptr_release(SoFCRenderCache * obj) { obj->unref(); }

#endif // FC_RENDERCACHE_H
// vim: noai:ts=2:sw=2
