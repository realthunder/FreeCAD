# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""A profile that is one wire of a sketch, Sketch.Wire2, reads as that.

docs/HandsOnQueue.md entry 92: a Pocket's profile shown as "Sketch047.2".
A link keeps, beside its value, the old-style name of the element it names.
Asked for that name of a WIRE by the wire's mapped name, the sketch -- which
names its own edges and vertexes and passes over the kinds it does not know
-- kept the number of "Wire2" and dropped the kind: "2". Files carry it:
<Sub value="2" shadowed="Wire2"/>. The value itself was right all along, and
so was the solid.
"""

import os
import tempfile
import unittest
import zipfile

import ArchiveMembers
import FreeCAD
import Part
import Sketcher

V = FreeCAD.Vector


class TestSketchWireProfile(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestSketchWireProfile")
        self.dir = tempfile.mkdtemp()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def makePad(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        for x, size in ((0, 10), (20, 6)):
            corners = [V(x, 0, 0), V(x + size, 0, 0), V(x + size, size, 0), V(x, size, 0)]
            first = sketch.GeometryCount
            for i in range(4):
                sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]), False)
            for i in range(4):
                sketch.addConstraint(
                    Sketcher.Constraint("Coincident", first + i, 2, first + (i + 1) % 4, 1)
                )
        self.Doc.recompute()
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = (sketch, ["Wire2"])
        pad.Length = 5
        self.Doc.recompute()
        return pad

    def reopen(self, path):
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        return self.Doc.getObject("Pad")

    def testWireProfileKeepsItsName(self):
        pad = self.makePad()
        self.assertEqual(len(self.Doc.Sketch.Shape.Wires), 2)
        self.assertEqual(list(pad.Profile[1]), ["Wire2"])
        self.assertAlmostEqual(pad.Shape.Volume, 6 * 6 * 5, places=6)
        path = os.path.join(self.dir, "WireProfile.FCStd")
        self.Doc.saveAs(path)
        pad = self.reopen(path)
        self.assertEqual(list(pad.Profile[1]), ["Wire2"])
        self.Doc.recompute(None, True)
        self.assertEqual(list(pad.Profile[1]), ["Wire2"])

    def testWireNamedByItsMappedName(self):
        """The sketch asked for the names of its wire by the wire's mapped name"""
        self.makePad()
        path = os.path.join(self.dir, "WireProfile.FCStd")
        self.Doc.saveAs(path)
        self.reopen(path)
        sketch = self.Doc.getObject("Sketch")
        # the name a reference to the wire had made for it, kept with the shape
        names = dict(sketch.Shape.ElementReverseMap)
        self.assertIn("Wire2", names)
        mapped = ";" + names["Wire2"] + ".Wire2"
        owner, new, old = sketch.resolveSubElement(mapped)
        self.assertEqual(owner, sketch)
        # "2" it was: the kind dropped, the number kept
        self.assertEqual(old, "Wire2")
        self.assertEqual(new, mapped)
        # and by its plain name, as before
        owner, new, old = sketch.resolveSubElement("Wire2")
        self.assertEqual((new, old), (mapped, "Wire2"))

    def testWireProfileFromAFileThatSays2(self):
        """A file that carries the number alone is put right by a recompute"""
        self.makePad()
        path = os.path.join(self.dir, "WireProfile.FCStd")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.newDocument("PartDesignTestSketchWireProfile")

        # the Sub element of Pad's Profile, as the reporter's file has it
        patched = os.path.join(self.dir, "WireProfileOld.FCStd")
        found = 0
        with zipfile.ZipFile(patched, "w", zipfile.ZIP_DEFLATED) as dst:
            for info, data in ArchiveMembers.members(path):
                if info.filename == "Document.xml":
                    text = data.decode("utf-8")
                    at = text.index('<Property name="Profile"')
                    start = text.index("<Sub value=", at)
                    end = text.index("/>", start) + 2
                    self.assertIn('value="Wire2"', text[start:end])
                    text = text[:start] + '<Sub value="2" shadowed="Wire2"/>' + text[end:]
                    data = text.encode("utf-8")
                    found += 1
                dst.writestr(info, data)
        self.assertEqual(found, 1)

        pad = self.reopen(patched)
        # as the file says, until the sketch is asked again
        self.assertIn(list(pad.Profile[1]), (["2"], ["Wire2"]))
        # the sketch made again, as the recompute an older file asks for at
        # its opening makes it
        self.Doc.Sketch.touch()
        self.Doc.recompute()
        self.assertEqual(list(pad.Profile[1]), ["Wire2"])
        self.assertAlmostEqual(pad.Shape.Volume, 6 * 6 * 5, places=6)
