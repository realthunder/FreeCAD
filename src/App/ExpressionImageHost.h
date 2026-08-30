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

#include <memory>
#include <string>
#include <vector>

#include <FCConfig.h>

namespace App
{
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

    /// Drop the live instance (tests; recovering from a trapped image).
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
