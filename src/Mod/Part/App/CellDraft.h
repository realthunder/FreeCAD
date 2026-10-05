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
#include <TopTools_IndexedMapOfShape.hxx>
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
 * Phase 1: planar drafted faces; planar, elementary and other neighbours;
 * a neighbour tangent to the face is refused. Faces are drafted one after
 * the other, each on the result of the last.
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

private:
    TopoDS_Shape myInput;
    std::vector<FaceDraft> myFaces;
    bool myStopAtBody = true;
    Handle(BRepTools_History) myHistory;
    TopTools_IndexedMapOfShape myResultMap;
    ErrorType myError = NoError;
    TopoDS_Face myErrorFace;
    TopoDS_Face myErrorNeighbour;
    std::string myErrorMessage;
};

}  // namespace Part

#endif  // PART_CELLDRAFT_H
