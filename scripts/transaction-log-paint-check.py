# GUI check of docs/TransactionLog.md sec 31.16, 31.18: the faces of a shape
# painted by name -- the names on the object (ColoredElements), their
# colours on its view provider (MappedColors) -- put back whole by what
# puts a value back: an undo, a branch switched to, a merge that takes
# them. And docs/ShapeAppearanceDesign.md sec 13.6, the store those colours
# are kept in (MappedAppearance, a material for each name). One GUI run in a
# fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-pt \
#     PAINTCHECK_OUT=/tmp/pt/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-paint-check.py
#
# It writes PASS/FAIL lines to $PAINTCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, re, struct, tempfile, traceback, zipfile, zlib
import FreeCAD as App
import FreeCADGui as Gui
from FreeCAD import Vector as V
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["PAINTCHECK_OUT"]
lines = []
RED, GREEN, BLUE = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def rgb(c):
    return tuple(round(x, 1) for x in c[:3])


def named(obj):
    # The painted elements by name, each with its colour.
    subs = obj.ColoredElements[1] if obj.ColoredElements else []
    colors = [rgb(c) for c in obj.ViewObject.MappedColors]
    if len(subs) != len(colors):
        return {"subs": list(subs), "colors": colors}
    return dict(zip(subs, colors))


def stored(obj):
    # The same out of the store itself: the colour of each name's material.
    subs = obj.ColoredElements[1] if obj.ColoredElements else []
    colors = [rgb(m.DiffuseColor) for m in obj.ViewObject.MappedAppearance]
    if len(subs) != len(colors):
        return {"subs": list(subs), "colors": colors}
    return dict(zip(subs, colors))


def older(path, to):
    # The file as a build older than MappedAppearance wrote it: the colours
    # in MappedColors, a plain colour list, and nowhere else. The entries are
    # copied as they are stored -- the blobs are zstd, which this Python's
    # zipfile does not read -- and GuiDocument.xml alone is written again.
    done = [0, 0]
    with zipfile.ZipFile(path) as zin, open(path, "rb") as fp, open(to, "wb") as out:
        central = []
        for item in zin.infolist():
            if item.filename == "GuiDocument.xml":
                xml = zin.read(item).decode()
                xml, done[0] = re.subn(r'\s*<Property name="MappedAppearance"[^>]*>.*?</Property>',
                                       "", xml, flags=re.S)
                xml, done[1] = re.subn(r'(<Property name="MappedColors" type=")[^"]*"',
                                       r'\1App::PropertyColorList"', xml)
                data = xml.encode()
                packer = zlib.compressobj(6, zlib.DEFLATED, -15)
                raw = packer.compress(data) + packer.flush()
                method, crc, size = 8, zlib.crc32(data), len(data)
            else:
                fp.seek(item.header_offset)
                head = fp.read(30)
                fp.seek(sum(struct.unpack("<HH", head[26:30])), 1)
                raw = fp.read(item.compress_size)
                method, crc, size = item.compress_type, item.CRC, item.file_size
            name = item.filename.encode()
            fields = (item.extract_version, 0x800, method, 0, 0x21, crc, len(raw), size, len(name), 0)
            central.append(struct.pack("<4sHHHHHHIIIHHHHHII", b"PK\x01\x02", 20, *fields,
                                       0, 0, 0, 0, out.tell()) + name)
            out.write(struct.pack("<4sHHHHHIIIHH", b"PK\x03\x04", *fields) + name + raw)
        start = out.tell()
        out.write(b"".join(central))
        out.write(struct.pack("<4sHHHHIIH", b"PK\x05\x06", 0, 0, len(central), len(central),
                              out.tell() - start, start, 0))
    return done


def shown(obj):
    # The faces drawn in another colour than the object's, by where they lie.
    vp = obj.ViewObject
    out = {}
    if len(vp.DiffuseColor) > 1:
        for i, c in enumerate(vp.DiffuseColor):
            if rgb(c) != rgb(vp.ShapeColor):
                b = obj.Shape.Faces[i].BoundBox
                out[tuple(round(v, 3) for v in (b.XMin, b.XMax, b.ZMin, b.ZMax))] = rgb(c)
    return out


def face(obj, **at):
    for i, f in enumerate(obj.Shape.Faces):
        if all(abs(getattr(f.BoundBox, k) - v) < 1e-6 for k, v in at.items()):
            return "Face%d" % (i + 1)
    return None


def paint(doc, name, obj, colors):
    doc.openTransaction(name)
    vp = doc.getObject(obj).ViewObject
    now = {k: v for k, v in vp.getElementColors().items() if k not in ("Face", "Edge", "Vertex")}
    now.update(colors)
    vp.setElementColors(now)
    doc.recompute()
    doc.commitTransaction()


def cut(name, folder):
    # A box a cylinder is cut from, the cylinder below it: six faces.
    doc = App.newDocument(name)
    doc.UndoMode = 1
    doc.openTransaction("base")
    box = doc.addObject("Part::Box", "Box")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 30
    cyl.Placement.Base = V(5, 5, -40)
    c = doc.addObject("Part::Cut", "Cut")
    c.Base = box
    c.Tool = cyl
    doc.recompute()
    doc.commitTransaction()
    doc.saveAs(os.path.join(folder, name + ".FCStd"))
    return doc


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 2)
        folder = tempfile.mkdtemp(prefix="fc-paint-")

        # Undo and redo: each step's names and colours, and no others.
        doc = App.newDocument("PaintUndo")
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(folder, "PaintUndo.FCStd"))
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        front = face(doc.Box, XMax=0)
        paint(doc, "red top", "Box", {top: RED})
        paint(doc, "green bottom", "Box", {bottom: GREEN})
        both = {top: RED, bottom: GREEN}
        check("two faces painted, by name (%r)" % named(doc.Box), named(doc.Box) == both)
        doc.undo()
        check("undone: the first only (%r)" % named(doc.Box), named(doc.Box) == {top: RED})
        doc.undo()
        check("undone again: none (%r)" % named(doc.Box), named(doc.Box) == {})
        doc.redo()
        doc.redo()
        check("redone: both (%r)" % named(doc.Box), named(doc.Box) == both)

        # A branch that paints one more, left and come back to. The list
        # put back is longer than the one there: it was cut to that one's
        # length, the third name gone for good.
        doc.createTransactionBranch("side")
        paint(doc, "side blue front", "Box", {front: BLUE})
        three = dict(both)
        three[front] = BLUE
        check("the branch has three (%r)" % named(doc.Box), named(doc.Box) == three)
        there = shown(doc.Box)
        doc.switchTransactionBranch("main")
        check("main has its two (%r)" % named(doc.Box), named(doc.Box) == both)
        check("and draws two (%r)" % shown(doc.Box), len(shown(doc.Box)) == 2)
        doc.switchTransactionBranch("side")
        check("back on the branch: three by name (%r)" % named(doc.Box), named(doc.Box) == three)
        check("and drawn where they were", shown(doc.Box) == there)
        App.closeDocument(doc.Name)

        # A merge that takes a painted face: theirs paints, ours changes a
        # radius. Nothing is asked, and the colour came without its name --
        # and then not at all.
        doc = cut("PaintTake", folder)
        doc.createTransactionBranch("side")
        side = face(doc.Cut, XMin=10)
        paint(doc, "side red", "Cut", {side: RED})
        there = shown(doc.Cut)
        check("theirs paints one face of the cut (%r)" % there, len(there) == 1)
        doc.switchTransactionBranch("main")
        check("main has none", named(doc.Cut) == {} and shown(doc.Cut) == {})
        doc.openTransaction("main radius")
        doc.Cyl.Radius = 3
        doc.recompute()
        doc.commitTransaction()
        pv = doc.previewTransactionMerge("side")
        check("the merge asks nothing (%r)" % [c["key"] for c in pv["changes"] if c["kind"] == "conflict"],
              pv["conflicts"] == 0)
        merged = doc.mergeTransactionBranch("side")
        check("and is done", (merged["unresolved"], merged["failed"]) == ([], []))
        doc.recompute()
        check("merged: the face by name (%r)" % named(doc.Cut), named(doc.Cut) == {side: RED})
        check("and drawn where theirs drew it (%r)" % shown(doc.Cut), shown(doc.Cut) == there)

        # The same with ours drilling through: seven faces where theirs
        # had six, and the painted one is not the face it was by number.
        doc2 = cut("PaintDrill", folder)
        doc2.createTransactionBranch("side")
        paint(doc2, "side red", "Cut", {face(doc2.Cut, XMin=10): RED})
        there = shown(doc2.Cut)
        doc2.switchTransactionBranch("main")
        doc2.openTransaction("main drills")
        doc2.Cyl.Placement.Base = V(5, 5, -10)
        doc2.recompute()
        doc2.commitTransaction()
        check("ours has a seventh face", len(doc2.Cut.Shape.Faces) == 7)
        merged = doc2.mergeTransactionBranch("side")
        check("merged with nothing asked", (merged["unresolved"], merged["failed"]) == ([], []))
        doc2.recompute()
        check("the face by name, as the merged shape numbers it (%r)" % named(doc2.Cut),
              named(doc2.Cut) == {face(doc2.Cut, XMin=10): RED})
        check("and drawn on that face alone (%r)" % shown(doc2.Cut), shown(doc2.Cut) == there)
        App.closeDocument(doc.Name)
        App.closeDocument(doc2.Name)

        # The store. A material for each name, its colour what was painted;
        # saved and read back; and read out of a file that has the colours
        # and no materials.
        doc = App.newDocument("PaintStore")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        both = {top: RED, bottom: GREEN}
        paint(doc, "two", "Box", both)
        check("a material for each name (%r)" % stored(doc.Box), stored(doc.Box) == both)
        there = shown(doc.Box)
        path = os.path.join(folder, "PaintStore.FCStd")
        doc.saveAs(path)
        App.closeDocument(doc.Name)
        doc = App.openDocument(path)
        check("saved and read: by name (%r)" % named(doc.Box),
              named(doc.Box) == both and stored(doc.Box) == both)
        check("and drawn where they were", shown(doc.Box) == there)
        App.closeDocument(doc.Name)
        old = os.path.join(folder, "PaintOlder.FCStd")
        check("an older file made of it", older(path, old) == [1, 1])
        doc = App.openDocument(old)
        check("an older file read: by name (%r)" % named(doc.Box), named(doc.Box) == both)
        check("its colours in the store (%r)" % stored(doc.Box), stored(doc.Box) == both)
        check("and drawn where they were", shown(doc.Box) == there)

        # A name taken away takes its paint (sec 13.2): in a document just
        # read, where which faces the names painted is not in the file; one
        # of two, then the last; and not the paint of a face coloured by its
        # number, which is no name's.
        vp = doc.Box.ViewObject
        vp.setElementColors({top: RED})
        doc.recompute()
        check("one name taken away, just read: its face is the object's again (%r)" % shown(doc.Box),
              named(doc.Box) == {top: RED} and list(shown(doc.Box).values()) == [RED])
        front = face(doc.Box, XMax=0)
        colors = list(vp.DiffuseColor)
        colors[int(front[4:]) - 1] = BLUE + (1.0,)
        vp.DiffuseColor = colors
        check("a face coloured by its number (%r)" % shown(doc.Box),
              sorted(shown(doc.Box).values()) == sorted([RED, BLUE]))
        vp.setElementColors({})
        doc.recompute()
        check("the last name taken away: that face alone is left (%r)" % shown(doc.Box),
              named(doc.Box) == {} and list(shown(doc.Box).values()) == [BLUE])
        App.closeDocument(doc.Name)

        # Every face painted one colour by name is the object painted: the
        # appearance keeps what all its faces agree on as the object's own
        # (docs/ShapeAppearanceDesign.md sec 12). The six are drawn red and
        # the object is red with them -- they came out the object's colour
        # as it had been, the paint gone, the object's material card having
        # taken the base back -- and a name taken away has that red to go
        # back to.
        doc = App.newDocument("PaintAll")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        vp = doc.Box.ViewObject
        vp.setElementColors({"Face%d" % (i + 1): RED for i in range(6)})
        doc.recompute()
        drawn = sorted({rgb(c) for c in vp.DiffuseColor})
        check("six faces red by name: all drawn red, the object with them (%r, %r)"
              % (drawn, rgb(vp.ShapeColor)),
              drawn == [RED] and rgb(vp.ShapeColor) == RED and len(named(doc.Box)) == 6)
        vp.setElementColors({"Face%d" % (i + 1): RED for i in range(5)})
        doc.recompute()
        drawn = sorted({rgb(c) for c in vp.DiffuseColor})
        check("one taken away: five names, and nothing left as it was not (%r)" % drawn,
              drawn == [RED] and len(named(doc.Box)) == 5)
        App.closeDocument(doc.Name)

        # A whole material by name (sec 13.6 step 4).
        doc = App.newDocument("PaintLook")
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        vp = doc.Box.ViewObject
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        itop, ibottom = int(top[4:]) - 1, int(bottom[4:]) - 1

        def gloss(m):
            return round(m.Shininess, 3)

        def glossed(value):
            # The object's gloss, through the appearance's base. Not through
            # ShapeMaterial: assigned a material that names the card the
            # object wears, in another gloss, that leaves the base as it was
            # (sec 13.7, the third of the trap).
            base = vp.ShapeAppearance.Base
            base.Shininess = value
            vp.ShapeAppearance.Base = base
            doc.recompute()

        own = gloss(vp.ShapeAppearance.Base)
        # A face given a colour is the object as it is then, in that
        # colour, whole: a gloss the object is given after is not the face's.
        paint(doc, "green bottom", "Box", {bottom: GREEN})
        glossed(0.5)
        check("a face given a colour keeps the gloss the object had (%r)" % gloss(vp.ShapeAppearance[ibottom]),
              gloss(vp.ShapeAppearance[ibottom]) == own and rgb(vp.DiffuseColor[ibottom]) == GREEN)
        check("and so its name says (%r)" % gloss(vp.getElementAppearances()[bottom]),
              gloss(vp.getElementAppearances()[bottom]) == own)
        glossed(own)

        doc.openTransaction("matte red top")
        looks = {k: v for k, v in vp.getElementAppearances().items() if k not in ("Face", "Edge", "Vertex")}
        looks[top] = App.Material(DiffuseColor=RED, Shininess=0.25)
        vp.setElementAppearances(looks)
        doc.recompute()
        doc.commitTransaction()
        check("a material by name: drawn with its gloss (%r)" % gloss(vp.ShapeAppearance[itop]),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and rgb(vp.DiffuseColor[itop]) == RED)
        check("the other faces with the object's (%r)" % sorted({gloss(x) for x in vp.ShapeAppearance}),
              sorted({gloss(x) for x in vp.ShapeAppearance}) == sorted({0.25, own}))
        check("read back by name (%r)" % gloss(vp.getElementAppearances()[top]),
              gloss(vp.getElementAppearances()[top]) == 0.25
              and named(doc.Box) == {top: RED, bottom: GREEN})
        check("the object's own look is not the names' (%r)" % gloss(vp.getElementAppearances()["Face"]),
              gloss(vp.getElementAppearances()["Face"]) == own)
        doc.undo()
        check("undone: the face the object's, the first name left (%r)" % named(doc.Box),
              gloss(vp.ShapeAppearance[itop]) == own and named(doc.Box) == {bottom: GREEN})
        doc.redo()
        check("redone (%r)" % gloss(vp.ShapeAppearance[itop]),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and named(doc.Box) == {top: RED, bottom: GREEN})
        # A colour given to a name that has a material changes its colour.
        paint(doc, "blue top", "Box", {top: BLUE})
        check("a colour by name keeps the name's material (%r)" % gloss(vp.ShapeAppearance[itop]),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and rgb(vp.DiffuseColor[itop]) == BLUE)
        there = shown(doc.Box)
        path = os.path.join(folder, "PaintLook.FCStd")
        doc.saveAs(path)
        App.closeDocument(doc.Name)
        doc = App.openDocument(path)
        vp = doc.Box.ViewObject
        check("saved and read: the material by name (%r)" % gloss(vp.getElementAppearances()[top]),
              gloss(vp.getElementAppearances()[top]) == 0.25
              and named(doc.Box) == {top: BLUE, bottom: GREEN})
        check("and drawn as it was (%r)" % gloss(vp.ShapeAppearance[itop]),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and shown(doc.Box) == there)
        # One name alone, and names that agree: a list keeps what all its
        # entries agree on as its base, and the looks are not read from that.
        matte = vp.getElementAppearances()[top]
        vp.setElementAppearances({top: matte})
        doc.recompute()
        check("one name alone keeps its material (%r)" % gloss(vp.ShapeAppearance[itop]),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and gloss(vp.getElementAppearances()[top]) == 0.25)
        vp.setElementAppearances({top: matte, bottom: matte})
        doc.recompute()
        check("two names with one material keep it (%r)" % sorted({gloss(x) for x in vp.ShapeAppearance}),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and gloss(vp.ShapeAppearance[ibottom]) == 0.25
              and sorted({gloss(x) for x in vp.ShapeAppearance}) == sorted({0.25, own}))
        # The object given the gloss a named face has, and then another:
        # the face's is its own through both.
        glossed(0.25)
        glossed(0.6)
        check("the object given that gloss and then another: the faces keep theirs (%r)"
              % sorted({gloss(x) for x in vp.ShapeAppearance}),
              gloss(vp.ShapeAppearance[itop]) == 0.25 and gloss(vp.ShapeAppearance[ibottom]) == 0.25
              and gloss(vp.ShapeAppearance.Base) == 0.6)
        glossed(own)
        vp.setElementAppearances({})
        doc.recompute()
        check("the names taken away: all of the look goes back (%r)" % sorted({gloss(x) for x in vp.ShapeAppearance}),
              {gloss(x) for x in vp.ShapeAppearance} == {own} and shown(doc.Box) == {})
        App.closeDocument(doc.Name)

        # Names out of an older file have colours and no more: each is the
        # object in its colour, whatever the object comes to look like --
        # until the names are written again, whole.
        doc = App.openDocument(old)
        vp = doc.Box.ViewObject
        was = named(doc.Box)
        glossed(0.5)
        check("names from an older file take the object's gloss (%r)" % sorted({gloss(x) for x in vp.ShapeAppearance}),
              {gloss(x) for x in vp.ShapeAppearance} == {0.5}
              and {gloss(v) for v in vp.getElementAppearances().values() if gloss(v) != 1.0} == {0.5}
              and named(doc.Box) == was and len(shown(doc.Box)) == len(was))
        vp.setElementColors(dict(was))
        doc.recompute()
        glossed(0.6)
        check("written again they are whole, and keep it (%r)" % sorted({gloss(x) for x in vp.ShapeAppearance}),
              sorted({gloss(x) for x in vp.ShapeAppearance}) == [0.5, 0.6] and named(doc.Box) == was)
        App.closeDocument(doc.Name)

        # The panel (sec 13.6 step 5): a face given a colour through Set
        # Colors, then a material through its Material... button, and both
        # kept by OK as one step.
        doc = App.newDocument("PaintPanel")
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        vp = doc.Box.ViewObject
        top = face(doc.Box, ZMin=10)
        own = gloss(vp.ShapeAppearance.Base)
        mw = Gui.getMainWindow()

        def modal(act):
            # What a modal dialog is to be told once it is up; closed
            # whatever becomes of that, or the check never comes back.
            def go():
                dlg = QtWidgets.QApplication.activeModalWidget()
                try:
                    act(dlg)
                    dlg.accept()
                except Exception:
                    lines.append("FAIL in a dialog\n" + traceback.format_exc())
                    if dlg:
                        dlg.reject()
            QtCore.QTimer.singleShot(200, go)

        Gui.getDocument(doc.Name).setEdit(doc.Box, 3)
        QtWidgets.QApplication.processEvents()
        rows = mw.findChild(QtWidgets.QListWidget, "elementList")
        button = mw.findChild(QtWidgets.QPushButton, "editMaterial")
        check("the panel is up, with its Material button", rows is not None and button is not None
              and not button.isHidden())
        Gui.Selection.addSelection(doc.Name, "Box", top)
        modal(lambda dlg: dlg.setCurrentColor(QtGui.QColor.fromRgbF(*RED)))
        mw.findChild(QtWidgets.QPushButton, "addSelection").click()
        check("a face coloured through the panel (%r)" % named(doc.Box), named(doc.Box) == {top: RED})
        check("with the object's gloss (%r)" % gloss(vp.getElementAppearances()[top]),
              gloss(vp.getElementAppearances()[top]) == own)
        for item in rows.findItems(top, QtCore.Qt.MatchExactly):
            item.setSelected(True)
        modal(lambda dlg: dlg.findChild(QtWidgets.QSpinBox, "shininess").setValue(25))
        button.click()
        check("a material through its button: drawn (%r)" % gloss(vp.ShapeAppearance[int(top[4:]) - 1]),
              gloss(vp.ShapeAppearance[int(top[4:]) - 1]) == 0.25)
        check("and by name, in the colour it had (%r)" % named(doc.Box),
              gloss(vp.getElementAppearances()[top]) == 0.25 and named(doc.Box) == {top: RED})
        check("the object's own look left alone (%r)" % gloss(vp.ShapeAppearance.Base),
              gloss(vp.ShapeAppearance.Base) == own)
        for box in mw.findChildren(QtWidgets.QDialogButtonBox):
            ok = box.button(QtWidgets.QDialogButtonBox.Ok)
            if ok is not None and box.isVisible():
                ok.click()
                break
        QtWidgets.QApplication.processEvents()
        check("kept by OK (%r)" % named(doc.Box),
              named(doc.Box) == {top: RED} and gloss(vp.getElementAppearances()[top]) == 0.25)
        doc.undo()
        check("and one step to undo (%r)" % named(doc.Box),
              named(doc.Box) == {} and gloss(vp.ShapeAppearance[int(top[4:]) - 1]) == own)
        App.closeDocument(doc.Name)

        # A face takes its source face's material (sec 13.6 step 6): the cut
        # is the box here, the cylinder below it, and each of its faces is
        # one of the box's.
        doc = cut("PaintHand", folder)
        vpb, vpc = doc.Box.ViewObject, doc.Cut.ViewObject
        side = face(doc.Box, XMin=10)
        iside = int(face(doc.Cut, XMin=10)[4:]) - 1
        own = gloss(vpc.ShapeAppearance.Base)
        vpb.setElementAppearances({side: App.Material(DiffuseColor=RED, Shininess=0.25)})
        doc.recompute()
        check("a face made from one with a material has it (%r, %r)"
              % (gloss(vpc.ShapeAppearance[iside]), rgb(vpc.DiffuseColor[iside])),
              gloss(vpc.ShapeAppearance[iside]) == 0.25 and rgb(vpc.DiffuseColor[iside]) == RED)
        check("and no other face of it (%r)" % sorted({gloss(x) for x in vpc.ShapeAppearance}),
              sorted({gloss(x) for x in vpc.ShapeAppearance}) == sorted({0.25, own}))
        vpb.setElementAppearances({})
        doc.recompute()
        vpb.setElementColors({side: BLUE})
        doc.recompute()
        check("from one that was only painted: its colour, and the gloss its own (%r, %r)"
              % (gloss(vpc.ShapeAppearance[iside]), rgb(vpc.DiffuseColor[iside])),
              gloss(vpc.ShapeAppearance[iside]) == own and rgb(vpc.DiffuseColor[iside]) == BLUE)
        vpb.setElementColors({})
        doc.recompute()
        check("the source's name taken away: nothing of it left (%r)" % shown(doc.Cut),
              {gloss(x) for x in vpc.ShapeAppearance} == {own} and shown(doc.Cut) == {})
        App.closeDocument(doc.Name)

        # A version restored brings the names with their looks (sec 13.2
        # item 3): the names are the object's and came alone, a view
        # provider's values being left as they are unless
        # ViewObjectTransaction is set.
        doc = App.newDocument("PaintRestore")
        doc.UndoMode = 1
        doc.openTransaction("base")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        doc.saveAs(os.path.join(folder, "PaintRestore.FCStd"))
        vp = doc.Box.ViewObject
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        paint(doc, "green bottom", "Box", {bottom: GREEN})
        doc.openTransaction("matte red top")
        looks = {k: v for k, v in vp.getElementAppearances().items() if k not in ("Face", "Edge", "Vertex")}
        looks[top] = App.Material(DiffuseColor=RED, Shininess=0.25)
        vp.setElementAppearances(looks)
        doc.recompute()
        doc.commitTransaction()
        both = {top: RED, bottom: GREEN}
        there = shown(doc.Box)
        doc.save()
        version = doc.getTransactionVersions()[-1]["num"]
        doc.openTransaction("none")
        vp.setElementAppearances({})
        doc.recompute()
        doc.commitTransaction()
        check("the names taken away after a version (%r)" % named(doc.Box),
              named(doc.Box) == {} and shown(doc.Box) == {})
        check("view state is not undo state here",
              not App.ParamGet("User parameter:BaseApp/Preferences/Document").GetBool("ViewObjectTransaction", False))
        doc.restoreTransactionVersion(version)
        doc.recompute()
        check("the version restored: the names with their colours (%r)" % named(doc.Box),
              named(doc.Box) == both and stored(doc.Box) == both)
        check("the material with its name (%r)" % gloss(vp.getElementAppearances()[top]),
              gloss(vp.getElementAppearances()[top]) == 0.25)
        check("and drawn as the version had them (%r)" % shown(doc.Box), shown(doc.Box) == there)
        App.closeDocument(doc.Name)

        # Painted on both branches (docs/TransactionLog.md sec 31.20): the
        # names and their looks are one thing, merged by name.
        def both(name):
            d = App.newDocument(name)
            d.UndoMode = 1
            d.openTransaction("base")
            d.addObject("Part::Box", "Box")
            d.recompute()
            d.commitTransaction()
            d.saveAs(os.path.join(folder, name + ".FCStd"))
            return d

        def asked(pv):
            return [(c["kind"], c["key"]) for c in pv["changes"] if c["kind"] in ("conflict", "unit")]

        doc = both("PaintBoth")
        top, bottom = face(doc.Box, ZMin=10), face(doc.Box, ZMax=0)
        itop = int(top[4:]) - 1
        doc.createTransactionBranch("side")
        doc.openTransaction("side matte red top")
        doc.Box.ViewObject.setElementAppearances({top: App.Material(DiffuseColor=RED, Shininess=0.25)})
        doc.recompute()
        doc.commitTransaction()
        doc.switchTransactionBranch("main")
        paint(doc, "main green bottom", "Box", {bottom: GREEN})
        pv = doc.previewTransactionMerge("side")
        check("each branch painted another face: nothing asked (%r)" % asked(pv), pv["conflicts"] == 0)
        merged = doc.mergeTransactionBranch("side")
        check("and merged", (merged["unresolved"], merged["failed"]) == ([], []))
        doc.recompute()
        vp = doc.Box.ViewObject
        check("both by name (%r)" % named(doc.Box), named(doc.Box) == {top: RED, bottom: GREEN})
        check("theirs' with its material (%r)" % gloss(vp.getElementAppearances()[top]),
              gloss(vp.getElementAppearances()[top]) == 0.25 and gloss(vp.ShapeAppearance[itop]) == 0.25)
        check("and both drawn (%r)" % shown(doc.Box), sorted(shown(doc.Box).values()) == sorted([RED, GREEN]))
        doc.undo()
        check("the merge undone: ours alone (%r)" % named(doc.Box),
              named(doc.Box) == {bottom: GREEN} and list(shown(doc.Box).values()) == [GREEN])
        doc.redo()
        check("and redone (%r)" % named(doc.Box),
              named(doc.Box) == {top: RED, bottom: GREEN} and len(shown(doc.Box)) == 2)
        App.closeDocument(doc.Name)

        # One face painted on both, and another taken away by theirs: the
        # first is ours unless the setting says otherwise, the second goes.
        def same(name):
            d = both(name)
            paint(d, "green bottom", "Box", {face(d.Box, ZMax=0): GREEN})
            d.createTransactionBranch("side")
            d.openTransaction("side red top, no bottom")
            d.Box.ViewObject.setElementColors({face(d.Box, ZMin=10): RED})
            d.recompute()
            d.commitTransaction()
            d.switchTransactionBranch("main")
            paint(d, "main blue top", "Box", {face(d.Box, ZMin=10): BLUE})
            return d

        doc = same("PaintSame")
        top = face(doc.Box, ZMin=10)
        setting = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        try:
            pv = doc.previewTransactionMerge("side")
            check("one face painted by both: nothing asked (%r)" % asked(pv), pv["conflicts"] == 0)
            setting.SetString("TransactionLogMergeFacePaint", "asked")
            pv = doc.previewTransactionMerge("side")
            check("asked, where the setting says to ask (%r)" % asked(pv),
                  pv["conflicts"] == 1 and ("conflict", "Box.ColoredElements") in asked(pv))
            setting.SetString("TransactionLogMergeFacePaint", "theirs")
            pv = doc.previewTransactionMerge("side")
            check("theirs, where it says theirs: nothing asked (%r)" % asked(pv), pv["conflicts"] == 0)
            setting.RemString("TransactionLogMergeFacePaint")
            merged = doc.mergeTransactionBranch("side")
            check("merged as it is set by default", (merged["unresolved"], merged["failed"]) == ([], []))
            doc.recompute()
            check("ours' colour on the face both painted, theirs' removal taken (%r)" % named(doc.Box),
                  named(doc.Box) == {top: BLUE} and list(shown(doc.Box).values()) == [BLUE])
            App.closeDocument(doc.Name)
            doc = same("PaintSameTheirs")
            setting.SetString("TransactionLogMergeFacePaint", "theirs")
            merged = doc.mergeTransactionBranch("side")
            doc.recompute()
            check("and with the setting theirs, theirs' (%r)" % named(doc.Box),
                  (merged["unresolved"], merged["failed"]) == ([], [])
                  and named(doc.Box) == {top: RED} and list(shown(doc.Box).values()) == [RED])
        finally:
            setting.RemString("TransactionLogMergeFacePaint")
        App.closeDocument(doc.Name)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
