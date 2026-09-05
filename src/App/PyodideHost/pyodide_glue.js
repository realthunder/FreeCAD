// Runs AFTER host_shim.js and pyodide.js: the JavaScript half of the
// pyodide runtime (ExpressionPyodideRuntime.cpp).  It boots pyodide from
// the directory the host scoped its reader to, loads the fcx_image wheel
// found there, and links the guest to the host in both directions.  It
// leaves these on the global for the host:
//
//   __fcx_boot(root, wheel, packagesDir, packages) -> Promise<string>
//                                                "booted" or throws
//   __fcx_link() -> {alloc, free, call, module}  the guest's exports and
//                                                emscripten Module
//   __fcx_setInterrupt(Int32Array)               the interpreter's interrupt buffer
//
// `packagesDir` is the user's package set (docs/Sandbox.md), the base
// pyodide's loader fetches lock-file packages from, and `packages` the
// names its manifest lists, loaded at boot in that order; both may be
// empty.  Only the host's scoped reader ever serves those fetches.
//
// Transport, both directions, with INTEGERS crossing and the bytes
// staying in wasm memory:
//
//  - guest -> host: the guest's side module imports `fcx_host_call` and
//    `fcx_host_fetch` from "env" (ImageBridge.cpp).  The host installs
//    them as V8 natives `__fcx_host_call` / `__fcx_host_fetch` before
//    boot; mergeLibSymbols puts them in the main module's symbol table,
//    where emscripten's dynamic linker resolves a side module's imports,
//    BEFORE the wheel is loaded.  A bridge op is then one wasm import
//    call landing in C++, which reads the request out of wasm memory in
//    place.  No Python callable, no proxy, no JS in the path.
//  - host -> guest: the side module's `fcx_alloc` / `fcx_call` /
//    `fcx_free` exports (ImageModule.cpp), found in emscripten's loaded
//    library table and handed to the host, which calls them straight
//    from C++.  No `__fcx_call` in JS any more.
//
// Earlier shapes -- a Python callable returning bytes (a proxy per
// crossing, 40 us of floor), then two bytearrays viewed with
// PyProxy.getBuffer (integers across, but a JS function, two typed-array
// copies and a detach check per hop) -- are gone.
(function () {
  "use strict";

  var py = null;
  var linked = null;

  // There is no import finder of ours any more (retired 2026-09-05):
  // a `sys.meta_path` finder asked the host about EVERY failed import,
  // including one a workload catches itself (`try: import regex` in
  // lark, uuid's `_uuid`), and a name in pyodide's lock became an
  // install offer nobody asked for.  The question is now asked by the
  // guest's error reply (ImageDispatch.cpp errorReply) for a
  // ModuleNotFoundError that leaves the guest uncaught -- the import
  // that actually ended the work.

  globalThis.__fcx_boot = async function (root, wheel, packagesDir, packages) {
    if (typeof globalThis.__fcx_host_call !== "function" ||
        typeof globalThis.__fcx_host_fetch !== "function")
      throw new Error("the host bridge natives are not installed");
    var options = {
      indexURL: root,
      stdout: function (s) { print(s); },
      stderr: function (s) { printErr(s); },
    };
    if (packagesDir) options.packageBaseUrl = packagesDir;
    py = await loadPyodide(options);
    // The guest's two "env" imports.  A side module's imports resolve
    // against the main module's import table at instantiation, so this
    // goes before loadPackage; a name already defined there is left
    // alone, hence the fcx_ prefix.
    py._module.mergeLibSymbols({
      fcx_host_call: globalThis.__fcx_host_call,
      fcx_host_fetch: globalThis.__fcx_host_fetch,
    }, "fcx");
    await py.loadPackage(wheel, { messageCallback: function () {} });
    if (packages && packages.length) {
      // A package that fails to load is reported and skipped: the guest
      // is still good without it, and the import will say what is wrong.
      await py.loadPackage(packages, {
        messageCallback: function () {},
        errorCallback: function (m) { printErr("sandbox package: " + m); },
      });
    }
    // The import runs PyInit__fcx_image: the in-image FreeCAD module,
    // the _fcx bridge, the eval globals.
    py.pyimport("_fcx_image").destroy();
    // The side module's exports, from the dynamic linker's own table
    // (the key is the .so's path in the guest's filesystem).
    var libs = py._module.LDSO.loadedLibsByName;
    var exports = null;
    for (var name in libs) {
      if (/(^|\/)_fcx_image\..*\.so$/.test(name)) {
        exports = libs[name].exports;
        break;
      }
    }
    if (!exports)
      throw new Error("fcx_image is not among the loaded side modules");
    for (var fn of ["fcx_alloc", "fcx_free", "fcx_call"]) {
      if (typeof exports[fn] !== "function")
        throw new Error("fcx_image exports no " + fn);
    }
    linked = {
      alloc: exports.fcx_alloc,
      free: exports.fcx_free,
      call: exports.fcx_call,
      module: py._module,
    };
    return "booted";
  };

  globalThis.__fcx_link = function () {
    if (!linked) throw new Error("fcx_image is not loaded");
    return linked;
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
    linked = null;
    py = null;
  };
})();
