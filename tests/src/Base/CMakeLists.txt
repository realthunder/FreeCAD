target_sources(
    Tests_run
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Axis.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Base64.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Base64Filter.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Bitmask.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/BoundBox.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Builder3D.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/CoordinateSystem.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/DualNumber.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/DualQuaternion.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Handle.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Matrix.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Placement.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Quantity.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/UnitsSchemaFormat.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Reader.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Rotation.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TimeInfo.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Tools.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Tools2D.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Tools3D.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Unit.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Vector3D.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/ViewProj.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Writer.cpp
)

setup_qt_test(InventorBuilder)

# The copy-on-write containers are a header over the standard library
# alone, so they test without linking anything of FreeCAD's.
add_executable(COWData_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/COWData.cpp)
target_include_directories(COWData_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src)
target_link_libraries(COWData_tests_run gtest_main)
add_test(NAME COWData_tests_run COMMAND COWData_tests_run)

# The attribute/item tracking that makes obj.Attr.Sub = x write back needs
# an interpreter and a throwaway Python type, but nothing of App.
add_executable(PyObjectTracking_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/PyObjectTracking.cpp)
target_include_directories(PyObjectTracking_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(PyObjectTracking_tests_run gtest_main FreeCADBase ${Python3_LIBRARIES})
add_test(NAME PyObjectTracking_tests_run COMMAND PyObjectTracking_tests_run)

# OutputStream and InputStream are one contract in two halves, and the
# halves are written far enough apart to have drifted. Header-only over
# FreeCADBase, so an executable of its own.
add_executable(Stream_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/Stream.cpp)
target_include_directories(Stream_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(Stream_tests_run gtest_main FreeCADBase)
add_test(NAME Stream_tests_run COMMAND Stream_tests_run)

# The document version gate (docs/ShapeAppearanceDesign.md 7.9) is a header
# and two reader overloads, so it tests against FreeCADBase alone.
add_executable(ProgramVersion_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/ProgramVersion.cpp)
target_include_directories(ProgramVersion_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(ProgramVersion_tests_run gtest_main FreeCADBase)
add_test(NAME ProgramVersion_tests_run COMMAND ProgramVersion_tests_run)

# The sequencer's parallel-progress manager only needs FreeCADBase and
# threads, so it tests from an executable of its own rather than a place
# in Tests_run, which drags in all of FreeCADApp.
add_executable(Sequencer_tests_run ${CMAKE_CURRENT_SOURCE_DIR}/Sequencer.cpp)
target_include_directories(Sequencer_tests_run PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS})
target_link_libraries(Sequencer_tests_run gtest_main FreeCADBase ${QtCore_LIBRARIES})
add_test(NAME Sequencer_tests_run COMMAND Sequencer_tests_run)
