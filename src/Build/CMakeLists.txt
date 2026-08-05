
set(PACKAGE_WCREF "$WCREV$")
set(PACKAGE_WCDATE "$WCDATE$")
set(PACKAGE_WCURL "$WCURL$")

# If the sources don't include a Version.h then create one
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Version.h)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/Version.h.in)

    # There is no way to know the working copy revision has changed without
    # asking git, so this has to run every build. A custom TARGET is how you say
    # that; the custom COMMAND this replaced declared OUTPUT "version_check"
    # while SubWCRev.py only ever writes Version.h.out, so the declared output
    # never existed. That did re-run every build, but for the wrong reason, and
    # it left the generator unable to reason about the Version.h edge below --
    # Version.h then went stale for days while the binaries kept reporting an
    # old FCRevision/FCRepositoryHash.
    add_custom_target(version_check
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/Tools/SubWCRev.py
            --srcdir=${CMAKE_SOURCE_DIR} --bindir=${CMAKE_BINARY_DIR}
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/Version.h.out
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking working copy revision")

    # Runs every build too, but copy_if_different is what keeps that cheap:
    # Version.h only gets a new timestamp when the revision actually moved, so
    # the translation units including it rebuild then and not otherwise.
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/Version.h
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/Version.h.out
            ${CMAKE_CURRENT_BINARY_DIR}/Version.h
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS version_check ${CMAKE_CURRENT_BINARY_DIR}/Version.h.out)

    ADD_CUSTOM_TARGET(fc_version DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Version.h)

else (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Version.h)

    ADD_CUSTOM_TARGET(fc_version DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Version.h)

endif (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Version.h)
