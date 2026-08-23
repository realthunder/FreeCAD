# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *                                                                         *
# *   Copyright (c) 2020 Yorik van Havre <yorik@uncreated.net>              *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

"""FreeCAD IFC importer - Multicore version"""

import os
import sys
import time

import FreeCAD
import Arch
import ArchIFC
import Draft

from FreeCAD import Base

from importers import importIFCHelper

# global dicts to store ifc object/freecad object relationships

layers = {}  # ifcid : Draft_Layer
materials = {}  # ifcid : Arch_Material
objects = {}  # ifcid : Arch_Component
subs = {}  # host_ifcid: [child_ifcid,...]
adds = {}  # host_ifcid: [child_ifcid,...]
colors = {}  # objname : (r,g,b)
settableattributes = {}  # (ifc entity type, ifc type) : [attribute name,...]
layermembers = {}  # ifcid : [Arch_Component,...] waiting to be put in the layer


def open(filename):
    "opens an IFC file in a new document"

    return insert(filename)


def insert(filename, docname=None, preferences=None):
    """imports the contents of an IFC file in the given document

    A wrapper around the import proper, so the live-view state is given back
    on every path out of it, including the ones that raise. This is the
    importer a plain file import actually reaches: importIFC.insert() routes
    here whenever MULTICORE is set, and getMulticore() never returns 0.
    """
    from importers.importIFC import _set_live_import

    try:
        return _insert(filename, docname, preferences)
    finally:
        _set_live_import(None, False)


def _insert(filename, docname=None, preferences=None):
    """imports the contents of an IFC file in the given document"""

    import ifcopenshell
    from ifcopenshell import geom

    # reset global values
    global layers
    global materials
    global objects
    global adds
    global subs
    global settableattributes
    global layermembers
    layers = {}
    materials = {}
    objects = {}
    adds = {}
    subs = {}
    layermembers = {}
    # the attribute cache is keyed on entity type, which only means the same
    # thing within one schema
    settableattributes = {}

    # statistics
    starttime = time.time()  # in seconds
    filesize = os.path.getsize(filename) * 0.000001  # in megabytes
    print("Opening", filename + ",", round(filesize, 2), "Mb")

    # setup ifcopenshell
    if not preferences:
        preferences = importIFCHelper.getPreferences()
    settings = importIFCHelper.getGeomSettings(preferences)

    # setup document
    if not FreeCAD.ActiveDocument:
        if not docname:
            docname = os.path.splitext(os.path.basename(filename))[0]
        doc = FreeCAD.newDocument(docname)
        doc.Label = docname
        FreeCAD.setActiveDocument(doc.Name)

    # open the file
    ifcfile = ifcopenshell.open(filename)
    progressbar = Base.ProgressIndicator()
    productscount = len(ifcfile.by_type("IfcProduct"))
    progressbar.start("Importing " + str(productscount) + " products...", productscount)
    cores = preferences["MULTICORE"]
    iterator = ifcopenshell.geom.iterator(settings, ifcfile, cores)
    iterator.initialize()
    count = 0
    aborted = False

    # The 3D view is the user's for the duration: the loop below offers the
    # event loop a turn per product, so there is something to deliver input
    # with. Released by insert()'s finally, on every path.
    from importers.importIFC import _set_live_import, _pump_live_import
    doc = FreeCAD.ActiveDocument
    docname = doc.Name
    _set_live_import(doc, True)

    # process objects
    try:
        for item in iterator:
            if docname not in FreeCAD.listDocuments():
                # A live view is one the user can act on, so the document can
                # go away under the loop -- and every product from here on
                # would be built into whatever document is active next.
                raise RuntimeError("the document being imported into was closed")
            brep = item.geometry.brep_data
            # 0.8 tells the two apart: guid is the IfcGloballyUniqueId string,
            # id is the STEP entity id that by_id wants
            ifcproduct = ifcfile.by_id(item.id if hasattr(item,"id") else item.guid)
            obj = createProduct(ifcproduct,brep)
            progressbar.next(True)
            # next() only pumps on its own 200ms bar-update throttle, which is
            # a slideshow to someone orbiting the model; this offers a turn per
            # product and is throttled on its own, shorter, budget.
            _pump_live_import()
            writeProgress(count, productscount, starttime)
            count += 1
    except FreeCAD.Base.FreeCADAbort:
        # Escape. Stop making products, but still finish the file: the layers,
        # relationships and colours all belong to products that were made, and
        # a half-related model is worse than a smaller one. The caller's
        # transaction still commits, so one undo takes the import back.
        aborted = True
        FreeCAD.Console.PrintWarning(
            "IFC import aborted after "
            + str(count)
            + " of "
            + str(productscount)
            + " products\n"
        )

    # process 2D annotations
    annotations = [] if aborted else ifcfile.by_type("IfcAnnotation")
    if annotations:
        print("Processing", str(len(annotations)), "annotations...")
        ifcscale = importIFCHelper.getScaling(ifcfile)
        for annotation in annotations:
            importIFCHelper.createAnnotation(
                annotation, FreeCAD.ActiveDocument, ifcscale, preferences
            )

    # post-processing
    applyLayers()
    processRelationships()
    storeColorDict()

    # finished
    importIFCHelper.reportFileDefects()
    progressbar.stop()
    FreeCAD.ActiveDocument.recompute()
    endtime = round(time.time() - starttime, 1)
    fs = round(filesize, 1)
    ratio = int(endtime / filesize)
    endtime = "%02d:%02d" % (divmod(endtime, 60))
    writeProgress()  # this cleans the line
    print("Finished importing", fs, "Mb in", endtime, "s, or", ratio, "s/Mb")
    return FreeCAD.ActiveDocument


def writeProgress(count=None, total=None, starttime=None):
    """write progress to console"""

    if not FreeCAD.GuiUp:
        if count is None:
            sys.stdout.write("\r")
            return
        r = count / total
        elapsed = round(time.time() - starttime, 1)
        if r:
            rest = elapsed * ((1 - r) / r)
            eta = "%02d:%02d" % (divmod(rest, 60))
        else:
            eta = "--:--"
        hashes = "#" * int(r * 10) + " " * int(10 - r * 10)
        fstring = "\rImporting " + str(total) + " products [{0}] {1}%, ETA: {2}"
        sys.stdout.write(fstring.format(hashes, int(r * 100), eta))


def createProduct(ifcproduct, brep):
    """creates an Arch object from an IFC product"""

    import Part

    shape = Part.Shape()
    shape.importBrepFromString(brep, False)
    shape.scale(1000.0)  # IfcOpenShell outputs in meters
    if ifcproduct.is_a("IfcSpace"):
        obj = Arch.makeSpace()
    else:
        obj = Arch.makeComponent()
    obj.Shape = shape
    objects[ifcproduct.id()] = obj
    setAttributes(obj, ifcproduct)
    setProperties(obj, ifcproduct)
    createLayer(obj, ifcproduct)
    createMaterial(obj, ifcproduct)
    createModelStructure(obj, ifcproduct)
    setRelationships(obj, ifcproduct)
    setColor(obj, ifcproduct)
    return obj


def setAttributes(obj, ifcproduct):
    """sets the IFC attributes of a component"""

    ifctype = ArchIFC.uncamel(ifcproduct.is_a())
    if ifcproduct.Name:
        obj.Label = ifcproduct.Name
    if ifctype in ArchIFC.IfcTypes:
        obj.IfcType = ifctype
    for attr in getSettableAttributes(obj, ifcproduct, ifctype):
        value = getattr(ifcproduct, attr)
        if value:
            try:
                setattr(obj, attr, value)
            except Exception:
                pass


def getSettableAttributes(obj, ifcproduct, ifctype):
    """returns the IFC attribute names this object has a matching property for

    Both halves of that question are settled by the entity type and the IFC
    type alone, so the answer is memoised: dir() rebuilds the entity's
    attribute list and PropertiesList rebuilds the object's property list, and
    asking for the second one inside a loop over the first rebuilt it once per
    attribute per product.
    """

    key = (ifcproduct.is_a(), ifctype)
    attrs = settableattributes.get(key)
    if attrs is None:
        properties = set(obj.PropertiesList)
        attrs = [attr for attr in dir(ifcproduct) if attr in properties]
        settableattributes[key] = attrs
    return attrs


def setProperties(obj, ifcproduct):
    """sets the IFC properties of a component"""

    props = obj.IfcProperties
    for prel in ifcproduct.IsDefinedBy:
        if prel.is_a("IfcRelDefinesByProperties"):
            pset = prel.RelatingPropertyDefinition
            if pset.is_a("IfcPropertySet"):
                for prop in pset.HasProperties:
                    if hasattr(prop, "NominalValue"):
                        propname = prop.Name + ";;" + pset.Name
                        v = [p.strip("'") for p in str(prop.NominalValue).strip(")").split(")")]
                        propvalue = ";;".join(v)


def setColor(obj, ifcproduct):
    """sets the color of an object"""

    global colors

    color = importIFCHelper.getColorFromProduct(ifcproduct)
    colors[obj.Name] = color
    if FreeCAD.GuiUp and color:
        obj.ViewObject.ShapeColor = color[:3]


def makeLayer(name):
    """makes a Draft layer that leaves its children's own colours alone

    A Layer paints every object added to it with its own LineColor and
    ShapeAppearance -- unconditionally, because Layer.onChanged() passes
    old_prop=None for a newly added child, which is the "overwrite whatever
    is there" case. What the IFC file says an element looks like is not a
    default to be improved on, so the two override switches go off before
    any member arrives.

    This used not to matter: the layer add ran inside createProduct(),
    one line before setColor(), so the colour landed after the stamp and
    won. Batching the adds into applyLayers() moved them to the end of the
    import, after every setColor() -- and the stamp started landing on top
    of the colours instead, turning 2904 of the 13758 products of one IFC
    building the layer's default grey.
    """

    layer = Draft.make_layer(name)
    vobj = getattr(layer, "ViewObject", None)
    if vobj is not None:
        # Upstream renamed OverrideShapeColorChildren to
        # OverrideShapeAppearanceChildren; both names are tolerated so this
        # keeps working against either Draft.
        for prop in ("OverrideLineColorChildren",
                     "OverrideShapeColorChildren",
                     "OverrideShapeAppearanceChildren"):
            if hasattr(vobj, prop):
                setattr(vobj, prop, False)
    return layer


def createLayer(obj, ifcproduct):
    """queues a component for its layers -- applyLayers() does the assigning"""

    global layers
    global layermembers

    if ifcproduct.Representation:
        for rep in ifcproduct.Representation.Representations:
            for layer in rep.LayerAssignments:
                if not layer.id() in layers:
                    layers[layer.id()] = makeLayer(layer.Name)
                layermembers.setdefault(layer.id(), []).append(obj)


def applyLayers():
    """puts the queued components in their layers, one assignment per layer

    Adding one child means writing the whole Group property back, and the
    Layer's own onChanged then walks that group. Doing that per child costs
    the square of the layer's size, and an IFC model puts thousands of
    objects in a single layer.
    """

    global layers
    global layermembers

    for layerid, members in layermembers.items():
        layer = layers[layerid]
        group = layer.Group
        names = {o.Name for o in group}
        for member in members:
            if member.Name not in names:
                names.add(member.Name)
                group.append(member)
        layer.Group = group
    layermembers = {}


def createMaterial(obj, ifcproduct):
    """sets the material of a component"""

    global materials

    for association in ifcproduct.HasAssociations:
        if association.is_a("IfcRelAssociatesMaterial"):
            material = association.RelatingMaterial
            if material.is_a("IfcMaterialList"):
                material = material.Materials[0]  # take the first one for now...
            if material.is_a("IfcMaterial"):
                if not material.id() in materials:
                    color = importIFCHelper.getColorFromMaterial(material)
                    materials[material.id()] = Arch.makeMaterial(material.Name, color=color)
                obj.Material = materials[material.id()]


def createModelStructure(obj, ifcobj):
    """sets the parent containers of an IFC object"""

    global objects

    for parent in importIFCHelper.getParents(ifcobj):
        if not parent.id() in objects:
            if parent.is_a("IfcProject"):
                parentobj = Arch.makeProject()
            elif parent.is_a("IfcSite"):
                parentobj = Arch.makeSite()
            else:
                parentobj = Arch.makeBuildingPart()
            setAttributes(parentobj, parent)
            setProperties(parentobj, parent)
            createModelStructure(parentobj, parent)
            objects[parent.id()] = parentobj
        if hasattr(objects[parent.id()].Proxy, "addObject"):
            objects[parent.id()].Proxy.addObject(objects[parent.id()], obj)


def setRelationships(obj, ifcobj):
    """sets additions/subtractions"""

    global adds
    global subs

    if hasattr(ifcobj, "HasOpenings") and ifcobj.HasOpenings:
        for rel in ifcobj.HasOpenings:
            subs.setdefault(ifcobj.id(), []).append(rel.RelatedOpeningElement)

    # TODO: assemblies & booleans


def processRelationships():
    """process all stored relationships"""

    for dom in ((subs, "Subtractions"), (adds, "Additions")):
        for key, vals in dom[0].items():
            if key in objects:
                for val in vals:
                    if val in objects:
                        if hasattr(objects[key], dom[1]):
                            g = getattr(objects[key], dom[1])
                            g.append(val)
                            setattr(objects[key], dom[1], g)


def storeColorDict():
    """stores the color dictionary in the document Meta if non-GUI mode"""

    if colors and not FreeCAD.GuiUp:
        import json

        d = FreeCAD.ActiveDocument.Meta
        d["colordict"] = json.dumps(colors)
        FreeCAD.ActiveDocument.Meta = d
