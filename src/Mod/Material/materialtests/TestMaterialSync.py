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
Test module for the explicit sync between a document and the material library.

The document's card wins, always, and a library edit never reaches an old
document (docs/MaterialStorage.md sec 2). What closes the gap is a deliberate
user action, in two directions: take the library's version, or give the
library yours. These test that the gap is reported honestly and that each
direction does exactly what it says (sec 13).
"""

import os
import re
import shutil
import tempfile
import unittest
import zipfile

import FreeCAD
import Materials

# A stock card, so it lives in the read only System library.
STEEL = "92589471-a6cb-4bbc-b748-d425a17dea7d"

FOLDER = "SyncTest"
CARD = FOLDER + "/SyncSteel.FCMat"


class MaterialSyncTestCases(unittest.TestCase):
    """Update from library, and save to library"""

    def setUp(self):
        self.MaterialManager = Materials.MaterialManager()
        self.uuids = Materials.UUIDs()
        self.docs = []
        self.tmp = tempfile.mkdtemp(prefix="fc_material_sync_")
        self.removeLibraryCard()

    def tearDown(self):
        for name in list(self.docs):
            if name in FreeCAD.listDocuments():
                FreeCAD.closeDocument(name)
        shutil.rmtree(self.tmp, ignore_errors=True)
        self.removeLibraryCard()

    # -- fixtures ---------------------------------------------------------

    def userLibraryRoot(self):
        for name, directory, _icon in self.MaterialManager.MaterialLibraries:
            if name == "User":
                return directory
        self.fail("no writable User library")

    def removeLibraryCard(self):
        """Leave the user's real library as it was found.

        The tests write into it because that is where a writable library is,
        which is the same thing TestMaterialCreation does.
        """
        folder = os.path.join(self.userLibraryRoot(), FOLDER)
        if os.path.isdir(folder):
            shutil.rmtree(folder, ignore_errors=True)
            self.MaterialManager.refresh()

    def restoreLibraryCard(self, card):
        """Put a card back under its own uuid, so the library has it again."""
        self.MaterialManager.save("User", card, CARD, overwrite=True)
        self.MaterialManager.refresh()
        return self.MaterialManager.getMaterial(card.UUID)

    def newLibraryCard(self, density="7850.0 kg/m^3"):
        """A card in the User library, with a uuid of its own."""
        material = Materials.Material()
        material.addPhysicalModel(self.uuids.Density)
        material.setPhysicalValue("Density", density)
        # overwrite=True is what keeps the uuid the card was created with.
        self.MaterialManager.save("User", material, CARD, overwrite=True)
        return self.MaterialManager.getMaterialByPath(CARD, "User")

    def editLibraryCard(self, card, density):
        """Someone else changes the library out from under the document."""
        card.setPhysicalValue("Density", density)
        self.MaterialManager.save("User", card, CARD, overwrite=True)
        return self.MaterialManager.getMaterial(card.UUID)

    def newDocument(self, name="MaterialSyncDoc"):
        doc = FreeCAD.newDocument(name)
        self.docs.append(doc.Name)
        doc.SaveSchemaVersion = 5
        return doc

    def boxWith(self, doc, material, name="Box"):
        obj = doc.addObject("Part::Box", name)
        obj.ShapeMaterial = material
        return obj

    def openDocument(self, path):
        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        return doc

    def rewriteDocumentXml(self, path, transform):
        """Rewrite Document.xml inside a saved project, keeping everything else."""
        source = zipfile.ZipFile(path)
        items = [(info, source.read(info.filename)) for info in source.infolist()]
        source.close()
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as target:
            for info, data in items:
                if info.filename == "Document.xml":
                    data = transform(data.decode("utf-8")).encode("utf-8")
                target.writestr(info, data)

    def status(self, obj):
        return Materials.libraryStatus(obj, "ShapeMaterial")

    def density(self, material):
        return material.getPhysicalValue("Density").getValueAs("kg/m^3").Value

    # -- what the states say ----------------------------------------------

    def testAnUntouchedLibraryCardIsCurrent(self):
        doc = self.newDocument()
        obj = self.boxWith(doc, self.newLibraryCard())
        self.assertEqual(self.status(obj), "Current")

    def testAStockCardIsCurrent(self):
        doc = self.newDocument()
        obj = self.boxWith(doc, self.MaterialManager.getMaterial(STEEL))
        self.assertEqual(self.status(obj), "Current")

    def testACardNoLibraryHasIsAbsent(self):
        """A card that was never saved anywhere: nothing to compare against."""
        material = Materials.Material()
        material.addPhysicalModel(self.uuids.Density)
        material.setPhysicalValue("Density", "1234.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, material)
        self.assertEqual(self.status(obj), "Absent")

    def testADeletedCardDoesNotComeBackOnRefresh(self):
        """Refresh reads the libraries as they are now. The loader's entry
        map is static so a card can inherit across libraries, and it was
        never reset between passes: a card whose file was deleted came back
        to the tree on every refresh until restart."""
        card = self.newLibraryCard()
        uuid = card.UUID
        self.assertEqual(self.MaterialManager.getMaterial(uuid).UUID, uuid)
        self.removeLibraryCard()
        with self.assertRaises(LookupError):
            self.MaterialManager.getMaterial(uuid)

    def testARenameDoesNotCountAsADivergence(self):
        """
        The comparison is by content. A card called something else is the same
        card, because the name is the referrer's and not the content's.
        """
        card = self.newLibraryCard()
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        card.Name = "Something Else"
        obj.ShapeMaterial = card
        self.assertEqual(self.status(obj), "Current")

    # -- the guarantee the sync exists to make bearable --------------------

    def testALibraryEditDoesNotReachTheDocument(self):
        """
        Sec 2, the whole policy: the document keeps the card it was assigned,
        and the library moving on is reported rather than applied.
        """
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, card)

        self.editLibraryCard(card, "8123.0 kg/m^3")
        self.assertAlmostEqual(self.density(obj.ShapeMaterial), 7850.0, places=6)
        self.assertEqual(self.status(obj), "Diverged")

    # -- update from library ----------------------------------------------

    def testUpdateFromLibraryTakesTheLibrarysCard(self):
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        self.editLibraryCard(card, "8123.0 kg/m^3")

        self.assertTrue(Materials.updateFromLibrary(obj, "ShapeMaterial"))
        self.assertAlmostEqual(self.density(obj.ShapeMaterial), 8123.0, places=6)
        self.assertEqual(self.status(obj), "Current")
        # The anchor is unchanged: this is still the same library card.
        self.assertEqual(obj.ShapeMaterial.UUID, card.UUID)

    def testUpdateFromLibraryIsAnOrdinaryChange(self):
        """It touches the object, so it recomputes and undoes like any edit."""
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        doc.recompute()
        self.editLibraryCard(card, "8123.0 kg/m^3")

        self.assertNotIn("Touched", obj.State)
        self.assertTrue(Materials.updateFromLibrary(obj, "ShapeMaterial"))
        self.assertIn("Touched", obj.State)

    def testUpdateFromLibraryUndoes(self):
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        doc.UndoMode = 1
        obj = self.boxWith(doc, card)
        doc.recompute()
        self.editLibraryCard(card, "8123.0 kg/m^3")

        doc.openTransaction("Update material")
        Materials.updateFromLibrary(obj, "ShapeMaterial")
        doc.commitTransaction()
        self.assertAlmostEqual(self.density(obj.ShapeMaterial), 8123.0, places=6)

        doc.undo()
        self.assertAlmostEqual(self.density(obj.ShapeMaterial), 7850.0, places=6)
        self.assertEqual(self.status(obj), "Diverged")

    def testUpdateFromLibraryRefusesWhenThereIsNothingToTake(self):
        card = self.newLibraryCard()
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        doc.recompute()

        self.assertEqual(self.status(obj), "Current")
        self.assertFalse(Materials.updateFromLibrary(obj, "ShapeMaterial"))
        self.assertNotIn("Touched", obj.State)

    def testUpdateFromLibraryRelinksACardThatNeverArrived(self):
        """
        The strongest case for the command: a document from someone else whose
        card this installation could not resolve, and a library that does have
        it. Nothing else can put those two together.

        Making the stored content unreadable is not enough on its own to
        produce that state -- restore falls back to resolving the uuid against
        the library, exactly as upstream does -- so this installation must not
        have the card either. Both halves are done here: the content the
        document names is made unproducible, and the card is taken out of the
        library it was created in.
        """
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        self.boxWith(doc, card)
        project = os.path.join(self.tmp, "relink.FCStd")
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        # The document now names content nothing here can produce, which is
        # the state a card that never arrived leaves behind: the assignment is
        # recorded, the card is a placeholder.
        self.rewriteDocumentXml(
            project, lambda xml: re.sub(r'hash="[0-9a-f]*"', 'hash="%s"' % ("0" * 40), xml)
        )
        self.removeLibraryCard()
        reopened = self.openDocument(project)
        obj = reopened.Box
        self.assertFalse(obj.ShapeMaterial.PhysicalModels)
        self.assertEqual(obj.ShapeMaterial.UUID, card.UUID)
        self.assertEqual(self.status(obj), "Absent")

        # The library has the card the document names. That is a divergence,
        # and taking the library's side of it is the relink.
        self.restoreLibraryCard(card)
        self.assertEqual(self.status(obj), "Diverged")
        self.assertTrue(Materials.updateFromLibrary(obj, "ShapeMaterial"))
        self.assertAlmostEqual(self.density(obj.ShapeMaterial), 7850.0, places=6)
        self.assertEqual(self.status(obj), "Current")

    def testSaveToLibraryRefusesACardThatNeverArrived(self):
        """
        A placeholder is a note about a missing card, not the card. Writing it
        to the library would put the note where the card should be.
        """
        card = self.newLibraryCard()
        doc = self.newDocument()
        self.boxWith(doc, card)
        project = os.path.join(self.tmp, "unresolved.FCStd")
        doc.saveAs(project)
        FreeCAD.closeDocument(doc.Name)

        self.rewriteDocumentXml(
            project, lambda xml: re.sub(r'hash="[0-9a-f]*"', 'hash="%s"' % ("0" * 40), xml)
        )
        reopened = self.openDocument(project)
        self.assertFalse(Materials.saveToLibrary(reopened.Box, "ShapeMaterial"))

    # -- save to library ---------------------------------------------------

    def testSaveToLibraryWritesTheDocumentsCardBack(self):
        card = self.newLibraryCard(density="7850.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, card)

        edited = obj.ShapeMaterial
        edited.setPhysicalValue("Density", "8123.0 kg/m^3")
        obj.ShapeMaterial = edited
        self.assertEqual(self.status(obj), "Diverged")

        self.assertTrue(Materials.saveToLibrary(obj, "ShapeMaterial"))
        self.assertEqual(self.status(obj), "Current")
        self.assertAlmostEqual(
            self.density(self.MaterialManager.getMaterial(card.UUID)), 8123.0, places=6
        )

    def testSaveToLibraryKeepsTheUuid(self):
        """Otherwise it is not the inverse of an update, it is a new card."""
        card = self.newLibraryCard()
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        edited = obj.ShapeMaterial
        edited.setPhysicalValue("Density", "8123.0 kg/m^3")
        obj.ShapeMaterial = edited

        Materials.saveToLibrary(obj, "ShapeMaterial")
        self.assertEqual(obj.ShapeMaterial.UUID, card.UUID)
        self.assertEqual(
            self.MaterialManager.getMaterialByPath(CARD, "User").UUID, card.UUID
        )

    def testSaveToLibrarySurvivesARereadFromDisk(self):
        """
        What the library holds after a restart is what is on disk, not what
        was in memory when it was written. If the two disagree the command
        does not converge: the document would read Diverged again tomorrow.
        """
        card = self.newLibraryCard()
        doc = self.newDocument()
        obj = self.boxWith(doc, card)
        edited = obj.ShapeMaterial
        # Deliberately awkward: a value the user's unit schema cannot render
        # in the two decimals it likes.
        edited.setPhysicalValue("Density", "7854.321 kg/m^3")
        obj.ShapeMaterial = edited

        self.assertTrue(Materials.saveToLibrary(obj, "ShapeMaterial"))
        self.MaterialManager.refresh()
        self.assertEqual(self.status(obj), "Current")

    def testSaveToLibraryRefusesAReadOnlyLibrary(self):
        """A stock card has no writable home, so the caller has to ask."""
        doc = self.newDocument()
        obj = self.boxWith(doc, self.MaterialManager.getMaterial(STEEL))
        edited = obj.ShapeMaterial
        edited.setPhysicalValue("Density", "8123.0 kg/m^3")
        obj.ShapeMaterial = edited

        self.assertEqual(self.status(obj), "Diverged")
        self.assertFalse(Materials.saveToLibrary(obj, "ShapeMaterial"))

    def testSaveToLibraryRefusesACardWithNoLibraryCard(self):
        material = Materials.Material()
        material.addPhysicalModel(self.uuids.Density)
        material.setPhysicalValue("Density", "1234.0 kg/m^3")
        doc = self.newDocument()
        obj = self.boxWith(doc, material)

        self.assertEqual(self.status(obj), "Absent")
        self.assertFalse(Materials.saveToLibrary(obj, "ShapeMaterial"))

    # -- addressing --------------------------------------------------------

    def testTheseAreForMaterialPropertiesOnly(self):
        doc = self.newDocument()
        obj = self.boxWith(doc, self.newLibraryCard())
        with self.assertRaises(AttributeError):
            Materials.libraryStatus(obj, "NotAProperty")
        with self.assertRaises(TypeError):
            Materials.libraryStatus(obj, "Length")
