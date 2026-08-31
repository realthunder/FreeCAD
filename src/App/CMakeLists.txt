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

# The wasmtime host embedding for the expression sandbox image
# (docs/ExpressionImage.md).  Host-only, never part of ExpressionCore.
if(BUILD_EXPR_IMAGE_HOST)
    if(NOT WASMTIME_CAPI_DIR)
        message(FATAL_ERROR "BUILD_EXPR_IMAGE_HOST needs WASMTIME_CAPI_DIR "
                            "(wasmtime C API prefix with include/ and lib/)")
    endif()
    set(ExpressionImageHost_SRC ExpressionImageHost.cpp ExpressionImageBridge.cpp)
    include_directories(${WASMTIME_CAPI_DIR}/include)
    # Lets ExpressionEvaluator.cpp route evaluation into the image; every
    # other TU ignores it, and no header depends on it (no ABI edge).
    add_definitions(-DFC_EXPR_IMAGE_HOST)
    list(APPEND FreeCADApp_LIBS ${WASMTIME_CAPI_DIR}/lib/libwasmtime.so)

    # The closed bridge member table, generated from the <Sandbox tier=.../>
    # annotations in the binding XMLs (docs/ExpressionSandbox.md sec 7.5).
    # The image build runs the same script for its facade classes, so both
    # sides of the boundary always agree.
    set(FCX_DISPATCH_INC ${CMAKE_CURRENT_BINARY_DIR}/FcxDispatch.inc)
    set(FCX_SANDBOX_GEN ${CMAKE_SOURCE_DIR}/src/Tools/bindings/generateSandboxFacades.py)
    # Same order-only-dep scoping as the binding generators below: the
    # command needs only python and its listed inputs.
    set(CMAKE_ADD_CUSTOM_COMMAND_DEPENDS_EXPLICIT_ONLY ON)
    add_custom_command(
        OUTPUT ${FCX_DISPATCH_INC}
        COMMAND ${PYTHON_EXECUTABLE} ${FCX_SANDBOX_GEN} --host-out ${FCX_DISPATCH_INC}
        DEPENDS ${FCX_SANDBOX_GEN}
                ${CMAKE_SOURCE_DIR}/src/App/ComplexGeoDataPy.xml
                ${CMAKE_SOURCE_DIR}/src/App/DocumentObjectPy.xml
                ${CMAKE_SOURCE_DIR}/src/App/DocumentPy.xml
                ${CMAKE_SOURCE_DIR}/src/Mod/Part/App/TopoShapePy.xml
                ${CMAKE_SOURCE_DIR}/src/Mod/Spreadsheet/App/SheetPy.xml
        COMMENT "Generating the expression sandbox dispatch table (FcxDispatch.inc)"
    )
    list(APPEND ExpressionImageHost_SRC ${FCX_DISPATCH_INC})
    set_source_files_properties(${FCX_DISPATCH_INC} PROPERTIES
        GENERATED TRUE HEADER_FILE_ONLY TRUE)
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
    Material.cpp
    MaterialList.cpp
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
    Material.h
    MaterialList.h
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
