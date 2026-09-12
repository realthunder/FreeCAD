macro(SetupSwig)
# -------------------------------- Swig ----------------------------------

    find_package(SWIG)

    if (NOT SWIG_FOUND)
        message("=====================================================\n"
                "SWIG not found, will not build SWIG binding for pivy.\n"
                "=====================================================\n")
    endif(NOT SWIG_FOUND)

    # ! SWIG is not a free choice: it has to be the same GENERATION as the
    # one pivy was built with, and nothing downstream will tell you if it
    # is not.
    #
    # SWIG shares its type registry through a module in the live Python
    # interpreter named "swig_runtime_data<VERSION>" (see the generated
    # swigpyrun.h, PyImport_AddModule). pivy registers its Coin types into
    # the module ITS SWIG named; our C++ looks up types in the module OURS
    # names. Two generations never meet, and the failure is invisible
    # until run time: the build succeeds, pivy imports fine, every symbol
    # resolves, and only a POINTER CROSSING the boundary fails -- with
    # "No SWIG wrapped library loaded", which reads as "pivy is missing"
    # when in fact both are present and healthy.
    #
    # That took a while to find on 2026-09-08: a tree configured against
    # the conda env's SWIG 4.5.0 (runtime 5) with pivy built by 4.2.x
    # (runtime 4). It breaks every createSWIGPointerObj call site at once
    # -- View3DInventorPy::getCameraNode, LinkView, AxisOrigin,
    # SceneInspector, the offscreen renderer, ViewProviderFeaturePython --
    # so effectively all Coin scripting, while the build reports success.
    #
    # So decide it HERE, where SWIG is chosen, and derive both sides
    # rather than hardcoding either: ours from the runtime this SWIG
    # emits, pivy's by asking the interpreter that will import it.
    if(SWIG_FOUND AND Python3_EXECUTABLE)
        set(_fc_swig_probe "${CMAKE_BINARY_DIR}/CMakeFiles/swigpyrun_probe.h")
        execute_process(
            COMMAND ${SWIG_EXECUTABLE} -python -external-runtime ${_fc_swig_probe}
            RESULT_VARIABLE _fc_swig_probe_result
            ERROR_QUIET)
        if(_fc_swig_probe_result EQUAL 0 AND EXISTS "${_fc_swig_probe}")
            file(STRINGS "${_fc_swig_probe}" _fc_swig_rt_line
                 REGEX "^#define[ \t]+SWIG_RUNTIME_VERSION")
            string(REGEX MATCH "\"([0-9]+)\"" _fc_swig_rt_m "${_fc_swig_rt_line}")
            set(SWIG_RUNTIME_VERSION "${CMAKE_MATCH_1}")
        endif()

        # pivy names its own generation: importing it registers the module,
        # so the interpreter can simply be asked. Quiet when pivy is
        # absent -- that is a legitimate configuration (a headless build
        # marshals no Coin nodes), and PIVY_VERSION in SetupCoin3D already
        # reports whether it is there.
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c
"import sys\ntry:\n    import pivy.coin\nexcept Exception:\n    raise SystemExit(0)\nfor m in sys.modules:\n    if m.startswith('swig_runtime_data'):\n        print(m[len('swig_runtime_data'):], end='')\n        break\n"
            OUTPUT_VARIABLE PIVY_SWIG_RUNTIME
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()

    if(SWIG_RUNTIME_VERSION AND PIVY_SWIG_RUNTIME
            AND NOT SWIG_RUNTIME_VERSION STREQUAL PIVY_SWIG_RUNTIME)
        message(FATAL_ERROR
            "SWIG runtime mismatch.\n"
            "  ${SWIG_EXECUTABLE}\n"
            "    emits swig_runtime_data${SWIG_RUNTIME_VERSION}\n"
            "  the installed pivy publishes\n"
            "    swig_runtime_data${PIVY_SWIG_RUNTIME}\n"
            "These cannot exchange pointers. The build would SUCCEED and "
            "every Coin object passed from C++ to Python would then fail "
            "at run time with \"No SWIG wrapped library loaded\".\n"
            "Fix by pointing -DSWIG_EXECUTABLE at a SWIG whose runtime is "
            "${PIVY_SWIG_RUNTIME}, or by installing a pivy built against "
            "this one. conda/environment.devenv.yml pins the pair that "
            "belong together.")
    endif()

endmacro(SetupSwig)
