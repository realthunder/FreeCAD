# Spreadsheet in the browser viewer -- tier evaluation

Ordered 2026-08-28, evaluated 2026-08-29. The question: a
`Spreadsheet::Sheet` has no remote representation at all. The browser
tier draws 3D scenes (SceneDump replayed by the wasm bgfx backend) and
TechDraw pages (Vg2D/Page2D, docs/TechDrawPortAndSection.md sec 24);
a sheet is neither -- on the desktop it is a Qt widget view
(`SpreadsheetGui::SheetView`), not a scene graph. Two candidate tiers
were costed, plus a hybrid. **The deliverable of this pass is the
decision, not code; nothing below is built.**

## 1. Ground truth

**The cell model is App-side.** `Spreadsheet::Cell` carries the
content expression, alignment, style set (bold/italic/underline),
foreground/background colors, display unit, alias, spans, and
exception text; the computed value and its formatting already live in
`Spreadsheet/App` (`Cell::getFormattedQuantity`, `DisplayUnit`,
`PropertySheet`). Grid geometry is `PropertyColumnWidths` /
`PropertyRowHeights` on the `Sheet`. The desktop's
`SheetModel.cpp` (~600 lines) is only the mapping of that state onto
Qt item roles -- the reference for a feed, not a dependency of one.
So the "new feed" both candidates need is a serializer over App-side
state; neither tier gets to skip it and neither needs the Qt widget.

**What the vg tier would reuse.** `PageServe.cpp` (540 lines) is the
template: a Gui-side observer builds `Page2D` items (paths, fills,
text runs, images), publishes the FULL `PageSnapshot` on every damage
tick, and `SceneStreamServer` splices per-viewer deltas
(`spliceObjectDelta`) with fonts/images as content-addressed chunks.
Multi-viewer catch-up, transport framing, and the wasm-side replay
all come free.

**What the SolidJS tier would reuse.** The control channel
(docs/ThinClient.md sec 4.2): id-correlated JSON ops on the scene
socket's text lane, with the `getProperties`/`setProperty` precedent
-- edits wrapped in `App::AutoTransaction`, recompute, structured
errors, `viewOnly` enforcement already on the connection. The chrome
(`src/Gui/Renderer/web`) already stacks DOM over the canvas per split
cell (split chrome, inspector, loupe), and the split layout protocol
carries a per-cell content flag (`id,x,y,w,h,p`, docs/SplitViews.md
sec 9.4) in exactly four places: wasm `main.cpp`, `splitview.tsx`,
the desktop ViewArea layout tokens, and the doc.

## 2. The evaluation

**A sheet is a text-editing UI, not geometry.** The hard parts are
cell editing (IME and the soft keyboard on mobile), selection,
clipboard, scrolling with frozen row/column headers, and
accessibility. The browser gives every one of these away free; vg
gives none of them, and each would be hand-built engine work. This
fork already made exactly this call once: the property inspector
chose DOM "native soft keyboard/IME -- the whole point of DOM"
(docs/ThinClient.md sec 4.2). A sheet is the same decision at a
larger scale.

**Frozen headers break the Page2D model.** `Page2D::View` is one
whole-page transform; sticky headers need content that does NOT pan
with the body. A vg sheet would need either engine changes (multiple
view transforms per page) or DOM overlays for the headers -- i.e. it
drifts into the hybrid anyway.

**vg's reuse advantage is transport-only, and sheet transport is
trivial.** A used-range cell table is kilobytes of JSON; the parts of
the page tier that are genuinely expensive to rebuild (tessellation,
damage-driven item replay, the GL compositor, content-chunk fonts)
solve problems a sheet does not have. Rendering performance is a
non-issue at sheet scale: the visible region is a few hundred cells,
where a virtualized DOM grid is a solved problem and Vg2D's measured
draw advantage (50k items ~25x Qt) buys nothing.

**What vg would honestly win, and why it does not decide.**
Pixel-identical sheets across desktop captures and browser (minor --
a sheet is not print artwork), and free multi-viewer catch-up via the
splice path. The control channel needs its own change push instead;
but at kilobyte scale the page publisher's own philosophy ("always
serializes the FULL form, deltas exist only as the server's splice")
collapses to: republish the full used range, debounced. No delta
machinery needed.

**The hybrid (vg display + DOM edit) is rejected.** It pays both
integration costs -- the vg feed AND a DOM edit overlay AND
coordinate/selection sync across two rendering worlds -- to save the
single cheapest component, DOM grid painting.

## 3. Decision

**SolidJS chrome (candidate a).** The sheet becomes a DOM grid
component in `src/Gui/Renderer/web`, fed over the control channel.

Shape of the v0, for whoever builds it:

- **Ops** (ThinClient.md sec 4.2 style): `sheet.get` returns the used
  range -- per cell: address, display string, alignment, style,
  colors, span, alias, exception -- plus column widths, row heights,
  and a version. `sheet.set` edits one cell's content (AutoTransaction
  + recompute + structured error, exactly the `setProperty` recipe;
  refused on `viewOnly` connections server-side). An unsolicited
  `sheet.changed` push notifies subscribed connections; the client
  re-gets. Debounce the push like PageServe's damage timer.
- **Serializer** feeds from App-side `Sheet`/`Cell`/`PropertySheet`
  (the host's unit/precision preferences are authoritative -- the
  display string is computed where the prefs live). `SheetModel.cpp`
  is the role-mapping reference. No Qt widget dependency.
- **Split-view**: the layout cell's boolean `page` flag becomes a
  content kind (`3d | page | sheet`). A sheet cell mounts the DOM grid
  over its rect and allocates NO wasm bank -- cheaper than a page
  cell; the canvas simply does not draw there. Wheel/touch inside the
  grid must not fall through to canvas navigation (the existing
  panel-over-canvas chrome already has the pattern).
- **Grid component**: virtualized rows/columns, sticky headers, cell
  selection, an input overlay for editing. SolidJS fine-grained
  reactivity maps naturally onto per-cell updates.

Rough size: 500-700 lines C++ (a `SheetServe`-analog observer +
serializer + three ops), 600-900 lines TSX for the grid, plus the
small cross-cutting layout-kind change.

Deliberately NOT v0: windowed/partial `sheet.get` for huge sheets
(the full used range is fine until proven otherwise), rich per-cell
formatting beyond what `Cell` stores, and formula editing UX beyond a
plain content string.
