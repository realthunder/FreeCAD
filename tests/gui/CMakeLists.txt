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
