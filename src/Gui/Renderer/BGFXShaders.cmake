# Build-time compilation of the FreeCAD bgfx shaders (docs/RenderDebug.md).
#
# The repository carries only shader SOURCE (bgfx/shaders/*.sc + *.sh
# includes + varying.def.sc); the .bin files are build artifacts produced
# by bgfx's shaderc, never committed. The desktop build compiles with the
# in-tree shaderc target; the Emscripten viewer build (which cannot build
# a host tool itself) points FCVIEWER_SHADERC at a host shaderc binary.
#
# fc_bgfx_compile_shaders(<out-var>
#     SHADERC   <shaderc executable or $<TARGET_FILE:shaderc>>
#     SHADERDIR <directory with vs_*.sc / fs_*.sc / *.sh / varying.def.sc>
#     BGFXINC   <bgfx shader include dir (bgfx/src, for bgfx_shader.sh)>
#     OUTDIR    <output root; bins land in OUTDIR/<profile>/<name>.bin>
#     PROFILES  <any of: glsl spirv essl metal>
#     [DEPENDS  <extra dependencies, e.g. the shaderc target>])
#
# Sets <out-var> to the list of generated .bin paths. Every *.sh include
# is a dependency of every shader — coarse, but include edits are rare
# and a stale-bin bug costs far more than the over-rebuild. The shader
# lists are GLOBs with CONFIGURE_DEPENDS: the build re-checks them and
# reconfigures itself when a file is added or removed, so an all-new .sc
# or .sh is picked up by the next build -- the runtime user-shader
# compile includes from a COPY of the .sh set, and a new include that
# was not in the copy failed every splice that named it while the flat
# program stood in (docs/MaterialStorage.md sec 17.22).
function(fc_bgfx_compile_shaders outvar)
    cmake_parse_arguments(ARG "" "SHADERC;SHADERDIR;BGFXINC;OUTDIR"
                          "PROFILES;DEPENDS" ${ARGN})
    file(GLOB _srcs CONFIGURE_DEPENDS
         ${ARG_SHADERDIR}/vs_*.sc ${ARG_SHADERDIR}/fs_*.sc)
    file(GLOB _incs CONFIGURE_DEPENDS ${ARG_SHADERDIR}/*.sh)
    # The bgfx headers come from BGFXINC, not SHADERDIR — the submodule
    # is fork-patched, so an edit there must also re-trigger shaderc.
    list(APPEND _incs ${ARG_BGFXINC}/bgfx_shader.sh
                      ${ARG_BGFXINC}/bgfx_compute.sh)
    set(_varying ${ARG_SHADERDIR}/varying.def.sc)
    set(_bins)
    foreach(_src ${_srcs})
        get_filename_component(_name ${_src} NAME_WE)
        if(_name MATCHES "^vs_")
            set(_type v)
        else()
            set(_type f)
        endif()
        foreach(_profile ${ARG_PROFILES})
            if(_profile STREQUAL "glsl")
                set(_flags --platform linux -p 140)
            elseif(_profile STREQUAL "spirv")
                set(_flags --platform linux -p spirv)
            elseif(_profile STREQUAL "essl")
                # The one essl pack serves the Emscripten viewer AND a
                # native GLES run (whose runtime user-shader compiles
                # use --platform android): the 300_es output differs
                # only in platform defines no fc shader consumes.
                set(_flags --platform asm.js -p 300_es)
            elseif(_profile STREQUAL "metal")
                set(_flags --platform osx -p metal)
            else()
                message(FATAL_ERROR "unknown shader profile: ${_profile}")
            endif()
            set(_out ${ARG_OUTDIR}/${_profile}/${_name}.bin)
            add_custom_command(OUTPUT ${_out}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        ${ARG_OUTDIR}/${_profile}
                COMMAND ${ARG_SHADERC} -f ${_src} -o ${_out}
                        --type ${_type} ${_flags}
                        -i ${ARG_BGFXINC} --varyingdef ${_varying}
                DEPENDS ${_src} ${_incs} ${_varying} ${ARG_DEPENDS}
                COMMENT "shaderc ${_profile}/${_name}"
                VERBATIM)
            list(APPEND _bins ${_out})
        endforeach()
    endforeach()
    set(${outvar} ${_bins} PARENT_SCOPE)
endfunction()

# Syntax-check shader sources that ship as SOURCE and are compiled at
# runtime — the bundled effect packages (docs/RenderEngine.md §5.11).
# Nothing built them until a user instantiated the effect, so a typo in
# one shipped silently and surfaced as a dead effect on someone else's
# machine. This compiles each of them the way the runtime user-shader
# path will (same include root and varying definition) and throws the
# output away; a failure is a build failure.
#
# fc_bgfx_check_shaders(<out-var>
#     SHADERC   <shaderc executable>
#     SRCDIR    <directory tree globbed for *.sc>
#     SHADERDIR <the stock shader dir: fc_*.sh includes + varying.def.sc>
#     BGFXINC   <bgfx shader include dir>
#     OUTDIR    <where the throwaway bins + stamps land>
#     PROFILES  <as fc_bgfx_compile_shaders>
#     [DEPENDS  <extra dependencies>])
#
# The stage follows the name the manifests use: *_vs.sc is a vertex
# stage, everything else (including the *_sim.sc state steps) is a
# fragment one.
#
# A volume-stage medium source is not a program — it is the two or
# three contract functions the engine splices into a volumetric
# raymarch body (assembleMediumVariant, BGFXRendererP.h), so it has no
# main() and cannot be compiled on its own. Those are checked through
# the same wrapper the runtime builds, for slot 0.
function(fc_bgfx_check_shaders outvar)
    cmake_parse_arguments(ARG "" "SHADERC;SRCDIR;SHADERDIR;BGFXINC;OUTDIR"
                          "PROFILES;DEPENDS" ${ARGN})
    file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS ${ARG_SRCDIR}/*.sc)
    file(GLOB _incs CONFIGURE_DEPENDS ${ARG_SHADERDIR}/*.sh)
    list(APPEND _incs ${ARG_BGFXINC}/bgfx_shader.sh
                      ${ARG_BGFXINC}/bgfx_compute.sh)
    set(_varying ${ARG_SHADERDIR}/varying.def.sc)
    set(_stamps)
    foreach(_src ${_srcs})
        get_filename_component(_name ${_src} NAME_WE)
        if(_name MATCHES "_vs$")
            set(_type v)
        else()
            set(_type f)
        endif()
        # A spliced medium source: wrap it the way the engine does
        # before handing it to shaderc.
        set(_dep ${_src})
        set(_srcinc)
        file(READ ${_src} _text)
        if(_text MATCHES "fcMedium(Field|Scatter)" AND
           NOT _text MATCHES "void[ \t]+main")
            # The wrapper must not be named after the medium it splices:
            # it sits in its own directory and pulls the medium in by
            # bare name, and a quoted include is resolved against the
            # including file's directory first — so a wrapper called
            # fire_medium.sc would include itself instead of the effect.
            set(_wrap ${ARG_OUTDIR}/wrap/${_name}_fccheck.sc)
            set(_w "$input v_texcoord0\n\n#include <bgfx_shader.sh>\n")
            if(_text MATCHES "fcMediumScatter")
                string(APPEND _w "#define FC_USER_SCATTER_0\n")
            else()
                string(APPEND _w "#define FC_USER_FIRE_0\n")
            endif()
            string(APPEND _w "#include \"fc_volume_fs.sh\"\n")
            if(_text MATCHES "fcMediumScatter")
                string(APPEND _w "#define fcMediumScatter fcUserScatter_0\n")
            else()
                string(APPEND _w "#define fcMediumField fcUserField_0\n"
                                 "#define fcMediumRamp fcUserRamp_0\n")
            endif()
            # Reach the medium through an -i search path, never by
            # absolute path: shaderc's preprocessor cannot open an
            # include whose name carries a Windows drive letter (the
            # colon), so "D:/.../fire_medium.sc" resolved to nothing and
            # the contract functions the wrapper #defines came out
            # unresolved — on Linux the same include has no colon and
            # worked, which is why this only ever broke the Windows build.
            get_filename_component(_srcdir ${_src} DIRECTORY)
            get_filename_component(_srcfile ${_src} NAME)
            string(APPEND _w "#define FC_MEDIUM_SLOT 0\n"
                             "#include \"${_srcfile}\"\n")
            file(WRITE ${_wrap} "${_w}")
            set(_dep ${_src})
            set(_srcinc -i ${_srcdir})
            set(_src ${_wrap})
            set(_type f)
        endif()
        foreach(_profile ${ARG_PROFILES})
            if(_profile STREQUAL "glsl")
                set(_flags --platform linux -p 140)
            elseif(_profile STREQUAL "spirv")
                set(_flags --platform linux -p spirv)
            elseif(_profile STREQUAL "essl")
                set(_flags --platform asm.js -p 300_es)
            elseif(_profile STREQUAL "metal")
                set(_flags --platform osx -p metal)
            else()
                message(FATAL_ERROR "unknown shader profile: ${_profile}")
            endif()
            set(_out ${ARG_OUTDIR}/${_profile}/${_name}.bin)
            add_custom_command(OUTPUT ${_out}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                        ${ARG_OUTDIR}/${_profile}
                COMMAND ${ARG_SHADERC} -f ${_src} -o ${_out}
                        --type ${_type} ${_flags}
                        -i ${ARG_BGFXINC} -i ${ARG_SHADERDIR} ${_srcinc}
                        --varyingdef ${_varying}
                DEPENDS ${_src} ${_dep} ${_incs} ${_varying}
                        ${ARG_DEPENDS}
                COMMENT "shader check ${_profile}/${_name}"
                VERBATIM)
            list(APPEND _stamps ${_out})
        endforeach()
    endforeach()
    set(${outvar} ${_stamps} PARENT_SCOPE)
endfunction()

# Ship the shader compile inputs next to the compiled bins, into
# OUTDIR/src: varying.def.sc, every fc *.sh include and the bgfx shader
# headers. The runtime user-shader compile (docs/RenderDebug.md §6.3 —
# BGFXRenderer invoking shaderc on SoShaderProgram source) uses this
# directory as its single include root and varying definition, so user
# source can `#include <bgfx_shader.sh>` and the fc_*.sh helpers like the
# stock shaders do.
function(fc_bgfx_copy_shader_src outvar)
    cmake_parse_arguments(ARG "" "SHADERDIR;BGFXINC;OUTDIR" "" ${ARGN})
    file(GLOB _incs CONFIGURE_DEPENDS ${ARG_SHADERDIR}/*.sh)
    list(APPEND _incs ${ARG_SHADERDIR}/varying.def.sc
                      ${ARG_BGFXINC}/bgfx_shader.sh
                      ${ARG_BGFXINC}/bgfx_compute.sh)
    set(_outs)
    foreach(_src ${_incs})
        get_filename_component(_name ${_src} NAME)
        set(_out ${ARG_OUTDIR}/src/${_name})
        add_custom_command(OUTPUT ${_out}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_OUTDIR}/src
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_src} ${_out}
            DEPENDS ${_src}
            COMMENT "shader src ${_name}"
            VERBATIM)
        list(APPEND _outs ${_out})
    endforeach()
    set(${outvar} ${_outs} PARENT_SCOPE)
endfunction()
