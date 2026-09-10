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

namespace App {
class PropertyContainer;
}

namespace Gui {
class View3DInventor;
}

class GuiExport SoFCRenderCacheManager
{
public:
  SoFCRenderCacheManager();
  virtual ~SoFCRenderCacheManager();

  /// Suppress the raw-GL draw of screen-space SoImage shapes (Sketcher
  /// constraint icons, frame labels). Set around the composite Coin pass
  /// when an external backend already draws their captured companions, so
  /// the icon is not doubled — mirrors SoDatumLabel::SuppressGLRender.
  static bool SuppressImageGLRender;

  void render(SoGLRenderAction *action);

  /// Build (or refresh, keyed on \a root's node id) the render cache of an
  /// explicit \a root without drawing anything. Only meaningful together
  /// with setExternalOverlay(): the captured content is mirrored to the
  /// backend's overlay feed. \a action supplies the traversal state seed.
  void capture(SoGLRenderAction *action, SoNode *root);

  /// The same build, seeded without a graphics context: a publisher with
  /// no 3D view has no SoGLRenderAction to take a state from
  /// (docs/HeadlessServe.md §3.2). The seed comes from a plain
  /// SoCallbackAction instead, whose state is created on demand with the
  /// default elements — which is what the rest of the traversal has
  /// always run on anyway, since every nested separator's cache is
  /// opened against the callback action's state, not the GL one.
  ///
  /// Unlike capture(), the result goes to the scene feed rather than an
  /// overlay one: this is the whole scene, just built by a change rather
  /// than by a frame.
  void traverse(SoNode *root, const SbViewportRegion &viewport);

  void clear();

  /// Attach an optional external render backend (see
  /// SoFCRenderer::setExternalRenderer()). Pass null to detach. \a view
  /// optionally identifies the owning 3D view for per-view dynamic
  /// property overrides.
  void setExternalRenderer(Render::Renderer *renderer,
                           App::PropertyContainer *view = nullptr);

  /// The owning 3D view object, whose Section_* properties override the
  /// section and clipping style. Told to the renderer whether or not a
  /// backend is attached, since the internal GL pass honors them too.
  void setViewObject(App::PropertyContainer *view);

  /// Re-translate what the attached backend already holds, for a change
  /// that is baked into a translated draw instead of read per frame (see
  /// SoFCRenderer::refreshExternalFeed).
  void refreshExternalFeed();

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
   *
   * With a face detail (Scope=Element) the override covers just that
   * face: a partial entry rendered over the untouched base draw (no
   * suppression), original geometry and normals, small negative polygon
   * offset. The detail is copied.
   */
  void addShaderOverride(const std::string & key,
                         SoPath * nodepath,
                         const std::shared_ptr<const Render::UserShader> & shader,
                         const SoDetail * detail = nullptr);

  void removeShaderOverride(const std::string & key);

  /** Scene-level user shaders from empty-target App::ShaderBinding bindings
   * (docs/RenderDebug.md §6.5): replaces the whole list, appended after
   * the node-captured shaders in the backend config so the activation
   * wins the "last shader on a stage" rule. Pass an empty list to clear.
   */
  void setAppearanceShaders(std::vector<Render::UserShader> && shaders);

  int clearSelection(bool alt = false);

  bool isOnTop(const std::string & key, bool altonly = true) const;

  bool hasOnTopObject() const;

  /** Drain the on-top keys whose Coin path went stale.
   *
   * A structural change under a resolved path (e.g. a PartDesign tip swap
   * re-parenting a body's children) truncates it, and a truncated path must
   * not be traversed as an on-top root. Such entries stop rendering and are
   * reported here so the caller can resolve them again from the object path.
   * Appends to \a keys and clears the pending set; returns false if empty.
   */
  bool takeInvalidSelections(std::vector<std::string> & keys);

  const SbFCMap<int, Gui::CoinPtr<SoPath> > & getSelectionPaths() const;

  void getBoundingBox(SbBox3f & bbox) const;

  SbFCUniqueId getSceneNodeId() const;

  /** How many shapes the last publish left a frame stale under the
   * capture budget (Render CaptureBudgetMS). Non-zero means the publish
   * is not done: the caller owning the render loop should schedule
   * another redraw, and each follow-up publish captures at least one
   * more shape until this returns 0.
   */
  int getDeferredCaptureCount() const;

  /// The scene's root render cache as last built by render(), capture()
  /// or traverse(); null before the first build. Read-only inspection
  /// (tests, external consumers).
  SoFCRenderCache * getSceneCache() const;

  void setHatchImage(const void *dataptr, int nc, int width, int height);

  void doLatePick(SoRayPickAction *action) const;

  const char *getRenderStatistics() const;

private:
  friend class SoFCRenderCacheManagerP;
  SoFCRenderCacheManagerP * pimpl;
};

namespace Gui
{
/// Load the configured section-cap hatch image (ViewParams'
/// SectionHatchTexture) into \a manager, caching the decode per file and
/// modification time. Shared rather than a viewer detail because it is a
/// property of how the document is drawn, not of a view: a publisher
/// with no 3D view has to put the same image in its snapshot
/// (docs/HeadlessServe.md §3.3).
GuiExport void applySectionHatchTexture(SoFCRenderCacheManager &manager,
                                       App::PropertyContainer *view = nullptr);

/// Materialize the Render_* dynamic properties that the per-frame config
/// push reads as a per-container override of the global RenderParams
/// (SoFCRendererBridge's translate* functions). Created for a 3D view
/// when a backend is selected, and by a view-less publisher for its own
/// container — the overrides describe how a document is presented, so a
/// publisher with no window still has to offer them, and the remote
/// control channel still has somewhere to put an edit
/// (docs/HeadlessServe.md §3.3).
GuiExport void initRenderProperties(App::PropertyContainer *view);

/// What a Coin SoShadowGroup still has to be told, out of the shadow
/// map's own settings. The rest of the family -- the ground receiver --
/// has no consumer on this side at all: the backend reads it through
/// the bridge.
struct ShadowRenderParams {
    double precision = 1.0;
    double epsilon = 1.0e-5;
    double threshold = 0.0;
    long smoothBorder = 0;
    long spreadSize = 0;
    long spreadSampleSize = 0;
};

/// Materialize the shadow map and ground receiver settings as
/// RenderShadow_* properties (group "Render Shadow"), and return the few
/// a Coin shadow group consumes. Called wherever the render properties
/// are created, so the surface exists whether or not a draw style ever
/// asks for it -- the bridge that feeds the backend only reads
/// (docs/CoinRetirement.md stage 4d).
GuiExport ShadowRenderParams materializeShadowRenderParams(
        App::PropertyContainer *view);

/// The RenderShadow_* names the engine itself consumes: a
/// null-terminated list, and the exclusion list of the custom shader
/// parameter rule (docs/RenderDebug.md sec 2.5) -- any OTHER RenderShadow_
/// property is a user uniform.
GuiExport const char * const *shadowRenderPropertyNames();

/// Turn on what the Shadow draw style stood for -- the renderer's scene
/// light and its shadow map -- for a container that asked for that style
/// by name (a restored DrawStyle, or a saved camera's overrideMode).
GuiExport void applyLegacyShadowStyle(App::PropertyContainer *view);

/// Rename a document's Shadow_* view properties onto their RenderShadow_
/// (and Render_Light*) equivalents, and convert a DrawStyle of "Shadow"
/// into the display style it wrapped plus Render_Light / Render_Shadow.
/// Called on view restore and when a saved view is applied, so a file
/// written before stage 4d keeps its look and stops carrying a surface
/// nothing reads.
GuiExport void migrateShadowProperties(App::PropertyContainer *view);

/// The per-container render property names that were retired to global
/// RenderParams (debug/measurement switches, ladder tuning, occlusion
/// culling, the GPU budget): a null-terminated list. Saved copies inside
/// old documents shadowed the globals, so they are no longer read.
GuiExport const char * const *legacyRenderPropertyNames();

/// Remove any retired render properties (legacyRenderPropertyNames)
/// still sitting on the container -- called after a view restores its
/// saved properties, so old documents load compatibly and the dead
/// override surface does not linger.
GuiExport void stripLegacyRenderProperties(App::PropertyContainer *view);

/// Drop the render properties that describe the machine rather than the
/// model and materialize them again from this installation's preferences.
/// The ones origin retired to global RenderParams are handled by
/// stripLegacyRenderProperties above; these are the ones that stay
/// per-view -- the AO and effect resolutions, the tessellation and level
/// knobs -- and carry Prop_NoPersist so they are never written. A file
/// written before that still carries them, which is what this is for: a
/// view calls it once it has restored itself. (The attribute cannot be
/// added afterwards: Property::setStatusValue masks that bit out, so the
/// only way to get it is to create the property again.)
GuiExport void reseedLocalRenderProperties(App::PropertyContainer *view);

/// Point the Cycles_Device enumeration at THIS machine: its value list
/// becomes the device types the local engine can see, keeping the
/// selected NAME when the list has it and falling back to the first
/// entry -- so a document saved on a machine with an OPTIX card still
/// renders here instead of failing on a device that does not exist.
/// Creates the property (seeded from the CyclesDevice preference) when
/// it is missing; called both at materialize time
/// (initRenderProperties) and after a view restores itself, which is
/// when the property holds the originating machine's list (a custom
/// enumeration restores its own CustomEnumList). A no-op without the
/// engine.
GuiExport void remapCyclesDeviceProperty(App::PropertyContainer *view);

/// The effective value of one section/clipping style key for \a view: its
/// Section_* property if it has one, and the ViewParams preference
/// otherwise. How a section is capped, hatched and filled is part of how a
/// clipped model is meant to be read, so a view - and the saved view that
/// restores it - can answer for it instead of moving everybody's default.
/// A property here IS the override: it exists only where somebody chose
/// one, which is also what makes it worth saving.
GuiExport bool sectionStyle(App::PropertyContainer *view, const char *name, bool def);
GuiExport double sectionStyle(App::PropertyContainer *view, const char *name, double def);
GuiExport std::string sectionStyle(App::PropertyContainer *view, const char *name,
                                   const std::string &def);
}

#endif // GUI_SOFCRENDERCACHEMANAGER_H
// vim: noai:ts=2:sw=2
