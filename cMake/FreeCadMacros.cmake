include (CheckCXXSourceRuns)

# ================================================================================
# == Macros, mostly for special targets ==========================================

MACRO (fc_copy_sources target_name outpath)
	if(BUILD_VERBOSE_GENERATION)
		set(fc_details " (fc_copy_sources called from ${CMAKE_CURRENT_SOURCE_DIR})")
	else()
		set(fc_details "")
	endif()
	foreach(it ${ARGN})
		get_filename_component(infile ${it} ABSOLUTE)
		get_filename_component(outfile "${outpath}/${it}" ABSOLUTE)
		add_file_dependencies("${infile}" "${outfile}")
		ADD_CUSTOM_COMMAND(
			COMMAND   "${CMAKE_COMMAND}" -E copy "${infile}" "${outfile}"
			OUTPUT   "${outfile}"
			COMMENT "Copying ${infile} to ${outfile}${fc_details}"
			MAIN_DEPENDENCY "${infile}"
		)
	endforeach(it)
	ADD_CUSTOM_COMMAND(
		TARGET    ${target_name}
		DEPENDS   ${ARGN}
	)
ENDMACRO(fc_copy_sources)

MACRO (fc_copy_file_if_different inputfile outputfile)
    if (EXISTS ${inputfile})
        if (EXISTS ${outputfile})
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E compare_files ${inputfile}
                                                          ${outputfile}
                RESULT_VARIABLE DIFFERENT_FILES
                OUTPUT_QUIET
                ERROR_QUIET
            )

            if (DIFFERENT_FILES)
                execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${inputfile}"
                                                                   "${outputfile}")
            endif()
        else()
            execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${inputfile}"
                                                               "${outputfile}")
        endif()
    endif()
ENDMACRO(fc_copy_file_if_different)

MACRO (fc_target_copy_resource target_name inpath outpath)
# Macro to copy a list of files into a nested directory structure
# Arguments -
#   target_name - name of the target the files will be added to
#   inpath      - name of the source directory
#   outpath     - name of the destination directory
#   ARGN        - a list of relative file names that will be copied
#
#   If a relative file name is foo/bar.txt then the foo directory
#   part will be kept so that the destination file name will be
#   ${outpath}/foo/bar.txt
#
	if(BUILD_VERBOSE_GENERATION)
		set(fc_details " (fc_target_copy_resource called from ${CMAKE_CURRENT_SOURCE_DIR})")
	else()
		set(fc_details "")
	endif()
	foreach(it ${ARGN})
		get_filename_component(infile "${inpath}/${it}" ABSOLUTE)
		get_filename_component(outfile "${outpath}/${it}" ABSOLUTE)
		add_file_dependencies("${infile}" "${outfile}")
		ADD_CUSTOM_COMMAND(
			COMMAND   "${CMAKE_COMMAND}" -E copy "${infile}" "${outfile}"
			OUTPUT   "${outfile}"
			COMMENT "Copying ${infile} to ${outfile}${fc_details}"
			MAIN_DEPENDENCY "${infile}"
		)
	endforeach(it)
	ADD_CUSTOM_COMMAND(
		TARGET    ${target_name}
		DEPENDS   ${ARGN}
	)
ENDMACRO(fc_target_copy_resource)

MACRO (fc_target_copy_resource_flat target_name inpath outpath)
# Macro to copy a list of files into a flat directory structure
# Arguments -
#   target_name - name of the target the files will be added to
#   inpath      - name of the source directory
#   outpath     - name of the destination directory
#   ARGN        - a list of relative file names that will be copied
#
#   If a relative file name is foo/bar.txt then the foo directory
#   part will be removed so that the destination file name will be
#   ${outpath}/bar.txt
#
	if(BUILD_VERBOSE_GENERATION)
		set(fc_details " (fc_target_copy_resource_flat called from ${CMAKE_CURRENT_SOURCE_DIR})")
	else()
		set(fc_details "")
	endif()
	foreach(it ${ARGN})
		get_filename_component(infile "${inpath}/${it}" ABSOLUTE)
		get_filename_component(outfile "${it}" NAME)
		get_filename_component(outfile "${outpath}/${outfile}" ABSOLUTE)
		add_file_dependencies("${infile}" "${outfile}")
		ADD_CUSTOM_COMMAND(
			COMMAND   "${CMAKE_COMMAND}" -E copy "${infile}" "${outfile}"
			OUTPUT    "${outfile}"
			COMMENT "Copying ${infile} to ${outfile}${fc_details}"
			MAIN_DEPENDENCY "${infile}"
		)
	endforeach(it)
	ADD_CUSTOM_COMMAND(
		TARGET    ${target_name}
		DEPENDS   ${ARGN}
	)
ENDMACRO(fc_target_copy_resource_flat)

# It would be a bit cleaner to generate these files in ${CMAKE_CURRENT_BINARY_DIR}

# Remember every binding file the current configuration generates, so
# fc_sweep_orphan_bindings() below can tell a live output from a leftover.
function(fc_record_generated_binding)
    set_property(GLOBAL APPEND PROPERTY FC_GENERATED_BINDINGS ${ARGN})
endfunction(fc_record_generated_binding)

# Delete generated binding files the current configuration no longer produces.
#
# CMake never removes an output it used to generate, and a build directory
# outlives the source layout: move Foo.xml into a new subdirectory and the
# FooPy.h generated at the OLD path stays behind forever. That is not inert.
# The old directory is still on the include path, so #include "FooPy.h" can
# resolve to the leftover, and it shadows the real one silently. It surfaces
# as an error inside generated code that reads as impossible, because the file
# being compiled is not the file the build just generated:
#
#   SelectionObjectPy.h: error: PyTypeObject* Gui::SelectionObjectPy::GetType()
#   marked override, but does not override
#
# Found 2026-08-21 in the optimized tree: an Aug-3 orphan, left from before
# SelectionObjectPy.xml moved into Selection/, shadowed the current header --
# and the generator template had changed underneath it in the meantime, so the
# orphan was not merely old but wrong. A build directory that survives a file
# move carries this indefinitely, and no amount of rebuilding clears it.
#
# Only files carrying the generator's own banner are removed, so nothing
# hand-written, and nothing produced by another tool, can be caught by it.
#
# Note this is "not produced by THIS configuration", which is broader than
# "the source moved": switching BUILD_FEM off, say, orphans the FEM bindings
# too, and they are swept. That is intentional -- they are artifacts of a
# different configuration -- and costs only their regeneration if the module
# is switched back on.
function(fc_sweep_orphan_bindings)
    get_property(_expected GLOBAL PROPERTY FC_GENERATED_BINDINGS)
    file(GLOB_RECURSE _found
         "${CMAKE_BINARY_DIR}/src/*Py.h" "${CMAKE_BINARY_DIR}/src/*Py.cpp"
         "${CMAKE_BINARY_DIR}/src/*Py_.h" "${CMAKE_BINARY_DIR}/src/*Py_.cpp")
    set(_removed 0)
    foreach(_f ${_found})
        if(NOT "${_f}" IN_LIST _expected)
            file(READ "${_f}" _head LIMIT 200)
            if(_head MATCHES "This file is generated by")
                file(REMOVE "${_f}")
                message(STATUS "orphaned generated binding removed: ${_f}")
                math(EXPR _removed "${_removed} + 1")
            endif()
        endif()
    endforeach()
    if(_removed GREATER 0)
        message(STATUS "swept ${_removed} orphaned generated binding file(s) "
                       "left behind by an earlier source layout")
    endif()
endfunction(fc_sweep_orphan_bindings)

macro(generate_from_xml BASE_NAME)
    set(TOOL_PATH "${CMAKE_SOURCE_DIR}/src/Tools/bindings/generate.py")
    file(TO_NATIVE_PATH "${TOOL_PATH}" TOOL_NATIVE_PATH)
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${BASE_NAME}.xml" SOURCE_NATIVE_PATH)

    set(SOURCE_CPP_PATH "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.cpp" )
    
    # BASE_NAME may include also a path name
    GET_FILENAME_COMPONENT(OUTPUT_PATH "${SOURCE_CPP_PATH}" PATH)
    file(TO_NATIVE_PATH "${OUTPUT_PATH}" OUTPUT_NATIVE_PATH)
    if(NOT EXISTS "${SOURCE_CPP_PATH}")
        # assures the source files are generated at least once
        message(STATUS "${SOURCE_CPP_PATH}")
        execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${TOOL_NATIVE_PATH}" --outputPath "${OUTPUT_NATIVE_PATH}" "${SOURCE_NATIVE_PATH}"
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.h" "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.cpp"
        COMMAND ${PYTHON_EXECUTABLE} "${TOOL_NATIVE_PATH}" --outputPath "${OUTPUT_NATIVE_PATH}" ${BASE_NAME}.xml
        MAIN_DEPENDENCY "${BASE_NAME}.xml"
        DEPENDS
        "${CMAKE_SOURCE_DIR}/src/Tools/bindings/templates/templateClassPyExport.py"
        "${TOOL_PATH}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building ${BASE_NAME}.h/.cpp out of ${BASE_NAME}.xml"
    )
    fc_record_generated_binding("${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.h"
                                "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}.cpp")
endmacro(generate_from_xml)

# Generate <BASE_NAME>Py<SUFFIX>.h/.cpp from a <BASE_NAME>.pyi binding definition.
# This is the format upstream authors new bindings in; generate_from_xml above
# drives the same generator from the older .xml definitions.
macro(generate_from_py_impl BASE_NAME SUFFIX)
    set(TOOL_PATH "${CMAKE_SOURCE_DIR}/src/Tools/bindings/generate.py")
    file(TO_NATIVE_PATH "${TOOL_PATH}" TOOL_NATIVE_PATH)
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${BASE_NAME}.pyi" SOURCE_NATIVE_PATH)

    set(SOURCE_CPP_PATH "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}Py${SUFFIX}.cpp")
    set(SOURCE_H_PATH "${CMAKE_CURRENT_BINARY_DIR}/${BASE_NAME}Py${SUFFIX}.h")

    # BASE_NAME may include also a path name
    GET_FILENAME_COMPONENT(OUTPUT_PATH "${SOURCE_CPP_PATH}" PATH)
    file(TO_NATIVE_PATH "${OUTPUT_PATH}" OUTPUT_NATIVE_PATH)
    if(NOT EXISTS "${SOURCE_CPP_PATH}")
        # assures the source files are generated at least once
        message(STATUS "${SOURCE_CPP_PATH}")
        execute_process(
            COMMAND "${PYTHON_EXECUTABLE}" "${TOOL_NATIVE_PATH}" --outputPath "${OUTPUT_NATIVE_PATH}" "${SOURCE_NATIVE_PATH}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    add_custom_command(
        OUTPUT "${SOURCE_H_PATH}" "${SOURCE_CPP_PATH}"
        COMMAND ${PYTHON_EXECUTABLE} "${TOOL_NATIVE_PATH}" --outputPath "${OUTPUT_NATIVE_PATH}" ${BASE_NAME}.pyi
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${BASE_NAME}.pyi"
        DEPENDS
        "${CMAKE_SOURCE_DIR}/src/Tools/bindings/templates/templateClassPyExport.py"
        "${TOOL_PATH}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building ${BASE_NAME}Py${SUFFIX}.h/.cpp out of ${BASE_NAME}.pyi"
    )
    fc_record_generated_binding("${SOURCE_H_PATH}" "${SOURCE_CPP_PATH}")
endmacro(generate_from_py_impl)

macro(generate_from_py BASE_NAME)
    generate_from_py_impl(${BASE_NAME} "")
endmacro(generate_from_py)

macro(generate_from_py_ BASE_NAME)
    generate_from_py_impl(${BASE_NAME} "_")
endmacro(generate_from_py_)

# Embed a .py file as a C++ header. Named generate_from_py before the .pyi
# binding generator above took that name; renamed to match upstream.
macro(generate_embed_from_py BASE_NAME OUTPUT_FILE)
		set(TOOL_PATH "${CMAKE_SOURCE_DIR}/src/Tools/PythonToCPP.py")
		file(TO_NATIVE_PATH "${TOOL_PATH}" TOOL_NATIVE_PATH)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${BASE_NAME}.py" SOURCE_NATIVE_PATH)
		add_custom_command(
		 		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE}"
		 		COMMAND "${PYTHON_EXECUTABLE}" "${TOOL_NATIVE_PATH}" "${SOURCE_NATIVE_PATH}" "${OUTPUT_FILE}"
				MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${BASE_NAME}.py"
				DEPENDS "${TOOL_PATH}"
				WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
				COMMENT "Building files out of ${BASE_NAME}.py")
endmacro(generate_embed_from_py)

macro(generate_from_any INPUT_FILE OUTPUT_FILE VARIABLE)
		set(TOOL_PATH "${CMAKE_SOURCE_DIR}/src/Tools/PythonToCPP.py")
		file(TO_NATIVE_PATH "${TOOL_PATH}" TOOL_NATIVE_PATH)
		file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${INPUT_FILE}" SOURCE_NATIVE_PATH)
		add_custom_command(
		 		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE}"
		 		COMMAND "${PYTHON_EXECUTABLE}" "${TOOL_NATIVE_PATH}" "${SOURCE_NATIVE_PATH}" "${OUTPUT_FILE}" "${VARIABLE}"
				MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${INPUT_FILE}"
				DEPENDS "${TOOL_PATH}"
				WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
				COMMENT "Building files out of ${INPUT_FILE}")
endmacro(generate_from_any)



MACRO(ADD_MSVC_PRECOMPILED_HEADER TargetName PrecompiledHeader PrecompiledSource SourcesVar)
  IF(MSVC)
    GET_FILENAME_COMPONENT(PrecompiledBasename ${PrecompiledHeader} NAME_WE)
    IF(MSVC_IDE)
      SET(PrecompiledBinary "$(IntDir)\\$(TargetName).pch")
    ELSE(MSVC_IDE)
      SET(PrecompiledBinary ${CMAKE_CURRENT_BINARY_DIR}/${TargetName}.pch)
    ENDIF(MSVC_IDE)
    SET(Sources ${${SourcesVar}})

    SET_SOURCE_FILES_PROPERTIES(${PrecompiledSource}
                                PROPERTIES COMPILE_FLAGS "/Yc\"${PrecompiledHeader}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_OUTPUTS "${PrecompiledBinary}")
    SET_SOURCE_FILES_PROPERTIES(${Sources}
                                PROPERTIES COMPILE_FLAGS "/Yu\"${PrecompiledHeader}\" /FI\"${PrecompiledBinary}\" /Fp\"${PrecompiledBinary}\""
                                           OBJECT_DEPENDS "${PrecompiledBinary}")  
    # Add precompiled header to SourcesVar
    LIST(APPEND ${SourcesVar} ${PrecompiledSource})
  ENDIF(MSVC)
ENDMACRO(ADD_MSVC_PRECOMPILED_HEADER)

MACRO(GET_MSVC_PRECOMPILED_SOURCE PrecompiledSource SourcesVar)
  IF(MSVC)
    FOREACH (it ${ARGN})
      GET_FILENAME_COMPONENT(file_ext ${it} EXT)
      GET_FILENAME_COMPONENT(file_name ${it} NAME)
	  STRING(COMPARE EQUAL ${it} ${PrecompiledSource} pch)
	  IF (NOT pch)
	    # get c++ source files
		STRING(REGEX MATCH "^(.cpp|.cc|.cxx)$" cpp_file ${file_ext})
		# ignore any generated source files from Qt
		STRING(REGEX MATCH "^(moc_|qrc_|ui_)" gen_file ${file_name})
		IF(cpp_file AND NOT gen_file)
			LIST(APPEND ${SourcesVar} ${it})
		ENDIF(cpp_file AND NOT gen_file)
	  ENDIF(NOT pch)
    ENDFOREACH (it)
  ENDIF(MSVC)
ENDMACRO(GET_MSVC_PRECOMPILED_SOURCE)

# Macro to replace all the binary output locations.  Takes 2 optional parameters.
# ${ARGVN} is zero based so the 3rd element is ${ARGV2}.  When the 3rd element is missing,
# Runtime and Lib directories default to /bin and /lib.  When present, the 3rd element
# specifies both Runtime and Lib directories.  4th specifies linux install path.
MACRO(SET_BIN_DIR ProjectName OutputName)
    set_target_properties(${ProjectName} PROPERTIES OUTPUT_NAME ${OutputName})
    if(${ARGC} GREATER 2)
        # VS_IDE (and perhaps others) make Release and Debug subfolders.  This removes them.
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}${ARGV2})
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}${ARGV2})
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}${ARGV2})
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}${ARGV2})
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}${ARGV2})
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}${ARGV2})
    else(${ARGC} GREATER 2)
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/bin)
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
        set_target_properties(${ProjectName} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/bin)
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY         ${CMAKE_BINARY_DIR}/lib)
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
        set_target_properties(${ProjectName} PROPERTIES LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/lib)
    endif(${ARGC} GREATER 2)

    if(WIN32)
        set_target_properties(${ProjectName} PROPERTIES DEBUG_OUTPUT_NAME ${OutputName}_d)
    else(WIN32)
        # FreeCADBase, SMDS, Driver and MEFISTO2 libs don't depend on parts from CMAKE_INSTALL_LIBDIR
        if(NOT ${ProjectName} MATCHES "^(FreeCADBase|SMDS|Driver|MEFISTO2)$")
            if(${ARGC} STREQUAL 4)
                set_property(TARGET ${ProjectName} APPEND PROPERTY INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${ARGV3})
            elseif(NOT IS_ABSOLUTE ${CMAKE_INSTALL_LIBDIR})
                set_property(TARGET ${ProjectName} APPEND PROPERTY INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
            else()
                set_property(TARGET ${ProjectName} APPEND PROPERTY INSTALL_RPATH ${CMAKE_INSTALL_LIBDIR})
            endif()
        endif()
    endif(WIN32)
ENDMACRO(SET_BIN_DIR)

# Set python prefix & suffix together
MACRO(SET_PYTHON_PREFIX_SUFFIX ProjectName)
    if(NOT MSVC)
        set_target_properties(${ProjectName} PROPERTIES PREFIX "")
    endif(NOT MSVC)
    
    if(WIN32)
        set_target_properties(${ProjectName} PROPERTIES SUFFIX ".pyd")
    # 0000661: cmake build on Mac OS: dealing with dylib versus so
    elseif(APPLE)
        set_target_properties(${ProjectName} PROPERTIES SUFFIX ".so")
    endif(WIN32)
ENDMACRO(SET_PYTHON_PREFIX_SUFFIX)

# The host widget layer's .ui generator (docs/Sandbox.md 7.12): for each
# <dir/Name.ui> emit ${CMAKE_CURRENT_BINARY_DIR}/fwui_<Name>.h, a
# `Ui_X::setupUi(Gui::Fw::UiForm*)` over the host widget models, and
# append the header to <OUT_VAR>.  The header is named fwui_, not ui_,
# because CMake's AUTOUIC claims every `ui_*.h` include for uic.  The
# form's file is loaded at run time from the Qt resource
# `:/ui/<Name>.ui`, so the .ui must be in a qrc under that prefix.
macro(fc_wrap_fwui OUT_VAR)
    foreach(_fwui_src ${ARGN})
        get_filename_component(_fwui_name "${_fwui_src}" NAME_WE)
        set(_fwui_out "${CMAKE_CURRENT_BINARY_DIR}/fwui_${_fwui_name}.h")
        add_custom_command(
            OUTPUT "${_fwui_out}"
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/src/Tools/fwuic.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/${_fwui_src}" -o "${_fwui_out}"
            MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${_fwui_src}"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/Tools/fwuic.py"
            COMMENT "fwuic: ${_fwui_src} as host widget models"
        )
        list(APPEND ${OUT_VAR} "${_fwui_out}")
    endforeach()
endmacro(fc_wrap_fwui)
