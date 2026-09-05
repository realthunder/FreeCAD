# Render tests: the ones that actually put pixels on a surface.
#
# Everything else called "render" in this tree tests the cache, the
# properties or the generated shader source, and is deliberately built
# without a GL context (see tests/src/Gui/CMakeLists.txt, and
# PublishOnly_tests_run, which asserts that no driver is even mapped).
# These are the other half: a frame is drawn and the pixels are read
# back.
#
# Two kinds, and they cost very different amounts:
#
#   - the vg-renderer offscreen smokes, which bring bgfx up headless in
#     their own process, draw, read the pixels back and check that every
#     primitive produced ink. Seconds, no display, no document -- so they
#     run in the default ctest.
#   - the golden captures, which stage a real document in a real FreeCAD
#     under xvfb and compare a (camera x RenderDebug_ViewMode) capture set
#     against blessed reference images, per pipeline stage
#     (docs/RenderDebug.md section 5). Half a minute for the small scene
#     and about a minute for the chess set, and the Cycles ones path
#     trace on the CPU, so only the small raster set runs by default and
#     the rest is opt-in:
#
#         cmake -DFC_RENDER_HEAVY_TESTS=ON <build> && ctest -L render-heavy
#
#     An option rather than the label alone, because a ctest LABEL does
#     not keep a test out of a default run -- it only gives -L something
#     to select on -- and neither does CONFIGURATIONS, which is not
#     filtered at all when no -C is passed (measured: the labelled test
#     ran anyway on a plain `ctest`). Registration is the only gate that
#     actually holds. The label is kept so -L selects them once on.
#
# The reference images live in their OWN repository, mounted here at
# tests/render/refs, because a golden set is binary and churns on every
# reblessing -- which is not something to do to the history of the
# source tree. Every golden test is skipped, not failed, when that
# checkout is absent: the same courtesy MaterialXGen_tests_run extends to
# the MaterialX submodule.

option(FC_RENDER_HEAVY_TESTS
       "Register the heavy golden render tests (CPU path tracing)" OFF)

set(FC_RENDER_REFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/refs" CACHE PATH
    "Reference images for the golden render tests (their own repository)")

# A font the text checks can rely on. Deliberately one from this repo
# rather than a configure-time search of the system: fcvgsmoke's built-in
# default is a Debian path that does not exist on every box, and a test
# that silently loses its text check is worse than one that cannot run.
set(_fc_render_font
    "${CMAKE_SOURCE_DIR}/src/Mod/TechDraw/Gui/Resources/fonts/osifont-lgpl3fe.ttf")

set(_fc_render_work "${CMAKE_BINARY_DIR}/Testing/render")

if(TARGET fcvgsmoke)
    # Two scenarios in the one binary: the raw vg pass (paths, gradients,
    # strokes, text) and the retained Page2D one (pan, zoom, band
    # crossing, rotation, damage, removal).
    #
    # --out matters: with none, the tool writes its dump into the current
    # working directory, which for a ctest run is the build tree root.
    #
    # Do NOT pass --size. The ink checks probe fixed bands of the frame,
    # so a non-default size moves the geometry out from under them and
    # the gradient and stroke checks read MISSING on a working build.
    add_test(NAME RenderSmokeVg_tests_run
             COMMAND fcvgsmoke --font "${_fc_render_font}"
                               --out "${_fc_render_work}/vgsmoke.ppm")
    add_test(NAME RenderSmokePage2D_tests_run
             COMMAND fcvgsmoke --page2d --font "${_fc_render_font}"
                               --out "${_fc_render_work}/page2d.ppm")
    set_tests_properties(RenderSmokeVg_tests_run RenderSmokePage2D_tests_run
                         PROPERTIES TIMEOUT 300)

    # Windows: the parent's fc_add_test_dll_path() sweep keys on a test
    # name that is also a target name, and these two are named for their
    # scenario rather than for fcvgsmoke -- so the sweep passes them by
    # and they would start without bgfx.dll on PATH. Carry the target's
    # own runtime DLL directories here instead.
    if(FC_TESTS_CARRY_DLL_PATH)
        string(REPLACE "@target@" "fcvgsmoke" _fc_vg_mods "${_fc_test_dll_path}")
        set_property(TEST RenderSmokeVg_tests_run RenderSmokePage2D_tests_run
                     APPEND PROPERTY ENVIRONMENT_MODIFICATION "${_fc_vg_mods}")
    endif()
endif()

# The golden captures need the whole application, a display to put it on
# and the harness' own conda wrapper. Anything missing means the test
# cannot run here -- report that once, at configure time, rather than
# failing a build that is simply not set up for it.
find_program(FC_XVFB_RUN xvfb-run)
set(_fc_golden_ready TRUE)
foreach(_req FreeCADMain FreeCADGui)
    if(NOT TARGET ${_req})
        set(_fc_golden_ready FALSE)
    endif()
endforeach()
if(NOT BUILD_BGFX OR NOT FC_XVFB_RUN OR NOT EXISTS "${CMAKE_SOURCE_DIR}/.conda/run.sh")
    set(_fc_golden_ready FALSE)
endif()

if(_fc_golden_ready AND IS_DIRECTORY "${FC_RENDER_REFS_DIR}")
    set(_fc_verify "${CMAKE_SOURCE_DIR}/scripts/render-verify.sh")
    set(_fc_scene "${CMAKE_SOURCE_DIR}/scripts/render-test-scene.py")

    # The small committed scene, raster only: one camera, the five
    # pipeline stages. This is the one that guards every run.
    if(IS_DIRECTORY "${FC_RENDER_REFS_DIR}/raster")
        add_test(NAME RenderGoldenRaster_tests_run
                 COMMAND "${_fc_verify}" capture "${_fc_render_work}/raster"
                         --scene "${_fc_scene}"
                         --golden "${FC_RENDER_REFS_DIR}/raster"
                         --modes 0,1,2,3,4 --timeout 420)
        set_tests_properties(RenderGoldenRaster_tests_run PROPERTIES
            ENVIRONMENT "FC_BUILD=${CMAKE_BINARY_DIR}" TIMEOUT 600)
    endif()

    # Same scene, path traced. CPU on purpose: it is the only device
    # every box has, and a golden blessed on one device does not compare
    # against another.
    if(FC_RENDER_HEAVY_TESTS AND BUILD_CYCLES
       AND IS_DIRECTORY "${FC_RENDER_REFS_DIR}/cycles")
        add_test(NAME RenderGoldenCycles_tests_run
                 COMMAND "${_fc_verify}" capture "${_fc_render_work}/cycles"
                         --scene "${_fc_scene}"
                         --golden "${FC_RENDER_REFS_DIR}/cycles"
                         --modes 0 --cycles --cycles-device CPU
                         --cycles-samples 64 --cycles-size 240x180
                         --timeout 600)
        set_tests_properties(RenderGoldenCycles_tests_run PROPERTIES
            ENVIRONMENT "FC_BUILD=${CMAKE_BINARY_DIR}"
            LABELS render-heavy TIMEOUT 900)
    endif()

    # The real-document leg: the MaterialX chess set, raster and path
    # traced. A synthetic scene cannot fail the way an imported asset
    # with a real material library does -- map binding, the texture path,
    # the MaterialX splice -- and an imported asset is too slow to run
    # every time, so there is one of each and this is the slow one.
    #
    # Both assets ship in the MaterialX submodule; when it is not checked
    # out the scene has nothing to import, so the test is not registered.
    set(_fc_chess_glb
        "${CMAKE_SOURCE_DIR}/src/3rdParty/MaterialX/resources/Geometry/chess_set.glb")
    if(FC_RENDER_HEAVY_TESTS AND BUILD_CYCLES
       AND EXISTS "${_fc_chess_glb}"
       AND IS_DIRECTORY "${FC_RENDER_REFS_DIR}/chess")
        add_test(NAME RenderGoldenChess_tests_run
                 COMMAND "${_fc_verify}" capture "${_fc_render_work}/chess"
                         --scene "${CMAKE_SOURCE_DIR}/scripts/render-test-chess.py"
                         --golden "${FC_RENDER_REFS_DIR}/chess"
                         --modes 0 --cycles --cycles-device CPU
                         --cycles-samples 64 --cycles-size 240x180
                         --timeout 900)
        set_tests_properties(RenderGoldenChess_tests_run PROPERTIES
            ENVIRONMENT "FC_BUILD=${CMAKE_BINARY_DIR}"
            LABELS render-heavy TIMEOUT 1200)
    endif()
endif()
