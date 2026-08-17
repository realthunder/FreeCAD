
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

add_subdirectory(App)
