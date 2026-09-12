# SPDX-License-Identifier: LGPL-2.1-or-later

set(CAMSimulator_LIBS
    Path
    Part
    Mesh
    FreeCADApp
    FreeCADGui
    # The simulator draws through the draw facade
    # (Gui/Renderer/DrawDevice.h, docs/CAMSimRenderPort.md) when there
    # is a backend to draw through, and through the raw-GL renderer
    # below when there is not -- a build without the bgfx submodule,
    # a backend that would not start, or a session that never warmed
    # one. Both are permanent; neither library set can go.
    FreeCADRenderer
    ${QtOpenGL_LIBRARIES}
    ${OPENGL_gl_LIBRARY}
)

SET(CAMSimulator_SRCS_Python
    CAMSim.pyi
    CAMSimPyImp.cpp
)


SET(CAMSimulator_SRCS_Module
    AppCAMSimulator.cpp
    CAMSim.cpp
    CAMSim.h
    CAMSimPyImp.cpp
    DlgCAMSimulator.cpp
    DlgCAMSimulator.h
    PreCompiled.h
)

SET(CAMSimulator_SRCS_Core
    EndMill.cpp
    EndMill.h
    GCodeParser.cpp
    GCodeParser.h
    GlUtils.cpp
    GlUtils.h
    GuiDisplay.cpp
    GuiDisplay.h
    linmath.h
    MillMotion.h
    MillPathLine.cpp
	MillPathLine.h
    MillPathSegment.cpp
    MillPathSegment.h
    MillSimulation.cpp
    MillSimulation.h
    OpenGlWrapper.cpp
    OpenGlWrapper.h
    Shader.cpp
    Shader.h
    SimDrawContext.cpp
    SimDrawContext.h
    SimDisplay.cpp
    SimDisplay.h
    SimShapes.cpp
    SimShapes.h
    StockObject.cpp
    StockObject.h
    SolidObject.cpp
    SolidObject.h
    ViewCAMSimulator.cpp
    ViewCAMSimulator.h
    Dummy3DViewer.cpp
    Dummy3DViewer.h
    TopoShapeViewProvider.cpp
    TopoShapeViewProvider.h
    CAMSettings.cpp
    CAMSettings.h
    View3DSettings.cpp
    View3DSettings.h
)

generate_from_py(CAMSim)

SOURCE_GROUP("Module" FILES ${CAMSimulator_SRCS_Module})
SOURCE_GROUP("Python" FILES ${CAMSimulator_SRCS_Python})
SOURCE_GROUP("Core" FILES ${CAMSimulator_SRCS_Core})

SET(CAMSimulator_SRCS_precomp
    ${CAMSimulator_SRCS_Module}
    ${CAMSimulator_SRCS_Python}
)

SET(CAMSimulator_SRCS
    ${CAMSimulator_SRCS_precomp}
    ${CAMSimulator_SRCS_Core}
)

add_library(CAMSimulator SHARED ${CAMSimulator_SRCS})
target_include_directories(
    CAMSimulator
    PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    # Dummy3DViewer pulls Gui/View3DInventorViewer.h, which includes
    # Coin headers; the local Coin fork lives outside the conda prefix.
    ${COIN3D_INCLUDE_DIRS}
)
# The Part/Path link interfaces carry bare -lTK* entries; the local
# OCCT install lives outside the default linker search path.
target_link_directories(CAMSimulator PRIVATE ${OCC_LIBRARY_DIR})
target_link_libraries(CAMSimulator ${CAMSimulator_LIBS})

if(FREECAD_USE_PCH)
    target_precompile_headers(CAMSimulator PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_LIST_DIR}/PreCompiled.h">
    )
    set_source_files_properties(${CAMSimulator_SRCS_Core} PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
endif(FREECAD_USE_PCH)

if (FREECAD_WARN_ERROR)
    target_compile_warn_error(CAMSimulator)
endif()

if(BUILD_BGFX)
    # The simulator's shaders (shaders/*.sc, ported from the GLSL 1.20
    # sources in Shader.cpp; docs/CAMSimRenderPort.md step 2) compile
    # with the same machinery as the engine's own and land in the same
    # runtime pack: the facade's DrawDevice::createProgram() resolves
    # names against the engine's shader path, and the Renderer's
    # install(DIRECTORY) of that pack ships whatever is in it. The
    # camsim_ name prefix keeps them clear of the engine's fc_ set.
    include(${CMAKE_SOURCE_DIR}/src/Gui/Renderer/BGFXShaders.cmake)
    set(CAMSIM_SHADER_PROFILES glsl spirv essl)
    if(APPLE)
        list(APPEND CAMSIM_SHADER_PROFILES metal)
    endif()
    # Must track the engine list, because these land in the SAME pack
    # directory and a backend reads one directory: a pack complete for
    # fc_ and short for camsim_ is not a partial feature, it is a
    # simulator drawing with substituted programs.
    if(WIN32)
        list(APPEND CAMSIM_SHADER_PROFILES dxbc dxil)
    endif()
    fc_bgfx_compile_shaders(CAMSimulator_ShaderBins
        SHADERC   $<TARGET_FILE:shaderc>
        SHADERDIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders
        BGFXINC   ${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bgfx/src
        OUTDIR    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/bgfx/assets/shaders
        PROFILES  ${CAMSIM_SHADER_PROFILES}
        DEPENDS   shaderc)
    add_custom_target(CAMSimulator_Shaders ALL
        DEPENDS ${CAMSimulator_ShaderBins})
    add_dependencies(CAMSimulator CAMSimulator_Shaders)
endif(BUILD_BGFX)

SET_BIN_DIR(CAMSimulator CAMSimulator /Mod/CAM)
SET_PYTHON_PREFIX_SUFFIX(CAMSimulator)

INSTALL(TARGETS CAMSimulator DESTINATION ${CMAKE_INSTALL_LIBDIR})
