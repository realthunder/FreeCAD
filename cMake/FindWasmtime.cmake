# Find the wasmtime C API: the runtime that executes the expression sandbox
# image (docs/ExpressionImage.md).
#
# Hints, in order:
#   WASMTIME_CAPI_DIR   cache variable or environment, a prefix holding
#                       include/wasmtime.h and lib/libwasmtime.so
#   the usual system prefixes (a distribution or conda package)
#
# Sets:
#   Wasmtime_FOUND          usable runtime present
#   Wasmtime_INCLUDE_DIR    directory holding wasmtime.h
#   Wasmtime_LIBRARY        the shared library to link
#   Wasmtime_HAS_EXCEPTIONS the headers expose the exception-handling knob
#   Wasmtime::Wasmtime      imported target
#
# The version is NOT checked.  What the image actually needs is the
# WebAssembly exception-handling proposal, which wasmtime gates behind
# wasmtime_config_wasm_exceptions_set(); the header either declares it or
# the runtime is too old, and that reads better in a build log than a
# version number nobody can map to a feature.

set(_wasmtime_hint "${WASMTIME_CAPI_DIR}")
if(NOT _wasmtime_hint AND DEFINED ENV{WASMTIME_CAPI_DIR})
    set(_wasmtime_hint "$ENV{WASMTIME_CAPI_DIR}")
endif()

find_path(Wasmtime_INCLUDE_DIR
    NAMES wasmtime.h
    HINTS ${_wasmtime_hint}
    PATH_SUFFIXES include
    DOC "Directory holding wasmtime.h")

find_library(Wasmtime_LIBRARY
    NAMES wasmtime
    HINTS ${_wasmtime_hint}
    PATH_SUFFIXES lib lib64
    DOC "The wasmtime C API shared library")

set(Wasmtime_HAS_EXCEPTIONS FALSE)
if(Wasmtime_INCLUDE_DIR AND EXISTS "${Wasmtime_INCLUDE_DIR}/wasmtime/config.h")
    file(STRINGS "${Wasmtime_INCLUDE_DIR}/wasmtime/config.h" _wasmtime_eh
         REGEX "WASMTIME_CONFIG_PROP\\(void, wasm_exceptions")
    if(_wasmtime_eh)
        set(Wasmtime_HAS_EXCEPTIONS TRUE)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Wasmtime
    REQUIRED_VARS Wasmtime_LIBRARY Wasmtime_INCLUDE_DIR Wasmtime_HAS_EXCEPTIONS
    REASON_FAILURE_MESSAGE "set WASMTIME_CAPI_DIR to a wasmtime C API prefix whose headers declare the exception-handling knob -- see docs/ExpressionImage.md")

if(Wasmtime_FOUND AND NOT TARGET Wasmtime::Wasmtime)
    # SHARED, not UNKNOWN, when it is one: IMPORTED_NO_SONAME below is
    # honoured only for a target CMake knows is a shared library, and an
    # UNKNOWN one is linked by its path no matter what the property says.
    if(Wasmtime_LIBRARY MATCHES "${CMAKE_SHARED_LIBRARY_SUFFIX}$")
        add_library(Wasmtime::Wasmtime SHARED IMPORTED)
    else()
        # The static archive is a Rust staticlib: its own std needs the
        # system libraries that the shared build already links for it,
        # and they have to come AFTER the archive on the link line --
        # without them the failure is a wall of undefined `dlsym`.
        add_library(Wasmtime::Wasmtime UNKNOWN IMPORTED)
        set_property(TARGET Wasmtime::Wasmtime PROPERTY
            INTERFACE_LINK_LIBRARIES ${CMAKE_DL_LIBS} pthread m)
    endif()
    set_target_properties(Wasmtime::Wasmtime PROPERTIES
        IMPORTED_LOCATION "${Wasmtime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Wasmtime_INCLUDE_DIR}"
        # The release tarball's libwasmtime.so has NO SONAME, and linking
        # an imported target by its IMPORTED_LOCATION then records that
        # ABSOLUTE PATH as the DT_NEEDED entry -- the installed FreeCAD
        # would go looking in whatever directory the build machine had.
        # This property is the documented cure: link -L<dir> -lwasmtime
        # instead, so DT_NEEDED is the SONAME when there is one and the
        # bare file name when there is not, and an RPATH can answer it.
        IMPORTED_NO_SONAME TRUE)
endif()

mark_as_advanced(Wasmtime_INCLUDE_DIR Wasmtime_LIBRARY)
