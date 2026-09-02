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
// ImageHost's bridge dispatch.  Proxy hygiene (PyProxy.destroy, buffer
// release) is done here, in the language where it is natural, so the
// C++ side sees only typed arrays.
(function () {
  "use strict";

  var py = null;
  var call = null;      // PyProxy of _fcx_image.call

  // Guest bytes -> a copy in a fresh Uint8Array, proxy destroyed.
  function takeBytes(proxy) {
    var buf = proxy.getBuffer();
    try {
      return new Uint8Array(buf.data);   // copies out of wasm memory
    } finally {
      buf.release();
      proxy.destroy();
    }
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
    // The guest's bridge callable: bytes in (a PyProxy here), bytes out.
    mod.set_host(function (reqProxy) {
      var req = takeBytes(reqProxy);
      return globalThis.__fcx_bridge(req);
    });
    // An attribute proxy is borrowed from its owner and dies with it;
    // copy() gives one that outlives mod.
    call = mod.call.copy();
    mod.destroy();
    return "booted";
  };

  globalThis.__fcx_call = function (request) {
    if (!call) throw new Error("fcx_image is not loaded");
    var replyProxy = call(request);
    return takeBytes(replyProxy);
  };

  globalThis.__fcx_teardown = function () {
    if (call) { call.destroy(); call = null; }
    py = null;
  };
})();
