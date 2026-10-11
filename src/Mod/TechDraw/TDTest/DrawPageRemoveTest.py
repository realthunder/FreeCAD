import FreeCAD
import unittest
from .TechDrawTestUtilities import createPageWithSVGTemplate


class DrawPageRemoveTest(unittest.TestCase):
    """A page removed outside a transaction takes its views with it, and survives doing so.

    Outside a transaction a removed object is deleted at once.  A part view removes its
    dimensions and balloons when it goes, and those stand in the page's list of views too:
    DrawPage::unsetupObject walked that list by the pointers it held before its loop, and
    reached a freed object -- a crash on a document of ten pages, 2026-10-11.
    """

    def setUp(self):
        self.document = FreeCAD.newDocument("TDPageRemove")
        FreeCAD.setActiveDocument(self.document.Name)
        self.document.addObject("Part::Box", "Box")
        self.page = createPageWithSVGTemplate(self.document)
        self.page.Scale = 5.0
        # the view first and what hangs on it after: the order that left freed pointers
        self.view = self.document.addObject("TechDraw::DrawViewPart", "View")
        self.page.addView(self.view)
        self.view.Source = [self.document.Box]
        self.names = ["Page", "Template", "View"]
        for i in range(12):
            dim = self.document.addObject("TechDraw::DrawViewDimension", "Dimension")
            self.page.addView(dim)
            dim.Type = "Distance"
            dim.References2D = [(self.view, "Edge1")]
            self.names.append(dim.Name)
        for i in range(4):
            balloon = self.document.addObject("TechDraw::DrawViewBalloon", "Balloon")
            balloon.SourceView = self.view
            self.page.addView(balloon)
            self.names.append(balloon.Name)
        self.document.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.document.Name)

    def testRemovePageOutsideTransaction(self):
        self.assertEqual(self.page.Views[0].Name, "View")
        self.assertGreater(len(self.page.Views), 12)
        self.document.removeObject("Page")
        left = [name for name in self.names if self.document.getObject(name) is not None]
        self.assertEqual(left, [])
        self.assertEqual([o.Name for o in self.document.Objects], ["Box"])

    def testRemoveViewOutsideTransaction(self):
        self.document.removeObject("View")
        self.assertIsNotNone(self.document.getObject("Page"))
        self.assertEqual([v.Name for v in self.page.Views], [])
        self.document.removeObject("Page")
        self.assertEqual([o.Name for o in self.document.Objects], ["Box"])


if __name__ == "__main__":
    unittest.main()
