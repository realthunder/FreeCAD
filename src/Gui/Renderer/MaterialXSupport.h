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

/// One public input of a document (docs/CyclesIntegration.md sec 6.11):
/// an input the document's own node graph DECLARES, which is
/// MaterialX's way of saying "this is a knob". A value stated on the
/// surface shader node is the document's statement, not an interface,
/// and stays a literal; what is declared here becomes a `Param_*`
/// dynamic property bound to both consumers.
struct MaterialInput {
    /// The parameter's name: the declaring input's own name, made
    /// unique across the document (prefixed with its graph's name when
    /// two graphs declare the same one) and reduced to characters a
    /// property name and a uniform name can both carry.
    std::string name;
    /// MaterialX type of the input ("float", "color3", ...).
    std::string type;
    /// Namepath of the declaring input element. This is the identity
    /// the generated shader's published uniforms are matched against,
    /// and what the path tracer overrides in the document.
    std::string path;
    /// What the document says about presenting it: `uiname`,
    /// `uifolder` and `doc`. Empty where the document says nothing.
    std::string label;
    std::string folder;
    std::string help;
    /// The document's own value, one float per component (1, 2, 3 or
    /// 4 of them). This is the default a consumer uses until something
    /// overrides it, never a padded lane count.
    std::vector<float> value;
};

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
    /// The document's public interface, in document order: what the
    /// graph feeding the first material declares as its inputs. Empty
    /// when the document declares none, which is what a bare surface
    /// node with stated values does.
    std::vector<MaterialInput> inputs;
};

/// Parse and validate a MaterialX document. `sourcePath` is the file
/// the text came from, when it came from one: a material states its
/// images relative to its own document, so that is what they resolve
/// against. Never throws: a document that will not parse comes back
/// invalid with the parser's message.
RendererExport DocumentInfo inspect(const std::string &xml,
                                    const std::string &sourcePath = {});

/// The raster shader generated from a document: a material-inputs
/// function, not a whole program. The engine splices `source` into the
/// stock mesh fragment shader, which calls
///
///     void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g)
///
/// once per fragment to let the document state the OpenPBR parameters
/// (fc_openpbr.sh) before the engine's own lighting runs. The document
/// therefore describes the SURFACE and the engine keeps the lighting,
/// which is what lets shadows, IBL, the section clip and the rest apply
/// to a MaterialX material unchanged.
struct GeneratedMaterial {
    /// The generation succeeded and `source` is worth compiling.
    bool valid = false;
    /// Why not, when it is not valid; empty otherwise. A material that
    /// cannot be generated renders as its stock appearance -- the
    /// document is reported, never half-applied.
    std::string error;
    /// Legal but consequential things about the document: a shading
    /// model that had to be translated, a node the raster path cannot
    /// express. Reported whether or not generation succeeded.
    std::vector<std::string> warnings;
    /// The generated shader text.
    std::string source;
};

/// Generate the raster material-inputs function for a document.
/// `sourcePath` resolves its relative file references, as in inspect().
/// Never throws: a document that will not generate comes back invalid
/// with the reason.
RendererExport GeneratedMaterial generate(const std::string &xml,
                                          const std::string &sourcePath = {});

/// Absolute path of the standard data library shipped beside the
/// binary, the directory holding stdlib/, pbrlib/, bxdf/ and the rest.
/// Empty when the library is not built.
RendererExport std::string dataLibraryPath();

}  // namespace Render::MaterialX

#endif  // RENDER_MATERIALX_SUPPORT_H
