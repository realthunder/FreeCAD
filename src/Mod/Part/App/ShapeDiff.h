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

#ifndef PART_SHAPEDIFF_H
#define PART_SHAPEDIFF_H

#include <set>
#include <string>
#include <vector>

#include <Message_ProgressRange.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <Mod/Part/PartGlobal.h>

namespace App
{
class Document;
struct CapturedValue;
struct ValueSummary;
}

namespace Part
{

class TopoShape;

/** What became of each face, edge and vertex between two states of a shape.
 *
 * Two states of one object's shape -- before and after an edit, or as two
 * branches have it -- are compared element by element: which element of the
 * one is which of the other, and whether it is as it was. It is data, with
 * nothing of the view in it (docs/TransactionLog.md sec 32).
 *
 * An element of A is paired with one of B in stages, each working on what the
 * one before left (sec 32.5 has what each is worth, measured):
 *
 *  - the whole shapes are one, or one is the other moved: by index;
 *  - one name in both;
 *  - one name once the steps that only one of the shapes has are taken out of
 *    it -- a feature put in or taken out early renames every element that
 *    passes it, and this is what sees through that;
 *  - one geometry, as a reference that lost its name is found again
 *    (TopoShape::searchSubShape);
 *  - the same paired faces around it.
 *
 * How a pair was found and whether it changed are said apart: a face found by
 * its name may have changed, and one found by its geometry has a boundary
 * that did not.
 */
class PartExport ShapeDiff
{
public:
    /// How a pair was found
    enum class By
    {
        None,     ///< not paired: added or removed
        Shape,    ///< the whole shapes are one, or one moved: by index
        Name,     ///< one name
        Steps,    ///< one name without the steps only one side has
        Geometry, ///< one geometry
        Around,   ///< the same paired faces around
    };

    /// What became of an element
    enum class What
    {
        Same,    ///< as it was, where it was
        Moved,   ///< as it was, elsewhere
        Changed, ///< another boundary or another size
        Added,   ///< in B only
        Removed, ///< in A only
    };

    struct Element
    {
        int a = 0; ///< its index in A, as in "Face3"; 0 where it is added
        int b = 0; ///< its index in B; 0 where it is removed
        By by = By::None;
        What what = What::Same;
    };

    struct Options
    {
        bool faces = true;
        bool edges = false;
        bool vertices = false;
        /// Pair by geometry what the names left. Each element left asks the
        /// whole of the other shape.
        bool geometry = true;
        /// Pair by the paired faces around what all else left
        bool around = true;
        /// How far two points may be apart and be one
        double tolerance = 1e-7;
    };

    /// What it took, in seconds, by what it was spent on
    struct Cost
    {
        double read = 0.;     ///< the elements and their names
        double whole = 0.;    ///< whether the whole is one, or moved
        double names = 0.;    ///< pairing by name
        double steps = 0.;    ///< spelling names out and pairing without steps
        double geometry = 0.; ///< pairing by geometry
        double around = 0.;   ///< pairing by the faces around
        double what = 0.;     ///< whether each pair is as it was
    };

    ShapeDiff(const TopoShape& a, const TopoShape& b, const Options& options);
    ShapeDiff(const TopoShape& a, const TopoShape& b);

    /// The two are one shape in one place
    bool same() const
    {
        return _same;
    }
    /// B is A moved as a whole, by motion()
    bool moved() const
    {
        return _moved;
    }
    const gp_Trsf& motion() const
    {
        return _motion;
    }
    const Cost& cost() const
    {
        return _cost;
    }

    /** The elements of one type, TopAbs_FACE, TopAbs_EDGE or TopAbs_VERTEX:
     * the pairs and what A alone has in A's order, then what B alone has.
     * Empty for a type that was not asked for.
     */
    const std::vector<Element>& elements(TopAbs_ShapeEnum type) const;

    /// How many elements of a type are 'what'
    int count(TopAbs_ShapeEnum type, What what) const;
    /// How many elements of a type were paired 'by'
    int count(TopAbs_ShapeEnum type, By by) const;

    static const char* name(By by);
    static const char* name(What what);

    /** A spelled-out element name with the steps of the objects in \a tags
     * taken out of it.
     *
     * A name is a root and then steps, each "operation;:Htag,T": the element
     * as it was in the shape of the object 'tag', and before it what was done
     * to it next -- by the object of the step after. So an object goes out as
     * its own tag and the operation written before the step before, whose tag
     * stays with the operation that was the object's own. An element whose
     * first step is the object's was made by it and keeps its name. The names an
     * operation lists, "(a|b)", are names and are treated alike, and sorted.
     * The lengths in a step count hashed text and are dropped.
     *
     * Public for its tests.
     */
    static std::string nameWithoutSteps(const std::string& spelled, const std::set<long>& tags);

    /// The material two states of a solid do not share, and what they do
    struct Material
    {
        TopoDS_Shape removed; ///< in A and not in B: solids; null where none
        TopoDS_Shape added;   ///< in B and not in A
        TopoDS_Shape common;  ///< in both
        double removedVolume = 0.;
        double addedVolume = 0.;
        double commonVolume = 0.;
        double seconds = 0.; ///< what it took
    };

    /** Two states of a solid taken apart by their material
     * (docs/TransactionLog.md sec 32.14): one general fuse of the two, and
     * of its cells those that only A has, those that only B has, and those
     * of both. Where the faces of a shape say which of them changed, this
     * says where: a box made taller is four faces changed and one moved,
     * and one slab added.
     *
     * Neither shape is touched. A piece smaller than a billionth of the
     * larger of the two is what two faces that should be one left between
     * them, and is dropped. \a fuzzy is the fuse's own tolerance, 0 for
     * what the shapes carry.
     *
     * The fuse is all of what this takes, and on two heavy solids it is
     * long (sec 32.16). \a progress is what it is told to stop by, from
     * another thread: one whose indicator says UserBreak().
     *
     * False where either has no solid, the fuse fails, or it was stopped.
     */
    static bool material(const TopoShape& a, const TopoShape& b, Material& out,
                         double fuzzy = 0.,
                         const Message_ProgressRange& progress = Message_ProgressRange());

    /** A shape value the transaction log of \a doc holds, by its ref, as a
     * shape with its element names (docs/TransactionLog.md sec 32.7): what an
     * object's shape was at a row, or is on another branch, with nothing of
     * the document touched. \a tag is the object's id, which the names of its
     * own elements are read by; 0 where it is not known.
     *
     * The geometry is the file the value names or the one beside it, parsed
     * as a restore parses it, so a value the document also holds comes back
     * as the same TShape. False where the log has no such value -- a shape is
     * a derived value and the log may have let it go -- or it is no shape.
     */
    static bool valueShape(const App::Document& doc, const std::string& ref, long tag,
                           TopoShape& shape);
    /// The same of a value already read; false where it is no shape.
    static bool valueShape(const App::Document& doc, const App::CapturedValue& value, long tag,
                           TopoShape& shape);
    /** What \a shape is, in a line (docs/TransactionLog.md sec 32.35): its
     * kind, how many solids, shells, faces, edges and vertices it has, its
     * volume, area or length, and the size of its bounding box -- for a value
     * of the log, whose text is the start of the geometry's file.
     */
    static void summary(const TopoShape& shape, App::ValueSummary& out);

private:
    bool _same = false;
    bool _moved = false;
    gp_Trsf _motion;
    Cost _cost;
    std::vector<Element> _elements[3];
};

}  // namespace Part

#endif  // PART_SHAPEDIFF_H
