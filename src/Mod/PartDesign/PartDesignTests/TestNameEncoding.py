# SPDX-License-Identifier: LGPL-2.1-or-later

"""Element references in documents written in either encoding of indexed
names survive an edit: upstream FreeCAD hashes a name's trailing index apart
from its text, this fork did not by default until 2026-10, and a document's
string hasher now keeps the encoding it was written in.

Both fixtures hold the same five bodies, each the TestLoft two-pad base with a
feature on a named element of the upper pad: a loft between two of its faces,
a pocket and a pad from sketches attached to faces, a fillet on a face and a
chamfer on an edge. FaceRefs_v1-1-4.FCStd was made by FreeCAD 1.1.4, and
FaceRefs_fork_whole_names.FCStd by this fork before the change. Neither file
says which encoding it used; the table shows it.
"""

import os
import tempfile
import unittest

import FreeCAD

FIXTURES = os.path.join(os.path.dirname(__file__), "Fixtures")

# (feature, property) of every element reference in the fixtures
REFERENCES = [
    ("Loft_loft", "Profile"),
    ("Loft_loft", "Sections"),
    ("SkTop_pocket", "AttachmentSupport"),
    ("SkSide_pad", "AttachmentSupport"),
    ("Fillet_fillet", "Base"),
    ("Chamfer_chamfer", "Base"),
]


class TestNameEncoding(unittest.TestCase):
    def setUp(self):
        self.Doc = None

    def tearDown(self):
        if self.Doc:
            FreeCAD.closeDocument(self.Doc.Name)

    def open(self, name):
        self.Doc = FreeCAD.openDocument(os.path.join(FIXTURES, name))
        return self.Doc

    def subNames(self):
        names = []
        for feature, prop in REFERENCES:
            value = getattr(self.Doc.getObject(feature), prop)
            for _obj, subs in value if isinstance(value, list) else [value]:
                names += list(subs) if isinstance(subs, (list, tuple)) else [subs]
        return names

    def editAndCheck(self, volumes):
        """Lengthen both pads of every body, which moves every referenced
        element, and check each reference still finds its element"""
        for obj in self.Doc.Objects:
            if obj.Name.startswith("PadA_"):
                obj.Length = 14
            elif obj.Name.startswith("PadB_"):
                obj.Length = 12
        self.Doc.recompute()
        names = self.subNames()
        self.assertEqual(len(names), len(REFERENCES))
        for name in names:
            self.assertFalse(name.startswith("?"), "missing reference %s" % name)
        for feature, volume in volumes.items():
            obj = self.Doc.getObject(feature)
            self.assertNotIn("Invalid", obj.State, feature)
            self.assertTrue(obj.Shape.isValid(), feature)
            self.assertAlmostEqual(obj.Shape.Volume, volume, places=3, msg=feature)

    def testUpstreamFileKeepsItsReferences(self):
        """A document from upstream indexes names, and edits like upstream:
        the volumes are FreeCAD 1.1.4's for the same edit"""
        self.open("FaceRefs_v1-1-4.FCStd")
        self.assertTrue(self.Doc.Hasher.IndexedNames)
        self.editAndCheck(
            {
                "Loft_loft": 12276.1647,
                "Pocket_pocket": 11560.9649,
                "Pad_pad": 11711.7613,
                "Fillet_fillet": 11670.1179,
                "Chamfer_chamfer": 11664.9420,
            }
        )

    def testForkFileKeepsItsReferences(self):
        """A document this fork wrote with whole names keeps them. Its loft
        is on another face than the upstream file's (the fork numbers the
        pad's faces differently), hence the other loft volume"""
        self.open("FaceRefs_fork_whole_names.FCStd")
        self.assertFalse(self.Doc.Hasher.IndexedNames)
        self.editAndCheck(
            {
                "Loft_loft": 12238.1843,
                "Pocket_pocket": 11560.9649,
                "Pad_pad": 11711.7613,
                "Fillet_fillet": 11670.1179,
                "Chamfer_chamfer": 11664.9420,
            }
        )

    def testEncodingIsSaved(self):
        """Saved again, a document keeps its encoding, now stored"""
        for name, indexed in (
            ("FaceRefs_fork_whole_names.FCStd", False),
            ("FaceRefs_v1-1-4.FCStd", True),
        ):
            self.open(name)
            path = os.path.join(tempfile.gettempdir(), "PartDesignTestNameEncoding.FCStd")
            self.Doc.saveAs(path)
            FreeCAD.closeDocument(self.Doc.Name)
            self.Doc = FreeCAD.openDocument(path)
            self.assertEqual(self.Doc.Hasher.IndexedNames, indexed, name)
            FreeCAD.closeDocument(self.Doc.Name)
            self.Doc = None
            os.remove(path)

    def testNewDocumentTakesThePreference(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestNameEncoding")
        param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
        self.assertEqual(self.Doc.Hasher.IndexedNames, param.GetBool("HashIndexedName", True))
