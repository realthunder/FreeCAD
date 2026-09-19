# Spreadsheet in the browser viewer -- tier evaluation

Ordered 2026-08-28, evaluated 2026-08-29. The question: a
`Spreadsheet::Sheet` has no remote representation at all. The browser
tier draws 3D scenes (SceneDump replayed by the wasm bgfx backend) and
TechDraw pages (Vg2D/Page2D, docs/TechDrawPortAndSection.md sec 24);
a sheet is neither -- on the desktop it is a Qt widget view
(`SpreadsheetGui::SheetView`), not a scene graph. Two candidate tiers
were costed, plus a hybrid. The deliverable of that pass was the
decision, not code. **BUILT 2026-08-31 -- see sec 5 for what shipped and
where it departs from the sketch below.**

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

## 4. Formula auto-completion (added 2026-08-29)

The desktop cell editor is a `Gui::ExpressionLineEdit` (lead char
`=`) driving `Gui::ExpressionCompleter`. That machinery is already
split App/Gui in exactly the place the remote tier needs:

- **`App::ExpressionTokenizer`** (`src/App/ExpressionTokenizer.h`,
  QtCore-only, no widgets): `perform(text, pos)` extracts the
  completion prefix range under the cursor, tracks `<<...>>` string
  quoting (`isInsideString`/`isClosingString`), detects
  unit-after-a-number (`isSearchingUnit`), honors the lead char. It
  runs on the host verbatim.
- **`Gui::ExpressionCompleterModel`** (2632 lines) is Qt-shaped -- a
  lazy `QAbstractItemModel` with bit-packed indexes -- but every
  candidate source it walks is App-side: the documents list, objects
  by name AND label, properties + pseudo-properties
  (`ObjectIdentifier::getPseudoProperties`), property sub-paths
  (`Property::getPaths()`, which is where sheet aliases come from),
  Python attribute drill-down (`ObjectIdentifier::getPyValue`),
  units, `FunctionExpression::getFunctions()`. The Qt model is NOT
  ported; a flat enumerator over the same sources is.
- The widget glue (popup, inline insertion) becomes DOM.

**Mapping: a server-side completion op -- the LSP shape.** The client
cannot enumerate candidates (the document world lives on the host and
churns with every recompute), and a completion query is tiny and
latency-tolerant behind a typing debounce.

- `sheet.complete { doc, obj, text, pos }` -> the host runs
  `ExpressionTokenizer::perform`, then enumerates ONE level of the
  same sources the desktop model walks, capped, and answers
  `{ start, end, items: [{ replace, label, kind, hint? }] }` where
  `kind` is `doc|obj|prop|alias|pseudo|function|unit|constant`.
  `replace` is the exact insertion text for `[start, end)` -- the
  host keeps the `<<...>>` quoting/closing logic, the client never
  re-implements it.
- **Round-trip discipline mirrors QCompleter's**: one query per
  completion LEVEL (when a `.` / `<<` / operator changes the token
  structure), client-side prefix filtering within the level on each
  keystroke. No per-character round trips.
- **Caveats**: the enumeration runs on the main thread like every
  control op (`getPyValue` executes Python attribute access -- GIL +
  document access), one level only and count-capped, exactly the
  discipline that makes the desktop model lazy. Completion is
  read-only, so `viewOnly` connections keep it. The unit case rides
  the same op (`isSearchingUnit` flips the candidate set to units).

This strengthens the sec 3 decision rather than complicating it:
completion is query/response by nature, nothing about it is
rendering. Under the vg tier the exact same op would still be
needed -- plus a hand-drawn popup.

## 5. Built (2026-08-31)

v0 is in, and it works end to end: a sheet edited in a browser lands in
the document, and a sheet edited anywhere else appears in the browser.

**Host** (`src/Mod/Spreadsheet/Gui/SheetControl.cpp`): `sheet.list`,
`sheet.get`, `sheet.set`, and a debounced `sheet.changed` push.

- Core Gui must not link a workbench, so the control channel grew an op
  REGISTRY: `Gui::registerSceneControlOp(name, mutating, handler)`,
  called from SpreadsheetGui's init. Declaring `mutating` at
  registration is what lets the dispatcher refuse an op on a view-only
  connection before the handler runs.
- **The serializer formats through `SheetModel`** -- a change from sec 1,
  which expected the feed to read `Cell` directly with SheetModel as
  "only the role-mapping reference". Reading it directly would have
  meant reimplementing several hundred lines of display logic (locale
  separators, display units, the units schema, error text, alias and
  negative-number colours) that would drift from the desktop the first
  time either side changed. SheetModel is a QAbstractTableModel, not a
  widget: there is nothing to host, and the result is byte-identical to
  what the desktop shows.
- `sheet.list` was not in the sec 3 sketch and turns out to be
  load-bearing: a sheet has no geometry, so it never appears in a scene
  and can never be picked. Without it a viewer has no way to name one.
- `sheet.set` follows SheetModel::setData's recipe but recomputes the
  SHEET's document rather than `App.ActiveDocument` -- a served document
  need not be the one a window is showing, and a headless backend has no
  active document at all.

**Client** (`web/src/sheet.tsx`, ~330 lines TSX): a virtualized DOM grid
with sticky headers, a formula bar, keyboard navigation, and in-cell
editing. **The grid keeps a light surface** although the surrounding
chrome is dark: the host sends the desktop's own colours, and black
text, a yellow alias background and a red error only mean what they mean
on paper white. The first build painted them onto a dark panel and made
default-coloured text invisible.

**Placement differs from sec 3.** The sheet is a floating panel (opened
from the launcher, or `?sheet` in the URL), NOT a split-view content
kind. Every other card in this chrome is a panel; a panel needed no
protocol change, no `main.cpp` layout work and no `3d|page|sheet` token,
and it is the same surface on a phone. The split-cell placement from
sec 3 remains a reasonable later refinement -- it is a placement
question, not a capability one.

**Harnesses**, because none existed for this lane:
`scripts/control-client.py` drives the control channel from a shell (a
minimal RFC 6455 client inline; the conda env has no websocket library),
`scripts/demo-sheet.py` is a servable document covering text, numbers,
quantities, cell-to-cell formulas, a formula reaching into the model, an
alias, styling and a broken cell, and `web/public/sheet-test.html` runs
the panel with NO wasm viewer behind it -- the DOM chrome needs only
`window.fcviewerControlSend` and `fc:control` events, both of which a
page can provide itself. That harness self-checks by reading the
panel's own rendered DOM and POSTing a report, which is how the tier is
verified headlessly.

Two traps worth keeping:

- The server sniffs the LITERAL `"cmd":"hello"` out of a frame to
  register a connection as a viewer, and only a registered viewer is
  sent broadcasts. A client using json.dumps' default `", "` spacing
  gets its replies and silently never receives a single push.
- `QJsonValue::toString()` answers an EMPTY STRING for a non-string, so
  `{"content": 42}` on `sheet.set` used to WIPE the cell rather than set
  it. sheet.set now converts numbers and bools and refuses the rest
  loudly; an absent `content` is a malformed request, not "clear".

Not built, deliberately: `sheet.complete` (sec 4), windowed
`sheet.get` for huge sheets, formatting beyond what `Cell` stores,
column/row resize from the client, and multi-cell selection.

## 6. Local preview evaluation (2026-08-31)

The sandbox's payoff for this tier (docs/ExpressionSandbox.md sec 10,
Phase 2): a formula being typed is evaluated IN THE BROWSER, in the same
expression engine the host runs, and the answer appears without a round
trip. The host's recompute remains the truth -- the preview is cleared
the moment a `sheet.changed` re-get lands, whether it agreed or not.

- `sheet.get` now carries a machine value per cell (`wv`) beside the
  display string, in the sandbox wire encoding: a number, a string, or
  `{t:'quantity',v,u}`. "10.00 cm^2" is for a human; an evaluator needs
  the number and its unit signature. Only the by-value set travels, and
  a cell without `wv` simply cannot take part in a preview.
- The panel loads the packed image lazily in the background when it
  opens, and previews are **pack-first**: every identifier the formula
  mentions is pre-resolved from the cells the host already sent, so the
  image never reaches back. That is exactly why it can run in a browser
  where the bridge is unattached (docs/ExpressionImage.md "The browser
  tier"). A formula referring to something outside the sheet has no
  binding and simply gets no preview.
- The preview shows as a chip in the formula bar, and in the cell in
  italic, both marked provisional. It is deliberately NOT the host's
  formatting: the host owns the unit schema, and the browser guessing
  one it cannot know would be worse than an obviously provisional
  number.
- **The error path is the underrated half.** `=B6 * / B7` answers
  "syntax error, unexpected '/'" instantly, in red, with no server
  involved -- the same parser, so the same message the host would give.

Measured in Firefox: 1042 ms from page load to a displayed preview (most
of it fetching the 10.9 MB image and the first `sheet.get`); the
evaluation itself is the 140 us round trip from the sandbox acceptance
run.

Traps, both found by testing:

- **Do not stage the image inside `build/wasm/web`.** That is vite's
  `outDir` and it is built with `emptyOutDir`, so every `npm run build`
  deletes it -- and because a missing image is a legitimate deployment
  (previews are an accelerator, never a dependency), it fails SILENTLY.
  `scripts/fcx-web-stage.sh` now stages to `build/wasm/fcx`, beside
  fcviewer.html, and the panel logs a console warning when a load it
  attempted fails.
- The formula bar must not pull focus into the in-cell editor. It did,
  which dropped keystrokes and -- via the blur that followed -- committed
  edits the user had not finished.
