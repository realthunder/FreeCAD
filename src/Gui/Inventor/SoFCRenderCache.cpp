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

#include "PreCompiled.h"

#include <iostream>
#include <unordered_map>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoTextureEnabledElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoLazyElementEx.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/VRMLnodes/SoVRMLMaterial.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoLineDetail.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/SbBox3f.h>

#include <cstdlib>
#include <Base/Console.h>
#include "../InventorBase.h"
#include "../RenderTiming.h"
#include "../ViewParams.h"
#include "../SoFCUnifiedSelection.h"
#include "SoFCRenderCache.h"
#include "Renderer/Renderer.h"

std::uint64_t Render::cacheSerialOf(const Render::FinishPalette *palette)
{
  return palette ? palette->serial.value : 0;
}

std::uint64_t Render::cacheSerialOf(const Render::FramePalette *palette)
{
  return palette ? palette->serial.value : 0;
}

std::uint64_t Render::cacheSerialOf(const Render::UserShader *shader)
{
  return shader ? shader->serial.value : 0;
}

std::uint64_t Render::cacheSerialOfNode(const void *node)
{
  return Render::CacheSerial::forNode(node);
}
#include "SoFCRenderMaterial.h"
#include "SoFCVertexCache.h"
#include "SoFCDetail.h"
#include "SoFCDiffuseElement.h"
#include "SoFCFaceTextureElement.h"
#include "SoFCFinishElement.h"
#include "SoFCPbrElement.h"
#include "SoFCZoomOffsetElement.h"
#include "SoFCDisplayModeElement.h"
#include "SoFCOwnDisplayModeElement.h"

#include <Gui/ViewProviderLink.h>

#include "ColorDiff/ColorUtils.cpp"

FC_LOG_LEVEL_INIT("Renderer", true, true)

using namespace Gui;

typedef CoinPtr<SoFCVertexCache> VertexCachePtr;
typedef CoinPtr<SoFCRenderCache> RenderCachePtr;
typedef SoFCRenderCache::Material Material;
typedef SoFCRenderCache::VertexCacheEntry VertexCacheEntry;
typedef SoFCRenderCache::VertexCacheArray VertexCacheArray;

typedef SoFCRenderCache::CacheEntry CacheEntry;

static FC_COIN_THREAD_LOCAL std::vector<std::unique_ptr<SoFCRenderCache::VertexCacheMap> > VertexCacheMaps;
static FC_COIN_THREAD_LOCAL std::vector<int> VertexCacheMapCounts;
// Scratch for per-element selection colours, shared by the two places
// a secondary context is applied.
static FC_COIN_THREAD_LOCAL SbFCVector<std::pair<int, uint32_t> > selcolors;

class SoFCRenderCacheP {
public:
  SoFCRenderCacheP()
  {
  }

  ~SoFCRenderCacheP()
  {
    freeCacheMap();
  }

  void freeCacheMap() {
    if (!this->vcachemap)
      return;
    if(VertexCacheMaps.size() < 10
        && this->cachecount < 100000
        && SoFCRenderCache::CacheEntryFreeCount + this->cachecount < 500000)
    {
      SoFCRenderCache::CacheEntryFreeCount += this->cachecount;
      VertexCacheMapCounts.push_back(this->cachecount);
      this->vcachemap->clear();
      VertexCacheMaps.push_back(std::move(this->vcachemap));
    }
    SoFCRenderCache::CacheEntryCount -= this->cachecount;
    this->vcachemap.reset();
  }

  /// Where one child's contribution to vcachemap sits: a run of entries
  /// in one material bucket. A child contributes one run per bucket it
  /// reaches, so the runs of child i are slices[sliceoffsets[i]] up to
  /// slices[sliceoffsets[i+1]] (docs/IncrementalPublish.md §5).
  struct ChildSlice {
    int bucket;
    int start;
    int count;
  };

  void captureMaterial(SoState * state);

  typedef std::unordered_map<SoFCRenderCache::CacheKeyPtr,
                             SoFCRenderCache::CacheKeyPtr,
                             SoFCRenderCache::CacheKeyHasher,
                             SoFCRenderCache::CacheKeyHasher> KeyMap;

  /// Merge one child cache's flattened map into \a vcachemap, which is
  /// the bulk of what a flatten does: a container's whole cost is this,
  /// once per child (docs/IncrementalPublish.md §4b).
  ///
  /// With \a slicesout, appends one ChildSlice per bucket this child
  /// reached, so a later publish can copy the child's entries instead of
  /// deriving them again. Returns false if the merge took a path the
  /// slices cannot describe (a secondary context that rewrites the
  /// material, or a merge mutation that erases entries already placed),
  /// in which case the caller must stop recording for this map.
  bool mergeChildCache(SoFCRenderCache::VertexCacheMap &vcachemap,
                       const CacheEntry &entry,
                       KeyMap &keymap,
                       bool canmerge,
                       int depth,
                       SbFCVector<ChildSlice> *slicesout);

  /// Build this cache's flattened map out of the map \a prevc produced,
  /// deriving only the children that are not the ones it already held
  /// (docs/IncrementalPublish.md §5). \a matchold says which child of
  /// \a prevc each of this cache's children is, -1 for one that is new;
  /// it comes from the change set, which asked exactly this question of
  /// this pair of caches as the cache closed. Children keep their order,
  /// so the result is the map a wholesale merge would have built, entry
  /// for entry. Returns false without touching anything if the
  /// predecessor cannot be spliced from, leaving the caller to merge as
  /// usual.
  bool spliceFrom(SoFCRenderCache *prevc,
                  const SbFCVector<int> &matchold,
                  KeyMap &keymap,
                  bool canmerge,
                  int depth);

  /// Re-derive every child whose run the splice copied and check that it
  /// still produces what was copied. The failure mode of an incremental
  /// publish is a stale sliver -- a child that changed but whose slice
  /// did not -- and it is invisible in the result until someone looks at
  /// that child (docs/IncrementalPublish.md §7). Slower than not
  /// splicing at all; RenderCacheIncremental 2 turns it on.
  void verifySplice(const SbFCVector<int> &matchold, bool canmerge, int depth);

  Material mergeMaterial(const SbMatrix &matrix,
                         bool &identity,
                         const Material &parent,
                         const Material &child);

  void finalizeMaterial(Material & material);

  void addChildCache(SoState *state,
                     SoFCRenderCache * cache,
                     SoFCVertexCache *vcache,
                     bool opencache);

  void checkState(SoState *);

  const SoShapeHintsElement * shapehintselement;
  const SoShadowStyleElement * shadowstyleelement;
  const SoLinePatternElement * linepatternelement;
  const SoLineWidthElement * linewidthelement;
  const SoPointSizeElement * pointsizeelement;
  const SoPolygonOffsetElement * polygonoffsetelement;
  const SoDrawStyleElement * drawstyleelement;
  const SoMaterialBindingElement * materialbindingelement;

  std::unique_ptr<SoFCRenderCache::VertexCacheMap> vcachemap;

  SbFCVector<ChildSlice> slices;
  SbFCVector<int> sliceoffsets;
  /// Whether slices/sliceoffsets describe the current vcachemap. Only a
  /// map recorded under the conditions the splice needs (no draw-call
  /// merging, no secondary selection contexts) sets this.
  bool slicesvalid = false;

  /// The cache this one replaced, kept only until the flatten has taken
  /// its map to splice from. Held instead of released in postSeparator,
  /// so it does not outlive the publish that consumes it.
  CoinPtr<SoFCRenderCache> spliceprev;
  /// Which child of spliceprev each of this cache's children is, worked
  /// out by the change set that diffed the same pair, -1 where none.
  /// Dropped with spliceprev.
  SbFCVector<int> splicematch;

  CoinPtr<SoFCRenderCache> prevcache;
  SbFCVector<CacheEntry> caches;
  SbFCUniqueId nodeid;
  SoFCSelectionRoot *selnode = nullptr;
  int cachehint = 0;

  std::shared_ptr<SoFCVertexCache::MergeMap> mergemap;
  int facecount = 0;
  int cachecount = 0;

#ifdef FCCOIN_TRACE_CACHE_NAME
  SbName nodename;
#endif

  typedef std::vector<std::pair<int, Material>> MStackEntry;
  std::unique_ptr<MStackEntry> mstack;
  int curdepth = 0;

  Material material;
  Material basematerial;
  bool resetmatrix;
  bool resetclip = false;
  // Set when a shape below kept a stale vertex cache under the capture
  // budget; see SoFCRenderCache::isIncomplete().
  bool incomplete = false;
  // Set when that shape is one of THIS cache's own drawables; see
  // SoFCRenderCache::isIncompleteHere().
  bool incompletehere = false;

  static FC_COIN_THREAD_LOCAL SoFCSelectionRoot::Stack RenderCacheStack;
};

SoFCSelectionRoot::Stack SoFCRenderCacheP::RenderCacheStack;
long SoFCRenderCache::CacheEntryCount;
long SoFCRenderCache::CacheEntryFreeCount;

template<class T>
static inline const T * constElement(SoState * state)
{
  // calling SoState::getConstElement() instead of SoElement::getConstElement()
  // to avoid cache dependency
  return static_cast<const T *>(state->getConstElement(T::getClassStackIndex()));
}

#define PRIVATE(obj) ((obj)->pimpl)

SoFCRenderCache::SoFCRenderCache(SoState *state, SoNode *node, SoFCRenderCache *prev)
  : SoCache(state), pimpl(new SoFCRenderCacheP)
{
  if (prev) {
    PRIVATE(this)->mergemap = PRIVATE(prev)->mergemap;
    // Kept until the publish takes it to diff the two (§5); see
    // takePreviousCache().
    PRIVATE(this)->prevcache = prev;
  }

  PRIVATE(this)->nodeid = node->getNodeId();
  if (node && node->isOfType(SoFCSelectionRoot::getClassTypeId())) {
    // DO NOT add reference. The cache will be monitored by a node sensor
    // inside SoFCRenderCacheManager, which is supposed to release the cache if
    // the node is destroyed. So must not add reference here.
    PRIVATE(this)->selnode = static_cast<SoFCSelectionRoot*>(node);
    PRIVATE(this)->cachehint = PRIVATE(this)->selnode->cacheHint.getValue();
  }

#ifdef FCCOIN_TRACE_CACHE_NAME
  PRIVATE(this)->nodename = node->getName();
  if (PRIVATE(this)->nodename.getLength() == 0) {
    auto obj = ViewProviderLink::linkedObjectByNode(node);
    if (obj) {
      std::string name("_");
      name += obj->getNameInDocument();
      PRIVATE(this)->nodename = name.c_str();
    }
  }
#endif
}

SoFCRenderCache::~SoFCRenderCache()
{
  delete pimpl;
}

static const SbMatrix matrixidentity(SbMatrix::identity());

void SoFCRenderCache::initClass()
{
  SO_ENABLE(SoCallbackAction, SoShadowStyleElement);
  SoFCDiffuseElement::initClass();
  SoFCZoomOffsetElement::initClass();
  SoFCPbrElement::initClass();
  SoFCFinishElement::initClass();
  SoFCFaceTextureElement::initClass();
}

void SoFCRenderCache::resetNode()
{
  PRIVATE(this)->selnode = nullptr;
}

void SoFCRenderCache::cleanup()
{
  SoFCDiffuseElement::cleanup();
  SoFCPbrElement::cleanup();
  SoFCFinishElement::cleanup();
  SoFCFaceTextureElement::cleanup();
}

static inline std::bitset<32>
getOverrideFlags(SoState * state)
{
  std::bitset<32> res;
  uint32_t flags = SoOverrideElement::getFlags(state);
  if (flags & SoOverrideElement::AMBIENT_COLOR)
    res.set(Material::FLAG_AMBIENT);
  if (flags & SoOverrideElement::DIFFUSE_COLOR)
    res.set(Material::FLAG_DIFFUSE);
  if (flags & SoOverrideElement::DRAW_STYLE)
    res.set(Material::FLAG_DRAW_STYLE);
  if (flags & SoOverrideElement::EMISSIVE_COLOR)
    res.set(Material::FLAG_EMISSIVE);
  if (flags & SoOverrideElement::LIGHT_MODEL)
    res.set(Material::FLAG_LIGHT_MODEL);
  if (flags & SoOverrideElement::LINE_PATTERN)
    res.set(Material::FLAG_LINE_PATTERN);
  if (flags & SoOverrideElement::LINE_WIDTH)
    res.set(Material::FLAG_LINE_WIDTH);
  if (flags & SoOverrideElement::MATERIAL_BINDING)
    res.set(Material::FLAG_MATERIAL_BINDING);
  if (flags & SoOverrideElement::POINT_SIZE)
    res.set(Material::FLAG_POINT_SIZE);
  if (flags & SoOverrideElement::SHAPE_HINTS)
    res.set(Material::FLAG_SHAPE_HINTS);
  if (flags & SoOverrideElement::SHININESS)
    res.set(Material::FLAG_SHININESS);
  if (flags & SoOverrideElement::SPECULAR_COLOR)
    res.set(Material::FLAG_SPECULAR);
  if (flags & SoOverrideElement::POLYGON_OFFSET)
    res.set(Material::FLAG_POLYGON_OFFSET);
  if (flags & SoOverrideElement::TRANSPARENCY)
    res.set(Material::FLAG_TRANSPARENCY);
  return res;
}

// Capture the array form of ambient/emissive/specular/shininess from
// the coin fork's extended lazy element into the material (empty when
// a field holds only its scalar, and on any traversal the element is
// not installed on). The element is the authority here: it has already
// resolved override and inheritance semantics, exactly like the scalar
// reads next to the call sites, so no per-field flag checks are
// repeated.
static void
captureMaterialArrays(SoFCRenderCache::_Material &m, SoState *state)
{
  m.ambients.reset();
  m.emissives.reset();
  m.speculars.reset();
  m.shininesses.reset();

  const SoLazyElementEx *ex = SoLazyElementEx::getInstance(state);
  if (!ex)
    return;

  auto capture = [](COWVector<uint32_t> &array,
                    const SoLazyElementEx::FieldArray &field) {
    if (field.num <= 1)
      return;
    array.reserve(field.num);
    for (int i = 0; i < field.num; ++i) {
      SbColor c(field.values[i*3], field.values[i*3+1], field.values[i*3+2]);
      array.append(c.getPackedValue(0.0f));
    }
  };
  capture(m.ambients, ex->getAmbientArray());
  capture(m.emissives, ex->getEmissiveArray());
  capture(m.speculars, ex->getSpecularArray());

  const SoLazyElementEx::FieldArray &shininess = ex->getShininessArray();
  if (shininess.num > 1) {
    m.shininesses.reserve(shininess.num);
    for (int i = 0; i < shininess.num; ++i)
      m.shininesses.append(shininess.values[i]);
  }
}

void
SoFCRenderCache::_Material::init(SoState * state)
{
  this->resetclip = false;
  this->skipbounds = false;
  this->depthtest = true;
  this->depthclamp = false;
  this->depthfunc = SoDepthBuffer::LEQUAL;
  this->depthwrite = true;
  this->order = 0;
  this->annotation = 0;
  this->diffuse = 0xff;
  this->facecolor = 0;
  this->linecolor = 0;
  this->ambient = 0xff;
  this->emissive = 0xff;
  this->specular = 0xff;
  this->linewidth = 1;
  this->pointsize = 1;
  this->shininess = 0.f;
  this->metallic = -1.f;
  this->roughness = -1.f;
  this->finish = 0;
  this->finishpitch = 0.f;
  this->finishdepth = 0.f;
  this->finishangle = 0.f;
  this->water = false;
  this->waterdensity = 0.f;
  this->glass = false;
  this->glassior = 0.f;
  this->glassdensity = 0.f;
  this->glassroughness = 0.f;
  this->cloud = false;
  this->clouddensity = 0.f;
  this->clouddetail = 0.f;
  this->cloudspeed = 1.f;
  this->fire = false;
  this->fireintensity = 0.f;
  this->firedetail = 0.f;
  this->fountain = false;
  this->fountaindensity = 0.f;
  this->fountaindetail = 0.f;
  this->fountainspeed = 1.f;
  this->lightsource = false;
  this->lightintensity = 0.f;
  this->lightrange = 0.f;
  this->lightshadow = false;
  this->lightshadowext = false;
  this->usershader.reset();
  this->firespeed = 1.f;
  this->polygonoffsetstyle = 0;
  this->polygonoffsetunits = 0.f;
  this->polygonoffsetfactor = 0.f;
  this->linepattern = 0xffff;
  this->type = 0;
  this->materialbinding = 0;
  this->capturedmode = 0;
  this->traversedmode = 0;
  this->interestbits = 0;
  this->pervertexcolor = false;
  this->transptexture = false;
  this->lightmodel = SoLazyElement::PHONG;
  this->vertexordering = SoLazyElement::CCW;
  this->culling = false;
  this->twoside = false;
  this->drawstyle = 0;
  this->shadowstyle = SoShadowStyleElement::CASTS_SHADOW_AND_SHADOWED; 
  this->ambients.reset();
  this->emissives.reset();
  this->speculars.reset();
  this->shininesses.reset();
  this->metallics.reset();
  this->roughnesses.reset();
  this->finishpalette.reset();
  this->finishindices.reset();
  this->framepalette.reset();
  this->frameindices.reset();
  this->texturematrices.clear();
  this->textures.clear();
  this->bumpmaps.clear();
  this->emissivemaps.clear();
  this->occlusionmaps.clear();
  this->metallicroughnessmaps.clear();
  this->facetextures.clear();
  this->facetextureindices.reset();
  this->facetexscale = 0.0f;
  this->lights.clear();
  this->partialhighlight = 0;
  this->selectstyle = Material::Full;
  this->outline = false;
  this->shapetype = SoShapeHintsElement::UNKNOWN_SHAPE_TYPE;

  if (!state)
    return;

  this->overrideflags = getOverrideFlags(state);

  float t;
  t = SoLazyElement::getTransparency(state, 0);
  this->diffuse = SoLazyElement::getDiffuse(state, 0).getPackedValue(t);

  t = 0.0f;
  this->emissive = SoLazyElement::getEmissive(state).getPackedValue(t);

  this->ambient = SoLazyElement::getAmbient(state).getPackedValue(t);

  this->specular = SoLazyElement::getSpecular(state).getPackedValue(t);

  this->shininess = SoLazyElement::getShininess(state);

  captureMaterialArrays(*this, state);

  this->lightmodel = SoLazyElement::getLightModel(state);

  SoShapeHintsElement::VertexOrdering ordering;
  SoShapeHintsElement::ShapeType shapetype;
  SoShapeHintsElement::FaceType facetype;
  SoShapeHintsElement::get(state, ordering, shapetype, facetype);
  this->shapetype = shapetype;
  this->vertexordering = ordering == SoShapeHintsElement::CLOCKWISE ?
                                          SoLazyElement::CW : SoLazyElement::CCW;
  // this->twoside = ordering != SoShapeHintsElement::UNKNOWN_ORDERING
  //                     && shapetype == SoShapeHintsElement::UNKNOWN_SHAPE_TYPE;
  this->culling = ordering != SoShapeHintsElement::UNKNOWN_ORDERING
                      && shapetype == SoShapeHintsElement::SOLID;
  this->twoside = SoLazyElement::getTwoSidedLighting(state);

  this->materialbinding = SoMaterialBindingElement::get(state);

  this->linepattern = (SoLinePatternElement::getScaleFactor(state) << 16)
                      | SoLinePatternElement::get(state);

  this->linewidth = SoLineWidthElement::get(state);
  if (this->linewidth < 1.0f)
    this->linewidth = 1.0f;

  this->pointsize = SoPointSizeElement::get(state);
  if (this->pointsize < 1.0f)
    this->pointsize = 1.0f;

  SbBool on;
  SoPolygonOffsetElement::Style style;
  SoPolygonOffsetElement::get(state,
                                this->polygonoffsetfactor,
                                this->polygonoffsetunits,
                                style,
                                on);
    if (!on)
      this->polygonoffsetstyle = 0;
    else
      this->polygonoffsetstyle = style;

  this->drawstyle = SoDrawStyleElement::get(state);

  // The object's own display mode, written by its SoFCSwitch. Read
  // straight off the state rather than through _checkMaterial: it is
  // not an overridable material quantity, just where in the tree we are.
  SoFCOwnDisplayModeElement::get(state, this->ownstyle, this->registeredstyles);
  this->capturedmode = SoFCCapturedModeElement::get(state);
  SoFCModeInterestElement::get(state, this->traversedmode, this->interestbits);
}

template<class T>
static inline const T *
_checkMaterial(SoState *state, Material &m, Material::FlagBits flag, const T *element)
{
  if (!m.overrideflags.test(flag)) {
    auto curelement = constElement<T>(state);
    if (element != curelement) {
      // Must not preserve the changed element, as the data inside element may
      // change without changing the element pointer
      //
      // element = curelement;

      m.maskflags.set(flag);
      return curelement;
    }
  }
  return nullptr;
}

void
SoFCRenderCacheP::captureMaterial(SoState * state)
{
  Material & m = this->material;

  if (_checkMaterial(state, m, Material::FLAG_MATERIAL_BINDING, this->materialbindingelement))
    m.materialbinding = SoMaterialBindingElement::get(state);

  if (_checkMaterial(state, m, Material::FLAG_LINE_PATTERN, this->linepatternelement)) {
    m.linepattern = (SoLinePatternElement::getScaleFactor(state) << 16)
                    | SoLinePatternElement::get(state);
  }

  if (_checkMaterial(state, m, Material::FLAG_LINE_WIDTH, this->linewidthelement)) {
    m.linewidth = SoLineWidthElement::get(state);
    if (m.linewidth < 1.0f)
      m.linewidth = 1.0f;
  }

  if (_checkMaterial(state, m, Material::FLAG_POINT_SIZE, this->pointsizeelement)) {
    m.pointsize = SoPointSizeElement::get(state);
    if (m.pointsize < 1.0f)
      m.pointsize = 1.0f;
  }

  if (_checkMaterial(state, m, Material::FLAG_POLYGON_OFFSET, this->polygonoffsetelement)) {
    SbBool on;
    SoPolygonOffsetElement::Style style;
    SoPolygonOffsetElement::get(state,
                                m.polygonoffsetfactor,
                                m.polygonoffsetunits,
                                style,
                                on);
    if (!on)
      m.polygonoffsetstyle = 0;
    else
      m.polygonoffsetstyle = style;
  }

  if (_checkMaterial(state, m, Material::FLAG_DRAW_STYLE, this->drawstyleelement))
    m.drawstyle = SoDrawStyleElement::get(state);

  SoFCOwnDisplayModeElement::get(state, m.ownstyle, m.registeredstyles);
  m.capturedmode = SoFCCapturedModeElement::get(state);
  SoFCModeInterestElement::get(state, m.traversedmode, m.interestbits);

  if (_checkMaterial(state, m, Material::FLAG_SHADOW_STYLE, this->shadowstyleelement))
    m.shadowstyle = SoShadowStyleElement::get(state);

  if (_checkMaterial(state, m, Material::FLAG_SHAPE_HINTS, this->shapehintselement)) {
    m.maskflags.set(Material::FLAG_CULLING);
    m.maskflags.set(Material::FLAG_VERTEXORDERING);
    m.maskflags.set(Material::FLAG_TWOSIDE);
    SoShapeHintsElement::VertexOrdering ordering;
    SoShapeHintsElement::ShapeType shapetype;
    SoShapeHintsElement::FaceType facetype;
    SoShapeHintsElement::get(state, ordering, shapetype, facetype);
    m.shapetype = shapetype;
    m.vertexordering = ordering == SoShapeHintsElement::CLOCKWISE ?
                                            SoLazyElement::CW : SoLazyElement::CCW;
    m.twoside = ordering != SoShapeHintsElement::UNKNOWN_ORDERING
                       && shapetype == SoShapeHintsElement::UNKNOWN_SHAPE_TYPE;
    m.culling = ordering != SoShapeHintsElement::UNKNOWN_ORDERING
                       && shapetype == SoShapeHintsElement::SOLID;
  }
  m.overrideflags |= getOverrideFlags(state);
}

void
SoFCRenderCache::setLightModel(SoState * state, const SoLightModel * lightmode)
{
  PRIVATE(this)->checkState(state);

  if (PRIVATE(this)->material.overrideflags.test(Material::FLAG_LIGHT_MODEL))
    return;
  if (lightmode->isOverride())
    PRIVATE(this)->material.overrideflags.set(Material::FLAG_LIGHT_MODEL);
  PRIVATE(this)->material.maskflags.set(Material::FLAG_LIGHT_MODEL);
  PRIVATE(this)->material.lightmodel = SoLightModelElement::get(state);
}

template<class N, class T>
static inline bool
_testMaterial(SoFCRenderCache::Material &m, N node, T field, int flag, int mask)
{
  if (!(node->*field).isIgnored() && !m.overrideflags.test(flag)) {
    if (node->isOverride())
      m.overrideflags.set(flag);
    m.maskflags.set(mask);
    return true;
  }
  return false;
}

template<class N, class T>
static inline bool
testMaterial(SoFCRenderCache::Material &m, N node, T field, int flag, int mask)
{
  return (node->*field).getNum() && _testMaterial(m, node, field, flag, mask);
}

void
SoFCRenderCache::setMaterial(SoState * state, const SoMaterial * material)
{
  PRIVATE(this)->checkState(state);

  Material & m = PRIVATE(this)->material;
  float t = 0.f;
  if (testMaterial(m, material, &SoMaterial::diffuseColor, Material::FLAG_DIFFUSE, Material::FLAG_DIFFUSE)) {
    m.diffuse &= 0xff;
    m.diffuse |= material->diffuseColor[0].getPackedValue(t) & 0xffffff00;
    SbFCUniqueId id = material->diffuseColor.getNum() > 1 ? material->getNodeId() : 0;
    SoFCDiffuseElement::set(state, &id, NULL);
  }
  if (testMaterial(m, material, &SoMaterial::transparency, Material::FLAG_TRANSPARENCY, Material::FLAG_TRANSPARENCY)) {
    m.diffuse &= 0xffffff00;
    float alpha = SbClamp(1.0f-material->transparency[0], 0.f, 1.f);
    m.diffuse |= (uint8_t)(alpha * 255);
    SbFCUniqueId id = material->transparency.getNum() > 1 ? material->getNodeId() : 0;
    SoFCDiffuseElement::set(state, NULL, &id);
  }
  if (testMaterial(m, material, &SoMaterial::ambientColor, Material::FLAG_AMBIENT, Material::FLAG_AMBIENT))
    m.ambient = material->ambientColor[0].getPackedValue(t);
  if (testMaterial(m, material, &SoMaterial::emissiveColor, Material::FLAG_EMISSIVE, Material::FLAG_EMISSIVE))
    m.emissive = material->emissiveColor[0].getPackedValue(t);
  if (testMaterial(m, material, &SoMaterial::specularColor, Material::FLAG_SPECULAR, Material::FLAG_SPECULAR))
    m.specular = material->specularColor[0].getPackedValue(t);
  if (testMaterial(m, material, &SoMaterial::shininess, Material::FLAG_SHININESS, Material::FLAG_SHININESS))
    m.shininess = material->shininess[0];

  // this runs in the node's post callback, after its doAction() updated
  // the lazy element, so the element already holds the array form of
  // whatever this node (or an override above it) contributed
  captureMaterialArrays(m, state);
}

void
SoFCRenderCache::setMaterial(SoState * state, const SoVRMLMaterial * material)
{
  PRIVATE(this)->checkState(state);

  Material & m = PRIVATE(this)->material;
  float t = 0.f;
  if (_testMaterial(m, material, &SoVRMLMaterial::diffuseColor,
        Material::FLAG_DIFFUSE, Material::FLAG_DIFFUSE)) {
    m.diffuse &= 0xff;
    m.diffuse |= material->diffuseColor.getValue().getPackedValue(t) & 0xffffff00;
    SbFCUniqueId id = 0;
    SoFCDiffuseElement::set(state, &id, NULL);
  }
  if (_testMaterial(m, material, &SoVRMLMaterial::transparency,
        Material::FLAG_TRANSPARENCY, Material::FLAG_TRANSPARENCY)) {
    m.diffuse &= 0xffffff00;
    float alpha = SbClamp(1.0f-material->transparency.getValue(), 0.f, 1.f);
    m.diffuse |= (uint8_t)(alpha * 255);
    SbFCUniqueId id = 0;
    SoFCDiffuseElement::set(state, NULL, &id);
  }

  // Note, VRML uses diffuse as ambient by default
  if (_testMaterial(m, material, &SoVRMLMaterial::diffuseColor,
        Material::FLAG_AMBIENT, Material::FLAG_AMBIENT)) {
    SbColor ambient = material->diffuseColor.getValue();
    if (!material->ambientIntensity.isIgnored())
      ambient *= material->ambientIntensity.getValue();
    m.ambient = ambient.getPackedValue(t);
  }

  if (_testMaterial(m, material, &SoVRMLMaterial::emissiveColor,
        Material::FLAG_EMISSIVE, Material::FLAG_EMISSIVE))
    m.emissive = material->emissiveColor.getValue().getPackedValue(t);
  if (_testMaterial(m, material, &SoVRMLMaterial::specularColor,
        Material::FLAG_SPECULAR, Material::FLAG_SPECULAR))
    m.specular = material->specularColor.getValue().getPackedValue(t);
  if (_testMaterial(m, material, &SoVRMLMaterial::shininess,
        Material::FLAG_SHININESS, Material::FLAG_SHININESS))
    m.shininess = material->shininess.getValue();

  captureMaterialArrays(m, state);
}

void
SoFCRenderCache::setBaseColor(SoState *state, const SoNode *node, const SoMFColor &c)
{
  PRIVATE(this)->checkState(state);
  Material & m = PRIVATE(this)->material;
  if (c.getNum() && !c.isIgnored() && !m.overrideflags.test(Material::FLAG_DIFFUSE)) {
    if (node->isOverride())
      m.overrideflags.set(Material::FLAG_DIFFUSE);
    m.maskflags.set(Material::FLAG_DIFFUSE);
    m.diffuse &= 0xff;
    float t = 0.f;
    m.diffuse |= c[0].getPackedValue(t) & 0xffffff00;
    SbFCUniqueId id = node->getNodeId();
    SoFCDiffuseElement::set(state, &id, NULL);
  }
}

void
SoFCRenderCache::setDepthBuffer(SoState * state, const SoDepthBuffer * node)
{
  PRIVATE(this)->checkState(state);

  Material & m = PRIVATE(this)->material;
  if (!node->test.isIgnored()) {
    m.maskflags.set(Material::FLAG_DEPTH_TEST);
    m.depthtest = node->test.getValue();
  }
  if (!node->write.isIgnored()) {
    m.maskflags.set(Material::FLAG_DEPTH_WRITE);
    m.depthwrite = node->write.getValue();
  }
  if (!node->function.isIgnored()) {
    m.maskflags.set(Material::FLAG_DEPTH_FUNC);
    m.depthfunc = node->function.getValue();
  }
}

static inline bool
canSetMaterial(SoFCRenderCache::Material & res,
               const SoFCRenderCache::Material & parent,
               uint32_t flag, uint32_t mask)
{
  return (flag && parent.overrideflags.test(flag))
      || (!res.maskflags.test(mask) && parent.maskflags.test(mask));
}

template<class T>
static inline void
copyMaterial(SoFCRenderCache::Material &res,
             const SoFCRenderCache::Material &parent,
             T member,
             uint32_t flag, uint32_t mask)
{
  if (canSetMaterial(res, parent, flag, mask)) {
    res.*member = parent.*member;
    res.maskflags.set(mask);
  }
}

void
SoFCRenderCache::increaseRenderingOrder(SoState *state, int priority)
{
  PRIVATE(this)->checkState(state);
  if (priority)
    PRIVATE(this)->material.annotation += 1000 + priority;
  else
    ++PRIVATE(this)->material.annotation;
}

void
SoFCRenderCache::decreaseRenderingOrder(SoState *state, int priority)
{
  PRIVATE(this)->checkState(state);
  if (priority)
    PRIVATE(this)->material.annotation -= 1000 + priority;
  else
    --PRIVATE(this)->material.annotation;
}

void
SoFCRenderCache::setSkipBounds(SoState *state, SbBool skip)
{
  PRIVATE(this)->checkState(state);
  PRIVATE(this)->material.skipbounds = skip ? true : false;
}

SoFCRenderCache::Material
SoFCRenderCacheP::mergeMaterial(const SbMatrix &matrix,
                                bool &identity,
                                const Material &parent,
                                const Material &child)
{
  // merge material from bottom up

  Material res = child;

  if (parent.order > child.order)
    res.order = parent.order;

  if (parent.annotation > child.annotation)
    res.annotation = parent.annotation;

  if (res.selectstyle != Material::Box
      && res.selectstyle != Material::BoxFull
      && res.selectstyle != Material::Unpickable)
    res.selectstyle = parent.selectstyle;

  res.outline |= parent.outline;

  // Inherited downwards like the outline flag: everything under a
  // SoSkipBoundingGroup is out of the scene bounds, however deep.
  res.skipbounds |= parent.skipbounds;

  auto mergeNodeInfo = [&](SoFCRenderCache::NodeInfoArray &thisarray,
                           const SoFCRenderCache::NodeInfoArray &other)
  {
    if (identity)
      thisarray.append(other);
    else if (other.getNum()) {
      for (const auto & info : other.getData()) {
        if (info.resetmatrix)
          thisarray.append(info);
        else {
          SoFCRenderCache::NodeInfo copy = info;
          if (copy.identity)
            copy.matrix = matrix;
          else
            copy.matrix.multLeft(matrix);
          copy.identity = false;
          thisarray.append(copy);
        }
      }
    }
  };
  if (child.resetclip)
    res.resetclip = true;
  else {
    res.clippers = parent.clippers;
    mergeNodeInfo(res.clippers, child.clippers);
  }

  res.autozoom = parent.autozoom;
  auto childzoom = child.autozoom;
  if (childzoom.getNum() && !identity) {
    const auto &info = childzoom.get(0);
    if (!info.resetmatrix) {
      auto copy = info;
      if (copy.identity)
        copy.matrix = matrix;
      else
        copy.matrix.multRight(matrix);
      copy.identity = false;
      childzoom.set(0, copy);
    }
    identity = true;
  }
  mergeNodeInfo(res.autozoom, childzoom);
  
  copyMaterial(res, parent, &Material::depthtest, 0, Material::FLAG_DEPTH_TEST);
  copyMaterial(res, parent, &Material::depthfunc, 0, Material::FLAG_DEPTH_FUNC);
  copyMaterial(res, parent, &Material::depthwrite, 0, Material::FLAG_DEPTH_WRITE);

  if (canSetMaterial(res, parent, Material::FLAG_DIFFUSE, Material::FLAG_DIFFUSE)) {
    res.diffuse &= 0xff;
    res.diffuse |= parent.diffuse & 0xffffff00;
    res.maskflags.set(Material::FLAG_DIFFUSE);
  }

  if (canSetMaterial(res, parent, Material::FLAG_TRANSPARENCY, Material::FLAG_TRANSPARENCY)) {
    res.diffuse &= 0xffffff00;
    res.diffuse |= parent.diffuse & 0xff;
    res.maskflags.set(Material::FLAG_TRANSPARENCY);
  }

  if (canSetMaterial(res, parent, Material::FLAG_LINE_COLOR, Material::FLAG_LINE_COLOR)) {
    res.linecolor = parent.linecolor;
    res.maskflags.set(Material::FLAG_LINE_COLOR);
  }

  if (canSetMaterial(res, parent, Material::FLAG_FACE_COLOR, Material::FLAG_FACE_COLOR)) {
    res.facecolor &= 0xff;
    res.facecolor |= parent.facecolor & 0xffffff00;
    res.maskflags.set(Material::FLAG_FACE_COLOR);
  }

  if (canSetMaterial(res, parent, Material::FLAG_FACE_TRANSPARENCY, Material::FLAG_FACE_TRANSPARENCY)) {
    res.facecolor &= 0xffffff00;
    res.facecolor |= parent.facecolor & 0xff;
    res.maskflags.set(Material::FLAG_FACE_TRANSPARENCY);
  }

  if (res.type == Material::Line || res.outline) {
    copyMaterial(res, parent, &Material::linewidth, Material::FLAG_LINE_WIDTH, Material::FLAG_LINE_WIDTH);
    copyMaterial(res, parent, &Material::linepattern, Material::FLAG_LINE_PATTERN, Material::FLAG_LINE_PATTERN);
    if (res.type == Material::Line)
      return res;
  }

  if (res.type == Material::Point) {
    copyMaterial(res, parent, &Material::pointsize, Material::FLAG_POINT_SIZE, Material::FLAG_POINT_SIZE);
    return res;
  }

  // A user "material"-stage shader inherits down the whole subtree with
  // link material-override semantics: the outer (parent) shader wins over
  // any shader set deeper inside. Lines/points are excluded above so their
  // batching keys stay unaffected (backends swap the mesh program only).
  // Exception: a "particle"-stage shader marks generated particle seed
  // geometry (its own SoFCSelectionRoot cache under a bound target,
  // docs/RenderEngine.md §5.11) — the effect's main program must not
  // recapture the seeds, so the inner particle program survives.
  if (parent.usershader
      && (!res.usershader || res.usershader->stage != "particle"))
    res.usershader = parent.usershader;

  copyMaterial(res, parent, &Material::materialbinding, Material::FLAG_MATERIAL_BINDING, Material::FLAG_MATERIAL_BINDING);

  // the array forms travel with their scalars: when the parent wins a
  // field, its array (possibly empty) replaces the child's too
  if (canSetMaterial(res, parent, Material::FLAG_AMBIENT, Material::FLAG_AMBIENT)) {
    res.ambient = parent.ambient;
    res.ambients = parent.ambients;
    res.maskflags.set(Material::FLAG_AMBIENT);
  }
  if (canSetMaterial(res, parent, Material::FLAG_EMISSIVE, Material::FLAG_EMISSIVE)) {
    res.emissive = parent.emissive;
    res.emissives = parent.emissives;
    res.maskflags.set(Material::FLAG_EMISSIVE);
  }
  if (canSetMaterial(res, parent, Material::FLAG_SPECULAR, Material::FLAG_SPECULAR)) {
    res.specular = parent.specular;
    res.speculars = parent.speculars;
    res.maskflags.set(Material::FLAG_SPECULAR);
  }
  if (canSetMaterial(res, parent, Material::FLAG_SHININESS, Material::FLAG_SHININESS)) {
    res.shininess = parent.shininess;
    res.shininesses = parent.shininesses;
    res.maskflags.set(Material::FLAG_SHININESS);
  }
  copyMaterial(res, parent, &Material::drawstyle, Material::FLAG_DRAW_STYLE, Material::FLAG_DRAW_STYLE);
  copyMaterial(res, parent, &Material::lightmodel, Material::FLAG_LIGHT_MODEL, Material::FLAG_LIGHT_MODEL);
  copyMaterial(res, parent, &Material::shadowstyle, 0, Material::FLAG_SHADOW_STYLE);

  if (canSetMaterial(res, parent, Material::FLAG_POLYGON_OFFSET, Material::FLAG_POLYGON_OFFSET)) {
    res.polygonoffsetstyle = parent.polygonoffsetstyle;
    res.polygonoffsetfactor = parent.polygonoffsetfactor;
    res.polygonoffsetunits = parent.polygonoffsetunits;
    res.maskflags.set(Material::FLAG_POLYGON_OFFSET);
  }

  if (canSetMaterial(res, parent, Material::FLAG_SHAPE_HINTS, Material::FLAG_SHAPE_HINTS)) {
    res.culling = parent.culling;
    res.vertexordering = parent.vertexordering;
    res.twoside = parent.twoside;
    res.shapetype = parent.shapetype;
    res.maskflags.set(Material::FLAG_SHAPE_HINTS);
    res.maskflags.set(Material::FLAG_CULLING);
    res.maskflags.set(Material::FLAG_VERTEXORDERING);
    res.maskflags.set(Material::FLAG_TWOSIDE);
  }
  else {
    copyMaterial(res, parent, &Material::culling, 0, Material::FLAG_CULLING);
    copyMaterial(res, parent, &Material::vertexordering, 0, Material::FLAG_VERTEXORDERING);
    copyMaterial(res, parent, &Material::twoside, 0, Material::FLAG_TWOSIDE);
  }

  if (res.overrideflags.test(Material::FLAG_NO_TEXTURE)
      || parent.overrideflags.test(Material::FLAG_NO_TEXTURE)) {
    res.overrideflags.set(Material::FLAG_NO_TEXTURE);
    res.textures.clear();
    res.bumpmaps.clear();
    res.emissivemaps.clear();
    res.occlusionmaps.clear();
    res.metallicroughnessmaps.clear();
    res.facetextures.clear();
    res.facetextureindices.reset();
    res.texturematrices.clear();
  } else {
    res.texturematrices.combine(parent.texturematrices);
    if (parent.texturematrices.getNum()) {
      for (auto & v : parent.texturematrices.getData()) {
        const SoFCRenderCache::TextureInfo * pinfo = res.textures.get(v.first);
        if (!pinfo) continue;
        SoFCRenderCache::TextureInfo info = * pinfo;
        if (info.identity)
          info.matrix = v.second.matrix;
        else
          info.matrix.multLeft(v.second.matrix);
        info.identity = false;
        res.textures.set(v.first, info);
      }
    }
    res.textures.add(parent.textures, false);
    res.bumpmaps.add(parent.bumpmaps, false);
    res.emissivemaps.add(parent.emissivemaps, false);
    res.occlusionmaps.add(parent.occlusionmaps, false);
    res.metallicroughnessmaps.add(parent.metallicroughnessmaps, false);
    // The face palette merges like the maps above -- the child's own
    // layers win -- but the indices that name them do NOT: a layer
    // index is only meaningful against the palette it was authored
    // with, so an outer node's index array must never be applied to an
    // inner node's images.
    res.facetextures.add(parent.facetextures, false);
  }

  res.lights = parent.lights;
  mergeNodeInfo(res.lights, child.lights);

  if (!res.transptexture && res.textures.getNum()) {
    for (auto & info : res.textures.getData()) {
      if (info.second.transparent) {
        res.transptexture = true;
        break;
      }
    }
  }

  return res;
}

SbFCUniqueId
SoFCRenderCache::getNodeId() const
{
  return PRIVATE(this)->nodeid;
}

bool
SoFCRenderCache::isIncomplete() const
{
  return PRIVATE(this)->incomplete;
}

void
SoFCRenderCache::setIncomplete()
{
  PRIVATE(this)->incomplete = true;
}

bool
SoFCRenderCache::isIncompleteHere() const
{
  return PRIVATE(this)->incompletehere;
}

void
SoFCRenderCache::setIncompleteHere()
{
  PRIVATE(this)->incompletehere = true;
}

bool
SoFCRenderCache::isSelectionRoot() const
{
  return PRIVATE(this)->selnode != nullptr;
}

SbBool
SoFCRenderCache::isValid(const SoState * state) const
{
  return inherited::isValid(state);
}

void
SoFCRenderCache::open(SoState *state, int selectstyle, bool initmaterial)
{
  SoCacheElement::set(state, this);

  if (PRIVATE(this)->selnode) {
    PRIVATE(this)->material.resetclip =
      PRIVATE(this)->selnode->resetClipPlane.getValue() ? true : false;
    if (PRIVATE(this)->selnode->setupColorOverride(state, true))
      initmaterial = true;
  }

  PRIVATE(this)->material.init(initmaterial ? state : nullptr);
  PRIVATE(this)->material.selectstyle = selectstyle;

  if (initmaterial)
    PRIVATE(this)->curdepth = state->getDepth();

  SbBool outline = FALSE;
  if (initmaterial && SoFCDisplayModeElement::showHiddenLines(state, &outline)) {
    PRIVATE(this)->material.outline = outline;
    PRIVATE(this)->material.linewidth = SoLineWidthElement::get(state);
  }

  // Call SoState::getElement() here to force create SoOverrideElement at the
  // current stack level to prevent it being captured in cache, because we want
  // to decouple override settings from child caches.
  state->getElement(SoOverrideElement::getClassStackIndex());

  // Remember the current override flags. If it weren't for the above call to
  // getElement(), this element will be added to our cache dependency.
  //
  // Convert override flags to our own mask, and then reset the flags
  auto flags = getOverrideFlags(state);
  PRIVATE(this)->material.overrideflags = flags;

  if (flags.test(Material::FLAG_AMBIENT))
    SoOverrideElement::setAmbientColorOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_DIFFUSE)) {
    SoOverrideElement::setDiffuseColorOverride(state, NULL, FALSE);
    if (!initmaterial) {
      SbFCUniqueId diffuseid = SoFCDiffuseElement::get(state, nullptr);
      if (diffuseid && diffuseid == getNodeId()) {
        uint32_t diffuse = SoLazyElement::getDiffuse(state, 0).getPackedValue(0.0f);
        PRIVATE(this)->material.diffuse &= 0xff;
        PRIVATE(this)->material.diffuse |= diffuse;
        PRIVATE(this)->material.maskflags.set(Material::FLAG_DIFFUSE);
      }
    }
  }
  if (flags.test(Material::FLAG_SPECULAR))
    SoOverrideElement::setSpecularColorOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_EMISSIVE))
    SoOverrideElement::setEmissiveColorOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_SHININESS))
    SoOverrideElement::setShininessOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_TRANSPARENCY)) {
    SoOverrideElement::setTransparencyOverride(state, NULL, FALSE);
    if (!initmaterial) {
      SbFCUniqueId transpid = 0;
      SoFCDiffuseElement::get(state, &transpid);
      if (transpid && transpid == getNodeId()) {
        float t = SoLazyElement::getTransparency(state, 0);
        SbColor color(0,0,0);
        PRIVATE(this)->material.diffuse &= ~0xff;
        PRIVATE(this)->material.diffuse |= color.getPackedValue(t);
        PRIVATE(this)->material.maskflags.set(Material::FLAG_TRANSPARENCY);
      }
    }
  }
  if (flags.test(Material::FLAG_DRAW_STYLE))
    SoOverrideElement::setDrawStyleOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_LINE_PATTERN))
    SoOverrideElement::setLinePatternOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_LINE_WIDTH))
    SoOverrideElement::setLineWidthOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_POINT_SIZE))
    SoOverrideElement::setPointSizeOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_MATERIAL_BINDING))
    SoOverrideElement::setMaterialBindingOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_POLYGON_OFFSET))
    SoOverrideElement::setPolygonOffsetOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_SHAPE_HINTS))
    SoOverrideElement::setShapeHintsOverride(state, NULL, FALSE);
  if (flags.test(Material::FLAG_LIGHT_MODEL))
    SoOverrideElement::setLightModelOverride(state, NULL, FALSE);

  // Capture current relevant elements to detect change happen inside the
  // current caching group node. When capturing materials, we only capture
  // elements set within the current node.
  PRIVATE(this)->linepatternelement = constElement<SoLinePatternElement>(state);
  PRIVATE(this)->linewidthelement = constElement<SoLineWidthElement>(state);
  PRIVATE(this)->pointsizeelement = constElement<SoPointSizeElement>(state);
  PRIVATE(this)->polygonoffsetelement = constElement<SoPolygonOffsetElement>(state);
  PRIVATE(this)->drawstyleelement = constElement<SoDrawStyleElement>(state);
  PRIVATE(this)->materialbindingelement = constElement<SoMaterialBindingElement>(state);
  PRIVATE(this)->shadowstyleelement = constElement<SoShadowStyleElement>(state);
  PRIVATE(this)->shapehintselement = constElement<SoShapeHintsElement>(state);

  PRIVATE(this)->resetmatrix = false;

  float transp = SoFCDisplayModeElement::getTransparency(state);
  if (transp >= 0.f && !PRIVATE(this)->material.overrideflags.test(Material::FLAG_FACE_TRANSPARENCY)) {
    PRIVATE(this)->material.facecolor &= ~0xff;
    SbColor color(0,0,0);
    PRIVATE(this)->material.facecolor |= color.getPackedValue(transp);
    PRIVATE(this)->material.maskflags.set(Material::FLAG_FACE_TRANSPARENCY);
    if (outline)
      PRIVATE(this)->material.overrideflags.set(Material::FLAG_FACE_TRANSPARENCY);
  }
  if (auto color = SoFCDisplayModeElement::getFaceColor(state)) {
    if (!PRIVATE(this)->material.overrideflags.test(Material::FLAG_FACE_COLOR)) {
      PRIVATE(this)->material.facecolor &= 0xff;
      PRIVATE(this)->material.facecolor |= color->getPackedValue(1.f);
      PRIVATE(this)->material.maskflags.set(Material::FLAG_FACE_COLOR);
      if (outline)
        PRIVATE(this)->material.overrideflags.set(Material::FLAG_FACE_COLOR);
    }
  }
  if (auto color = SoFCDisplayModeElement::getLineColor(state)) {
    if (!PRIVATE(this)->material.overrideflags.test(Material::FLAG_LINE_COLOR)) {
      PRIVATE(this)->material.linecolor = color->getPackedValue(0.f);
      PRIVATE(this)->material.maskflags.set(Material::FLAG_LINE_COLOR);
      if (outline)
        PRIVATE(this)->material.overrideflags.set(Material::FLAG_LINE_COLOR);
    }
  }

  if (PRIVATE(this)->curdepth)
    PRIVATE(this)->basematerial = PRIVATE(this)->material;
}

void
SoFCRenderCache::close(SoState *state)
{
  if (PRIVATE(this)->selnode)
    PRIVATE(this)->selnode->resetColorOverride(state);
  PRIVATE(this)->material.init();
  PRIVATE(this)->mstack.reset();
  PRIVATE(this)->curdepth = 0;
}

class MyMultiTextureMatrixElement : public SoMultiTextureMatrixElement
{
public:
  int getNumUnits() const {
    return SoMultiTextureMatrixElement::getNumUnits();
  }

  const UnitData & getUnitData(const int unit) const {
    return SoMultiTextureMatrixElement::getUnitData(unit);
  }
};

void
SoFCRenderCache::resetActionStateStackDepth()
{
  PRIVATE(this)->curdepth = 0;
}

void
SoFCRenderCache::checkState(SoState *state)
{
  if (PRIVATE(this)->curdepth)
    PRIVATE(this)->checkState(state);
}

void
SoFCRenderCacheP::checkState(SoState *state)
{
  int depth = state->getDepth();
  if (!this->curdepth) {
    this->curdepth = depth;
    this->basematerial = this->material;
    return;
  }
  if (depth == this->curdepth)
    return;
  if (!this->mstack)
    this->mstack.reset(new MStackEntry);
  if (depth > this->curdepth) {
    mstack->emplace_back(this->curdepth, this->material);
    this->curdepth = depth;
  }
  else if (this->mstack->empty()) {
    this->material = this->basematerial;
    this->curdepth = 0;
  }
  else {
    while (this->mstack->size() > 1
          && (*this->mstack)[this->mstack->size()-2].first > depth)
      this->mstack->pop_back();
    this->material = this->mstack->back().second;
    this->curdepth = this->mstack->back().first;
    this->mstack->pop_back();
  }
}

void
SoFCRenderCacheP::addChildCache(SoState * state,
                                SoFCRenderCache * cache,
                                SoFCVertexCache * vcache,
                                bool opencache)
{
  checkState(state);

  auto elem = constElement<SoModelMatrixElement>(state);
  SbMatrix matrix = elem->getModelMatrix();
  bool identity = (matrix == matrixidentity);

  if (opencache) {
    if (!identity) {
      // reset to identity matrix to decouple model transformation from child cache
      static_cast<SoModelMatrixElement*>(
          state->getElement(SoModelMatrixElement::getClassStackIndex()))->init(state);
    }

    // reset textrue matrix to identity
    auto elem = constElement<MyMultiTextureMatrixElement>(state);
    for (int i=0, n=elem->getNumUnits(); i<n; ++i) {
      if (elem->getUnitData(i).textureMatrix != matrixidentity)
        SoMultiTextureMatrixElement::set(state, NULL, i, matrixidentity);
    }
  }

  this->caches.emplace_back(matrix,
                            identity,
                            this->resetmatrix,
                            cache,
                            vcache);
  captureMaterial(state);
  Material & m = this->caches.back().material;
  m = this->material;
}

void
SoFCRenderCache::addChildCache(SoState *state, SoFCRenderCache * cache)
{
  PRIVATE(this)->addChildCache(state, cache, NULL, false);
  this->addCacheDependency(state, cache);
}

void
SoFCRenderCache::addChildCache(SoState *state, SoFCVertexCache * cache)
{
  PRIVATE(this)->addChildCache(state, NULL, cache, false);
  this->addCacheDependency(state, cache);
}

void
SoFCRenderCache::beginChildCaching(SoState *state, SoFCRenderCache * cache)
{
  PRIVATE(this)->addChildCache(state, cache, NULL, true);
}

void
SoFCRenderCache::beginChildCaching(SoState *state, SoFCVertexCache * cache)
{
  PRIVATE(this)->addChildCache(state, NULL, cache, true);
}

void
SoFCRenderCache::endChildCaching(SoState * state, SoFCVertexCache * vcache)
{
  if (vcache->isEmpty()) {
      if (PRIVATE(this)->caches.size()
          && PRIVATE(this)->caches.back().vcache == vcache)
    {
      PRIVATE(this)->caches.pop_back();
    }
  }
  this->addCacheDependency(state, vcache);
}

void
SoFCRenderCache::endChildCaching(SoState * state, SoFCRenderCache * cache)
{
  if (cache->isEmpty()) {
      if (PRIVATE(this)->caches.size()
          && PRIVATE(this)->caches.back().cache == cache)
    {
      PRIVATE(this)->caches.pop_back();
    }
  }
  this->addCacheDependency(state, cache);
}

SbBool
SoFCRenderCache::isEmpty() const
{
  return PRIVATE(this)->caches.empty();
}

const SbBox3f &
SoFCRenderCache::VertexCacheEntry::getBoundingBox() const
{
  if (this->bboxfor != this->cache.get()) {
    this->cache->getBoundingBox(this->identity ? nullptr : &this->matrix,
                                this->bboxmemo);
    this->bboxfor = this->cache.get();
  }
  return this->bboxmemo;
}

const SbFCVector<SoFCRenderCache::CacheEntry> &
SoFCRenderCache::getChildCaches() const
{
  return PRIVATE(this)->caches;
}

CoinPtr<SoFCRenderCache>
SoFCRenderCache::takePreviousCache()
{
  CoinPtr<SoFCRenderCache> prev;
  prev.swap(PRIVATE(this)->prevcache);
  return prev;
}

void
SoFCRenderCache::setSpliceSource(SoFCRenderCache *prev, const SbFCVector<int> &match)
{
  PRIVATE(this)->spliceprev = prev;
  PRIVATE(this)->splicematch = match;
}

class MyMultiTextureImageElement : public SoMultiTextureImageElement
{
public:
  SbBool hasTransparency(const int unit) const {
    return SoMultiTextureImageElement::hasTransparency(unit);
  }
};

void
SoFCRenderCache::addTexture(SoState * state, const SoNode * texture)
{
  PRIVATE(this)->checkState(state);

  int unit = SoTextureUnitElement::get(state);

  TextureInfo info;
  info.setTexture(const_cast<SoNode*>(texture));

  auto elem = constElement<MyMultiTextureImageElement>(state);
  info.transparent = elem->hasTransparency(unit);
  // The merge path (mergeMaterial) derives transptexture when caches
  // nest; a texture applied in the same cache as the shape (e.g. a flat
  // overlay graph) must set it here or the draw never blends.
  if (info.transparent)
    PRIVATE(this)->material.transptexture = true;

  info.identity = true;
  auto melem = constElement<MyMultiTextureMatrixElement>(state);
  if (melem->getNumUnits() > unit) {
    const auto & data = melem->getUnitData(unit);
    if (data.textureMatrix != matrixidentity) {
      info.identity = false;
      info.matrix = data.textureMatrix;
    }
  }

  PRIVATE(this)->material.textures.set(unit, info);
}

void
SoFCRenderCache::addBumpMap(SoState * state, const SoNode * bumpmap)
{
  PRIVATE(this)->checkState(state);

  // SoBumpMap sets its element only during GL rendering, so the node is
  // captured directly (the external backends read its fields); like GL
  // bump mapping it lives on the current texture unit, in practice 0.
  int unit = SoTextureUnitElement::get(state);

  TextureInfo info;
  info.setTexture(const_cast<SoNode*>(bumpmap));
  info.transparent = false;
  info.identity = true;

  PRIVATE(this)->material.bumpmaps.set(unit, info);
}

void
SoFCRenderCache::addRenderMaterial(SoState * state, const SoNode * node)
{
  PRIVATE(this)->checkState(state);

  // SoFCRenderMaterial has no Coin element at all; the node is captured
  // directly for external backends (per-object PBR parameters).
  auto material = static_cast<const Gui::SoFCRenderMaterial *>(node);
  PRIVATE(this)->material.metallic = material->metallic.getValue();
  PRIVATE(this)->material.roughness = material->roughness.getValue();
  // The per-face pair the same node carries (empty unless a PBR
  // appearance states one per face). Copied, not borrowed: a material
  // outlives the traversal that captured it.
  auto capturefactors = [](COWVector<float> &array, const SoMFFloat &field) {
    array.reset();
    const int num = field.getNum();
    if (num <= 1)
      return;
    const float *values = field.getValues(0);
    array.reserve(num);
    for (int i = 0; i < num; ++i)
      array.append(values[i]);
  };
  capturefactors(PRIVATE(this)->material.metallics, material->metallics);
  capturefactors(PRIVATE(this)->material.roughnesses, material->roughnesses);
  // The machined surface finish: a pattern this build does not know is
  // captured unchanged and dropped by the backend, not here -- the node
  // may state one a later build writes (App::SurfaceFinish::pattern).
  int32_t pattern = material->finish.getValue();
  PRIVATE(this)->material.finish = pattern > 0 && pattern < 256
      ? static_cast<uint8_t>(pattern) : 0;
  PRIVATE(this)->material.finishpitch = material->finishPitch.getValue();
  PRIVATE(this)->material.finishdepth = material->finishDepth.getValue();
  PRIVATE(this)->material.finishangle = material->finishAngle.getValue();
  // The per-face form: the palette of distinct finishes, and one index
  // into it per face. Built once here rather than per draw, so every
  // draw off this node shares the pointer -- which is what the batching
  // comparison keys on.
  PRIVATE(this)->material.finishpalette.reset();
  PRIVATE(this)->material.finishindices.reset();
  const int numpalette = material->finishPalette.getNum();
  const int numindices = material->finishIndices.getNum();
  if (numpalette > 1 && numindices > 0) {
    auto palette = std::make_shared<Render::FinishPalette>();
    const SbVec4f *entries = material->finishPalette.getValues(0);
    const int num = std::min(numpalette, Render::MaxFinishPalette);
    palette->entries.reserve(num);
    for (int i = 0; i < num; ++i) {
      Render::FinishPalette::Entry entry;
      const float value = entries[i][0];
      entry.pattern = value > 0.0f && value < 256.0f
          ? static_cast<uint8_t>(value + 0.5f) : 0;
      entry.pitch = entries[i][1];
      entry.depth = entries[i][2];
      entry.angle = entries[i][3];
      palette->entries.push_back(entry);
    }
    PRIVATE(this)->material.finishpalette = std::move(palette);
    const int32_t *indices = material->finishIndices.getValues(0);
    PRIVATE(this)->material.finishindices.reserve(numindices);
    for (int i = 0; i < numindices; ++i) {
      // An index the palette cap dropped resolves to entry 0, the
      // object's own finish (ViewProviderGeometryObject caps the same
      // way, so this only catches a hand-built node).
      const int32_t idx = indices[i];
      PRIVATE(this)->material.finishindices.append(
              idx > 0 && idx < num ? idx : 0);
    }
  }
  // The projection frames the finish is laid out in. Three SbVec4f make
  // one frame -- (origin, kind), (axis, radius), (xdir, spare) -- and
  // entry 0 is the first face's, which is what a draw with no
  // per-vertex stream to index with reads.
  PRIVATE(this)->material.framepalette.reset();
  PRIVATE(this)->material.frameindices.reset();
  const int numframevalues = material->framePalette.getNum();
  const int numframeindices = material->frameIndices.getNum();
  if (numframevalues >= 3 && numframeindices > 0) {
    auto palette = std::make_shared<Render::FramePalette>();
    const SbVec4f *values = material->framePalette.getValues(0);
    const int num = std::min(numframevalues / 3, Render::MaxFramePalette);
    palette->entries.reserve(num);
    for (int i = 0; i < num; ++i) {
      Render::SurfaceFrame frame;
      const SbVec4f &o = values[i * 3];
      const SbVec4f &a = values[i * 3 + 1];
      const SbVec4f &x = values[i * 3 + 2];
      const float kind = o[3];
      frame.kind = kind > 0.0f && kind < 256.0f
          ? static_cast<uint8_t>(kind + 0.5f) : 0;
      for (int k = 0; k < 3; ++k) {
        frame.origin[k] = o[k];
        frame.axis[k] = a[k];
        frame.xdir[k] = x[k];
      }
      frame.radius = a[3];
      palette->entries.push_back(frame);
    }
    if (!palette->entries.empty()) {
      PRIVATE(this)->material.framepalette = std::move(palette);
      const int32_t *indices = material->frameIndices.getValues(0);
      PRIVATE(this)->material.frameindices.reserve(numframeindices);
      for (int i = 0; i < numframeindices; ++i) {
        const int32_t idx = indices[i];
        PRIVATE(this)->material.frameindices.append(
                idx > 0 && idx < num ? idx : 0);
      }
    }
  }
  // The per-face texture layers, and how large the images they name are
  // laid out. The palette itself arrives as sibling SoFCRenderTexture
  // nodes (addRenderTexture), so what is captured here is only the
  // index array -- clamped to what the palette cap allows, since a
  // layer nothing uploaded would sample whatever the array texture
  // holds there.
  PRIVATE(this)->material.facetextureindices.reset();
  PRIVATE(this)->material.facetexscale =
      material->faceTextureScale.getValue();
  const int numlayers = material->faceTextureIndices.getNum();
  if (numlayers > 0) {
    const int32_t *layers = material->faceTextureIndices.getValues(0);
    PRIVATE(this)->material.facetextureindices.reserve(numlayers);
    for (int i = 0; i < numlayers; ++i) {
      const int32_t idx = layers[i];
      PRIVATE(this)->material.facetextureindices.append(
              idx > 0 && idx < Render::MaxFaceTexturePalette ? idx : 0);
    }
  }
  PRIVATE(this)->material.water = material->water.getValue();
  PRIVATE(this)->material.waterdensity = material->waterDensity.getValue();
  PRIVATE(this)->material.glass = material->glass.getValue();
  PRIVATE(this)->material.glassior = material->glassIOR.getValue();
  PRIVATE(this)->material.glassdensity = material->glassDensity.getValue();
  PRIVATE(this)->material.glassroughness
      = material->glassRoughness.getValue();
  PRIVATE(this)->material.cloud = material->cloud.getValue();
  PRIVATE(this)->material.clouddensity
      = material->cloudDensity.getValue();
  PRIVATE(this)->material.clouddetail = material->cloudDetail.getValue();
  PRIVATE(this)->material.cloudspeed = material->cloudSpeed.getValue();
  PRIVATE(this)->material.fire = material->fire.getValue();
  PRIVATE(this)->material.fireintensity
      = material->fireIntensity.getValue();
  PRIVATE(this)->material.firedetail = material->fireDetail.getValue();
  PRIVATE(this)->material.firespeed = material->fireSpeed.getValue();
  PRIVATE(this)->material.fountain = material->fountain.getValue();
  PRIVATE(this)->material.fountaindensity
      = material->fountainDensity.getValue();
  PRIVATE(this)->material.fountaindetail
      = material->fountainDetail.getValue();
  PRIVATE(this)->material.fountainspeed
      = material->fountainSpeed.getValue();
  PRIVATE(this)->material.lightsource = material->lightSource.getValue();
  PRIVATE(this)->material.lightintensity
      = material->lightIntensity.getValue();
  PRIVATE(this)->material.lightrange = material->lightRange.getValue();
  PRIVATE(this)->material.lightshadow = material->lightShadow.getValue();
  PRIVATE(this)->material.lightshadowext
      = material->lightShadowExtended.getValue();
}

void
SoFCRenderCache::setUserShader(SoState * state,
                               std::shared_ptr<const Render::UserShader> shader)
{
  PRIVATE(this)->checkState(state);

  // A user "material"-stage shader program (docs/RenderDebug.md §6),
  // translated by the cache manager. It applies to the shapes captured
  // after it in this cache and, unlike SoFCRenderMaterial, merges down
  // into child caches (outer shader wins — link material-override
  // semantics, see mergeMaterial); only external backends consume it.
  PRIVATE(this)->material.usershader = std::move(shader);
}

void
SoFCRenderCache::addRenderTexture(SoState * state, const SoNode * node)
{
  PRIVATE(this)->checkState(state);

  // SoFCRenderTexture has no Coin element either; the node routes into
  // the material map its slot field selects (emissive/occlusion/
  // metallic-roughness), kept out of `textures` like the bump map.
  // Unit 0 like the other maps.
  auto texture = static_cast<const Gui::SoFCRenderTexture *>(node);

  TextureInfo info;
  info.setTexture(const_cast<SoNode *>(node));
  info.transparent = false;
  info.identity = true;

  switch (texture->slot.getValue()) {
  case Gui::SoFCRenderTexture::OCCLUSION:
    PRIVATE(this)->material.occlusionmaps.set(0, info);
    break;
  case Gui::SoFCRenderTexture::METALLIC_ROUGHNESS:
    PRIVATE(this)->material.metallicroughnessmaps.set(0, info);
    break;
  case Gui::SoFCRenderTexture::FACE: {
    // A palette entry rather than a map: the key is the LAYER it
    // occupies, and layer 0 is the untextured face -- a node claiming
    // it (or a layer past the cap) states nothing at all.
    const int32_t layer = texture->layer.getValue();
    if (layer > 0 && layer < Render::MaxFaceTexturePalette)
      PRIVATE(this)->material.facetextures.set(int(layer), info);
    break;
  }
  default:
    PRIVATE(this)->material.emissivemaps.set(0, info);
    break;
  }
}

void
SoFCRenderCache::addTextureTransform(SoState * state, const SoNode * node)
{
  PRIVATE(this)->checkState(state);

  (void)node;
  int unit = SoTextureUnitElement::get(state);

  MatrixInfo info;
  bool identity = true;
  auto melem = constElement<MyMultiTextureMatrixElement>(state);
  if (melem->getNumUnits() > unit) {
    const auto & data = melem->getUnitData(unit);
    if (data.textureMatrix != matrixidentity) {
      identity = false;
      info.matrix = data.textureMatrix;
    }
  }
  if (identity)
    PRIVATE(this)->material.texturematrices.erase(unit);
  else
    PRIVATE(this)->material.texturematrices.set(unit, info);
}

void
SoFCRenderCache::addLight(SoState * state, const SoNode * light)
{
  PRIVATE(this)->checkState(state);

  (void)state;

  NodeInfo info;
  info.setNode(const_cast<SoNode*>(light));
  info.resetmatrix = PRIVATE(this)->resetmatrix;

  auto elem = constElement<SoModelMatrixElement>(state);
  if (elem->getModelMatrix() == matrixidentity)
    info.identity = true;
  else {
    info.identity = false;
    info.matrix = elem->getModelMatrix();
  }

  PRIVATE(this)->material.lights.append(info);
}

void
SoFCRenderCache::addClipPlane(SoState * state, const SoClipPlane * node)
{
  PRIVATE(this)->checkState(state);

  (void)state;
  if (!node->on.getValue() || node->on.isIgnored()) return;

  NodeInfo info;
  info.setNode(const_cast<SoClipPlane*>(node));
  info.resetmatrix = PRIVATE(this)->resetmatrix;

  auto elem = constElement<SoModelMatrixElement>(state);
  if (elem->getModelMatrix() == matrixidentity)
    info.identity = true;
  else {
    info.identity = false;
    info.matrix = elem->getModelMatrix();
  }

  PRIVATE(this)->material.clippers.append(info);
}

void
SoFCRenderCache::addAutoZoom(SoState * state, const SoAutoZoomTranslation * node)
{
  PRIVATE(this)->checkState(state);

  (void)state;

  NodeInfo info;
  info.setNode(const_cast<SoAutoZoomTranslation*>(node));
  info.resetmatrix = PRIVATE(this)->resetmatrix;

  auto elem = constElement<SoModelMatrixElement>(state);
  if (elem->getModelMatrix() == matrixidentity)
    info.identity = true;
  else {
    info.identity = false;
    info.matrix = elem->getModelMatrix();
  }

  PRIVATE(this)->material.autozoom.append(info);
}

void
SoFCRenderCache::resetMatrix(SoState *state)
{
  PRIVATE(this)->checkState(state);
  PRIVATE(this)->resetmatrix = true;
  PRIVATE(this)->material.autozoom.clear();
}

inline void
SoFCRenderCacheP::finalizeMaterial(Material & material)
{
  if (material.materialbinding == SoMaterialBindingElement::OVERALL)
    material.pervertexcolor = false;

  if (material.type == Material::Triangle) {
    if (material.maskflags.test(Material::FLAG_FACE_COLOR)) {
      material.pervertexcolor = false;
      material.diffuse = (material.facecolor & ~0xff) | (material.diffuse & 0xff);
    }
    if (material.maskflags.test(Material::FLAG_FACE_TRANSPARENCY)) {
      uint8_t alpha = static_cast<uint8_t>(
          std::max(std::min(255 - int(material.facecolor & 0xff), 255), 0) * 255.f);
      material.diffuse = (material.diffuse & ~0xff) | alpha;
    }
  } else if (material.linecolor) {
    material.pervertexcolor = false;
    material.diffuse = material.linecolor;
  }

  // if (material.pervertexcolor && material.maskflags.test(Material::FLAG_TRANSPARENCY))
  //   material.overrideflags.set(Material::FLAG_TRANSPARENCY);
}

struct RenderCacheStackHelper {
  RenderCacheStackHelper(SoFCSelectionRoot *node)
    :node(node)
  {
    if (node)
      SoFCRenderCacheP::RenderCacheStack.push_back(node);
  }
  ~RenderCacheStackHelper() {
    if (node)
      SoFCRenderCacheP::RenderCacheStack.pop_back();
  }

  SoFCSelectionRoot *node;
};

static int checkSelectionContext(SoFCRenderCache::Material &material,
                                 const SoFCSelectionContextExPtr &ctx,
                                 VertexCachePtr &vcache);

static bool sameVertexEntry(const VertexCacheEntry &a, const VertexCacheEntry &b)
{
  if (a.cache != b.cache
      || a.identity != b.identity
      || a.resetmatrix != b.resetmatrix
      || a.mergecount != b.mergecount
      || a.skipcount != b.skipcount
      || a.partidx != b.partidx)
    return false;
  if (!a.identity && a.matrix != b.matrix)
    return false;
  if (!a.key || !b.key)
    return a.key == b.key;
  return *a.key == *b.key;
}

void
SoFCRenderCacheP::verifySplice(const SbFCVector<int> &matchold,
                               bool canmerge,
                               int depth)
{
  const int newn = (int)this->caches.size();
  const int savedfaces = this->facecount;
  int bad = 0;

  for (int i = 0; i < newn; ++i) {
    if (matchold[i] < 0)
      continue;  // derived, not copied: nothing was assumed about it

    SoFCRenderCache::VertexCacheMap scratch;
    SbFCVector<ChildSlice> scratchslices;
    KeyMap scratchkeys;
    if (!this->mergeChildCache(scratch, this->caches[i], scratchkeys, canmerge,
                               depth, &scratchslices))
      continue;  // not describable by slices, so it was not copied either

    const int from = this->sliceoffsets[i];
    const int to = this->sliceoffsets[i + 1];
    bool ok = (to - from) == (int)scratchslices.size();
    for (int k = 0; ok && k < (int)scratchslices.size(); ++k) {
      const ChildSlice &want = scratchslices[k];
      const ChildSlice &got = this->slices[from + k];
      if (want.count != got.count) {
        ok = false;
        break;
      }
      const auto &wantbucket = scratch.nth(want.bucket);
      const auto &gotbucket = this->vcachemap->nth(got.bucket);
      if (wantbucket->first < gotbucket->first || gotbucket->first < wantbucket->first) {
        ok = false;
        break;
      }
      for (int e = 0; e < want.count; ++e) {
        if (!sameVertexEntry(wantbucket->second[want.start + e],
                             gotbucket->second[got.start + e])) {
          ok = false;
          break;
        }
      }
    }
    if (!ok && ++bad <= 4) {
      FC_ERR("incremental flatten: child " << i << " of " << newn
             << " kept a slice it would not derive now");
    }
  }

  this->facecount = savedfaces;
  if (bad)
    FC_ERR("incremental flatten: " << bad << " of " << newn
           << " copied children disagree with a fresh merge");
}

bool
SoFCRenderCacheP::spliceFrom(SoFCRenderCache *prevc,
                             const SbFCVector<int> &matchold,
                             KeyMap &keymap,
                             bool canmerge,
                             int depth)
{
  auto & prev = *PRIVATE(prevc);
  if (!prev.vcachemap || !prev.slicesvalid)
    return false;

  const int oldn = (int)prev.caches.size();
  const int newn = (int)this->caches.size();
  if (!oldn || !newn || (int)prev.sliceoffsets.size() != oldn + 1)
    return false;

  // Which of the previous children each of these is was worked out by the
  // change set as this cache closed, over the same two caches. A match is
  // a whole entry matching -- the transform and material live here in the
  // parent, so the same child cache under a moved parent is a changed
  // contribution -- which is the condition for copying its slice.
  if ((int)matchold.size() != newn)
    return false;

  int kept = 0;
  for (int i = 0; i < newn; ++i) {
    // A shape child puts entries in the map that no child slice describes.
    if (!this->caches[i].cache)
      return false;
    if (matchold[i] < 0)
      continue;
    if (matchold[i] >= oldn)
      return false;
    ++kept;
  }
  // Nothing to inherit: merging the lot is the same work without the
  // bookkeeping.
  if (!kept)
    return false;

  // Derive the children the previous publish did not have, into a map of
  // their own so their runs can be placed in child order below.
  SoFCRenderCache::VertexCacheMap addmap;
  SbFCVector<ChildSlice> addslices;
  SbFCVector<int> addoffsets;
  SbFCVector<int> addindex(newn, -1);
  addoffsets.push_back(0);
  for (int i = 0; i < newn; ++i) {
    if (matchold[i] >= 0)
      continue;
    addindex[i] = (int)addoffsets.size() - 1;
    if (!this->mergeChildCache(addmap, this->caches[i], keymap, canmerge, depth,
                               &addslices)) {
      this->facecount = 0;
      return false;
    }
    addoffsets.push_back((int)addslices.size());
  }

  auto & oldmap = *prev.vcachemap;
  auto & newmap = *this->vcachemap;
  newmap.clear();

  // The buckets of the result are those of both sources. Only a handful,
  // so the material comparisons this costs do not signify.
  for (auto & v : oldmap)
    newmap.emplace(v.first, VertexCacheArray());
  for (auto & v : addmap)
    newmap.emplace(v.first, VertexCacheArray());

  SbFCVector<int> oldtonew(oldmap.size(), -1);
  int b = 0;
  for (auto & v : oldmap)
    oldtonew[b++] = (int)(newmap.find(v.first) - newmap.begin());
  SbFCVector<int> addtonew(addmap.size(), -1);
  b = 0;
  for (auto & v : addmap)
    addtonew[b++] = (int)(newmap.find(v.first) - newmap.begin());

  for (size_t i = 0; i < oldmap.size(); ++i)
    newmap.nth(oldtonew[i])->second.reserve(oldmap.nth(i)->second.size());

  this->slices.clear();
  this->sliceoffsets.clear();
  this->sliceoffsets.push_back(0);

  for (int i = 0; i < newn; ++i) {
    const bool isold = matchold[i] >= 0;
    const auto & srcslices = isold ? prev.slices : addslices;
    const auto & srcoffsets = isold ? prev.sliceoffsets : addoffsets;
    const auto & srcmap = isold ? oldmap : addmap;
    const auto & tonew = isold ? oldtonew : addtonew;
    const int si = isold ? matchold[i] : addindex[i];

    for (int k = srcoffsets[si]; k < srcoffsets[si + 1]; ++k) {
      const ChildSlice & s = srcslices[k];
      const auto & src = srcmap.nth(s.bucket)->second;
      auto & dst = newmap.nth(tonew[s.bucket])->second;
      ChildSlice ns;
      ns.bucket = tonew[s.bucket];
      ns.start = (int)dst.size();
      ns.count = s.count;
      dst.insert(dst.end(), src.begin() + s.start, src.begin() + s.start + s.count);
      this->slices.push_back(ns);
    }
    // mergeChildCache already counted the faces of the children it built.
    if (isold)
      this->facecount += PRIVATE(this->caches[i].cache)->facecount;
    this->sliceoffsets.push_back((int)this->slices.size());
  }

  // A bucket whose every child went away is a bucket a wholesale merge
  // would never have made, and leaving it would make the two maps compare
  // unequal for no reason.
  if (newmap.size()) {
    SbFCVector<int> shift(newmap.size(), 0);
    int drop = 0;
    for (size_t i = 0; i < newmap.size(); ++i) {
      shift[i] = drop;
      if (newmap.nth(i)->second.empty())
        ++drop;
    }
    if (drop) {
      for (auto & s : this->slices)
        s.bucket -= shift[s.bucket];
      for (size_t i = newmap.size(); i-- > 0;) {
        if (newmap.nth(i)->second.empty())
          newmap.erase(newmap.nth(i));
      }
    }
  }

  this->slicesvalid = true;

  if (ViewParams::getRenderCacheIncremental() > 1)
    this->verifySplice(matchold, canmerge, depth);

  return true;
}

bool
SoFCRenderCacheP::mergeChildCache(SoFCRenderCache::VertexCacheMap &vcachemap,
                                  const CacheEntry &entry,
                                  KeyMap &keymap,
                                  bool canmerge,
                                  int depth,
                                  SbFCVector<ChildSlice> *slicesout)
{
  bool sliceable = slicesout != nullptr;
  auto it = vcachemap.end();
  const auto & childvcaches = entry.cache->getVertexCaches(canmerge, depth+1);
  for (const auto & child : childvcaches) {
    bool identity = entry.identity;
    Material material = this->mergeMaterial(
          entry.matrix, identity, entry.material, child.first);

    if (depth == 0)
      this->finalizeMaterial(material);

    SoFCRenderCache::VertexCacheMap::value_type value(material, {});

    VertexCacheArray *pushed_entries = nullptr;
    int mutated = 0;
    int slicestart = -1;
    int slicecount = 0;
    for (auto & childentry : child.second) {
      VertexCacheArray *ventries = pushed_entries;
      int res = 1;
      auto vcache = childentry.cache;
      SoFCRenderCache::CacheKeyPtr & key = keymap[childentry.key];
      if (!key) {
        if (!this->selnode)
          key = childentry.key;
        else {
          key = std::allocate_shared<SoFCRenderCache::CacheKey>(
              SoFCAllocator<SoFCRenderCache::CacheKey>());
          key->push(this->selnode);
          key->append(childentry.key);
        }
      }
      SoFCSelectionContextExPtr ctx;
      if (this->selnode)
        ctx = key->getSecondaryContext(SoFCRenderCacheP::RenderCacheStack, vcache->getNode());
      res = checkSelectionContext(material, ctx, vcache);
      if (!res)
        continue;

      if (res < 0) {
        // The context rewrote the material, so this entry lands in a
        // bucket of its own choosing rather than the child's: a slice
        // can no longer describe where the child's entries went.
        sliceable = false;
        if (childentry.skipcount && !mutated) {
          if (!pushed_entries) {
            auto iter = vcachemap.find(value.first);
            if (iter != vcachemap.end()) {
              pushed_entries = &iter->second;
              mutated = (int)pushed_entries->size();
            }
          }
        }
        ventries = &vcachemap[material];
        it = vcachemap.end();
        material = value.first; // revert back to original material
      } else if (!ventries) {
        const size_t before = vcachemap.size();
        it = vcachemap.insert(it, value);
        pushed_entries = ventries = &it->second;
        if (sliceable && vcachemap.size() != before) {
          // A new bucket shifts the index of every bucket after it, and
          // the slices already recorded name buckets by index.
          const int at = (int)(it - vcachemap.begin());
          for (auto & s : *slicesout) {
            if (s.bucket >= at)
              ++s.bucket;
          }
        }
        if (sliceable)
          slicestart = (int)ventries->size();
      }

      ventries->emplace_back(vcache, childentry, key);
      // Entries passing through a cache the defer marked belong to the
      // deferring object (the mark stops at its selection root), so
      // they pick the mark up here even when they were captured under a
      // sibling nested cache the defer never opened.
      if (this->incompletehere)
        ventries->back().incomplete = true;
      ++slicecount;
      if (!identity && !childentry.resetmatrix) {
        if (!childentry.identity)
          ventries->back().matrix.multRight(entry.matrix);
        else {
          ventries->back().matrix = entry.matrix;
          ventries->back().identity = false;
        }
      }
    }

    if (mutated) {
      // Discard any affected merged caches due to mutation. TODO: there
      // could be more optimal way to selectively discard merges, but need
      // much more complex logic to make it correct, because there could be
      // multiple mutations.
      sliceable = false;
      for (int i=0; i<mutated; ++i) {
        auto & mentry = (*pushed_entries)[i];
        if (mentry.mergecount < mutated - i)
          continue;
        for (auto iter=pushed_entries->begin()+i; iter!=pushed_entries->end();) {
          if (iter->mergecount)
            iter = pushed_entries->erase(iter);
          else {
            iter->skipcount = 0;
            ++i;
          }
        }
      }
    }
    if (sliceable && slicestart >= 0 && slicecount > 0) {
      ChildSlice slice;
      slice.bucket = (int)(it - vcachemap.begin());
      slice.start = slicestart;
      slice.count = slicecount;
      slicesout->push_back(slice);
    }
    if (it != vcachemap.end())
      ++it;
  }
  this->facecount += PRIVATE(entry.cache)->facecount;
  return sliceable;
}

// Apply a secondary selection context to one entry's material and
// vertex cache. Returns 0 to skip the entry, 1 to proceed with the
// material unchanged, -1 if the material was changed. Was a lambda
// inside getVertexCaches until the per-child merge moved out of it.
static int checkSelectionContext(SoFCRenderCache::Material &material,
                                 const SoFCSelectionContextExPtr &ctx,
                                 VertexCachePtr &vcache)
{
    // Check for secondary selection context for color override and partial rendering
    // return 0 if should skip this entry, 1 if proceed with same material, -1
    // if material is changed.
    if (!ctx)
      return 1;
    if (ctx->selectionIndex.empty())
      return 0;
    if (ctx->selectionIndex.begin()->first < 0 && ctx->colors.empty())
      return 1;
    switch(material.type) {
      case Material::Triangle: {
        if (ctx->selectionIndex.begin()->first >= 0) {
          vcache = new SoFCVertexCache(*vcache);
          vcache->addTriangles(ctx->selectionIndex);
        } else {
          if (ctx->colors.empty())
            return 1;
          vcache = new SoFCVertexCache(*vcache);
          vcache->addTriangles();
        }
        if (ctx->colors.empty())
          return 1;
        if (ctx->colors.begin()->first < 0 && ctx->colors.size() == 1) {
          vcache->setFaceColors();
          // The override colour's alpha is an opacity, which is exactly what
          // the packed material diffuse stores -- no conversion.
          uint32_t diffuse = ctx->colors.begin()->second.getPackedValue();
          if (diffuse != material.diffuse || material.pervertexcolor) {
            material.diffuse = diffuse;
            material.pervertexcolor = false;
            material.materialbinding = SoMaterialBindingElement::OVERALL;
            return -1;
          }
          return 1;
        }
        selcolors.clear();
        for (auto &v : ctx->colors) {
          if (v.first < 0)
            continue;
          selcolors.emplace_back(v.first, v.second.getPackedValue());
        }
        vcache->setFaceColors(selcolors);
        if ((material.pervertexcolor && !vcache->colorPerVertex())
            || (!material.pervertexcolor && vcache->colorPerVertex()))
        {
          material.pervertexcolor = !material.pervertexcolor;
          material.materialbinding = material.pervertexcolor ?
            SoMaterialBindingElement::PER_PART : SoMaterialBindingElement::OVERALL;
          return -1;
        }
        break;
      }
      case Material::Line: {
        if (ctx->selectionIndex.begin()->first < 0)
          return 1;
        vcache = new SoFCVertexCache(*vcache);
        vcache->addLines(ctx->selectionIndex);
        break;
      }
      case Material::Point: {
        if (ctx->selectionIndex.begin()->first < 0)
          return 1;
        vcache = new SoFCVertexCache(*vcache);
        vcache->addPoints(ctx->selectionIndex);
        break;
      }
      default:
        return 0;
    }
    return 1;
}

const SoFCRenderCache::VertexCacheMap &
SoFCRenderCache::getVertexCaches(bool canmerge, int depth)
{
  RenderCacheStackHelper guard(PRIVATE(this)->selnode);

  if (PRIVATE(this)->vcachemap) {
    if (PRIVATE(this)->vcachemap->size())
      return *PRIVATE(this)->vcachemap;
  }
  else if (VertexCacheMaps.size()) {
    PRIVATE(this)->vcachemap = std::move(VertexCacheMaps.back());
    VertexCacheMaps.pop_back();
    CacheEntryFreeCount -= VertexCacheMapCounts.back();
    VertexCacheMapCounts.pop_back();
  } else
    PRIVATE(this)->vcachemap.reset(new VertexCacheMap);

  // Split by level, because the two are different work: the top level
  // copies every descendant entry the tree has already produced once,
  // while the levels below it are where those entries are made
  // (docs/IncrementalPublish.md §4b).
  Gui::RenderTiming::Scope timing(depth ? Gui::RenderTiming::FlattenSub
                                        : Gui::RenderTiming::Flatten);

  auto & vcachemap = *PRIVATE(this)->vcachemap;
  PRIVATE(this)->facecount = 0;
  PRIVATE(this)->cachecount = 0;

  std::unordered_map<CacheKeyPtr, CacheKeyPtr, CacheKeyHasher, CacheKeyHasher> keymap;
  CacheKeyPtr selfkey;


  // Record where each child's entries land, so the publish after this one
  // can copy them instead of merging every child again (§5). Only for a
  // cache whose children are all nested caches -- the shape branch below
  // contributes entries a child slice does not describe -- and only while
  // nothing can rewrite a material out from under the record: a secondary
  // selection context, or draw-call merging fusing entries across children
  // (§6.1).
  bool recordslices = ViewParams::getRenderCacheIncremental() > 0
                      && !Gui::SoFCSelectionRoot::hasSecondaryContext()
                      && !ViewParams::getRenderCacheMergeCount();
  PRIVATE(this)->slices.clear();
  PRIVATE(this)->sliceoffsets.clear();
  PRIVATE(this)->slicesvalid = false;
  if (recordslices)
    PRIVATE(this)->sliceoffsets.push_back(0);

  // Inherit the previous publish's map where there is one to inherit, and
  // derive only the children it did not already hold.
  bool spliced = false;
  if (recordslices && PRIVATE(this)->spliceprev) {
    spliced = PRIVATE(this)->spliceFrom(PRIVATE(this)->spliceprev,
                                        PRIVATE(this)->splicematch, keymap,
                                        canmerge, depth);
  }
  PRIVATE(this)->spliceprev.reset();
  PRIVATE(this)->splicematch.clear();

  // TEMPORARY (dots investigation): a spliced publish reuses the
  // previous map wholesale and never runs the emission loop below, so
  // an entry missing from that map stays missing.
  if (getenv("FC_DOTS_DUMP") && spliced)
    Base::Console().Message("DOTS emit spliced (loop skipped)\n");

  if (!spliced)
  for (auto & entry : PRIVATE(this)->caches) {
    if (entry.vcache) {
      recordslices = false;
      if (!selfkey && PRIVATE(this)->selnode) {
        selfkey = std::allocate_shared<CacheKey>(SoFCAllocator<CacheKey>());
        selfkey->push(PRIVATE(this)->selnode);
        if (!canmerge) {
          if (auto id = entry.vcache->getSelectionNodeId())
            selfkey->forcePush(id);
        }
      }
      SoFCSelectionContextExPtr ctx;
      if (selfkey)
          ctx = selfkey->getSecondaryContext(
              SoFCRenderCacheP::RenderCacheStack, entry.vcache->getNode());

      entry.material.pervertexcolor = entry.vcache->colorPerVertex();

      auto vcache = entry.vcache;

      // TEMPORARY (dots investigation): which entries the loop sees.
      if (getenv("FC_DOTS_DUMP") && entry.vcache->getNumLineIndices())
        Base::Console().Message(
            "DOTS emit entry tri=%d line=%d point=%d shouldTri=%d\n",
            entry.vcache->getNumTriangleIndices(),
            entry.vcache->getNumLineIndices(),
            entry.vcache->getNumPointIndices(),
            int(entry.vcache->shouldRenderTriangles()));

      if (entry.vcache->shouldRenderTriangles()) {
        Material material = entry.material;
        material.type = Material::Triangle;
        if (!checkSelectionContext(material, ctx, vcache))
          continue;
        if (depth == 0) {
          PRIVATE(this)->finalizeMaterial(material);
          if (!entry.vcache->getNormalArray())
            material.lightmodel = SoLazyElement::BASE_COLOR;
        }
        if (entry.vcache->hasFlipNormal()) {
          material.vertexordering = SoShapeHints::CLOCKWISE;
          material.maskflags.set(Material::FLAG_VERTEXORDERING);
        }
        if (vcache->hasSolid() > 1) {
          material.shapetype = SoShapeHintsElement::SOLID;
          // material.culling = 1;
          if (ViewParams::getForceSolidSingleSideLighting()) {
            material.twoside = false;
            material.overrideflags.set(Material::FLAG_TWOSIDE);
            material.maskflags.set(Material::FLAG_TWOSIDE);
          }
        }
        vcachemap[material].emplace_back(vcache,
                                         entry.matrix,
                                         entry.identity,
                                         entry.resetmatrix,
                                         selfkey);
        // The defer under the capture budget marked this cache: every
        // sibling drawable of the deferred shape carries the mark out,
        // so the display can tell an object whose companion drawable
        // has not arrived from one whose mode omits it (#13b).
        vcachemap[material].back().incomplete = PRIVATE(this)->incompletehere;
        PRIVATE(this)->facecount += vcache->getNumFaceParts();
      }
      if (entry.vcache->getNumLineIndices()) {
        Material material = entry.material;
        material.type = Material::Line;
        const bool dotsCtxOk = checkSelectionContext(material, ctx, vcache);
        // TEMPORARY (dots investigation).
        if (getenv("FC_DOTS_DUMP"))
          Base::Console().Message(
              "DOTS emit LINE idx=%d ctxok=%d depth=%d\n",
              entry.vcache->getNumLineIndices(), int(dotsCtxOk), depth);
        if (!dotsCtxOk)
          continue;
        if (depth == 0) {
          PRIVATE(this)->finalizeMaterial(material);
          if (!entry.vcache->getNormalArray())
            material.lightmodel = SoLazyElement::BASE_COLOR;
        }
        vcachemap[material].emplace_back(vcache,
                                         entry.matrix,
                                         entry.identity,
                                         entry.resetmatrix,
                                         selfkey);
        // The defer under the capture budget marked this cache: every
        // sibling drawable of the deferred shape carries the mark out,
        // so the display can tell an object whose companion drawable
        // has not arrived from one whose mode omits it (#13b).
        vcachemap[material].back().incomplete = PRIVATE(this)->incompletehere;
      }
      if (entry.vcache->getNumPointIndices()) {
        Material material = entry.material;
        material.type = Material::Point;
        if (!checkSelectionContext(material, ctx, vcache))
          continue;
        if (depth == 0) {
          PRIVATE(this)->finalizeMaterial(material);
          if (!entry.vcache->getNormalArray())
            material.lightmodel = SoLazyElement::BASE_COLOR;
        }
        vcachemap[material].emplace_back(vcache,
                                         entry.matrix,
                                         entry.identity,
                                         entry.resetmatrix,
                                         selfkey);
        // The defer under the capture budget marked this cache: every
        // sibling drawable of the deferred shape carries the mark out,
        // so the display can tell an object whose companion drawable
        // has not arrived from one whose mode omits it (#13b).
        vcachemap[material].back().incomplete = PRIVATE(this)->incompletehere;
      }
      continue;
    }
    if (!PRIVATE(this)->mergeChildCache(vcachemap, entry, keymap, canmerge, depth,
                                        recordslices ? &PRIVATE(this)->slices : nullptr))
      recordslices = false;
    if (recordslices)
      PRIVATE(this)->sliceoffsets.push_back((int)PRIVATE(this)->slices.size());
    // A child's map is dropped as soon as it has been copied up, which is
    // why the next publish re-derives one for every object in the scene
    // however little moved — measured at 5982 of them per publish on a
    // 6002-object import, 25ms of a 28ms flatten
    // (docs/IncrementalPublish.md §4b). A small map is kept instead: it
    // is a few entries of memory against re-deriving it every frame, and
    // the traversal hands the whole cache back untouched when nothing
    // below it changed, memo and all. Big maps are the copies of whole
    // subtrees, and those are still dropped -- unless the splice is on,
    // which needs the big ones alive: a rebuilt cache inherits its
    // predecessor's map, and the predecessor is a child of some parent
    // that would otherwise have dropped it here (§5).
    if (PRIVATE(entry.cache)->cachehint < 2
        && !ViewParams::getRenderCacheIncremental()
        && PRIVATE(entry.cache)->cachecount > ViewParams::getRenderCacheKeepMax())
      PRIVATE(entry.cache)->freeCacheMap();
  }

  if (!spliced && recordslices
      && PRIVATE(this)->sliceoffsets.size() == PRIVATE(this)->caches.size() + 1)
    PRIVATE(this)->slicesvalid = true;

  if ((canmerge || PRIVATE(this)->mergemap)
      && ViewParams::getRenderCacheMergeCount()
      && depth >= ViewParams::getRenderCacheMergeDepthMin()
      && (ViewParams::getRenderCacheMergeDepthMax() < 0
        || depth <= ViewParams::getRenderCacheMergeDepthMax()))
  {
    if (ViewParams::getRenderCacheMergeDepthMax()
        && ViewParams::getRenderCacheMergeDepthMin())
    for (auto & v : vcachemap) {
      int count = 0;
      for (int i=0; i<(int)v.second.size(); ++i) {
        ++count;
        i += v.second[i].mergecount;
      }
      if (count < std::max(ViewParams::getRenderCacheMergeCountMin(),
            ViewParams::getRenderCacheMergeCount()))
        continue;

      VertexCacheEntry newentry;
      for (int i=0; i<(int)v.second.size(); ++i) {
        int idx = i;
        auto &entry = v.second[i];
        newentry.mergecount = 0;
        newentry.cache = entry.cache->merge(canmerge,
            PRIVATE(this)->mergemap, v.second, idx, newentry.mergecount);
        if (!newentry.cache) {
          if (newentry.mergecount)
            i = idx + newentry.mergecount;
          else
            i += entry.mergecount;
        } else {
          assert(newentry.mergecount>0);
          newentry.key = std::allocate_shared<CacheKey>(SoFCAllocator<CacheKey>());
          newentry.key->forcePush(0x80000000 | newentry.cache->getCacheId());
          // A merged entry stands for every member it covers, so it is
          // incomplete if any of them was -- losing the mark here would
          // let a draw-call merge unhide a companion-less drawable.
          newentry.incomplete = false;
          for (int j = idx; j < idx + newentry.mergecount
                            && j < (int)v.second.size(); ++j)
            newentry.incomplete |= v.second[j].incomplete;
          v.second.insert(v.second.begin()+idx, newentry);
          i = idx + newentry.mergecount;
        }
      }
    }

    if (canmerge && PRIVATE(this)->mergemap)
      PRIVATE(this)->mergemap->cleanup();
  }

#ifdef FCCOIN_TRACE_ACHE_NAME
  std::size_t count = 0;
  for (auto &v : vcachemap)
    count += v.second.size();
  for (int i=0; i<depth; ++i)
    std::cerr << ' ';
  std::cerr << count << ": " << PRIVATE(this)->nodename.getString() << "\n";
#endif

  for (auto &v : vcachemap)
    PRIVATE(this)->cachecount += (int)v.second.size();
  CacheEntryCount += PRIVATE(this)->cachecount;

  // TEMPORARY (dots investigation): what the finished map holds, by
  // material type, at every depth. The depth-0 map is what the renderer
  // is handed, so a Line entry emitted at depth 1 that is absent here
  // was lost on the way up.
  if (getenv("FC_DOTS_DUMP")) {
    int nt = 0, nl = 0, np = 0;
    for (auto &v : vcachemap) {
      if (v.first.type == Material::Triangle) nt += (int)v.second.size();
      else if (v.first.type == Material::Line) nl += (int)v.second.size();
      else if (v.first.type == Material::Point) np += (int)v.second.size();
    }
    Base::Console().Message(
        "DOTS map depth=%d tri=%d line=%d point=%d node=%s\n",
        depth, nt, nl, np, PRIVATE(this)->nodename.getString());
  }

  return vcachemap;
}

bool makeDistinctColor(SbColor &res, const SbColor &color, const SbColor &other) {
    double delta = ColorUtils::getColorDeltaE(
          ColorUtils::rgbColor(color[0], color[1], color[2]),
          ColorUtils::rgbColor(other[0], other[1], other[2]));
    if (delta > ViewParams::getSelectionColorDifference())
      return false;

    float h,s,v;
    color.getHSVValue(h,s,v);
    h += 0.3f;
    if(h>1.0f)
        h = 1.0f-h;
    if(s<0.2f)
        s = 1.0f-s;
    res.setHSVValue(h,s,1.0f);
    return true;
}

bool makeDistinctColor(uint32_t &res, uint32_t color, uint32_t other) {
    SbColor r, c, o;
    float t;
    o.setPackedValue(other,t);
    c.setPackedValue(color,t);
    if(!makeDistinctColor(r,c,o))
        return false;
    res = r.getPackedValue(t);
    return true;
}

SoFCRenderCache::VertexCacheMap
SoFCRenderCache::buildWholeCacheMap(int order, const SoDetail * detail)
{
  // Face-element scope: pick the face indices out of the detail. Only
  // face elements can carry a material-stage shader; a non-face detail
  // yields an empty map.
  const SoFaceDetail * fd = nullptr;
  const SoFCDetail * d = nullptr;
  void * fctx = nullptr;
  if (detail) {
    if (detail->isOfType(SoFaceDetail::getClassTypeId())) {
      fd = static_cast<const SoFaceDetail*>(detail);
      if (fd->isOfType(SoFCFaceDetail::getClassTypeId()))
        fctx = static_cast<const SoFCFaceDetail*>(fd)->getContext();
    }
    else if (detail->isOfType(SoFCDetail::getClassTypeId())) {
      d = static_cast<const SoFCDetail*>(detail);
      fctx = d->getContext(SoFCDetail::Face);
      if (d->getIndices(SoFCDetail::Face).empty())
        return {};
    }
    else
      return {};
  }

  VertexCacheMap res;
  for (auto & child : getVertexCaches(false)) {
    if (detail && child.first.type != Material::Triangle)
      continue;
    for (auto & ventry : child.second) {
      if (detail) {
        // Partial rendering works on the original per-shape caches, not
        // the merged composites (the buildHighlightCache convention).
        if (ventry.mergecount)
          continue;
        if (fctx && fctx != ventry.cache->getNode())
          continue;
      }
      else if (ventry.skipcount || ventry.mergecount)
        continue;
      Material material = child.first;
      material.order = order;
      material.depthfunc = SoDepthBuffer::LEQUAL;
      if (!detail) {
        res[material].push_back(ventry);
        continue;
      }

      VertexCacheEntry newentry(ventry);
      if (fd) {
        if (fd->getPartIndex() < 0)
          continue;
        newentry.partidx = fd->getPartIndex();
      }
      else {
        const auto & indices = d->getIndices(SoFCDetail::Face);
        if (indices.size() == 1 && *indices.begin() >= 0)
          newentry.partidx = *indices.begin();
        else if (indices.size() > 1) {
          newentry.cache = new SoFCVertexCache(*newentry.cache);
          newentry.cache->addTriangles(indices);
        }
        else
          continue;
      }
      // The element entry is partial, so the base draw is not
      // key-suppressed and the face renders twice at identical depth.
      // A small negative polygon offset lets the shader-carrying copy
      // win regardless of draw order (bgfx sorts within a view; GL
      // draws selections after the scene either way).
      material.polygonoffsetstyle = SoPolygonOffsetElement::FILLED;
      material.polygonoffsetfactor =
        -ViewParams::getRenderHighlightPolygonOffsetFactor();
      material.polygonoffsetunits =
        -ViewParams::getRenderHighlightPolygonOffsetUnits();
      res[material].push_back(newentry);
    }
  }
  return res;
}

SoFCRenderCache::VertexCacheMap
SoFCRenderCache::buildHighlightCache(SbFCMap<int, VertexCachePtr> &sharedcache,
                                     int order,
                                     const SoDetail * detail,
                                     uint32_t color,
                                     int flags)
{
  VertexCacheMap res;
  uint32_t alpha = color & 0xff;
  color &= 0xffffff00;
  uint32_t _color = color;

  bool checkindices = (flags & CheckIndices) ? true : false;
  bool wholeontop = (flags & WholeOnTop) ? true : false;
  bool preselect = (flags & PreselectHighlight) ? true : false;

  const SoPointDetail * pd = nullptr;
  const SoLineDetail * ld = nullptr;
  const SoFaceDetail * fd = nullptr;
  const SoFCDetail * d = nullptr;
  void *fctx = nullptr;
  void *pctx = nullptr;
  void *lctx = nullptr;
  if (detail) {
    if (detail->isOfType(SoPointDetail::getClassTypeId())) {
      pd = static_cast<const SoPointDetail*>(detail);
      if (pd->isOfType(SoFCPointDetail::getClassTypeId()))
        pctx = static_cast<const SoFCPointDetail*>(pd)->getContext();
    }
    else if (detail->isOfType(SoLineDetail::getClassTypeId())) {
      ld = static_cast<const SoLineDetail*>(detail);
      if (ld->isOfType(SoFCLineDetail::getClassTypeId()))
        lctx = static_cast<const SoFCLineDetail*>(ld)->getContext();
    }
    else if (detail->isOfType(SoFaceDetail::getClassTypeId())) {
      fd = static_cast<const SoFaceDetail*>(detail);
      if (fd->isOfType(SoFCFaceDetail::getClassTypeId()))
        fctx = static_cast<const SoFCFaceDetail*>(fd)->getContext();
    }
    else if (detail->isOfType(SoFCDetail::getClassTypeId())) {
      d = static_cast<const SoFCDetail*>(detail);
      fctx = d->getContext(SoFCDetail::Face);
      lctx = d->getContext(SoFCDetail::Edge);
      pctx = d->getContext(SoFCDetail::Vertex);
    }

    // Some shape nodes (e.g. SoBrepFaceSet), support partial highlight on
    // whole object selection. 'checkindices' is used to indicate if we shall
    // check the internal highlight indices of those shapes that support it.
    // However, if we are not doing whole object selection (i.e. detail is not
    // null here), we should not check the indices.
    checkindices = false;
  }

  bool bboxinited = false;
  Material bboxmaterial;
  SbBox3f bbox;
  const VertexCacheEntry *detailentry = nullptr;
  int mergecount = 0;
  int entrycount = 0;
  for (auto & child : getVertexCaches(false)) {
    if (!wholeontop && detail) {
      // We are doing partial highlight, 'wholeontop' indicates that we shall
      // bring the whole object to top with the original color. So if not
      // 'wholeontop' and there is some highlight detail, it means we are
      // highlighting some sub-element of a shape node.

      if (child.first.selectstyle == Material::Unpickable && !ViewParams::getOverrideSelectability()) {
        // Either the parent is not selectable, or the shape is not
        // sub-element selectable (checked in the loop below).
        continue;
      }

      switch(child.first.type) {
      case Material::Point:
        if (!pd && (!d || d->getIndices(SoFCDetail::Vertex).empty()))
          continue;
        break;
      case Material::Line:
        if (!ld && (!d || d->getIndices(SoFCDetail::Edge).empty()))
          continue;
        break;
      default:
        if (!fd && (!d || d->getIndices(SoFCDetail::Face).empty()))
          continue;
      }
    }

    for (auto & ventry : child.second) {
      color = _color;

      bool elementselectable = ventry.cache->isElementSelectable()
        && child.first.selectstyle != Material::Unpickable;

      if (detail) {
        if (pctx || fctx || lctx) {
          // If there is detail context, make sure it matches to the node
          if ((pctx && child.first.type == Material::Point && pctx != ventry.cache->getNode())
              || (lctx && child.first.type == Material::Line && lctx != ventry.cache->getNode())
              || (fctx && child.first.type == Material::Triangle && fctx != ventry.cache->getNode()))
            continue;
        }
        if (ventry.mergecount)
          continue;
      } else if (ventry.skipcount)
        continue;

      if (!wholeontop && detail && !elementselectable)
        continue;

      Material material = child.first;
      material.order = order;
      material.depthfunc = SoDepthBuffer::LEQUAL;

      if (order > 0) {
        float scale = 1.f;
        if (material.type == Material::Line) {
          scale = 2.f;
          material.polygonoffsetstyle = SoPolygonOffsetElement::LINES;
        }
        else if (material.type == Material::Triangle) {
          material.polygonoffsetstyle = SoPolygonOffsetElement::POINTS;
          scale = 1.5f;
        }
        else
          material.polygonoffsetstyle = SoPolygonOffsetElement::FILLED;
        material.polygonoffsetfactor = -ViewParams::getRenderHighlightPolygonOffsetFactor();
        material.polygonoffsetunits = -ViewParams::getRenderHighlightPolygonOffsetUnits();
        if (preselect) {
          material.polygonoffsetfactor -= ViewParams::getRenderHighlightPolygonOffsetFactor();
          material.polygonoffsetunits -= ViewParams::getRenderHighlightPolygonOffsetUnits();
        }
        material.polygonoffsetfactor *= scale;
        material.polygonoffsetunits *= scale;
      }

      if (color && (material.selectstyle == Material::Box
                    || (material.selectstyle == Material::BoxFull && !detail)
                    || (ViewParams::getShowSelectionBoundingBox()
                        && (!detail || !preselect))
                    || (ViewParams::getShowSelectionBoundingBoxThreshold()
                        && ViewParams::getShowSelectionOnTop()
                        && PRIVATE(this)->facecount > ViewParams::getShowSelectionBoundingBoxThreshold())))
      {
        if (!bboxinited) {
          bboxinited = true;
          bboxmaterial = material;
          bboxmaterial.diffuse = color | (bboxmaterial.diffuse & 0xff);
        }
        const SbMatrix *matrix = ventry.identity ? nullptr : &ventry.matrix;
        switch(material.type) {
        case Material::Point:
          if (pd && pd->getCoordinateIndex() >= 0) {
            detailentry = &ventry;
            ventry.cache->getPointsBoundingBox(nullptr, bbox, pd->getCoordinateIndex());
          } else if (!detail)
            ventry.cache->getPointsBoundingBox(matrix, bbox);
          break;
        case Material::Line:
          if (ld && ld->getLineIndex() >= 0) {
            detailentry = &ventry;
            ventry.cache->getLinesBoundingBox(nullptr, bbox, ld->getLineIndex());
          } else if (!detail)
            ventry.cache->getLinesBoundingBox(matrix, bbox);
          break;
        case Material::Triangle:
          if (fd && fd->getPartIndex() >= 0) {
            detailentry = &ventry;
            ventry.cache->getTrianglesBoundingBox(nullptr, bbox, fd->getPartIndex());
          } else if (!detail)
            ventry.cache->getTrianglesBoundingBox(matrix, bbox);
          break;
        }
        if (!(flags & SoFCRenderCache::AltGroup))
          continue;
        color = 0;
      }

      if (color) {
        if (order <= 0 && detail)
            material.polygonoffsetstyle = 0;
        if (material.type != Material::Triangle)
          material.lightmodel = SoLazyElement::BASE_COLOR;
        if (material.lightmodel != SoLazyElement::BASE_COLOR && detail) {
          material.emissive = color | 0xff;
          makeDistinctColor(material.emissive, material.emissive, material.diffuse);
          // the scalar override is the authority now (consumers treat a
          // present array as authoritative for its channel)
          material.emissives.reset();
        }
        uint32_t c = material.diffuse;
        material.diffuse = color | (material.diffuse & 0xff);
        makeDistinctColor(material.diffuse, material.diffuse, c);
        material.pervertexcolor = false;
      }

      VertexCacheEntry newentry(ventry);

      auto addWholeOnTop = [&](bool partial = false) {
        if (!wholeontop)
          return;
        Material m = child.first;
        m.order = material.order;
        m.depthfunc = material.depthfunc;
        if (partial) {
          m.partialhighlight = 1;
          material.partialhighlight = -1;
          ++material.order;
        }
        else if (m.type != Material::Triangle) {
          m.overrideflags.set(Material::FLAG_TRANSPARENCY);
          m.diffuse = (m.diffuse & 0xffffff00) | 0xff;
        }

        if (m.type == Material::Triangle && alpha != 0xff) {
          m.overrideflags.set(Material::FLAG_TRANSPARENCY);
          m.diffuse = (m.diffuse & 0xffffff00) | (material.diffuse & 0xff);
        }
        res[m].push_back(ventry);
        ++entrycount;
        if (ventry.mergecount)
          ++mergecount;
      };

      auto checkHighlightIndices = [&](bool newcache) {
        auto cache = newentry.cache->checkHighlightIndices(&newentry.partidx, newcache);
        if (!cache) {
          Material m = child.first;
          m.order = material.order;
          m.depthfunc = material.depthfunc;
          m.partialhighlight = 1;
          if (m.type == Material::Triangle && alpha != 0xff) {
            m.overrideflags.set(Material::FLAG_TRANSPARENCY);
            m.diffuse = (m.diffuse & 0xffffff00) | (material.diffuse & 0xff);
          }
          res[m].push_back(ventry);
          ++entrycount;
          if (ventry.mergecount)
            ++mergecount;
          return true;
        }
        if (newentry.partidx >= 0 || cache != newentry.cache) {
          newentry.cache = cache;
          addWholeOnTop(true);
        }
        return false;
      };

      switch(material.type) {
      case Material::Point:
        if (!material.order)
          material.order = 1;
        if (!elementselectable) {
          if (!detail) {
            if (checkHighlightIndices(false))
              continue;
            res[material].push_back(ventry);
          } else {
            Material m = child.first;
            m.order = material.order;
            m.depthfunc = material.depthfunc;
            m.partialhighlight = 1;
            res[m].push_back(ventry);
          }
          ++entrycount;
          if (ventry.mergecount)
            ++mergecount;
          continue;
        }
        else if (pd) {
          addWholeOnTop();
          if (pd->getCoordinateIndex() >= 0)
            newentry.partidx = pd->getCoordinateIndex();
          else
            continue;
        }
        else if (d) {
          addWholeOnTop();
          const auto & indices = d->getIndices(SoFCDetail::Vertex);
          if (indices.size() == 1 && *indices.begin() >= 0) {
            newentry.partidx = *indices.begin();
          } else if (indices.size() > 1) {
            newentry.cache = new SoFCVertexCache(*newentry.cache);
            newentry.cache->addPoints(indices);
          } else
            continue;
        }
        else if (checkindices) {
          if (checkHighlightIndices(true))
            continue;
        } else if (detail) {
          addWholeOnTop();
          continue;
        }
        break;

      case Material::Line:
        if (!elementselectable) {
          if (!detail) {
            if (checkHighlightIndices(false))
              continue;
            res[material].push_back(ventry);
          } else {
            Material m = child.first;
            m.order = material.order;
            m.depthfunc = material.depthfunc;
            m.partialhighlight = 1;
            res[m].push_back(ventry);
          }
          ++entrycount;
          if (ventry.mergecount)
            ++mergecount;
          continue;
        }
        else if (ld) {
          addWholeOnTop();
          if (ld->getLineIndex() >= 0) {
            // Because of possible line strip, we do not use partidx for
            // partial rendering
            //
            // newentry.partidx = ld->getLineIndex();
            newentry.cache = new SoFCVertexCache(*newentry.cache);
            newentry.cache->addLines(SbFCVector<int>(1, ld->getLineIndex()));
          } else
            continue;
        }
        else if (d) {
          addWholeOnTop();
          const auto & indices = d->getIndices(SoFCDetail::Edge);
          if (indices.size() && *indices.begin()>=0) {
            newentry.cache = new SoFCVertexCache(*newentry.cache);
            newentry.cache->addLines(indices);
          } else
            continue;
        }
        else if (checkindices) {
          if (checkHighlightIndices(true))
            continue;
        } else if (detail) {
          addWholeOnTop();
          continue;
        }
        break;

      case Material::Triangle:
        if (alpha != 0xff) {
          uint32_t a = (child.first.diffuse & 0xff);
          if (child.first.pervertexcolor && ventry.cache->hasTransparency()) {
            if (a == 0xff)
              a = alpha;
          }
          if (a > alpha)
            a = alpha;
          material.overrideflags.set(Material::FLAG_TRANSPARENCY);
          material.diffuse = (material.diffuse & 0xffffff00) | a;
        }
        if (!elementselectable) {
          if (!detail) {
            if (checkHighlightIndices(false))
              continue;
            res[material].push_back(ventry);
          } else {
            Material m = child.first;
            m.order = material.order;
            m.depthfunc = material.depthfunc;
            m.partialhighlight = 1;
            res[m].push_back(ventry);
          }
          ++entrycount;
          if (ventry.mergecount)
            ++mergecount;
          continue;
        }
        else if (fd) {
          addWholeOnTop();
          if (fd->getPartIndex() >= 0)
            newentry.partidx = fd->getPartIndex();
          else
            continue;
        }
        else if (d) {
          addWholeOnTop();
          const auto & indices = d->getIndices(SoFCDetail::Face);
          if (indices.size() == 1 && *indices.begin() >= 0)
            newentry.partidx = *indices.begin();
          else if (indices.size() > 1) {
            newentry.cache = new SoFCVertexCache(*newentry.cache);
            newentry.cache->addTriangles(indices);
          } else
            continue;
        }
        else if (checkindices) {
          if (checkHighlightIndices(true))
            continue;
        } else if (detail) {
          addWholeOnTop();
          continue;
        }

        if (color && newentry.partidx >= 0) {
          uint32_t col = newentry.cache->getFaceColor(newentry.partidx);
          if ((col & 0xff) != 0xff && alpha == 0xff) {
            uint32_t a = static_cast<uint32_t>(ViewParams::getSelectionTransparency() * 255);
            material.diffuse = (material.diffuse & ~0xff) | std::max(a, col&0xff);
          }
          makeDistinctColor(material.diffuse, material.diffuse, col);
          if (material.lightmodel != SoLazyElement::BASE_COLOR) {
            material.emissive = material.diffuse | 0xff;
            material.emissives.reset();
          }
        }
        break;
      }
      res[material].push_back(newentry);
      ++entrycount;
      if (newentry.mergecount)
        ++mergecount;
    }
  }

  color = _color;

  FC_TRACE("highlight cache " << res.size() << " materials, "
        << entrycount << " entries, "
        << mergecount << " merged caches, "
        << PRIVATE(this)->facecount << " faces");

  // if (!bbox.isEmpty() && res.empty()) {
  if (isValidBBox(bbox)) {
    SbVec3f unitsize(1.f, 1.f, 1.f);
    auto size = bbox.getSize();
    int cacheid = 0;
    if (size[0] < 1e-6f) {
      unitsize[0] = 0.f;
      size[0] = 1.f;
      if (size[1] < 1e-6f) {
        unitsize[1] = 0.f;
        size[1] = 1.f;
        if (size[2] < 1e-6f) {
          unitsize[2] = 0.f;
          size[2] = 1.f;
          cacheid = 1;
        } else
          cacheid = 2;
      } else if (size[2] < 1e-6f) {
        unitsize[2] = 0.f;
        size[2] = 1.f;
        cacheid = 3;
      }
    } else if (size[1] < 1e-6f) {
      unitsize[1] = 0.f;
      size[1] = 1.f;
      if (size[2] < 1e-6f) {
        unitsize[2] = 0.f;
        size[2] = 1.f;
        cacheid = 4;
      } else
        cacheid = 5;
    } else if (size[2] < 1e-6f) {
      unitsize[2] = 0.f;
      size[2] = 1.f;
      cacheid = 6;
    } else
      cacheid = 7;
    auto &cache = sharedcache[cacheid];
    if (!cache) 
      cache = new SoFCVertexCache(SbBox3f(SbVec3f(0.f, 0.f, 0.f), unitsize));

    SbMatrix matrix;
    matrix.setTransform(bbox.getMin(), SbRotation(), size, SbRotation());
    if (detailentry && !detailentry->identity)
      matrix.multRight(detailentry->matrix);

    bboxmaterial.type = cache->getNumLineIndices() ? Material::Line : Material::Point;
    bboxmaterial.diffuse = color | 0xff;
    bboxmaterial.linewidth = ViewParams::getSelectionBBoxLineWidth();
    bboxmaterial.pointsize = bboxmaterial.linewidth * 2;
    bboxmaterial.depthclamp = true;
    bboxmaterial.overrideflags.set(Material::FLAG_NO_TEXTURE);
    bboxmaterial.textures.clear();
    bboxmaterial.bumpmaps.clear();
    bboxmaterial.emissivemaps.clear();
    bboxmaterial.occlusionmaps.clear();
    bboxmaterial.metallicroughnessmaps.clear();
    bboxmaterial.facetextures.clear();
    bboxmaterial.facetextureindices.reset();
    bboxmaterial.texturematrices.clear();
    bboxmaterial.usershader.reset();
    bboxmaterial.finishpalette.reset();
    bboxmaterial.finishindices.reset();
    bboxmaterial.framepalette.reset();
    bboxmaterial.frameindices.reset();

    res[bboxmaterial].emplace_back(cache, matrix, false, false, CacheKeyPtr());
  }

  return res;
}

long
SoFCRenderCache::getCacheEntryCount()
{
  return CacheEntryCount;
}

// vim: noai:ts=2:sw=2
