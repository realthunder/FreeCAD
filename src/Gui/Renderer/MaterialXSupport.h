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
    /// Names of the renderable surfaces, in document order: the
    /// namepath of each `surfacematerial` node, or of the bare surface
    /// shader node where the document states no material. These are the
    /// names a consumer says which surface it wears by, and what a
    /// MaterialX <look> assigns by (docs/MaterialStorage.md sec 17.13).
    /// One entry for a document describing a single material; a
    /// document carrying a whole asset's set states many.
    std::vector<std::string> materials;
    /// The one of those this inspection was about: the surface asked
    /// for, or the first when none was. Empty when nothing was valid.
    std::string material;
    /// The surface-shader node category of that material
    /// ("open_pbr_surface", "standard_surface", "gltf_pbr", ...).
    std::string surface;
    /// Image files the document names that are not on disk where it
    /// says they are. Reported as warnings: the material still
    /// renders, with those maps missing.
    std::vector<std::string> missingImages;
    /// Absolute paths of the image files the document names AND that
    /// are there, deduplicated, in document order. This is what a
    /// consumer has to have in hand before it can draw the material:
    /// the raster path loads these and binds them to the samplers
    /// generate() reports, joining the two lists on the path itself
    /// (docs/CyclesIntegration.md sec 6.12). The path tracer needs
    /// none of it -- it opens the files itself.
    std::vector<std::string> images;
    /// The document's public interface, in document order: what the
    /// graph feeding the first material declares as its inputs. Empty
    /// when the document declares none, which is what a bare surface
    /// node with stated values does.
    std::vector<MaterialInput> inputs;

    /// What the surface states for its transmission, read FLAT -- as a
    /// consumer that samples nothing per fragment has to read it. The
    /// raster path has no transmission lobe (fc_openpbr.sh): a
    /// transmissive surface is drawn as a glass BODY by the engine's
    /// glass pass, which takes one colour, one IOR, one density and one
    /// roughness per draw, and this is where those come from
    /// (docs/MaterialStorage.md sec 17.21). Read off the OpenPBR
    /// surface AFTER translation, so a standard_surface `transmission`
    /// and an OpenPBR `transmission_weight` answer alike.
    struct Transmission {
        /// The surface is a glass body: `transmission_weight` is stated
        /// as a constant of one half or more. A mapped weight cannot be
        /// read flat and leaves this false (reported as a warning): the
        /// surface then draws opaque, as it did before there was a route.
        bool glass = false;
        float weight = 0.0f;
        /// `transmission_color`, linear as the document states it. A
        /// mapped colour reads white (a warning says so).
        float color[3] = {1.0f, 1.0f, 1.0f};
        /// `transmission_depth` in scene units. OpenPBR gives the colour
        /// two meanings by it: over a positive depth it is what survives
        /// that path length (absorption at density 1 / depth), and at
        /// zero it tints the transmitted light ONCE at the surface.
        float depth = 0.0f;
        /// `specular_ior`; 1.5 unstated.
        float ior = 1.5f;
        /// `specular_roughness` when it is a constant; the model's
        /// default when it is not.
        float roughness = 0.3f;
        /// The image that feeds `specular_roughness` directly, resolved,
        /// when a map does rather than a constant (empty otherwise). A
        /// flat consumer may stand the map's mean in for the constant it
        /// cannot have -- closer to the document than the default is.
        std::string roughnessImage;
    };
    Transmission transmission;
};

/// Parse and validate a MaterialX document. `sourcePath` is the file
/// the text came from, when it came from one: a material states its
/// images relative to its own document, so that is what they resolve
/// against. `surface` names which of the document's surfaces the answer
/// is about, empty meaning its first; a name the document does not
/// state comes back invalid, listing the names it does state. Never
/// throws: a document that will not parse comes back invalid with the
/// parser's message.
RendererExport DocumentInfo inspect(const std::string &xml,
                                    const std::string &sourcePath = {},
                                    const std::string &surface = {});

/// The document with its public inputs overridden by `u_<name>`
/// parameter values (the vec4-lane packing of
/// RenderDebugConfig::UserParam), serialized again: what an editor of
/// the text shows when the values live in `Param_*` properties beside
/// it. A parameter naming no public input is ignored; a text that does
/// not parse comes back as it was.
/// \a surface names which of the document's surfaces the interface is
/// read from, empty meaning its first.
/// The public interface of a document with the values it states now
/// (the `inputs` of inspect(), without the validation): what an editor
/// that moves a value needs per change, and cheap -- the text is parsed
/// against the shared data library, not validated or generated.
/// \a surface names which of the document's surfaces the interface is
/// read from, empty meaning its first. Empty on a text that does not
/// parse.
RendererExport std::vector<MaterialInput> publicInputs(const std::string &xml,
                                                      const std::string &surface = {});

RendererExport std::string applyInputsToDocument(
        const std::string &xml,
        const std::vector<RenderDebugConfig::UserParam> &params,
        const std::string &surface = {});

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

    /// One layer of the image array the generated code samples.
    ///
    /// Every image the document names is a layer of ONE array texture
    /// rather than a sampler of its own (docs/CyclesIntegration.md sec
    /// 6.12): the mesh shader leaves too few units free for a material
    /// with an ordinary set of maps. Two nodes naming the same file
    /// share a layer, so this is a list of the DISTINCT images, in the
    /// order the engine must stack them.
    struct Image {
        /// Which layer of the array. Layer order is what the engine
        /// builds to; the generated code names these numbers, so the
        /// two sides agree without matching anything.
        int layer = 0;
        /// Absolute path of the file the document names for it. The
        /// pixels are not loaded here: this is the join key against
        /// the images the capture side loaded (DocumentInfo::images).
        std::string path;
        /// The colour space the document states for the file ("srgb_texture",
        /// "lin_rec709", or empty where it states none). What the shader
        /// does about it is already in `source`; this is for reporting.
        std::string colorSpace;
        /// The generated variable of the first image node that named
        /// the file. For reporting only -- the binding is by layer.
        std::string name;
    };
    /// The layers `source` samples, in layer order. Empty for a
    /// document that names no image.
    std::vector<Image> images;
    /// The sampler2DArray `source` declares for those layers, and the
    /// texture unit it claims. Empty and 0 when there are no images,
    /// so an imageless document leaves the unit free.
    std::string imageSampler;
    int imageUnit = 0;
};

/// Generate the raster material-inputs function for a document.
/// `sourcePath` resolves its relative file references and `surface`
/// names which surface is generated, both as in inspect(). Never
/// throws: a document that will not generate comes back invalid with
/// the reason.
RendererExport GeneratedMaterial generate(const std::string &xml,
                                          const std::string &sourcePath = {},
                                          const std::string &surface = {});

/// One image file a document refers to.
///
/// `name` is what the DOCUMENT calls the file, with the inherited
/// `fileprefix` chain applied and nothing else: it is a function of the
/// document text alone, so it is the same string on every machine that
/// reads that text. That is what makes it usable as the key of a stored
/// set of files (App::PropertyFileIncludedList) -- the resolved path
/// below is not, being an answer about one machine's disk.
struct ImageReference {
    /// What the document calls the file. The stable key.
    std::string name;
    /// Where that resolves on this machine, empty when it resolves
    /// nowhere. A document carrying its images has every name in the
    /// stored set and needs none of these.
    std::string path;
};

/// The image files a document refers to, in document order,
/// deduplicated by name.
///
/// Parses the text and nothing else -- no data library, no validation --
/// because this answers a question about what the document SAYS, and is
/// asked on every source change. `sourcePath` resolves relative names as
/// it does in inspect(); pass none and only absolute names and data
/// library names resolve. Never throws: a document that will not parse
/// refers to nothing.
RendererExport std::vector<ImageReference> imageReferences(const std::string &xml,
                                                           const std::string &sourcePath = {});

/// The document with each image reference named in \a files replaced by
/// the path given for it.
///
/// This is how a document travels: its images are stored as blobs and
/// the text handed to the consumers names them where they actually are,
/// so every consumer downstream goes on opening files and none of them
/// has to learn what a blob is. The `fileprefix` attributes are dropped
/// along with the names they qualified -- an absolute path must not be
/// prefixed again by whoever resolves the result.
///
/// A name the document does not refer to is ignored, and a reference the
/// map does not name is left as the document wrote it. Never throws: a
/// document that will not parse comes back unchanged.
RendererExport std::string substituteImages(const std::string &xml,
                                            const std::vector<ImageReference> &files);

/// One material assignment of a <look>: a surface of the document, and
/// the geometry that wears it.
struct LookAssignment {
    /// Namepath of the assigned `surfacematerial` node, exactly as the
    /// look writes it. This is what a card names as its surface
    /// (docs/MaterialStorage.md sec 17.13), so it goes straight through.
    std::string material;
    /// The geometry the look assigns it to: a space-separated list of
    /// MaterialX geometry name paths, wildcards and all, as written.
    /// A consumer decides what its own objects are called.
    std::string geom;
    /// Where that came from: the assignment's own `geom`, or the name of
    /// the `collection` it names, whose include geometry `geom` above
    /// then holds. For reporting.
    std::string collection;
};

/// A `<look>`: the document's statement of which of its materials each
/// piece of an asset wears.
///
/// A material document says how a surface looks; a look says WHO looks
/// that way, which is the other half an importer needs and the only
/// place the two are connected. MaterialX's chess set is one document
/// with fifteen surfaces and one look assigning each to a mesh of
/// chess_set.glb by name.
struct Look {
    /// The look's own name, as the document writes it.
    std::string name;
    /// Its assignments, in document order.
    std::vector<LookAssignment> assignments;
};

/// The looks a document states, in document order.
///
/// Parses the text and nothing else -- no data library, no validation --
/// like imageReferences(): a look is structure, not shading, and this is
/// asked before anything is rendered. `sourcePath` is the file the text
/// came from, when it came from one. A collection an assignment names is
/// resolved to its include geometry; anything it excludes is left to the
/// caller, since MaterialX's exclusion is by geometry path and a
/// consumer's names are its own. Never throws: a document that will not
/// parse states no look.
RendererExport std::vector<Look> looks(const std::string &xml,
                                       const std::string &sourcePath = {});

/// Absolute path of the standard data library shipped beside the
/// binary, the directory holding stdlib/, pbrlib/, bxdf/ and the rest.
/// Empty when the library is not built.
RendererExport std::string dataLibraryPath();

}  // namespace Render::MaterialX

#endif  // RENDER_MATERIALX_SUPPORT_H
