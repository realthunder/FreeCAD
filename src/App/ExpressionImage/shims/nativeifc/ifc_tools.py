# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guest shim of nativeifc.ifc_tools: the project/file lookups only,
mirroring the native functions for an object with no IFC project
(None), and refusing where native code would open ifcopenshell."""

from . import unavailable

PROJECT_TYPES = ("IfcProject", "IfcProjectLibrary")


def get_project(obj):
    """The IFC project object of obj, or None -- as nativeifc.ifc_tools
    .get_project finds it for a document object (the ifcopenshell
    instance branches of the native function cannot arise here)."""

    if hasattr(obj, "IfcFilePath"):
        return obj
    if hasattr(getattr(obj, "Document", None), "IfcFilePath"):
        return obj.Document
    if getattr(obj, "Class", None) in PROJECT_TYPES:
        return obj
    if hasattr(obj, "InListRecursive"):
        for parent in obj.InListRecursive:
            if getattr(parent, "Class", None) in PROJECT_TYPES:
                return parent
    return None


def get_ifcfile(obj):
    """None when obj has no IFC project (native: the same); an IFC
    project means an ifcopenshell file the guest cannot hold."""

    project = get_project(obj)
    if project is None:
        return None
    unavailable("nativeifc.ifc_tools.get_ifcfile of an IFC-backed object")


def get_ifc_element(obj, ifcfile=None):
    if not ifcfile:
        ifcfile = get_ifcfile(obj)
    if ifcfile and hasattr(obj, "StepId"):
        unavailable("nativeifc.ifc_tools.get_ifc_element")
    return None


def __getattr__(name):
    unavailable("nativeifc.ifc_tools." + name)
