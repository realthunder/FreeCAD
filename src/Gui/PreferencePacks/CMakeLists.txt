
SET(PreferencePacks_Files
"package.xml"
)

# The themes a build ships, matching what upstream bundles.
SET(PreferencePacks_Directories
"Classic"
"Dark"
"Light"
)

# Kept in the source tree but not shipped: the stylesheets they select are
# installed either way, so the look each one describes is still a choice on the
# Theme page. To bundle one, list it above and declare it in package.xml.
SET(PreferencePacks_Unbundled
"Darker"
"Dark modern"
"Dark behave"
"Dark contrast"
"ProDark"
"Light modern"
)

ADD_CUSTOM_TARGET(PreferencePacks_data ALL
SOURCES ${PreferencePacks_Files} ${PreferencePacks_Directories} ${PreferencePacks_Unbundled}
)

FILE(COPY ${PreferencePacks_Files} ${PreferencePacks_Directories} DESTINATION "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Gui/PreferencePacks")

INSTALL(
FILES
    ${PreferencePacks_Files}
DESTINATION
    ${CMAKE_INSTALL_DATADIR}/Gui/PreferencePacks
)

INSTALL(
DIRECTORY
    ${PreferencePacks_Directories}
DESTINATION
    ${CMAKE_INSTALL_DATADIR}/Gui/PreferencePacks
)