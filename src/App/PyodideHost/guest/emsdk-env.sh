# Activates the emscripten that pyodide 314.0.6 was built with (5.0.3),
# from its own emsdk clone so the wasm viewer's emsdk is never touched.
#
#   source src/App/PyodideHost/guest/emsdk-env.sh
#
# Two things this box needs that a stock `source emsdk_env.sh` does not
# do: emsdk and emcc both require Python >= 3.10 and the system python3
# is 3.8, and emsdk_env.sh CLEARS EMSDK_PYTHON while constructing the
# environment -- so it is exported before (for emsdk itself) and again
# after (for the emcc/em++ wrappers).
FCX_EMSDK=${FCX_EMSDK:-$HOME/works/sw/emsdk-5.0.3}
FCX_EMSDK_PYTHON=${FCX_EMSDK_PYTHON:-$HOME/miniforge3/envs/v8build/bin/python}
export EMSDK_PYTHON=$FCX_EMSDK_PYTHON
EMSDK_QUIET=1 source "$FCX_EMSDK/emsdk_env.sh"
export EMSDK_PYTHON=$FCX_EMSDK_PYTHON
# The cross-build environment pyodide-build installed: Python 3.14
# headers built for wasm32-emscripten.
export PYODIDE_XBUILDENV=${PYODIDE_XBUILDENV:-$HOME/works/sw/pyodide/xbuildenv/314.0.6/xbuildenv/pyodide-root}
export PYODIDE_PYTHON_INCLUDE=$PYODIDE_XBUILDENV/cpython/installs/python-3.14.2/include/python3.14
