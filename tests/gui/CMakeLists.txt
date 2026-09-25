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
