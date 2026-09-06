# Standard C++ GTest tests
target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Assistant.cpp
)

# Qt tests
setup_qt_test(QuantitySpinBox)

# The host widget layer (docs/Sandbox.md 7.12, H0): the property core,
# the generator's output for Draft's OrthoArray form, and the Qt backend
# binding uic's widgets to the models.  The header is generated here
# from the .ui in the tree by the generator under test.
set(_fw_test_ui ${CMAKE_SOURCE_DIR}/src/Mod/Draft/Resources/ui/TaskPanel_OrthoArray.ui)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fwui_TaskPanel_OrthoArray.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/Tools/fwuic.py ${_fw_test_ui}
            -o ${CMAKE_CURRENT_BINARY_DIR}/fwui_TaskPanel_OrthoArray.h
    DEPENDS ${_fw_test_ui} ${CMAKE_SOURCE_DIR}/src/Tools/fwuic.py
    COMMENT "fwuic: TaskPanel_OrthoArray.ui as host widget models")
setup_qt_test(FormWidgets)
target_sources(FormWidgets_Tests_run PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fwui_TaskPanel_OrthoArray.h)
target_include_directories(FormWidgets_Tests_run PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/Gui)
target_compile_definitions(FormWidgets_Tests_run PRIVATE FW_TEST_UI_FILE="${_fw_test_ui}")

# The drain's walk over a live document (docs/DocumentLoad.md §13). The
# cursor is header-only over App, and the claim being tested -- that the
# walk survives objects coming and going between slices -- needs no window,
# no view provider and no GL context, so it links FreeCADApp alone.
add_executable(DrainCursor_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/DrainCursor.cpp)
target_include_directories(DrainCursor_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(DrainCursor_tests_run gtest_main FreeCADApp FreeCADBase)
add_test(NAME DrainCursor_tests_run COMMAND DrainCursor_tests_run)

# The scene-stream serializer is backend-agnostic and lives in its own
# library, so it tests without a GL context, a window or the document
# model — hence an executable of its own rather than a place in
# Tests_run, which drags in all of FreeCADApp.
# Per-face material capture through the coin fork's extended lazy
# element (docs/ShapeAppearanceDesign.md stage 2). Links FreeCADGui:
# the render cache is a Gui class, but the whole build-and-read-back
# runs on an SoCallbackAction, so the test needs no GL context, no
# window and no QApplication. Run it a second time with a stock libCoin
# LD_PRELOADed to exercise the fallback leg.
if(TARGET FreeCADGui)
    add_executable(RenderCacheMaterial_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/RenderCacheMaterial.cpp)
    target_include_directories(RenderCacheMaterial_tests_run PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
        ${Python3_INCLUDE_DIRS}
        ${XercesC_INCLUDE_DIRS}
        ${COIN3D_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
    target_link_libraries(RenderCacheMaterial_tests_run
                          gtest_main FreeCADGui FreeCADApp FreeCADBase)
    add_test(NAME RenderCacheMaterial_tests_run
             COMMAND RenderCacheMaterial_tests_run)

    # A material card's Render_* view properties landing on a view
    # provider (docs/CyclesIntegration.md phase 6 item 14). The subject
    # is the dynamic property table, which the mapping reaches through
    # App::PropertyContainer alone, so the test states it on a document
    # object: a view provider cannot exist without the Gui application
    # and its main window (every property write of one reaches
    # updateActions). Links FreeCADGui for the function; no GL context,
    # no window, no QApplication.
    add_executable(RenderProperties_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/RenderProperties.cpp)
    target_include_directories(RenderProperties_tests_run PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
        ${Python3_INCLUDE_DIRS}
        ${XercesC_INCLUDE_DIRS}
        ${COIN3D_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
    target_link_libraries(RenderProperties_tests_run
                          gtest_main FreeCADGui FreeCADApp FreeCADBase)
    add_test(NAME RenderProperties_tests_run
             COMMAND RenderProperties_tests_run)
endif()

if(TARGET FreeCADRenderer)
    add_executable(SceneDump_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/SceneDump.cpp)
    target_link_libraries(SceneDump_tests_run gtest_main FreeCADRenderer)
    add_test(NAME SceneDump_tests_run COMMAND SceneDump_tests_run)

    add_executable(MeshSimplify_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/MeshSimplify.cpp)
    target_link_libraries(MeshSimplify_tests_run gtest_main FreeCADRenderer)
    add_test(NAME MeshSimplify_tests_run COMMAND MeshSimplify_tests_run)

    add_executable(SceneLadder_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/SceneLadder.cpp)
    target_link_libraries(SceneLadder_tests_run gtest_main FreeCADRenderer)
    add_test(NAME SceneLadder_tests_run COMMAND SceneLadder_tests_run)

    add_executable(ProxyHierarchy_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/ProxyHierarchy.cpp)
    target_link_libraries(ProxyHierarchy_tests_run gtest_main FreeCADRenderer)
    add_test(NAME ProxyHierarchy_tests_run COMMAND ProxyHierarchy_tests_run)

    add_executable(ProxyStore_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/ProxyStore.cpp)
    target_link_libraries(ProxyStore_tests_run gtest_main FreeCADRenderer)
    add_test(NAME ProxyStore_tests_run COMMAND ProxyStore_tests_run)

    add_executable(OcclusionCull_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/OcclusionCull.cpp)
    target_link_libraries(OcclusionCull_tests_run gtest_main FreeCADRenderer)
    add_test(NAME OcclusionCull_tests_run COMMAND OcclusionCull_tests_run)

    # The software occlusion rasterizer (docs/FarFieldProxies.md 12.12).
    # Its central claim is a one-directional one -- it may under-cull and
    # may never over-cull -- so the tests carry a reference
    # full-resolution depth buffer to compare against, and the comparison
    # is an inequality rather than an image.
    add_executable(MaskedOcclusion_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/MaskedOcclusion.cpp)
    target_link_libraries(MaskedOcclusion_tests_run gtest_main FreeCADRenderer)
    add_test(NAME MaskedOcclusion_tests_run COMMAND MaskedOcclusion_tests_run)

    # The coarse occluder hulls (docs/FarFieldProxies.md 12.16). Same
    # one-directional claim as the buffer they feed -- a hull may make
    # the pass hide less and never more -- so the central test runs the
    # real pass twice and compares the two masks as an inequality.
    add_executable(OccluderMesh_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/OccluderMesh.cpp)
    target_link_libraries(OccluderMesh_tests_run gtest_main FreeCADRenderer)
    add_test(NAME OccluderMesh_tests_run COMMAND OccluderMesh_tests_run)

    # The runtime cost/benefit decision (docs/FarFieldProxies.md 12.13).
    # It decides from timings alone, so it tests from timings alone -- no
    # GL context, no scene, no clock.
    add_executable(CullBenefit_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/CullBenefit.cpp)
    target_link_libraries(CullBenefit_tests_run gtest_main FreeCADRenderer)
    add_test(NAME CullBenefit_tests_run COMMAND CullBenefit_tests_run)

    # Publishing without drawing (docs/HeadlessServe.md §3.1). Same
    # reasoning as above, and then some: the whole claim of the mode is
    # that the process needs no GPU and no display, which only a test
    # that links nothing graphical can make.
    # UNIX only: it speaks to the server over BSD sockets and reads
    # /proc/self/maps to prove no driver got mapped, neither of which
    # Windows has -- porting it needs Winsock and another way to
    # enumerate the loaded modules.
    if(UNIX)
        add_executable(PublishOnly_tests_run
                       ${CMAKE_CURRENT_SOURCE_DIR}/PublishOnly.cpp)
        target_link_libraries(PublishOnly_tests_run gtest_main FreeCADRenderer)
        add_test(NAME PublishOnly_tests_run COMMAND PublishOnly_tests_run)
    endif()
endif()
