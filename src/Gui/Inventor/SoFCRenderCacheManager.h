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

#ifndef GUI_SOFCRENDERCACHEMANAGER_H
#define GUI_SOFCRENDERCACHEMANAGER_H

#include "COWData.h"
#include "../InventorBase.h"

class SoSFImage;
class SoGLRenderAction;
class SoGroup;
class SoNode;
class SoFCRenderCache;
class SoFCRenderCacheManagerP;
class SoPath;
class SoDetail;

namespace Render {
class Renderer;
struct OverlayAnchor;
struct UserShader;
}

namespace Gui {
class View3DInventor;
}

class GuiExport SoFCRenderCacheManager
{
public:
  SoFCRenderCacheManager();
  virtual ~SoFCRenderCacheManager();

  void render(SoGLRenderAction *action);

  /// Build (or refresh, keyed on \a root's node id) the render cache of an
  /// explicit \a root without drawing anything. Only meaningful together
  /// with setExternalOverlay(): the captured content is mirrored to the
  /// backend's overlay feed. \a action supplies the traversal state seed.
  void capture(SoGLRenderAction *action, SoNode *root);

  void clear();

  /// Attach an optional external render backend (see
  /// SoFCRenderer::setExternalRenderer()). Pass null to detach. \a view
  /// optionally identifies the owning 3D view for per-view dynamic
  /// property overrides.
  void setExternalRenderer(Render::Renderer *renderer,
                           Gui::View3DInventor *view = nullptr);

  /// Route the scene feed to the backend's overlay feed instead (see
  /// SoFCRenderer::setExternalOverlay()): this manager then captures an
  /// overlay root (foreground superimposition, corner axis cross) and
  /// mirrors it as Renderer::setOverlay(\a id, ..., \a anchor), while
  /// render() stops drawing any internal GL pass. Pass null to detach.
  void setExternalOverlay(Render::Renderer *renderer, int id,
                          const Render::OverlayAnchor &anchor);

  SoPath *getHighlightPath() const;
  void setHighlight(SoPath * path,
                    const SoDetail * detail,
                    uint32_t color,
                    bool ontop = false,
                    bool wholeontop = false);

  void clearHighlight();

  void addSelection(const std::string & key,
                    const std::string & element,
                    SoPath * nodepath,
                    SoPath * detailpath,
                    const SoDetail * detail,
                    uint32_t color,
                    bool ontop = false,
                    bool alt = false,
                    bool implicit = false);

  void addSelection(const std::string & key,
                    const std::string & element,
                    SoNode * node,
                    uint32_t color,
                    bool ontop = false,
                    bool alt = false);

  void removeSelection(const std::string & key,
                       const std::string & element,
                       bool alt = false);

  /** Apply a user shader to the whole object at a full instance path
   * (docs/RenderDebug.md §6.5, the Appearance binding).
   *
   * Rides the per-path selection side channel: a non-on-top whole-object
   * entry whose materials carry the shader replaces the base draws via
   * the same-key suppression, scoped to exactly this path (one instance
   * of a linked object, not all instances). One path per key; a repeated
   * key updates the shader/path in place.
   */
  void addShaderOverride(const std::string & key,
                         SoPath * nodepath,
                         const std::shared_ptr<const Render::UserShader> & shader);

  void removeShaderOverride(const std::string & key);

  /** Scene-level user shaders from empty-target App::Appearance bindings
   * (docs/RenderDebug.md §6.5): replaces the whole list, appended after
   * the node-captured shaders in the backend config so the activation
   * wins the "last shader on a stage" rule. Pass an empty list to clear.
   */
  void setAppearanceShaders(std::vector<Render::UserShader> && shaders);

  int clearSelection(bool alt = false);

  bool isOnTop(const std::string & key, bool altonly = true) const;

  bool hasOnTopObject() const;

  const SbFCMap<int, Gui::CoinPtr<SoPath> > & getSelectionPaths() const;

  void getBoundingBox(SbBox3f & bbox) const;

  SbFCUniqueId getSceneNodeId() const;

  void setHatchImage(const void *dataptr, int nc, int width, int height);

  void doLatePick(SoRayPickAction *action) const;

  const char *getRenderStatistics() const;

private:
  friend class SoFCRenderCacheManagerP;
  SoFCRenderCacheManagerP * pimpl;
};

#endif // GUI_SOFCRENDERCACHEMANAGER_H 
// vim: noai:ts=2:sw=2
