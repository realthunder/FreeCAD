# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from typing import Any

from Base.BaseClass import BaseClass
from Base.Metadata import export, no_args

from Gui import Document
from Part.App.TopoShape import TopoShape
from CAM.App.Command import Command

@export(
    Include="Mod/CAM/PathSimulator/AppGL/CAMSim.h",
    FatherInclude="Base/BaseClassPy.h",
    Namespace="CAMSimulator",
    Constructor=True,
    Delete=True,
)
class CAMSim(BaseClass):
    """
    FreeCAD python wrapper of CAMSimulator

          CAMSimulator.CAMSim():

          Create a path simulator object

    Author: Shai Seger (shaise_at_g-mail)
    License: LGPL-2.1-or-later
    """

    def BeginSimulation(self, stock: TopoShape, resolution: float) -> None:
        """
        Start a simulation process on a box shape stock with given resolution
        """
        ...

    def ResetSimulation(self, document: Document, /) -> None:
        """
        Clear the simulation and all gcode commands
        """
        ...

    def AddTool(self, shape: TopoShape, toolnumber: int, diameter: float, resolution: float) -> Any:
        """
        Set the shape of the tool to be used for simulation
        """
        ...

    def SetBaseShape(self, shape: TopoShape, resolution: float) -> None:
        """
        Set the shape of the base object of the job
        """
        ...

    def AddCommand(self, command: Command, /) -> Any:
        """
        Add a path command to the simulation.
        """
        ...

    def GetProgress(self) -> tuple:
        """
        Where the running simulation stands: (playing, motionIndex, fraction).
        motionIndex indexes the parsed motion list, -1 before the first cut;
        fraction is the consumed part of that motion, in (0, 1].
        """
        ...

    def GetLineTable(self) -> list:
        """
        Motions parsed per added command line: entry i is the total motion
        count after line i (tool-change lines included), mapping a motion
        index back to the command it came from.
        """
        ...

    def GetMotion(self, index: int, /) -> Any:
        """
        The parsed motion at index as a dict (type, tool, x, y, z, i, j, k,
        r, retractZ), or None when out of range.
        """
        ...

    def AttachDocumentView(self) -> bool:
        """
        Fan the running simulator's drawing out to the document's own 3D
        view as an additional host. False when there is no simulator
        window, no document 3D view, or no renderer to borrow; never
        creates the simulator window.
        """
        ...

    def DetachDocumentView(self) -> None:
        """
        Undo AttachDocumentView for the document's 3D view.
        """
        ...
