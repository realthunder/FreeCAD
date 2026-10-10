import os
import re
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD

from .TechDrawTestUtilities import createPageWithSVGTemplate


class RestoredEnumerationTest(unittest.TestCase):
    """A file can hold an index an enumeration does not have. The property
    kept it, and whatever asked for its text afterwards threw -- at load, at
    a recompute, in a paint. A TechDraw object now puts such an enumeration
    back to what a new object holds, as it is restored."""

    # object, property, the index written into the file
    CASES = (
        ("Page", "ProjectionType", 7),
        ("View", "ScaleType", 99),
        ("Balloon", "BubbleShape", 99),
        ("Balloon", "EndType", -3),
    )

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.doc = FreeCAD.newDocument("TDRestoredEnum")
        FreeCAD.setActiveDocument(self.doc.Name)

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)
        shutil.rmtree(self.dir, ignore_errors=True)

    def testOutOfRangeEnumerationIsPutBack(self):
        doc = self.doc
        box = doc.addObject("Part::Box", "Box")
        page = createPageWithSVGTemplate()
        view = doc.addObject("TechDraw::DrawViewPart", "View")
        view.Source = [box]
        page.addView(view)
        balloon = doc.addObject("TechDraw::DrawViewBalloon", "Balloon")
        balloon.SourceView = view
        page.addView(balloon)
        doc.recompute()
        self.assertEqual(page.Name, "Page")
        fresh = {(name, prop): getattr(doc.getObject(name), prop) for name, prop, _ in self.CASES}
        for value in fresh.values():
            self.assertIsInstance(value, str)

        good = os.path.join(self.dir, "good.FCStd")
        bad = os.path.join(self.dir, "bad.FCStd")
        doc.saveAs(good)
        FreeCAD.closeDocument(doc.Name)

        # the same file, with indexes no list has
        with zipfile.ZipFile(good) as src, zipfile.ZipFile(bad, "w", zipfile.ZIP_DEFLATED) as dst:
            for item in src.infolist():
                data = src.read(item.filename)
                if item.filename == "Document.xml":
                    text = data.decode("utf-8")
                    for name, prop, index in self.CASES:
                        start = text.index('<Object name="%s"' % name, text.index("<ObjectData"))
                        end = text.index("</Object>", start)
                        block, count = re.subn(
                            r'(<Property name="%s" type="App::PropertyEnumeration"[^>]*>\s*'
                            r'<Integer value=")-?\d+(")' % prop,
                            r"\g<1>%d\g<2>" % index,
                            text[start:end],
                        )
                        self.assertEqual(count, 1, "%s.%s not found in the file" % (name, prop))
                        text = text[:start] + block + text[end:]
                    data = text.encode("utf-8")
                dst.writestr(item, data)

        self.doc = FreeCAD.openDocument(bad)
        for name, prop, _ in self.CASES:
            obj = self.doc.getObject(name)
            self.assertIsNotNone(obj, name)
            self.assertEqual(getattr(obj, prop), fresh[(name, prop)], "%s.%s" % (name, prop))
        # and the document works: a recompute asks these for their text
        self.doc.recompute()
        for name in ("Page", "View", "Balloon"):
            self.assertTrue(self.doc.getObject(name).isValid(), name)


if __name__ == "__main__":
    unittest.main()
