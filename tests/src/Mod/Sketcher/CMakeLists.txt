
target_include_directories(Sketcher_tests_run PUBLIC
    ${EIGEN3_INCLUDE_DIR}
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)

# Sketcher itself gets these through a directory-scoped link_directories() in
# src/Mod/Sketcher/App, which does not reach a target declared in tests/. Same
# reason, same fix as tests/src/Mod/Part/CMakeLists.txt.
target_link_directories(Sketcher_tests_run PRIVATE
    ${OCC_LIBRARY_DIR}
)

target_link_libraries(Sketcher_tests_run
    gtest_main
    ${Google_Tests_LIBS}
    Sketcher
)

add_subdirectory(App)
