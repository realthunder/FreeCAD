target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Application.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Branding.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ComplexGeoData.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Document.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Expression.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ElementMap.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/IndexedName.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/License.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MappedElement.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MappedName.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Metadata.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Property.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/StringHasher.cpp
)

# The deferred archive-entry restore (docs/DocumentLoad.md §14) is App's own
# machinery -- park, serve, cancel, flush -- and is tested with a stand-in
# property rather than Part's shape, so the suite links no geometry kernel.
# An executable of its own, like the other focused suites here.
add_executable(DeferredLoad_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/DeferredLoad.cpp)
target_include_directories(DeferredLoad_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(DeferredLoad_tests_run gtest_main FreeCADApp FreeCADBase)
add_test(NAME DeferredLoad_tests_run COMMAND DeferredLoad_tests_run)
