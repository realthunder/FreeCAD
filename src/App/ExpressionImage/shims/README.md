# Guest shims

Pure-Python stand-ins packed into a bundled sandbox wheel
(`src/Tools/bindings/packSandboxWheel.py --add shims=.`) for host modules a
workbench's App side imports but never needs in the guest: the three names
Draft reaches PySide through (`QT_TRANSLATE_NOOP`, `QCoreApplication.translate`,
`QTimer.singleShot`), the compiled Qt resource modules (`Draft_rc`, `Arch_rc`),
and the `freecad` namespace package that carries `freecad.deprecation`.

The rule (docs/Sandbox.md sec 7): the guest sees FreeCAD's API, never Qt's.
These files exist so that UNMODIFIED App-side sources import; anything a GUI
would do is absent and raises `AttributeError` where it is reached.  G2's
subset shim (U7) grows from here.
