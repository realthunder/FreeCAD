if (${PYTHON_VERSION_STRING} VERSION_LESS "3.10")
    # deprecated distutils package still required for ubuntu 20.04 and similar
    # see https://bugs.launchpad.net/ubuntu/+source/python3.8/+bug/2039511
    # TODO: remove this once we drop support for 20.04
    EXECUTE_PROCESS(COMMAND ${PYTHON_EXECUTABLE} -c
        "from distutils.sysconfig import get_python_lib; print(get_python_lib(plat_specific=True))"
        OUTPUT_VARIABLE python_libs OUTPUT_STRIP_TRAILING_WHITESPACE )
else()
    EXECUTE_PROCESS(COMMAND ${PYTHON_EXECUTABLE} -c
        "from sysconfig import get_path; print(get_path('purelib'))"
        OUTPUT_VARIABLE python_libs OUTPUT_STRIP_TRAILING_WHITESPACE )
endif()

SET(PYTHON_MAIN_DIR ${python_libs})

set(NAMESPACE_DIR "${CMAKE_BINARY_DIR}/Ext/freecad")
set(NAMESPACE_INIT "${NAMESPACE_DIR}/__init__.py")
if (WIN32)
    get_filename_component(FREECAD_LIBRARY_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}"
                       REALPATH BASE_DIR "${CMAKE_INSTALL_PREFIX}")
    set( ${CMAKE_INSTALL_BINDIR})
else()
    get_filename_component(FREECAD_LIBRARY_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}"
                       REALPATH BASE_DIR "${CMAKE_INSTALL_PREFIX}")
endif()

configure_file(__init__.py.template ${NAMESPACE_INIT})
configure_file(project_utility.py ${NAMESPACE_DIR}/project_utility.py)
configure_file(UiTools.py ${NAMESPACE_DIR}/UiTools.py)
configure_file(utils.py ${NAMESPACE_DIR}/utils.py)
configure_file(deprecation.py ${NAMESPACE_DIR}/deprecation.py)
configure_file(prefs.py ${NAMESPACE_DIR}/prefs.py)

# MCP debug console (pure-Python; requires the 'mcp' package at runtime)
configure_file(mcp_console/__init__.py ${NAMESPACE_DIR}/mcp_console/__init__.py COPYONLY)
configure_file(mcp_console/server.py ${NAMESPACE_DIR}/mcp_console/server.py COPYONLY)

# Render-effect factory: instantiates the bundled effect packages
# (share/Renderer/effects) into user documents (docs/RenderEngine.md §5.11)
configure_file(rendereffects/__init__.py ${NAMESPACE_DIR}/rendereffects/__init__.py COPYONLY)

# The pyodide bootstrap: installs the sandbox runtime and its packages
# per user (docs/PyodideHost.md sec 12).  Pure Python, host side.
configure_file(pyodide/__init__.py ${NAMESPACE_DIR}/pyodide/__init__.py COPYONLY)

if (INSTALL_TO_SITEPACKAGES)
    SET(SITE_PACKAGE_DIR ${PYTHON_MAIN_DIR}/freecad)
else ()
    SET(SITE_PACKAGE_DIR Ext/freecad)
endif()

INSTALL(
    FILES
        ${NAMESPACE_INIT}
        deprecation.py
        prefs.py
        project_utility.py
        UiTools.py
        utils.py
    DESTINATION
        ${SITE_PACKAGE_DIR}
)

INSTALL(
    FILES
        mcp_console/__init__.py
        mcp_console/server.py
    DESTINATION
        ${SITE_PACKAGE_DIR}/mcp_console
)

INSTALL(
    FILES
        rendereffects/__init__.py
    DESTINATION
        ${SITE_PACKAGE_DIR}/rendereffects
)

INSTALL(
    FILES
        pyodide/__init__.py
    DESTINATION
        ${SITE_PACKAGE_DIR}/pyodide
)

add_subdirectory(fc_cadquery)
