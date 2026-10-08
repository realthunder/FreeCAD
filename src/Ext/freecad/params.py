# SPDX-License-Identifier: LGPL-2.1-or-later
"""The way into the settings registry for a module written in Python.

A setting that C++ reads is described by a generated class: a definition
file, `FooParams.py`, lists the settings of a group with the classes of
`Tools/params_utils.py`, and the class generated from it registers them in
`App::ParamRegistry` when its library loads. The registry is where the omni
search ("/param"), the browser and the control interface find a setting,
its documentation and the editor to show for it.

A module written in Python has no generated class. It writes the same kind
of definition file and hands it to `register()`:

    # Mod/Foo/FooParams.py
    import sys
    from freecad.params import ParamBool, ParamInt, ParamSpinBox, register

    NameSpace = "Foo"
    ClassName = "FooParams"
    ParamPath = "User parameter:BaseApp/Preferences/Mod/Foo"

    Params = [
        ParamBool("ShowGrid", True, title="Show grid",
                  doc="Draws the grid of a new sheet. Takes effect at once."),
        ParamInt("GridSize", 10, title="Grid size", proxy=ParamSpinBox(1, 1000, 1),
                 doc="Distance between two grid lines, in mm. Takes effect at once."),
    ]

    register(sys.modules[__name__])

and imports that file from its `Init.py`, so that its settings are listed
whether or not the workbench has been used. Registering describes a
setting; it reads nothing and stores nothing, and the code that reads the
setting stays as it is. The default is a Python value here -- where a
generated class takes a C++ expression -- and has to be the one the readers
pass.

A module that keeps a table of its settings already calls
`FreeCAD.registerParam()` for each row instead of writing them out again.
"""

import sys
import types

import FreeCAD


def _definitions():
    """`Tools/params_utils.py`, installed beside this file.

    It writes C++ through `cog`, which only exists while the generator runs.
    Nothing here generates code, so an empty module stands in for it during
    the import and is taken away again.
    """
    stub = None
    if "cog" not in sys.modules:
        stub = sys.modules["cog"] = types.ModuleType("cog")
    try:
        from . import params_utils
    finally:
        if stub is not None:
            del sys.modules["cog"]
    return params_utils


_utils = _definitions()

ParamBool = _utils.ParamBool
ParamInt = _utils.ParamInt
ParamUInt = _utils.ParamUInt
ParamHex = _utils.ParamHex
ParamFloat = _utils.ParamFloat
ParamString = _utils.ParamString
ParamComboBox = _utils.ParamComboBox
ComboBoxItem = _utils.ComboBoxItem
ParamColor = _utils.ParamColor
ParamSpinBox = _utils.ParamSpinBox
ParamFile = _utils.ParamFile
ParamLinePattern = _utils.ParamLinePattern
ParamShortcutEdit = _utils.ParamShortcutEdit

# What register() could not describe because something else -- a generated
# class, or another definition file -- describes that path and entry
# already: (definition, path, entry). A test holds it empty.
conflicts = []

_owners = {}


def describe(param, namespace, class_name, param_path):
    """The keywords of `FreeCAD.registerParam()` for one entry of a
    definition file's `Params`: what the generator would write into the
    registrar of a C++ class."""
    spec = {
        "path": param.registry_path(param_path),
        "entry": param.param_name,
        "type": param.RegistryType,
        "default": param._default,
        "title": param.title,
        "doc": param._doc,
        "name": param.name,
        "namespace": namespace,
        "context": class_name,
        "onChange": bool(param.on_change),
    }
    proxy = param.proxy
    if proxy:
        spec["proxy"] = proxy.registry_name()
        if isinstance(proxy, ParamComboBox):
            spec["items"] = [
                (item.text, item.tooltips, item._data if isinstance(item._data, str) else None)
                for item in proxy.items
            ]
            spec["translateItems"] = bool(proxy.translate)
        elif isinstance(proxy, ParamColor):
            spec["transparency"] = bool(proxy.transparency)
        elif isinstance(proxy, ParamSpinBox):
            spec["minimum"] = float(proxy.value_min)
            spec["maximum"] = float(proxy.value_max)
            spec["step"] = float(proxy.value_step)
            spec["decimals"] = int(proxy.decimals)
    return spec


def register(module):
    """Describe the settings of a definition file to the registry.

    `module` has `NameSpace`, `ClassName`, `ParamPath` and `Params`, as a
    definition file of a generated class has. Returns the (path, entry) of
    those it did not register because they are described already; these are
    also noted in `conflicts`. Registering the same file again is not one.
    """
    add = getattr(FreeCAD, "registerParam", None)
    if add is None:
        # no registry on this side: the sandbox guest, whose host has it
        return []
    owner = "%s::%s" % (module.NameSpace, module.ClassName)
    refused = []
    for param in module.Params:
        spec = describe(param, module.NameSpace, module.ClassName, module.ParamPath)
        key = (spec["path"], spec["entry"])
        if add(**spec):
            _owners[key] = owner
        elif _owners.get(key) != owner:
            refused.append(key)
            conflicts.append((owner,) + key)
            FreeCAD.Console.PrintWarning(
                "%s: %s/%s is described already\n" % ((owner,) + key)
            )
    return refused
