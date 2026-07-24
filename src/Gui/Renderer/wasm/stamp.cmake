# Post-link build stamp of the WASM viewer bundle (docs/RenderDebug.md
# §4.4): a short content hash over the three artifacts a page loads.
# The page fetches it (no-store) and reports it in the hello handshake;
# the scene backend compares against this file on disk
# (FC_BGFX_VIEWER_BUILD) and pushes a cache-busting reload on mismatch —
# so open pages follow ANY rebuild, not just snapshot-format bumps.
# Usage: cmake -DOUT=<dir> -P stamp.cmake  (run in the build dir)
file(SHA1 ${OUT}/fcviewer.wasm _w)
file(SHA1 ${OUT}/fcviewer.data _d)
file(SHA1 ${OUT}/fcviewer.js _j)
string(SHA1 _h "${_w}${_d}${_j}")
string(SUBSTRING ${_h} 0 16 _h)
file(WRITE ${OUT}/fcviewer.stamp "${_h}")
