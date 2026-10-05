"""A served scene with a picture panel open (docs/Sandbox.md 7.22, W3).

The third of the panel demos, and it exists for the reason the second did:
neither of the others can show what this stage built.  Pad's panel has no
custom-painted leaf and Sketcher's no button icon, while W3 is pictures and
icons -- so this panel carries one of each, plus the one thing a picture
needs that a widget does not:

  * a `QSvgWidget`, which the mirror cannot describe as a widget at all and
    sends as a PICTURE, an `img:<sha1>` the client fetches with
    `widgets.image`.  It is display until the pointer comes back: a click in
    the page is replayed into the real widget on the host
    (`PanelMirror::replayMouse`), and the event filter below turns that into
    a visible change -- the label counts the clicks and names where they
    landed, so the round trip is readable on screen rather than inferred;
  * a `QPushButton` carrying a real `QIcon`, which the mirror files the same
    way (`ImageStore::ofIcon`) and the card draws as the button's face
    instead of W1's `...` placeholder;
  * a label whose text the host changes, which is what proves the card
    repaints at all: it was written while fixing exactly that.

The SVG is drawn here rather than loaded from the installation, so the
picture is the same bytes on any box.

Run it through scripts/renderer-serve.sh:

    scripts/renderer-serve.sh scripts/demo-picturepanel.py 8079

Nothing here starts the panel mirror: it starts with the first client that
subscribes with `panels` (7.19) and stops with the last.

Runs persistently (no auto-close) so it can be viewed and streamed.
"""

import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")

# Qt objects the panel needs to outlive this script's locals.
_kept = []


def note(msg):
    FreeCAD.Console.PrintMessage("picturepanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def svg_bytes(color):
    """A square with a circle in it, so a scaled or mis-mapped picture is
    obvious on screen rather than plausible."""
    from PySide import QtCore

    return QtCore.QByteArray(
        (
            '<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96">'
            '<rect width="96" height="96" fill="%s"/>'
            '<circle cx="48" cy="48" r="34" fill="#ffffff" fill-opacity="0.35"/>'
            "</svg>" % color
        ).encode()
    )


def icon_pixmap():
    """A QIcon built here, for the same reason the SVG is: no dependence on
    which icon theme the box happens to have."""
    from PySide import QtCore, QtGui

    pixmap = QtGui.QPixmap(24, 24)
    pixmap.fill(QtGui.QColor("#4f8cff"))
    painter = QtGui.QPainter(pixmap)
    painter.setBrush(QtGui.QColor("#ffffff"))
    painter.setPen(QtCore.Qt.NoPen)
    painter.drawEllipse(QtCore.QRect(6, 6, 12, 12))
    painter.end()
    return QtGui.QIcon(pixmap)


def build_panel():
    """The form, as a plain Qt widget tree -- no workbench, no document
    object.  `Gui.Control.showDialog` takes anything with a `form`."""
    from PySide import QtCore, QtWidgets

    try:
        from PySide6.QtSvgWidgets import QSvgWidget
    except ImportError as exc:
        note("no QSvgWidget (%s); the picture half cannot be shown" % exc)
        return None

    form = QtWidgets.QWidget()
    form.setObjectName("pictureForm")
    form.setWindowTitle("Picture")
    layout = QtWidgets.QVBoxLayout(form)

    picture = QSvgWidget()
    picture.setObjectName("svgLeaf")
    picture.load(svg_bytes("#c2410c"))
    picture.setFixedSize(96, 96)
    layout.addWidget(picture)

    counter = QtWidgets.QLabel("clicks: 0")
    counter.setObjectName("clickCount")
    layout.addWidget(counter)

    button = QtWidgets.QPushButton("Recolour")
    button.setObjectName("recolour")
    button.setIcon(icon_pixmap())
    layout.addWidget(button)

    state = {"clicks": 0, "hot": False}

    def recolour():
        state["hot"] = not state["hot"]
        picture.load(svg_bytes("#0f766e" if state["hot"] else "#c2410c"))
        # a changed picture is a NEW img: id on the wire, which is the
        # cache's other half: the old id is not re-fetched and the new one
        # is fetched once
        note("recoloured -> %s" % ("teal" if state["hot"] else "orange"))

    button.clicked.connect(recolour)

    class Clicks(QtCore.QObject):
        """Installed on the picture rather than subclassing it: a Python
        subclass would be mirrored under its own class name, and the point
        is that this is an ordinary QSvgWidget."""

        def eventFilter(self, obj, event):
            if event.type() == QtCore.QEvent.Type.MouseButtonPress:
                state["clicks"] += 1
                at = event.position().toPoint()
                counter.setText("clicks: %d at %d,%d" % (state["clicks"], at.x(), at.y()))
                note("picture click %d at %d,%d" % (state["clicks"], at.x(), at.y()))
            return False

    clicks = Clicks()
    picture.installEventFilter(clicks)
    _kept.extend([form, picture, counter, button, clicks])
    return form


def open_panel():
    """Deferred, as the other two demos defer theirs: a startup script runs
    before the event loop has spun, and the mirror wants a laid-out tree."""
    try:
        form = build_panel()
        if form is None:
            return

        class Panel:
            def __init__(self, form):
                self.form = form

        panel = Panel(form)
        _kept.append(panel)
        FreeCADGui.Control.showDialog(panel)
        note("dialog up: %s" % bool(FreeCADGui.Control.activeDialog()))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("PicturePanel")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()

    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8079"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, open_panel)
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
