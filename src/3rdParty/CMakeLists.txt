# The signals library. Not optional: Base, App and Gui all declare signals.
add_subdirectory(FastSignals)

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
    #
    # This has to be a FORCEd cache entry, not a plain variable: bgfx's own
    # CMakeLists.txt does set(BGFX_LIBRARY_TYPE "SHARED" CACHE STRING ...), and
    # creating a cache entry REMOVES any normal variable of that name from the
    # calling scope -- so on a fresh cache the plain value is discarded and the
    # SHARED default wins, which is invisible until the link wall above.
    if (WIN32)
        set(BGFX_LIBRARY_TYPE "STATIC" CACHE STRING "" FORCE)
    else()
        set(BGFX_LIBRARY_TYPE "SHARED" CACHE STRING "" FORCE)
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
    # Every 3D view on the bgfx path holds a contiguous block of
    # BGFXView::NUM_VIEWS ids for its pass sequence, and that sequence has
    # grown (particle simulation steps, bulb shadow tiles, water/glass,
    # nine overlay slots) to the point where bgfx's default 256 fits only
    # two viewers. The per-frame pass map since cut a plain viewer to 13
    # ids (a 16-id block at the 8-id granule), so this is ~256 viewers.
    #
    # This is now only the CEILING -- what the build can address. What a
    # session actually uses and walks is Render/MaxViewIds, a runtime
    # limit clamped to this (bgfx's Init::Limits::maxViews), default 1024.
    # The split matters because the two used to be one number: raising
    # the constant for capacity also made every frame pay for slots no
    # view would ever use.
    #
    # What the ceiling itself costs at this width, measured rather than
    # reasoned (desktop GL, trivial scene, limit at 1024):
    #  - Host RAM: nothing you can find. The per-view tables are static
    #    arrays, so the pages past the limit are never touched and never
    #    fault in -- resident memory came out the same as a 1024 build.
    #  - One more bit of the 64-bit draw sort key for the view field.
    #    There is room: the tightest key variant packs view bits + 47,
    #    so 59 of 64 here, and BlitKey needs the field under 16 bits.
    #  - The per-frame terms -- Context::swap()'s copy of the view table,
    #    Frame::sort()'s rect/scissor fixup, and the per-view pools the
    #    GPU profiler drives -- follow the runtime limit, not this.
    # Raising the LIMIT to this ceiling is what is not free: with render
    # stage timing on it took a 59fps session to 19 with resident memory
    # climbing 95MB in thirty seconds. Capacity is cheap, use is not.
    # Must stay a power of two (static_assert in bgfx's config.h).
    set(BGFX_CONFIG_MAX_VIEWS "4096" CACHE STRING "" FORCE)
    # The same story one pool over, and it bites sooner: a view's render
    # targets are ~41 framebuffers (the sized set of forEachHandle, the
    # six-level AO depth pyramid and the particle ping-pong), against a
    # bgfx default of 128. That is one viewer -- because bgfx frees a
    # handle at the END of the frame that destroyed it, so a re-init
    # (any resize) transiently holds both the old set and the new, ~82,
    # and a second 3D view then has nowhere to go.
    #
    # What that looked like: "Failed to allocate frame buffer handle",
    # then createFrameBuffer asserting on an invalid attachment, i.e. a
    # SIGTRAP on opening a second view or resizing one a few times.
    # 512 fits five viewers with a re-init in flight, matching the view
    # id budget above; the cost is bgfx's FrameBufferRef table, tens of
    # bytes a slot.
    set(BGFX_CONFIG_MAX_FRAME_BUFFERS "512" CACHE STRING "" FORCE)
    add_subdirectory(bgfx)
    # ⚠️⚠️ A bgfx occlusion query handle is an object's identity, not a
    # slot to rent: createOcclusionQuery() is the only place bgfx writes
    # the "no result yet" sentinel into a handle's slot, so a handle
    # reused for a second test answers with the first one's verdict, and
    # the guard that would catch it can never fire
    # (docs/FarFieldProxies.md §12.11). The renderer therefore creates a
    # handle per test and destroys it when the answer is read -- and
    # bgfx defers the free to the end of the frame, so the live count is
    # the tests in flight plus a frame's worth of released ones, for the
    # culler and the measurement probe both. The 256 default fits
    # neither: one large-assembly walk alone offers 128 tests a frame at
    # a two-frame answer latency. Not one of the counts bgfx's own CMake
    # forwards, hence set on the target; PUBLIC to match how it forwards
    # the others. Costs one GL query object and eight bytes of handle
    # table per slot.
    target_compile_definitions(bgfx PUBLIC BGFX_CONFIG_MAX_OCCLUSION_QUERIES=2048)

    # vg-renderer: the 2D vector engine for drawing pages (TechDraw), a
    # retained command-list renderer on top of bgfx -- see
    # docs/TechDrawPortAndSection.md section 16 for why it was chosen.
    # Vendored as a submodule like bgfx itself; libtess2, fontstash and
    # stb_truetype live inside it, so it adds no external dependency.
    # Upstream builds with GENie and ships no CMake, so the target is
    # defined here.
    add_library(vg-renderer STATIC
        vg-renderer/src/vg.cpp
        vg-renderer/src/path.cpp
        vg-renderer/src/stroker.cpp
        vg-renderer/src/vg_util.cpp
        vg-renderer/src/libs/fontstash.cpp
        vg-renderer/src/libs/stb_truetype.cpp
        vg-renderer/src/libtess2/bucketalloc.c
        vg-renderer/src/libtess2/dict.c
        vg-renderer/src/libtess2/geom.c
        vg-renderer/src/libtess2/mesh.c
        vg-renderer/src/libtess2/priorityq.c
        vg-renderer/src/libtess2/sweep.c
        vg-renderer/src/libtess2/tess.c
    )
    target_include_directories(vg-renderer PUBLIC vg-renderer/include)
    # vg/vg.h includes bx headers, so bx must be PUBLIC -- which also
    # inherits bx's PUBLIC BX_CONFIG_DEBUG define, without which bx
    # headers refuse to compile.
    target_link_libraries(vg-renderer PUBLIC bx bgfx)
    # The embedded shaders are baked by vg-renderer/src/shaders/rebake.sh
    # and carry every profile bgfx bakes for its own, Direct3D included
    # since 2026-09-06 -- so nothing overrides embedded_shader.h's
    # platform-support switches here any more. It used to: the set was
    # baked on Linux, where shaderc cannot produce dxbc or dxil, and the
    # two switches were forced off to match. That was wrong on Windows,
    # where bgfx picks Direct3D 11 by default: createEmbeddedShader found
    # no entry for the running renderer, every vg program came back
    # invalid, and bgfx substitutes program handle 0 for an invalid
    # program rather than refusing the draw -- so the frame was drawn by
    # an unrelated program and only the gradient band of the smoke test
    # noticed (docs/Testing.md, "The vg smokes on Windows"). Rebake on
    # Windows if these are ever regenerated.
    # C++20 to match bgfx/bx (bx headers require it); PIC because the
    # static archive links into the FreeCADRenderer shared lib.
    set_target_properties(vg-renderer PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
endif()

if(BUILD_CYCLES)
    # Cycles (docs/CyclesIntegration.md): Blender's path tracer, vendored
    # like bgfx but, unlike bgfx, not self-contained -- it links
    # OpenImageIO, TBB, Embree and OpenImageDenoise from the environment.
    # WITH_LIBS_PRECOMPILED is the Blender bundle route, refused because
    # the bundle ships its own TBB and this process already links TBB
    # through OCCT (sec 3 of the doc). The OFF list is dead weight for a
    # CAD viewport; OpenSubdiv is the one worth revisiting. The tree
    # comes in through CYCLES_EMBEDDED, which skips its executable,
    # install and CTest and exports the cycles_embed target instead.
    # Cache entries rather than plain variables on purpose: the tree's
    # cmake_minimum_required(3.10) puts its option() calls on the old
    # behaviour, where a plain variable is clobbered by the default.
    set(CYCLES_EMBEDDED ON CACHE BOOL "" FORCE)
    set(WITH_LIBS_PRECOMPILED OFF CACHE BOOL "" FORCE)
    foreach(_cycles_off
            WITH_CYCLES_OSL
            WITH_CYCLES_USD
            WITH_CYCLES_HYDRA_RENDER_DELEGATE
            WITH_CYCLES_ALEMBIC
            WITH_CYCLES_OPENVDB
            WITH_CYCLES_NANOVDB
            WITH_CYCLES_OPENSUBDIV
            WITH_CYCLES_STANDALONE_GUI)
        set(${_cycles_off} OFF CACHE BOOL "" FORCE)
    endforeach()
    unset(_cycles_off)
    set(WITH_CYCLES_EMBREE ON CACHE BOOL "" FORCE)
    set(WITH_CYCLES_OPENIMAGEDENOISE ON CACHE BOOL "" FORCE)
    # GPU devices keep the tree's defaults (CUDA, OptiX, HIP on where the
    # platform allows); OptiX needs OPTIX_ROOT_DIR and disables itself
    # otherwise. Kernel binaries stay off: they compile at runtime.
    add_subdirectory(cycles)
endif()

if(BUILD_MATERIALX)
    # MaterialX (docs/CyclesIntegration.md sec 8 item 15 phase B): the
    # node-graph material DESCRIPTION language. Two consumers, neither
    # of which uses MaterialX's own lighting -- the Cycles interpreter
    # walks the graph into ccl nodes, and the raster path generates a
    # material-inputs function through a bgfx-flavoured ShaderGen
    # target. So only Core, Format, GenShader, GenHw and GenGlsl are
    # built; Render (and with it the 134MB resources install), the
    # viewer, the graph editor, the OSL/MDL/MSL/Slang back-ends, the
    # Python and JavaScript bindings and the tests are all off.
    #
    # Static, and the tree already sets CMAKE_POSITION_INDEPENDENT_CODE
    # for itself, which is what the link into the FreeCADRenderer shared
    # library needs.
    set(MATERIALX_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(MATERIALX_BUILD_GEN_GLSL ON CACHE BOOL "" FORCE)
    foreach(_mtlx_off
            MATERIALX_BUILD_GEN_OSL
            MATERIALX_BUILD_GEN_MDL
            MATERIALX_BUILD_GEN_MSL
            MATERIALX_BUILD_GEN_SLANG
            MATERIALX_BUILD_RENDER
            MATERIALX_BUILD_TESTS
            MATERIALX_BUILD_PYTHON
            MATERIALX_BUILD_JS
            MATERIALX_BUILD_VIEWER
            MATERIALX_BUILD_GRAPH_EDITOR
            MATERIALX_BUILD_DOCS
            MATERIALX_BUILD_MONOLITHIC)
        set(${_mtlx_off} OFF CACHE BOOL "" FORCE)
    endforeach()
    unset(_mtlx_off)

    # Three things the tree does to the ENCLOSING project that a
    # vendored build must not inherit:
    #
    #  - Its install rules. MaterialX installs its static archives, its
    #    headers, an EXPORT set and a package config file named after
    #    CMAKE_PROJECT_NAME -- which under add_subdirectory() is
    #    FreeCAD, so it would write a FreeCADConfig.cmake over ours.
    #    Every one of those rules is guarded by NOT SKBUILD (the
    #    wheel-build switch), so declaring SKBUILD here suppresses the
    #    lot; its only other effects are on the Python bindings (off)
    #    and on RPATH (meaningless for a static archive). The data
    #    library is then installed below, where we want it.
    #  - CMAKE_INSTALL_PREFIX. The tree FORCEs it to
    #    <bindir>/installed whenever the prefix was left at its
    #    default, which for a build that did not pass one would move
    #    the whole of FreeCAD's install.
    #  - enable_testing() plus its own CTest cases; ours is the only
    #    test registry (nothing is registered here because
    #    MATERIALX_BUILD_TESTS is off, but the guard is free).
    set(SKBUILD ON)
    set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
    # Nothing here is a Qt target; FreeCAD turns AUTOMOC on globally,
    # which otherwise runs moc over every MaterialX translation unit.
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTOUIC OFF)
    set(CMAKE_AUTORCC OFF)
    add_subdirectory(MaterialX)
    unset(SKBUILD)

    # The .mtlx node definitions of the standard library are DATA: both
    # consumers read them at run time to resolve a document's node
    # references, the way the Cycles kernel source ships beside the
    # binary. Staged into the build tree as well so a build tree runs
    # without installing.
    set(_mtlx_libs ${CMAKE_CURRENT_SOURCE_DIR}/MaterialX/libraries)
    add_custom_target(Renderer_materialx_libraries ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${_mtlx_libs}
                ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Renderer/materialx/libraries
        COMMENT "MaterialX data library"
        VERBATIM)
    install(DIRECTORY ${_mtlx_libs}
            DESTINATION ${CMAKE_INSTALL_DATADIR}/Renderer/materialx)
    unset(_mtlx_libs)
endif()

if (BUILD_DILIGENT)
    add_subdirectory(Diligent/DiligentCore)
    add_subdirectory(Diligent/DiligentFX)
    add_subdirectory(Diligent/DiligentTools)
    if (BUILD_DILIGENT_SAMPLES)
        add_subdirectory(Diligent/DiligentSamples)
    endif()
endif()

if (BUILD_GUI)
    # Vendored verbatim from FreeCAD/FreeCAD#26766 (PaddleStroke:core_customtitlebar),
    # CRLF and all, so a re-sync with that branch diffs to just what changed there.
    # Local divergence is confined to the reversible-detach work needed for
    # switching the title bar at run time; see HANDOFF_Todos.md.
    add_subdirectory(customtitlebarkit)
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
