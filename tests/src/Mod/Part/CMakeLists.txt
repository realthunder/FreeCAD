
target_include_directories(Part_tests_run PUBLIC
    ${EIGEN3_INCLUDE_DIR}
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)

# Part itself gets these through a directory-scoped link_directories() in
# src/Mod/Part/App, which does not reach a target declared in tests/.
target_link_directories(Part_tests_run PRIVATE
    ${OCC_LIBRARY_DIR}
)

target_link_libraries(Part_tests_run
    gtest_main
    ${Google_Tests_LIBS}
    Part
)

# The external reference format gets its own executable: Part_tests_run does
# not link on this tree (tests/src/Mod/Part/App/TopoShapeCache.cpp reaches for
# symbols Part.so does not export), and that is nothing to do with this.
add_executable(ShapeRefSet_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/App/ShapeRefSet.cpp)
target_include_directories(ShapeRefSet_tests_run PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)
target_link_directories(ShapeRefSet_tests_run PRIVATE
    ${OCC_LIBRARY_DIR}
)
target_link_libraries(ShapeRefSet_tests_run gtest_main Part)
add_test(NAME ShapeRefSet_tests_run COMMAND ShapeRefSet_tests_run)

# The element-map expansion of TopoShape -- TopoShapeEx.cpp, 6276 lines and
# the core of the fork's topological naming -- had no coverage of its own.
# This suite is upstream's TopoShapeExpansion.cpp translated through
# docs/UpstreamNameMap.md. Its own executable because Part_tests_run does not
# link, for the reason given above.
add_executable(TopoShapeEx_tests_run
    ${CMAKE_CURRENT_SOURCE_DIR}/App/TopoShapeEx.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/PartTestHelpers.cpp)
target_include_directories(TopoShapeEx_tests_run PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)
target_link_directories(TopoShapeEx_tests_run PRIVATE
    ${OCC_LIBRARY_DIR}
)
# The suite imports Part through the interpreter, and a test binary is not
# where FreeCAD looks for its modules. Hand it the built module directories.
target_compile_definitions(TopoShapeEx_tests_run PRIVATE
    FC_BUILD_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
    FC_BUILD_MOD_PART_DIR="${CMAKE_BINARY_DIR}/Mod/Part"
)
target_link_libraries(TopoShapeEx_tests_run gtest_main Part)
add_test(NAME TopoShapeEx_tests_run COMMAND TopoShapeEx_tests_run)

add_subdirectory(App)
