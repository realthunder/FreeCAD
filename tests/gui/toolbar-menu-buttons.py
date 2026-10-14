"""A tool bar's menu buttons have their mark in a strip beside the icon, each of them.

docs/HandsOnQueue.md entry 90: "Std_SelUp toolbar icon expansion indicator
overlaps with the icon, unlike others", and entry 89: "Std_DisplayStyle
toolbar icon seem to have some triangle overlaid inside ... a lot smaller
than normal expansion arrow".

The style sheets give a menu button the room for its strip by the button's
name, qt_toolbutton_menubutton. The helper that names it was called BEFORE
the action was added and took the tool bar's last button: the button of
the action added before. In a run of menu buttons each was named by the
next and the last by none -- Std_SelUp, after the two selection history
buttons. Std_DisplayStyle did not use the helper: it opened its menu
instantly, a mode in which Qt draws a small mark on the icon.

Claims, for the tool bars as the start-up workbench has them:
  - every tool bar button whose action has a menu is a menu button
    (QToolButton::MenuButtonPopup) and has the name;
  - Std_SelUp and Std_DisplayStyle are among them, and as wide as the
    selection history's "back" button beside them: the icon and the strip;
  - no button whose action has NO menu has the name: it would be drawn
    with a strip that opens nothing;
  - a click on Std_DisplayStyle's button opens its menu, as before.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
NAME = "qt_toolbutton_menubutton"
MENU_BUTTON = QtWidgets.QToolButton.MenuButtonPopup


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def mw():
    return FreeCADGui.getMainWindow()


def label(button):
    action = button.defaultAction()
    text = (action.text() if action is not None else "").replace("&", "")
    return text or button.toolTip()[:30] or "?"


def has_menu(button):
    """A menu of the button's own (a group of actions gives it one) or of its action"""
    action = button.defaultAction()
    return button.menu() is not None or (action is not None and action.menu() is not None)


def bar_buttons():
    """(tool bar, button) of every button a visible tool bar shows for an action"""
    out = []
    for bar in mw().findChildren(QtWidgets.QToolBar):
        if not bar.isVisible():
            continue
        for action in bar.actions():
            widget = bar.widgetForAction(action)
            if isinstance(widget, QtWidgets.QToolButton) and widget.isVisible():
                out.append((bar, widget))
    return out


def button_of(command):
    actions = FreeCADGui.Command.get(command).getAction()
    for bar, button in bar_buttons():
        if button.defaultAction() in actions:
            return button
    return None


def run():
    buttons = bar_buttons()
    with_menu = [(bar, b) for bar, b in buttons if has_menu(b)]
    check("the tool bars have menu buttons to look at", len(with_menu) >= 5, len(with_menu))

    wrong_mode = ["%s/%s" % (bar.objectName(), label(b)) for bar, b in with_menu
                  if b.popupMode() != MENU_BUTTON]
    check("every button whose action has a menu is a menu button", not wrong_mode, wrong_mode)
    unnamed = ["%s/%s" % (bar.objectName(), label(b)) for bar, b in with_menu
               if b.objectName() != NAME]
    check("and has the name the sheets give the strip its room by", not unnamed, unnamed)

    stray = ["%s/%s" % (bar.objectName(), label(b)) for bar, b in buttons
             if b.objectName() == NAME and not has_menu(b)]
    check("no button without a menu has that name", not stray, stray)

    back = button_of("Std_SelBack")
    for command in ("Std_SelUp", "Std_DisplayStyle"):
        button = button_of(command)
        if not check("%s has a button in a tool bar" % command, button is not None):
            continue
        check("%s: a menu button with the name" % command,
              button.popupMode() == MENU_BUTTON and button.objectName() == NAME,
              "%s, %r" % (button.popupMode(), button.objectName()))
        if back is not None:
            check("%s: as wide as the selection history's back button, icon and strip" % command,
                  button.width() == back.width(),
                  "%d against %d" % (button.width(), back.width()))
        image = QtGui.QImage(button.size(), QtGui.QImage.Format_ARGB32)
        image.fill(0)
        button.render(image)
        image.save(os.path.join(OUT, command + ".png"))
    if back is not None:
        image = QtGui.QImage(back.size(), QtGui.QImage.Format_ARGB32)
        image.fill(0)
        back.render(image)
        image.save(os.path.join(OUT, "Std_SelBack.png"))

    # a click on the Display style button opens its menu: nothing else for it to do
    doc = FreeCAD.newDocument("MenuButtons")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(800)
    button = button_of("Std_DisplayStyle")
    seen = {}

    def look():
        popup = QtWidgets.QApplication.activePopupWidget()
        seen["menu"] = popup.objectName() if popup is not None else None
        if popup is not None:
            popup.close()

    if button is not None and button.isEnabled():
        QtCore.QTimer.singleShot(700, look)
        button.click()
        settle(400)
        check("a click on Std_DisplayStyle's button opens its menu",
              seen.get("menu") == "DisplayStyleMenu", seen.get("menu"))
    else:
        check("Std_DisplayStyle's button can be pressed with a 3D view open", False,
              button is not None)


def start():
    try:
        run()
    except Exception:
        note("ABORT: " + traceback.format_exc().replace("\n", " | "))
    for popup in (QtWidgets.QApplication.activePopupWidget(),):
        if popup is not None:
            popup.close()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(3000, start)
