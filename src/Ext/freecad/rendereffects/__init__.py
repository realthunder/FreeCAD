"""Render-effect factory (docs/RenderEngine.md §5.11).

Effects ship as package directories — a folder holding an
``effect.json`` manifest plus the bgfx ``.sc`` sources it references:

    share/Renderer/effects/<name>/      (bundled with FreeCAD)
    <user app data>/Renderer/effects/<name>/   (user-authored, wins)

Activating an effect COPIES it into the target document as
``App::ShaderProgram`` / ``App::Shader`` objects plus a binding
``App::Appearance`` — documents stay self-contained and portable, and
picking up a newer bundled version is an explicit re-activation, never
an implicit central upgrade.

Manifest schema (all program fields except ``fragment`` optional)::

    {
      "label": "Water surface",
      "description": "...",
      "viewProps": {"Render_WaterSurface": true},
      "programs": [
        {
          "name": "WaterSurface",
          "stage": "water",            # material / water / volume / post
          "fragment": "water_surface.sc",
          "vertex": "billboard_vs.sc",
          "blend": "Additive",         # Default / Alpha / Additive
          "depthwrite": false,
          "enabled": true,             # false = shipped switched off
          "params": {"Speed": 1.0}     # Param_* defaults (floats)
        }
      ]
    }

``viewProps`` are boolean view properties switched on in the active 3D
view at activation (e.g. the global water-surface toggle, which
defaults off) — without them a freshly activated effect can render
nothing.

Usage::

    from freecad import rendereffects
    rendereffects.list_effects()
    look = rendereffects.activate("water", targets=[doc.Pool])
    doc.water_WaterSurface.Enabled = False   # a program toggle
    rendereffects.deactivate(look)
"""

import json
import os

import FreeCAD


def _effect_dirs():
    """Effect search path, highest priority first (user dir wins)."""
    dirs = [
        os.path.join(FreeCAD.getUserAppDataDir(), "Renderer", "effects"),
        os.path.join(FreeCAD.getResourceDir(), "Renderer", "effects"),
    ]
    return [d for d in dirs if os.path.isdir(d)]


def _read_manifest(path):
    with open(os.path.join(path, "effect.json"), encoding="utf-8") as f:
        return json.load(f)


def list_effects():
    """All discoverable effects: [{name, label, description, path}]."""
    seen = {}
    for base in _effect_dirs():
        for name in sorted(os.listdir(base)):
            path = os.path.join(base, name)
            if name in seen or not os.path.isfile(
                    os.path.join(path, "effect.json")):
                continue
            try:
                m = _read_manifest(path)
            except Exception as e:
                FreeCAD.Console.PrintWarning(
                    "rendereffects: bad manifest %s: %s\n" % (path, e))
                continue
            seen[name] = {
                "name": name,
                "label": m.get("label", name),
                "description": m.get("description", ""),
                "path": path,
            }
    return list(seen.values())


def _find(name):
    for base in _effect_dirs():
        path = os.path.join(base, name)
        if os.path.isfile(os.path.join(path, "effect.json")):
            return path
    raise ValueError("rendereffects: no effect named %r (searched %s)"
                     % (name, ", ".join(_effect_dirs()) or "nothing"))


def activate(name, targets=None, doc=None, scope="Object"):
    """Instantiate effect `name` into `doc` and bind it to `targets`.

    Returns the created App::Appearance. `targets` empty/None with a
    post-stage effect gives the scene-level activation. The effect
    objects are copies — self-contained in the document.
    """
    path = _find(name)
    manifest = _read_manifest(path)
    targets = list(targets or [])
    if doc is None:
        doc = targets[0].Document if targets else FreeCAD.ActiveDocument
    if doc is None:
        raise ValueError("rendereffects: no document")

    progs = []
    for pm in manifest.get("programs", []):
        prog = doc.addObject("App::ShaderProgram",
                             "%s_%s" % (name, pm.get("name", "Program")))
        prog.Label = pm.get("name", prog.Name)
        prog.Stage = pm.get("stage", "material")
        with open(os.path.join(path, pm["fragment"]),
                  encoding="utf-8") as f:
            prog.FragmentProgram = f.read()
        if pm.get("vertex"):
            with open(os.path.join(path, pm["vertex"]),
                      encoding="utf-8") as f:
                prog.VertexProgram = f.read()
        if pm.get("blend"):
            prog.Blend = pm["blend"]
        if "depthwrite" in pm:
            prog.DepthWrite = bool(pm["depthwrite"])
        prog.Enabled = bool(pm.get("enabled", True))
        em = pm.get("emitter")
        if em:
            # particle companion: seed quads generated at each bound
            # target, fit to its bounding box
            prog.EmitterCount = int(em.get("count", 200))
            prog.EmitterSeed = int(em.get("seed", 1))
            if "spread" in em:
                prog.EmitterSpread = FreeCAD.Vector(*em["spread"])
            if "offset" in em:
                prog.EmitterOffset = FreeCAD.Vector(*em["offset"])
            if "margin" in em:
                prog.EmitterMargin = float(em["margin"])
        for pname, pval in (pm.get("params") or {}).items():
            prog.addProperty("App::PropertyFloat", "Param_" + pname)
            setattr(prog, "Param_" + pname, float(pval))
        progs.append(prog)

    shader = doc.addObject("App::Shader", name + "_Fx")
    shader.Label = manifest.get("label", name)
    shader.Programs = progs
    shader.Demo = "None"

    look = doc.addObject("App::Appearance", name + "_Look")
    look.Scope = scope
    look.ElementList = [shader] + targets
    doc.recompute()

    # Boolean view switches the effect depends on (e.g. the global
    # water-surface toggle, default off) — set on the active 3D view.
    props = manifest.get("viewProps") or {}
    if props:
        try:
            import FreeCADGui
            view = FreeCADGui.ActiveDocument.ActiveView if \
                FreeCADGui.ActiveDocument else None
        except Exception:
            view = None
        if view is not None:
            for k, v in props.items():
                try:
                    setattr(view, k, bool(v))
                except Exception as e:
                    FreeCAD.Console.PrintWarning(
                        "rendereffects: view prop %s: %s\n" % (k, e))
        else:
            FreeCAD.Console.PrintWarning(
                "rendereffects: no active 3D view, set %s manually\n"
                % ", ".join(props))
    return look


def deactivate(look):
    """Remove an activated effect: the Appearance, its Shader and the
    Shader's programs (targets are left alone)."""
    doc = look.Document
    shader = None
    for child in look.ElementList:
        if child.isDerivedFrom("App::Shader"):
            shader = child
            break
    doc.removeObject(look.Name)
    if shader is not None:
        progs = list(shader.Programs)
        doc.removeObject(shader.Name)
        for p in progs:
            if p.isDerivedFrom("App::ShaderProgram"):
                doc.removeObject(p.Name)
    doc.recompute()
