# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD contributors                               *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# ***************************************************************************

"""How a document stores its shapes (docs/SharedShapeStorage.md).

  ShapeLocationCases  location canonicalization (sec 11.4, build step 12.2):
                      the geometry goes out at the identity and the top level
                      location is written as a `loc=` attribute instead

Run headless with:  FreeCADCmd -t ShapeStorage
"""

import os
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD

try:
    import Part

    HAS_PART = True
except ImportError:
    HAS_PART = False

BLOB_DIR = "blobs"
BLOB_INDEX = "Content.xml"


@unittest.skipUnless(HAS_PART, "Part module not available")
class ShapeLocationCases(unittest.TestCase):
    """The location lives in the XML, never in the geometry.

    Three things follow from that and are pinned down here: a placement still
    survives a round trip (it must -- outside a recompute Feature::onChanged
    reads Placement back out of the shape's own transform, so geometry
    announced at the identity would zero it), moving an object no longer
    rewrites its geometry, and two equal parts at different placements
    serialize to the same bytes.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="fc_shape_test_")
        self.docs = []

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ----------------------------------------------------------

    def newDocument(self, name="ShapeDoc"):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        # A new document defaults to upstream's schema, and canonicalized
        # locations are part of this fork's -- an incompatible file is chosen,
        # never inherited. The case that wants the default says so itself.
        doc.SaveSchemaVersion = 5
        return doc

    def openDocument(self, path):
        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        return doc

    def box(self, doc, name, placement=None, length=10, width=20, height=30):
        obj = doc.addObject("Part::Box", name)
        obj.Length = length
        obj.Width = width
        obj.Height = height
        if placement is not None:
            obj.Placement = placement
        return obj

    def placement(self, x, y, z, angle=0.0, axis=(0, 0, 1)):
        return FreeCAD.Placement(
            FreeCAD.Vector(x, y, z), FreeCAD.Rotation(FreeCAD.Vector(*axis), angle)
        )

    def projectPath(self, name="project.FCStd"):
        return os.path.join(self.tmp, name)

    def directoryPath(self, name="project_dir"):
        path = os.path.join(self.tmp, name)
        os.makedirs(path)
        return path

    # -- assertions --------------------------------------------------------

    def documentXml(self, project):
        """All of a project's XML.

        A directory project is split per object (SplitXML, on by default in
        this fork), so a shape's element is not in Document.xml at all.
        """
        if os.path.isdir(project):
            parts = []
            for name in sorted(os.listdir(project)):
                if name.endswith(".xml"):
                    with open(os.path.join(project, name), "rb") as handle:
                        parts.append(handle.read().decode("utf-8"))
            return "\n".join(parts)
        archive = zipfile.ZipFile(project)
        return "\n".join(
            archive.read(name).decode("utf-8")
            for name in sorted(archive.namelist())
            if name.endswith(".xml") and not name.startswith(BLOB_DIR + "/")
        )

    def storedGeometry(self, project):
        """The serialized geometry of a project, wherever this writer put it.

        A directory project writes ASCII BRep inside each object's XML -- that
        is the whole point of the directory format -- while an archive puts it
        in the shape store, one content-addressed blob. Either way what is
        compared is the geometry and nothing around it.
        """
        if os.path.isdir(project):
            bodies = []
            for name in sorted(os.listdir(project)):
                if not name.endswith(".xml"):
                    continue
                with open(os.path.join(project, name), "rb") as handle:
                    text = handle.read().decode("utf-8")
                for chunk in text.split("<Part ")[1:]:
                    head, _, rest = chunk.partition(">")
                    if "brep=" in head or "binary=" in head:
                        bodies.append(rest.split("</Part>")[0])
            return bodies
        archive = zipfile.ZipFile(project)
        entries = sorted(
            n
            for n in archive.namelist()
            if n.startswith(BLOB_DIR + "/") and n != "%s/%s" % (BLOB_DIR, BLOB_INDEX)
        )
        return [archive.read(n) for n in entries]

    def assertPlacement(self, obj, expected, msg=""):
        actual = obj.Placement
        self.assertLess(
            (actual.Base - expected.Base).Length,
            1e-9,
            "%s position %s != %s" % (msg, actual.Base, expected.Base),
        )
        self.assertLess(
            abs(actual.Rotation.Angle - expected.Rotation.Angle),
            1e-9,
            "%s rotation %s != %s" % (msg, actual.Rotation, expected.Rotation),
        )

    # -- cases -------------------------------------------------------------

    def testPlacementSurvivesRoundTrip(self):
        """The whole reason the location cannot simply be dropped."""
        doc = self.newDocument()
        wanted = {
            "Plain": self.placement(0, 0, 0),
            "Moved": self.placement(11, -22.5, 3.25),
            "Turned": self.placement(1.5, 2.5, 3.5, angle=37.0, axis=(1, 2, 3)),
        }
        for name, placement in wanted.items():
            self.box(doc, name, placement)
        doc.recompute()
        elements = {n: doc.getObject(n).Shape.ElementMapSize for n in wanted}
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        for name, placement in wanted.items():
            obj = reopened.getObject(name)
            self.assertPlacement(obj, placement, name)
            # The element map is written against the same geometry and is
            # index-based, so taking the location off must not disturb it --
            # a mismatch would come back as a document asking to recompute.
            self.assertEqual(obj.Shape.ElementMapSize, elements[name], name)
            self.assertEqual(obj.State, ["Up-to-date"], name)
            # The geometry has to have moved with it, not just the property.
            self.assertLess(
                (obj.Shape.BoundBox.Center - placement.multVec(FreeCAD.Vector(5, 10, 15))).Length,
                1e-9,
                "%s geometry is not where its placement says" % name,
            )

    def testServingTheGeometryDoesNotZeroThePlacement(self):
        """The restore trap, at the moment it would fire.

        A schema-5 shape is served out of the store on first use, long after
        the placement was restored. That serve announces a value, and outside
        a recompute Feature::onChanged answers by reading Placement out of the
        shape -- so the location has to be back on before the announcement.
        """
        doc = self.newDocument()
        wanted = self.placement(7, 8, 9, angle=45.0, axis=(0, 1, 0))
        self.box(doc, "Moved", wanted)
        doc.recompute()
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        obj = reopened.getObject("Moved")
        self.assertPlacement(obj, wanted, "before the shape is touched")
        self.assertFalse(obj.Shape.isNull())
        self.assertPlacement(obj, wanted, "after the shape is served")

    def testLocationIsWrittenAsAnAttribute(self):
        doc = self.newDocument()
        self.box(doc, "Plain")
        self.box(doc, "Moved", self.placement(11, -22.5, 3.25))
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.documentXml(project)
        # One placed shape, one at the identity -- and an identity location is
        # nothing to say, so exactly one attribute is expected. A placement of
        # zero is still a location object (it just does nothing), so this is
        # the assertion that stops it from being written.
        self.assertEqual(xml.count(' loc="'), 1)
        self.assertIn('loc="1 0 0 11 0 1 0 -22.5 0 0 1 3.25"', xml)
        # Round numbers stay round: the shortest text that reads back exactly.
        self.assertNotIn("-22.500000", xml)
        # And with the location gone, the two boxes are the same geometry.
        bodies = self.storedGeometry(project)
        self.assertEqual(len(bodies), 2)
        self.assertEqual(bodies[0], bodies[1])

    def moveLeavesTheGeometryUntouched(self, project):
        doc = self.newDocument()
        obj = self.box(doc, "Moved", self.placement(1, 2, 3))
        doc.recompute()
        doc.saveAs(project)
        before = self.storedGeometry(project)
        self.assertTrue(before, "the document stored no geometry to compare")

        moved = self.placement(40, 50, 60, angle=15.0)
        obj.Placement = moved
        doc.recompute()
        doc.save()
        self.assertEqual(self.storedGeometry(project), before)

        FreeCAD.closeDocument(doc.Name)
        reopened = self.openDocument(project)
        self.assertPlacement(reopened.getObject("Moved"), moved)

    def testMoveLeavesTheGeometryUntouched(self):
        """The point of the step: a move is an XML diff, not a rewrite."""
        self.moveLeavesTheGeometryUntouched(self.directoryPath())

    def testMoveLeavesTheStoredGeometryUntouched(self):
        """The same, through the archive writer -- where the geometry is the
        shape store, and an unchanged store is a blob the manager's hash skip
        does not write at all."""
        self.moveLeavesTheGeometryUntouched(self.projectPath())

    def testPlacementDoesNotReachTheGeometry(self):
        """Two documents differing only in placement store the same bytes.

        This is what makes duplicates visible to whole-file hashing: the
        fasteners of a real assembly are equal parts sitting at different
        placements, each with its own TShape (docs sec 11.4).
        """
        placed = self.newDocument("Placed")
        self.box(placed, "A", self.placement(0, 0, 0))
        self.box(placed, "B", self.placement(100, 5, -7, angle=30.0, axis=(1, 1, 0)))
        placed.recompute()
        placedProject = self.directoryPath("placed_dir")
        placed.saveAs(placedProject)

        flat = self.newDocument("Flat")
        self.box(flat, "A")
        self.box(flat, "B")
        flat.recompute()
        flatProject = self.directoryPath("flat_dir")
        flat.saveAs(flatProject)

        self.assertEqual(self.storedGeometry(placedProject), self.storedGeometry(flatProject))

    def testInlinedGeometryCarriesNoLocationEither(self):
        """Geometry written inside the XML is a separate branch of both the
        save and the restore -- and the one a directory project always takes,
        because ASCII BRep that version control can read is what it is for.
        """
        doc = self.newDocument()
        wanted = self.placement(4, 5, 6, angle=60.0, axis=(1, 0, 1))
        self.box(doc, "Moved", wanted)
        doc.recompute()
        project = self.directoryPath()
        doc.saveAs(project)
        xml = self.documentXml(project)
        self.assertIn(' loc="', xml)
        self.assertIn(' brep="1"', xml)
        # The location is not in the geometry as well: the BRep location table
        # of a shape written at the identity is empty.
        self.assertIn("Locations 0", xml)
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        obj = reopened.getObject("Moved")
        self.assertPlacement(obj, wanted)
        self.assertLess(
            (obj.Shape.BoundBox.Center - wanted.multVec(FreeCAD.Vector(5, 10, 15))).Length,
            1e-9,
        )

    def testCopyBetweenDocumentsKeepsThePlacement(self):
        """The export writer is self-contained by design and carries no
        schema, so it never canonicalizes -- the location stays in the
        geometry there, and has to still arrive."""
        source = self.newDocument("Source")
        wanted = self.placement(2, -3, 4, angle=90.0, axis=(0, 1, 0))
        obj = self.box(source, "Moved", wanted)
        source.recompute()
        target = self.newDocument("Target")
        copied = target.copyObject(obj)
        target.recompute()
        self.assertPlacement(copied, wanted)
        self.assertLess(
            (copied.Shape.BoundBox.Center - wanted.multVec(FreeCAD.Vector(5, 10, 15))).Length,
            1e-9,
        )

    def testDefaultSchemaIsUnchanged(self):
        """Schema 4 is upstream's format and stays exactly that: the geometry
        keeps its location, and an older reader that never heard of `loc=`
        reads the document unharmed."""
        doc = FreeCAD.newDocument("LegacyDoc")
        self.docs.append(doc.Name)
        wanted = self.placement(3, 4, 5, angle=20.0)
        self.box(doc, "Moved", wanted)
        doc.recompute()
        self.assertEqual(doc.SaveSchemaVersion, 4)
        project = self.projectPath("legacy.FCStd")
        doc.saveAs(project)
        self.assertNotIn(' loc="', self.documentXml(project))
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        self.assertPlacement(reopened.getObject("Moved"), wanted)


if __name__ == "__main__":
    unittest.main()
