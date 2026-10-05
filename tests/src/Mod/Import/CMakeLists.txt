target_sources(
    Import_tests_run
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestExportMarks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestImportErrors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestReaderLook.cpp
)

target_include_directories(Import_tests_run PUBLIC
    ${CMAKE_SOURCE_DIR}
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

# TestExportMarks imports Part through the interpreter, TestImportErrors the
# Import module too, and a test binary is not where FreeCAD looks for its
# modules. Hand it the built module directories -- Material's too, since
# Part's init imports Materials.
target_compile_definitions(Import_tests_run PRIVATE
    FC_BUILD_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
    FC_BUILD_MOD_PART_DIR="${CMAKE_BINARY_DIR}/Mod/Part"
    FC_BUILD_MOD_MATERIAL_DIR="${CMAKE_BINARY_DIR}/Mod/Material"
    FC_BUILD_MOD_IMPORT_DIR="${CMAKE_BINARY_DIR}/Mod/Import"
)

target_link_libraries(Import_tests_run
    gtest_main
    ${Google_Tests_LIBS}
    Import
)
