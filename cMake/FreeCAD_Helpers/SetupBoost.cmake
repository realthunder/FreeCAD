macro(SetupBoost)
# -------------------------------- Boost --------------------------------

    set(_boost_TEST_VERSIONS ${Boost_ADDITIONAL_VERSIONS})

    # Boost.System is header-only and 1.90 no longer exports a component for it;
    # nothing in src/ includes boost/system either. Upstream dropped it (and
    # boost::filesystem, which this fork still uses) for the same reason.
    set (BOOST_COMPONENTS filesystem program_options regex thread date_time)
    find_package(Boost ${BOOST_MIN_VERSION}
        COMPONENTS ${BOOST_COMPONENTS} REQUIRED)

    if(UNIX AND NOT APPLE)
        # Boost.Thread 1.67+ headers reference pthread_condattr_*
        list(APPEND Boost_LIBRARIES pthread)
    endif()

    if(NOT Boost_FOUND)
        set (NO_BOOST_COMPONENTS)
        foreach (comp ${BOOST_COMPONENTS})
            string(TOUPPER ${comp} uppercomp)
            if (NOT Boost_${uppercomp}_FOUND)
                list(APPEND NO_BOOST_COMPONENTS ${comp})
            endif()
        endforeach()
        message(FATAL_ERROR "=============================================\n"
                            "Required components:\n"
                            " ${BOOST_COMPONENTS}\n"
                            "Not found, install the components:\n"
                            " ${NO_BOOST_COMPONENTS}\n"
                            "=============================================\n")
    endif(NOT Boost_FOUND)
    
    if (WIN32)
        if (Boost_VERSION_STRING VERSION_GREATER_EQUAL "1.78.0")
            message("-- NOTE: Boost version ${Boost_VERSION} >= 1.78 and building on Windows: (1) disabling Win32 Regex Localization (2) enabling generic cmath.")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_REGEX_NO_W32 -DBOOST_CORE_USE_GENERIC_CMATH")
        endif()
    endif(WIN32)
    
    

endmacro(SetupBoost)
