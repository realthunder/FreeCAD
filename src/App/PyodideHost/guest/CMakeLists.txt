# The pyodide guest of the expression sandbox: the image's source slice
# (src/App/ExpressionImage/ImageSources.cmake) compiled by emscripten into
# a CPython extension module for pyodide's wasm32-emscripten interpreter,
# and wrapped into a wheel the host loads through its scoped reader
# (docs/PyodideHost.md).  A STANDALONE cross project, like the wasi image:
#
#   source src/App/PyodideHost/guest/emsdk-env.sh
#   emcmake cmake -S src/App/PyodideHost/guest -B build/pyodide-guest -G Ninja \
#     -DFREECAD_GENERATED_DIR=$PWD/build/conda-relwithdebinfo-801/src \
#     -DBOOST_INCLUDE_DIR=$PWD/.conda/freecad/include
#   cmake --build build/pyodide-guest
#   -> build/pyodide-guest/dist/fcx_image-<ver>-cp314-cp314-pyodide_2026_0_wasm32.whl
#
# The flags are pyodide's own for extension modules (its Makefile.envs
# and pyodide-build's pywasmcross): the same emscripten version, wasm
# exceptions, wasm longjmp, the call trampoline define, and a side
# module that exports its PyInit.  Getting any of these wrong does not
# fail the build; it fails at import in the probe.
cmake_minimum_required(VERSION 3.20)
project(FCPyodideGuest C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "configure with emcmake (source guest/emsdk-env.sh first)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(FREECAD_GENERATED_DIR "" CACHE PATH
    "A FreeCAD build tree's src/ dir holding the generated *Py.h/cpp")
set(BOOST_INCLUDE_DIR "" CACHE PATH "Boost headers (header-only use)")
set(PYODIDE_PYTHON_INCLUDE "$ENV{PYODIDE_PYTHON_INCLUDE}" CACHE PATH
    "pyodide's cross-build Python headers (emsdk-env.sh exports it)")
# The wheel tag pyodide checks at load; from pyodide-lock.json "info".
set(PYODIDE_ABI "cp314" CACHE STRING "CPython ABI tag of the target pyodide")
set(PYODIDE_PLATFORM "pyodide_2026_0_wasm32" CACHE STRING "platform tag of the target pyodide")
set(FCX_IMAGE_VERSION "0.1" CACHE STRING "wheel version")

foreach(var FREECAD_GENERATED_DIR BOOST_INCLUDE_DIR PYODIDE_PYTHON_INCLUDE)
    if(NOT ${var})
        message(FATAL_ERROR "${var} must be set")
    endif()
endforeach()

get_filename_component(FC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
include(${FC_SRC}/App/ExpressionImage/ImageSources.cmake)
fcx_add_facades_command()

# An executable target, not add_library(MODULE): CMake's Emscripten
# platform archives a MODULE with emar, and an archive is not a side
# module.  emcc picks the output kind from the extension, and a .so with
# -sSIDE_MODULE is a wasm shared library.
add_executable(_fcx_image
    ${FC_SRC}/App/ExpressionImage/ImageModule.cpp
    ${FCX_FACADES_INC}
    ${FCX_IMAGE_COMMON}
    ${FCX_CORE_SLICE}
    ${FCX_BASE_SLICE}
    ${FCX_PYCXX_SRCS}
)
string(REPLACE "cp" "cpython-" _soabi ${PYODIDE_ABI})
set_target_properties(_fcx_image PROPERTIES
    PREFIX ""
    SUFFIX ".${_soabi}-wasm32-emscripten.so")

target_include_directories(_fcx_image PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FC_SRC}/App/ExpressionImage
    ${FCX_IMAGE_INCLUDES}
    ${PYODIDE_PYTHON_INCLUDE}
)
target_compile_definitions(_fcx_image PRIVATE
    ${FCX_IMAGE_DEFINES}
    FC_EXPR_PYODIDE
    PY_CALL_TRAMPOLINE
)
target_compile_options(_fcx_image PRIVATE
    -fPIC -fwasm-exceptions -sSUPPORT_LONGJMP=wasm
    -fvisibility=hidden -flto
)
target_link_options(_fcx_image PRIVATE
    -fwasm-exceptions -sSUPPORT_LONGJMP=wasm
    -flto
    -sSIDE_MODULE=2 -sWASM_BIGINT
    "-sEXPORTED_FUNCTIONS=[_PyInit__fcx_image]"
)

# The wheel, rebuilt whenever the module is.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(FCX_WHEEL ${CMAKE_CURRENT_BINARY_DIR}/dist/fcx_image-${FCX_IMAGE_VERSION}-${PYODIDE_ABI}-${PYODIDE_ABI}-${PYODIDE_PLATFORM}.whl)
add_custom_command(
    OUTPUT ${FCX_WHEEL}
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/make_wheel.py
            --name fcx_image --version ${FCX_IMAGE_VERSION}
            --abi ${PYODIDE_ABI} --platform ${PYODIDE_PLATFORM}
            --summary "FreeCAD expression sandbox image for pyodide"
            --out ${CMAKE_CURRENT_BINARY_DIR}/dist
            $<TARGET_FILE:_fcx_image>
    DEPENDS _fcx_image ${CMAKE_CURRENT_SOURCE_DIR}/make_wheel.py
    COMMENT "Wrapping _fcx_image into a wheel")
add_custom_target(fcx_image_wheel ALL DEPENDS ${FCX_WHEEL})
