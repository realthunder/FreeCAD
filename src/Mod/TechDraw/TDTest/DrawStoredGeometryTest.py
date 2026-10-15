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

    def testASilhouetteIsNamedFromItsFaceAndSurvivesAReload(self):
        """a silhouette has no source edge; it is named from the face it
        lies on, and the face index is stored with the geometry"""
        cyl = FreeCAD.ActiveDocument.addObject("Part::Cylinder", "Cyl")
        self.view.Source = [cyl]
        self.view.Direction = (1.0, 0.0, 0.0)
        FreeCAD.ActiveDocument.recompute()
        self.wait()

        def silhouettes(view):
            return sorted(
                name for name, source, index in view.getEdgeNames()
                if index == 0 and source.startswith("Face1")
            )

        before = silhouettes(self.view)
        self.assertEqual(len(before), 2, self.view.getEdgeNames())
        # every projected edge of the cylinder has a source now
        self.assertTrue(all(source for _, source, _ in self.view.getEdgeNames()))
        self.page.KeepUpdated = False
        FreeCAD.ActiveDocument.saveAs(self.file)
        FreeCAD.closeDocument(self.docName)
        doc = FreeCAD.openDocument(self.file)
        self.wait(1000)
        self.assertEqual(silhouettes(doc.getObject("View")), before)

    def plateWithHole(self):
        """the view onto a 10 by 10 plate, 2 thick, with a hole through it"""
        plate = FreeCAD.ActiveDocument.addObject("Part::Box", "Plate")
        plate.Length = plate.Width = 10.0
        plate.Height = 2.0
        hole = FreeCAD.ActiveDocument.addObject("Part::Cylinder", "Hole")
        hole.Radius = 2.0
        hole.Height = 2.0
        hole.Placement.Base = FreeCAD.Vector(5.0, 5.0, 0.0)
        cut = FreeCAD.ActiveDocument.addObject("Part::Cut", "Holed")
        cut.Base = plate
        cut.Tool = hole
        self.view.Source = [cut]
        self.view.Direction = (0.0, 0.0, 1.0)
        FreeCAD.ActiveDocument.recompute()
        self.wait()

    def testFacesByWireJoiner(self):
        """docs/HandsOnQueue.md entry 148: the WireJoiner finder makes the
        plate one face, the hole its hole; the finder of before made a face
        of each loop, the hole's too"""
        self.assertTrue(self.view.UseWireJoiner, "a new view finds its faces the new way")
        self.plateWithHole()
        joined = len(self.view.getFaceNames())
        self.view.UseWireJoiner = False
        FreeCAD.ActiveDocument.recompute()
        self.wait()
        former = len(self.view.getFaceNames())
        self.assertEqual(joined, 1)
        self.assertGreater(former, joined)

    def testAnOlderViewKeepsTheFormerFinder(self):
        """a view from a file without the property keeps the finder of
        before; one saved with it keeps its value"""
        import re
        import zipfile

        import ArchiveMembers

        self.page.KeepUpdated = False
        FreeCAD.ActiveDocument.saveAs(self.file)
        FreeCAD.closeDocument(self.docName)
        doc = FreeCAD.openDocument(self.file)
        self.wait(500)
        self.assertTrue(doc.getObject("View").UseWireJoiner)
        name = doc.Name
        FreeCAD.closeDocument(name)

        old = os.path.join(tempfile.gettempdir(), "TDStoredGeometryOld.FCStd")
        prop = re.compile(
            r'<Property name="UseWireJoiner" type="App::PropertyBool"[^>]*>.*?</Property>\s*', re.S)
        count = re.compile(r'<Properties Count="(\d+)"')
        with zipfile.ZipFile(old, "w", zipfile.ZIP_DEFLATED) as zout:
            for item, data in ArchiveMembers.members(self.file):
                if item.filename == "Document.xml":
                    xml = data.decode("utf-8")
                    found = list(prop.finditer(xml))
                    self.assertEqual(len(found), 1)
                    m = found[0]
                    # the object's property count, one less
                    heads = list(count.finditer(xml, 0, m.start()))
                    head = heads[-1]
                    xml = (xml[:head.start(1)] + str(int(head.group(1)) - 1)
                           + xml[head.end(1):m.start()] + xml[m.end():])
                    data = xml.encode("utf-8")
                zout.writestr(item, data)
        try:
            doc = FreeCAD.openDocument(old)
            self.wait(500)
            self.assertFalse(doc.getObject("View").UseWireJoiner)
            FreeCAD.closeDocument(doc.Name)
        finally:
            os.remove(old)


if __name__ == "__main__":
    unittest.main()
