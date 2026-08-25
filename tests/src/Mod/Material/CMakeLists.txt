# The library target is Materials, plural. Naming it Material here made CMake
# take it for a plain -lMaterial instead of a target, so none of its interface
# include directories reached the suite and <fastsignals/signal.h>, which
# App/Application.h pulls in, was not found.
target_include_directories(Material_tests_run PUBLIC
    ${CMAKE_SOURCE_DIR}/tests
    ${EIGEN3_INCLUDE_DIR}
    ${OCC_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)

target_link_libraries(Material_tests_run
    gtest_main
    ${Google_Tests_LIBS}
    Materials
)

add_subdirectory(App)
