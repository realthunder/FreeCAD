
# The boost_fix header overlay patches boost::geometry internals of older
# boost releases; its copies are incompatible with boost >= 1.85
if(Boost_VERSION_STRING VERSION_LESS 1.85)
    include_directories(before system ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/boost_fix)
endif()

add_subdirectory(Build)
add_subdirectory(3rdParty)
add_subdirectory(Base)
add_subdirectory(App)
add_subdirectory(Main)
add_subdirectory(Mod)
add_subdirectory(Ext)
add_subdirectory(Doc)

if(BUILD_GUI)
    add_subdirectory(Gui)
    if(UNIX AND NOT APPLE)
        add_subdirectory(XDGData)
    endif()
endif(BUILD_GUI)

if(BUILD_TEMPLATE)
    add_subdirectory(Tools/_TEMPLATE_)
endif(BUILD_TEMPLATE)

# "if" clause moved into local CMakeLists.txt file to support Conda and Homebrew builds
add_subdirectory(MacAppBundle)

