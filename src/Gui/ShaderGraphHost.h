/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef GUI_SHADERGRAPHHOST_H
#define GUI_SHADERGRAPHHOST_H

/// \file ShaderGraphHost.h
/// The shader graph editor's host on the desktop (docs/
/// ShaderGraphEditor.md sec 4.2, phase 2): the renderer's
/// EngineGraphHost with the preview rendered -- the material icon's
/// sphere wearing the edited document, run through the render-cache
/// pipeline and handed to the document's live 3D view backend as a
/// transient capture scene, the way the TechDraw shaded underlay
/// captures a derived shape. No backend (no 3D view on the renderer
/// path) means no preview pane; the editor works without one.
///
/// The same pane can be path traced instead (sec 15): the same sphere
/// scene handed to a Cycles Viewport of this host's own, registered
/// with no backend, its refining frames taken as the served stream
/// takes them and composited into the preview texture.

#include <QObject>
#include <QTimer>
#include <fastsignals/signal.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Gui/InventorBase.h>
#include <Gui/Renderer/GraphEditor/EngineGraphHost.h>

class SoSeparator;
class SoPerspectiveCamera;
class SoFragmentShader;
class SoShaderParameterArray1f;
class SoFCRenderCacheManager;
class SbMatrix;

namespace App {
class ShaderProgram;
}
namespace Render {
class GraphEditorWidget;
class Renderer;
namespace Cycles {
class Viewport;
struct SceneInput;
}
}

namespace Gui {

class BaseView;
class View3DInventorViewer;

class GuiExport ShaderGraphHost : public QObject, public Render::GraphEditor::EngineGraphHost {
    Q_OBJECT
public:
    ShaderGraphHost(App::ShaderProgram *program, Render::GraphEditorWidget *editor);
    ~ShaderGraphHost() override;

    /// The program's Images or Surface changed: thumbnails and the
    /// preview are stale.
    void programChanged();
    /// The text the editor was loaded with. The preview's shader is
    /// generated from THIS text, with the document's public input
    /// values -- the ones a drag moves -- carried as parameters the way
    /// the viewport carries Param_* properties, so a value drag
    /// changes uniforms and never regenerates or recompiles.
    void setBaseText(const std::string &xml);

    std::string resolveImage(const std::string &name) override;
    std::vector<std::string> imageNames() override;
    bool loadImage(const std::string &path, int &width, int &height,
                   std::vector<uint8_t> &rgba) override;
    void previewInvalidated() override;
    const std::vector<std::string> &surfaceNames() override;
    std::string currentSurface() override;
    /// Writes the program's Surface property, deferred to the event
    /// loop: the pick is made inside the editor's frame, and the write
    /// reloads the editor.
    void selectSurface(const std::string &name) override;
    /// Raster and Path traced when the build carries the Cycles
    /// engine, else nothing (no menu). A pick is applied from the
    /// event loop: the session is created outside the editor's frame.
    const std::vector<std::string> &previewModes() override;
    int previewMode() override;
    void setPreviewMode(int mode) override;
    /// The device types the path tracer can compute on, while the
    /// traced mode is up; none in raster mode, which has no choice to
    /// make. The default is the 3D view's effective Cycles_Device, and
    /// a pick here overrides it for this editor only.
    const std::vector<std::string> &previewDevices() override;
    int previewDevice() override;
    void setPreviewDevice(int device) override;

    /// Watches the editor widget's show and hide: a pane nobody can
    /// see is not worth a render, and a path tracer session behind a
    /// hidden pane is a device held for nothing.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Render the preview from the event loop: never from inside the
    /// editor's paint, where a backend frame is being encoded.
    void renderPreview();
    /// The raster path: the draws as the backend's transient capture
    /// scene, renderOffscreen into an FBO, the pixels read back.
    void renderRaster(int w, int h, Render::Renderer *renderer, View3DInventorViewer *viewer);
    /// The traced path: the draws to the tracer when the scene changed,
    /// the camera alone when not; the frames land in takeTracedFrame.
    void renderTraced(int w, int h, View3DInventorViewer *viewer);
    void takeTracedFrame();
    void updateTracedStatus();
    void applyPreviewMode(int mode);
    /// Create the session on the view's effective options, replacing
    /// any there is. False when there is no view to take them from or
    /// the engine refused the device -- and then the mode still says
    /// traced, so a view attaching later tries again.
    bool startTracer();
    void stopTracer();
    /// Read the editor's visibility and act on a change: the session
    /// pauses with the pane and resumes with it, and a render skipped
    /// while hidden is made up when it comes back.
    void updateHidden();
    /// A view of the program's document came or went. The preview's
    /// whole configuration -- the backend, the environment, the light
    /// rig, the tracer's options -- comes from a 3D view, so both ends
    /// of that matter: the last one leaving parks the session and
    /// empties the pane, and one arriving starts them again.
    void viewsChanged();
    /// A 3D view of the program's document, one with a backend
    /// preferred (the raster path needs one; the traced path only
    /// needs the view's settings); null when the document has none.
    View3DInventorViewer *findViewer() const;
    /// The viewer's backend, when the found viewer has one.
    bool findBackend(Render::Renderer *&renderer, View3DInventorViewer *&viewer) const;
    /// The document as the preview's shader node takes it: the text
    /// with the program's stored images named where they are.
    std::string documentForRender() const;
    void buildScene();
    /// The shader node's text, surface and parameters from the live
    /// document, and the camera from the orbit, for a pane of w x h.
    void updateScene(int w, int h);
    /// The scene through the render caches into the backend-neutral
    /// draw list both paths consume. The cache manager persists across
    /// renders so the sphere keeps its cache identity: the tracer keys
    /// its meshes on it, and a fresh manager per render would hand it
    /// a new sphere -- a mesh rebuilt and a session reset -- each time.
    bool translateScene(int w, int h, Render::DrawCallList &draws);
    /// The camera's matrices as the renderers take them.
    void cameraMatrices(int w, int h, SbMatrix &view, SbMatrix &proj) const;

    App::ShaderProgram *const program;
    Render::GraphEditorWidget *const editor;
    CoinPtr<SoSeparator> root;
    CoinPtr<SoPerspectiveCamera> previewCamera;
    CoinPtr<SoFragmentShader> fragment;
    std::unique_ptr<SoFCRenderCacheManager> manager;
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> paramNodes;
    /// The baseline's public input values as parameters (setBaseText).
    std::vector<Render::RenderDebugConfig::UserParam> baseParams;
    /// The text the surface names were last read from, and the names.
    std::string namesText;
    std::vector<std::string> names;
    /// The live text last inspected, and its public inputs.
    std::string inspectedText;
    std::vector<Render::RenderDebugConfig::UserParam> liveParams;
    /// Renders coalesce on this timer: a drag reports every motion.
    QTimer render;
    /// While the last preview drew a stand-in for a shader still
    /// compiling, the poll asks the backend until the compile lands.
    QTimer poll;
    int compileGeneration = 0;

    /// 0 = raster, 1 = path traced (the index into previewModes).
    int mode = 0;
    /// The engine refused the device: do not ask it again until
    /// something changes (a mode pick, a view attaching).
    bool tracerBlocked = false;
    /// The editor is not on screen (another cell's tab is up, the view
    /// is closed but not yet deleted): the tracer is paused and no
    /// preview is rendered.
    bool hidden = false;
    /// Something invalidated the preview while hidden: render once the
    /// pane is back.
    bool staleWhileHidden = false;
    /// The document's view attach and detach, for viewsChanged.
    fastsignals::connection attachConnection;
    fastsignals::connection detachConnection;
    /// The device types devices() reports, deduplicated, read once.
    std::vector<std::string> deviceTypes;
    /// The device picked in the menu; empty follows the 3D view's.
    std::string device;
    /// The path tracer's session while the mode is traced.
    std::unique_ptr<Render::Cycles::Viewport> tracer;
    /// What the tracer holds: the shader node's text, surface and
    /// parameter values as one string, and the configs (draws and
    /// camera cleared), to tell a restate from a camera move.
    std::string tracedSignature;
    std::unique_ptr<Render::Cycles::SceneInput> tracedInput;
    /// Frames taken from the current session, for the one-per-session
    /// log line that says it is producing any.
    int tracedFrames = 0;
    /// Staged frames coalesce on this timer before they are taken.
    QTimer frame;
};

} // namespace Gui

#endif // GUI_SHADERGRAPHHOST_H
