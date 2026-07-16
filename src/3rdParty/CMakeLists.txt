# Build SalomeMesh for all Platforms since heavily patched
if (BUILD_SMESH AND NOT FREECAD_USE_EXTERNAL_SMESH)
    add_subdirectory(salomesmesh)
endif()

if (BUILD_BGFX)
    set(BGFX_LIBRARY_TYPE "SHARED")
    # example-common (bgfx_utils/entry, used by the renderer) is still built
    # via BGFX_BUILD_TOOLS; skip the ~50 example executables.
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "")
    add_subdirectory(bgfx)
endif()

if (BUILD_DILIGENT)
    add_subdirectory(Diligent/DiligentCore)
    add_subdirectory(Diligent/DiligentFX)
    add_subdirectory(Diligent/DiligentTools)
    if (BUILD_DILIGENT_SAMPLES)
        add_subdirectory(Diligent/DiligentSamples)
    endif()
endif()

add_subdirectory(lazy_loader)
add_subdirectory(libE57Format)

if (BUILD_ASSEMBLY AND NOT FREECAD_USE_EXTERNAL_ONDSELSOLVER)
    if( NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdParty/OndselSolver/CMakeLists.txt" )
        message( SEND_ERROR "The OndselSolver git submodule is not available. Please run
        git submodule update --init" )
    endif()
    add_subdirectory(OndselSolver)
endif()
