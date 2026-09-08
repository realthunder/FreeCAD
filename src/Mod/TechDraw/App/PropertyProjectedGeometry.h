/***************************************************************************
 *   Copyright (c) 2026 The FreeCAD project                                *
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

#ifndef TECHDRAW_PROPERTYPROJECTEDGEOMETRY_H
#define TECHDRAW_PROPERTYPROJECTEDGEOMETRY_H

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

#include <App/Property.h>
#include <Base/Vector3D.h>
#include <Mod/TechDraw/TechDrawGlobal.h>


namespace TechDraw
{
class GeometryObject;

/*! What one projection produced, stored in the document so that reopening it
 *  does not have to run HLR, face finding and (for a section) the boolean cut
 *  again just to draw what was drawn before.  See docs/TechDrawStoredGeometry.md.
 *
 *  The geometry itself travels as a BRep file of its own -- the projected
 *  edges in their own order, then the edges of every face's wires, flattened
 *  -- because that is the only form that round trips a projection exactly:
 *  BaseGeom::baseFactory() reads an edge back into the same subclass with the
 *  same derived values it had when HLR made it.  Everything the edge does not
 *  carry -- the class of the edge, its visibility, the source element it was
 *  named from -- is an attribute in the XML beside it, one record per element
 *  in the order the view numbers them, which is what keeps Edge3 the same
 *  Edge3 after a reload.
 */
class TechDrawExport PropertyProjectedGeometry: public App::Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyProjectedGeometry();
    ~PropertyProjectedGeometry() override;

    //! take everything the geometry object holds now.  The centroid travels
    //! with it because it belongs to the same projection and nothing else
    //! stores it (DrawViewPart::getOriginalCentroid).
    void capture(const GeometryObject& source, const Base::Vector3d& centroid,
                 const TopoDS_Shape& cutFaces = TopoDS_Shape());
    void clear();
    bool isEmpty() const
    {
        return m_edges.empty() && m_vertices.empty();
    }
    int countEdges() const
    {
        return static_cast<int>(m_edges.size());
    }
    int countVertices() const
    {
        return static_cast<int>(m_vertices.size());
    }
    int countFaces() const
    {
        return static_cast<int>(m_faces.size());
    }
    //! the faces a section cut opened, in page coordinates.  Empty for
    //! every view that is not a section (DrawViewSection is the only thing
    //! that fills it, from m_sectionTopoDSFaces).
    const TopoDS_Shape& getCutFaces() const
    {
        return m_cutFaces;
    }
    Base::Vector3d getCentroid() const
    {
        return m_centroid;
    }

    //! rebuild the stored projection into target.  False when there is nothing
    //! stored, or when what was stored does not read back element for element
    //! -- the caller then projects the way it always did rather than draw a
    //! view whose numbering has shifted.
    bool restoreInto(GeometryObject& target) const;

    //! two snapshots are the same when they came from the same projection:
    //! the same curves, in the same numbers, off the same centroid
    bool isSame(const App::Property& other) const override;
    App::Property* Copy() const override;
    void Paste(const App::Property& from) override;
    unsigned int getMemSize() const override;

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;
    void SaveDocFile(Base::Writer& writer) const override;
    void RestoreDocFile(Base::Reader& reader) override;

    PyObject* getPyObject() override;
    void setPyObject(PyObject* value) override;

    const char* getEditorName() const override
    {
        return "";
    }

private:
    //! one projected edge, as BaseGeom carries it minus the curve itself
    struct EdgeRecord
    {
        int geomType {0};
        int classOfEdge {0};
        int ref3D {-1};
        int source {0};
        int sourceIndex {-1};
        bool hlrVisible {true};
        bool reversed {false};
        bool cosmetic {false};
        std::string cosmeticTag;
        std::string source3D;
        std::string hlrName;
    };

    //! one projected vertex.  It has no curve, so its point travels here too
    struct VertexRecord
    {
        Base::Vector3d point;
        int ref3D {-1};
        bool hlrVisible {true};
        bool cosmetic {false};
        bool center {false};
        bool reference {false};
        std::string cosmeticTag;
        std::string hlrName;
        std::vector<std::string> sources3D;
    };

    //! one projected face: how many edges each of its wires takes from the
    //! flattened face-edge compound, in order
    struct FaceRecord
    {
        std::vector<int> wireSizes;
        std::string hlrName;
        std::vector<std::string> sources3D;
    };

    //! the three compounds the BRep file holds, in this order
    TopoDS_Shape m_edgeShapes;
    TopoDS_Shape m_faceEdgeShapes;
    TopoDS_Shape m_cutFaces;

    std::vector<EdgeRecord> m_edges;
    std::vector<VertexRecord> m_vertices;
    std::vector<FaceRecord> m_faces;
    Base::Vector3d m_centroid;
};

}// namespace TechDraw

#endif// TECHDRAW_PROPERTYPROJECTEDGEOMETRY_H
