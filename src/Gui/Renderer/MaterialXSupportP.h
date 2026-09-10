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

#ifndef RENDER_MATERIALX_SUPPORT_P_H
#define RENDER_MATERIALX_SUPPORT_P_H

/// The MaterialX-typed half of MaterialXSupport.h: what the consumers
/// of a document share (the Cycles interpreter, the raster shader
/// generator). Only a translation unit built with MaterialX on its
/// include path may include this -- MaterialXSupport.h is the one the
/// rest of the tree sees.

#include <string>
#include <vector>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/File.h>

#include "MaterialXSupport.h"

/// The library's own convention, and the one every MaterialX example
/// uses. Declared at global scope on purpose: inside
/// Render::MaterialX, a bare "MaterialX" would name our namespace.
namespace mx = ::MaterialX;

namespace Render::MaterialX {

/// The standard data library -- stdlib, pbrlib, bxdf and the rest of
/// dataLibraryPath() -- parsed once and shared. Null when the library
/// is not on disk, which is a deployment fault worth reporting once.
mx::ConstDocumentPtr dataLibrary();

/// Parse a document and resolve its node references against the data
/// library. `sourcePath` is the file the text came from, when it came
/// from one, and becomes the document's source URI so that its
/// relative file references resolve. Null on failure, with why in
/// `error`. Never throws.
mx::DocumentPtr loadDocument(const std::string &xml,
                             const std::string &sourcePath,
                             std::string &error);

/// The search path a document's file references resolve against: its
/// own directory when it has one, then the data library.
mx::FileSearchPath searchPath(const mx::DocumentPtr &doc);

/// Resolve one `filename`-typed input value to an absolute path.
/// Empty when the file is not on the search path.
std::string resolveFile(const mx::DocumentPtr &doc, const std::string &name);

/// The one texture unit a generated material claims for its image
/// nodes, and how many images it may put on it
/// (docs/CyclesIntegration.md sec 6.12).
///
/// The generated function is spliced into the stock mesh fragment
/// stage, which declares samplers 0..10 of its own, and a stateful
/// particle emitter binds 11 and 12 for its state textures. A sampler
/// per image therefore left room for three -- fewer maps than an
/// ordinary PBR material has, which made a library card renderable by
/// the path tracer and refused by the rasterizer. So the images become
/// the LAYERS of one array texture on the single unit below, the way
/// the per-face palette already works at unit 10, and the unit count
/// stops being what bounds a material.
///
/// What bounds it now is the array: its layers are all one size, so
/// every map is resampled onto the largest of them and a document
/// naming a great many is paying for that on each. The cap below is
/// where that stops being worth it; beyond it the document is refused
/// whole rather than drawn with some of its maps missing.
constexpr int kImageUnit = 13;
constexpr int kMaxImageLayers = 16;

/// The name the generated code gives that sampler. It reaches the
/// engine on GeneratedMaterial::imageSampler as well -- the draw side
/// has no MaterialX headers -- and is here because the generator is
/// what writes it.
constexpr const char *kImageSampler = "s_fcMtlxImages";

/// The surface-shader nodes a document renders, in document order: the
/// shader nodes of its material nodes, plus any standalone surface
/// shader. Empty means the document describes no surface, which is the
/// one thing a syntactically valid document can still get wrong.
std::vector<mx::NodePtr> surfaceShaders(const mx::DocumentPtr &doc);

/// What each of those surfaces is CALLED, one per entry of
/// surfaceShaders() and in the same order: the namepath of the material
/// node the shader belongs to, or the shader's own namepath where the
/// document states a bare shader and no material. These are the names a
/// card, an App::ShaderProgram and a MaterialX <look> all use to say
/// which surface is worn (docs/MaterialStorage.md sec 17.13).
std::vector<std::string> surfaceNames(const mx::DocumentPtr &doc);

/// Which surface \a name picks out, as an index into surfaceShaders(),
/// or -1 when the document has none by that name. An empty name is the
/// first surface, which is what a single-material document has.
///
/// A name matches a surface by the material node's namepath or its bare
/// name, or by the shader node's -- one string is written down, and
/// whether it was copied out of a <look>, out of the property editor or
/// typed by hand should not decide whether it resolves.
int surfaceIndex(const mx::DocumentPtr &doc, const std::string &name);

/// The public interface of a document (docs/CyclesIntegration.md sec
/// 6.11): the inputs DECLARED by the node graphs that feed \a surface,
/// in document order. Reachability is what keeps the standard library
/// out of it -- importing the library puts hundreds of its own graphs
/// in the document, and only the ones the rendered surface actually
/// reaches are the document's interface. An input no node inside its
/// graph names is left out: it is declared but drives nothing.
std::vector<MaterialInput> publicInputs(const mx::DocumentPtr &doc,
                                        const mx::NodePtr &surface);

/// Override the document's public inputs with `u_<name>` parameter
/// values (the vec4-lane packing of RenderDebugConfig::UserParam), in
/// place. This is how the path tracer takes a parameter: it has no
/// uniforms, so a value becomes part of the document it interprets and
/// then a ValueNode in the shader graph, where the raster path binds a
/// uniform lane instead. A parameter naming no public input is ignored
/// -- the same list feeds both consumers and carries the reserved
/// entries (fc_state, ...) that neither reads here.
/// \a surface names which of the document's surfaces the interface is
/// read from, empty meaning its first.
void applyInputs(const mx::DocumentPtr &doc,
                 const std::vector<RenderDebugConfig::UserParam> &params,
                 const std::string &surface = {});

/// One input of a node read FLAT, through the `dot` nodes, graph outputs
/// and interface sockets the OpenPBR translation leaves between a
/// surface and what the document wrote: true with the constant it
/// resolves to, false for an input that is unstated, mapped, or fed by
/// a pattern graph. For the inputs whose VALUE decides a consumer's
/// shape -- a transmission depth that is or is not there -- which a
/// literal read of the (translated) surface node cannot answer, its
/// inputs being connections.
bool flatConstant(const mx::NodePtr &node, const char *name,
                  std::vector<float> &out);

/// The one surface a document renders, as an OpenPBR node. OpenPBR is
/// the canonical surface model, so a document stating any other one is
/// put through MaterialX's OWN translation graphs rather than a second
/// native mapping -- there is one shading model to be right about and
/// the rest is the library's business. Null when the document states no
/// surface, or states one the library cannot translate (UsdPreviewSurface
/// and the hair models have no translation TO OpenPBR), with the reason
/// in `error`. A translation is an approximation and says so in
/// `warnings`.
/// \a surface names which one, empty meaning the document's first.
mx::NodePtr openPbrSurface(const mx::DocumentPtr &doc,
                           const std::string &surface, std::string &error,
                           std::vector<std::string> &warnings);

/// Whether a nodedef has an implementation for the raster generator's
/// target -- what the graph editor asks before it lets a link be made.
bool hasImplementation(const mx::NodeDef &def);

}  // namespace Render::MaterialX

#endif  // RENDER_MATERIALX_SUPPORT_P_H
