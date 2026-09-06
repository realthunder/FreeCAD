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
