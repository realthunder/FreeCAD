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

#ifndef APP_EXPRESSION_IMAGE_RUNTIME_H
#define APP_EXPRESSION_IMAGE_RUNTIME_H

/* The transport under ImageHost: what carries a CBOR request into the
 * sandbox guest and its CBOR reply back, and what the guest's mid-eval
 * bridge ops travel through in the other direction.  Everything above
 * this line -- the handle table, the bindings pack, the bridge dispatch,
 * result decoding -- is runtime-agnostic and lives in ImageHost; a
 * runtime is only the pipe and the guest it is attached to.
 *
 * Two runtimes exist (docs/ExpressionImage.md, docs/PyodideHost.md):
 *   - wasmtime, running the wasm32-wasi image (fcx_image.wasm + a stdlib
 *     slice), the shipping default;
 *   - V8 + pyodide, running the same image slice as a pyodide extension
 *     wheel (fcx_image-*.whl) beside a pyodide distribution.
 * Which one ImageHost instantiates is the preference
 * BaseApp/Preferences/Expression/Sandbox:Runtime ("wasi" or "pyodide").
 *
 * Internal to the App library: not installed, not part of the API.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace App
{
namespace ExpressionSandbox
{

class ImageRuntime
{
public:
    virtual ~ImageRuntime() = default;

    /// One guest->host bridge op: CBOR request bytes in, CBOR reply out.
    /// ImageHost supplies it; the runtime calls it from inside a round
    /// trip when the guest reaches back.
    using BridgeFn = std::function<std::vector<uint8_t>(const uint8_t*, std::size_t)>;

    /// The paths a runtime resolves for itself (ImageHost::Location).
    struct Paths
    {
        std::string image;   ///< fcx_image.wasm, or the fcx_image wheel
        std::string stdlib;  ///< the stdlib slice dir, or the pyodide dir
        std::string cache;   ///< compiled-form cache, when the runtime has one
    };

    /// The runtime's name as the preference spells it.
    virtual const char* name() const = 0;

    /// Where this runtime's guest files are, given the explicit
    /// configuration (both empty when nothing was configured).
    virtual Paths resolve(const std::string& image, const std::string& stdlib) const = 0;

    /// Bring the guest up.  False (with the reason logged) when the
    /// files are missing or the guest fails to start.
    virtual bool initialize(const Paths& paths, BridgeFn bridge) = 0;

    /// One request/reply round trip.  False on a transport failure,
    /// after which ImageHost drops the runtime.
    virtual bool roundTrip(const std::vector<uint8_t>& request,
                           std::vector<uint8_t>& reply) = 0;

    virtual void teardown() = 0;
};

/// The wasm32-wasi image under wasmtime (ExpressionWasmtimeRuntime.cpp).
std::unique_ptr<ImageRuntime> makeWasmtimeRuntime();

#ifdef FC_EXPR_PYODIDE_HOST
/// pyodide on the bare V8 of v8-embed (ExpressionPyodideRuntime.cpp).
std::unique_ptr<ImageRuntime> makePyodideRuntime();
#endif

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_IMAGE_RUNTIME_H
