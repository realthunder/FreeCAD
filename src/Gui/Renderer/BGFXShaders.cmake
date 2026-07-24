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
#     PROFILES  <any of: glsl spirv essl>
#     [DEPENDS  <extra dependencies, e.g. the shaderc target>])
#
# Sets <out-var> to the list of generated .bin paths. Every *.sh include
# is a dependency of every shader — coarse, but include edits are rare
# and a stale-bin bug costs far more than the over-rebuild. The shader
# list is a configure-time GLOB: an all-new .sc file still needs a cmake
# reconfigure to be picked up (unchanged from the old committed-bin flow).
function(fc_bgfx_compile_shaders outvar)
    cmake_parse_arguments(ARG "" "SHADERC;SHADERDIR;BGFXINC;OUTDIR"
                          "PROFILES;DEPENDS" ${ARGN})
    file(GLOB _srcs ${ARG_SHADERDIR}/vs_*.sc ${ARG_SHADERDIR}/fs_*.sc)
    file(GLOB _incs ${ARG_SHADERDIR}/*.sh)
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
                set(_flags --platform asm.js -p 300_es)
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
