# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for parameters in Preferences/Mod/TechDraw
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString

NameSpace = 'TechDraw'
ClassName = 'TechDrawParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/TechDraw'
ClassDoc = 'Convenient class to obtain the settings of TechDraw'

# TechDraw reads its settings through the hand-written accessors of
# App/Preferences.cpp and Gui/PreferencesGui.cpp, and at a good many places
# straight from the groups, each with a default of its own. The defaults
# are here; the accessors and the direct reads take them from this class.
#
# First the sub-group General. Where its readers and its preference page
# disagreed, the default here is the READER's -- what the program does
# while the key is not stored -- and the page was changed to show it:
# NewFaceFinder, VertexScale, TemplateDotSize.
#
# Not listed: DefaultPageScale and CoarseView, which are read and never
# written (the pages store DefaultScale and HLR/UsePolygon); ReportProgress,
# which nothing reads; SectionLiveUpdate and SectionUpdateDelay, which the
# section task keeps in a group of another name.
Params = [
    ParamBool('AllowPageOverride', True, subpath='General',
        title = "Allow Page Override",
        doc = "When updating of drawings is switched off globally, let a page\n"
              "whose Keep Updated property is on update anyway. Read each time a\n"
              "page decides whether to update."),
    ParamBool('AutoDist', True, subpath='General',
        title = "Auto Dist",
        doc = "New projection groups position their secondary views\n"
              "automatically. Applies to groups created afterwards; each group\n"
              "has its own AutoDistribute property."),
    ParamFloat('DefaultScale', 1.0, subpath='General',
        title = "Default Scale",
        doc = "Scale of a new drawing page. Applies to pages created afterwards."),
    ParamInt('DefaultScaleType', 0, subpath='General',
        title = "Default Scale Type",
        doc = "Scale type of a new view: 0 Page (follow the page scale), 1\n"
              "Automatic (fit the page), 2 Custom. Applies to views created\n"
              "afterwards."),
    ParamFloat('DefaultViewScale', 1.0, subpath='General',
        title = "Default View Scale",
        doc = "Scale of a new view whose scale type is Custom. Applies to views\n"
              "created afterwards."),
    ParamBool('DrawFaceEdges', False, subpath='General',
        title = "Draw Face Edges",
        doc = "Draw the outline of each detected face in addition to its fill.\n"
              "Takes effect when a view is redrawn."),
    ParamFloat('EdgeFuzz', 10.0, subpath='General',
        title = "Edge Fuzz",
        doc = "Size of the area around an edge in which a click selects it.\n"
              "Larger makes edges easier to pick but may catch a neighbouring\n"
              "edge. the unit."),
    ParamBool('FixColorAlphaOnLoad', True, subpath='General',
        title = "Fix Color Alpha On Load",
        doc = "When opening a document, read colours stored with zero opacity as\n"
              "opaque; older files saved an unused transparency of 0. Applies to\n"
              "documents opened afterwards."),
    ParamFloat('FocusDistance', 100.0, subpath='General',
        title = "Focus Distance",
        doc = "Focus distance of a new perspective view. Applies to views created\n"
              "afterwards."),
    ParamBool('GlobalUpdateDrawings', True, subpath='General',
        title = "Global Update Drawings",
        doc = "Keep all drawing pages in step with the 3D model. Off stops pages\n"
              "updating unless page override is allowed. Read each time a page\n"
              "decides whether to update."),
    ParamBool('HandleFaces', True, subpath='General',
        title = "Handle Faces",
        doc = "Find the faces of each view, needed for hatching and face colours.\n"
              "Off is faster. Takes effect when a view is recomputed."),
    ParamInt('KbPan', 1, subpath='General',
        title = "Kb Pan",
        doc = "Direction of horizontal movement of a drawing page with the arrow\n"
              "keys: 1 normal, -1 reversed (used as a multiplier). Read when a\n"
              "page view is opened."),
    ParamInt('KbScroll', 1, subpath='General',
        title = "Kb Scroll",
        doc = "Direction of vertical movement of a drawing page with the arrow\n"
              "keys: 1 normal, -1 reversed. Read when a page view is opened."),
    ParamBool('KeepPagesUpToDate', True, subpath='General',
        title = "Keep Pages Up To Date",
        doc = "New pages keep themselves in step with the model. Applies to pages\n"
              "created afterwards; each page has its own Keep Updated property."),
    ParamFloat('MarkFuzz', 5.0, subpath='General',
        title = "Mark Fuzz",
        doc = "Size of the selection area around a centre mark. Larger makes\n"
              "marks easier to pick."),
    ParamBool('NewFaceFinder', False, subpath='General',
        title = "New Face Finder",
        doc = "Use the newer algorithm to find the faces of a view. Takes effect\n"
              "when a view is recomputed."),
    ParamBool('PageRendererVg', False, subpath='General',
        title = "Page Renderer Vg",
        doc = "Draw drawing pages with the rendering backend instead of the Qt\n"
              "scene items. Applies at once to open pages."),
    ParamBool('PageRendererVgComposite', True, subpath='General',
        title = "Page Renderer Vg Composite",
        doc = "With the backend page renderer, compose its picture on the\n"
              "graphics card instead of reading it back as an image. Applies at\n"
              "once."),
    ParamBool('PageRendererVgVerify', False, subpath='General',
        title = "Page Renderer Vg Verify",
        doc = "Also paint the Qt scene items over the backend's picture of the\n"
              "page, so that differences between the two show. Applies at once."),
    ParamInt('ProjectionAngle', 0, subpath='General',
        title = "Projection Angle",
        doc = "Projection convention of new pages and projection groups: 0 first\n"
              "angle, 1 third angle. The third choice on the page, Page,\n"
              "currently reads as first angle."),
    ParamBool('restoreCosmetic', True, subpath='General',
        title = "Restore Cosmetic",
        doc = "Read cosmetic vertices, edges and centre lines when a document is\n"
              "opened. Off skips them, to open a file whose cosmetic data is\n"
              "damaged."),
    ParamInt('ScrubCount', 0, subpath='General',
        title = "Scrub Count",
        doc = "Number of extra clean-up passes over the edges produced by hidden\n"
              "line removal in a new view. Applies to views created afterwards."),
    ParamBool('SectionFuseFirst', False, subpath='General',
        title = "Section Fuse First",
        doc = "New section views fuse the source shapes into one before cutting.\n"
              "Slower, but needed for some overlapping shapes. Applies to\n"
              "sections created afterwards."),
    ParamBool('SectionUsePreviousCut', False, subpath='General',
        title = "Section Use Previous Cut",
        doc = "A new section whose base view is itself a section cuts the already\n"
              "cut shape instead of the original. Applies to sections created\n"
              "afterwards."),
    ParamBool('ShadedUnderlayBackend', True, subpath='General',
        title = "Shaded Underlay Backend",
        doc = "Render the shaded picture under a view with the rendering backend;\n"
              "off falls back to capturing it from the 3D view."),
    ParamInt('ShadedUnderlayMaxFaces', 5000, subpath='General',
        title = "Shaded Underlay Max Faces",
        doc = "Largest number of faces of a shape for which the shaded underlay\n"
              "works out a colour per face; above it the shape gets one colour."),
    ParamBool('ShowDetailHighlight', True, subpath='General',
        title = "Show Detail Highlight",
        doc = "New detail views show the highlight outline in their source view.\n"
              "Applies to detail views created afterwards."),
    ParamBool('ShowDetailMatting', True, subpath='General',
        title = "Show Detail Matting",
        doc = "New detail views show the matting, the frame around the detail.\n"
              "Applies to detail views created afterwards."),
    ParamBool('ShowSectionEdges', True, subpath='General',
        title = "Show Section Edges",
        doc = "Draw the edges of the cut surface in section views. Takes effect\n"
              "when a section is redrawn."),
    ParamBool('StoreProjectedGeometry', True, subpath='General',
        title = "Store Projected Geometry",
        doc = "Keep each view's projected geometry in the document so that\n"
              "reopening it does not project again. Off makes pages rebuild when\n"
              "opened and keeps files smaller."),
    ParamFloat('TemplateDotSize', 5.0, subpath='General',
        title = "Template Dot Size",
        doc = "Size in mm of the click boxes that mark the editable texts of a\n"
              "page template. Takes effect when a template is loaded again."),
    ParamFloat('VertexScale', 3.0, subpath='General',
        title = "Vertex Scale",
        doc = "Size of vertex dots as a multiple of the visible line width.\n"
              "Applies to views and cosmetic vertices created afterwards."),
    ParamFloat('gridSpacing', 10.0, subpath='General',
        title = "Grid Spacing",
        doc = "Spacing in mm of the grid on a new drawing page. Applies to pages\n"
              "created afterwards; each page has its own Grid Spacing property."),
    ParamBool('multiSelection', False, subpath='General',
        title = "Multi Selection",
        doc = "Clicking adds to the selection without holding Ctrl. Applies at\n"
              "once."),
    ParamBool('showGrid', False, subpath='General',
        title = "Show Grid",
        doc = "New drawing pages show a grid. Applies to pages created\n"
              "afterwards; each page has its own Show Grid property."),

    # ------------------------------------------------------------------
    # The other sub-groups, as far as their settings have a default that is
    # a plain value. Where a reader and its page disagreed the default is
    # the reader's: ShowCenterMarks (off; the page showed it on) and
    # TolSizeAdjust (0.5; the page showed 0.8).
    #
    # Not listed yet: the colours; the line widths, styles and standards;
    # the file names of the Files group and PAT/FilePattern, whose defaults
    # are built from where the program is installed; Dimensions/ArrowSize,
    # FontSize, StandardAndStyle and DiameterSymbol, whose defaults are
    # constants of TechDraw's own or a character outside ASCII. HLR/HardViz
    # and HLR/UsePolygon are stored by the page and read by nothing.

    # --- Decorations
    ParamInt('BalloonArrow', 0, subpath='Decorations',
        title = "Balloon Arrow",
        doc = "Arrowhead at the end of a new balloon's leader line, as an index\n"
              "into the list of arrow styles. Applies to balloons created\n"
              "afterwards."),
    ParamInt('BalloonShape', 0, subpath='Decorations',
        title = "Balloon Shape",
        doc = "Shape of a new balloon: 0 circular, 1 none, 2 triangle, 3\n"
              "inspection, 4 hexagon, 5 square, 6 rectangle, 7 line. Applies to\n"
              "balloons created afterwards."),
    ParamFloat('CenterMarkScale', 0.5, subpath='Decorations',
        title = "Center Mark Scale",
        doc = "Size of the centre marks of arcs and circles in a new view, as a\n"
              "factor. Applies to views created afterwards."),
    ParamFloat('CosmoCLExtend', 3.0, subpath='Decorations',
        title = "Cosmo CLExtend",
        doc = "Distance in mm by which a new cosmetic centre line extends beyond\n"
              "the geometry it is drawn on."),
    ParamInt('CutSurfaceDisplay', 2, subpath='Decorations',
        title = "Cut Surface Display",
        doc = "How a new section shows its cut surface: 0 hidden, 1 solid colour,\n"
              "2 SVG hatch, 3 PAT hatch. Applies to sections created afterwards."),
    ParamInt('MattingStyle', 0, subpath='Decorations',
        title = "Matting Style",
        doc = "Outline of detail views and of their highlight in the source view:\n"
              "0 circle, 1 square. Takes effect when detail views are recomputed."),
    ParamInt('MaxSVGTile', 10000, subpath='Decorations',
        title = "Max SVGTile",
        doc = "Largest number of SVG tiles used to hatch one face. 1 to 1000000.\n"
              "A limit that keeps a very fine hatch from freezing the program."),
    ParamBool('PrintCenterMarks', False, subpath='Decorations',
        title = "Print Center Marks",
        doc = "Include centre marks when a page is printed or exported. Takes\n"
              "effect at the next print or redraw."),
    ParamBool('PyramidOrtho', True, subpath='Decorations',
        title = "Pyramid Ortho",
        doc = "Keep a filled-triangle balloon end symbol upright instead of\n"
              "turning it with the leader line. Takes effect when balloons are\n"
              "redrawn."),
    ParamBool('SectionLineMarks', True, subpath='Decorations',
        title = "Section Line Marks",
        doc = "New views show marks where the section line of a complex section\n"
              "changes direction. Applies to views created afterwards."),
    ParamBool('ShowCenterMarks', False, subpath='Decorations',
        title = "Show Center Marks",
        doc = "New views show centre marks on arcs and circles. Applies to views\n"
              "created afterwards."),
    ParamFloat('SvgOverlapFactor', 1.25, subpath='Decorations',
        title = "Svg Overlap Factor",
        doc = "How far the tiled SVG hatch reaches beyond the face it fills, as a\n"
              "factor of the face size. Raise it if a hatch leaves gaps at the\n"
              "edge of a face."),
    ParamFloat('SymbolFactor', 1.25, subpath='Decorations',
        title = "Symbol Factor",
        doc = "Size factor for welding symbols. Takes effect when welding symbols\n"
              "are redrawn."),

    # --- Dimensions
    ParamInt('AltDecimals', 2, subpath='Dimensions',
        title = "Alt Decimals",
        doc = "Number of decimals in dimension values when Use Global Decimals is\n"
              "off. Takes effect when dimensions are recomputed."),
    ParamInt('ArrowStyle', 0, subpath='Dimensions',
        title = "Arrow Style",
        doc = "Arrowhead style for dimensions, as an index into the list of arrow\n"
              "styles. Takes effect when dimensions are redrawn."),
    ParamBool('AutoCorrectRefs', True, subpath='Dimensions',
        title = "Auto Correct Refs",
        doc = "When the geometry a dimension refers to has changed, try to find\n"
              "the matching geometry again. Read each time a dimension is\n"
              "recomputed."),
    ParamFloat('BalloonKink', 5.0, subpath='Dimensions',
        title = "Balloon Kink",
        doc = "Length in mm of the short segment between a new balloon and the\n"
              "bend of its leader line. Applies to balloons created afterwards."),
    ParamFloat('GapASME', 0.0, subpath='Dimensions',
        title = "Gap ASME",
        doc = "Gap between the measured point and the start of the extension line\n"
              "for ASME dimensions, as a factor. Applies to dimensions created\n"
              "afterwards."),
    ParamFloat('GapISO', 0.0, subpath='Dimensions',
        title = "Gap ISO",
        doc = "Gap between the measured point and the start of the extension line\n"
              "for ISO dimensions, as a factor. Applies to dimensions created\n"
              "afterwards."),
    ParamBool('ShowUnits', False, subpath='Dimensions',
        title = "Show Units",
        doc = "Append the unit to dimension values. Takes effect when dimensions\n"
              "are recomputed."),
    ParamFloat('SymbolSize', 64.0, subpath='Dimensions',
        title = "Symbol Size",
        doc = "Nominal size of the welding symbol pictures; the supplied symbols\n"
              "are drawn at 64. Change only for a symbol set drawn at another\n"
              "size."),
    ParamFloat('TileTextAdjust', 0.75, subpath='Dimensions',
        title = "Tile Text Adjust",
        doc = "Text size of a new welding symbol relative to the dimension font\n"
              "size. Applies to welding symbols created afterwards."),
    ParamFloat('TolSizeAdjust', 0.5, subpath='Dimensions',
        title = "Tol Size Adjust",
        doc = "Size of tolerance text relative to the dimension text. Takes\n"
              "effect when dimensions are redrawn."),
    ParamBool('UseGlobalDecimals', True, subpath='Dimensions',
        title = "Use Global Decimals",
        doc = "Show dimension values with the number of decimals set for the\n"
              "whole program. Off uses the alternate decimals instead."),
    ParamString('formatSpec', '%.2w', subpath='Dimensions',
        title = "Format Spec",
        doc = "Format of dimension values when global decimals are not used, in\n"
              "printf style, for example %.2f; with w in place of f trailing\n"
              "zeros are dropped."),

    # --- HLR
    ParamBool('HardHid', False, subpath='HLR',
        title = "Hard Hid",
        doc = "New views show hidden hard edges. Applies to views created\n"
              "afterwards; each view has its own property."),
    ParamBool('IsoHid', False, subpath='HLR',
        title = "Iso Hid",
        doc = "New views show hidden iso-parameter lines. Applies to views\n"
              "created afterwards."),
    ParamBool('IsoViz', False, subpath='HLR',
        title = "Iso Viz",
        doc = "New views show visible iso-parameter lines. Applies to views\n"
              "created afterwards."),
    ParamBool('SeamHid', False, subpath='HLR',
        title = "Seam Hid",
        doc = "New views show hidden seam lines. Applies to views created\n"
              "afterwards."),
    ParamBool('SeamViz', False, subpath='HLR',
        title = "Seam Viz",
        doc = "New views show visible seam lines. Applies to views created\n"
              "afterwards."),
    ParamBool('SmoothHid', False, subpath='HLR',
        title = "Smooth Hid",
        doc = "New views show hidden smooth edges, where faces meet tangentially.\n"
              "Applies to views created afterwards."),
    ParamBool('SmoothViz', True, subpath='HLR',
        title = "Smooth Viz",
        doc = "New views show visible smooth edges, where faces meet\n"
              "tangentially. Applies to views created afterwards."),

    # --- PAT
    ParamFloat('GeomWeight', 0.1, subpath='PAT',
        title = "Geom Weight",
        doc = "Line width of the PAT hatch on the cut surface of a new section.\n"
              "Applies to sections created afterwards."),
    ParamInt('MaxSeg', 10000, subpath='PAT',
        title = "Max Seg",
        doc = "Largest number of line segments used to draw the PAT hatch of one\n"
              "face. 1 to 1000000. A limit that keeps a very fine hatch from\n"
              "freezing the program."),
    ParamString('NamePattern', 'Diamond', subpath='PAT',
        title = "Name Pattern",
        doc = "Name of the pattern, within the PAT file, used for new geometric\n"
              "hatches. Applies to hatches created afterwards."),

    # --- Colors
    ParamBool('ClearFace', False, subpath='Colors',
        title = "Clear Face",
        doc = "Faces of new views are transparent instead of filled with the face\n"
              "colour. Applies to views created afterwards."),
    ParamBool('LightOnDark', False, subpath='Colors',
        title = "Light On Dark",
        doc = "Draw pages in light colours for a dark page background. Printing\n"
              "and export always use the normal colours. Takes effect when a page\n"
              "is redrawn."),
    ParamBool('Monochrome', False, subpath='Colors',
        title = "Monochrome",
        doc = "With Light on dark, draw everything in the single light text\n"
              "colour instead of lightened colours. Takes effect when a page is\n"
              "redrawn."),

    # --- Labels
    ParamString('LabelFont', 'osifont', subpath='Labels',
        title = "Label font",
        doc = "Font of view labels, and the font new dimensions, balloons and\n"
              "annotations start with."),
    ParamFloat('LabelSize', 5.0, subpath='Labels',
        title = "Label size",
        doc = "Text size of view labels in mm, and the size new annotations start\n"
              "with."),

    # --- LeaderLine
    ParamBool('AutoHorizontal', True, subpath='LeaderLine',
        title = "Leader line auto horizontal",
        doc = "New leader lines end in a horizontal segment."),

    # --- Rez
    ParamFloat('Resolution', 10.0, subpath='Rez',
        title = "Scene resolution",
        doc = "Scene units per millimetre of a drawing page. Read once when the\n"
              "TechDraw user interface is loaded."),

    # --- Tracker
    ParamFloat('TrackerWeight', 4.0, subpath='Tracker',
        title = "Tracker line width",
        doc = "Line width of the rubber band lines drawn while a tool tracks the\n"
              "mouse on a page."),

    # --- debug
    ParamBool('allowCrazyEdge', False, subpath='debug',
        title = "Allow crazy edges",
        doc = "Keep edges of unreasonable length that projection sometimes\n"
              "produces instead of dropping them. For developers."),
    ParamBool('debugDetail', False, subpath='debug',
        title = "Debug detail views",
        doc = "Write the intermediate shapes of a detail view to files while it\n"
              "is recomputed. For developers."),
    ParamBool('debugSection', False, subpath='debug',
        title = "Debug section views",
        doc = "Write the intermediate shapes of a section view to files while it\n"
              "is recomputed. For developers."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
