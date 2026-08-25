if(MSVC)
    add_definitions(-DFCApp -DFCArea -DHAVE_ACOSH -DHAVE_ASINH -DHAVE_ATANH)
else(MSVC)
    add_definitions(-DFCApp -DFCArea -DHAVE_LIMITS_H -DHAVE_CONFIG_H)
endif(MSVC)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)

include_directories(
    SYSTEM
    ${Boost_INCLUDE_DIRS}
    ${OCC_INCLUDE_DIR}
    ${PYTHON_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
    ${XercesC_INCLUDE_DIRS}
)

link_directories(${OCC_LIBRARY_DIR})

set(Area_LIBS
    Part
    libarea::area
    FreeCADApp
)

generate_from_xml(AreaPy)

SET(Python_SRCS
    AreaPy.xml
    AreaPyImp.cpp
)
SOURCE_GROUP("Python" FILES ${Python_SRCS})

SET(Area_SRCS
    ${Python_SRCS}
    AppArea.cpp
    Area.cpp
    Area.h
    ClipperEnums.h
    AreaParams.h
    ParamsHelper.h
    PreCompiled.cpp
    PreCompiled.h
)

add_library(Area SHARED ${Area_SRCS})
target_link_libraries(Area ${Area_LIBS})
if (FREECAD_WARN_ERROR)
    target_compile_warn_error(Area)
endif()

SET_BIN_DIR(Area Area /Mod/Area)
SET_PYTHON_PREFIX_SUFFIX(Area)

install(TARGETS Area DESTINATION ${CMAKE_INSTALL_LIBDIR})
