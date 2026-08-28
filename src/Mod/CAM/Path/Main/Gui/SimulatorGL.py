# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2017 Shai Seger <shaise at gmail>                       *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************
"""
Command and task window handler for the OpenGL based CAM simulator
"""

import math
import os
import threading
import traceback
import FreeCAD
import Path
import Path.Base.Util as PathUtil
import Path.Dressup.Utils as PathDressup
from PathScripts import PathUtils
import CAMSimulator

from FreeCAD import Vector, Placement, Rotation

# lazily loaded modules
from lazy_loader.lazy_loader import LazyLoader

Mesh = LazyLoader("Mesh", globals(), "Mesh")
Part = LazyLoader("Part", globals(), "Part")

if FreeCAD.GuiUp:
    import FreeCADGui
    from PySide import QtGui, QtCore
    from PySide.QtGui import QDialogButtonBox

_filePath = os.path.dirname(os.path.abspath(__file__))


def IsSame(x, y):
    """Check if two floats are the same within an epsilon"""
    return abs(x - y) < 0.0001


def RadiusAt(edge, p):
    """Find the tool radius within a point on its circumference"""
    x = edge.valueAt(p).x
    y = edge.valueAt(p).y
    return math.sqrt(x * x + y * y)


class CAMSimTaskUi:
    """Handles the simulator task panel"""

    def __init__(self, parent):
        # this will create a Qt widget from our ui file
        self.form = FreeCADGui.PySideUic.loadUi(":/panels/TaskCAMSimulator.ui")
        self.parent = parent

    def getStandardButtons(self, *_args):
        """Task panel needs only Close button"""
        return QDialogButtonBox.Close

    def reject(self):
        """User Pressed the Close button"""
        self.parent.cancel()
        FreeCADGui.Control.closeDialog()


def TSError(msg):
    """Display error message"""
    QtGui.QMessageBox.information(None, "Path Simulation", msg)


class _CutMeshWorker(threading.Thread):
    """Replays a stopped GL simulation through the volumetric
    simulator and meshes the result (docs/CAMSimRenderPort.md
    11.7.3). The items list is precomputed on the GUI thread; this
    thread only applies it -- the PathSim bindings release the GIL,
    which is what keeps the GUI live while it grinds. The un-machined
    and machined result halves are merged, un-machined first, and the
    split recorded for the Cut display mode's two-tone colouring."""

    def __init__(self, items, stockShape, resolution, toolAccuracy, initialPos, key):
        super().__init__(daemon=True)
        self.items = items
        self.stockShape = stockShape
        self.resolution = resolution
        self.toolAccuracy = toolAccuracy
        self.initialPos = initialPos
        self.key = key
        self.cancelled = False
        self.result = None
        self.error = None

    def run(self):
        try:
            import PathSimulator

            sim = PathSimulator.PathSim()
            sim.BeginSimulation(self.stockShape, self.resolution)
            pos = Placement(self.initialPos, Rotation())
            for kind, payload in self.items:
                if self.cancelled:
                    return
                if kind == "tool":
                    sim.SetToolShape(payload, self.toolAccuracy)
                else:
                    pos = sim.ApplyCommand(pos, payload)
            if self.cancelled:
                return
            outer, inner = sim.GetResultMesh()
            uncut = outer.CountFacets
            outer.addMesh(inner)
            if not self.cancelled:
                self.result = (self.key, outer, uncut)
        except Exception:
            self.error = traceback.format_exc()


class _CutMeshSwap:
    """The run-state swap, Route B half (docs/CAMSimRenderPort.md
    11.7): polls the GL simulator, and once it has stood still for
    the debounce interval, replays the consumed motions through the
    volumetric simulator on a worker thread and lands the merged
    result on the Job's Stock object through its Cut display mode.
    Play reverses the landing and the pixels take over again.

    The replay runs off the GL parser's own motion list rather than
    the original commands: sticky words are resolved, drill cycles
    already expanded, and the final motion truncates cleanly at the
    sub-step interpolant -- so the mesh lands exactly where the
    pixels stopped, mid-command included. Rapids are fed too (the GL
    sim cuts on every move; the volumetric one treats G0 as G1)."""

    POLL_MS = 250
    QUIET_POLLS = 2

    def __init__(self, millSim, job, quality):
        self.millSim = millSim
        self.stockObj = job.Stock
        # Re-read every poll, never cached: the simulator's overlay
        # button writes this preference, so a cached copy would keep
        # re-attaching after the operator switched it off
        # (docs/CAMSimRenderPort.md sec 11.11).
        self.prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/CAM")
        self.docAttached = False
        self.savedDocVisibility = None
        self.stockShape = job.Stock.Shape
        accuracy = max(0.1, 1.1 - 0.1 * quality)
        bb = self.stockShape.BoundBox
        self.resolution = 0.01 * accuracy * max(bb.XLength, bb.YLength)
        self.toolAccuracy = 0.05 * accuracy
        self.initialPos = Vector(0, 0, bb.ZMax)
        self.toolShapes = {}
        self.worker = None
        self.landed = False
        self.savedMode = None
        self.savedVisibility = None
        self.lastMeshedKey = None
        self.lastPos = None
        self.quiet = 0
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self._poll)
        self.timer.start(self.POLL_MS)

    def addTool(self, toolNumber, shape):
        self.toolShapes[toolNumber] = shape

    def stop(self):
        """Panel closing: stop observing. A landed mesh stays -- the
        settled cut shape outliving the simulator is the feature."""
        self.timer.stop()
        self._cancelWorker()
        self._detachDocView()

    def _attachDocView(self):
        """Route A in the document view (docs/CAMSimRenderPort.md sec
        11.9 stage 3): while the pixels are the only carve there is,
        fan the simulator's drawing out to the document's own 3D view,
        and hide the Stock there -- its uncut wireframe would sit on
        top of the carved one. False from the attach (no renderer to
        borrow) just leaves the document view alone."""
        if self.docAttached or not self._showInDocView():
            return
        if not self.millSim.AttachDocumentView():
            return
        self.docAttached = True
        vobj = self.stockObj.ViewObject
        self.savedDocVisibility = vobj.Visibility
        vobj.Visibility = False

    def _showInDocView(self):
        return self.prefs.GetBool("SimulatorShowInDocumentView", True)

    def _detachDocView(self):
        if not self.docAttached:
            return
        self.millSim.DetachDocumentView()
        self.docAttached = False
        if self.savedDocVisibility is not None:
            self.stockObj.ViewObject.Visibility = self.savedDocVisibility
            self.savedDocVisibility = None

    def _cancelWorker(self):
        if self.worker is not None:
            self.worker.cancelled = True
            self.worker = None

    def _poll(self):
        try:
            # Switched off mid-session (the overlay button or the
            # preferences page): give the document view back now
            # rather than at the next mesh landing.
            if self.docAttached and not self._showInDocView():
                self._detachDocView()
            playing, motionIndex, fraction = self.millSim.GetProgress()
            key = (motionIndex, round(fraction, 4))
            if playing:
                self.quiet = 0
                self._cancelWorker()
                self._unland()
                self._attachDocView()
                self.lastPos = key
                return
            if motionIndex < 0:
                return
            if key != self.lastPos:
                # Scrubbing moves the pixels just like playing does:
                # the document view follows them, and the landing
                # below takes it back when the position settles.
                self._attachDocView()
                self.lastPos = key
                self.quiet = 0
                return
            worker = self.worker
            if worker is not None:
                if worker.error:
                    self.worker = None
                    FreeCAD.Console.PrintError(
                        "CAM cut mesh replay failed:\n" + worker.error
                    )
                elif worker.result is not None:
                    self.worker = None
                    self._land(*worker.result)
                return
            if key == self.lastMeshedKey:
                return
            self.quiet += 1
            if self.quiet >= self.QUIET_POLLS:
                self.quiet = 0
                self._startWorker(motionIndex, fraction)
        except Exception:
            # The document or the Stock can go away under the timer;
            # stop observing rather than fail once per poll.
            self.stop()
            raise

    def _startWorker(self, motionIndex, fraction):
        items = self._buildItems(motionIndex, fraction)
        if not items:
            return
        key = (motionIndex, round(fraction, 4))
        self.worker = _CutMeshWorker(
            items, self.stockShape, self.resolution, self.toolAccuracy, self.initialPos, key
        )
        self.worker.start()

    def _buildItems(self, motionIndex, fraction):
        """Everything the GL sim has consumed, as ("tool", shape) and
        ("cmd", Path.Command) entries ready for the worker's dumb
        apply loop: arcs expanded to chords sized by the resolution
        (the expansion Simulator.py always fed VolSim), the final
        motion truncated at the consumed fraction."""
        sim = self.millSim
        first = sim.GetMotion(0)
        if first is None:
            return None
        items = [("cmd", Path.Command("G0", {"X": first["x"], "Y": first["y"], "Z": first["z"]}))]
        curTool = None
        prev = first
        for m in range(1, motionIndex + 1):
            motion = sim.GetMotion(m)
            if motion is None:
                break
            tool = motion["tool"]
            if tool != curTool and tool in self.toolShapes:
                items.append(("tool", self.toolShapes[tool]))
                curTool = tool
            frac = fraction if m == motionIndex else 1.0
            self._motionCommands(items, prev, motion, frac)
            prev = motion
        return items

    def _motionCommands(self, items, prev, motion, frac):
        if motion["type"] == "line":
            x = prev["x"] + (motion["x"] - prev["x"]) * frac
            y = prev["y"] + (motion["y"] - prev["y"]) * frac
            z = prev["z"] + (motion["z"] - prev["z"]) * frac
            items.append(("cmd", Path.Command("G1", {"X": x, "Y": y, "Z": z})))
            return
        if motion["type"] not in ("cw", "ccw"):
            return
        # i/j hold the centre offset from the start point, raw from
        # the g-code word
        cx = prev["x"] + motion["i"]
        cy = prev["y"] + motion["j"]
        radius = math.sqrt(motion["i"] ** 2 + motion["j"] ** 2)
        if radius < 1e-9:
            return
        a0 = math.atan2(prev["y"] - cy, prev["x"] - cx)
        a1 = math.atan2(motion["y"] - cy, motion["x"] - cx)
        da = a1 - a0
        if motion["type"] == "ccw":
            da = da % (2 * math.pi)
        else:
            da = -((-da) % (2 * math.pi))
        da *= frac
        dzTotal = (motion["z"] - prev["z"]) * frac
        n = max(1, math.ceil(math.sqrt(radius * da * da / self.resolution)))
        for i in range(1, n + 1):
            a = a0 + da * i / n
            items.append(
                (
                    "cmd",
                    Path.Command(
                        "G1",
                        {
                            "X": cx + radius * math.cos(a),
                            "Y": cy + radius * math.sin(a),
                            "Z": prev["z"] + dzTotal * i / n,
                        },
                    ),
                )
            )

    def _land(self, key, mesh, uncut):
        import Path.Main.Gui.Stock as PathStockGui

        # The mesh replaces the pixels: the document view goes back to
        # drawing its own objects -- the Stock, now in its Cut mode --
        # before the landing below makes that mode current.
        self._detachDocView()

        stock = self.stockObj
        PathStockGui.EnsureViewProvider(stock)
        stock.CutMesh = mesh
        stock.CutMeshUncutCount = uncut
        vobj = stock.ViewObject
        if not self.landed:
            self.savedMode = vobj.DisplayMode
            self.savedVisibility = vobj.Visibility
        if "Cut" in vobj.getEnumerationsOfProperty("DisplayMode"):
            vobj.DisplayMode = "Cut"
        vobj.Visibility = True
        self.landed = True
        self.lastMeshedKey = key

    def _unland(self):
        if not self.landed:
            return
        vobj = self.stockObj.ViewObject
        if self.savedMode:
            vobj.DisplayMode = self.savedMode
        if self.savedVisibility is not None:
            vobj.Visibility = self.savedVisibility
        self.landed = False
        self.lastMeshedKey = None


class CAMSimulation:
    """Handles and prepares CAM jobs for simulation"""

    def __init__(self):
        self.debug = False
        self.stdrot = FreeCAD.Rotation(Vector(0, 0, 1), 0)
        self.iprogress = 0
        self.numCommands = 0
        self.simperiod = 20
        self.quality = 10
        self.resetSimulation = False
        self.jobs = []
        self.initdone = False
        self.taskForm = None
        self.disableAnim = False
        self.firstDrill = True
        self.millSim = None
        self.job = None
        self.activeOps = []
        self.ioperation = 0
        self.stock = None
        self.busy = False
        self.operations = []
        self.baseShape = None
        self.cutSwap = None

    def Connect(self, but, sig):
        """Connect task panel buttons"""
        QtCore.QObject.connect(but, QtCore.SIGNAL("clicked()"), sig)

    def FindClosestEdge(self, edges, px, pz):
        """Convert tool shape to tool profile needed by GL simulator"""
        for edge in edges:
            p1 = edge.FirstParameter
            p2 = edge.LastParameter
            rad1 = RadiusAt(edge, p1)
            z1 = edge.valueAt(p1).z
            if IsSame(px, rad1) and IsSame(pz, z1):
                return edge, p1, p2
            rad2 = RadiusAt(edge, p2)
            z2 = edge.valueAt(p2).z
            if IsSame(px, rad2) and IsSame(pz, z2):
                return edge, p2, p1
            # sometimes a flat circle is without edge, so return edge with
            # same height and later a connecting edge will be interpolated
            if IsSame(pz, z1):
                return edge, p1, p2
            if IsSame(pz, z2):
                return edge, p2, p1
        return None, 0.0, 0.0

    def FindTopMostEdge(self, edges):
        """Examine tool solid edges and find the top most one"""
        maxz = -99999999.0
        topedge = None
        top_p1 = 0.0
        top_p2 = 0.0
        for edge in edges:
            p1 = edge.FirstParameter
            p2 = edge.LastParameter
            z = edge.valueAt(p1).z
            if z > maxz:
                topedge = edge
                top_p1 = p1
                top_p2 = p2
                maxz = z
            z = edge.valueAt(p2).z
            if z > maxz:
                topedge = edge
                top_p1 = p2
                top_p2 = p1
                maxz = z
        return topedge, top_p1, top_p2

    def GetToolProfile(self, tool, resolution):
        """Get the edge profile of a tool solid. Basically locating the
        side edge that OCC creates on any revolved object
        """
        shape = tool.Shape.copy()
        shape.Placement = Placement()
        sideEdgeList = []
        for _i, edge in enumerate(shape.Edges):
            if not edge.isClosed():
                # v1 = edge.firstVertex()
                # v2 = edge.lastVertex()
                # tp = "arc" if type(edge.Curve) is Part.Circle else "line"
                sideEdgeList.append(edge)

        # sort edges as a single 3d line on the x-z plane

        # first find the topmost edge
        edge, p1, p2 = self.FindTopMostEdge(sideEdgeList)
        profile = [RadiusAt(edge, p1), edge.valueAt(p1).z]
        endrad = 0.0
        # one by one find all connecting edges
        while edge is not None:
            sideEdgeList.remove(edge)
            if isinstance(edge.Curve, Part.Circle):
                # if edge is curved, approximate it with lines based on resolution
                nsegments = int(edge.Length / resolution) + 1
                step = (p2 - p1) / nsegments
                location = p1 + step
                while nsegments > 0:
                    endrad = RadiusAt(edge, location)
                    endz = edge.valueAt(location).z
                    profile.append(endrad)
                    profile.append(endz)
                    location += step
                    nsegments -= 1
            else:
                endrad = RadiusAt(edge, p2)
                endz = edge.valueAt(p2).z
                profile.append(endrad)
                profile.append(endz)
            edge, p1, p2 = self.FindClosestEdge(sideEdgeList, endrad, endz)
            if edge is None:
                break
            startrad = RadiusAt(edge, p1)
            if not IsSame(startrad, endrad):
                profile.append(startrad)
                startz = edge.valueAt(p1).z
                profile.append(startz)

        return profile

    def Activate(self):
        """Invoke the simulator task panel"""
        self.initdone = False
        self.taskForm = CAMSimTaskUi(self)
        form = self.taskForm.form
        self.Connect(form.toolButtonPlay, self.SimPlay)
        form.sliderAccuracy.valueChanged.connect(self.onAccuracyBarChange)
        self.onAccuracyBarChange()

        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/CAM")
        if prefs.GetBool("SimulatorFollowsVisibility"):
            form.followsVisibility.setCheckState(QtCore.Qt.CheckState.Checked)
        form.followsVisibility.clicked.connect(self.followsVisibilityChange)

        self._populateJobSelection(form)
        form.comboJobs.currentIndexChanged.connect(self.onJobChange)
        self.onJobChange()
        form.listOperations.itemChanged.connect(self.onOperationItemChange)
        FreeCADGui.Control.showDialog(self.taskForm)
        self.disableAnim = False
        self.firstDrill = True
        self.millSim = CAMSimulator.PathSim()
        self.initdone = True
        self.job = self.jobs[self.taskForm.form.comboJobs.currentIndex()]
        # self.SetupSimulation()

    def _populateJobSelection(self, form):
        """Make Job selection combobox"""
        # Get list of Job objects in active document
        jobList = FreeCAD.ActiveDocument.findObjects("Path::FeaturePython", "Job.*")

        # Get name of selected Job
        jobName = ""
        selection = FreeCADGui.Selection.getSelection()
        if selection:  #  Identify job selected by user
            job = PathUtils.findParentJob(selection[0])
            if job:
                jobName = job.Name

        # Prepare combobox
        form.comboJobs.blockSignals(True)
        form.comboJobs.clear()
        form.comboJobs.blockSignals(False)

        # Get index of selected Job
        setJobIdx = 0
        for i, job in enumerate(jobList):
            # Populate the job selection combobox
            form.comboJobs.addItem(job.ViewObject.Icon, job.Label)
            self.jobs.append(job)
            if job.Name == jobName:
                setJobIdx = i

        # Preselect GUI-selected job in the combobox
        form.comboJobs.setCurrentIndex(setJobIdx)

    def SetupSimulation(self):
        """Prepare all selected job operations for simulation"""
        form = self.taskForm.form
        self.activeOps = []
        self.numCommands = 0
        self.ioperation = 0
        for i in range(form.listOperations.count()):
            if form.listOperations.item(i).checkState() == QtCore.Qt.CheckState.Checked:
                self.firstDrill = True
                self.activeOps.append(self.operations[i])
                self.numCommands += len(self.operations[i].Path.Commands)

        self.stock = self.job.Stock.Shape
        self.busy = False

    def onJobChange(self):
        """When a new job is selected from the drop-down, update job operation list"""
        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/CAM")
        followsVisibility = prefs.GetBool("SimulatorFollowsVisibility")
        form = self.taskForm.form
        j = self.jobs[form.comboJobs.currentIndex()]
        self.job = j
        form.listOperations.clear()
        self.operations = []
        allhidden = all(
            not op.Visibility for op in j.Operations.OutList if PathUtil.opProperty(op, "Active")
        )
        for op in j.Operations.OutList:
            if PathUtil.opProperty(op, "Active"):
                listItem = QtGui.QListWidgetItem(op.ViewObject.Icon, op.Label)
                listItem.setFlags(listItem.flags() | QtCore.Qt.ItemIsUserCheckable)
                if followsVisibility and not op.Visibility and not allhidden:
                    listItem.setCheckState(QtCore.Qt.CheckState.Unchecked)
                else:
                    listItem.setCheckState(QtCore.Qt.CheckState.Checked)
                self.operations.append(op)
                form.listOperations.addItem(listItem)
        if len(j.Model.OutList) > 0:
            self.baseShape = Part.makeCompound([o.Shape for o in j.Model.OutList])
        else:
            self.baseShape = None

    def onAccuracyBarChange(self):
        """Update simulation quality"""
        form = self.taskForm.form
        self.quality = form.sliderAccuracy.value()
        qualText = QtCore.QT_TRANSLATE_NOOP("CAM_Simulator", "High")
        if self.quality < 4:
            qualText = QtCore.QT_TRANSLATE_NOOP("CAM_Simulator", "Low")
        elif self.quality < 9:
            qualText = QtCore.QT_TRANSLATE_NOOP("CAM_Simulator", "Medium")
        form.labelAccuracy.setText(qualText)

    def followsVisibilityChange(self):
        """Update job list in accordance with operations visibility"""
        form = self.taskForm.form
        state = form.followsVisibility.isChecked()
        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/CAM")
        prefs.SetBool("SimulatorFollowsVisibility", state)
        self.onJobChange()

    def onOperationItemChange(self, _item):
        """Check if at least one operation is selected to enable the Play button"""
        playvalid = False
        form = self.taskForm.form
        for i in range(form.listOperations.count()):
            if form.listOperations.item(i).checkState() == QtCore.Qt.CheckState.Checked:
                playvalid = True
                break
        form.toolButtonPlay.setEnabled(playvalid)

    def SimPlay(self):
        """Activate the simulation"""
        self.SetupSimulation()
        if self.cutSwap is not None:
            self.cutSwap.stop()
        self.millSim.ResetSimulation(FreeCADGui.getDocument(self.job.Document))
        # Observes the run and lands the cut mesh on the Stock when
        # it stops (docs/CAMSimRenderPort.md sec 11.7)
        self.cutSwap = _CutMeshSwap(self.millSim, self.job, self.quality)
        for op in self.activeOps:
            tool = PathDressup.toolController(op).Tool
            toolNumber = PathDressup.toolController(op).ToolNumber
            toolProfile = self.GetToolProfile(tool, 0.5)
            self.millSim.AddTool(toolProfile, toolNumber, tool.Diameter, 1)
            self.cutSwap.addTool(toolNumber, tool.Shape)
            opCommands = PathUtils.getPathWithPlacement(op).Commands
            for cmd in opCommands:
                self.millSim.AddCommand(cmd)
        self.millSim.BeginSimulation(self.stock, self.quality)
        if self.baseShape is not None:
            self.millSim.SetBaseShape(self.baseShape, 1)

    def cancel(self):
        """Cancel the simulation"""
        if self.cutSwap is not None:
            self.cutSwap.stop()
            self.cutSwap = None


class CommandCAMSimulate:
    """FreeCAD invoke simulation task panel command"""

    def GetResources(self):
        """Command info"""
        return {
            "Pixmap": "CAM_SimulatorGL",
            "MenuText": QtCore.QT_TRANSLATE_NOOP("CAM_Simulator", "CAM Simulator"),
            "Accel": "P, N",
            "ToolTip": QtCore.QT_TRANSLATE_NOOP("CAM_Simulator", "Simulates G-code on stock"),
        }

    def IsActive(self):
        """Command is active if at least one CAM job exists"""
        if FreeCAD.ActiveDocument is not None:
            for o in FreeCAD.ActiveDocument.Objects:
                if o.Name[:3] == "Job":
                    return True
        return False

    def Activated(self):
        """Activate the simulation"""
        CamSimulation = CAMSimulation()
        CamSimulation.Activate()


if FreeCAD.GuiUp:
    # register the FreeCAD command
    FreeCADGui.addCommand("CAM_SimulatorGL", CommandCAMSimulate())
    FreeCAD.Console.PrintLog("Loading PathSimulator Gui… done\n")
