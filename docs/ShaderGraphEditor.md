# The Shader Graph Editor

MaterialX's node-graph editor, ported into the fork and hosted on the
bgfx renderer, as the view half of "Edit Shader Graph..."
(`docs/MaterialStorage.md` 17.11). Researched and ruled 2026-09-03;
coding starts in the session after. This is the reference the coding
sessions work from: what exists, what was decided and why, the
interfaces, the file map, the build order, and the day-one checklist.

## 1. Why

`docs/MaterialStorage.md` 17.11 built the model half of editing a
card's MaterialX graph: the button materializes the card's document as
an `App::ShaderProgram` (MATERIALX dialect) plus an `App::Shader` and a
Scope=Object `App::ShaderBinding`, the binding beats the card the
object still wears, and reverting removes the three objects. It built
no view half. `FragmentProgram` is a `PropertyStringIncluded`; the
property editor opens it through `PropertyStringItem`, a one-line
`ExpLineEdit` (`src/Gui/propertyeditor/PropertyItem.cpp:939`), over a
whole XML document. The knobs are fine -- the graph's declared inputs
are `Param_*` properties (RenderEngine.md 5.5, CyclesIntegration.md
6.11) -- but changing the GRAPH means Python or an outside editor. And
`ViewProviderShaderProgram` has no `doubleClicked`, so a program in the
tree opens nothing. The button promises an editor the fork lacks.

## 2. What MaterialX ships, measured

`src/3rdParty/MaterialX/source/MaterialXGraphEditor/` (MaterialX
1.39.5, option `MATERIALX_BUILD_GRAPH_EDITOR`, off in our build), 8.1k
lines:

| File | Lines | What it is |
|---|---|---|
| `Graph.cpp`, `Graph.h` | 5.1k | The editor: node UI from nodedefs, pins and links, add/delete/copy/paste, descent into compound nodegraphs, a property panel of 22 widget kinds, node search popup, read-only for library graphs, save/load, node positions as `xpos`/`ypos` attributes |
| `RenderView.cpp`, `.h` | 1.4k | A complete GL viewer on `MaterialXRenderGlsl`: shader ball, HDR environment, shadow map, its own image handler |
| `Layout.cpp`, `.h` | 0.6k | Auto layout |
| `UiNode.cpp`, `.h` | 0.4k | The UI node / pin / edge model |
| `FileDialog*`, `Main.cpp` | 0.6k | An ImGui file browser (with a Darwin `.mm`); the GLFW window loop and option parsing |

Dependencies: Dear ImGui (nested submodule, commit `9aae45eb`),
`imgui-node-editor` (nested submodule `2f99b2d6`, plus `drawing.cpp`
and `widgets.cpp` from its blueprints example), a vendored minimal
GLFW, Glad through `MaterialXRenderGlsl`, and the `MaterialXRender` +
`MaterialXRenderGlsl` libraries -- which we build with
`MATERIALX_BUILD_RENDER=OFF` (`src/3rdParty/CMakeLists.txt`). Neither
ImGui submodule is checked out on this box. The JavaScript tier
(`javascript/MaterialXView`) is a viewer only; there is no MaterialX
web editor to borrow.

**The coupling to its viewer is narrow.** `_renderer` is named 47 times
in `Graph.cpp`, through this surface and nothing more:

- `setDocument(doc)`, `updateMaterials(elem)`, `setMaterialCompilation
  (bool)` -- the preview follows the document; the flag shows a
  "compiling" badge (`shaderPopup`).
- `getGenContext().getShaderGenerator().getImplementation(nodedef,
  ctx)` -- one use, in `checkCanAddLink`, refusing a node with no
  implementation.
- `getImageHandler()` -- `supportedExtensions()` for the file filter
  and `acquireImage(path)->getResourceId()` for the property panel's
  thumbnails (`ImGui::Image`, `Graph.cpp:965`).
- `getViewWidth/Height`, `setViewWidth/Height`, `getPixelRatio`,
  `getViewCamera()->setViewportSize`, `_textureID` -- the embedded
  preview viewport (`Graph.cpp:3089-3130`) -- and `setMouseMotionEvent`,
  `setMouseButtonEvent`, `setKeyEvent`, `setScrollEvent` from
  `handleRenderViewInputs`.
- `loadMesh(file)`, `getXincludeFiles()`, `initialize()`.
- `getMaterials()[0]->modifyUniform(name, value)` -- a value drag
  without recompiling.

Direct GL: two calls (`glEnable/glDisable(GL_FRAMEBUFFER_SRGB)` in
draw-list callbacks, `Graph.cpp:110-114`) and the
`MaterialXRenderGlsl/External/Glad/glad.h` include. Everything else is
ImGui, the node editor, and `mx::Document`.

**Its document contract is ours.** The editor mutates one
`mx::DocumentPtr _graphDoc` in place: `loadDocument` reads a file with
an XInclude callback and sets the data library (`doc->setDataLibrary
(_stdLib)`); `saveDocument` writes XML, optionally stripping positions.
Our program is text in a property, so the integration is a string
round trip, section 4.

## 3. Rulings (2026-09-03)

1. **Port the MaterialX editor; do not rewrite it, do not launch it.**
   Five hosts were weighed (MaterialStorage.md 17.25 keeps the short
   form): ImGui drawn by bgfx inside a `QOpenGLWidget` through the draw
   facade (chosen); ImGui through `imgui_impl_opengl3` (a desktop-GL
   assumption in a new subsystem, against the CLAUDE.md rule); a Qt
   widgets rewrite in the QuiltiX shape (throws away 5k lines of
   MaterialX-specific logic, needs PyMaterialX and NodeGraphQt, no
   browser path -- mined for UX only); a node editor on our own
   `Vg2D`/`Page2D` facade (from scratch); and launching the stock
   `MaterialXGraphEditor` on a temp file (a second GLFW window, a
   preview that disagrees with the viewport by design per 17.19, no
   undo, needs `MATERIALX_BUILD_RENDER`).
2. **ImGui is bgfx's copy, drawn by bgfx's renderer.** The bgfx tree
   carries Dear ImGui 1.92.8 WIP (`src/3rdParty/bgfx/bgfx/3rdparty/
   dear-imgui`, `imconfig.h` with bx allocators and
   `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`) and a 651-line ImGui renderer
   (`bgfx/examples/common/imgui/imgui.cpp`) with embedded prebuilt
   shaders for every backend. Neither is compiled by us today. We do
   not check out MaterialX's nested ImGui; `imgui-node-editor` becomes
   OUR submodule under `src/3rdParty/`, at a commit that builds against
   1.92.8.
3. **The preview is our engine's, not MaterialX's.** `RenderView` is
   dropped, and with it `MaterialXRender`, `MaterialXRenderGlsl`, Glad
   and GLFW. The preview is the material icon's path --
   `Renderer::renderOffscreen` with a `setCaptureScene` transient sphere
   (Renderer.h; `material-icon-rendering`) -- so it shows what the
   viewport shows, through the same `BgfxShaderGenerator`
   (`MaterialXGen.cpp`).
4. **The editor is a view of an `App::ShaderProgram`, not of a card.**
   "Edit Shader Graph..." materializes if needed and then opens it;
   "Revert Shader Graph" becomes its own button beside it, keeping the
   confirm-when-edited rule. Double-clicking a MATERIALX-dialect
   program in the tree opens the same view, so the effect library's
   documents get the editor too.
5. **Hosted the way a TechDraw page, a spreadsheet and a text
   document are: through `Gui::ViewPlacement::place(view,
   Category::DocView, doc)`** (`src/Gui/ViewPlacement.h`,
   `docs/ViewPlacement.md`), never a hard-coded MDI tab. The user's
   `View/OpenView` preference decides -- and the Document-view
   category's DEFAULT is **Split**: the view opens as a cell of the
   document's split-view area beside the 3D view, reusing the
   last-used non-3D cell when there is one (a 3D cell is never
   replaced), with the Alt inversion popping it out to a tab. The
   view is a `Gui::MDIView` subclass in the `TextDocumentEditorView`
   shape so that everything the area does for a page or a sheet --
   the cell content menu, layout persistence, restore, reveal-if-open
   -- works for it unchanged.
6. **A one-day spike precedes the port** (section 7, phase 0). It is
   thrown away if either risk of section 8 bites.
7. **The ported sources are a COPY**, `src/Gui/Renderer/GraphEditor/`,
   the CAM port's model (`cam-port-clipper2`); the MaterialX submodule
   stays pristine. The ASCII rule applies to lines we change, never to
   the copied text.

## 4. Design

### 4.1 The document round trip

Open: `mx::readFromXmlString(doc, obj->FragmentProgram.getValue())`
with `doc->setDataLibrary(Render::MaterialX::dataLibrary())`
(`MaterialXSupportP.h`) -- the fork already loads the shipped data
library once and shares it; the editor's own `loadStandardLibraries`
and `_stdLib` go. Relative image names resolve the way the program's
own consumers resolve them: `Render::MaterialX::searchPath(doc)` and
`resolveFile(doc, name)`, against the program's `Images`
(`PropertyFileIncludedList`).

Commit: every completed gesture -- a node added or deleted, a link made
or broken, a value drag released, a rename -- writes
`mx::writeToXmlString(doc)` into `FragmentProgram` inside one
`Gui::Command::openCommand/commitCommand` pair, through the Python
primitive the sync commands use (`Materials.materializeShaderGraph` is
bound in `src/Mod/Material/App/AppMaterial.cpp:88`; the write itself
is a plain property assignment on the macro record). A value drag
coalesces: the document is updated live for the preview while the
mouse is down, and the property is written on release. The sync that
runs on the text change -- `ViewProviderShaderProgram::updateData`
(`ViewProviderShaderObject.cpp:336`): regenerate, resync `Param_*`,
carry the images the text names -- updates the viewport. Undo is the
property's undo. The editor is a view over a property; it holds no
state the document does not.

Positions ride as `xpos`/`ypos`, MaterialX's own attributes. Our
generator ignores them; `ShaderGraph::cardFromEdit` keeps them, as the
library's example files do. A freshly materialized library card
carries none, so the view runs `Layout` once on open when no node has a
position (`_needsLayout` already does this).

`Surface` (the program's property naming which surface of a
many-surface document it renders, 17.13) is shown as a picker in the
view; changing it writes the property.

### 4.2 `GraphHost`: what replaces `RenderView`

One abstract class of about ten methods, the section-2 list reduced to
what a host must answer, declared in `GraphEditor/GraphHost.h`:

```cpp
class GraphHost {
public:
    virtual ~GraphHost() = default;
    // The document changed (topology or a value); a null element means
    // everything. The host re-renders the preview when it can.
    virtual void documentChanged(mx::ElementPtr changed) = 0;
    // A value moved during a drag: preview only, no property write.
    virtual void valueDragged(mx::InputPtr input, mx::ValuePtr value) = 0;
    // Whether a nodedef has an implementation for our target
    // (checkCanAddLink's one use of the generator context).
    virtual bool hasImplementation(const mx::NodeDef &def) = 0;
    // A thumbnail for an image the document names: an ImTextureID
    // (bgfx handle packed by ImGui::toId) plus its size, or a null id.
    virtual ImTextureID thumbnail(const std::string &name, int &w, int &h) = 0;
    virtual const std::vector<std::string> &imageExtensions() = 0;
    // The preview: its texture at the requested size, and its input.
    virtual ImTextureID preview(int w, int h) = 0;
    virtual void previewMouse(float x, float y, int button, bool down) = 0;
    virtual void previewScroll(float delta) = 0;
    virtual bool compiling() const = 0;
};
```

The implementation is in two halves (phase 2, section 12), split
where the libraries split. `EngineGraphHost`
(`GraphEditor/EngineGraphHost.h`, `FreeCADRenderer`) answers what the
renderer can by itself: `thumbnail` -> `DrawDevice::createTexture2D`
from our image loader (`ImageDecode.cpp`), `hasImplementation` -> the
generator's `getImplementation` through `Render::MaterialX`, the
preview texture's ownership and upload, the orbit camera the pane
drives, `compiling()`. Its header names no MaterialX type -- the
`GraphHost` the editor talks to is an adapter it owns -- so a
subclass can live in a library without MaterialX headers, which Gui
is. `Gui::ShaderGraphHost` (`src/Gui/ShaderGraphHost.cpp`) is that
subclass and RENDERS the preview: the material icon's sphere wearing
the edited document, as a Coin scene run through the render-cache
pipeline (`SoFCRenderCacheManager::traverse` + `RendererBridge::
translate`) and handed to the document's live 3D view backend as a
transient capture scene (`setCaptureScene` + `renderOffscreen`, the
TechDraw shaded underlay's derived-shape path), read back and uploaded.
The render-cache pipeline lives in Gui, which is why the preview cannot
be the renderer's; and it means the preview needs a 3D view on the
renderer path -- without one the pane is simply absent. The async
compile the viewport waits for is read through two new `Renderer`
virtuals (`shaderCompilePending`, `shaderCompileGeneration`); a
stand-in frame polls until the compile lands. Phase 1 shipped a
`NullGraphHost` (no preview, no thumbnails, every nodedef implemented)
so the editor was usable before the engine half existed.

### 4.3 The ImGui surface

`GraphEditor/ImGuiSurface.{h,cpp}`: a `QOpenGLWidget` owning a
standalone `Render::DrawSurface` (`DrawSurface::create(widget, 1)`,
the CAM simulator's pattern -- `DlgCAMSimulator.cpp:597`, `paintGL` at
858 calling `beginFrame(w, h)` / `endFrame()`), one pass, drawn by
bgfx's ImGui renderer on that pass's view id (`BGFXDrawSurface::
viewIdOf(pass)`, `BGFXDrawDevice.cpp:673`; the surface class is inside
`FreeCADRenderer`, so the editor reaches it directly). The renderer's
`entry::` key map and `inputGetModifiersState` (`imgui.cpp:297-548`)
are replaced by Qt event translation: `mousePressEvent`/`Move`/
`Release` -> `io.AddMousePosEvent`/`AddMouseButtonEvent`, `wheelEvent`
-> `AddMouseWheelEvent`, `keyPressEvent`/`Release` -> `AddKeyEvent` over
a `Qt::Key` -> `ImGuiKey` table plus modifiers, `inputMethodEvent`/
key text -> `AddInputCharactersUTF8`, `focusOutEvent` -> `AddFocusEvent
(false)`. The widget takes `Qt::StrongFocus` and `setMouseTracking
(true)`; `devicePixelRatioF()` feeds `io.DisplayFramebufferScale` and
the editor's `setFontScale`. ImGui's `.ini` is disabled (`io.IniFilename
= nullptr`); the node editor's own settings go through
`ed::Config::SaveSettings`/`LoadSettings` into the program's document
or nowhere (positions are already in the document).

A frame is drawn on `paintGL` only; the widget calls `update()` on
every input event and while `compiling()` or a drag is live, so an
idle editor costs nothing. The ImGui context is one per
`ImGuiSurface` (`ImGui::CreateContext` / `SetCurrentContext` around
each frame), since two program tabs may be open.

### 4.4 The view, its placement, and the commands

`Gui::ShaderGraphView : MDIView` (`src/Gui/ShaderGraphView.{h,cpp}`,
the `TextDocumentEditorView` shape): owns an `ImGuiSurface`, the ported
`Graph`, a `GraphHost`; watches the program's `FragmentProgram` and
`Label` through `fastsignals` connections so an outside change (undo,
Python) reloads the document, and the object's deletion closes the
view. `getName()` = `"ShaderGraphView"`; `onMsg` handles `Undo`/`Redo`
by forwarding to the document, as the text view does.

**Placement is the shared policy, not the view's business.** The
opener follows `ViewProviderTextDocument::doubleClicked`
(`src/Gui/ViewProviderTextDocument.cpp:85-150`) line for line:

- `activateView()` first -- rule 0, reveal-if-open: a view already
  open for this program is activated (`ViewPlacement::reveal`), never
  duplicated.
- Otherwise construct the view with `getMainWindow()` as parent and
  hand it to `ViewPlacement::place(view, Category::DocView, guiDoc)`.
  That call reads `BaseApp/Preferences/View/OpenView`; the Document
  view category defaults to **Split** (ViewPlacement.md 3.1), so by
  default the graph opens as a cell of the document's `ViewArea`
  beside the 3D view -- in the last-used non-3D cell if the user has
  one (a page or sheet cell), splitting otherwise, never over a 3D
  cell -- and as a tab only when the user set that or holds Alt.
  `placeTab` is reserved for restore and is not called here.
- `ViewProviderShaderProgram::getMDIView()` returns the open view for
  its object (scan `getMDIViewsOfType(ShaderGraphView::getClassTypeId
  ())`, as the text provider does) and `show()` opens one when none
  exists. These two are what the split-view area keys on: the cell's
  content menu lists every object whose `getMDIView()` is non-null
  (`ViewArea.cpp:507-530`), and a saved layout names the cell by
  `O:<object name>` and rehydrates it through `getMDIView()` then
  `show()` (`Document.cpp:2878`).
- The view's Qt `objectName` is the program's internal name
  (`MDIViewPage::setDocumentObject` sets it the same way): the layout
  save derives the `O:` token from it (`Document.cpp:3676-3699`) and
  a provider-map scan is explicitly not trusted there.

Two doors open it: `ViewProviderShaderProgram::doubleClicked` (new) for
any MATERIALX-dialect program in the tree, and
`DlgDisplayPropertiesImp::onEditShaderGraph`, which after
`Materials.materializeShaderGraph(...)` resolves
`ShaderGraph::materialized(obj)` -> `ShaderGraph::programOf(binding)`
and calls the same opener on that program's view provider. The Alt
inversion is suppressed on the button path only if the button ever
gains an Alt shortcut (`SuppressAltInversion`).

The menu bar the editor draws (`graphButtons`: File / Graph / Viewer /
Options / Help) loses File (no load/save: the property is the file)
and Viewer's Load Geometry; Graph (Auto Layout) and Help stay, and a
Surface menu is added (phase 3, section 13) when the document names
two or more renderable surfaces. The node-editor canvas, property
panel and search popup are unchanged.

### 4.5 Where the code goes

```
src/3rdParty/imgui-node-editor/            new submodule (thedmd/imgui-node-editor, sec 10)
src/Gui/Renderer/GraphEditor/
    ImGuiBgfx.{h,cpp}                      bgfx's example ImGui renderer, per instance, entry:: removed
    ImGuiStb.cpp                           the stb_truetype/stb_rect_pack implementation (sec 10)
    ImGuiSurface.{h,cpp}                   QOpenGLWidget + DrawSurface + Qt input bridge
    GraphEditorWidget.{h,cpp}              the editor's widget: an ImGuiSurface drawing the canvas
    GraphHost.h                            the interface of 4.2, with NullGraphHost inline (phase 1)
    EngineGraphHost.{h,cpp}                phase 2: preview + thumbnails on the engine
    Graph.{h,cpp}, UiNode.{h,cpp},
    Layout.{h,cpp}                         ported copies (RenderView/FileDialog/Main gone), in
                                           namespace Render::GraphEditor
    UiProperties.{h,cpp}                   UIProperties/getUIProperties copied from MaterialXRender
    ImGuiStdlib.{h,cpp}                    InputText over std::string (bgfx's ImGui has no misc/cpp)
    NodeEditorDrawing.{h,cpp},
    NodeEditorWidgets.{h,cpp}              the two blueprints-example utilities, copied
src/Gui/ShaderGraphView.{h,cpp}            the view (an MDIView; placed by ViewPlacement)
src/Gui/Renderer/DrawSurface.h             nativePassId(pass): the backend's own id of a pass
```

`DrawSurface::nativePassId` is the one addition to the facade: the ImGui
renderer draws with transient buffers and embedded shaders, which the
facade does not carry, so it submits to bgfx directly on the surface's
pass id. Only code inside the renderer library can make use of it.
`FC_SHADER_GRAPH_EDITOR` is the compile-time switch, set by both
`src/Gui/Renderer/CMakeLists.txt` and `src/Gui/CMakeLists.txt` when
`BUILD_BGFX AND BUILD_MATERIALX`; `Gui::ShaderGraphView::init()` runs
in `Application.cpp` under it.

CMake: a `BUILD_MATERIALX`-guarded block in
`src/Gui/Renderer/CMakeLists.txt` (the `HAVE_MATERIALX` block at line
84 is the hook) adds the `GraphEditor/` sources, the node editor's
sources, and bgfx's `dear-imgui` with bgfx's `imconfig.h` include
path; the `.bin.h` shaders are embedded, so no `shaderc` step. Without
`BUILD_MATERIALX` the view is not built and the button stays what it
is today. The wasm viewer's `wasm/CMakeLists.txt` does not include any
of it until phase 4.

## 5. What is deliberately not done

- Editing the card's graph in the library (Materials editor). The
  editor edits programs; the return leg to a card is
  `Material_SaveToLibrary` (17.12 item 3), unchanged.
- A per-face document (17.11 option 1) -- storage round-trips, the
  consumer gap warns, nothing here changes that.
- IME composition beyond `inputMethodEvent` text commits.
- The browser viewer (phase 4): today the viewer receives PRECOMPILED
  shader binaries and no MaterialX at all (17.23).

## 6. Prior art

- **MaterialX Graph Editor** (ASWF, ImGui + imgui-node-editor): the
  source of the port.
- **QuiltiX** (Prism Pipeline; PySide + NodeGraphQt + PyMaterialX): a
  Qt-native MaterialX editor with a hydra/MaterialX viewer. Its UX --
  material list on the left, node search on Tab, live preview --
  informs phase 3, not the code.
- **Blender's shader editor, Unreal's material editor, Houdini's
  LookdevX**: one datablock shared between users and a per-object edit
  makes a copy -- the COW rule CyclesIntegration.md decision 1a already
  adopted; the editor edits the materialized copy.
- **The fork's own CAM simulator and TechDraw page**: a standalone
  `DrawSurface` in a `QOpenGLWidget`, the hosting pattern reused here.

## 7. Build order, each its own commit

0. **Spike -- DONE 2026-09-03, section 10.** Add `imgui-node-editor` as a submodule; compile bgfx's
   ImGui + the node editor into `FreeCADRenderer` behind
   `BUILD_MATERIALX`; `ImGuiSurface` with Qt input; a throwaway
   `ShaderGraphView` showing the node editor's canvas with two dummy
   nodes and a link, opened through `ViewPlacement::place(...,
   DocView, ...)` so it lands in a split cell beside a live 3D view
   (and in a tab under Alt). Exit criteria: it draws, drags, types
   into an ImGui text field, survives being split, maximized, closed
   from the cell menu and reopened, survives a second instance for a
   second program, and the 3D cell keeps rendering next to it. This is one day; it answers section 8.
1. **The port -- DONE 2026-09-04, section 11.** `Graph`/`UiNode`/`Layout` copied in, `RenderView`
   replaced by `GraphHost` (with `NullGraphHost`), `FileDialog`/`Main`
   gone, the document round trip of 4.1, the view and the two commands
   of 4.4. Exit criteria: materialize a bundled MaterialX card, open,
   add a node, link it, watch the viewport change, undo it from Edit >
   Undo, revert. `TestShaderGraph.py` gains nothing (the primitives
   are unchanged); a GUI probe under Xvfb (`gui-tests-xvfb`) opens the
   view and writes one edit.
2. **`EngineGraphHost`.** Preview sphere on the icon path, redrawn when
   the async compile lands; thumbnails; `Param_*` two-way (a
   property-editor edit reaches the panel through the `FragmentProgram`
   watch -- values are in the text -- and a panel drag writes the
   property).
3. **Polish -- DONE 2026-09-04 except the two last items, section 13.**
   Read-only library nodegraphs (`readOnly()` exists), the `Surface`
   picker, HiDPI font scale, a path-traced preview through the Cycles
   consumer (not built), a dock option if asked for (not asked).
4. **The browser viewer.** MaterialX Core/Format/GenGlsl to wasm
   (`JsMaterialX` proves the emscripten build), the same `GraphEditor/`
   sources in the viewer with a host that sends the edited text back
   over the SceneServer control channel as one more op beside `cycles`
   (SceneStreaming.md). Nothing in phases 0-3 is redone for it.

## 8. Risks, settled by the spike

1. **ImGui version skew.** `imgui-node-editor` includes
   `imgui_internal.h` and tracks upstream closely; MaterialX's editor
   code calls at least one API 1.92 has retired
   (`ImGui::IsKeyPressedMap`, `Graph.cpp:3839`); bgfx's copy is patched
   (`imgui_user.h/.inl`, bx allocators, obsolete API off). The spike
   picks the node-editor commit and lists the touch-ups the port needs.
   If the node editor cannot build against bgfx's ImGui, the fallback
   is a second, unpatched ImGui for the editor alone -- two ImGui
   copies in one process are fine as long as nothing links both into
   one translation unit -- and bgfx's renderer recompiled against it.
2. **A second bgfx consumer beside the 3D views.** bgfx is
   single-threaded and main-thread here (`BGFXRenderer.cpp:1384`);
   every 3D view holds an id block from the same pool (RenderEngine.md
   3.1); the desktop compositor is GL-only (`BGFXRenderer.cpp:1465`).
   The CAM simulator and the TechDraw page already prove a standalone
   surface beside 3D views; the spike proves it for a widget that
   wants keyboard focus and redraws on every event.

### 8.1 Settled by the spike (2026-09-03)

Risk 1, the touch-ups against ImGui 1.92.8, all found by the compiler
or the first frame -- none by reading:

- `imgui-node-editor` master (`021aa0e`, 2026-02-20, "fixing for
  modern imgui") builds against 1.92.8 with ONE change: ImGui 1.92.7
  (`IMGUI_VERSION_NUM` 19270) added `operator*(float, ImVec2)` to its
  math-operator set, and `imgui_extra_math.{h,inl}` redefined it. Guarded
  with `# if IMGUI_VERSION_NUM < 19270`, the style of the file's own
  `< 19002` / `< 18955` guards. Upstream `develop` has the same
  unguarded line. Commit `b683192` on the submodule's `LinkVibe`
  branch; see section 10 for where that commit has to live.
- Dear ImGui itself needs nothing: bgfx.cmake compiles bgfx's patched
  copy into `example-common` (as `${DEAR_IMGUI_SOURCES}`), which
  FreeCADRenderer already links, so the editor gets ImGui for free.
- Two things in that arrangement bite, both link-time or first-frame:
  (a) bgfx's `imconfig.h` disables ImGui's stb implementations and
  expects the example renderer to provide them, and the example's
  `STBTT_malloc` goes through a global allocator that only its
  `imguiCreate()` sets -- the first glyph rasterized without it is a
  null dereference inside `stbtt_GetGlyphShape`. (b) The example
  object is pulled into the link anyway, by ONE symbol: bgfx's
  `widgets/file_list.inl` calls `ImGui::PushFont(Font::Enum, float)`,
  which only the example defines. `ImGuiStb.cpp` answers both: it
  defines that `PushFont` and the two stb implementations from the
  same bgfx headers, so the linker never reaches the example object.
- The MaterialX editor's `IsKeyPressedMap` call (Graph.cpp:3839) is
  not reached by the spike; it is phase 1's, and `ImGui::IsKeyPressed`
  is the replacement.

Risk 2 did not bite: a `DrawSurface` with one pass beside a live 3D
view draws, takes keyboard focus, and repaints on every event; the 3D
cell keeps rendering next to it (section 10).

## 9. Day-one checklist for the coding session

- Read this doc, MaterialStorage.md 17.11-17.13, RenderEngine.md 5,
  CAMSimRenderPort.md 3 and 8 (the draw facade), `DrawSurface.h`,
  `BGFXDrawDevice.cpp:647-760` (`BGFXDrawSurface`), and
  `bgfx/examples/common/imgui/imgui.cpp`.
- `git submodule add https://github.com/thedmd/imgui-node-editor
  src/3rdParty/imgui-node-editor` (the fork's own submodule, at a
  commit that builds against ImGui 1.92 -- check its `imgui_node_editor.
  cpp` against `IMGUI_VERSION_NUM`); MaterialX's nested submodules stay
  unfetched.
- Read `docs/ViewPlacement.md` sec 3-4 and `docs/SplitViews.md` sec
  5.2/5.5 before writing the opener; the text document's provider is
  the template, the page's `createMDIViewPage` the second reference.
- Build with the standard preset only (`conda-relwithdebinfo-801`,
  `build-wait-harness`); the spike's view is opened from the Python
  console (`Gui.getMainWindow()`) under Xvfb with `QT_QPA_PLATFORM=xcb`
  and `ShowNaviCube=False`.
- Commit the spike as its own commit even if it is later deleted; the
  touch-up list from risk 1 goes into section 8 of this doc.

## 10. Phase 0 result (2026-09-03)

Built and run on the standard preset, under Xvfb (`gui-tests-xvfb`),
by a probe kept outside the repo
(`~/works/sw/fcad-probes/shader_graph_spike.{py,sh}`). Every exit
criterion of section 7 phase 0 held, in one process, with a clean
exit and no crash log:

- Opened through the provider door (`ViewObject.doubleClicked()`,
  the tree's path), the view lands as a split cell beside the 3D
  view: the 3D cell went from 1178 to 587 px wide, the editor drew
  its first frame at 587x623 on its own pass id, and the 3D cell
  still rendered after a later recompute.
- It draws (menu bar, label, a text field, two nodes and a link on
  the node-editor canvas), a node drags with the link following, and
  a text field takes typing -- after one fix (below).
- A second program opens a second instance; by the placement policy
  (ViewPlacement.md 3.2 step 6) it ADOPTS the most-recent non-3D cell
  and the first view goes through its close path, so the count stays
  at one view; a second double-click on an open program reveals it
  rather than duplicating; close and reopen work; deleting a program
  whose view is open closes the view.

What the run found and fixed:

- **Typing dropped characters** ("typed42" arrived as "pe2"), and it
  was not ImGui: the widget never received the key PRESSES, only the
  releases. Qt's shortcut map sees a press first, and a key that
  starts one of the application's multi-key shortcut sequences is
  consumed there. `ImGuiSurface::event` now accepts
  `QEvent::ShortcutOverride` -- everything while a text field is
  active or an item is being dragged, and any unmodified key
  otherwise; a Ctrl/Alt/Meta chord with no field active stays with
  the application so undo/redo/save keep their meaning. The same
  rule QLineEdit applies.
- The view type has to be registered (`ShaderGraphView::init()` in
  `Application.cpp`), or `getMDIViewsOfType` -- and with it the
  provider's own reveal-if-open -- finds nothing.
- The two link-time traps of 8.1.

What the spike does NOT prove: HiDPI (Xvfb is 1x; the framebuffer
scale path is written but unexercised), a real GPU (llvmpipe), cell
maximize, and the cell content menu (exercised through `close()`
only). `QWidget::grab` of the main window shows the GL widget's text
doubled; `grabFramebuffer()` of the widget is crisp, so that is the
grab path, not the frame -- an on-screen check is still owed.

**The submodule's home** is `realthunder/imgui-node-editor`, the
fork's pattern for every vendored engine (bgfx.cmake, vg-renderer,
cycles): branch `LinkVibe` carries the 8.1 guard (`b683192`) on top
of upstream master, `.gitmodules` names both, and the local checkout
keeps upstream as the `upstream` remote for later rebases.

Phase 1 starts from here: `GraphEditorWidget::drawUi` is the body
the ported `Graph` replaces, `ImGuiSurface` and `ImGuiBgfx` are
final, the view and the provider hooks are final in shape.

## 11. Phase 1 result (2026-09-04)

The port is in: `Graph`/`UiNode`/`Layout` copied from MaterialX 1.39.5
into `GraphEditor/` (namespace `Render::GraphEditor`, so the global
`Graph`/`Link`/`Layout` names stay inside the library), `RenderView`,
the file dialogs and `Main` gone, `GraphHost` in their place with the
inline `NullGraphHost`, the blueprints `drawing`/`widgets` utilities
beside them, and two small units the plan did not foresee:
`ImGuiStdlib` (bgfx's ImGui copy has no `misc/cpp`) and `UiProperties`
(`getUIProperties` is MaterialXRender's, which the renderer does not
build; `getNodeDefInput` it needs is GenShader's). Every exit
criterion of section 7 held under Xvfb, one process, no crash,
probe `~/works/sw/fcad-probes/shader_graph_phase1.{py,sh}`:

- Open: the view draws the menu bar, the property pane and the
  canvas with the document's two nodes laid out; opening writes
  nothing (text unchanged, no undo step).
- Edit: a rubber band over the canvas plus Delete empties the
  document (one undo step, the provider's sync reports "no surface"
  in the report view, which is the text reaching the viewport path);
  Tab, a typed filter, Down, Return adds a `constant` node (one undo
  step); a node drag writes `xpos`/`ypos`.
- Undo/Redo from the application walk the text back and forth and
  the editor reloads each time; a Python write of the property
  reloads it too; the 3D cell keeps rendering; close works.

What the run found and fixed, none of it visible by reading:

- **A data library is not a separate document to the accessors.**
  With `setDataLibrary` attached, `Document::getNodeGraphs()`,
  `getNodes()`, `getActiveInputs()` and `getOutputs()` answer the
  LIBRARY's children too (that is how nodedefs resolve), so the first
  graph showed 267 nodes: every library nodegraph in one column, and
  Delete on them removed nothing from our document. The MaterialX
  editor filtered these by source URI against its xinclude set; the
  port filters by owning document (`elem->getDocument() == _graphDoc`).
- **`IsWindowFocused(ImGuiFocusedFlags_RootWindow)` is an identity
  test** of ImGui's nav window against the root, and a click on the
  canvas focuses the node editor's CHILD window, so every keyboard
  action on the canvas (Delete, Tab, arrows) was dead in this hosting.
  `canvasHasKeyboard()` asks for the root's own hierarchy without
  popups, and no active item (so Backspace in a name field does not
  delete nodes).
- **Change detection is generic, not per site.** The first draft
  flagged edits where the MaterialX code flagged a preview recompile,
  and selecting a node recompiles the preview -- so a rubber band
  committed. Now a gesture end (a mouse or key release, or a settled
  topology update, with nothing held and no text field active) writes
  positions, serializes, and commits only if the text differs from the
  last committed text. The opening auto-layout re-baselines so the
  first click costs nothing; the user's Auto Layout is an edit.
  Comparing texts also absorbs the node editor handing positions back
  a fraction off the value set.
- `strToPython` escapes quotes and backslashes but not newlines: a
  multi-line document made an unterminated Python literal. The view
  builds the literal itself.
- ImGui 1.92 retired `SetWindowFontScale` (obsolete API is off in
  bgfx's copy); the port carries its four-line body. `IsKeyPressedMap`
  became `IsKeyPressed`; the node editor's math needs
  `IMGUI_DEFINE_MATH_OPERATORS` before ImGui's header, set for the
  directory in CMake.

Not done, by design of phase 1: no preview, no thumbnails (the
`NullGraphHost`), the filename input is typed rather than browsed
(the program's `Images` is what it should pick from, phase 2), the
`Surface` picker, HiDPI (font scale is 1). Phase 2 starts at
`EngineGraphHost`.

## 12. Phase 2 result (2026-09-04)

`EngineGraphHost` and `Gui::ShaderGraphHost` are in, split as 4.2 now
describes: the renderer half owns textures and answers the generator,
the Gui half renders. Four things about the build that were not in
the plan:

- **The preview cannot be the renderer's.** The sphere is a Coin scene
  because that is what the render-cache pipeline translates, and that
  pipeline (`SoFCRenderCacheManager`, `RendererBridge`) is Gui code,
  not renderer code. So the host that renders is in Gui, and the
  renderer-side class is the MaterialX-free base it derives from --
  `EngineGraphHost.h` names no MaterialX type, the `GraphHost` the
  editor talks to is an adapter inside its `.cpp`, and Gui, which has
  no MaterialX headers, subclasses it freely. The material icon's own
  renderer (`MatGui::IconScene`) is a hidden `View3DInventorViewer`
  in the Material module, which Gui cannot link either; the preview
  borrows the document's live 3D view backend instead, through the
  transient capture scene the TechDraw shaded underlay already uses
  (`setCaptureScene` + `renderOffscreen`, read back and uploaded).
  Without a 3D view on the renderer path there is no preview pane.
- **A rendered preview is never made inside the editor's paint.** The
  ImGui surface is itself a bgfx frame being encoded during `paintGL`;
  a capture frame there would flush half the UI into the capture.
  `preview(w, h)` only records the size and returns the texture it
  has; the render runs from the event loop on a 40 ms coalescing
  timer, and the widget is asked for a frame when the texture lands.
- **A declared input's uniform is ZERO unless the shader carries a
  parameter for it.** The engine seeds no defaults
  (`pushUserParams` zeroes every uniform absent from `params`), and
  the viewport only works because the provider always binds the
  `Param_*` properties. The preview's draw therefore carries
  `SoShaderParameterArray1f` nodes for the document's public inputs
  (`Render::MaterialX::publicInputs(xml)`, new, parses against the
  attached library without importing it), and the shader node's text
  is the live document with those inputs set BACK to the values of the
  text the editor was loaded with (`setBaseText`,
  `applyInputsToDocument`). A value drag then moves a uniform and
  never regenerates or recompiles -- the generated source folds only
  UNDECLARED values as constants, so an undeclared value drag still
  costs a generation per step, as it does in the viewport.
- **A bgfx texture created WITH its data is immutable.** The first
  preview uploaded through `createTexture2D(..., data)` and every
  later one through `updateTexture2D`, which bgfx drops on an
  immutable texture without a word: the probe saw the parameter edit
  reload, the orbit move the camera and each render complete, and the
  pane never changed. The preview texture is created empty and
  written; the same bgfx rule applies to any texture a consumer means
  to update.
- **The compile state crosses the `Renderer` interface**, two virtuals
  (`shaderCompilePending`, `shaderCompileGeneration`) over
  `userShaderInflight` / `userCompileGeneration`. A preview rendered
  while a compile was in flight shows the stock stand-in and the
  editor's "Compiling Shaders" popup, and the host polls the backend
  every 250 ms until the generation moves, then renders again.

`Param_*` two-way: the view loads the editor with the property values
written into the text (`documentForEditor`), reloads on a `Param_*`
change, and a commit writes, in the same transaction as the text,
every `Param_*` property whose public input the text now states
differently (`writeParams`, through `inspect`'s input list). The
filename field of an image input has a picker over the program's
`Images` beside it.

Verified under Xvfb (llvmpipe, `~/works/sw/fcad-probes/
shader_graph_phase2.{py,sh}`), one process, no crash, the pane measured
on sphere pixels only: the striped enamel
(`scripts/materialx/fc_declared_interface.mtlx`) opens with its blue
base at the bottom of the ball (the band is one broad stripe at
`stripe_scale` 1.2); `Param_base_color = red` from Python reloads the
editor and the bottom turns red with the text untouched; an orbit
drag in the pane changes the picture and writes nothing; a wheel step
grows the ball by half with nothing written; Std_Undo of the property
edit turns it blue again; a rubber band still commits nothing; a
second program opens with its own preview (the OpenPBR default,
grey). The compile was disk-cached in every run, so the stand-in +
poll path executed (pending false, generation 0) but was never SEEN
compiling; thumbnails were not probed (a node has to be selected for
the pane to show one) and are verified by reading. Not proven: HiDPI,
real GPU, the look of a MaterialX material under the viewer's PBR
environment (the capture takes the viewer's settings, so it is
whatever the viewport shows), the pane after the last 3D view closes
(the probe's editor was not visible by then; the code clears the
preview and the pane goes).

Open after phase 2: `materialXVariants` and the program cache grow by
one entry per distinct text (a topology edit, or an undeclared value
drag) for the session; the preview borrows a 3D view and renders eight
settle frames per update, which is fine for a 373 px pane and would
not be for a large one; the `Surface` picker (phase 3) is still a
property edit.

## 13. Phase 3 result (2026-09-04)

Opened on the chess set first (`standard_surface_chess_set.mtlx` over
`chess_set.glb`, a card per piece, `Materials.materializeShaderGraph`
on one piece for the program), which is what phase 3 was scoped by:
the screenshot showed the preview ball a flat ivory beside a marbled
piece.

- **The preview sphere carries UVs now.** A MaterialX image node
  samples unit-0 texcoords; Coin's sphere generator states one per
  vertex, but `SoFCVertexCache` keeps them only while a texture unit
  is enabled on the capture traversal, which nothing in the preview
  scene does -- so every fragment sampled texel (0, 0). The ball is a
  `SoSphere` subclass carrying the `forceTexCoords` field the cache
  reads (the `SoTextImage` / `SoDatumLabel` pattern), and the chess
  materials show their base colour, roughness and normal maps on it.
- **The Surface menu.** A document carrying an asset's whole material
  set is viewed as ONE surface (the program's `Surface`). The menu
  bar lists the document's renderable surfaces in document order when
  there are two or more, the worn one checked; a pick writes the
  property in one transaction from the event loop (the pick is made
  inside the editor's frame and the write reloads the editor), so
  Edit > Undo takes it back. `GraphHost` / `EngineGraphHost` carry
  `surfaceNames` / `currentSurface` / `selectSurface` with defaults
  that draw no menu; the Gui host answers from one `inspect` per
  distinct text, cached. The reload puts the canvas back at the
  overview.
- **Read-only library nodegraphs needed nothing.** A double-click on a
  `standard_surface` node opens its library implementation
  (`NG_standard_surface_surfaceshader_100`) and `readOnly()` -- the
  element's source URI against the document's, which a document read
  from a string has empty -- already refuses Delete (the "Read Only"
  popup) and writes nothing on a drag. Verified, not changed.
- **F frames AND zooms** (`NavigateToSelection(true)`): the stock
  call only centred, and a node picked in the fifteen-material
  overview stayed a thumbnail.
- **HiDPI needed nothing in the editor.** ImGui 1.92 with a backend
  that has `RendererHasTextures` (ours) takes the font raster density
  from the framebuffer scale, so at `QT_SCALE_FACTOR=2` on a 4K Xvfb
  screen the text is crisp, the layout is logical, and a click lands
  on its node. Open: the preview is rendered at the pane's LOGICAL
  size and upscaled (soft at 2x; `preview(w, h)` could ask for
  physical), and the 3D view itself drew in its bottom-left quadrant
  under that scale factor -- the viewer's, not the editor's, noted in
  the session memory and not chased here.
- **Warnings print when they appear, not per parse.** The editor
  writes the text per gesture, each write a parse in the provider and
  a variant in the renderer, and the "translated to OpenPBR" note of
  a standard_surface document printed on every one and raised the
  report view each time. The provider prints a warning absent from
  the previous parse's list; the renderer prints each distinct
  message once. Not part of the editor, but it is what made the
  editor unusable on that document.

Not built: the path-traced preview through the Cycles consumer (a
second render path into the same pane; nothing in the host forbids
it) and the dock option (not asked for). The probe is
`~/works/sw/fcad-probes/shader_graph_chess.{py,sh}` (`PROBE_HIDPI=1`
for the 4K run): it finds node boxes by pixel projection, zooms with
the wheel, dives with a double-click, and reads the menu by a fixed
click. ctest 473/473 after phase 3.

## 14. The open items of sec 13 (2026-09-04)

Four of the five, in the order of their value; the fifth (the
path-traced preview through the Cycles consumer) is a design item and
was not started.

- **The 3D view under a HiDPI scale factor.** The bgfx backend sized
  its view from the host widget's `width()`/`height()`, which are
  Qt's LOGICAL pixels, and blitted that frame into the widget's
  default framebuffer, which is physical -- so at `QT_SCALE_FACTOR=2`
  the frame landed in the bottom-left quadrant and the rest stayed
  black (the probe's four-quadrant sample read three of them dark).
  `BGFXRendererLibP::framebufferWidth/Height` (logical times
  `devicePixelRatioF`) now size the view and bgfx's init resolution,
  and the Diligent backend got the same two lines. Everything else
  already spoke physical: Quarter's viewport region, the viewer's
  pick coordinates, the canvas cell rects, the blit's sub-view
  offsets. Verified: all four quadrants carry the scene at dpr 2, the
  1x frame unchanged.
- **The preview at physical size.** `Graph` asked `preview(w, h)` at
  the pane's ImGui size; it asks for that times
  `io.DisplayFramebufferScale` now, so the host renders the sphere at
  the pixels the pane actually has, and `GraphHost::preview` states
  the unit. At 1x nothing changes.
- **A Surface pick keeps the navigation.** `Graph::setDocument`
  records where the user is before the reload -- the nodegraph levels
  below the root by their element name path (and whether each is a
  compound graph or a functional implementation) plus the selected
  node's name -- and `restoreNavigation` re-enters them one level per
  settled layout, through the node that opens each (the dive's own
  body, now `enterNodeGraph`, shared with the double-click; the
  read-only popup is not repeated). One level per layout because a
  dive saves the positions of the level it leaves, and those exist
  only once that level is laid out. With the levels back the
  selection is restored and `_keepView` suppresses the reload's
  `NavigateToContent`, so the canvas stays at the user's zoom and
  scroll -- the node editor keeps its view across our reload, we only
  had to stop asking it to reframe. A level the new text no longer
  has (an undo that removed the nodegraph) ends the descent there and
  frames what is left. This covers every reload: the Surface pick,
  Edit > Undo, a write from Python.
- **The variant and program caches are bounded.** Every lookup stamps
  its entry with the lib's `frameSerial`; `sweepUserCaches`, once per
  frame after `bgfx::frame`, drops entries not looked up for 120
  frames once a map is past its cap (32 variants, 64 programs). Below
  the cap nothing is touched, so a scene's own material set is never
  churned by an object out of view; what this frame used is never
  dropped; and every user of a program handle re-resolves it per
  frame and holds it only within the frame (checked at all eight
  sites), which is what makes a drop after the frame safe. A program
  owns its two shader handles (`createProgram(..., true)`), so the
  session of editing that grew toward bgfx's 512 shader handles now
  plateaus. The eight settle frames per preview update stay: they are
  what the temporal passes need, and the pane is small.

Probe: `shader_graph_chess.py` gained the four-quadrant sample of the
3D view, a report-view scrape of the preview's render size (FC_LOG
lands there, not on stdout; `checkLogging` on), and a layout compare
across the Surface pick.

## 15. The path-traced preview (2026-09-04)

The fifth item of sec 13, the one sec 14 left as a design item: the
same preview pane, rendered by the Cycles path tracer instead of the
raster backend, chosen from the editor's menu bar. Nothing in the
host forbade it; this section says how the pane's sphere scene reaches
the Cycles consumer, what owns the session, how its frames land in the
preview texture, and where the toggle lives. Written before the code.

### 15.1 Prior art

- **Substance 3D Designer 15** replaced its OpenGL and Iray viewers
  with one 3D view renderer that has a rasterizer mode for the live
  edit and a path tracer mode for the accurate look, over the same
  material and the same scene, switched in the view. That is the
  shape wanted here: one pane, two engines, the raster one the
  default because a drag must answer at once.
- **Blender's material preview** renders its preview scene with the
  scene's own engine, Cycles included, as a background job whose
  refining frames replace the thumbnail as they arrive. The preview
  is never the viewport's session: it is a scene of its own.
- **MaterialX's own graph editor** has only its GL viewer; there was
  nothing to port.
- **In this repo** the pattern exists twice. `FrameStream`
  (`CyclesStream.cpp`, CyclesIntegration.md sec 7.1) is a host that
  DRAWS NOTHING: it owns a `Viewport`, feeds it a scene and a camera,
  and pulls each staged frame through `Viewport::takeFrame`, which was
  added for exactly that -- a consumer with no backend to blit into.
  And `View3DInventorViewer::Private::feedCyclesViewport` is where a
  render-cache scene becomes a `SceneInput` with the view's PBR,
  bump, output, light, section and background configs.

### 15.2 The scene

The raster preview (sec 12) already builds the whole answer: the
material icon's two-unit sphere carrying the edited document as a
`material`-stage `SoShaderProgram` with the live public inputs as
parameters, traversed by a `SoFCRenderCacheManager` and translated by
`RendererBridge::translate` into a `DrawCallList`. The tracer takes
THAT list. The document crosses on the draw's `UserShader` the way it
crosses in the viewport (`SceneTranslator::materialXShader`,
CyclesIntegration.md sec 6.9), the stored images as the absolute paths
`documentForRender` already substitutes, the input values as
`user.params` -- so a value drag, which only moves uniforms on the
raster path, is a new shader key on the Cycles path and the recycle
pool re-graphs a parked node (sec 6.9's pool, verified on the
`Param_roughness` sweep in session 16).

One thing changes: the cache manager becomes a member of the host
instead of a local of each render. The raster path could afford a
fresh manager per render because its capture scene is transient; the
tracer's restate (CyclesIntegration.md sec 5.10) keys meshes on the
vertex cache's `cacheId`, and a fresh manager would hand it a new
sphere every render -- a mesh rebuilt and a session reset per camera
motion. Persisted, the sphere is translated once and every later
`setScene` finds it in place.

The configs come from the document's 3D view, as the raster path
takes its backend from one: the config half of `feedCyclesViewport`
moves into a public `View3DInventorViewer::cyclesSceneConfig
(SceneInput &, const QColor &background)`, and the options block of
`syncExternalShading` into a public `cyclesViewportOptions()`, and the
feed, the sync and the editor call them. The environment, the light
rig, the output transform and the background of the preview are then
the view's, which is what the raster preview shows too. A document
with no 3D view open has no preview in either mode.

### 15.3 The session and its frames

One `Render::Cycles::Viewport` per editor host, created when the user
picks Path traced with the view's effective options, destroyed when
they pick Raster or the editor closes. It is registered with NO
backend: the host draws nothing.

Each preview render in traced mode compares a scene signature -- the
rendered text, the surface, the parameter values -- with what the
tracer was last given: different, `setScene` (a restate in place, a
reset only if the translation changed something); the same,
`setCamera` alone, which is the cheap throttled path a camera drag
wants. The camera is the same Coin camera's matrices the raster path
builds, at the pane's framebuffer size.

Frames arrive the way the stream takes them. Cycles' threads call the
redraw callback; it is marshalled to the host with a queued call on
the host QObject (a widget on its way out drops it), which starts a
30 ms coalescing timer; the timer's slot calls `takeFrame`, composites
the premultiplied linear half4 over the view's background, encodes it
to sRGB exactly when the scene is colour managed -- the same bytes the
raster readback holds -- and hands it to `setPreviewImage` bottom-up
as GL reads a framebuffer. The stream's `composite` becomes the shared
`Render::Cycles::compositeFrame`, with the channel count and row order
as arguments: the stream wants top-down RGB8 for JPEG, the pane
bottom-up RGBA8. The status the engine reports ("Sample 64/256",
"Rendering Done") is shown where "Compiling Shaders" is, so the user
sees the refinement; the compiling flag itself is false in this mode,
there being no shader compile to wait for.

Switching to traced keeps the raster frame on the pane until the
first traced one lands; switching back renders a raster frame at
once. The host's destructor resets the tracer before anything else:
the viewport's own destructor nulls its callback under its lock
before the session goes.

### 15.4 The toggle

`GraphHost` grows a preview mode contract beside the surface one:
`previewModes()` (names, empty by default), `previewMode()` and
`setPreviewMode(int)`. The Graph draws a `Preview` menu after
`Surface` only when there are two or more modes, with a radio item
per mode. `EngineGraphHost` carries the same three virtuals with the
same empty defaults; the Gui host answers `Raster` and `Path traced`
when the build carries the engine, and nothing otherwise, so a build
without Cycles draws no menu. The mode is per editor and starts at
Raster; it is not persisted -- a pane that path traces from the first
frame would surprise the next open of any program.

### 15.5 Not in this step

A device submenu (the device is the view's effective `Cycles_Device`,
the preferences underneath), pausing the session while the pane is
hidden, and GPU interop -- the frame crosses the CPU twice (half4 to
bytes, bytes to the texture), which a pane of a few hundred pixels a
side does not feel.

### 15.6 Result (2026-09-04)

Built as designed, two commits (`7cc395f2bc` the shared plumbing,
`683062fac0` the editor), and two things the design did not foresee.

- **The persistent cache manager captured nothing on its first pass.**
  Constructed at the top of `buildScene`, before `PreviewSphere::
  initClass()` registered the sphere's node class, the manager's first
  traversal came back with zero draws and, its root id unchanged, never
  traversed again -- the raster pane stayed empty until an edit. The
  per-render manager it replaced was always constructed after the
  scene, which is why the order never mattered before. Constructed
  after the nodes, the first pass has its draw. The manager's own
  gate (`sceneid == root->getNodeId()`) makes the persistence sound:
  every field the host sets on the shader node moves the root's id.
- **The "Compiling Shaders" note was an ImGui popup**, re-opened every
  frame while there was something to say. A popup blocks hovering of
  every other window and closes the menus, so the moment the status
  became permanent (a render mode's) the orbit drag and the menu bar
  went dead -- the first probe run saw the second menu never open.
  The note is a plain no-input overlay window now, for the compile
  case too.
- **The pane's background** is the same flat colour in both modes: the
  traced input leaves `envBackground` off (the film is transparent
  where the sky would be) and composites over the view's flat colour,
  which is what the raster path clears its offscreen frame to. Left
  as the config feed states it, the traced pane showed Cycles' own
  procedural sky over the view's gradient.

Verified under Xvfb on CPU at 32 spp (`~/works/sw/fcad-probes/
shader_graph_cycles.{py,sh}`, `PROBE_DEVICE=CUDA` for the GPU): the
raster pane first (blue enamel, 17111 sphere pixels of the 373x373
pane); Preview > Path traced, the first traced frame about a second
after the pick, settled at "Rendering Done, Sample 32/32" with 16888
sphere pixels and the pane background byte-identical to the raster
pane's; an orbit drag moved the picture through `setCamera` alone (the
log shows the camera renders without a retranslation) and added no
undo step; `Param_base_color` set to red re-traced the sphere red
through one restate; Preview > Raster brought the backend's frame
back, red; Path traced again started a second session; closing the 3D
view and then the editor with the session running left no crash. The
menu items sit where the probe clicks them (item rows 36 and 58 px
under the editor's top edge at 1x). ctest 473/473 after the change.

Open: the traced frame stays on the pane after the last 3D view
closes, since nothing invalidates the preview then (the raster pane
goes only because the cell re-layout resizes it); the session is
never paused while the pane is hidden; no device submenu. All three
are closed in sec 16.


## 16. The open items of sec 15 (2026-09-04)

The three sec 15.6 left, in the order of their value. All of them are
about the preview following what is actually there: the views that
configure it, the pane that shows it, and the device it runs on.

- **The pane follows the document's 3D views.** Everything the preview
  needs comes from a 3D view of the program's document -- the backend
  the raster path renders through, and the environment, light rig,
  output transform and tracer options the traced path states -- and
  nothing told the host when one came or went. The traced frame
  therefore stayed on the pane after the last view closed, with a
  session still holding its device for a picture that could no longer
  be refreshed. (The raster pane appeared to behave only because the
  cell re-layout happened to resize it, which invalidates by accident.)
  The host connects to the Gui document's `signalAttachView` and
  `signalDetachView` and re-reads the view list from the event loop:
  detach is reported BEFORE the view leaves the document's list, so a
  check made in the handler still finds the one that is going, and a
  queued call on this object dies with it if the whole document is
  closing. With no view left the session is parked and the pane
  cleared; the mode stays `Path traced`, so the next view to attach
  starts a session again. `Viewport::create` moved out of
  `applyPreviewMode` into `startTracer()`, called from the pick and
  from the render, which is also what makes a `Path traced` pick made
  before any view is open resolve itself later instead of warning and
  doing nothing. A device the engine refuses sets `tracerBlocked`, so
  the next thousand invalidations do not ask it again; a view
  attaching clears it.
- **The preview is held while its pane is hidden.** A session behind a
  cell showing another tab kept sampling -- a GPU or every core spent
  on a frame nobody paints -- and the raster path spent a backend
  frame per invalidation for the same nothing. The host filters the
  editor widget for `Show`/`Hide` and `ShowToParent`/`HideToParent`
  (the last two arrive when an ancestor is the one that moved) and
  reads `isVisible()` from them. Hidden pauses the session
  (`Viewport::setPaused`, which is `session->set_pause`) and stops the
  render and frame timers; `previewInvalidated` remembers the
  invalidation instead of answering it, and one render when the pane
  comes back answers everything that arrived meanwhile. A session
  created behind a hidden pane starts paused.
- **A device submenu under Preview.** `GraphHost` grows a device
  contract beside the mode one -- `previewDevices()`,
  `previewDevice()`, `setPreviewDevice(int)`, the same shape and the
  same empty defaults -- and the Graph draws a `Device` submenu under
  a separator inside the `Preview` menu when the current mode reports
  two or more. The Gui host answers the device TYPES this machine has,
  deduplicated as the view's `Cycles_Device` enum is built
  (`ViewportOptions.device` names a type, so two cards of one type are
  one entry), and only while the mode is traced: the raster mode draws
  through the view's backend and has no choice to make. The default is
  the view's effective device, which is what the submenu shows checked
  until a pick is made; a pick overrides it for this editor alone, is
  applied from the event loop as a mode pick is, and starts a fresh
  session on it. The view's own property is not written -- an editor
  trying the GPU should not change what the 3D view path traces with.

Two log lines were added with the submenu, because a device pick is
otherwise not observable: `tracer on <device>` when a session starts,
and `first traced frame <w>x<h>` once per session. Two devices tracing
the same scene to the same sample count settle on the same picture, so
pixels cannot tell them apart -- the evidence that a pick took effect
is that a session started on the named device and produced a frame.

Verified under Xvfb on CPU at 32 spp
(`~/works/sw/fcad-probes/shader_graph_cycles.{py,sh}`, extended for
these three; the wrapper runs the binary under `stdbuf -oL` so the
probe can count log lines while it runs, the report view holding none).
Hiding the editor's view logged the pane hidden and rendered NOTHING
while it was down, including for a `Param_base_color` edit made
meanwhile; showing it again logged the pane shown, rendered once, and
settled on the green sphere the edit asked for. Closing the last 3D
view emptied the pane within a second (134 stray pixels of 16464) and
logged the tracer parked once; `Std_ViewCreate` brought a view back and
the traced green sphere with it (12641 pixels in the re-laid-out pane),
with no pick or reload in between. `Preview > Device > CUDA` logged the
device, started a session on CUDA, took a frame from it and restated
the scene once. ctest 473/473 after the change.

Found here, fixed elsewhere: picking a device whose kernel cache is
cold used to freeze the application. Picking CUDA on this machine
starts an `nvcc` run of several minutes, and the next `stopTracer` --
closing the 3D view, switching back to Raster, closing the editor --
waited inside `Viewport`'s destructor for that thread to join, with the
GUI thread in it. The two commits after this section fixed it on both
sides: a released session is now destroyed on a reaper worker instead
of on the caller, and the kernel compile itself can be stopped and
leaves no torn cache behind. It is written up as
`docs/CyclesIntegration.md` sec 5.12, which is also where the one part
still open lives -- on Windows the compiler runs under `system()` and
the cancel only raises a flag, so a teardown during a first compile
still waits it out there.

Still open, from sec 15.5: GPU interop -- a traced frame crosses the
CPU twice (half4 to bytes, bytes to the texture), which a pane of a few
hundred pixels a side does not feel. Sec 7 phase 4 (the browser
viewer's editor) is recorded and deliberately not started.
