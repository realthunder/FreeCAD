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
    # two viewers. Doubling it fits five. The cost is bgfx's fixed
    # per-view tables -- a couple of hundred bytes per id, twice over for
    # the submit/render frame pair -- and one more bit of the 64-bit draw
    # sort key for the view field, which has room: the tightest key
    # variant packs 55 bits at the old width. Must stay a power of two
    # (static_assert in bgfx's config.h).
    set(BGFX_CONFIG_MAX_VIEWS "512" CACHE STRING "" FORCE)
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
