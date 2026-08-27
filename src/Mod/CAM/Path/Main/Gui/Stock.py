# SPDX-License-Identifier: LGPL-2.1-or-later

"""View provider for the Job's Stock object.

The plain icon view provider plus a "Cut" display mode showing the
simulated cut result stored in the Stock's CutMesh property
(docs/CAMSimRenderPort.md section 11.6). The mesh is written by the
CAM simulator when a run stops; in every other mode the Stock shows
its ordinary uncut shape through the Part display modes.
"""

import FreeCAD
import Path
import Path.Base.Gui.IconViewProvider as PathIconViewProvider

__title__ = "CAM Stock ViewProvider"
__doc__ = "Icon view provider for Stock, plus the Cut display mode."

translate = FreeCAD.Qt.translate

# The GL simulator's colours (MillSimulation::stockColor / cutColor),
# so the settled mesh keeps the two-tone picture the pixels had:
# virgin stock surface in the first, machined facets in the second.
StockColor = (0.5, 0.55, 0.9)
CutColor = (0.5, 0.84, 0.73)


class ViewProvider(PathIconViewProvider.ViewProvider):
    """Stock's view provider: the icon plus the Cut display mode."""

    def attach(self, vobj):
        super().attach(vobj)
        # Called directly by __init__ AND by the framework when the
        # Proxy property lands; the mode node is built once. The
        # pivy objects are runtime state only -- dumps/loads stay the
        # base class's, and a restored proxy rebuilds here.
        if getattr(self, "cutNode", None) is not None:
            return
        from pivy import coin

        sep = coin.SoSeparator()
        hints = coin.SoShapeHints()
        hints.vertexOrdering = coin.SoShapeHints.COUNTERCLOCKWISE
        # The outer result mesh is closed; SOLID buys backface culling.
        hints.shapeType = coin.SoShapeHints.SOLID
        sep.addChild(hints)
        # Its own materials: the inherited one is the uncut stock's
        # deliberately-unobtrusive translucent look, wrong for a
        # solid carve. Two entries, indexed per face off
        # CutMeshUncutCount by _updateCutMesh.
        mat = coin.SoMaterial()
        mat.diffuseColor.setValues(0, 2, [StockColor, CutColor])
        sep.addChild(mat)
        binding = coin.SoMaterialBinding()
        binding.value = coin.SoMaterialBinding.PER_FACE_INDEXED
        sep.addChild(binding)
        self.cutCoords = coin.SoCoordinate3()
        sep.addChild(self.cutCoords)
        self.cutFaces = coin.SoIndexedFaceSet()
        sep.addChild(self.cutFaces)
        self.cutNode = sep
        vobj.addDisplayMode(sep, "Cut")
        # Restore replays properties before the proxy attaches, so a
        # document saved with a mesh arrives here with CutMesh
        # already set and no updateData to come.
        self._updateCutMesh(vobj.Object)

    def getDisplayModes(self, vobj):
        return ["Cut"]

    def setDisplayMode(self, mode):
        return mode

    def updateData(self, obj, prop):
        if prop in ("CutMesh", "CutMeshUncutCount"):
            self._updateCutMesh(obj)

    def _updateCutMesh(self, obj):
        if getattr(self, "cutNode", None) is None:
            return
        mesh = getattr(obj, "CutMesh", None)
        if mesh is None or mesh.CountFacets == 0:
            self.cutCoords.point.setNum(0)
            self.cutFaces.coordIndex.setNum(0)
            return
        points, facets = mesh.Topology
        self.cutCoords.point.setValues(0, len(points), [(p.x, p.y, p.z) for p in points])
        self.cutCoords.point.setNum(len(points))
        index = []
        for f in facets:
            index.extend((f[0], f[1], f[2], -1))
        self.cutFaces.coordIndex.setValues(0, len(index), index)
        self.cutFaces.coordIndex.setNum(len(index))
        # Facets up to CutMeshUncutCount lie on the original stock
        # surface, the rest were machined; -1 (or nonsense) paints
        # everything as virgin stock.
        uncut = getattr(obj, "CutMeshUncutCount", -1)
        if 0 <= uncut <= len(facets):
            matIndex = [0] * uncut + [1] * (len(facets) - uncut)
        else:
            matIndex = [0] * len(facets)
        self.cutFaces.materialIndex.setValues(0, len(matIndex), matIndex)
        self.cutFaces.materialIndex.setNum(len(matIndex))


def EnsureViewProvider(obj):
    """Give a Stock object this module's view provider if it still has
    the plain icon one -- documents saved before the Cut mode restore
    with the old proxy, and the swap cannot happen at restore time
    (the Gui restores its proxies after the App side's
    onDocumentRestored has run, clobbering an eager swap)."""
    vobj = obj.ViewObject
    if vobj is None:
        return None
    proxy = vobj.Proxy
    if isinstance(proxy, ViewProvider):
        return proxy
    return ViewProvider(vobj, "Stock")


FreeCAD.Console.PrintLog("Loading PathStockGui... done\n")
