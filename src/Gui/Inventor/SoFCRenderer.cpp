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

#ifndef FC_OS_WIN32
# ifndef GL_GLEXT_PROTOTYPES
# define GL_GLEXT_PROTOTYPES 1
# endif
#endif

# ifdef FC_OS_WIN32
#  include <windows.h>
#  include <GL/gl.h>
#  include <GL/glext.h>
# else
#  ifdef FC_OS_MACOSX
#   include <OpenGL/gl.h>
#   include <OpenGL/glext.h>
#  else
#   include <GL/gl.h>
#   include <GL/glext.h>
#  endif //FC_OS_MACOSX
# endif //FC_OS_WIN32
// Should come after glext.h to avoid warnings
# include <Inventor/C/glue/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoTextureEnabledElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoOverrideElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoGLLazyElement.h>
#include <Inventor/elements/SoLinePatternElement.h>
#include <Inventor/elements/SoLineWidthElement.h>
#include <Inventor/elements/SoPointSizeElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoMaterialBindingElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/elements/SoProjectionMatrixElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoTextureUnitElement.h>
#include <Inventor/elements/SoMultiTextureEnabledElement.h>
#include <Inventor/elements/SoMultiTextureImageElement.h>
#include <Inventor/elements/SoMultiTextureMatrixElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoLightModelElement.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoClipPlaneElement.h>
#include <Inventor/elements/SoCullElement.h>
#include <Inventor/elements/SoGLShaderProgramElement.h>
#include <Inventor/sensors/SoFieldSensor.h>
#include <Inventor/annex/FXViz/elements/SoShadowStyleElement.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowSpotLight.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbBox3f.h>
#include <Inventor/SbSphere.h>
#include <Inventor/SbRotation.h>

#include <Base/Console.h>
#include "SoAutoZoomTranslation.h"
#include "SoFCRenderer.h"
#include "SoFCRenderCache.h"
#include "SoFCRendererBridge.h"
#include "SoFCVertexCache.h"
#include "SoFCDisplayModeElement.h"
#include "../Renderer/Renderer.h"
#include "../RenderTiming.h"
#include "../ViewParams.h"

FC_LOG_LEVEL_INIT("Renderer", true, true)

using namespace Gui;

typedef SoFCRenderCache::Material Material;
typedef SoFCRenderCache::VertexCacheEntry VertexCacheEntry;
typedef SoFCRenderCache::VertexCacheMap VertexCacheMap;
typedef SoFCRenderCache::CacheKey CacheKey;
typedef SoFCRenderCache::CacheKeyPtr CacheKeyPtr;
typedef Gui::CoinPtr<SoFCRenderCache> RenderCachePtr;
typedef Gui::CoinPtr<SoFCVertexCache> VertexCachePtr;

#define PRIVATE(obj) ((obj)->pimpl)

/** Signatures for the entry points resolved through Coin's glue.
 *
 * Apple ships the PFNGL* names only in the Core-profile <OpenGL/gl3.h>,
 * which cannot be included beside the compatibility <OpenGL/gl.h> that
 * Coin's glue pulls in -- its own <OpenGL/glext.h> spells the same thing
 * glBlendColorEXTProcPtr. cc_glglue_getprocaddress() hands back an
 * untyped pointer either way, so name the signatures here rather than
 * depend on which GL header a platform happens to carry.
 */
#if defined(__APPLE__)
typedef void (*FCGLBlendColorProc)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*FCGLActiveTextureProc)(GLenum);
#else
typedef PFNGLBLENDCOLORPROC FCGLBlendColorProc;
typedef PFNGLACTIVETEXTUREPROC FCGLActiveTextureProc;
#endif


#define FC_GLERROR_CHECK _check_glerror(__LINE__)
  
static inline void
_check_glerror(int line) {
  if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
      _FC_ERR(__FILE__, line, "GL error: " << err);
  }
}

/** Does this driver actually honour GL_CONSTANT_ALPHA blending?
 *
 * There is no query for it, so it has to be drawn: white over black at a
 * constant alpha of 0.5 must land near mid grey. Mesa's d3d12 driver on
 * WSLg accepts glBlendColor and reads the value back through
 * GL_BLEND_COLOR, then blends as though the constant were zero, which
 * drops the draw entirely.
 *
 * Answering "cannot tell" as true keeps the behaviour drivers had before
 * there was a check at all.
 *
 * The entry points come through Coin's glue because Windows exports only
 * GL 1.1 from opengl32, so none of this links there directly.
 *
 * ⚠️ It needs a target of its own. The on-screen target is a multisampled
 * FBO, and glReadPixels on one of those is an INVALID_OPERATION that
 * returns nothing -- probing the widget's own framebuffer would report
 * every driver as broken.
 */
static bool
_constantAlphaBlendWorks(const cc_glglue * glue, FCGLBlendColorProc blendColor)
{
  if (!glue || !blendColor || !cc_glglue_has_framebuffer_objects(glue))
    return true;

  GLint prevfbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevfbo);

  GLuint fbo = 0, rbo = 0;
  cc_glglue_glGenFramebuffers(glue, 1, &fbo);
  cc_glglue_glGenRenderbuffers(glue, 1, &rbo);
  cc_glglue_glBindRenderbuffer(glue, GL_RENDERBUFFER, rbo);
  cc_glglue_glRenderbufferStorage(glue, GL_RENDERBUFFER, GL_RGBA8, 1, 1);
  cc_glglue_glBindFramebuffer(glue, GL_FRAMEBUFFER, fbo);
  cc_glglue_glFramebufferRenderbuffer(glue, GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, rbo);

  bool works = true;
  if (cc_glglue_glCheckFramebufferStatus(glue, GL_FRAMEBUFFER)
        == GL_FRAMEBUFFER_COMPLETE)
  {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    blendColor(0.0F, 0.0F, 0.0F, 0.5F);
    glBlendFunc(GL_CONSTANT_ALPHA_EXT, GL_ONE_MINUS_CONSTANT_ALPHA_EXT);
    glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex3f(-1.0F, -1.0F, 0.0F);
    glVertex3f( 1.0F, -1.0F, 0.0F);
    glVertex3f(-1.0F,  1.0F, 0.0F);
    glVertex3f( 1.0F,  1.0F, 0.0F);
    glEnd();

    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();

    // A driver that ignores the constant lands on 0 (as though it were
    // zero) or 255 (as though one); an honest one lands near 127.
    works = px[0] > 64 && px[0] < 192;
  }

  cc_glglue_glBindFramebuffer(glue, GL_FRAMEBUFFER, (GLuint)prevfbo);
  cc_glglue_glDeleteFramebuffers(glue, 1, &fbo);
  cc_glglue_glDeleteRenderbuffers(glue, 1, &rbo);
  while (glGetError() != GL_NO_ERROR) { }   // the probe's own errors, only

  return works;
}

static inline void
setDepthFunc(int depthfunc)
{
    switch (depthfunc) {
    case SoDepthBuffer::NEVER:     glDepthFunc(GL_NEVER);     break;
    case SoDepthBuffer::ALWAYS:    glDepthFunc(GL_ALWAYS);    break;
    case SoDepthBuffer::LESS:      glDepthFunc(GL_LESS);      break;
    case SoDepthBuffer::LEQUAL:    glDepthFunc(GL_LEQUAL);    break;
    case SoDepthBuffer::EQUAL:     glDepthFunc(GL_EQUAL);     break;
    case SoDepthBuffer::GEQUAL:    glDepthFunc(GL_GEQUAL);    break;
    case SoDepthBuffer::GREATER:   glDepthFunc(GL_GREATER);   break;
    case SoDepthBuffer::NOTEQUAL:  glDepthFunc(GL_NOTEQUAL);  break;
    }
    FC_GLERROR_CHECK;
}

typedef SoFCRenderCache::CacheKeySet CacheKeySet;
typedef SoFCRenderCache::CacheKeyHasher CacheKeyHasher;

struct DrawEntry {
  const Material * material;
  const VertexCacheEntry * ventry;
  SbBox3f bbox;
  int skip;

  DrawEntry(const Material * m, const VertexCacheEntry * v)
    :material(m), ventry(v), skip(0)
  {
    this->bbox = v->getBoundingBox();
  }
};

struct DrawEntryIndex {
  std::size_t idx;
  float distance;
  DrawEntryIndex(std::size_t i)
    : idx(i)
  {}
};

enum RenderPass {
  RenderPassNormal            = 0,
  RenderPassLineSolid         = 1,
  RenderPassLinePattern       = 2,
  RenderPassLineMask          = 3,
  RenderPassHighlight         = 4,
  RenderPassSectionFill       = 8,
  RenderPassSelectionOutline  = 9,
};

struct HatchTexture
{
  const void *key = nullptr;
  SbFCVector<unsigned char> data;
  GLuint texture = 0;
  FC_COIN_COUNTER(int) refcount = 0;
  int width = 100;
  int height = 100;
  int nc = 0;
};

class SoFCRendererP {
public:
  SoFCRendererP()
  {
    this->updateselection = false;
    memset(this->stats, 0, sizeof(this->stats));
    dummynode = new SoGroup;
  }

  ~SoFCRendererP()
  {
  }

  void deleteHatchTexture();

  bool applyMaterial(SoGLRenderAction * action,
                     const Material & next,
                     bool transp,
                     int pass = RenderPassNormal);

  void setupMatrix(SoGLRenderAction * action, const DrawEntry &draw_entry);

  static void applyBillboard(SoState * state, const SoAutoZoomTranslation * zoom);

  void updateSelection();

  static std::size_t pushDrawEntry(SbFCVector<DrawEntry> & draw_entries,
                                   const Material & material,
                                   const VertexCacheEntry & ventry);

  bool renderSection(SoGLRenderAction *action, DrawEntry &draw_entry, int &pass, bool &pushed, bool transp);
  void _renderSection(SoGLRenderAction *action, DrawEntry **pp_draw_entry, size_t count, int &pass, bool &pushed, bool setupmatrix);
  void renderSectionGrouped(SoGLRenderAction *action, bool transp);

  void renderOutline(SoGLRenderAction *action, DrawEntry &draw_entry, bool highlight);
  void renderSceneOutline(SoGLRenderAction *action);

  void pauseShadowRender(SoState *state, bool paused);
  void renderLines(SoState *state, int array, DrawEntry &draw_entry);
  void renderPoints(SoGLRenderAction *action, int array, DrawEntry &draw_entry);
  void renderTriangles(const DrawEntry &draw_entry,
                       SoGLRenderAction *action,
                       const int arrays = SoFCVertexCache::ALL,
                       int part = -1,
                       const SbPlane *plane = nullptr);

  void renderOpaque(SoGLRenderAction * action,
                    SbFCVector<DrawEntry> & draw_entries,
                    SbFCVector<std::size_t> & indices,
                    int pass = RenderPassNormal);

  void renderTransparency(SoGLRenderAction * action,
                          SbFCVector<DrawEntry> & draw_entries,
                          SbFCVector<DrawEntryIndex> & indices,
                          bool sort=true);

  void applyKeys(const CacheKeySet &keys, int skip=1);
  void changeKey(const CacheKeySet &keys, int idx, int skip);

  SbFCVector<DrawEntry> drawentries;
  SbFCVector<DrawEntry> slentries;
  SbFCVector<DrawEntry> hlentries; 

  SbFCVector<std::size_t> opaquevcache;
  SbFCVector<std::size_t> opaqueontop;
  SbFCVector<std::size_t> opaqueselections;
  SbFCVector<std::size_t> opaquehighlight;
  SbFCVector<std::size_t> opaquelineshighlight; // has both lines and points
  SbFCVector<std::size_t> linesontop; // has both lines and points
  SbFCVector<std::size_t> trianglesontop;

  SbPlane prevplane;
  SbFCVector<DrawEntryIndex> transpvcache;
  SbFCVector<DrawEntryIndex> transpontop;
  SbFCVector<DrawEntryIndex> transpselections;
  SbFCVector<DrawEntryIndex> transphighlight;

  SbFCMap<int, const VertexCacheMap *> selections;
  SbFCMap<int, const VertexCacheMap *> selectionsontop;
  SbFCVector<DrawEntryIndex> transpselectionsontop; // whole on top
  SbFCVector<DrawEntryIndex> transpselectionsfaceontop; // face selection on top
  SbFCVector<std::size_t> selstriangleontop;
  SbFCVector<std::size_t> selsontop; // include only non-explicitly selected lines and points
  SbFCVector<std::size_t> selslineontop; // include only explicitly selected lines
  SbFCVector<std::size_t> selspointontop; // include only explicitly selected points
  SbFCVector<std::size_t> seloutline; // include all explicitly selected triangles for outline rendering
  SbFCVector<std::size_t> highlightlinesontop; // include pre-selected lines and points
  SbFCVector<std::size_t> preseloutline; // include preselected triangles for outline rendering
  bool updateselection;

  SbFCVector<DrawEntry*> section_entries;
  SbFCVector<DrawEntry*> transp_section_entries;

  std::unordered_map<CacheKeyPtr,
                     SbFCVector<std::size_t>,
                     CacheKeyHasher,
                     CacheKeyHasher> cachetable;

  VertexCacheMap highlightcaches;
  CacheKeySet highlightkeys;
  CacheKeySet selectionkeys;
  CacheKeyPtr selkey;

  RenderCachePtr scene;

  SbBox3f scenebbox;
  SbBox3f highlightbbox;
  SbBox3f selectionbbox;

  Material material;
  const Material * prevmaterial;
  bool recheckmaterial;
  int prevpass;

  SbMatrix matrix;
  bool identity;

  uint32_t highlightcolor;
  bool notexture;
  bool depthwriteonly;
  bool hlwholeontop = false;

  bool shadowrenderpaused = false;
  bool shadowrendering = false;
  bool shadowmapping = false;
  bool transpshadowmapping = false;

  SoFCDisplayModeElement::HiddenLineConfig hiddenLineConfig;
  bool showHiddenLine = false;

  HatchTexture *hatchtexture = nullptr;

  // The owning 3D view object, for the Section_* style overrides below.
  // Held whether or not a backend is attached, since the internal GL pass
  // honors them too.
  App::PropertyContainer *viewobject = nullptr;
  // The section/clipping style of the frame being drawn, snapshotted once
  // per render() from the view's Section_* overrides and the preferences
  // behind them. Snapshotted because these are read per draw entry, deep
  // inside the loops below.
  struct SectionStyle {
    bool fill = false;
    bool fillInvert = false;
    bool fillGroup = false;
    bool concave = false;
    bool hatch = false;
    bool noOnTop = true;
    double hatchScale = 1.0;
  } section;
  void updateSectionStyle();

  /// The two style keys that decide whether an on-top draw is sectioned,
  /// for the bridge to bake into the draw calls it translates. Resolved
  /// afresh instead of read from the snapshot above: a feed runs before
  /// the frame that follows it takes its snapshot (the cache manager
  /// republishes into setScene(), then calls render()), so the snapshot
  /// would answer with the style of the frame before the change. There is
  /// one of these per feed, against one per draw entry for the snapshot.
  RendererBridge::SectionOnTop sectionOnTop() const;

  /// Translate everything this renderer holds into the attached backend:
  /// the scene, every selection feed and the highlight. Used when a
  /// backend is attached mid-session, and when something baked into the
  /// translated draws has changed.
  void feedExternal();

  // Optional external render backend mirroring the scene/selection feeds.
  Render::Renderer *external = nullptr;
  // Owning 3D view of the external backend, for per-view dynamic property
  // overrides (Render_*/Shadow_*) in the per-frame config feed.
  App::PropertyContainer *externalview = nullptr;
  // The identities the external backend has been told, kept across
  // publishes because an identity is fixed: a publish adds the keys that
  // are new to it and says nothing about the rest
  // (docs/IncrementalPublish.md §4d-iv). Dropped whenever the backend
  // stops being the one this describes -- a new backend, or a cleared
  // scene -- so it can never claim a key the backend does not hold.
  Render::ObjectInfoMap objinfo;
  // Draws the last publish produced, which is what the resident map's
  // size is judged against.
  std::size_t objinfodraws = 0;
  // Which renderer, and which statement of its table, the map above is a
  // claim about. A renderer's address is not enough: a backend rebuilt on
  // a preference change can be allocated where the last one was, and
  // sending deltas to it against the dead one's table would leave every
  // object already in the scene permanently nameless to a viewer.
  uint64_t objinfoinstance = 0;
  uint32_t objinfoversion = 0;

  /// Whether the external backend still holds the table objinfo claims.
  /// False means the map describes something else and has to be stated
  /// whole rather than added to.
  bool objinfoInSync() const
  {
    return external && external->instanceId() == objinfoinstance
        && external->objectInfoVersion() == objinfoversion;
  }

  /// Record the backend's token after stating or updating the table.
  void objinfoSynced()
  {
    if (!external)
      return;
    objinfoinstance = external->instanceId();
    objinfoversion = external->objectInfoVersion();
  }
  // Overlay-capture mode (setExternalOverlay): the scene feed routes to
  // external->setOverlay(overlayid, ..., overlayanchor) and render() is a
  // no-op.
  bool overlaymode = false;
  int overlayid = 0;
  Render::OverlayAnchor overlayanchor;

  // User shader programs captured from scene SoShaderProgram nodes on the
  // last cache rebuild (docs/RenderDebug.md §6); pushed to the external
  // backend with the other per-frame configs.
  Render::UserShaderConfig usershaders;
  // Scene-level shaders from empty-target App::ShaderBinding bindings
  // (§6.5), independent of scene recapture.
  std::vector<Render::UserShader> appearanceshaders;
  // What the backend gets: captured node shaders first, appearance
  // shaders after (the last shader on a stage wins). Rebuilt on either
  // setter instead of per frame.
  Render::UserShaderConfig mergedshaders;

  void mergeUserShaders()
  {
    mergedshaders = usershaders;
    mergedshaders.shaders.insert(mergedshaders.shaders.end(),
                                 appearanceshaders.begin(),
                                 appearanceshaders.end());
  }

  char stats[512];
  int drawcallcount;

  CoinPtr<SoNode> dummynode;
  SbColor sbcolor;
  SbColor *fromPackedColor(uint32_t col)
  {
    float t;
    sbcolor.setPackedValue(col, t);
    return &sbcolor;
  }
  uint32_t diffusecolor;
};

static std::map<const void *, HatchTexture> _HatchTextures;

SoFCRenderer::SoFCRenderer()
  : pimpl(new SoFCRendererP)
{
}

SoFCRenderer::~SoFCRenderer()
{
  PRIVATE(this)->deleteHatchTexture();
  delete pimpl;
}

void
SoFCRendererP::deleteHatchTexture()
{
  if (!this->hatchtexture || --this->hatchtexture->refcount)
    return;
  if (this->hatchtexture->texture)
    glDeleteTextures(1, &this->hatchtexture->texture);
  _HatchTextures.erase(this->hatchtexture->key);
  this->hatchtexture = nullptr;
}

void
SoFCRenderer::setHatchImage(const void *dataptr, int nc, int width, int height)
{
  if (PRIVATE(this)->external)
    PRIVATE(this)->external->setHatchImage(dataptr, nc, width, height);

  if (!dataptr) {
    PRIVATE(this)->deleteHatchTexture();
    return;
  }

  auto &info = _HatchTextures[dataptr];
  if (&info == PRIVATE(this)->hatchtexture)
    return;

  PRIVATE(this)->deleteHatchTexture();
  if (++info.refcount == 1) {
    info.width = width;
    info.height = height;
    info.nc = nc;
    info.key = dataptr;
    info.data.resize(nc * width * height);
    memcpy(&info.data[0], dataptr, info.data.size());
  }
  PRIVATE(this)->hatchtexture = &info;
}

static inline void
setGLColor(int name, uint32_t col)
{
  GLfloat c[4];
  c[0] = ((col >> 24)&0xff)/255.0f;
  c[1] = ((col >> 16)&0xff)/255.0f;
  c[2] = ((col >> 8)&0xff)/255.0f;
  c[3] = 1.0f;
  glMaterialfv(GL_FRONT_AND_BACK, name, c);
  FC_GLERROR_CHECK;
}

static inline void
setGLFeature(int name, int current, int next, int mask)
{
  if ((current & mask) && !(next & mask))
    glDisable(name);
  else if (!(current & mask) && (next & mask))
    glEnable(name);
  FC_GLERROR_CHECK;
}

static const SbMatrix matrixidentity(SbMatrix::identity());

bool
SoFCRendererP::applyMaterial(SoGLRenderAction * action,
                             const Material & next,
                             bool transp,
                             int pass)
{
  bool first = this->prevmaterial == nullptr;
  SoState * state = action->getState();

  if (this->shadowmapping
      && (next.isOnTop() || !(next.shadowstyle & SoShadowStyleElement::CASTS_SHADOW)))
  {
    return false;
  }

  // depth buffer write without color
  if (this->depthwriteonly) {
    // disable any texture
    if (this->material.textures.getNum()) {
      this->material.textures.clear();
      state->pop();
      state->push();
    }
    // disable lighting
    if (this->material.lightmodel != SoLazyElement::BASE_COLOR) {
      this->material.lightmodel = SoLazyElement::BASE_COLOR;
      SoLazyElement::setLightModel(state, this->material.lightmodel);
      glDisable(GL_LIGHTING);
      FC_GLERROR_CHECK;
    }
    // disable per vertex color
    this->material.pervertexcolor = false;
    // enable depth write
    if (!this->material.depthwrite) {
      this->material.depthwrite = true;
      glDepthMask(GL_TRUE);
      FC_GLERROR_CHECK;
    }
    // force GL_LESS depth function
    if (this->material.depthfunc != SoDepthBuffer::LESS) {
      this->material.depthfunc = SoDepthBuffer::LESS;
      glDepthFunc(GL_LESS);
      FC_GLERROR_CHECK;
    }
    // enable depth test
    if (!this->material.depthtest) {
      this->material.depthtest = true;
      glEnable(GL_DEPTH_TEST);
      FC_GLERROR_CHECK;
    }
    return true;
  }

  this->material.pervertexcolor = next.pervertexcolor;

  auto clippers = next.clippers;
  if (this->shadowmapping
      || ((this->section.noOnTop
          || (this->section.concave && clippers.getNum() > 1))
          && next.isOnTop()))
    clippers.clear();

  bool clipperchanged = first || this->material.clippers != clippers;
  bool texturechanged = clipperchanged
            || this->material.textures != next.textures;
  bool lightchanged = texturechanged
            || this->material.lights != next.lights;

  if (clipperchanged || texturechanged || lightchanged) {
    state->pop();
    state->push();

    if (clippers.getNum()) {
      for(auto & info : clippers.getData()) {
        if (!info.identity)
          SoModelMatrixElement::set(state, NULL, info.matrix);
        state->setCacheOpen(false);
        info.node->GLRender(action);
        if (!info.identity)
          SoModelMatrixElement::makeIdentity(state, NULL);
      }
    }
    this->material.clippers = clippers;

    if (!this->notexture && texturechanged) {
      if (next.textures.getNum()) {
        for (auto & texentry : next.textures.getData()) {
          auto t = this->material.textures.get(texentry.first);
          if (t && *t == texentry.second)
            continue;
          SoMultiTextureMatrixElement::set(state, NULL, texentry.first,
              texentry.second.identity ? matrixidentity : texentry.second.matrix);
          SoTextureUnitElement::set(state, NULL, texentry.first);
          state->setCacheOpen(false);
          texentry.second.texture->GLRender(action);
        }
      }
      this->material.textures = next.textures;
    }

    if (lightchanged) {
      if (next.lights.getNum()) {
        for(auto & info : next.lights.getData()) {
          if (!info.identity)
            SoModelMatrixElement::set(state, NULL, info.matrix);
          state->setCacheOpen(false);
          info.node->GLRender(action);
          if (!info.identity)
            SoModelMatrixElement::makeIdentity(state, NULL);
        }
      }
      this->material.lights = next.lights;
    }
  }

  bool depthtest = next.isOnTop() ? false : next.depthtest;
  bool depthwrite = !next.isOnTop() && transp ? false : next.depthwrite;
  int8_t depthfunc = next.depthfunc;
  uint32_t linepattern = next.linepattern;
  uint32_t col = next.diffuse;
  uint32_t emissive = next.emissive;
  auto overrideflags = next.overrideflags;
  float linewidth = next.linewidth;
  float pointsize = next.pointsize;

  if ((pass & RenderPassLineMask) == RenderPassLinePattern) {
    if (pass == RenderPassLinePattern) {
      transp = true;
      // TransparencyOnTop is a transparency, but this byte is an alpha with
      // 0xff meaning opaque -- SoFCRenderCache's convention, and Coin's in
      // SbColor::getPackedValue(). Inverting is what keeps
      // TransparencyOnTop = 0 meaning a solid on-top line rather than an
      // invisible one. Same correction as View3DInventorSelection.cpp.
      float t = std::min(std::max((float)ViewParams::getTransparencyOnTop(), 0.0F), 1.0F);
      uint32_t alpha = (uint32_t)((1.0F - t) * 255.0F + 0.5F);
      if (alpha < (col & 0xff))
        col = (col & 0xffffff00) | alpha;
      overrideflags.set(Material::FLAG_TRANSPARENCY);
    }
    depthtest = false;
    uint32_t sellinepattern = ViewParams::getSelectionLinePattern();
    if (sellinepattern && ViewParams::getSelectionLinePatternScale() > 1)
      sellinepattern |= ViewParams::getSelectionLinePatternScale() << 16;

    if (sellinepattern && !next.hasLinePattern())
      linepattern  = sellinepattern;
  }
  else if ((pass & RenderPassLineMask) == RenderPassLineSolid) {
    depthtest = true;
    depthfunc = SoDepthBuffer::LEQUAL;
    depthwrite = false;
  }

  if (pass & RenderPassHighlight) {
    float scale = ViewParams::getSelectionLineThicken();
    if (scale < 1.0)
      scale = 1.0;
    float w = linewidth * scale;
    if (ViewParams::getSelectionLineMaxWidth() > 1.0)
      w = std::min<float>(w, std::max<float>(linewidth, ViewParams::getSelectionLineMaxWidth()));
    linewidth = w;

    float pscale = ViewParams::getSelectionPointScale();
    if (pscale < 1.0)
      pscale = scale;
    w = pointsize * pscale;
    if (ViewParams::getSelectionPointMaxSize() > 1.0)
      w = std::min<float>(w, std::max<float>(pointsize, ViewParams::getSelectionPointMaxSize()));
    pointsize = w;
  }

  bool update_depth = false;
  if (first || this->material.depthtest != depthtest) {
    if (depthtest) {
      glEnable(GL_DEPTH_TEST);
    } else {
      glDisable(GL_DEPTH_TEST);
    }
    FC_GLERROR_CHECK;
    this->material.depthtest = depthtest;
    update_depth = true;
  }

  if (first || this->material.depthclamp != next.depthclamp) {
    if (next.depthclamp)
      glEnable(GL_DEPTH_CLAMP);
    else
      glDisable(GL_DEPTH_CLAMP);
    FC_GLERROR_CHECK;
    this->material.depthclamp = next.depthclamp;
  }

  if (first || this->material.depthwrite != depthwrite) {
    glDepthMask(depthwrite ? GL_TRUE : GL_FALSE);
    FC_GLERROR_CHECK;
    this->material.depthwrite = depthwrite;
    update_depth = true;
  }

  if (first || this->material.depthfunc != depthfunc) {
    setDepthFunc(depthfunc);
    this->material.depthfunc = depthfunc;
    update_depth = true;
  }

  if (update_depth)
    SoDepthBufferElement::set(state, depthtest, depthwrite,
        static_cast<SoDepthBufferElement::DepthWriteFunction>(depthfunc),
        SbVec2f(0, 1));

  auto lightmodel = next.lightmodel;
  if (next.type != Material::Triangle)
    lightmodel = SoLazyElement::BASE_COLOR;

  if (first || this->material.lightmodel != lightmodel) {
    SoLazyElement::setLightModel(state, lightmodel);
    if (lightmodel == SoLazyElement::PHONG)
      glEnable(GL_LIGHTING);
    else
      glDisable(GL_LIGHTING);
    FC_GLERROR_CHECK;
    this->material.lightmodel = lightmodel;
  }

  // Always set color because the current color may be changed by opengl draw call
  glColor4ub((unsigned char)((col>>24)&0xff),
              (unsigned char)((col>>16)&0xff),
              (unsigned char)((col>>8)&0xff),
              (unsigned char)(col&0xff));
  diffusecolor = col;
  // in order to make SoLazyElement::setPacked() working
  dummynode->touch();
  SoLazyElement::setPacked(state, dummynode, 1, &diffusecolor, (col&0xff)!=0xff);
  FC_GLERROR_CHECK;

  if (overrideflags != this->material.overrideflags
      || (overrideflags.test(Material::FLAG_TRANSPARENCY)
          && (col&0xff) != (this->material.diffuse&0xff)))
  {
    static bool hasBlendColor = true;
    GLenum sfactor = GL_SRC_ALPHA, dfactor = GL_ONE_MINUS_SRC_ALPHA;
    // A constant-alpha blend is only worth asking for when the geometry
    // carries per-vertex colors: that is the one case where the vertices'
    // own alphas would otherwise win over the override. Without them every
    // fragment already takes its alpha from the glColor4ub above, which is
    // the same number the blend color would carry, so the two blends are
    // arithmetically identical and only one of them is portable.
    //
    // Mesa's d3d12 driver (WSLg) accepts glBlendColor and reports it back
    // through GL_BLEND_COLOR, then blends as though the constant were
    // zero, which drops the draw entirely. Since FLAG_TRANSPARENCY is set
    // only when alpha != 0xff, that turned every partially transparent
    // on-top object invisible there while a fully opaque one still drew.
    if (hasBlendColor && next.pervertexcolor
        && overrideflags.test(Material::FLAG_TRANSPARENCY)) {
#ifdef FC_OS_WIN32
      static FCGLBlendColorProc glBlendColor;
      if (hasBlendColor && !glBlendColor) {
        const cc_glglue * glue = cc_glglue_instance(action->getCacheContext());
        glBlendColor = (FCGLBlendColorProc)cc_glglue_getprocaddress(glue, "glBlendColor");
        hasBlendColor = (glBlendColor != nullptr);
      }
#endif
      // Having the entry point says nothing about the driver doing the
      // arithmetic, so ask it once, the first time one is actually
      // wanted -- geometry that needs a constant alpha is uncommon, and
      // a driver that has the call but ignores it drops the draw
      // silently rather than failing.
      static bool probed = false;
      if (hasBlendColor && !probed) {
        probed = true;
        // No & — on Windows glBlendColor is the glue-resolved pointer
        // variable above, and taking its address yields a pointer to
        // the pointer. Elsewhere it is the GL function itself, which
        // decays to the same pointer type on its own.
        hasBlendColor = _constantAlphaBlendWorks(
            cc_glglue_instance(action->getCacheContext()), glBlendColor);
        if (!hasBlendColor)
          FC_WARN("constant alpha blending is not honoured by this driver; "
                  "an overridden transparency on per-vertex colored geometry "
                  "falls back to source alpha");
      }
      if (hasBlendColor) {
        glBlendColor(0.f, 0.f, 0.f,  (col & 0xff)/255.f);
        sfactor = GL_CONSTANT_ALPHA_EXT;
        dfactor = GL_ONE_MINUS_CONSTANT_ALPHA_EXT;
        FC_GLERROR_CHECK;
      }
    }
    glBlendFunc(sfactor, dfactor);
    SoLazyElement::enableBlending(state, sfactor, dfactor);
    FC_GLERROR_CHECK;
  }

  this->material.overrideflags = overrideflags;
  this->material.diffuse = col;

  // Must clear emission color for lines and points if they are to be rendered
  // with lighting as BASE_COLOR. For some reason, if shadow is enabled
  // (possibly due to extra light source), emission color is taking effect
  // even if lighting is BASE_COLOR.
  if (this->material.lightmodel == SoLazyElement::BASE_COLOR)
    emissive = 0;

  if (first || this->material.emissive != emissive) {
    setGLColor(GL_EMISSION, emissive);
    SoLazyElement::setEmissive(state, fromPackedColor(emissive));
    this->material.emissive = emissive;
  }

  // A triangle draw carrying SoDrawStyle::LINES is handed to
  // glPolygonMode below and comes out as edges, so it wants the line
  // width and pattern as much as a line draw does -- Coin sets both from
  // the elements without asking what the shape is, and without this a
  // dashed bounding box (PartGui's geometry check) drew solid.
  if (next.type == Material::Line
      || next.drawstyle == SoDrawStyleElement::LINES) {
    if (first || this->material.linewidth != linewidth) {
      glLineWidth(linewidth);
      FC_GLERROR_CHECK;
      this->material.linewidth = linewidth;
      SoLineWidthElement::set(state, linewidth);
    }

    if (first || this->material.linepattern != linepattern) {
      GLint factor = linepattern >> 16;
      GLushort pattern = linepattern & 0xffff;
      if (pattern == 0xffff) {
        glDisable(GL_LINE_STIPPLE);
        factor = 1;
      } else {
        glEnable(GL_LINE_STIPPLE);
        if (factor > 256)
          factor = 256;
        else if (factor < 1)
          factor = 1;
        glLineStipple(factor, pattern);
      }
      FC_GLERROR_CHECK;
      this->material.linepattern = linepattern;
      SoLinePatternElement::set(state, pattern, factor);
    }
    // A line draw is done here; a triangle drawn as lines still needs the
    // rest of the material, the polygon mode below most of all.
    if (!first && next.type == Material::Line)
      return true;
  }

  if (next.type == Material::Point) {
    if (first || this->material.pointsize != pointsize) {
      glPointSize(pointsize);
      FC_GLERROR_CHECK;
      this->material.pointsize = pointsize;
      SoPointSizeElement::set(state, pointsize);
    }
    if (!first)
      return true;
  }

  if (first || this->material.ambient != next.ambient) {
    setGLColor(GL_AMBIENT, next.ambient);
    SoLazyElement::setAmbient(state, fromPackedColor(next.ambient));
    this->material.ambient = next.ambient;
  }

  if (first || this->material.specular != next.specular) {
    setGLColor(GL_SPECULAR, next.specular);
    SoLazyElement::setSpecular(state, fromPackedColor(next.specular));
    this->material.specular = next.specular;
  }

  if (first || this->material.shininess != next.shininess) {
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, next.shininess*128.0f);
    FC_GLERROR_CHECK;
    SoLazyElement::setShininess(state, next.shininess);
    this->material.shininess = next.shininess;
  }

  if (first || this->material.vertexordering != next.vertexordering) {
    glFrontFace(next.vertexordering == SoLazyElement::CW ? GL_CW : GL_CCW);
    FC_GLERROR_CHECK;
    SoLazyElement::setVertexOrdering(state,
        static_cast<SoLazyElement::VertexOrdering>(next.vertexordering));
    this->material.vertexordering = next.vertexordering;
  }

  bool twoside = next.twoside;
  if (transp || next.isOnTop())
    twoside = true;
  if (first || this->material.twoside != twoside) {
    SoLazyElement::setTwosideLighting(state, twoside);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, twoside ? GL_TRUE : GL_FALSE);
    FC_GLERROR_CHECK;
    SoLazyElement::setTwosideLighting(state, twoside);
    this->material.twoside = twoside;
  }

  bool culling = next.culling;
  if (transp)
    culling = false;
  if (first || this->material.culling != culling) {
    if (culling) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    FC_GLERROR_CHECK;
    SoLazyElement::setBackfaceCulling(state, culling);
    this->material.culling = culling;
  }

  if (first || this->material.drawstyle != next.drawstyle) {
    switch ((SoDrawStyleElement::Style)next.drawstyle) {
    case SoDrawStyleElement::LINES:
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
      break;
    case SoDrawStyleElement::POINTS:
      glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
      break;
    default:
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    FC_GLERROR_CHECK;
    SoDrawStyleElement::set(state, static_cast<SoDrawStyleElement::Style>(next.drawstyle));
    this->material.drawstyle = next.drawstyle;
  }

  bool update_polygonoffset = false;
  if (first || this->material.polygonoffsetstyle != next.polygonoffsetstyle) {
    setGLFeature(GL_POLYGON_OFFSET_FILL,
                 this->material.polygonoffsetstyle,
                 next.polygonoffsetstyle,
                 SoPolygonOffsetElement::FILLED);
    setGLFeature(GL_POLYGON_OFFSET_LINE,
                 this->material.polygonoffsetstyle,
                 next.polygonoffsetstyle,
                 SoPolygonOffsetElement::LINES);
    setGLFeature(GL_POLYGON_OFFSET_POINT,
                 this->material.polygonoffsetstyle,
                 next.polygonoffsetstyle,
                 SoPolygonOffsetElement::POINTS);
    this->material.polygonoffsetstyle = next.polygonoffsetstyle;
    update_polygonoffset = true;
  }

  if (first || this->material.polygonoffsetfactor != next.polygonoffsetfactor
            || this->material.polygonoffsetunits != next.polygonoffsetunits) {
    glPolygonOffset(next.polygonoffsetfactor, next.polygonoffsetunits);
    FC_GLERROR_CHECK;
    this->material.polygonoffsetfactor = next.polygonoffsetfactor;
    this->material.polygonoffsetunits = next.polygonoffsetunits;
    update_polygonoffset = true;
  }

  if (update_polygonoffset) {
    SoPolygonOffsetElement::set(state,
                                dummynode,
                                next.polygonoffsetfactor,
                                next.polygonoffsetunits,
                                next.polygonoffsetstyle?
                                  static_cast<SoPolygonOffsetElement::Style>(next.polygonoffsetstyle)
                                  :SoPolygonOffsetElement::FILLED,
                                next.polygonoffsetstyle!=0);
  }
  return true;
}

void
SoFCRenderer::setExternalRenderer(Render::Renderer * renderer,
                                  App::PropertyContainer * view)
{
  PRIVATE(this)->externalview = renderer ? view : nullptr;
  if (PRIVATE(this)->external == renderer)
    return;
  PRIVATE(this)->external = renderer;
  // A different backend holds none of the identities the last one was
  // told, so the resident map describes nothing until this re-feed
  // restates it.
  PRIVATE(this)->objinfo.clear();
  if (!renderer)
    return;
  // Feed the current state so a backend attached mid-session (e.g. on a
  // preference change) does not have to wait for the next scene rebuild.
  PRIVATE(this)->feedExternal();
}

void
SoFCRendererP::feedExternal()
{
  // Mirror the live feed paths' translate() context exactly: setScene()
  // resolves the object info map alongside the draws, and the selection /
  // highlight feeds carry their id / highlight flag — without them the
  // materials translate as plain scene draws (dimmed to TransparencyOnTop,
  // stippled, unthickened) and the replayed selection is near-invisible.
  if (this->scene) {
    auto draws = RendererBridge::translate(
          this->scene->getVertexCaches(true),
          this->sectionOnTop(), 0, false, false,
          &this->objinfo);
    this->external->setObjectInfo(Render::ObjectInfoMap(this->objinfo));
    this->objinfodraws = draws.size();
    this->objinfoSynced();
    this->external->setScene(std::move(draws));
  }
  for (auto & sel : this->selections)
    this->external->addSelection(sel.first,
          RendererBridge::translate(*sel.second, this->sectionOnTop(),
                                    sel.first));
  for (auto & sel : this->selectionsontop)
    this->external->addSelection(sel.first,
          RendererBridge::translate(*sel.second, this->sectionOnTop(),
                                    sel.first));
  if (!this->highlightcaches.empty())
    this->external->setHighlight(
          RendererBridge::translate(this->highlightcaches,
                                    this->sectionOnTop(), 0, true),
          this->hlwholeontop);
  if (auto hatch = this->hatchtexture)
    this->external->setHatchImage(hatch->data.data(), hatch->nc,
                                  hatch->width, hatch->height);
}

void
SoFCRenderer::refreshExternalFeed()
{
  auto self = PRIVATE(this);
  if (!self->external)
    return;
  // Everything the backend holds was translated from caches this renderer
  // still has, so a re-bake is a re-translation and nothing more: no
  // traversal, and -- unlike dropping the caches -- the selection and
  // highlight feeds come back with it rather than waiting for the user to
  // select something again.
  if (self->overlaymode) {
    if (self->scene)
      self->external->setOverlay(self->overlayid,
          RendererBridge::translate(self->scene->getVertexCaches(true),
                                    self->sectionOnTop(), 0, false, true),
          self->overlayanchor);
    return;
  }
  self->feedExternal();
}

void
SoFCRenderer::setUserShaders(Render::UserShaderConfig && config)
{
  PRIVATE(this)->usershaders = std::move(config);
  PRIVATE(this)->mergeUserShaders();
}

void
SoFCRenderer::setAppearanceShaders(std::vector<Render::UserShader> && shaders)
{
  PRIVATE(this)->appearanceshaders = std::move(shaders);
  PRIVATE(this)->mergeUserShaders();
}

void
SoFCRenderer::setExternalOverlay(Render::Renderer * renderer, int id,
                                 const Render::OverlayAnchor & anchor)
{
  auto self = PRIVATE(this);
  if (self->external == renderer && self->overlaymode
      && self->overlayid == id && self->overlayanchor == anchor)
    return;
  // Detaching or re-keying: remove the previously fed overlay.
  if (self->external && self->overlaymode
      && (self->external != renderer || self->overlayid != id))
    self->external->removeOverlay(self->overlayid);
  self->overlaymode = (renderer != nullptr);
  self->overlayid = id;
  self->overlayanchor = anchor;
  self->externalview = nullptr;
  self->external = renderer;
  if (renderer && self->scene)
    renderer->setOverlay(id,
        RendererBridge::translate(self->scene->getVertexCaches(true),
                                  self->sectionOnTop(), 0, false, true),
        anchor);
}

void
SoFCRenderer::clear()
{
  if (PRIVATE(this)->external && PRIVATE(this)->overlaymode) {
    PRIVATE(this)->external->removeOverlay(PRIVATE(this)->overlayid);
  }
  else if (PRIVATE(this)->external) {
    for (auto & sel : PRIVATE(this)->selections)
      PRIVATE(this)->external->removeSelection(sel.first);
    for (auto & sel : PRIVATE(this)->selectionsontop)
      PRIVATE(this)->external->removeSelection(sel.first);
    PRIVATE(this)->external->setScene({});
    // Drop both copies together: the resident map is only ever a claim
    // about what the backend holds.
    PRIVATE(this)->external->setObjectInfo({});
    PRIVATE(this)->external->clearHighlight();
  }
  PRIVATE(this)->objinfo.clear();

  PRIVATE(this)->prevplane = SbPlane();
  PRIVATE(this)->opaquevcache.clear();
  PRIVATE(this)->transpvcache.clear();
  PRIVATE(this)->opaqueontop.clear();
  PRIVATE(this)->transpontop.clear();

  PRIVATE(this)->linesontop.clear();
  PRIVATE(this)->trianglesontop.clear();

  PRIVATE(this)->opaqueselections.clear();
  PRIVATE(this)->transpselections.clear();
  PRIVATE(this)->selections.clear();
  PRIVATE(this)->seloutline.clear();
  PRIVATE(this)->preseloutline.clear();
  PRIVATE(this)->selectionsontop.clear();
  PRIVATE(this)->transpselectionsontop.clear();
  PRIVATE(this)->transpselectionsfaceontop.clear();
  PRIVATE(this)->selstriangleontop.clear();
  PRIVATE(this)->selslineontop.clear();
  PRIVATE(this)->selspointontop.clear();
  PRIVATE(this)->selsontop.clear();
  PRIVATE(this)->selectionkeys.clear();

  PRIVATE(this)->highlightcaches.clear();
  PRIVATE(this)->opaquehighlight.clear();
  PRIVATE(this)->opaquelineshighlight.clear();
  PRIVATE(this)->transphighlight.clear();
  PRIVATE(this)->highlightlinesontop.clear();
  PRIVATE(this)->highlightkeys.clear();

  PRIVATE(this)->cachetable.clear();
}

void
SoFCRendererP::changeKey(const CacheKeySet & keys, int idx, int skip)
{
  auto & draw_entry = this->drawentries[idx];
  int prev = draw_entry.skip;
  draw_entry.skip += skip;
  if (!draw_entry.ventry->mergecount)
    return;
  if (!prev && draw_entry.skip) {
    for (int i=1; i<=draw_entry.ventry->mergecount; ++i) {
      changeKey(keys, idx+i, -1);
      i += this->drawentries[idx+i].ventry->mergecount;
    }
  }
  else if (prev && !draw_entry.skip) {
    for (int i=1; i<=draw_entry.ventry->mergecount; ++i) {
      changeKey(keys, idx+i, 1);
      i += this->drawentries[idx+i].ventry->mergecount;
    }
  }
}

void
SoFCRendererP::applyKeys(const CacheKeySet & keys, int skip)
{
  for (auto & key : keys) {
    auto it = this->cachetable.find(key);
    if (it != this->cachetable.end()) {
      for (std::size_t idx : it->second)
        changeKey(keys, idx, skip);
    }
  }
}

void
SoFCRenderer::clearHighlight()
{
  PRIVATE(this)->hlwholeontop = false;
  PRIVATE(this)->highlightcaches.clear();
  PRIVATE(this)->opaquehighlight.clear();
  PRIVATE(this)->opaquelineshighlight.clear();
  PRIVATE(this)->preseloutline.clear();
  PRIVATE(this)->highlightlinesontop.clear();
  PRIVATE(this)->transphighlight.clear();
  PRIVATE(this)->hlentries.clear();
  PRIVATE(this)->applyKeys(PRIVATE(this)->highlightkeys, -1);
  PRIVATE(this)->highlightkeys.clear();
  PRIVATE(this)->highlightbbox = SbBox3f();

  if (PRIVATE(this)->external)
    PRIVATE(this)->external->clearHighlight();
}

inline std::size_t
SoFCRendererP::pushDrawEntry(SbFCVector<DrawEntry> & draw_entries,
                             const Material & material, 
                             const VertexCacheEntry & ventry)
{
  draw_entries.emplace_back(&material, &ventry);
  // if (draw_entries.back().bbox.isEmpty()) {
  //   draw_entries.pop_back();
  //   return 0;
  // }
  return draw_entries.size();
}

const Gui::CoinPtr<SoFCRenderCache> &
SoFCRenderer::getScene() const
{
  return PRIVATE(this)->scene;
}

void
SoFCRenderer::setScene(const RenderCachePtr &cache)
{
  if (!cache) {
    clear();
    return;
  }

  PRIVATE(this)->scenebbox = SbBox3f();
  PRIVATE(this)->prevplane = SbPlane();
  PRIVATE(this)->opaquevcache.clear();
  PRIVATE(this)->opaqueontop.clear();
  PRIVATE(this)->highlightlinesontop.clear();
  PRIVATE(this)->preseloutline.clear();
  PRIVATE(this)->seloutline.clear();
  PRIVATE(this)->transpvcache.clear();
  PRIVATE(this)->transpontop.clear();
  PRIVATE(this)->cachetable.clear();
  PRIVATE(this)->drawentries.clear();
  PRIVATE(this)->linesontop.clear();
  PRIVATE(this)->trianglesontop.clear();

  PRIVATE(this)->scene = cache;
  PRIVATE(this)->scenebbox = SbBox3f();

  int mergecount = 0;
  const SoFCRenderCache::VertexCacheMap * cachesp;
  {
    // Flattening every child cache into one material-keyed map. Memoized
    // per cache object, and therefore recomputed in full whenever the
    // scene cache is rebuilt from scratch.
    Gui::RenderTiming::Scope timing(Gui::RenderTiming::Flatten);
    cachesp = &cache->getVertexCaches(true);
  }
  const auto & caches = *cachesp;

  if (Gui::RenderTiming::enabled()) {
    int nentries = 0;
    for (const auto & v : caches)
      nentries += (int)v.second.size();
    Gui::RenderTiming::noteMapShape((int)caches.size(), nentries);
  }

  Gui::RenderTiming::Scope entrytiming(Gui::RenderTiming::Entries);
  for (const auto & v : caches) {
    auto & material = v.first;
    auto & ventries = v.second;
    if (ventries.empty()) continue;
    if (material.drawstyle == SoDrawStyleElement::INVISIBLE) continue;

    bool fulltransp = material.transptexture;
    if (!fulltransp && !material.pervertexcolor)
      fulltransp = (material.diffuse & 0xff) == 0xff ? false : true;

    int vidx = -1;
    for (auto & ventry : ventries) {
      ++vidx;
      std::size_t idx = SoFCRendererP::pushDrawEntry(PRIVATE(this)->drawentries, material, ventry);
      if (!idx)
        continue;
      --idx;
      if (isValidBBox(PRIVATE(this)->drawentries.back().bbox))
        PRIVATE(this)->scenebbox.extendBy(PRIVATE(this)->drawentries.back().bbox);
      if (!ventry.skipcount)
        ++mergecount;
      else
        PRIVATE(this)->drawentries.back().skip = ventry.skipcount;
      PRIVATE(this)->cachetable[ventry.key].push_back(idx);
      for (int i=0; i<ventry.mergecount; ++i) {
        int j = i+vidx+1;
        if (j >= (int)ventries.size())
          break;
        auto &indices = PRIVATE(this)->cachetable[ventries[j].key];
        if (indices.empty() || indices.back() != idx)
          indices.push_back(idx);
        i += ventries[j].mergecount;
      }

      if (material.isOnTop() && material.type == Material::Triangle)
        PRIVATE(this)->trianglesontop.emplace_back(idx);

      if (!fulltransp && (!material.pervertexcolor
                          || ventry.cache->hasOpaqueParts())) {
        if (material.isOnTop()) {
          if (material.type != Material::Triangle)
            PRIVATE(this)->linesontop.emplace_back(idx);
          else 
            PRIVATE(this)->opaqueontop.emplace_back(idx);
        } else
          PRIVATE(this)->opaquevcache.emplace_back(idx);
      }

      if (fulltransp || (material.pervertexcolor
                          && ventry.cache->hasTransparency())) {
        if (material.isOnTop())
          PRIVATE(this)->transpontop.emplace_back(idx);
        else
          PRIVATE(this)->transpvcache.emplace_back(idx);
      }
    }
  }

  entrytiming.stop();

  FC_TRACE("update scene " << caches.size() << " materials, "
        << PRIVATE(this)->drawentries.size() << " entries, "
        << mergecount << " after merge");

  if (PRIVATE(this)->external) {
    if (PRIVATE(this)->overlaymode)
      PRIVATE(this)->external->setOverlay(PRIVATE(this)->overlayid,
          RendererBridge::translate(caches, PRIVATE(this)->sectionOnTop(),
                                    0, false, true),
          PRIVATE(this)->overlayanchor);
    else {
      // Collect draw identities alongside the draws, against the map the
      // backend already holds: what an objectKey renders cannot change,
      // so a publish only ever has new keys to announce, and a scene that
      // gained nothing announces nothing.
      //
      // The resident map is dropped and restated whole once it has grown
      // well past the scene it describes. Keys leave the scene without
      // saying so -- an objectKey is a content hash, so a deletion is
      // simply a key never mentioned again -- and this is what stops that
      // residue accumulating for the life of a session.
      auto & resident = PRIVATE(this)->objinfo;
      // Restate whenever the map is not a claim about the table this
      // backend actually holds, or when the residue has outgrown the
      // scene. The size test is judged against the previous publish, so
      // the decision is made before the work rather than after it:
      // restating costs a copy of the map, not a second translate.
      const bool restate =
          !PRIVATE(this)->objinfoInSync()
          || resident.size() > 4 * PRIVATE(this)->objinfodraws + 4096;
      if (restate)
        resident.clear();
      Render::ObjectInfoMap added;
      Gui::RenderTiming::Scope xlate(Gui::RenderTiming::Translate);
      auto draws = RendererBridge::translate(caches,
                                             PRIVATE(this)->sectionOnTop(),
                                             0, false, false,
                                             &resident,
                                             restate ? nullptr : &added);
      PRIVATE(this)->objinfodraws = draws.size();
      xlate.stop();
      Gui::RenderTiming::Scope backend(Gui::RenderTiming::Backend);
      if (restate)
        PRIVATE(this)->external->setObjectInfo(Render::ObjectInfoMap(resident));
      else
        PRIVATE(this)->external->updateObjectInfo(std::move(added));
      PRIVATE(this)->objinfoSynced();

      PRIVATE(this)->external->setScene(std::move(draws));
    }
  }

  PRIVATE(this)->applyKeys(PRIVATE(this)->highlightkeys);
  PRIVATE(this)->selectionkeys.clear();
  PRIVATE(this)->updateselection = true;
}

void
SoFCRenderer::setHighlight(VertexCacheMap && caches, bool wholeontop)
{
  clearHighlight();
  PRIVATE(this)->highlightcaches = std::move(caches);
  PRIVATE(this)->hlwholeontop = wholeontop;

  for (auto & v : PRIVATE(this)->highlightcaches) {
    auto & material = v.first;
    auto & ventries = v.second;
    if (ventries.empty()) continue;
    if (material.drawstyle == SoDrawStyleElement::INVISIBLE) continue;

    bool fulltransp = material.transptexture;
    if (!fulltransp && !material.pervertexcolor)
      fulltransp = (material.diffuse & 0xff) == 0xff ? false : true;

    for (auto & ventry : ventries) {
      std::size_t idx = SoFCRendererP::pushDrawEntry(PRIVATE(this)->hlentries, material, ventry);
      if (!idx)
        continue;
      --idx;

      if (material.isOnTop()
          && (material.partialhighlight 
              || (ventry.partidx < 0
                  && ventry.cache == ventry.cache->getWholeCache())))
      {
        // hide original object because we are doing full object highlight on top
        PRIVATE(this)->highlightkeys.insert(ventry.key);
        if (isValidBBox(PRIVATE(this)->hlentries.back().bbox))
          PRIVATE(this)->highlightbbox.extendBy(PRIVATE(this)->hlentries.back().bbox);
      }

      if (ventry.partidx >= 0) {
        PRIVATE(this)->preseloutline.emplace_back(idx);
      }

      if (material.overrideflags.test(Material::FLAG_TRANSPARENCY)) {
        if ((material.diffuse & 0xff) != 0xff)
          PRIVATE(this)->transphighlight.emplace_back(idx);
        else if (material.type == Material::Triangle)
          PRIVATE(this)->opaquehighlight.emplace_back(idx);
        else if (ventry.partidx < 0 && ventry.cache == ventry.cache->getWholeCache())
          PRIVATE(this)->opaquelineshighlight.emplace_back(idx);
        else
          PRIVATE(this)->highlightlinesontop.emplace_back(idx);
      }
      else {
        if (!fulltransp && (!material.pervertexcolor
                            || ventry.cache->hasOpaqueParts()))
        {
          if (material.type == Material::Triangle)
            PRIVATE(this)->opaquehighlight.emplace_back(idx);
          else if (ventry.partidx < 0 && ventry.cache == ventry.cache->getWholeCache())
            PRIVATE(this)->opaquelineshighlight.emplace_back(idx);
          else
            PRIVATE(this)->highlightlinesontop.emplace_back(idx);
        }

        if (fulltransp || (material.pervertexcolor
                            && ventry.cache->hasTransparency()))
          PRIVATE(this)->transphighlight.emplace_back(idx);
      }
    }
  }
  PRIVATE(this)->applyKeys(PRIVATE(this)->highlightkeys);

  if (PRIVATE(this)->external)
    PRIVATE(this)->external->setHighlight(
          RendererBridge::translate(PRIVATE(this)->highlightcaches,
                                    PRIVATE(this)->sectionOnTop(), 0, true),
          wholeontop);
}

void
SoFCRenderer::addSelection(int id, const VertexCacheMap & caches)
{
  if (id > 0)
    PRIVATE(this)->selectionsontop[id] = &caches;
  else
    PRIVATE(this)->selections[id] = &caches;
  PRIVATE(this)->updateselection = true;

  if (PRIVATE(this)->external)
    PRIVATE(this)->external->addSelection(
          id, RendererBridge::translate(caches, PRIVATE(this)->sectionOnTop(),
                                        id));
}

void
SoFCRenderer::removeSelection(int id)
{
  if (id > 0) {
    if (PRIVATE(this)->selectionsontop.erase(id))
      PRIVATE(this)->updateselection = true;
  }
  else if (PRIVATE(this)->selections.erase(id))
    PRIVATE(this)->updateselection = true;

  if (PRIVATE(this)->external)
    PRIVATE(this)->external->removeSelection(id);
}

void
SoFCRendererP::updateSelection()
{
  if (!this->updateselection)
    return;

  this->updateselection = false;
  this->opaqueselections.clear();
  this->transpselections.clear();
  this->seloutline.clear();
  this->transpselectionsontop.clear();
  this->transpselectionsfaceontop.clear();
  this->selstriangleontop.clear();
  this->selslineontop.clear();
  this->selspointontop.clear();
  this->selsontop.clear();
  this->slentries.clear();
  this->selectionbbox = SbBox3f();
 
  CacheKeySet renderkeys;

  applyKeys(this->selectionkeys, -1);
  this->selectionkeys.clear();

  // checkKey() serves two purposes. In case of whole object selection, 1) make
  // sure normal object rendering is skipped, 2) make sure no duplicate
  // rendering of the same object selection.
  auto checkKey = [&](const Material & material, const VertexCacheEntry & ventry) -> std::size_t {
    std::size_t idx = pushDrawEntry(this->slentries, material, ventry);
    if (!idx)
      return 0;
    if (!ventry.key || ventry.partidx >= 0 || ventry.cache != ventry.cache->getWholeCache())
      return idx;
    if (!this->selkey)
      this->selkey = std::allocate_shared<CacheKey>(SoFCAllocator<CacheKey>());
    this->selkey->forcePush(ventry.cache->getNodeId());
    this->selkey->forcePush(material.type);
    this->selkey->append(ventry.key);
    if (this->selectionkeys.insert(ventry.key).second) {
      renderkeys.insert(this->selkey);
      this->selkey.reset();
    }
    else if (renderkeys.insert(this->selkey).second)
      this->selkey.reset();
    else {
      this->selkey->clear();
      this->slentries.pop_back();
      return 0;
    }
    if (isValidBBox(this->slentries.back().bbox))
      this->selectionbbox.extendBy(this->slentries.back().bbox);
    return idx;
  };

  for (auto & sel : this->selectionsontop) {
    for (auto & v : *sel.second) {
      auto & material = v.first;
      auto & ventries = v.second;
      if (ventries.empty()) continue;
      if (material.drawstyle == SoDrawStyleElement::INVISIBLE) continue;

      for (auto & ventry : ventries) {
        std::size_t idx = checkKey(material, ventry);
        if (!idx)
          continue;
        --idx;
        switch (material.type) {
          case Material::Triangle:
            if (ventry.partidx >= 0) {
              this->seloutline.emplace_back(idx);
              this->transpselectionsfaceontop.emplace_back(idx);
            } else
              this->transpselectionsontop.emplace_back(idx);
            if (!(sel.first & SoFCRenderer::SelIdSelected) || material.partialhighlight)
              this->selstriangleontop.emplace_back(idx);
            break;
          case Material::Line:
            if (sel.first & SoFCRenderer::SelIdPartial)
              this->selslineontop.emplace_back(idx);
            else if (!(sel.first & SoFCRenderer::SelIdFull) || material.partialhighlight)
              this->selsontop.emplace_back(idx);
            else
              this->selslineontop.emplace_back(idx);
            break;
          case Material::Point:
            if (sel.first & SoFCRenderer::SelIdPartial)
              this->selspointontop.emplace_back(idx);
            else if (!(sel.first & SoFCRenderer::SelIdFull) || material.partialhighlight)
              this->selsontop.emplace_back(idx);
            else
              this->selslineontop.emplace_back(idx);
            break;
        }
      }
    }
  }

  for (auto & sel : this->selections) {
    for (auto & v : *sel.second) {
      auto & material = v.first;
      auto & ventries = v.second;
      if (ventries.empty()) continue;
      if (material.drawstyle == SoDrawStyleElement::INVISIBLE) continue;

      bool fulltransp = material.transptexture;
      if (!fulltransp && !material.pervertexcolor)
        fulltransp = (material.diffuse & 0xff) == 0xff ? false : true;

      for (auto & ventry : ventries) {
        std::size_t idx = checkKey(material, ventry);
        if (!idx)
            continue;
        --idx;
        if (!fulltransp && (!material.pervertexcolor
                            || ventry.cache->hasOpaqueParts()))
          this->opaqueselections.emplace_back(idx);

        if (fulltransp || (material.pervertexcolor
                            && ventry.cache->hasTransparency()))
          this->transpselections.emplace_back(idx);

        if (material.type == Material::Triangle && ventry.partidx >= 0)
          this->seloutline.emplace_back(idx);
      }
    }
  }

  applyKeys(this->selectionkeys);
}

void
SoFCRenderer::getBoundingBox(SbBox3f & bbox) const
{
  PRIVATE(this)->updateSelection();
  if (isValidBBox(PRIVATE(this)->scenebbox))
    bbox.extendBy(PRIVATE(this)->scenebbox);
  if (isValidBBox(PRIVATE(this)->highlightbbox))
    bbox.extendBy(PRIVATE(this)->highlightbbox);
  if (isValidBBox(PRIVATE(this)->selectionbbox))
    bbox.extendBy(PRIVATE(this)->selectionbbox);
}

// Screen-align a billboard autozoom, which the node itself cannot do:
// SoAutoZoomTranslation::doAction is also what the CAPTURE traversal
// runs, and a camera-dependent rotation and scale baked into a static
// vertex cache is the very failure the capture companion exists to avoid
// (the SoFCImageQuad note in SoFCRenderCacheManager.cpp). So the
// per-frame math belongs to each render path: the external backend does
// it in BGFXRendererP.h's setDrawTransform, this does it for the GL pass
// here. Substitute the accumulated matrix's 3x3 with the camera basis
// scaled to world units per screen pixel, keeping the accumulated
// translation as the anchor -- geometry emitted in pixels (SoImage
// capture companions, text glyph quads) then draws at its screen size
// instead of being read as world units.
void
SoFCRendererP::applyBillboard(SoState * state, const SoAutoZoomTranslation * zoom)
{
  const SbViewportRegion & vp = SoViewportRegionElement::get(state);
  float vpheight = static_cast<float>(vp.getViewportSizePixels()[1]);
  if (vpheight < 1.0f)
    return;

  SbMatrix matrix = SoModelMatrixElement::get(state); // clazy:exclude=rule-of-two-soft
  SbVec3f anchor(matrix[3][0], matrix[3][1], matrix[3][2]);

  // The camera's world-space axes are the COLUMNS of the viewing matrix's
  // 3x3 (Coin's row-vector layout: p_view = p_world * VM), and the model
  // matrix's rows are the local axes in world space.
  const SbMatrix & vm = SoViewingMatrixElement::get(state);

  // World units per screen pixel at the anchor: a world length L at view
  // depth d covers L*P[1][1]/d of the projection's height, which is 2
  // wide, so one pixel is 2d/(P[1][1]*H) world units -- and the depth
  // term drops out of an orthographic projection. This is the backend's
  // expression, element for element (BGFXRendererP.h, setDrawTransform),
  // so the two paths size a billboard by one piece of arithmetic.
  // SbViewVolume::getWorldToScreenScale answers a nearby question -- the
  // world radius of a SPHERE covering a given screen radius -- and its
  // perspective form is a tangent construction that is only linear in
  // the small and drifts off-axis: measured 8% small on a label in the
  // corner of a perspective view.
  const SbMatrix & pm = SoProjectionMatrixElement::get(state);
  float p5 = std::abs(pm[1][1]) > 1e-8f ? pm[1][1] : 1.0f;
  float scale = 2.0f / (p5 * vpheight);
  if (std::abs(pm[3][3]) < 1e-6f) {  // perspective: w carries -z
    float zview = anchor[0]*vm[0][2] + anchor[1]*vm[1][2]
                + anchor[2]*vm[2][2] + vm[3][2];
    float depth = -zview;            // in front of the camera => positive
    scale *= depth > 1e-4f ? depth : 1e-4f;
  }
  float pixelscale = zoom->pixelScale.getValue();
  scale *= pixelscale > 0.0f ? pixelscale
                             : SoAutoZoomTranslation::DefaultPixelScale;

  for (int i = 0; i < 3; ++i) {
    matrix[0][i] = vm[i][0] * scale;   // local X -> screen right
    matrix[1][i] = vm[i][1] * scale;   // local Y -> screen up
    matrix[2][i] = vm[i][2] * scale;   // local Z -> toward the viewer
  }
  matrix[0][3] = matrix[1][3] = matrix[2][3] = 0.0f;
  matrix[3][3] = 1.0f;
  SoModelMatrixElement::set(state, NULL, matrix);
}

void inline
SoFCRendererP::setupMatrix(SoGLRenderAction * action, const DrawEntry &draw_entry)
{
  SoState *state = action->getState();
  const VertexCacheEntry *ventry = draw_entry.ventry;

  SoModelMatrixElement::makeIdentity(state, NULL);
  if (!this->identity)
    SoModelMatrixElement::mult(state, NULL, this->matrix);

  if (draw_entry.material->autozoom.getNum()) {
    for (auto &info : draw_entry.material->autozoom.getData()) {
      if (info.resetmatrix) {
        if (info.identity)
          SoModelMatrixElement::makeIdentity(state, NULL);
        else
          SoModelMatrixElement::set(state, NULL, info.matrix);
      } else if (!info.identity)
        SoModelMatrixElement::mult(state, NULL, info.matrix);
      auto zoom = info.cast<SoAutoZoomTranslation>();
      if (zoom->billboard.getValue())
        applyBillboard(state, zoom);
      else
        info.node->GLRender(action);
    }
  }

  if (!ventry->identity)
    SoModelMatrixElement::mult(state, NULL, ventry->matrix);
}

void
SoFCRendererP::pauseShadowRender(SoState *state, bool paused)
{
  if (!this->shadowrendering || this->shadowrenderpaused == paused)
    return;
  this->shadowrenderpaused = paused;
  SoGLShaderProgramElement::enable(state, paused ? FALSE: TRUE);
}

void
SoFCRendererP::renderLines(SoState *state, int array, DrawEntry &draw_entry)
{
  if (this->depthwriteonly || this->shadowmapping)
    return;
  bool noseam = draw_entry.ventry->partidx < 0
                  && draw_entry.material->outline
                  && this->hiddenLineConfig.hideSeam;
  pauseShadowRender(state, true);
  draw_entry.ventry->cache->renderLines(state, array, draw_entry.ventry->partidx, noseam);
  ++this->drawcallcount;
}

void
SoFCRendererP::renderPoints(SoGLRenderAction *action, int array, DrawEntry &draw_entry)
{
  if (this->depthwriteonly || this->shadowmapping)
    return;
  if (draw_entry.ventry->partidx >= 0
      || !draw_entry.material->outline
      || !this->hiddenLineConfig.hideVertex)
  {
    pauseShadowRender(action->getState(), true);
    draw_entry.ventry->cache->renderPoints(action, array, draw_entry.ventry->partidx);
    ++this->drawcallcount;
  }
}

void
SoFCRendererP::renderTriangles(const DrawEntry &draw_entry,
                                  SoGLRenderAction *action,
                                  const int arrays,
                                  int part,
                                  const SbPlane *plane)
{
  auto cache = draw_entry.ventry->cache;
  if (cache->shouldGLRender()) {
    // TODO: can we assume none of the legacy shape nodes is suitable for
    // shadow rendering?
    if (this->shadowmapping)
      return;
    pauseShadowRender(action->getState(), true);
  }
  cache->renderTriangles(action, arrays, part, plane);
}

void
SoFCRendererP::renderOutline(SoGLRenderAction *action,
                             DrawEntry &draw_entry,
                             bool highlight)
{
  int drawidx = draw_entry.ventry->partidx;
  if (this->shadowmapping
      || this->depthwriteonly
      || draw_entry.material->type != Material::Triangle
      || (!highlight
          && !this->hiddenLineConfig.perFaceOutline
          && this->hiddenLineConfig.sceneOutline)
      || (!draw_entry.material->outline
          && (!highlight || drawidx < 0)))
    return;

  SoState *state = action->getState();

  int numparts = draw_entry.ventry->cache->getNumNonFlatParts();
  int dummyparts[1];
  const int *partindices = nullptr;
  if ((this->material.clippers.getNum() && drawidx < 0)
      || (!highlight && this->hiddenLineConfig.perFaceOutline
                     && !this->hiddenLineConfig.sceneOutline
                     && this->hiddenLineConfig.outlineWidth > 0.0f))
  {
    numparts = draw_entry.ventry->cache->getNumFaceParts();
  } else if (this->hiddenLineConfig.perFaceOutline && numparts && drawidx < 0) {
    partindices = draw_entry.ventry->cache->getNonFlatParts();
  } else {
    numparts = 1;
    dummyparts[0] = drawidx;
    partindices = dummyparts;
  }

  bool pushed = false;
  float current_linewidth = 0.f;
  uint32_t current_color = 0;
  for (int i=0; i<numparts; ++i) {
    int partidx;
    if (partindices) {
      if (drawidx >= 0 && drawidx != partindices[i])
        continue;
      partidx = partindices[i];
    } else
      partidx = i;

    if (!pushed) {
      pushed = true;
      glPushAttrib(GL_ENABLE_BIT
          | GL_DEPTH_BUFFER_BIT
          | GL_STENCIL_BUFFER_BIT
          | GL_CURRENT_BIT
          | GL_POLYGON_BIT);

      pauseShadowRender(state, true);

      glEnable(GL_STENCIL_TEST);
      glDisable(GL_LIGHTING);
      glDisable(GL_TEXTURE_2D);
      glDisable(GL_CULL_FACE);
      glDisable(GL_LINE_STIPPLE);
      if (highlight) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
      }
    }
    auto col = drawidx >= 0 ? this->material.emissive
                            : (draw_entry.material->linecolor 
                                ? draw_entry.material->linecolor
                                : draw_entry.material->diffuse);
    if (col != current_color) {
      current_color = col;
      glColor3ub((unsigned char)((col>>24)&0xff),
          (unsigned char)((col>>16)&0xff),
          (unsigned char)((col>>8)&0xff));
    }

    float linewidth = draw_entry.material->linewidth;
    if (highlight) {
      float w = linewidth * std::max(1.0, ViewParams::getSelectionLineThicken());
      if (ViewParams::getSelectionLineMaxWidth() > 1.0)
        w = std::min<float>(w, std::max<float>(linewidth, ViewParams::getSelectionLineMaxWidth()));
      linewidth = w;
    }
    if (linewidth < this->hiddenLineConfig.outlineWidth)
      linewidth = this->hiddenLineConfig.outlineWidth;

    linewidth = std::max(linewidth*1.5f,
                         static_cast<float>(draw_entry.material->linewidth*ViewParams::getOutlineThicken()));
    if (linewidth != current_linewidth) {
      current_linewidth = linewidth;
      glLineWidth(linewidth);
      glPointSize(linewidth);
    }

    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glStencilFunc (GL_ALWAYS, 1, -1);
    glStencilOp (GL_KEEP, GL_REPLACE, GL_REPLACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY, partidx);
    ++this->drawcallcount;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 1, -1);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY, partidx);
    if (highlight || this->hiddenLineConfig.hideVertex) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
      renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY, partidx);
    }
    ++this->drawcallcount;
  }
  if (pushed) {
    glPopAttrib();
    // For some reason, GL_CURRENT_BIT doesn't seem to restore the color?
    auto col = this->material.diffuse;
    unsigned char r = (col >> 24) & 0xff;
    unsigned char g = (col >> 16) & 0xff;
    unsigned char b = (col >> 8) & 0xff;
    unsigned char a = col & 0xff;
    glColor4ub(r, g, b, a);
    if (highlight) {
      glLineWidth(this->material.linewidth);
      glPointSize(this->material.pointsize);
    }
  }
}

void
SoFCRendererP::renderSceneOutline(SoGLRenderAction *action)
{
  SoState * state = action->getState();

  if (this->transpshadowmapping
      || this->shadowmapping
      || !this->showHiddenLine
      || !this->hiddenLineConfig.sceneOutline)
    return;

  glPushAttrib(GL_ENABLE_BIT
      | GL_DEPTH_BUFFER_BIT
      | GL_STENCIL_BUFFER_BIT
      | GL_CURRENT_BIT
      | GL_POLYGON_BIT);

  pauseShadowRender(state, true);

  glEnable(GL_STENCIL_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LINE_STIPPLE);
  // glDisable(GL_DEPTH_TEST);
  unsigned col;
  if (auto pColor = SoFCDisplayModeElement::getLineColor(state))
    col = pColor->getPackedValue(0.0);
  else
    col = ViewParams::getHiddenLineColor();
  glColor3ub((unsigned char)((col>>24)&0xff),
             (unsigned char)((col>>16)&0xff),
             (unsigned char)((col>>8)&0xff));

  float linewidth = std::max(1.0f, hiddenLineConfig.outlineWidth);
  glLineWidth(linewidth*1.5f);

  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

  glStencilFunc (GL_ALWAYS, 1, -1);
  glStencilOp (GL_KEEP, GL_REPLACE, GL_REPLACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  for (const auto &draw_entry : this->drawentries) {
    if (draw_entry.skip > 0
        || draw_entry.material->type != Material::Triangle)
      continue;

    if (this->material.clippers != draw_entry.material->clippers) {
      state->pop();
      state->push();

      for(auto & info : draw_entry.material->clippers.getData()) {
        if (!info.identity)
          SoModelMatrixElement::set(state, NULL, info.matrix);
        state->setCacheOpen(false);
        info.node->GLRender(action);
        if (!info.identity)
          SoModelMatrixElement::makeIdentity(state, NULL);
      }
      this->material.clippers = draw_entry.material->clippers;
    }
    setupMatrix(action, draw_entry);

    renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY);
  }

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_NOTEQUAL, 1, -1);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  for (const auto &draw_entry : this->drawentries) {
    if (draw_entry.skip > 0
        || draw_entry.material->drawstyle == SoDrawStyleElement::INVISIBLE
        || draw_entry.material->type != Material::Triangle)
      continue;

    setupMatrix(action, draw_entry);

    renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY);
    ++this->drawcallcount;
  }

  if (hiddenLineConfig.hideVertex) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glPointSize(linewidth);
    for (const auto &draw_entry : this->drawentries) {
      if (draw_entry.skip > 0
          || draw_entry.material->type != Material::Triangle)
        continue;

      setupMatrix(action, draw_entry);

      renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED_ARRAY);
      ++this->drawcallcount;
    }
  }

  glPopAttrib();
}

bool
SoFCRendererP::renderSection(SoGLRenderAction *action,
                             DrawEntry &draw_entry,
                             int &pass,
                             bool &pushed,
                             bool transp)
{
  int curpass = pass++;

  int numclip = this->material.clippers.getNum();
  bool concave = this->section.concave && numclip > 1;

  if (this->depthwriteonly
      || curpass >= numclip
      || draw_entry.ventry->partidx >= 0
      || (!this->section.fill && !concave))
    return curpass == 0;

  if (draw_entry.material->type != Material::Triangle) {
    if (!concave)
      return curpass == 0;
    if (!pushed) {
      pushed = true;
      glPushAttrib(GL_ENABLE_BIT);
    }
    if (curpass == 0) {
      for (int i=1; i<numclip; ++i)
        glDisable(GL_CLIP_PLANE0 + i);
    } else
      glDisable(GL_CLIP_PLANE0 + curpass - 1);
    glEnable(GL_CLIP_PLANE0 + curpass);
    return true;
  }

  if (draw_entry.material->shapetype != SoShapeHintsElement::SOLID
      && !draw_entry.ventry->cache->hasSolid())
    return curpass == 0;

  if (!concave && this->section.fillGroup) {
    if (curpass != 0)
      return false;
    if (transp)
      this->transp_section_entries.push_back(&draw_entry);
    else
      this->section_entries.push_back(&draw_entry);
    return true;
  }

  auto pdraw_entry = &draw_entry;
  _renderSection(action, &pdraw_entry, 1, curpass, pushed, false);
  pass = curpass;
  return true;
}

void
SoFCRendererP::renderSectionGrouped(SoGLRenderAction *action, bool transp)
{
  bool pushed = false;

  size_t head = 0;
  auto & entries = transp ? this->transp_section_entries : this->section_entries;
  size_t size = entries.size();
  if (size == 0)
    return;
  const Material * head_mat;
  for (size_t i = 0; i < size; ++i) {
    const auto &draw_entry = *entries[i];
    const Material *this_mat = draw_entry.material;
    bool last = i+1 == size;
    if (i == head) {
      head_mat = this_mat;
      if (!last)
        continue;
    } else if (!last && head_mat->diffuse == this_mat->diffuse
                     && head_mat->clippers == this_mat->clippers
                     && head_mat->autozoom == this_mat->autozoom)
      continue;

    applyMaterial(action, *head_mat, true, RenderPassSectionFill);
    int numclip = this->material.clippers.getNum();
    size_t count = last ? size - head : i - head;
    for (int pass = 0; pass < numclip;)
      _renderSection(action, &entries[head], count, pass, pushed, true);
  }

  if (pushed)
    glPopAttrib();
}

void
SoFCRendererP::_renderSection(SoGLRenderAction *action,
                              DrawEntry **pp_draw_entry,
                              size_t count,
                              int &pass,
                              bool &pushed,
                              bool setupmatrix)
{
  int curpass = pass++;
  if (!pushed) {
    pushed = true;
    glPushAttrib(GL_ENABLE_BIT
        | GL_DEPTH_BUFFER_BIT
        | GL_STENCIL_BUFFER_BIT);
  }

  int numclip = this->material.clippers.getNum();
  bool concave = this->section.concave && numclip > 1;

  if (curpass == 0 && concave) {
    if (this->material.depthfunc != SoDepthBuffer::LESS)
      glDepthFunc(GL_LESS);
    if (this->material.polygonoffsetstyle & SoPolygonOffsetElement::FILLED)
      glDisable(GL_POLYGON_OFFSET_FILL);
  }

  glEnable(GL_STENCIL_TEST);
  glClear(GL_STENCIL_BUFFER_BIT);

  for (int i=0; i<numclip; ++i) {
    if (i == curpass)
      glEnable(GL_CLIP_PLANE0 + i);
    else
      glDisable(GL_CLIP_PLANE0 + i);
    FC_GLERROR_CHECK;
  }

  glPushAttrib(GL_ENABLE_BIT);
  FC_GLERROR_CHECK;
  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  FC_GLERROR_CHECK;

  glStencilFunc (GL_ALWAYS, 1, 0x01);
  FC_GLERROR_CHECK;
  // Assuming OpenGL 2.0 support with two side stencil operation. So disable
  // face culling, and use GL_INVERT for stencil op.
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glStencilOp (GL_KEEP, GL_KEEP, GL_INVERT);
  FC_GLERROR_CHECK;

  SbBox3f bbox;
  for (size_t i=0; i<count; ++i) {
    const auto &draw_entry = *pp_draw_entry[i];
    if (setupmatrix)
      setupMatrix(action, draw_entry);
    draw_entry.ventry->cache->renderSolids(action->getState());
    // DrawEntry::bbox already has the entry's matrix applied (see the
    // DrawEntry constructor); transforming it again here used to inflate
    // the grouped section bounds of transformed entries.
    if (isValidBBox(draw_entry.bbox))
      bbox.extendBy(draw_entry.bbox);
    ++this->drawcallcount;
  }

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  FC_GLERROR_CHECK;

  glPopAttrib();

  if (!concave) {
    for (int i=0; i<numclip; ++i) {
      if (i != curpass)
        glEnable(GL_CLIP_PLANE0 + i);
      FC_GLERROR_CHECK;
    }
  }
  glDisable(GL_CLIP_PLANE0 + curpass);

  glStencilFunc (GL_EQUAL, 1, 0x01);
  glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
  FC_GLERROR_CHECK;

  glPushAttrib(GL_ENABLE_BIT
      | GL_DEPTH_BUFFER_BIT
      | (this->hatchtexture ? 
          (GL_COLOR_BUFFER_BIT|GL_CURRENT_BIT|GL_TEXTURE_BIT): 0));
  FC_GLERROR_CHECK;

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  FC_GLERROR_CHECK;

  const auto &info = this->material.clippers.get(curpass);
  const SoClipPlane *clipper = info.cast<SoClipPlane>();

  SbPlane plane = clipper->plane.getValue();
  if (!info.identity)
    plane.transform(info.matrix);

  SbVec3f dir = plane.getNormal();
  SbRotation rotation(SbVec3f(0,0,1), dir);
  SbVec3f u,v;
  rotation.multVec(SbVec3f(1,0,0), u);

  SbSphere sphere;
  sphere.circumscribe(bbox);
  float radius = sphere.getRadius();
  u *= radius;
  rotation.multVec(SbVec3f(0,1,0), v);
  v *= radius;
  SbVec3f center = bbox.getCenter();
  dir *= -1;
  center += dir * plane.getDistance(center);
  SbVec3f v1,v2,v3,v4;
  v1 = v2 = center + v;
  v1 -= u;
  v2 += u;
  v3 = v4 = center - v;
  v3 += u;
  v4 -= u;
  if (setupmatrix)
    SoModelMatrixElement::makeIdentity(action->getState(), NULL);
  else {
    auto matrix = SoModelMatrixElement::get(action->getState()).inverse();
    matrix.multVecMatrix(v1, v1);
    matrix.multVecMatrix(v2, v2);
    matrix.multVecMatrix(v3, v3);
    matrix.multVecMatrix(v4, v4);
  }

  if (this->section.fillInvert) {
    auto col = this->material.diffuse;
    unsigned char r = (col >> 24) & 0xff;
    unsigned char g = (col >> 16) & 0xff;
    unsigned char b = (col >> 8) & 0xff;
    unsigned char a = col & 0xff;
    if (r > 120 && r < 140) r = 180; else r = 255 - r;
    if (g > 120 && g < 140) g = 180; else g = 255 - g;
    if (b > 120 && b < 140) b = 180; else b = 255 - b;
    if (r+g+b < 10)
      r = g = b = 50;
    glColor4ub(r, g, b, a);
  }

  float hatchscale = std::max(1e-4, 0.3 * this->section.hatchScale);

  auto hatch = this->hatchtexture;
  if (!this->section.hatch)
    hatch = nullptr;
  if (hatch) {
    pauseShadowRender(action->getState(), true);
#ifdef FC_OS_WIN32
    static FCGLActiveTextureProc glActiveTexture;
    if (!glActiveTexture) {
      const cc_glglue * glue = cc_glglue_instance(action->getCacheContext());
      glActiveTexture = (FCGLActiveTextureProc)cc_glglue_getprocaddress(glue, "glActiveTexture");
    }
    if(glActiveTexture)
#endif
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    if (hatch->texture == 0) {
      glGenTextures(1, &hatch->texture);
      glBindTexture(GL_TEXTURE_2D, hatch->texture);
      glTexImage2D(GL_TEXTURE_2D, 0, hatch->nc,
          hatch->width, hatch->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &hatch->data[0]);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    } else
      glBindTexture(GL_TEXTURE_2D, hatch->texture);

    SbViewVolume vv = SoViewVolumeElement::get(action->getState());
    // Using sight point behaves badly in perspective view
    SbVec3f center = vv.getSightPoint(vv.getNearDist() + vv.getDepth() * 0.5f);
    const SbViewportRegion & vp = SoViewportRegionElement::get(action->getState());
    SbVec2s vp_size = vp.getViewportSizePixels();
    float scale = hatchscale * vv.getWorldToScreenScale(center, 1.f);
    // This gives the pixel size of the current world unit size
    float pixelsize = vp_size[0] / scale;
    // This gives the pixel width of the current drawing section plane
    float width = radius * pixelsize;
    // And now we have the texture scale
    hatchscale = std::max(1e-3f, width / hatch->width);
  }

  glDisable(GL_LIGHTING);
  glBegin(GL_QUADS);
  glNormal3fv(dir.getValue());
  if(hatch)
    glTexCoord2f(0.f, hatchscale);
  glVertex3fv(v1.getValue());
  if(hatch)
    glTexCoord2f(0.f, 0.f);
  glVertex3fv(v2.getValue());
  if(hatch)
    glTexCoord2f(hatchscale, 0.f);
  glVertex3fv(v3.getValue());
  if(hatch)
    glTexCoord2f(hatchscale, hatchscale);
  glVertex3fv(v4.getValue());
  glEnd();
  FC_GLERROR_CHECK;

  glPopAttrib();

  if (this->section.fillInvert) {
    auto col = this->material.diffuse;
    unsigned char r = (col >> 24) & 0xff;
    unsigned char g = (col >> 16) & 0xff;
    unsigned char b = (col >> 8) & 0xff;
    unsigned char a = col & 0xff;
    glColor4ub(r, g, b, a);
  }

  glDisable(GL_STENCIL_TEST);
  FC_GLERROR_CHECK;

  if (!concave) {
    if (pass < numclip)
      _renderSection(action, pp_draw_entry, count, pass, pushed, setupmatrix);
    if (curpass == 0) {
      for (int i=0; i<numclip; ++i) {
        glEnable(GL_CLIP_PLANE0 + i);
        FC_GLERROR_CHECK;
      }
    }
  } else {
    for (int i=0; i<numclip; ++i) {
      if (i == curpass)
        glEnable(GL_CLIP_PLANE0 + i);
      else
        glDisable(GL_CLIP_PLANE0 + i);
      FC_GLERROR_CHECK;
    }
  }
}

void
SoFCRendererP::renderOpaque(SoGLRenderAction * action,
                            SbFCVector<DrawEntry> & draw_entries,
                            SbFCVector<std::size_t> & indices,
                            int pass)
{
  if (this->transpshadowmapping)
    return;

  bool pauseshadow = (&draw_entries == &this->slentries || &draw_entries == &this->hlentries);

  SoState * state = action->getState();
  for (std::size_t idx : indices) {
    auto & draw_entry = draw_entries[idx];
    if (draw_entry.skip > 0
        && !this->shadowmapping
        && ((!this->section.concave && !this->section.noOnTop)
            || !draw_entry.material->clippers.getNum()))
      continue;

    if (this->recheckmaterial 
        || this->prevpass != pass
        || this->prevmaterial != draw_entry.material) {
      if (!applyMaterial(action, *draw_entry.material, false, pass))
        continue;
      this->prevpass = pass;
      this->recheckmaterial = false;
      this->prevmaterial = draw_entry.material;
    }

    setupMatrix(action, draw_entry);

    if (pass == RenderPassSelectionOutline) {
      renderOutline(action, draw_entry, true);
      continue;
    }

    int array = SoFCVertexCache::ALL;
    if (!this->material.pervertexcolor)
      array ^= SoFCVertexCache::COLOR;
    if (this->notexture)
      array ^= SoFCVertexCache::TEXCOORD;
    if (this->material.lightmodel == SoLazyElement::BASE_COLOR)
      array ^= SoFCVertexCache::NORMAL;
    else if (draw_entry.ventry->cache->getNumTriangleIndices() && !draw_entry.ventry->cache->getNormalArray()) {
      array ^= SoFCVertexCache::NORMAL;
      this->material.lightmodel = SoLazyElement::BASE_COLOR;
      glDisable(GL_LIGHTING);
      FC_GLERROR_CHECK;
    }

    int n = 0;
    bool pushed = false;
    while (renderSection(action, draw_entry, n, pushed, false)) {
      if (!this->section.concave
          && this->material.clippers.getNum() > 0
          && isValidBBox(draw_entry.bbox)
          && SoCullElement::cullTest(state, draw_entry.bbox, FALSE))
      {
          continue;
      }
      switch (draw_entry.material->type) {
      case Material::Triangle:
        if (&draw_entries != &this->slentries
            && &draw_entries != &this->hlentries
            && draw_entry.material->outline
            && this->hiddenLineConfig.hideFace)
          continue;

        pauseShadowRender(state, pauseshadow
            || !(draw_entry.material->shadowstyle & SoShadowStyleElement::SHADOWED));

        if (!draw_entry.ventry->cache->hasTransparency()) {
          renderTriangles(draw_entry, action, array, draw_entry.ventry->partidx);
          ++this->drawcallcount;
        }
        else if (!this->material.pervertexcolor) {
          // this means override transparency (i.e. force opaque)
          renderTriangles(draw_entry, action, SoFCVertexCache::NON_SORTED, draw_entry.ventry->partidx);
          ++this->drawcallcount;
        }
        else {
          if (!this->material.twoside) {
            SoLazyElement::setTwosideLighting(state, TRUE);
            glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
          }
          renderTriangles(draw_entry, action, array, draw_entry.ventry->partidx);
          ++this->drawcallcount;
          if (!this->material.twoside) {
            SoLazyElement::setTwosideLighting(state, FALSE);
            glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
          }
          FC_GLERROR_CHECK;
        }
        break;
      case Material::Line:
        renderLines(state, array, draw_entry);
        break;
      case Material::Point:
        renderPoints(action, array, draw_entry);
        break;
      }
      if (draw_entry.ventry->partidx < 0) {
          renderOutline(action, draw_entry, &draw_entries == &this->hlentries);
      }
    }
    if (pushed)
      glPopAttrib();
  }
}

void
SoFCRendererP::renderTransparency(SoGLRenderAction * action,
                                  SbFCVector<DrawEntry> & draw_entries,
                                  SbFCVector<DrawEntryIndex> & indices,
                                  bool sort)
{
  if (indices.empty())
    return;

  SoState * state = action->getState();

  if (this->shadowmapping) {
    if (!this->transpshadowmapping)
      return;
    sort = false;
  }

  bool pauseshadow = (&draw_entries == &this->slentries || &draw_entries == &this->hlentries);

  bool notriangle = false;
  if (&draw_entries != &this->slentries
      && &draw_entries != &this->hlentries
      && this->showHiddenLine
      && this->hiddenLineConfig.hideFace)
  {
    notriangle = true;
  }

  if (!notriangle && sort) {
    SbPlane plane = SoViewVolumeElement::get(state).getPlane(0.0);
    if (plane.getNormal() != this->prevplane.getNormal()) {
      this->prevplane = plane;
      if (!this->identity)
        plane.transform(this->matrix.inverse());
      for (auto & v : indices)
        v.distance = plane.getDistance(draw_entries[v.idx].bbox.getCenter());

      std::sort(indices.begin(), indices.end(),
        [](const DrawEntryIndex &a, const DrawEntryIndex &b) {
          return a.distance < b.distance;
        });
    }
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  FC_GLERROR_CHECK;

  bool highlight = &draw_entries == &this->hlentries;
  bool sel_highlight = &draw_entries == &this->slentries;

  for (auto & v : indices) {
    auto & draw_entry = draw_entries[v.idx];
    if (draw_entry.skip > 0 && !this->shadowmapping)
      continue;
    if (this->recheckmaterial || this->prevmaterial != draw_entry.material) {
      if (!applyMaterial(action, *draw_entry.material, true))
        continue;
      this->recheckmaterial = false;
      this->prevmaterial = draw_entry.material;
    }
    setupMatrix(action, draw_entry);

    int array = SoFCVertexCache::ALL;
    if (!this->material.pervertexcolor)
      array ^= SoFCVertexCache::COLOR;
    if (this->notexture)
      array ^= SoFCVertexCache::TEXCOORD;

    bool overridelightmodel = false;
    if (this->material.lightmodel == SoLazyElement::BASE_COLOR)
      array ^= SoFCVertexCache::NORMAL;
    else if (!draw_entry.ventry->cache->getNormalArray()) {
      array ^= SoFCVertexCache::NORMAL;
      overridelightmodel = true;
      glDisable(GL_LIGHTING);
      FC_GLERROR_CHECK;
    }

    switch (draw_entry.material->type) {
    case Material::Line:
      renderLines(state, array, draw_entry);
      break;
    case Material::Point:
      renderPoints(action, array, draw_entry);
      break;
    case Material::Triangle:
      {
        bool pushed = false;
        int n = 0;
        while (renderSection(action, draw_entry, n, pushed, true)) {
          if (!this->section.concave
              && this->material.clippers.getNum() > 0
              && isValidBBox(draw_entry.bbox)
              && SoCullElement::cullTest(state, draw_entry.bbox, FALSE))
          {
            continue;
          }
          if (!notriangle) {

            if (draw_entry.ventry->partidx>=0) {
              if (highlight && ViewParams::getNoPreSelFaceHighlightWithOutline()
                            && ViewParams::getShowPreSelectedFaceOutline())
              {
                continue;
              }
              if (sel_highlight && ViewParams::getNoSelFaceHighlightWithOutline()
                                && ViewParams::getShowSelectedFaceOutline())
              {
                continue;
              }
            }

            if (this->shadowmapping)
              array |= SoFCVertexCache::NON_SORTED_ARRAY;
            else if (!draw_entry.ventry->cache->hasTransparency()
                || draw_entry.material->overrideflags.test(Material::FLAG_TRANSPARENCY))
              array |= SoFCVertexCache::FULL_SORTED_ARRAY;
            else
              array |= SoFCVertexCache::SORTED_ARRAY;

            pauseShadowRender(state, pauseshadow
                || !(draw_entry.material->shadowstyle & SoShadowStyleElement::SHADOWED));

            if (draw_entry.ventry->partidx < 0)
              renderOutline(action, draw_entry, highlight);

            bool restoreDepthTest = false;
            if (draw_entry.ventry->partidx>=0) {
              if (((highlight && ViewParams::getShowPreSelectedFaceOutline())
                    || (sel_highlight && ViewParams::getShowSelectedFaceOutline()))
                  && (!this->material.depthtest
                    || this->material.depthfunc != SoDepthBuffer::LEQUAL))
              {
                restoreDepthTest = true;
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
              }
            }
            
            renderTriangles(draw_entry, action, array, draw_entry.ventry->partidx,
                            sort ? &this->prevplane : nullptr);

            if (restoreDepthTest) {
              if (!this->material.depthtest)
                glDisable(GL_DEPTH_TEST);
              if (this->material.depthfunc != SoDepthBuffer::LEQUAL)
                setDepthFunc(this->material.depthfunc);
            }

            ++this->drawcallcount;
          }
        }
        if (pushed) {
          glPopAttrib();
          FC_GLERROR_CHECK;
        }
      }
      break;
    }

    if (overridelightmodel)
      glEnable(GL_LIGHTING);
    FC_GLERROR_CHECK;
  }

  glDisable(GL_BLEND);
  FC_GLERROR_CHECK;
}

void
SoFCRenderer::pushExternalConfigs(SoState * state)
{
  // The hidden-line draw style configuration lives in the traversal state
  // and is resolved per render; mirror it to the external backend (which
  // draws before this traversal, so it applies one frame late like the
  // scene feed).
  if (PRIVATE(this)->external) {
    PRIVATE(this)->external->setHiddenLineConfig(
        RendererBridge::translateHiddenLineConfig(state));
    PRIVATE(this)->external->setSectionConfig(
        RendererBridge::translateSectionConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setAOConfig(
        RendererBridge::translateAOConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setCavityConfig(
        RendererBridge::translateCavityConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setMatcapConfig(
        RendererBridge::translateMatcapConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setRenderDebugConfig(
        RendererBridge::translateRenderDebugConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setOcclusionCullConfig(
        RendererBridge::translateOcclusionCullConfig(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setUserShaderConfig(PRIVATE(this)->mergedshaders);
    PRIVATE(this)->external->setPBRConfig(
        RendererBridge::translatePBRConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setOutputConfig(
        RendererBridge::translateOutputConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setBumpConfig(
        RendererBridge::translateBumpConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setLightConfig(
        RendererBridge::translateLightConfig(state,
                                             PRIVATE(this)->externalview));
    PRIVATE(this)->external->setViewLightConfig(
        RendererBridge::translateViewLightConfig(state));
    PRIVATE(this)->external->setVolumetricConfig(
        RendererBridge::translateVolumetricConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setWaterConfig(
        RendererBridge::translateWaterConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setBloomConfig(
        RendererBridge::translateBloomConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setTemporalConfig(
        RendererBridge::translateTemporalConfig(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setPreselConfig(
        RendererBridge::translatePreselConfig());
    PRIVATE(this)->external->setSelConfig(
        RendererBridge::translateSelConfig());
    PRIVATE(this)->external->setEffectResolution(
        RendererBridge::translateEffectResolution(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setSSAOResolution(
        RendererBridge::translateSSAOResolution(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setLevelTolerance(
        RendererBridge::translateLevelTolerance(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setGpuMemoryBudget(
        RendererBridge::translateGpuMemoryBudget(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setLevelPressureRelease(
        RendererBridge::translateLevelPressureRelease(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setDowngradeLedger(
        RendererBridge::translateDowngradeLedger(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setClimbAdmission(
        RendererBridge::translateClimbHardLimit(
            PRIVATE(this)->externalview),
        RendererBridge::translateClimbAdmitBatch(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setDescentOrderBatch(
        RendererBridge::translateDescentOrderBatch(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setLevelBudgetDeadband(
        RendererBridge::translateLevelBudgetDeadband(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setLevelDebug(
        RendererBridge::translateLevelDebug(PRIVATE(this)->externalview));
    PRIVATE(this)->external->setElementGates(
        RendererBridge::translateShapeVertices(PRIVATE(this)->externalview),
        RendererBridge::translatePressureDropEdges(
            PRIVATE(this)->externalview),
        RendererBridge::translateLoadDropElements(
            PRIVATE(this)->externalview),
        RendererBridge::translateElementGateStagger(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setTinyElementCutoff(
        RendererBridge::translateTinyElementCutoff(
            PRIVATE(this)->externalview));
    PRIVATE(this)->external->setAutoZoomScale(
        RendererBridge::translateAutoZoomScale(state));
  }
}

void
SoFCRendererP::updateSectionStyle()
{
  this->section.fill = Gui::sectionStyle(this->viewobject, "Fill",
                                         ViewParams::getSectionFill());
  this->section.fillInvert = Gui::sectionStyle(this->viewobject, "FillInvert",
                                               ViewParams::getSectionFillInvert());
  this->section.fillGroup = Gui::sectionStyle(this->viewobject, "FillGroup",
                                              ViewParams::getSectionFillGroup());
  this->section.concave = Gui::sectionStyle(this->viewobject, "Concave",
                                            ViewParams::getSectionConcave());
  this->section.hatch = Gui::sectionStyle(this->viewobject, "Hatch",
                                          ViewParams::getSectionHatchTextureEnable());
  this->section.hatchScale = Gui::sectionStyle(this->viewobject, "HatchScale",
                                               ViewParams::getSectionHatchTextureScale());
  this->section.noOnTop = Gui::sectionStyle(this->viewobject, "NoOnTop",
                                            ViewParams::getNoSectionOnTop());
}

RendererBridge::SectionOnTop
SoFCRendererP::sectionOnTop() const
{
  RendererBridge::SectionOnTop res;
  res.noOnTop = Gui::sectionStyle(this->viewobject, "NoOnTop",
                                  ViewParams::getNoSectionOnTop());
  res.concave = Gui::sectionStyle(this->viewobject, "Concave",
                                  ViewParams::getSectionConcave());
  return res;
}

void
SoFCRenderer::setViewObject(App::PropertyContainer * view)
{
  PRIVATE(this)->viewobject = view;
  PRIVATE(this)->updateSectionStyle();
}

void
SoFCRenderer::render(SoGLRenderAction * action)
{
  // In overlay-capture mode this renderer only exists as a feed conduit:
  // the backend draws the overlay itself, and the GL fallback keeps its
  // own drawing path (e.g. drawAxisCross), so never render here and never
  // push per-frame configs.
  if (PRIVATE(this)->overlaymode)
    return;

  // Drawing the frame from the state the stages above produced.
  Gui::RenderTiming::Scope timing(Gui::RenderTiming::Submit);

  pushExternalConfigs(action->getState());

  // When an external backend has rendered the current scene (it draws into
  // the framebuffer before the Coin traversal), skip the internal
  // fixed-function GL pass entirely. FC_RENDERER_PARALLEL_GL=1 keeps this
  // pass drawing on top of the backend output for A/B comparison.
  static const bool parallelgl = (std::getenv("FC_RENDERER_PARALLEL_GL") != nullptr);
  if (PRIVATE(this)->external
      && !parallelgl
      && PRIVATE(this)->external->canSkipInternal())
    return;

  SoState * state = action->getState();

  const SoShapeStyleElement * shapestyle = SoShapeStyleElement::get(state);
  unsigned int shapestyleflags = shapestyle->getFlags();

  PRIVATE(this)->shadowrenderpaused = false;
  PRIVATE(this)->shadowrendering = (shapestyleflags & SoShapeStyleElement::SHADOWS) ? true : false;
  PRIVATE(this)->shadowmapping = (shapestyleflags & SoShapeStyleElement::SHADOWMAP) ? true : false;
  PRIVATE(this)->transpshadowmapping = PRIVATE(this)->shadowmapping && (shapestyleflags & 0x01000000);

  PRIVATE(this)->showHiddenLine = SoFCDisplayModeElement::showHiddenLines(state, &PRIVATE(this)->hiddenLineConfig);

  PRIVATE(this)->updateSectionStyle();

  PRIVATE(this)->section_entries.clear();
  PRIVATE(this)->transp_section_entries.clear();

  if (PRIVATE(this)->shadowmapping
      && !PRIVATE(this)->transpshadowmapping
      && PRIVATE(this)->transpvcache.size()
      && !action->isRenderingDelayedPaths()
      && !action->isRenderingTranspPaths())
  {
    // signal SoShadowGroup to render transparent shadow
    action->handleTransparency(TRUE);
  }

  PRIVATE(this)->updateSelection();

  PRIVATE(this)->depthwriteonly = false;
  PRIVATE(this)->notexture = false;
  PRIVATE(this)->prevmaterial = nullptr;
  PRIVATE(this)->recheckmaterial = false;
  PRIVATE(this)->material.init();

  glPushAttrib(GL_ALL_ATTRIB_BITS);
  state->push();

  SoLazyElement::setToDefault(state);
  glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
  glEnable(GL_COLOR_MATERIAL);

  PRIVATE(this)->matrix = SoModelMatrixElement::get(state);
  PRIVATE(this)->identity = (PRIVATE(this)->matrix == SbMatrix::identity());

  if (!action->isRenderingDelayedPaths()) {
    PRIVATE(this)->drawcallcount = 0;

    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->drawentries,
                                PRIVATE(this)->opaquevcache);

    PRIVATE(this)->recheckmaterial = true;
    // PRIVATE(this)->notexture = true;

    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->slentries,
                                PRIVATE(this)->opaqueselections,
                                RenderPassHighlight);

    PRIVATE(this)->renderSectionGrouped(action, false);

    PRIVATE(this)->recheckmaterial = true;
    PRIVATE(this)->notexture = false;

    if (!PRIVATE(this)->shadowmapping) {
      action->addDelayedPath(action->getCurPath()->copy());
      state->pop();
      glPopAttrib();
      FC_GLERROR_CHECK;
      SoGLLazyElement::getInstance(state)->reset(state,
                                                SoLazyElement::LIGHT_MODEL_MASK|
                                                SoLazyElement::TWOSIDE_MASK|
                                                SoLazyElement::SHADE_MODEL_MASK);
      return;
    }
  }

  PRIVATE(this)->renderTransparency(action,
                                    PRIVATE(this)->drawentries,
                                    PRIVATE(this)->transpvcache);

  PRIVATE(this)->recheckmaterial = true;

  PRIVATE(this)->renderTransparency(action,
                                    PRIVATE(this)->slentries,
                                    PRIVATE(this)->transpselections);

  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->drawentries,
                              PRIVATE(this)->opaqueontop);

  PRIVATE(this)->renderTransparency(action,
                                    PRIVATE(this)->drawentries,
                                    PRIVATE(this)->transpontop,
                                    false);

  PRIVATE(this)->renderSectionGrouped(action, true);

  if (PRIVATE(this)->shadowmapping) {
    state->pop();
    glPopAttrib();
    FC_GLERROR_CHECK;
    SoGLLazyElement::getInstance(state)->reset(state,
                                              SoLazyElement::LIGHT_MODEL_MASK|
                                              SoLazyElement::TWOSIDE_MASK|
                                              SoLazyElement::SHADE_MODEL_MASK);
    return;
  }
  
  PRIVATE(this)->recheckmaterial = true;
  // PRIVATE(this)->notexture = true;

  PRIVATE(this)->renderTransparency(action,
                                    PRIVATE(this)->slentries,
                                    PRIVATE(this)->transpselectionsontop,
                                    false);

  if (PRIVATE(this)->hlwholeontop) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->hlentries,
                                  PRIVATE(this)->opaquehighlight);
      PRIVATE(this)->renderTransparency(action,
                                        PRIVATE(this)->hlentries,
                                        PRIVATE(this)->transphighlight,
                                        false);
  }

  bool hassel = PRIVATE(this)->selsontop.size()
                        || PRIVATE(this)->selslineontop.size();
  bool hasontop = PRIVATE(this)->trianglesontop.size()
                      && PRIVATE(this)->linesontop.size();
  int pass = RenderPassNormal;

  if (hassel || hasontop || PRIVATE(this)->hlwholeontop) {
    // If there is lines/points on top perform a depth write only rendering
    // pass for all the triangles on top, so that we can distinguish line style
    // for hidden (by depth test) and non-hidden lines/points.

    PRIVATE(this)->recheckmaterial = true;
    PRIVATE(this)->depthwriteonly = true;
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    if (hasontop) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->drawentries,
                                  PRIVATE(this)->trianglesontop);
    }

    if (hassel) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->slentries,
                                  PRIVATE(this)->selstriangleontop);
    }

    if (PRIVATE(this)->hlwholeontop) {
        PRIVATE(this)->renderOpaque(action,
                                    PRIVATE(this)->hlentries,
                                    PRIVATE(this)->opaquehighlight,
                                    RenderPassHighlight);
        PRIVATE(this)->renderTransparency(action,
                                          PRIVATE(this)->hlentries,
                                          PRIVATE(this)->transphighlight,
                                          false);
    }

    PRIVATE(this)->depthwriteonly = false;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    pass = RenderPassLinePattern;
  }

  // Even if we are calling renderOpaque() below (because of lines and points
  // render), we shall still respect the transparency setting, e.g. we'll use
  // transparency to dim the hidden lines. So we enable blending here.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  FC_GLERROR_CHECK;

  // Rendering lines/points on top (i.e. without depth test), with user
  // configurable line pattern.
  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->drawentries,
                              PRIVATE(this)->linesontop,
                              pass);

  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->slentries,
                              PRIVATE(this)->selsontop,
                              pass);

  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->slentries,
                              PRIVATE(this)->selslineontop,
                              pass | RenderPassHighlight);

  if (PRIVATE(this)->hlwholeontop) {
    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->hlentries,
                                PRIVATE(this)->opaquelineshighlight,
                                pass);
  }

  if (hassel || hasontop || PRIVATE(this)->hlwholeontop) {
    // Second pass for rendering non-hidden lines/points. The depth test will
    // be enabled by applyMaterial() up on seeing this RenderPassLineSolid
    pass = RenderPassLineSolid;

    if (hasontop) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->drawentries,
                                  PRIVATE(this)->linesontop,
                                  pass);
    }
    if (hassel) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->slentries,
                                  PRIVATE(this)->selsontop,
                                  pass);

      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->slentries,
                                  PRIVATE(this)->selslineontop,
                                  pass | RenderPassHighlight);
    }

    if (PRIVATE(this)->hlwholeontop) {
      PRIVATE(this)->renderOpaque(action,
                                  PRIVATE(this)->hlentries,
                                  PRIVATE(this)->opaquelineshighlight,
                                  pass | RenderPassHighlight);
    }
  }

  glDisable(GL_BLEND);
  FC_GLERROR_CHECK;

  PRIVATE(this)->renderTransparency(action,
                                    PRIVATE(this)->slentries,
                                    PRIVATE(this)->transpselectionsfaceontop,
                                    false);

  if (!PRIVATE(this)->hlwholeontop) {
    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->hlentries,
                                PRIVATE(this)->opaquehighlight);

    PRIVATE(this)->renderTransparency(action,
                                      PRIVATE(this)->hlentries,
                                      PRIVATE(this)->transphighlight,
                                      false);
    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->hlentries,
                                PRIVATE(this)->opaquelineshighlight,
                                RenderPassHighlight);
  }

  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->slentries,
                              PRIVATE(this)->selspointontop,
                              RenderPassHighlight);

  PRIVATE(this)->renderOpaque(action,
                              PRIVATE(this)->hlentries,
                              PRIVATE(this)->highlightlinesontop,
                              RenderPassHighlight);

  PRIVATE(this)->renderSceneOutline(action);

  if (ViewParams::getShowSelectedFaceOutline()) {
    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->slentries,
                                PRIVATE(this)->seloutline,
                                RenderPassSelectionOutline);
  }

  if (ViewParams::getShowPreSelectedFaceOutline()) {
    PRIVATE(this)->renderOpaque(action,
                                PRIVATE(this)->hlentries,
                                PRIVATE(this)->preseloutline,
                                RenderPassSelectionOutline);
  }

  state->pop();
  glPopAttrib();
  FC_GLERROR_CHECK;
  SoGLLazyElement::getInstance(state)->reset(state,
                                             SoLazyElement::LIGHT_MODEL_MASK|
                                             SoLazyElement::TWOSIDE_MASK|
                                             SoLazyElement::SHADE_MODEL_MASK);
}

const char *
SoFCRenderer::getStatistics() const
{
  snprintf(PRIVATE(this)->stats, sizeof(PRIVATE(this)->stats)-1,
      "draw calls: %d", PRIVATE(this)->drawcallcount);
  return PRIVATE(this)->stats;
}

// vim: noai:ts=2:sw=2
