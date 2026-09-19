# /***************************************************************************
# *   Copyright (c) 2019 Victor Titov (DeepSOIC) <vv.titov@gmail.com>       *
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

import FreeCADGui


class Camera(SceneDetail):
    """Camera(doc): TempoVis plugin for saving and restoring camera."""

    class_id = "SDCamera"

    def __init__(self, doc):
        self.doc = doc
        self.key = "the_cam"

    def _viewer(self):
        """The 3D view whose camera this saves and restores, or None when
        the document has none.

        A document served to browsers has no 3D view at all: each client
        keeps its own camera and the server mirrors it (docs/ThinClient.md
        sec 8.3). Indexing the empty list raised IndexError out of every
        edit mode that saves the camera through TempoVis, which is every
        sketch edit -- logged rather than fatal, but a traceback per
        session all the same, and the restore afterwards was lost with it.
        """
        gdoc = FreeCADGui.getDocument(self.doc.Name)
        v = gdoc.activeView()
        if not hasattr(v, "getCamera"):
            views = gdoc.mdiViewsOfType("Gui::View3DInventor")
            v = views[0] if views else None
        return v

    def scene_value(self):
        v = self._viewer()
        return v.getCamera() if v else None

    def apply_data(self, val):
        # Nothing was saved (no view to save from), so there is nothing to
        # put back -- and a client's camera is the client's own anyway.
        v = self._viewer()
        if v and val is not None:
            v.setCamera(val)
