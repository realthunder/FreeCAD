// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <cstdio>
#include <string>

#include <BRepPrimAPI_MakeBox.hxx>
#include <NCollection_Sequence.hxx>
#include <STEPControl_Writer.hxx>
#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Base/FileInfo.h>
#include <Mod/Import/App/ReaderStep.h>
#include <Mod/Part/App/PartPyCXX.h>

// The streamed STEP read hands the file's roots to the document batch by
// batch. It is the OCCT fork's (STEPCAFControl_Reader::TransferRootRange and
// three more), and Import reaches those through the C functions the fork
// exports for them, looked up by name at run time: called by their C++ names
// they would keep Import from loading where the libraries are upstream's.
// Without them a file is not streamable, and the test is skipped.

class ReaderStepStreamTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

TEST_F(ReaderStepStreamTest, twoRootsArriveInTwoBatches)
{
    if (!Part::lookUpOCCT("TKDESTEP", "STEPCAFControl_Reader_TransferRootRange")) {
        GTEST_SKIP() << "no STEPCAFControl_Reader_TransferRootRange: not the OCCT fork";
    }
    const std::string name = Base::FileInfo::getTempFileName("fc_step_stream") + ".step";
    {
        STEPControl_Writer writer;
        ASSERT_EQ(writer.Transfer(BRepPrimAPI_MakeBox(1., 2., 3.).Shape(), STEPControl_AsIs),
                  IFSelect_RetDone);
        ASSERT_EQ(writer.Transfer(BRepPrimAPI_MakeBox(gp_Pnt(5., 0., 0.), 2., 2., 2.).Shape(),
                                  STEPControl_AsIs),
                  IFSelect_RetDone);
        ASSERT_EQ(writer.Write(name.c_str()), IFSelect_RetDone);
    }

    Handle(TDocStd_Document) hDoc;
    XCAFApp_Application::GetApplication()->NewDocument(TCollection_ExtendedString("MDTV-CAF"),
                                                       hDoc);
    Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(hDoc->Main());
    auto freeShapes = [&shapes]() {
        NCollection_Sequence<TDF_Label> labels;
        shapes->GetFreeShapes(labels);
        return int(labels.Size());
    };

    Import::ReaderStep reader {Base::FileInfo(name)};
    const int roots = reader.openStream();
    EXPECT_EQ(roots, 2);
    EXPECT_EQ(freeShapes(), 0);
    if (roots == 2) {
        reader.transferRootRange(hDoc, 1, 1);
        EXPECT_EQ(freeShapes(), 1);
        reader.transferRootRange(hDoc, 2, 2);
        EXPECT_EQ(freeShapes(), 2);
    }
    reader.closeStream();
    std::remove(name.c_str());
}
