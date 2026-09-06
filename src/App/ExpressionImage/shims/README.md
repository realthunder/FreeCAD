# Guest shims

Pure-Python stand-ins packed into a bundled sandbox wheel
(`src/Tools/bindings/packSandboxWheel.py --add shims=.`) for host modules a
workbench's App side imports but never needs in the guest: the three names
Draft reaches PySide through (`QT_TRANSLATE_NOOP`, `QCoreApplication.translate`,
`QTimer.singleShot`), the compiled Qt resource modules (`Draft_rc`, `Arch_rc`),
and the `freecad` namespace package that carries `freecad.deprecation`.
`nativeifc/` (fcx_bim, 2026-09-05) answers the one recompute-time reach into
BIM's IFC layer -- ArchSchedule's `save_ifc_props` -> `ifc_psets.edit_pset`,
which natively looks the object's IFC file up and returns when there is
none -- the same way for a plain document, and raises `IfcUnavailableError`
for an IFC-backed object rather than editing or skipping it silently.

G2b (docs/Sandbox.md 7.9, 2026-09-06) added what the workbenches' InitGui.py
reach when they run in the guest: `PartGui` (named at import by
`bimcommands`, nothing read), `nativeifc/ifc_commands.py` (`get_commands()`
answers an empty IFC tool bar) and `nativeifc/ifc_observer.py` (the IFC
document observer as no-ops: nothing to keep in step without ifcopenshell).
With `FreeCAD.GuiUp` the host's in the guest (ruling 2026-09-06), the `PySide`
shims also carry `QDesktopServices` (`openUrl` raises), `QFileSystemModel` (a
base class BIM's library browser derives from; constructing raises) and
`QtCore.Slot` (the identity decorator factory).

The rule (docs/Sandbox.md sec 7): the guest sees FreeCAD's API, never Qt's.
These files exist so that UNMODIFIED App-side sources import; anything a GUI
would do is absent and raises `AttributeError` where it is reached.  G2's
subset shim (U7) grows from here.
