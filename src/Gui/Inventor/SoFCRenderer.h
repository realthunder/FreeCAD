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

#ifndef GUI_SOFCRENDERER_H
#define GUI_SOFCRENDERER_H

#include "../InventorBase.h"
#include "SoFCRenderCache.h"

class SoGLRenderAction;
class SoGroup;
class SoFCRenderCache;
class SoPath;
class SoFCRendererP;

namespace Render {
class Renderer;
struct OverlayAnchor;
struct UserShaderConfig;
}

namespace App {
class PropertyContainer;
}

namespace Gui {
class View3DInventor;
}

class GuiExport SoFCRenderer {
public:
  SoFCRenderer();
  virtual ~SoFCRenderer();

  void clear();

  /// Attach an optional external render backend. When set, the scene,
  /// selection and highlight feeds are mirrored to it in translated
  /// (backend-neutral) form, and render() skips the internal fixed-function
  /// GL pass while the backend reports canSkipInternal(). Pass null to
  /// detach. Set env FC_RENDERER_PARALLEL_GL=1 to keep the GL pass drawing
  /// on top for comparison. The optional \a view identifies the owning 3D
  /// view object so per-frame configs can honor its Render_*/Shadow_*
  /// dynamic property overrides.
  void setExternalRenderer(Render::Renderer * renderer,
                           App::PropertyContainer * view = nullptr);

  /// The owning 3D view object, whose Section_* properties can override
  /// the section and clipping style this renderer draws with. Set
  /// independently of any backend, since the internal GL pass honors the
  /// same overrides. The style is snapshotted once per frame: it is read
  /// deep inside the draw loops, where a property lookup per draw entry
  /// would cost more than the setting is worth.
  void setViewObject(App::PropertyContainer * view);

  /// Re-translate everything already fed to the attached backend (the
  /// scene, the selections, the highlight). For the two section style
  /// keys that are baked into a translated draw rather than read per
  /// frame -- whether an on-top draw is sectioned, and whether the
  /// section is concave -- since nothing else would republish a scene
  /// that has not otherwise changed. Cheap and lossless next to dropping
  /// the caches: no traversal, and the selection feeds survive.
  void refreshExternalFeed();

  /// Route this renderer's scene feed to an external backend's overlay
  /// feed instead of the main scene feed: setScene() translates the
  /// caches into Renderer::setOverlay(\a id, draws, \a anchor), while
  /// render() becomes a no-op (no per-frame configs, no internal GL
  /// pass). Used to capture overlay roots (the foreground
  /// superimposition, the corner axis cross) through the same
  /// render-cache traversal as the main scene. Pass null to detach,
  /// which removes the overlay from the backend.
  void setExternalOverlay(Render::Renderer * renderer, int id,
                          const Render::OverlayAnchor & anchor);

  void render(SoGLRenderAction * action);

  /// Push every per-frame configuration (AO, PBR, water, bloom, light,
  /// hidden line, ...) to the external backend. Called on the way
  /// through render(), and separately by a publisher that never renders
  /// (docs/HeadlessServe.md §3.3) — these describe how the scene looks,
  /// not how a frame is drawn, so a snapshot needs them just as much as
  /// a frame does. \a state supplies the three that are read from the
  /// traversal (hidden line, light, autozoom); a headless caller passes
  /// the same GL-free state it traverses with.
  void pushExternalConfigs(SoState * state);

  void setScene(const Gui::CoinPtr<SoFCRenderCache> & cache);

  /// The scene cache last given to setScene(); null before the first
  /// build. Read-only inspection (tests, external consumers).
  const Gui::CoinPtr<SoFCRenderCache> & getScene() const;

  /// User shader programs captured from scene SoShaderProgram nodes by
  /// the render cache manager during the last cache rebuild; mirrored to
  /// the external backend each render (docs/RenderDebug.md §6).
  void setUserShaders(Render::UserShaderConfig && config);

  /// Scene-level user shaders activated by App::ShaderBinding objects with
  /// an empty target list (docs/RenderDebug.md §6.5): appended after the
  /// node-captured shaders in the config fed to the backend, so a
  /// document-object activation wins over a raw scene node ("the last
  /// shader on a stage wins"). Owned by the Appearance binding registry —
  /// replaced wholesale on every rebuild, independent of scene recapture.
  void setAppearanceShaders(std::vector<Render::UserShader> && shaders);

  typedef SoFCRenderCache::VertexCacheMap VertexCacheMap;

  void setHighlight(VertexCacheMap && caches, bool wholeontop);
  void clearHighlight();

  enum SelIdBits {
    SelIdImplicit   = 0x01000000,
    SelIdAlt        = 0x02000000,
    SelIdFull       = 0x04000000,
    SelIdPartial    = 0x08000000,
    SelIdMask       = 0x0fffffff,
    SelIdSelected = (SelIdPartial|SelIdFull),
  };
  void addSelection(int id, const VertexCacheMap & caches);
  void removeSelection(int id);

  void getBoundingBox(SbBox3f & bbox) const;

  void setHatchImage(const void *dataptr, int nc, int width, int height);

  const char * getStatistics() const;

private:
  friend class SoFCRenderCacheP;
  SoFCRendererP * pimpl;
};

#endif //GUI_SOFCRENDERER_H
// vim: noai:ts=2:sw=2
