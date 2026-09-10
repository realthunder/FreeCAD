"""Render-effect factory (docs/RenderEngine.md §5.11).

Effects ship as package directories — a folder holding an
``effect.json`` manifest plus the bgfx ``.sc`` sources it references:

    share/Renderer/effects/<name>/      (bundled with FreeCAD)
    <user app data>/Renderer/effects/<name>/   (user-authored, wins)

Activating an effect COPIES it into the target document as
``App::ShaderProgram`` / ``App::Shader`` objects plus a binding
``App::ShaderBinding`` -- documents stay self-contained and portable, and
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
          "simulate": "step.sc",       # particle state step (§5.8)
          "params": {"Speed": 1.0}     # Param_* defaults (floats)
        }
      ]
    }

A particle program with ``simulate`` is a *stateful* emitter: the named
fragment program advances its particles one fixed step at a time in
ping-pong state textures, and the vertex stage reads the result instead
of computing position from the clock. Its ``emitter`` block then also
accepts ``rate`` (fixed steps per second), ``warmup`` (seconds
simulated before a frozen frame draws) and ``timescale`` (the rate of
the emitter's clock against the wall clock, 1 = real time). The time
scale changes how briskly the motion plays and nothing about its
shape, so it is the knob for an effect that reads too slow or too
hurried at the size of the scene it was dropped into.

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

    # Standalone on the Shader's built-in demo geometry, no target:
    fx = rendereffects.instantiate("fire")
    fx.Demo = "Box"
    fx.DemoPlacement = FreeCAD.Placement(
        FreeCAD.Vector(8, 6, 4.5), FreeCAD.Rotation())
    rendereffects.deactivate(fx)
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


def _apply_view_props(manifest):
    # Boolean view switches the effect depends on (e.g. the global
    # water-surface toggle, default off) — set on the active 3D view.
    props = manifest.get("viewProps") or {}
    if not props:
        return
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


def instantiate(name, doc=None, view_props=True):
    """Copy effect `name` into `doc` WITHOUT a binding Appearance.

    Creates the App::ShaderProgram objects plus the grouping
    App::Shader and returns the Shader — for standalone use on its
    built-in demo geometry: set the Shader's Demo shape and
    DemoPlacement and the effect renders on it, enabled particle
    companions included, no target object needed.
    """
    path = _find(name)
    manifest = _read_manifest(path)
    if doc is None:
        doc = FreeCAD.ActiveDocument
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
        if pm.get("simulate"):
            # stateful emitter: the particle state step (§5.8)
            with open(os.path.join(path, pm["simulate"]),
                      encoding="utf-8") as f:
                prog.SimulateProgram = f.read()
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
            if "rate" in em:
                prog.EmitterRate = float(em["rate"])
            if "warmup" in em:
                prog.EmitterWarmup = float(em["warmup"])
            if "timescale" in em:
                prog.EmitterTimeScale = float(em["timescale"])
        for pname, pval in (pm.get("params") or {}).items():
            # A scalar feeds u_<Name>.x; a list feeds the vec4 lanes,
            # so a program can take a couple of related numbers without
            # a property each (padded to three, the vector width).
            if isinstance(pval, (list, tuple)):
                vals = [float(v) for v in pval][:3]
                vals += [0.0] * (3 - len(vals))
                prog.addProperty("App::PropertyVector", "Param_" + pname)
                setattr(prog, "Param_" + pname, FreeCAD.Vector(*vals))
            else:
                prog.addProperty("App::PropertyFloat", "Param_" + pname)
                setattr(prog, "Param_" + pname, float(pval))
        progs.append(prog)

    shader = doc.addObject("App::Shader", name + "_Fx")
    shader.Label = manifest.get("label", name)
    shader.Programs = progs
    shader.Demo = "None"
    if view_props:
        _apply_view_props(manifest)
    return shader


def activate(name, targets=None, doc=None, scope="Object"):
    """Instantiate effect `name` into `doc` and bind it to `targets`.

    Returns the created App::ShaderBinding. `targets` empty/None with a
    post-stage effect gives the scene-level activation. The effect
    objects are copies — self-contained in the document.
    """
    targets = list(targets or [])
    if doc is None:
        doc = targets[0].Document if targets else FreeCAD.ActiveDocument
    shader = instantiate(name, doc=doc)
    doc = shader.Document

    look = doc.addObject("App::ShaderBinding", name + "_Look")
    look.Scope = scope
    look.ElementList = [shader] + targets
    doc.recompute()
    return look


def deactivate(look):
    """Remove an activated effect -- accepts the App::ShaderBinding from
    activate() or the bare App::Shader from instantiate(); removes it
    with the Shader's programs (targets are left alone)."""
    doc = look.Document
    shader = None
    if look.isDerivedFrom("App::Shader"):
        shader = look
    else:
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
