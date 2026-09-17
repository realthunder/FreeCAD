/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                  *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ***************************************************************************/

#ifndef PART_HLRPROJECTOR_H
#define PART_HLRPROJECTOR_H

#include <memory>
#include <vector>

#include <gp_Ax3.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <TopoDS_Shape.hxx>

#include "TopoShape.h"

namespace Part {

/** Hidden line projection that remembers where every edge came from
 *
 * HLRBRep_HLRToShape knows the source element of each edge it emits and
 * throws it away: its public API returns bare compounds. This class runs
 * the same OCCT algorithm (HLRBRep_Algo) and replays HLRToShape's traversal
 * in the same order, keeping the source with each edge: an edge of the
 * input for an ordinary projected edge, the face it belongs to for a
 * silhouette (outline) or an isoparametric line the algorithm invented.
 * The compound it returns carries an element map naming each projected
 * edge from its source, with a fragment index for a source that is broken
 * into several pieces by hiding, so a caller can track a projected edge
 * across recomputes instead of trusting its position in the output.
 *
 * Nothing here is a heuristic and there is no second HLR pass: the
 * correspondence is the algorithm's own (docs/TopoNamingEnhance.md 3.5).
 *
 * Usage:
 * @code
 *   Part::HLRProjector proj;
 *   proj.add(shape);
 *   proj.build(viewAxis);
 *   Part::TopoShape edges = proj.edges();
 * @endcode
 *
 * The output edges lie on the XY plane of the view axis, in view
 * coordinates (as HLRToShape's 2D compounds), with 3D curves built.
 */
class PartExport HLRProjector {
public:
    /// HLRToShape's edge categories, numbered as its InternalCompound does
    enum EdgeType {
        Iso = 1,     ///< isoparametric line
        Outline = 2, ///< silhouette, an edge the algorithm invented on a face
        Smooth = 3,  ///< edge of G1 continuity between two faces (Rg1Line)
        Seam = 4,    ///< edge of CN continuity, a seam on one face (RgNLine)
        Hard = 5,    ///< sharp edge (C0)
    };

    /// Bit masks over EdgeType for selecting categories
    enum EdgeTypeMask : unsigned {
        IsoMask = 1u << Iso,
        OutlineMask = 1u << Outline,
        SmoothMask = 1u << Smooth,
        SeamMask = 1u << Seam,
        HardMask = 1u << Hard,
        AllTypes = IsoMask | OutlineMask | SmoothMask | SeamMask | HardMask,
        /// what a drawing shows without seams and iso lines
        DefaultTypes = OutlineMask | SmoothMask | HardMask,
    };

    /// Where a projected edge came from
    struct Edge {
        /// the source: an edge of the input, or the face of a silhouette or
        /// iso line; null when the input did not contain it
        TopoDS_Shape source;
        /// the input shape (as passed to add()) the source belongs to, or -1
        int input = -1;
        EdgeType type = Hard;
        bool visible = true;
        /// 1-based position among the pieces this source produced, in
        /// traversal order (the pieces of one hidden-line split come in
        /// parameter order along the source)
        int fragment = 1;
    };

    /// The parameters of build() and edges(), shared with TopoShape::makEHLR
    using Params = TopoShape::HLRParams;

    HLRProjector();
    ~HLRProjector();
    HLRProjector(const HLRProjector &) = delete;
    HLRProjector &operator=(const HLRProjector &) = delete;

    /// Add an input shape. Its element map, if any, names the output.
    void add(const TopoShape &shape);

    /** Run the projection
     *
     * @param view: the view axis; the projection looks along its Z, and the
     *              output lies on its XY plane in its coordinates
     */
    void build(const gp_Ax3 &view, const Params &params = Params());

    /// Run the projection with an OCCT projector
    void build(const HLRAlgo_Projector &projector, const Params &params = Params());

    /// Whether build() has run
    bool isDone() const;

    /** The projected edges
     *
     * @param params: types (EdgeTypeMask bits), visible and hidden select
     *                the pieces; the build parameters are ignored here
     * @param op: op code for the element names, OpCodes::HLR by default
     * @param hasher: optional hasher for the element names
     *
     * @return a compound of the edges, in HLRToShape's order (visible then
     *         hidden, each hard, smooth, seam, outline, iso), with an
     *         element map when an input carries one. Every call hands out
     *         the same edge shapes, so info() answers for any of them.
     */
    TopoShape edges(const Params &params = Params(), const char *op = nullptr,
                    App::StringHasherRef hasher = App::StringHasherRef()) const;

    /// Where an edge produced by build() came from, null if unknown
    const Edge *info(const TopoDS_Shape &edge) const;

    /// The underlying algorithm, for what this class does not expose
    const Handle(HLRBRep_Algo) &algo() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Part

#endif // PART_HLRPROJECTOR_H
