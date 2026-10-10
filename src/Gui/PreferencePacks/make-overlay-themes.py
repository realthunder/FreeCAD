#!/usr/bin/env python3
"""Write the two overlay themes from the themes they are made of.

"Overlay dark theme" is the Dark theme and "Overlay light theme" the Light
theme, each with the dock windows in overlay mode: the tree and the property
view apart, on the left over the 3D view, the console, the selection view
and the report view folded away at the bottom and the top. Until 2026-10
that layout came as two presets (data/settings/OverlayDark.FCParam and
OverlayLight.FCParam), which also wrote colours and style sheets of their
own, over whatever theme was there: a theme and a preset each owned half of
the look, and switching among them left the halves of two different ones
standing -- light text on the console's light background was one such.

The preset's overlay part went two ways ("color shall follow the Dark/Light
theme. port those overlay part", 2026-10-10):
  - the LAYOUT is what this script adds to a base theme: which panel is in
    which overlay, the tree and the property view apart, the tree's hidden
    column, two overlay switches;
  - the overlay's LOOK is in Dark.cfg and Light.cfg themselves, since a
    panel can be put into an overlay under any theme and has to be read
    there: the overlay style sheet made for see-through panels (the outline
    one, which the presets named), the backing behind a tree item, and the
    console's background -- the preset's light grey went with no theme, and
    it is the page the theme draws an editor on now ("need to modify python
    editor part to make it more suitable to the theme").
So a theme of this kind is its base theme, every key of it with the base's
value, and the layout; a switch between the two changes where the panels are
and nothing else. Run this after editing Dark.cfg or Light.cfg:

    python3 src/Gui/PreferencePacks/make-overlay-themes.py

tests/gui/theme-overlay-themes.py fails when the files it writes are stale.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# BaseApp/MainWindow/DockWindows: which panel is in which overlay, and how
# each side behaves. Beside Preferences, not under it.
PANELS = """\
      <FCParamGroup Name="MainWindow">
        <FCParamGroup Name="DockWindows">
          <FCParamGroup Name="OverlayLeft">
            <FCText Name="Widgets">Tree view,Property view,</FCText>
            <FCText Name="Sizes">220,220</FCText>
            <FCBool Name="Transparent" Value="1"/>
            <FCInt Name="Width" Value="300"/>
            <FCInt Name="Height" Value="502"/>
          </FCParamGroup>
          <FCParamGroup Name="OverlayRight">
            <FCText Name="Widgets">Combo View,</FCText>
            <FCBool Name="Transparent" Value="1"/>
            <FCInt Name="Width" Value="300"/>
            <FCInt Name="Height" Value="360"/>
          </FCParamGroup>
          <FCParamGroup Name="OverlayBottom">
            <FCText Name="Widgets">Python console,Selection view,</FCText>
            <FCText Name="Sizes">632,631</FCText>
            <FCBool Name="AutoHide" Value="1"/>
            <FCBool Name="EditShow" Value="0"/>
            <FCBool Name="EditHide" Value="0"/>
            <FCBool Name="TaskShow" Value="0"/>
            <FCInt Name="Width" Value="1305"/>
            <FCInt Name="Height" Value="180"/>
          </FCParamGroup>
          <FCParamGroup Name="OverlayTop">
            <FCText Name="Widgets">Report view,</FCText>
            <FCText Name="Sizes">1263</FCText>
            <FCBool Name="Transparent" Value="1"/>
            <FCBool Name="AutoHide" Value="1"/>
            <FCBool Name="EditShow" Value="0"/>
            <FCBool Name="EditHide" Value="0"/>
            <FCBool Name="TaskShow" Value="0"/>
            <FCInt Name="Width" Value="803"/>
            <FCInt Name="Height" Value="150"/>
          </FCParamGroup>
          <FCBool Name="Std_PythonView" Value="1"/>
          <FCBool Name="Std_ReportView" Value="0"/>
          <FCBool Name="Std_SelectionView" Value="1"/>
        </FCParamGroup>
      </FCParamGroup>
"""

# Preferences/DockWindows: the tree and the property view as panels of their
# own instead of the combo view that holds both.
DOCKS = """\
        <FCParamGroup Name="DockWindows">
          <FCParamGroup Name="ComboView">
            <FCBool Name="Enabled" Value="0"/>
          </FCParamGroup>
          <FCParamGroup Name="TreeView">
            <FCBool Name="Enabled" Value="1"/>
          </FCParamGroup>
          <FCParamGroup Name="PropertyView">
            <FCBool Name="Enabled" Value="1"/>
          </FCParamGroup>
        </FCParamGroup>
"""

# One line each, added to a group the base theme has already.
INTO = (
    ('        <FCParamGroup Name="TreeView">\n',
     '          <FCBool Name="HideColumn" Value="1"/>\n'),
    ('        <FCParamGroup Name="View">\n',
     '          <FCBool Name="DockOverlayCheckNaviCube" Value="1"/>\n'
     '          <FCBool Name="DockOverlayActivateOnHover" Value="0"/>\n'),
)

HEAD = """\
  <!-- Written by make-overlay-themes.py from %s.cfg: do not edit, edit that
       file or the script and run the script. -->
"""


def once(text, anchor, new, before):
    if text.count(anchor) != 1:
        raise SystemExit("%r is in the base theme %d times, not once" % (anchor, text.count(anchor)))
    return text.replace(anchor, new + anchor if before else anchor + new)


def make(base, name):
    with open(os.path.join(HERE, base, base + ".cfg"), encoding="utf-8", newline="") as f:
        text = f.read()
    # the single lines first: the groups added after them hold groups of the
    # same names, further in
    for anchor, lines in INTO:
        text = once(text, anchor, lines, False)
    # The style sheet's parameters are found by the theme's name,
    # parameters/<theme>.yaml, and these two have their base theme's.
    empty = '<FCText Name="ThemeStyleParametersFile"></FCText>\n'
    named = '<FCText Name="ThemeStyleParametersFile">qss:parameters/%s.yaml</FCText>\n' % base
    text = once(text, empty, named, True).replace(named + empty, named)
    text = once(text, '  <FCParamGroup Name="Root">\n', HEAD % base, True)
    text = once(text, '      <FCParamGroup Name="Preferences">\n', PANELS, True)
    text = once(text, '\n        <FCParamGroup Name="MainWindow">\n', "\n" + DOCKS.rstrip("\n"), True)
    os.makedirs(os.path.join(HERE, name), exist_ok=True)
    with open(os.path.join(HERE, name, name + ".cfg"), "w", encoding="utf-8", newline="") as f:
        f.write(text)


make("Dark", "Overlay dark theme")
make("Light", "Overlay light theme")
