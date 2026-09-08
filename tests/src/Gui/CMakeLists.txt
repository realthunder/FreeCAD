# Standard C++ GTest tests
target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Assistant.cpp
)

# Qt tests
setup_qt_test(QuantitySpinBox)

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

    # Whether flat_map is the right container for the draw list. A
    # measurement rather than an assertion, so its body is DISABLED and
    # ctest only pays for a process that reports zero tests run.
    add_executable(RenderCacheMapBench_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/RenderCacheMapBench.cpp)
    target_include_directories(RenderCacheMapBench_tests_run PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
        ${Python3_INCLUDE_DIRS}
        ${XercesC_INCLUDE_DIRS}
        ${COIN3D_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
    target_link_libraries(RenderCacheMapBench_tests_run
                          gtest_main FreeCADGui FreeCADApp FreeCADBase)
    add_test(NAME RenderCacheMapBench_tests_run
             COMMAND RenderCacheMapBench_tests_run)

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

    # The selection instance stack (docs/ThinClient.md section 8.4). What
    # it asserts -- which instance Gui::Selection() resolves to, and that
    # an observer stays pinned to the room -- is settled entirely inside
    # SelectionSingleton, so the test needs no document, no main window
    # and no QApplication, and links FreeCADGui only to reach the class.
    add_executable(SelectionStack_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/SelectionStack.cpp)
    target_include_directories(SelectionStack_tests_run PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src
        ${Python3_INCLUDE_DIRS}
        ${XercesC_INCLUDE_DIRS}
        ${COIN3D_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
    target_link_libraries(SelectionStack_tests_run
                          gtest_main FreeCADGui FreeCADApp FreeCADBase)
    add_test(NAME SelectionStack_tests_run
             COMMAND SelectionStack_tests_run)
endif()

if(TARGET FreeCADRenderer)
    add_executable(SceneDump_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/SceneDump.cpp)
    target_link_libraries(SceneDump_tests_run gtest_main FreeCADRenderer)
    add_test(NAME SceneDump_tests_run COMMAND SceneDump_tests_run)

    # The raster MaterialX generator. It reads the shipped data
    # library, and a bare test binary has no one to seed the renderer's
    # resource path, so the build states where it is.
    add_executable(MaterialXGen_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/MaterialXGen.cpp)
    target_link_libraries(MaterialXGen_tests_run gtest_main FreeCADRenderer)
    # And where the vendored example materials are, for the one case
    # the whole surface-name feature exists for: a real asset whose
    # whole material set is one document (docs/MaterialStorage.md 17.13).
    # Skipped at run time when the submodule is not checked out.
    target_compile_definitions(MaterialXGen_tests_run PRIVATE
        # CMAKE_INSTALL_DATADIR, not a literal "share": it is what stages the
        # data library (src/3rdParty/CMakeLists.txt), and it is "data" on
        # Windows -- where the literal left every case failing with "the
        # MaterialX data library is missing from this install".
        FC_TEST_RENDERER_RESOURCES="${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/"
        FC_TEST_MATERIALX_RESOURCES="${CMAKE_SOURCE_DIR}/src/3rdParty/MaterialX/resources/")
    add_test(NAME MaterialXGen_tests_run COMMAND MaterialXGen_tests_run)

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
    # UNIX only: it speaks to the server over BSD sockets, and proves no
    # driver got mapped by enumerating the loaded images -- through
    # /proc/self/maps on Linux and dyld on macOS. Windows has neither;
    # porting it needs Winsock and EnumProcessModules.
    if(UNIX)
        add_executable(PublishOnly_tests_run
                       ${CMAKE_CURRENT_SOURCE_DIR}/PublishOnly.cpp)
        target_link_libraries(PublishOnly_tests_run gtest_main FreeCADRenderer)
        add_test(NAME PublishOnly_tests_run COMMAND PublishOnly_tests_run)
    endif()

    # The scene server over a real socket (docs/SceneServerPort.md
    # stage 0): the oracle every stage of the port is judged by. The
    # client side is Boost.Beast -- header-only, hence the include dir
    # and no Boost component to link -- so the test is portable and the
    # handshake is checked by an implementation that is not ours. Built
    # on every platform on purpose: where the listener is still the
    # POSIX-only stub the failure is the verdict.
    add_executable(SceneServerWire_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/SceneServerWire.cpp)
    target_include_directories(SceneServerWire_tests_run PRIVATE
        ${Boost_INCLUDE_DIRS})
    target_link_libraries(SceneServerWire_tests_run gtest_main FreeCADRenderer)
    if(WIN32)
        target_link_libraries(SceneServerWire_tests_run ws2_32 mswsock)
    elseif(UNIX)
        target_link_libraries(SceneServerWire_tests_run pthread)
    endif()
    add_test(NAME SceneServerWire_tests_run COMMAND SceneServerWire_tests_run)
endif()
