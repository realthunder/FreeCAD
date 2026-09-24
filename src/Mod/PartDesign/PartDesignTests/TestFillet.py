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
import unittest

import FreeCAD

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

    def tearDown(self):
        #closing doc
        FreeCAD.closeDocument("PartDesignTestFillet")
        # print ("omit closing document for debugging")

