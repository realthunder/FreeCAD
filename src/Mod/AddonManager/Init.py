# SPDX-License-Identifier: LGPL-2.1-or-later
# FreeCAD init script of the AddonManager module
# (c) 2001 Juergen Riegel
# License LGPL

import FreeCAD

FreeCAD.__unit_test__ += ["TestAddonManagerApp"]

# The Addon Manager's settings, described to the settings registry
import addonmanager_params_registry
