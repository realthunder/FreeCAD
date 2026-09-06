set(Library FreeCADRenderer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# stb_image (ImageDecode.cpp): bimg's vendored copy, header only. The
# submodule is present whether or not the bgfx backend is built.
include_directories(${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bimg/3rdparty)

file(GLOB CMakeCache RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/CMakeCache.txt)

set(Renderer_Headers Renderer.h ImageDecode.h MaterialXSupport.h Environment.h Page2D.h Page2DWire.h DrawDevice.h DrawSurface.h FrameImageConsumer.h FrameStreamWire.h SceneDump.h SceneLadder.h ProxyHierarchy.h ProxyStore.h OcclusionCull.h MaskedOcclusion.h Simd4.h CullBenefit.h MeshSimplify.h OccluderMesh.h MeshSource.h SceneServer.h)
set(Renderer_HPP_SRCS ${Renderer_Headers})
set(Renderer_CPP_SRCS Renderer.cpp MaterialXSupport.cpp MaterialXGen.cpp Environment.cpp Page2D.cpp Page2DWire.cpp DrawDevice.cpp FrameImageConsumer.cpp ImageDecode.cpp SceneDump.cpp SceneLadder.cpp ProxyHierarchy.cpp ProxyStore.cpp OcclusionCull.cpp MaskedOcclusion.cpp CullBenefit.cpp MeshSimplify.cpp OccluderMesh.cpp MeshSource.cpp SceneServer.cpp)
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
        BGFXDrawDevice.cpp
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
    # vg-renderer BEFORE example-common: both vendor fontstash (vg-renderer
    # compiles it directly, bgfx inlines it into nanovg.cpp), and nothing here
    # uses bgfx's nanovg. With example-common first, Apple's ld resolves the
    # fons* symbols out of nanovg.cpp.o, then pulls vg-renderer's fontstash.cpp.o
    # for its own symbols and fails with 32 duplicates. GNU ld happens not to.
    list(APPEND Renderer_PRIVATE_LIBS bgfx bx bimg vg-renderer example-common)
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

# MaterialX (docs/CyclesIntegration.md sec 8 item 15 phase B). Like the
# Cycles unit below, MaterialXSupport.cpp and MaterialXGen.cpp compile either way -- with
# the library it is the real thing, without it the stub that reports
# the support absent -- so only the flag and the libraries are
# conditional. PRIVATE: MaterialXSupport.h names no MaterialX type, so
# nothing outside this library needs its headers.
if(BUILD_MATERIALX)
    list(APPEND Renderer_HPP_SRCS MaterialXSupportP.h)
    list(APPEND Renderer_PRIVATE_LIBS MaterialXCore MaterialXFormat
                                      MaterialXGenShader MaterialXGenGlsl)
    add_definitions(-DHAVE_MATERIALX)
endif()

# The shader graph editor (docs/ShaderGraphEditor.md): MaterialX's node
# editor hosted on bgfx's ImGui inside a Qt widget. Dear ImGui itself
# is bgfx's patched copy, already compiled into example-common (with
# bgfx's own example renderer, which is not used); imgui-node-editor is
# our submodule. FC_SHADER_GRAPH_EDITOR is the compile-time switch the
# Gui side reads too (src/Gui/CMakeLists.txt), so both halves come and
# go together.
if(BUILD_BGFX AND BUILD_MATERIALX)
    set(_node_editor ${CMAKE_SOURCE_DIR}/src/3rdParty/imgui-node-editor)
    if(NOT EXISTS ${_node_editor}/imgui_node_editor.cpp)
        message(FATAL_ERROR "imgui-node-editor submodule missing: "
                            "run 'git submodule update --init src/3rdParty/imgui-node-editor'")
    endif()
    list(APPEND Renderer_CPP_SRCS
        GraphEditor/ImGuiBgfx.cpp
        GraphEditor/ImGuiStb.cpp
        GraphEditor/ImGuiStdlib.cpp
        GraphEditor/ImGuiSurface.cpp
        GraphEditor/GraphEditorWidget.cpp
        GraphEditor/EngineGraphHost.cpp
        GraphEditor/Graph.cpp
        GraphEditor/UiNode.cpp
        GraphEditor/Layout.cpp
        GraphEditor/UiProperties.cpp
        GraphEditor/NodeEditorDrawing.cpp
        GraphEditor/NodeEditorWidgets.cpp
        ${_node_editor}/imgui_node_editor.cpp
        ${_node_editor}/imgui_node_editor_api.cpp
        ${_node_editor}/imgui_canvas.cpp
        ${_node_editor}/crude_json.cpp)
    list(APPEND Renderer_HPP_SRCS
        GraphEditor/ImGuiBgfx.h
        GraphEditor/ImGuiStdlib.h
        GraphEditor/ImGuiSurface.h
        GraphEditor/GraphEditorWidget.h
        GraphEditor/EngineGraphHost.h
        GraphEditor/GraphHost.h
        GraphEditor/Graph.h
        GraphEditor/UiNode.h
        GraphEditor/Layout.h
        GraphEditor/UiProperties.h
        GraphEditor/NodeEditorDrawing.h
        GraphEditor/NodeEditorWidgets.h)
    # The node editor includes <imgui.h> and <imgui_internal.h> bare;
    # our own sources take <dear-imgui/...> through example-common's
    # public include path. Same files either way.
    include_directories(${_node_editor}
                        ${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/bgfx/3rdparty/dear-imgui)
    # The node editor and the ported editor use ImVec2 arithmetic, which
    # ImGui only defines on request, and before its own header.
    add_definitions(-DFC_SHADER_GRAPH_EDITOR -DIMGUI_DEFINE_MATH_OPERATORS)
    unset(_node_editor)
endif()

# The Cycles unit (docs/CyclesIntegration.md). One source either way:
# built with the engine it is an object library of its own, because the
# Cycles headers come with the SSE4.2 flags and feature defines that
# cycles_embed carries and nothing else in this library should compile
# under them; built without, the same file is the stub that reports the
# engine absent.
list(APPEND Renderer_HPP_SRCS CyclesRenderer.h)
if(BUILD_CYCLES)
    add_library(FreeCADRendererCycles OBJECT
        CyclesRenderer.cpp CyclesScene.cpp CyclesViewport.cpp CyclesStream.cpp
        CyclesRenderer.h CyclesSceneP.h)
    if(BUILD_MATERIALX)
        target_sources(FreeCADRendererCycles PRIVATE
            CyclesMaterialX.cpp CyclesMaterialXP.h)
    endif()
    target_link_libraries(FreeCADRendererCycles
        PUBLIC cycles_embed
        PRIVATE FreeCADBase ${QtCore_LIBRARIES} ${QtWidgets_LIBRARIES})
    # TBB has to be linked here, and PUBLIC, or the MSVC link of
    # FreeCADRenderer.dll dies with LNK1104 on a bare "tbb12.lib".
    # Two things combine: the oneTBB headers auto-link on MSVC
    # (_config.h: #pragma comment(lib, "tbb12.lib")), which puts a
    # DEFAULTLIB directive carrying only the file name into every object
    # that includes them; and cycles_embed never propagates TBB, because
    # cycles_external_libraries_append() has no TBB entry -- the Cycles
    # libraries consume it privately through bf_deps_optional_tbb. So the
    # directive arrives with no /LIBPATH that holds the file. TBB::tbb is
    # the env's own config package (lib/cmake/TBB), and it points at
    # tbb12.lib; Cycles' bundled FindTBB picks the legacy tbb.lib, which
    # has no DLL beside it and does not answer the directive.
    # Nothing else in this tree loads TBB's config package, so ask for
    # it here. Cycles' external_libs.cmake finds TBB in MODULE mode
    # through its own bundled FindTBB, which sets TBB_LIBRARY and
    # TBB_INCLUDE_DIR but defines no imported target -- so TBB::tbb does
    # not exist, the guard below silently does nothing, and the LNK1104
    # described above is exactly what the link produces. QUIET, and the
    # guard is kept, because a platform without the config package does
    # not emit the auto-link pragma either and has nothing to answer.
    find_package(TBB CONFIG QUIET)
    if(TARGET TBB::tbb)
        target_link_libraries(FreeCADRendererCycles PUBLIC TBB::tbb)
    endif()
    # FreeCADRenderer_EXPORTS: the objects land in the FreeCADRenderer
    # shared library, so RendererExport has to mean export here too.
    target_compile_definitions(FreeCADRendererCycles PRIVATE HAVE_CYCLES FreeCADRenderer_EXPORTS)
    # add_definitions(-DHAVE_MATERIALX) above reaches this target too
    # (same directory), so it must be able to include the headers that
    # define promises: the interpreter of a MaterialX graph into ccl
    # nodes lives in CyclesScene.cpp.
    if(BUILD_MATERIALX)
        target_link_libraries(FreeCADRendererCycles PRIVATE MaterialXCore
                                                           MaterialXFormat
                                                           MaterialXGenShader)
    endif()
    set_target_properties(FreeCADRendererCycles PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    list(APPEND Renderer_PRIVATE_LIBS FreeCADRendererCycles)

    # The GPU devices compile their kernels at runtime from source:
    # nvcc is run on source/kernel/device/<api>/kernel.cu with -I on
    # source/, which the kernel includes "kernel/..." and "util/..."
    # against. So those two trees ship beside the binary, at the
    # resource path CyclesRenderer.cpp hands path_init(), in the build
    # tree and the install alike -- the arrangement the standalone
    # install uses (docs/CyclesIntegration.md sec 4.1).
    set(_cycles_src ${CMAKE_SOURCE_DIR}/src/3rdParty/cycles/src)
    set(_cycles_out ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/cycles/source)
    add_custom_target(Renderer_cycles_source ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${_cycles_src}/kernel ${_cycles_out}/kernel
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${_cycles_src}/util ${_cycles_out}/util
        COMMENT "cycles kernel source"
        VERBATIM)
    install(DIRECTORY ${_cycles_src}/kernel ${_cycles_src}/util
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Renderer/cycles/source)
    unset(_cycles_src)
    unset(_cycles_out)
else()
    list(APPEND Renderer_CPP_SRCS CyclesRenderer.cpp)
endif()

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
