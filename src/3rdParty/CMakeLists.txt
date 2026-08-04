# Build SalomeMesh for all Platforms since heavily patched
if (BUILD_SMESH AND NOT FREECAD_USE_EXTERNAL_SMESH)
    add_subdirectory(salomesmesh)
endif()

if (BUILD_BGFX)
    # bgfx applies its export macro only to the C99 API, so a bgfx DLL exports
    # the C entry points and not a single C++ symbol (209 exports, 0 mangled).
    # Every C++ consumer -- our renderer, and bgfx's own example-common,
    # geometryv and texturev -- then fails to link with ~78 unresolved
    # externals. ELF default visibility exports everything regardless, which is
    # why only Windows sees it.
    if (WIN32)
        set(BGFX_LIBRARY_TYPE "STATIC")
    else()
        set(BGFX_LIBRARY_TYPE "SHARED")
    endif()
    # example-common (bgfx_utils/entry, used by the renderer) is still built
    # via BGFX_BUILD_TOOLS; skip the ~50 example executables.
    set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "")
    # A large assembly holds one vertex + up to three index buffers per
    # distinct mesh; the 4096 defaults die on 100k+ object scenes. The
    # cap sits just under the uint16 invalid-handle sentinel (a 65536th
    # slot would collide with it); handle tables cost bytes per slot.
    set(BGFX_CONFIG_MAX_VERTEX_BUFFERS "65535" CACHE STRING "" FORCE)
    set(BGFX_CONFIG_MAX_INDEX_BUFFERS "65535" CACHE STRING "" FORCE)
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
