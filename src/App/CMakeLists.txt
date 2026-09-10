if(WIN32)
    add_definitions(-DFCApp)
    # BOOST_ALL_DYN_LINK, not BOOST_DYN_LINK: the latter is not a Boost macro and
    # has never done anything. Boost only marks its symbols __declspec(dllimport)
    # when told it is being linked dynamically; without it, calls still resolve
    # through import thunks but *data* symbols do not, so linking against a shared
    # Boost fails on boost::program_options::arg and options_description::
    # m_default_line_length. Static-Boost builds (LibPack) never noticed.
    add_definitions(-DBOOST_ALL_DYN_LINK)
endif(WIN32)

if(FREECAD_RELEASE_SEH)
    add_definitions(-DHAVE_SEH)
endif(FREECAD_RELEASE_SEH)

# This causes some problems with the resource files to be found, especially with the StartPage
IF(RESOURCEDIR)
    add_definitions(-DRESOURCEDIR="${RESOURCEDIR}")
ENDIF(RESOURCEDIR)

IF(LIBRARYDIR)
    add_definitions(-DLIBRARYDIR="${LIBRARYDIR}")
ENDIF(LIBRARYDIR)

IF(DOCDIR)
    add_definitions(-DDOCDIR="${DOCDIR}")
ENDIF(DOCDIR)

#if you want to use the old DAG structure uncomment this line
# add_definitions(-DUSE_OLD_DAG)

# -----------------------------------------------------------------------------

#write relevant cmake variables to a file for later access with python. Exported are all variables
#starting with BUILD. As the variable only exists if the user set it to ON a dict is useless, we
#use a python list for export.
set(_vars "const char CMakeVariables[] =\"cmake = [")
set(_delim "")
get_cmake_property(_variableNames VARIABLES)
foreach (_variableName ${_variableNames})
    if (${_variableName})
        STRING(REGEX MATCH "^[_]?[^_]*" _prefix "${_variableName}_")
        if(${_prefix} STREQUAL "BUILD")
            STRING(REPLACE "\\" "\\\\" _name ${_variableName})
            set(_vars "${_vars}${_delim}\\n\"\n\"\\\"${_name}\\\"")
            set(_delim ",")
        endif()
    endif ()
endforeach()
set(_vars "${_vars}]\\n\" \n;")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CMakeScript.hh "${_vars}" )

fc_copy_file_if_different(
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeScript.hh"
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeScript.h"
)

# -----------------------------------------------------------------------------

include_directories(
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/src
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/3rdParty/json/single_include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}
)

set(FreeCADApp_LIBS
    FreeCADBase
    ${Boost_LIBRARIES}
    fmt::fmt
)

include_directories(
    ${QtCore_INCLUDE_DIRS}
    ${QtXml_INCLUDE_DIRS}
)
list(APPEND FreeCADApp_LIBS
        ${QtCore_LIBRARIES}
        ${QtXml_LIBRARIES}
)

# The Python sandbox host seam (docs/ExpressionSandbox.md): ImageHost,
# the bridge, and whichever runtimes this build carries.  Host-only,
# never part of ExpressionCore.
if(BUILD_EXPR_IMAGE_HOST)
    if(NOT BUILD_EXPR_PYODIDE_HOST AND NOT BUILD_EXPR_WASI_RUNTIME)
        message(FATAL_ERROR "BUILD_EXPR_IMAGE_HOST is ON but no runtime is: "
                            "set BUILD_EXPR_PYODIDE_HOST (needs v8-embed; "
                            "docs/PyodideHost.md) or BUILD_EXPR_WASI_RUNTIME "
                            "(needs the wasmtime C API; docs/ExpressionImage.md)")
    endif()
    set(ExpressionImageHost_SRC ExpressionImageHost.cpp ExpressionImageBridge.cpp
                                ExpressionGuestProxy.cpp ExpressionImageRuntime.h)
    # Lets ExpressionEvaluator.cpp route evaluation into the image; every
    # other TU ignores it, and no header depends on it (no ABI edge).
    add_definitions(-DFC_EXPR_IMAGE_HOST)

    # The REFERENCE runtime (docs/SandboxNetwork.md sec 0): the wasm32-wasi
    # image under wasmtime, docs/ExpressionImage.md.  Off by default since
    # 2026-09-03; asked for explicitly, so a missing wasmtime is an error.
    if(BUILD_EXPR_WASI_RUNTIME)
        find_package(Wasmtime)
        if(NOT Wasmtime_FOUND)
            message(FATAL_ERROR "BUILD_EXPR_WASI_RUNTIME is ON but the wasmtime C "
                                "API was not found (set WASMTIME_CAPI_DIR; see "
                                "docs/ExpressionImage.md)")
        endif()
        list(APPEND ExpressionImageHost_SRC ExpressionWasmtimeRuntime.cpp)
        include_directories(${Wasmtime_INCLUDE_DIR})
        add_definitions(-DFC_EXPR_WASI_RUNTIME)
        list(APPEND FreeCADApp_LIBS Wasmtime::Wasmtime)

        # The release tarball's libwasmtime.so carries NO SONAME, so the link
        # records the bare file name and an installed FreeCAD finds it only if
        # a copy sits on its RPATH -- which is exactly the install libdir that
        # SET_BIN_DIR puts there.  A wasmtime that came from a package manager
        # is already inside the prefix (or in a system directory) and is a
        # dependency of the package instead, so bundling defaults off there.
        get_filename_component(_wasmtime_libdir "${Wasmtime_LIBRARY}" DIRECTORY)
        set(_wasmtime_bundle ON)
        foreach(_dir "${CMAKE_INSTALL_PREFIX}" /usr /lib /lib64)
            string(FIND "${_wasmtime_libdir}/" "${_dir}/" _at)
            if(_at EQUAL 0)
                set(_wasmtime_bundle OFF)
            endif()
        endforeach()
        option(FREECAD_BUNDLE_WASMTIME
               "Install a copy of libwasmtime beside FreeCAD's own libraries"
               ${_wasmtime_bundle})
        if(FREECAD_BUNDLE_WASMTIME)
            install(FILES ${Wasmtime_LIBRARY} DESTINATION ${CMAKE_INSTALL_LIBDIR})
        endif()

        # The image itself is cross-built by a SEPARATE project
        # (src/App/ExpressionImage, wasi-sdk + a wasm CPython), so a host build
        # can only install one that already exists.  Point
        # FREECAD_EXPR_IMAGE_DIR at that project's install/stage tree; with it
        # unset the build is fine and the sandbox is simply unavailable at run
        # time, which ExpressionImageHost.cpp reports and degrades from.
        if(FREECAD_EXPR_IMAGE_DIR)
            if(NOT EXISTS "${FREECAD_EXPR_IMAGE_DIR}/fcx_image.wasm")
                message(FATAL_ERROR "FREECAD_EXPR_IMAGE_DIR has no fcx_image.wasm: "
                                    "${FREECAD_EXPR_IMAGE_DIR}")
            endif()
            install(FILES ${FREECAD_EXPR_IMAGE_DIR}/fcx_image.wasm
                    DESTINATION ${CMAKE_INSTALL_DATADIR}/Fcx)
            install(DIRECTORY ${FREECAD_EXPR_IMAGE_DIR}/Lib
                    DESTINATION ${CMAKE_INSTALL_DATADIR}/Fcx)
            # ... and mirrored into the build tree beside the other data, the
            # way fc_copy_sources does it for module resources, so a build
            # directory resolves the image through the SAME code path an
            # installed FreeCAD uses instead of needing a preference set.
            set(_fcx_data ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Fcx)
            if(CMAKE_VERSION VERSION_LESS 3.26)
                set(_fcx_copy copy_directory)
            else()
                set(_fcx_copy copy_directory_if_different)
            endif()
            add_custom_target(fcx_image_data ALL
                COMMAND ${CMAKE_COMMAND} -E ${_fcx_copy}
                        ${FREECAD_EXPR_IMAGE_DIR} ${_fcx_data}
                COMMENT "Staging the reference sandbox image into the build tree")
        endif()
    endif()

    # The closed bridge member table, generated from the <Sandbox tier=.../>
    # annotations in the binding XMLs (docs/ExpressionSandbox.md sec 7.5).
    # The image build runs the same script for its facade classes, so both
    # sides of the boundary always agree.
    set(FCX_DISPATCH_INC ${CMAKE_CURRENT_BINARY_DIR}/FcxDispatch.inc)
    set(FCX_SANDBOX_GEN ${CMAKE_SOURCE_DIR}/src/Tools/bindings/generateSandboxFacades.py)
    # Same order-only-dep scoping as the binding generators below: the
    # command needs only python and its listed inputs.
    set(CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY ON)
    # The annotated XML list is the generator's own (ANNOTATED_XMLS),
    # asked for at configure time so the dependency list cannot drift
    # from what the generator reads (49 files as of G1 step 5).
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} ${FCX_SANDBOX_GEN} --list-xmls
        OUTPUT_VARIABLE _fcx_annotated_xmls
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _fcx_list_rc)
    if(NOT _fcx_list_rc EQUAL 0)
        message(FATAL_ERROR "${FCX_SANDBOX_GEN} --list-xmls failed")
    endif()
    string(REPLACE "\n" ";" _fcx_annotated_xmls "${_fcx_annotated_xmls}")
    list(TRANSFORM _fcx_annotated_xmls PREPEND ${CMAKE_SOURCE_DIR}/)
    # A grown ANNOTATED_XMLS must reconfigure, or the new XML's edits
    # never regenerate the table (see ImageSources.cmake).
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FCX_SANDBOX_GEN})
    add_custom_command(
        OUTPUT ${FCX_DISPATCH_INC}
        COMMAND ${PYTHON_EXECUTABLE} ${FCX_SANDBOX_GEN} --host-out ${FCX_DISPATCH_INC}
        DEPENDS ${FCX_SANDBOX_GEN} ${_fcx_annotated_xmls}
        COMMENT "Generating the expression sandbox dispatch table (FcxDispatch.inc)"
    )
    list(APPEND ExpressionImageHost_SRC ${FCX_DISPATCH_INC})
    set_source_files_properties(${FCX_DISPATCH_INC} PROPERTIES
        GENERATED TRUE HEADER_FILE_ONLY TRUE)

    # THE runtime (docs/PyodideHost.md): pyodide on the bare V8 of
    # v8-embed, behind the same seam.  Its two JavaScript files -- the
    # Web-environment shim and the boot/call glue -- are compiled INTO the
    # library: the shim is the security policy, and a policy that can be
    # edited beside the program is not one.
    if(BUILD_EXPR_PYODIDE_HOST)
        find_package(v8-embed CONFIG REQUIRED)
        find_package(Threads REQUIRED)
        set(FCX_PYODIDE_DIR ${CMAKE_SOURCE_DIR}/src/App/PyodideHost)
        foreach(_js host_shim:PyodideShim pyodide_glue:PyodideGlue)
            string(REPLACE ":" ";" _pair ${_js})
            list(GET _pair 0 _file)
            list(GET _pair 1 _ident)
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_ident}.inc
                COMMAND ${PYTHON_EXECUTABLE} ${FCX_PYODIDE_DIR}/embed_js.py
                        ${FCX_PYODIDE_DIR}/${_file}.js
                        ${CMAKE_CURRENT_BINARY_DIR}/${_ident}.inc k${_ident}
                DEPENDS ${FCX_PYODIDE_DIR}/${_file}.js ${FCX_PYODIDE_DIR}/embed_js.py
                COMMENT "Embedding ${_file}.js into the pyodide runtime")
            list(APPEND ExpressionImageHost_SRC ${CMAKE_CURRENT_BINARY_DIR}/${_ident}.inc)
            set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${_ident}.inc PROPERTIES
                GENERATED TRUE HEADER_FILE_ONLY TRUE)
        endforeach()
        list(APPEND ExpressionImageHost_SRC ExpressionPyodideRuntime.cpp
                                            ExpressionPyodide.cpp ExpressionPyodide.h)
        add_definitions(-DFC_EXPR_PYODIDE_HOST)
        list(APPEND FreeCADApp_LIBS v8-embed::v8 Threads::Threads)

        # The fcx_image wheel -- FreeCAD's own guest, matched to this
        # build -- ships under <datadir>/Pyodide/wheels/ (one per pyodide
        # ABI tag); the pyodide runtime itself is what the bootstrap
        # installs per user (docs/PyodideHost.md sec 12).  Mirrored into
        # the build tree like the rest.
        # The same for the pivy wheel (Coin and pivy.coin compiled for
        # the guest, docs/Sandbox.md 7.10; FREECAD_PIVY_WHEEL): a compiled
        # bundled wheel the runtime loads with an fcx_image of its ABI.
        # And the third-party pure wheels the forms need (ipywidgets
        # and traitlets, docs/Sandbox.md 7.3; FREECAD_BUNDLED_WHEELS, a
        # list): bundled the same way, one target each.
        set(_fcx_wheel_data ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Pyodide/wheels)
        set(_fcx_wheels "")
        foreach(_wheel_var FREECAD_FCX_IMAGE_WHEEL FREECAD_PIVY_WHEEL)
            if(${_wheel_var})
                list(APPEND _fcx_wheels "${${_wheel_var}}")
            endif()
        endforeach()
        foreach(_wheel ${FREECAD_BUNDLED_WHEELS})
            list(APPEND _fcx_wheels "${_wheel}")
        endforeach()
        foreach(_wheel ${_fcx_wheels})
            if(NOT EXISTS "${_wheel}")
                message(FATAL_ERROR "bundled wheel does not exist: ${_wheel}")
            endif()
            install(FILES ${_wheel} DESTINATION ${CMAKE_INSTALL_DATADIR}/Pyodide/wheels)
            get_filename_component(_fcx_wheel_name "${_wheel}" NAME)
            add_custom_command(
                OUTPUT ${_fcx_wheel_data}/${_fcx_wheel_name}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_wheel} ${_fcx_wheel_data}/${_fcx_wheel_name}
                DEPENDS ${_wheel}
                COMMENT "Mirroring ${_fcx_wheel_name} into the build tree")
            string(REGEX REPLACE "-.*" "" _wheel_target "${_fcx_wheel_name}")
            add_custom_target(${_wheel_target}_wheel ALL DEPENDS ${_fcx_wheel_data}/${_fcx_wheel_name})
        endforeach()

        # The guest side of the forms (docs/Sandbox.md 7.3): the `comm`
        # shim over the bridge and the IPython stand-in, packed as the
        # fcx_widgets wheel from src/App/ExpressionImage/widgets.
        set(_fcx_widgets_src ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionImage/widgets)
        file(GLOB_RECURSE _fcx_widgets_files ${_fcx_widgets_src}/*.py)
        set(_fcx_widgets_wheel ${_fcx_wheel_data}/fcx_widgets-${PACKAGE_VERSION}-py3-none-any.whl)
        add_custom_command(
            OUTPUT ${_fcx_widgets_wheel}
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/Tools/bindings/packSandboxWheel.py
                    --name fcx_widgets --version ${PACKAGE_VERSION}
                    --out-dir ${_fcx_wheel_data}
                    --root ${_fcx_widgets_src}
                    --add ${_fcx_widgets_src}=.
            DEPENDS ${CMAKE_SOURCE_DIR}/src/Tools/bindings/packSandboxWheel.py ${_fcx_widgets_files}
            COMMENT "Packing the guest comm shim as the fcx_widgets sandbox wheel")
        add_custom_target(WidgetsSandboxWheel ALL DEPENDS ${_fcx_widgets_wheel})
        install(FILES ${_fcx_widgets_wheel} DESTINATION ${CMAKE_INSTALL_DATADIR}/Pyodide/wheels)

        # The same models as one JSON table (docs/Sandbox.md 7.18 (c)):
        # the classes, their q_ traits with types and defaults, the
        # signals and the Qt-class map, for a backend that renders the
        # models outside the guest (the DOM tier of docs/ThinClient.md).
        # Read with `ast`, so no ipywidgets is needed on this Python.
        set(_fcx_models_json ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Pyodide/widget-models.json)
        add_custom_command(
            OUTPUT ${_fcx_models_json}
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/Tools/bindings/dumpWidgetModels.py
                    --root ${_fcx_widgets_src}/freecad/widgets
                    --out ${_fcx_models_json}
            DEPENDS ${CMAKE_SOURCE_DIR}/src/Tools/bindings/dumpWidgetModels.py
                    ${_fcx_widgets_src}/freecad/widgets/models.py
                    ${_fcx_widgets_src}/freecad/widgets/items.py
            COMMENT "Dumping the guest widget models as widget-models.json")
        add_custom_target(WidgetModelsJson ALL DEPENDS ${_fcx_models_json})
        install(FILES ${_fcx_models_json} DESTINATION ${CMAKE_INSTALL_DATADIR}/Pyodide)

        # A whole pyodide distribution (pyodide.js, pyodide.asm.*,
        # python_stdlib.zip, pyodide-lock.json, wheels) bundled as
        # <datadir>/Pyodide: what a dev tree does with the npm directory
        # (the wheel dropped in beside the runtime), and what a package
        # may choose to do instead of relying on the bootstrap.
        if(FREECAD_PYODIDE_DIR)
            if(NOT EXISTS "${FREECAD_PYODIDE_DIR}/pyodide.js")
                message(FATAL_ERROR "FREECAD_PYODIDE_DIR has no pyodide.js: ${FREECAD_PYODIDE_DIR}")
            endif()
            install(DIRECTORY ${FREECAD_PYODIDE_DIR}/
                    DESTINATION ${CMAKE_INSTALL_DATADIR}/Pyodide)
            set(_pyodide_data ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/Pyodide)
            # The mirror is what a dev-tree FreeCAD boots from, so it must
            # follow the guest wheel too, not only pyodide.js: a rebuilt
            # fcx_image wheel copied beside the runtime used to sit unseen
            # while the mirror kept serving the previous one.
            file(GLOB _pyodide_wheels ${FREECAD_PYODIDE_DIR}/fcx_image-*.whl)
            add_custom_command(
                OUTPUT ${_pyodide_data}/pyodide.js
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        ${FREECAD_PYODIDE_DIR} ${_pyodide_data}
                COMMAND ${CMAKE_COMMAND} -E touch ${_pyodide_data}/pyodide.js
                DEPENDS ${FREECAD_PYODIDE_DIR}/pyodide.js ${_pyodide_wheels}
                COMMENT "Mirroring the pyodide distribution into the build tree")
            add_custom_target(PyodideData ALL DEPENDS ${_pyodide_data}/pyodide.js)
        endif()
    endif()
    set(CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY OFF)
else()
    set(ExpressionImageHost_SRC)
endif()

# The binding generators need only python and the .xml (their DEPENDS are
# complete), but CMake still gives their custom commands order-only deps on
# the WHOLE dependency closure of the target they attach to (FreeCADApp).
# With the ExpressionCore object library in that closure, and ExpressionCore
# TUs including these generated headers, that implicit edge is a ninja
# dependency cycle. Scope it off for the generation commands (CMake >= 3.27;
# older CMake ignores the variable and would re-hit the cycle).
set(CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY ON)

generate_from_xml(DocumentPy)
generate_from_xml(DocumentObjectPy)
generate_from_xml(ExtensionPy)
generate_from_xml(ExtensionContainerPy)
generate_from_xml(DocumentObjectExtensionPy)
generate_from_xml(GroupExtensionPy)
generate_from_xml(LinkBaseExtensionPy)
generate_from_xml(DocumentObjectGroupPy)
generate_from_xml(GeoFeaturePy)
generate_from_xml(GeoFeatureGroupExtensionPy)
# Authored as a .pyi, the way upstream writes bindings now.
generate_from_py(SuppressibleExtension)
generate_from_py(DocumentSettings)
generate_from_xml(MetadataPy)
generate_from_xml(OriginGroupExtensionPy)
generate_from_xml(PartPy)
generate_from_xml(ExpressionPy)
generate_from_xml(StringHasherPy)
generate_from_xml(StringIDPy)

generate_from_xml(ComplexGeoDataPy)
generate_from_xml(PropertyContainerPy)
generate_from_xml(MaterialPy)
generate_from_xml(MaterialListPy)

generate_embed_from_py(FreeCADInit InitScript.h)
generate_embed_from_py(FreeCADTest TestScript.h)

set(CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY OFF)

SET(FreeCADApp_XML_SRCS
    ExtensionPy.xml
    ExtensionContainerPy.xml
    DocumentObjectExtensionPy.xml
    GroupExtensionPy.xml
    LinkBaseExtensionPy.xml
    MetadataPy.xml
    DocumentObjectGroupPy.xml
    DocumentObjectPy.xml
    GeoFeaturePy.xml
    GeoFeatureGroupExtensionPy.xml
    SuppressibleExtension.pyi
    DocumentSettings.pyi
    OriginGroupExtensionPy.xml
    PartPy.xml
    DocumentPy.xml
    PropertyContainerPy.xml
    ComplexGeoDataPy.xml
    MaterialPy.xml
    MaterialListPy.xml
    ExpressionPy.xml
    StringHasherPy.xml
    StringIDPy.xml
)
SOURCE_GROUP("XML" FILES ${FreeCADApp_XML_SRCS})

# The document stuff
SET(Document_CPP_SRCS
    Annotation.cpp
    StringHasher.cpp
    StringHasherPyImp.cpp
    StringIDPyImp.cpp
    Document.cpp
    DocumentParams.cpp
    FileBlobManager.cpp
    FileSet.cpp
    GroupParams.cpp
    DocumentObject.cpp
    Extension.cpp
    ExtensionPyImp.cpp
    DocumentObjectExtension.cpp
    DocumentObjectExtensionPyImp.cpp
    ExtensionContainer.cpp
    ExtensionContainerPyImp.cpp
    Graphviz.cpp
    GroupExtension.cpp
    GroupExtensionPyImp.cpp
    DocumentObjectFileIncluded.cpp
    DocumentObjectGroup.cpp
    DocumentObjectGroupPyImp.cpp
    GeoFeaturePyImp.cpp
    DocumentObjectPyImp.cpp
    DocumentObserver.cpp
    DocumentObserverPython.cpp
    DocumentPyImp.cpp
    ExpressionDocumentOps.cpp
    ExpressionEvaluator.cpp
    ExpressionSandboxPy.cpp
    ExpressionSecurity.cpp
    ExpressionSecurityPy.cpp
    ExpressionSecurityRuntime.cpp
    ExpressionTokenizer.cpp
    ${ExpressionImageHost_SRC}
    FeaturePython.cpp
    FeatureTest.cpp
    GeoFeature.cpp
    GeoFeatureGroupExtensionPyImp.cpp
    GeoFeatureGroupExtension.cpp
    InputStratum.cpp
    SuppressibleExtensionPyImp.cpp
    SuppressibleExtension.cpp
    DocumentSettingsPyImp.cpp
    DocumentSettings.cpp
    ImagePlane.cpp
    OriginGroupExtensionPyImp.cpp
    OriginGroupExtension.cpp
    PartPyImp.cpp
    Part.cpp
    Origin.cpp
    Path.cpp
    InventorObject.cpp
    MeasureDistance.cpp
    Placement.cpp
    ProjectFile.cpp
    OriginFeature.cpp
    Transactions.cpp
    TransactionalObject.cpp
    VRMLObject.cpp
    MaterialObject.cpp
    MergeDocuments.cpp
    ShaderObject.cpp
    TextDocument.cpp
    Link.cpp
    LinkParams.cpp
    LinkBaseExtensionPyImp.cpp
    ExpressionPyImp.cpp
    SavedView.cpp
    License.h
)

SET(Document_HPP_SRCS
    Annotation.h
    StringHasher.h
    Document.h
    DocumentParams.h
    FileBlobManager.h
    FileSet.h
    GroupParams.h
    DocumentObject.h
    Extension.h
    ExtensionContainer.h
    GroupExtension.h
    DocumentObjectExtension.h
    DocumentObjectFileIncluded.h
    DocumentObjectGroup.h
    DocumentObserver.h
    DocumentObserverPython.h
    Expression.h
    ExpressionEvaluator.h
    ExpressionGuestProxy.h
    ExpressionImageBridge.h
    ExpressionImageHost.h
    ExpressionSecurity.h
    ExpressionSecurityRuntime.h
    ExpressionParser.h
    ExpressionTokenizer.h
    ExpressionVisitors.h
    FeatureCustom.h
    FeaturePython.h
    FeaturePythonPyImp.h
    FeaturePythonPyImp.inl
    FeatureTest.h
    GeoFeature.h
    GeoFeatureGroupExtension.h
    InputStratum.h
    SuppressibleExtension.h
    DocumentSettings.h
    ImagePlane.h
    OriginGroupExtension.h
    Part.h
    Origin.h
    Path.h
    InventorObject.h
    MeasureDistance.h
    Placement.h
    ProjectFile.h
    OriginFeature.h
    Range.h
    Transactions.h
    TransactionDefs.h
    TransactionalObject.h
    VRMLObject.h
    MaterialObject.h
    MergeDocuments.h
    ShaderObject.h
    TextDocument.h
    Link.h
    SavedView.h
)
SET(Document_SRCS
    ${Document_CPP_SRCS}
    ${Document_HPP_SRCS}
)
SOURCE_GROUP("Document" FILES ${Document_SRCS})

# The property stuff
SET(Properties_CPP_SRCS
    DynamicProperty.cpp
    Property.cpp
    PropertyContainer.cpp
    PropertyContainerPyImp.cpp
    PropertyFile.cpp
    PropertyGeo.cpp
    PropertyLinks.cpp
    PropertyPythonObject.cpp
    PropertyStandard.cpp
    PropertyUnits.cpp
    PropertyExpressionEngine.cpp
)
SET(Properties_HPP_SRCS
    DynamicProperty.h
    ObjectIdentifier.h
    Property.h
    PropertyContainer.h
    PropertyFile.h
    PropertyGeo.h
    PropertyLinks.h
    PropertyPythonObject.h
    PropertyStandard.h
    PropertyUnits.h
    PropertyExpressionEngine.h
)
SET(Properties_SRCS
    ${Properties_CPP_SRCS}
    ${Properties_HPP_SRCS}
)
SOURCE_GROUP("Properties" FILES ${Properties_SRCS})

SET(FreeCADApp_CPP_SRCS
    ${Document_CPP_SRCS}
    ${Properties_CPP_SRCS}
    Application.cpp
    ApplicationPy.cpp
    AutoTransaction.cpp
    Branding.cpp
    CleanupProcess.cpp
    ColorModel.cpp
    MappedElement.cpp
    ComplexGeoData.cpp
    ComplexGeoDataPyImp.cpp
    ElementMap.cpp
    Enumeration.cpp
    IndexedName.cpp
    MappedElement.cpp
    MappedName.cpp
    MaterialAppearance.cpp
    MaterialXDocument.cpp
    AppearanceList.cpp
    MaterialPyImp.cpp
    MaterialListPyImp.cpp
    Metadata.cpp
    MetadataPyImp.cpp
    ElementNamingUtils.cpp
    StringHasher.cpp
    StringHasherPyImp.cpp
    StringIDPyImp.cpp
)

SET(FreeCADApp_HPP_SRCS
    ${Document_HPP_SRCS}
    ${Properties_HPP_SRCS}
    Application.h
    AutoTransaction.h
    Branding.h
    CleanupProcess.h
    Color.h
    ColorModel.h
    MappedElement.h
    ComplexGeoData.h
    ElementMap.h
    Enumeration.h
    IndexedName.h
    MappedName.h
    MaterialAppearance.h
    MaterialXDocument.h
    AppearanceList.h
    Metadata.h
    ElementNamingUtils.h
    StringHasher.h
)

SET(FreeCADApp_SRCS
    ${FreeCADApp_CPP_SRCS}
    ${FreeCADApp_HPP_SRCS}
    ${FreeCADApp_XML_SRCS}
    FreeCADInit.py
    FreeCADTest.py
    PreCompiled.cpp
    PreCompiled.h
)

if(FREECAD_USE_PCH)
    add_definitions(-D_PreComp_)
    ADD_MSVC_PRECOMPILED_HEADER(FreeCADApp PreCompiled.h PreCompiled.cpp FreeCADApp_CPP_SRCS)
endif(FREECAD_USE_PCH)

# The expression evaluation core, as its own object library so the sandbox
# carve-out (docs/ExpressionSandbox.md sec 8.1, docs/ExpressionSandboxPhase0.md
# sec 2) has a named target whose dependency discipline can ratchet: these TUs
# are meant to eventually compile without Qt, the property/link system or
# document persistence. Their document-maintenance and persistence-restore
# halves live in ExpressionDocumentOps.cpp, which stays in FreeCADApp proper.
# The generated headers are listed so their custom commands order before this
# target's compiles.
SET(ExpressionCore_CPP_SRCS
    Expression.cpp
    ObjectIdentifier.cpp
    Range.cpp
)
SOURCE_GROUP("ExpressionCore" FILES ${ExpressionCore_CPP_SRCS})
add_library(ExpressionCore OBJECT
    ${ExpressionCore_CPP_SRCS}
    ${CMAKE_CURRENT_BINARY_DIR}/ExpressionPy.h
    ${CMAKE_CURRENT_BINARY_DIR}/DocumentObjectPy.h
)
# The objects link into the FreeCADApp shared library. Linking the same
# libraries only propagates their usage requirements (include dirs, defines)
# to these compiles; the object library itself produces no link artifact.
# The core is meant to be Qt-free, so no moc machinery either.
set_target_properties(ExpressionCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(ExpressionCore ${FreeCADApp_LIBS})

add_library(FreeCADApp SHARED ${FreeCADApp_SRCS} $<TARGET_OBJECTS:ExpressionCore>)
target_link_libraries(FreeCADApp ${FreeCADApp_LIBS})

add_dependencies(FreeCADApp fc_version)

SET_BIN_DIR(FreeCADApp FreeCADApp)

if(WIN32)
    INSTALL(TARGETS FreeCADApp
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
else(WIN32)
    INSTALL(TARGETS FreeCADApp
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif(WIN32)
