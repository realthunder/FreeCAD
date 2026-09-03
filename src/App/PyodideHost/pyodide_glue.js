// Runs AFTER host_shim.js and pyodide.js: the JavaScript half of the
// pyodide runtime (ExpressionPyodideRuntime.cpp).  It boots pyodide from
// the directory the host scoped its reader to, loads the fcx_image wheel
// found there, wires the guest's bridge to the host's native function, and
// leaves two functions on the global for the host to call:
//
//   __fcx_boot(root, wheel, packagesDir, packages) -> Promise<string>
//                                                "booted" or throws
//   __fcx_call(Uint8Array)  -> Uint8Array        one CBOR round trip
//   __fcx_setInterrupt(Int32Array)               the interpreter's interrupt buffer
//
// `packagesDir` is the user's package set (docs/PyodideHost.md sec 12),
// the base pyodide's loader fetches lock-file packages from, and
// `packages` the names its manifest lists, loaded at boot in that
// order; both may be empty.  Only the host's scoped reader ever serves
// those fetches.
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

  // The last-in-line import finder (docs/SandboxNetwork.md sec 9.3):
  // when nothing in the guest can import a module, ask the host what it
  // knows through the one bridge op the guest has.  An empty answer is
  // "unknown" and the ordinary ModuleNotFoundError follows; anything
  // else is the message to raise -- an offer the host has recorded for
  // the user, or "installed, next evaluation".  Nothing is loaded from
  // inside an import: the install is the user's click on the host side,
  // and a fresh guest picks the package up at boot.
  var FINDER =
    "import sys, importlib.abc\n" +
    "class _FcxPackageFinder(importlib.abc.MetaPathFinder):\n" +
    "    def find_spec(self, fullname, path=None, target=None):\n" +
    "        if path is not None:\n" +
    "            return None\n" +
    "        import _fcx\n" +
    "        try:\n" +
    "            answer = _fcx.op('pkg.missing', 0, fullname)\n" +
    "        except Exception:\n" +
    "            return None\n" +
    "        if not answer:\n" +
    "            return None\n" +
    "        raise ModuleNotFoundError(answer, name=fullname)\n" +
    "sys.meta_path.append(_FcxPackageFinder())\n";

  globalThis.__fcx_boot = async function (root, wheel, packagesDir, packages) {
    if (typeof globalThis.__fcx_bridge !== "function")
      throw new Error("__fcx_bridge is not installed");
    var options = {
      indexURL: root,
      stdout: function (s) { print(s); },
      stderr: function (s) { printErr(s); },
    };
    if (packagesDir) options.packageBaseUrl = packagesDir;
    py = await loadPyodide(options);
    await py.loadPackage(wheel, { messageCallback: function () {} });
    if (packages && packages.length) {
      // A package that fails to load is reported and skipped: the guest
      // is still good without it, and the import will say what is wrong.
      await py.loadPackage(packages, {
        messageCallback: function () {},
        errorCallback: function (m) { printErr("sandbox package: " + m); },
      });
    }
    py.runPython(FINDER);
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

  // The soft stage of the host's time budget: pyodide polls buf[0] from
  // the eval loop and raises the signal it finds there (2 = SIGINT ->
  // KeyboardInterrupt).  The host owns the storage and writes it from
  // its watchdog thread.  null turns the polling off.
  globalThis.__fcx_setInterrupt = function (buf) {
    if (!py) throw new Error("pyodide is not booted");
    py.setInterruptBuffer(buf || undefined);
  };

  globalThis.__fcx_teardown = function () {
    if (reqView) { reqView.release(); reqView = null; }
    if (repView) { repView.release(); repView = null; }
    for (var p of [callLen, growReq, reqProxy, repProxy]) if (p) p.destroy();
    callLen = growReq = reqProxy = repProxy = null;
    py = null;
  };
})();
