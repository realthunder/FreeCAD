# Theme Porting — the fork's stylesheets vs upstream's parameter model

Status: assessment. **Nothing has been ported.** Measurements below are from
`upstream/main` at `63382d52a6` (2026-08-04) against `LinkVibe`.

The fork and upstream FreeCAD now theme the GUI in two incompatible ways. This
document records what each does, what the difference costs to close, and in what
order it would be closed. It exists so the decision can be made on numbers rather
than on impressions.

## 1. The two models

### 1.1 The fork: one stylesheet per theme

A theme is a `.qss` file. `MainWindow/StyleSheet` names it, `Application::setStyleSheet`
loads it, and `Application::replaceVariablesInQss` substitutes `@Name` tokens before
handing the text to Qt. Tokens come from two places:

- `Themes/ThemeAccentColor1..3` — three colors, predating everything else.
- `Themes/Variables` — an arbitrary group of typed values, substituted by name.

We ship eight sheets (`Light`, `Dark`, `Darker`, `Light-modern`, `Dark-modern`,
`Dark-contrast`, `ProDark`, `Behave-dark`) totalling **21871 lines**, plus 33 overlay
sheets and 3 menu sheets, and nine preference packs that select among them.

Recoloring a theme means editing its sheet, or adding variables and threading them
through by hand. There is no way to express "this color is that color, darker".

### 1.2 Upstream: one stylesheet, many parameter sets

Upstream deleted the per-theme sheets in three steps:

| Commit | Date | What |
|---|---|---|
| `b205922c42` (#13772) | 2024-06-14 | Eight sheets collapsed to two. `Dark.qss` → `Light theme.qss`, `Darker.qss` → `Dark theme.qss`; `Light`, `Light-modern`, `Dark-modern`, `Dark-contrast`, `ProDark` deleted. Colors moved into `PreferencePacks/*.cfg`. |
| `3a32ae9985` (#14749) | 2024-07-15 | Those two deleted as well. |
| `9b92902c0d` | 2025-07-12 | `FreeCAD_stylesheet.qss` renamed to `FreeCAD.qss`, its present name. |

Today upstream ships **one** sheet, `FreeCAD.qss` (2722 lines), plus a 20-line
`defaults.qss`, and defines a theme as a **parameter set**: `Stylesheets/parameters/
FreeCAD Dark.yaml` and `FreeCAD Light.yaml`, 66 entries each. Entries are named
semantic roles, and their values are expressions:

```yaml
GeneralBackgroundHoverColor: "@PrimaryColorLighten4"
GeneralBorderHoverColor:     "@PrimaryColorLighten6"
CheckedButtonTopBackgroundColor: "darken(@ButtonTopBackgroundColor, 5)"
DefaultButtonTopBackgroundColor: "blend(@ButtonTopBackgroundColor, @AccentColor, 10)"
```

`src/Gui/StyleParameters/` evaluates them: a tokenizer and recursive-descent parser
(`Parser.{h,cpp}`), a value/colour type (`Value.{h,cpp}`), and a manager that layers
several *parameter sources* — built-in, theme YAML, user overrides — and exposes
`replacePlaceholders()` (`ParameterManager.{h,cpp}`). Grepping the sources turns up
`darken`, `lighten` and `blend` as builtins; that list was read off the code and may
not be exhaustive.

Adding a theme upstream is a 66-line YAML file. No sheet is touched.

## 2. How the preference packs differ

|  | fork | upstream |
|---|---|---|
| Packs | 9 — Classic, Dark, Darker, Dark modern, Dark contrast, Dark behave, Light, Light modern, ProDark | 4 — FreeCAD Dark, FreeCAD Light, FreeCAD Classic, Dark behave |
| `Dark` pack | 114 lines | 154 lines |
| `Light` pack | 80 lines | 173 lines |
| Theme selection | `StyleSheet: Dark.qss` — a **file** | `StyleSheet: FreeCAD.qss` + `Theme: FreeCAD Dark` — a file and a **named parameter set** |
| Fork-only keys | `ColorScheme`, `MenuStyleSheet`, `CustomTitleBar`, `TitleBarToolBars`, `IconSet` | — |
| Upstream-only keys | — | `QtStyle: FreeCAD` (a `QProxyStyle`), `Theme`, `ThemeStyleParametersFile` |
| `ThemeAccentColor1..3` | **were not set at all** — see §6 | set, to `#00ABFF`, `#B477FF`, `#557BB6` |

Upstream's packs also cover more ground: CAM colors and a much larger `View` group.

Note that upstream's three accents are three *unrelated hues*. Ours are not
interchangeable with them, because the shipped fork sheets give the three slots three
different jobs — see §6.

## 3. Prerequisites already met

This is the encouraging part. Porting the evaluator needs almost nothing new:

- **`yaml-cpp` and `fmt` are already in our build** (`cMake/FreeCAD_Helpers/SetupLibYaml.cmake`,
  `SetupLibFmt.cmake`). `StyleParameters` needs exactly these two.
- **`ParamHandler.h` already has the API upstream's wiring calls**, including
  `addDelayedHandler(const char* path, const std::vector<const char*>& keys, Func)`.
  The file originated in this fork and upstream adopted it; the two copies differ by
  39/29 lines in the `.cpp` and are largely formatting.
- **We are C++20**, so the `std::views` usage compiles.
- **Substitution has one call site**, `Application.cpp:3191`, inside the stylesheet
  loader. Swapping the engine is therefore contained: it cannot leak into the overlay
  or menu sheets, and in fact **none of the 33 overlay sheets uses an `@` variable**.

## 4. What is missing

| Component | Lines | Note |
|---|---|---|
| `Base/ServiceProvider.h` | 200 | absent here; the source-registration mechanism `ParameterManager` is registered through |
| `Gui/StyleParameters/` — 6 files | 2358 | `ParameterManager` 462+481, `Parser` 556+198, `Value` 377+284 |
| `Gui/FreeCADStyle.{h,cpp}` | 87 | the `QProxyStyle` that `QtStyle: FreeCAD` selects |
| `Stylesheets/FreeCAD.qss` + `defaults.qss` | ~2742 | replaces our eight sheets |
| `Stylesheets/parameters/*.yaml` | 2 files, 66 entries each | the theme definitions proper |
| `Application.cpp` wiring | ~80 | `initStyleParameterManager()`; `replaceVariablesInQss` becomes a wrapper over `replacePlaceholders()` |
| PreferencePacks — FreeCAD Dark / Light / Classic | 3 `.cfg` | |
| `Dialogs/DlgThemeEditor.{h,cpp}` | 932 | the theme editor GUI; optional |

Two structural obstacles:

1. **`StartupProcess.cpp` does not exist here.** Upstream moved startup out of
   `Application`, and the `QtStyle` handler lives in it. That code has to be relocated
   into our `Application` rather than copied.
2. **`Application.cpp` has diverged 1754/1332 against upstream.** Nothing in this area
   can be cherry-picked; the ~80 lines of wiring get hand-written.

## 5. Fork-side merge cost

Smaller than the file sizes suggest.

- Our two live sheets are only **149/44** (`Light.qss`) and **148/43** (`Dark.qss`)
  lines from the last upstream version of the same paths, across **nine commits**:

  ```
  2345aabf30  Gui: give hover its own shade, so it stops looking like selection
  fb23978a39  Gui: match the file card highlight to the New File cards above it
  5e51f4a672  Gui: give the Start page file cards a hover highlight in the themes
  b39dd73fed  Gui: fold the title bar menu behind the FreeCAD logo
  64bf67bdf9  Gui: drop the marker-coloured divider on a hovered menu button
  2f73a29bfa  Gui: nudge the toolbar icon on hover and press
  6adf06fa1a  Gui: size a toolbar button to what it draws
  5694f1d2c0  Gui: change groupbox margin in stylesheets
  1ee0bb05ea  Gui: change tool button menu arrow size in stylesheet
  ```

- Of the **101** widget selectors in our `Dark.qss`, only **8** have no counterpart in
  `FreeCAD.qss` + `defaults.qss`, and they are mostly the fork's custom title bar:

  ```
  #titleBarLogo
  WindowDecorationButton
  CustomTitleBarWindow WindowDecorationButton#closeButton
  Gui--TipLabel
  QMdiArea
  QDialogButtonBox QPushButton
  Gui--PropertyEditor--PropertyEditor QDoubleSpinBox
  .QFrame
  ```

- The 33 overlay sheets and 3 menu sheets need no work: different load path, no
  variables.

- Two fork mechanisms would be **superseded**, not merged: the `Themes/Variables`
  substitution group, and the `@ThemeAccentColorLight` / `@ThemeAccentColorDark`
  shades derived in `replaceVariablesInQss` (commit `2345aabf30`). Both become
  ordinary entries in a parameter set once `darken`/`lighten`/`blend` exist.

- The open judgement call is the other six sheets (`Darker`, `Dark-modern`,
  `Light-modern`, `Dark-contrast`, `ProDark`, `Behave-dark`): drop them, or re-express
  each as a 66-line parameter set. Re-expressing them is the whole point of the new
  model, but it is also the only part of this that is genuinely creative work, because
  nobody has checked whether those six can be described by upstream's 66 roles.

## 6. Done already: populating the three accent slots

The smallest useful piece has been taken separately, because it needed none of the
above.

The three accent parameters all defaulted to the same value, `#557BB6`, and no fork
preference pack set them. The shipped sheets, however, use the three slots for three
different jobs:

| Slot | Uses in `Light.qss` | Meaning |
|---|---|---|
| `@ThemeAccentColor1` | 98 | the highlight — hover, selected, checked |
| `@ThemeAccentColor2` | 27 | the engaged state — focus, pressed, combo box open |
| `@ThemeAccentColor3` | 6 | only ever the far stop of a gradient whose near stop is 1 |

With one shared value, focus was indistinguishable from hover and every accent
gradient was flat. `Application::DefaultAccentColor` is now three constants, and the
`Light` and `Dark` packs set the trio explicitly — Dark lifting slot 2 rather than
deepening it, since on a dark background the engaged state should get brighter.

New defaults alone fixed nothing, though, and the reason is worth recording for the
port: **the Start wizard writes all three slots into the user's configuration**, with
the one shared color, for everyone who passes through it. Stored values beat defaults,
so no existing installation would ever have seen the change. `checkForDeprecatedSettings()`
now carries a migration: where the stored trio still matches the old shared value —
the wizard's signature, since nothing else sets the three together — slots 2 and 3 are
rewritten, choosing the light or dark shades according to `MainWindow/ColorScheme`.

The same trap applies to anything the port changes the default of. Persisted values
from the wizard will mask it.

This is a stopgap that the port would replace: under upstream's model the same
relationships are `darken(@AccentColor, 15)` and friends, written once.

## 7. Effort

Estimates from reading the code, not from attempting the port.

- **Phase 1 — the evaluator alone, keeping our sheets. ~1 day, low risk.**
  `ServiceProvider.h`, `StyleParameters/`, the `Application` wiring. Nothing visual
  changes until a sheet uses the new syntax. Immediately gives `darken`/`lighten`/
  `blend` and named roles, and retires the ad-hoc derived shades from §5. Reversible.
- **Phase 2 — adopt `FreeCAD.qss` and the YAML themes. ~2–3 days.**
  The sheet, both parameter files, the three packs; re-apply the nine commits and the
  eight selectors from §5. Dominated by visual QA over fork-specific UI — the custom
  title bar, overlay docks, `Gui--PieButton`, the Start page — which upstream has never
  seen. **This is the estimate to distrust**; the QA is not sizeable from a diff.
- **Phase 3 — optional. ~1–2 days.** Decide the fate of the six extra sheets; port
  `DlgThemeEditor` if the GUI is wanted.

Phase 1 stands on its own and does not commit us to Phase 2.

## 8. Open questions

- Do upstream's 66 roles actually cover what our eight sheets express, or would the
  fork need roles of its own? Unknown until someone tries one non-Light/Dark theme.
- `QtStyle: FreeCAD` — the proxy style is only 87 lines, but what it changes and
  whether the fork's widgets depend on the platform style has not been examined.
- Upstream keeps a "Theme Parameters - Fallback" source reading
  `Themes/UserTokens`, marked in their code as compatibility to be removed before
  release. If we port, we should not inherit it.
