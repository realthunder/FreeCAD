"""What every GUI test starts from, said before the test's own script runs.

scripts/gui-test.sh hands this to FreeCAD ahead of the test. A test runs on
a profile of its own, so it sees the defaults -- and a default is not a
thing a hundred tests should lean on without knowing: when one changes,
they all move.

  - View/AntiAliasing = 0. The default is MSAA 4x since 2026-10-09
    (docs/HandsOnQueue.md entry 51). The tests that read pixels were written
    against pictures without multisampling, and a multisampled view has no
    depth to read back: getRenderStats() answers geometryPixels -1 for it.
    A test about multisampling sets the value itself, which is after this.

Only what is not stored yet is set: a test profile kept from an earlier run
keeps what that run left in it, as before.
"""
import FreeCAD

_view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
if "AntiAliasing" not in _view.GetInts():
    _view.SetInt("AntiAliasing", 0)
