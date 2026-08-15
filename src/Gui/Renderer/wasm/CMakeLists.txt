# Standalone WebAssembly build of the FreeCAD bgfx renderer.
#
# Configure with the Emscripten toolchain, out of the main build tree:
#   source ~/works/sw/emsdk/emsdk_env.sh
#   emcmake cmake -S src/Gui/Renderer/wasm -B build/wasm -G Ninja \
#       -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/wasm
#
# Output: build/wasm/fcviewer.{html,js,wasm,data}. Serve the directory over
# http (e.g. python3 -m http.server) and open fcviewer.html; the .data pack
# preloads the compiled essl shaders and, when present, a scene dump
# captured on the desktop with FC_BGFX_DUMP_SCENE (see ../SceneDump.h).

cmake_minimum_required(VERSION 3.20)
project(FreeCADRendererWasm CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project must be configured with emcmake/emcc.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(FC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)

# --- vendored bgfx (static; tools/examples off) --------------------------
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLE_COMMON OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
# GLES3 profile: off-Android the config defaults to GLES2, where core-in-ES3
# entry points (glRenderbufferStorageMultisample, ...) are only reachable
# through EXT/IMG-suffixed proc lookups WebGL2 does not export — the MSAA
# render targets then call a NULL import. The viewer is WebGL2-only anyway
# (MIN_WEBGL_VERSION=2), so compile bgfx against the ES3 headers and link
# the core functions statically.
set(BGFX_OPENGLES_VERSION 30 CACHE STRING "" FORCE)
add_subdirectory(${FC_SRC}/3rdParty/bgfx bgfx EXCLUDE_FROM_ALL)

add_executable(fcviewer
    main.cpp
    ../Renderer.cpp
    ../SceneDump.cpp
    ../SceneLadder.cpp
    ../MeshSimplify.cpp
    ../BGFXRenderer.cpp
    ../BGFXFrame.cpp
    ../BGFXScene.cpp
    ../BGFXViewEffects.cpp
    ../BGFXViewEnv.cpp
    ../BGFXViewLifecycle.cpp
    ../BGFXViewOverlay.cpp
    ../BGFXViewParticles.cpp
    ../BGFXViewShadow.cpp
    ../BGFXViewSubmit.cpp
    ../StandalonePlatform.cpp
)

target_compile_definitions(fcviewer PRIVATE
    FC_RENDERER_STANDALONE
    FreeCADRenderer_STATIC
    HAVE_BGFX
)

target_include_directories(fcviewer PRIVATE
    ${FC_SRC}                  # FCConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(fcviewer PRIVATE bgfx bimg bx)

# The shaders compile at build time from source (no committed .bin
# files). An Emscripten build cannot build the host shaderc tool itself,
# so it uses one from a native build tree — the conda-debug desktop
# build by default; point FCVIEWER_SHADERC elsewhere if that build does
# not exist.
set(FCVIEWER_SHADERC
    "${FC_SRC}/../build/conda-debug/src/3rdParty/bgfx/cmake/bgfx/shaderc"
    CACHE FILEPATH "Host shaderc binary for essl shader compilation")
if(NOT EXISTS ${FCVIEWER_SHADERC})
    message(FATAL_ERROR
        "host shaderc not found: ${FCVIEWER_SHADERC}\n"
        "Build the desktop tree first (it builds shaderc), or set "
        "-DFCVIEWER_SHADERC=/path/to/shaderc.")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../BGFXShaders.cmake)
fc_bgfx_compile_shaders(fcviewer_shader_bins
    SHADERC   ${FCVIEWER_SHADERC}
    SHADERDIR ${FC_SRC}/Gui/Renderer/bgfx/shaders
    BGFXINC   ${FC_SRC}/3rdParty/bgfx/bgfx/src
    OUTDIR    ${CMAKE_BINARY_DIR}/assets/shaders
    PROFILES  essl)
add_custom_target(fcviewer_shaders DEPENDS ${fcviewer_shader_bins})
add_dependencies(fcviewer fcviewer_shaders)

target_link_options(fcviewer PRIVATE
    -sMAX_WEBGL_VERSION=2
    -sMIN_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1
    -sFETCH=1
    -lwebsocket.js
    -sSTACK_SIZE=1048576
    -sEXIT_RUNTIME=0
    # Compiled essl shaders at the resource path the renderer resolves
    # (setResourcePath("/assets") + "bgfx/assets/shaders/essl/...").
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/assets/shaders/essl@/assets/bgfx/assets/shaders/essl"
    # Mobile-friendly HTML shell (viewport meta + full-viewport, touch-action
    # canvas) instead of the stock desktop emscripten template.
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)

# Optional desktop-captured scene snapshot (FC_BGFX_DUMP_SCENE) baked
# into the preload pack as /scene.fcsd.
set(FCVIEWER_SCENE "" CACHE FILEPATH "Scene snapshot (.fcsd) to preload")
if(FCVIEWER_SCENE)
    target_link_options(fcviewer PRIVATE
        "SHELL:--preload-file ${FCVIEWER_SCENE}@/scene.fcsd")
endif()

# Relink (repack fcviewer.data) when shader bins or the snapshot change.
set_property(TARGET fcviewer APPEND PROPERTY
    LINK_DEPENDS ${fcviewer_shader_bins} ${FCVIEWER_SCENE}
    ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

set_target_properties(fcviewer PROPERTIES SUFFIX ".html")

# Build stamp next to the bundle: the page reports it in the hello
# handshake and the backend (FC_BGFX_VIEWER_BUILD pointing here) pushes
# a reload to any open page whose stamp no longer matches — see
# stamp.cmake.
add_custom_command(TARGET fcviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/stamp.cmake
    COMMENT "Stamping fcviewer bundle")
