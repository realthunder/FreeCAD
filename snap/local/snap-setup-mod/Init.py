def add_snap_pythonpath():
  import os
  import sys

  pythonpath = os.environ.get("SNAP_PYTHONPATH")
  if pythonpath:
    print(f"Adding snap-specific PYTHONPATH to sys.path: {pythonpath}")
    os.environ["PYTHONPATH"] = pythonpath
    for path in pythonpath.split(":"):
      sys.path.insert(0, path)

def configure_mod_raytracing():
  import FreeCAD
  param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Raytracing")
  if not param.GetString("PovrayExecutable", ""):
    param.SetString("PovrayExecutable", "/snap/freecad/current/usr/bin/povray")

add_snap_pythonpath()
configure_mod_raytracing()
