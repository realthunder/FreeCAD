// Does OCCT's position-based binary shape format support restoring ONE
// shape out of a central store, without parsing the rest, and with
// TShape identity preserved between separately-read shapes?
//
// That is the question the central-shape-storage plan turns on.
// BinTools_ShapeWriter/BinTools_ShapeReader (OCCT 7.6+) write references
// as absolute stream positions; the reader seeks to them and caches
// position -> shape. BinTools::Write/Read -- and FreeCAD's
// TopoShape::exportBinary -- use the OLDER BinTools_ShapeSet instead,
// which is a grouped table with no positions.

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BinTools_ShapeReader.hxx>
#include <BinTools_ShapeWriter.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <gp_Trsf.hxx>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    // The parent/child case: children are whole shapes, and the parent
    // is a compound whose leaves ARE those children (same TShape).
    const int N = 6;
    std::vector<TopoDS_Shape> children;
    for (int i = 0; i < N; ++i)
        children.push_back(BRepPrimAPI_MakeTorus(8.0 + 0.05 * i, 3.0).Shape());

    BRep_Builder builder;
    TopoDS_Compound parent;
    builder.MakeCompound(parent);
    for (int i = 0; i < N; ++i) {
        // Leaves are the children moved -- a located copy of the SAME
        // TShape, exactly what a compound feature produces.
        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(30.0 * i, 0, 0));
        TopoDS_Shape moved = children[i].Moved(TopLoc_Location(trsf));
        builder.Add(parent, moved);
    }

    // ---- write one central store, recording each shape's position ----
    std::ostringstream out(std::ios::binary);
    BinTools_ShapeWriter writer;
    std::vector<uint64_t> pos;

    pos.push_back(uint64_t(out.tellp()));
    writer.Write(parent, out);
    for (int i = 0; i < N; ++i) {
        pos.push_back(uint64_t(out.tellp()));
        writer.Write(children[i], out);
    }
    const std::string blob = out.str();
    std::printf("central store: %zu bytes for the parent + %d children\n",
                blob.size(), N);
    std::printf("  parent at %llu, children at",
                (unsigned long long)pos[0]);
    for (int i = 1; i <= N; ++i)
        std::printf(" %llu", (unsigned long long)pos[i]);
    std::printf("\n");

    // What a per-property store would have cost: each shape written to
    // its own stream, which is what one member per property does today.
    size_t separate = 0;
    for (int i = 0; i < N; ++i) {
        std::ostringstream one(std::ios::binary);
        BinTools_ShapeWriter w;
        w.Write(children[i], one);
        separate += one.str().size();
    }
    {
        std::ostringstream one(std::ios::binary);
        BinTools_ShapeWriter w;
        w.Write(parent, one);
        separate += one.str().size();
    }
    std::printf("one member per property would be %zu bytes (%.2fx)\n",
                separate, double(separate) / double(blob.size()));

    // ---- selective restore: read ONLY child 4, from the middle ----
    {
        std::istringstream in(blob, std::ios::binary);
        BinTools_ShapeReader reader;
        in.seekg(std::streampos(pos[5]));   // child index 4
        TopoDS_Shape one;
        reader.Read(in, one);
        std::printf("selective read of child 4 alone: %s, type %d\n",
                    one.IsNull() ? "NULL" : "ok", int(one.ShapeType()));
    }

    // ---- identity across separate reads, one reader kept alive ----
    {
        std::istringstream in(blob, std::ios::binary);
        BinTools_ShapeReader reader;

        in.seekg(std::streampos(pos[0]));
        TopoDS_Shape rParent;
        reader.Read(in, rParent);

        in.seekg(std::streampos(pos[1]));
        TopoDS_Shape rChild0;
        reader.Read(in, rChild0);

        TopExp_Explorer exp(rParent, TopAbs_SOLID);
        TopoDS_Shape firstLeaf = exp.Current();
        std::printf("parent leaf 0 IsPartner child 0: %s\n",
                    firstLeaf.IsPartner(rChild0) ? "TRUE" : "FALSE");
        std::printf("  (same TShape pointer: %s)\n",
                    firstLeaf.TShape() == rChild0.TShape() ? "yes" : "no");
    }

    // ---- and with a FRESH reader per property, as a control ----
    {
        std::istringstream in1(blob, std::ios::binary);
        BinTools_ShapeReader r1;
        in1.seekg(std::streampos(pos[0]));
        TopoDS_Shape rParent;
        r1.Read(in1, rParent);

        std::istringstream in2(blob, std::ios::binary);
        BinTools_ShapeReader r2;
        in2.seekg(std::streampos(pos[1]));
        TopoDS_Shape rChild0;
        r2.Read(in2, rChild0);

        TopExp_Explorer exp(rParent, TopAbs_SOLID);
        std::printf("control, one reader per property: IsPartner %s\n",
                    exp.Current().IsPartner(rChild0) ? "TRUE" : "FALSE");
    }
    return 0;
}
