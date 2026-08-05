# SPDX-License-Identifier: LGPL-2.1-or-later


# Upstream carries these on the targets it links against; this fork still sets
# them per module, the way every other Mod/*/Gui does.
include_directories(
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OCC_INCLUDE_DIR}
    ${COIN3D_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
    ${PYTHON_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
    ${EIGEN3_INCLUDE_DIR}
)
link_directories(${OCC_LIBRARY_DIR})

set(AssemblyGui_LIBS
    Assembly
    PartDesign
    PartGui
    Spreadsheet
    SpreadsheetGui
    FreeCADGui
)

set (Assembly_TR_QRC ${CMAKE_CURRENT_BINARY_DIR}/Resources/Assembly_translation.qrc)
qt_find_and_add_translation(QM_SRCS "Resources/translations/*_*.ts"
    ${CMAKE_CURRENT_BINARY_DIR}/Resources/translations)
qt_create_resource_file(${Assembly_TR_QRC} ${QM_SRCS})
qt_add_resources(AssemblyResource_SRCS
    Resources/Assembly.qrc
    ${Assembly_TR_QRC}
    OPTIONS ${FREECAD_RCC_OPTIONS}
)

SOURCE_GROUP("Resources" FILES ${AssemblyResource_SRCS})

generate_from_py(ViewProviderAssembly)

set(AssemblyGui_UIC_SRCS
    TaskAssemblyCreateBom.ui
    TaskAssemblyCreateJoint.ui
    TaskAssemblyCreateSimulation.ui
    TaskAssemblyCreateView.ui
    TaskAssemblyInsertLink.ui
    Assembly.ui
)

SET(Python_SRCS
    ViewProviderAssembly.pyi
    ViewProviderAssemblyPyImp.cpp
)
SOURCE_GROUP("Python" FILES ${Python_SRCS})

SET(AssemblyGui_SRCS_Module
    AppAssemblyGui.cpp
    AppAssemblyGuiPy.cpp
    PreCompiled.h
    Commands.cpp
    Commands.h
    TaskAssemblyMessages.cpp
    TaskAssemblyMessages.h
    ViewProviderAssembly.cpp
    ViewProviderAssembly.h
    ViewProviderAssemblyLink.cpp
    ViewProviderAssemblyLink.h
    ViewProviderBom.cpp
    ViewProviderBom.h
    ViewProviderGroups.cpp
    ViewProviderGroups.h
    ${Assembly_QRC_SRCS}
)

SOURCE_GROUP("Module" FILES ${AssemblyGui_SRCS_Module})


SET(AssemblyGui_SRCS
    ${AssemblyResource_SRCS}
    ${AssemblyGui_UIC_HDRS}
    ${AssemblyGui_SRCS_Module}
    ${Python_SRCS}
)

SET(AssemblyGuiIcon_SVG
    Resources/icons/AssemblyWorkbench.svg
)

add_library(AssemblyGui SHARED ${AssemblyGui_SRCS} ${AssemblyGuiIcon_SVG})

if(FREECAD_USE_PCH)
    target_precompile_headers(AssemblyGui PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_LIST_DIR}/PreCompiled.h">
    )
endif(FREECAD_USE_PCH)

target_include_directories(
    AssemblyGui
    PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(WIN32)
    target_compile_definitions(AssemblyGui PRIVATE _USE_MATH_DEFINES)
endif(WIN32)

target_link_libraries(AssemblyGui ${AssemblyGui_LIBS})
if (FREECAD_WARN_ERROR)
    target_compile_warn_error(AssemblyGui)
endif()

SET_BIN_DIR(AssemblyGui AssemblyGui /Mod/Assembly)
SET_PYTHON_PREFIX_SUFFIX(AssemblyGui)

fc_copy_sources(AssemblyGui "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Mod/Assembly" ${AssemblyGuiIcon_SVG})

# Upstream also copies and installs ${Assembly_QRC_SRCS} here, but nothing ever
# sets that variable -- the resources are in AssemblyResource_SRCS and compiled
# into the library. This fork's fc_target_copy_resource rejects the resulting
# empty argument list, so the dead calls are dropped rather than worked around.

INSTALL(TARGETS AssemblyGui DESTINATION ${CMAKE_INSTALL_LIBDIR})
INSTALL(FILES ${AssemblyGuiIcon_SVG} DESTINATION "${CMAKE_INSTALL_DATADIR}/Mod/Assembly/Resources/icons")
