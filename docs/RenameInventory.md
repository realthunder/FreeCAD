# Rename inventory — giving the fork its own name

Status: survey, 2026-08-07. No name chosen yet; no commercialization plan —
the motivation is identity: users should know whose software they run, where
to report problems, and what the browser viewer they were just linked to is
called. Decision so far (discussion 2026-08-07): **rename the product now,
keep the code relationship with upstream** — rebases are still cheap (38
commits replayed over 31 upstream with zero conflicts) and a hard fork is a
separate, later decision that becomes right only when rebase cost exceeds its
value.

The raw count — `FreeCAD` appears in ~4,500 source files — is misleading.
The governing principle splits it into a small "change" pile and a huge
"deliberately keep" pile:

> **Rename the product identity. Keep the formats and the APIs.**

## 0. The one decision that gates everything

The name itself. Constraints worth honoring:

- Domain in hand: `thundereal.com` — a name in that family means the product,
  the share links (`cad.thundereal.com`), and the website reinforce each
  other.
- Check before announcing: trademark collisions (EUIPO/USPTO quick search),
  conda/PyPI package-name availability, GitHub org/repo availability, and
  that the name survives being said aloud in a bug report.
- The `fc`/`FC_` prefixes sprinkled through code and env vars are *not* a
  constraint — see §5.

## 1. The branding mechanism already exists

Upstream FreeCAD supports derivative branding, and the fork inherits it:

- `App::Application::Config()["ExeName"]` / `["ExeVendor"]` — set once in
  `src/Main/MainGui.cpp:186` (currently `"FreeCAD"`). Window titles, log
  file name (`<ExeName>.log`), console banners, crash messages, Qt
  `applicationName` (`src/Gui/Application.cpp:2278`) all read it.
- `src/App/Application.cpp:1465` explicitly replaces "FreeCAD" with the
  branded name in user-visible strings ("Due to branding stuff…").
- User config/data paths derive from `ExeName`/`ExeVendor`
  (`getUserAppDataDir`, `src/App/Application.cpp:1155`).

So the *mechanical core* of the rename is a handful of lines in `src/Main/`,
not a tree-wide sed. Everything else is assets, packaging, and migration.

## 2. Change — user-visible identity

| Item | Where | Notes |
|---|---|---|
| `ExeName` / `ExeVendor` | `src/Main/MainGui.cpp`, `MainCmd.cpp` | the switch itself |
| Binary names `FreeCAD`, `FreeCADCmd` | `src/Main/CMakeLists.txt` (`SET_BIN_DIR`) | keep old names as symlinks for one release cycle |
| CMake `project(FreeCAD)` | top-level `CMakeLists.txt:22` | cosmetic but sets several derived vars |
| Icons & splash | `src/Gui/Icons/freecad*.{svg,png,xpm}`, `freecadsplash.png`, `freecadabout.png` | new artwork; the largest creative task in the tier |
| About dialog | `src/Gui/Dialogs/DlgAbout*` | must credit lineage: "based on FreeCAD" (LGPL attribution is code-level; this is courtesy + clarity) |
| Desktop/AppStream | `src/XDGData/org.freecad.FreeCAD.desktop`, `org.freecad.FreeCAD.metainfo.xml.in` | new reverse-DNS id (`com.thundereal.*`); affects Wayland app_id, taskbar grouping, software-center listing |
| WASM viewer page | `src/Gui/Renderer/wasm/shell.html` title/strings (few sites) | the page strangers see first — highest identity leverage per line changed |
| Start page / workbench strings | `src/Mod/Start`, translated `.ts` files | bulk but mechanical; translations regenerate |
| Windows installer / macOS bundle ids | packaging repos | with the reverse-DNS decision above |

## 3. Change with migration — filesystem & OS integration

These rename *and* must keep reading the old locations, the same
backward-compatible pattern the fork already practices for document
properties:

- **User config/data**: `~/.config/FreeCAD/`, `~/.local/share/FreeCAD/`,
  `%APPDATA%\FreeCAD` → new name, with first-run migration (copy, don't
  move; leave a marker). `user.cfg`/`system.cfg` contents are
  name-agnostic.
- **Cache**: `~/.cache/FreeCAD/` — safe to start fresh (it is transient by
  definition), no migration needed.
- **MIME/file association**: register the new app id as handler for the
  *unchanged* `.FCStd` type. Upstream's `org.freecad` MIME declaration
  stays recognized.
- **Lock files / single-instance keys** derive from `ExeName` — old and new
  builds will not see each other's instances during a transition; acceptable.

## 4. Change — ecosystem & packaging (outside this repo)

- **GitHub repo rename** (`realthunder/FreeCAD` → new): GitHub installs
  permanent redirects for git operations, web, API, raw and release URLs;
  existing clones keep working and forks/issues/stars move intact. The
  redirect survives until a new repo reuses the old name (we control that;
  keep the account name — account renames are the fragile variant). Update
  in-content URLs on our side: feedstock recipes, docs, submodule references
  in sibling repos, CI.
- **Feedstocks**: `freecad-rt-feedstock` package name; `coin3d-feedstock`,
  `pivy-feedstock` unaffected except source URLs. Old conda package name
  should get one final release whose description points at the new name.
- **Companion repos** (`realthunder/coin`, `realthunder/OCCT`): no rename
  needed — they are libraries, not products.
- **Website / wiki / release notes / issue templates**: the loud part.
  Issue templates are the actual fix for the misdirected-bug-report problem —
  state plainly "this is not upstream FreeCAD; report here."
- **Addon Manager**: point default sources at our own addon list if/when it
  diverges; today upstream's list works and can stay.

## 5. Keep — deliberately unchanged

The compatibility surface. Renaming any of these buys nothing and breaks
the ecosystem the product depends on:

- **`.FCStd` format and extension** — file exchange with FreeCAD users is a
  feature. A distinct format would be a *hard-fork* act, exactly what we're
  not doing.
- **Python modules `FreeCAD`, `FreeCADGui`, `Part`, …** — every workbench,
  addon and user macro imports these. This API *is* the ecosystem. (Upstream
  derivatives that rebranded all kept them.) The ~4,500-file grep count
  lives overwhelmingly here and in internal C++ naming.
- **Env vars** — 243 distinct `FC_*`/`FREECAD_*` names in the tree; scripts,
  harnesses, and user setups depend on them. `FC_` is name-neutral anyway.
- **Internal C++ namespaces, class prefixes (`SoFC*`), CMake target names,
  `fcviewer.*`, `fcscenediff`** — invisible to users; churning them is pure
  rebase-conflict liability against upstream.
- **Document XML tags and property names** — persistence compat, as ever.

## 6. Suggested phasing

1. **Name chosen + collision-checked** (trademark, domain, GitHub, conda).
2. **Soft identity switch** — `ExeName`/vendor, About, icons/splash, viewer
   page, issue templates, README. One PR-sized change; the product stops
   introducing itself as FreeCAD. Old binary names symlinked.
3. **OS integration + migration** — desktop/metainfo ids, config-dir
   migration code, MIME handler. Needs one release of soak.
4. **Repo + packaging rename** — GitHub rename (redirects carry the old
   links), feedstock rename with a final pointer release under the old name,
   website/docs sweep.
5. **Never (until an actual hard fork is decided)**: formats, Python API,
   env vars, internal naming.

Rebase-friction note: phases 2–3 touch files upstream also touches
(`MainGui.cpp`, `Application.cpp`, XDG data) but only a few dozen lines;
the branding indirection keeps the diff small. The tree-wide churn that
would make rebases expensive is precisely what §5 declines to do.

## 7. Open questions

- The name.
- Reverse-DNS id: `com.thundereal.<name>` vs a project-owned org id.
- Whether the browser viewer carries the same name as the desktop product
  or its own sub-brand (one product, one name is the safer default).
- When (if ever) `LinkMerge`/`LinkVibe` branch names should follow — cosmetic,
  zero urgency, redirects make it cheap whenever.
