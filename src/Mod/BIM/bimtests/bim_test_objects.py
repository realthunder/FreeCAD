# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Developers                                 *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful,            *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with FreeCAD; if not, write to the Free Software        *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************

"""Run this file to create a standard test document for BIM (Arch) objects.

Modelled on ``drafttests.draft_test_objects``: one instance of every Arch
scripted-object class that can be built headless (``FreeCAD.GuiUp`` False)
is added to a document, on a grid of small metre-scale sites so that
nothing overlaps.  Every object recomputes cleanly.

Use it as input for the program executable.

::

    freecad bim_test_objects.py

Or load it as a module and use the defined function.

>>> import bimtests.bim_test_objects as bt
>>> bt.create_test_file()
"""

## @package bim_test_objects
# \ingroup bimtests
# \brief Run this file to create a standard test document for BIM objects.

## \addtogroup bimtests
# @{

import datetime

import FreeCAD as App
import Part
import Draft
import Arch
import ArchPrecast
import Sketcher
from FreeCAD import Vector
from draftutils.messages import _msg, _wrn

if App.GuiUp:
    import FreeCADGui as Gui

# Distance between the sites of the object grid (mm)
_PITCH = 10000.0


def _site(column, row=0):
    """Return the origin of the given cell of the object grid."""
    return Vector(column * _PITCH, row * _PITCH, 0)


def _placement(column, row=0):
    return App.Placement(_site(column, row), App.Rotation())


def _hide(obj):
    """Hide a helper object when a GUI is running."""
    if App.GuiUp and obj is not None and hasattr(obj, "ViewObject") and obj.ViewObject:
        obj.ViewObject.Visibility = False


def _create_frame(doc=None):
    """Draw a frame with information on the version of the software."""
    if not doc:
        doc = App.activeDocument()
    if not doc:
        doc = App.newDocument()

    version = App.Version()
    now = datetime.datetime.now().strftime("%Y/%m/%dT%H:%M:%S")

    _text = [
        "BIM test file",
        "Created: {}".format(now),
        "\n",
        "Version: " + ".".join(version[0:3]),
        "Release: " + " ".join(version[3:5]),
        "Branch: " + " ".join(version[5:]),
    ]

    record = doc.addObject("App::Annotation", "Description")
    record.LabelText = _text
    record.Position = Vector(0, -5000, 0)

    if App.GuiUp:
        record.ViewObject.DisplayMode = "World"
        record.ViewObject.FontSize = 400
        record.ViewObject.TextColor = (0.0, 0.0, 0.0)

    p1 = Vector(-5000, -8000, 0)
    p2 = Vector(11 * _PITCH, -8000, 0)
    p3 = Vector(11 * _PITCH, 3 * _PITCH, 0)
    p4 = Vector(-5000, 3 * _PITCH, 0)

    poly = Part.makePolygon([p1, p2, p3, p4, p1])
    frame = doc.addObject("Part::Feature", "Frame")
    frame.Shape = poly


def _create_objects(doc=None):
    """Create the objects of the test file.

    Returns the document.  Objects that need a GUI or an external file
    (Reference) are skipped with a printed message, as the Draft generator
    does for a missing font.
    """
    if not doc:
        doc = App.activeDocument()
    if not doc:
        doc = App.newDocument()
    # The Arch make functions work on the active document
    App.setActiveDocument(doc.Name)

    # Materials #############################################################

    _msg(16 * "-")
    _msg("Material")
    material = Arch.makeMaterial(name="CorpusMaterial", color=(0.8, 0.4, 0.2))
    material.Label = "CorpusMaterial"

    _msg(16 * "-")
    _msg("MultiMaterial")
    material_inner = Arch.makeMaterial(name="CorpusMaterialInner", color=(0.9, 0.9, 0.9))
    material_inner.Label = "CorpusMaterialInner"
    multi_material = Arch.makeMultiMaterial(name="CorpusMultiMaterial")
    multi_material.Label = "CorpusMultiMaterial"
    multi_material.Materials = [material, material_inner]
    multi_material.Names = ["Outer", "Inner"]
    multi_material.Thicknesses = [120, 80]

    # Row 0: walls, structures, openings ####################################

    # Wall from a Draft line
    _msg(16 * "-")
    _msg("Wall")
    base = _site(0)
    wall_line = Draft.make_line(base, base + Vector(4000, 0, 0))
    wall_line.Label = "CorpusWallLine"
    _hide(wall_line)
    doc.recompute()
    wall = Arch.makeWall(wall_line, width=200, height=2700, align="Left", name="CorpusWall")
    wall.Label = "CorpusWall"
    wall.Material = multi_material

    # Window from a preset, hosted in the wall
    _msg(16 * "-")
    _msg("Window (preset)")
    window_placement = App.Placement(
        base + Vector(1000, 0, 900), App.Rotation(Vector(1, 0, 0), 90)
    )
    window = Arch.makeWindowPreset(
        "Fixed",
        width=1000.0,
        height=1200.0,
        h1=50.0,
        h2=50.0,
        h3=50.0,
        w1=100.0,
        w2=50.0,
        o1=0.0,
        o2=50.0,
        placement=window_placement,
    )
    window.Label = "CorpusWindow"
    window.Hosts = [wall]
    if window.Base is not None:
        window.Base.Label = "CorpusWindowSketch"

    # Door from a preset, hosted in the wall
    _msg(16 * "-")
    _msg("Door (preset)")
    door_placement = App.Placement(base + Vector(2600, 0, 0), App.Rotation(Vector(1, 0, 0), 90))
    door = Arch.makeWindowPreset(
        "Simple door",
        width=900.0,
        height=2100.0,
        h1=50.0,
        h2=50.0,
        h3=0.0,
        w1=100.0,
        w2=40.0,
        o1=0.0,
        o2=0.0,
        placement=door_placement,
    )
    door.Label = "CorpusDoor"
    door.Hosts = [wall]
    if door.Base is not None:
        door.Base.Label = "CorpusDoorSketch"

    # Structure by dimensions (a beam)
    _msg(16 * "-")
    _msg("Structure (beam by dimensions)")
    beam = Arch.makeStructure(length=2000, width=200, height=300, name="CorpusBeam")
    beam.Label = "CorpusBeam"
    beam.Placement = _placement(1)
    beam.Material = material

    # Profile and a Structure (column) built from it
    _msg(16 * "-")
    _msg("Profile")
    profile = Arch.makeProfile([169, "HEA", "HEA100", "H", 100.0, 96.0, 5.0, 8.0])
    profile.Label = "CorpusProfile"
    profile.Placement = App.Placement(_site(1) + Vector(0, 2000, 0), App.Rotation())

    _msg(16 * "-")
    _msg("Structure (column from profile)")
    column = Arch.makeStructure(profile, height=2500.0, name="CorpusColumn")
    column.Label = "CorpusColumn"

    # Rebar: a structure hosting a sketch on its top face
    _msg(16 * "-")
    _msg("Rebar")
    rebar = None
    try:
        rebar_host = Arch.makeStructure(length=2000, width=300, height=400, name="CorpusRebarHost")
        rebar_host.Label = "CorpusRebarHost"
        rebar_host.Placement = _placement(2)
        doc.recompute()
        sketch = doc.addObject("Sketcher::SketchObject", "CorpusRebarSketch")
        sketch.AttachmentSupport = (rebar_host, ["Face6"])
        sketch.MapMode = "FlatFace"
        pts = [
            Vector(-850, 125, 0),
            Vector(750, 125, 0),
            Vector(750, -120, 0),
            Vector(-850, -120, 0),
        ]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]), False)
        for i in range(4):
            sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
        doc.recompute()
        rebar = Arch.makeRebar(rebar_host, sketch, diameter=12, amount=4, offset=25)
        rebar.Label = "CorpusRebar"
    except Exception as err:
        _wrn("Rebar could not be created: {}".format(err))

    # Precast beam
    _msg(16 * "-")
    _msg("Precast (beam)")
    precast = ArchPrecast.makePrecast(
        "Beam", length=2000, width=200, height=300, chamfer=10, dentlength=100, dentheight=50
    )
    precast.Label = "CorpusPrecastBeam"
    precast.Placement = _placement(3)

    # Axis, AxisSystem, Grid, StructuralSystem
    _msg(16 * "-")
    _msg("Axis")
    axis_x = Arch.makeAxis(num=3, size=1000, name="CorpusAxisX")
    axis_x.Label = "CorpusAxisX"
    axis_x.Length = 3000
    axis_x.Placement = _placement(4)

    axis_y = Arch.makeAxis(num=2, size=1500, name="CorpusAxisY")
    axis_y.Label = "CorpusAxisY"
    axis_y.Length = 3000
    axis_y.Placement = App.Placement(_site(4), App.Rotation(Vector(0, 0, 1), 90))

    _msg(16 * "-")
    _msg("AxisSystem")
    axis_system = Arch.makeAxisSystem([axis_x, axis_y], name="CorpusAxisSystem")
    axis_system.Label = "CorpusAxisSystem"

    _msg(16 * "-")
    _msg("StructuralSystem")
    system_column = Arch.makeStructure(length=200, width=200, height=2500, name="CorpusSysColumn")
    system_column.Label = "CorpusSysColumn"
    _hide(system_column)
    doc.recompute()
    structural_system = Arch.makeStructuralSystem(
        [system_column], [axis_x, axis_y], name="CorpusStructuralSystem"
    )
    structural_system.Label = "CorpusStructuralSystem"

    _msg(16 * "-")
    _msg("Grid")
    grid = Arch.makeGrid(name="CorpusGrid")
    grid.Label = "CorpusGrid"
    grid.Placement = _placement(5)

    # Row 1: roof, stairs, space, pipe, fence, panel, frame #################

    # Roof from a closed wire
    _msg(16 * "-")
    _msg("Roof")
    roof_wire = Draft.make_rectangle(length=4000, height=3000, placement=_placement(0, 1))
    roof_wire.Label = "CorpusRoofOutline"
    roof_wire.MakeFace = False
    _hide(roof_wire)
    doc.recompute()
    roof = Arch.makeRoof(
        roof_wire,
        angles=[30.0, 40.0, 30.0, 40.0],
        run=[1500.0, 0.0, 1500.0, 0.0],
        idrel=[-1, 0, -1, 0],
        thickness=[50.0],
        overhang=[100.0],
        name="CorpusRoof",
    )
    roof.Label = "CorpusRoof"

    # Stairs by dimensions, and railings on them
    _msg(16 * "-")
    _msg("Stairs")
    stairs = Arch.makeStairs(length=4000, width=1000, height=2700, steps=15, name="CorpusStairs")
    stairs.Label = "CorpusStairs"
    stairs.Placement = _placement(1, 1)
    doc.recompute()

    # Railings: makeStairs already calls makeRailing, which hangs one Pipe
    # (the railing) off RailingLeft/RailingRight; the stairs' execute then
    # gives each pipe a Part::Feature outline wire as its Base
    _msg(16 * "-")
    _msg("Railing")
    try:
        if not (stairs.RailingLeft or stairs.RailingRight):
            Arch.makeRailing([stairs])
            doc.recompute()
        for side in ("Left", "Right"):
            railing = getattr(stairs, "Railing" + side)
            if railing is None:
                _wrn("Railing{} could not be created from the stairs".format(side))
                continue
            railing.Label = "CorpusRailing" + side
            if railing.Base is not None:
                railing.Base.Label = "CorpusRailingWire" + side
    except Exception as err:
        _wrn("Railing could not be created: {}".format(err))

    # Space from a box
    _msg(16 * "-")
    _msg("Space")
    space_box = doc.addObject("Part::Box", "CorpusSpaceBox")
    space_box.Length = 4000
    space_box.Width = 3000
    space_box.Height = 2700
    space_box.Placement = _placement(2, 1)
    _hide(space_box)
    doc.recompute()
    space = Arch.makeSpace([space_box], name="CorpusSpace")
    space.Label = "CorpusSpace"

    # Pipe from a wire
    _msg(16 * "-")
    _msg("Pipe")
    base = _site(3, 1)
    pipe_wire = Draft.make_wire(
        [
            base,
            base + Vector(2000, 0, 0),
            base + Vector(2000, 1500, 0),
            base + Vector(2000, 1500, 1000),
        ]
    )
    pipe_wire.Label = "CorpusPipePath"
    _hide(pipe_wire)
    doc.recompute()
    pipe = Arch.makePipe(pipe_wire, diameter=100, name="CorpusPipe")
    pipe.Label = "CorpusPipe"

    # PipeConnector: two straight pipes meeting at a corner
    _msg(16 * "-")
    _msg("PipeConnector")
    base = _site(3, 1) + Vector(0, 4000, 0)
    corner = base + Vector(1500, 0, 0)
    pipe_line_a = Draft.make_line(base, corner)
    pipe_line_a.Label = "CorpusPipeALine"
    pipe_line_b = Draft.make_line(corner, corner + Vector(0, 1500, 0))
    pipe_line_b.Label = "CorpusPipeBLine"
    _hide(pipe_line_a)
    _hide(pipe_line_b)
    doc.recompute()
    pipe_a = Arch.makePipe(pipe_line_a, diameter=100, name="CorpusPipeA")
    pipe_a.Label = "CorpusPipeA"
    pipe_b = Arch.makePipe(pipe_line_b, diameter=100, name="CorpusPipeB")
    pipe_b.Label = "CorpusPipeB"
    doc.recompute()
    connector = Arch.makePipeConnector([pipe_a, pipe_b], radius=200, name="CorpusPipeConnector")
    connector.Label = "CorpusPipeConnector"

    # Fence: section + post + path
    _msg(16 * "-")
    _msg("Fence")
    base = _site(4, 1)
    section_placement = App.Placement(base, App.Rotation(Vector(1, 0, 0), 90))
    fence_section = Draft.make_rectangle(length=1900, height=1000, placement=section_placement)
    fence_section.Label = "CorpusFenceSection"
    fence_post = Draft.make_rectangle(
        length=100, height=100, placement=App.Placement(base, App.Rotation())
    )
    fence_post.Label = "CorpusFencePost"
    fence_path = Draft.make_line(base, base + Vector(6000, 0, 0))
    fence_path.Label = "CorpusFencePath"
    _hide(fence_section)
    _hide(fence_post)
    _hide(fence_path)
    doc.recompute()
    fence = Arch.makeFence(fence_section, fence_post, fence_path)
    fence.Label = "CorpusFence"

    # Panel from a rectangle
    _msg(16 * "-")
    _msg("Panel")
    panel_rect = Draft.make_rectangle(length=2400, height=1200, placement=_placement(5, 1))
    panel_rect.Label = "CorpusPanelOutline"
    _hide(panel_rect)
    doc.recompute()
    panel = Arch.makePanel(panel_rect, thickness=18, name="CorpusPanel")
    panel.Label = "CorpusPanel"

    # PanelCut and PanelSheet from that panel
    _msg(16 * "-")
    _msg("PanelCut")
    doc.recompute()
    panel_cut = Arch.makePanelCut(panel, name="CorpusPanelCut")
    panel_cut.Label = "CorpusPanelCut"
    panel_cut.Placement = App.Placement(_site(5, 1) + Vector(0, 3000, 0), App.Rotation())

    _msg(16 * "-")
    _msg("PanelSheet")
    panel_sheet = Arch.makePanelSheet([panel_cut], name="CorpusPanelSheet")
    panel_sheet.Label = "CorpusPanelSheet"
    panel_sheet.Placement = App.Placement(_site(5, 1) + Vector(0, 6000, 0), App.Rotation())

    # Frame: a profile swept along a path
    _msg(16 * "-")
    _msg("Frame")
    base = _site(6, 1)
    frame_path = Draft.make_wire([base, base + Vector(0, 0, 2000), base + Vector(2000, 0, 2000)])
    frame_path.Label = "CorpusFramePath"
    frame_profile = Draft.make_rectangle(
        length=100, height=60, placement=App.Placement(base, App.Rotation())
    )
    frame_profile.Label = "CorpusFrameProfile"
    _hide(frame_path)
    _hide(frame_profile)
    doc.recompute()
    frame = Arch.makeFrame(frame_path, frame_profile, name="CorpusFrame")
    frame.Label = "CorpusFrame"

    # Row 2: equipment, covering, curtain wall, truss ######################

    # Equipment from a shape
    _msg(16 * "-")
    _msg("Equipment")
    equipment_box = doc.addObject("Part::Box", "CorpusEquipmentBox")
    equipment_box.Length = 600
    equipment_box.Width = 600
    equipment_box.Height = 850
    _hide(equipment_box)
    doc.recompute()
    equipment = Arch.makeEquipment(
        equipment_box, placement=_placement(0, 2), name="CorpusEquipment"
    )
    equipment.Label = "CorpusEquipment"

    # Covering on the top face of a slab
    _msg(16 * "-")
    _msg("Covering")
    slab = doc.addObject("Part::Box", "CorpusCoveringSlab")
    slab.Length = 3000
    slab.Width = 2000
    slab.Height = 200
    slab.Placement = _placement(1, 2)
    doc.recompute()
    covering = Arch.makeCovering((slab, ["Face6"]), name="CorpusCovering")
    covering.Label = "CorpusCovering"
    covering.FinishMode = "Solid Tiles"
    covering.TileLength = 300.0
    covering.TileWidth = 300.0
    covering.JointWidth = 10.0
    covering.TileThickness = 20.0

    # Curtain wall from a vertical face
    _msg(16 * "-")
    _msg("CurtainWall")
    cw_placement = App.Placement(_site(2, 2), App.Rotation(Vector(1, 0, 0), 90))
    cw_face = Draft.make_rectangle(length=4000, height=3000, placement=cw_placement)
    cw_face.Label = "CorpusCurtainWallFace"
    cw_face.MakeFace = True
    _hide(cw_face)
    doc.recompute()
    curtain_wall = Arch.makeCurtainWall(cw_face, name="CorpusCurtainWall")
    curtain_wall.Label = "CorpusCurtainWall"

    # Truss from a line
    _msg(16 * "-")
    _msg("Truss")
    base = _site(3, 2)
    truss_line = Draft.make_line(base, base + Vector(6000, 0, 0))
    truss_line.Label = "CorpusTrussLine"
    _hide(truss_line)
    doc.recompute()
    truss = Arch.makeTruss(truss_line, name="CorpusTruss")
    truss.Label = "CorpusTruss"

    # Hierarchy ############################################################

    _msg(16 * "-")
    _msg("Floor")
    floor = Arch.makeFloor([wall, beam, column], name="CorpusFloor")
    floor.Label = "CorpusFloor"

    _msg(16 * "-")
    _msg("BuildingPart")
    building_part = Arch.makeBuildingPart([stairs], name="CorpusBuildingPart")
    building_part.Label = "CorpusBuildingPart"

    _msg(16 * "-")
    _msg("Building")
    building = Arch.makeBuilding([floor, building_part], name="CorpusBuilding")
    building.Label = "CorpusBuilding"

    _msg(16 * "-")
    _msg("Site")
    site = Arch.makeSite([building], name="CorpusSite")
    site.Label = "CorpusSite"

    _msg(16 * "-")
    _msg("Project")
    project = Arch.makeProject([site], name="CorpusProject")
    project.Label = "CorpusProject"

    # Annotation-like objects ##############################################

    _msg(16 * "-")
    _msg("SectionPlane")
    doc.recompute()
    section_plane = Arch.makeSectionPlane([wall], name="CorpusSectionPlane")
    section_plane.Label = "CorpusSectionPlane"

    _msg(16 * "-")
    _msg("2D Drawing")
    drawing = Arch.make2DDrawing(baseobj=section_plane, name="CorpusDrawing")
    drawing.Label = "CorpusDrawing"

    _msg(16 * "-")
    _msg("Schedule")
    schedule = Arch.makeSchedule()
    schedule.Label = "CorpusSchedule"
    schedule.Operation = ["Walls"]
    schedule.Value = ["Count"]
    schedule.Unit = [""]
    schedule.Objects = [""]
    schedule.Filter = ["Name:Wall"]

    _msg(16 * "-")
    _msg("Report")
    try:
        report = Arch.makeReport(name="CorpusReport")
        if report:
            report.Label = "CorpusReport"
            sheet = getattr(report, "Target", None)
            if sheet is not None:
                sheet.Label = "CorpusReportSheet"
    except Exception as err:
        _wrn("Report could not be created: {}".format(err))

    # Skipped ##############################################################

    _msg(16 * "-")
    _msg("Reference")
    _wrn("Reference skipped: it needs an external FCStd file to link to")

    doc.recompute()
    return doc


def create_test_file(file_name="bim_test_objects", file_path=None):
    """Create a complete test file of BIM objects.

    It draws a frame with information on the software used to create
    the test document, and fills it with every Arch object that can be
    created headless.

    Parameters
    ----------
    file_name: str, optional
        It defaults to `"bim_test_objects"`.
        Name of the document that is created.

    file_path: str, optional
        It defaults to `None`.
        When given, the document is saved to this full path (with the
        `.FCStd` extension) after creation.

    Returns
    -------
    App::Document
        A reference to the test document that was created.
    """
    _msg(16 * "-")
    _msg("If the units tests fail, this script may fail as well")
    doc = App.newDocument(file_name)

    _create_frame(doc=doc)
    _create_objects(doc=doc)

    if file_path:
        doc.saveAs(file_path)

    if App.GuiUp:
        Gui.runCommand("Std_ViewFitAll")
        Gui.Selection.clearSelection()

    return doc


## @}


if __name__ == "__main__":
    create_test_file()
