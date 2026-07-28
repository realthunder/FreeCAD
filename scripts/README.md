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

## Browser-side tools (headless Chromium)

| Script | What it does |
|--------|--------------|
| `wasm-shot.js <url> <out.png>` | Screenshot the WASM viewer at a chosen `?cam=` via headless Chromium (swiftshader) — a separate client, never touches a live view. |
| `wasm-hold.js <url> [ms]` | Hold a headless-Chromium page on the WASM viewer so the backend can drive the `dumpFrame` capture protocol (`saveRenderDump(source="viewer")`). |
| `wasm-burst.js <url> <prefix> [ms,…]` | Shoot the viewer repeatedly **while a scene streams in**, over a `KBPS`-throttled link, so the coarse rungs of the fidelity ladder are on screen to be captured. Pair with the viewer's `&stream` flag. |

## Verification harnesses

| Script | What it does |
|--------|--------------|
| `render-verify.sh capture\|diff …` | **Render verification harness** (docs/RenderDebug.md §5): capture staged (camera × `RenderDebug_ViewMode`) frame sets from an isolated FreeCAD (xvfb default, `--gpu` real-GPU leg, `--viewer` browser leg), and diff two capture sets per pipeline stage. |
| `render_verify.py` | In-FreeCAD capture driver used by `render-verify.sh` — stages cameras (named views, or 1:1 restage from golden sidecar JSONs) and calls `saveRenderDump` per mode. |
| `render_diff.py` | Stage-ordered capture-set comparer: reports the **first divergent pipeline stage** (depth → normal → ao → shadow → beauty) with difference heatmaps. |
| `user-shader-verify.sh desktop\|viewer\|all …` | **User-shader harness** (docs/RenderDebug.md §6.4/§6.5): runs the four suites below from isolated FreeCAD instances and judges their result files. The viewer leg needs `build/wasm` + `PUPPETEER_PATH`. |
| `user_shader_params.py` | In-FreeCAD suite: `Param_*` dynamic properties on `App::ShaderProgram` → uniforms on the consuming draws, `App::Appearance` per-binding overrides, non-`Param` groups ignored, byte-exact restores. |
| `user_shader_post.py` | In-FreeCAD suite: scene-level post activation by an empty-target `App::Appearance` — TreeRank precedence and every deactivation path (override removal, hide, re-target, delete). |
| `user_shader_viewer.py` | Browser-tier suite (scene-graph route): post + material `SoShaderProgram` nodes reach a connected WASM viewer through the server-side compile + snapshot shader table; broken-shader fallback and removal restore. |
| `user_shader_viewer_appearance.py` | Browser-tier suite (document-object route): an empty-target Appearance's post shader reaches the WASM viewer, parameter edits propagate, hiding restores. |

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
| `demo-effects.py` | Every shipped effect package as standalone shader objects on their built-in demo geometry — water + spray, fountain + droplets, fire + embers, rain. |
| `demo-many.py` | **Payload benchmark**: a grid of `COUNT` *identical* boxes. The geometry deduplicates to a handful of content keys, so whatever the stream still spends scales with the object count and nothing else. |
| `demo-varied.py` | **Stream benchmark**: `COUNT` ellipsoids with seeded per-object radii, so every object is a distinct mesh chunk and nothing deduplicates — what exercises batched fetch and the fidelity ladder, which the box grid barely touches. |
| `textures/` | CC0 texture assets used by the demo scenes (ambientCG Bark012 color + normal maps; see its README). |

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

A regression is reported against the first pipeline stage whose buffer
diverges, not just the final image. Add `--gpu` for a real-GPU leg (opens a
window on the desktop) and `--viewer` for the browser leg — see the header
of `render-verify.sh` and docs/RenderDebug.md §5. The two harnesses are
complementary: `render-verify.sh` guards the stock pipeline against
goldens, `user-shader-verify.sh` exercises the user-shader feature
end-to-end.
