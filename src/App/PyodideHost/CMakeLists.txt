# The pyodide host: CPython-on-emscripten (pyodide) running on the bare V8
# that the v8-embed package ships, with FreeCAD supplying the Web
# environment the emscripten glue expects and deliberately nothing more.
# Phase 0 is a standalone probe (docs/PyodideHost.md); this is a STANDALONE
# project, configured separately from the host build, in the same pattern
# as src/App/ExpressionImage:
#
#   conda activate v8embed        # v8-embed + cxx-compiler + cmake + ninja
#   cmake -S src/App/PyodideHost -B build/pyodide-host -G Ninja
#   cmake --build build/pyodide-host
#   build/pyodide-host/pyodide_probe ~/works/sw/pyodide/npm-314.0.6
cmake_minimum_required(VERSION 3.20)
project(FCPyodideHost CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# The package's own config: the imported target carries the include dir
# and the V8_GN_HEADER define that makes v8config.h agree with the library.
find_package(v8-embed CONFIG REQUIRED)
message(STATUS "v8-embed ${V8_EMBED_VERSION}: ${V8_EMBED_LIBRARY}")

find_package(Threads REQUIRED)
add_executable(pyodide_probe PyodideProbe.cc)
target_link_libraries(pyodide_probe PRIVATE v8-embed::v8 Threads::Threads)
target_compile_definitions(pyodide_probe PRIVATE
    FCX_SHIM_JS="${CMAKE_CURRENT_SOURCE_DIR}/host_shim.js")
