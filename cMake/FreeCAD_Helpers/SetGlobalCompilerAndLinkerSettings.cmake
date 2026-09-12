macro(SetGlobalCompilerAndLinkerSettings)
    # ================================================================================
    # == Global Compiler and Linker Settings =========================================

    include_directories(${CMAKE_BINARY_DIR}/src
                        ${CMAKE_SOURCE_DIR}/src)

    # check for 64-bit platform
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(STATUS "Platform is 64-bit, set -D_OCC64")
        add_definitions(-D_OCC64 )
    else(CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(STATUS "Platform is 32-bit")
    endif(CMAKE_SIZEOF_VOID_P EQUAL 8)

    # check for mips64 platform
    if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "mips64")
        message(STATUS "Architecture: mips64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mxgot")
    endif()

    if(MSVC)
        # set default compiler settings
        # /Zm150 and /bigobj are configuration-independent: App/Document.cpp
        # exceeds the object-file section limit (C1128) whatever the optimisation
        # level. They used to be attached to Release and Debug only, so
        # RelWithDebInfo and MinSizeRel could not build at all. Set them once.
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zm150 /bigobj")
        # /utf-8 and /nologo are the two flags this tree was getting only
        # by accident. src/3rdParty/cycles used to FORCE the global flag
        # cache variables (its configure_build.cmake, Blender build code
        # written for a top-level project), and its string happened to
        # carry both; it no longer reaches outside itself, and the tree
        # still wants them. /utf-8 is not cosmetic: the sources are UTF-8
        # without a BOM and over a thousand tracked files carry non-ASCII
        # legitimately, so without it MSVC reads them in the system code
        # page -- 936 on this box, where such a literal is mojibake at
        # best and C2001 at worst. /nologo only silences a banner, once
        # per translation unit across 4600-odd targets.
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /nologo /utf-8")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /nologo /utf-8")
        set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DFC_DEBUG")
        # set default libs
        set (CMAKE_C_STANDARD_LIBRARIES "kernel32.lib user32.lib gdi32.lib winspool.lib SHFolder.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib winmm.lib comsupp.lib Ws2_32.lib dbghelp.lib ")
        set (CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES}")
        # Qt6 forces -permissive- on every MSVC consumer via its
        # INTERFACE_COMPILE_OPTIONS, and -permissive- implies /Zc:strictStrings,
        # which rejects initialising a char* from a string literal. That is the
        # documented CPython idiom for PyArg_ParseTupleAndKeywords' kwlist, used
        # in 17 places here, so relax that single rule rather than rewrite them.
        # With MSVC the last /Zc setting on the line wins, and a linked target's
        # interface options are emitted after anything add_compile_options() or
        # CMAKE_CXX_FLAGS can place -- so the override has to ride on the same
        # interface list, appended after Qt's own entries. This relaxes only
        # strictStrings; the rest of -permissive- stays in force.
        if (TARGET Qt6::Platform)
            set_property(TARGET Qt6::Platform APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                "$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:strictStrings->")
        endif()

        # Exclude the C runtimes this configuration is not using.
        # A bare /NODEFAULTLIB used to be set here to keep a dependency from
        # dragging in a mismatched CRT, but with no argument it discards every
        # default library including the CRT this build needs -- and the library
        # list above supplies none -- so no C++ target can link. bgfx surfaces it
        # first: 92 unresolved CRT math symbols out of bimg.lib. Name the
        # flavours to exclude instead: the static CRTs always (we always build
        # against the DLL runtime), plus whichever dynamic CRT is the opposite of
        # the current configuration, which is the mismatch actually worth blocking.
        set (_fc_exclude_static_crt "/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib")
        foreach (_fc_link_kind SHARED MODULE EXE)
            set (CMAKE_${_fc_link_kind}_LINKER_FLAGS_DEBUG
                 "${CMAKE_${_fc_link_kind}_LINKER_FLAGS_DEBUG} ${_fc_exclude_static_crt} /NODEFAULTLIB:msvcrt.lib")
            foreach (_fc_config RELEASE RELWITHDEBINFO MINSIZEREL)
                set (CMAKE_${_fc_link_kind}_LINKER_FLAGS_${_fc_config}
                     "${CMAKE_${_fc_link_kind}_LINKER_FLAGS_${_fc_config}} ${_fc_exclude_static_crt} /NODEFAULTLIB:msvcrtd.lib")
            endforeach()
        endforeach()
        unset (_fc_exclude_static_crt)
        if(FREECAD_RELEASE_PDB)
            set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
            set (CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
        endif(FREECAD_RELEASE_PDB)
        if(FREECAD_RELEASE_SEH)
            # remove /EHsc or /EHs flags because they are incompatible with /EHa
            if (${CMAKE_BUILD_TYPE} MATCHES "Release")
                string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
                string(REPLACE "/EHs" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
                set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /EHa")
            endif()
        endif(FREECAD_RELEASE_SEH)
        if(CCACHE_PROGRAM)
            # By default Visual Studio generators will use /Zi which is not compatible
            # with ccache, so tell Visual Studio to use /Z7 instead.
            string(REGEX REPLACE "/Z[iI]" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
            string(REGEX REPLACE "/Z[iI]" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
            string(REGEX REPLACE "/Z[iI]" "/Z7" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        endif(CCACHE_PROGRAM)

	option(FREECAD_USE_MP_COMPILE_FLAG "Add /MP flag to the compiler definitions. Speeds up the compile on multi processor machines" ON)
        if(FREECAD_USE_MP_COMPILE_FLAG)
            # set "Build with Multiple Processes"
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
        endif()

        # Mark 32 bit executables large address aware so they can use > 2GB address space
        # NOTE: This setting only has an effect on machines with at least 3GB of RAM, although it sets the linker option it doesn't set the linker switch 'Enable Large Addresses'
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LARGEADDRESSAWARE")
            set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LARGEADDRESSAWARE")
        endif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    else(MSVC)
        set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DFC_DEBUG")
            #message(STATUS "DEBUG: ${CMAKE_CXX_FLAGS_DEBUG}")
    endif(MSVC)

    if(MINGW)
        if(CMAKE_COMPILER_IS_CLANGXX)
            # clang for MSYS doesn't support -mthreads
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols")
            #set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-all-symbols")
        else()
            # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=12477
            # Actually '-Wno-inline-dllimport' should work to suppress warnings of the form:
            # inline function 'foo' is declared as dllimport: attribute ignored
            # But it doesn't work with MinGW gcc 4.5.0 while using '-Wno-attributes' seems to
            # do the trick.
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-all-symbols")
            #set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-all-symbols")
            # http://stackoverflow.com/questions/8375310/warning-auto-importing-has-been-activated-without-enable-auto-import-specifie
            # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
            link_libraries(-lgdi32)
        endif()
    endif(MINGW)
endmacro(SetGlobalCompilerAndLinkerSettings)
