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
'''Auto code generator for parameters in Preferences/Mod/Material
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString

NameSpace = 'Materials'
ClassName = 'MaterialParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Material'
ClassDoc = 'Convenient class to obtain the settings of the Material module'

# The lists of recent and favourite materials, the sizes of the editor and
# the folders each workbench brings are kept beside these and are not
# settings. Editor/ShowLegacy is the reader's, off; the page showed it on
# and stored that at OK.
Params = [
    ParamString('DefaultMaterial', '7f9fd73b-50c9-41d8-b7b2-575a030c1eeb',
        title = "Default material",
        doc = "Material used wherever the default material is asked for, such as\n"
              "for objects that have none of their own. Takes effect at the next\n"
              "use."),
    ParamBool('EditorShowFavorites', True, subpath='Editor', param_name='ShowFavorites',
        title = "Show favourites (editor)",
        doc = "Shows the Favourites group in the tree of the materials editor.\n"
              "Takes effect the next time the editor is opened."),
    ParamBool('EditorShowRecent', True, subpath='Editor', param_name='ShowRecent',
        title = "Show recent (editor)",
        doc = "Shows the group of recently used materials in the tree of the\n"
              "materials editor. Takes effect the next time the editor is opened."),
    ParamBool('EditorShowEmptyFolders', False, subpath='Editor', param_name='ShowEmptyFolders',
        title = "Show empty folders (editor)",
        doc = "Shows folders that hold no material in the tree of the materials\n"
              "editor. Takes effect the next time the editor is opened."),
    ParamBool('EditorShowEmptyLibraries', True, subpath='Editor', param_name='ShowEmptyLibraries',
        title = "Show empty libraries (editor)",
        doc = "Shows libraries that hold no material in the tree of the materials\n"
              "editor. Takes effect the next time the editor is opened."),
    ParamBool('EditorShowLegacy', False, subpath='Editor', param_name='ShowLegacy',
        title = "Show legacy files (editor)",
        doc = "Shows material cards that are still in the old file format in the\n"
              "tree of the materials editor. Takes effect the next time the\n"
              "editor is opened."),
    ParamBool('SelectorShowFavorites', True, subpath='TreeWidget', param_name='ShowFavorites',
        title = "Show favourites (selector)",
        doc = "Shows the Favourites group in the list of the material selector.\n"
              "Takes effect the next time a material selector is created."),
    ParamBool('SelectorShowRecent', True, subpath='TreeWidget', param_name='ShowRecent',
        title = "Show recent (selector)",
        doc = "Shows the group of recently used materials in the list of the\n"
              "material selector. Takes effect the next time a material selector\n"
              "is created."),
    ParamBool('SelectorShowEmptyFolders', False, subpath='TreeWidget', param_name='ShowEmptyFolders',
        title = "Show empty folders (selector)",
        doc = "Shows folders that hold no material in the list of the material\n"
              "selector. Takes effect the next time a material selector is\n"
              "created."),
    ParamBool('SelectorShowEmptyLibraries', True, subpath='TreeWidget', param_name='ShowEmptyLibraries',
        title = "Show empty libraries (selector)",
        doc = "Shows libraries that hold no material in the list of the material\n"
              "selector. Takes effect the next time a material selector is\n"
              "created."),
    ParamBool('SelectorShowLegacy', False, subpath='TreeWidget', param_name='ShowLegacy',
        title = "Show legacy files (selector)",
        doc = "Shows material cards that are still in the old file format in the\n"
              "list of the material selector. Takes effect the next time a\n"
              "material selector is created."),
    ParamInt('SelectorIconSize', 64, subpath='TreeWidget', param_name='IconSize',
        title = "Material preview size",
        doc = "Size in pixels of the material previews in the list of the\n"
              "material selector, kept within a legible range. Takes effect the\n"
              "next time a material selector is created."),
    ParamInt('RecentMax', 5, subpath='Recent',
        title = "Recent materials kept",
        doc = "Greatest number of recently used materials that is remembered.\n"
              "Takes effect the next time the materials editor or a material\n"
              "selector is opened."),
    ParamInt('ModelsRecentMax', 5, subpath='Models/Recent', param_name='RecentMax',
        title = "Recent models kept",
        doc = "Greatest number of recently used material models that is\n"
              "remembered. Takes effect the next time the model selection\n"
              "dialogue is opened."),
    ParamBool('UseBuiltInMaterials', True, subpath='Resources',
        title = "Use built-in materials",
        doc = "Lists the material cards and models that come with FreeCAD. Takes\n"
              "effect when the material libraries are next loaded."),
    ParamBool('UseMaterialsFromWorkbenches', True, subpath='Resources',
        title = "Use workbench materials",
        doc = "Lists the material cards and models added by external workbenches.\n"
              "Takes effect when the material libraries are next loaded."),
    ParamBool('UseMaterialsFromConfigDir', True, subpath='Resources',
        title = "Use user materials",
        doc = "Lists the material cards and models found in the Material and\n"
              "Models folders of the user's FreeCAD data directory. Takes effect\n"
              "when the material libraries are next loaded."),
    ParamBool('UseMaterialsFromCustomDir', True, subpath='Resources',
        title = "Use custom directory",
        doc = "Lists the material cards and models found in the user-defined\n"
              "directory. Takes effect when the material libraries are next\n"
              "loaded."),
    ParamString('CustomMaterialsDir', '', subpath='Resources',
        title = "Custom materials directory",
        doc = "Directory with the user's own material cards. Takes effect when\n"
              "the material libraries are next loaded."),
    ParamBool('UseExternal', False, subpath='ExternalInterface',
        title = "Use external interface",
        doc = "Uses an external material interface in addition to the local\n"
              "material libraries. Takes effect at once."),
    ParamString('ExternalInterface', 'None', subpath='ExternalInterface', param_name='Current',
        title = "External interface",
        doc = "Name of the external material interface to use, or None. Takes\n"
              "effect at once: the connection is made again when the value\n"
              "changes."),
    ParamInt('ModelCacheSize', 100, subpath='ExternalInterface',
        title = "Model cache size",
        doc = "Number of material models kept in the cache of the external\n"
              "material interface. Takes effect after restart."),
    ParamInt('MaterialCacheSize', 100, subpath='ExternalInterface',
        title = "Material cache size",
        doc = "Number of materials kept in the cache of the external material\n"
              "interface. Takes effect after restart."),
    # --- what the program keeps for itself. Their readers read the group
    # as before. The four counts are the lengths of lists kept beside
    # them, a key per entry (FAV0.., MRU0..), which are not listed.
    ParamInt('EditorWidth', 835, subpath='Editor',
        title = 'Materials editor: width',
        doc = "Width in pixels the materials editor last had. Stored when the\n"
              "editor closes."),
    ParamInt('EditorHeight', 542, subpath='Editor',
        title = 'Materials editor: height',
        doc = "Height in pixels the materials editor last had. Stored when the\n"
              "editor closes."),
    ParamInt('FavoritesCount', 0, subpath='Favorites', param_name='Favorites',
        title = 'Favourite materials: count',
        doc = "How many favourite materials are kept, in the keys beside this\n"
              "one. Stored by the program with the list."),
    ParamInt('RecentCount', 0, subpath='Recent', param_name='Recent',
        title = 'Recent materials: count',
        doc = "How many recent materials are kept, in the keys beside this one.\n"
              "Stored by the program with the list."),
    ParamInt('ModelsFavoritesCount', 0, subpath='Models/Favorites', param_name='Favorites',
        title = 'Favourite material models: count',
        doc = "How many favourite material models are kept, in the keys beside\n"
              "this one. Stored by the program with the list."),
    ParamInt('ModelsRecentCount', 0, subpath='Models/Recent', param_name='Recent',
        title = 'Recent material models: count',
        doc = "How many recent material models are kept, in the keys beside this\n"
              "one. Stored by the program with the list."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
