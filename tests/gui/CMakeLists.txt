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
