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

import os
import ArchiveMembers
import unittest

import FreeCAD

class TestChamfer(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestChamfer")

    def testChamferCubeToOctahedron(self):
        self.Body = self.Doc.addObject('PartDesign::Body','Body')
        self.Box = self.Doc.addObject('PartDesign::AdditiveBox','Box')
        self.Body.addObject(self.Box)
        self.Box.Length=10.00
        self.Box.Width=10.00
        self.Box.Height=10.00
        self.Doc.recompute()
        self.Chamfer = self.Doc.addObject("PartDesign::Chamfer","Chamfer")
        self.Chamfer.Base = (self.Box, ['Face'+str(i+1) for i in range(6)])
        self.Chamfer.Size = 4.999999
        self.Body.addObject(self.Chamfer)
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        #test UseAllEdges property
        self.Chamfer.UseAllEdges = True
        self.Chamfer.Base = (self.Box, ['']) # no subobjects, should still work
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        self.Chamfer.Base = (self.Box, ['Face50']) # non-existent face, test topo naming resilience
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        self.Chamfer.UseAllEdges = False
        self.Chamfer.Base = (self.Box, ['Face1'])
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 9)

    def testUpstream021FaceRule(self):
        """A two-distance chamfer made on a face by upstream before 1.0 keeps
        its sizes (upstream 4d712f44c2, done per edge). 0.21 measured Size on
        the selected face, on the edge's other face when flipped; the fork
        measures on the edge's first face, its last when flipped. On a
        cube's Face6, the last face of all its edges, the sizes came out
        swapped; on Face1, the first, they did not, and upstream's
        migration, which flips every such chamfer, would swap those."""
        import os
        import re
        import tempfile
        import zipfile

        def upstream_021(src, dst):
            # what upstream 0.21 writes: its version, no string hasher, and
            # none of the fork's ChamferInfo
            with zipfile.ZipFile(src) as zin, \
                    zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
                for item, data in ArchiveMembers.members(src):
                    if item.filename == "Document.xml":
                        text = data.decode("utf-8")
                        text = re.sub(r'ProgramVersion="[^"]*"',
                                      'ProgramVersion="0.21R33771 (Git)"', text, count=1)
                        text = text.replace(' StringHasher="1"', "", 1)
                        text = re.sub(r"<StringHasher[^>]*/>\s*", "", text, count=1)
                        text = re.sub(r"<StringHasher2.*?</StringHasher2>\s*", "", text,
                                      count=1, flags=re.S)
                        m = re.search(r'<Property name="ChamferInfo".*?</Property>\s*', text,
                                      flags=re.S)
                        head = text[:m.start()]
                        k = head.rfind('<Properties Count="')
                        count = int(re.match(r'<Properties Count="(\d+)"', head[k:]).group(1))
                        head = head[:k] + head[k:].replace(
                            'Count="%d"' % count, 'Count="%d"' % (count - 1), 1)
                        data = (head + text[m.end():]).encode("utf-8")
                    zout.writestr(item, data)

        tmp = tempfile.mkdtemp()
        path = os.path.join(tmp, "chamfer.FCStd")
        old = os.path.join(tmp, "chamfer021.FCStd")
        for face in (1, 6):
            for flip in (False, True):
                doc = FreeCAD.newDocument("PartDesignTestChamfer021")
                body = doc.addObject("PartDesign::Body", "Body")
                box = body.newObject("PartDesign::AdditiveBox", "Box")
                box.Length = box.Width = box.Height = 10
                doc.recompute()
                selected = box.Shape.Faces[face - 1]
                normal = selected.normalAt(0, 0)
                center = selected.CenterOfMass
                chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
                chamfer.Base = (box, ["Face%d" % face])
                chamfer.ChamferType = "Two distances"
                chamfer.Size = 1
                chamfer.Size2 = 3
                chamfer.FlipDirection = flip
                doc.recompute()
                doc.saveAs(path)
                FreeCAD.closeDocument(doc.Name)
                upstream_021(path, old)
                doc = FreeCAD.openDocument(old)
                try:
                    chamfer = doc.getObject("Chamfer")
                    chamfer.touch()
                    doc.recompute()
                    area = max(f.Area for f in chamfer.Shape.Faces
                               if (f.normalAt(0, 0) - normal).Length < 1e-9
                               and abs((f.CenterOfMass - center).dot(normal)) < 1e-9)
                    # Size (1) off each side of the selected face, Size2 (3)
                    # when flipped
                    self.assertAlmostEqual(area, 64 if not flip else 16,
                                           msg="Face%d flip %s" % (face, flip))
                finally:
                    FreeCAD.closeDocument(doc.Name)

    def testV021ChamferKeepsItsEdges(self):
        # A chamfer from a 0.21 file names its edges by the element map of
        # then; a recompute here must chamfer the same edges (upstream
        # e5b1d05813, its fixture)
        path = os.path.join(os.path.dirname(__file__), 'Fixtures', 'ModelFromV021.FCStd')
        doc = FreeCAD.openDocument(path)
        try:
            chamfer = doc.getObject('Chamfer')

            def points(names):
                shape = chamfer.BaseFeature.Shape
                return [[tuple(round(c, 9) for c in v.Point)
                         for v in shape.getElement(n).Vertexes] for n in names]

            before = points(chamfer.Base[1])
            self.assertEqual(len(before), 4)
            doc.recompute()
            self.assertNotIn('Invalid', chamfer.State)
            self.assertEqual(points(chamfer.Base[1]), before)
        finally:
            FreeCAD.closeDocument(doc.Name)

    def testFailedEdgesLeftOut(self):
        # docs/HandsOnQueue.md entry 143, the chamfer as the fillet: an edge
        # whose chamfer fails is left out, the result holds the others', the
        # feature is in error still, the failed one's faces as last made kept
        body = self.Doc.addObject('PartDesign::Body', 'PartialBody')
        box = body.newObject('PartDesign::AdditiveBox', 'PartialBox')
        box.Length = box.Width = 10.0
        box.Height = 2.0
        self.Doc.recompute()
        shape = box.Shape
        # a vertical edge, 2 long, between faces 10 wide; and an edge on the
        # top away from it, its side 2 high
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
        chamfer = body.newObject('PartDesign::Chamfer', 'PartialChamfer')
        chamfer.Base = (box, [vertical, top])
        chamfer.Size = 0.5
        self.Doc.recompute()
        self.assertTrue(chamfer.isValid())
        self.assertEqual(list(chamfer.FailedEdges), [])
        whole = chamfer.Shape.Volume

        chamfer.Size = 3.0
        self.Doc.recompute()
        self.assertFalse(chamfer.isValid())
        self.assertEqual(list(chamfer.FailedEdges), [top])
        self.assertTrue(chamfer.Shape.isValid())
        # the vertical chamfer made, the top one not: a 3 by 3 corner off,
        # 2 high
        self.assertAlmostEqual(chamfer.Shape.Volume, 200.0 - 4.5 * 2.0, places=3)
        self.assertGreater(len(chamfer.FailedShape.Faces), 0)

        chamfer.Size = 0.5
        self.Doc.recompute()
        self.assertTrue(chamfer.isValid())
        self.assertEqual(list(chamfer.FailedEdges), [])
        self.assertTrue(chamfer.FailedShape.isNull())
        self.assertAlmostEqual(chamfer.Shape.Volume, whole, places=6)

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestChamfer")
        # print ("omit closing document for debugging")



class TestChamferGroups(unittest.TestCase):
    """Edge groups of a chamfer's own values (docs/OnViewValueEditing.md 6.5):
    an id per customised edge, a change to the sizes or the angle of one
    member mirrored to the others while each keeps its own flip, ids saved
    and restored, an older file giving each edge a group of its own. And the
    Python form of the property, whose angle read back as a boolean and whose
    setter refused the four and five items its own message names."""

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestChamferGroups")
        self.Body = self.Doc.addObject('PartDesign::Body', 'Body')
        self.Box = self.Body.newObject('PartDesign::AdditiveBox', 'Box')
        self.Box.Length = self.Box.Width = self.Box.Height = 10
        self.Chamfer = self.Body.newObject('PartDesign::Chamfer', 'Chamfer')
        self.Chamfer.Base = (self.Box, ['Edge1', 'Edge2', 'Edge3'])
        self.Chamfer.Size = 1
        self.Doc.recompute()

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            if name.startswith("PartDesignTestChamferGroups"):
                FreeCAD.closeDocument(name)

    def group(self, edge, obj=None):
        return (obj or self.Chamfer).evalExpression('ChamferInfo.%s.Group' % edge)

    def info(self, edge, obj=None):
        size, size2, angle, flip = (obj or self.Chamfer).evalExpression('ChamferInfo.%s' % edge)
        return (round(float(size), 6), round(float(size2), 6), round(float(angle), 6), bool(flip))

    def testOwnGroupEach(self):
        # each edge leaving the defaults gets a group of its own
        self.Chamfer.ChamferInfo = [('Edge1', 2.0), ('Edge2', 1.5)]
        self.assertEqual((self.group('Edge1'), self.group('Edge2')), (1, 2))

    def testJoinTakesValuesNotFlip(self):
        self.Chamfer.ChamferInfo = [('Edge1', 2.0, 1.0, 0.0, False),
                                    ('Edge2', 1.5, 0.0, 0.0, True)]
        self.Chamfer.setExpression('.ChamferInfo.Edge2.Group', '1')
        self.Doc.recompute()
        self.assertEqual(self.group('Edge2'), 1)
        # the group's sizes, its own flip
        self.assertEqual(self.info('Edge2'), (2.0, 1.0, 0.0, True))
        # a change to one member, an expression's too, reaches the other
        self.Chamfer.setExpression('.ChamferInfo.Edge1.Size', '0.5 mm')
        self.Doc.recompute()
        self.assertEqual(self.info('Edge2'), (0.5, 1.0, 0.0, True))
        self.assertTrue(self.Chamfer.isValid())

    def testAngleMirroredFlipNot(self):
        self.Chamfer.ChamferInfo = [('Edge1', 1.0, 0.0, 45.0, False, 1),
                                    ('Edge2', 1.0, 0.0, 45.0, False, 1)]
        self.Chamfer.setExpression('.ChamferInfo.Edge1.Angle', '30')
        self.Chamfer.setExpression('.ChamferInfo.Edge1.Flip', '1')
        self.Doc.recompute()
        self.assertEqual(self.info('Edge1'), (1.0, 0.0, 30.0, True))
        self.assertEqual(self.info('Edge2'), (1.0, 0.0, 30.0, False))
        self.assertTrue(self.Chamfer.isValid())

    def testDefaultLeavesGroup(self):
        self.Chamfer.ChamferInfo = [('Edge1', 2.0, 0.0, 0.0, False, 1),
                                    ('Edge2', 2.0, 0.0, 0.0, False, 1)]
        self.Chamfer.ChamferInfo = [('Edge1', 2.0)]
        self.assertEqual(self.group('Edge1'), 1)
        with self.assertRaises(Exception):
            self.group('Edge2')
        # the next to leave the defaults takes the lowest free id
        self.Chamfer.ChamferInfo = [('Edge1', 2.0), ('Edge3', 1.0)]
        self.assertEqual(self.group('Edge3'), 2)

    def testPythonForm(self):
        self.Chamfer.ChamferInfo = [('Edge1', 1.0, 0.0, 30.0, True, 4)]
        value = self.Chamfer.ChamferInfo
        self.assertEqual(len(value), 1)
        edge, size, size2, angle, flip, group = value[0]
        self.assertEqual((edge, size, size2, angle, flip, group),
                         ('Edge1', 1.0, 0.0, 30.0, True, 4))
        # what it gives, it takes back unchanged
        self.Chamfer.ChamferInfo = value
        self.assertEqual(self.Chamfer.ChamferInfo, value)

    def saveAndOpen(self, strip=False):
        import re
        import tempfile
        import zipfile
        path = os.path.join(tempfile.mkdtemp(), 'groups.FCStd')
        self.Doc.saveAs(path)
        name = self.Doc.Name
        if strip:
            # as a build without groups wrote it; through ArchiveMembers: the
            # blob members may be zstd
            rewritten = path + '.tmp'
            with zipfile.ZipFile(rewritten, 'w', zipfile.ZIP_DEFLATED) as dst:
                for item, data in ArchiveMembers.members(path):
                    if item.filename == 'Document.xml':
                        data = re.sub(rb'" group="\d+"', b'"', data)
                    dst.writestr(item, data)
            os.replace(rewritten, path)
        FreeCAD.closeDocument(name)
        self.Doc = FreeCAD.openDocument(path)
        return self.Doc.getObject('Chamfer')

    def testSavedAndRestored(self):
        self.Chamfer.ChamferInfo = [('Edge1', 1.0, 2.0, 0.0, False, 3),
                                    ('Edge2', 1.0, 2.0, 0.0, True, 3),
                                    ('Edge3', 1.5, 0.0, 0.0, False, 5)]
        chamfer = self.saveAndOpen()
        self.assertEqual([self.group(e, chamfer) for e in ('Edge1', 'Edge2', 'Edge3')],
                         [3, 3, 5])
        self.assertEqual(self.info('Edge2', chamfer), (1.0, 2.0, 0.0, True))
        self.assertEqual(self.info('Edge3', chamfer), (1.5, 0.0, 0.0, False))

    def testOlderFileGroupsOfTheirOwn(self):
        self.Chamfer.ChamferInfo = [('Edge1', 2.0, 0.0, 0.0, False, 1),
                                    ('Edge2', 2.0, 0.0, 0.0, False, 1)]
        chamfer = self.saveAndOpen(strip=True)
        groups = [self.group(e, chamfer) for e in ('Edge1', 'Edge2')]
        self.assertEqual(len(set(groups)), 2, groups)
        self.assertEqual(self.info('Edge2', chamfer), (2.0, 0.0, 0.0, False))
