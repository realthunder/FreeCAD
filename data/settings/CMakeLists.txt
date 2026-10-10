
SET(Conf_Files 
    LargeFontIcon.FCParam
    VerticalToolBars.FCParam
    VerticalSplit.FCParam
    DisplaySettings.FCParam
)

# The two overlay presets are themes now ("Overlay dark theme", "Overlay light
# theme" in src/Gui/PreferencePacks). A tree built before that still has the
# copies made of them here, and every *.FCParam in this directory is listed
# in the presets menu: take them out, or the menu shows what the source no
# longer has.
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    file(REMOVE
        "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/settings/OverlayDark.FCParam"
        "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/settings/OverlayLight.FCParam")
endif()

ADD_CUSTOM_TARGET(Conf_data ALL
    SOURCES ${Conf_Files}
)

if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    # Application::getResourceDir() is CMAKE_INSTALL_DATADIR, which is "share"
    # only where GNUInstallDirs says so -- MSVC builds use "data". Copying to a
    # hard-coded "share" left the presets somewhere nothing reads on Windows.
    fc_copy_sources(Conf_data "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/settings" ${Conf_Files})
endif()

INSTALL(
    FILES
        ${Conf_Files}
    DESTINATION
        ${CMAKE_INSTALL_DATADIR}/settings
)

