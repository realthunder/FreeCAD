add_subdirectory(App)
add_subdirectory(libarea)

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
