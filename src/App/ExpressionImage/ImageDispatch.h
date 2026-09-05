/* The sandbox image's request dispatcher and module setup, shared by the
 * two guests that carry it: the wasm32-wasi reactor (ImageMain.cpp,
 * CPython embedded, run under wasmtime) and the pyodide extension module
 * (ImageModule.cpp, loaded into pyodide's CPython on V8).  Everything
 * here is transport-agnostic: a CBOR request in, a CBOR reply out, per
 * FcxWire.h; how the bytes arrive is the entry file's business.
 */
#ifndef APP_FCX_IMAGE_DISPATCH_H
#define APP_FCX_IMAGE_DISPATCH_H
#include <Python.h>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace FcxImage
{
/// The in-image `FreeCAD` module (math types, exception types, Units);
/// a new module object each call, not yet in sys.modules.
PyObject* initFreeCADModule();

/// Build the eval globals (__builtins__, FreeCAD, App, Units) once the
/// FreeCAD module is importable.  0 on success, else the fcx_init rc.
int initEvalGlobals();

/// The eval globals dict (borrowed); nullptr before initEvalGlobals().
PyObject* evalGlobals();

/// One decoded request -> its reply (never throws; protocol errors are
/// replies).  A pending Python error is cleared on the way out.
nlohmann::json dispatch(const nlohmann::json& req);

/** Ask the host what it knows about a module the guest could not
 * import (`pkg.missing <name>`, docs/Sandbox.md 5.5): the offer to
 * install it, "installed, next evaluation", or "" when the host has
 * nothing to say.  Asked only for an import that ENDS the work -- an
 * uncaught ModuleNotFoundError leaving the guest (the error reply) and
 * the expression language's own `import` statement, which nothing can
 * catch (Expression.cpp ImportModules).  Never raises; leaves the
 * current Python error as it found it.
 */
std::string missingImportOffer(const std::string& module);

/// CBOR request bytes -> CBOR reply bytes.
std::vector<uint8_t> dispatchCbor(const uint8_t* req, size_t len);
}  // namespace FcxImage

#endif  // APP_FCX_IMAGE_DISPATCH_H
