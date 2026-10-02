#***************************************************************************
#*   Copyright (c) 2026 FreeCAD contributors                               *
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


import unittest

import FreeCAD


class TestVarSet(unittest.TestCase):
    """A body takes a VarSet for its parameters (upstream ec841ed6d4)"""

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestVarSet")

    def testBodyHoldsAVarSet(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        self.Doc.recompute()
        varSet = self.Doc.addObject("App::VarSet", "VarSet")
        varSet.addProperty("App::PropertyLength", "Size", "Variables")
        varSet.Size = 20
        body.addObject(varSet)
        self.assertIn(varSet, body.Group)
        # a parameter, not a feature: the tip stays
        self.assertEqual(body.Tip, box)
        box.setExpression("Length", "VarSet.Size")
        self.Doc.recompute()
        self.assertAlmostEqual(box.Shape.Volume, 20 * 10 * 10, places=6)
        self.assertAlmostEqual(body.Shape.Volume, 20 * 10 * 10, places=6)

    def tearDown(self):
        FreeCAD.closeDocument("PartDesignTestVarSet")
