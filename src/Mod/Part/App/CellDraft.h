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

#ifndef PART_CELLDRAFT_H
#define PART_CELLDRAFT_H

#include <string>
#include <vector>

#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepTools_History.hxx>
#include <TopTools_DataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** A draft that can change topology (docs/NewDraft.md).
 *
 * Each added face turns about its line with the neutral plane onto a new
 * plane, as BRepOffsetAPI_DraftAngle turns it. Instead of moving the
 * vertices and edges of the shape, the space around the face is split by
 * the shape, the new plane and the neighbours' surfaces extended (a local
 * general fuse), and the cells are chosen: outside the region the face
 * sweeps nothing changes, inside it the new plane decides. A neighbour
 * grows or shrinks to meet the new plane, a new edge appears where one is
 * needed, and what cannot be done is refused with an error naming the face.
 *
 * A face is drafted with the faces coplanar with it and its tangent chain
 * (the walls and fillets tangent to it, and so on): planes turn, cylinders
 * and cones about the pull direction turn into cones, and the new surfaces,
 * joined along the chain's tangent edges, take the new plane's place. Past
 * the apex of a cone between two planes the planes meet in a ridge, and a
 * chain may close on itself at a sharp edge between two planes, where the
 * new planes meet.
 * Neighbours may be planar, elementary or other surfaces. Faces are drafted
 * one after the other, each on the result of the last; a face in the tangent
 * chain of one drafted before is drafted with it.
 */
class PartExport CellDraft: public BRepBuilderAPI_MakeShape
{
public:
    enum ErrorType
    {
        NoError,
        ParallelToNeutral,
        AngleTooSteep,
        TurnsOver,
        UnsupportedSurface,
        TangentNeighbour,
        NoClosure,
        FaceVanishes,
        SplitsSolid,
        NotASolid,
        Boolean,
        RefilletFails,
    };

    explicit CellDraft(const TopoDS_Shape& shape);

    /// Adds a face of the shape to draft. Nothing is checked until Build().
    void Add(const TopoDS_Face& face,
             const gp_Dir& direction,
             double angle,
             const gp_Pln& neutralPlane);

    /** Whether a drafted face stops at the body (default true): where the
     * face leans out, its neighbours grow to meet it, but not past a plane
     * of the body that has the whole body on its inner side.
     */
    void SetStopAtBody(bool stop)
    {
        myStopAtBody = stop;
    }
    bool StopAtBody() const
    {
        return myStopAtBody;
    }

    /** Whether a drafted face takes its tangent chain with it (default
     * true), as BRepOffsetAPI_DraftAngle does. Off, only the faces added
     * are drafted, with their coplanar pieces and the added faces tangent
     * to them. A fillet tangent to them that was not added -- a cylinder
     * between two planes -- is taken off, the faces drafted, and the fillet
     * made again at its radius on the edge where the two planes now meet
     * (draft before fillet); any other face tangent to them is refused
     * (TangentNeighbour; docs/NewDraft.md section 17).
     */
    void SetTangentPropagation(bool propagate)
    {
        myTangentPropagation = propagate;
    }
    bool TangentPropagation() const
    {
        return myTangentPropagation;
    }

    void Build(const Message_ProgressRange& theRange = Message_ProgressRange()) override;

    ErrorType Error() const
    {
        return myError;
    }
    /// The face (of the input shape, where it is one) the error is about.
    const TopoDS_Face& ErrorFace() const
    {
        return myErrorFace;
    }
    /// The neighbour of ErrorFace() the error is about, if any.
    const TopoDS_Face& ErrorNeighbour() const
    {
        return myErrorNeighbour;
    }
    /// A sentence saying what went wrong, without element names.
    const std::string& ErrorMessage() const
    {
        return myErrorMessage;
    }
    static const char* ErrorName(ErrorType error);

    const TopTools_ListOfShape& Modified(const TopoDS_Shape& shape) override;
    const TopTools_ListOfShape& Generated(const TopoDS_Shape& shape) override;
    bool IsDeleted(const TopoDS_Shape& shape) override;

    /// The history of the whole operation, from the input shape.
    const Handle(BRepTools_History) & History() const
    {
        return myHistory;
    }

    /** Checks the result of another draft of the same faces on the same
     * shape (BRepOffsetAPI_DraftAngle) for what the cell draft builds
     * otherwise (docs/NewDraft.md section 11): a face the draft made that
     * crosses another face of the body, and, with \a stopAtBody, the body
     * grown past a plane that bounds it. The faces the draft did not touch
     * are taken as clean. Returns what is wrong, or an empty string.
     */
    static std::string CheckDraft(const TopoDS_Shape& input,
                                  const TopoDS_Shape& result,
                                  const std::vector<TopoDS_Face>& faces,
                                  bool stopAtBody);

    /** The faces of \a shape that drafting \a faces would draft with them
     * only by tangent propagation: the faces of their tangent chains that
     * are neither among \a faces nor pieces of the surface of one. Empty
     * when turning propagation off changes nothing.
     */
    static std::vector<TopoDS_Face> TangentFaces(const TopoDS_Shape& shape,
                                                 const std::vector<TopoDS_Face>& faces);

    struct FaceDraft
    {
        TopoDS_Face face;
        gp_Dir direction;
        double angle;
        gp_Pln neutralPlane;
    };

private:
    void setError(ErrorType error,
                  const TopoDS_Shape& face,
                  const TopoDS_Shape& neighbour,
                  const std::string& message);
    TopoDS_Face inputFace(const TopoDS_Shape& face,
                          const Handle(BRepTools_History) & history) const;
    void uniqueList(const TopTools_ListOfShape& from, TopTools_ListOfShape& to);

    // a fillet taken off before the draft and made again after it
    struct Refillet
    {
        // of the input shape
        TopoDS_Face fillet;
        TopoDS_Face a;
        TopoDS_Face b;
        double radius;
    };
    bool takeOffFillets(TopoDS_Shape& cur,
                        const Handle(BRepTools_History) & total,
                        std::vector<Refillet>& refillets);
    bool makeFilletsAgain(TopoDS_Shape& cur,
                          const Handle(BRepTools_History) & total,
                          const std::vector<Refillet>& refillets);

private:
    TopoDS_Shape myInput;
    std::vector<FaceDraft> myFaces;
    bool myStopAtBody = true;
    bool myTangentPropagation = true;
    Handle(BRepTools_History) myHistory;
    TopTools_IndexedMapOfShape myResultMap;
    // a fillet of the input made again -> its new faces
    TopTools_DataMapOfShapeListOfShape myRefillet;
    ErrorType myError = NoError;
    TopoDS_Face myErrorFace;
    TopoDS_Face myErrorNeighbour;
    std::string myErrorMessage;
};

}  // namespace Part

#endif  // PART_CELLDRAFT_H
