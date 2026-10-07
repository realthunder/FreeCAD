# GUI tests: FreeCAD-driven scripts that need the whole application on a
# display. Not gtest binaries: each is a Python file handed to the FreeCAD
# binary under xvfb by scripts/gui-test.sh, and it is judged by the
# result file it writes, not by the exit code (docs/Testing.md).
#
# Same readiness as the golden render tests next door (tests/render):
# the FreeCAD binaries, xvfb-run and the harness' conda wrapper. Anything
# missing means the box is not set up for them, which is reported once at
# configure time rather than failing a build that never asked.
find_program(FC_XVFB_RUN xvfb-run)
set(_fc_gui_ready TRUE)
foreach(_req FreeCADMain FreeCADGui)
    if(NOT TARGET ${_req})
        set(_fc_gui_ready FALSE)
    endif()
endforeach()
if(NOT FC_XVFB_RUN OR NOT EXISTS "${CMAKE_SOURCE_DIR}/.conda/run.sh")
    set(_fc_gui_ready FALSE)
endif()
if(NOT _fc_gui_ready)
    message(STATUS "GUI tests not registered: need FreeCADMain, FreeCADGui, "
                   "xvfb-run and .conda/run.sh")
    return()
endif()

set(_fc_gui_run "${CMAKE_SOURCE_DIR}/scripts/gui-test.sh")
set(_fc_gui_work "${CMAKE_BINARY_DIR}/Testing/gui")

# One test = one script; its work directory (result.txt, run.log, the
# isolated configuration) is named after it under Testing/gui.
function(fc_add_gui_test name script)
    add_test(NAME ${name}
             COMMAND "${_fc_gui_run}" "${CMAKE_CURRENT_SOURCE_DIR}/${script}"
                     "${_fc_gui_work}/${name}" ${ARGN})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "FC_BUILD=${CMAKE_BINARY_DIR}"
        TIMEOUT 600)
endfunction()

# The live-import nested-loop crash (docs/DocumentLoad.md sec 15.2): a
# command's nested event loop lets the tree populate a document that a
# progressive import is still filling. Imports the chess set from the
# MaterialX submodule; without the submodule there is nothing to import.
set(_fc_chess_glb
    "${CMAKE_SOURCE_DIR}/src/3rdParty/MaterialX/resources/Geometry/chess_set.glb")
if(EXISTS "${_fc_chess_glb}")
    fc_add_gui_test(GuiLiveImportNestedLoop_tests_run live-import-nested-loop.py
                    --timeout 420)
endif()

# A remote pick on a headless serve source comes back as a scene push
# (docs/ThinClient.md sec 8.9 step 0): the selection root fed nothing
# without a viewer, and the source was no selection observer.
fc_add_gui_test(GuiServeSelectionEcho_tests_run serve-selection-echo.py
                --timeout 180)

# The sandbox guest's files served next to the scene, for a guest booted in
# the viewer's page (docs/Sandbox.md 7.20, C1): boot.json behind the door,
# the runtime and the wheels ahead of it and byte for byte, nothing else.
# The browser leg (sandbox-console-browser.py) is not registered: it needs
# node, puppeteer-core and a Chrome (docs/Testing.md).
fc_add_gui_test(GuiSandboxConsoleServe_tests_run sandbox-console-serve.py
                --timeout 180)

# A guest in a viewer's page reaches the served document over the socket
# (docs/Sandbox.md 7.20, C2): bridge frames through the per-connection
# endpoint, under the document's principal, with no browser -- the browser
# leg (sandbox-bridge-browser.py) is not registered, for the reason above.
fc_add_gui_test(GuiSandboxBridgeServe_tests_run sandbox-bridge-serve.py
                --timeout 180)

# A desktop selection still reaches the panels that observe it
# (docs/ThinClient.md sec 8.4). Gui::Selection() resolves to the current
# instance now, and observers were pinned to the room; on the desktop no
# scope is ever opened, so this must read exactly as it did before.
fc_add_gui_test(GuiSelectionRoomPanels_tests_run selection-room-panels.py
                --timeout 180)

# A served click is resolved through the mirror of the client that sent
# it (docs/ThinClient.md sec 8.9 step 3): Coin ignores setRadius() on an
# explicitly set ray, so an edge was unpickable from a browser until the
# ray went back to the viewport point it came from.
fc_add_gui_test(GuiServeMirrorPick_tests_run serve-mirror-pick.py
                --timeout 180)

# A desktop sketch enters and leaves edit with its scene graph intact
# (docs/ThinClient.md sec 8.9 step 4). The editing root belongs to
# Gui::ViewerContext now rather than to the desktop viewer, and the rule
# that an edit mode's geometry is moved out of the view provider's root
# and put back is what this reads, through the provider's child count.
fc_add_gui_test(GuiSketchEditRoot_tests_run sketch-edit-root.py
                --timeout 180)

# An edit mode entered from a browser runs in that client's own view
# (docs/ThinClient.md sec 8.9 step 4), end to end over a real socket:
# the `edit` op refused until a camera is stated, the sketch's graph
# moved under the editing root and back, an 'E' input event replayed,
# and a client that drops mid-edit leaving no dangling binding behind.
fc_add_gui_test(GuiServeMirrorEdit_tests_run serve-mirror-edit.py
                --timeout 200)
fc_add_gui_test(GuiServeSharedEdit_tests_run serve-shared-edit.py
                --timeout 240)

# Undo and redo are two control ops on the served document
# (docs/ThinClient.md 8.11 item 2): one session, one undo stack, so a
# client's undo is the desktop's Ctrl+Z -- the room cleared as there,
# every client's own instance cleared too and told, the stacks answered
# by transaction name, and the refusals that keep a modal off a serving
# process's GUI thread.
fc_add_gui_test(GuiServeUndoRedo_tests_run serve-undo-redo.py
                --timeout 180)

# The External and CarbonCopy pick from a browser (docs/ThinClient.md
# 8.11 item 3): the served root's hover and click logic runs in the
# client's own view, ahead of its edit callback, and selects into the
# session's instance where the tool's observer listens -- so a click on
# a box's edge from a browser adds external geometry to the sketch the
# desktop is editing, and a click on another sketch's line carbon-copies
# it into the one the browser is editing.
fc_add_gui_test(GuiServeExternalPick_tests_run serve-external-pick.py
                --timeout 240)

# A sketch tool's on-view parameters reach a client and are typed into
# (docs/ThinClient.md sec 8.7, stage 5): the allowlisted `command` op
# that starts a tool at all, the entry boxes stated with a world anchor,
# and a key frame off the wire typed into the box that has the keys --
# which is the claim the design rests on, the box being the same
# QuantitySpinBox the desktop uses with nothing to paint it on.
fc_add_gui_test(GuiServeOnViewParams_tests_run serve-onview-params.py
                --timeout 200)

# A pattern's on-view spacing labels over the wire (upstream 6fa9125919 on
# the fork's label): a click replayed through the scene opens the label's
# box, key frames type into it, and Enter sets the pattern.
fc_add_gui_test(GuiServePatternLabels_tests_run serve-pattern-labels.py
                --timeout 200)

# A pattern's instance toggles over the wire (upstream e22e537c4b on the
# fork's marker node): a click replayed through the scene, picked over the
# instance the marker sits in, leaves the instance out and brings it back.
fc_add_gui_test(GuiServePatternMarkers_tests_run serve-pattern-markers.py
                --timeout 200)

# Leaving a PartDesign feature's edit by a bare resetEdit (Esc in a view, a
# served client leaving it) keeps the change and shows it: preview-on-edit
# paused the feature's recompute, and only the panel's OK made it again.
fc_add_gui_test(GuiPdLeaveEditRecompute_tests_run pd-leave-edit-recompute.py
                --timeout 200)

# A PartDesign feature's edit preview is the edit session's: the tinted
# tool hangs in the session's editing root and each session view swaps
# the feature for its base in its own visibility table. No Visibility is
# written, and a Link's occurrence of the feature keeps it, with no tool.
fc_add_gui_test(GuiPdPreviewPerView_tests_run pd-preview-per-view.py
                --timeout 300)

# The same on a served document: a client with a view is told the swap,
# one without is told nothing, the tool is published with the session's
# overlay and not in the shared scene -- with a desktop window in the
# session, and for a document that has none.
fc_add_gui_test(GuiServePdPreview_tests_run serve-pd-preview.py
                --timeout 240)

# The preference PerViewEdit: an edit runs in the one view it is started
# in. The other 3D windows of the document do not join -- they keep the
# document as it is, and answer "not in edit" -- and with it off every
# window joins, as before.
fc_add_gui_test(GuiEditPerView_tests_run edit-per-view.py
                --timeout 300)

# An edit started after the main window is activated again, or with
# another document's view the active one, is bound to the 3D view: the
# split-view container (Gui::ViewArea) was handed out as the active view
# in both cases, and setEdit found no viewer in it.
fc_add_gui_test(GuiEditAfterReactivation_tests_run edit-after-reactivation.py
                --timeout 180)

# The same on a served document: a session that is one view's is told to
# that view's client only, the desktop window stays out of a client's, and
# no other connection can end it or replace it.
fc_add_gui_test(GuiServePerViewEdit_tests_run serve-per-view-edit.py
                --timeout 300)

# TempoVis inside an edit hides and shows in the views of that edit, by
# entries of their visibility tables, and writes no Visibility: a sketch
# on a pad, its dependents hidden and its support shown in the window of
# the edit only.
fc_add_gui_test(GuiTempoVisPerView_tests_run tempovis-per-view.py
                --timeout 300)

# The same on a served document: a client in the session is told the
# table, a client with no view nothing.
fc_add_gui_test(GuiServeTempoVis_tests_run serve-tempovis.py
                --timeout 240)

# Editing a PartDesign feature that is not the tip shows it in the tip's
# place in the views of the edit only: no Visibility is written, and a
# Link to the body keeps the tip.
fc_add_gui_test(GuiPdEditShownPerView_tests_run pd-edit-shown-per-view.py
                --timeout 300)

# The origin a PartDesign panel shows for picking (Revolution's axes, a
# primitive's planes) is an entry of the edit's views, on the body being
# edited: no Visibility is written, and a Link to the body shows none.
fc_add_gui_test(GuiPdOriginInEdit_tests_run pd-origin-in-edit.py
                --timeout 240)

# A TechDraw view gets its view provider with the line standard preference
# out of range: the preference page stored -1 in it, and every view of a
# part then threw out of its view provider's constructor.
fc_add_gui_test(GuiTechDrawLineStandardOutOfRange_tests_run
                techdraw-line-standard-out-of-range.py --timeout 180)

# A TechDraw page switched to the backend's page layer and back while it is
# open: on a session whose backend is not OpenGL the switch crashed in the
# page's paint, and switching off left the view on a GL viewport.
fc_add_gui_test(GuiTechDrawPageBackendSwitch_tests_run
                techdraw-page-backend-switch.py --timeout 180)

# A TechDraw page drawn by the backend is drawn once: the Qt items of what
# the backend's layer holds are not painted over it, and the layer shows
# what only they used to -- frames, labels, what is selected.
fc_add_gui_test(GuiTechDrawPageBackendSingleDraw_tests_run
                techdraw-page-backend-single-draw.py --timeout 240)

# A click on a TechDraw section line is not a move, and a move is by what
# the line moved: every release recomputed the section, and at any scale
# but 1:1 each time took it further from the centroid.
fc_add_gui_test(GuiTechDrawSectionLineClick_tests_run
                techdraw-section-line-click.py --timeout 180)

# A TechDraw colour a document stored with no opacity -- every colour of an
# older document -- is drawn: dimensions, balloons and hatches restored
# from one were on the page fully transparent.
fc_add_gui_test(GuiTechDrawColourWithoutOpacity_tests_run
                techdraw-colour-without-opacity.py --timeout 180)

# A browser's right click (docs/ThinClient.md sec 8.11b): the desktop's 3D
# view context menu built for what the ray hits, its entries judged for the
# connection, an edit entry entered in the client's view with no 3D view
# opened -- a PartDesign Body's menu included, whose building asks for one.
fc_add_gui_test(GuiServeContextMenu_tests_run serve-context-menu.py
                --timeout 200)

# The expression editor on a live desktop (docs/ExpressionEditor.md): its
# tool bar shown only while an editor is the active view, the split that
# does not evict the spreadsheet being edited, folding from the margin,
# Apply/Diff/Revert/Refresh/Unbind driven as commands, and the one
# question for deleted blocks.
fc_add_gui_test(GuiExpressionEditor_tests_run expression-editor.py
                --timeout 180)

# A served document's scene follows claims made while it is served
# (Gui::Document::signalToggleInSceneGraph): a box put into an offset
# App::Part is picked through the part and not also at the top level, and
# taken back out it is picked at the top level and not in the part.
fc_add_gui_test(GuiServeClaimChildren_tests_run serve-claim-children.py
                --timeout 180)

# The line mid-point auto-constraint (upstream 64054d13c4, 280452bcc8):
# a point dropped within 5% of a line's length from its middle is
# constrained Symmetric about the line's two ends rather than merely
# PointOnObject. Driven over the wire because the suggestion is only
# made for PRESELECTED geometry, and synthetic Qt mouse events
# preselect nothing.
fc_add_gui_test(GuiSketchMidpointAutoConstraint_tests_run
                sketch-midpoint-autoconstraint.py
                --timeout 200)

# A sketch tool's click lands where the client pointed (ThinClient 8.3):
# a click in empty space is placed by projecting the pixel onto the
# sketch plane, and that projection used to apply the aspect ratio twice
# in a mirror, putting every off-centre x aspect times too far out. Exact
# at the middle of the canvas, which is where every other probe clicks.
fc_add_gui_test(GuiServeSketchClickPlacement_tests_run
                serve-sketch-click-placement.py
                --timeout 180)

# The tangent auto-constraint search (upstream ed45e20768, b71a54d9cc):
# tangency is sought before alignment and suppresses it, and the search
# speaks GeoIds rather than indices into getCompleteGeometry(), which
# appends external geometry in reverse.
fc_add_gui_test(GuiSketchTangentAutoConstraint_tests_run
                sketch-tangent-autoconstraint.py
                --timeout 200)

# The line-extension auto-constraint hint and its snap (group C). A click
# past a segment's END, where nothing is drawn, picks up a PointOnObject
# to that segment and is placed exactly on its prolongation; nothing else
# in the search can speak out there. Over the wire because the fork's
# visibility gate asks the edit session's viewer for its viewport region
# rather than the active window's GL widget (ThinClient 8.3), so only a
# mirror exercises the code that had to be rewritten.
fc_add_gui_test(GuiSketchLineExtensionAutoConstraint_tests_run
                sketch-line-extension-autoconstraint.py
                --timeout 240)

# Hovering an axis locks one coordinate and leaves the other to the grid
# (upstream 72d021108f, 9f2b0f910b). The object step used to treat an axis
# as a full snap and return before the grid ever ran, so the free
# coordinate stayed wherever the pointer was.
fc_add_gui_test(GuiSketchAxisGridSnap_tests_run
                sketch-axis-grid-snap.py
                --timeout 180)

# Construction mode belongs to the sketch (upstream 9a1020929e): it was
# one global, so turning it on in one sketch made the next line drawn in
# any other sketch construction geometry too.
fc_add_gui_test(GuiSketchConstructionMode_tests_run
                sketch-construction-mode.py
                --timeout 240)

# The sketch tool's mode is a command, not a hardcoded key: M was the raw
# Coin constant SoKeyboardEvent::M inside each handler's registerPressedKey,
# so it could not be rebound, put on a tool bar, or sent by a client.
fc_add_gui_test(GuiSketchNextToolMode_tests_run
                sketch-next-tool-mode.py
                --timeout 240)

# The origin is drawn as an outline while a drawing tool is active
# (upstream 16908241f0). Upstream flips a scene node of its own; here the
# origin is point 0 of the marker set every vertex shares, so the marker
# set has to speak per point for as long as the tool is up.
fc_add_gui_test(GuiSketchOriginMarker_tests_run
                sketch-origin-marker.py
                --timeout 240)

# Entering a sketch must not SHOW the datum it is attached to
# (upstream dc2aec50d4, 4b50d72769). The TempoVis snippet in
# ViewProviderSketch::setEdit excludes the support by class name, and
# after the datums port an origin plane is an App::Plane -- so a test
# naming only PartDesign::Plane stopped catching it and the origin came
# up with every sketch. A class rename silently disabling a type test
# leaves nothing behind to notice, which is what this watches.
fc_add_gui_test(GuiSketchSupportVisibility_tests_run
                sketch-support-visibility.py
                --timeout 240)

# A view's own object visibility (View3DInventor ObjectVisibilities,
# PerViewVisibilities): picks, the scene bounding box and the backend
# frame of one view follow its map, the other view never sees it. Bare
# entries are gated by the switch, path entries are not.
fc_add_gui_test(GuiPerViewVisibility_tests_run
                per-view-visibility.py
                --timeout 240)

# A child a Link also shows is still drawn and picked after a reopen: the
# load created its view provider after its Part claimed it: the Part went
# missing from its claimers, so only the Link judged it and switched it
# off (ViewProviderDocumentObject::updateChildren), and the Part built its
# 3D children without it (Document::slotFinishImportObjects).
fc_add_gui_test(GuiReopenClaimedChildPick_tests_run
                reopen-claimed-child-pick.py
                --timeout 240)

# A colour-dialog hide (element colours with the hidden marker) of a
# grandchild: an App::Part's is its content, so a Link to the Part hides it
# too; a Link's own stays in that Link -- picked, bounded AND drawn. The
# capture shares one cache per node between occurrences, so it keeps what
# a hide takes out and the flatten drops the draw per its key. A force show
# ("!show") of a grandchild whose Visibility is off mirrors it. Both
# LinkChildrenDirect modes.
fc_add_gui_test(GuiElementColorHide_tests_run
                element-color-hide.py
                --timeout 240)

# An edit session hides the occurrence it edits (ViewerContext::
# hideEditedObject) in every view of the session, a view opened during
# the edit included, as a transient path entry: picks and the backend
# frame lose it, another occurrence through a Link keeps it, no view's
# map or saved file holds it, and leaving the edit brings it back.
fc_add_gui_test(GuiEditHide_tests_run
                edit-hide.py
                --timeout 240)

# A sketch in edit (mode 3) no longer turns its Visibility off nor moves
# its view provider's children: it hands the editing root its own edit
# node and each view of the session hides the occurrence being edited.
# Red shapes tell the sketch's own drawing from the edit geometry: gone
# in edit, back after; another occurrence through a Link stays, both
# ways round.
fc_add_gui_test(GuiSketchEditHide_tests_run
                sketch-edit-hide.py
                --timeout 240)

# A portrait view draws (backend frame), projects (getPointOnViewport)
# and picks (Coin ray) with ONE projection: Coin's ADJUST_CAMERA widens
# the volume by 1/aspect below unit aspect, and the backend feed skipped
# that. Off-centre cubes in both axes; a landscape view is the control.
fc_add_gui_test(GuiPortraitPickVsDraw_tests_run
                portrait-pick-vs-draw.py
                --timeout 240)

# One view's own visibility table costs no other view its pick culling:
# the per-object bounding box caches are shared by the views, and only
# the switch of an object some view has an entry for may read the
# table. Times a pick through a gap in the other view, before and after.
fc_add_gui_test(GuiPerViewVisibilityPickCull_tests_run
                per-view-visibility-pick-cull.py
                --timeout 240)

# The auto clipping's bounding box pass, which every view runs on every
# render, answers for no view (docs/CoinRetirement.md 5.25): it holds what
# any view shows, and views with DIFFERENT maps share its caches after a
# change instead of each rebuilding the last one's. The exact scene box
# stays per view right after it, picks answer per view and still cull.
fc_add_gui_test(GuiPerViewClipCache_tests_run
                per-view-clip-cache.py
                --timeout 240)

# A structure change resolves again only the visibility entries whose
# resolution went through the object it names (docs/CoinRetirement.md
# 5.26): an unrelated change resolves none; a box moved out of its Part, a
# Link's target losing a child, a relink, a new object an entry waited for
# and a deleted one each resolve theirs, and picks follow.
fc_add_gui_test(GuiPerViewResolveSelective_tests_run
                per-view-resolve-selective.py
                --timeout 240)

# Released per-view-shown objects (hidden objects no view shows any more,
# kept in the capture for a quick show again) are evicted first when
# memory stands over PerViewShownEvictWatermark, never one a view still
# shows, and come back when shown again. A simulated 1 MB GPU budget.
fc_add_gui_test(GuiPerViewShownEviction_tests_run
                per-view-shown-eviction.py
                --timeout 240)

# A served client's own visibility map (the view.visibility op) steers the
# host's picks for that client alone: hide, show, a path entry, a clear,
# with a second client untouched throughout.
fc_add_gui_test(GuiServeClientVisibility_tests_run
                serve-client-visibility.py
                --timeout 240)

# Ctrl+A in a focused spreadsheet selects its cells (upstream 4f4e9244e6):
# Ctrl+A is Std_SelectAll's window shortcut, and the table keeps it only by
# claiming it in ShortcutOverride.
fc_add_gui_test(GuiSpreadsheetSelectAll_tests_run
                spreadsheet-select-all.py
                --timeout 180)

# An element row's single icon is a button: it drops down the element's
# parts and picks one; the icon follows the part selected last. Replaces the
# retired Type combo.
fc_add_gui_test(GuiSketchElementsIcon_tests_run
                sketch-elements-icon.py
                --timeout 180)

# The elements list's Mode filter: element kinds and geometry types, ticked
# in combination (upstream's list and ElementFilterState parameter).
fc_add_gui_test(GuiSketchElementsFilter_tests_run
                sketch-elements-filter.py
                --timeout 180)

# A sketch element on the hidden visual layer is not drawn, picked, boxed or
# taken by Select All from the view. The fork's draw() ignored layers.
fc_add_gui_test(GuiSketchVisualLayers_tests_run
                sketch-visual-layers.py
                --timeout 180)

# Select All (Ctrl+A) in a sketch edit selects the sketch's elements, or
# what a focused task panel list shows (upstream 3b76d77ed8 and follow-ups);
# it selected the document's objects.
fc_add_gui_test(GuiSketchSelectAll_tests_run
                sketch-select-all.py
                --timeout 180)

# Selecting in a sketch re-edited within one event-loop turn: the first
# edit's elements panel still observes the selection until it is deleted,
# and kept itemMap pointing at the tree items sketchClosed() deleted.
fc_add_gui_test(GuiSketchReEditSelect_tests_run
                sketch-reedit-select.py
                --timeout 120)

# A bulk selection reaches the sketch being edited. Past
# MaxSelectionNotification a paused batch (addSelections) arrives as one
# SetSelection, which the sketch's view provider and both task panels
# ignored: more than a hundred elements selected and none shown as such.
fc_add_gui_test(GuiSketchBulkSelection_tests_run
                sketch-bulk-selection.py
                --timeout 180)

# The Sketcher's grid display preferences reach the grid. The grid
# extension reads no preference itself and takes its look from setters,
# which upstream calls from ViewProviderSketch's ParameterObserver; this
# fork has none, and the 2023 merge that brought the extension in dropped
# the calls, so the Grid display page wrote values nothing read.
fc_add_gui_test(GuiSketchGridPreferences_tests_run
                sketch-grid-preferences.py
                --timeout 180)

# Sketch edit mode is sized in device pixels on a scaled screen (upstream
# 418c09899b, 37b4560893): it scaled by logical DPI / 96, which Qt 6 keeps
# near 96 on a 200% screen, so lines, points, markers and labels came out
# half size. QT_SCALE_FACTOR=2 gives the xvfb viewer a ratio of 2.
fc_add_gui_test(GuiSketchHiDpiSizes_tests_run
                sketch-hidpi-sizes.py
                --timeout 180)
set_property(TEST GuiSketchHiDpiSizes_tests_run APPEND PROPERTY
             ENVIRONMENT "QT_SCALE_FACTOR=2")

# A Sketcher dimension's number is drawn at its own size under the bgfx
# renderer (mode 3, the default). The backend scales the glyph quad through
# an autozoom factor the label computes as k / viewport height; k = 7.5 was
# fitted while the capture saw Coin's 100 px default viewport, and once it
# got the real one the number came out nearly seven times too small.
fc_add_gui_test(GuiSketchDatumTextMode3_tests_run
                sketch-datum-text-mode3.py
                --timeout 180)

# ...and its arrowheads are drawn, at a pixel size that holds when the view
# zooms. GL sizes them (and the gap for the number) in pixels every frame;
# the capture baked them at Coin's default view volume, about a hundred
# times too small. The leader vertices now carry screen-space offsets the
# backend resolves against its own camera.
fc_add_gui_test(GuiSketchDatumLeadersMode3_tests_run
                sketch-datum-leaders-mode3.py
                --timeout 180)

# The edit graph is drawn once in mode 3. The backend draws it from the
# editing overlay feed, and Coin's GL pass drew it again over the backend's
# frame -- grid, edges and markers on top of the datums. Measured as the
# viewport against the backend's own pre-composite framebuffer.
fc_add_gui_test(GuiSketchEditSingleDrawMode3_tests_run
                sketch-edit-single-draw-mode3.py
                --timeout 180)

# A sketch's vertices are drawn as their markers in mode 3 -- the 7x7 disk
# GL's glBitmap draws -- not as squares of the draw style's point size.
# The backend was handed SoMarkerSet points as plain points; the markers
# now reach it as bitmaps (Render::MeshData::markers).
fc_add_gui_test(GuiSketchPointMarkersMode3_tests_run
                sketch-point-markers-mode3.py
                --timeout 180)

# A sketch's highlight edits reach the nodes' notification: the point
# coordinates' startEditing() was never finished, and the SelectedCurveSet
# was silenced for good by an enableNotify(false) meant for its sibling.
# Mode 3 reuses a vertex cache while the node id stands; both were hidden
# only by the shared material's own notification in the same pass.
fc_add_gui_test(GuiSketchHighlightNotify_tests_run
                sketch-highlight-notify.py
                --timeout 180)

# A sketch's (pre)selection is drawn by overlays of its own: a hover leaves
# the geometry's materials, coordinates and line set alone, writes only the
# constraint whose highlight changed, and the backend draws the hovered
# edge in the preselection colour.
fc_add_gui_test(GuiSketchHighlightOverlay_tests_run
                sketch-highlight-overlay.py
                --timeout 180)

# In mode 3 the sketch preselection is the view's own highlight overlay,
# drawn over the whole edit graph: a hover writes no edit graph node, and
# the hovered edge wins where a datum label crosses it.
fc_add_gui_test(GuiSketchHighlightView_tests_run
                sketch-highlight-view.py
                --timeout 180)

# A preselected constraint is the view's own highlight in mode 3 too: its
# label or icon shown on top, its coincident points as elements.
fc_add_gui_test(GuiSketchConstraintHighlightView_tests_run
                sketch-constraint-highlight-view.py
                --timeout 180)

# The number of a dimension picks over the whole of its text: no bowtie
# hole in its pick box, no bounding box that culls it to the line.
fc_add_gui_test(GuiSketchDatumLabelPick_tests_run
                sketch-datum-label-pick.py
                --timeout 240)

# A new sketch's vertex size is the shape point size preference.
fc_add_gui_test(GuiSketchNewPointSize_tests_run
                sketch-new-point-size.py
                --timeout 120)

# A sketch holding only external geometry is not reported empty.
fc_add_gui_test(GuiSketchEmptyMessage_tests_run
                sketch-empty-message.py
                --timeout 120)

# The context menu offers to change a value for one dimension alone.
fc_add_gui_test(GuiSketchContextMenuValue_tests_run
                sketch-context-menu-value.py
                --timeout 150)

# Hovering a constraint driven by an expression shows the expression.
fc_add_gui_test(GuiSketchExpressionToolTip_tests_run
                sketch-expression-tooltip.py
                --timeout 120)

# Sketcher tools stay off, and do not crash, while a sketch is in Transform edit.
fc_add_gui_test(GuiSketchTransformEditTools_tests_run
                sketch-transform-edit-tools.py
                --timeout 120)

# A curve being drawn takes the edge or the construction colour, and a toggle recolours it.
fc_add_gui_test(GuiSketchPreviewConstructionColor_tests_run
                sketch-preview-construction-color.py
                --timeout 120)

# A distance made in edit, however it was made, puts its label at a view-scaled distance.
fc_add_gui_test(GuiSketchDistanceLabelScale_tests_run
                sketch-distance-label-scale.py
                --timeout 120)

# The Dimension tool's preview label is pulled back from the pointer, not put under it.
fc_add_gui_test(GuiSketchDimensionLabelOffset_tests_run
                sketch-dimension-label-offset.py
                --timeout 120)

# The sketch's solver panel is Core's TaskSolverMessages, with its settings menu.
fc_add_gui_test(GuiSketchSolverPanelSettings_tests_run
                sketch-solver-panel-settings.py
                --timeout 120)

# View Selection fits a sketch's edit element; FitSketchOnEdit fits the sketch on entering edit.
fc_add_gui_test(GuiSketchViewFitEdit_tests_run
                sketch-view-fit-edit.py
                --timeout 150)

# View Selection on a single point recentres the view and keeps its zoom.
fc_add_gui_test(GuiViewSelectionPoint_tests_run
                view-selection-point.py
                --timeout 120)

# A sketch's face colour follows the preference under AutoColor, with the fork's default.
fc_add_gui_test(GuiSketchAutoFaceColor_tests_run
                sketch-auto-face-color.py
                --timeout 200)

# A sketch's automatic colours are not mapped onto shapes made from it.
fc_add_gui_test(GuiSketchColorMapAutomatic_tests_run
                sketch-color-map-automatic.py
                --timeout 150)

# A merged constraint icon wider than 32767 pixels wrapped Coin's image size
# negative; the exception left draw() before it enabled the constraints, so
# none showed on entering edit until a hover.
fc_add_gui_test(GuiSketchMergedIconOverflow_tests_run
                sketch-merged-icon-overflow.py
                --timeout 300)

# Icons on one spot pick one constraint each, and a "+N" picks the rest.
fc_add_gui_test(GuiSketchMergedIconLabels_tests_run
                sketch-merged-icon-labels.py
                --timeout 300)

# Constraint icons on one spot are laid out side by side, a "+N" past the
# slots the settings give them.
fc_add_gui_test(GuiSketchIconLayout_tests_run
                sketch-icon-layout.py
                --timeout 300)

# An SoImage is drawn pixel for pixel in mode 3, as glDrawPixels does. The
# backend's textured quad sat wherever the anchor projected, and half a
# pixel off the filter spread each texel over two pixels: a Sketcher
# constraint icon's 2-pixel stroke came out as three pale rows.
fc_add_gui_test(GuiImagePixelsMode3_tests_run
                image-pixels-mode3.py
                --timeout 180)

# A sketch tool's on-view parameters are drawn in mode 3. Their labels are
# SoDatumLabels, whose GLRender stands down while the backend draws the
# edit graph; they reach the backend only through the getImageNode()
# companion, which Gui::EditableDatumLabel never added -- nobody drew them.
fc_add_gui_test(GuiSketchOnViewLabelsMode3_tests_run
                sketch-onview-labels-mode3.py
                --timeout 180)

# Auto-constraints for a DRAG, not only for a drawing tool (upstream
# ec298e9e9a and its follow-ups): hold a dragged point still on top of
# another point and the Coincident that pins it there is written when
# the button comes up. The dwell timer is the discrimination -- sweeping
# through on the way somewhere else proposes nothing.
fc_add_gui_test(GuiSketchDragAutoConstraint_tests_run
                sketch-drag-autoconstraint.py
                --timeout 300)

# An undo with the mouse button still held takes away what the drag holds
# by index; the drag has to be dropped, not carried on (upstream
# 16aff10544). A label drag carried on crashed in moveConstraint().
fc_add_gui_test(GuiSketchUndoDuringDrag_tests_run
                sketch-undo-during-drag.py
                --timeout 300)

# What a box selection in a sketch edit leaves behind: construction lines
# still drawn as such (upstream 9bff63e38d), and no context menu from the
# right press that cancels one (39329e547f, e469eb5ccb), and the box drawn
# in the colour of the kind of selection it makes (7b85239093).
fc_add_gui_test(GuiSketchBoxSelection_tests_run
                sketch-box-selection.py
                --timeout 300)

# A box selection under a selection gate takes what the gate allows and
# leaves the rest out quietly, not one refusal message and beep per face or
# edge it turned away (upstream a5bf17b144).
fc_add_gui_test(GuiBoxSelectionGate_tests_run
                box-selection-gate.py
                --timeout 300)

# The status bar's messageChanged announces the main window's action
# messages too, and announcing one leaves the console pane's colour alone.
fc_add_gui_test(GuiStatusMessageSignal_tests_run
                status-message-signal.py
                --timeout 300)

# A sketch edited with the "Editing workbench" preference cleared never
# builds the Sketcher toolbars, so its commands have no action -- and
# Sketcher_Snap's icon swap dereferenced that null from isActive().
fc_add_gui_test(GuiSketchToolbarCommandNoAction_tests_run
                sketch-toolbar-command-no-action.py
                --timeout 200)

# The B-spline knot commands read the selection straight into
# isBsplineKnotOrEndPoint, which dereferenced the null getGeometry()
# answers for the GeoUndef a non-geometry sub-element name leaves behind.
fc_add_gui_test(GuiSketchKnotCommandNonGeometry_tests_run
                sketch-knot-command-nongeometry.py
                --timeout 200)

# The constraint tools tell the hint bar what to pick next, and the
# dimension tool names the constraint its next mode would make -- in the
# order makeAppropriateConstraint walks, and silently when the selection
# leaves it no other mode.
fc_add_gui_test(GuiSketchConstraintHints_tests_run
                sketch-constraint-hints.py
                --timeout 240)

# The PartDesign Gui defects of docs/PartDesignPort.md sec 7, as the user
# meets them: a tree move that reversed the features, the crash closing
# the document after it, a deleted Loft or Pipe leaving its sketches
# hidden, a double click ignoring the user edit mode, a body Transparency
# switching its Tip's colour mapping off, a nearly planar face refused for
# a sketch.
fc_add_gui_test(GuiPartDesignDefects_tests_run
                pd-gui-defects.py
                --timeout 300)

# The small PartDesign fixes of docs/PartDesignPort.md sec 8 only the Gui
# shows: a MultiTransform's new transformation in its own body, a feature
# kept in a body when dragged, a body's own datum lines as references, the
# Sprocket panel under a translation, the shaft wizard's constraint combo.
fc_add_gui_test(GuiPartDesignPortFixes_tests_run
                pd-port-fixes.py
                --timeout 300)

# The PartDesign candidates of docs/PartDesignPort.md sec 8 taken
# 2026-10-01: Placement in the context menu, the checked active body, the
# binder watcher and outline, the Tip of a body shown with nothing
# visible, several table references deleted at once, New Sketch's
# attacher, the Pad direction rows, no legacy workflow prompt.
fc_add_gui_test(GuiPartDesignPortCandidates_tests_run
                pd-port-candidates.py
                --timeout 300)

# A dimension's drawing follows its constraint under the bgfx renderer: the
# label's companion shapes feed the capture, and nothing told it they had
# changed, so a changed value kept its old number at the old place.
fc_add_gui_test(GuiSketchDatumFollowsMode3_tests_run
                sketch-datum-follows-mode3.py
                --timeout 200)

# A sketch arc's length has a label (upstream 646b4381f9), and an arc's
# length and angle labels drag to the cursor, past the arc's centre too
# (f3e1e6cec0, 7bcaa766de), the angle's end lines reaching back to the arc
# (df867a25b2).
fc_add_gui_test(GuiSketchArcLabels_tests_run
                sketch-arc-labels.py
                --timeout 240)

# A line angle's end lines reach the lines it measures: a single line's
# horizontal reference through its middle, and a line pair continued out
# to, or joined to, the label's arc (upstream 827781ab3f, dca00ec80e).
fc_add_gui_test(GuiSketchLineAngleLabels_tests_run
                sketch-line-angle-labels.py
                --timeout 200)

# Upstream's Sketcher preselection tests (SketcherTests/TestConstraintPreselectionGui.py)
# on the fork's own hover pick. Under the default renderer the number of a
# dimension could not be picked: its pick box was never sized for the view.
fc_add_gui_test(GuiSketchPreselectionUpstream_tests_run
                sketch-preselection-upstream.py
                --timeout 400)

# A double click on an edge selects its wire, external edges included
# (upstream 6db820a580, a9bff78974, 0b1187b2cd); one on the sketch in edit
# aligns the view instead of leaving and re-entering (321a782eff).
fc_add_gui_test(GuiSketchDoubleClick_tests_run
                sketch-double-click.py
                --timeout 200)

# Cancelling a sketch edit reverts it, by undo while the history reaches
# back, else from a copy taken on entry; Esc keeps the edit (upstream
# 189d86ee53, facca5c426).
fc_add_gui_test(GuiSketchCancelEdit_tests_run
                sketch-cancel-edit.py
                --timeout 240)

# An edit entered from the tree takes the keyboard, so Escape right away
# leaves it (upstream 22a98d81f0); a click with a tool active keeps the
# tool's cursor (a1487106ab).
fc_add_gui_test(GuiSketchFocusCursor_tests_run
                sketch-focus-cursor.py
                --timeout 200)

# A sketch's edge and vertex colours follow the preferences and stay out
# of the file; a file from before keeps colours set by hand (upstream
# 8def94e6f8, e260cf5c8a, 97e7b9d1f2).
fc_add_gui_test(GuiSketchAutoColor_tests_run
                sketch-auto-color.py
                --timeout 200)

# A selected arc grabbed by its centre drags by its centre, not its rim, and
# a conic grabbed by its edge moves by the cursor's move instead of jumping
# its centre to the cursor (upstream 2cd45b07f7).
fc_add_gui_test(GuiSketchDragArcConic_tests_run
                sketch-drag-arc-conic.py
                --timeout 240)

# A reopen builds each shape's visual once, and resolving the per-view
# entries it brings back builds none: a path entry's getDetailPath faulted
# the blob-held shape in (and rebuilt its visual) inside the pass, and the
# load's drain built every blob-held shape twice (docs/CoinRetirement.md
# 5.27).
fc_add_gui_test(GuiReopenVisualBuilds_tests_run
                reopen-visual-builds.py
                --timeout 240)

# A progressive load (ProgressiveLoad, view providers built after the App
# load) builds the Gui state an eager load does: per view provider, per
# scene path to every selection root, a pick grid and the backend's frame,
# over a generated corpus of containers, links, a Body, a sketch, an image
# and a cross-document pair, each opened eagerly, progressively twice and
# eagerly again (the noise floor); then edits, deletes, moves, undo, hides,
# a recompute and a revert made while the drain still runs, against the
# same after an eager open, and closes during the drain
# (docs/DocumentLoad.md sec 19).
fc_add_gui_test(GuiProgressiveLoadDiff_tests_run
                progressive-load-diff.py
                --timeout 1500)

# An automatic datum plane comes back from a file at its saved size in
# either open mode, and the origin sized over it with it: it sized itself
# only during the load, before the features after it had a visual
# (docs/DocumentLoad.md sec 19).
fc_add_gui_test(GuiDatumSizeAfterOpen_tests_run
                datum-size-after-open.py
                --timeout 240)

# A Part object keeps the tessellation it was saved with when a view's
# renderer attaches or goes away: the instancing gate re-ran reload(),
# which wrote the preferences over every object's Deviation and
# AngularDeflection; the gate still rebuilds the representation, and the
# minimum preferences still bound a file's values (docs/DocumentLoad.md
# sec 16).
fc_add_gui_test(GuiPartTessellationReload_tests_run
                part-tessellation-reload.py
                --timeout 240)

# A Part object re-meshes when only its AngularDeflection changes: OCCT
# keeps a resident mesh whose linear deflection still fits and does not
# record the angle, so an angle-only edit (property or preference) drew
# the old mesh until the build learned to clear it.
fc_add_gui_test(GuiPartAngleOnlyRemesh_tests_run
                part-angle-only-remesh.py
                --timeout 240)

# A SubShapeBinder keeps its saved Map*Color settings when opened: reading
# ShapeColor from the file after MapFaceColor switched the mapping off, as
# a colour picked by the user does (docs/DocumentLoad.md sec 19).
fc_add_gui_test(GuiBinderMapColorRestore_tests_run
                binder-map-color-restore.py
                --timeout 240)

# An instanced Part object draws a uniform face colour list at the
# transparency the list says: the uniform branch took the colour's alpha,
# an opacity, for the transparency, so an opaque list drew invisible
# (docs/DocumentLoad.md sec 19).
fc_add_gui_test(GuiInstancedFaceTransparency_tests_run
                instanced-face-transparency.py
                --timeout 240)

# A shape tessellates the same whatever mesh it already carries: every
# tessellation parameter came from a bounding box that read the resident
# mesh, so a coarse rung changed the exact mesh built after it
# (docs/DocumentLoad.md sec 19).
fc_add_gui_test(GuiMeshingBoundsHistory_tests_run
                meshing-bounds-history.py
                --timeout 240)

# ViewObject.getBoundingBox takes each of its five arguments: the parse
# passed one pointer too many, so `mat` was checked against something
# that is no type and `depth` was written into the matrix pointer.
fc_add_gui_test(GuiViewProviderBoundingBox_tests_run
                view-provider-bounding-box.py
                --timeout 120)

# A coincidence that would fold an element onto a point is refused on
# both ways into the command -- the two ends of one line, or an end and
# a point already on the other end -- and a B-spline's ends still join
# (upstream e38154474a, 084379651a).
fc_add_gui_test(GuiSketchCoincidentSameElement_tests_run
                sketch-coincident-same-element.py
                --timeout 150)

# A curve is not made tangent to itself: an arc selected with its own end
# point added Tangent(arc, start, arc) (the subject of upstream
# 1050996387, on the path the fork can reach).
fc_add_gui_test(GuiSketchTangentSelf_tests_run
                sketch-tangent-self.py
                --timeout 120)

# The symmetric constraint takes an element by its two ends -- a line, an
# arc, an open B-spline -- with a symmetry line, an axis or a point, on
# both ways into the command; an element without ends is refused
# (upstream bc3c0dc19a).
fc_add_gui_test(GuiSketchSymmetricElement_tests_run
                sketch-symmetric-element.py
                --timeout 150)

# The Dimension tool: a level line keeps the distance along its own axis
# wherever the pointer goes (upstream 76a84f63ab), and a corner's
# coincident vertices count as one point (upstream 36dd4b983c).
fc_add_gui_test(GuiSketchDimensionToolPoints_tests_run
                sketch-dimension-tool-points.py
                --timeout 150)

# A dimension made from the geometry states the geometry's value to
# fifteen significant digits, not six decimals (the subject of upstream
# b9155035fe); two perpendicular lines are a right angle.
fc_add_gui_test(GuiSketchConstraintValuePrecision_tests_run
                sketch-constraint-value-precision.py
                --timeout 120)

# A sketch whose external reference broke says so in the tree: an extra
# icon beside the item, with a tooltip, gone again once the reference is
# mended (upstream 07b2d9973d, by the fork's extra-icon mechanism).
fc_add_gui_test(GuiSketchMissingExternalTree_tests_run
                sketch-missing-external-tree.py
                --timeout 120)

# A new distance's label goes on a fixed side, clear of both points:
# below for a horizontal distance, beside for a vertical one, upper left
# of an aligned one (upstream 3d87975faf).
fc_add_gui_test(GuiSketchDistanceLabelSide_tests_run
                sketch-distance-label-side.py
                --timeout 150)

# A new angle's arc is drawn where the lines are -- past the nearer end
# when they do not reach their crossing -- and an arc's own angle outside
# the arc (upstream 9663cf8dd4).
fc_add_gui_test(GuiSketchAngleLabelPlace_tests_run
                sketch-angle-label-place.py
                --timeout 150)

# The angle tool takes an arc's own angle by one pick; a line still waits
# for the second, and other tools see an arc as an edge (upstream
# 129c7d4d03).
fc_add_gui_test(GuiSketchAngleToolArc_tests_run
                sketch-angle-tool-arc.py
                --timeout 120)

# The sketch's axes reach across any view: decade-stepped polylines out to
# ten kilometres, shared by every view, still outside the bounding box
# (upstream 5969df37f4, without a per-camera update).
fc_add_gui_test(GuiSketchAxesReach_tests_run
                sketch-axes-reach.py
                --timeout 150)

# A sketch's face lying on a solid's face is the one drawn and the one
# picked: its view is a coplanar overlay, with a polygon offset in front
# of an ordinary face and the win of a depth tie in the pick (upstream
# a2468774d3, by the fork's means). Three poses, the top one square on.
fc_add_gui_test(GuiSketchFaceOnSolidPick_tests_run
                sketch-face-on-solid-pick.py
                --timeout 150)

# A new sketch made with a group or an App::Part selected goes into it
# (upstream 17c3286e52); the command made no sketch at all.
fc_add_gui_test(GuiSketchNewInGroup_tests_run
                sketch-new-in-group.py
                --timeout 120)

# Attach Sketch does not offer the selected sketch as the one to attach
# (upstream 93173ba797): it could be attached to itself.
fc_add_gui_test(GuiSketchAttachNotItself_tests_run
                sketch-attach-not-itself.py
                --timeout 120)

# Merge Sketches carries external geometry over and remaps the constraints
# onto it (upstream d34081b9fe): the merged sketch had none, and an
# external id was moved as if it were the sketch's own geometry.
fc_add_gui_test(GuiSketchMergeExternal_tests_run
                sketch-merge-external.py
                --timeout 120)

# An edge or a vertex lying in a face is picked within the pick radius
# from either side and at any pose: what lies in the plane of the face
# hit first is not behind it. Both ways the single pick is decided.
fc_add_gui_test(GuiPickEdgeInFace_tests_run
                pick-edge-in-face.py
                --timeout 120)

# Outside picking stacked on the constraint tools: an external geometry
# command pressed while the Dimension tool or a constraint command runs
# switches outside picking on that tool instead of replacing it, one
# persistent setting for all of them (the fork's form of upstream
# 999fed9c4e). Real clicks: the reference and the constraint are one undo
# step, and a tool left before the constraint leaves nothing.
fc_add_gui_test(GuiSketchConstraintExternalPick_tests_run
                sketch-constraint-external-pick.py
                --timeout 180)

# The same from a browser, in a session the desktop began and in one the
# client began: a constraint command is not on the browser's command list,
# so the desktop starts it; the client's Sketcher_External switches outside
# picking on the running tool, and its clicks, picked in its own mirror,
# make the reference and the constraint in one undo step.
fc_add_gui_test(GuiServeConstraintExternalPick_tests_run
                serve-constraint-external-pick.py
                --timeout 240)

# Upstream 0c34c93fe4 is not applicable here, and this is the evidence:
# a constraint tool selects by the bare sketch, the sketch and its panels
# through the edited occurrence's path, and the fork's selection puts both
# on the top parent's path, so one element is one entry and what one way
# selected the other unselects. A sketch edited inside an App::Part.
fc_add_gui_test(GuiSketchConstraintToolSelectPath_tests_run
                sketch-constraint-tool-select-path.py
                --timeout 180)

# The constraints panel: "Named" alone in the filter lists the named
# constraints (it listed none), and a double click on a geometric
# constraint's row opens its name for editing (upstream 67f8852697,
# 2d5d8ab86c).
fc_add_gui_test(GuiSketchConstraintPanel_tests_run
                sketch-constraint-panel.py
                --timeout 120)

# A step of the wall clock changes nothing the pointer does: no spin after
# a rest, no click out of a held button, no hover pick put off. The step
# is made on demand by a preloaded gettimeofday(), so the test needs a
# loader that takes LD_PRELOAD.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(FCClockStepShim MODULE clock-step-shim.c)
    target_link_libraries(FCClockStepShim PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(FCClockStepShim PROPERTIES
        PREFIX "" C_VISIBILITY_PRESET default
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/gui")
    fc_add_gui_test(GuiClockStep_tests_run clock-step.py --timeout 180)
    set_property(TEST GuiClockStep_tests_run APPEND PROPERTY ENVIRONMENT
        "FC_CLOCK_SHIM=$<TARGET_FILE:FCClockStepShim>"
        "LD_PRELOAD=$<TARGET_FILE:FCClockStepShim>")
endif()

# The Sketcher's General preference page: the options that decide which
# commands its tool bars carry are shown as the workbench reads them, reach
# the bars when the page is saved, each button in its place, and "Reset
# page" takes back the values the page keeps outside its own widgets
# (upstream 4f429e3288, 8ae1d9bbde, 255949134f, 09209436d2).
fc_add_gui_test(GuiSketcherPreferences_tests_run
                sketcher-preferences.py
                --timeout 120)

# The two "B-spline from knots" commands run the unified B-spline tool in
# its knots method, as upstream's do: the tool widget is there, and four
# clicks and a right click make the interpolating curve, periodic or not.
fc_add_gui_test(GuiSketchBSplineFromKnots_tests_run
                sketch-bspline-from-knots.py
                --timeout 120)

# A drawing tool puts its point where the pointer is, not on whatever the
# pick radius reaches -- its own preview first: the B-spline tool's poles
# landed three pixels short of the click.
fc_add_gui_test(GuiSketchToolClickPosition_tests_run
                sketch-tool-click-position.py
                --timeout 120)

# A sketch's curves are drawn with a line width and a pattern by what they
# are -- normal, construction, internal, external, defining external --
# from the preferences (upstream b140feabaf, 1155182ac3); a visual layer's
# own pattern wins; the mode 3 backend draws the gaps.
fc_add_gui_test(GuiSketchLineStyles_tests_run
                sketch-line-styles.py
                --timeout 180)

# Constraint symbol size, the label's font and the axes' transparency are
# preferences (upstream eef738b312, b9a89bada1, cda241dbd0), and an OK on
# the untouched Display page stores no font.
fc_add_gui_test(GuiSketchDisplaySettings_tests_run
                sketch-display-settings.py
                --timeout 180)

# A dimension's value is typed in the view, at the constraint's label
# (ruled 2026-10-02): Enter applies, Escape takes a new constraint back, a
# click elsewhere applies; the dialog stays for what a box cannot edit.
fc_add_gui_test(GuiSketchDatumInPlace_tests_run
                sketch-datum-in-place.py
                --timeout 180)

# The same from a served client: a dimensional command is on the browser's
# list, its value box travels in the "onview" push and is typed into with
# key frames, and the host shows no modal dialog.
fc_add_gui_test(GuiServeDatumInPlace_tests_run
                serve-datum-in-place.py
                --timeout 240)

# An undo while a dimension's value is typed in place (ruled 2026-10-03):
# Std_Undo/Std_Redo end the typing as Escape does and undo nothing older;
# an undo from Python is followed by tag, or closes the box.
fc_add_gui_test(GuiSketchDatumUndo_tests_run
                sketch-datum-undo.py
                --timeout 240)

# One editor for a constraint's value (ruled 2026-10-03): Tab applies and
# moves it, the entry is one undo step, an expression, the reference
# toggle, the name, and the rows kept off what the dimension measures.
fc_add_gui_test(GuiSketchDatumEditor_tests_run
                sketch-datum-editor.py
                --timeout 240)

# A B-spline tool that is cancelled leaves nothing, and one that finishes is
# one undo step: the tool's command, opened as it starts, outlives the
# command that started it. With continuous mode off, finishing crashed.
fc_add_gui_test(GuiSketchBSplineCancel_tests_run
                sketch-bspline-cancel.py
                --timeout 240)

# Toggle Construction passes over what the sketch refuses to toggle -- an
# ellipse's axes -- and goes on to the rest of the selection.
fc_add_gui_test(GuiSketchToggleConstructionInternal_tests_run
                sketch-toggle-construction-internal.py
                --timeout 120)

# A named reference dimension is read-only in the property editor, as the
# unnamed ones are.
fc_add_gui_test(GuiSketchConstraintPropertyReadOnly_tests_run
                sketch-constraint-property-readonly.py
                --timeout 120)

# Select Associated Constraints answers for a point and an axis, not only
# for an edge (upstream 732501d89d).
fc_add_gui_test(GuiSketchSelectConstraintsParts_tests_run
                sketch-select-constraints-parts.py
                --timeout 120)

# Copying sketch elements to the clipboard frees the copies it makes
# (upstream 5268aa43db, fe8d2845ea): measured as the process's growth.
fc_add_gui_test(GuiSketchCopyClipboardLeak_tests_run
                sketch-copy-clipboard-leak.py
                --timeout 240)

# Hovering the Elements list does not take the keyboard out of a text
# being typed (upstream 1d4a09366c).
fc_add_gui_test(GuiSketchElementsHoverFocus_tests_run
                sketch-elements-hover-focus.py
                --timeout 120)

# A press in the Elements list acts on the row under it, not on the row
# the pointer last entered; on no row, it clears the selection (upstream
# 98d64f9939).
fc_add_gui_test(GuiSketchElementsEmptyClick_tests_run
                sketch-elements-empty-click.py
                --timeout 120)

# The "B-spline by knots" commands and the line group follow the
# construction mode with their icons (upstream 581dee4d48).
fc_add_gui_test(GuiSketchConstructionIcons_tests_run
                sketch-construction-icons.py
                --timeout 120)

# The External geometry tool shows a hint, in each of its flavours
# (upstream 94d39087d3).
fc_add_gui_test(GuiSketchExternalToolHint_tests_run
                sketch-external-tool-hint.py
                --timeout 120)

# The Validate Sketch panel's fixes are Python commands on the sketch
# (upstream 37f0ad43f9, 5696ee821c).
fc_add_gui_test(GuiSketchValidationPanel_tests_run
                sketch-validation-panel.py
                --timeout 120)

# A circle's size said as its radius or as its diameter, switched in the
# value editor (upstream's dialog has it as radio buttons, c9041132f9).
fc_add_gui_test(GuiSketchDatumMeasure_tests_run
                sketch-datum-measure.py
                --timeout 180)

# The same switch from a served client: stated in the "onview" push,
# flipped by an onViewAction.
fc_add_gui_test(GuiServeDatumMeasure_tests_run
                serve-datum-measure.py
                --timeout 240)

# A clipboard copy costs in proportion to what is copied, and what is
# pasted is what was copied.
fc_add_gui_test(GuiSketchCopyClipboardCost_tests_run
                sketch-copy-clipboard-cost.py
                --timeout 240)

# The Elements list selects what its own selection says: arrow keys,
# Shift+arrow, a press on one of several selected rows. It toggled a
# remembered row instead.
fc_add_gui_test(GuiSketchElementsKeys_tests_run
                sketch-elements-keys.py
                --timeout 120)

# An element the sketch already refers to, picked the other way -- projected,
# then by intersection -- is one reference of both kinds
# (SketchObject.ExternalTypes, upstream 0e5e071d72).
fc_add_gui_test(GuiSketchExternalBothWays_tests_run
                sketch-external-both-ways.py
                --timeout 120)

# Input is held while a blocking sequence runs and no longer: the progress
# bar keeps its wait cursor through a 200 ms grace period after a sequence
# stops, and that cursor swallowed every key and click in it.
fc_add_gui_test(GuiSequencerInputAfterStop_tests_run
                sequencer-input-after-stop.py
                --timeout 120)

# The progress bar is on screen only while something runs: the status bar's
# item registry shows every widget that does not carry a userEnabled
# property, and the bar did not, so each registration showed it idle.
fc_add_gui_test(GuiStatusBarProgressIdle_tests_run
                statusbar-progress-idle.py
                --timeout 120)

# A number typed into a tool's on-view box is the number the tool uses: the
# box commits on Enter and Tab ahead of reading its own text, and committed
# what it held before the typing.
fc_add_gui_test(GuiSketchOnViewTypedValues_tests_run
                sketch-onview-typed-values.py
                --timeout 200)

# Gui.Control.activeTaskDialog().accept() and .reject() press the dialog's
# buttons; the dialog was never given its button box, so they did nothing.
fc_add_gui_test(GuiTaskDialogPythonAccept_tests_run
                task-dialog-python-accept.py
                --timeout 120)

# A vertical distance on a vertex of blocked geometry is a reference, as
# the horizontal one is.
fc_add_gui_test(GuiSketchDistanceFixedVertex_tests_run
                sketch-distance-fixed-vertex.py
                --timeout 120)

# A distance to a line whose foot falls past an end of the segment has a
# helper line from that end to the foot (upstream cb5a28acd7), set by the
# sketch and drawn by the label.
fc_add_gui_test(GuiSketchDistanceLabelExtension_tests_run
                sketch-distance-label-extension.py
                --timeout 200)

# The two label tests above again, on the classic GL path (render cache
# mode 0), where a dimension draws itself in SoDatumLabel::GLRender
# rather than through the capture. The axes' transparent material stayed
# in the state past the axes, and a label under it drew its pick box and
# nothing else: no leaders, no number.
fc_add_gui_test(GuiSketchLineAngleLabelsGL_tests_run
                sketch-line-angle-labels.py
                --timeout 200)
fc_add_gui_test(GuiSketchDistanceLabelExtensionGL_tests_run
                sketch-distance-label-extension.py
                --timeout 200)
set_tests_properties(GuiSketchLineAngleLabelsGL_tests_run
                     GuiSketchDistanceLabelExtensionGL_tests_run
                     PROPERTIES ENVIRONMENT
                     "FC_BUILD=${CMAKE_BINARY_DIR};GT_RENDER_CACHE=0")

# Fit All in a sketch. An empty sketch gives it nothing to frame: the fit
# went to the origin point, a hundred and fifty times closer than a new
# document's view. And what a tool left behind -- the position its cursor
# text last had -- is not part of what a fit frames.
fc_add_gui_test(GuiSketchFitAll_tests_run
                sketch-fit-all.py
                --timeout 200)

# The view in sight keeps its camera animation: the timer that stops a
# view's animation once it is in the background was left armed on the
# view just switched to, and cut a turn of its camera short three
# seconds after it opened.
fc_add_gui_test(GuiViewAnimationBackgroundTimer_tests_run
                view-animation-background-timer.py
                --timeout 120)

# A hidden child of an App::Part exports to STEP, hidden and in its own
# colour: OCCT's writer faulted on an invisible component without a colour
# of its own (occ-issues local05), which ExportOCAF2's workaround missed.
fc_add_gui_test(GuiExportHiddenComponent_tests_run
                export-hidden-component.py
                --timeout 180)

# A served document with nothing to draw still answers its clients: a
# hello to an empty document went unanswered, and deleting a document's
# last object was never published -- the serve source alone publishes a
# served document now, and a publish was refused for want of a draw or an
# overlay.
fc_add_gui_test(GuiServeEmptyDocument_tests_run
                serve-empty-document.py
                --timeout 180)

# A file opened through a command arrives whole: File > Open and the recent
# list run the load under the command's own UserEditGuard, the load marks
# its document as filling, and every object of the file was refused.
fc_add_gui_test(GuiOpenThroughCommand_tests_run
                open-through-command.py
                --timeout 180)

# The status bar's view dimension says a shared unit once, "100 x 80 mm",
# and keeps both where the sides differ.
fc_add_gui_test(GuiStatusDimensionText_tests_run
                status-dimension-text.py
                --timeout 120)

# A tip shows its whole icon and keeps its own pictures: a tip of two lines
# cut its icon's last rows off, and the first picture of the navigation
# styles' table was taken for the tip's icon.
fc_add_gui_test(GuiToolTipIconAndImages_tests_run
                tooltip-icon-and-images.py
                --timeout 180)

# Switching the custom title bar on a maximized window leaves it in one
# place: on Windows the frame change left Qt 8 px away from where the
# system had the client area, and the normal placement crept off the screen.
fc_add_gui_test(GuiTitleBarSwitchMaximized_tests_run
                titlebar-switch-maximized.py
                --timeout 180)

# A view on a read-back backend shows the frame that was drawn
# (Render/ReadbackFrameMode): a highlight, a wheel zoom and a new size
# came with the next redraw. The waiting itself is only claimed where the
# backend reads its frame back, which the default one here (OpenGL) does
# not; the pictures are claimed everywhere.
fc_add_gui_test(GuiReadbackFrameMode_tests_run
                readback-frame-mode.py
                --timeout 300)

# What a theme leaves dark on dark: the custom title bar's buttons, a check
# box's edge, the navigation icon, a spreadsheet's text, the report view's
# older lines -- in Dark, in Light after it and in Dark again.
fc_add_gui_test(GuiThemeSwitchContrast_tests_run
                theme-switch-contrast.py
                --timeout 240)

# A report view line that stands in for repeats has its mark in the margin:
# no underline, the text in line with the others, and the mark -- not the
# line -- is what a click unfolds.
fc_add_gui_test(GuiReportFoldMark_tests_run
                report-fold-mark.py
                --timeout 120)

# OK in the preferences on a profile that never changed anything stores, for
# every setting with a definition, the definition's default and its type:
# a page showing another default, or clamping one to a spin box's range,
# changed the setting behind the user's back.
fc_add_gui_test(GuiPreferencesOkKeepsDefaults_tests_run
                preferences-ok-keeps-defaults.py
                --timeout 180)

# The report view follows its newest line unless told not to: "Go to end"
# is on for a profile that never stored it, and still a choice.
fc_add_gui_test(GuiReportGoToEnd_tests_run
                report-go-to-end.py
                --timeout 120)

# OK in the preferences with nothing changed sets no stylesheet again,
# activates no language again, and a key stored with the value it had
# while unset is no change of the thing it names; a real change still
# arrives.
fc_add_gui_test(GuiPreferencesOkReappliesNothing_tests_run
                preferences-ok-reapplies-nothing.py
                --timeout 240)

# Every theme sets TechDraw's preselection colour: Classic stored one and
# Dark and Light did not mention the key, so after Classic a switch left
# TechDraw yellow under a blue 3D view.
fc_add_gui_test(GuiThemeOwnsTechDrawPreselect_tests_run
                theme-owns-techdraw-preselect.py
                --timeout 180)

# A menu is see-through only when a menu sheet was chosen: the menus that
# pop up over the 3D view are shared with the main menu, and Tools >
# Command history came up see-through whenever a theme had a stylesheet.
fc_add_gui_test(GuiMenuSeeThroughIsAChoice_tests_run
                menu-see-through-is-a-choice.py
                --timeout 180)

# Bringing the omni search up for the first time makes no command's icon:
# the completers' lists are laid out with the box's own delegate, not the
# stock one that reads every row's icon to size it.
fc_add_gui_test(GuiOmniSearchFirstBringUp_tests_run
                omni-search-first-bring-up.py
                --timeout 120)

# Switching anti-aliasing on gives the view multisampled targets: a
# multisampled depth cannot be read as a texture, and built that way every
# MSAA view lost its framebuffer and fell back to none for the session.
fc_add_gui_test(GuiMsaaReachesTheView_tests_run
                msaa-reaches-the-view.py
                --timeout 180)

# The report view's settings are behind ReportViewParams: listed by the
# omni search, one default each, followed at once -- and "Record critical
# messages" no longer switches normal messages off.
fc_add_gui_test(GuiReportViewSettings_tests_run
                report-view-settings.py
                --timeout 180)

# Part's measurements follow their settings at once: the colours and the
# font of the 3D dimensions are behind PartGui's PartParams, and a change
# rebuilds the measurements on screen.
fc_add_gui_test(GuiPartMeasureSettings_tests_run
                part-measure-settings.py
                --timeout 180)

# The settings of Preferences/General are behind GeneralParams: listed by
# the omni search, the page's tool tip icon size stored where it is read,
# and "apply preferences at once" applying itself at once.
fc_add_gui_test(GuiGeneralSettings_tests_run
                general-settings.py
                --timeout 180)

# The preferences opened and cancelled with nothing touched write no
# setting and ask nothing: a page that sets a widget after its restore
# stores the key when preferences are applied at once, and the dialog took
# the dock windows' own layout being saved for a change to revert.
fc_add_gui_test(GuiPreferencesCancelAsksNothing_tests_run
                preferences-cancel-asks-nothing.py
                --timeout 180)

# The main window's and the theme's settings are behind MainWindowParams
# and ThemeParams: listed by the omni search, the accent colours with one
# default each wherever one is needed, and the global tool bar area
# followed when it alone changes.
fc_add_gui_test(GuiMainWindowSettings_tests_run
                mainwindow-settings.py
                --timeout 180)

# The notification area's settings are behind NotificationAreaParams:
# listed by the omni search, and the area still follows its switch at once.
fc_add_gui_test(GuiNotificationAreaSettings_tests_run
                notification-area-settings.py
                --timeout 180)

# The Python console's, the macros' and the dialogs' settings are behind
# PythonConsoleParams, MacroParams and DialogParams: listed by the omni
# search and followed at once; a macro path stored empty means the user's
# macro directory, and Draft's ShapeString panel leaves the file dialog
# switch as it found it.
fc_add_gui_test(GuiConsoleMacroSettings_tests_run
                console-macro-settings.py
                --timeout 180)

# The unit settings are behind App::UnitsParams: listed by the omni
# search, and a change of the unit system, the number of decimals or the
# inch fraction is in force when it is made, not at the next start, the
# next document or the next OK in the preferences.
fc_add_gui_test(GuiUnitsSettings_tests_run
                units-settings.py
                --timeout 180)

# The editors' settings are behind EditorParams: listed by the omni search
# (the colours too, whose keys contain spaces), Tab and Enter agree on
# spaces, and the report view's line limit is the view's own key.
fc_add_gui_test(GuiEditorSettings_tests_run
                editor-settings.py
                --timeout 180)

# The rest of the document settings are behind App::DocumentParams: listed
# by the omni search, and the Document page stores no key nothing reads.
fc_add_gui_test(GuiDocumentSettings_tests_run
                document-settings.py
                --timeout 180)

# Sixty-one more settings of the View group are behind ViewParams, and a
# setting that is not stored is its default to every reader: zooming still
# works after a zoom step was stored and removed, and Home is the view a
# new document opens in.
fc_add_gui_test(GuiViewSettings_tests_run
                view-settings.py
                --timeout 180)

# The navigation cube's settings are behind NaviCubeParams: listed by the
# omni search, and the views still follow the cube's switch at once.
fc_add_gui_test(GuiNaviCubeSettings_tests_run
                navicube-settings.py
                --timeout 180)

# PartDesign's settings are behind PartDesign::PartDesignParams: listed by
# the omni search, and new features and datums still take them.
fc_add_gui_test(GuiPartDesignSettings_tests_run
                partdesign-settings.py
                --timeout 180)

# Part's Boolean and geometry check options are behind the two PartParams
# classes, in their sub-groups: listed by the omni search, and a new Boolean
# still takes its refine switch.
fc_add_gui_test(GuiPartOptionsSettings_tests_run
                part-options-settings.py
                --timeout 180)

# Fourteen settings of small parameter groups (recent macros, gizmos, cache
# directory, shortcut timeout, workbench tab bar, the start-up switches,
# the dependency graph) are behind MiscParams, each under its own group.
fc_add_gui_test(GuiMiscSettings_tests_run
                misc-settings.py
                --timeout 180)

# The settings of the Sketcher's own group are behind
# Sketcher::SketcherParams: listed by the omni search, and a new sketch
# still takes what it starts with from them.
fc_add_gui_test(GuiSketcherSettings_tests_run
                sketcher-settings.py
                --timeout 180)

# The Sketcher's solver settings are behind Sketcher::SketcherParams, and
# the solver box of the task panel stores a parameter of the redundant
# solver under that solver's key.
fc_add_gui_test(GuiSketcherSolverSettings_tests_run
                sketcher-solver-settings.py
                --timeout 180)
