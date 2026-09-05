if(BUILD_EXPR_IMAGE_HOST)
    set(ExpressionImageHost_TEST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionImageHost.cpp)
    if(BUILD_EXPR_PYODIDE_HOST)
        list(APPEND ExpressionImageHost_TEST_SRC
            ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionPyodide.cpp)
    endif()
else()
    set(ExpressionImageHost_TEST_SRC)
endif()

target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Application.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Branding.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Document.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Expression.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionSecurity.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionSecurityRuntime.cpp
            ${ExpressionImageHost_TEST_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/License.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Metadata.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Property.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TypeImport.cpp
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

# The per field material storage (docs/ShapeAppearanceDesign.md §3) is a
# property and nothing else -- no document, no view provider, no renderer --
# so it gets an executable of its own like the suites below.
add_executable(PropertyMaterialList_tests_run
               ${CMAKE_CURRENT_SOURCE_DIR}/PropertyMaterialList.cpp)
target_include_directories(PropertyMaterialList_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(PropertyMaterialList_tests_run gtest_main FreeCADApp FreeCADBase)
add_test(NAME PropertyMaterialList_tests_run COMMAND PropertyMaterialList_tests_run)

# The restore-drain scope (docs/DocumentLoad.md §13) is App's own machinery
# too -- a status bit and the two touch paths that honor it -- and needs no
# more than a document with an object in it. Its own executable for the same
# reason as the suite above.
add_executable(RestoreDrain_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/RestoreDrain.cpp)
target_include_directories(RestoreDrain_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(RestoreDrain_tests_run gtest_main FreeCADApp FreeCADBase)
add_test(NAME RestoreDrain_tests_run COMMAND RestoreDrain_tests_run)

# The element-map core -- IndexedName, MappedName, MappedElement, the
# StringHasher that backs them, ElementMap itself, and ComplexGeoData's
# serialization of the map -- is the oldest part of the topological-naming
# work and the least covered. These suites arrived from upstream in 2023 and
# then sat unbuilt in Tests_run for two years. Give them an executable of
# their own, like the suites above, so they actually run.
#
# ElementMap.cpp drives the map through ComplexGeoData rather than
# constructing Data::ElementMap, which this fork keeps inside its own
# translation unit. See the comment at the top of that file, and
# docs/UpstreamNameMap.md section 7.
add_executable(Toponaming_tests_run
    ${CMAKE_CURRENT_SOURCE_DIR}/ComplexGeoData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ElementMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IndexedName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MappedElement.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MappedName.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StringHasher.cpp)
target_include_directories(Toponaming_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(Toponaming_tests_run gtest_main FreeCADApp FreeCADBase
    ${QtCore_LIBRARIES} fmt::fmt)
add_test(NAME Toponaming_tests_run COMMAND Toponaming_tests_run)
