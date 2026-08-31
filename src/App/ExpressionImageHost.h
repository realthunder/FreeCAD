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
 * embeds wasmtime, owns one instantiated image per principal, and
 * speaks the FcxWire CBOR protocol to src/App/ExpressionImage/.
 *
 * Compiled only when FREECAD_EXPR_IMAGE is ON (needs libwasmtime);
 * everything here is host-only and never part of ExpressionCore.
 */

#include <cstddef>
#include <cstdint>
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
    /// (ImagePath, StdlibPath) resolved lazily on first use.
    void configure(const std::string& imagePath, const std::string& stdlibPath);

    /// Evaluate one expression source with pre-resolved bindings.
    /// `bindingsCbor` is a CBOR-encoded map of name -> wire value
    /// (may be empty).  Runs on the calling thread; the image is
    /// single-threaded, callers serialize through an internal lock.
    ImageResult eval(const std::string& source,
                     const std::vector<unsigned char>& bindingsCbor);

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
