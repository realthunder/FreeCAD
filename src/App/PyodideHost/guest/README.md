# fcx_image

The guest image of FreeCAD's expression sandbox, compiled for Pyodide.

FreeCAD evaluates expressions and expression-language programs that are
carried inside a document in a WebAssembly sandbox rather than in the host
interpreter, so a file from an untrusted source cannot reach the host's
Python, its files or its network. `fcx_image` is the guest half of that
arrangement: a `wasm32-emscripten` extension module that runs inside Pyodide
and implements the curated surface the host exposes to sandboxed code.

## This is not a package you install

It is not importable on a normal CPython host, and a plain
`pip install fcx-image` will not find a matching wheel. The file is loaded by
FreeCAD's own Pyodide runtime through `loadPackage()`, from
`<FreeCAD user data>/Pyodide/wheels/`. FreeCAD downloads and verifies the
Pyodide runtime itself; this wheel is the piece built from FreeCAD's own
sources.

To fetch it deliberately, which is what a packager does:

    pip download --no-deps --only-binary=:all: \
        --platform pyemscripten_2026_0_wasm32 \
        --python-version 3.14 --implementation cp --abi cp314 \
        fcx-image

One step is needed after downloading: rename the file so its platform tag
reads `pyodide_<abi>_wasm32` rather than `pyemscripten_<abi>_wasm32`. The two
spell the same ABI, but PyPI standardised on the first spelling (PEP 783)
while Pyodide and FreeCAD's loader use the second. The bytes are identical;
only the name changes.

## Versioning and compatibility

Each file is tied to one Pyodide ABI, named by the platform tag, and to the
FreeCAD build it was generated from. The guest carries a copy of the host's
surface stamp; if the two disagree the sandbox still starts, but members fail
to cross as they are used. A wheel is therefore only useful with a FreeCAD
whose curated surface matches the one it was built against.

## Where it comes from

Home: https://github.com/realthunder/FreeCAD

Built from `src/App/PyodideHost/guest`. The design, the runtime bootstrap and
the package flow are documented in `docs/PyodideHost.md` and
`docs/Sandbox.md`.
