if(MSVC)
    add_compile_options(/wd4251)

    option(
        gtest_force_shared_crt
        "Use shared (DLL) run-time lib even when Google Test is built as static lib."
        ON)
    option(gtest_disable_pthreads "Disable uses of pthreads in gtest." ON)

    set(Google_Tests_LIBS
        oldnames.lib
        debug msvcrtd.lib
        debug msvcprtd.lib
        optimized msvcrt.lib
        optimized msvcprt.lib
    )

    # Universal C runtime introduced in VS 2015 (cl version 19)
    if(NOT(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19"))
        list(APPEND Google_Tests_LIBS
            debug vcruntimed.lib
            debug ucrtd.lib
            debug concrtd.lib
            optimized vcruntime.lib
            optimized ucrt.lib
            optimized concrt.lib
        )
    endif()
endif()

if(WIN32)
    add_definitions(-DCOIN_DLL)
endif(WIN32)

if(NOT BUILD_DYNAMIC_LINK_PYTHON)
    list(APPEND Google_Tests_LIBS
        ${PYTHON_LIBRARIES}
    )
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(CMAKE_AUTOMOC ON)

function(setup_qt_test)
    foreach(_testname ${ARGN})
        add_executable(${_testname}_Tests_run ${_testname}.cpp)
        add_test(NAME ${_testname}_Tests_run COMMAND ${_testname}_Tests_run)

        if(NOT BUILD_DYNAMIC_LINK_PYTHON)
            list(APPEND ${_testname}_LIBS
                ${PYTHON_LIBRARIES}
            )
        endif()

        target_include_directories(${_testname}_Tests_run PUBLIC
            ${Python3_INCLUDE_DIRS}
            ${XercesC_INCLUDE_DIRS}
            ${QtGui_INCLUDE_DIRS}
            ${QtWidgets_INCLUDE_DIRS}
            ${QtTest_INCLUDE_DIRS}
            ${COIN3D_INCLUDE_DIRS})
        target_link_libraries(${_testname}_Tests_run
            FreeCADApp
            FreeCADGui
            ${QtCore_LIBRARIES}
            ${QtWidgets_LIBRARIES}
            ${QtTest_LIBRARIES}
            ${${_testname}_LIBS})
    endforeach()
endfunction()

# Add test executables here

set(TestExecutables
    Tests_run
    Import_tests_run
    Material_tests_run
    Mesh_tests_run
    Part_tests_run
    Points_tests_run
    Sketcher_tests_run
)

# -------------------------

foreach (exe ${TestExecutables})
    add_executable(${exe})
endforeach()

add_subdirectory(lib)
add_subdirectory(src)

target_include_directories(Tests_run PUBLIC
    ${Python3_INCLUDE_DIRS}
    ${XercesC_INCLUDE_DIRS}
)
target_link_libraries(Tests_run
    gtest_main
    gmock_main
    ${Google_Tests_LIBS}
    FreeCADApp
)

set(FC_TESTS_CARRY_DLL_PATH OFF)
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    # 3.29 for TEST_LAUNCHER; 3.28 for set_property(TEST ... DIRECTORY) and
    # 3.27 for TARGET_RUNTIME_DLL_DIRS. The project itself asks for 3.16, so an
    # older cmake keeps what it had: the tests build, and running them needs
    # the DLL directories put on PATH by hand.
    set(FC_TESTS_CARRY_DLL_PATH ON)
endif()

if(FC_TESTS_CARRY_DLL_PATH)
    # Windows has no rpath. A test exe finds FreeCADApp.dll in the build's bin,
    # and Part.pyd under Mod/Part, only through PATH -- and without them it
    # dies at load with 0xc0000135, which ctest reports as an abnormal exit
    # with no output, so it reads as the test crashing rather than as the
    # loader never getting there. PRE_TEST discovery hits it first, and one
    # failed discovery aborts the whole run: `ctest` could not run a single
    # case on Windows before this.
    #
    # TARGET_RUNTIME_DLL_DIRS is every directory the target's own dependencies
    # live in, so each test gets exactly what it links and there is no list to
    # keep up to date here. It reaches the tests two different ways, because
    # the two kinds of test here consume it differently:
    #
    # - The suites registered with add_test() take it as the test property
    #   ENVIRONMENT_MODIFICATION, one `PATH=path_list_prepend:` entry per
    #   directory. The separator is $<SEMICOLON> rather than an escaped `\;`
    #   because the property is written into CTestTestfile.cmake as a bracket
    #   argument, where a backslash is literal -- and ctest then reads the
    #   whole thing as one modification, prepending `PATH=path_list_prepend:`
    #   to PATH as if it were a directory.
    #
    # - The six that gtest_discover_tests() expands need the PATH before that,
    #   for the discovery run itself, and only TEST_LAUNCHER (CMake 3.29)
    #   prefixes that command. It is passed one `--modify` per directory for
    #   the same reason: GoogleTest carries the launcher as a CMake list, and
    #   a semicolon inside one argument would come back as the next argument.
    #   The same launcher then prefixes every case it discovers.
    #
    # Do not merge the two: a launcher on an add_test() target arrives as ONE
    # argument, semicolons and all -- which prepends `--modify` and the rest to
    # PATH as if they were directories, and leaves the real ones out.
    set(_fc_test_dll_path
        "PATH=path_list_prepend:$<JOIN:$<TARGET_RUNTIME_DLL_DIRS:@target@>,$<SEMICOLON>PATH=path_list_prepend:>")

    function(fc_add_test_dll_path dir)
        get_property(_tests DIRECTORY ${dir} PROPERTY TESTS)
        foreach(_test IN LISTS _tests)
            # The test names here are their target's, and a discovered suite's
            # placeholder (<target>_NOT_BUILT) names no target.
            if(TARGET ${_test})
                string(REPLACE "@target@" "${_test}" _mods "${_fc_test_dll_path}")
                set_property(TEST ${_test} DIRECTORY ${dir}
                             APPEND PROPERTY ENVIRONMENT_MODIFICATION "${_mods}")
            endif()
        endforeach()
        get_property(_subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
        foreach(_subdir IN LISTS _subdirs)
            fc_add_test_dll_path(${_subdir})
        endforeach()
    endfunction()

    # The discovered suites, before gtest_discover_tests() below: the module
    # reads TEST_LAUNCHER when it is called, not when the build is generated,
    # so a property set afterwards would reach the tests and not the discovery.
    foreach(exe ${TestExecutables})
        set_property(TARGET ${exe} PROPERTY TEST_LAUNCHER
            ${CMAKE_COMMAND} -E env --modify
            "PATH=path_list_prepend:$<JOIN:$<TARGET_RUNTIME_DLL_DIRS:${exe}>,\;--modify\;PATH=path_list_prepend:>"
            --)
    endforeach()
endif()

include(GoogleTest)
# discovers tests by asking the compiled test executable to enumerate its tests
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

foreach (exe ${TestExecutables})
    gtest_discover_tests(${exe})
endforeach()

if(FC_TESTS_CARRY_DLL_PATH)
    # Last, so that every add_test() in the tree above has run.
    fc_add_test_dll_path(${CMAKE_CURRENT_SOURCE_DIR})
endif()
