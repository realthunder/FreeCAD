"""The MaterialX chess set, staged for the golden render tests.

The real-document leg of the render test set (docs/RenderDebug.md
section 5.2): glTF pieces wearing the MaterialX chess-set document under
a stated HDR environment. Where render-test-scene.py is four primitives
built in process, this one imports a real asset with a real material
library, which is the case that exercises the map binding, the texture
path and the MaterialX splice.

It is the heavy leg on purpose and is registered only with
FC_RENDER_HEAVY_TESTS=ON. Both assets ship in the MaterialX submodule,
so nothing outside the tree is needed; the ctest entry checks they are
checked out before registering.

An in-repo companion of ~/works/sw/fcad-probes/chess_serve.py, which
stages the same scene for a streamed viewer. Kept here rather than
referenced there because a test may not depend on a probe directory that
is not part of the repository.

Deterministic by construction, as a golden scene must be: every setting
is a literal, the environment picture is the one in the tree, and the
camera is assigned as a literal rotation rather than animated into place
(viewIsometric() and friends animate, and a capture taken mid-flight is
not reproducible).
"""
import os
import traceback

import FreeCAD
import FreeCADGui

# Before the first 3D view exists: the MaterialX splice only happens in
# the bgfx renderer, which only exists in render-cache mode 3.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(REPO, "src/3rdParty/MaterialX/resources")
GLB = os.path.join(RES, "Geometry/chess_set.glb")
MTLX = os.path.join(
    RES, "Materials/Examples/StandardSurface/standard_surface_chess_set.mtlx")
HDR = os.path.join(RES, "Lights/san_giuseppe_bridge.hdr")
RENDER = "User parameter:BaseApp/Preferences/View/Render"

# FC_RENDER_TEST_BG=0 keeps the HDR environment as the LIGHT but stops
# drawing it as the picture behind the model. It matters most here: the
# chess set occupies a modest part of the frame and the Venice HDR fills
# the rest, so with the background on most of a golden's pixels are
# scenery. A regression in the material of a piece moves a few hundred
# of them; a camera that lands a degree off moves a hundred thousand.
# The flat case makes the model the subject of its own test, and the
# background case still covers the environment path -- so both exist.
BACKGROUND = os.environ.get("FC_RENDER_TEST_BG", "1") != "0"


def say(s):
    FreeCAD.Console.PrintMessage("[render-test-chess] %s\n" % s)


def stage():
    import ImportGui
    from pivy import coin

    FreeCAD.ParamGet(
        "User parameter:BaseApp/Preferences/NotificationArea").SetBool(
        "NonIntrusiveNotificationsEnabled", False)
    # Autosave fires on a timer and would land mid-capture.
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
        "AutoSaveTimeout", 0)

    p = FreeCAD.ParamGet(RENDER)
    p.SetBool("PBR", True)
    p.SetBool("PBRFromSpecular", False)
    p.SetBool("PBREnvBackground", BACKGROUND)
    p.SetFloat("PBREnvBlur", 0.0)
    p.SetFloat("PBREnvIntensity", 1.0)
    p.SetString("PBREnvImage", HDR)
    p.SetBool("AO", False)
    p.SetBool("Cavity", False)
    p.SetBool("Matcap", False)
    p.SetBool("Bloom", False)
    p.SetBool("Volumetric", False)
    p.SetBool("GroundReflection", False)
    p.SetBool("Shadow", False)
    # Temporal accumulation keeps refining a parked frame, so two runs
    # would capture different amounts of convergence.
    p.SetBool("TemporalAccum", False)
    p.SetBool("Light", False)
    p.SetInt("OutputTransform", 1)
    p.SetFloat("Exposure", 1.0)

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetBool("ShowAxisCross", False)
    view.SetBool("ShowNaviCube", False)
    view.SetBool("CornerCoordSystem", False)
    view.SetBool("ShowFPS", False)
    if not BACKGROUND:
        view.SetBool("Gradient", False)
        view.SetBool("RadialGradient", False)
        view.SetBool("UseBackgroundColorMid", False)
        view.SetUnsigned("BackgroundColor", 858993663)

    doc = FreeCAD.newDocument("RenderTestChess")
    FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
    ImportGui.insert(GLB, doc.Name)
    ImportGui.insert(MTLX, doc.Name)
    doc.recompute()

    pieces = [o for o in doc.Objects if o.isDerivedFrom("Part::Feature")]
    say("%d shapes, %d wearing a surface"
        % (len(pieces),
           sum(1 for o in pieces
               if o.ShapeMaterial
               and o.ShapeMaterial.getAppearanceValue("MaterialXSurface"))))

    v = FreeCADGui.ActiveDocument.ActiveView
    try:
        v.ShowNaviCube = False
    except Exception:
        pass
    try:
        v.setAxisCross(False)
    except Exception:
        pass
    v.setCameraType("Perspective")
    v.getCameraNode().orientation.setValue(
        coin.SbRotation(0.4247, 0.1759, 0.3389, 0.8226))
    v.fitAll()
    say("STAGED OK")


def deferred():
    try:
        stage()
    except Exception:
        say(traceback.format_exc())
        say("STAGE FAILED")


from PySide import QtCore  # noqa: E402  (after the parameter setup above)

QtCore.QTimer.singleShot(1500, deferred)
