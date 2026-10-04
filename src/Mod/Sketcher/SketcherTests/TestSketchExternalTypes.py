# SPDX-License-Identifier: LGPL-2.1-or-later
# Tests for the kind of an external reference: projected onto the sketch
# plane, cut by it, or both (SketchObject.ExternalTypes, 0 / 1 / 2, as
# upstream keeps it since 0e5e071d72).
#
# The kind used to be a flag on the geometry of the reference. A file written
# that way is migrated when it is read, and so is one written by upstream,
# which has the property and neither the flag nor the sketch's version. Both
# are made here by rewriting a saved document: there is no build of either to
# write them.

import os
import re
import tempfile
import unittest
import zipfile

import FreeCAD as App
import Part
import Sketcher
from FreeCAD import Vector

PROJECTION, INTERSECTION, BOTH = 0, 1, 2
INTERSECTION_FLAG = 32  # ExternalGeometryExtension::Intersection, as a bit


def externals(sketch):
    """(id, short type, geometry) of every external geometry with a reference."""
    out = []
    for geo in sketch.ExternalGeo:
        facade = Sketcher.ExternalGeometryFacade(geo)
        if facade.Ref:
            out.append((facade.Id, geo.TypeId.split("::Geom")[1], geo))
    return out


def shapes(sketch):
    return [name for _, name, _ in externals(sketch)]


def flagged(sketch):
    """Which of the referring geometries carry the Intersection flag."""
    out = []
    for geo in sketch.ExternalGeo:
        facade = Sketcher.ExternalGeometryFacade(geo)
        if facade.Ref:
            out.append(facade.testFlag("Intersection"))
    return out


def rewriteSketches(source, target, edit):
    """Copy a saved document with the XML of each sketch passed through edit()."""
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as out:
        for item in zin.infolist():
            data = zin.read(item.filename)
            if item.filename == "Document.xml":
                text = data.decode("utf-8")
                text = re.sub(
                    r'<Object name="Sketch[^"]*".*?</Object>',
                    lambda m: edit(m.group(0)),
                    text,
                    flags=re.S,
                )
                data = text.encode("utf-8")
            out.writestr(item, data)


def dropProperty(xml, name):
    xml, n = re.subn(
        r' *<Property name="%s".*?</Property>\r?\n' % name, "", xml, count=1, flags=re.S
    )
    if n:
        xml = re.sub(
            r'<Properties Count="(\d+)"',
            lambda m: '<Properties Count="%d"' % (int(m.group(1)) - 1),
            xml,
            count=1,
        )
    return xml


def setFlags(xml, change):
    """change(flags, index) for each referring external geometry, in order."""
    index = [0]

    def one(m):
        if not m.group(2):
            return m.group(0)
        flags = change(int(m.group(4)), index[0])
        index[0] += 1
        return '%s"%s"%s"%d"' % (m.group(1), m.group(2), m.group(3), flags)

    return re.sub(
        r'(Sketcher::ExternalGeometryExtension"[^>]*? Ref=)"([^"]*)"([^>]*? Flags=)"(\d+)"',
        one,
        xml,
    )


def asOldFork(xml):
    """What this fork wrote before the property: the kind is the flag alone."""
    return dropProperty(xml, "ExternalTypes")


def asUpstream(xml):
    """What upstream writes: the property, no flag, no sketch version."""
    xml = setFlags(xml, lambda flags, index: flags & ~INTERSECTION_FLAG)
    return dropProperty(xml, "_Version")


def asForkBeforeVersion2(xml):
    """An old file of this fork from before version 2, where an edge taken by
    intersection came with its projection and every piece was flagged."""
    xml = asOldFork(xml)
    xml = setFlags(xml, lambda flags, index: flags | INTERSECTION_FLAG)
    return re.sub(
        r'(<Property name="_Version".*?<Integer value=)"\d+"', r'\1"1"', xml, flags=re.S
    )


class TestSketchExternalTypes(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("TestSketchExternalTypes")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        # A plane through (0, 0, 5) tilted 45 degrees about X: it cuts the two
        # upright edges at y = 0, and neither of those is square to it, so an
        # edge's projection is a line and its cut a point.
        self.sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        self.sketch.Placement = App.Placement(Vector(0, 0, 5), App.Rotation(Vector(1, 0, 0), 45))
        self.near = self.far = self.face = None
        for i, edge in enumerate(self.box.Shape.Edges):
            p, q = edge.Vertexes[0].Point, edge.Vertexes[1].Point
            if abs(p.y) < 1e-6 and abs(q.y) < 1e-6 and abs(p.x - q.x) < 1e-6:
                if abs(p.x) < 1e-6:
                    self.near = "Edge%d" % (i + 1)
                else:
                    self.far = "Edge%d" % (i + 1)
        for i, face in enumerate(self.box.Shape.Faces):
            if abs(face.CenterOfMass.x - 10) < 1e-6:
                self.face = "Face%d" % (i + 1)
        self.doc.recompute()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def kinds(self, sketch=None):
        return list((sketch or self.sketch).ExternalTypes)

    def lengthen(self, sketch=None):
        """Move the far edge from x = 10 to x = 12, which rebuilds the externals."""
        self.doc.getObject("Box").Length = 12
        self.doc.recompute()
        return sketch or self.sketch

    def reopen(self, edit=None, schema=None):
        """Save, optionally rewrite the sketches' XML, and open the result.

        Upstream writes schema 4. This fork's own schema leaves a property
        out when it is at its default, so there a missing one says nothing.
        """
        tmp = tempfile.mkdtemp()
        path = os.path.join(tmp, "saved.FCStd")
        if schema:
            self.doc.SaveSchemaVersion = schema
        self.doc.saveAs(path)
        App.closeDocument(self.doc.Name)
        if edit:
            forged = os.path.join(tmp, "forged.FCStd")
            rewriteSketches(path, forged, edit)
            path = forged
        self.doc = App.openDocument(path)
        self.sketch = self.doc.getObject("Sketch")
        return self.sketch

    def assertCutAtFarEdge(self, sketch, x):
        points = [geo for _, name, geo in externals(sketch) if name == "Point"]
        self.assertEqual(len(points), 1, shapes(sketch))
        self.assertAlmostEqual(points[0].X, x)
        self.assertAlmostEqual(points[0].Y, 0.0)

    # --- the kinds -------------------------------------------------------

    def testProjectionIsTheDefault(self):
        self.sketch.addExternal("Box", self.far)
        self.assertEqual(self.kinds(), [PROJECTION])
        self.assertEqual(shapes(self.sketch), ["LineSegment"])
        self.assertEqual(flagged(self.sketch), [False])

    def testIntersection(self):
        self.sketch.addExternal("Box", self.far, False, True)
        self.assertEqual(self.kinds(), [INTERSECTION])
        self.assertEqual(shapes(self.sketch), ["Point"])
        self.assertEqual(flagged(self.sketch), [True])
        self.assertCutAtFarEdge(self.lengthen(), 12)

    def testTheSameKindTwiceIsRefused(self):
        self.sketch.addExternal("Box", self.far, False, True)
        with self.assertRaises(ValueError):
            self.sketch.addExternal("Box", self.far, False, True)
        self.assertEqual(self.kinds(), [INTERSECTION])

    def testBothWaysProjectionFirst(self):
        self.sketch.addExternal("Box", self.far)
        self.sketch.addExternal("Box", self.far, False, True)
        self.assertEqual(self.kinds(), [BOTH])
        self.assertEqual(len(self.sketch.ExternalGeometry), 1)
        self.assertEqual(shapes(self.sketch), ["LineSegment", "Point"])
        self.assertEqual(flagged(self.sketch), [False, True])
        for intersection in (False, True):
            with self.assertRaises(ValueError):
                self.sketch.addExternal("Box", self.far, False, intersection)
        sketch = self.lengthen()
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertCutAtFarEdge(sketch, 12)

    def testBothWaysIntersectionFirst(self):
        # The cut is there first and something is constrained to it; taking
        # the projection as well must leave the cut what and where it was.
        self.sketch.addExternal("Box", self.far, False, True)
        self.sketch.addGeometry(Part.Point(Vector(3, 1, 0)))
        self.sketch.addConstraint(Sketcher.Constraint("Coincident", 0, 1, -3, 1))
        self.doc.recompute()
        cut = externals(self.sketch)[0][0]
        self.sketch.addExternal("Box", self.far)
        self.doc.recompute()
        self.assertEqual(self.kinds(), [BOTH])
        ext = externals(self.sketch)
        self.assertEqual([name for _, name, _ in ext], ["Point", "LineSegment"])
        self.assertEqual(ext[0][0], cut)
        self.assertEqual(flagged(self.sketch), [True, False])
        sketch = self.lengthen()
        self.assertEqual(externals(sketch)[0][0], cut)
        self.assertNotIn("Invalid", sketch.State)
        point = sketch.Geometry[0]
        self.assertAlmostEqual(point.X, 12)
        self.assertAlmostEqual(point.Y, 0)

    def testFaceBothWays(self):
        self.sketch.addExternal("Box", self.face, False, True)
        cut = shapes(self.sketch)
        self.assertEqual(self.kinds(), [INTERSECTION])
        self.sketch.addExternal("Box", self.face)
        self.assertEqual(self.kinds(), [BOTH])
        self.assertGreater(len(shapes(self.sketch)), len(cut))
        self.assertEqual(flagged(self.sketch).count(True), len(cut))

    # --- the list stays in step with the references -----------------------

    def testKindsFollowADeletedReference(self):
        self.sketch.addExternal("Box", self.near)
        self.sketch.addExternal("Box", self.far, False, True)
        self.assertEqual(self.kinds(), [PROJECTION, INTERSECTION])
        self.sketch.delExternal(0)
        self.assertEqual(self.kinds(), [INTERSECTION])
        self.assertEqual(shapes(self.sketch), ["Point"])
        self.assertCutAtFarEdge(self.lengthen(), 12)

    def testKindSurvivesItsElementGoingMissing(self):
        # Lifted clear of the box the plane cuts nothing: the reference is
        # dropped and its geometry kept as missing. Put back, it is the cut
        # again, not the projection.
        self.sketch.addExternal("Box", self.far, False, True)
        tilt = self.sketch.Placement.Rotation
        self.sketch.Placement = App.Placement(Vector(0, 0, 50), tilt)
        self.doc.recompute()
        self.assertEqual(self.kinds(), [])
        self.sketch.Placement = App.Placement(Vector(0, 0, 5), tilt)
        self.doc.recompute()
        self.assertEqual(self.kinds(), [INTERSECTION])
        self.assertEqual(shapes(self.sketch), ["Point"])

    def links(self, sketch=None):
        return [
            (obj.Name, sub)
            for obj, subs in (sketch or self.sketch).ExternalGeometry
            for sub in subs
        ]

    def looseSource(self):
        """A box whose shape, and with it every element, can be taken away."""
        source = self.doc.addObject("Part::Feature", "Source")
        source.Shape = Part.makeBox(10, 10, 10)
        return source

    def takeAway(self):
        self.doc.getObject("Source").Shape = Part.Vertex(0, 0, 0)
        self.doc.recompute()
        self.assertEqual(self.links(), [])

    def giveBack(self):
        """The source again, its far edge at x = 12."""
        self.doc.getObject("Source").Shape = Part.makeBox(12, 10, 10)
        self.doc.recompute()
        return self.sketch

    def farEdgeBothWaysAndGone(self):
        """The far edge of a loose source taken both ways, something
        constrained to its line and to its cut, and the source taken away.
        Gives the ids of the two geometries."""
        self.looseSource()
        self.sketch.addExternal("Source", self.far)
        self.sketch.addExternal("Source", self.far, False, True)
        self.sketch.addGeometry(Part.Point(Vector(3, 1, 0)))
        self.sketch.addGeometry(Part.Point(Vector(4, 2, 0)))
        self.sketch.addConstraint(Sketcher.Constraint("PointOnObject", 0, 1, -3))
        self.sketch.addConstraint(Sketcher.Constraint("Coincident", 1, 1, -4, 1))
        self.doc.recompute()
        self.assertEqual(shapes(self.sketch), ["LineSegment", "Point"])
        ids = [id for id, _, _ in externals(self.sketch)]
        self.takeAway()
        return ids

    def testMissingReferenceComesBackOnce(self):
        # Two geometries are missing the one reference. It is put back as
        # one reference, of the kind it was, and they keep their ids and
        # what is constrained to them.
        ids = self.farEdgeBothWaysAndGone()
        sketch = self.giveBack()
        self.assertEqual(self.links(), [("Source", self.far)])
        self.assertEqual(self.kinds(), [BOTH])
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
        self.assertEqual(len(sketch.Constraints), 2)
        self.assertCutAtFarEdge(sketch, 12)

    def testWholeObjectIsOneReference(self):
        # No element named: the object itself is the reference, one however
        # much is in it, and of both kinds like any other.
        self.sketch.addExternal("Box", "")
        self.assertEqual(self.links(), [("Box", "")])
        self.assertEqual(self.kinds(), [PROJECTION])
        projected = len(shapes(self.sketch))
        self.sketch.addExternal("Box", "", False, True)
        self.assertEqual(self.links(), [("Box", "")])
        self.assertEqual(self.kinds(), [BOTH])
        self.assertGreater(len(shapes(self.sketch)), projected)
        with self.assertRaises(ValueError):
            self.sketch.addExternal("Box", "", False, True)
        self.assertEqual(self.kinds(), [BOTH])

    def testUndoAndRedo(self):
        self.doc.UndoMode = 1
        self.sketch.addExternal("Box", self.near)
        self.doc.recompute()
        self.doc.openTransaction("add")
        self.sketch.addExternal("Box", self.far, False, True)
        self.doc.commitTransaction()
        self.assertEqual(self.kinds(), [PROJECTION, INTERSECTION])
        self.doc.undo()
        self.assertEqual(self.kinds(), [PROJECTION])
        self.doc.redo()
        self.assertEqual(self.kinds(), [PROJECTION, INTERSECTION])
        self.assertEqual(shapes(self.lengthen()), ["LineSegment", "Point"])

    def testSaveAndLoad(self):
        self.sketch.addExternal("Box", self.near)
        self.sketch.addExternal("Box", self.far, False, True)
        self.doc.recompute()
        sketch = self.reopen()
        self.assertEqual(self.kinds(), [PROJECTION, INTERSECTION])
        self.assertEqual(shapes(self.lengthen()), ["LineSegment", "Point"])

    # --- files written before the property, and by upstream ---------------

    def testOldForkFile(self, schema=None):
        self.sketch.addExternal("Box", self.near)
        self.sketch.addExternal("Box", self.far, False, True)
        self.doc.recompute()
        sketch = self.reopen(asOldFork, schema)
        self.assertEqual(self.kinds(), [PROJECTION, INTERSECTION])
        self.assertEqual(sketch._Version, 2)
        sketch = self.lengthen()
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertCutAtFarEdge(sketch, 12)

    def testOldForkFileSchema4(self):
        self.testOldForkFile(schema=4)

    def testOldForkFileFromBeforeVersion2(self, schema=None):
        # Before version 2 an edge taken by intersection came with its
        # projection, every piece flagged. That is "both", and the pieces
        # keep their ids.
        self.sketch.addExternal("Box", self.far)
        self.sketch.addExternal("Box", self.far, False, True)
        self.doc.recompute()
        ids = [id for id, _, _ in externals(self.sketch)]
        sketch = self.reopen(asForkBeforeVersion2, schema)
        self.assertEqual(sketch._Version, 1)
        self.assertEqual(flagged(sketch), [True, True])
        self.assertEqual(self.kinds(), [BOTH])
        sketch = self.lengthen()
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
        self.assertEqual(flagged(sketch), [False, True])
        self.assertCutAtFarEdge(sketch, 12)

    def testOldForkFileFromBeforeVersion2Schema4(self):
        self.testOldForkFileFromBeforeVersion2(schema=4)

    def testOldForkFileFromBeforeVersion2MissingEdge(self):
        # The same edge, its element missing when the file was written: the
        # reference is not among the links, and only its geometry -- every
        # piece flagged -- says what it was. Back, it is of both kinds, and
        # the line keeps its id and what is constrained to it.
        ids = self.farEdgeBothWaysAndGone()
        sketch = self.reopen(asForkBeforeVersion2)
        self.assertEqual(sketch._Version, 1)
        self.assertEqual(self.links(), [])
        self.assertEqual(flagged(sketch), [True, True])
        sketch = self.giveBack()
        self.assertEqual(self.links(), [("Source", self.far)])
        self.assertEqual(self.kinds(), [BOTH])
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
        self.assertEqual(flagged(sketch), [False, True])
        self.assertEqual(len(sketch.Constraints), 2)
        self.assertCutAtFarEdge(sketch, 12)

    def testOldForkFileFromBeforeVersion2MissingFace(self):
        # A face taken by intersection was the cut alone before version 2 as
        # well, and stays that when it comes back.
        self.looseSource()
        self.sketch.addExternal("Source", self.face, False, True)
        self.doc.recompute()
        cut = shapes(self.sketch)
        ids = [id for id, _, _ in externals(self.sketch)]
        self.takeAway()
        sketch = self.reopen(asForkBeforeVersion2)
        self.assertEqual(self.links(), [])
        sketch = self.giveBack()
        self.assertEqual(self.links(), [("Source", self.face)])
        self.assertEqual(self.kinds(), [INTERSECTION])
        self.assertEqual(shapes(sketch), cut)
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
        self.assertTrue(all(flagged(sketch)))

    def testUpstreamFile(self):
        self.sketch.addExternal("Box", self.far, False, True)
        self.sketch.addExternal("Box", self.face, False, True)
        self.doc.recompute()
        before = shapes(self.sketch)
        sketch = self.reopen(asUpstream, schema=4)
        self.assertEqual(self.kinds(), [INTERSECTION, INTERSECTION])
        # written by an upstream that builds the way version 2 does
        self.assertEqual(sketch._Version, 2)
        self.assertTrue(all(flagged(sketch)))
        sketch = self.lengthen()
        self.assertEqual(shapes(sketch), before)
        self.assertCutAtFarEdge(sketch, 12)

    def testUpstreamFileBothWays(self):
        self.sketch.addExternal("Box", self.far)
        self.sketch.addExternal("Box", self.far, False, True)
        self.doc.recompute()
        ids = [id for id, _, _ in externals(self.sketch)]
        sketch = self.reopen(asUpstream, schema=4)
        self.assertEqual(self.kinds(), [BOTH])
        self.assertEqual(flagged(sketch), [False, False])
        sketch = self.lengthen()
        self.assertEqual(shapes(sketch), ["LineSegment", "Point"])
        self.assertEqual([id for id, _, _ in externals(sketch)], ids)
        self.assertEqual(flagged(sketch), [False, True])

    def testUpstreamFileSquareFace(self):
        # No intersection at all: a face square to the sketch, projected. In
        # a sketch taken for one from before version 1 it comes back 20000
        # long.
        flat = self.doc.addObject("Sketcher::SketchObject", "SketchFlat")
        flat.addExternal("Box", self.face)
        self.doc.recompute()
        self.assertAlmostEqual(externals(flat)[0][2].length(), 10)
        self.reopen(asUpstream, schema=4)
        flat = self.doc.getObject("SketchFlat")
        self.assertEqual(flat._Version, 2)
        self.lengthen()
        self.assertEqual(shapes(flat), ["LineSegment"])
        self.assertAlmostEqual(externals(flat)[0][2].length(), 10)
