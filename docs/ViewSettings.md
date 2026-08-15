# Per-view settings and what travels in a document

How a 3D view can answer for a setting itself instead of reading this
installation's preference, which settings are allowed to do that, and
what that means for a document opened on somebody else's machine.

Companion documents:

- `docs/RenderEngine.md` -- the renderer these settings feed, and the
  light budget (3.2) behind the light rig.
- `docs/RenderDebug.md` -- the verification harness and the render dump
  sidecar, which records the parameters a property does not cover.
- `docs/CoinRetirement.md` 3.4 -- the scene light, the render-cache
  capture root, and why a light has to sit above it.

## 1. The problem

Every look-affecting setting used to be one global preference under
`BaseApp/Preferences/View`. `View3DSettings::OnChange` broadcast it to
every viewer, so one application had one lighting rig, one section
style, one clip plane widget. Two things follow from that, and both are
wrong:

- A document could not state how it is meant to be read. A model whose
  section is meant to be capped and hatched, or lit from the side,
  restored none of that.
- A saved view that tried to restore such a look had to **move the
  reader's own defaults** to do it. The document arrives on somebody
  else's installation and changes their FreeCAD.

## 2. The rule

**A saved view may capture only settings that have a view-overriding
property, and it never writes a preference.** The invariant is
greppable: no `ViewParams::set*` and no `ParameterGrp::Set*` under
`ViewProviderSavedView::capture()` or `apply()`.

The test for any knob is what the recipient would say:

| they would say | verdict |
|---|---|
| "this document looks the way its author meant it to" | it may travel |
| "this document changed my FreeCAD" | it must never travel |

Background colour, the navi cube and the sample count are named
examples of the second kind. They stay global preferences, have no view
property, and are captured by nothing.

## 3. The three property families

A view (`Gui::View3DInventor`, a `App::PropertyContainer`) carries
dynamic properties in three groups. All three are read by the drawing
code with the preference as the fallback, so a view with no property
draws exactly as it did before any of this existed.

| prefix | group | who materializes it | fallback |
|---|---|---|---|
| `Render_*` | Render | `Gui::initRenderProperties`, once a renderer is selected | the `View/Render` parameter it was seeded from |
| `Light_*` | Light | nothing | the `View` light key |
| `Section_*` | Section | nothing | the `View` section key |

### 3.1 Presence is the override

Nothing seeds `Light_*` or `Section_*`. A property in those groups
exists only because somebody chose it -- a saved view applied, a script,
or an edit in the Clipping panel -- so **its mere presence is the
"overridden" bit**. There is no flag to maintain, nothing to keep in
step with the preference, and no save-or-not question: what the view
carries is what its author meant, and that is exactly what belongs in
the document.

`Render_*` is the older family and works the other way round: the
properties are materialized on the view and seeded from the parameters,
so presence says nothing there. That is why the render dump sidecar
carries the `View/Render` parameter group beside the properties -- the
difference between the two is what says whether a value was seeded or
overridden (`docs/RenderDebug.md`).

### 3.2 Machine-local render settings

Some `Render_*` properties describe the machine, not the look: AO
resolution and slices, effect resolution, coarse tessellation, the GPU
memory budget, the level tolerance, and the whole `RenderDebug_*` set.
They are created through `_localRenderParam` with
`App::Property::PropNoPersist`, so they can be tuned live on the view
but never reach a file, and `reseedLocalRenderProperties` puts this
installation's preference back when a document written before that
still carries one. `Gui::isLocalRenderProperty` is the list, and
`ViewProviderSavedView` excludes the same names at both ends.

`AOMethod` is deliberately *not* local: SSAO versus GTAO is an
authoring choice.

## 4. The two ways a setting travels

Promoting a knob to a view property IS the decision to let it travel.
The capture rule in 2 is necessary but not sufficient, because there are
two channels and only one of them is opt-in:

- **Automatic.** Every view property is written per view into
  `GuiDocument.xml` (`Gui/Document.cpp`, `<View3D id=...>` around 3069)
  and replayed into the new view on open (around 2211). No user action
  is involved.
- **Opt-in.** `ViewProviderSavedView` captures the `Render_*`,
  `Light_*` and `Section_*` properties of the active view
  (`isViewRenderProperty`, `viewRenderProperties`) under
  `CaptureOption::RenderSettings`, and applies them only when the
  recipient clicks the saved view.

A document written before the settings became per-view states the
section style and the backlight as global preferences under its
clipping capture. `applyLegacyClipSettings` translates those old names
(`ClipFill`, `ClipHatch`, `ClipShowPlane`, `BackLight*`,
`BackFaceLight*`, ...) into the view properties that answer for them
now, so restoring one becomes an override on that view instead of a
change to the reader's defaults.

## 5. The light rig

Fourteen keys: enable, colour, direction and intensity for the
headlight, the backlight and the fill light, plus the scene ambient
colour and intensity. Each can be answered by `Light_<key>` on the view.

- `View3DInventorViewer::applyLightPreference(key)` is the single
  dispatch: it takes the view's property where there is one and reads
  the preference otherwise. `View3DSettings::OnChange` calls it for the
  whole family, which is what replaced fourteen branches there.
- `applyLightProperty(prop)` is the other direction, driven by
  `onViewPropertyChanged` when something sets a `Light_*` property.
- `syncLightProperties()` runs the whole rig, and `View3DInventor::
  Restore` calls it so a document's own lighting reaches the nodes.
- A viewer with no view object (a headless publisher, a preview widget)
  has nowhere to keep an override and simply reads the preferences.

The intensity keys are stored as an **Int per cent** in the parameter
group and as a **float factor** in the property, because that is what
the Coin light node takes. See the trap in 8.

## 6. The section and clipping style

Nine keys, all read through `Gui::sectionStyle(view, name, default)`:

| property | preference | read by |
|---|---|---|
| `Section_Fill` | `SectionFill` | both renderers |
| `Section_FillInvert` | `SectionFillInvert` | both |
| `Section_FillGroup` | `SectionFillGroup` | both |
| `Section_Concave` | `SectionConcave` | both |
| `Section_Hatch` | `SectionHatchTextureEnable` | both |
| `Section_HatchScale` | `SectionHatchTextureScale` | both |
| `Section_HatchTexture` | `SectionHatchTexture` | `applySectionHatchTexture` |
| `Section_ShowPlane` | `ShowClipPlane` | the Clipping panel, `Std_ClipPlaneDragger` |
| `Section_PlaneSize` | `ClipPlaneSize` | the four `ClipDragger`s |

The backend reads them through `RendererBridge::translateSectionConfig
(view)`, which takes the view like the other per-frame configs. The
internal GL pass reads a **snapshot taken once per frame**
(`SoFCRendererP::updateSectionStyle`), because these are read per draw
entry, deep inside the loops, where a property lookup would cost more
than the setting is worth. `SoFCRenderer::setViewObject` is told the
view independently of any backend, since it draws these whether or not
one is attached.

`NoSectionOnTop` is the one style key in the Clipping panel with no view
property: it is read by the render cache manager (which rebuilds when it
changes) and by `SoFCRayPickAction`, not per frame, so promoting it
means dealing with cache invalidation rather than adding a lookup.

## 7. The Clipping panel

The panel is per view -- there is one for each 3D view, keyed by the
view, and it builds that view's clip planes -- so every style widget in
it answers to the view property that owns the setting: the seven
`Section_*` style keys, the two clip plane widget keys, and the
backlight's three `Light_*` keys.

**Where a change is written depends on where it came from.**

| origin | the view property | the preference |
|---|---|---|
| a user turning a knob in the panel | written | written |
| a program: a saved view applied, a script, the preference page moving underneath an open panel | written | untouched |
| the panel filling a widget from what the view already says | untouched | untouched |

A user means it for this view and means it as their default, so it
reaches both. Anything the program drives lands on the view alone,
which is the rule in 2 seen from the dialog's side. The third row is
what keeps merely opening the panel from leaving an override behind on
every key.

The mechanism uses what the preference widgets already do. The
preference write stays where it was -- `Gui::PrefWidget`'s own auto-save,
which its `m_Busy` guard already suppresses while restoring, plus the
two hand-written slots for the fill checkbox and the hatch file chooser.
The new code only adds the view side (`Clipping::Private::bindStyle`,
one connection per widget) and a `populating` guard that, together with
`setAutoSave(false)`, covers the panel's own filling.

A `Section_*` or `Light_*` property change reaches the panel through
`View3DInventorViewer::onViewPropertyChanged` ->
`Clipping::onViewPropertyChanged`, so a saved view or a script moves the
widgets, and the clip plane widget follows.

WARNING: **The panel and the dock are static state.** `Clipping::done()`
(Escape in the panel) deletes the whole dock, and the deletion is
deferred, so it drops `_DockWidget` and `_StackedWidget` immediately as
well as clearing the per-view map. Leaving the statics to their
QPointers meant a toggle before the event loop got to the deletion found
a live dock and built a *second* panel for a view that already had one:
both alive, both holding clip planes in that view's scene graph, and
only the newer one reachable to switch them off again.

## 8. Traps

- WARNING: **A view property outranks the preference on a live view.**
  `ParamGet(...).SetBool("Shadow", False)` is inert on a view that has
  `Render_Shadow`. Any probe or script that means to change what a view
  draws must set the property, and any probe that means to test the
  preference path must check that no property answers for it.
- WARNING: **A parameter group is a set of typed maps.** An Int and a Float
  of the same name are two different values, and
  `SetInt("BackgroundColor", ...)` followed by `GetUnsigned` reads zero.
  `BacklightIntensity` was read as a Float fraction by `ViewParams` and
  as an Int per cent by everything that actually lit anything;
  `ViewParams::migrate()` now drops the Float slot. When a preference
  and a property disagree about units, say so where both are read.
- WARNING: **`PropNoPersist` cannot be a runtime flag.**
  `Property::setStatusValue` masks it (and the other `Prop*` bits) out
  of any later write, so `setStatus(PropNoPersist, true)` is a silent
  no-op. The attribute can only be passed to `addDynamicProperty` as
  its `attr` argument. This is why "presence is the override" beats a
  per-property saved/not-saved flag.
- WARNING: **`Transient` is not the same thing.** A `Transient` property still
  writes its element and its status, and an older file's stored status
  then clears the mark on restore; `PropNoPersist` drops the property
  from the save map entirely (`PropertyContainer.cpp`).
- `App::Property::User3` is the established "the view wrote this, not
  the user" mark: `View3DInventorViewer::onViewPropertyChanged` returns
  early on it.
- The scene-root insert for the viewer's lights is anchored on the
  **camera**, not on an index -- `activateShadow()` swaps the scene root
  by index.

## 9. How this was verified

The behaviour of the section family cannot be probed with a clip plane
inserted from Python: such a plane clips through GL without entering the
render cache, so `material.clippers` stays empty and the section pass
returns before it reads the fill. The clipping the **Clipping panel**
builds does enter it, which makes the panel the way in.

Four legs of one clipped box, captured with `saveRenderDump`:

| leg | preference | property | result |
|---|---|---|---|
| A | fill off | none | the reference |
| B | fill on | none | differs from A by 414 px |
| C | fill off | `Section_Fill` on | **byte-identical to B** |
| D | fill off | `Section_Fill` + `Section_Hatch` | 223 px more, at delta 217 |

That the property and the preference give the same frame to the byte is
the claim the promotion makes. The fill's own effect on a plain box is
small -- it tints the cut boundary -- so a scene that is meant to show
the section fill wants a hatch or a shape with a real interior.

The panel contract is verified the same way, by driving the real widgets
through Qt: opening the panel leaves no property behind, a widget edit
writes both the view and the preference, a property set from outside
moves the widget and leaves the preference alone, and the dragger
command writes the view.

WARNING: Two harness notes that cost real time. Under xvfb the dock can hold a
panel per live 3D view, and `mainWindow.findChild` hands back the first,
which may be bound to another view -- take the panel the stack is
showing. And a Z clip is invisible to a camera pointing down Z: stage
`viewAxonometric()` once and leave the camera alone.

## 10. Open

- `SoFCRayPickAction` reads `getSectionConcave()` globally, so picking
  does not follow a view's own section style.
- Outside the Clipping panel there is no UI to create an override: a
  `Light_*` property appears only through a saved view, a script, or
  that panel.
- `RenderCache` is to be demoted to a development-only setting. When
  that happens `UseVBO` and `TransparentObjectRenderType` go with it --
  they are the same kind of thing. Until then all three are simply
  dropped from every capture path.
