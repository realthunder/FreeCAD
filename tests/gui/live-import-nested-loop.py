"""The live-import nested-loop crash, made deterministic.

A user command runs inside App::Document::UserEditGuard, and a command
that runs a nested event loop -- the animated view fit ImportGui.insert
runs itself, a modal dialog -- lets the tree view's status timer fire
inside that guard while a progressive import is still filling the
document (App::Document::LiveImport). The tree's populate writes
TreeRank. Before 2026-09-05 the guard judged that write as the command's
edit and threw, unwinding DocumentItem::createNewItem between rootItem
being set and the item being inserted, and the next tick walked the
dangling item: SIGSEGV in DocumentObjectItem::getParentItem. The fix
exempts TreeRank by identity in App::Document::checkUserEdit
(docs/DocumentLoad.md sec 15.2).

The render goldens found it by accident, under load, inside the
ten-frame animated fit -- and then stopped finding it, because a golden
must not animate. This test opens the same window on purpose and wide: a
command registered here pumps a nested QEventLoop for three seconds,
invoked from a timer that waits for the document to carry LiveImport, so
the tree's timer fires inside the guard however fast or slow the box is.
Navigation animation is left ON: the import's own fit is how a user
meets this.

The document is held live through Gui.setLiveImport around the import,
the way a Python progressive importer holds it: the load's own claim on
LiveImport ends with its visual drain, and on an idle box the chess set
drains before the tree's tick, so the rank is written to a document that
is no longer live and the guard has nothing to judge (which is why the
crash needed three heavy tests in parallel to show). The hold makes the
window's end deterministic; the import still supplies the objects and
runs its animated fit.

What is asserted, so that the test cannot pass without having reached
the mechanism:
  - the window was open: the document carried LiveImport when the
    command started AND when its loop ended, the tree created items
    INSIDE the loop, and it rewrote TreeRank doing so -- the write the
    guard judges. The tree
    only renumbers once it has connected the document's change signal,
    which it does at the end of its first tick over that document, so
    the document is seeded with one object and the import waits until
    the tree shows it (a loop run over a tree that has not connected,
    or one already populated, proves nothing);
  - no user-edit refusal was reported (a console observer catches the
    "still being filled in" warning in-process, so no log buffering can
    hide it) and the Python command itself ran without error;
  - after the import and its visual drain finished, and enough ticks for
    a dangling item to be walked, the tree holds one item per object;
  - and the process lived to write DONE.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt. Proven by
reverting the TreeRank exemption: the run then reports the refusal and
dies on the next tick.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GLB = os.path.join(REPO, "src/3rdParty/MaterialX/resources/Geometry/chess_set.glb")
DOC = "LiveImportNestedLoop"
CMD = "Test_LiveImportNestedLoop"
SEED = "Seed"
LOOP_MS = 3000
# The tree coalesces new objects on a timer that restarts at every
# arrival. Longer than the default 100 ms so that an import pumping
# events between objects does not itemise them piecemeal before the
# command gets to run; well short of the loop, so that it fires inside.
TREE_TICK_MS = 500
WINDOW_WAIT_S = 60
DRAIN_WAIT_S = 240

state = {
    "doc": None,
    "reports": [],
    "cmd_error": None,
    "loop_entered": False,
    "live_at_start": None,
    "live_at_end": None,
    "held": False,
    "import_returned": False,
    "items_before": None,
    "items_after": None,
    "ranks_before": None,
    "ranks_after": None,
    "t0": time.monotonic(),
}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def observe(notifier, msg, level):
    # The refusal names the object and property; the command failure is
    # what PythonCommand::activated reports when the loop is unwound.
    if "still being filled in" in msg or "Running the Python command" in msg:
        state["reports"].append("%s: %s" % (level, msg.strip()))


def tree_labels(doc):
    """Labels of every item under the document's item in the first tree
    widget that shows the document, or None if no tree does."""
    mw = FreeCADGui.getMainWindow()
    seen = []
    for tree in mw.findChildren(QtWidgets.QTreeWidget):
        seen.append("%s(%s):%s" % (
            tree.metaObject().className(), tree.objectName(),
            [tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())]))
        if tree.metaObject().className() != "Gui::TreeWidget":
            continue
        # Document items hang under the tree's own "Application" root.
        roots = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())]
        for top in [c for r in roots for c in (r.child(j) for j in range(r.childCount()))]:
            # A modified document's item reads "Label *".
            if top.text(0) not in (doc.Label, doc.Label + " *"):
                continue
            labels = []
            stack = [top.child(j) for j in range(top.childCount())]
            while stack:
                item = stack.pop()
                labels.append(item.text(0))
                stack.extend(item.child(j) for j in range(item.childCount()))
            return labels
    note("no tree shows the document; trees seen: " + "; ".join(seen))
    return None


def ranks(doc):
    return {o.Name: o.TreeRank for o in doc.Objects}


class NestedLoopCommand:
    """A command whose whole action is to run a nested event loop, the
    way an animated fit or a modal dialog does, inside the user-edit
    guard Command::_invoke holds around Activated."""

    def GetResources(self):
        return {"MenuText": "Nested event loop (test)",
                "ToolTip": "Pumps a nested QEventLoop for a few seconds"}

    def Activated(self):
        try:
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(LOOP_MS, loop.quit)
            state["loop_entered"] = True
            run = getattr(loop, "exec", None) or loop.exec_
            run()
        except Exception:
            state["cmd_error"] = traceback.format_exc()


def fire():
    """Runs the command once the window is open: the document carries
    LiveImport, and the import has created its objects (Restoring is
    the bit it holds while it does) with the tree yet to itemise them.
    Too early -- a progress pump inside the import with one object made
    -- and the tick inside the loop has nothing to renumber, which is
    what the first parallel ctest run hit."""
    doc = state["doc"]
    if not (doc.LiveImport and not doc.Restoring and len(doc.Objects) > 1):
        if time.monotonic() - state["t0"] > WINDOW_WAIT_S:
            check("window opened", False,
                  "LiveImport=%s Restoring=%s objects=%d after %ds"
                  % (doc.LiveImport, doc.Restoring, len(doc.Objects),
                     WINDOW_WAIT_S))
            finish()
            return
        QtCore.QTimer.singleShot(10, fire)
        return
    state["live_at_start"] = doc.LiveImport
    labels = tree_labels(doc)
    state["items_before"] = len(labels) if labels is not None else -1
    state["ranks_before"] = ranks(doc)
    FreeCADGui.runCommand(CMD)
    state["live_at_end"] = doc.LiveImport
    labels = tree_labels(doc)
    state["items_after"] = len(labels) if labels is not None else -1
    state["ranks_after"] = ranks(doc)
    state["t_loop"] = time.monotonic()
    QtCore.QTimer.singleShot(200, settle)


def settle():
    """Waits for the import and its visual drain to end (LiveImport
    clears with the drain), then for a few more tree ticks: the crash
    was on the tick AFTER the refusal."""
    doc = state["doc"]
    release_hold()
    if doc.LiveImport or doc.Restoring:
        if time.monotonic() - state["t0"] > DRAIN_WAIT_S:
            check("import and drain finished", False,
                  "LiveImport=%s Restoring=%s after %ds"
                  % (doc.LiveImport, doc.Restoring, DRAIN_WAIT_S))
            finish()
            return
        QtCore.QTimer.singleShot(200, settle)
        return
    QtCore.QTimer.singleShot(4 * TREE_TICK_MS, verify)


def release_hold():
    """Gives the live-import hold back once both the import and the
    command have returned; the tick that matters has happened by then."""
    doc = state["doc"]
    if state["held"] and state["import_returned"] and not doc.Restoring:
        FreeCADGui.setLiveImport(doc, False)
        state["held"] = False


def verify():
    doc = state["doc"]
    check("document carried LiveImport when the command started",
          state["live_at_start"] is True)
    check("document still carried LiveImport when the loop ended",
          state["live_at_end"] is True)
    check("the command entered its nested loop", state["loop_entered"])
    check("the command ran without a Python error", state["cmd_error"] is None,
          state["cmd_error"] or "")
    before, after = state["items_before"], state["items_after"]
    check("the tree populated inside the nested loop",
          before is not None and after is not None and after > before,
          "items before %s, after %s" % (before, after))
    rb, ra = state["ranks_before"], state["ranks_after"]
    rewritten = [n for n in (rb or {}) if n in (ra or {}) and ra[n] != rb[n]]
    check("the tree rewrote TreeRank inside the nested loop", bool(rewritten),
          "%d of %d objects renumbered" % (len(rewritten), len(rb or {})))
    check("no user-edit refusal or command failure was reported",
          not state["reports"], "; ".join(state["reports"]))
    labels = tree_labels(doc)
    want = sorted(o.Label for o in doc.Objects)
    got = sorted(labels) if labels is not None else None
    check("one tree item per object", got == want,
          "%s objects, %s items" % (len(want), None if got is None else len(got)))
    finish()


def finish():
    try:
        FreeCAD.Console.DetachObserver(observe)
    except Exception:
        pass
    try:
        if state["held"]:
            FreeCADGui.setLiveImport(state["doc"], False)
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        FreeCAD.ParamGet(
            "User parameter:BaseApp/Preferences/NotificationArea").SetBool(
            "NonIntrusiveNotificationsEnabled", False)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TreeView").SetInt(
            "StatusTimeout", TREE_TICK_MS)
        FreeCAD.Console.AttachObserver(observe)
        FreeCADGui.addCommand(CMD, NestedLoopCommand())

        doc = FreeCAD.newDocument(DOC)
        state["doc"] = doc
        FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
        note("platform " + QtWidgets.QApplication.platformName())
        # One object, so that the tree ticks over this document before
        # the import and connects it (see the module docstring).
        doc.addObject("App::DocumentObjectGroup", SEED)
        wait_tree()
    except Exception:
        note("FAIL run:\n" + traceback.format_exc())
        finish()


def wait_tree():
    doc = state["doc"]
    labels = tree_labels(doc)
    if not labels or SEED not in labels:
        if time.monotonic() - state["t0"] > WINDOW_WAIT_S:
            check("the tree showed the seed object", False, str(labels))
            finish()
            return
        QtCore.QTimer.singleShot(50, wait_tree)
        return
    start_import()


def start_import():
    doc = state["doc"]
    try:
        import ImportGui

        # Armed BEFORE the import so that its deadline precedes the
        # tree's (which restarts at every new object): it fires in the
        # first event-loop turn after the objects exist -- the animated
        # fit the import runs while the load is still live, or the turn
        # after it returns -- and the tree's tick lands inside the loop
        # it opens. Animation stays on.
        QtCore.QTimer.singleShot(0, fire)
        # Held the way a Python progressive importer holds it, so that the
        # document is still live at the tree's tick however fast the box
        # drains the visuals: the load's own claim on LiveImport ends
        # with its visual drain, which on an idle box is over before the
        # tick (that is why the crash needed load to show).
        FreeCADGui.setLiveImport(doc, True)
        state["held"] = True
        ImportGui.insert(GLB, doc.Name)
        state["import_returned"] = True
        note("imported %d objects, LiveImport=%s" % (len(doc.Objects), doc.LiveImport))
    except Exception:
        state["import_returned"] = True
        note("FAIL import:\n" + traceback.format_exc())
        finish()


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
