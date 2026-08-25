# -*- coding: utf-8 -*-
# Tests for the element-name mapping exposed on a shape: setElementName and
# eraseElementName.
#
# eraseElementName takes one string and has to decide for itself whether it was
# given an element name or a mapped name, so most of what is checked here is
# that the split lands the right way round.

import unittest

import Part


class ElementNameTest(unittest.TestCase):
    def setUp(self):
        self.box = Part.makeBox(1, 1, 1)
        self.box.Tag = 1

    def testEraseMappedNameLeavesTheOthers(self):
        first = self.box.setElementName("Edge1", "FIRST", overwrite=True)
        second = self.box.setElementName("Edge1", "SECOND")
        self.assertEqual(len(self.box.ElementMap), 2)

        # An element can carry several mapped names; erasing one keeps the rest.
        self.assertTrue(self.box.eraseElementName(second))
        self.assertEqual(len(self.box.ElementMap), 1)
        self.assertEqual(self.box.getElementMappedName("Edge1"), first)

    def testEraseMappedNameIsIdempotent(self):
        name = self.box.setElementName("Edge1", "ONLY", overwrite=True)
        self.assertTrue(self.box.eraseElementName(name))
        # Gone already, so nothing to report the second time.
        self.assertFalse(self.box.eraseElementName(name))

    def testEraseIndexedNameTakesTheWholeElement(self):
        self.box.setElementName("Edge1", "FIRST", overwrite=True)
        self.box.setElementName("Edge1", "SECOND")
        self.assertEqual(len(self.box.ElementMap), 2)

        # An element name erases every mapped name of that element.
        self.assertTrue(self.box.eraseElementName("Edge1"))
        self.assertEqual(len(self.box.ElementMap), 0)

    def testEraseUnknownNameReportsFalse(self):
        self.box.setElementName("Edge1", "FIRST", overwrite=True)
        self.assertFalse(self.box.eraseElementName("NOSUCHNAME"))
        self.assertFalse(self.box.eraseElementName("Edge9"))
        self.assertEqual(len(self.box.ElementMap), 1)

    def testMappedNameIsNotMistakenForAnElementName(self):
        # A bare word like "SECOND" parses as an indexed name unless the type is
        # checked against the shape's own element types. If that check is missed,
        # the erase looks in the wrong direction and quietly finds nothing.
        self.box.setElementName("Edge1", "FIRST", overwrite=True)
        second = self.box.setElementName("Edge1", "SECOND")
        self.assertTrue(self.box.eraseElementName(second))
        self.assertEqual(len(self.box.ElementMap), 1)

    def testMappedNameAcceptsTheSubnamePrefix(self):
        # Mapped names appear semicolon-prefixed inside a subname; take either.
        name = self.box.setElementName("Edge1", "PREFIXED", overwrite=True)
        self.assertTrue(self.box.eraseElementName(";" + name))
        self.assertEqual(len(self.box.ElementMap), 0)
