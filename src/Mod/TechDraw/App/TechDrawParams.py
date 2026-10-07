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

from params_utils import ParamBool, ParamInt, ParamFloat

NameSpace = 'TechDraw'
ClassName = 'TechDrawParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/TechDraw'
ClassDoc = 'Convenient class to obtain the settings of TechDraw'

# TechDraw reads its settings through the hand-written accessors of
# App/Preferences.cpp and Gui/PreferencesGui.cpp, and at a good many places
# straight from the groups, each with a default of its own. The defaults
# are here; the accessors and the direct reads take them from this class.
#
# So far the sub-group General. Where its readers and its preference page
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
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
