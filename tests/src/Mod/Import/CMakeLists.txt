target_sources(
    Import_tests_run
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestReaderLook.cpp
)

target_include_directories(Import_tests_run PUBLIC
    ${CMAKE_SOURCE_DIR}/tests
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${QtCore_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)

# Import itself gets these through a directory-scoped link_directories() in
# src/Mod/Import/App, which does not reach a target declared in tests/.
target_link_directories(Import_tests_run PRIVATE
    ${OCC_LIBRARY_DIR}
)

target_link_libraries(Import_tests_run
    gtest_main
    ${Google_Tests_LIBS}
    Import
)
