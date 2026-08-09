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

    # Publishing without drawing (docs/HeadlessServe.md §3.1). Same
    # reasoning as above, and then some: the whole claim of the mode is
    # that the process needs no GPU and no display, which only a test
    # that links nothing graphical can make.
    add_executable(PublishOnly_tests_run
                   ${CMAKE_CURRENT_SOURCE_DIR}/PublishOnly.cpp)
    target_link_libraries(PublishOnly_tests_run gtest_main FreeCADRenderer)
    add_test(NAME PublishOnly_tests_run COMMAND PublishOnly_tests_run)
endif()
