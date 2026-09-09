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

#include <cstdio>
#include <cstdlib>

#include <Inventor/lists/SoTypeList.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/elements/SoGLCacheContextElement.h>
#include <Inventor/elements/SoTextureEnabledElement.h>
#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/elements/SoShapeStyleElement.h>
#include <Inventor/elements/SoModelMatrixElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/annex/FXViz/nodes/SoShadowStyle.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/nodes/SoIndexedShape.h>
#include <Inventor/nodes/SoImage.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoResetTransform.h>
#include <Inventor/nodes/SoBumpMap.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include "SoFCRenderMaterial.h"
#include "SoFCRendererBridge.h"
#include "../Renderer/Renderer.h"
#include <Inventor/nodes/SoTexture2Transform.h>
#include <Inventor/nodes/SoTexture3Transform.h>
#include <Inventor/nodes/SoTextureMatrixTransform.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/VRMLnodes/SoVRMLMaterial.h>
#include <Inventor/VRMLnodes/SoVRMLTexture.h>
#include <Inventor/VRMLnodes/SoVRMLTextureTransform.h>
#include <Inventor/VRMLnodes/SoVRMLLight.h>
#include <Inventor/VRMLnodes/SoVRMLColor.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLight.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoIndexedShape.h>
#include <Inventor/sensors/SoDataSensor.h>
#include <Inventor/sensors/SoIdleSensor.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/sensors/SoPathSensor.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/details/SoFaceDetail.h>
#include <Inventor/details/SoLineDetail.h>
#include <Inventor/details/SoPointDetail.h>
#include <Inventor/misc/SoTempPath.h>
#include <Inventor/misc/SoChildList.h>
#include <Inventor/errors/SoDebugError.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/flat_set.hpp>
#include <unordered_map>
#include <map>
#include <set>

#include <Base/Console.h>
#include "../ViewParams.h"
#include "../RenderParams.h"
#include <chrono>
#include "../RenderTiming.h"
#include "../InventorBase.h"
#include "../SoFCBoundingBox.h"
#include "../SoFCUnifiedSelection.h"
#include "../SoFCSelectionAction.h"
#include "../SoFCSelection.h"

#include "SoFCVertexCache.h"
#include "SoFCRenderCache.h"
#include "SoFCRenderer.h"
#include "SoFCRenderCacheManager.h"
#include "ScenePublishDelta.h"
#include "SoFCZoomOffsetElement.h"

using namespace Gui;

FC_LOG_LEVEL_INIT("Renderer", true, true)

// ---------------------------------------------------------------

typedef CoinPtr<SoPath> PathPtr;
typedef CoinPtr<SoFCVertexCache> VertexCachePtr;
typedef CoinPtr<SoFCRenderCache> RenderCachePtr;
typedef SoFCRenderCache::VertexCacheMap VertexCacheMap;
typedef SoFCRenderCache::Material Material;

struct ElementEntry {
  ElementEntry()
    : id(0), color(0)
  {}

  std::unique_ptr<SoDetail> detail;
  int id;
  uint32_t color;
  VertexCacheMap vcachemap;
  // Set for shader-override entries (addShaderOverride): the rebuilt
  // vcachemap's triangle materials carry this shader.
  std::shared_ptr<const Render::UserShader> usershader;
};

// Re-key the map so its triangle materials carry the user shader; the
// non-on-top whole-object entry then draws in the normal scene passes
// with the shader substituted, while the base draw is key-suppressed.
static VertexCacheMap
applyUserShader(const VertexCacheMap & vcachemap,
                const std::shared_ptr<const Render::UserShader> & shader)
{
  VertexCacheMap res;
  for (auto & v : vcachemap) {
    Material material = v.first;
    if (material.type == Material::Triangle)
      material.usershader = shader;
    auto & entries = res[material];
    entries.insert(entries.end(), v.second.begin(), v.second.end());
  }
  return res;
}

// ---------------------------------------------------------------
// Capture companion for stock SoImage shapes (Sketcher constraint icons,
// SoFrameLabel, ...). SoImage draws in screen space (glDrawPixels at a
// projected point, constant pixel size), but its generatePrimitives()
// emits a quad sized in model units for the capture-time view — baked
// into a static vertex cache, that quad then scales WITH the camera
// (the "thickening ghost" on zoom), and the texel data set inside
// generatePrimitives never reaches the captured material, so it renders
// as a solid diffuse block. preShape() substitutes this companion
// instead (same recipe as SoTextImage / SoDatumLabel's glyph): the image
// as a REPLACE-mode texture on a quad in native pixel units under a
// billboard SoAutoZoomTranslation with pixelScale 1, which the backend
// re-scales to exact raw-GL pixel size every frame.
// ---------------------------------------------------------------

class SoFCImageQuad : public SoShape {
  typedef SoShape inherited;
  SO_NODE_HEADER(SoFCImageQuad);

public:
  static void initClass();
  SoFCImageQuad();

  // Read by SoFCVertexCache (by field name) so the explicit UVs are
  // captured even though no texture unit is enabled on the capture
  // traversal.
  SoSFBool forceTexCoords;
  // Constant screen-space quad offset in pixels — the accumulated
  // Sketcher zoom-translation part (SoFCZoomOffsetElement), which the GL
  // path recomputes from the view each frame and so must not be baked
  // into the anchor matrix. Written by preImage(); a change gives the
  // node a fresh id, retiring the stale vertex cache.
  SoSFVec3f pixelOffset;
  const SoImage *owner = nullptr;

protected:
  ~SoFCImageQuad() override = default;
  // Only ever traversed by the capture action; the owning SoImage handles
  // GL drawing and picking itself.
  void GLRender(SoGLRenderAction *) override {}
  void computeBBox(SoAction *, SbBox3f & box, SbVec3f & center) override;
  void generatePrimitives(SoAction * action) override;
};

SO_NODE_SOURCE(SoFCImageQuad)

void SoFCImageQuad::initClass()
{
  SO_NODE_INIT_CLASS(SoFCImageQuad, SoShape, "Shape");
}

SoFCImageQuad::SoFCImageQuad()
{
  SO_NODE_CONSTRUCTOR(SoFCImageQuad);
  SO_NODE_ADD_FIELD(forceTexCoords, (TRUE));
  SO_NODE_ADD_FIELD(pixelOffset, (SbVec3f(0.f, 0.f, 0.f)));
}

void
SoFCImageQuad::computeBBox(SoAction *, SbBox3f & box, SbVec3f & center)
{
  // The quad is emitted at the origin in native pixels and placed
  // screen-constant by the autozoom; contribute only a point so it
  // neither dominates fitAll nor leaves an invalid bbox.
  box.setBounds(SbVec3f(0.f, 0.f, 0.f), SbVec3f(0.f, 0.f, 0.f));
  center = SbVec3f(0.f, 0.f, 0.f);
}

void
SoFCImageQuad::generatePrimitives(SoAction * action)
{
  if (!this->owner || !action->isOfType(SoCallbackAction::getClassTypeId()))
    return;

  SbVec2s size;
  int nc;
  if (!this->owner->image.getValue(size, nc)
      || size[0] <= 0 || size[1] <= 0)
    return;
  // The width/height fields override the on-screen size (SoImage::getSize).
  if (this->owner->width.getValue() > 0)
    size[0] = short(this->owner->width.getValue());
  if (this->owner->height.getValue() > 0)
    size[1] = short(this->owner->height.getValue());

  const float w = float(size[0]);
  const float h = float(size[1]);

  // GL parity (SoImage::getQuad): quad centred on the projected anchor
  // point, then shifted half a size per alignment.
  const SbVec3f & off = this->pixelOffset.getValue();
  float x0 = off[0] - 0.5f * w;
  switch (this->owner->horAlignment.getValue()) {
  case SoImage::LEFT:  x0 += 0.5f * w; break;
  case SoImage::RIGHT: x0 -= 0.5f * w; break;
  default: break; // CENTER
  }
  float y0 = off[1] - 0.5f * h;
  switch (this->owner->vertAlignment.getValue()) {
  case SoImage::TOP:    y0 -= 0.5f * h; break;
  case SoImage::BOTTOM: y0 += 0.5f * h; break;
  default: break; // HALF
  }
  const float x1 = x0 + w;
  const float y1 = y0 + h;

  // Coin images are stored bottom-up: v=0 is the bottom row.
  struct Corner { float x, y, u, v; };
  const Corner corners[4] = {
      {x0, y0, 0.f, 0.f},
      {x1, y0, 1.f, 0.f},
      {x1, y1, 1.f, 1.f},
      {x0, y1, 0.f, 1.f},
  };

  SoPrimitiveVertex pv;
  pv.setNormal(SbVec3f(0.f, 0.f, 1.f));
  pv.setMaterialIndex(0);
  auto emit = [&](int i) {
    pv.setPoint(SbVec3f(corners[i].x, corners[i].y, 0.f));
    pv.setTextureCoords(SbVec4f(corners[i].u, corners[i].v, 0.f, 1.f));
    shapeVertex(&pv);
  };

  this->beginShape(action, TRIANGLES);
  emit(0); emit(1); emit(2);
  emit(0); emit(2); emit(3);
  this->endShape();
}

// ---------------------------------------------------------------

class SelectionSensor : public SoNodeSensor {
public:
  SelectionSensor()
    :tmpPath(10)
  {
    tmpPath.ref();
  }

  ~SelectionSensor() {
    attachPath(nullptr);
    tmpPath.unrefNoDelete();
  }

  void attachPath(SoPath *path)
  {
    int adjustment;
    if (path) {
      attachPath(nullptr);
      adjustment = 1;
    } else 
      adjustment = -1;

    if (path) {
      tmpPath.append(path);
      int idx = 0;
      if (path->getLength() > 1
          && path->getHead()->isOfType(SoFCUnifiedSelection::getClassTypeId()))
          ++idx;
      attach(path->getNode(idx));
    } else
      detach();

    attachedPath = path;
    for (int i=0, c=tmpPath.getLength(); i<c; ++i) {
      auto node = tmpPath.getNode(i);
      if (node->isOfType(SoFCSwitch::getClassTypeId())) {
        auto pcSwitch = static_cast<SoFCSwitch*>(node);
        int v = pcSwitch->childNotify.getValue() + adjustment;
        if (v < 0)
          v = 0;
        pcSwitch->childNotify.enableNotify(FALSE);
        pcSwitch->childNotify = v;
        pcSwitch->childNotify.enableNotify(TRUE);
      }
    }
    if (!path)
      tmpPath.truncate(0);
  }

  void refresh(SoFCRenderer * renderer) {
    if (!attachedPath)
      return;
    SoPath *path = attachedPath;
    // Coin truncates an SoPath at the point where a node leaves its parent, so
    // any structural change under us (a PartDesign tip swap re-parenting the
    // body's children, a display mode rebuild) leaves the path a prefix of what
    // we resolved. Rebuild the missing tail from the full copy in tmpPath.
    if (path->getLength() && path->getLength() < tmpPath.getLength()) {
      auto node = tmpPath.getNode(path->getLength()-1);
      for (int i=path->getLength(), c=tmpPath.getLength(); i<c; ++i) {
        auto child = tmpPath.getNode(i);
        auto children = node->getChildren();
        if (!children)
          break;
        bool found = false;
        for (int j=0, n=children->getLength(); j<n; ++j) {
          if ((*children)[j] == child) {
            found = true;
            path->append(j);
            break;
          }
        }
        if (!found)
          break;
        node = child;
      }
    }

    // If the tail could not be restored the path no longer reaches the object
    // it was resolved for. It must not be traversed: a path apply visits
    // everything below its tail, and the on-top pass only overrides the
    // switches *on* the path -- so a prefix ending near the scene root drags
    // every visible object into the on-top group while leaving the intended
    // (typically hidden) one out of it. Report it instead and let the Gui layer
    // re-resolve from the App::SubObjectT.
    broken = path->getLength() < tmpPath.getLength();

    if (this->cache) {
      this->cache.reset();
      for (auto & v : this->elements) {
        renderer->removeSelection(v.second.id);
        v.second.vcachemap.clear();
      }
    }
  }

  SoTempPath tmpPath;
  CoinPtr<SoPath> attachedPath;
  std::unordered_map<std::string, ElementEntry> elements;
  RenderCachePtr cache;
  bool ontop = false;
  // The attached path no longer reaches the object it was resolved for; see
  // refresh(). Set here, consumed by updateSelection() and reported out
  // through takeInvalidSelections().
  bool broken = false;
  // selcaches key this sensor lives under, so a broken entry can be named.
  std::string key;
};

typedef std::unordered_map<PathPtr,
                           SelectionSensor,
                           PathHasher<PathPtr>,
                           PathHasher<PathPtr>> SelectionPathMap;

class SoFCRenderCacheManagerP {
public:
  SoFCRenderCacheManagerP();
  ~SoFCRenderCacheManagerP();

  void addSelection(const char *key,
                    const SoDetail * detail,
                    uint32_t color,
                    bool ontop,
                    bool alt);

  void initAction();
  void doLatePick(SoRayPickAction *action) const;

  static SoCallbackAction::Response preSeparator(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postSeparator(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preFCSel(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postFCSel(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postSep(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preLatePickGroup(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preAnnotation(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postAnnotation(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response prePathAnnotation(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postPathAnnotation(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preSkipBounds(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postSkipBounds(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preShape(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preImage(SoFCRenderCacheManagerP *self, SoCallbackAction *action, const SoImage * node);
  static SoCallbackAction::Response postShape(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postClipPlane(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response preAutoZoom(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postAutoZoom(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postShadowStyle(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postLightModel(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postLight(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postVRMLLight(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postMaterial(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postVRMLMaterial(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postColor(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postVRMLColor(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postDepthBuffer(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postResetTransform(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postTextureTransform(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postTexture(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postBumpMap(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postRenderMaterial(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postRenderTexture(void *, SoCallbackAction *action, const SoNode * node);
  static SoCallbackAction::Response postShaderProgram(void *, SoCallbackAction *action, const SoNode * node);
  static void addTriangle(void *,
                          SoCallbackAction * action,
                          const SoPrimitiveVertex * v0,
                          const SoPrimitiveVertex * v1,
                          const SoPrimitiveVertex * v2);
  static void addLine(void *,
                      SoCallbackAction * action,
                      const SoPrimitiveVertex * v0,
                      const SoPrimitiveVertex * v1);
  static void addPoint(void *, SoCallbackAction * action, const SoPrimitiveVertex * v);

  static void updateSelection(void *, SoSensor *sensor);

  class NodeSensor : public SoDataSensor
  {
  public:
    NodeSensor()
      : node(NULL)
    {}

    ~NodeSensor() { detach(); }

    void attach(SoFCRenderCacheManagerP * master, const SoNode *node) {
      (void)master;
      if (this->node == node) return;
      assert(!this->node);
      this->node = const_cast<SoNode *>(node);
      this->node->addAuditor(this, SoNotRec::SENSOR);
    }

    void detach() {
      if (!this->node) return;
      this->node->removeAuditor(this, SoNotRec::SENSOR);
      this->node = NULL;
    }

    SoNode * node;
  };

  class CacheSensor : public NodeSensor
  {
  public:
    virtual void dyingReference(void) {
      SoNode * node = this->node;
      this->detach();
      for (auto &cache : caches)
        cache->resetNode();
      SoFCRenderCacheManagerP::cachetable.erase(node);
    }

    SbFCVector<RenderCachePtr> caches;
  };

  class VCacheSensor : public NodeSensor
  {
  public:
    virtual void dyingReference(void) {
      SoNode * node = this->node;
      this->detach();
      for (auto &vcache : caches)
        vcache->resetNode();
      SoFCRenderCacheManagerP::vcachetable.erase(node);
    }

    SbFCVector<VertexCachePtr> caches;

    // SoImage nodes only: the capture companion sub-graph
    // [SoTexture2(REPLACE) -> SoAutoZoomTranslation(billboard, pixelScale 1)
    //  -> SoFCImageQuad] traversed in the node's place by preShape(), and
    // the owner node-id it was last captured against (a change retires the
    // companion quad's vertex cache — the quad's own node-id cannot see
    // owner edits like an icon recolour/resize).
    CoinPtr<SoSeparator> imageroot;
    SoFCImageQuad *imagequad = nullptr;
    SbFCUniqueId imageid = 0;
  };

  class PathCacheSensor : public SoPathSensor
  {
  public:
    PathCacheSensor()
    {
      setFunction([](void *, SoSensor *sensor) {
        auto self = static_cast<PathCacheSensor*>(sensor);
        self->detach();
        if (self->master->highlightcache == self->cache) {
          self->master->highlightcache.reset();
          self->master->renderer->clearHighlight();
        }
        self->master->pathcachetable.erase(self->path);
        return;
      });
    }

    virtual void notify(SoNotList * l) {
#if 1
      (void)l;
#else
      SoBase * firstbase = l->getLastRec()->getBase();
      SoBase * lastbase = l->getFirstRec()->getBase();
      if (lastbase != firstbase)
#endif
      {
        if (!isScheduled())
          schedule();
      }
    }

    RenderCachePtr cache;
    SoFCRenderCacheManagerP *master = nullptr;
    PathPtr path;
  };

  class LatePickPathSensor : public SoPathSensor
  {
  public:
    LatePickPathSensor()
    {
      setFunction([](void *, SoSensor *sensor) {
        auto self = static_cast<LatePickPathSensor*>(sensor);
        self->detach();
        self->master->latepickpaths.truncate(0);
        self->master->latepicktable.erase(self->tmpPath);
      });
    }

    virtual void notify(SoNotList * l) {
      (void)l;
      master->latepickpaths.truncate(0);
      if (!isScheduled())
        schedule();
    }

    SoFCRenderCacheManagerP *master = nullptr;
    PathPtr tmpPath;
    PathPtr attachedPath;
  };

  static FC_COIN_THREAD_LOCAL std::unordered_map<const SoNode *, CacheSensor> cachetable;
  static FC_COIN_THREAD_LOCAL std::unordered_map<const SoNode *, VCacheSensor> vcachetable;
  std::unordered_map<PathPtr,
                     PathCacheSensor,
                     PathHasher<PathPtr>,
                     PathHasher<PathPtr>> pathcachetable;
  std::unordered_map<PathPtr,
                     LatePickPathSensor,
                     PathHasher<PathPtr>,
                     PathHasher<PathPtr>> latepicktable;
  mutable SoPathList latepickpaths;
  bool obeysrules;
  RenderCachePtr highlightcache;
  CoinPtr<SoPath> highlightpath;
  // Whether on-top draws escape the section, as this view answers for it
  // (Section_NoOnTop, the preference behind it otherwise). The highlight
  // caches below are built with it baked in, so they are dropped whenever
  // it moves.
  bool nosectionontop = false;
  // The owning 3D view object, for that override.
  App::PropertyContainer *viewobject = nullptr;
  bool sectionNoOnTop() const {
    return Gui::sectionStyle(viewobject, "NoOnTop",
                             ViewParams::getNoSectionOnTop());
  }
  bool override_selectstyle = false;

  SoCallbackAction *action;
  int shapetypeid;
  VertexCachePtr vcache;

  // Last real viewport seen by render()/capture(), re-applied when
  // initAction() recreates the action (screen-space captures read it).
  SbViewportRegion lastvp;
  bool lastvpset = false;

  std::unordered_map<std::string, SelectionPathMap> selcaches;
  // On-top selcaches keys whose Coin path went stale and could not be
  // rebuilt (SelectionSensor::refresh). Drained by takeInvalidSelections().
  std::set<std::string> invalidsels;
  // Path-keyed user-shader overrides (addShaderOverride): same sensor
  // machinery as selcaches, kept separate so selection bookkeeping
  // (clearSelection, isOnTop, ...) never touches them.
  std::unordered_map<std::string, SelectionPathMap> shadercaches;
  SbFCMap<int, CoinPtr<SoPath> > selpaths;

  SbFCMap<int, VertexCachePtr> sharedcache;

  int selid;
  
  SbFCVector<RenderCachePtr> stack;
  SbFCVector<SbFCUniqueId> selnodeid;
  // User shader programs (SoShaderProgram nodes) captured during the
  // current cache-rebuild traversal (docs/RenderDebug.md §6). Note that
  // a program inside a still-valid cached separator is pruned with its
  // subtree, so scene-level programs belong at the top level of the
  // scene graph.
  Render::UserShaderConfig usershaders;
  SbFCUniqueId sceneid;
  // What the last publish changed in the scene cache's children, and the
  // previous scene cache it changed them from (docs/IncrementalPublish.md
  // §5). Recorded on every publish; not consumed by one yet.
  ScenePublishDelta publishdelta;
  boost::container::flat_set<const SoNode *> nodeset;
  const SoNode * prunenode;
  int traversedepth;
  SoFCRenderer *renderer;
  int annotation;
  /// Nesting depth of Gui::SoSkipBoundingGroup in the traversal. The
  /// group can nest (a gizmo built out of gizmos), so the flag is
  /// cleared by the outermost close, not by the first one.
  int skipbounds;

  // The capture budget of one publish (Render CaptureBudgetMS): how much
  // time this rebuild's shape captures have spent, how many shapes were
  // captured and deferred, and whether the budget applies at all -- only
  // render()'s rebuild sets it, so a sensor-triggered selection capture
  // outside a publish is never deferred (nothing would schedule the
  // follow-up publish that catches a deferred shape up).
  bool capturebudgeting = false;
  double capturespentms = 0.0;
  int capturecount = 0;
  int deferredcount = 0;
  std::chrono::steady_clock::time_point capturestart;

  // Worker-emitted content adopted by the open capture, or held for the
  // verify arm's compare at postShape (docs/WorkerVertexCache.md).
  std::shared_ptr<const SoFCVertexCache::PrebuiltContent> verifyprebuilt;
  int adoptedcount = 0;

  // Verify arm only (Render_WorkerVertexCache = 2): comparisons that
  // ran and AGREED. A mismatch shouts on its own; agreement has to be
  // counted, or "no mismatch" is indistinguishable from "compared
  // nothing" and the arm passes by being silent.
  int verifiedcount = 0;

  // Why shapes that DID have worker content still could not adopt it,
  // keyed on the literal SoFCVertexCache::prebuiltReject() returned --
  // pointer identity is enough, they all come from one function. The
  // registry-side half of the same question is counted there
  // (SoFCVertexCache::prebuiltStats).
  std::vector<std::pair<const char *, int> > adoptrejects;

  // Which node CLASS was registered and then touched before the publish
  // could pick it up. Keyed on the interned SoType name, so the same
  // pointer-identity counting works.
  std::vector<std::pair<const char *, int> > adoptstale;

  static void count(std::vector<std::pair<const char *, int> > & tally,
                    const char * key)
  {
    if (!key)
      return;
    for (auto & entry : tally) {
      if (entry.first == key) {
        ++entry.second;
        return;
      }
    }
    tally.emplace_back(key, 1);
  }

  void countAdoptReject(const char * why) { count(this->adoptrejects, why); }

  void countAdoptStale(const SoNode * node)
  {
    count(this->adoptstale, node->getTypeId().getName().getString());
  }
};

std::unordered_map<const SoNode *,
                   SoFCRenderCacheManagerP::CacheSensor> SoFCRenderCacheManagerP::cachetable;

std::unordered_map<const SoNode *,
                   SoFCRenderCacheManagerP::VCacheSensor> SoFCRenderCacheManagerP::vcachetable;

#define PRIVATE(obj) ((obj)->pimpl)

static FC_COIN_THREAD_LOCAL int _shapetypeid = -1;

// Marks a render-cache capture traversal for the duration of an apply():
// Sketcher-style zoom translations divert their per-frame screen-space
// offset into SoFCZoomOffsetElement only while this is set.
struct CaptureFlagGuard {
  CaptureFlagGuard() { SoFCZoomOffsetElement::setCapturing(true); }
  ~CaptureFlagGuard() { SoFCZoomOffsetElement::setCapturing(false); }
};

static int
getMaxShapeTypeId()
{
  SoTypeList derivedtypes;
  int n = SoType::getAllDerivedFrom(SoShape::getClassTypeId(), derivedtypes);
  int res = 0;
  for (int i=0; i<n; ++i) {
    int idx = static_cast<int>(derivedtypes[i].getData());
    if (res < idx)
      res = idx;
  }
  return res;
}

SoFCRenderCacheManagerP::SoFCRenderCacheManagerP()
{
  this->obeysrules = boost::ends_with(SoDB::getVersion(), "rt");
  this->prunenode = nullptr;
  this->selid = 0;
  this->sceneid = 0;
  this->annotation = 0;
  this->skipbounds = 0;
  this->action = nullptr;
  this->shapetypeid = 0;

  if (SoFCImageQuad::getClassTypeId() == SoType::badType())
    SoFCImageQuad::initClass();

  if (_shapetypeid < 0) {
    // Raw-GL draw of screen-space images is suppressed while an external
    // backend draws their captured companions (SoFCImageQuad), mirroring
    // SoDatumLabel::SuppressGLRender — the flag is set per composite pass
    // by View3DInventorViewer. Registered on SoBoxSelectionRenderAction
    // (the viewer's render action) as well: its method table is built from
    // SoGLRenderAction's at its own setUp and does not re-sync a parent
    // override added afterwards.
    auto imageMethod = [](SoAction *action, SoNode *node) {
        static int dbg = std::getenv("FC_DEBUG_IMAGEQUAD") ? 1 : 0;
        if (dbg)
            fprintf(stderr, "SoImage GLRender %p suppress=%d\n",
                    static_cast<void*>(node),
                    int(SoFCRenderCacheManager::SuppressImageGLRender));
        if (!SoFCRenderCacheManager::SuppressImageGLRender)
            SoNode::GLRenderS(action, node);
    };
    SoGLRenderAction::addMethod(SoImage::getClassTypeId(), imageMethod);
    if (SoBoxSelectionRenderAction::getClassTypeId() != SoType::badType())
        SoBoxSelectionRenderAction::addMethod(
            SoImage::getClassTypeId(), imageMethod);

    // In case the shape node is defined in late loaded module, we need to
    // re-init SoCallbackAction (by calling initAction()), because
    // SoCallbackAction only works for existing type ID (i.e. all existing
    // SoShape derived types in our case) at the time of calling
    // addPre/PostCallback().
    SoCallbackAction::addMethod(SoShape::getClassTypeId(),
        [](SoAction *action, SoNode *node) {
            if (_shapetypeid < static_cast<int>(node->getTypeId().getData())) {
                node->touch(); // make sure to revisit
                _shapetypeid = 0;
            }

            // Sanity check for indexed shape to make sure all indices are
            // within the boundary. There is a bug in
            // SoIndexedLineSet::generatePrimitive() that causing visual
            // defects if there is invalid index. In libCoin3D debug build,
            // this will trigger assertion.
            //
            // See https://github.com/realthunder/FreeCAD/issues/445

            if (static_cast<SoShape*>(node)->isOfType(SoIndexedShape::getClassTypeId())) {
              auto indexed_shape = static_cast<SoIndexedShape*>(node);
              auto callback = static_cast<SoCallbackAction*>(action);
              int numcoords = callback->getNumCoordinates();
              auto indices = indexed_shape->coordIndex.getValues(0);
              for (int i=0, count=indexed_shape->coordIndex.getNum(); i<count; ++i) {
                if (indices[i] < -1 || indices[i] >= numcoords)
                  return;
              }
            }
            SoNode::callbackS(action, node);
        }
    );
  }
  initAction();
  this->renderer = new SoFCRenderer;
}

void SoFCRenderCacheManagerP::initAction()
{
  if (this->action && this->shapetypeid && _shapetypeid == this->shapetypeid)
    return;
  delete this->action;
  _shapetypeid = this->shapetypeid = getMaxShapeTypeId();
  this->action = new SoCallbackAction;
  if (this->lastvpset)
    this->action->setViewportRegion(this->lastvp);
  this->action->addPreCallback(SoFCSelectionRoot::getClassTypeId(), &preSeparator, this);
  this->action->addPostCallback(SoFCSelectionRoot::getClassTypeId(), &postSeparator, this);
  this->action->addPreCallback(SoFCLatePickGroup::getClassTypeId(), &preLatePickGroup, this);
  this->action->addPostCallback(SoSeparator::getClassTypeId(), &postSep, this);
  this->action->addPreCallback(SoFCSelection::getClassTypeId(), &preFCSel, this);
  this->action->addPostCallback(SoFCSelection::getClassTypeId(), &postFCSel, this);
  this->action->addPreCallback(SoAnnotation::getClassTypeId(), &preAnnotation, this);
  this->action->addPostCallback(SoAnnotation::getClassTypeId(), &postAnnotation, this);
  this->action->addPreCallback(SoFCPathAnnotation::getClassTypeId(), &prePathAnnotation, this);
  this->action->addPostCallback(SoFCPathAnnotation::getClassTypeId(), &postPathAnnotation, this);
  this->action->addPreCallback(SoSkipBoundingGroup::getClassTypeId(), &preSkipBounds, this);
  this->action->addPostCallback(SoSkipBoundingGroup::getClassTypeId(), &postSkipBounds, this);
  this->action->addPreCallback(SoShape::getClassTypeId(), &preShape, this);
  this->action->addPostCallback(SoShape::getClassTypeId(), &postShape, this);
  this->action->addPostCallback(SoTexture::getClassTypeId(), &postTexture, this);
  this->action->addPostCallback(SoVRMLTexture::getClassTypeId(), &postTexture, this);
  // SoBumpMap::callback() is a no-op (its element only exists during GL
  // rendering), so the node is captured directly for external backends.
  this->action->addPostCallback(SoBumpMap::getClassTypeId(), &postBumpMap, this);
  // SoFCRenderMaterial has no Coin element; captured directly for the
  // external backends (per-object PBR parameters).
  this->action->addPostCallback(Gui::SoFCRenderMaterial::getClassTypeId(), &postRenderMaterial, this);
  // SoFCRenderTexture: emissive/occlusion material maps, external
  // backends only (a plain SoNode, so Coin's texture handling and the
  // unit-0 texture capture above never see it).
  this->action->addPostCallback(Gui::SoFCRenderTexture::getClassTypeId(), &postRenderTexture, this);
  // User shader programs (docs/RenderDebug.md §6): captured directly for
  // the external backends; Coin's own GL path consumes these nodes in its
  // GL traversal independently (and skips BGFX_SC sources).
  this->action->addPostCallback(SoShaderProgram::getClassTypeId(), &postShaderProgram, this);
  this->action->addPostCallback(SoResetTransform::getClassTypeId(), &postResetTransform, this);
  this->action->addPostCallback(SoTextureMatrixTransform::getClassTypeId(), &postTextureTransform, this);
  this->action->addPostCallback(SoTexture2Transform::getClassTypeId(), &postTextureTransform, this);
  this->action->addPostCallback(SoTexture3Transform::getClassTypeId(), &postTextureTransform, this);
  this->action->addPostCallback(SoVRMLTextureTransform::getClassTypeId(), &postTextureTransform, this);
  this->action->addPostCallback(SoLightModel::getClassTypeId(), &postLightModel, this);
  this->action->addPostCallback(SoShadowStyle::getClassTypeId(), &postShadowStyle, this);
  this->action->addPostCallback(SoMaterial::getClassTypeId(), &postMaterial, this);
  this->action->addPostCallback(SoVRMLMaterial::getClassTypeId(), &postVRMLMaterial, this);
  this->action->addPostCallback(SoBaseColor::getClassTypeId(), &postColor, this);
  this->action->addPostCallback(SoVRMLColor::getClassTypeId(), &postVRMLColor, this);
  this->action->addPostCallback(SoDepthBuffer::getClassTypeId(), &postDepthBuffer, this);
  this->action->addPostCallback(SoLight::getClassTypeId(), &postLight, this);
  this->action->addPostCallback(SoVRMLLight::getClassTypeId(), &postVRMLLight, this);
  this->action->addPostCallback(SoClipPlane::getClassTypeId(), &postClipPlane, this);
  this->action->addPreCallback(SoAutoZoomTranslation::getClassTypeId(), &preAutoZoom, this);
  this->action->addPostCallback(SoAutoZoomTranslation::getClassTypeId(), &postAutoZoom, this);

  this->action->addTriangleCallback(SoShape::getClassTypeId(), &addTriangle, this);
  this->action->addLineSegmentCallback(SoShape::getClassTypeId(), &addLine, this);
  this->action->addPointCallback(SoShape::getClassTypeId(), &addPoint, this);
}

SoFCRenderCacheManagerP::~SoFCRenderCacheManagerP()
{
  delete this->action;
  delete this->renderer;
}

bool SoFCRenderCacheManager::SuppressImageGLRender = false;

const SbFCMap<int, CoinPtr<SoPath> > &
SoFCRenderCacheManager::getSelectionPaths() const
{
  return PRIVATE(this)->selpaths;
}

SoFCRenderCacheManager::SoFCRenderCacheManager()
  :pimpl(new SoFCRenderCacheManagerP)
{
}

SoFCRenderCacheManager::~SoFCRenderCacheManager()
{
  delete pimpl;
}

void
SoFCRenderCacheManager::setExternalRenderer(Render::Renderer *renderer,
                                            App::PropertyContainer *view)
{
  PRIVATE(this)->renderer->setExternalRenderer(renderer, view);
}

void
SoFCRenderCacheManager::setViewObject(App::PropertyContainer *view)
{
  PRIVATE(this)->viewobject = view;
  PRIVATE(this)->renderer->setViewObject(view);
}

void
SoFCRenderCacheManager::refreshExternalFeed()
{
  PRIVATE(this)->renderer->refreshExternalFeed();
}

void
SoFCRenderCacheManager::setExternalOverlay(Render::Renderer *renderer,
                                           int id,
                                           const Render::OverlayAnchor &anchor)
{
  PRIVATE(this)->renderer->setExternalOverlay(renderer, id, anchor);
}

void
SoFCRenderCacheManager::clear()
{
  PRIVATE(this)->stack.clear();
  PRIVATE(this)->selnodeid.clear();
  PRIVATE(this)->nodeset.clear();
  PRIVATE(this)->cachetable.clear();
  PRIVATE(this)->vcachetable.clear();
  PRIVATE(this)->selcaches.clear();
  PRIVATE(this)->invalidsels.clear();
  PRIVATE(this)->shadercaches.clear();
  PRIVATE(this)->selpaths.clear();
  PRIVATE(this)->renderer->clear();
  // The scene the delta was a delta against is gone; the next publish
  // reports the whole scene as added rather than diffing against a cache
  // no traversal will ever hand back.
  PRIVATE(this)->publishdelta.clear();
  PRIVATE(this)->latepicktable.clear();
  PRIVATE(this)->latepickpaths.truncate(0);
}

bool
SoFCRenderCacheManager::isOnTop(const std::string & key, bool altonly) const
{
  auto it = PRIVATE(this)->selcaches.find(key);
  if (it == PRIVATE(this)->selcaches.end())
    return false;
  for (auto & v : it->second) {
    if (v.second.ontop) {
      if (!altonly)
        return true;
      auto iter = v.second.elements.find(std::string());
      if (iter != v.second.elements.end()
          && (iter->second.id & SoFCRenderer::SelIdAlt))
        return true;
    }
  }
  return false;
}

bool
SoFCRenderCacheManager::hasOnTopObject() const
{
  return !PRIVATE(this)->selcaches.empty();
}

bool
SoFCRenderCacheManager::takeInvalidSelections(std::vector<std::string> & keys)
{
  if (PRIVATE(this)->invalidsels.empty())
    return false;
  for (auto & key : PRIVATE(this)->invalidsels)
    keys.push_back(key);
  PRIVATE(this)->invalidsels.clear();
  return true;
}

SoPath *
SoFCRenderCacheManager::getHighlightPath() const
{
  return PRIVATE(this)->highlightpath;
}

void
SoFCRenderCacheManager::setHighlight(SoPath * path,
                                     const SoDetail * detail,
                                     uint32_t color,
                                     bool ontop,
                                     bool wholeontop)
{
  if (!path || path->getLength() == 0)
    return;
  SoState * state = PRIVATE(this)->action->getState();

  PRIVATE(this)->highlightpath = path;

  RenderCachePtr cache;
  if (PRIVATE(this)->nosectionontop != PRIVATE(this)->sectionNoOnTop()) {
    PRIVATE(this)->nosectionontop = PRIVATE(this)->sectionNoOnTop();
    PRIVATE(this)->pathcachetable.clear();
  }
  auto it = PRIVATE(this)->pathcachetable.find(path);
  if (it != PRIVATE(this)->pathcachetable.end())
    cache = it->second.cache;
  else {
    cache = new SoFCRenderCache(state, path->getHead());
    cache->open(state);
    PRIVATE(this)->stack.resize(1, cache);
    if (ontop) {
      SoFCSwitch::setOverrideSwitch(state, true);
      SoFCSwitch::pushSwitchPath(path);
    }
    PRIVATE(this)->override_selectstyle = false;
    {
      CaptureFlagGuard capguard;
      PRIVATE(this)->action->apply(path);
    }
    if (ontop) {
      SoFCSwitch::popSwitchPath();
      SoFCSwitch::setOverrideSwitch(state, false);
    }
    cache->close(state);
    PRIVATE(this)->stack.clear();
    PRIVATE(this)->selnodeid.clear();
    if (!cache->isEmpty()) {
      // Must use SoTempPath as key to avoid path changes, because we are using
      // the path as key which is supposed to be immutable.
      PathPtr tmppath = new SoTempPath(path->getLength());
      tmppath->append(path);
      auto &sensor = PRIVATE(this)->pathcachetable[tmppath];
      sensor.path = tmppath;
      sensor.master = PRIVATE(this);
      sensor.attach(path->copy());
      sensor.cache = cache;
    }
  }

  int order = ontop ? 1 : 0;
  PRIVATE(this)->highlightcache = cache;
  PRIVATE(this)->renderer->setHighlight(
        cache->buildHighlightCache(
          PRIVATE(this)->sharedcache, order, detail, color,
          SoFCRenderCache::PreselectHighlight
          | SoFCRenderCache::CheckIndices
          | (wholeontop ? SoFCRenderCache::WholeOnTop : 0)),
        wholeontop);
}

void
SoFCRenderCacheManager::clearHighlight()
{
  PRIVATE(this)->highlightpath.reset();
  PRIVATE(this)->highlightcache.reset();
  PRIVATE(this)->renderer->clearHighlight();
}

void
SoFCRenderCacheManagerP::updateSelection(void * userdata, SoSensor * _sensor)
{
  SoFCRenderCacheManagerP * self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  SelectionSensor * sensor = static_cast<SelectionSensor*>(_sensor);
  SoPath *path = sensor->attachedPath;
  if (!path)
    return;
  sensor->refresh(self->renderer);
  if (!path->getLength())
    return;
  if (sensor->broken) {
    // refresh() has already dropped this entry's caches from the renderer, so
    // nothing stale is drawn. Name it so refreshGroupOnTop() can resolve it
    // again from the object path once the scene graph settles.
    if (sensor->ontop && sensor->key.size()) {
      FC_LOG("on top path truncated: " << sensor->key);
      self->invalidsels.insert(sensor->key);
    }
    return;
  }

  SoState * state = self->action->getState();
  RenderCachePtr cache = new SoFCRenderCache(state, path->getHead());
  cache->open(state);
  self->stack.resize(1, cache);
  if (sensor->ontop) {
    SoFCSwitch::setOverrideSwitch(state, true);
    SoFCSwitch::pushSwitchPath(path);
  }

  self->override_selectstyle = false;
  if (sensor->ontop && (int)self->selpaths.size() >= ViewParams::getMaxOnTopSelections()) {
    for (auto & v : sensor->elements) {
      auto & elentry = v.second;
      if (elentry.id & SoFCRenderer::SelIdFull) {
        self->override_selectstyle = true;
        break;
      }
    }
  }

  {
    CaptureFlagGuard capguard;
    self->action->apply(path);
  }
  if (sensor->ontop) {
    SoFCSwitch::popSwitchPath();
    SoFCSwitch::setOverrideSwitch(state, false);
  }
  cache->close(state);
  self->stack.clear();
  self->selnodeid.clear();

  if (cache->isEmpty())
    return;

  sensor->cache = cache;
  for (auto & v : sensor->elements) {
    auto & elentry = v.second;
    int flags = 0;
    if (ViewParams::highlightIndicesOnFullSelect())
      flags |= SoFCRenderCache::CheckIndices;
    if (elentry.id > 0)
      flags |= SoFCRenderCache::WholeOnTop;
    if (elentry.id & SoFCRenderer::SelIdAlt)
      flags |= SoFCRenderCache::AltGroup;
    if (elentry.usershader)
      // Shader overrides replace the base draws in place: original
      // geometry and materials (normals intact), shader stamped on. A
      // face detail (Scope=Element) narrows the map to that face,
      // rendered over the untouched base draw instead of replacing it.
      elentry.vcachemap = applyUserShader(
          sensor->cache->buildWholeCacheMap(elentry.id, elentry.detail.get()),
          elentry.usershader);
    else
      elentry.vcachemap = sensor->cache->buildHighlightCache(
          self->sharedcache, elentry.id, elentry.detail.get(), elentry.color, flags);

    self->renderer->addSelection(elentry.id, elentry.vcachemap);
    if (elentry.vcachemap.size() == 1
        && elentry.vcachemap.begin()->second.size() == 1
        && !elentry.vcachemap.begin()->second[0].key)
    {
      // here means the selection shows a bounding box, so don't put it on
      // selpaths for pick list
      self->selpaths.erase(elentry.id);
    }
  }
}

void
SoFCRenderCacheManager::addSelection(const std::string & key,
                                     const std::string & element,
                                     SoPath * nodepath,
                                     SoPath * detailpath,
                                     const SoDetail * detail,
                                     uint32_t color,
                                     bool ontop,
                                     bool alt,
                                     bool implicit)
{
  if (!detailpath || !detailpath->getLength())
    return;

  bool update = false;
  PathPtr selpath;
  auto & paths = PRIVATE(this)->selcaches[key];
  SelectionSensor * sensor;
  auto it = paths.find(detailpath);
  if (it != paths.end()) {
    sensor = &it->second;
    selpath = it->first;
  }
  else {
    selpath = detailpath->copy();
    sensor = &paths[selpath];
    sensor->attachPath(selpath);
    sensor->setFunction(&SoFCRenderCacheManagerP::updateSelection);
    sensor->setData(PRIVATE(this));
    update = true;
  }
  sensor->key = key;
  PRIVATE(this)->invalidsels.erase(key);

  if (sensor->ontop)
    ontop = true;

  int id = 0;
  if (alt) {
    id |= SoFCRenderer::SelIdAlt;
    ontop = true;
  }
  id += ++PRIVATE(this)->selid;
  if (!ontop)
    id = id | (-1 - SoFCRenderer::SelIdMask);

  if (id > 0 && !sensor->ontop) {
    sensor->ontop = true;
    update = true;
    if (element.size()) {
      if (nodepath != detailpath) {
        addSelection(key, "", nodepath, nodepath, nullptr, color & 0xff, true, false, true);
      } else {
        // When any element is selected on top, we shall bring the whole object on
        // top. So make sure a nil element entry exist.
        auto & elentry = sensor->elements[std::string()];
        if (!elentry.id) {
          elentry.id = id++;
          elentry.id |= SoFCRenderer::SelIdImplicit;
          ++PRIVATE(this)->selid;
        }
      }
    }
    for (auto & v : sensor->elements) {
      auto & elentry = v.second;
      if (elentry.id < 0) {
        PRIVATE(this)->renderer->removeSelection(elentry.id);
        elentry.id = id++;
        ++PRIVATE(this)->selid;
        if (!element.empty())
          elentry.id |= SoFCRenderer::SelIdPartial;
        else if (implicit)
          elentry.id |= SoFCRenderer::SelIdImplicit;
        else
          elentry.id |= SoFCRenderer::SelIdFull;
      }

      elentry.color &= 0xffffff00;
      elentry.color |= color & 0xff;

      if (v.first.empty())
        PRIVATE(this)->selpaths[elentry.id] = selpath;

      if (sensor->cache) {
        int flags = 0;
        if (ViewParams::highlightIndicesOnFullSelect())
          flags |= SoFCRenderCache::CheckIndices;
        if (elentry.id > 0)
          flags |= SoFCRenderCache::WholeOnTop;
        if (elentry.id & SoFCRenderer::SelIdAlt)
          flags |= SoFCRenderCache::AltGroup;
        elentry.vcachemap = sensor->cache->buildHighlightCache(
            PRIVATE(this)->sharedcache, elentry.id, elentry.detail.get(), elentry.color, flags);
        PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
      }
    }
  }

  auto & elentry = sensor->elements[element];

  if (elentry.id & SoFCRenderer::SelIdAlt)
    id |= SoFCRenderer::SelIdAlt;
  if (id > 0 && (color & 0xffffff00)) {
    if (element.empty())
      id |= SoFCRenderer::SelIdFull;
    else
      id |= SoFCRenderer::SelIdPartial;
  }
  else if (id > 0 && implicit)
    id |= SoFCRenderer::SelIdImplicit;
  
  if (elentry.id && elentry.id != id) {
    if (elentry.id > 0 && element.empty())
      PRIVATE(this)->selpaths.erase(elentry.id);
    PRIVATE(this)->renderer->removeSelection(elentry.id);
  }
  if (id > 0 && element.empty())
    PRIVATE(this)->selpaths[id] = selpath;
  elentry.id = id;
  elentry.color = color;
  elentry.detail.reset(detail ? detail->copy() : nullptr);
  if (sensor->cache) {
    int flags = 0;
    if (ViewParams::highlightIndicesOnFullSelect())
      flags |= SoFCRenderCache::CheckIndices;
    if (elentry.id > 0)
      flags |= SoFCRenderCache::WholeOnTop;
    if (elentry.id & SoFCRenderer::SelIdAlt)
      flags |= SoFCRenderCache::AltGroup;
    elentry.vcachemap = sensor->cache->buildHighlightCache(
        PRIVATE(this)->sharedcache, elentry.id, elentry.detail.get(), elentry.color, flags);
    PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
  }

  if (update)
    SoFCRenderCacheManagerP::updateSelection(PRIVATE(this), sensor);
}

void
SoFCRenderCacheManager::addSelection(const std::string & key,
                                     const std::string & element,
                                     SoNode * node,
                                     uint32_t color,
                                     bool ontop,
                                     bool alt)
{
  static FC_COIN_THREAD_LOCAL SoTempPath path(1);
  path.ref();
  path.truncate(0);
  path.append(node);
  addSelection(key, element, &path, &path, nullptr, color, ontop, alt);
  path.truncate(0);
  path.unrefNoDelete();
}

void
SoFCRenderCacheManager::removeSelection(const std::string & key,
                                        const std::string & element,
                                        bool alt)
{
  auto it = PRIVATE(this)->selcaches.find(key);
  if (it == PRIVATE(this)->selcaches.end())
    return;

  auto & paths = it->second;

  for (auto itpath = paths.begin(); itpath!=paths.end(); ) {
    auto & sensor = itpath->second;
    auto iter = sensor.elements.find(element);
    if (iter == sensor.elements.end()) {
      ++itpath;
      continue;
    }
    auto & elentry = iter->second;
    if (alt) {
      if (!(elentry.id & SoFCRenderer::SelIdAlt)) {
        ++itpath;
        continue;
      }
      if (element.empty())
        PRIVATE(this)->selpaths.erase(elentry.id);
      PRIVATE(this)->renderer->removeSelection(elentry.id);
      if (!(elentry.id & SoFCRenderer::SelIdSelected)) {
        sensor.elements.erase(iter);
        if (sensor.elements.empty())
          itpath = paths.erase(itpath);
        else
          ++itpath;
        continue;
      }
      elentry.id ^= SoFCRenderer::SelIdAlt;
      PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
      if (element.empty())
        PRIVATE(this)->selpaths[elentry.id] = itpath->first;
      ++itpath;
      continue;
    }
    if (elentry.id & SoFCRenderer::SelIdAlt) {
      if (elentry.id & SoFCRenderer::SelIdSelected) {
        PRIVATE(this)->selpaths.erase(elentry.id);
        PRIVATE(this)->renderer->removeSelection(elentry.id);
        elentry.id &= ~(SoFCRenderer::SelIdSelected);
        PRIVATE(this)->selpaths[elentry.id] = itpath->first;
        elentry.color &= 0xff;
        elentry.vcachemap = sensor.cache->buildHighlightCache(
            PRIVATE(this)->sharedcache, elentry.id, elentry.detail.get(), elentry.color,
            elentry.id>0 ? SoFCRenderCache::WholeOnTop : 0);
        PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
      }
      ++itpath;
      continue;
    }
    if (element.empty())
      PRIVATE(this)->selpaths.erase(elentry.id);
    PRIVATE(this)->renderer->removeSelection(elentry.id);
    sensor.elements.erase(iter);
    if (sensor.ontop && element.size()) {
      iter = sensor.elements.find(std::string());
      if (iter != sensor.elements.end()) {
        auto & elentry = iter->second;
        if (!(elentry.id & SoFCRenderer::SelIdAlt)) {
          PRIVATE(this)->selpaths.erase(elentry.id);
          PRIVATE(this)->renderer->removeSelection(elentry.id);
          sensor.elements.erase(iter);
        }
      }
    }
    if (!sensor.elements.empty()) {
      ++itpath;
      continue;
    }

    itpath = paths.erase(itpath);
    if (paths.size() != 1)
      continue;

    // Check if there is an implicit full selection, added because selection on
    // top is active.
    const auto &other = paths.begin()->second;
    if (other.elements.size() != 1
        || !other.elements.begin()->first.empty())
      continue;

    const auto &otherentry = other.elements.begin()->second;
    if ((otherentry.id & SoFCRenderer::SelIdImplicit)
        && !(otherentry.id & SoFCRenderer::SelIdAlt))
    {
      PRIVATE(this)->selpaths.erase(otherentry.id);
      PRIVATE(this)->renderer->removeSelection(otherentry.id);
      paths.clear();
      break;
    }
  }

  if (paths.empty())
    PRIVATE(this)->selcaches.erase(it);
}

void
SoFCRenderCacheManager::addShaderOverride(
    const std::string & key,
    SoPath * nodepath,
    const std::shared_ptr<const Render::UserShader> & shader,
    const SoDetail * detail)
{
  if (!nodepath || !nodepath->getLength() || !shader)
    return;

  auto & paths = PRIVATE(this)->shadercaches[key];
  SelectionSensor * sensor;
  PathPtr selpath;
  auto it = paths.find(nodepath);
  if (it != paths.end()) {
    sensor = &it->second;
    selpath = it->first;
  }
  else {
    // One path per key: a binding addresses one instance path. A key
    // re-added with a different path replaces the old binding.
    if (!paths.empty()) {
      for (auto & v : paths) {
        for (auto & elentry : v.second.elements)
          PRIVATE(this)->renderer->removeSelection(elentry.second.id);
      }
      paths.clear();
    }
    selpath = nodepath->copy();
    sensor = &paths[selpath];
    sensor->attachPath(selpath);
    sensor->setFunction(&SoFCRenderCacheManagerP::updateSelection);
    sensor->setData(PRIVATE(this));
  }

  auto & elentry = sensor->elements[std::string()];
  elentry.usershader = shader;
  elentry.color = 0;
  elentry.detail.reset(detail ? detail->copy() : nullptr);
  if (!elentry.id) {
    // Non-on-top encoding (the addSelection convention): the entry
    // renders in the normal scene passes and suppresses the base draw
    // through the whole-object key, replacing instead of overlaying.
    int id = ++PRIVATE(this)->selid;
    elentry.id = id | (-1 - SoFCRenderer::SelIdMask);
  }

  // (Re)build through the shared sensor callback: capture the cache at
  // the path, build the whole-object vcachemap, apply the shader and
  // hand it to the renderer.
  SoFCRenderCacheManagerP::updateSelection(PRIVATE(this), sensor);
}

void
SoFCRenderCacheManager::setAppearanceShaders(
    std::vector<Render::UserShader> && shaders)
{
  PRIVATE(this)->renderer->setAppearanceShaders(std::move(shaders));
}

void
SoFCRenderCacheManager::removeShaderOverride(const std::string & key)
{
  auto it = PRIVATE(this)->shadercaches.find(key);
  if (it == PRIVATE(this)->shadercaches.end())
    return;
  for (auto & v : it->second) {
    for (auto & elentry : v.second.elements)
      PRIVATE(this)->renderer->removeSelection(elentry.second.id);
  }
  PRIVATE(this)->shadercaches.erase(it);
}

int
SoFCRenderCacheManager::clearSelection(bool alt)
{
  int res = 0;
  auto itsel = PRIVATE(this)->selcaches.begin();
  while (itsel!=PRIVATE(this)->selcaches.end()) {
    auto & paths = itsel->second;
    for (auto itpath=paths.begin(); itpath!=paths.end(); ) {
      auto & sensor = itpath->second;
      for (auto iter=sensor.elements.begin(); iter!=sensor.elements.end(); ) {
        auto & elentry = iter->second;
        if (alt) {
          if (!(elentry.id & SoFCRenderer::SelIdAlt)) {
            ++iter;
            continue;
          }
          if (iter->first.empty())
            PRIVATE(this)->selpaths.erase(elentry.id);
          ++res;
          PRIVATE(this)->renderer->removeSelection(elentry.id);
          if (elentry.color & ~0xff) {
            elentry.id = ++PRIVATE(this)->selid;
            if (iter->first.empty())
              PRIVATE(this)->selpaths[elentry.id] = itpath->first;
            PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
            ++iter;
            continue;
          }
          iter = sensor.elements.erase(iter);
          continue;
        }
        else if (elentry.id & SoFCRenderer::SelIdAlt) {
          if (elentry.id & SoFCRenderer::SelIdSelected) {
            ++res;
            PRIVATE(this)->renderer->removeSelection(elentry.id);
            PRIVATE(this)->selpaths.erase(elentry.id);
            elentry.id &= ~(SoFCRenderer::SelIdSelected);
            PRIVATE(this)->selpaths[elentry.id] = itpath->first;
            elentry.color &= 0xff;
            if (sensor.cache) {
              elentry.vcachemap = sensor.cache->buildHighlightCache(
                  PRIVATE(this)->sharedcache, elentry.id, elentry.detail.get(), elentry.color,
                  elentry.id>0 ? SoFCRenderCache::WholeOnTop : 0);
              PRIVATE(this)->renderer->addSelection(elentry.id, elentry.vcachemap);
            }
          }
          ++iter;
          continue;
        }
        else {
          if (iter->first.empty())
            PRIVATE(this)->selpaths.erase(elentry.id);
          PRIVATE(this)->renderer->removeSelection(elentry.id);
          iter = sensor.elements.erase(iter);
          ++res;
        }
      }
      if (sensor.elements.empty())
        itpath = paths.erase(itpath);
      else
        ++itpath;
    }
    if (paths.empty())
      itsel = PRIVATE(this)->selcaches.erase(itsel);
    else
      ++itsel;
  }
  if (PRIVATE(this)->selcaches.empty())
    PRIVATE(this)->selid = 0;

  return res;
}

void
SoFCRenderCacheManager::render(SoGLRenderAction * action)
{
  SoState * state = action->getState();
  SoCacheElement::invalidate(state);
  SoGLCacheContextElement::shouldAutoCache(state,
                                           SoGLCacheContextElement::DONT_AUTO_CACHE);

  // The capture action initializes its own state, whose viewport element
  // starts at the 100px default; screen-space captures (SoTextImage,
  // SoFCImageQuad pixel offsets) read the real viewport, so carry it
  // over. Sticky on the action, so the sensor-triggered selection
  // rebuilds inherit it too (and initAction() re-applies it).
  PRIVATE(this)->lastvp = SoViewportRegionElement::get(state);
  PRIVATE(this)->lastvpset = true;
  PRIVATE(this)->action->setViewportRegion(PRIVATE(this)->lastvp);

  const SoPath * path = action->getCurPath();
  if (!PRIVATE(this)->sceneid || PRIVATE(this)->sceneid != path->getTail()->getNodeId()) {
    SoState * state = action->getState();
    const SoShapeStyleElement * shapestyle = SoShapeStyleElement::get(state);
    unsigned int shapestyleflags = shapestyle->getFlags();
    if (!(shapestyleflags & SoShapeStyleElement::SHADOWMAP))
      PRIVATE(this)->sceneid = path->getTail()->getNodeId();

    // Everything below is the whole-scene republish this node-id test
    // triggers on any change beneath the root (docs/IncrementalPublish.md);
    // the traversal is what this stage measures, the stages nested inside
    // renderer->setScene() account for themselves.
    Gui::RenderTiming::Scope timing(Gui::RenderTiming::Traverse);

    RenderCachePtr cache = new SoFCRenderCache(state, path->getTail());
    cache->open(state);
    // Note that we are capturing state of the SoGLRenderAction here. However,
    // we will change to use SoCallBackAction to build the rest of the render
    // cache. That's why we need to reset the internal kept action state stack
    // depth as shown below to avoid popping the initially captured state here.
    cache->resetActionStateStackDepth();
    PRIVATE(this)->stack.resize(1, cache);
    PRIVATE(this)->initAction();
    PRIVATE(this)->override_selectstyle = false;
    PRIVATE(this)->usershaders.shaders.clear();
    PRIVATE(this)->publishdelta.begin();
    // Arm the capture budget for this publish and this publish only
    // (preShape defers nothing outside a publish -- see the member note).
    PRIVATE(this)->capturebudgeting = true;
    PRIVATE(this)->capturespentms = 0.0;
    PRIVATE(this)->capturecount = 0;
    PRIVATE(this)->deferredcount = 0;
    PRIVATE(this)->adoptedcount = 0;
    PRIVATE(this)->adoptrejects.clear();
    PRIVATE(this)->adoptstale.clear();
    PRIVATE(this)->verifiedcount = 0;
    SoFCVertexCache::resetPrebuiltStats();
    {
      CaptureFlagGuard capguard;
      PRIVATE(this)->action->apply(path->getTail());
    }
    PRIVATE(this)->capturebudgeting = false;
    if (PRIVATE(this)->deferredcount > 0) {
      // Shapes were left a publish stale: forget the scene id so the
      // next render republishes (the caller schedules that redraw), and
      // each pass captures at least one more shape until none defer.
      PRIVATE(this)->sceneid = 0;
    }
    {
      // The adoption side of the line reports its own failure: a bare
      // "0 adopted" cannot distinguish "nothing was ever registered"
      // from "registered but voided" from "refused by the captured
      // state", and those have three different fixes. Printed whenever
      // a shape offered anything, so the count cannot go quiet.
      const auto & pstats = SoFCVertexCache::prebuiltStats();
      if (Gui::RenderParams::getLevelDebug()
          && (PRIVATE(this)->deferredcount > 0
              || PRIVATE(this)->adoptedcount > 0
              || pstats.requested > 0)) {
        std::string why;
        auto add = [&why](int count, const char * what) {
          if (!count)
            return;
          char buf[128];
          std::snprintf(buf, sizeof(buf), ", %d %s", count, what);
          why += buf;
        };
        add(pstats.missing, "no entry");
        add(PRIVATE(this)->verifiedcount, "verified");
        for (const auto & entry : PRIVATE(this)->adoptstale) {
          char what[96];
          std::snprintf(what, sizeof(what), "stale %s", entry.first);
          add(entry.second, what);
        }
        for (const auto & entry : PRIVATE(this)->adoptrejects)
          add(entry.second, entry.first);
        Base::Console().Message(
            "capture budget: %d captured in %.0fms, %d deferred, "
            "%d adopted of %d offered%s\n",
            PRIVATE(this)->capturecount, PRIVATE(this)->capturespentms,
            PRIVATE(this)->deferredcount, PRIVATE(this)->adoptedcount,
            pstats.requested, why.c_str());
      }
    }
    cache->close(state);

    {
      // What this publish actually changed. Recorded whether or not
      // anything reads it, so the diff is there to be measured and
      // asserted against before the publish below starts depending on it
      // (docs/IncrementalPublish.md §8 phase 2).
      Gui::RenderTiming::Scope deltatiming(Gui::RenderTiming::Delta);
      PRIVATE(this)->publishdelta.update(cache);
    }

    PRIVATE(this)->renderer->setScene(cache);
    PRIVATE(this)->renderer->setUserShaders(
        std::move(PRIVATE(this)->usershaders));
    PRIVATE(this)->usershaders = {};
    PRIVATE(this)->stack.clear();
    PRIVATE(this)->selnodeid.clear();
  }

  PRIVATE(this)->renderer->render(action);
}

void
SoFCRenderCacheManager::capture(SoGLRenderAction * action, SoNode * root)
{
  // Same cache build as render(), but over an explicit \a root instead of
  // the action's current path tail, and without any drawing: used for
  // overlay roots (foreground superimposition, corner axis cross) that
  // are captured outside their own traversal and mirrored to the backend
  // through the overlay feed (setExternalOverlay()).
  // Real viewport for screen-space captures; see render().
  PRIVATE(this)->lastvp = SoViewportRegionElement::get(action->getState());
  PRIVATE(this)->lastvpset = true;
  PRIVATE(this)->action->setViewportRegion(PRIVATE(this)->lastvp);

  if (PRIVATE(this)->sceneid == root->getNodeId())
    return;
  PRIVATE(this)->sceneid = root->getNodeId();

  SoState * state = action->getState();
  RenderCachePtr cache = new SoFCRenderCache(state, root);
  cache->open(state);
  cache->resetActionStateStackDepth();
  PRIVATE(this)->stack.resize(1, cache);
  PRIVATE(this)->initAction();
  PRIVATE(this)->override_selectstyle = false;
  PRIVATE(this)->usershaders.shaders.clear();
  // An overlay capture publishes no delta, so its separators are not this
  // scene's; reset the counters rather than let them leak into whatever
  // publish comes next.
  PRIVATE(this)->publishdelta.begin();
  {
    CaptureFlagGuard capguard;
    PRIVATE(this)->action->apply(root);
  }
  cache->close(state);
  PRIVATE(this)->renderer->setScene(cache);
  // Not routed anywhere in overlay mode (render() is a no-op there), but
  // kept symmetric with render() so the capture state never goes stale.
  PRIVATE(this)->renderer->setUserShaders(
      std::move(PRIVATE(this)->usershaders));
  PRIVATE(this)->usershaders = {};
  PRIVATE(this)->stack.clear();
  PRIVATE(this)->selnodeid.clear();
}

void
SoFCRenderCacheManager::traverse(SoNode * root, const SbViewportRegion & viewport)
{
  // The seed. SoAction::getState() builds the state on first call with
  // the action's default elements, so this is a complete, GL-free state
  // before any traversal has run — no context, no drawable, nothing to
  // make current.
  SoCallbackAction seedaction(viewport);
  SoState * state = seedaction.getState();

  // Real viewport for screen-space captures; see render().
  PRIVATE(this)->lastvp = viewport;
  PRIVATE(this)->lastvpset = true;
  PRIVATE(this)->action->setViewportRegion(viewport);

  // Before the change check, not after: the configs describe how the
  // scene looks (AO, water, hidden line, ...) and an edit to one of them
  // moves no node id at all, so gating them on the graph having changed
  // would drop exactly the republish a remote property edit asks for.
  PRIVATE(this)->renderer->pushExternalConfigs(state);

  if (PRIVATE(this)->sceneid == root->getNodeId())
    return;
  PRIVATE(this)->sceneid = root->getNodeId();

  Gui::RenderTiming::Scope timing(Gui::RenderTiming::Traverse);

  RenderCachePtr cache = new SoFCRenderCache(state, root);
  cache->open(state);
  cache->resetActionStateStackDepth();
  PRIVATE(this)->stack.resize(1, cache);
  PRIVATE(this)->initAction();
  PRIVATE(this)->override_selectstyle = false;
  PRIVATE(this)->usershaders.shaders.clear();
  {
    CaptureFlagGuard capguard;
    PRIVATE(this)->action->apply(root);
  }
  cache->close(state);
  PRIVATE(this)->renderer->setScene(cache);
  PRIVATE(this)->renderer->setUserShaders(
      std::move(PRIVATE(this)->usershaders));
  PRIVATE(this)->usershaders = {};
  PRIVATE(this)->stack.clear();
  PRIVATE(this)->selnodeid.clear();
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preSeparator(void *userdata,
                                      SoCallbackAction *action,
                                      const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (!self->nodeset.insert(node).second) {
      self->prunenode = node;
      SoFCSelectionRoot::reportCyclicScene(action, const_cast<SoNode*>(node));
      return SoCallbackAction::PRUNE;
  }

  SoState * state = action->getState();
  SoFCRenderCache *currentcache = self->stack.empty() ? nullptr : self->stack.back();
  RenderCachePtr prevcache;
  SbFCVector<RenderCachePtr> *sensorcaches = nullptr;
  if (action->getCurPathCode() == SoAction::BELOW_PATH
      || action->getCurPathCode() == SoAction::NO_PATH) {
    CacheSensor &sensor = self->cachetable[node];
    sensor.attach(self, node);
    sensorcaches = &sensor.caches;
    for (auto it=sensorcaches->begin(); it!=sensorcaches->end();) {
      prevcache = *it;
      // An incomplete cache (one holding a capture-budget-deferred
      // child) reads like a mismatch: reusing it would prune the very
      // path the follow-up publish exists to walk again.
      if (prevcache->getNodeId() != node->getNodeId()
          || prevcache->isIncomplete()) {
        it = sensorcaches->erase(it);
        continue;
      }
      if (prevcache->isValid(state)) {
        if (currentcache)
          currentcache->addChildCache(state, prevcache);
        self->stack.push_back(prevcache);
        self->publishdelta.countSeparator(true);
        return SoCallbackAction::PRUNE;
      }
      ++it;
    }
  }

  state->push();

  int selectstyle = Material::Full;
  auto selroot = static_cast<const SoFCSelectionRoot*>(node);

  switch(selroot->selectionStyle.getValue()) {
  case SoFCSelectionRoot::Box:
    selectstyle = Material::Box;
    break;
  case SoFCSelectionRoot::Unpickable:
    if (action->getCurPathCode() != SoAction::IN_PATH)
      selectstyle = Material::Unpickable;
    break;
  default:
    if (self->override_selectstyle)
      selectstyle = Material::BoxFull;
    break;
  }

  RenderCachePtr cache(new SoFCRenderCache(state, const_cast<SoNode*>(node), prevcache));
  self->publishdelta.countSeparator(false);

  if (sensorcaches)
    sensorcaches->push_back(cache);
  if (currentcache)
    currentcache->beginChildCaching(state, cache);
  self->stack.push_back(cache);
  cache->open(state, selectstyle, false);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postSeparator(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);

  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  if (self->prunenode == node) {
    self->prunenode = nullptr;
    return SoCallbackAction::PRUNE;
  }

  self->nodeset.erase(node);
  SoState * state = action->getState();
  RenderCachePtr cache(self->stack.back());

  // Taken unconditionally: the link exists to be consumed by the publish
  // that built the cache, and a cache that keeps it holds a whole
  // previous generation alive for as long as it lives.
  RenderCachePtr prev = cache->takePreviousCache();

  self->stack.pop_back();
  if (SoCacheElement::getCurrentCache(state) == cache) {
    cache->close(state);
    state->pop();
    if (self->stack.size())
      self->stack.back()->endChildCaching(state, cache);
    // The cache is complete, so it can be told apart from the one it
    // replaced. This is where the change set of a nested scene lives: the
    // scene root usually holds a single container, and everything that
    // moved is a child of some cache below it.
    if (prev) {
      {
        Gui::RenderTiming::Scope timing(Gui::RenderTiming::Delta);
        self->publishdelta.updateCache(cache, prev);
      }
      // The flatten wants the map this cache's predecessor built, and the
      // predecessor is about to go out of scope. Handed over rather than
      // held by default: it lasts until the flatten takes it, not the life
      // of the cache (docs/IncrementalPublish.md §5). The match goes with
      // it: the diff just above answered which child of the predecessor
      // each child of this cache is, which is the same question the
      // flatten would otherwise ask again about the same pair.
      if (ViewParams::getRenderCacheIncremental() > 0)
        cache->setSpliceSource(prev, self->publishdelta.lastMatch());
    }
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postSep(void *userdata,
                                 SoCallbackAction *action,
                                 const SoNode * node)
{
  (void)node;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);

  if (!self->stack.empty())
    self->stack.back()->checkState(action->getState());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preFCSel(void *userdata,
                                 SoCallbackAction *action,
                                 const SoNode * node)
{
  (void)action;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  self->selnodeid.push_back(node->getNodeId());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postFCSel(void *userdata,
                                   SoCallbackAction *action,
                                   const SoNode * node)
{
  (void)action;
  (void)node;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  self->selnodeid.pop_back();
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postPathAnnotation(void *userdata,
                                            SoCallbackAction *action,
                                            const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  auto annotation = static_cast<const SoFCPathAnnotation*>(node);
  if (annotation->priority.getValue())
    self->stack.back()->decreaseRenderingOrder(action->getState(), annotation->priority.getValue());
  else if (annotation->getPath() && --self->annotation == 0)
    self->stack.back()->decreaseRenderingOrder(action->getState());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::prePathAnnotation(void *userdata,
                                           SoCallbackAction *action,
                                           const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  auto annotation = static_cast<const SoFCPathAnnotation*>(node);
  if (annotation->priority.getValue())
    self->stack.back()->increaseRenderingOrder(action->getState(), annotation->priority.getValue());
  else if (annotation->getPath() && ++self->annotation == 1)
    self->stack.back()->increaseRenderingOrder(action->getState());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preLatePickGroup(void *userdata,
                                          SoCallbackAction *action,
                                          const SoNode * node)
{
  (void)node;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
    return SoCallbackAction::CONTINUE;

  const SoPath *path = action->getCurPath();
  auto it = self->latepicktable.find(const_cast<SoPath*>(path));
  if (it != self->latepicktable.end())
    return SoCallbackAction::CONTINUE;

  // Must use SoTempPath as key to avoid path changes, because we are using
  // the path as key which is supposed to be immutable.
  PathPtr tmppath = new SoTempPath(path->getLength());
  tmppath->append(path);
  LatePickPathSensor &sensor = self->latepicktable[tmppath];
  sensor.tmpPath = tmppath;
  sensor.master = self;
  sensor.attachedPath = path->copy();
  sensor.attach(sensor.attachedPath);
  self->latepickpaths.truncate(0);

  return SoCallbackAction::CONTINUE;
}

void SoFCRenderCacheManager::doLatePick(SoRayPickAction *action) const
{
  PRIVATE(this)->doLatePick(action);
}

void SoFCRenderCacheManagerP::doLatePick(SoRayPickAction *action) const
{
  if (latepicktable.empty())
    return;
  if (latepickpaths.getLength() == 0) {
    for (auto &v : latepicktable)
      latepickpaths.append(v.first);
    if (obeysrules)
      latepickpaths.sort();
  }
  action->apply(latepickpaths, obeysrules);
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preAnnotation(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  (void)node;
  if (++self->annotation == 1)
    self->stack.back()->increaseRenderingOrder(action->getState());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postAnnotation(void *userdata,
                                        SoCallbackAction *action,
                                        const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  (void)node;
  if (self->annotation && --self->annotation == 0)
    self->stack.back()->decreaseRenderingOrder(action->getState());
  return SoCallbackAction::CONTINUE;
}

// A Gui::SoSkipBoundingGroup says its subtree is not scene geometry:
// Coin drops it from the scene bounding box whenever the requester asks
// for exclusion (SoSkipBoundingBoxElement). That is a traversal-time
// statement, and the render cache is what the external backends see
// instead of the traversal -- so record it on the material, the way the
// annotation depth is recorded, and let the backends' bounds honour it.
SoCallbackAction::Response
SoFCRenderCacheManagerP::preSkipBounds(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  (void)node;
  if (++self->skipbounds == 1)
    self->stack.back()->setSkipBounds(action->getState(), TRUE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postSkipBounds(void *userdata,
                                        SoCallbackAction *action,
                                        const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  (void)node;
  if (self->skipbounds && --self->skipbounds == 0)
    self->stack.back()->setSkipBounds(action->getState(), FALSE);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postTexture(void *userdata,
                                     SoCallbackAction *action,
                                     const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node);
  self->stack.back()->addTexture(action->getState(), node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postBumpMap(void *userdata,
                                     SoCallbackAction *action,
                                     const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node);
  self->stack.back()->addBumpMap(action->getState(), node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postRenderMaterial(void *userdata,
                                            SoCallbackAction *action,
                                            const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node);
  self->stack.back()->addRenderMaterial(action->getState(), node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postShaderProgram(void *userdata,
                                           SoCallbackAction *action,
                                           const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  assert(node);
  Render::UserShader shader;
  if (RendererBridge::translateShaderProgram(node, shader)) {
    // "material"-, "water"-, "volume"- and "particle"-stage programs
    // attach to the shapes captured after them in the enclosing cache
    // (the SoFCRenderMaterial placement rules); scene-level stages
    // ("post") ride the manager list. A "water"/"volume"-stage program
    // additionally marks the shapes as a water/fire body downstream
    // (RendererBridge translateMaterial); a "particle"-stage program
    // survives the parent merge-down (SoFCRenderCache mergeMaterial).
    if (shader.stage == "material" || shader.stage == "water"
        || shader.stage == "volume" || shader.stage == "particle") {
      if (!self->stack.empty())
        self->stack.back()->setUserShader(
            action->getState(),
            std::make_shared<Render::UserShader>(std::move(shader)));
    }
    else
      self->usershaders.shaders.push_back(std::move(shader));
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postRenderTexture(void *userdata,
                                           SoCallbackAction *action,
                                           const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node);
  self->stack.back()->addRenderTexture(action->getState(), node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postResetTransform(void *userdata,
                                           SoCallbackAction *action,
                                           const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoResetTransform::getClassTypeId()));
  const SoResetTransform *rs = static_cast<const SoResetTransform*>(node);
  if (!rs->whatToReset.isIgnored() &&
      (rs->whatToReset.getValue() & SoResetTransform::TRANSFORM)) {
    self->stack.back()->resetMatrix(action->getState());
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postTextureTransform(void *userdata,
                                              SoCallbackAction *action,
                                              const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  self->stack.back()->addTextureTransform(action->getState(), node);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postLightModel(void *userdata,
                                        SoCallbackAction *action,
                                        const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoLightModel::getClassTypeId()));
  const SoLightModel *lightmodel = static_cast<const SoLightModel*>(node);
  self->stack.back()->setLightModel(action->getState(), lightmodel);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postShadowStyle(void *userdata,
                                        SoCallbackAction *action,
                                        const SoNode * node)
{
  (void)userdata;
  assert(node && node->isOfType(SoShadowStyle::getClassTypeId()));
  const SoShadowStyle *shadowstyle = static_cast<const SoShadowStyle*>(node);
  // This is a work around of the fact that SoShadowStyle does not handle SoCallbackAction
  SoShadowStyleElement::set(action->getState(), const_cast<SoNode*>(node), shadowstyle->style.getValue());
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postMaterial(void *userdata,
                                      SoCallbackAction *action,
                                      const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoMaterial::getClassTypeId()));
  const SoMaterial *material = static_cast<const SoMaterial*>(node);
  self->stack.back()->setMaterial(action->getState(), material);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postVRMLMaterial(void *userdata,
                                          SoCallbackAction *action,
                                          const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoVRMLMaterial::getClassTypeId()));
  auto material = static_cast<const SoVRMLMaterial*>(node);
  self->stack.back()->setMaterial(action->getState(), material);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postColor(void *userdata,
                                   SoCallbackAction *action,
                                   const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoBaseColor::getClassTypeId()));
  const SoBaseColor *color = static_cast<const SoBaseColor*>(node);
  self->stack.back()->setBaseColor(action->getState(), color, color->rgb);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postVRMLColor(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoVRMLColor::getClassTypeId()));
  const SoVRMLColor *color = static_cast<const SoVRMLColor*>(node);
  self->stack.back()->setBaseColor(action->getState(), color, color->color);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postDepthBuffer(void *userdata,
                                         SoCallbackAction *action,
                                         const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoDepthBuffer::getClassTypeId()));
  const SoDepthBuffer *dnode = static_cast<const SoDepthBuffer*>(node);
  self->stack.back()->setDepthBuffer(action->getState(), dnode);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postLight(void *userdata,
                                  SoCallbackAction *action,
                                  const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoLight::getClassTypeId()));
  auto light = static_cast<const SoLight*>(node);
  if (light->on.getValue() && !light->on.isIgnored())
    self->stack.back()->addLight(action->getState(), light);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postVRMLLight(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoVRMLLight::getClassTypeId()));
  auto light = static_cast<const SoVRMLLight*>(node);
  if (light->on.getValue() && !light->on.isIgnored())
    self->stack.back()->addLight(action->getState(), light);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postClipPlane(void *userdata,
                                       SoCallbackAction *action,
                                       const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoClipPlane::getClassTypeId()));
  self->stack.back()->addClipPlane(action->getState(), static_cast<const SoClipPlane*>(node));
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preAutoZoom(void *userdata,
                                     SoCallbackAction *action,
                                     const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  assert(node && node->isOfType(SoAutoZoomTranslation::getClassTypeId()));
  self->stack.back()->addAutoZoom(action->getState(), static_cast<const SoAutoZoomTranslation*>(node));
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postAutoZoom(void *userdata,
                                      SoCallbackAction *action,
                                      const SoNode * node)
{
  (void)node;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::CONTINUE;

  auto state = action->getState();

  // reset to identity matrix because auto zoom will dynamically adjust scale
  // factor of the current transform depending on the current view, so must
  // evaluate on each frame
  SoModelMatrixElement::makeIdentity(state, nullptr);
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preShape(void *userdata,
                                  SoCallbackAction *action,
                                  const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (self->stack.empty())
      return SoCallbackAction::PRUNE;

  // Screen-space image shapes get a substituted capture: see SoFCImageQuad.
  if (node->isOfType(SoImage::getClassTypeId()))
    return preImage(self, action, static_cast<const SoImage *>(node));

  SoState * state = action->getState();
  SoFCRenderCache *currentcache = self->stack.back();

  VCacheSensor & sensor = self->vcachetable[node];
  sensor.attach(self, node);
  CoinPtr<SoFCVertexCache> prev;
  for (auto it=sensor.caches.begin(); it!=sensor.caches.end();) {
    auto & cache = *it;
    if (!prev)
      prev = cache;
    if (cache->getNodeId() != node->getNodeId()) {
      it = sensor.caches.erase(it);
      continue;
    }
    if (cache->isValid(state)) {
      currentcache->addChildCache(state, cache);
      return SoCallbackAction::PRUNE;
    }
    ++it;
  }

  // Worker-emitted content for this node (docs/WorkerVertexCache.md):
  // the fill worker already built the arrays the capture below would
  // re-derive by traversal; adopt them instead. Consumed here one-shot
  // -- takePrebuilt drops the entry when the node was touched since
  // registration.
  std::shared_ptr<const SoFCVertexCache::PrebuiltContent> prebuilt;
  const int wvcmode = Gui::RenderParams::getWorkerVertexCache();
  if (wvcmode > 0) {
    bool stale = false;
    prebuilt = SoFCVertexCache::takePrebuilt(node, &stale);
    if (stale)
      self->countAdoptStale(node);
  }

  // The capture budget (Render CaptureBudgetMS). A publish that has
  // already spent its budget capturing changed shapes keeps this shape's
  // previous vertex cache for the frame -- the mesh is a publish stale,
  // in a scene that is churning anyway -- and a first-time shape simply
  // stays out of the frame, which is what a live import looks like.
  // Every open ancestor cache is poisoned so the next publish walks back
  // down here (a completed ancestor would otherwise turn valid, prune,
  // and freeze the stale child in for good), and the stale cache goes
  // back into the sensor so the NEXT deferral still has a stand-in.
  // Requiring one capture first makes the publish sequence monotonic:
  // each pass captures at least one shape, so the storm drains.
  // A shape with adoptable prebuilt content is never deferred: the
  // budget rations traversal-capture time, and an adoption costs a
  // fraction of it while producing a CORRECT frame instead of a stale
  // one.
  if (self->capturebudgeting && self->capturecount > 0 && !prebuilt) {
    const long budget = Gui::RenderParams::getCaptureBudgetMS();
    if (budget > 0 && self->capturespentms >= double(budget)) {
      if (prev) {
        currentcache->addChildCache(state, prev);
        sensor.caches.emplace_back(prev);
      }
      for (auto & opencache : self->stack)
        opencache->setIncomplete();
      // The caches up to the object's selection root additionally
      // record that the deferred shape is one of THEIR OWN: entries
      // merged through a so-marked cache carry the mark out to the
      // display (VertexCacheEntry::incomplete), so an adopted point or
      // line set never draws ahead of a face set the budget held back
      // (#13b). Up to the selection root and no further, because the
      // root is where the object's sibling drawables -- possibly
      // captured under nested caches of their own -- all pass through,
      // and anything above it belongs to other objects.
      for (auto it = self->stack.rbegin(); it != self->stack.rend(); ++it) {
        (*it)->setIncompleteHere();
        if ((*it)->isSelectionRoot())
          break;
      }
      ++self->deferredcount;
      return SoCallbackAction::PRUNE;
    }
  }

  static int noproto = -1;
  if (noproto < 0)
    noproto = std::getenv("FC_NO_VCACHE_PROTO") ? 1 : 0;
  if (!prev && !noproto) {
    // A color variant of a shared tessellation names its base shape
    // node (protoNode field): seed the fresh cache with a cache of that
    // node so the equality-preserving capture keeps the geometry arrays
    // shared — only the baked color array detaches.
    if (SoNode *proto = SoFCVertexCache::getProtoNode(node)) {
      auto it = self->vcachetable.find(proto);
      if (it != self->vcachetable.end() && it->second.caches.size())
        prev = it->second.caches.front();
    }
  }

  if (self->capturebudgeting)
    self->capturestart = std::chrono::steady_clock::now();
  state->push();
  self->vcache.reset(new SoFCVertexCache(state, const_cast<SoNode*>(node), prev));
  if (self->selnodeid.size())
    self->vcache->setSelectionNodeId(self->selnodeid.back());
  sensor.caches.emplace_back(self->vcache.get());

  currentcache->beginChildCaching(state, self->vcache);
  self->vcache->open(state);

  if (prebuilt) {
    if (wvcmode >= 2) {
      // Verify arm: run the full traversal capture and compare against
      // the worker's content at postShape. An out-of-contract shape is
      // not a defect (the adoption path would have fallen back), so it
      // is not compared.
      if (self->vcache->prebuiltApplicable())
        self->verifyprebuilt = std::move(prebuilt);
    }
    else if (const char * why = self->vcache->prebuiltReject()) {
      // Had content, refused it: name the clause (the fallback below
      // is the normal path, but a publish that never adopts has to be
      // able to say whether it was the registry or the state).
      self->countAdoptReject(why);
    }
    else if (self->vcache->installPrebuilt(*prebuilt)) {
      ++self->adoptedcount;
      // Skip the per-primitive capture; postShape still fires (post
      // callbacks run on PRUNE) and closes the cache exactly as it
      // closes a traversal capture.
      return SoCallbackAction::PRUNE;
    }
    // Contract fallback: the cache is open and empty -- the traversal
    // capture below is the normal path.
  }
  return SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::preImage(SoFCRenderCacheManagerP *self,
                                  SoCallbackAction *action,
                                  const SoImage * node)
{
  SbVec2s size;
  int nc;
  if (!node->image.getValue(size, nc) || size[0] <= 0 || size[1] <= 0)
    return SoCallbackAction::PRUNE;   // empty image: nothing to draw

  VCacheSensor & sensor = self->vcachetable[node];
  sensor.attach(self, node);
  if (!sensor.imageroot) {
    auto *texture = new SoTexture2;
    // The image bakes its own colours (glDrawPixels semantics): replace
    // the fragment colour with the texel, alpha-blended, no material tint.
    texture->model = SoTexture2::REPLACE;
    // Track the owner's pixels live (icon recolour on selection etc.).
    texture->image.connectFrom(&const_cast<SoImage *>(node)->image);

    auto *zoom = new SoAutoZoomTranslation;
    // SoImage is always screen-aligned; pixelScale 1 renders the
    // native-pixel quad at exactly the raw-GL on-screen size.
    zoom->billboard = TRUE;
    zoom->pixelScale = 1.0f;

    auto *quad = new SoFCImageQuad;
    quad->owner = node;

    sensor.imageroot = new SoSeparator;
    // The companion reads the owner's fields back during capture; keep it
    // out of the render/bbox caches so owner edits always re-emit.
    sensor.imageroot->renderCaching = SoSeparator::OFF;
    sensor.imageroot->boundingBoxCaching = SoSeparator::OFF;
    sensor.imageroot->addChild(texture);
    sensor.imageroot->addChild(zoom);
    sensor.imageroot->addChild(quad);
    sensor.imagequad = quad;
  }

  // The companion quad's vertex cache is keyed on the quad's own node id,
  // which cannot see owner edits — retire it when the owner changed.
  if (sensor.imageid != node->getNodeId()) {
    sensor.imageid = node->getNodeId();
    sensor.imagequad->touch();
  }

  // Fold the accumulated Sketcher zoom-translation offset into the quad
  // as a constant pixel offset: one autozoom scale unit spans
  // viewportHeight/50 pixels (SoZoomTranslation's sf times the ortho
  // pixels-per-world factor — the camera height cancels, leaving a
  // screen-constant offset exactly like the per-frame GL path). Setting
  // the field only on change keeps the quad's node id — and with it the
  // vertex cache — stable.
  SoState * state = action->getState();
  SbVec2f zoomoff = SoFCZoomOffsetElement::get(state);
  float vph = float(
      SoViewportRegionElement::get(state).getViewportSizePixels()[1]);
  SbVec3f offpx(zoomoff[0] * 0.02f * vph, zoomoff[1] * 0.02f * vph, 0.f);
  static int dbg = std::getenv("FC_DEBUG_IMAGEQUAD") ? 1 : 0;
  if (dbg)
    fprintf(stderr, "preImage %p zoomoff=(%g,%g) vph=%g offpx=(%g,%g)\n",
            static_cast<const void*>(node), zoomoff[0], zoomoff[1],
            vph, offpx[0], offpx[1]);
  if (sensor.imagequad->pixelOffset.getValue() != offpx)
    sensor.imagequad->pixelOffset = offpx;

  action->traverse(sensor.imageroot);
  return SoCallbackAction::PRUNE;   // skip the raw model-space quad capture
}

SoCallbackAction::Response
SoFCRenderCacheManagerP::postShape(void *userdata,
                                   SoCallbackAction *action,
                                   const SoNode * node)
{
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (!self->vcache) {
    // Never let a verify entry survive into another shape's compare.
    self->verifyprebuilt.reset();
    return SoCallbackAction::PRUNE;
  }

  SoState *state = action->getState();
  state->pop();
  self->vcache->close(state);
  self->stack.back()->endChildCaching(state, self->vcache);

  // TEMPORARY (dots investigation): what a finished capture actually
  // holds. The renderer emits a line draw only when getNumLineIndices()
  // is non-zero (SoFCRenderCache.cpp ~2434), so this is the number that
  // decides whether an object's edges reach the frame at all.
  if (getenv("FC_DOTS_DUMP")) {
    static const auto dotsT0 = std::chrono::steady_clock::now();
    Base::Console().Message(
        "DOTS capture %8.1fms %-16s %p tri=%d line=%d point=%d\n",
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - dotsT0).count(),
        node->getTypeId().getName().getString(),
        static_cast<const void *>(node),
        self->vcache->getNumTriangleIndices(),
        self->vcache->getNumLineIndices(),
        self->vcache->getNumPointIndices());
  }

  if (self->capturebudgeting) {
    self->capturespentms += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - self->capturestart).count();
    ++self->capturecount;
  }

  if (self->verifyprebuilt) {
    std::string diff;
    if (!self->vcache->comparePrebuilt(*self->verifyprebuilt, diff)) {
      int nodeindices = -1;
      if (node->isOfType(SoIndexedShape::getClassTypeId()))
        nodeindices = static_cast<const SoIndexedShape*>(node)
            ->coordIndex.getNum();
      FC_ERR("worker vertex cache mismatch on " << node->getName()
             << " (" << node->getTypeId().getName().getString()
             << " " << static_cast<const void*>(node)
             << " coordIndex " << nodeindices
             << " nodeid " << node->getNodeId()
             << " stamped " << self->verifyprebuilt->nodeid
             << "): " << diff);
    }
    else {
      // Console directly, not FC_LOG: the verify arm is an opt-in
      // diagnostic and its positive signal must be distinguishable
      // from "never ran" without a log-level hunt.
      Base::Console().Log("worker vertex cache verified on %s (%s)\n",
                          node->getName().getString(),
                          node->getTypeId().getName().getString());
      // ...and counted for the publish line, because the Log above is
      // suppressed at default log level: a verify run that compared
      // NOTHING otherwise reads exactly like one that compared
      // everything and agreed.
      ++self->verifiedcount;
    }
    self->verifyprebuilt.reset();
  }

  static int debugproto = -1;
  if (debugproto < 0)
    debugproto = std::getenv("FC_DEBUG_VCACHE_PROTO") ? 1 : 0;
  if (debugproto)
    fprintf(stderr, "vcache node %p (%s) id %llx proto %p verts %p norms %p tri %p\n",
            static_cast<const void*>(node), node->getTypeId().getName().getString(),
            (unsigned long long)self->vcache->getCacheId(),
            static_cast<void*>(SoFCVertexCache::getProtoNode(node)),
            static_cast<const void*>(self->vcache->getVertexArray()),
            static_cast<const void*>(self->vcache->getNormalArray()),
            static_cast<const void*>(self->vcache->getNumTriangleIndices() > 0
                ? self->vcache->getTriangleIndices() : nullptr));

  static int noproto = -1;
  if (noproto < 0)
    noproto = std::getenv("FC_NO_VCACHE_PROTO") ? 1 : 0;
  if (SoNode *proto = noproto ? nullptr : SoFCVertexCache::getProtoNode(node)) {
    // Register the finished cache under its prototype node as well, so
    // sibling variants derive shared arrays even when the base shape
    // itself is never traversed (every instance divergent). A foreign
    // entry never passes the node-id check in preShape, so at most one
    // is kept, purely as a prev-seed candidate.
    VCacheSensor &psensor = self->vcachetable[proto];
    psensor.attach(self, proto);
    for (auto it = psensor.caches.begin(); it != psensor.caches.end();) {
      if ((*it)->getNodeId() != proto->getNodeId())
        it = psensor.caches.erase(it);
      else
        ++it;
    }
    psensor.caches.emplace_back(self->vcache.get());
  }

  self->vcache.reset();
  return SoCallbackAction::CONTINUE;
}

void
SoFCRenderCacheManagerP::addTriangle(void * userdata, SoCallbackAction *action,
                                     const SoPrimitiveVertex * v0,
                                     const SoPrimitiveVertex * v1,
                                     const SoPrimitiveVertex * v2)
{
  (void)action;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (!self->vcache) return;

  assert(v0 < v1 && v0 < v2);
  assert(((const char*)v1- (const char*)v0) % sizeof(SoPrimitiveVertex) == 0);
  assert(((const char*)v2- (const char*)v0) % sizeof(SoPrimitiveVertex) == 0);

  int pdidx[3];
  pdidx[0] = 0;
  pdidx[1] = (int)(v1 - v0);
  pdidx[2] = (int)(v2 - v0);
  self->vcache->addTriangle(v0,v1,v2,pdidx);
}

void
SoFCRenderCacheManagerP::addLine(void * userdata, SoCallbackAction *action,
                                 const SoPrimitiveVertex * v0,
                                 const SoPrimitiveVertex * v1)
{
  (void)action;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (!self->vcache) return;
  self->vcache->addLine(v0,v1);
}

void
SoFCRenderCacheManagerP::addPoint(void * userdata, SoCallbackAction *action,
                                  const SoPrimitiveVertex * v0)
{
  (void)action;
  SoFCRenderCacheManagerP *self = reinterpret_cast<SoFCRenderCacheManagerP*>(userdata);
  if (!self->vcache) return;
  self->vcache->addPoint(v0);
}

void
SoFCRenderCacheManager::getBoundingBox(SbBox3f & bbox) const
{
  PRIVATE(this)->renderer->getBoundingBox(bbox);
}

SbFCUniqueId
SoFCRenderCacheManager::getSceneNodeId() const
{
  return PRIVATE(this)->sceneid;
}

int
SoFCRenderCacheManager::getDeferredCaptureCount() const
{
  return PRIVATE(this)->deferredcount;
}

SoFCRenderCache *
SoFCRenderCacheManager::getSceneCache() const
{
  return PRIVATE(this)->renderer->getScene();
}

void
SoFCRenderCacheManager::setHatchImage(const void *dataptr, int nc, int width, int height)
{
  PRIVATE(this)->renderer->setHatchImage(dataptr, nc, width, height);
}

const char *
SoFCRenderCacheManager::getRenderStatistics() const
{
  return PRIVATE(this)->renderer->getStatistics();
}

// vim: noai:ts=2:sw=2
