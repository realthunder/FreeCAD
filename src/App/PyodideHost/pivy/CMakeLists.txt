# pivy.coin for the pyodide guest (docs/Sandbox.md 7.10, Probe A): the
# SWIG wrapper of Coin compiled by emscripten into a CPython extension for
# pyodide's wasm32-emscripten interpreter, Coin linked STATICALLY into it
# (a side module exports only what it lists, so a second side module for
# Coin would export nothing to this one), wrapped into a `pivy` wheel the
# host loads beside fcx_image.  A STANDALONE cross project like ../guest:
#
#   # Coin first, from the Coin fork with its GL stub:
#   cd ~/works/sw/coin && emcmake cmake -S . -B build_wasm -G Ninja \
#     -DCMAKE_BUILD_TYPE=Release -DCOIN_BUILD_SHARED_LIBS=OFF -DCOIN_BUILD_TESTS=OFF \
#     -DCOIN_BUILD_GLX=OFF -DCOIN_BUILD_EGL=OFF -DCOIN_BUILD_GL_STUB=ON -DHAVE_SOUND=OFF \
#     -DBoost_INCLUDE_DIR=$FCAD/.conda/freecad/include \
#     -DCMAKE_CXX_FLAGS="-fPIC -fwasm-exceptions -sSUPPORT_LONGJMP=wasm" \
#     -DCMAKE_C_FLAGS="-fPIC -fwasm-exceptions -sSUPPORT_LONGJMP=wasm" \
#     -DCMAKE_INSTALL_PREFIX=$PWD/install/wasm && ninja -C build_wasm install
#   # then this:
#   source src/App/PyodideHost/guest/emsdk-env.sh
#   emcmake cmake -S src/App/PyodideHost/pivy -B build/pyodide-pivy -G Ninja \
#     -DPIVY_SOURCE_DIR=$HOME/works/sw/pivy -DCOIN_WASM_PREFIX=$HOME/works/sw/coin/install/wasm \
#     -DSWIG_EXECUTABLE=$PWD/.conda/freecad/bin/swig
#   cmake --build build/pyodide-pivy
#   -> build/pyodide-pivy/dist/pivy-<ver>-cp314-cp314-pyodide_2026_0_wasm32.whl
#
# The pivy checkout must have had install_helpers.py run against a Coin
# install with the same headers (its native build does that at configure):
# SWIG reads the swigified copies under <pivy>/Inventor, never Coin's own.
cmake_minimum_required(VERSION 3.20)
project(FCPyodidePivy C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "configure with emcmake (source guest/emsdk-env.sh first)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(PIVY_SOURCE_DIR "" CACHE PATH "The pivy checkout (interfaces/coin.i, pivy/, the swigified Inventor/)")
set(COIN_WASM_PREFIX "" CACHE PATH "The Coin fork installed for wasm (include/, lib/libCoinRT.a)")
set(SWIG_EXECUTABLE "" CACHE FILEPATH "SWIG 4.x on the host")
set(PYODIDE_PYTHON_INCLUDE "$ENV{PYODIDE_PYTHON_INCLUDE}" CACHE PATH
    "pyodide's cross-build Python headers (emsdk-env.sh exports it)")
set(PYODIDE_ABI "cp314" CACHE STRING "CPython ABI tag of the target pyodide")
set(PYODIDE_PLATFORM "pyodide_2026_0_wasm32" CACHE STRING "platform tag of the target pyodide")
set(PIVY_WHEEL_VERSION "0.6.10" CACHE STRING "wheel version")

foreach(var PIVY_SOURCE_DIR COIN_WASM_PREFIX SWIG_EXECUTABLE PYODIDE_PYTHON_INCLUDE)
    if("${${var}}" STREQUAL "")
        message(FATAL_ERROR "${var} must be set")
    endif()
endforeach()

# not find_library: the Emscripten toolchain confines it to the sysroot
set(COIN_WASM_LIB ${COIN_WASM_PREFIX}/lib/libCoinRT.a)
if(NOT EXISTS ${COIN_WASM_LIB})
    set(COIN_WASM_LIB ${COIN_WASM_PREFIX}/lib/libCoin.a)
endif()
if(NOT EXISTS ${COIN_WASM_LIB})
    message(FATAL_ERROR "no libCoinRT.a / libCoin.a under ${COIN_WASM_PREFIX}/lib")
endif()
set(COIN_WASM_INCLUDE ${COIN_WASM_PREFIX}/include)

# ---- SWIG on the host: the wrapper and the Python half ----
set(_wrap ${CMAKE_CURRENT_BINARY_DIR}/coinPYTHON_wrap.cxx)
set(_stage ${CMAKE_CURRENT_BINARY_DIR}/stage/pivy)
file(MAKE_DIRECTORY ${_stage})
add_custom_command(
    OUTPUT ${_wrap} ${_stage}/coin.py
    COMMAND ${SWIG_EXECUTABLE} -python -c++ -includeall -D__PIVY__
            -I${PIVY_SOURCE_DIR} -I${PIVY_SOURCE_DIR}/fake_headers -I${COIN_WASM_INCLUDE}
            -w302,306,307,312,314,325,361,362,467,389,503,509,510
            -interface _coin -outdir ${_stage} -o ${_wrap}
            ${PIVY_SOURCE_DIR}/interfaces/coin.i
    DEPENDS ${PIVY_SOURCE_DIR}/interfaces/coin.i ${PIVY_SOURCE_DIR}/interfaces/coin_header_includes.h
    COMMENT "SWIG: pivy.coin wrapper"
)

# ---- the extension, Coin inside it ----
# An executable target, not add_library(MODULE): CMake's Emscripten
# platform archives a MODULE with emar (see ../guest/CMakeLists.txt).
add_executable(_coin ${_wrap})
string(REPLACE "cp" "cpython-" _soabi ${PYODIDE_ABI})
set(_soname "_coin.${_soabi}-wasm32-emscripten.so")
set_target_properties(_coin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "_coin"
    SUFFIX ".${_soabi}-wasm32-emscripten.so"
    RUNTIME_OUTPUT_DIRECTORY ${_stage})
target_include_directories(_coin PRIVATE
    ${PIVY_SOURCE_DIR}/interfaces
    ${COIN_WASM_INCLUDE}
    ${PYODIDE_PYTHON_INCLUDE}
)
target_compile_definitions(_coin PRIVATE COIN_NOT_DLL PY_CALL_TRAMPOLINE)
target_compile_options(_coin PRIVATE
    -fPIC -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -fvisibility=hidden
    -Wno-deprecated-declarations
)
target_link_options(_coin PRIVATE
    -fwasm-exceptions -sSUPPORT_LONGJMP=wasm
    -sSIDE_MODULE=2 -sWASM_BIGINT
    "-sEXPORTED_FUNCTIONS=[_PyInit__coin]"
)
target_link_libraries(_coin PRIVATE ${COIN_WASM_LIB})

# ---- the wheel ----
set(_wheel_dir ${CMAKE_CURRENT_BINARY_DIR}/dist)
set(_wheel ${_wheel_dir}/pivy-${PIVY_WHEEL_VERSION}-${PYODIDE_ABI}-${PYODIDE_ABI}-${PYODIDE_PLATFORM}.whl)
set(_pkg_files ${PIVY_SOURCE_DIR}/pivy/__init__.py ${PIVY_SOURCE_DIR}/pivy/pivy_meta.py)
add_custom_command(
    OUTPUT ${_wheel}
    COMMAND ${CMAKE_COMMAND} -E copy ${_pkg_files} ${_stage}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_wheel_dir}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../guest/make_wheel.py
            --name pivy --version ${PIVY_WHEEL_VERSION} --out ${_wheel_dir}
            --abi ${PYODIDE_ABI} --platform ${PYODIDE_PLATFORM}
            ${_stage}
    DEPENDS _coin ${_stage}/coin.py ${_pkg_files} ${CMAKE_CURRENT_SOURCE_DIR}/../guest/make_wheel.py
    COMMENT "Packing the pivy wheel for pyodide"
)
add_custom_target(pivy_wheel ALL DEPENDS ${_wheel})
