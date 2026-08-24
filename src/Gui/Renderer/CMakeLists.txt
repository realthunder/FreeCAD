set(Library FreeCADRenderer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB CMakeCache RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/CMakeCache.txt)

set(Renderer_Headers Renderer.h Page2D.h SceneDump.h SceneLadder.h ProxyHierarchy.h ProxyStore.h OcclusionCull.h MaskedOcclusion.h Simd4.h CullBenefit.h MeshSimplify.h OccluderMesh.h MeshSource.h SceneServer.h)
set(Renderer_HPP_SRCS ${Renderer_Headers})
set(Renderer_CPP_SRCS Renderer.cpp Page2D.cpp SceneDump.cpp SceneLadder.cpp ProxyHierarchy.cpp ProxyStore.cpp OcclusionCull.cpp MaskedOcclusion.cpp CullBenefit.cpp MeshSimplify.cpp OccluderMesh.cpp MeshSource.cpp SceneServer.cpp)
set(Renderer_LIBS FreeCADBase
                  ${QtCore_LIBRARIES}
                  ${QtWidgets_LIBRARIES}
                  ${QtOpenGL_LIBRARIES}
                  ${QtOpenGLWidgets_LIBRARIES})

if(WIN32)
    # The GL 1.1 entry points (glGetError, glReadPixels, glDrawBuffer, ...) are
    # declared dllimport by <GL/gl.h> and live in opengl32.lib. Qt6::OpenGL hands
    # consumers libGL on Linux but nothing equivalent on Windows, so link it here.
    # Anything newer than 1.1 is resolved at runtime via QOpenGLExtraFunctions.
    list(APPEND Renderer_LIBS opengl32)
endif()

if(BUILD_BGFX)
    add_definitions(-DHAVE_BGFX)
    list(APPEND Renderer_CPP_SRCS
        BGFXRenderer.cpp
        BGFXViewEnv.cpp
        BGFXViewShadow.cpp
        BGFXViewOverlay.cpp
        BGFXViewParticles.cpp
        BGFXViewEffects.cpp
        BGFXViewSubmit.cpp
        BGFXViewLifecycle.cpp
        BGFXScene.cpp
        BGFXFrame.cpp
        Vg2D.cpp
    )
    list(APPEND Renderer_HPP_SRCS BGFXRenderer.h BGFXRendererP.h Vg2D.h)
    # Private: nothing outside this library calls bgfx, and example-common
    # carries bgfx's own entry point, which asks any executable that links
    # it for a _main_ it does not have.
    list(APPEND Renderer_PRIVATE_LIBS bgfx bx bimg example-common vg-renderer)
endif(BUILD_BGFX)

if(BUILD_DILIGENT)
    add_definitions(-DHAVE_DILIGENT)

    list(APPEND Renderer_CPP_SRCS DiligentRenderer.cpp
                                  Diligent/TexturedCube.cpp)
    list(APPEND Renderer_HPP_SRCS DiligentRenderer.h
                                  Diligent/TexturedCube.hpp)
    list(APPEND Renderer_LIBS
        Diligent-Common
        Diligent-GraphicsTools
        Diligent-TextureLoader
        Diligent-GraphicsEngineOpenGL-static
        Diligent-GraphicsEngineVk-static)
    # if (APPLE)
    #     list(APPEND Renderer_LIBS
    #         Diligent-GraphicsEngineMetal-static)
    # endif()
    # if (MSVC)
    #     list(APPEND Renderer_LIBS
    #         Diligent-GraphicsEngineD3D11-static
    #         Diligent-GraphicsEngineD3D12-static)
    # endif()

    file(GLOB_RECURSE Assets RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} Diligent/assets/*)
    list(APPEND Renderer_Assets ${Assets})

endif(BUILD_DILIGENT)

set(Renderer_SRCS ${Renderer_CPP_SRCS} ${Renderer_HPP_SRCS})

# Create the library
IF(Renderer_BUILD_STATIC_LIBRARY)
    add_library(${Library} STATIC ${Renderer_SRCS})
    target_compile_definitions(${Library} INTERFACE FreeCADRenderer_STATIC=1)
ELSE(Renderer_BUILD_STATIC_LIBRARY)
    add_library(${Library} SHARED ${Renderer_SRCS})
    target_link_libraries(${Library} PUBLIC ${Renderer_LIBS} PRIVATE ${Renderer_PRIVATE_LIBS})
ENDIF(Renderer_BUILD_STATIC_LIBRARY)

if(BUILD_BGFX)
    # bx (bgfx base library) requires C++20 since bgfx 1.150
    set_target_properties(${Library} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
endif()

source_group("source" FILES ${Renderer_CPP_SRCS})
source_group("include" FILES ${Renderer_HPP_SRCS})
source_group("interface" FILES ${Renderer_Headers})

SET_BIN_DIR(FreeCADRenderer FreeCADRenderer)

if(WIN32)
    install(TARGETS FreeCADRenderer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
else(WIN32)
    install(TARGETS FreeCADRenderer
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif(WIN32)

if(BUILD_BGFX)
    # The bgfx shaders compile at build time from source (no committed
    # .bin files) with the in-tree shaderc, straight into the build
    # tree's resource path. All three profiles are produced: the desktop
    # renderer loads glsl or spirv per backend, and the Emscripten
    # viewer build reuses this machinery for its essl pack (see
    # wasm/CMakeLists.txt). Building the library (thus any target
    # linking it, e.g. FreeCADGui) refreshes the compiled shaders too.
    include(${CMAKE_CURRENT_SOURCE_DIR}/BGFXShaders.cmake)
    # An Apple build adds the metal pack: bgfx picks Metal there by
    # default, and without the pack the runtime's shaders/metal lookup
    # (shadercTarget) finds an empty directory — black viewport.
    set(FC_SHADER_PROFILES glsl spirv essl)
    if(APPLE)
        list(APPEND FC_SHADER_PROFILES metal)
    endif()
    fc_bgfx_compile_shaders(Renderer_ShaderBins
        SHADERC   $<TARGET_FILE:shaderc>
        SHADERDIR ${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders
        BGFXINC   ${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bgfx/src
        OUTDIR    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/bgfx/assets/shaders
        PROFILES  ${FC_SHADER_PROFILES}
        DEPENDS   shaderc)
    # The compile inputs travel with the bins (assets/shaders/src) so the
    # runtime user-shader compile has its include root + varying def, and
    # the Renderer knows where the in-tree shaderc landed (env
    # FC_BGFX_SHADERC overrides at runtime).
    fc_bgfx_copy_shader_src(Renderer_ShaderSrc
        SHADERDIR ${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders
        BGFXINC   ${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bgfx/src
        OUTDIR    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/bgfx/assets/shaders)
    target_compile_definitions(${Library} PRIVATE
        FC_BGFX_SHADERC_PATH="$<TARGET_FILE:shaderc>")
    # Bundled effect packages (docs/RenderEngine.md §5.11): one folder
    # per effect (effect.json manifest + .sc sources), copied verbatim —
    # the freecad.rendereffects factory instantiates them into user
    # documents, and the runtime user-shader compile builds the sources.
    file(GLOB_RECURSE FC_EFFECT_FILES RELATIVE
         ${CMAKE_CURRENT_SOURCE_DIR}/effects
         ${CMAKE_CURRENT_SOURCE_DIR}/effects/*)
    set(Renderer_EffectFiles)
    foreach(_f ${FC_EFFECT_FILES})
        set(_out ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/effects/${_f})
        add_custom_command(OUTPUT ${_out}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/effects/${_f} ${_out}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/effects/${_f}
            COMMENT "effect ${_f}"
            VERBATIM)
        list(APPEND Renderer_EffectFiles ${_out})
    endforeach()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/effects/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Renderer/effects)
    # ...and compiled once here purely to be told when they do not
    # compile: they ship as source, so nothing else in the build reads
    # them, and a typo in a bundled effect would surface only when
    # somebody instantiated it.
    fc_bgfx_check_shaders(Renderer_EffectChecks
        SHADERC   $<TARGET_FILE:shaderc>
        SRCDIR    ${CMAKE_CURRENT_SOURCE_DIR}/effects
        SHADERDIR ${CMAKE_CURRENT_SOURCE_DIR}/bgfx/shaders
        BGFXINC   ${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bgfx/src
        OUTDIR    ${CMAKE_CURRENT_BINARY_DIR}/effect-checks
        PROFILES  ${FC_SHADER_PROFILES}
        DEPENDS   shaderc)

    ADD_CUSTOM_TARGET(Renderer_assets ALL
        DEPENDS ${Renderer_ShaderBins} ${Renderer_ShaderSrc}
                ${Renderer_EffectFiles} ${Renderer_EffectChecks})
    add_dependencies(${Library} Renderer_assets)
    install(DIRECTORY
        ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/bgfx/assets/shaders/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/Renderer/bgfx/assets/shaders)
endif(BUILD_BGFX)

# Structural diff of two scene dumps (docs/HeadlessServe.md §4): how a
# publisher with no 3D view is checked against a real viewer. A build-tree
# developer tool — not installed, and cheap enough to always build.
add_executable(fcscenediff tools/scenediff.cpp)
target_link_libraries(fcscenediff ${Library})
SET_BIN_DIR(fcscenediff fcscenediff)

if(BUILD_BGFX)
    # Offscreen vg-renderer smoke test (paths + text into a headless bgfx
    # frame buffer, pixels read back and checked). Build-tree developer
    # tool like fcscenediff -- not installed. Links vg-renderer/bgfx
    # directly, no Qt and no ${Library}.
    add_executable(fcvgsmoke tools/vgsmoke.cpp)
    target_link_libraries(fcvgsmoke ${Library} vg-renderer bgfx bx)
    set_target_properties(fcvgsmoke PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    SET_BIN_DIR(fcvgsmoke fcvgsmoke)
endif()

if (Renderer_Assets)
    install(FILES ${Renderer_Assets} DESTINATION ${CMAKE_INSTALL_DATADIR}/Renderer)

    ADD_CUSTOM_TARGET(Renderer_assets_static ALL SOURCES ${Renderer_Assets})
    fc_copy_sources(Renderer_assets_static
                    "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer"
                    ${Renderer_Assets})
    add_dependencies(${Library} Renderer_assets_static)
endif (Renderer_Assets)
