# The expression sandbox image: CPython + the Base math bindings (Ring 0
# of docs/ExpressionSandbox.md sec 7.4) compiled to a wasm32-wasi reactor
# module.  This is a STANDALONE cross project, configured separately from
# the host build (same pattern as the wasm viewer, src/Gui/Renderer/wasm):
#
#   cmake -B build/wasi-image -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$WASI_SDK/share/cmake/wasi-sdk.cmake \
#     -DWASI_SDK_PREFIX=$WASI_SDK \
#     -DCPYTHON_WASI_SRC=$HOME/works/sw/cpython-wasi/Python-3.12.13 \
#     -DFREECAD_GENERATED_DIR=$PWD/build/conda-relwithdebinfo-801/src \
#     -DBOOST_INCLUDE_DIR=$PWD/.conda/freecad/include \
#     src/App/ExpressionImage
#   cmake --build build/wasi-image
#
# Requirements (docs/ExpressionSandbox.md sec 6, CppExceptions.md in
# wasi-sdk): wasi-sdk >= 33 (ships the exceptions-enabled libc++), a
# CPython built for wasm32-wasi with the same SDK, and a host runtime
# with the wasm exception-handling proposal on (wasmtime -W exceptions=y).

cmake_minimum_required(VERSION 3.20)
project(FCExpressionImage C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to an optimized image.  Without this the project configures
# with an EMPTY build type, i.e. -O0 for every TU we compile -- CPython
# itself is -O3 from its own build, so the unoptimized half is exactly
# the glue on the hot path (CBOR, the marshaller, the expression walker),
# and it measured 3-5x slower per round trip.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/stage CACHE PATH
        "Where `cmake --install` stages the desktop bundle" FORCE)
endif()

set(CPYTHON_WASI_SRC "" CACHE PATH
    "CPython source tree with a builddir/wasi cross build in it")
set(FREECAD_GENERATED_DIR "" CACHE PATH
    "A FreeCAD build tree's src/ dir holding the generated *Py.h/cpp")
set(BOOST_INCLUDE_DIR "" CACHE PATH "Boost headers (header-only use)")

foreach(var CPYTHON_WASI_SRC FREECAD_GENERATED_DIR BOOST_INCLUDE_DIR)
    if(NOT ${var})
        message(FATAL_ERROR "${var} must be set")
    endif()
endforeach()

set(CPYTHON_WASI_BUILD ${CPYTHON_WASI_SRC}/builddir/wasi)
get_filename_component(FC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

# The source slice, shared with the pyodide guest
# (src/App/PyodideHost/guest): see ImageSources.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/ImageSources.cmake)
fcx_add_facades_command()

add_executable(fcx_image
    ImageMain.cpp
    ${FCX_FACADES_INC}
    ${FCX_IMAGE_COMMON}
    ${FCX_CORE_SLICE}
    ${FCX_BASE_SLICE}
    ${FCX_PYCXX_SRCS}
)
set_target_properties(fcx_image PROPERTIES SUFFIX ".wasm")

target_include_directories(fcx_image PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FCX_IMAGE_INCLUDES}
    ${CPYTHON_WASI_SRC}/Include
    ${CPYTHON_WASI_BUILD}
)

target_compile_definitions(fcx_image PRIVATE
    ${FCX_IMAGE_DEFINES}
    _WASI_EMULATED_SIGNAL
    _WASI_EMULATED_GETPID
    _WASI_EMULATED_PROCESS_CLOCKS
)

# C++ exceptions per wasi-sdk CppExceptions.md: the flags select the
# eh/ sysroot variant; the C TUs stay plain.
target_compile_options(fcx_image PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false>
)

target_link_options(fcx_image PRIVATE
    -mexec-model=reactor
    -fwasm-exceptions
    -z stack-size=1048576
    -Wl,--stack-first
    -Wl,--initial-memory=33554432
)

target_link_libraries(fcx_image PRIVATE
    ${CPYTHON_WASI_BUILD}/libpython3.12.a
    ${CPYTHON_WASI_BUILD}/Modules/_decimal/libmpdec/libmpdec.a
    ${CPYTHON_WASI_BUILD}/Modules/_hacl/libHacl_Hash_SHA2.a
    ${CPYTHON_WASI_BUILD}/Modules/expat/libexpat.a
    dl
    unwind
    wasi-emulated-signal
    wasi-emulated-getpid
    wasi-emulated-process-clocks
    m
)

# The shipping bundles (docs/ExpressionImage.md "The browser tier" and
# "Packaging"): the stripped image plus the sixteen CPython files the image
# actually opens -- once laid out for the browser (with a manifest, fetched
# over HTTP) and once for the desktop (installed as <datadir>/Fcx).  Built
# alongside the image so the three can never be out of step;
# scripts/fcx-web-stage.sh copies the browser one next to the viewer, and
# the host build installs the desktop one via FREECAD_EXPR_IMAGE_DIR.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_command(TARGET fcx_image POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_image.py
                --image $<TARGET_FILE:fcx_image>
                --stdlib ${CPYTHON_WASI_SRC}/Lib
                --out ${CMAKE_CURRENT_BINARY_DIR}/web
                --desktop-out ${CMAKE_CURRENT_BINARY_DIR}/desktop
        COMMENT "Packing the sandbox image for the browser and desktop tiers")

    # `cmake --install` stages the desktop bundle; with no prefix given it
    # lands in the build tree rather than in /usr/local, because this
    # project's output is data for another build to install, not something
    # to install system-wide on its own.
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/desktop/fcx_image.wasm
            DESTINATION .)
    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/desktop/Lib
            DESTINATION .)
else()
    message(WARNING "python3 not found: the shipping bundles will not be packed")
endif()
