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

# Auto-constraints for a DRAG, not only for a drawing tool (upstream
# ec298e9e9a and its follow-ups): hold a dragged point still on top of
# another point and the Coincident that pins it there is written when
# the button comes up. The dwell timer is the discrimination -- sweeping
# through on the way somewhere else proposes nothing.
fc_add_gui_test(GuiSketchDragAutoConstraint_tests_run
                sketch-drag-autoconstraint.py
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
