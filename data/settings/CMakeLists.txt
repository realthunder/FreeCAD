
SET(Conf_Files 
    LargeFontIcon.FCParam
    VerticalToolBars.FCParam
    VerticalSplit.FCParam
    OverlayDark.FCParam
    OverlayLight.FCParam
    DisplaySettings.FCParam
)

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

