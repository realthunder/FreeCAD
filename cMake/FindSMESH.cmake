# Try to find Salome SMESH
# Once done this will define
#
# SMESH_FOUND         - system has Salome SMESH
# SMESH_INCLUDE_DIR   - where the Salome SMESH include directory can be found
# SMESH_LIBRARIES     - Link this to use Salome SMESH
#

# A packaged SMESH exports imported targets whose link interface names
# Boost::filesystem, Boost::regex, Boost::serialization and Boost::thread, and
# CMake rejects the config the moment those targets do not exist. Upstream gets
# away with never asking for them because VTK's own config happens to run
# find_package(Boost <exact> ) for its xdmf3 module first -- which only works
# while the environment's Boost is exactly the one VTK was built against. Ask
# for them directly instead, so the order does not depend on that accident.
find_package(Boost QUIET COMPONENTS filesystem regex serialization thread)

# SMESH needs VTK. Ask for one module rather than the whole toolkit: a bare
# find_package(VTK) requires EVERY module VTK was built with, and 9.6's xdmf3
# wants an EXACT Boost version that need not be the one FreeCAD links against.
# SetupSalomeSMESH() finds the full component list immediately after this.
find_package(VTK COMPONENTS vtkCommonCore REQUIRED NO_MODULE)

# If this definition is not set, linker errors will occur against SMESH on 64 bit machines.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	add_definitions(-DSALOME_USE_64BIT_IDS)
endif(CMAKE_SIZEOF_VOID_P EQUAL 8)
 
FIND_PATH(SMESH_INCLUDE_DIR SMESH_Mesh.hxx
# These are default search paths, why specify them?
PATH_SUFFIXES smesh SMESH smesh/SMESH
)
FIND_LIBRARY(SMESH_LIBRARY SMESH)

IF(SMESH_INCLUDE_DIR)
	SET(SMESH_INC_ROOT "${SMESH_INCLUDE_DIR}/..")
	# Append extra include dirs. This has to build a real CMake list: the former
	# one-string form carried the newlines and tabs of its own source lines into
	# every entry, so the compiler was handed -I"<sourcedir>	<realpath>".
	foreach(_smesh_inc
		Controls Driver DriverDAT DriverGMF DriverSTL DriverUNV
		Geom Kernel MEFISTO2 MeshVSLink Netgen NETGENPlugin
		SMDS SMESHDS SMESHUtils StdMeshers)
		list(APPEND SMESH_INCLUDE_DIR "${SMESH_INC_ROOT}/${_smesh_inc}")
	endforeach()
	# SetupSalomeSMESH() reads SMESH_INCLUDE_PATH, not SMESH_INCLUDE_DIR, when it
	# takes the external branch -- leaving it unset compiles with no SMESH headers.
	set(SMESH_INCLUDE_PATH ${SMESH_INCLUDE_DIR})
ELSE(SMESH_INCLUDE_DIR)
	message(FATAL_ERROR "SMESH include directories not found!")
ENDIF(SMESH_INCLUDE_DIR)

SET(SMESH_FOUND FALSE)
IF(SMESH_LIBRARY)
  SET(SMESH_FOUND TRUE)
  GET_FILENAME_COMPONENT(SMESH_LIBRARY_DIR ${SMESH_LIBRARY} PATH)

  # A packaged SMESH keeps its version in its CMake config. Read the numbers out
  # of that file rather than including it: including it would also define the
  # imported targets, whose link interface names Boost::serialization and so
  # drags a second Boost into a build that already links its own. Without this
  # the hardcoded internal 7.7.1 stands and every "#if SMESH_VERSION_MAJOR >= 9"
  # in src/Mod/Fem compiles the pre-9 branch against a 9.x SMESH.
  find_file(SMESH_CONFIG_FILE NAMES SMESHConfig.cmake
    HINTS "${SMESH_LIBRARY_DIR}/.." "${SMESH_LIBRARY_DIR}/../.."
    PATH_SUFFIXES cmake lib/cmake share/cmake NO_DEFAULT_PATH)
  if(SMESH_CONFIG_FILE)
    foreach(_smesh_part MAJOR MINOR PATCH TWEAK)
      file(STRINGS "${SMESH_CONFIG_FILE}" _smesh_ver_line
        REGEX "set\\(SMESH_VERSION_${_smesh_part} +[0-9]+\\)")
      if(_smesh_ver_line)
        string(REGEX MATCH "[0-9]+ *\\)" _smesh_ver_num "${_smesh_ver_line}")
        string(REGEX MATCH "[0-9]+" SMESH_VERSION_${_smesh_part} "${_smesh_ver_num}")
      endif()
    endforeach()
    message(STATUS "External SMESH version: ${SMESH_VERSION_MAJOR}.${SMESH_VERSION_MINOR}.${SMESH_VERSION_PATCH}.${SMESH_VERSION_TWEAK}")
  endif()
  set(SMESH_LIBRARIES
    ${SMESH_LIBRARY_DIR}/libDriver.so
    ${SMESH_LIBRARY_DIR}/libDriverDAT.so
    ${SMESH_LIBRARY_DIR}/libDriverSTL.so
    ${SMESH_LIBRARY_DIR}/libDriverUNV.so
    ${SMESH_LIBRARY_DIR}/libSMDS.so
    ${SMESH_LIBRARY_DIR}/libSMESH.so
    ${SMESH_LIBRARY_DIR}/libSMESHDS.so
    ${SMESH_LIBRARY_DIR}/libStdMeshers.so
  )
  # A packaged SMESH may also carry the NETGEN plugin (the conda smesh package
  # links its own nglib), which is what -DFCWithNetgen then talks to.
  if(BUILD_FEM_NETGEN AND EXISTS ${SMESH_LIBRARY_DIR}/libNETGENPlugin.so)
    list(APPEND SMESH_LIBRARIES ${SMESH_LIBRARY_DIR}/libNETGENPlugin.so)
  endif()
  set(EXTERNAL_SMESH_LIBS ${SMESH_LIBRARIES})
ELSE(SMESH_LIBRARY)
	message(FATAL_ERROR "SMESH libraries NOT FOUND!")
ENDIF(SMESH_LIBRARY)

