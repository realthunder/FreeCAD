# scripts/

Developer helper scripts for the bgfx render-cache backend
(`src/Gui/Renderer/`). See `docs/RenderDebug.md` for the render
debugging / user-shader architecture, `docs/ShaderDesign.md` for the
renderer/shader design, and `docs/DevEnvironment.md` for the build
stacks.

## Launching & serving

| Script | What it does |
|--------|--------------|
| `renderer-desktop.sh [scene.py] [port]` | Launch the desktop GUI on the **WSLg real GPU** (Mesa d3d12 over `/dev/dxg`) with the bgfx backend; optionally stream the scene on `FC_BGFX_SERVE_SCENE=<port>`. |
| `renderer-serve.sh [scene.py] [port]` | Headless (Xvfb, **software GL**) FreeCAD streaming a scene for the WASM viewer — for agents/CI or no-display hosts. Also starts the MCP debug console (`mcp-console.py`). |
| `wasm-viewer.sh [http] [scene]` | Serve the built WASM viewer over HTTP (`Cache-Control: no-store` — never a stale bundle) and print the URL to open it against a scene backend. |
| `mcp-console.py` | In-FreeCAD script starting the **MCP debug console** (`freecad.mcp_console`: `run_python` / `search_api` over streamable-HTTP, default port 8765, `FC_MCP_PORT` overrides, `FC_MCP_PORT=0` disables) so an AI agent can drive the live process. |

## Browser-side tools (Chromium)

| Script | What it does |
|--------|--------------|
| `wasm-chrome.js probe\|drive …` | **The Chrome launcher, both tiers**: `--headless` = swiftshader (no display), default = a **headful window on the WSLg desktop with the real GPU** (WebGL2 on ANGLE-over-D3D12, real compositor at 60Hz) — the only browser tier that reproduces frame-pacing / GPU-state bugs. `probe` reports fps + the WebGL renderer actually obtained; `drive <url> [out.png]` runs the standard settle → zoom-burst → stationary-quiet regression against a served scene. Also require-able (`launch()`) by other harnesses. |
| `wasm-shot.js <url> <out.png>` | Screenshot the WASM viewer at a chosen `?cam=` via headless Chromium (swiftshader) — a separate client, never touches a live view. |
| `wasm-hold.js <url> [ms]` | Hold a headless-Chromium page on the WASM viewer so the backend can drive the `dumpFrame` capture protocol (`saveRenderDump(source="viewer")`). |
| `wasm-burst.js <url> <prefix> [ms,…]` | Shoot the viewer repeatedly **while a scene streams in**, over a `KBPS`-throttled link, so the coarse rungs of the fidelity ladder are on screen to be captured. Pair with the viewer's `&stream` flag. |
| `wasm-orbit.js <url> [settle_ms] [steps]` | Settle a scene under `&membudget=`, then orbit — the only way to exercise the ladder running *backwards*. `wasm-burst.js` never evicts: the default budget is not reached, and a fixed camera converges and then merely refuses. Reports releases before vs after the orbit. |

## Verification harnesses

| Script | What it does |
|--------|--------------|
| `render-verify.sh capture\|diff …` | **Render verification harness** (docs/RenderDebug.md §5): capture staged (camera × `RenderDebug_ViewMode`) frame sets from an isolated FreeCAD (xvfb default, `--gpu` real-GPU leg, `--viewer` browser leg), and diff two capture sets per pipeline stage. |
| `render_verify.py` | In-FreeCAD capture driver used by `render-verify.sh` — stages cameras (named views, or 1:1 restage from golden sidecar JSONs) and calls `saveRenderDump` per mode. |
| `render_diff.py` | Stage-ordered capture-set comparer: reports the **first divergent pipeline stage** (depth → normal → ao → shadow → beauty) with difference heatmaps. |
| `user-shader-verify.sh desktop\|viewer\|all …` | **User-shader harness** (docs/RenderDebug.md §6.4/§6.5): runs the four suites below from isolated FreeCAD instances and judges their result files. The viewer leg needs `build/wasm` + `PUPPETEER_PATH`. |
| `user_shader_params.py` | In-FreeCAD suite: `Param_*` dynamic properties on `App::ShaderProgram` -> uniforms on the consuming draws, `App::ShaderBinding` per-binding overrides, non-`Param` groups ignored, byte-exact restores. |
| `user_shader_post.py` | In-FreeCAD suite: scene-level post activation by an empty-target `App::ShaderBinding` -- TreeRank precedence and every deactivation path (override removal, hide, re-target, delete). |
| `user_shader_viewer.py` | Browser-tier suite (scene-graph route): post + material `SoShaderProgram` nodes reach a connected WASM viewer through the server-side compile + snapshot shader table; broken-shader fallback and removal restore. |
| `user_shader_viewer_appearance.py` | Browser-tier suite (document-object route): an empty-target Appearance's post shader reaches the WASM viewer, parameter edits propagate, hiding restores. |
| `cull_audit.py` | **Occlusion-culling audit** (docs/FarFieldProxies.md §12.9, §12.15): in-FreeCAD driver that measures what the culling actually deleted, per setting, on one fixed camera. Re-rasterizes each frame with the cull mask ignored and every draw writing its own id, so the ids owning a pixel are an exact answer to which draws reach the screen — their intersection with the mask is a list of **proven over-culls**, each a named draw with a pixel count, rather than a count of pixels that merely differ. Reports the picture difference beside it from the same frames, and min/med/max of every timing field. `FC_ROWS` selects the settings to compare. |
| `ontop_edge_repro.py` | In-FreeCAD repro for **show-on-top hidden-edge dimming**: box(hidden, on-top, Edge2 selected) + cylinder, captures a bgfx and a plain-GL (`Type=Default`) window grab at the canonical 1600x837 size. The cylinder is required — an all-on-top scene makes `canSkipInternal()` false and the internal GL pass paints over the backend frame. |
| `ontop_edge_judge.py judge\|trace …` | Standalone (PIL) judge for those captures: samples many points along every box edge with per-edge expectations (hidden→DIMMED, front→SOLID, selected→GREEN), accepting a pixel as a line only against its measured local background — a fixed-point luminance probe cannot tell a dimmed line from background. `trace` dumps detected line runs per scanline to re-derive the edge table after a scene/camera change. |

## Demo scenes

All of them run persistently (no auto-close) so they can be viewed on the
desktop or streamed to the WASM viewer; pass them to
`renderer-desktop.sh` / `renderer-serve.sh`.

The first group is about **pixels** — what the renderer draws. The second is
about **payload**: what a publish costs and how a scene arrives
(docs/SceneStreaming.md), where the geometry is deliberately dull.

| Script | What it does |
|--------|--------------|
| `demo-lights.py` | Two colored shadow-casting point-light bulbs (warm/cool) over a pillar + cross-beam on a matte floor — the default verification scene (bulb shadows, bloom, AO; env knobs `SUN`, `BULB_*`, `BLOOM`, `AO`, `VOL`, `SHADOWSMOOTH`). |
| `demo-ao.py` | SSAO/GTAO isolation scene: every other effect off, matte objects in mutual contact with tight concave corners where ambient occlusion reads strongest. |
| `demo-water.py` | Full-effect showcase: water pool (refraction, planar reflection, caustics, water shadow), bark-textured gantry (PBR + normal map), metallic cylinder, fire plume with volumetric lighting. |
| `demo-pbr.py` | PBR showcase: the metallic × roughness sphere chart twice over (silver in front, gold behind) against the IBL studio environment drawn as the background. |
| `demo-finish.py` | **Surface finish showcase** (docs/ShapeAppearanceDesign.md §9): one column per `App::SurfaceFinish` pattern, each authored twice -- a cylinder through the `Render_Finish*` view properties at the pattern's real pitch, a plate through the appearance itself at an exaggerated one. |
| `demo-finish-faces.py` | One appearance, a **different finish per face** -- the per-face storage carrying a finish, not just a colour. |
| `demo-finish-frames.py` | **Projection frames** (§9 rung 3): the pattern laid out in the surface's own coordinates (a cylinder's axis, a cone's taper, an off-centre turned face) instead of triplanarly. `FRAMES_OFF=1` is the one-variable control. |
| `demo-finish-shading.py` | **Every finish under both shading models**: the same six-column chart (five patterns + an unfinished control) captured twice, `Render_PBR` on and off, with each part stating a Phong specular/shininess *and* a `Render_Metallic`/`Render_Roughness` pair computed to be the same surface. `SHADING_SHOT=shot.png` writes `shot-pbr.png` and `shot-phong.png`. |
| `demo-inspect.py` | **Inspection shading** (matcap + cavity, docs/ShaderDesign.md §3.11/§3.12): a deliberately monochrome grid of parts chosen for what the two effects respond to — corrugation with a halving groove radius, a staircase, a dimpled golf ball, a grooved hex shaft, a machined pocketed plate, a crease-free blob. Finely tessellated on purpose: cavity reads prepass normals, so a coarse mesh wears its own triangle grid. Env knobs `MATCAP`, `CAVITY`, `PRESET`, `VALLEY`, `RIDGE`, `AO`. |
| `demo-materialx.py` | **MaterialX material balls** (docs/CyclesIntegration.md sec 6.11): 17 documents from `scripts/materialx/` embedded one per sphere -- the `.mtlx` text rides the `App::ShaderProgram`, so the saved file carries every material in it. The first ball declares a graph interface, which comes up as five `Param_*` properties to drag; the script prints which balls carry parameters. `MTLX_CYCLES=1` (`MTLX_CYCLES_DEV=CUDA`, `MTLX_CYCLES_SPP`) renders the same frame with the path tracer through the Cycles VIEWPORT, so the capture carries the same chrome and labels as the raster shot and the two can be held side by side; needs a build with `BUILD_CYCLES`. |
| `demo-effects.py` | Every shipped effect package as standalone shader objects on their built-in demo geometry — water + spray, fountain + droplets, fire + embers, rain. |
| `demo-particles.py` | Stateful particles beside stateless ones: two `sparks` emitters (integrated state — gravity, drag, bounce, rest) against a closed-form `fountain` arc that cannot bounce. |
| `demo-fountain.py` | The stateful water jet in its setting — a real-water basin (refraction, reflection) with a stone rim, a tall centre jet and two low flanking ones, splash rings feeding the surface. |
| `demo-many.py` | **Payload benchmark**: a grid of `COUNT` *identical* boxes. The geometry deduplicates to a handful of content keys, so whatever the stream still spends scales with the object count and nothing else. |
| `demo-varied.py` | **Stream benchmark**: `COUNT` ellipsoids with seeded per-object radii, so every object is a distinct mesh chunk and nothing deduplicates — what exercises batched fetch and the fidelity ladder, which the box grid barely touches. |
| `textures/` | CC0 texture assets used by the demo scenes (ambientCG Bark012 color + normal maps; see its README). |

## Material icons

| Script | What it does |
|--------|--------------|
| `material-icons.py` | In-FreeCAD script that renders the **bundled material icons** -- one per shipped appearance preset and per surface finish pattern -- into `src/Mod/Material/Gui/Resources/icons/materials/` and rewrites the matching block of `Material.qrc`. Run it on the real GPU (`renderer-desktop.sh`'s d3d12 env; the docstring has the exact command) whenever a preset, the shading model or the engine's output changes, then commit what differs. A few icons come back differing by GPU noise every run (mean difference well under one level); revert those. `FC_ICON_DIR` writes elsewhere, `FC_ICON_KEEP=1` leaves the GUI up. |

## Shader rebuilds

| Script | What it does |
|--------|--------------|
| `compile-shaders.sh [build_dir]` | Recompile the bgfx shaders and refresh the build-tree copies so a shader edit takes effect without a full `ninja` (wrapper around `ninja Renderer_assets`). |

## Real GPU vs software

The conda env ships **no DRI drivers**, so a plain `xcb` launch falls back to
`llvmpipe` (software = slow). `renderer-desktop.sh` launches on WSLg
**Wayland** with the `d3d12` gallium driver to use the real GPU. Verify:

```
grep 'BGFX     Renderer:' /tmp/fc-renderer-desktop.log
# want: D3D12 (NVIDIA ...);  not: llvmpipe
```

Set `FC_ADAPTER=NVIDIA` to force the discrete GPU, or `FC_PLATFORM=xcb` to
force the software path for a deterministic (GPU-independent) render.

The same split exists in the browser: the headless Chromium tools above are
always swiftshader (WSL2 exposes `/dev/dxg` but no `/dev/dri`, so headless
Chrome cannot reach the GPU), which masks frame-pacing and GPU-upload bugs.
`wasm-chrome.js` without `--headless` is the real-GPU browser leg: an X11
window on the WSLg desktop with Mesa steered to `d3d12` and Chrome forced
onto ANGLE-over-native-GL (`--ignore-gpu-blocklist --use-gl=angle
--use-angle=gl`). Three things are all required — libasound on
`LD_LIBRARY_PATH` (the conda lib dir), the d3d12 env, and those flags;
missing pieces degrade silently to no-WebGL2 or llvmpipe, so check with
`node scripts/wasm-chrome.js probe` (want `ANGLE (... D3D12 ...)` at ~60fps).
It opens a window on the developer's desktop — announce before launching.

## Typical loop

```sh
# real-GPU desktop + stream to browser
scripts/renderer-desktop.sh scripts/demo-water.py 8077
scripts/wasm-viewer.sh 8000 8077          # open the printed URL

# after a shader edit
scripts/compile-shaders.sh                # refresh desktop bins
source ~/works/sw/emsdk/emsdk_env.sh && cmake --build build/wasm   # for the browser
```

## Verifying a renderer change

```sh
# bless a golden set once (before the change)
scripts/render-verify.sh capture /path/goldens

# after the change: restage from the goldens' sidecars, capture, per-stage diff
scripts/render-verify.sh capture /tmp/rv --golden /path/goldens

# and keep the user-shader feature green (desktop leg; add viewer with
# build/wasm + PUPPETEER_PATH)
scripts/user-shader-verify.sh desktop /tmp/us
```

## Verifying a scene-streaming change

```sh
# a scene with one distinct mesh per object, so chunks actually stream
COUNT=200 SMOKE_RESULT=/tmp/s.txt scripts/renderer-serve.sh scripts/demo-varied.py 8077
scripts/wasm-viewer.sh 8011 8077
# wait for "SETUP OK" in /tmp/s.txt — 200 objects take about a minute to build

KBPS=3000 node scripts/wasm-burst.js \
  'http://127.0.0.1:8011/fcviewer.html?scene=http://127.0.0.1:8077&noidb&stream&cam=0.6,0.4,300,60,60,5,0,0' \
  /tmp/rung 4000,9000,16000,30000
```

`&stream` prints what each assembly pass could draw (`N draws, M of them
coarse, K objects still arriving`); the counts should climb monotonically
to zero coarse, and the shots should show the model appearing as boxes and
refining into geometry.

**Throttling is not optional.** On loopback the whole scene lands faster
than a frame, so every intermediate rung is real but invisible, and a
broken ladder photographs exactly like a working one. `&noidb` forces the
cold path past the browser's IndexedDB chunk cache.

**Throttling is also not a link.** CDP's `emulateNetworkConditions` caps
each *request*, not the connection, so a page with a hundred requests in
flight gets a hundred times the nominal rate — measured, 1053 KB/s against a
750 KB/s setting. Never compare two builds' total load time under it if they
differ in how many requests they keep outstanding; throttle for the
screenshots, and time the load with throttling off. Elapsed time on
loopback is dominated by request concurrency either way: a single browser
request delivers around 170 KB/s here whatever its size, against 17 MB/s
for the same batch over `curl`.

## Verifying an occlusion-culling change

```sh
export LD_LIBRARY_PATH=~/opt/virtualgl/usr/lib:$LD_LIBRARY_PATH
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb \
  FC_ROWS="sw/1/250000/0/1,sw/1/250000/0/0" \
  xvfb-run -a --server-args='-screen 0 1920x1200x24' \
  ~/opt/virtualgl/opt/VirtualGL/bin/vglrun -d egl0 \
  .conda/run.sh build/conda-relwithdebinfo-801/bin/FreeCAD \
  --log-file ~/cull_audit.log scripts/cull_audit.py
# results in ~/cull_audit.txt as they are produced
```

Each row is a setting: `<ttl>/<confirm>` for the hardware-query oracle,
`sw/<divisor>/<tris>/<threads>/<simd>/<coarse>/<level>/<bias>/<perinst>` for
the CPU masked buffer (coarse occluder hulls §12.16, per-instance testing
§12.17).
**Give every field of a software row** — the rows set view properties and
nothing resets them, so an omitted field inherits the previous row's value.

⚠️ A coarse row prints `pending` beside its hull counts, and it must be 0.
Hulls are built a few per frame, so a row read while the cache is still
filling measures the warm-up and reads as a weak version of the mechanism.

**The instrument is validated before its verdict is read**, and the script
does it for you: the first row runs with culling *off*, where nothing is
masked and the over-cull must therefore be 0. Anything else means the mask
snapshot or the id decode is broken — and a broken id pass reports a
spotless "0 over-culls", which is what a working one reports too. Check
also that `covered px` is a large fraction of the viewport, or the id image
is empty and no row beneath it means anything.

**Read the spread, not the last line.** Both the over-cull pixels and every
timing field are reported min/med/max over the row's ~30 frames, because
both are wide: a culled frame oscillates (§12.6 measured two captures of
one static scene differing as much as culling-on differed from
culling-off), and two runs of an identical configuration reported raster
9.00 ms and 4.31 ms. A 5% effect was once published off single samples of
that quantity — see §12.14, corrected in §12.15.

The one number that must never move is **over-cull 0 px**. It is the gate
on every change to the occlusion path: under-culling is always allowed,
over-culling is geometry deleted from the screen.

## Notes on the other harnesses

A regression is reported against the first pipeline stage whose buffer
diverges, not just the final image. Add `--gpu` for a real-GPU leg (opens a
window on the desktop) and `--viewer` for the browser leg — see the header
of `render-verify.sh` and docs/RenderDebug.md §5. The two harnesses are
complementary: `render-verify.sh` guards the stock pipeline against
goldens, `user-shader-verify.sh` exercises the user-shader feature
end-to-end.
