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

The desktop implementation (`EngineGraphHost`) lives in
`FreeCADRenderer` beside the bgfx code and answers: `documentChanged`
-> serialize and hand the text to the preview's program; `preview` ->
the icon path's transient sphere rendered into a bgfx texture (the
async compile the viewport already waits for signals completion,
`BGFXViewEffects.cpp:266`, stock media standing in meanwhile --
`compiling()` reads that); `thumbnail` -> `DrawDevice::createTexture2D`
from our image loader (`ImageDecode.cpp`); `hasImplementation` -> the
generator's `getImplementation` through `Render::MaterialX`. Phase 1
ships a `NullGraphHost` (no preview, no thumbnails, every nodedef
implemented) so the editor is usable before the engine half exists.

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
and Viewer's Load Geometry; Graph (Auto Layout) and Help stay. The
node-editor canvas, property panel and search popup are unchanged.

### 4.5 Where the code goes

```
src/3rdParty/imgui-node-editor/            new submodule (thedmd/imgui-node-editor, sec 10)
src/Gui/Renderer/GraphEditor/
    ImGuiBgfx.{h,cpp}                      bgfx's example ImGui renderer, per instance, entry:: removed
    ImGuiStb.cpp                           the stb_truetype/stb_rect_pack implementation (sec 10)
    ImGuiSurface.{h,cpp}                   QOpenGLWidget + DrawSurface + Qt input bridge
    GraphEditorWidget.{h,cpp}              the editor's widget: an ImGuiSurface drawing the canvas
    GraphHost.h                            the interface of 4.2
    NullGraphHost.{h,cpp}                  phase 1
    EngineGraphHost.{h,cpp}                phase 2: preview + thumbnails on the engine
    Graph.{h,cpp}, UiNode.{h,cpp},
    Layout.{h,cpp}                         ported copies (RenderView/FileDialog/Main gone)
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
1. **The port.** `Graph`/`UiNode`/`Layout` copied in, `RenderView`
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
3. **Polish**: read-only library nodegraphs (`readOnly()` exists),
   the `Surface` picker, HiDPI font scale, a path-traced preview
   through the Cycles consumer, a dock option if asked for.
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

**The submodule needs a home.** `.gitmodules` points at
`thedmd/imgui-node-editor`, but the recorded commit (`b683192`, the
8.1 guard) exists only in this checkout, on a local `LinkVibe`
branch. The fork's pattern is a `realthunder/` fork for every
vendored engine (bgfx.cmake, vg-renderer, cycles); creating
`realthunder/imgui-node-editor`, pushing that branch, and switching
the URL is the outstanding step, and a push -- so it waits for the
ask. Until then a fresh clone cannot resolve the gitlink.

Phase 1 starts from here: `GraphEditorWidget::drawUi` is the body
the ported `Graph` replaces, `ImGuiSurface` and `ImGuiBgfx` are
final, the view and the provider hooks are final in shape.
