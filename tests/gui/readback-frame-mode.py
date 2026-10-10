"""A 3D view on a read-back backend shows the frame that was drawn.

On Direct3D, Vulkan and Metal the backend's frame reaches the Qt view by
being read back (docs/DeviceAdoption.md section 10). Pipelined, a frame
shows the newest copy that has landed, a frame or two old, and nothing
asked for the frame that would catch the screen up: a hover highlight
came with the NEXT redraw, stayed after the mouse had left, and a view
kept its old picture at its old size after a resize (docs/HandsOnQueue.md
entries 9 and 10). Render/ReadbackFrameMode decides it now.

Claims. What the view holds is read out of its GL widget with no paint
of the test's own, a second after the event.

  With the default, 'Pipelined while animating':
  - the mouse rests on a face: the face is highlighted;
  - it leaves: the highlight is gone;
  - the wheel is turned eight steps over the view: the view holds the
    picture it still holds four redraws later;
  - the window takes a new size, three times: the view holds the picture
    it still holds four redraws later;
  - while the view spins, no frame waits for its copy;
  - the spin stopped: the view holds the picture it still holds four
    redraws later.

  With 'Pipelined': the first four again -- the frame that waits comes
  by itself when the redraws stop -- and no frame of a spin waits.

  With 'Wait': the first four again, and the frames of a spin wait.

The waiting is read from the renderer's own report (Render/DebugTiming,
"render readback composite"), which only a read-back backend prints; on
OpenGL the frame is blitted, those claims are noted as not applicable
and the rest hold whatever the mode.

Scored against the tree before the change: see the commit message.
"""
import ctypes
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtOpenGLWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FrameMode"
RENDER = "User parameter:BaseApp/Preferences/View/Render"
MODES = {0: "Wait", 1: "Pipelined while animating", 2: "Pipelined"}
REPORT = re.compile(r"render readback composite \(ms/frame\):.*?wait ([0-9.]+) .*?-- (\d+) frames")


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def gl_widget():
    best = None
    for w in FreeCADGui.getMainWindow().findChildren(QtOpenGLWidgets.QOpenGLWidget):
        if w.isVisible() and (best is None or w.width() * w.height() > best.width() * best.height()):
            best = w
    return best


def held(tag):
    """What the view's GL widget holds for the screen, as it stands."""
    w = gl_widget()
    gl = ctypes.windll.opengl32 if os.name == "nt" else ctypes.CDLL("libGL.so.1")
    w.makeCurrent()
    try:
        ctx = QtGui.QOpenGLContext.currentContext()
        ctx.functions().glBindFramebuffer(0x8D40, w.defaultFramebufferObject())
        dpr = w.devicePixelRatioF()
        pw, ph = int(w.width() * dpr), int(w.height() * dpr)
        buf = (ctypes.c_ubyte * (pw * ph * 4))()
        while gl.glGetError():
            pass
        gl.glPixelStorei(0x0D05, 1)
        gl.glReadPixels(0, 0, pw, ph, 0x1908, 0x1401, buf)
    finally:
        w.doneCurrent()
    img = QtGui.QImage(bytes(buf), pw, ph, pw * 4, QtGui.QImage.Format_RGBA8888).mirrored(False, True)
    img = img.convertToFormat(QtGui.QImage.Format_RGB32)
    img.save(os.path.join(OUT, tag + ".png"))
    return img


def highlighted(img):
    """Samples in a highlight colour: far from grey."""
    n = 0
    for y in range(0, img.height(), 2):
        for x in range(0, img.width(), 2):
            p = img.pixel(x, y)
            c = ((p >> 16) & 255, (p >> 8) & 255, p & 255)
            if max(c) - min(c) > 90:
                n += 1
    return n


def differing(a, b):
    if a.size() != b.size():
        return -1
    n = 0
    for y in range(0, a.height(), 2):
        for x in range(0, a.width(), 2):
            pa, pb = a.pixel(x, y), b.pixel(x, y)
            if max(abs(((pa >> s) & 255) - ((pb >> s) & 255)) for s in (16, 8, 0)) > 12:
                n += 1
    return n


def move_mouse(pos):
    w = gl_widget()
    p = QtCore.QPointF(pos[0], pos[1])
    target = w.parentWidget() if w.parentWidget() is not None else w
    for cand in (w, target):
        QtWidgets.QApplication.sendEvent(
            cand, QtGui.QMouseEvent(QtCore.QEvent.MouseMove, p,
                                    QtCore.QPointF(cand.mapToGlobal(p.toPoint())),
                                    QtCore.Qt.NoButton, QtCore.Qt.NoButton, QtCore.Qt.NoModifier))


def redraws(view, n=4):
    for _ in range(n):
        view.redraw()
        settle(0.25)


def report():
    """The read-back lines of the renderer's timing report so far: (wait
    in ms a frame, frames) each."""
    out = []
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTextEdit):
        if "ReportOutput" in w.metaObject().className():
            for m in REPORT.finditer(w.toPlainText()):
                out.append((float(m.group(1)), int(m.group(2))))
    return out


def hover(view, tag):
    w = gl_widget()
    p = view.getPointOnViewport(FreeCAD.Vector(5, 5, 10))
    face = (p[0], w.height() - 1 - p[1])
    away = (8, 8)
    move_mouse(away)
    settle(0.4)
    redraws(view)
    plain = highlighted(held(tag + "-0-plain"))
    move_mouse((face[0] - 2, face[1]))
    settle(0.05)
    move_mouse(face)
    settle(1.0)
    rest = highlighted(held(tag + "-1-rest")) - plain
    redraws(view)
    settled = highlighted(held(tag + "-1-rest-settled")) - plain
    check(tag + ": the face the mouse rests on is highlighted", settled > 50 and rest == settled,
          "highlight samples over plain: %d a second later, %d once settled" % (rest, settled))
    move_mouse(away)
    settle(1.0)
    left = highlighted(held(tag + "-2-left")) - plain
    check(tag + ": the highlight is gone when the mouse has left", left == 0,
          "%d highlight samples over plain" % left)


def wheel(view, tag):
    w = gl_widget()
    cam = view.getCameraNode()
    before = (cam.height.getValue() if hasattr(cam, "height") else 0.0,
              tuple(cam.position.getValue().getValue()))
    p = QtCore.QPointF(w.width() / 2.0, w.height() / 2.0)
    g = QtCore.QPointF(w.mapToGlobal(p.toPoint()))
    for target in (w, w.parentWidget()):
        if target is None:
            continue
        for _ in range(8):
            QtWidgets.QApplication.sendEvent(
                target, QtGui.QWheelEvent(p, g, QtCore.QPoint(0, 0), QtCore.QPoint(0, 120),
                                          QtCore.Qt.NoButton, QtCore.Qt.NoModifier,
                                          QtCore.Qt.NoScrollPhase, False))
            settle(0.03)
        after = (cam.height.getValue() if hasattr(cam, "height") else 0.0,
                 tuple(cam.position.getValue().getValue()))
        if after != before:
            break
    settle(1.0)
    first = held(tag + "-wheel-0")
    redraws(view)
    later = held(tag + "-wheel-1")
    n = differing(first, later)
    check(tag + ": a second after eight wheel steps the view holds its settled picture",
          after != before and n == 0,
          "%d samples differ; the camera %s" % (n, "moved" if after != before else "did NOT move"))
    view.viewIsometric()
    view.fitAll()
    settle(0.5)


def resize(view, tag):
    mw = FreeCADGui.getMainWindow()
    for rnd, size in enumerate(((1300, 950), (900, 700), (1000, 800))):
        mw.resize(*size)
        settle(1.0)
        first = held("%s-size%d-0" % (tag, rnd))
        redraws(view)
        later = held("%s-size%d-1" % (tag, rnd))
        n = differing(first, later)
        check("%s: a second after taking the size %dx%d the view holds its settled picture" % (
            tag, size[0], size[1]), n == 0,
            "%d of %d samples differ" % (n, (first.width() // 2) * (first.height() // 2)))


def spin(view, tag, readback, waits):
    before = len(report())
    view.startAnimating(0, 0, 1, 0.3)
    settle(4.5)
    lines = report()[before:]
    view.stopAnimating()
    settle(1.0)
    first = held(tag + "-spin-0-stopped")
    redraws(view)
    later = held(tag + "-spin-1-settled")
    n = differing(first, later)
    check(tag + ": a second after the spin stopped the view holds its settled picture", n == 0,
          "%d samples differ" % n)
    if not readback:
        note("NOTE %s: no read-back composite on this backend; the waiting is not claimed" % tag)
        return
    # the first line may hold frames from before the spin, the last one
    # frames from after it
    body = lines[1:-1] if len(lines) > 2 else lines
    busy = [l for l in body if l[1] >= 20]
    text = ", ".join("%.2f ms over %d frames" % l for l in lines)
    if waits:
        check(tag + ": the frames of a spin wait for their copy",
              bool(busy) and all(l[0] > 0.0 for l in busy), text)
    else:
        check(tag + ": no frame of a spin waits for its copy",
              bool(busy) and all(l[0] == 0.0 for l in busy), text)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool("ShowNaviCube", False)
        params = FreeCAD.ParamGet(RENDER)
        params.SetBool("DebugTiming", True)
        mode = params.GetInt("ReadbackFrameMode", 1)
        check("the default is 'Pipelined while animating'", mode == 1, MODES.get(mode, mode))
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1000, 800)
        settle(0.5)
        view = FreeCADGui.getDocument(DOC).activeView()
        view.viewIsometric()
        view.fitAll()
        settle(2.5)
        readback = bool(report())
        note("backend %r, read-back composite %s, forced by FC_BGFX_READBACK_SYNC=%s" % (
            params.GetString("Type", ""), readback, os.environ.get("FC_BGFX_READBACK_SYNC")))
        for mode, waits in ((1, False), (2, False), (0, True)):
            params.SetInt("ReadbackFrameMode", mode)
            settle(0.5)
            tag = "mode%d" % mode
            note("-- %s" % MODES[mode])
            hover(view, tag)
            wheel(view, tag)
            resize(view, tag)
            view.viewIsometric()
            view.fitAll()
            settle(1.0)
            spin(view, tag, readback, waits)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            FreeCAD.ParamGet(RENDER).RemInt("ReadbackFrameMode")
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
