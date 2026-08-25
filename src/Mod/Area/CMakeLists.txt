# libarea used to be vendored here and built as part of this module. It is its
# own repository and its own package now, so that IfcOpenShell and FreeCAD link
# one library rather than each carrying a fork of it.
# 0.3.0 is where both Clippers grew a Z member, which the adaptive clearing
# in PyArea needs and which changes the layout of a point -- so an older
# one is not merely missing a feature, it disagrees about the ABI.
find_package(libarea 0.3.0 CONFIG REQUIRED)

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
