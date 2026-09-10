macro(InitializeFreeCADBuildOptions)
    # ==============================================================================
    # =================   All the options for the build process    =================
    # ==============================================================================

    option(BUILD_FORCE_DIRECTORY "The build directory must be different to the source directory." OFF)
    option(BUILD_GUI "Build FreeCAD Gui. Otherwise you have only the command line and the Python import module." ON)
    option(FREECAD_USE_EXTERNAL_ZIPIOS "Use system installed zipios++ instead of the bundled." OFF)
    option(FREECAD_USE_EXTERNAL_SMESH "Use system installed smesh instead of the bundled." OFF)
    option(FREECAD_USE_EXTERNAL_KDL "Use system installed orocos-kdl instead of the bundled." OFF)
    option(FREECAD_USE_EXTERNAL_FMT "Use system installed fmt library if available instead of fetching the source." ON)
    option(FREECAD_USE_EXTERNAL_ONDSELSOLVER "Use system installed OndselSolver instead of git submodule." OFF)
    option(FREECAD_USE_FREETYPE "Builds the features using FreeType libs" ON)
    option(FREECAD_BUILD_DEBIAN "Prepare for a build of a Debian package" OFF)
    option(BUILD_WITH_CONDA "Set ON if you build FreeCAD with conda" OFF)
    option(BUILD_DYNAMIC_LINK_PYTHON "If OFF extension-modules do not link against python-libraries" ON)
    option(INSTALL_TO_SITEPACKAGES "If ON the freecad root namespace (python) is installed into python's site-packages" OFF)
    option(OCCT_CMAKE_FALLBACK "disable usage of occt-config files" OFF)
    if (WIN32 OR APPLE)
        option(FREECAD_USE_QT_FILEDIALOG "Use Qt's file dialog instead of the native one." OFF)
    else()
        option(FREECAD_USE_QT_FILEDIALOG "Use Qt's file dialog instead of the native one." ON)
    endif()
    option(FREECAD_FORCE_USE_QT_FILEDIALOG "Always use Qt's file dialog and ignore user parameter settings." OFF)

    # == Win32 is default behaviour use the LibPack copied in Source tree ==========
    if(MSVC)
        option(FREECAD_RELEASE_PDB "Create PDB files for Release version." ON)
        option(FREECAD_RELEASE_SEH "Enable Structured Exception Handling for Release version." ON)
        option(FREECAD_LIBPACK_USE "Use the LibPack to Build FreeCAD (only Win32 so far)." ON)
        option(FREECAD_USE_PCH "Activate precompiled headers where it's used." ON)

        if (DEFINED ENV{FREECAD_LIBPACK_DIR})
            set(FREECAD_LIBPACK_DIR $ENV{FREECAD_LIBPACK_DIR} CACHE PATH  "Directory of the FreeCAD LibPack")
            message(STATUS "Found libpack env variable: ${FREECAD_LIBPACK_DIR}")
        else()
            set(FREECAD_LIBPACK_DIR ${CMAKE_SOURCE_DIR} CACHE PATH  "Directory of the FreeCAD LibPack")
        endif()

        set(LIBPACK_FOUND OFF )
        if (EXISTS ${FREECAD_LIBPACK_DIR}/plugins/imageformats/qsvg.dll)
            set(LIBPACK_FOUND ON )
            set(COPY_LIBPACK_BIN_TO_BUILD OFF )
            # Create install commands for dependencies for INSTALL target in FreeCAD solution
            option(FREECAD_INSTALL_DEPEND_DIRS "Create install dependency commands for the INSTALL target found
                in the FreeCAD solution." ON)
            # Copy libpack smaller dependency folders to build folder per user request - if non-existent at destination
            if (NOT EXISTS ${CMAKE_BINARY_DIR}/bin/imageformats/qsvg.dll)
                option(FREECAD_COPY_DEPEND_DIRS_TO_BUILD "Copy smaller libpack dependency directories to build directory." OFF)
            endif()
            # Copy libpack 'bin' directory contents to build 'bin' per user request - only IF NOT EXISTS already
            if (NOT EXISTS ${CMAKE_BINARY_DIR}/bin/DLLs)
                set(COPY_LIBPACK_BIN_TO_BUILD ON )
                option(FREECAD_COPY_LIBPACK_BIN_TO_BUILD "Copy larger libpack dependency 'bin' folder to the build directory." OFF)
                # Copy only the minimum number of files to get a working application
                option(FREECAD_COPY_PLUGINS_BIN_TO_BUILD "Copy plugins to the build directory." OFF)
            endif()
        else()
            message("Libpack NOT found.\nIf you intend to use a Windows libpack, set the FREECAD_LIBPACK_DIR to the libpack directory.")
            message(STATUS "Visit: https://github.com/FreeCAD/FreeCAD-Libpack/releases/ for Windows libpack downloads.")
        endif()
    else(MSVC)
        option(FREECAD_LIBPACK_USE "Use the LibPack to Build FreeCAD (only Win32 so far)." OFF)
        set(FREECAD_LIBPACK_DIR ""  CACHE PATH  "Directory of the FreeCAD LibPack")
    endif(MSVC)

    ChooseQtVersion()

    # https://blog.kitware.com/constraining-values-with-comboboxes-in-cmake-cmake-gui/
    set(FREECAD_USE_OCC_VARIANT "Community Edition"  CACHE STRING  "Official OpenCASCADE version or community edition")
    set_property(CACHE FREECAD_USE_OCC_VARIANT PROPERTY STRINGS
                 "Official Version"
                 "Community Edition"
    )

    configure_file(${CMAKE_SOURCE_DIR}/src/QtOpenGL.h.cmake ${CMAKE_BINARY_DIR}/src/QtOpenGL.h)

    option(BUILD_DESIGNER_PLUGIN "Build and install the designer plugin" OFF)

    if(APPLE)
        option(FREECAD_CREATE_MAC_APP "Create app bundle on install" OFF)

        if(FREECAD_CREATE_MAC_APP)
            install(
                DIRECTORY ${CMAKE_SOURCE_DIR}/src/MacAppBundle/FreeCAD.app/
                DESTINATION ${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app
            )

            # It should be safe to assume we've got sed on OSX...
            install(CODE "
                execute_process(COMMAND
                    sed -i \"\" -e s/VERSION_STRING_FROM_CMAKE/${PACKAGE_VERSION}/
                    -e s/NAME_STRING_FROM_CMAKE/${PROJECT_NAME}/
                    ${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app/Contents/Info.plist)
                   ")

            set(CMAKE_INSTALL_PREFIX
                ${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.app/Contents)
            set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib )
        endif(FREECAD_CREATE_MAC_APP)
        set(CMAKE_MACOSX_RPATH TRUE )
    endif(APPLE)

    option(BUILD_FEM "Build the FreeCAD FEM module" ON)
    option(BUILD_SANDBOX "Build the FreeCAD Sandbox module which is only for testing purposes" OFF)
    option(BUILD_TEMPLATE "Build the FreeCAD template module which is only for testing purposes" OFF)
    option(BUILD_ADDONMGR "Build the FreeCAD addon manager module" ON)
    option(BUILD_BIM "Build the FreeCAD BIM module" ON)
    option(BUILD_DRAFT "Build the FreeCAD draft module" ON)
    option(BUILD_DRAWING "Build the FreeCAD drawing module" OFF)
    option(BUILD_HELP "Build the FreeCAD help module" ON)
    option(BUILD_IDF "Build the FreeCAD idf module" ON)
    option(BUILD_IMPORT "Build the FreeCAD import module" ON)
    option(BUILD_INSPECTION "Build the FreeCAD inspection module" ON)
    option(BUILD_JTREADER "Build the FreeCAD jt reader module" OFF)
    option(BUILD_MATERIAL "Build the FreeCAD material module" ON)
    option(BUILD_MESH "Build the FreeCAD mesh module" ON)
    option(BUILD_MESH_PART "Build the FreeCAD mesh part module" ON)
    option(BUILD_FLAT_MESH "Build the FreeCAD flat mesh module" ON)
    option(BUILD_OPENSCAD "Build the FreeCAD openscad module" ON)
    option(BUILD_PART "Build the FreeCAD part module" ON)
    option(BUILD_PART_DESIGN "Build the FreeCAD part design module" ON)
    option(BUILD_AREA "Build the FreeCAD 2D area engine (libarea/Clipper)" ON)
    option(BUILD_CAM "Build the FreeCAD CAM module" ON)
    option(BUILD_CAM_SIMULATOR_GL "Build the CAM simulator's own OpenGL viewer" ON)
    option(BUILD_ASSEMBLY "Build the FreeCAD Assembly module" ON)
    option(BUILD_PLOT "Build the FreeCAD plot module" ON)
    option(BUILD_POINTS "Build the FreeCAD points module" ON)
    option(BUILD_REVERSEENGINEERING "Build the FreeCAD reverse engineering module" ON)
    option(BUILD_ROBOT "Build the FreeCAD robot module" ON)
    option(BUILD_SHOW "Build the FreeCAD Show module (helper module for visibility automation)" ON)
    option(BUILD_SKETCHER "Build the FreeCAD sketcher module" ON)
    option(BUILD_SPREADSHEET "Build the FreeCAD spreadsheet module" ON)
    option(BUILD_START "Build the FreeCAD start module" ON)
    option(BUILD_TEST "Build the FreeCAD test module" ON)
    option(BUILD_TECHDRAW "Build the FreeCAD Technical Drawing module" ON)
    option(BUILD_TUX "Build the FreeCAD Tux module" ON)
    # OFF: the module needs Qt WebEngine, which the conda-forge stack this
    # project builds against does not make available the way a distro does.
    # qt6-main ships without it, the separate qt6-webengine package exists
    # only for some platforms (there is none for osx-64) and lags qt6-main
    # by a release, so a matching set has to be pinned by hand; conda's
    # pyside6 is built without QtWebEngineWidgets regardless. Leaving this
    # ON made a fresh configure fail outright, because SetupQt.cmake asks
    # for the WebEngineWidgets component REQUIRED. Turn it on deliberately,
    # on a stack that has WebEngine -- see docs/DevEnvironment.md.
    option(BUILD_WEB "Build the FreeCAD web module" OFF)
    option(BUILD_SURFACE "Build the FreeCAD surface module" ON)
    option(BUILD_VR "Build the FreeCAD Oculus Rift support (need Oculus SDK 4.x or higher)" OFF)
    option(BUILD_CLOUD "Build the FreeCAD cloud module" OFF)
    option(BUILD_BGFX "Build bgfx renderer module" ON)
    if(BUILD_BGFX AND NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdParty/bgfx/CMakeLists.txt")
        # ON by default, but the engine lives in a submodule a plain
        # clone does not have; degrade instead of failing configure.
        message(WARNING "BUILD_BGFX is ON but the bgfx submodule is not "
                        "checked out (git submodule update --init "
                        "src/3rdParty/bgfx); building without the renderer.")
        set(BUILD_BGFX OFF)
    endif()
    option(BUILD_DILIGENT "Build DiligentEngine renderer module" OFF)
    # The Python sandbox (docs/ExpressionSandbox.md): ImageHost is the
    # runtime-agnostic seam, and a runtime carries the guest behind it.
    # THE runtime is pyodide on the bare V8 of the v8-embed package
    # (docs/PyodideHost.md), DETECTED rather than asked for: a box that
    # has v8-embed gets the sandbox, a box that does not still builds,
    # because the routing preference degrades to in-process evaluation
    # when no host is compiled in.  The wasm32-wasi image under wasmtime
    # (docs/ExpressionImage.md) is the REFERENCE implementation since
    # 2026-09-03 (docs/SandboxNetwork.md): kept building, gets no new
    # features, and is asked for explicitly -- it never turns itself on.
    find_package(v8-embed CONFIG QUIET)
    option(BUILD_EXPR_PYODIDE_HOST "Build the V8 + pyodide runtime for the Python sandbox (needs v8-embed; see docs/PyodideHost.md)" ${v8-embed_FOUND})
    set(FREECAD_PYODIDE_DIR "" CACHE PATH "Directory holding a pyodide distribution plus the fcx_image wheel, to install under the data dir as Pyodide/; see docs/PyodideHost.md")
    set(FREECAD_FCX_IMAGE_WHEEL "" CACHE FILEPATH "The fcx_image pyodide wheel to ship under the data dir as Pyodide/wheels/ (the runtime itself is bootstrapped per user); see docs/PyodideHost.md sec 12")
    set(FREECAD_PIVY_WHEEL "" CACHE FILEPATH "The pivy pyodide wheel (Coin and pivy.coin compiled for the guest, src/App/PyodideHost/pivy) to ship beside the fcx_image wheel; see docs/Sandbox.md 7.10")
    set(FREECAD_BUNDLED_WHEELS "" CACHE STRING "Third-party pure-Python wheels to bundle for the guest beside the fcx_image wheel, a ;-list of paths (ipywidgets and traitlets for the forms, fetched by scripts/sandbox-fetch-wheels.py); see docs/Sandbox.md 7.3")
    set(WASMTIME_CAPI_DIR "" CACHE PATH "wasmtime C API prefix (include/ + lib/libwasmtime.so) for BUILD_EXPR_WASI_RUNTIME")
    option(BUILD_EXPR_WASI_RUNTIME "Build the wasmtime runtime for the wasm32-wasi reference image of the Python sandbox (needs the wasmtime C API; see docs/ExpressionImage.md)" OFF)
    set(FREECAD_EXPR_IMAGE_DIR "" CACHE PATH "Directory holding a built reference sandbox image (fcx_image.wasm + Lib/) to install under the data dir as Fcx/; see docs/ExpressionImage.md")
    # The seam itself follows the runtimes: on when at least one is, and
    # ON without any runtime is an error rather than a silent nothing,
    # because it was asked for explicitly.
    if(BUILD_EXPR_PYODIDE_HOST OR BUILD_EXPR_WASI_RUNTIME)
        set(_expr_host_default ON)
    else()
        set(_expr_host_default OFF)
    endif()
    option(BUILD_EXPR_IMAGE_HOST "Build the Python sandbox host seam (ImageHost); needs at least one runtime, BUILD_EXPR_PYODIDE_HOST or BUILD_EXPR_WASI_RUNTIME" ${_expr_host_default})
    # Blender's Cycles path tracer as a vendored renderer
    # (docs/CyclesIntegration.md). OFF: it is a heavy build with
    # environment dependencies (OpenImageIO, Embree, OpenImageDenoise),
    # so it is asked for rather than degraded into like bgfx.
    option(BUILD_CYCLES "Build the Cycles path tracer renderer module" OFF)
    if(BUILD_CYCLES AND NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdParty/cycles/CMakeLists.txt")
        message(FATAL_ERROR "BUILD_CYCLES is ON but the cycles submodule is not "
                            "checked out (git submodule update --init "
                            "src/3rdParty/cycles).")
    endif()
    # MaterialX: the node-graph material description language both
    # renderer back-ends read (docs/CyclesIntegration.md sec 8 item 15
    # phase B). ON like bgfx rather than asked for like Cycles -- Core,
    # Format, GenShader and GenGlsl have no external dependency at all
    # -- and degraded into rather than fatal, because the engine lives
    # in a submodule a plain clone does not have.
    option(BUILD_MATERIALX "Build MaterialX material-graph support" ON)
    if(BUILD_MATERIALX AND NOT EXISTS "${CMAKE_SOURCE_DIR}/src/3rdParty/MaterialX/CMakeLists.txt")
        message(WARNING "BUILD_MATERIALX is ON but the MaterialX submodule is "
                        "not checked out (git submodule update --init "
                        "src/3rdParty/MaterialX); building without it.")
        set(BUILD_MATERIALX OFF)
    endif()
    option(BUILD_DILIGENT_SAMPLES "Build DiligentEngine samples" OFF)
    option(ENABLE_DEVELOPER_TESTS "Build the FreeCAD unit tests suit" OFF)

    if(MSVC)
        option(BUILD_FEM_NETGEN "Build the FreeCAD FEM module with the NETGEN mesher" ON)
        option(FREECAD_USE_PCL "Build the features that use PCL libs" OFF) # 3/5/2021 current LibPack uses non-C++17 FLANN
        option(FREECAD_USE_3DCONNEXION "Use the 3D connexion SDK to support 3d mouse." ON)
    elseif(APPLE)
        find_library(3DCONNEXIONCLIENT_FRAMEWORK 3DconnexionClient)
        if(IS_DIRECTORY ${3DCONNEXIONCLIENT_FRAMEWORK})
            option(FREECAD_USE_3DCONNEXION "Use the 3D connexion SDK to support 3d mouse." ON)
        else(IS_DIRECTORY ${3DCONNEXIONCLIENT_FRAMEWORK})
            option(FREECAD_USE_3DCONNEXION "Use the 3D connexion SDK to support 3d mouse." OFF)
        endif(IS_DIRECTORY ${3DCONNEXIONCLIENT_FRAMEWORK})
    else(MSVC)
        set(FREECAD_USE_3DCONNEXION OFF )
    endif(MSVC)
    if(NOT MSVC)
        option(BUILD_FEM_NETGEN "Build the FreeCAD FEM module with the NETGEN mesher" OFF)
        option(FREECAD_USE_PCL "Build the features that use PCL libs" OFF)
    endif(NOT MSVC)

    # if this is set override some options
    if (FREECAD_BUILD_DEBIAN)
        set(FREECAD_USE_EXTERNAL_ZIPIOS ON )
        # A Debian package for SMESH doesn't exist
        #set(FREECAD_USE_EXTERNAL_SMESH ON )
    endif (FREECAD_BUILD_DEBIAN)

    if(BUILD_FEM)
        set(BUILD_SMESH ON )
    endif()

    # CAM's tsp_solver and FlatMesh both bind through pybind11. Setting the
    # normal variable here wins over the option() in SetupPybind11, which runs
    # after this file and honours a value that is already set (CMP0077).
    if(BUILD_CAM OR BUILD_FLAT_MESH)
        set(FREECAD_USE_PYBIND11 ON )
    endif()

    # force build directory to be different to source directory
    if (BUILD_FORCE_DIRECTORY)
        if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
            message(FATAL_ERROR "The build directory (${CMAKE_BINARY_DIR}) must be different to the source directory (${CMAKE_SOURCE_DIR}).\n"
                                "Please choose another build directory! Or disable the option BUILD_FORCE_DIRECTORY.")
        endif()
    endif()
endmacro(InitializeFreeCADBuildOptions)
