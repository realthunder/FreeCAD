// Runs AFTER host_shim.js and pyodide.js: the JavaScript half of the
// pyodide runtime (ExpressionPyodideRuntime.cpp).  It boots pyodide from
// the directory the host scoped its reader to, loads the fcx_image wheel
// found there, wires the guest's bridge to the host's native function, and
// leaves two functions on the global for the host to call:
//
//   __fcx_boot(root, wheel) -> Promise<string>   "booted" or throws
//   __fcx_call(Uint8Array)  -> Uint8Array        one CBOR round trip
//
// The host installs `__fcx_bridge(Uint8Array) -> Uint8Array` before boot;
// it is the only way out of the guest, and it goes straight to
// ImageHost's bridge dispatch.
//
// Transport: two bytearrays the guest owns, viewed here directly in wasm
// memory (PyProxy.getBuffer), so a round trip moves bytes with typed-array
// copies and crosses the language boundary with INTEGERS only.  Minting a
// proxy per crossing measured 40 us of floor; this is the fix.  A view
// detaches when wasm memory grows or a buffer is resized, so every use
// checks and re-acquires.
(function () {
  "use strict";

  var py = null;
  var callLen = null;     // PyProxy of _fcx_image.call_len (owned copy)
  var growReq = null;     // PyProxy of _fcx_image.grow_request
  var reqProxy = null;    // PyProxy of the request bytearray
  var repProxy = null;    // PyProxy of the reply bytearray
  var reqView = null;     // PyBuffer over reqProxy (data: Uint8Array in wasm memory)
  var repView = null;

  function acquire(proxy, old) {
    if (old) old.release();
    return proxy.getBuffer();
  }
  function reqData() {
    if (!reqView || reqView.data.byteLength === 0) reqView = acquire(reqProxy, reqView);
    return reqView.data;
  }
  function repData() {
    if (!repView || repView.data.byteLength === 0) repView = acquire(repProxy, repView);
    return repView.data;
  }

  // Put `bytes` at the start of the request buffer, growing it first if
  // it is too small (which replaces its storage, hence re-acquire).
  function putRequest(bytes) {
    var data = reqData();
    if (bytes.byteLength > data.byteLength) {
      growReq(bytes.byteLength);
      reqView = acquire(reqProxy, reqView);
      data = reqView.data;
    }
    data.set(bytes);
    return bytes.byteLength;
  }

  globalThis.__fcx_boot = async function (root, wheel) {
    if (typeof globalThis.__fcx_bridge !== "function")
      throw new Error("__fcx_bridge is not installed");
    py = await loadPyodide({
      indexURL: root,
      stdout: function (s) { print(s); },
      stderr: function (s) { printErr(s); },
    });
    await py.loadPackage(wheel, { messageCallback: function () {} });
    var mod = py.pyimport("_fcx_image");
    // Attribute proxies are borrowed from their owner and die with it;
    // copy() gives ones that outlive mod.
    callLen = mod.call_len.copy();
    growReq = mod.grow_request.copy();
    var bufs = mod.buffers();
    reqProxy = bufs.get(0).copy();
    repProxy = bufs.get(1).copy();
    bufs.destroy();
    // The guest's bridge: its request sits in the reply buffer, n bytes
    // long; our reply goes into the request buffer and we return its
    // length.  A copy out is needed because the host may run Python
    // that grows wasm memory before it is done with the bytes.
    mod.set_host_buffered(function (n) {
      var req = new Uint8Array(repData().subarray(0, n));
      var reply = globalThis.__fcx_bridge(req);
      return putRequest(reply);
    });
    mod.destroy();
    return "booted";
  };

  globalThis.__fcx_call = function (request) {
    if (!callLen) throw new Error("fcx_image is not loaded");
    var n = putRequest(request);
    var len = callLen(n);
    return new Uint8Array(repData().subarray(0, len));
  };

  globalThis.__fcx_teardown = function () {
    if (reqView) { reqView.release(); reqView = null; }
    if (repView) { repView.release(); repView = null; }
    for (var p of [callLen, growReq, reqProxy, repProxy]) if (p) p.destroy();
    callLen = growReq = reqProxy = repProxy = null;
    py = null;
  };
})();
