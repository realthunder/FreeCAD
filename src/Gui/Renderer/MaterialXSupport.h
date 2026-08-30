/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef RENDER_MATERIALX_SUPPORT_H
#define RENDER_MATERIALX_SUPPORT_H

#include <string>
#include <vector>

#include "Renderer.h"

/// MaterialX material documents (docs/CyclesIntegration.md sec 8 item
/// 15 phase B). MaterialX is the material DESCRIPTION language: a
/// document is a graph of pattern nodes feeding one surface-shader
/// node, and each back-end reads it in its own vocabulary rather than
/// compiling it -- the path tracer walks the graph into its own shader
/// nodes, the raster path generates a material-inputs function from it.
/// Neither uses MaterialX's own lighting; the engine keeps that.
///
/// Nothing here names a MaterialX type, so the header is the same with
/// or without the library and a build without it answers available()
/// false and fails the rest with a message. The typed API the
/// consumers share is MaterialXSupportP.h.
namespace Render::MaterialX {

/// Whether this build carries the MaterialX library.
RendererExport bool available();

/// What a document turned out to be: enough for a caller to report the
/// problem, or to say which surface it will render.
struct DocumentInfo {
    /// The document parsed, its node references resolved against the
    /// standard data library, and it names at least one renderable
    /// surface.
    bool valid = false;
    /// Why not, when it is not valid; empty otherwise.
    std::string error;
    /// Anything the document is doing that is legal but will not
    /// render as written (an unsupported node, say). Reported whether
    /// or not the document is valid.
    std::vector<std::string> warnings;
    /// Names of the renderable surface materials, in document order.
    /// The first is the one a consumer renders.
    std::vector<std::string> materials;
    /// The surface-shader node category of the first material
    /// ("open_pbr_surface", "standard_surface", "gltf_pbr", ...).
    std::string surface;
    /// Image files the document names that are not on disk where it
    /// says they are. Reported as warnings: the material still
    /// renders, with those maps missing.
    std::vector<std::string> missingImages;
};

/// Parse and validate a MaterialX document. `sourcePath` is the file
/// the text came from, when it came from one: a material states its
/// images relative to its own document, so that is what they resolve
/// against. Never throws: a document that will not parse comes back
/// invalid with the parser's message.
RendererExport DocumentInfo inspect(const std::string &xml,
                                    const std::string &sourcePath = {});

/// Absolute path of the standard data library shipped beside the
/// binary, the directory holding stdlib/, pbrlib/, bxdf/ and the rest.
/// Empty when the library is not built.
RendererExport std::string dataLibraryPath();

}  // namespace Render::MaterialX

#endif  // RENDER_MATERIALX_SUPPORT_H
