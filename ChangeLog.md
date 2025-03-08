Stable release (branch [LinkStable](https://github.com/realthunder/FreeCAD/tree/LinkStable)) is merged with upstream aa3b2f39 2023.05.22

# Stable 2024.10.03

Fix Link copy on change hidden group out of scope problem
Support dragging of toolbar inside status/menu bar
Fix pre-selection highlight in `Pick geometry` menu
Support opening directory through command line (#1023)
Fix alias property display in property view
Fix FreeCAD freezes when clicking inside spreadsheet (#1018)
Fix crash crash on showing main window in command line (#1026)
Fix crash on null topological IndexedName (#1012)
Fix recursive selection notification (#557, #710)
Part: disable randomization of duplicate element resolving (#1029)
Part: do not report error/warning if not recomputing
Part: fix user script defined topo shape mapping (#1013)
Part: fix missing recompute on changing property `FixShape` and `ValidateShape`
Part: fix SubShapeBinder tree item tooltip
Part: improve shape refine
Part: fix regression on file loading (#1019)
Part/PartDesign: fix regression of extrude/pad until face (#989)
PartDesign: allow non-face support for sketch based feature (#970)
PartDesign: add `OffsetBaseFeature` property to pattern features (#994)
PartDesign: add `HideBaseFeature` property to pattern features (#1002)
Renderer: fix rendering of legacy shape with transparency
Renderer: fix getting bounding box for legacy shape (#1008)
Sketcher: fix constraint pre-selection highlight (#992)
Sketcher: support external reference without subname (#1028)
Sketcher: fix false selection on dragging element
Sketcher: allow copying of external geometry (#1006)
Sketcher: sort sketch names in `Map sketch to face` command (#1004)
Spreadsheet: fix combo box editing mode
Spreadsheet: fix editing through expression line editor with edit mode
Spreadsheet: support vertical configuration table (#1024)
Spreadsheet: support pasting cells transposed through context menu `Paste special... -> Paste transposed`
Spreadsheet: fix undo/redo of cut and paste of cells with alias
Spreadsheet: remember dialog position and size
OCCT: fix regression in making thrusection (Loft) ([#2](realthunder/OCCT#2))
OCCT: fix handling of concave face in makeThickSolid ([#1](realthunder/OCCT#1), [#3](realthunder/OCCT#3))

-- 20241003stable --

# Stable 2024.04.07

Fix expression renaming (#974)
Add option to sync draw style preference settings to document (Preferences -> Display -> Draw styles -> Hidden Line/Shadow -> Synchronize)
Support toolbar drag & drop on menu bar
Fix icon size setting for toolbar not in main window
Fix crash on pie menu context switch (#978)
Fix overlay widget eating up Escape key
Fix crash on variant link copy on change
Fix `App::GeoFeatureGroupExtension (App::Part)` still touched on recompute after undo
Part: fix pocket up to face (#985)

# Stable 2024.03.22

Part: fix freezing when making wire (#973)

# Stable 2024.03.17

Fix shadow light document setting (#948)
Fix customized toolbar size handling (#947)
Fix hidden line rendering
Fix Python console lost of bracket on auto completion
Show runtime attributes in Python console auto completion call tips
Fix SpinBox context menu
Part: fix meshing of infinite shape for visualization (#955)
Part: improved exception handling in pre-selection macro
Part|Surface: fix making BSpline face (#965)
PartDesign: fixed thickness editing panel input sync problem
Sketcher: fix crash on deleting external geometry (#967)
Sketcher: fix linear approximation of BSpline external edge (#958)
Sketcher: clear selection before trimming edge to avoid error message
Sketcher: suppress error message on creating arc

# Stable 2024.01.23

Fix crash on closing document (#936)
Fix Python object saving back compatibility (#933)
Fix axis origin rendering (#929)
Fix Spreadsheet foreground color in ProDark (#940)
PartDesign: expose MaxDegree setting in Loft
PartDesign: fix `Parallel transform` option in Pattern feature editing panel

# Stable 2024.01.13

Fix PythonObject saving in Python 3.11 (#927)
Fix compatibility with Mac 3Dconnexion driver  (#923)
Change default image plane selection style
Import: fix code page handling in DXF import 
Part: fix BSpline surface trimming
PartDesign: improve handling of split profile in Loft
PartDesign: expose `Linearize` option to editing task panel
Sketcher: fix circle to circle distance constraint (#928)

# Stable 2024.01.04

Fix notification background with stylesheet (#923)
Sketcher: fix crash on editing (#922)

# Stable|Tip 2023.12.31

Add mesh facet selection mode `Mesh_SubElementSelection`. Can be used together with new commands `View front face (V, N)` and `View back face (V, B)`.
Remember user closed overlay panel ([#11680](FreeCAD/FreeCAD#11680)
Fix crash on manual alignment (#906)
Fix lost key when resolving multi-key-sequence shortcut (#909)
Fix crash on `Tools -> Customization...`
Fix crash on tree view selection ([#899](https://github.com/realthunder/FreeCAD/issues/899#issuecomment-1776927054))
PartDesign: fix backward compatibility with upstream sketch based feature (#905)
Sketcher: fix projection error in OCC 7.7
Sketcher: fix crash on adding tagent (#917)
TechDraw: fix circle center vertex selection (#907)
TechDraw: fix undesired border when drawing SVG pattern (#909)

# Tip 2023.11.02

Introducing new commands `View front face (V, N)` and `View back face (V, B)` (See b6bd57a2 for more details)
TechDraw: fix projection group recompute problem (#901)

# Tip 2023.10.28

Fix view property editing problem (#896)

# Tip 2023.10.24

Fix git installation in AppImage (affects AddonManager) (#895)
Fix mouse wheel pass through on Windows
Part: fix handling of compound shape in loft (#478)

# Tip 2023.10.18

Add 3D view property 'ThumbnailView' to mark which view is preferred for capturing document thumbnail
Fix undesired behavior when deleting some child object (#877)
Fix crash on right click of view property editor (#829)
Fix pre-selection handling in `Pick geometry` command
Fix thumbnail restore and saving with logo
Support various linked file path resolve mode in App::Link
Part|PartDesign: disable `ValidateShape` by default to avoid slow down on complex shape
PartDesign: fix sketch based profile manual edit
PartDesign: do not auto set UseAllEdges when creating fillet/chamfer
PartDesign: fix UseAllEdges handling in fillet editing task (#883)
Renderer: fix handling of shadow style
Renderer: fix multiple sub-element preselection highlight
Renderer: fix back face picking for objects shown on top
TechDraw: fix restore of dimension when opening document (#812, #845, #868)
TechDraw: fix cosmetic edge deletion (#885)
TechDraw: fix lost of view section when moved outside of the shape bound
TechDraw: refactor background computation
TechDraw: improve selection highlight of various objects
TechDraw: add move by mouse for simple section, detail view, and weld symbol

Second try to fix Python 3.11 not using user site package issue (#864)
Path: fix legacy Controller object backward compatibility issue

# Tip 2023.08.11

Second try to fix Python 3.11 not using user site package issue (#864)
Path: fix legacy Controller object backward compatibility issue

# Tip 2023.08.10

Support user defined document thumbnail through document property ThumbnailFile (cac98c12)
Fix navigation cube corner setting (#855)
Enable user site package for Python 3.11 (#864)
Sketcher: fix editing through App::Link

# Tip 2023.08.06

Fix unit system ordering problem (#859)
Fix workbench enable/disable settings (#852)
Fix navigation cube corner setting (#855)
Fix stylesheet for checkbox with icon in menu
Fix object bounding box restore
Sketcher: support undo constraint dragging
Sketcher: fix missing settings init (#854)
Sketcher: fix external circular edge precision problem (#836, #827)
Renderer: add support for some legacy Coin3D shape nodes (#856)

# Tip 2023.07.30

Fix libfmt packaging problem
PartDesign: fix auto recompute when editing pattern features (#839)

# Tip 2023.07.29

Merged with upstream aa3b2f39 2023.05.22
Fix handling of vendor prefixed (Link branch) user configuration (#824, #833)
Export NaviCube corner size setting through context menu
Allow branding `IssuePage` (#837)
Add new `SplitDark` overlay stylesheet
Improve scene inspector (9d58c46b5)
Implement late 3D picking, used by origin plane
Improve handling of corrupted/incompatible string table in saved file
Use checkbox for checkable menu item
Expose preference package settings in Preference -> General -> Preference pack
Sketcher: fix polyline command toggle (#349)
Sketcher: fix deletion of multiple external edges

# Tip 2023.05.21

Gui: allow child object export override in `Part` container
Gui: allow cancel importer selection
AddonManager: fix macro installer (#822)
Measurement|Part: check for infinite shape (#817)
PartDesign: fix pocket profile normal direction (#816)

# Tip 2023.05.16

Gui: fix API compatibility (#815)
Sketcher: add missing view sketch toolbar button
Sketcher: fix clip plane setup (#810)
Sketcher: fix delete of external element (#814)

# Tip 2023.05.10

Sketcher: fix element selection through task panel (#809)
PartDesign: fix Pad/Pocket creation (#808)
PartDesign: fix Pad/Pocket auto inner taper editing


