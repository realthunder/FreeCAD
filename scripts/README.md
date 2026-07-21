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
