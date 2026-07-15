macro(SetupEigen)
# -------------------------------- Eigen --------------------------------

    # necessary for Sketcher module
    # necessary for Robot module

    # Prefer Eigen's own CMake config (required for Eigen >= 5, whose version
    # macros moved and are no longer parsed by the bundled FindEigen3.cmake);
    # normalize to the legacy EIGEN3_* variables the modules consume.
    find_package(Eigen3 CONFIG QUIET)
    if(TARGET Eigen3::Eigen)
        set(EIGEN3_FOUND ON)
        set(EIGEN3_VERSION ${Eigen3_VERSION})
        get_target_property(EIGEN3_INCLUDE_DIR Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
    else()
        find_package(Eigen3)
    endif()
    if(NOT EIGEN3_FOUND)
        message("=================\n"
                "Eigen3 not found.\n"
                "=================\n")
    endif(NOT EIGEN3_FOUND)

    if (EIGEN3_FOUND AND ${EIGEN3_VERSION} VERSION_LESS "3.3.1")
        message(WARNING "Disable module flatmesh because it requires "
                        "minimum Eigen3 version 3.3.1 but version ${EIGEN3_VERSION} was found")
        set (BUILD_FLAT_MESH OFF)
    endif()

    # Older versions raise the warning -Wdeprecated-copy with clang10/gcc10
    if (EIGEN3_FOUND AND ${EIGEN3_VERSION} VERSION_LESS "3.3.8")
        unset(_flag_found CACHE)
        check_cxx_compiler_flag("-Wno-deprecated-copy" _flag_found)
        if (_flag_found)
            set (EIGEN3_NO_DEPRECATED_COPY "-Wno-deprecated-copy")
        endif ()
    endif()

endmacro(SetupEigen)
