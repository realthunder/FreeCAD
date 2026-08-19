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

#ifndef PART_SHAPECONGRUENCE_H
#define PART_SHAPECONGRUENCE_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** The shapes a save has already written, indexed so that a part which is
 * another part moved can be recognized as one.
 *
 * Content addressing shares parts whose bytes match, and sec 12.2's location
 * canonicalization takes care of a placement carried *beside* the geometry. An
 * exporter that multiplies the placement *into* the coordinates defeats both:
 * on the assembly this was measured against, 10872 distinct contents are 1446
 * distinct shapes, and the storage pays for all 10872
 * (docs/SharedShapeStorage.md sec 12.9).
 *
 * *** What this does NOT do is compare encodings. Two independently computed
 * copies of one part do not hold the same numbers -- not the same count of
 * them, even, since two projections of one curve can carry different poles and
 * knots -- while agreeing to 5e-12 in absolute terms, far inside
 * Precision::Confusion (sec 12.11). So the comparison is geometric throughout:
 * a cheap signature proposes, and a recovered motion verified point by point
 * decides.
 *
 * The correspondence is taken from the sub-shape maps in index order, which is
 * what makes this safe for more than geometry: the borrowing object keeps its
 * own element map, and those names resolve through sub-shape indices. Two
 * instances whose topology is ordered differently simply fail to match and are
 * written out in full.
 */
class PartExport CongruenceIndex
{
public:
    /// What was found: the caller's own key for the shape already stored, and
    /// the rigid motion that takes that shape to the one asked about.
    struct Match
    {
        int slot = 0;
        gp_Trsf motion;
    };

    /** Look for a shape congruent to \a shape among those added.
     *
     * Answers false unless a stored shape can be mapped onto \a shape by a
     * rigid motion that every vertex and every edge midpoint agrees with, in
     * sub-shape index order. A mirrored instance is not a rigid motion and is
     * refused rather than counted.
     */
    bool find(const TopoDS_Shape& shape, Match& match) const;

    /// Remember \a shape under the caller's \a slot.
    void add(const TopoDS_Shape& shape, int slot);

    void clear();

    std::size_t size() const
    {
        return _count;
    }

    /** The index for the save in progress.
     *
     * Keyed on the generation for the same reason the owner table is: the
     * count is process-wide, so every save of every document gets its own, and
     * an index cannot survive into a save it was not built for. It holds
     * shapes, which the document owns anyway for the length of a save; they
     * are dropped when the next save clears it.
     */
    static CongruenceIndex* forSave(std::uint64_t generation);

private:
    /** Area and volume, which no rigid motion changes.
     *
     * Computed once per shape and only when a bucket actually proposes
     * something, because they are the expensive part of this. They are what
     * stands between sharing and a shape whose corners agree while its faces
     * do not -- transformGeometry() produces exactly that, approximating an
     * exact cylinder with a spline that keeps every vertex and every edge
     * midpoint and moves the volume by a part in a thousand.
     */
    struct Invariants
    {
        double length = 0.;
        double area = 0.;
        double volume = 0.;
        bool lengthKnown = false;
        bool areaKnown = false;
        bool volumeKnown = false;
    };

    struct Entry
    {
        TopoDS_Shape shape;
        int slot = 0;
        mutable Invariants invariants;
    };

    /** The two invariants, each computed on the first question that needs it.
     *
     * Separately, because the volume is only ever asked for once the area has
     * already matched, and on the assembly of sec 12.12 the area alone settles
     * most of it: computing both together spent 9.4s answering questions
     * nobody asked. Each is still computed at most once per shape.
     */
    static double lengthOf(const TopoDS_Shape& shape, Invariants& cache);
    static double areaOf(const TopoDS_Shape& shape, Invariants& cache);
    static double volumeOf(const TopoDS_Shape& shape, Invariants& cache);

    /** What a save spent here, reported when the index is dropped.
     *
     * This costs a document's save real time -- 17s of it on the assembly of
     * sec 12.12 -- and the three parts of it answer to different fixes, so
     * the breakdown is worth carrying rather than re-deriving with a profiler
     * every time the question comes up. Written at log level `Congruence`.
     */
    struct Cost
    {
        std::size_t keys = 0;
        std::size_t finds = 0;
        std::size_t candidates = 0;
        std::size_t motions = 0;
        std::size_t matches = 0;
        double keySeconds = 0.;
        double invariantSeconds = 0.;
        double motionSeconds = 0.;
    };
    void report() const;

    std::unordered_map<std::size_t, std::vector<Entry>> _buckets;
    std::size_t _count = 0;
    mutable Cost _cost;
};

/** The rigid motion taking \a from onto \a to, if there is one.
 *
 * Exposed for its own sake: this is the whole of what makes sharing a moved
 * part sound, and it is what the tests are pointed at. The correspondence is
 * by sub-shape index, the motion is built from three spread vertices and then
 * checked against every vertex and every edge midpoint, and a reflection is
 * rejected on the determinant rather than being allowed to pass as a rotation.
 */
PartExport bool recoverShapeMotion(const TopoDS_Shape& from,
                                   const TopoDS_Shape& to,
                                   gp_Trsf& motion);

/** A key that a rigid motion cannot change.
 *
 * Sub-shape counts and a coarsely quantized spectrum of vertex distances from
 * the centroid. Coarse deliberately: a bucket that splits costs a sharing
 * opportunity, which is nothing, while the verification behind it is what
 * costs if it is wrong. Answers 0 for a shape too small or too degenerate to
 * be worth proposing.
 */
PartExport std::size_t shapeCongruenceKey(const TopoDS_Shape& shape);

}  // namespace Part

#endif  // PART_SHAPECONGRUENCE_H
