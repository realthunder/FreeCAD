# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(App)

# AppGL is ~7,800 lines of hand-written desktop OpenGL with its own viewer.
# That runs against this fork's renderer direction, so it is off unless asked
# for. The revisit is underway: docs/CAMSimRenderPort.md ports its rendering
# onto the FreeCADRenderer draw facade (Gui/Renderer/DrawDevice.h), stage by
# stage; turn this on to work on that port.
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
