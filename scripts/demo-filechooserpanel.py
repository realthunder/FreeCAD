"""A served scene whose task panel carries a Gui::FileChooser (docs/Sandbox.md 7.22, W4b).

The fourth of the panel demos, and like the others it exists because none of
the first three can exercise this stage: Pad's fields, Sketcher's lists, the
picture panel and the message box all leave the file chooser untouched.

A chooser is the one leaf whose write path does not exist on the wire until
W4b. The mirror sends it as a LEAF -- the walk never goes into it -- so the
"..." button that raises the host's own QFileDialog is not a model any client
can click, and a browser therefore has no way to name a file at all except to
send one. That is deliberate, and it is the ruling this is built to
(2026-09-22): "Never expose host file system to browser.  But implement
browser side file chooser to upload file to host."

So the round trip is: the page picks a LOCAL file with its own picker, the
bytes go up over the control lane, the host writes them into a directory of
its own choosing, and the path comes back to the card, which announces it as
a pick -- `fileSelected`, the request that ends where the desktop's own pick
ends, so the panel's slot runs exactly as it would for a desktop user.

The label under the chooser reports what the HOST received: the path it was
handed and the size of that file on its own disk. That makes the whole trip
readable on screen rather than only in a log -- the same reason
demo-picturepanel.py counts its clicks. The size is the half that matters:
a path proves a string crossed, a size proves the BYTES did.

Run it through scripts/renderer-serve.sh:

    scripts/renderer-serve.sh scripts/demo-filechooserpanel.py 8081

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
    FreeCAD.Console.PrintMessage("filechooserpanel: %s\n" % msg)
    if _out:
        with open(_out, "a") as handle:
            handle.write(str(msg) + "\n")


def build_panel():
    """The form: a chooser, and a label reporting what the host got.

    `Gui.Control.showDialog` takes anything with a `form`, so no workbench
    and no document object are needed -- the same shape the other demos use.
    """
    from PySide import QtWidgets

    form = QtWidgets.QWidget()
    form.setObjectName("chooserForm")
    form.setWindowTitle("Choose a file")
    layout = QtWidgets.QVBoxLayout(form)

    layout.addWidget(QtWidgets.QLabel("Font file:", form))

    # Built through the UiLoader rather than imported: Gui::FileChooser is
    # registered as a widget producer (Gui/resource.cpp), which is how a
    # .ui file gets one, and it is the only route Python has to the class.
    chooser = FreeCADGui.UiLoader().createWidget("Gui::FileChooser", form)
    if chooser is None:
        note("the loader would not make a Gui::FileChooser")
        return None
    chooser.setObjectName("fontFile")
    chooser.setProperty("filter", "Fonts (*.ttf *.otf);;All files (*)")
    layout.addWidget(chooser)

    got = QtWidgets.QLabel("nothing chosen yet", form)
    got.setObjectName("gotLabel")
    layout.addWidget(got)

    def report(path):
        """What the HOST holds, which is the point of the scene."""
        path = str(path)
        try:
            size = os.path.getsize(path)
            got.setText("got %s (%d bytes on this machine)" % (path, size))
        except OSError as exc:
            got.setText("got %s -- but cannot read it: %s" % (path, exc))
        note("panel slot got %s" % path)

    # A panel connects to `fileNameSelected`, and that is the signal a
    # browser's pick has to reach -- a plain property write fires
    # `fileNameChanged` and leaves the slot unrun, which is the whole
    # reason W4b sends a request rather than a value.
    #
    # Whether Python can SEE that signal depends on how the widget is
    # wrapped: the loader hands back the nearest shiboken-known base, and
    # Gui::FileChooser has no binding of its own. So the real signal is
    # taken when it is there, and the child line edit's own
    # `textChanged` stands in when it is not -- the chooser's text is set
    # before either fires, so the label is right on both paths. Which one
    # is in use is logged, because they do not prove the same thing.
    try:
        chooser.fileNameSelected.connect(report)
        note("watching fileNameSelected (the signal a panel's slot uses)")
    except AttributeError:
        line = chooser.findChild(QtWidgets.QLineEdit)
        if line is None:
            note("no way to watch the chooser: no signal and no line edit")
        else:
            line.textChanged.connect(report)
            note("watching the line edit's textChanged (fileNameSelected is not"
                 " wrapped for Python here)")

    # W4b built two more shapes than its own scene could raise, and so left
    # both unproven on screen (7.22): a DIRECTORY chooser, which no browser
    # picker can answer, and a chooser inside a mirrored DIALOG rather than
    # a task panel.
    #
    # The directory one is here to be SEEN refusing: the card draws its
    # button disabled and says why, because a page can upload a file and
    # has no way to name a folder on the serving machine -- and the ruling
    # this is built to forbids showing it one.
    layout.addWidget(QtWidgets.QLabel("Output folder (a directory chooser):", form))
    folder = FreeCADGui.UiLoader().createWidget("Gui::FileChooser", form)
    if folder is not None:
        folder.setObjectName("outputDir")
        # Gui::FileChooser::Mode -- File 0, Directory 1 (Gui/FileDialog.h).
        # Set through the property, which is the only route Python has:
        # the class has no binding of its own, so the enum is not reachable
        # by name here.
        folder.setProperty("mode", 1)
        # Set, not read back: `property("mode")` raises "Can't find
        # converter for 'Gui::FileChooser::Mode'" -- shiboken wraps the
        # setter's int but has no converter for the enum coming out, and
        # that exception took the whole panel down the first time.
        note("directory chooser mode set to Directory (1)")
        layout.addWidget(folder)
        _kept.append(folder)
    else:
        note("the loader would not make the directory chooser")

    # A chooser inside a DIALOG. The panel's own chooser is content in a
    # task panel; this one is content in a `dialog:<n>` root, which reaches
    # the card through the modal layer W4 built rather than through the
    # panel container -- a different path to the same leaf, and the one
    # nothing had ever drawn.
    def in_dialog():
        dlg = QtWidgets.QDialog(FreeCADGui.getMainWindow())
        dlg.setObjectName("chooserDialog")
        dlg.setWindowTitle("Pick a file")
        lay = QtWidgets.QVBoxLayout(dlg)
        lay.addWidget(QtWidgets.QLabel("A chooser inside a dialog:", dlg))
        inner = FreeCADGui.UiLoader().createWidget("Gui::FileChooser", dlg)
        if inner is None:
            note("the loader would not make the dialog's chooser")
            return
        inner.setObjectName("dialogFile")
        lay.addWidget(inner)
        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok
            | QtWidgets.QDialogButtonBox.StandardButton.Cancel, dlg)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        lay.addWidget(buttons)

        def picked(path):
            got.setText("the dialog's chooser got %s" % path)
            note("dialog chooser got %s" % path)

        try:
            inner.fileNameSelected.connect(picked)
        except AttributeError:
            line = inner.findChild(QtWidgets.QLineEdit)
            if line is not None:
                line.textChanged.connect(picked)
        _kept.extend([dlg, inner])
        dlg.show()
        note("chooser dialog shown")

    raise_dialog = QtWidgets.QPushButton("Choose in a dialog", form)
    raise_dialog.setObjectName("dialogButton")
    raise_dialog.clicked.connect(in_dialog)
    layout.addWidget(raise_dialog)

    _kept.extend([form, chooser, got, raise_dialog])
    return form


def open_panel():
    """Deferred, as the other demos defer theirs: a startup script runs
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
        note("panel up: %s" % bool(FreeCADGui.Control.activeDialog()))
    except Exception:
        note("opening the panel failed:\n" + traceback.format_exc())


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)  # the renderer (bgfx) path, which streams
    view.SetBool("ShowNaviCube", True)

    doc = FreeCAD.newDocument("FileChooserPanel")
    box = doc.addObject("Part::Box", "Box")
    box.Length = 20
    box.Width = 12
    box.Height = 8
    doc.recompute()
    note("built %s" % box.Name)

    port = int(os.environ.get("FC_BGFX_SERVE_SCENE", "8081"))
    served = FreeCADGui.serveDocument(doc, port)
    note("serveDocument(%s, %d) -> %s" % (doc.Name, port, served))

    from PySide import QtCore

    QtCore.QTimer.singleShot(1500, open_panel)
    note("panel scheduled; viewer page at /fcviewer.html?doc=%s" % doc.Name)
except Exception:
    note("scene failed:\n" + traceback.format_exc())
