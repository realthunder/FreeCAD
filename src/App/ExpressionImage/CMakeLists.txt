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

# The Base math slice: pure math TUs plus their Python bindings.  The
# binding TU is the *PyImp.cpp, which #includes the generated *Py.cpp.
set(BASE_SLICE
    ${FC_SRC}/Base/Type.cpp
    ${FC_SRC}/Base/BaseClass.cpp
    ${FC_SRC}/Base/Vector3D.cpp
    ${FC_SRC}/Base/Rotation.cpp
    ${FC_SRC}/Base/Matrix.cpp
    ${FC_SRC}/Base/Placement.cpp
    ${FC_SRC}/Base/DualQuaternion.cpp
    ${FC_SRC}/Base/Quantity.cpp
    ${FC_SRC}/Base/Unit.cpp
    ${FC_SRC}/Base/Exception.cpp
    ${FC_SRC}/Base/PyObjectBase.cpp
    ${FC_SRC}/Base/GeometryPyCXX.cpp
    ${FC_SRC}/Base/VectorPyImp.cpp
    ${FC_SRC}/Base/RotationPyImp.cpp
    ${FC_SRC}/Base/PlacementPyImp.cpp
    ${FC_SRC}/Base/MatrixPyImp.cpp
    ${FC_SRC}/Base/BoundBoxPyImp.cpp
    ${FC_SRC}/Base/QuantityPyImp.cpp
    ${FC_SRC}/Base/UnitPyImp.cpp
)

set(PYCXX_SRCS
    ${FC_SRC}/CXX/cxx_extensions.cxx
    ${FC_SRC}/CXX/cxx_exceptions.cxx
    ${FC_SRC}/CXX/cxxsupport.cxx
    ${FC_SRC}/CXX/cxxextensions.c
    ${FC_SRC}/CXX/IndirectPythonInterface.cxx
)

add_executable(fcx_image
    ImageBridge.cpp
    ImageMain.cpp
    ImageMarshal.cpp
    ImageStubs.cpp
    ${BASE_SLICE}
    ${PYCXX_SRCS}
)
set_target_properties(fcx_image PROPERTIES SUFFIX ".wasm")

target_include_directories(fcx_image PRIVATE
    ${FC_SRC}
    ${FREECAD_GENERATED_DIR}
    ${FREECAD_GENERATED_DIR}/Base
    ${CPYTHON_WASI_SRC}/Include
    ${CPYTHON_WASI_BUILD}
    ${BOOST_INCLUDE_DIR}
    ${FC_SRC}/3rdParty/FastSignals/libfastsignals/include
    ${FC_SRC}/3rdParty/json/single_include
)

target_compile_definitions(fcx_image PRIVATE
    FC_NO_QT
    BOOST_DISABLE_THREADS
    BOOST_SYSTEM_DISABLE_THREADS
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
