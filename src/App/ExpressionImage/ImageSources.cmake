# The sandbox image's source slice, shared by its two guests: the
# wasm32-wasi reactor (CMakeLists.txt beside this file, run under wasmtime)
# and the pyodide extension module (src/App/PyodideHost/guest, run in
# pyodide's CPython on V8).  One list, so the two guests can never drift
# apart in what they compile; docs/ExpressionImage.md "The core carve".
#
# Expects FC_SRC = the absolute path of the repository's src/ directory.

# The ExpressionCore carve: the real parser + AST walker + identifier code
# compiled against the S1 adapter world (FcxDocument.*), selected by
# FC_EXPR_IMAGE.
set(FCX_CORE_SLICE
    ${FC_SRC}/App/Expression.cpp
    ${FC_SRC}/App/ObjectIdentifier.cpp
    ${FC_SRC}/App/Range.cpp
    ${FC_SRC}/App/ExpressionImage/FcxDocument.cpp
)

# The Base math slice: pure math TUs plus their Python bindings.  The
# binding TU is the *PyImp.cpp, which #includes the generated *Py.cpp.
set(FCX_BASE_SLICE
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

set(FCX_PYCXX_SRCS
    ${FC_SRC}/CXX/cxx_extensions.cxx
    ${FC_SRC}/CXX/cxx_exceptions.cxx
    ${FC_SRC}/CXX/cxxsupport.cxx
    ${FC_SRC}/CXX/cxxextensions.c
    ${FC_SRC}/CXX/IndirectPythonInterface.cxx
)

# The image's own machinery, minus the entry file each guest supplies
# (ImageMain.cpp / ImageModule.cpp).
set(FCX_IMAGE_COMMON
    ${FC_SRC}/App/ExpressionImage/ImageBridge.cpp
    ${FC_SRC}/App/ExpressionImage/ImageDispatch.cpp
    ${FC_SRC}/App/ExpressionImage/ImageMarshal.cpp
    ${FC_SRC}/App/ExpressionImage/ImageStubs.cpp
)

# Include directories every guest needs beyond its Python headers.
# FREECAD_GENERATED_DIR is a host build tree's src/ with the generated
# *Py.h/cpp; BOOST_INCLUDE_DIR is header-only use.
set(FCX_IMAGE_INCLUDES
    ${FC_SRC}
    ${FREECAD_GENERATED_DIR}
    ${FREECAD_GENERATED_DIR}/Base
    ${BOOST_INCLUDE_DIR}
    ${FC_SRC}/3rdParty/FastSignals/libfastsignals/include
    ${FC_SRC}/3rdParty/json/single_include
)

set(FCX_IMAGE_DEFINES
    FC_EXPR_IMAGE
    FC_NO_QT
    BOOST_DISABLE_THREADS
    BOOST_SYSTEM_DISABLE_THREADS
)

# The in-image facade classes, generated from the <Sandbox tier=.../>
# annotations in the binding XMLs (docs/ExpressionSandbox.md sec 7.5).
# The host build runs the same script for its dispatch table, so both
# sides of the boundary always agree.  Sets FCX_FACADES_INC to the
# generated file, to be added to the guest target's sources.
function(fcx_add_facades_command)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(FCX_FACADES_INC ${CMAKE_CURRENT_BINARY_DIR}/FcxFacades.inc PARENT_SCOPE)
    set(gen ${FC_SRC}/Tools/bindings/generateSandboxFacades.py)
    # The XML list is the generator's own (ANNOTATED_XMLS), asked for at
    # configure time so it cannot drift from what the generator reads.
    get_filename_component(_fcx_root ${FC_SRC} DIRECTORY)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${gen} --list-xmls
        OUTPUT_VARIABLE _fcx_annotated_xmls
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _fcx_list_rc)
    if(NOT _fcx_list_rc EQUAL 0)
        message(FATAL_ERROR "${gen} --list-xmls failed")
    endif()
    string(REPLACE "\n" ";" _fcx_annotated_xmls "${_fcx_annotated_xmls}")
    list(TRANSFORM _fcx_annotated_xmls PREPEND ${_fcx_root}/)
    # The list lives in the generator: when an XML joins ANNOTATED_XMLS
    # the configure must run again, or the new file's edits never
    # regenerate the table (a guest tree sat on a stale DEPENDS list
    # through two annotations, 2026-09-04).
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${gen})
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/FcxFacades.inc
        COMMAND Python3::Interpreter ${gen} --image-out ${CMAKE_CURRENT_BINARY_DIR}/FcxFacades.inc
        DEPENDS ${gen} ${_fcx_annotated_xmls}
        COMMENT "Generating the expression sandbox facades (FcxFacades.inc)"
    )
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/FcxFacades.inc PROPERTIES
        GENERATED TRUE HEADER_FILE_ONLY TRUE)
endfunction()
