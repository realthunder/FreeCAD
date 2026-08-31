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
# The ExpressionCore carve (docs/ExpressionImage.md): the real parser +
# AST walker + identifier code compiled against the S1 adapter world
# (FcxDocument.*), selected by FC_EXPR_IMAGE.
set(CORE_SLICE
    ${FC_SRC}/App/Expression.cpp
    ${FC_SRC}/App/ObjectIdentifier.cpp
    ${FC_SRC}/App/Range.cpp
    FcxDocument.cpp
)

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
    ${FC_SRC}/Base/BaseClassPyImp.cpp
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

# The in-image facade classes, generated from the <Sandbox tier=.../>
# annotations in the binding XMLs (docs/ExpressionSandbox.md sec 7.5).
# The host build runs the same script for its dispatch table, so both
# sides of the boundary always agree.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(FCX_FACADES_INC ${CMAKE_CURRENT_BINARY_DIR}/FcxFacades.inc)
set(FCX_SANDBOX_GEN ${FC_SRC}/Tools/bindings/generateSandboxFacades.py)
add_custom_command(
    OUTPUT ${FCX_FACADES_INC}
    COMMAND Python3::Interpreter ${FCX_SANDBOX_GEN} --image-out ${FCX_FACADES_INC}
    DEPENDS ${FCX_SANDBOX_GEN}
            ${FC_SRC}/App/ComplexGeoDataPy.xml
            ${FC_SRC}/App/DocumentObjectPy.xml
            ${FC_SRC}/App/DocumentPy.xml
            ${FC_SRC}/Mod/Part/App/TopoShapePy.xml
            ${FC_SRC}/Mod/Spreadsheet/App/SheetPy.xml
    COMMENT "Generating the expression sandbox facades (FcxFacades.inc)"
)

add_executable(fcx_image
    ImageBridge.cpp
    ImageMain.cpp
    ImageMarshal.cpp
    ImageStubs.cpp
    ${FCX_FACADES_INC}
    ${CORE_SLICE}
    ${BASE_SLICE}
    ${PYCXX_SRCS}
)
set_source_files_properties(${FCX_FACADES_INC} PROPERTIES
    GENERATED TRUE HEADER_FILE_ONLY TRUE)
set_target_properties(fcx_image PROPERTIES SUFFIX ".wasm")

target_include_directories(fcx_image PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
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
    FC_EXPR_IMAGE
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

# The browser bundle (docs/ExpressionImage.md "The browser tier"): the
# stripped image plus the CPython files the image actually opens, with a
# manifest.  Built alongside the image so the two can never be out of step;
# scripts/fcx-web-stage.sh copies the result next to the viewer.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_command(TARGET fcx_image POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/webpack_image.py
                --image $<TARGET_FILE:fcx_image>
                --stdlib ${CPYTHON_WASI_SRC}/Lib
                --out ${CMAKE_CURRENT_BINARY_DIR}/web
        COMMENT "Packing the sandbox image for the browser tier")
else()
    message(WARNING "python3 not found: the browser bundle will not be packed")
endif()
