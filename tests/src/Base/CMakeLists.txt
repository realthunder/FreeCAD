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
