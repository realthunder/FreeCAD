import os
import sys

pythonpath = os.environ.get("SNAP_PYTHONPATH")

if pythonpath:
  print(f"Adding snap-specific PYTHONPATH to sys.path: {pythonpath}")
  os.environ["PYTHONPATH"] = pythonpath
  for path in pythonpath.split(":"):
    sys.path.insert(0, path)
