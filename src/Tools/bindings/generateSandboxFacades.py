#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Generate the expression-sandbox facade/dispatch pair from the
<Sandbox tier="value|handle|call"/> annotations in the Py binding XMLs
(docs/ExpressionSandbox.md sec 7.5).

Two outputs, both derived from the SAME annotation set so the host and
the image cannot drift:

  --host-out FcxDispatch.inc   C table rows compiled into
                               src/App/ExpressionImageBridge.cpp: the
                               closed member set the host dispatcher
                               will serve.  An op naming any other
                               member is a protocol error, never a
                               getattr.
  --image-out FcxFacades.inc   A C string literal of Python source
                               defining the in-image proxy classes --
                               exactly the declared members, each
                               forwarding (handle, member, args) over
                               the _fcx bridge -- consumed by
                               src/App/ExpressionImage/ImageMarshal.cpp.

The annotated XML list below is the canonical one; a member without a
<Sandbox/> annotation does not exist across the boundary (DENY by
default).  The security review of the whole bridge is "diff the
annotations": git log -p on these XML files.
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET

# Canonical annotated binding definitions, relative to the repo root
# (this script lives in src/Tools/bindings).  Keep this list sorted and
# append-only; the CMake custom commands in src/App/CMakeLists.txt and
# src/App/ExpressionImage/CMakeLists.txt list the same files as deps.
ANNOTATED_XMLS = [
    # Fathers before children: the generator chains a facade to its
    # Father only when that Father is annotated too, and an XML with
    # no annotation of its own (Persistence, TrimmedCurve, ...) is
    # listed so the chain reaches through it.  Grown for G1 step 5
    # (2026-09-04) from the Draft App-side surface report
    # (scripts/sandbox_gui_lint.py --surface).
    "src/Base/BaseClassPy.xml",
    "src/Base/PersistencePy.xml",
    "src/App/ComplexGeoDataPy.xml",
    "src/App/ExtensionContainerPy.xml",
    "src/App/PropertyContainerPy.xml",
    "src/App/DocumentObjectPy.xml",
    "src/App/GeoFeaturePy.xml",
    "src/App/DocumentPy.xml",
    # Extensions (G1c): their methods are injected per INSTANCE on the
    # host, so a handle names its extensions' facades ("ext") and the
    # guest composes the proxy class; the host looks members up on the
    # container's extension types after the type's own MRO.
    "src/App/ExtensionPy.xml",
    "src/App/DocumentObjectExtensionPy.xml",
    "src/App/LinkBaseExtensionPy.xml",
    "src/Mod/Part/App/AttachExtensionPy.xml",
    "src/Mod/Part/App/TopoShapePy.xml",
    "src/Mod/Part/App/TopoShapeEdgePy.xml",
    "src/Mod/Part/App/TopoShapeWirePy.xml",
    "src/Mod/Part/App/TopoShapeFacePy.xml",
    "src/Mod/Part/App/TopoShapeVertexPy.xml",
    "src/Mod/Part/App/TopoShapeShellPy.xml",
    "src/Mod/Part/App/TopoShapeSolidPy.xml",
    "src/Mod/Part/App/TopoShapeCompoundPy.xml",
    "src/Mod/Part/App/TopoShapeCompSolidPy.xml",
    "src/Mod/Part/App/GeometryPy.xml",
    "src/Mod/Part/App/GeometryCurvePy.xml",
    "src/Mod/Part/App/GeometrySurfacePy.xml",
    "src/Mod/Part/App/BoundedCurvePy.xml",
    "src/Mod/Part/App/TrimmedCurvePy.xml",
    "src/Mod/Part/App/ConicPy.xml",
    "src/Mod/Part/App/ArcOfConicPy.xml",
    "src/Mod/Part/App/ArcOfCirclePy.xml",
    "src/Mod/Part/App/ArcOfEllipsePy.xml",
    "src/Mod/Part/App/ArcOfHyperbolaPy.xml",
    "src/Mod/Part/App/CirclePy.xml",
    "src/Mod/Part/App/EllipsePy.xml",
    "src/Mod/Part/App/HyperbolaPy.xml",
    "src/Mod/Part/App/LinePy.xml",
    "src/Mod/Part/App/LineSegmentPy.xml",
    "src/Mod/Part/App/BSplineCurvePy.xml",
    "src/Mod/Part/App/BezierCurvePy.xml",
    "src/Mod/Part/App/BSplineSurfacePy.xml",
    "src/Mod/Part/App/BezierSurfacePy.xml",
    "src/Mod/Part/App/PointPy.xml",
    "src/Mod/Part/App/PlanePy.xml",
    "src/Mod/Part/App/SpherePy.xml",
    "src/Mod/Part/App/ToroidPy.xml",
    "src/Mod/Part/App/ConePy.xml",
    "src/Mod/Part/App/CylinderPy.xml",
    "src/Mod/Part/App/SurfaceOfExtrusionPy.xml",
    "src/Mod/Part/App/SurfaceOfRevolutionPy.xml",
    "src/Mod/Part/App/GeometryExtensionPy.xml",
    "src/Mod/Part/App/GeometryBoolExtensionPy.xml",
    "src/Mod/Part/App/GeometryDoubleExtensionPy.xml",
    "src/Mod/Part/App/GeometryIntExtensionPy.xml",
    "src/Mod/Part/App/GeometryStringExtensionPy.xml",
    "src/Mod/Spreadsheet/App/SheetPy.xml",
]

# The key of a facade is the type's RUNTIME tp_name -- what the host's
# MRO walk (facadeKeyFor / facadeMemberLookup) compares -- which is
# PythonName, else Namespace.Twin, EXCEPT where module init renames the
# type afterwards.  Part does that for every TopoShape binding
# (src/Mod/Part/App/AppPart.cpp, "Part::TopoShapePy::Type.tp_name =
# ..."); without this table a live shape ('Part.Solid') matched no
# facade at all and fell through to read_prop.
RUNTIME_TYPE_NAMES = {
    "TopoShapePy": "Part.Shape",
    "TopoShapeVertexPy": "Part.Vertex",
    "TopoShapeEdgePy": "Part.Edge",
    "TopoShapeWirePy": "Part.Wire",
    "TopoShapeFacePy": "Part.Face",
    "TopoShapeShellPy": "Part.Shell",
    "TopoShapeSolidPy": "Part.Solid",
    "TopoShapeCompoundPy": "Part.Compound",
    "TopoShapeCompSolidPy": "Part.CompSolid",
}

# Module facades: host modules the guest sees as a closed list of names.
# Callables run on the host (FcxWire mod_call) and return their result
# by value or as a handle -- so Part.LineSegment(...) in the guest is a
# handle on a host GeomLineSegment, with the annotated facade above it;
# constants are read once (mod_get); exceptions are guest-local classes
# the bridge raises when the host reply names them.  The Part list is
# the 32 names Draft's App side uses (docs/Sandbox.md sec 7.6).  Same
# rule as the members: absent means DENY.  Each module names the
# catalog permission its ops are checked against (sec 2.2): a curated
# geometry constructor list is geom.call; the host's parameter store,
# read through Draft's own get_param, is prefs.read.
MODULE_FACADES = {
    "Part": {
        "callables": [
            "Arc", "ArcOfCircle", "ArcOfEllipse", "BSplineCurve", "BezierCurve",
            "Circle", "Compound", "Edge", "Ellipse", "Face", "Line", "LineSegment",
            "Plane", "Point", "Shape", "Vertex", "Wire",
            "__sortEdges__", "getShape", "getSortedClusters", "makeBox", "makeCircle",
            "makeCompound", "makeFace", "makeLine", "makePlane", "makePolygon", "makeShell",
            "makeSolid", "makeWireString", "makeWires", "sortEdges",
        ],
        "constants": ["OCC_VERSION"],
        "exceptions": ["OCCError"],
        "permission": "geom.call",
    },
    # TechDraw's projection entry points, the ones Draft's App side
    # calls (G1d, docs/Sandbox.md 7.6): Shape2DView.execute projects
    # through projectEx, Hatch through makeGeomHatch, the SVG/DXF
    # exporters through projectToSVG/projectToDXF.  Geometry in,
    # geometry out, on the host, under geom.call like Part.
    "TechDraw": {
        "callables": ["makeGeomHatch", "project", "projectEx", "projectToDXF", "projectToSVG"],
        "constants": [],
        "exceptions": [],
        "permission": "geom.call",
    },
    # Draft's preference reader (docs/Sandbox.md sec 7.6): the guest's
    # bundled fcx_draft wheel leaves draftutils/params.py out, and these
    # two answer from the host's parameter store, by value.  The module
    # facade goes into sys.modules before the wheel's draftutils package
    # is imported, so `from draftutils import params` finds it.  Its
    # permission is prefs.read -- read only, through Draft's own reader,
    # ALLOW for every principal (user ruling 2026-09-04), so a document
    # object's Wire.__init__ never prompts for its MakeFaceMode.
    "draftutils.params": {
        "callables": ["get_param", "get_param_arch", "get_param_view"],
        "constants": [],
        "exceptions": [],
        # local no-ops: Draft's Initialize() starts the preference
        # observer that refreshes the tray and grid on a change, and
        # the native function is `if App.GuiUp:` -- 0 in the guest
        # (docs/Sandbox.md 7.9, G2b; a change notification that
        # crosses is a known gap, sec 13)
        "stubs": ["_param_observer_start"],
        "permission": "prefs.read",
    },
    # Draft's preference WRITER, the same module: a task panel stores
    # what the user chose (task_orthoarray's LinearModeOn on toggle,
    # DraftGui's ContinueMode, 83 call sites in Draft's GUI side).  A
    # module facade carries one permission, so the writers are a facade
    # of their own merged into the same guest module: prefs.write, DENY
    # for a document (not promptable), ALLOW for the session and addons
    # (G3a, docs/Sandbox.md 7.11).
    "draftutils.params#write": {
        "module": "draftutils.params",
        "callables": ["set_param", "set_param_arch", "set_param_view"],
        "constants": [],
        "exceptions": [],
        "permission": "prefs.write",
    },
    # The guest's FreeCAD.ParamGet (a Python shim in the in-image
    # FreeCAD module, ImageDispatch.cpp) answers every Get* through
    # this host module (src/Ext/freecad/prefs.py), read only: BIM's
    # ArchSchedule reads the parameter store at import, ArchTessellation
    # and ArchComponent at execute.
    "freecad.prefs": {
        "callables": ["read", "names", "has_group"],
        "constants": [],
        "exceptions": [],
        "permission": "prefs.read",
    },
    # ... and its Set* / RemBool family through `write` and `remove`,
    # under prefs.write (a panel's ParamGet(...).SetBool from the guest).
    "freecad.prefs#write": {
        "module": "freecad.prefs",
        "callables": ["write", "remove"],
        "constants": [],
        "exceptions": [],
        "permission": "prefs.write",
    },
}

VALID_ATTRIBUTE_TIERS = ("value", "handle")
VALID_METHODE_TIERS = ("call",)


class Facade:
    def __init__(self, name, type_key, father_name):
        self.name = name            # XML Name, e.g. DocumentObjectPy
        self.type_key = type_key    # tp_name, e.g. App.DocumentObject
        self.father_name = father_name
        self.attributes = []        # (member, tier)
        self.methods = []           # (member,)


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, "..", "..", ".."))
    if not os.path.isfile(os.path.join(root, "src", "Tools", "bindings", "generate.py")):
        sys.exit("generateSandboxFacades: cannot locate the repo root from %s" % here)
    return root


def parse_export(path):
    tree = ET.parse(path)
    export = tree.getroot()
    if export.tag != "PythonExport":
        export = export.find("PythonExport")
    if export is None:
        sys.exit("%s: no PythonExport element" % path)
    name = export.get("Name")
    twin = export.get("Twin")
    namespace = export.get("Namespace")
    python_name = export.get("PythonName")
    type_key = python_name if python_name else "%s.%s" % (namespace, twin)
    type_key = RUNTIME_TYPE_NAMES.get(name, type_key)
    facade = Facade(name, type_key, export.get("Father"))
    for member in export:
        if member.tag not in ("Methode", "Attribute"):
            continue
        sandbox = member.find("Sandbox")
        if sandbox is None:
            continue
        tier = sandbox.get("tier")
        mname = member.get("Name")
        if member.tag == "Attribute":
            if tier not in VALID_ATTRIBUTE_TIERS:
                sys.exit("%s: attribute %s: bad Sandbox tier '%s' (want value|handle)"
                         % (path, mname, tier))
            facade.attributes.append((mname, tier))
        else:
            if tier not in VALID_METHODE_TIERS:
                sys.exit("%s: method %s: bad Sandbox tier '%s' (want call)"
                         % (path, mname, tier))
            facade.methods.append((mname,))
    return facade


def merge_same_key(facades):
    """One facade per runtime type name.

    Two bindings can share a tp_name (before RUNTIME_TYPE_NAMES existed
    the eight TopoShape sub-shape XMLs all read "Part.TopoShape"); the
    host cannot tell them apart by name and the guest picks its proxy
    class by that name, so facades sharing a key merge into the first
    (fathers-first, so the base binding): the union of the declared
    members, where a member the object lacks raises AttributeError from
    the host as it does natively.  The closed table still holds -- only
    declared names cross.  With the table filled in this is a safety
    net, not the normal path.
    """
    by_key = {}
    merged = []
    for f in facades:
        base = by_key.get(f.type_key)
        if base is None:
            by_key[f.type_key] = f
            merged.append(f)
            continue
        have = {m for m, _ in base.attributes} | {m for (m,) in base.methods}
        for member, tier in f.attributes:
            if member not in have:
                base.attributes.append((member, tier))
                have.add(member)
        for (member,) in f.methods:
            if member not in have:
                base.methods.append((member,))
                have.add(member)
    # a child whose Father merged away must chain to the survivor
    names = {f.name for f in facades}
    survivor_of = {f.name: by_key[f.type_key].name for f in facades}
    for f in merged:
        if f.father_name in names:
            f.father_name = survivor_of[f.father_name]
    return merged


def topo_sort(facades):
    """Fathers before children so the generated class statements work."""
    by_name = {f.name: f for f in facades}
    ordered, seen = [], set()

    def visit(f):
        if f.name in seen:
            return
        seen.add(f.name)
        father = by_name.get(f.father_name)
        if father is not None:
            visit(father)
        ordered.append(f)

    for f in facades:
        visit(f)
    return ordered


def emit_host(facades, out):
    lines = [
        "// Generated by src/Tools/bindings/generateSandboxFacades.py from the",
        "// <Sandbox tier=.../> annotations in the binding XMLs -- do not edit.",
        "// Included by ExpressionImageBridge.cpp; one row per declared member.",
    ]
    lines.append("static const FacadeMember FacadeTable[] = {")
    for f in facades:
        for mname, tier in f.attributes:
            lines.append('    {"%s", "%s", FacadeKind::Attribute, FacadeTier::%s},'
                         % (f.type_key, mname, tier.capitalize()))
        for (mname,) in f.methods:
            lines.append('    {"%s", "%s", FacadeKind::Method, FacadeTier::Call},'
                         % (f.type_key, mname))
    lines.append("};")
    lines.append("static const ModuleMember ModuleTable[] = {")
    for key, spec in MODULE_FACADES.items():
        # an entry may name its module (a second permission for the
        # same guest module: "draftutils.params#write")
        modname = spec.get("module", key)
        perm = spec["permission"]
        for n in spec["callables"]:
            lines.append('    {"%s", "%s", ModuleKind::Callable, "%s"},' % (modname, n, perm))
        for n in spec["constants"]:
            lines.append('    {"%s", "%s", ModuleKind::Constant, "%s"},' % (modname, n, perm))
        for n in spec["exceptions"]:
            lines.append('    {"%s", "%s", ModuleKind::Exception, "%s"},' % (modname, n, perm))
    lines.append("};")
    with open(out, "w") as fp:
        fp.write("\n".join(lines) + "\n")


def emit_image(facades, out):
    py = [
        "# Generated facades: exactly the declared members, nothing else.",
        "# HostHandle, _attr and _method come from the hand-written prelude",
        "# in ImageMarshal.cpp; undeclared attribute access falls through to",
        "# HostHandle.__getattr__ = the host's C++ property system (read_prop).",
    ]
    proxy_name = {f.name: f.name.replace("Py", "") + "Proxy" for f in facades}
    for f in facades:
        base = proxy_name.get(f.father_name, "HostHandle")
        py.append("")
        py.append("class %s(%s):" % (proxy_name[f.name], base))
        py.append("    __slots__ = ()")
        for mname, _tier in f.attributes:
            py.append("    %s = _attr('%s')" % (mname, mname))
        for (mname,) in f.methods:
            py.append("    %s = _method('%s')" % (mname, mname))
    py.append("")
    py.append("FACADES = {")
    for f in facades:
        py.append("    '%s': %s," % (f.type_key, proxy_name[f.name]))
    py.append("}")
    py.append("")
    py.append("# Module facades: the prelude's _install_modules builds a module per")
    py.append("# entry -- callables forward over mod_call, constants read once over")
    py.append("# mod_get, exceptions are local classes the bridge raises by name,")
    py.append("# stubs are local no-ops (a native function that is a no-op without")
    py.append("# a GUI, which the guest has none of).")
    py.append("MODULES = {")
    merged = {}
    for key, spec in MODULE_FACADES.items():
        modname = spec.get("module", key)
        entry = merged.setdefault(modname, {"callables": [], "constants": [], "exceptions": [],
                                            "stubs": []})
        for k in ("callables", "constants", "exceptions", "stubs"):
            entry[k].extend(spec.get(k, []))
    for modname, spec in merged.items():
        py.append("    '%s': {" % modname)
        for key in ("callables", "constants", "exceptions", "stubs"):
            py.append("        '%s': (%s)," % (key, "".join("'%s', " % n for n in spec[key])))
        py.append("    },")
    py.append("}")

    lines = [
        "// Generated by src/Tools/bindings/generateSandboxFacades.py from the",
        "// <Sandbox tier=.../> annotations in the binding XMLs -- do not edit.",
        "// Included by ImageMarshal.cpp and exec'd over the proxy prelude.",
        'static const char FcxFacadesSource[] = R"FCXPY(',
    ]
    lines.extend(py)
    lines.append(')FCXPY";')
    with open(out, "w") as fp:
        fp.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host-out", help="write the host dispatch table (.inc)")
    ap.add_argument("--image-out", help="write the embedded image facades (.inc)")
    ap.add_argument("--list-xmls", action="store_true",
                    help="print ANNOTATED_XMLS, one repo-relative path per line, and exit"
                         " (the CMake custom commands take their DEPENDS from this)")
    args = ap.parse_args()
    if args.list_xmls:
        print("\n".join(ANNOTATED_XMLS))
        return
    if not args.host_out and not args.image_out:
        ap.error("nothing to do: give --host-out and/or --image-out")

    root = repo_root()
    facades = [parse_export(os.path.join(root, rel)) for rel in ANNOTATED_XMLS]
    facades = topo_sort(merge_same_key(facades))
    total = sum(len(f.attributes) + len(f.methods) for f in facades)
    if total == 0:
        sys.exit("generateSandboxFacades: no <Sandbox/> annotations found")

    if args.host_out:
        emit_host(facades, args.host_out)
    if args.image_out:
        emit_image(facades, args.image_out)
    print("generateSandboxFacades: %d members across %d facades" % (total, len(facades)))


if __name__ == "__main__":
    main()
