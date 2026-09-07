import os
import tempfile
import unittest

import FreeCAD
from PySide import QtCore

from .TechDrawTestUtilities import createPageWithSVGTemplate


class DrawStoredGeometryTest(unittest.TestCase):
    """A view stores what it projected, and a reopened document uses it.

    See docs/TechDrawStoredGeometry.md.
    """

    def setUp(self):
        self.docName = "TDStoredGeometry"
        self.file = os.path.join(tempfile.gettempdir(), "TDStoredGeometry.FCStd")
        FreeCAD.newDocument(self.docName)
        FreeCAD.setActiveDocument(self.docName)
        FreeCAD.ActiveDocument = FreeCAD.getDocument(self.docName)

        self.box = FreeCAD.ActiveDocument.addObject("Part::Box", "Box")
        self.page = createPageWithSVGTemplate()
        self.page.Scale = 5.0

        self.view = FreeCAD.ActiveDocument.addObject("TechDraw::DrawViewPart", "View")
        self.page.addView(self.view)
        self.view.Source = [self.box]
        self.view.Direction = (0.0, 0.0, 1.0)
        self.view.X = 30.0
        self.view.Y = 150.0
        FreeCAD.ActiveDocument.recompute()
        self.wait()

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            if name.startswith(self.docName):
                FreeCAD.closeDocument(name)
        if os.path.exists(self.file):
            os.remove(self.file)

    def wait(self, ms=2000):
        """the projection runs in worker threads; give them the event loop"""
        loop = QtCore.QEventLoop()
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(loop.quit)
        timer.start(ms)
        loop.exec_()

    def snapshot(self, view):
        return (
            tuple(view.ProjectedGeometry),
            len(view.getVisibleEdges()),
            [view.getGeometryName("Edge%d" % i) for i in range(len(view.getEdgeNames()))],
            list(view.getVertexNames()),
            list(view.getFaceNames()),
        )

    def reopen(self, keepUpdated):
        self.page.KeepUpdated = keepUpdated
        FreeCAD.ActiveDocument.recompute()
        self.wait(500)
        before = self.snapshot(self.view)

        FreeCAD.ActiveDocument.saveAs(self.file)
        FreeCAD.closeDocument(self.docName)
        doc = FreeCAD.openDocument(self.file)
        self.wait(1000)
        return before, self.snapshot(doc.getObject("View"))

    def testAViewStoresWhatItProjected(self):
        """the projection is in the property, not only in memory"""
        edges, vertices, faces = self.view.ProjectedGeometry
        self.assertEqual(edges, len(self.view.getEdgeNames()))
        self.assertEqual(vertices, len(self.view.getVertexNames()))
        self.assertEqual(faces, len(self.view.getFaceNames()))
        self.assertTrue(edges > 0, "nothing was stored for a view that projected")

    def testTheProjectionSurvivesAReload(self):
        """same numbering, same names, element for element"""
        before, after = self.reopen(True)
        self.assertEqual(before, after)

    def testAPageThatCannotUpdateStillHasItsGeometry(self):
        """KeepUpdated off means nothing may project on restore, so geometry
        that is there can only have come out of the file"""
        before, after = self.reopen(False)
        self.assertEqual(before, after)
        self.assertTrue(after[1] > 0, "a page that cannot update came back empty")


if __name__ == "__main__":
    unittest.main()
