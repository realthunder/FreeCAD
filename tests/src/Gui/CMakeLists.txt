# Standard C++ GTest tests
target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Assistant.cpp
)

# Qt tests
setup_qt_test(QuantitySpinBox)

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
endif()
