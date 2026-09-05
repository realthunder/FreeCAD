/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef APP_EXPRESSION_IMAGE_HOST_H
#define APP_EXPRESSION_IMAGE_HOST_H

/* The desktop host side of the expression sandbox image
 * (docs/ExpressionSandbox.md secs 6/7, docs/ExpressionImage.md):
 * owns one instantiated image and speaks the FcxWire CBOR protocol to
 * src/App/ExpressionImage/ through a runtime (ExpressionImageRuntime.h):
 * wasmtime for the wasm32-wasi image, or V8 + pyodide for the same image
 * as a pyodide extension wheel (docs/PyodideHost.md).
 *
 * Compiled only when FREECAD_EXPR_IMAGE is ON (needs libwasmtime);
 * everything here is host-only and never part of ExpressionCore.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <FCConfig.h>

typedef struct _object PyObject;

namespace App
{

class DocumentObject;
class Expression;

namespace ExpressionSandbox
{

/// One evaluation outcome crossing back from the image.
struct ImageResult
{
    bool ok = false;
    /// CBOR bytes of the reply "val" re-encoded standalone (empty on error).
    std::vector<unsigned char> value;
    /// Python exception type name and message when !ok.
    std::string excType;
    std::string message;
    /// The guest's formatted traceback when !ok and the guest could
    /// format one (empty otherwise): what native FreeCAD prints for a
    /// failed execute(), so a failure inside a guest Proxy is not just
    /// its last line.
    std::string traceback;
};

class AppExport ImageHost
{
public:
    static ImageHost& instance();

    /// True when an engine, an image file and a stdlib dir are all
    /// available and the image instantiated successfully.
    bool available();

    /// Explicit configuration (tests, headless flags).  Defaults come
    /// from the parameter group BaseApp/Preferences/Expression/Sandbox
    /// (ImagePath, StdlibPath), then FCX_IMAGE / FCX_STDLIB, then the
    /// bundle a packaged FreeCAD installs as <datadir>/Fcx -- resolved
    /// lazily on first use.
    void configure(const std::string& imagePath, const std::string& stdlibPath);

    /// Where the image, its stdlib slice and its compiled-module cache
    /// resolve to.  The answer to "why is there no sandbox on this box":
    /// available() only says no.
    struct Location
    {
        std::string image;
        std::string stdlib;
        std::string cache;
        /// The user's package set the guest boots with (pyodide only).
        std::string packages;
    };
    Location location();

    /** Drop the live instance AFTER the current round trip: a package
     * just installed is not in this guest, and the next evaluation's
     * fresh instance loads it at boot.  Called from inside a bridge op
     * (the guest is mid-call, so the instance cannot go away right now).
     */
    void scheduleReset();

    /// The runtime carrying the sandbox ("pyodide", or "wasi" for the
    /// reference build): the live one, else the one the preference /
    /// FCX_RUNTIME would select.
    std::string runtime();

    /// Evaluate one expression source with pre-resolved bindings.
    /// `bindingsCbor` is a CBOR-encoded map of name -> wire value
    /// (may be empty).  Runs on the calling thread; the image is
    /// single-threaded, callers serialize through an internal lock.
    /// `owner`, when given, is the evaluation's principal and the ONE
    /// object the guest may write (write_prop, addProperty, ...); it
    /// must also be in the pack, exported by the caller.  Without it
    /// the evaluation has no principal scope of its own and no writes.
    ImageResult eval(const std::string& source,
                     const std::vector<unsigned char>& bindingsCbor,
                     const App::DocumentObject* owner = nullptr);

    /** Evaluate one EXPRESSION-language source in the image on behalf
     * of `owner` (docs/ExpressionSandbox.md 7.3): parses host-side,
     * pre-resolves every identifier into the bindings pack under the
     * owner's principal (a permission the pack step needs but the
     * principal lacks fails the evaluation as PermissionError, exactly
     * like the native path), exports the owner as the transaction
     * handle, and ships {lang:"expr", src, ctx, owner_h, bindings}.
     * A host-side parse failure ships anyway -- the image raises the
     * identical ParserError.  Handles minted for the pack live until
     * clearHandles().
     *
     * `options` is an App::Expression::EvalOption mask and crosses with
     * the request: the image parses and evaluates under the SAME
     * options the native engine would have used.  Dropping them was a
     * parity hole -- OptionCallFrame is what makes statements legal at
     * all, and OptionPythonMode selects a different lexer start state
     * AND a different name-binding rule.
     */
    ImageResult evalExpression(const App::DocumentObject* owner,
                               const std::string& source,
                               const App::Expression* parsed = nullptr,
                               int options = 0);

    /** Run statements in the guest (FcxWire::OpExec).  With `module`
     * the source becomes a module of that name in the guest's
     * sys.modules -- the way the host pushes workbench Python into the
     * guest while there is no package loader for it (tests today, G1's
     * Draft loader later).  Not counted as an evaluation; bridge ops the
     * source makes are counted.  `ok` says whether it ran; there is no
     * value.
     */
    ImageResult exec(const std::string& source, const std::string& module = std::string());

    /** Rung 2 (docs/Sandbox.md 7.6, G1c): a scripted object's Proxy
     * living in the guest.  proxyNew imports `module` and calls
     * `cls(*args)` there -- `obj.Proxy = self` inside it crosses as
     * write_prop and installs the host stand-in (ExpressionGuestProxy.h)
     * -- or, with `alloc`, only allocates the instance (the Restore
     * path); the value is the stand-in (decodeResult), registered
     * whether or not __init__ installed it.  proxyCall runs hook `hook` of
     * proxy `id` with the host arguments (a document object crosses as
     * a handle) and returns its result by value; it is what a stand-in's
     * hook attributes do.  `owner` is the object the guest may write.
     * Both nest: a guest hook that writes a property runs the host's
     * onChanged inside the bridge op, and that hook's proxyCall is a
     * round trip inside the round trip.
     */
    ImageResult proxyNew(const std::string& module,
                         const std::string& cls,
                         PyObject* args,
                         bool alloc,
                         const App::DocumentObject* owner);
    /// The same with keyword arguments (`kwargs` may be nullptr).
    ImageResult proxyNew(const std::string& module,
                         const std::string& cls,
                         PyObject* args,
                         PyObject* kwargs,
                         bool alloc,
                         const App::DocumentObject* owner);
    ImageResult proxyCall(uint64_t id,
                          const std::string& hook,
                          PyObject* args,
                          PyObject* kwargs,
                          const App::DocumentObject* owner);
    /** A host read or write of a guest proxy's attribute (G1d: Draft's
     * `get_type` reads `obj.Proxy.Type`, BIM writes `Proxy.svgcache`).
     * proxyGet answers the value by value, or a forwarder bound to
     * (id, name) when the attribute is callable (FcxWire::TagGuestMethod
     * decodes to it); the guest's AttributeError comes back as one.
     * proxySet stores a VALUE: a host object would cross as a handle
     * that no transaction outlives, so it is refused before the trip
     * (TypeError).  Neither has an owner: nothing the guest does inside
     * a getter may write a document object.
     */
    ImageResult proxyGet(uint64_t id, const std::string& name);
    ImageResult proxySet(uint64_t id, const std::string& name, PyObject* value);
    /// A stand-in died: the guest drops the proxy with the next request.
    void dropProxy(uint64_t id);

    /** Decode the value of a successful result into a new host PyObject
     * reference (nullptr on failure).  Handles in the reply resolve
     * against the live table, so call this BEFORE clearHandles().  The
     * caller holds the GIL.
     */
    PyObject* decodeResult(const ImageResult& result);

    /** Register a live host object for the current transaction and
     * return its wire handle id; pass it into bindings as
     * {"t":"h","id":<id>,"ty":<type>}.  The image reaches back through
     * the fcx.host_call bridge (get_attr/call/get_item/len ops, each
     * permission-checked) when the evaluation touches it.  The table
     * holds one reference until release/clearHandles.
     */
    uint64_t exportObject(PyObject* obj);

    /// Drop every live handle (end of a recompute transaction).
    void clearHandles();

    /// Live handle count (tests: proxies release on image-side __del__).
    std::size_t handleCount() const;

    /// Evaluations that have crossed into the image since startup.
    /// Diagnostics, and how a test proves an evaluation really was
    /// routed rather than answered in-process.
    std::size_t evalCount() const;

    /** Bridge traffic since startup or resetStats(): evaluations,
     * handles minted (pack exports and reply values alike), and every
     * guest->host op by its wire name (read_prop, get_attr, call,
     * get_item, len, release, pkg.missing).  This is what prices a
     * workload's crossing -- how many hops a Draft or Arch execute()
     * really makes on handles -- before any snapshot op is designed
     * (docs/Sandbox.md sec 8.1).  Exposed as
     * FreeCAD.ExpressionSandbox.stats().
     */
    struct Stats
    {
        std::size_t evals = 0;
        std::size_t handles = 0;
        /// host->guest Proxy ops (proxyNew, proxyCall, proxyGet, proxySet)
        std::size_t proxyCalls = 0;
        /// guest->host bridge ops by wire name
        std::map<std::string, std::size_t> ops;
        /// host->guest ops by wire name (eval, exec, proxy_*)
        std::map<std::string, std::size_t> hostOps;
    };
    Stats stats() const;
    void resetStats();

    /** Raw protocol call: a CBOR request in, the CBOR reply out, with
     * no host-side parsing, packing or result re-encoding.  Diagnostics
     * and measurement (the transport floor) plus any future op the
     * typed entry points above do not cover; returns false when the
     * image is unavailable or the round trip failed.
     */
    bool rawCall(const std::vector<unsigned char>& requestCbor,
                 std::vector<unsigned char>& replyCbor);

    /// Drop the live instance (tests; recovering from a trapped image).
    /// Handles survive a reset: they are host-side state.
    void reset();

    ~ImageHost();
    ImageHost(const ImageHost&) = delete;
    ImageHost& operator=(const ImageHost&) = delete;

private:
    ImageHost();
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_IMAGE_HOST_H
