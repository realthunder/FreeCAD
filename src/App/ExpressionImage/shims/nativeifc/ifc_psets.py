# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guest shim of nativeifc.ifc_psets: edit_pset() for an object with no
IFC project returns without doing anything, exactly as the native
function does (it looks the file up and returns when there is none);
an IFC-backed object refuses instead of being edited or skipped."""

from . import ifc_tools, unavailable


def edit_pset(obj, prop, value=None, force=False, ifcfile=None, element=None):
    if value is None:
        value = getattr(obj, prop)
    if not ifcfile:
        ifcfile = ifc_tools.get_ifcfile(obj)
        if not ifcfile:
            return
    unavailable("nativeifc.ifc_psets.edit_pset of an IFC-backed object")


def __getattr__(name):
    unavailable("nativeifc.ifc_psets." + name)
