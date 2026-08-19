
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

add_subdirectory(App)
