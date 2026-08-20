# SPDX-License-Identifier: LGPL-2.1-or-later

#**************************************************************************
#   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
#                                                                         *
#   This file is part of the FreeCAD CAx development system.              *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU Lesser General Public License (LGPL)    *
#   as published by the Free Software Foundation; either version 2 of     *
#   the License, or (at your option) any later version.                   *
#   for detail see the LICENCE text file.                                 *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful,            *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU Library General Public License for more details.                  *
#                                                                         *
#   You should have received a copy of the GNU Library General Public     *
#   License along with FreeCAD; if not, write to the Free Software        *
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#   USA                                                                   *
#**************************************************************************

"""
Test module for material cards stored in a document.

A card assigned to an object is carried by the document as content, not
referenced by uuid, so a document opened on an installation that never had
that card still reports the material by name with its values intact. That is
the acceptance test below; the rest guard the storage around it
(docs/MaterialStorage.md sec 11).
"""

import os
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD
import Materials

STEEL = "92589471-a6cb-4bbc-b748-d425a17dea7d"
BLOB_DIR = "blobs"
# A uuid no installation has, standing in for a card the reader lacks.
ABSENT = "deadbeef-0000-4000-8000-000000000001"


class MaterialBlobTestCases(unittest.TestCase):
    """A material card as document content"""

    def setUp(self):
        self.MaterialManager = Materials.MaterialManager()
        self.steelName = self.MaterialManager.getMaterial(STEEL).Name
        self.tmp = tempfile.mkdtemp(prefix="fc_material_blob_")
        self.docs = []

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ---------------------------------------------------------

    def newDocument(self, name="MaterialDoc", schema=5):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        # Stored cards are this fork's format. A new document defaults to
        # upstream's schema, so the choice is made here rather than inherited.
        doc.SaveSchemaVersion = schema
        return doc

    def openDocument(self, path):
        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        return doc

    def boxWithMaterial(self, doc, name="Box", uuid=STEEL):
        """A box carrying a stock card, exactly as installed."""
        obj = doc.addObject("Part::Box", name)
        obj.ShapeMaterial = self.MaterialManager.getMaterial(uuid)
        return obj

    def customMaterial(self):
        """A card no installation has: a stock one with a value changed.

        This is the case the storage exists for. A stock card is content
        every reader already has, so a document referring to one does not
        carry it -- which means it cannot stand in for a card that has to
        travel.
        """
        material = self.MaterialManager.getMaterial(STEEL)
        material.Name = "Steel, but denser"
        material.setPhysicalValue("Density", "12345.0 kg/m^3")
        return material

    def boxWithCustomMaterial(self, doc, name="Box"):
        obj = doc.addObject("Part::Box", name)
        obj.ShapeMaterial = self.customMaterial()
        return obj

    def projectPath(self, name="project.FCStd"):
        return os.path.join(self.tmp, name)

    def blobEntries(self, path):
        return [n for n in zipfile.ZipFile(path).namelist() if n.startswith(BLOB_DIR + "/")]

    def rewriteDocumentXml(self, path, transform):
        """Rewrite Document.xml inside a saved project, keeping everything else.

        This is how a document written by another installation is simulated:
        the same file, with the material element saying something this
        installation cannot resolve.
        """
        source = zipfile.ZipFile(path)
        items = [(info, source.read(info.filename)) for info in source.infolist()]
        source.close()
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as target:
            for info, data in items:
                if info.filename == "Document.xml":
                    data = transform(data.decode("utf-8")).encode("utf-8")
                target.writestr(info, data)

    def documentXml(self, path):
        return zipfile.ZipFile(path).read("Document.xml").decode("utf-8")

    # -- tests ------------------------------------------------------------

    def testCardSurvivesTheRoundTrip(self):
        doc = self.newDocument()
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        density = doc.Box.ShapeMaterial.getPhysicalValue("Density")
        FreeCAD.closeDocument(doc.Name)

        reopened = self.openDocument(project)
        self.assertEqual(reopened.Box.ShapeMaterial.Name, self.steelName)
        self.assertEqual(reopened.Box.ShapeMaterial.UUID, STEEL)
        self.assertEqual(reopened.Box.ShapeMaterial.getPhysicalValue("Density"), density)

    def testCardOpensWhereItIsNotInstalled(self):
        """
        The acceptance test. A document whose card this installation does not
        have reports the material by name with its physical values intact --
        where a uuid reference silently became Default.
        """
        doc = self.newDocument()
        self.boxWithCustomMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        density = doc.Box.ShapeMaterial.getPhysicalValue("Density")
        FreeCAD.closeDocument(doc.Name)

        # The uuid now names a card nothing here can resolve. The content is
        # still in the file, which is the whole point.
        self.rewriteDocumentXml(project, lambda xml: xml.replace(STEEL, ABSENT))

        reopened = self.openDocument(project)
        material = reopened.Box.ShapeMaterial
        self.assertEqual(material.Name, "Steel, but denser")
        self.assertEqual(material.UUID, ABSENT)
        self.assertEqual(material.getPhysicalValue("Density"), density)

    def testStockCardIsNotCarried(self):
        """
        A document using only installed cards costs no extra bytes: the hash
        says which card it is and every reader with the library has it.
        """
        doc = self.newDocument()
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        self.assertFalse([n for n in self.blobEntries(project) if n.endswith(".FCMat")])
        self.assertIn("PropertyMaterial hash", self.documentXml(project))

    def testEditedCardIsCarried(self):
        """A card that is not installed anywhere else travels with it."""
        doc = self.newDocument()
        self.boxWithCustomMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        self.assertEqual(len([n for n in self.blobEntries(project) if n.endswith(".FCMat")]), 1)

    def testUpstreamUuidOnlyDocumentStillOpens(self):
        """A document written by upstream carries a uuid and nothing else."""
        doc = self.newDocument()
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        import re

        self.rewriteDocumentXml(
            project,
            lambda xml: re.sub(
                r'<PropertyMaterial [^/]*/>',
                '<PropertyMaterial uuid="%s"/>' % STEEL,
                xml,
            ),
        )

        reopened = self.openDocument(project)
        self.assertEqual(reopened.Box.ShapeMaterial.Name, self.steelName)
        self.assertEqual(reopened.Box.ShapeMaterial.UUID, STEEL)

    def testUnresolvedCardIsNotSilentlyDefault(self):
        """
        Neither content nor a resolvable uuid: the assignment stays visible,
        keeping what the document recorded, rather than reverting to Default.
        """
        doc = self.newDocument()
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        import re

        self.rewriteDocumentXml(
            project,
            lambda xml: re.sub(
                r'<PropertyMaterial [^/]*/>',
                '<PropertyMaterial uuid="%s"/>' % ABSENT,
                xml,
            ),
        )

        reopened = self.openDocument(project)
        material = reopened.Box.ShapeMaterial
        self.assertEqual(material.UUID, ABSENT)
        self.assertNotEqual(material.Name, "Default")
        self.assertFalse(material.PhysicalModels)

    def testUnresolvedCardSurvivesAResave(self):
        """
        Opening a document on a machine that cannot resolve its card, and
        saving it there, must not be what destroys the assignment.
        """
        doc = self.newDocument()
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        import re

        self.rewriteDocumentXml(
            project,
            lambda xml: re.sub(
                r'<PropertyMaterial [^/]*/>',
                '<PropertyMaterial uuid="%s" name="Absent Steel"/>' % ABSENT,
                xml,
            ),
        )

        reopened = self.openDocument(project)
        self.assertEqual(reopened.Box.ShapeMaterial.UUID, ABSENT)
        reopened.save()
        xml = self.documentXml(project)
        self.assertIn(ABSENT, xml)
        self.assertNotIn("PropertyMaterial hash", xml)
        self.assertFalse([n for n in self.blobEntries(project) if n.endswith(".FCMat")])

    def testOneCardIsOneArchiveEntry(self):
        """Two objects with the same material store it once."""
        doc = self.newDocument()
        material = self.customMaterial()
        for name in ("Box1", "Box2"):
            doc.addObject("Part::Box", name).ShapeMaterial = material
        project = self.projectPath()
        doc.saveAs(project)
        cards = [n for n in self.blobEntries(project) if n.endswith(".FCMat")]
        self.assertEqual(len(cards), 1)

    def testSavingTwiceWritesTheSameBytes(self):
        doc = self.newDocument()
        self.boxWithCustomMaterial(doc)
        first = self.projectPath("first.FCStd")
        doc.saveAs(first)
        second = self.projectPath("second.FCStd")
        doc.saveAs(second)

        def card(path):
            names = [n for n in self.blobEntries(path) if n.endswith(".FCMat")]
            self.assertEqual(len(names), 1)
            return zipfile.ZipFile(path).read(names[0])

        self.assertEqual(card(first), card(second))

    def testSchemaFourWritesUpstreamsForm(self):
        """A document written for upstream carries the uuid and no content."""
        doc = self.newDocument(schema=4)
        self.boxWithMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)

        xml = self.documentXml(project)
        self.assertIn('<PropertyMaterial uuid="%s"/>' % STEEL, xml)
        self.assertNotIn("PropertyMaterial hash", xml)
        self.assertFalse([n for n in self.blobEntries(project) if n.endswith(".FCMat")])

    def testObjectsSharingACardShareOneInstance(self):
        """
        Storage de-duplication is worth little on its own: what makes it pay
        is that the shared content is parsed once and held once, however many
        objects refer to it.
        """
        doc = self.newDocument()
        material = self.customMaterial()
        for index in range(20):
            doc.addObject("Part::Box", "Box%d" % index).ShapeMaterial = material
        project = self.projectPath()
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        before = Materials.cardCacheSize()
        reopened = self.openDocument(project)
        self.assertEqual(len(reopened.Objects), 20)
        self.assertEqual(Materials.cardCacheSize(), before + 1)
        self.assertEqual(reopened.Box0.ShapeMaterial.Name, "Steel, but denser")

    def testStoredCardIsTheCanonicalForm(self):
        """What is stored is the canonical form, hashed under its own name."""
        doc = self.newDocument()
        obj = self.boxWithCustomMaterial(doc)
        project = self.projectPath()
        doc.saveAs(project)

        names = [n for n in self.blobEntries(project) if n.endswith(".FCMat")]
        self.assertEqual(len(names), 1)
        stored = zipfile.ZipFile(project).read(names[0]).decode("utf-8")
        self.assertEqual(stored, obj.ShapeMaterial.CanonicalForm)
