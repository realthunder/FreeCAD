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
                for item in zin.infolist():
                    data = zin.read(item.filename)
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

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestChamfer")
        # print ("omit closing document for debugging")

