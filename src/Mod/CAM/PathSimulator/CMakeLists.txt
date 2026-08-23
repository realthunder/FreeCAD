# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(App)

# AppGL is ~7,800 lines of hand-written desktop OpenGL with its own viewer.
# That runs against this fork's renderer direction, so it is off unless asked
# for, and gets revisited as renderer work rather than as part of CAM.
if(BUILD_CAM_SIMULATOR_GL)
    add_subdirectory(AppGL)
endif(BUILD_CAM_SIMULATOR_GL)
#if(BUILD_GUI)
#     add_subdirectory(Gui)
#endif(BUILD_GUI)

# install(
    # FILES
        # Gui/PathSimulator.py
    # DESTINATION
        # Mod/CAM
# )
