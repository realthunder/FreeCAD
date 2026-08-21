# The Python module "libarea": libarea's own wrapper, plus the two pieces that
# never belonged to the library -- Adaptive.cpp, FreeCAD's adaptive-clearing
# toolpaths, and AreaDxf.cpp, which needs the Import module for dxf.h. The
# library itself comes from the libarea package, found in the parent directory.
if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS /wd4244)
endif(MSVC)

include_directories(
    ${PYTHON_INCLUDE_DIRS}
    ${OCC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/Mod/Import/App/dxf
)

# Import and Part hand on their OCCT dependencies as bare -lTK names, so the
# link needs to know where those live.
link_directories(${OCC_LIBRARY_DIR})

if(NOT FREECAD_USE_PYBIND11)
    if(NOT FREECAD_LIBPACK_USE OR FREECAD_LIBPACK_CHECKFILE_CLBUNDLER)
        # boost-python >= 1.67 on some platforms has suffix
        set(BOOST_PY_SUFFIX ${PYTHON_VERSION_MAJOR}${PYTHON_VERSION_MINOR})

        find_package( Boost COMPONENTS python${BOOST_PY_SUFFIX} )
        if (NOT Boost_PYTHON${BOOST_PY_SUFFIX}_FOUND)
            # try just the major version
            find_package( Boost COMPONENTS python${PYTHON_VERSION_MAJOR} )
            if (NOT Boost_PYTHON${PYTHON_VERSION_MAJOR}_FOUND)
                # unversioned
                find_package( Boost COMPONENTS python REQUIRED)
            endif()
        endif()

        if(Boost_FOUND)
            include_directories(${Boost_INCLUDE_DIRS})
            MESSAGE(STATUS "found Boost: " ${Boost_LIB_VERSION})
        endif()
    else()
        include_directories(${Boost_INCLUDE_DIRS})
    endif()
    set(PYAREA_SRC
        PythonStuff.cpp
        PythonStuff.h
    )
else()
    include_directories(${pybind11_INCLUDE_DIR})
    set(PYAREA_SRC
        pyarea.cpp
    )
endif(NOT FREECAD_USE_PYBIND11)

add_library(area SHARED
    ${PYAREA_SRC}
    Adaptive.cpp
    Adaptive.hpp
    AreaDxf.cpp
    AreaDxf.h
)

set(area_LIBS
    libarea::area
    Import
    ${Boost_LIBRARIES}
)
if(BUILD_DYNAMIC_LINK_PYTHON)
    list(APPEND area_LIBS ${PYTHON_LIBRARIES})
endif(BUILD_DYNAMIC_LINK_PYTHON)

target_link_libraries(area ${area_LIBS})

# TODO why CMAKE_SHARED_LINKER_FLAGS is not used here?
# This is a dirty workaround!
if(NOT BUILD_DYNAMIC_LINK_PYTHON AND CMAKE_COMPILER_IS_CLANGXX)
    target_link_libraries(area "-Wl,-undefined,dynamic_lookup")
endif()

SET_BIN_DIR(area libarea /Mod/Area)
SET_PYTHON_PREFIX_SUFFIX(area)

install(TARGETS area DESTINATION ${CMAKE_INSTALL_LIBDIR})
