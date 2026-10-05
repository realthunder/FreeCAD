# /***************************************************************************
# *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This library is free software; you can redistribute it and/or         *
# *   modify it under the terms of the GNU Library General Public           *
# *   License as published by the Free Software Foundation; either          *
# *   version 2 of the License, or (at your option) any later version.      *
# *                                                                         *
# *   This library  is distributed in the hope that it will be useful,      *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this library; see the file COPYING.LIB. If not,    *
# *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
# *   Suite 330, Boston, MA  02111-1307, USA                                *
# *                                                                         *
# ***************************************************************************/

from Show.SceneDetail import SceneDetail

import FreeCAD as App

if App.GuiUp:
    import FreeCADGui as Gui


def session_document():
    """The Gui document whose edit session takes visibility entries of its
    own views -- the one being entered, or the one running -- or None: no
    edit, or one whose views have no visibility table (render cache modes
    0-2)."""
    if not App.GuiUp:
        return None
    gdoc = Gui.editDocument()
    if gdoc is None or not gdoc.canSetEditVisibility():
        return None
    return gdoc


class SessionVisibility(SceneDetail):
    """SessionVisibility(object, val = None): plugin for TempoVis to show or
    hide an object in the views of the running edit session only.

    The value is True (shown), False (hidden) or None (the views draw the
    object by its own Visibility). Where VProperty writes the object's
    Visibility -- which is the document's, so every view of it and every
    served client follows -- this puts a transient entry in the visibility
    table of each view of the edit session (Gui.Document.setEditVisibility).
    Nothing is written to the document, so nothing has to be undone for a
    save, and the entries end with the edit whether or not the TempoVis is
    restored.

    One TempoVis-wide owner holds the entries, so there is one value per
    object as TempoVis's stack assumes. A TempoVis restored after its edit
    has ended finds its entries gone already and does nothing."""

    class_id = "SDSessionVisibility"
    affects_persistence = False
    mild_restore = False
    objname = ""
    gdocname = ""

    def __init__(self, object, val=None):
        self.objname = object.Name
        self.doc = object.Document
        gdoc = session_document()
        self.gdocname = gdoc.Document.Name if gdoc else ""
        self.key = self.objname
        self.data = val

    def _target(self):
        """(gui document, object), either None when it is gone"""
        obj = self.doc.getObject(self.objname) if self.doc else None
        try:
            gdoc = Gui.getDocument(self.gdocname) if self.gdocname else None
        except Exception:
            gdoc = None
        return gdoc, obj

    def scene_value(self):
        gdoc, obj = self._target()
        if gdoc is None or obj is None:
            return None
        return gdoc.getEditVisibility(obj)

    def apply_data(self, val):
        gdoc, obj = self._target()
        if gdoc is None or obj is None:
            return
        gdoc.setEditVisibility(obj, val)
