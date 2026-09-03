#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The sandbox GUI porting linter (docs/SandboxGui.md, G0 / U7).

Walks Python workbench code and lists, per file, every PySide, pivy and
FreeCADGui use, sorted into the buckets of docs/SandboxGui.md sec 4:

    subset        the U7 compatibility module covers it: no edit needed
    U1            registration data (addCommand, addWorkbench, ...)
    U2            host services (dialogs, defer, cursor, clipboard, hints)
    U3            forms (.ui as a model-sync proxy tree; hand-built widget
                  trees rewritten as .ui)
    U4            selection, view, edit state, runCommand
    U4.doCommand  the eval primitive with its own permission
    U5            annotation scene primitives (replaces pivy nodes)
    U6            interactive tools: snapper, trackers, 3D event stream
    unmapped      nothing in sec 4 covers it: the residue

The mapping table (RULES below) is the protocol's allowlist as it stands
in the document; whatever lands in `unmapped` is what the document may
have missed.  Run without arguments from anywhere in the tree to lint
src/Mod/Draft and src/Mod/BIM.

    sandbox_gui_lint.py [ROOT ...] [--all] [--list FILE] [--ui]
                        [--json OUT] [--surface] [--self-test]

Pure standard library; no FreeCAD needed.  Python >= 3.10 (Draft and BIM use
`match` statements, which an older parser rejects as a syntax error); on the
dev box that is `.conda/freecad/bin/python3`.
"""

import argparse
import ast
import collections
import json
import os
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

BUCKETS = ["subset", "U1", "U2", "U3", "U4", "U4.doCommand", "U5", "U6", "unmapped"]

# Widget classes that a .ui file expresses; hand-built uses of them are the
# U3 rewrite (the widget tree becomes a .ui, the logic stays).
UI_WIDGETS = """
QWidget QDialog QFrame QGroupBox QLabel QPushButton QToolButton QRadioButton
QCheckBox QLineEdit QTextEdit QPlainTextEdit QComboBox QSpinBox QDoubleSpinBox
QSlider QProgressBar QTreeWidget QTreeWidgetItem QTableWidget QTableWidgetItem
QListWidget QListWidgetItem QTabWidget QStackedWidget QScrollArea QSplitter
QHBoxLayout QVBoxLayout QGridLayout QFormLayout QBoxLayout QLayout QSpacerItem
QSizePolicy QDialogButtonBox QToolBar QAction QMenu QMenuBar QHeaderView
QAbstractItemView QButtonGroup QDateEdit QTimeEdit QDateTimeEdit QFontComboBox
QColorDialog QFontDialog QToolBox QCalendarWidget QLCDNumber QDial QKeySequenceEdit
QTextBrowser QCommandLinkButton QMdiSubWindow QWizard QWizardPage
""".split()

# Pure value types the compatibility module can provide in Python.
VALUE_TYPES = """
QSize QSizeF QPoint QPointF QRect QRectF QUrl QRegularExpression QRegExp QLocale
QDate QTime QDateTime QMargins QKeySequence QColor QFont QVariant QLineF QLine
""".split()

# Annotation scene primitives of sec 4 U5, keyed by the Coin node they replace.
COIN_PRIMITIVES = {
    "SoSeparator": "group",
    "SoGroup": "group",
    "SoAnnotation": "group (overlay)",
    "SoSwitch": "switch/visibility",
    "SoTransform": "transform",
    "SoMatrixTransform": "transform",
    "SoCoordinate3": "vertex data",
    "SoVertexProperty": "vertex data",
    "SoLineSet": "polyline",
    "SoIndexedLineSet": "polyline",
    "SoPointSet": "marker set",
    "SoMarkerSet": "marker set",
    "SoFaceSet": "face set",
    "SoIndexedFaceSet": "face set",
    "SoAsciiText": "text (world)",
    "SoText2": "text (screen)",
    "SoFont": "text font",
    "SoMaterial": "color/material",
    "SoBaseColor": "color/material",
    "SoMaterialBinding": "color/material",
    "SoDrawStyle": "line style",
    "SoPickStyle": "pick style",
    "SoShapeHints": "face set (winding)",
    "SoLightModel": "color/material (unlit)",
    "SoTexture2": "image",
    "SoTexture2Transform": "image",
    "SoTextureCoordinatePlane": "image",
    "SoSFImage": "image",
    "SoSphere": "marker set (3D marker)",
    "SoCube": "face set (box)",
    "SoCone": "arrow/dimension (arrow head)",
    "SoClipPlane": "section plane (C++ candidate)",
    "SoNormal": "vertex data",
    "SoNormalBinding": "vertex data",
    "SoComplexity": "line style",
    "SoLineHighlightRenderAction": "pick style",
}

COIN_EVENTS = {
    "SoMouseButtonEvent",
    "SoLocation2Event",
    "SoKeyboardEvent",
    "SoButtonEvent",
    "SoEvent",
    "SoEventCallback",
    "SoMotion3Event",
    "SoSpaceballButtonEvent",
}

# View methods reached through a stored view object (self.view.addEventCallback):
# the root is a local variable, so these are matched by attribute name alone.
BARE_METHODS = {
    "addEventCallback": ("U6", "tool.events (subscribe)"),
    "addEventCallbackPivy": ("U6", "tool.events (subscribe)"),
    "removeEventCallback": ("U6", "tool.events (unsubscribe)"),
    "removeEventCallbackPivy": ("U6", "tool.events (unsubscribe)"),
    "getSceneGraph": ("unmapped", "scene graph handle: U5 owns the scene, no raw access"),
    "getCameraNode": ("U4", "view.camera (rewrite: Coin camera node to camera get/set)"),
    "getViewer": ("unmapped", "viewer widget handle (Quarter): no protocol equivalent"),
    "getActiveObject": ("U4", "view.activeObject"),
    "setActiveObject": ("U4", "view.activeObject"),
    "getObjectInfo": ("U4", "view.pick"),
    "getObjectsInfo": ("U4", "view.pick"),
    "getCursorPos": ("U4", "view.cursor"),
    "getViewDirection": ("U4", "view.camera"),
    "fitAll": ("U4", "view.fit"),
    "viewTop": ("U4", "view.orient"),
    "viewIsometric": ("U4", "view.orient"),
    "viewFront": ("U4", "view.orient"),
    "viewAxonometric": ("U4", "view.orient"),
}

# The mapping table: canonical chain prefix -> (bucket, protocol op / note).
# A trailing "()" means "only when the chain itself is called" (construction
# of a class, as opposed to a static member of it).  Longest prefix wins.
RULES = {
    # ---- the U7 compatibility subset: data-level Qt -------------------------
    "QApplication.translate": ("subset", "i18n.translate"),
    "QCoreApplication.translate": ("subset", "i18n.translate"),
    "QT_TRANSLATE_NOOP": ("subset", "i18n.noop"),
    "QT_TRANSLATE_NOOP_UTF8": ("subset", "i18n.noop"),
    "QApplication.UnicodeUTF8": ("subset", "i18n.translate (ignored arg)"),
    "QIcon": ("subset", "icon (resource reference)"),
    "QIcon.fromTheme": ("subset", "icon (theme reference)"),
    "QPixmap()": ("subset", "icon (resource reference)"),
    "Qt": ("subset", "Qt enum value"),
    "QMessageBox.question": ("subset", "dialog.question"),
    "QMessageBox.information": ("subset", "dialog.message"),
    "QMessageBox.warning": ("subset", "dialog.message"),
    "QMessageBox.critical": ("subset", "dialog.message"),
    "QMessageBox.about": ("subset", "dialog.message"),
    "QMessageBox.Yes": ("subset", "dialog button enum"),
    "QMessageBox.No": ("subset", "dialog button enum"),
    "QMessageBox.Ok": ("subset", "dialog button enum"),
    "QMessageBox.Cancel": ("subset", "dialog button enum"),
    "QMessageBox.Save": ("subset", "dialog button enum"),
    "QMessageBox.Discard": ("subset", "dialog button enum"),
    "QMessageBox.Close": ("subset", "dialog button enum"),
    "QMessageBox.Abort": ("subset", "dialog button enum"),
    "QMessageBox.Retry": ("subset", "dialog button enum"),
    "QMessageBox.Ignore": ("subset", "dialog button enum"),
    "QMessageBox.YesToAll": ("subset", "dialog button enum"),
    "QMessageBox.NoToAll": ("subset", "dialog button enum"),
    "QMessageBox.StandardButton": ("subset", "dialog button enum"),
    "QMessageBox.Warning": ("subset", "dialog icon enum"),
    "QMessageBox.Information": ("subset", "dialog icon enum"),
    "QMessageBox.Critical": ("subset", "dialog icon enum"),
    "QMessageBox.Question": ("subset", "dialog icon enum"),
    "QInputDialog.getText": ("subset", "dialog.input (text)"),
    "QInputDialog.getItem": ("subset", "dialog.input (choice)"),
    "QInputDialog.getInt": ("subset", "dialog.input (int)"),
    "QInputDialog.getDouble": ("subset", "dialog.input (float)"),
    "QInputDialog.getMultiLineText": ("subset", "dialog.input (text)"),
    "QFileDialog.getOpenFileName": ("subset", "dialog.file (fs grant)"),
    "QFileDialog.getOpenFileNames": ("subset", "dialog.file (fs grant)"),
    "QFileDialog.getSaveFileName": ("subset", "dialog.file (fs grant)"),
    "QFileDialog.getExistingDirectory": ("subset", "dialog.file (fs grant)"),
    "QColorDialog.getColor": ("subset", "dialog.color"),
    "QFontDialog.getFont": ("subset", "dialog.font"),
    "QTimer.singleShot": ("subset", "defer"),
    "QApplication.setOverrideCursor": ("subset", "cursor.override"),
    "QApplication.restoreOverrideCursor": ("subset", "cursor.restore"),
    "QApplication.clipboard": ("subset", "clipboard (grant)"),
    "QApplication.processEvents": ("subset", "no-op (host owns the loop)"),
    "QCoreApplication.processEvents": ("subset", "no-op (host owns the loop)"),
    "QDialogButtonBox.Ok": ("subset", "standard button enum"),
    "QDialogButtonBox.Cancel": ("subset", "standard button enum"),
    "QDialogButtonBox.Close": ("subset", "standard button enum"),
    "QDialogButtonBox.Apply": ("subset", "standard button enum"),
    "QDialogButtonBox.Reset": ("subset", "standard button enum"),
    "QDialogButtonBox.Help": ("subset", "standard button enum"),
    "QDialogButtonBox.NoButton": ("subset", "standard button enum"),
    "QDialogButtonBox.StandardButton": ("subset", "standard button enum"),
    "QAbstractItemView.SelectRows": ("subset", "Qt enum value"),
    "QAbstractItemView.ExtendedSelection": ("subset", "Qt enum value"),
    "QAbstractItemView.SingleSelection": ("subset", "Qt enum value"),
    "QAbstractItemView.MultiSelection": ("subset", "Qt enum value"),
    "QAbstractItemView.NoEditTriggers": ("subset", "Qt enum value"),
    "QHeaderView.ResizeToContents": ("subset", "Qt enum value"),
    "QHeaderView.Stretch": ("subset", "Qt enum value"),
    "QHeaderView.Interactive": ("subset", "Qt enum value"),
    "QSizePolicy.Expanding": ("subset", "Qt enum value"),
    "QSizePolicy.Fixed": ("subset", "Qt enum value"),
    "QSizePolicy.Minimum": ("subset", "Qt enum value"),
    "QSizePolicy.Preferred": ("subset", "Qt enum value"),
    "QSizePolicy.MinimumExpanding": ("subset", "Qt enum value"),
    "QFrame.HLine": ("subset", "Qt enum value"),
    "QFrame.VLine": ("subset", "Qt enum value"),
    "QFrame.Sunken": ("subset", "Qt enum value"),
    "QLineEdit.Normal": ("subset", "Qt enum value"),
    "QFont.Bold": ("subset", "Qt enum value"),
    "QComboBox.AdjustToContents": ("subset", "Qt enum value"),
    "QStyle.SP_DirIcon": ("subset", "icon (standard icon reference)"),
    "QStyle.SP_FileIcon": ("subset", "icon (standard icon reference)"),
    "QStyle.SP_DialogOpenButton": ("subset", "icon (standard icon reference)"),
    "QStyle.StandardPixmap": ("subset", "icon (standard icon reference)"),
    # ---- U2 host services that need a rewrite from a builder to one op ------
    "QMessageBox()": ("U2", "dialog.message (rewrite: builder to one op)"),
    "QFileDialog()": ("U2", "dialog.file (rewrite: builder to one op)"),
    "QInputDialog()": ("U2", "dialog.input (rewrite: builder to one op)"),
    "QTimer()": ("U2", "defer (rewrite: repeating timer to defer chain)"),
    "QTimer": ("U2", "defer"),
    "QProgressDialog": ("U2", "progress (Base.ProgressIndicator shape)"),
    "QDesktopServices.openUrl": ("unmapped", "openUrl: an external launch, needs a new U2 op and a grant"),
    "QApplication.activeWindow": ("U3", "form.parent (dropped: the host picks the parent)"),
    "QApplication.style": ("unmapped", "style/palette query: theme data, a new U2 query op"),
    "QApplication.palette": ("unmapped", "style/palette query: theme data, a new U2 query op"),
    "QApplication.instance": ("unmapped", "process handle: never crosses"),
    "QApplication.sendEvent": ("unmapped", "synthetic Qt event: no protocol equivalent"),
    "QApplication.keyboardModifiers": ("U6", "tool.events (modifiers on the event)"),
    "QApplication": ("unmapped", "QApplication member outside the subset"),
    # ---- U3 forms -----------------------------------------------------------
    "QObject.connect": ("U3", "form.signal (subscribe)"),
    "QObject.disconnect": ("U3", "form.signal (unsubscribe)"),
    "QObject": ("unmapped", "QObject outside signal connect: event filters, custom QObjects"),
    "QStandardItem": ("unmapped", "model/view (QStandardItem): no .ui equivalent, needs a U3 tree/table model op"),
    "QStandardItemModel": ("unmapped", "model/view (QStandardItemModel): needs a U3 tree/table model op"),
    "QAbstractItemModel": ("unmapped", "model/view (custom model): needs a U3 tree/table model op"),
    "QAbstractTableModel": ("unmapped", "model/view (custom model): needs a U3 tree/table model op"),
    "QSortFilterProxyModel": ("unmapped", "model/view (proxy model): needs a U3 tree/table model op"),
    "QItemSelectionModel": ("unmapped", "model/view (selection model): needs a U3 tree/table model op"),
    "QStyledItemDelegate": ("unmapped", "item delegate (custom editor/painting): no protocol equivalent"),
    "QItemDelegate": ("unmapped", "item delegate (custom editor/painting): no protocol equivalent"),
    "QCompleter": ("unmapped", "completer: a U3 field property (completion list)"),
    "QValidator": ("unmapped", "validator: a U3 field property (input mask)"),
    "QDoubleValidator": ("unmapped", "validator: a U3 field property (input mask)"),
    "QIntValidator": ("unmapped", "validator: a U3 field property (input mask)"),
    "QRegularExpressionValidator": ("unmapped", "validator: a U3 field property (input mask)"),
    "QShortcut": ("unmapped", "shortcut on a widget: U1 accelerator data or a U3 form property"),
    "QDockWidget": ("unmapped", "dock widget: U3 dock placement of a .ui form (not in the subset yet)"),
    "QMainWindow": ("unmapped", "main window: never crosses"),
    "QMdiArea": ("unmapped", "MDI area walk to find the 3D view: U4 view.list/active instead"),
    "QMdiSubWindow": ("unmapped", "MDI window: never crosses"),
    "QSystemTrayIcon": ("unmapped", "tray icon: never crosses"),
    "QEvent": ("unmapped", "Qt event types: event filters have no protocol equivalent"),
    "QKeyEvent": ("unmapped", "Qt event types: event filters have no protocol equivalent"),
    "QMouseEvent": ("unmapped", "Qt event types: event filters have no protocol equivalent"),
    "QCursor": ("unmapped", "cursor object: cursor.override with a named cursor only"),
    # ---- raster, painting, text documents, printing ---------------------------
    "QImage": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QPixmap": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QPainter": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QPen": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QBrush": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QPainterPath": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QPolygonF": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QTransform": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QTextCharFormat": ("unmapped", "rich text document: no protocol equivalent"),
    "QTextCursor": ("unmapped", "rich text document: no protocol equivalent"),
    "QTextDocument": ("unmapped", "rich text document: no protocol equivalent"),
    "QSyntaxHighlighter": ("unmapped", "rich text document: no protocol equivalent"),
    "QPrinter": ("unmapped", "printing: no protocol equivalent"),
    "QPrintDialog": ("unmapped", "printing: no protocol equivalent"),
    "QGraphicsView": ("unmapped", "QGraphics scene: no protocol equivalent"),
    "QGraphicsScene": ("unmapped", "QGraphics scene: no protocol equivalent"),
    "QGraphicsItem": ("unmapped", "QGraphics scene: no protocol equivalent"),
    "QSvgWidget": ("unmapped", "SVG widget: no protocol equivalent"),
    "QSvgRenderer": ("unmapped", "SVG renderer: no protocol equivalent"),
    "QPalette": ("unmapped", "style/palette query: theme data, a new U2 query op"),
    "QStyle": ("unmapped", "style query: theme data, a new U2 query op"),
    "QStyleOption": ("unmapped", "style painting: no protocol equivalent"),
    "QStyleOptionViewItem": ("unmapped", "style painting: no protocol equivalent"),
    # ---- I/O, process, threads: never cross -----------------------------------
    "QByteArray": ("unmapped", "Qt I/O types: bytes in the guest instead"),
    "QBuffer": ("unmapped", "Qt I/O types: bytes in the guest instead"),
    "QIODevice": ("unmapped", "Qt I/O types: bytes in the guest instead"),
    "QFile": ("unmapped", "filesystem: never crosses (fs grant is the file dialog only)"),
    "QDir": ("unmapped", "filesystem: never crosses"),
    "QFileInfo": ("unmapped", "filesystem: never crosses"),
    "QStandardPaths": ("unmapped", "filesystem: never crosses"),
    "QSettings": ("unmapped", "settings: FreeCAD parameters instead"),
    "QProcess": ("unmapped", "process launch: never crosses"),
    "QThread": ("unmapped", "threads: never cross"),
    "QRunnable": ("unmapped", "threads: never cross"),
    "QThreadPool": ("unmapped", "threads: never cross"),
    "QNetworkAccessManager": ("unmapped", "network: SandboxNetwork.md N1"),
    "QNetworkRequest": ("unmapped", "network: SandboxNetwork.md N1"),
    "QNetworkReply": ("unmapped", "network: SandboxNetwork.md N1"),
    "QWebEngineView": ("unmapped", "web view: never crosses"),
    "qRgb": ("subset", "color value"),
    "qRgba": ("subset", "color value"),
    "qRed": ("subset", "color value"),
    "qGreen": ("subset", "color value"),
    "qBlue": ("subset", "color value"),
    "qAlpha": ("subset", "color value"),
    "qGray": ("subset", "color value"),
    "QLinearGradient": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QRadialGradient": ("unmapped", "icon composition and textures (QImage/QPainter): needs icon.swatch / icon.overlay ops, textures as U5 image data"),
    "QFontMetrics": ("unmapped", "font metrics: needs a U3 text-measure query op"),
    "QActionGroup": ("U3", "widget tree (hand-built: rewrite as .ui)"),
    "QTreeView": ("unmapped", "model/view (QTreeView): needs a U3 tree/table model op"),
    "QTableView": ("unmapped", "model/view (QTableView): needs a U3 tree/table model op"),
    "QListView": ("unmapped", "model/view (QListView): needs a U3 tree/table model op"),
    "SIGNAL": ("U3", "form.signal (signal name)"),
    "SLOT": ("U3", "form.signal (slot name)"),
    "Signal": ("unmapped", "custom Qt signal: guest-side callback instead"),
    "Slot": ("unmapped", "Qt slot decorator: plain method instead"),
    "QObject.tr": ("subset", "i18n.translate"),
    # ---- FreeCADGui: U1 registration -----------------------------------------
    "Gui.addCommand": ("U1", "register.command"),
    "Gui.addWorkbench": ("U1", "register.workbench"),
    "Gui.Workbench": ("U1", "register.workbench (base class)"),
    "Gui.addIconPath": ("U1", "register.iconPath"),
    "Gui.addResourcePath": ("U1", "register.resourcePath"),
    "Gui.addLanguagePath": ("U1", "register.languagePath"),
    "Gui.addPreferencePage": ("U1", "register.preferencePage"),
    "Gui.updateLocale": ("U1", "register.languagePath (reload)"),
    "Gui.getIcon": ("U1", "icon (registered icon reference)"),
    "Gui.listCommands": ("U1", "register.command (query)"),
    "Gui.Command.get": ("U1", "register.command (query)"),
    "Gui.Command": ("U1", "register.command (query)"),
    "Gui.activateWorkbench": ("U1", "workbench.activate"),
    "Gui.activeWorkbench": ("U1", "workbench.active"),
    "Gui.listWorkbenches": ("U1", "workbench.list"),
    "Gui.getWorkbench": ("U1", "workbench.get"),
    "Gui.addModule": ("U4", "macro stream: record an import (no eval)"),
    "Gui.addDocumentObserver": ("U4", "document observer events"),
    "Gui.removeDocumentObserver": ("U4", "document observer events"),
    # ---- FreeCADGui: U2 host services ----------------------------------------
    "Gui.InputHint": ("U2", "hints (data)"),
    "Gui.UserInput": ("U2", "hints (data)"),
    "Gui.HintManager": ("U2", "hints.show/hide"),
    "Gui.updateGui": ("U2", "no-op (host owns the loop)"),
    "Gui.getMainWindow().statusBar": ("unmapped", "status bar message: needs a U2 status op"),
    "Gui.getMainWindow().showMessage": ("unmapped", "status bar message: needs a U2 status op"),
    "Gui.getMainWindow().addStatusBarItem": ("unmapped", "status bar widget: needs a U3 dock placement (status)"),
    # ---- FreeCADGui: U3 forms ------------------------------------------------
    "Gui.PySideUic.loadUi": ("U3", "form.load (.ui)"),
    "Gui.PySideUic": ("U3", "form.load (.ui)"),
    "Gui.UiLoader": ("U3", "form.load (.ui)"),
    "Gui.Control.showDialog": ("U3", "panel.show"),
    "Gui.Control.closeDialog": ("U3", "panel.close"),
    "Gui.Control.activeDialog": ("U3", "panel.active"),
    "Gui.Control.isActiveDialog": ("U3", "panel.active"),
    "Gui.Control.showTaskView": ("U3", "panel.show"),
    "Gui.Control.clearTaskWatcher": ("U3", "panel.watchers"),
    "Gui.Control.addTaskWatcher": ("U3", "panel.watchers"),
    "Gui.Control": ("U3", "panel.*"),
    "Gui.getMainWindow": ("U3", "form.parent (dropped: the host picks the parent)"),
    "Gui.getMainWindow().getActiveWindow": ("U4", "view.active"),
    "Gui.getMainWindow().getActiveWindow().getViewer": ("unmapped", "viewer widget handle (Quarter): no protocol equivalent"),
    "Gui.getMainWindow().setActiveWindow": ("U4", "view.activate"),
    "Gui.getMainWindow().getWindows": ("U4", "view.list"),
    "Gui.getMainWindow().cursor": ("unmapped", "cursor position on the main window: tool.events carries it"),
    "Gui.getMainWindow().mainWindowClosed": ("unmapped", "main window signal: an application lifecycle event op"),
    "Gui.getMainWindow().findChild": ("unmapped", "reaching into the main window's widget tree: never crosses"),
    "Gui.getMainWindow().findChildren": ("unmapped", "reaching into the main window's widget tree: never crosses"),
    "Gui.getMainWindow().workbenchActivated": ("unmapped", "workbench signal: a U1 workbench event op"),
    "Gui.draftToolBar": ("U3", "Draft toolbar: a .ui form with dock placement"),
    "Gui.ExpressionBinding": ("U3", "form.field (expression binding)"),
    "Gui.getMarkerIndex": ("U5", "marker set (marker style)"),
    # ---- FreeCADGui: U4 selection, view, edit --------------------------------
    "Gui.Selection": ("U4", "selection.*"),
    "Gui.Selection.addObserver": ("U4", "selection observer events"),
    "Gui.Selection.removeObserver": ("U4", "selection observer events"),
    "Gui.ActiveDocument": ("U4", "document GUI state"),
    "Gui.ActiveDocument.ActiveView": ("U4", "view.*"),
    "Gui.ActiveDocument.ActiveView.getSceneGraph": ("unmapped", "scene graph handle: U5 owns the scene, no raw access"),
    "Gui.ActiveDocument.ActiveView.getCameraNode": ("U4", "view.camera (rewrite: Coin camera node to camera get/set)"),
    "Gui.ActiveDocument.ActiveView.getViewer": ("unmapped", "viewer widget handle (Quarter): no protocol equivalent"),
    "Gui.ActiveDocument.ActiveView.addEventCallback": ("U6", "tool.events (subscribe)"),
    "Gui.ActiveDocument.ActiveView.addEventCallbackPivy": ("U6", "tool.events (subscribe)"),
    "Gui.ActiveDocument.ActiveView.removeEventCallback": ("U6", "tool.events (unsubscribe)"),
    "Gui.ActiveDocument.ActiveView.removeEventCallbackPivy": ("U6", "tool.events (unsubscribe)"),
    "Gui.ActiveDocument.setEdit": ("U4", "edit.set"),
    "Gui.ActiveDocument.resetEdit": ("U4", "edit.reset"),
    "Gui.ActiveDocument.getInEdit": ("U4", "edit.current"),
    "Gui.getDocument": ("U4", "document GUI state"),
    "Gui.runCommand": ("U4", "runCommand"),
    "Gui.SendMsgToActiveView": ("U4", "runCommand (view message)"),
    "Gui.getUserEditMode": ("U4", "edit.mode"),
    "Gui.setUserEditMode": ("U4", "edit.mode"),
    "Gui.listUserEditModes": ("U4", "edit.mode"),
    "Gui.doCommand": ("U4.doCommand", "doCommand (gui.doCommand permission)"),
    "Gui.doCommandGui": ("U4.doCommand", "doCommand (gui.doCommand permission)"),
    "Gui.getMainWindow().getActiveWindow().getSceneGraph": ("unmapped", "scene graph handle: U5 owns the scene, no raw access"),
    # ---- FreeCADGui: U5 view providers ---------------------------------------
    "Gui.ViewProviderDocumentObject": ("U5", "view provider hooks (base class)"),
    "Gui.ViewProviderDocumentObjectPython": ("U5", "view provider hooks (base class)"),
    # ---- FreeCADGui: U6 tools ------------------------------------------------
    "Gui.Snapper": ("U6", "tool.* (snapper: C++ snap engine)"),
    "Gui.Snapper.ui": ("U3", "Draft toolbar: a .ui form with dock placement"),
    "Gui.Snapper.trackers": ("U6", "tool trackers (U5 primitives)"),
    "Gui.Snapper.grid": ("U6", "tool trackers (grid)"),
    "Gui.Snapper.setGrid": ("U6", "tool trackers (grid)"),
    "Gui.Snapper.get_snap_toolbar": ("U3", "snap toolbar: a .ui form"),
    "Gui.Snapper.show_snap_toolbar": ("U3", "snap toolbar: a .ui form"),
    "Gui.addPreferencePage()": ("U1", "register.preferencePage"),
    "Gui.activeView": ("U4", "view.*"),
    "Gui.activeView().addEventCallback": ("U6", "tool.events (subscribe)"),
    "Gui.activeView().addEventCallbackPivy": ("U6", "tool.events (subscribe)"),
    "Gui.activeView().removeEventCallback": ("U6", "tool.events (unsubscribe)"),
    "Gui.activeView().removeEventCallbackPivy": ("U6", "tool.events (unsubscribe)"),
    "Gui.activeView().getSceneGraph": ("unmapped", "scene graph handle: U5 owns the scene, no raw access"),
    "Gui.activeView().getViewer": ("unmapped", "viewer widget handle (Quarter): no protocol equivalent"),
    "Gui.activeView().getCameraNode": ("U4", "view.camera (rewrite: Coin camera node to camera get/set)"),
    "Gui.addWorkbenchManipulator": ("U1", "register.workbenchManipulator (menu/toolbar edits as data)"),
    "Gui.removeWorkbenchManipulator": ("U1", "register.workbenchManipulator (menu/toolbar edits as data)"),
    "Gui.suspendWaitCursor": ("U2", "cursor.override (suspend)"),
    "Gui.resumeWaitCursor": ("U2", "cursor.override (resume)"),
    "Gui.showPreferences": ("unmapped", "preferences dialog: needs a U2 preferences.show op"),
    # ---- fork- or addon-specific objects hung on the Gui module -------------
    "Gui.setLiveImport": ("unmapped", "fork live-import switch: a host op of its own"),
    "Gui.isLiveImport": ("unmapped", "fork live-import switch: a host op of its own"),
    "Gui.pumpLiveImport": ("unmapped", "fork live-import switch: a host op of its own"),
    "Gui.IFC_WBManipulator": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.BIM_WBManipulator": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.IFC_saveshortcut": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.bimSetup": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.BIMStatusWidget": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.bimWidgets": ("unmapped", "addon state hung on the Gui module: guest module state instead"),
    "Gui.draftToolBar()": ("U3", "Draft toolbar: a .ui form with dock placement"),
    "Gui.showMainWindow": ("unmapped", "main window: never crosses"),
    "Gui.getSoDBVersion": ("unmapped", "Coin version query: never crosses"),
    "Gui.exportSubgraph": ("unmapped", "Coin subgraph export: never crosses"),
    "Gui.createViewer": ("unmapped", "creating a viewer: never crosses"),
    "Gui.addIcon": ("unmapped", "icon from XPM data: icons cross as references, not bitmaps"),
    "Gui.getIconPath": ("U1", "icon (registered icon reference)"),
    "Gui.addLanguagePath()": ("U1", "register.languagePath"),
    "Gui.Snapper()": ("U6", "tool.* (snapper: C++ snap engine)"),
    "Gui.subgraphFromObject": ("unmapped", "Coin subgraph of an object: U5 annotation primitives instead"),
    "Gui.getMarkerIndex()": ("U5", "marker set (marker style)"),
    "Gui.reload": ("unmapped", "reload workbench: never crosses"),
    "Gui.open": ("U4", "document.open (GUI)"),
    "Gui.insert": ("U4", "document.insert (GUI)"),
    "Gui.export": ("U4", "document.export (GUI)"),
    "Gui.getLocale": ("U1", "i18n.locale"),
    "Gui.setLocale": ("U1", "i18n.locale"),
    "Gui.supportedLocales": ("U1", "i18n.locale"),
    "Gui.hide": ("U4", "visibility"),
    "Gui.show": ("U4", "visibility"),
    "Gui.getObjectInfo": ("U4", "view.pick"),
    "Gui.coinRemoveAllChildren": ("unmapped", "Coin subgraph edit: U5 annotation primitives instead"),
    # ---- pivy value types ----------------------------------------------------
    "coin.SbVec3f": ("subset", "vector value (FreeCAD.Vector)"),
    "coin.SbVec2f": ("subset", "vector value"),
    "coin.SbVec2s": ("subset", "vector value"),
    "coin.SbColor": ("subset", "color value"),
    "coin.SbRotation": ("subset", "rotation value (FreeCAD.Rotation)"),
    "coin.SbMatrix": ("subset", "matrix value (FreeCAD.Matrix)"),
    "coin.SbPlane": ("subset", "plane value"),
    "coin.SbBox3f": ("subset", "bounding box value"),
    "coin.SbViewportRegion": ("unmapped", "viewport region for a Coin action: U4 view query instead"),
    "coin.SoType": ("U6", "tool.events (event type id)"),
    "coin.SoDB": ("unmapped", "Coin database: version query, init"),
    "coin.SoDB.readAll": ("unmapped", "Inventor-string shape copy (writeInventor -> SoInput -> readAll): a U6 ghost tracker / U5 shape-reference primitive"),
    "coin.SoType.fromName": ("unmapped", "FreeCAD Coin node by type name (SoBrepEdgeSet, SoBrepFaceSet, SoDatumLabel, SoFCSelection, SoSkipBoundingGroup): U5 selectable primitives, C++ dimension"),
    "coin.SoInput": ("unmapped", "Inventor-string shape copy (writeInventor -> SoInput -> readAll): a U6 ghost tracker / U5 shape-reference primitive"),
    "coin.SoOutput": ("unmapped", "Coin .iv writer: no protocol equivalent"),
    "coin.SoWriteAction": ("unmapped", "Coin .iv writer: no protocol equivalent"),
    "coin.SoGetBoundingBoxAction": ("unmapped", "Coin bounding box action: U4 view/object bbox query instead"),
    "coin.SoGetMatrixAction": ("unmapped", "Coin matrix action: U4 query instead"),
    "coin.SoSearchAction": ("unmapped", "Coin scene search: U5 owns the scene, no raw access"),
    "coin.SoRayPickAction": ("unmapped", "Coin ray pick: U4 view.pick instead"),
    "coin.SoOffscreenRenderer": ("unmapped", "offscreen render: a U4 view.saveImage op"),
    "coin.SoPerspectiveCamera": ("unmapped", "camera node: U4 view.camera get/set instead"),
    "coin.SoOrthographicCamera": ("unmapped", "camera node: U4 view.camera get/set instead"),
    "coin.SoCamera": ("unmapped", "camera node: U4 view.camera get/set instead"),
    "coin.SoDirectionalLight": ("unmapped", "light node: a U5 primitive to add (or a view setting)"),
    "coin.SoShadowGroup": ("unmapped", "shadow group: a view setting, not annotation"),
    "coin.SoQtViewer": ("unmapped", "viewer widget: never crosses"),
    "coin.SoSceneManager": ("unmapped", "scene manager: never crosses"),
    "coin.SoNode": ("unmapped", "generic Coin node handle"),
    "coin.SoFieldSensor": ("unmapped", "Coin field sensor: guest-side change events instead"),
    "coin.SoNodeSensor": ("unmapped", "Coin node sensor: guest-side change events instead"),
    "coin.SoTimerSensor": ("unmapped", "Coin timer: defer instead"),
    "coin.SoCallback": ("unmapped", "GL callback node: never crosses"),
    "coin.SoGLRenderAction": ("unmapped", "GL render action: never crosses"),
    "quarter": ("unmapped", "Quarter viewer widget: never crosses"),
}

RULES_PREFIXES = sorted(RULES, key=len, reverse=True)

# Widget classes and value types are keyed by class, then a member on them is
# a hand-built widget tree in any case.
for _w in UI_WIDGETS:
    RULES.setdefault(_w, ("U3", "widget tree (hand-built: rewrite as .ui)"))
for _v in VALUE_TYPES:
    RULES.setdefault(_v, ("subset", "value type"))
for _c, _p in COIN_PRIMITIVES.items():
    RULES.setdefault("coin." + _c, ("U5", "annotation primitive: " + _p))
for _e in COIN_EVENTS:
    RULES.setdefault("coin." + _e, ("U6", "tool.events (event data)"))

QT_MODULES = re.compile(r"^Qt[A-Z]\w*$")
HOOKS = {
    "attach", "updateData", "onChanged", "getIcon", "claimChildren", "setEdit", "unsetEdit",
    "doubleClicked", "setupContextMenu", "getDisplayModes", "getDefaultDisplayMode",
    "setDisplayMode", "onDelete", "canDragObjects", "canDropObjects", "dragObject",
    "dropObject", "canDragObject", "canDropObject", "onBeforeChange", "getDetailPath",
    "getElementPicked", "replaceObject", "canAddToSceneGraph", "canReplaceObject",
    "dumps", "loads", "__getstate__", "__setstate__",
}

Use = collections.namedtuple("Use", "file line chain called ctx bucket op family")


class Aliases:
    """Local name -> (family, canonical prefix parts) for one module."""

    def __init__(self):
        self.map = {}

    def feed(self, tree):
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for a in node.names:
                    self._import(a.name, a.asname)
            elif isinstance(node, ast.ImportFrom):
                mod = node.module or ""
                for a in node.names:
                    self._from(mod, a.name, a.asname)
            elif isinstance(node, ast.Assign):
                if (
                    isinstance(node.value, ast.Name)
                    and node.value.id in self.map
                    and len(node.targets) == 1
                    and isinstance(node.targets[0], ast.Name)
                ):
                    self.map[node.targets[0].id] = self.map[node.value.id]

    def _import(self, name, asname):
        local = asname or name.split(".")[0]
        if name == "FreeCADGui":
            self.map[local] = ("gui", ["Gui"])
        elif name in ("PySide", "PySide2", "PySide6"):
            self.map[local] = ("qt", [])
        elif re.match(r"^PySide[26]?\.Qt\w+$", name):
            self.map[local] = ("qt", [])
        elif name in ("pivy", "pivy.coin"):
            self.map[local] = ("coin", ["coin"])
        elif name == "pivy.quarter":
            self.map[local] = ("coin", ["quarter"])

    def _from(self, mod, name, asname):
        local = asname or name
        if mod in ("PySide", "PySide2", "PySide6"):
            self.map[local] = ("qt", [])
        elif re.match(r"^PySide[26]?\.Qt\w+$", mod):
            self.map[local] = ("qt", [name])
        elif mod == "pivy":
            self.map[local] = ("coin", [name])
        elif mod == "pivy.coin":
            self.map[local] = ("coin", ["coin", name])
        elif mod == "pivy.quarter":
            self.map[local] = ("coin", ["quarter", name])
        elif mod == "FreeCADGui":
            self.map[local] = ("gui", ["Gui", name])
        elif mod == "" and name == "FreeCADGui":
            self.map[local] = ("gui", ["Gui"])


def chain_parts(node):
    """Attribute/Call/Name chain -> list of parts, '()' marking a call; None if not a chain."""
    parts = []
    while True:
        if isinstance(node, ast.Attribute):
            parts.append(node.attr)
            node = node.value
        elif isinstance(node, ast.Call):
            parts.append("()")
            node = node.func
        elif isinstance(node, ast.Name):
            parts.append(node.id)
            break
        else:
            return None
    parts.reverse()
    return parts


def render(parts):
    out = ""
    for p in parts:
        if p == "()":
            out += "()"
        elif out:
            out += "." + p
        else:
            out = p
    return out


def classify(family, parts, called):
    """Return (bucket, op) for a canonical chain."""
    named = [p for p in parts if p != "()"]
    # try prefixes of the rendered chain, longest first
    for i in range(len(parts), 0, -1):
        if parts[i - 1] == "()":
            continue
        key = render(parts[:i])
        if i == len(parts) and called and key + "()" in RULES:
            return RULES[key + "()"]
        if key in RULES:
            return RULES[key]
    if family == "coin" and len(named) >= 2:
        cls = named[1]
        if cls in COIN_EVENTS or cls.endswith("Event"):
            return ("U6", "tool.events (event data)")
        if cls.startswith("Sb"):
            return ("subset", "value type")
        if cls.isupper() or cls.startswith("SO_"):
            return ("subset", "Coin constant")
        if cls.startswith("So"):
            return ("unmapped", "Coin class outside the primitive set: " + cls)
    if family == "qt":
        return ("unmapped", "Qt class outside the subset: " + (named[0] if named else "?"))
    if family == "gui":
        if len(named) >= 2 and re.match(r"^(BIM|IFC|Bim|Ifc|bim|ifc|draft|Draft)", named[1]):
            return ("unmapped", "addon state hung on the Gui module: guest module state instead")
        return ("unmapped", "FreeCADGui member outside the protocol: " + render(parts[:2]))
    return ("unmapped", "?")


class Scanner(ast.NodeVisitor):
    def __init__(self, path, aliases):
        self.path = path
        self.aliases = aliases
        self.uses = []
        self.hooks = 0
        self.vp_classes = 0

    # -- chains ---------------------------------------------------------------
    def handle(self, node, called=False, ctx="attr"):
        parts = chain_parts(node)
        if parts is None:
            return self.generic_visit(node)
        if called and parts and parts[-1] == "()":
            parts = parts[:-1]
        root = parts[0]
        info = self.aliases.map.get(root)
        if info is None:
            # a method reached through a stored object, matched by name
            named = [p for p in parts if p != "()"]
            last = named[-1] if named else ""
            if last in BARE_METHODS and len(named) >= 2:
                bucket, op = BARE_METHODS[last]
                self.uses.append(
                    Use(self.path, node.lineno, "?." + last, called, ctx, bucket, op, "view")
                )
                self.visit_call_args(node)
                return
            return self.generic_visit(node)
        family, prefix = info
        canon = prefix + parts[1:]
        if family == "qt":
            while canon and QT_MODULES.match(canon[0]):
                canon = canon[1:]
        if not canon:
            return self.generic_visit(node)
        bucket, op = classify(family, canon, called)
        chain = render(canon)
        self.uses.append(Use(self.path, node.lineno, chain, called, ctx, bucket, op, family))
        self.visit_call_args(node)

    def visit_call_args(self, node):
        # visit the arguments of every call inside the chain
        while True:
            if isinstance(node, ast.Call):
                for a in node.args:
                    self.visit(a)
                for k in node.keywords:
                    self.visit(k.value)
                node = node.func
            elif isinstance(node, ast.Attribute):
                node = node.value
            else:
                return

    def visit_Attribute(self, node):
        self.handle(node)

    def visit_Call(self, node):
        if isinstance(node.func, (ast.Attribute, ast.Name, ast.Call)):
            self.handle(node, called=True)
        else:
            self.generic_visit(node)

    def visit_Name(self, node):
        info = self.aliases.map.get(node.id)
        if info and info[1] and isinstance(node.ctx, ast.Load):
            family, prefix = info
            if len(prefix) == 1 and prefix[0] in ("Gui", "coin", "quarter"):
                return  # the module object itself, passed around: no use to bucket
            bucket, op = classify(family, prefix, False)
            self.uses.append(
                Use(self.path, node.lineno, render(prefix), False, "attr", bucket, op, family)
            )

    def visit_ClassDef(self, node):
        for b in node.bases:
            parts = chain_parts(b)
            if parts and parts[0] in self.aliases.map:
                self.handle(b, ctx="base")
            elif parts and parts[-1] == "()":
                self.visit(b)
        names = {n.name for n in node.body if isinstance(n, ast.FunctionDef)}
        hooks = names & HOOKS
        if hooks and ("attach" in hooks or "updateData" in hooks or "getIcon" in hooks):
            self.vp_classes += 1
        self.hooks += len(hooks)
        for n in node.body:
            self.visit(n)
        for d in node.decorator_list:
            self.visit(d)


def scan_file(path):
    with open(path, "rb") as f:
        src = f.read()
    try:
        tree = ast.parse(src, filename=path)
    except SyntaxError as e:
        return None, "syntax error: %s" % e
    aliases = Aliases()
    aliases.feed(tree)
    sc = Scanner(path, aliases)
    sc.visit(tree)
    imports = {fam for fam, _ in aliases.map.values()}
    return sc, imports


def scan_ui(path):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        return None, "parse error: %s" % e
    classes = collections.Counter()
    for w in root.iter("widget"):
        classes[w.get("class", "?")] += 1
    custom = set()
    for c in root.iter("customwidget"):
        cls = c.find("class")
        if cls is not None and cls.text:
            custom.add(cls.text.strip())
    return classes, custom


def walk(roots, exts):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for d, dirs, files in os.walk(root):
            dirs[:] = sorted(x for x in dirs if x not in ("__pycache__",))
            for f in sorted(files):
                if os.path.splitext(f)[1] in exts:
                    yield os.path.join(d, f)


def lint(roots):
    result = {
        "roots": roots,
        "files": {},  # path -> record
        "errors": [],
        "ui": {},
    }
    for path in walk(roots, {".py"}):
        sc, imports = scan_file(path)
        if sc is None:
            result["errors"].append((path, imports))
            continue
        counts = collections.Counter(u.bucket for u in sc.uses)
        result["files"][path] = {
            "imports": sorted(imports),
            "counts": {b: counts.get(b, 0) for b in BUCKETS},
            "hooks": sc.hooks,
            "vp_classes": sc.vp_classes,
            "uses": sc.uses,
        }
    for path in walk(roots, {".ui"}):
        classes, custom = scan_ui(path)
        if classes is None:
            result["errors"].append((path, custom))
            continue
        result["ui"][path] = {"classes": classes, "custom": sorted(custom)}
    return result


# ---- reports -----------------------------------------------------------------


def rel(path, roots):
    for r in roots:
        base = os.path.dirname(r.rstrip(os.sep)) or "."
        if path.startswith(base + os.sep):
            return path[len(base) + 1 :]
    return path


def weight(rec):
    c = rec["counts"]
    return c["U3"] + c["U5"] + c["U6"] + 3 * c["unmapped"]


def flags(rec):
    c = rec["counts"]
    out = []
    if c["U3"]:
        out.append("form")
    if c["U5"]:
        out.append("scene")
    if c["U6"]:
        out.append("tool")
    if c["unmapped"]:
        out.append("RESIDUE")
    if not out and any(c[b] for b in BUCKETS):
        out.append("no-edit")
    if not any(c[b] for b in BUCKETS):
        out.append("clean")
    return "+".join(out)


def report_summary(res, out):
    roots = res["roots"]
    print("== Summary", file=out)
    print("  %-28s %6s %8s %6s %6s %6s" % ("root", "py", "PySide", "pivy", "Gui", ".ui"), file=out)
    for r in roots:
        recs = [(p, x) for p, x in res["files"].items() if p.startswith(r.rstrip(os.sep) + os.sep) or p == r]
        uis = [p for p in res["ui"] if p.startswith(r.rstrip(os.sep) + os.sep)]
        print(
            "  %-28s %6d %8d %6d %6d %6d"
            % (
                os.path.basename(r.rstrip(os.sep)),
                len(recs),
                sum(1 for _, x in recs if "qt" in x["imports"]),
                sum(1 for _, x in recs if "coin" in x["imports"]),
                sum(1 for _, x in recs if "gui" in x["imports"]),
                len(uis),
            ),
            file=out,
        )
    if res["errors"]:
        print("  errors:", file=out)
        for p, e in res["errors"]:
            print("    %s: %s" % (rel(p, roots), e), file=out)
    print(file=out)
    print("== Buckets (uses / files)", file=out)
    for b in BUCKETS:
        uses = sum(x["counts"][b] for x in res["files"].values())
        files = sum(1 for x in res["files"].values() if x["counts"][b])
        print("  %-13s %6d %5d" % (b, uses, files), file=out)
    hooks = sum(x["hooks"] for x in res["files"].values())
    vps = sum(x["vp_classes"] for x in res["files"].values())
    print("  %-13s %6d %5d   (view-provider hook methods / proxy classes; U5 keeps them)" % ("hooks", hooks, vps), file=out)
    verdicts = collections.Counter(flags(x) for x in res["files"].values())
    print(file=out)
    print("== File verdicts", file=out)
    for k, n in sorted(verdicts.items(), key=lambda kv: -kv[1]):
        print("  %-32s %5d" % (k, n), file=out)
    print(file=out)


def report_residue(res, out, top=None):
    roots = res["roots"]
    groups = collections.defaultdict(lambda: [0, set()])
    for rec in res["files"].values():
        for u in rec["uses"]:
            if u.bucket != "unmapped":
                continue
            parts = u.chain.split(".")
            key = u.chain if len(parts) <= 4 else ".".join(parts[:4])
            g = groups[(key, u.op)]
            g[0] += 1
            g[1].add(u.file)
    print("== Residue: unmapped uses (count / files / chain -- what sec 4 lacks)", file=out)
    rows = sorted(groups.items(), key=lambda kv: (-kv[1][0], kv[0]))
    if top:
        rows = rows[:top]
    for (chain, op), (n, files) in rows:
        print("  %5d %4d  %-48s %s" % (n, len(files), chain, op), file=out)
    print(file=out)
    # the residue by theme
    themes = collections.defaultdict(lambda: [0, set()])
    for (chain, op), (n, files) in groups.items():
        theme = op.split(":")[0]
        themes[theme][0] += n
        themes[theme][1] |= files
    print("== Residue by theme (uses / files)", file=out)
    for theme, (n, files) in sorted(themes.items(), key=lambda kv: -kv[1][0]):
        print("  %5d %4d  %s" % (n, len(files), theme), file=out)
    print(file=out)


def report_worklist(res, out, show_all=False, top=40):
    roots = res["roots"]
    rows = [(p, x) for p, x in res["files"].items() if weight(x) or x["counts"]["unmapped"]]
    rows.sort(key=lambda px: (-weight(px[1]), px[0]))
    if not show_all:
        rows = rows[:top]
    print("== Work list (files needing an edit, heaviest first; --all for every file)", file=out)
    print(
        "  %6s %6s %5s %5s %5s %5s %5s %5s  %-22s %s"
        % ("weight", "subset", "U1-2", "U3", "U4", "doCmd", "U5", "U6", "residue/flags", "file"),
        file=out,
    )
    for p, x in rows:
        c = x["counts"]
        print(
            "  %6d %6d %5d %5d %5d %5d %5d %5d  %4d %-17s %s"
            % (
                weight(x),
                c["subset"],
                c["U1"] + c["U2"],
                c["U3"],
                c["U4"],
                c["U4.doCommand"],
                c["U5"],
                c["U6"],
                c["unmapped"],
                flags(x),
                rel(p, roots),
            ),
            file=out,
        )
    print(file=out)


def report_noedit(res, out):
    roots = res["roots"]
    rows = [p for p, x in res["files"].items() if flags(x) == "no-edit"]
    print("== Files that port with no edit (subset + U1/U2/U4 ops only): %d" % len(rows), file=out)
    for p in sorted(rows):
        print("  " + rel(p, roots), file=out)
    print(file=out)


def report_ui(res, out):
    roots = res["roots"]
    classes = collections.Counter()
    files = collections.defaultdict(set)
    custom = collections.Counter()
    for p, x in res["ui"].items():
        for c, n in x["classes"].items():
            classes[c] += n
            files[c].add(p)
        for c in x["custom"]:
            custom[c] += 1
    print("== .ui widget classes (widgets / files): the U3 renderer subset", file=out)
    for c, n in sorted(classes.items(), key=lambda kv: (-kv[1], kv[0])):
        tag = "  custom" if c in custom else ""
        print("  %5d %4d  %s%s" % (n, len(files[c]), c, tag), file=out)
    print(file=out)


def report_list(res, path, out):
    roots = res["roots"]
    matches = [p for p in res["files"] if p == path or p.endswith(os.sep + path) or rel(p, roots) == path]
    if not matches:
        print("no such file scanned: %s" % path, file=out)
        return
    for p in matches:
        print("== %s" % rel(p, roots), file=out)
        for u in sorted(res["files"][p]["uses"], key=lambda u: (u.line, u.chain)):
            print(
                "  %5d  %-13s %-44s %s%s"
                % (u.line, u.bucket, u.chain + ("()" if u.called else ""), u.op, "  [base]" if u.ctx == "base" else ""),
                file=out,
            )
        print(file=out)


def report_surface(roots, tree, out):
    """The App-side API surface: which members declared in the Py XMLs the
    roots read or call, per XML, against the members annotated <Sandbox>.
    This is G1's work list (docs/SandboxGui.md sec 9)."""
    import glob

    xmls = (
        sorted(glob.glob(os.path.join(tree, "src", "Mod", "Part", "App", "*Py.xml")))
        + sorted(glob.glob(os.path.join(tree, "src", "Base", "*Py.xml")))
        + [
            os.path.join(tree, "src", "App", n)
            for n in (
                "DocumentObjectPy.xml",
                "DocumentPy.xml",
                "PropertyContainerPy.xml",
                "ExtensionContainerPy.xml",
                "GeoFeaturePy.xml",
            )
        ]
    )
    decl = {}  # member -> set of type names
    annotated = {}  # type -> set of annotated members
    for x in xmls:
        if not os.path.exists(x):
            continue
        s = open(x).read()
        typ = os.path.basename(x)[: -len("Py.xml")]
        for m in re.finditer(r'<(?:Methode|Attribute) Name="([A-Za-z_]+)"(.*?)</(?:Methode|Attribute)>', s, re.S):
            decl.setdefault(m.group(1), set()).add(typ)
            if "<Sandbox" in m.group(2):
                annotated.setdefault(typ, set()).add(m.group(1))
    used = collections.Counter()
    modcalls = collections.Counter()
    modules = {"Part", "FreeCAD", "App"}
    for path in walk(roots, {".py"}):
        try:
            t = ast.parse(open(path, "rb").read(), filename=path)
        except SyntaxError:
            continue
        alias = {}
        for n in ast.walk(t):
            if isinstance(n, ast.Import):
                for a in n.names:
                    alias[a.asname or a.name.split(".")[0]] = a.name.split(".")[0]
        for n in ast.walk(t):
            if isinstance(n, ast.Attribute) and isinstance(n.ctx, ast.Load):
                if isinstance(n.value, ast.Name) and alias.get(n.value.id, n.value.id) in modules:
                    modcalls[alias.get(n.value.id, n.value.id) + "." + n.attr] += 1
                elif n.attr in decl:
                    used[n.attr] += 1
    print("== App-side surface: module members used (Part / FreeCAD)", file=out)
    for m in sorted(modules):
        rows = sorted(((k, v) for k, v in modcalls.items() if k.startswith(m + ".")), key=lambda kv: -kv[1])
        if rows:
            print("  %s: %d distinct, %d uses" % (m, len(rows), sum(v for _, v in rows)), file=out)
            print("    " + ", ".join("%s %d" % (k.split(".", 1)[1], v) for k, v in rows), file=out)
    byxml = collections.defaultdict(list)
    for n, c in used.items():
        for x in decl[n]:
            byxml[x].append((n, c))
    print(file=out)
    print("== App-side surface: declared members read or called, per Py type", file=out)
    print("   (%d distinct names, %d reads; a name declared on several types is listed under each)" % (len(used), sum(used.values())), file=out)
    print("  %-26s %5s %5s  members (annotated <Sandbox> marked *)" % ("type", "used", "annot"), file=out)
    for x in sorted(byxml, key=lambda k: (-len(byxml[k]), k)):
        rows = sorted(byxml[x], key=lambda kv: -kv[1])
        ann = annotated.get(x, set())
        print(
            "  %-26s %5d %5d  %s"
            % (x, len(rows), len(ann & {n for n, _ in rows}), ", ".join(("*" if n in ann else "") + "%s %d" % (n, c) for n, c in rows)),
            file=out,
        )
    print(file=out)
    total_ann = sum(len(v) for v in annotated.values())
    print("  annotated members in these XMLs today: %d (%s)" % (total_ann, ", ".join("%s %d" % (k, len(v)) for k, v in sorted(annotated.items()))), file=out)
    print(file=out)


def to_json(res):
    roots = res["roots"]
    files = {}
    for p, x in res["files"].items():
        files[rel(p, roots)] = {
            "imports": x["imports"],
            "counts": x["counts"],
            "hooks": x["hooks"],
            "vp_classes": x["vp_classes"],
            "flags": flags(x),
            "uses": [
                {
                    "line": u.line,
                    "chain": u.chain,
                    "called": u.called,
                    "ctx": u.ctx,
                    "bucket": u.bucket,
                    "op": u.op,
                }
                for u in x["uses"]
            ],
        }
    ui = {rel(p, roots): {"classes": dict(x["classes"]), "custom": x["custom"]} for p, x in res["ui"].items()}
    return {"roots": roots, "files": files, "ui": ui, "errors": [[rel(p, roots), e] for p, e in res["errors"]]}


# ---- self test -----------------------------------------------------------------

FIXTURE = '''
import FreeCADGui as Gui
import FreeCADGui
from PySide import QtCore, QtGui
from PySide.QtCore import QT_TRANSLATE_NOOP
from PySide.QtWidgets import QMessageBox
from pivy import coin
App = None
G = Gui

class Panel(QtGui.QWidget):
    def attach(self, vobj):
        self.node = coin.SoSeparator()
        self.node.addChild(coin.SoLineSet())
        vobj.addDisplayMode(self.node, "Wireframe")
    def updateData(self, obj, prop):
        pass

def run(view):
    Gui.doCommand("x")
    FreeCADGui.addCommand("Draft_Line", None)
    G.Selection.getSelection()
    QtGui.QMessageBox.question(None, QtGui.QApplication.translate("a", "b"))
    QMessageBox.information(None, "a")
    t = QtCore.Qt.AlignLeft
    box = QtGui.QMessageBox()
    view.addEventCallbackPivy(coin.SoLocation2Event.getClassTypeId(), run)
    Gui.getMainWindow().getActiveWindow().getViewer()
    QT_TRANSLATE_NOOP("a", "b")
    img = QtGui.QImage()
    QtCore.QTimer.singleShot(0, run)
    icon = QtGui.QIcon(":/icons/x.svg")
    p = QtGui.QPixmap(":/icons/x.svg")
    q = QtGui.QPixmap.fromImage(img)
    Gui.Snapper.snap(t)
    w = QtGui.QLabel("x")
    Gui.ActiveDocument.ActiveView.getSceneGraph()
    QtCore.QObject.connect(w, QtCore.SIGNAL("clicked()"), run)
'''

EXPECT = [
    ("Gui.doCommand", "U4.doCommand", True),
    ("Gui.addCommand", "U1", True),
    ("Gui.Selection.getSelection", "U4", True),
    ("QMessageBox.question", "subset", True),
    ("QApplication.translate", "subset", True),
    ("QMessageBox.information", "subset", True),
    ("Qt.AlignLeft", "subset", False),
    ("QMessageBox", "U2", True),
    ("?.addEventCallbackPivy", "U6", True),
    ("coin.SoLocation2Event.getClassTypeId", "U6", True),
    ("Gui.getMainWindow().getActiveWindow().getViewer", "unmapped", True),
    ("QT_TRANSLATE_NOOP", "subset", True),
    ("QImage", "unmapped", True),
    ("QTimer.singleShot", "subset", True),
    ("QIcon", "subset", True),
    ("QPixmap", "subset", True),
    ("QPixmap.fromImage", "unmapped", True),
    ("Gui.Snapper.snap", "U6", True),
    ("QLabel", "U3", True),
    ("Gui.ActiveDocument.ActiveView.getSceneGraph", "unmapped", True),
    ("QObject.connect", "U3", True),
    ("QWidget", "U3", False),
    ("coin.SoSeparator", "U5", True),
    ("coin.SoLineSet", "U5", True),
    ("SIGNAL", "U3", True),
]


def self_test():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "fixture.py")
        with open(path, "w") as f:
            f.write(FIXTURE)
        sc, imports = scan_file(path)
        assert sc is not None, imports
        got = [(u.chain, u.bucket, u.called) for u in sc.uses]
        bad = []
        for e in EXPECT:
            if e not in got:
                bad.append("missing %r" % (e,))
        if len(got) != len(EXPECT):
            bad.append("expected %d uses, got %d:\n  %s" % (len(EXPECT), len(got), "\n  ".join(map(str, got))))
        if sc.hooks != 2 or sc.vp_classes != 1:
            bad.append("hooks %d vp_classes %d" % (sc.hooks, sc.vp_classes))
        if imports != {"gui", "qt", "coin"}:
            bad.append("imports %r" % (imports,))
        if bad:
            print("SELF-TEST FAILED")
            print("\n".join(bad))
            return 1
    print("self-test OK (%d uses)" % len(EXPECT))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("roots", nargs="*", help="directories or files to lint (default: src/Mod/Draft src/Mod/BIM)")
    ap.add_argument("--all", action="store_true", help="every file in the work list, not the top 40")
    ap.add_argument("--no-edit", action="store_true", help="list the files that port with no edit")
    ap.add_argument("--list", metavar="FILE", action="append", help="every use in FILE with line and bucket")
    ap.add_argument("--ui", action="store_true", help="the widget classes the .ui files use")
    ap.add_argument("--residue-top", type=int, default=0, help="limit the residue table to N rows")
    ap.add_argument("--json", metavar="OUT", help="write the full result as JSON")
    ap.add_argument(
        "--surface",
        action="store_true",
        help="instead of the GUI buckets: the Part/Base/App API members the roots use, per Py type, "
        "against the <Sandbox> annotations (G1's work list; default roots are Draft's App side)",
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    here = os.path.dirname(os.path.abspath(__file__))
    tree = os.path.dirname(here)
    roots = args.roots
    out = sys.stdout
    if args.surface:
        if not roots:
            d = os.path.join(tree, "src", "Mod", "Draft")
            roots = [os.path.join(d, n) for n in ("draftobjects", "draftgeoutils", "draftfunctions", "draftmake",
                                                    "DraftGeomUtils.py", "DraftVecUtils.py", "WorkingPlane.py")]
        report_surface([os.path.abspath(r) for r in roots], tree, out)
        return 0
    if not roots:
        roots = [os.path.join(tree, "src", "Mod", "Draft"), os.path.join(tree, "src", "Mod", "BIM")]
    roots = [os.path.abspath(r) for r in roots]
    res = lint(roots)
    if args.list:
        for p in args.list:
            report_list(res, p, out)
        return 0
    report_summary(res, out)
    report_residue(res, out, args.residue_top or None)
    report_worklist(res, out, args.all)
    if args.no_edit:
        report_noedit(res, out)
    if args.ui:
        report_ui(res, out)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(to_json(res), f, indent=1, sort_keys=True)
        print("wrote %s" % args.json, file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
