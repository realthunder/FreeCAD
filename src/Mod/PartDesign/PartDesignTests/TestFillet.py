#***************************************************************************
#*   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU Lesser General Public License (LGPL)    *
#*   as published by the Free Software Foundation; either version 2 of     *
#*   the License, or (at your option) any later version.                   *
#*   for detail see the LICENCE text file.                                 *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU Library General Public License for more details.                  *
#*                                                                         *
#*   You should have received a copy of the GNU Library General Public     *
#*   License along with this program; if not, write to the Free Software   *
#*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#*   USA                                                                   *
#*                                                                         *
#***************************************************************************

from __future__ import division
from math import pi
import os
import re
import shutil
import tempfile
import unittest
import zipfile

import ArchiveMembers
import FreeCAD
import Part

class TestFillet(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFillet")

    def testFilletCubeToSphere(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.Fillet = self.Doc.addObject("PartDesign::Fillet","Fillet")
        self.Fillet.Base = (self.Box, ['Face'+str(i+1) for i in range(6)])
        self.Fillet.Radius = 4.999999
        self.Body.addObject(self.Fillet)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4/3 * pi * 5**3, places=3)
        #test UseAllEdges property
        self.Fillet.UseAllEdges = True
        self.Fillet.Base = (self.Box, ['']) # no subobjects, should still work
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4/3 * pi * 5**3, places=3)
        self.Fillet.Base = (self.Box, ['Face50']) # non-existent face, topo naming resilience
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4/3 * pi * 5**3, places=3)
        self.Fillet.UseAllEdges = False
        self.Fillet.Base = (self.Box, ['Face1'])
        self.Doc.recompute()
        self.assertNotAlmostEqual(self.Fillet.Shape.Volume, 4/3 * pi * 5**3, places=3)

    def _createBoxWithFillet(self):
        body = self.Doc.addObject('PartDesign::Body','Body')
        box = body.newObject('PartDesign::AdditiveBox','Box')
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()
        fillet = body.newObject('PartDesign::Fillet','Fillet')
        fillet.Base = (box, ['Edge1'])
        fillet.Radius = 1.0
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        return body, box, fillet

    def _findEdgeWithMatchCount(self, source, target, count):
        for index in range(1, len(source.Edges) + 1):
            name = 'Edge%d' % index
            matches = target.searchSubShape(source.getElement(name), needName=True)
            if len(matches) == count:
                return name, matches[0][0] if matches else None
        self.skipTest('Test model did not contain a suitable edge')

    def _removeUnderFollowup(self, count):
        body, box, fillet = self._createBoxWithFillet()
        oldEdge, newEdge = self._findEdgeWithMatchCount(fillet.Shape, box.Shape, count)
        followup = body.newObject('PartDesign::Fillet','FollowupFillet')
        followup.Base = (fillet, [oldEdge])
        followup.Radius = 0.25
        self.Doc.recompute()
        self.assertTrue(followup.isValid())
        body.removeObject(fillet)
        self.Doc.removeObject(fillet.Name)
        return box, followup, newEdge

    def testRemovingPreviousFeatureRelinksUniqueEdge(self):
        # (upstream 26c895c30d)
        box, followup, newEdge = self._removeUnderFollowup(1)
        self.assertEqual(followup.Base[0].Name, box.Name)
        self.assertEqual(list(followup.Base[1]), [newEdge])
        self.Doc.recompute()
        self.assertTrue(followup.isValid())
        self.assertLess(followup.Shape.Volume, 1000)

    def testRemovingPreviousFeatureKeepsUnmatchedEdgeBroken(self):
        # an edge the removed feature made has no counterpart: the fillet
        # reports it rather than rounding some other edge
        box, followup, _ = self._removeUnderFollowup(0)
        self.Doc.recompute()
        self.assertFalse(followup.isValid())

    def testSolidReferenceIsAllItsEdges(self):
        # (upstream f87d968447) a solid in Base stands for all its edges,
        # as a face does for its own; it was skipped as an invalid shape
        body = self.Doc.addObject('PartDesign::Body', 'SolidRefBody')
        box = body.newObject('PartDesign::AdditiveBox', 'SolidRefBox')
        self.Doc.recompute()
        fillet = body.newObject('PartDesign::Fillet', 'SolidRefFillet')
        fillet.Base = (box, ['Solid1'])
        fillet.Radius = 1
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        byRef = fillet.Shape.Volume
        fillet.Base = (box, ['Edge1'])
        fillet.UseAllEdges = True
        self.Doc.recompute()
        self.assertAlmostEqual(byRef, fillet.Shape.Volume, places=6)

    def testFailedEdgesLeftOut(self):
        # docs/HandsOnQueue.md entry 143: an edge whose fillet fails is left
        # out, the result holds the others' fillets, the feature is in error
        # still, and the faces the failed one had when last made are kept
        body = self.Doc.addObject('PartDesign::Body', 'PartialBody')
        box = body.newObject('PartDesign::AdditiveBox', 'PartialBox')
        box.Length = box.Width = 10.0
        box.Height = 2.0
        self.Doc.recompute()
        shape = box.Shape
        # a vertical edge, 2 long, between two faces 10 wide; and an edge on
        # the top, between the top and a side 2 high, away from it
        vertical = top = None
        for i, e in enumerate(shape.Edges):
            d = e.Vertexes[1].Point - e.Vertexes[0].Point
            if abs(d.z) > 1e-7:
                vertical = vertical or (i + 1, e)
        for i, e in enumerate(shape.Edges):
            d = e.Vertexes[1].Point - e.Vertexes[0].Point
            if abs(d.z) < 1e-7 and all(abs(v.Point.z - 2.0) < 1e-7 for v in e.Vertexes) \
                    and not any(v.isSame(w) for v in e.Vertexes for w in vertical[1].Vertexes):
                top = top or (i + 1, e)
        self.assertTrue(vertical and top)
        vertical, top = 'Edge%d' % vertical[0], 'Edge%d' % top[0]
        fillet = body.newObject('PartDesign::Fillet', 'PartialFillet')
        fillet.Base = (box, [vertical, top])
        fillet.Radius = 0.5
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        self.assertEqual(list(fillet.FailedEdges), [])
        self.assertTrue(fillet.FailedShape.isNull())
        whole = fillet.Shape.Volume

        # 3 rounds the vertical edge; the top one's side is 2 high
        fillet.Radius = 3.0
        self.Doc.recompute()
        self.assertFalse(fillet.isValid())
        self.assertEqual(list(fillet.FailedEdges), [top])
        self.assertTrue(fillet.Shape.isValid())
        self.assertEqual(len(fillet.Shape.Solids), 1)
        # the vertical fillet made, the top one not
        self.assertAlmostEqual(fillet.Shape.Volume, 200.0 - (1 - pi / 4) * 9 * 2, places=3)
        self.assertGreater(len(fillet.FailedShape.Faces), 0)
        # the top edge's faces as made at 0.5: along the edge, 10 long, 0.5
        # high
        bound = fillet.FailedShape.BoundBox
        self.assertAlmostEqual(max(bound.XLength, bound.YLength), 10.0, places=3)
        self.assertAlmostEqual(bound.ZLength, 0.5, places=3)

        # it fails again: the faces stay
        fillet.Radius = 3.5
        self.Doc.recompute()
        self.assertEqual(list(fillet.FailedEdges), [top])
        self.assertGreater(len(fillet.FailedShape.Faces), 0)

        fillet.Radius = 0.5
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        self.assertEqual(list(fillet.FailedEdges), [])
        self.assertTrue(fillet.FailedShape.isNull())
        self.assertAlmostEqual(fillet.Shape.Volume, whole, places=6)

    def testMissingEdgeLeftOut(self):
        # entry 143: a reference that is gone is left out too
        body = self.Doc.addObject('PartDesign::Body', 'MissingBody')
        box = body.newObject('PartDesign::AdditiveBox', 'MissingBox')
        self.Doc.recompute()
        fillet = body.newObject('PartDesign::Fillet', 'MissingFillet')
        fillet.Base = (box, ['Edge1', 'Edge50'])
        fillet.Radius = 1
        self.Doc.recompute()
        self.assertFalse(fillet.isValid())
        self.assertEqual(list(fillet.FailedEdges), ['Edge50'])
        self.assertLess(fillet.Shape.Volume, 1000.0 - 1e-6)

    def testMissingEdgeKeepsItsFaceAfterReopen(self):
        # entry 160 (the reporter, 2026-10-16: "scanner document, open then
        # recompute got missing edge on Adaptor, failed fillet face
        # missing"): a reference gone at the first recompute after a reopen
        # shows its fillet face as the file had it -- this session never
        # made it
        import os
        import tempfile

        body = self.Doc.addObject('PartDesign::Body', 'ReopenBody')
        wedge = body.newObject('PartDesign::AdditiveWedge', 'ReopenWedge')
        self.Doc.recompute()
        # a top edge along X: both ends at the top, at Z2min
        top = wedge.Shape.BoundBox.YMax
        name = None
        for i, e in enumerate(wedge.Shape.Edges):
            ends = e.Vertexes
            if (len(ends) == 2 and all(abs(v.Point.y - top) < 1e-7 for v in ends)
                    and all(abs(v.Point.z - wedge.Z2min.Value) < 1e-7 for v in ends)):
                name = 'Edge%d' % (i + 1)
        self.assertIsNotNone(name)
        fillet = body.newObject('PartDesign::Fillet', 'ReopenFillet')
        fillet.Base = (wedge, [name])
        fillet.Radius = 0.5
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        # the document's name kept: tearDown closes it by that
        path = os.path.join(tempfile.mkdtemp(), 'PartDesignTestFillet.FCStd')
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        wedge = self.Doc.getObject('ReopenWedge')
        fillet = self.Doc.getObject('ReopenFillet')
        # the top collapses to a line: the edge is gone
        wedge.X2min = 5
        wedge.X2max = 5
        self.Doc.recompute()
        # a reference gone is kept marked missing: '?Edge11'
        self.assertEqual([n.lstrip('?') for n in fillet.FailedEdges], [name])
        self.assertGreaterEqual(len(fillet.FailedShape.Faces), 1)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestFillet")
        # print ("omit closing document for debugging")


def _nameAt(shape, kind, point):
    """The name of the vertex at point, or of the edges ending there"""
    vertexes = [v for v in shape.Vertexes if v.Point.isEqual(point, 1e-7)]
    if kind == 'Vertex':
        return 'Vertex%d' % shape.findSubShape(vertexes[0])[1]
    return ['Edge%d' % shape.findSubShape(e)[1] for e in shape.Edges
            if any(v.isSame(vertexes[0]) for v in e.Vertexes)]


def _faceAt(shape, point, normal):
    """The name of the face through point whose normal there is normal"""
    for i, f in enumerate(shape.Faces):
        if f.isInside(point, 1e-7, True):
            u, v = f.Surface.parameter(point)
            if f.normalAt(u, v).isEqual(normal, 1e-7):
                return 'Face%d' % (i + 1)
    raise ValueError('no face at %s' % point)


def _toOldCorners(name, old, vertex):
    """Writes the document file name as old, its fillet's corners in the form
    of before they were a link: no link in Corners, the vertex in Base"""
    corners = r'(<FilletCorners count="\d+") link="1"(>.*?)\s*<LinkSub .*?</LinkSub>'
    base = (
        r'(<Property name="Base" type="App::PropertyLinkSub"[^>]*>\s*'
        r'<LinkSub value="Box" count=")'
    )
    # the blob entries of a saved file may be zstd members, which zipfile
    # reads only from Python 3.14 on
    with zipfile.ZipFile(old, "w", zipfile.ZIP_DEFLATED) as zout:
        for item, data in ArchiveMembers.members(name):
            if item.filename == "Document.xml":
                xml, n = re.subn(corners, r"\1\2", data.decode("utf-8"), flags=re.S)
                assert n == 1
                xml, n = re.subn(
                    base + r'(\d+)(">)',
                    lambda m: '%s%d%s<Sub value="%s"/>'
                    % (m.group(1), int(m.group(2)) + 1, m.group(3), vertex),
                    xml,
                )
                assert n == 1
                data = xml.encode("utf-8")
            zout.writestr(item, data)


class TestFilletCorners(unittest.TestCase):
    """The Corners of a fillet: its setback corners (docs/CornerBlending.md)"""

    Corner = FreeCAD.Vector(10, 10, 10)

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFilletCorners")
        self.Body = self.Doc.addObject('PartDesign::Body', 'Body')
        self.Box = self.Body.newObject('PartDesign::AdditiveBox', 'Box')
        self.Doc.recompute()
        self.Vertex = _nameAt(self.Box.Shape, 'Vertex', self.Corner)
        self.Edges = _nameAt(self.Box.Shape, 'Edge', self.Corner)
        self.Fillet = self.Body.newObject('PartDesign::Fillet', 'Fillet')
        self.Fillet.Base = (self.Box, self.Edges)
        self.Fillet.Radius = 1
        self.Doc.recompute()
        self.Plain = self.Fillet.Shape.Volume
        try:
            self.expected({self.Vertex: 0})
        except Exception as e:
            if 'OCCT fork' not in str(e):
                raise
            self.skipTest('setback corners need the OCCT fork')

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def expected(self, corners):
        edges = [self.Box.Shape.getElement(e) for e in self.Edges]
        return self.Box.Shape.makeFillet(1, edges, corners=corners).Volume

    def recompute(self):
        self.Doc.recompute()
        self.assertTrue(self.Fillet.isValid())
        self.assertTrue(self.Fillet.Shape.isValid())
        return self.Fillet.Shape.Volume

    def testSetback(self):
        self.Fillet.Corners = {self.Vertex: 2}
        self.assertEqual(self.Fillet.Corners, {self.Vertex: (2.0, {})})
        # the vertex is Corners' own link, not Base's: Base holds what is filleted
        self.assertNotIn(self.Vertex, self.Fillet.Base[1])
        self.assertAlmostEqual(self.recompute(), self.expected({self.Vertex: 2}), places=6)
        self.assertLess(self.Fillet.Shape.Volume, self.Plain - 0.1)
        patches = [f for f in self.Fillet.Shape.Faces
                   if f.Surface.TypeId == 'Part::GeomBSplineSurface']
        self.assertEqual(len(patches), 1)
        # no corners, the plain fillet again
        self.Fillet.Corners = {}
        self.assertAlmostEqual(self.recompute(), self.Plain, places=6)

    def testPerEdge(self):
        e = self.Edges
        self.Fillet.Corners = [(self.Vertex, (2, {e[0]: 3}))]
        self.assertAlmostEqual(self.recompute(),
                               self.expected({self.Vertex: (2, {e[0]: 3})}), places=6)
        self.Fillet.Corners = {self.Vertex: {e[0]: 3, e[1]: 1.5}}
        self.assertEqual(self.Fillet.Corners, {self.Vertex: (-1.0, {e[0]: 3.0, e[1]: 1.5})})
        self.assertAlmostEqual(self.recompute(),
                               self.expected({self.Vertex: {e[0]: 3, e[1]: 1.5}}), places=6)

    def testExpressions(self):
        v, e = self.Vertex, self.Edges[0]
        self.Fillet.Corners = {self.Vertex: 1}
        self.Fillet.setExpression('.Corners.%s.Setback' % v, '2 mm')
        self.Fillet.setExpression('.Corners.%s.%s' % (v, e), '1 mm + 2 mm')
        self.recompute()
        self.assertEqual(self.Fillet.Corners, {v: (2.0, {e: 3.0})})
        self.assertAlmostEqual(self.Fillet.Shape.Volume,
                               self.expected({v: (2, {e: 3})}), places=6)
        # and read by others
        other = self.Doc.addObject('Part::Box', 'Other')
        other.setExpression('Length', 'Fillet.Corners.%s.%s * 2' % (v, e))
        self.Doc.recompute()
        self.assertAlmostEqual(other.Length.Value, 6.0)

    def testSaveRestore(self):
        e = self.Edges
        corners = {self.Vertex: (2.0, {e[0]: 3.0})}
        self.Fillet.Corners = corners
        volume = self.recompute()
        path = tempfile.mkdtemp(prefix='FilletCorners')
        try:
            name = os.path.join(path, 'corners.FCStd')
            self.Doc.saveAs(name)
            FreeCAD.closeDocument(self.Doc.Name)
            self.Doc = FreeCAD.openDocument(name)
            self.Fillet = self.Doc.getObject('Fillet')
            self.assertEqual(self.Fillet.Corners, corners)
            self.Fillet.touch()
            self.assertAlmostEqual(self.recompute(), volume, places=6)
        finally:
            shutil.rmtree(path, ignore_errors=True)

    def testFaceDepth(self):
        top = _faceAt(self.Box.Shape, self.Corner, FreeCAD.Vector(0, 0, 1))
        corners = {self.Vertex: (4.0, {top: 1.0})}
        self.Fillet.Corners = corners
        self.assertEqual(self.Fillet.Corners, corners)
        # the face is Corners' to link, not Base's (it would be filleted)
        self.assertNotIn(top, self.Fillet.Base[1])
        volume = self.recompute()
        self.assertAlmostEqual(volume, self.expected(corners), places=6)
        self.assertNotAlmostEqual(volume, self.expected({self.Vertex: 4}), places=4)
        # a path, and an expression on it
        self.Fillet.setExpression('.Corners.%s.%s' % (self.Vertex, top), '0.5 mm')
        self.recompute()
        self.assertEqual(self.Fillet.Corners, {self.Vertex: (4.0, {top: 0.5})})
        self.assertAlmostEqual(self.Fillet.Shape.Volume,
                               self.expected({self.Vertex: (4, {top: 0.5})}), places=6)
        self.Fillet.setExpression('.Corners.%s.%s' % (self.Vertex, top), None)
        # saved and restored
        path = tempfile.mkdtemp(prefix='FilletCorners')
        try:
            name = os.path.join(path, 'depth.FCStd')
            self.Doc.saveAs(name)
            FreeCAD.closeDocument(self.Doc.Name)
            self.Doc = FreeCAD.openDocument(name)
            self.Fillet = self.Doc.getObject('Fillet')
            self.Box = self.Doc.getObject('Box')
            self.assertEqual(self.Fillet.Corners, {self.Vertex: (4.0, {top: 0.5})})
            self.Fillet.touch()
            self.assertAlmostEqual(self.recompute(),
                                   self.expected({self.Vertex: (4, {top: 0.5})}), places=6)
        finally:
            shutil.rmtree(path, ignore_errors=True)
        # removing the depth
        self.Fillet.Corners = {self.Vertex: 4}
        self.assertAlmostEqual(self.recompute(), self.expected({self.Vertex: 4}), places=6)

    def testStaleCorner(self):
        # a corner that ends no fillet is kept, and skipped
        far = _nameAt(self.Box.Shape, 'Vertex', FreeCAD.Vector(0, 0, 0))
        self.Fillet.Corners = {far: 2}
        self.assertNotIn(far, self.Fillet.Base[1])
        self.assertAlmostEqual(self.recompute(), self.Plain, places=6)
        self.assertEqual(list(self.Fillet.Corners), [far])
        # and one of a vertex the base does not have
        self.Fillet.Corners = {'Vertex99': 2}
        self.assertNotIn('Vertex99', self.Fillet.Base[1])
        self.assertAlmostEqual(self.recompute(), self.Plain, places=6)

    def testBaseLeftAlone(self):
        # Base is what is filleted; setting it again leaves the corners
        self.Fillet.Corners = {self.Vertex: 2}
        volume = self.recompute()
        self.Fillet.Base = (self.Box, self.Edges)
        self.assertEqual(self.Fillet.Corners, {self.Vertex: (2.0, {})})
        self.assertAlmostEqual(self.recompute(), volume, places=6)
        self.Fillet.Corners = {}
        self.assertAlmostEqual(self.recompute(), self.Plain, places=6)

    def testOldFile(self):
        # A file from before Corners was a link: the corner's vertex in Base,
        # no link in the corners. Made by editing a saved file to that form.
        corners = {self.Vertex: (2.0, {self.Edges[0]: 3.0})}
        self.Fillet.Corners = corners
        volume = self.recompute()
        path = tempfile.mkdtemp(prefix='FilletCorners')
        try:
            name = os.path.join(path, 'corners.FCStd')
            old = os.path.join(path, 'old.FCStd')
            self.Doc.saveAs(name)
            FreeCAD.closeDocument(self.Doc.Name)
            _toOldCorners(name, old, self.Vertex)
            self.Doc = FreeCAD.openDocument(old)
            self.Fillet = self.Doc.getObject('Fillet')
            # the vertex moves out of Base into Corners
            self.assertNotIn(self.Vertex, self.Fillet.Base[1])
            self.assertEqual(sorted(self.Fillet.Base[1]), sorted(self.Edges))
            self.assertEqual(self.Fillet.Corners, corners)
            self.Fillet.touch()
            self.assertAlmostEqual(self.recompute(), volume, places=6)
        finally:
            shutil.rmtree(path, ignore_errors=True)

    def testNamesFollowTopology(self):
        # A primitive has no element map, so its names cannot be followed;
        # a pad with a cut ahead of the fillet has. Lowering the cut splits
        # the side faces and renumbers the corner.
        body = self.Doc.addObject('PartDesign::Body', 'PadBody')
        sketch = body.newObject('Sketcher::SketchObject', 'Sketch')
        pts = [FreeCAD.Vector(x, y, 0) for x, y in ((0, 0), (10, 0), (10, 10), (0, 10))]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
        pad = body.newObject('PartDesign::Pad', 'Pad')
        pad.Profile = sketch
        pad.Length = 10
        cut = body.newObject('PartDesign::SubtractiveBox', 'Cut')
        cut.Length = cut.Width = 2
        cut.Height = 20
        self.Doc.recompute()
        vertex = _nameAt(cut.Shape, 'Vertex', self.Corner)
        edges = _nameAt(cut.Shape, 'Edge', self.Corner)
        fillet = body.newObject('PartDesign::Fillet', 'PadFillet')
        fillet.Base = (cut, edges)
        fillet.Radius = 1
        side = _faceAt(cut.Shape, self.Corner, FreeCAD.Vector(1, 0, 0))
        fillet.Corners = {vertex: (2, {edges[0]: 3, side: 1})}
        fillet.setExpression('.Corners.%s.%s' % (vertex, edges[0]), '3 mm')
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        volume = fillet.Shape.Volume
        point = cut.Shape.getElement(edges[0]).CenterOfMass

        cut.Height = 5
        self.Doc.recompute()
        newVertex = _nameAt(cut.Shape, 'Vertex', self.Corner)
        newEdge = [n for n in _nameAt(cut.Shape, 'Edge', self.Corner)
                   if cut.Shape.getElement(n).CenterOfMass.isEqual(point, 1e-7)][0]
        self.assertNotEqual((newVertex, newEdge), (vertex, edges[0]))
        newSide = _faceAt(cut.Shape, self.Corner, FreeCAD.Vector(1, 0, 0))
        self.assertTrue(fillet.isValid())
        self.assertEqual(fillet.Corners, {newVertex: (2.0, {newEdge: 3.0, newSide: 1.0})})
        self.assertEqual([p for p, _ in fillet.ExpressionEngine],
                         ['.Corners.%s.%s' % (newVertex, newEdge)])
        # the cut, through the pad's 10 before, now takes 2 x 2 x 5 less;
        # the corner is the same
        self.assertAlmostEqual(fillet.Shape.Volume, volume + 20, delta=1e-4)


class TestDressUpNamesFollow(unittest.TestCase):
    """An edge's own values of a fillet (Segments) and of a chamfer
    (ChamferInfo), and expressions bound to them, follow the edge when the
    base renames it. The expressions went the wrong way: the rename was
    asked from the new name to the old, and a fillet's per-point paths were
    renamed into themselves, every second one skipped."""

    Corner = FreeCAD.Vector(10, 10, 10)

    def setUp(self):
        # A pad with a cut ahead of the dress-up, as testNamesFollowTopology
        # above: lowering the cut renumbers the edges at the corner
        self.Doc = FreeCAD.newDocument("PartDesignTestDressUpNames")
        body = self.Doc.addObject('PartDesign::Body', 'Body')
        sketch = body.newObject('Sketcher::SketchObject', 'Sketch')
        pts = [FreeCAD.Vector(x, y, 0) for x, y in ((0, 0), (10, 0), (10, 10), (0, 10))]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
        pad = body.newObject('PartDesign::Pad', 'Pad')
        pad.Profile = sketch
        pad.Length = 10
        self.Cut = body.newObject('PartDesign::SubtractiveBox', 'Cut')
        self.Cut.Length = self.Cut.Width = 2
        self.Cut.Height = 20
        self.Body = body
        self.Doc.recompute()
        self.Edge = _nameAt(self.Cut.Shape, 'Edge', self.Corner)[0]
        self.Point = self.Cut.Shape.getElement(self.Edge).CenterOfMass

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def lowerTheCut(self):
        self.Cut.Height = 5
        self.Doc.recompute()
        edge = [n for n in _nameAt(self.Cut.Shape, 'Edge', self.Corner)
                if self.Cut.Shape.getElement(n).CenterOfMass.isEqual(self.Point, 1e-7)][0]
        self.assertNotEqual(edge, self.Edge)
        return edge

    def testFilletSegments(self):
        fillet = self.Body.newObject('PartDesign::Fillet', 'Fillet')
        fillet.Base = (self.Cut, [self.Edge])
        fillet.Radius = 0.5
        fillet.Segments = [(self.Edge, [(0.0, 0.5), (0.5, 0.8), (1.0, 0.5)])]
        # one on every point: the second was skipped
        for i in range(3):
            fillet.setExpression('.Segments.%s[%d].Radius' % (self.Edge, i), '0.%d mm' % (5 + i))
        self.Doc.recompute()
        edge = self.lowerTheCut()
        self.assertEqual(sorted(p for p, _ in fillet.ExpressionEngine),
                         sorted('.Segments.%s[%d].Radius' % (edge, i) for i in range(3)))

    def testChamferInfo(self):
        chamfer = self.Body.newObject('PartDesign::Chamfer', 'Chamfer')
        chamfer.Base = (self.Cut, [self.Edge])
        chamfer.Size = 0.5
        chamfer.ChamferInfo = [(self.Edge, 0.7)]
        chamfer.setExpression('.ChamferInfo.%s.Size' % self.Edge, '0.6 mm')
        self.Doc.recompute()
        edge = self.lowerTheCut()
        self.assertEqual([p for p, _ in chamfer.ExpressionEngine],
                         ['.ChamferInfo.%s.Size' % edge])


class TestFilletGroups(unittest.TestCase):
    """Edge groups of a fillet's own values (docs/OnViewValueEditing.md
    section 6): an id per customised edge, a change to one member mirrored
    to the others, a member reversed in its group, ids saved and restored,
    an older file giving each edge a group of its own."""

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFilletGroups")
        self.Body = self.Doc.addObject('PartDesign::Body', 'Body')
        self.Box = self.Body.newObject('PartDesign::AdditiveBox', 'Box')
        self.Box.Length = self.Box.Width = self.Box.Height = 10
        self.Fillet = self.Body.newObject('PartDesign::Fillet', 'Fillet')
        self.Fillet.Base = (self.Box, ['Edge1', 'Edge2', 'Edge3'])
        self.Fillet.Radius = 1
        self.Doc.recompute()

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            if name.startswith("PartDesignTestFilletGroups"):
                FreeCAD.closeDocument(name)

    def group(self, edge, obj=None):
        return (obj or self.Fillet).evalExpression('Segments.%s.Group' % edge)

    def points(self, edge, obj=None):
        return [(round(p, 6), round(float(r), 6))
                for p, r, _ in (obj or self.Fillet).evalExpression('Segments.%s' % edge)]

    def testOwnGroupEach(self):
        # each edge leaving the default gets a group of its own
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0)]), ('Edge2', [(0.0, 1.5)])]
        self.assertEqual((self.group('Edge1'), self.group('Edge2')), (1, 2))

    def testJoinTakesValuesAndMirrors(self):
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0), (1.0, 1.0)]), ('Edge2', [(0.0, 1.5)])]
        # joining takes the group's points
        self.Fillet.setExpression('.Segments.Edge2.Group', '1')
        self.Doc.recompute()
        self.assertEqual(self.group('Edge2'), 1)
        self.assertEqual(self.points('Edge2'), [(0.0, 2.0), (1.0, 1.0)])
        # a change to one member, an expression's too, reaches the other
        self.Fillet.setExpression('.Segments.Edge1[0].Radius', '0.5 mm')
        self.Doc.recompute()
        self.assertEqual(self.points('Edge2'), [(0.0, 0.5), (1.0, 1.0)])
        self.assertTrue(self.Fillet.isValid())

    def testReversedMember(self):
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0), (1.0, 1.0)], 1),
                                ('Edge2', [(0.0, 2.0), (1.0, 1.0)], 1)]
        self.Fillet.setExpression('.Segments.Edge2.Reversed', '1')
        self.Doc.recompute()
        self.assertEqual(self.points('Edge2'), [(0.0, 1.0), (1.0, 2.0)])
        # mirrored end for end: params as 1 - param
        self.Fillet.setExpression('.Segments.Edge1[1].Param', '0.25')
        self.Doc.recompute()
        self.assertEqual(self.points('Edge1'), [(0.0, 2.0), (0.25, 1.0)])
        self.assertEqual(self.points('Edge2'), [(0.75, 1.0), (1.0, 2.0)])

    def testDefaultLeavesGroup(self):
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0)], 1), ('Edge2', [(0.0, 2.0)], 1)]
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0)])]
        self.assertEqual(self.group('Edge1'), 1)
        with self.assertRaises(Exception):
            self.group('Edge2')
        # the next to leave the default takes the lowest free id
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0)]), ('Edge3', [(0.0, 1.0)])]
        self.assertEqual(self.group('Edge3'), 2)

    def saveAndOpen(self, strip=False):
        path = os.path.join(tempfile.mkdtemp(), 'groups.FCStd')
        self.Doc.saveAs(path)
        name = self.Doc.Name
        if strip:
            # as a build without groups wrote it
            # through ArchiveMembers, as _toOldCorners: the blob members may be
            # zstd, which zipfile reads only from Python 3.14 on
            rewritten = path + '.tmp'
            with zipfile.ZipFile(rewritten, 'w', zipfile.ZIP_DEFLATED) as dst:
                for item, data in ArchiveMembers.members(path):
                    if item.filename == 'Document.xml':
                        data = re.sub(rb' group="\d+"( rev="1")?', b'', data)
                    dst.writestr(item, data)
            os.replace(rewritten, path)
        FreeCAD.closeDocument(name)
        self.Doc = FreeCAD.openDocument(path)
        return self.Doc.getObject('Fillet')

    def testSavedAndRestored(self):
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0), (1.0, 1.0)], 3),
                                ('Edge2', [(0.0, 1.0), (1.0, 2.0)], 3, True),
                                ('Edge3', [(0.0, 1.5)], 5)]
        fillet = self.saveAndOpen()
        self.assertEqual([self.group(e, fillet) for e in ('Edge1', 'Edge2', 'Edge3')], [3, 3, 5])
        self.assertTrue(fillet.evalExpression('Segments.Edge2.Reversed'))
        self.assertEqual(self.points('Edge2', fillet), [(0.0, 1.0), (1.0, 2.0)])

    def testOlderFileGroupsOfTheirOwn(self):
        self.Fillet.Segments = [('Edge1', [(0.0, 2.0)], 1), ('Edge2', [(0.0, 2.0)], 1)]
        fillet = self.saveAndOpen(strip=True)
        groups = [self.group(e, fillet) for e in ('Edge1', 'Edge2')]
        self.assertEqual(len(set(groups)), 2, groups)
        self.assertEqual(self.points('Edge2', fillet), [(0.0, 2.0)])
