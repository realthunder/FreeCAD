# scripts/

Developer helper scripts for the bgfx render-cache backend
(`src/Gui/Renderer/`). See `docs/ShaderDesign.md` for the renderer/shader
architecture and `docs/DevEnvironment.md` for the build stacks.

| Script | What it does |
|--------|--------------|
| `renderer-desktop.sh [scene.py] [port]` | Launch the desktop GUI on the **WSLg real GPU** (Mesa d3d12 over `/dev/dxg`) with the bgfx backend; optionally stream the scene on `FC_BGFX_SERVE_SCENE=<port>`. |
| `renderer-serve.sh [scene.py] [port]` | Headless (Xvfb, **software GL**) FreeCAD streaming a scene for the WASM viewer — for agents/CI or no-display hosts. |
| `wasm-viewer.sh [http] [scene]` | Serve the built WASM viewer over HTTP and print the URL to open it against a scene backend. |
| `wasm-shot.js <url> <out.png>` | Screenshot the WASM viewer at a chosen `?cam=` via headless Chromium (swiftshader) — a separate client, never touches a live view. |
| `wasm-hold.js <url> [ms]` | Hold a headless-Chromium page on the WASM viewer so the backend can drive the `dumpFrame` capture protocol (`saveRenderDump(source="viewer")`). |
| `render-verify.sh capture\|diff …` | **Render verification harness** (docs/RenderDebug.md §5): capture staged (camera × `RenderDebug_ViewMode`) frame sets from an isolated FreeCAD (xvfb default, `--gpu` real-GPU leg, `--viewer` browser leg), and diff two capture sets per pipeline stage. |
| `render_verify.py` | In-FreeCAD capture driver used by `render-verify.sh` — stages cameras (named views, or 1:1 restage from golden sidecar JSONs) and calls `saveRenderDump` per mode. |
| `render_diff.py` | Stage-ordered capture-set comparer: reports the **first divergent pipeline stage** (depth → normal → ao → shadow → beauty) with difference heatmaps. |
| `user-shader-verify.sh desktop\|viewer\|all …` | **User-shader harness** (docs/RenderDebug.md §6.4/§6.5): the desktop leg runs the document-object-model GUI suites under xvfb; the viewer leg re-runs the shader pipeline against a live headless-Chromium WASM viewer (needs `build/wasm` + `PUPPETEER_PATH`). |
| `user_shader_params.py` | In-FreeCAD suite: `Param_*` dynamic properties on `App::ShaderProgram` → uniforms, `App::Appearance` per-binding overrides, byte-exact restores. |
| `user_shader_post.py` | In-FreeCAD suite: scene-level post activation by empty-target `App::Appearance` — TreeRank precedence, hide/re-target/delete restores. |
| `user_shader_viewer.py` | Browser-tier suite (scene-graph route): post + material `SoShaderProgram` nodes reach a connected WASM viewer via the snapshot shader table. |
| `user_shader_viewer_appearance.py` | Browser-tier suite (document-object route): an empty-target Appearance's post shader reaches the WASM viewer, params propagate, hide restores. |
| `compile-shaders.sh [build_dir]` | Recompile the bgfx shaders and refresh the build-tree copies so a shader edit takes effect without a full `ninja`. |
| `demo-water.py` | Example scene (water pool + metallic cylinder + fire plume) exercising volumetric / SSAO / shadow / water-surface / caustics. |

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
```

A regression is reported against the first pipeline stage whose buffer
diverges, not just the final image. Add `--gpu` for a real-GPU leg (opens a
window on the desktop) and `--viewer` for the browser leg — see the header
of `render-verify.sh` and docs/RenderDebug.md §5.
