"""Fountain showcase (docs/RenderEngine.md §5.8/§5.11).

The stateful water jet in the setting it is meant for: a basin of real
water (Render_Water, so the pool refracts and reflects), a stone rim
and pedestal for the jets to rise from, and a tall centre jet flanked
by two low ones.

What it is showing, next to the stateless `fountain` package the
particle showcase carries:

- The arc is integrated, not a closed form of the clock, so the launch
  jitter and the drag compound per droplet instead of every droplet
  tracing the same parabola.
- Droplets are absorbed where they hit the pool rather than bouncing,
  which is what makes the fall read as water meeting water.
- Sprites stretch along their own screen-space velocity and take their
  colour from speed, so the fast core is foam and the slow fall is
  thin blue.

Usage: FreeCAD scripts/demo-fountain.py           (GUI or xvfb)
       FC_BGFX_SERVE_SCENE=<port> FreeCAD scripts/demo-fountain.py
Env:   FN_DOC   save path (default: do not save)
       FN_SHOT  screenshot path (optional, saveRenderDump)
       FN_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
_render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
_render.SetString("Type", "bgfx - OpenGL")
# The pool is the point as much as the jets are: without the water
# surface it is a flat blue disc and the falling droplets have nothing
# to land in that looks like water.
_render.SetBool("AO", True)
_render.SetBool("Volumetric", True)
_render.SetBool("WaterSurface", True)
_render.SetFloat("WaterWaveStrength", 0.22)
# Rings expanding from impacts rather than the directional swell: a
# round basin has no fetch for a travelling wave train, and what
# disturbs this pool is droplets landing in it.
_render.SetInt("WaterRippleType", 1)
_render.SetFloat("WaterRippleDensity", 1.6)
_render.SetBool("WaterRefraction", True)
_render.SetBool("WaterReflection", True)
_render.SetBool("WaterPlanarReflection", True)
_render.SetBool("WaterShadow", True)

# Look of this fountain, as opposed to of the bundled package: the
# effect ships defaults for an emitter a few units wide, and this basin
# is twenty units across.
TIME_SCALE = 1.5     # motion rate; the plume's shape is unchanged
OPACITY = 0.65

DOC = os.environ.get("FN_DOC", "")
SHOT = os.environ.get("FN_SHOT", "")
EXIT = os.environ.get("FN_EXIT", "") == "1"


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=30):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def place(x, y, z):
    return FreeCAD.Placement(FreeCAD.Vector(x, y, z), FreeCAD.Rotation())


# Height fraction of the emitter box the nozzle sits at, matching
# Param_Nozzle.y below: the step program launches from there and
# treats it as the pool surface to be absorbed into.
NOZZLE_FRAC = 0.12


def jet(doc, rendereffects, label, x, y, nozzle_z, width, height, launch,
        count, seed, spread, life, drop):
    """One water jet whose nozzle sits at world height nozzle_z.

    DemoPlacement is the CENTRE of the emitter box, and the box has to
    be tall enough to hold the whole arc, so placing it at the nozzle
    height would bury the nozzle half a box deep — under the terrace,
    where the jet is invisible. Solve for the centre that puts the
    nozzle where it was asked for instead.

    Demo=Emitter: the seed quads ARE the shape, so no preview solid
    competes with the masonry underneath.
    """
    fx = rendereffects.instantiate("waterjet")
    fx.Label = label
    fx.Demo = "Emitter"
    fx.DemoSize = FreeCAD.Vector(width, width, height)
    fx.DemoPlacement = place(x, y, nozzle_z + (0.5 - NOZZLE_FRAC) * height)
    fx.EmitterCount = count
    fx.EmitterSeed = seed
    prog = fx.Programs[0]
    # The seed geometry is generated from the Shader's count while the
    # state grid is sized from the program's: they have to agree or
    # part of the grid is simulated with nothing drawing it.
    prog.EmitterCount = count
    prog.EmitterSeed = seed
    prog.Label = label + "_Step"
    # Time scale. Water at this size moves faster than the package
    # default reads: the launch alone cannot fix that, since raising it
    # throws the jet higher instead of making it brisker. The emitter's
    # clock is what to change — the arc is height v^2/2g, which the
    # clock does not enter, so the same plume is simply traced faster.
    # Everything with time in its units follows on its own, because
    # nothing about the step changes but how often it is asked for.
    prog.EmitterTimeScale = TIME_SCALE
    prog.Param_Launch = launch
    prog.Param_Spread = (spread, 0.35, 0.0)
    prog.Param_Life = life
    # Spray thin enough to read as water rather than as a solid body:
    # a stack of sprites reaches 1-(1-a)^N, so the package default
    # saturates the jet core after about three overlaps.
    prog.Param_Opacity = OPACITY
    # Droplet size is in model units, so it belongs to the scene, not
    # to the package: this fountain is twenty units across and the
    # package default suits an emitter a few units wide.
    prog.Param_Size = drop
    return fx


def build():
    try:
        from freecad import rendereffects

        doc = FreeCAD.newDocument("FountainShowcase")

        # --- the pool: a shallow slab flagged as water ---
        pool = doc.addObject("Part::Cylinder", "Pool")
        pool.Radius = 20
        pool.Height = 1.6
        pool.Placement.Base = FreeCAD.Vector(0, 0, 0)

        # --- masonry: rim around the pool, pedestal in the middle ---
        rimOuter = doc.addObject("Part::Cylinder", "RimOuter")
        rimOuter.Radius = 22.5
        rimOuter.Height = 3.0
        rimBore = doc.addObject("Part::Cylinder", "RimBore")
        rimBore.Radius = 20
        rimBore.Height = 5.0
        rimBore.Placement.Base = FreeCAD.Vector(0, 0, -1)
        rim = doc.addObject("Part::Cut", "Rim")
        rim.Base = rimOuter
        rim.Tool = rimBore

        pedestal = doc.addObject("Part::Cylinder", "Pedestal")
        pedestal.Radius = 3.2
        pedestal.Height = 3.4
        pedestal.Placement.Base = FreeCAD.Vector(0, 0, 0)

        terrace = doc.addObject("Part::Box", "Terrace")
        terrace.Length = 74
        terrace.Width = 74
        terrace.Height = 1.5
        terrace.Placement.Base = FreeCAD.Vector(-37, -37, -1.5)

        doc.recompute()

        pool.ViewObject.ShapeColor = (0.10, 0.34, 0.55)
        pool.ViewObject.addProperty(
            "App::PropertyBool", "Render_Water").Render_Water = True
        pool.ViewObject.addProperty(
            "App::PropertyFloat",
            "Render_WaterDensity").Render_WaterDensity = 0.0
        for o, c in ((rim, (0.62, 0.60, 0.56)),
                     (pedestal, (0.62, 0.60, 0.56)),
                     (terrace, (0.50, 0.49, 0.47))):
            o.ViewObject.ShapeColor = c

        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        FreeCADGui.runCommand("Std_DrawStyleShadow", 0)

        # --- the jets: a tall centre one, two low flanking ones ---
        # Nozzles: the centre one on top of the pedestal, the flanking
        # pair just above the pool surface.
        # Launch speed is what sets the height of the plume: the apex
        # is v^2 / 2g before drag, so on a pool twenty units across a
        # jet needs to leave the nozzle at around 25 to read as a
        # fountain rather than as a bubbling spring.
        jet(doc, rendereffects, "JetCentre", 0, 0, 3.4,
            3.0, 30.0, 25.0, 2200, 5, 0.17, 4.0, 0.34)
        jet(doc, rendereffects, "JetEast", 11.5, 0, 1.7,
            2.4, 18.0, 16.0, 1100, 17, 0.28, 3.0, 0.30)
        jet(doc, rendereffects, "JetWest", -11.5, 0, 1.7,
            2.4, 18.0, 16.0, 1100, 23, 0.28, 3.0, 0.30)

        doc.recompute()
        view.viewIsometric()
        view.fitAll()
        settle()
        settle()

        FreeCAD.Console.PrintMessage(
            "fountain showcase: 3 stateful water jets over a water pool\n")
        if DOC:
            os.makedirs(os.path.dirname(DOC), exist_ok=True)
            doc.saveAs(DOC)
            FreeCAD.Console.PrintMessage("showcase saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("showcase shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("fountain showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
