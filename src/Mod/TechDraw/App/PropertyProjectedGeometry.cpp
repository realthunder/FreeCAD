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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Part/App/TopoShape.h>

#include "PropertyProjectedGeometry.h"
#include "Geometry.h"
#include "GeometryObject.h"


using namespace TechDraw;

TYPESYSTEM_SOURCE(TechDraw::PropertyProjectedGeometry, App::Property)

//! the schema this build writes.  A file written by a later one is not read
//! back; the view projects instead, which is what every document did before
//! any of this existed.
static const int currentFormat = 1;

PropertyProjectedGeometry::PropertyProjectedGeometry() = default;

PropertyProjectedGeometry::~PropertyProjectedGeometry() = default;

void PropertyProjectedGeometry::clear()
{
    aboutToSetValue();
    m_edgeShapes = TopoDS_Shape();
    m_faceEdgeShapes = TopoDS_Shape();
    m_cutFaces = TopoDS_Shape();
    m_edges.clear();
    m_vertices.clear();
    m_faces.clear();
    m_centroid = Base::Vector3d();
    hasSetValue();
}

void PropertyProjectedGeometry::capture(const GeometryObject& source,
                                        const Base::Vector3d& centroid,
                                        const TopoDS_Shape& cutFaces)
{
    // Every projected edge has to have its curve, because the records are
    // matched to the curves by position on the way back in: one missing
    // curve would renumber everything after it.  Nothing is stored instead.
    for (const auto& geom : source.getEdgeGeometry()) {
        if (geom->getOCCEdge().IsNull()) {
            Base::Console().Log("TechDraw - a projected edge has no curve, storing nothing\n");
            clear();
            return;
        }
    }

    aboutToSetValue();

    m_edges.clear();
    m_vertices.clear();
    m_faces.clear();
    m_centroid = centroid;

    BRep_Builder builder;
    TopoDS_Compound edgeShapes;
    builder.MakeCompound(edgeShapes);
    TopoDS_Compound faceEdgeShapes;
    builder.MakeCompound(faceEdgeShapes);

    for (const auto& geom : source.getEdgeGeometry()) {
        EdgeRecord record;
        record.geomType = static_cast<int>(geom->getGeomType());
        record.classOfEdge = static_cast<int>(geom->getClassOfEdge());
        record.ref3D = geom->getRef3d();
        record.source = geom->source();
        record.sourceIndex = geom->sourceIndex();
        record.hlrVisible = geom->getHlrVisible();
        record.reversed = geom->getReversed();
        record.cosmetic = geom->getCosmetic();
        record.cosmeticTag = geom->getCosmeticTag();
        record.source3D = geom->getSource3d();
        record.hlrName = geom->getHlrName();
        m_edges.push_back(record);
        builder.Add(edgeShapes, geom->getOCCEdge());
    }

    for (const auto& vertex : source.getVertexGeometry()) {
        VertexRecord record;
        record.point = vertex->point();
        record.ref3D = vertex->getRef3d();
        record.hlrVisible = vertex->getHlrVisible();
        record.cosmetic = vertex->getCosmetic();
        record.center = vertex->isCenter();
        record.reference = vertex->isReference();
        record.cosmeticTag = vertex->getCosmeticTag();
        record.hlrName = vertex->getHlrName();
        record.sources3D = vertex->getSources3d();
        m_vertices.push_back(record);
    }

    for (const auto& face : source.getFaceGeometry()) {
        FaceRecord record;
        record.hlrName = face->getHlrName();
        record.sources3D = face->getSources3d();
        for (const auto& wire : face->wires) {
            int count {0};
            for (const auto& geom : wire->geoms) {
                if (geom->getOCCEdge().IsNull()) {
                    continue;
                }
                builder.Add(faceEdgeShapes, geom->getOCCEdge());
                count++;
            }
            record.wireSizes.push_back(count);
        }
        m_faces.push_back(record);
    }

    m_edgeShapes = edgeShapes;
    m_faceEdgeShapes = faceEdgeShapes;
    m_cutFaces = cutFaces;

    hasSetValue();
}

bool PropertyProjectedGeometry::restoreInto(GeometryObject& target) const
{
    if (isEmpty()) {
        return false;
    }

    // the edges, in the order the view numbers them
    std::vector<TopoDS_Edge> edgeShapes;
    if (!m_edgeShapes.IsNull()) {
        for (TopoDS_Iterator it(m_edgeShapes); it.More(); it.Next()) {
            if (it.Value().IsNull() || it.Value().ShapeType() != TopAbs_EDGE) {
                Base::Console().Warning("TechDraw - stored geometry holds something that is not an edge\n");
                return false;
            }
            edgeShapes.push_back(TopoDS::Edge(it.Value()));
        }
    }
    if (edgeShapes.size() != m_edges.size()) {
        Base::Console().Warning("TechDraw - stored geometry has %d edges for %d records\n",
                                static_cast<int>(edgeShapes.size()),
                                static_cast<int>(m_edges.size()));
        return false;
    }

    BaseGeomPtrVector edges;
    edges.reserve(m_edges.size());
    for (size_t i = 0; i < m_edges.size(); i++) {
        BaseGeomPtr geom = BaseGeom::baseFactory(edgeShapes.at(i));
        if (!geom) {
            // an edge the factory will not take back is an edge the numbering
            // would lose, so nothing is restored and the view projects instead
            Base::Console().Warning("TechDraw - stored edge %d does not read back\n",
                                    static_cast<int>(i));
            return false;
        }
        const EdgeRecord& record = m_edges.at(i);
        geom->setClassOfEdge(static_cast<edgeClass>(record.classOfEdge));
        geom->setHlrVisible(record.hlrVisible);
        geom->setReversed(record.reversed);
        geom->setRef3d(record.ref3D);
        geom->source(record.source);
        geom->sourceIndex(record.sourceIndex);
        geom->setCosmetic(record.cosmetic);
        geom->setCosmeticTag(record.cosmeticTag);
        geom->setSource3d(record.source3D);
        geom->setHlrName(record.hlrName);
        edges.push_back(geom);
    }

    std::vector<VertexPtr> vertices;
    vertices.reserve(m_vertices.size());
    for (const auto& record : m_vertices) {
        VertexPtr vertex = std::make_shared<TechDraw::Vertex>(record.point);
        vertex->setRef3d(record.ref3D);
        vertex->setHlrVisible(record.hlrVisible);
        vertex->setCosmetic(record.cosmetic);
        vertex->isCenter(record.center);
        vertex->isReference(record.reference);
        vertex->setCosmeticTag(record.cosmeticTag);
        vertex->setHlrName(record.hlrName);
        vertex->setSources3d(record.sources3D);
        vertices.push_back(vertex);
    }

    // the face wires' edges, flattened in face order then wire order
    std::vector<TopoDS_Edge> faceEdgeShapes;
    if (!m_faceEdgeShapes.IsNull()) {
        for (TopoDS_Iterator it(m_faceEdgeShapes); it.More(); it.Next()) {
            if (it.Value().IsNull() || it.Value().ShapeType() != TopAbs_EDGE) {
                continue;
            }
            faceEdgeShapes.push_back(TopoDS::Edge(it.Value()));
        }
    }

    std::vector<FacePtr> faces;
    size_t next {0};
    for (const auto& record : m_faces) {
        FacePtr face = std::make_shared<TechDraw::Face>();
        for (int wireSize : record.wireSizes) {
            auto* wire = new TechDraw::Wire();
            for (int i = 0; i < wireSize; i++) {
                if (next >= faceEdgeShapes.size()) {
                    delete wire;
                    Base::Console().Warning("TechDraw - stored faces are short of edges\n");
                    return false;
                }
                BaseGeomPtr geom = BaseGeom::baseFactory(faceEdgeShapes.at(next));
                next++;
                if (geom) {
                    wire->geoms.push_back(geom);
                }
            }
            face->wires.push_back(wire);
        }
        face->setHlrName(record.hlrName);
        face->setSources3d(record.sources3D);
        faces.push_back(face);
    }

    target.clear();
    target.setEdgeGeometry(edges);
    target.setVertexGeometry(vertices);
    target.setFaces(std::move(faces));
    return true;
}

bool PropertyProjectedGeometry::isSame(const App::Property& other) const
{
    if (&other == this) {
        return true;
    }
    const auto* twin = dynamic_cast<const PropertyProjectedGeometry*>(&other);
    if (!twin) {
        return false;
    }
    // the curves are the test.  A snapshot is only ever built from a
    // projection that has just finished, so two that hold the same shapes in
    // the same numbers hold the same attributes with them.
    return m_edges.size() == twin->m_edges.size()
        && m_vertices.size() == twin->m_vertices.size()
        && m_faces.size() == twin->m_faces.size() && m_centroid == twin->m_centroid
        && m_edgeShapes.IsSame(twin->m_edgeShapes)
        && m_faceEdgeShapes.IsSame(twin->m_faceEdgeShapes)
        && m_cutFaces.IsSame(twin->m_cutFaces);
}

App::Property* PropertyProjectedGeometry::Copy() const
{
    auto* copy = new PropertyProjectedGeometry();
    copy->m_edgeShapes = m_edgeShapes;
    copy->m_faceEdgeShapes = m_faceEdgeShapes;
    copy->m_cutFaces = m_cutFaces;
    copy->m_edges = m_edges;
    copy->m_vertices = m_vertices;
    copy->m_faces = m_faces;
    copy->m_centroid = m_centroid;
    return copy;
}

void PropertyProjectedGeometry::Paste(const App::Property& from)
{
    const auto& other = dynamic_cast<const PropertyProjectedGeometry&>(from);
    aboutToSetValue();
    m_edgeShapes = other.m_edgeShapes;
    m_faceEdgeShapes = other.m_faceEdgeShapes;
    m_cutFaces = other.m_cutFaces;
    m_edges = other.m_edges;
    m_vertices = other.m_vertices;
    m_faces = other.m_faces;
    m_centroid = other.m_centroid;
    hasSetValue();
}

unsigned int PropertyProjectedGeometry::getMemSize() const
{
    auto size = static_cast<unsigned int>(sizeof(*this));
    for (const auto& record : m_edges) {
        size += static_cast<unsigned int>(sizeof(record) + record.cosmeticTag.size()
                                          + record.source3D.size() + record.hlrName.size());
    }
    for (const auto& record : m_vertices) {
        size += static_cast<unsigned int>(sizeof(record) + record.cosmeticTag.size()
                                          + record.hlrName.size());
        for (const auto& name : record.sources3D) {
            size += static_cast<unsigned int>(name.size());
        }
    }
    for (const auto& record : m_faces) {
        size += static_cast<unsigned int>(sizeof(record) + record.wireSizes.size() * sizeof(int)
                                          + record.hlrName.size());
        for (const auto& name : record.sources3D) {
            size += static_cast<unsigned int>(name.size());
        }
    }
    return size;
}

void PropertyProjectedGeometry::Save(Base::Writer& writer) const
{
    writer.Stream() << writer.ind() << "<ProjectedGeometry format=\"" << currentFormat
                    << "\" edges=\"" << m_edges.size() << "\" vertices=\"" << m_vertices.size()
                    << "\" faces=\"" << m_faces.size() << "\" cx=\"" << m_centroid.x << "\" cy=\""
                    << m_centroid.y << "\" cz=\"" << m_centroid.z << "\"";
    if (isEmpty()) {
        writer.Stream() << "/>\n";
        return;
    }
    writer.Stream() << " file=\"" << writer.addFile(getFileName(".brp"), this) << "\">\n";
    writer.incInd();

    for (const auto& record : m_edges) {
        writer.Stream() << writer.ind() << "<Edge gt=\"" << record.geomType << "\" ec=\""
                        << record.classOfEdge << "\" vis=\"" << (record.hlrVisible ? 1 : 0)
                        << "\" rev=\"" << (record.reversed ? 1 : 0) << "\" ref=\"" << record.ref3D
                        << "\" cos=\"" << (record.cosmetic ? 1 : 0) << "\" src=\"" << record.source
                        << "\" si=\"" << record.sourceIndex << "\" tag=\""
                        << encodeAttribute(record.cosmeticTag) << "\" s3d=\""
                        << encodeAttribute(record.source3D) << "\" name=\""
                        << encodeAttribute(record.hlrName) << "\"/>\n";
    }

    for (const auto& record : m_vertices) {
        writer.Stream() << writer.ind() << "<Vertex x=\"" << record.point.x << "\" y=\""
                        << record.point.y << "\" vis=\"" << (record.hlrVisible ? 1 : 0)
                        << "\" ref=\"" << record.ref3D << "\" cos=\"" << (record.cosmetic ? 1 : 0)
                        << "\" ctr=\"" << (record.center ? 1 : 0) << "\" rfv=\""
                        << (record.reference ? 1 : 0) << "\" tag=\""
                        << encodeAttribute(record.cosmeticTag) << "\" name=\""
                        << encodeAttribute(record.hlrName) << "\" sources=\""
                        << record.sources3D.size() << "\">\n";
        writer.incInd();
        for (const auto& name : record.sources3D) {
            writer.Stream() << writer.ind() << "<S3D value=\"" << encodeAttribute(name) << "\"/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</Vertex>\n";
    }

    for (const auto& record : m_faces) {
        writer.Stream() << writer.ind() << "<Face wires=\"" << record.wireSizes.size()
                        << "\" name=\"" << encodeAttribute(record.hlrName) << "\" sources=\""
                        << record.sources3D.size() << "\">\n";
        writer.incInd();
        for (int wireSize : record.wireSizes) {
            writer.Stream() << writer.ind() << "<Wire edges=\"" << wireSize << "\"/>\n";
        }
        for (const auto& name : record.sources3D) {
            writer.Stream() << writer.ind() << "<S3D value=\"" << encodeAttribute(name) << "\"/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</Face>\n";
    }

    writer.decInd();
    writer.Stream() << writer.ind() << "</ProjectedGeometry>\n";
}

void PropertyProjectedGeometry::Restore(Base::XMLReader& reader)
{
    reader.readElement("ProjectedGeometry");

    const int format = reader.getAttributeAsInteger("format");
    const int edgeCount = reader.getAttributeAsInteger("edges");
    const int vertexCount = reader.getAttributeAsInteger("vertices");
    const int faceCount = reader.getAttributeAsInteger("faces");
    Base::Vector3d centroid(reader.getAttributeAsFloat("cx"), reader.getAttributeAsFloat("cy"),
                            reader.getAttributeAsFloat("cz"));
    const bool hasFile = reader.hasAttribute("file");
    const std::string file = hasFile ? std::string(reader.getAttribute("file")) : std::string();

    aboutToSetValue();
    m_edgeShapes = TopoDS_Shape();
    m_faceEdgeShapes = TopoDS_Shape();
    m_cutFaces = TopoDS_Shape();
    m_edges.clear();
    m_vertices.clear();
    m_faces.clear();
    m_centroid = centroid;

    if (!hasFile) {
        // nothing was stored, and nothing follows the opening element
        hasSetValue();
        return;
    }

    if (format > currentFormat) {
        // a newer schema than this build reads.  Skip to the end of the
        // element and leave the property empty: the view projects, as it did
        // before any of this existed.
        Base::Console().Warning(
            "TechDraw - stored geometry is format %d, this build reads %d - reprojecting\n", format,
            currentFormat);
        reader.readEndElement("ProjectedGeometry");
        hasSetValue();
        return;
    }

    m_edges.reserve(edgeCount);
    for (int i = 0; i < edgeCount; i++) {
        reader.readElement("Edge");
        EdgeRecord record;
        record.geomType = reader.getAttributeAsInteger("gt");
        record.classOfEdge = reader.getAttributeAsInteger("ec");
        record.hlrVisible = reader.getAttributeAsInteger("vis") != 0;
        record.reversed = reader.getAttributeAsInteger("rev") != 0;
        record.ref3D = reader.getAttributeAsInteger("ref");
        record.cosmetic = reader.getAttributeAsInteger("cos") != 0;
        record.source = reader.getAttributeAsInteger("src");
        record.sourceIndex = reader.getAttributeAsInteger("si");
        record.cosmeticTag = reader.getAttribute("tag");
        record.source3D = reader.getAttribute("s3d");
        record.hlrName = reader.getAttribute("name");
        m_edges.push_back(record);
    }

    m_vertices.reserve(vertexCount);
    for (int i = 0; i < vertexCount; i++) {
        reader.readElement("Vertex");
        VertexRecord record;
        record.point =
            Base::Vector3d(reader.getAttributeAsFloat("x"), reader.getAttributeAsFloat("y"), 0.0);
        record.hlrVisible = reader.getAttributeAsInteger("vis") != 0;
        record.ref3D = reader.getAttributeAsInteger("ref");
        record.cosmetic = reader.getAttributeAsInteger("cos") != 0;
        record.center = reader.getAttributeAsInteger("ctr") != 0;
        record.reference = reader.getAttributeAsInteger("rfv") != 0;
        record.cosmeticTag = reader.getAttribute("tag");
        record.hlrName = reader.getAttribute("name");
        const int sourceCount = reader.getAttributeAsInteger("sources");
        for (int j = 0; j < sourceCount; j++) {
            reader.readElement("S3D");
            record.sources3D.emplace_back(reader.getAttribute("value"));
        }
        reader.readEndElement("Vertex");
        m_vertices.push_back(record);
    }

    m_faces.reserve(faceCount);
    for (int i = 0; i < faceCount; i++) {
        reader.readElement("Face");
        FaceRecord record;
        const int wireCount = reader.getAttributeAsInteger("wires");
        record.hlrName = reader.getAttribute("name");
        const int sourceCount = reader.getAttributeAsInteger("sources");
        for (int j = 0; j < wireCount; j++) {
            reader.readElement("Wire");
            record.wireSizes.push_back(reader.getAttributeAsInteger("edges"));
        }
        for (int j = 0; j < sourceCount; j++) {
            reader.readElement("S3D");
            record.sources3D.emplace_back(reader.getAttribute("value"));
        }
        reader.readEndElement("Face");
        m_faces.push_back(record);
    }

    reader.readEndElement("ProjectedGeometry");

    // the geometry itself arrives in a file of its own
    reader.addFile(file.c_str(), this);

    hasSetValue();
}

void PropertyProjectedGeometry::SaveDocFile(Base::Writer& writer) const
{
    BRep_Builder builder;
    TopoDS_Compound both;
    builder.MakeCompound(both);
    TopoDS_Compound empty;
    builder.MakeCompound(empty);
    builder.Add(both, m_edgeShapes.IsNull() ? TopoDS_Shape(empty) : m_edgeShapes);
    builder.Add(both, m_faceEdgeShapes.IsNull() ? TopoDS_Shape(empty) : m_faceEdgeShapes);
    builder.Add(both, m_cutFaces.IsNull() ? TopoDS_Shape(empty) : m_cutFaces);
    Part::TopoShape(both).exportBrep(writer.Stream(), true);
}

void PropertyProjectedGeometry::RestoreDocFile(Base::Reader& reader)
{
    Part::TopoShape shape;
    try {
        shape.importBrep(reader);
    }
    catch (const Standard_Failure& e) {
        Base::Console().Warning("TechDraw - stored geometry does not read back - %s\n",
                                e.GetMessageString());
        clear();
        return;
    }

    std::vector<TopoDS_Shape> parts;
    if (!shape.isNull()) {
        for (TopoDS_Iterator it(shape.getShape()); it.More(); it.Next()) {
            parts.push_back(it.Value());
        }
    }
    if (parts.size() != 3) {
        Base::Console().Warning("TechDraw - stored geometry file holds %d parts, not 3\n",
                                static_cast<int>(parts.size()));
        clear();
        return;
    }

    aboutToSetValue();
    m_edgeShapes = parts.at(0);
    m_faceEdgeShapes = parts.at(1);
    m_cutFaces = parts.at(2);
    hasSetValue();
}

PyObject* PropertyProjectedGeometry::getPyObject()
{
    // the geometry is read through the view's own accessors (getEdgeGeometry
    // and friends); this property is storage, and reports only how much of
    // each kind it holds
    return Py_BuildValue("(iii)", countEdges(), countVertices(), countFaces());
}

void PropertyProjectedGeometry::setPyObject(PyObject* value)
{
    (void)value;
    throw Base::AttributeError("the stored projection is derived state and is not assignable");
}
