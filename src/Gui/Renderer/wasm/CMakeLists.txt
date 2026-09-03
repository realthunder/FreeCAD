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
project(FreeCADRendererWasm C CXX)

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

# vg-renderer, for the 2D page tier (docs/TechDrawPortAndSection.md
# sec 24). Defined locally -- the 3rdParty block is Qt/FreeCAD-shaped
# and cannot be included here. Its shaders are committed embedded C
# arrays carrying an essl variant, so the WebGL backend needs no
# shaderc pass.
set(FC_VG ${FC_SRC}/3rdParty/vg-renderer)
add_library(vg-renderer STATIC
    ${FC_VG}/src/vg.cpp
    ${FC_VG}/src/path.cpp
    ${FC_VG}/src/stroker.cpp
    ${FC_VG}/src/vg_util.cpp
    ${FC_VG}/src/libs/fontstash.cpp
    ${FC_VG}/src/libs/stb_truetype.cpp
    ${FC_VG}/src/libtess2/bucketalloc.c
    ${FC_VG}/src/libtess2/dict.c
    ${FC_VG}/src/libtess2/geom.c
    ${FC_VG}/src/libtess2/mesh.c
    ${FC_VG}/src/libtess2/priorityq.c
    ${FC_VG}/src/libtess2/sweep.c
    ${FC_VG}/src/libtess2/tess.c
)
target_include_directories(vg-renderer PUBLIC ${FC_VG}/include)
target_link_libraries(vg-renderer PUBLIC bx bgfx)
target_compile_definitions(vg-renderer PRIVATE
    BGFX_PLATFORM_SUPPORTS_DXBC=0
    BGFX_PLATFORM_SUPPORTS_DXIL=0)
set_target_properties(vg-renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

add_executable(fcviewer
    main.cpp
    ../Page2D.cpp
    ../Page2DWire.cpp
    ../Vg2D.cpp
    ../Renderer.cpp
    ../Environment.cpp
    ../ImageDecode.cpp
    ../SceneDump.cpp
    ../SceneLadder.cpp
    ../MeshSimplify.cpp
    ../BGFXRenderer.cpp
    # The draw facade: the streamed frame is blitted through a
    # FrameConsumer (docs/CyclesIntegration.md sec 7.1).
    ../DrawDevice.cpp
    ../BGFXDrawDevice.cpp
    ../FrameImageConsumer.cpp
    ../BGFXFrame.cpp
    ../BGFXScene.cpp
    ../BGFXViewEffects.cpp
    ../BGFXViewEnv.cpp
    ../BGFXViewLifecycle.cpp
    ../BGFXViewOverlay.cpp
    ../BGFXViewParticles.cpp
    ../BGFXViewShadow.cpp
    ../BGFXViewSubmit.cpp
    # The occlusion culler and the spatial index it walks: the frame path
    # calls them unguarded, so this tier links them like the desktop does.
    # Dependency-light by design (no Qt, no App) -- MeshSource.cpp is the
    # one neighbour that is not, and stays out.
    ../OcclusionCull.cpp
    ../ProxyHierarchy.cpp
    ../CullBenefit.cpp
    # The software oracle. Its SIMD layer already has a wasm_simd128
    # backend (Simd4.h) and falls back to scalar without -msimd128, and
    # its platform probes are guarded, so it ports as-is.
    ../MaskedOcclusion.cpp
    ../OccluderMesh.cpp
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
    ${FC_SRC}/3rdParty/bgfx/bimg/3rdparty   # stb/stb_image.h, the frame decoder
)

target_link_libraries(fcviewer PRIVATE vg-renderer bgfx bimg bx)

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

# --- the DOM UI layer (docs/ThinClient.md) --------------------------------
#
# The menu and the HUD card are a separate vite bundle under ../web, which
# shell.html loads as `web/inspector.js`. The canvas deliberately does not
# depend on it -- a page served without it 404s that one request and runs
# bare -- and that is exactly why its absence is silent: the viewer comes
# up looking like a viewer, just with no chrome, and the only evidence is
# one console line. Building it here is what makes `cmake --build
# build/wasm` produce the whole page instead of half of it.
#
# Built by default. With it on, the node toolchain is a HARD dependency,
# like emscripten and the host shaderc above: the page is not the canvas,
# it is the canvas plus its chrome, and a build that quietly produces the
# first half has produced something that looks finished and is not, which
# is exactly how this went unnoticed. Missing tools stop the configure
# here rather than surfacing as one console line in a browser later.
#
# -DFCVIEWER_UI=OFF is the way to build without node at all -- an
# explicit choice to produce the bare page, rather than a silent
# fallback. shell.html then 404s web/inspector.js and logs "fcviewer-ui:
# bundle failed to load", and the viewer runs with no menu and no HUD
# card.
option(FCVIEWER_UI "Build the viewer's DOM UI bundle (requires node/npm)" ON)
if(FCVIEWER_UI)
    find_program(FCVIEWER_NPM npm)
    find_program(FCVIEWER_NODE node)
    if(NOT FCVIEWER_NPM OR NOT FCVIEWER_NODE)
        message(FATAL_ERROR
            "the WASM viewer needs the node toolchain to build its DOM UI "
            "bundle -- the menu and the HUD card (src/Gui/Renderer/web, built "
            "with vite).\n"
            "  node: ${FCVIEWER_NODE}\n"
            "  npm:  ${FCVIEWER_NPM}\n"
            "Install node and npm (or put them on PATH) and configure again.")
    endif()
    # vite 6 asks for node >= 20. 19.x builds the bundle correctly and only
    # warns (EBADENGINE), so this says so rather than failing a build that
    # works -- but it is the first thing to suspect if the bundle misbehaves.
    execute_process(COMMAND ${FCVIEWER_NODE} --version
                    OUTPUT_VARIABLE FCVIEWER_NODE_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    string(REGEX REPLACE "^v" "" FCVIEWER_NODE_VERSION "${FCVIEWER_NODE_VERSION}")
    if(FCVIEWER_NODE_VERSION AND FCVIEWER_NODE_VERSION VERSION_LESS 20)
        message(STATUS
            "fcviewer UI: node ${FCVIEWER_NODE_VERSION} is below the 20 vite 6 "
            "asks for; the bundle still builds, with an EBADENGINE warning")
    endif()
    set(FCVIEWER_UI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../web)
    file(GLOB_RECURSE FCVIEWER_UI_FILES CONFIGURE_DEPENDS
         ${FCVIEWER_UI_SRC}/src/*.ts ${FCVIEWER_UI_SRC}/src/*.tsx
         ${FCVIEWER_UI_SRC}/src/*.css)
    # Keyed on the manifest, so the install runs when the dependencies change
    # and not on every build. The script also decides which rollup this
    # machine can run (see UiDeps.cmake) -- a build-time question, because it
    # depends on what the install just put in node_modules.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web-deps.stamp
        COMMAND ${CMAKE_COMMAND}
                -DNPM=${FCVIEWER_NPM}
                -DNODE=${FCVIEWER_NODE}
                -DUI_SRC=${FCVIEWER_UI_SRC}
                -DSTAMP=${CMAKE_CURRENT_BINARY_DIR}/web-deps.stamp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/UiDeps.cmake
        DEPENDS ${FCVIEWER_UI_SRC}/package.json
                ${CMAKE_CURRENT_SOURCE_DIR}/UiDeps.cmake
        COMMENT "fcviewer UI: installing npm dependencies"
        VERBATIM)
    # --outDir keeps CMake authoritative about where the bundle lands, rather
    # than the tree-relative path baked into vite.config.ts (which stays, so a
    # bare `npm run build` still works on its own).
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/web/inspector.js
        COMMAND ${FCVIEWER_NPM} run build --
                --outDir ${CMAKE_CURRENT_BINARY_DIR}/web --emptyOutDir
        WORKING_DIRECTORY ${FCVIEWER_UI_SRC}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/web-deps.stamp
                ${FCVIEWER_UI_FILES} ${FCVIEWER_UI_SRC}/vite.config.ts
        COMMENT "fcviewer UI: building the DOM bundle (vite)"
        VERBATIM)
    add_custom_target(fcviewer_ui ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/web/inspector.js)
    # So that asking for the viewer gets the whole page, not just the canvas
    # half of it.
    add_dependencies(fcviewer fcviewer_ui)
endif()

# Build stamp next to the bundle: the page reports it in the hello
# handshake and the backend (FC_BGFX_VIEWER_BUILD pointing here) pushes
# a reload to any open page whose stamp no longer matches — see
# stamp.cmake.
add_custom_command(TARGET fcviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/stamp.cmake
    COMMENT "Stamping fcviewer bundle")
