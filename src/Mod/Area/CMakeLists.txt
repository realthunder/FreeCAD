# libarea used to be vendored here and built as part of this module. It is its
# own repository and its own package now, so that IfcOpenShell and FreeCAD link
# one library rather than each carrying a fork of it.
find_package(libarea CONFIG REQUIRED)

add_subdirectory(App)
add_subdirectory(PyArea)

set(Area_Scripts
    Init.py
)

INSTALL(
    FILES
        ${Area_Scripts}
    DESTINATION
        Mod/Area
)

ADD_CUSTOM_TARGET(AreaScripts ALL
    SOURCES ${Area_Scripts}
)

fc_copy_sources(AreaScripts "${CMAKE_BINARY_DIR}/Mod/Area" ${Area_Scripts})
