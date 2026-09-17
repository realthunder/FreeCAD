
SET(Test_SRCS
    __init__.py
    Init.py
    BaseTests.py
    Document.py
    FileBlobs.py
    InputProperty.py
    ShapeStorage.py
    Metadata.py
    StringHasher.py
    Menu.py
    TestApp.py
    TestGui.py
    UnicodeTests.py
    UnitTests.py
    Workbench.py
    unittestgui.py
    testmakeWireString.py
    TestPythonSyntax.py
    SandboxPyodide.py
    SandboxGui.py
    SandboxWidgets.py
    SandboxForms.py
    SandboxNative.py
    SandboxPanels.py
    SandboxSelection.py
    SandboxSessionDoc.py
    SandboxHostFiles.py
    SandboxProxyImport.py
    SandboxModelDump.py
    SandboxToolBarMirror.py
    SandboxPanelMirror.py
    SandboxMirrorBench.py
    ViewProviderHooks.py
    ViewProviderChain.py
    FeaturePythonChain.py
    SandboxProgram.py
    SandboxProgramFixtures.py
)

SET(TestData_SRCS
    TestData/basic_metadata.xml
    TestData/bad_root_node.xml
    TestData/bad_xml.xml
    TestData/bad_version.xml
    TestData/content_items.xml
)

# written by SandboxProgramFixtures.writeFixtures(); installed as a directory of
# their own, since the consumer links the source by a path relative to itself
SET(SandboxProgramData_SRCS
    TestData/SandboxProgram/ProgramBracket.FCStd
    TestData/SandboxProgram/ProgramStair.FCStd
    TestData/SandboxProgram/ProgramFlange.FCStd
    TestData/SandboxProgram/ProgramBracketsSource.FCStd
    TestData/SandboxProgram/ProgramBracketsConsumer.FCStd
    TestData/SandboxProgram/ProgramFlangeSheet.FCStd
)

SOURCE_GROUP("" FILES ${Test_SRCS} ${TestData_SRCS} ${SandboxProgramData_SRCS})

if(BUILD_GUI)
    add_subdirectory(Gui)
    list (APPEND Test_SRCS InitGui.py)
endif(BUILD_GUI)

ADD_CUSTOM_TARGET(Test ALL
    SOURCES ${Test_SRCS} ${TestData_SRCS} ${SandboxProgramData_SRCS}
)

fc_copy_sources(Test "${CMAKE_BINARY_DIR}/Mod/Test" ${Test_SRCS} ${TestData_SRCS}
    ${SandboxProgramData_SRCS})

INSTALL(
    FILES
        ${Test_SRCS}
    DESTINATION
        Mod/Test
)

INSTALL(
    FILES
        ${TestData_SRCS}
    DESTINATION
        Mod/Test/TestData
)

INSTALL(
    FILES
        ${SandboxProgramData_SRCS}
    DESTINATION
        Mod/Test/TestData/SandboxProgram
)
