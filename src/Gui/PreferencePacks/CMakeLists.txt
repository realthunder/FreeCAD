
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

# The packs' contents, enumerated as files rather than as directories.
#
# fc_copy_sources runs `cmake -E copy` once per item and that does not take a
# directory, which is why this was a configure-time FILE(COPY) instead -- and
# why editing a .cfg did nothing until the next configure. The pack in the
# build tree stayed the old one, silently, with the build reporting success.
# CONFIGURE_DEPENDS re-runs the glob at build time, so adding a file to a pack
# is picked up too, not just editing one.
SET(PreferencePacks_PackFiles)
FOREACH(pack ${PreferencePacks_Directories})
    FILE(GLOB pack_files CONFIGURE_DEPENDS
         RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${pack}/*")
    LIST(APPEND PreferencePacks_PackFiles ${pack_files})
ENDFOREACH()

SET(PreferencePacks_UnbundledFiles)
FOREACH(pack ${PreferencePacks_Unbundled})
    FILE(GLOB pack_files CONFIGURE_DEPENDS
         RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/${pack}/*")
    LIST(APPEND PreferencePacks_UnbundledFiles ${pack_files})
ENDFOREACH()

ADD_CUSTOM_TARGET(PreferencePacks_data ALL
SOURCES ${PreferencePacks_Files} ${PreferencePacks_PackFiles} ${PreferencePacks_UnbundledFiles}
)

fc_copy_sources(PreferencePacks_data "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Gui/PreferencePacks"
                                  ${PreferencePacks_Files}
                                  ${PreferencePacks_PackFiles})

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