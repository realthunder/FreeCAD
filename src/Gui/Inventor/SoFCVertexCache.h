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
#ifndef FC_SOPEVERTEXCACHE_H
#define FC_SOPEVERTEXCACHE_H

#include <set>
#include <map>
#include <memory>

#include <Inventor/caches/SoCache.h>
#include <Inventor/system/gl.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbVec4f.h>
#include <Inventor/SbVec2f.h>

#include <memory>
#include <string>

#include "../InventorBase.h"
#include "SoFCRenderCache.h"

class SoFCVertexCacheP;
class SoPrimitiveVertex;
class SoPointDetail;
class SoState;
class SbVec3f;
class SbBox3f;

class GuiExport SoFCVertexCache : public SoCache {
  typedef SoCache inherited;
public:
  SoFCVertexCache(SoState * state, SoNode * node, SoFCVertexCache *prev=NULL);
  SoFCVertexCache(SoFCVertexCache & prev);
  SoFCVertexCache(const SbBox3f &bbox); // create a cache for rendering a wireframe cube

  virtual ~SoFCVertexCache();

  enum Arrays {
    NORMAL = 0x01,
    TEXCOORD = 0x02,
    COLOR = 0x04,
    SORTED_ARRAY = 0x08,
    FULL_SORTED_ARRAY = 0x10,
    NON_SORTED_ARRAY = 0x20,
    ALL = (NORMAL|TEXCOORD|COLOR),
    ALL_SORTED = (NORMAL|TEXCOORD|COLOR|SORTED_ARRAY),
    NON_SORTED = (NORMAL|TEXCOORD|NON_SORTED_ARRAY),
  };

  static void initClass();
  static void cleanup();

  /// Read \a node's by-name 'protoNode' field (SoSFNode). A color
  /// variant of a shared tessellation names its base shape node there,
  /// so a fresh cache of the variant can be seeded with the base's
  /// cache: the equality-preserving capture then keeps the geometry
  /// arrays shared and only the baked color array detaches.
  static SoNode * getProtoNode(const SoNode * node);

  /** Worker-emitted vertex cache content (docs/WorkerVertexCache.md).
   *
   * The plain-value mirror of what generatePrimitives + addTriangle/
   * addLine/addPoint would build for one shape node: the first-seen-order
   * deduplicated vertex/normal arrays and the index lists over them. A
   * fill worker emits it off-thread; the landing registers it on the
   * shape node (setPrebuilt), and the next render-cache capture installs
   * it (installPrebuilt) instead of re-running the per-primitive
   * traversal. Uniform-color content only: no baked color array, no
   * texture coordinates -- installPrebuilt() rejects any state that
   * needs them and the caller falls back to the traversal capture.
   */
  struct PrebuiltContent {
    SbFCVector<SbVec3f> vertices;
    SbFCVector<SbVec3f> normals;
    /// Triangle corners, 3 entries per triangle; part offsets come from
    /// the node's partIndex field at close(), same as the traversal.
    SbFCVector<int32_t> triangleindices;
    /// Line segment endpoints, 2 entries per segment...
    SbFCVector<int32_t> lineindices;
    /// ...and per segment the polyline ordinal it belongs to.
    SbFCVector<int32_t> linepartindices;
    /// Point indices, one per rendered point.
    SbFCVector<int32_t> pointindices;
    /// node->getNodeId() at registration; a later touch of the node
    /// voids the entry.
    SbFCUniqueId nodeid = 0;
  };

  /// Register worker-emitted content for \a node (GUI thread; replaces
  /// any previous entry). A null content clears the entry.
  static void setPrebuilt(const SoNode * node,
                          std::shared_ptr<const PrebuiltContent> content);
  /// Consume the registered content for \a node if its stamped node id
  /// still matches, else drop and return null.
  static std::shared_ptr<const PrebuiltContent>
  takePrebuilt(const SoNode * node);

  /** What the registry answered, counted since the last reset.
   *
   * A publish that adopts nothing has to say which way it failed:
   * \a missing means no worker ever registered content for the shape,
   * \a stale means content was registered but the node was touched
   * after the stamp, so the capture could not trust it. Without the
   * split the two are indistinguishable from the outside, and the
   * "0 adopted" line names no cause (docs/WorkerVertexCache.md).
   */
  struct PrebuiltStats {
    /// takePrebuilt() calls -- shapes a capture offered to adopt.
    int requested = 0;
    /// ...of those, ones with no registry entry at all.
    int missing = 0;
    /// ...and ones whose entry was dropped on the node id check.
    int stale = 0;
  };
  static void resetPrebuiltStats();
  static const PrebuiltStats & prebuiltStats();

  /** Install prebuilt content into this cache in place of the
   * per-primitive capture. Call between open() and close() exactly
   * where the traversal would have fed primitives. Returns false --
   * with the cache still empty and open -- when the captured state is
   * outside the prebuilt contract (per-face colors, texture units,
   * bump coords, markers); the caller then continues with the normal
   * traversal capture.
   */
  /// Whether the state captured by open() is inside the prebuilt
  /// contract (uniform color, no texture units, no markers). Only
  /// meaningful between open() and close().
  bool prebuiltApplicable() const;

  /// Which contract clause put this cache outside it, as a short
  /// literal, or null when prebuiltApplicable() is true. The counted
  /// half of the same question the stats above ask of the registry:
  /// a state-side refusal has several distinct causes and the caller
  /// reports which one it hit.
  const char * prebuiltReject() const;

  bool installPrebuilt(const PrebuiltContent & content);

  /// Verify-mode check (Render WorkerVertexCache = 2): compare this
  /// traversal-captured cache against worker-emitted content; on
  /// mismatch returns false and appends a description to \a diff.
  bool comparePrebuilt(const PrebuiltContent & content,
                       std::string & diff) const;

  virtual SbBool isValid(const SoState * state) const;

  void open(SoState * state);
  void close(SoState * state);

  void renderTriangles(SoGLRenderAction *action, const int arrays = ALL, int part = -1, const SbPlane *plane = nullptr);
  void renderLines(SoState * state, const int arrays = ALL, int part = -1, bool noseam = false);
  void renderPoints(SoGLRenderAction * action, const int array = ALL, int part = -1);

  void renderSolids(SoState * state);

  void addTriangles(const std::map<int, int> & faces);
  void addTriangles(const std::set<int> & faces);
  void addTriangles(const SbFCVector<int> & faces = {});
  void addLines(const std::map<int, int> & lines);
  void addLines(const std::set<int> & lines);
  void addLines(const SbFCVector<int> & lines = {});
  void addPoints(const std::map<int, int> & points);
  void addPoints(const std::set<int> & points);
  void addPoints(const SbFCVector<int> & points = {});

  struct MergeMap {
    int mergeid = 0;
    SbFCMap<SbFCVector<SbFCUniqueId>, Gui::CoinPtr<SoFCVertexCache> > map;
    void cleanup();
  };
  Gui::CoinPtr<SoFCVertexCache> merge(bool allownewmerge,
                                      std::shared_ptr<MergeMap> & mergemap,
                                      SoFCRenderCache::VertexCacheArray & entires,
                                      int idx, int & mergecount);

  int getMergeId() const;
  SbFCUniqueId getCacheId() const;

  SoFCVertexCache * checkHighlightIndices(int * indices = nullptr,
                                          bool newcache = true);

  void addTriangle(const SoPrimitiveVertex * v0,
                   const SoPrimitiveVertex * v1,
                   const SoPrimitiveVertex * v2,
                   const int * pointdetailidx = NULL);
  void addLine(const SoPrimitiveVertex * v0,
               const SoPrimitiveVertex * v1);
  void addPoint(const SoPrimitiveVertex * v);

  int getNumVertices(void) const;
  const SbVec3f * getVertexArray(void) const;
  const SbVec3f * getNormalArray(void) const;
  const SbVec4f * getTexCoordArray(void) const;
  const SbVec2f * getBumpCoordArray(void) const;
  const uint8_t * getColorArray(void) const;

  /// Bytes per vertex of the baked material stream below.
  static const int MaterialStride = 12;

  /** Baked per-vertex material stream, MaterialStride bytes per vertex:
   * rgba8 emissive followed by rgb8 specular with the shininess (0..1)
   * quantized into the last byte -- or, when hasPbrMaterial(), the PBR
   * factor pair in those two alpha slots instead -- and then the
   * surface finish palette index in one byte, the projection frame
   * palette index in the next and two reserved after them
   * (hasFinishMaterial()). Present only when the coin fork's
   * extended lazy element carried per-face material arrays whose
   * resolved values actually diverge (SoLazyElementEx), or a per-face
   * PBR appearance carried factors (SoFCPbrElement), or a per-face
   * finish or projection frame carried indices (SoFCFinishElement);
   * null for every uniform-material cache. Only triangle vertices carry values --
   * vertices referenced by lines/points alone hold zeros, and
   * line/point draws never shade with these fields.
   */
  const uint8_t * getMaterialArray(void) const;

  /** Whether that stream's two alpha slots carry the PBR factor pair
   *
   * A per-face PBR appearance (SoFCPbrElement) states a metallic and a
   * roughness per face, and neither has a material field to ride, so
   * they take the stream's spare alpha slots: the metallic where the
   * emissive alpha is otherwise a constant 0xff, the roughness where
   * the shininess sits -- the Phong quantity the PBR shading branch
   * does not read. A consumer must know which reading applies before
   * it shades from the stream.
   */
  SbBool hasPbrMaterial(void) const;

  /** Whether that stream's third slot carries the finish indices
   *
   * A per-face surface finish (SoFCFinishElement) is four numbers per
   * face, so what the stream carries is one index into the draw
   * material's palette of the distinct finishes -- and beside it, in
   * the next byte, an index into its palette of the PROJECTION FRAMES
   * those finishes are laid out in. Either may be the reason the slot
   * exists: a uniformly finished shaft states one finish and a frame
   * per face. False means every vertex reads index 0 in both, which is
   * the material's own finish in its own frame -- the answer an unbound
   * attribute gives too, so a consumer needs this only to decide
   * whether the palettes are worth uploading.
   */
  SbBool hasFinishMaterial(void) const;

  void setFaceColors(const SbFCVector<std::pair<int, uint32_t> > &colors = {});

  int getNumTriangleIndices(void) const;
  const GLint * getTriangleIndices(void) const;
  int32_t getTriangleIndex(const int idx) const;

  SbBool colorPerVertex(void) const;
  SbBool hasTransparency(void) const;
  SbBool hasOpaqueParts(void) const;
  int hasSolid(void) const;
  bool hasFlipNormal() const;
  bool isEmpty() const;
  bool shouldRenderTriangles() const;
  bool shouldGLRender() const;

  uint32_t getFaceColor(int part) const;
  uint32_t getLineColor(int part) const;
  uint32_t getPointColor(int part) const;

  const SbVec4f * getMultiTextureCoordinateArray(const int unit) const;

  int getNumLineIndices(void) const;
  const GLint * getLineIndices(void) const;

  /** Line indices excluding seam lines (the set renderLines(.., noseam=true)
   * draws), built lazily. Returns 0 if the cache has no seam lines, in
   * which case the full line index set already is the no-seam set.
   */
  int getNumNoSeamLineIndices(void) const;
  const GLint * getNoSeamLineIndices(void) const;

  int getNumPointIndices(void) const;
  const GLint * getPointIndices(void) const;

  /** Pin the current generation of every CPU array exposed by the raw
   * pointer accessors above. The pointers point into copy-on-write
   * storage that a later write replaces — e.g. an equality-shared
   * index array detaching under sort_triangles — and keeping the cache
   * itself alive does not keep the superseded generation. Holding the
   * returned token does.
   */
  std::shared_ptr<const void> copyArrayRefs(void) const;

  /** Get the index range of one part (as used for partial rendering, e.g.
   * a single face/edge/point) inside the respective index array. \a start
   * and \a count are in index units. Returns FALSE if \a part is out of
   * range.
   */
  SbBool getTrianglePartRange(int part, int & start, int & count) const;
  SbBool getLinePartRange(int part, int & start, int & count) const;
  SbBool getPointPartRange(int part, int & start, int & count) const;

  /** Part numbers of a partial subset cache (a copy restricted by
   * addTriangles()/addLines()/addPoints(), e.g. a single selected edge):
   * the index ARRAY getters still expose the whole parent array with the
   * restriction recorded here, part by part (ranges via
   * get*PartRange()). Empty when the cache draws its full index array —
   * note a partial POINT subset rebuilds the index array instead and
   * stays empty here.
   */
  const SbFCVector<int> & getPartialTriangleParts() const;
  const SbFCVector<int> & getPartialLineParts() const;
  const SbFCVector<int> & getPartialPointParts() const;

  /** The triangle index ranges renderSolids() draws when only some face
   * parts belong to solids (hasSolid() == 1); 0 when renderSolids() draws
   * the whole triangle set. \a start and \a count are in index units.
   */
  int getNumSolidParts(void) const;
  SbBool getSolidPartRange(int part, int & start, int & count) const;

  SoNode *getNode() const;
  void resetNode();
  SbFCUniqueId getNodeId() const;

  SbFCUniqueId getSelectionNodeId() const;
  void setSelectionNodeId(SbFCUniqueId id);

  SbVec3f getCenter() const;
  const SbBox3f & getBoundingBox() const;
  void getBoundingBox(const SbMatrix * matrix, SbBox3f & bbox) const;

  void getTrianglesBoundingBox(const SbMatrix *matrix, SbBox3f &bbox, int part = -1) const;
  void getLinesBoundingBox(const SbMatrix *matrix, SbBox3f &bbox, int part = -1) const;
  void getPointsBoundingBox(const SbMatrix *matrix, SbBox3f &bbox, int part = -1) const;

  SoFCVertexCache * getWholeCache() const;

  int getNumNonFlatParts(void) const; 
  const int *getNonFlatParts() const;

  int getNumFaceParts() const;

  bool isElementSelectable() const;
  bool allowOnTopPattern() const;

private:
  friend class SoFCVertexCacheP;
  SoFCVertexCacheP * pimpl;

  SoFCVertexCache & operator = (const SoFCVertexCache & rhs) = delete; // N/A

};

// support for CoinPtr
inline void intrusive_ptr_add_ref(SoFCVertexCache * obj) { obj->ref(); }
inline void intrusive_ptr_release(SoFCVertexCache * obj) { obj->unref(); }

#endif // FC_SOVEVERTEXCACHE_H
// vim: noai:ts=2:sw=2
