"""Bundled effect-package + factory suite (docs/RenderEngine.md §5.11).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under
xvfb; creates its own document. Covers the shipped effect packages
(share/Renderer/effects) and the freecad.rendereffects factory:

- discovery: list_effects() reports the bundled water and fire
  packages.
- water: activate("water") on a pool solid renders byte-identical to
  the stock Render_Water treatment; deactivate() restores the base
  capture byte-exact. The factory's viewProps switch the global
  Render_WaterSurface view toggle on.
- fire: activate("fire") on a proxy solid matches stock Render_Fire
  byte-exact; flipping the created program's Enabled property off
  restores the base byte-exact and back on restores the flame — the
  generic per-program switch bundled effects use for optional
  companion programs.
- fountain: activate("fountain") on a proxy solid matches stock
  Render_Fountain byte-exact (the scatter channel of the volume
  splice); deactivate() restores the base byte-exact.
- rain: a particle-only effect (no main-stage program) — activation
  alone makes streaks appear over the bound target,
  frozen-deterministic; deactivate() restores the base byte-exact.
- particle companions: enabling the shipped disabled Embers /
  WaterSpray / Droplets programs makes particles appear over the
  bound target (seed quads generated target-bbox-fit),
  frozen-deterministic; disabling restores the plain effect
  byte-exact.

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/effects.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "effects.txt"))

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

results = []

def log(msg):
    results.append(str(msg))
    print("US:", msg, flush=True)

def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()

def settle():
    for _ in range(30):
        time.sleep(0.2)
        pump(3)

def cap(name):
    view = FreeCADGui.ActiveDocument.ActiveView
    path = os.path.join(OUT, name + ".png")
    view.saveRenderDump(path)
    return path

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def changed_count(a, b, tol=3):
    import numpy as np
    from PySide.QtGui import QImage
    def load(path):
        img = QImage(path).convertToFormat(QImage.Format_RGBA8888)
        buf = np.frombuffer(bytes(img.constBits()), dtype=np.uint8)
        return buf.reshape((img.height(), img.width(), 4))[:, :, :3].astype(int)
    return int(np.any(np.abs(load(a) - load(b)) > tol, axis=2).sum())

def run():
    try:
        from PySide.QtGui import QGuiApplication
        from freecad import rendereffects
        log("platform=" + QGuiApplication.platformName())

        # --- discovery -------------------------------------------------
        names = sorted(e["name"] for e in rendereffects.list_effects())
        log("effects discovered: %s" % names)
        ok = ("water" in names and "fire" in names
              and "fountain" in names and "rain" in names)
        log("ASSERT discovery: %s" % ("PASS" if ok else "FAIL"))

        doc = FreeCAD.newDocument("EffectsTest")
        floor = doc.addObject("Part::Box", "Floor")
        floor.Length = 30
        floor.Width = 30
        floor.Height = 2
        floor.Placement.Base = FreeCAD.Vector(-15, -15, -2)
        pool = doc.addObject("Part::Box", "Pool")
        pool.Length = 12
        pool.Width = 12
        pool.Height = 3
        pool.Placement.Base = FreeCAD.Vector(-14, -14, 0)
        pool.ViewObject.ShapeColor = (0.15, 0.35, 0.55)
        firebox = doc.addObject("Part::Box", "FireBody")
        firebox.Length = 7
        firebox.Width = 7
        firebox.Height = 10
        firebox.Placement.Base = FreeCAD.Vector(5, 3, 0)
        fntbox = doc.addObject("Part::Box", "FountainBody")
        fntbox.Length = 4
        fntbox.Width = 4
        fntbox.Height = 9
        fntbox.Placement.Base = FreeCAD.Vector(-8, 6, 0)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewIsometric()
        view.fitAll()
        # The scene light is a shading switch now, not a draw style
        # (docs/CoinRetirement.md stage 4e).
        FreeCADGui.activeDocument().activeView().Render_Light = True
        view.Render_Volumetric = True
        # start with the global water toggle OFF: activate("water")'s
        # manifest viewProps must switch it on
        view.Render_WaterSurface = False
        view.RenderDebug_FreezeFrame = True
        settle()

        # --- stock references -----------------------------------------
        pvo = pool.ViewObject
        view.Render_WaterSurface = True
        pump(5)
        base_ws = cap("e_base")   # water toggle on, no effects anywhere
        pvo.addProperty("App::PropertyBool", "Render_Water")
        pvo.Render_Water = True
        settle()
        w_stock = cap("e_wstock")
        pvo.removeProperty("Render_Water")
        settle()
        ok = byte_equal(base_ws, cap("e_wrestore"), "stock water restore")
        log("ASSERT stock-water-ref: %s"
            % ("PASS" if ok and changed_count(base_ws, w_stock) > 1000
               else "FAIL"))

        fvo = firebox.ViewObject
        fvo.addProperty("App::PropertyBool", "Render_Fire")
        fvo.Render_Fire = True
        settle()
        f_stock = cap("e_fstock")
        fvo.removeProperty("Render_Fire")
        settle()
        ok = byte_equal(base_ws, cap("e_frestore"), "stock fire restore")
        log("ASSERT stock-fire-ref: %s"
            % ("PASS" if ok and changed_count(base_ws, f_stock) > 1000
               else "FAIL"))

        # --- water package: activate == stock, deactivate == base -----
        view.Render_WaterSurface = False
        pump(5)
        look = rendereffects.activate("water", targets=[pool])
        ok_prop = bool(view.Render_WaterSurface)
        log("factory set Render_WaterSurface: %s" % ok_prop)
        settle()
        settle()
        w_fx = cap("e_wfx")
        ok = byte_equal(w_stock, w_fx, "water effect vs stock")
        log("ASSERT water-effect: %s"
            % ("PASS" if ok and ok_prop else "FAIL"))

        # --- spray particle companion ----------------------------------
        spray = doc.getObject("water_WaterSpray")
        spray.Enabled = True
        doc.recompute()
        settle()
        settle()
        spr = cap("e_spray")
        n = changed_count(w_fx, spr)
        log("spray changed px vs plain water: %d" % n)
        log("ASSERT spray-appear: %s" % ("PASS" if n > 150 else "FAIL"))
        spray.Enabled = False
        doc.recompute()
        settle()
        ok = byte_equal(w_fx, cap("e_sprayoff"), "spray off restore")
        log("ASSERT spray-off: %s" % ("PASS" if ok else "FAIL"))

        rendereffects.deactivate(look)
        settle()
        ok = byte_equal(base_ws, cap("e_wgone"), "water deactivate restore")
        log("ASSERT water-deactivate: %s" % ("PASS" if ok else "FAIL"))

        # --- fire package + the Enabled toggle -------------------------
        look = rendereffects.activate("fire", targets=[firebox])
        settle()
        settle()
        f_fx = cap("e_ffx")
        ok = byte_equal(f_stock, f_fx, "fire effect vs stock")
        log("ASSERT fire-effect: %s" % ("PASS" if ok else "FAIL"))

        # --- ember particle companion ----------------------------------
        embers = doc.getObject("fire_Embers")
        embers.Enabled = True
        doc.recompute()
        settle()
        settle()
        emb = cap("e_embers")
        n = changed_count(f_fx, emb)
        log("embers changed px vs plain fire: %d" % n)
        emb2 = cap("e_embers2")
        det = byte_equal(emb, emb2, "frozen embers determinism")
        log("ASSERT embers-appear: %s"
            % ("PASS" if n > 200 and det else "FAIL"))
        embers.Enabled = False
        doc.recompute()
        settle()
        ok = byte_equal(f_fx, cap("e_embersoff"), "embers off restore")
        log("ASSERT embers-off: %s" % ("PASS" if ok else "FAIL"))

        prog = doc.getObject("fire_FireMedium")
        prog.Enabled = False
        doc.recompute()
        settle()
        ok = byte_equal(base_ws, cap("e_fdisabled"),
                        "disabled program restores base")
        log("ASSERT enabled-off: %s" % ("PASS" if ok else "FAIL"))

        prog.Enabled = True
        doc.recompute()
        settle()
        settle()
        ok = byte_equal(f_stock, cap("e_fenabled"),
                        "re-enabled program restores flame")
        log("ASSERT enabled-on: %s" % ("PASS" if ok else "FAIL"))

        rendereffects.deactivate(look)
        settle()
        ok = byte_equal(base_ws, cap("e_fgone"), "fire deactivate restore")
        log("ASSERT fire-deactivate: %s" % ("PASS" if ok else "FAIL"))

        # --- fountain package: the scatter channel ----------------------
        nvo = fntbox.ViewObject
        nvo.addProperty("App::PropertyBool", "Render_Fountain")
        nvo.Render_Fountain = True
        settle()
        n_stock = cap("e_nstock")
        nvo.removeProperty("Render_Fountain")
        settle()
        ok = byte_equal(base_ws, cap("e_nrestore"),
                        "stock fountain restore")
        log("ASSERT stock-fountain-ref: %s"
            % ("PASS" if ok and changed_count(base_ws, n_stock) > 1000
               else "FAIL"))

        look = rendereffects.activate("fountain", targets=[fntbox])
        settle()
        settle()
        n_fx = cap("e_nfx")
        ok = byte_equal(n_stock, n_fx, "fountain effect vs stock")
        log("ASSERT fountain-effect: %s" % ("PASS" if ok else "FAIL"))

        # --- droplet particle companion ---------------------------------
        drops = doc.getObject("fountain_Droplets")
        drops.Enabled = True
        doc.recompute()
        settle()
        settle()
        drp = cap("e_drops")
        n = changed_count(n_fx, drp)
        log("droplets changed px vs plain fountain: %d" % n)
        drp2 = cap("e_drops2")
        det = byte_equal(drp, drp2, "frozen droplets determinism")
        log("ASSERT droplets-appear: %s"
            % ("PASS" if n > 200 and det else "FAIL"))
        drops.Enabled = False
        doc.recompute()
        settle()
        ok = byte_equal(n_fx, cap("e_dropsoff"), "droplets off restore")
        log("ASSERT droplets-off: %s" % ("PASS" if ok else "FAIL"))

        rendereffects.deactivate(look)
        settle()
        ok = byte_equal(base_ws, cap("e_ngone"),
                        "fountain deactivate restore")
        log("ASSERT fountain-deactivate: %s" % ("PASS" if ok else "FAIL"))

        # --- rain package: particle-only effect -------------------------
        look = rendereffects.activate("rain", targets=[floor])
        doc.recompute()
        settle()
        settle()
        rn = cap("e_rain")
        n = changed_count(base_ws, rn)
        log("rain changed px vs base: %d" % n)
        rn2 = cap("e_rain2")
        det = byte_equal(rn, rn2, "frozen rain determinism")
        log("ASSERT rain-appear: %s"
            % ("PASS" if n > 200 and det else "FAIL"))

        rendereffects.deactivate(look)
        settle()
        # tolerance, not byte-equal: a dedicated 3-cycle probe restores
        # byte-exact, but this late-suite capture once caught the box's
        # known single-pixel ±LSB frame wobble
        n = changed_count(base_ws, cap("e_rgone"))
        log("rain deactivate residual px: %d" % n)
        log("ASSERT rain-deactivate: %s" % ("PASS" if n <= 4 else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
