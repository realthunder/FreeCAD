// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Mod/Import/App/ExportOCAF2.h>

#include <TDF_ChildIterator.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

// The "!hide"/"!show" colour marks a Link keeps in its ColoredElements reach
// the exported document without a view provider. ExportOCAF2 used to ask its
// colour callback for them, and only the GUI exporter's callback
// (ViewProvider::getElementColors) knew them: Import.export, the App
// exporter, dropped the marks silently. The callback here knows no colour at
// all, as the App exporter's knows none for a Link.

class ExportMarksTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // A test binary is not where FreeCAD looks for its modules; see this
        // suite's CMakeLists.
        Base::Interpreter().runString("import sys; sys.path[:0] = ['" FC_BUILD_LIB_DIR
                                     "', '" FC_BUILD_MOD_PART_DIR
                                     "', '" FC_BUILD_MOD_MATERIAL_DIR "']");
        Base::Interpreter().runString("import Part");
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    App::DocumentObject* addBox(const char* name, double x = 0)
    {
        auto box = static_cast<App::GeoFeature*>(_doc->addObject("Part::Box", name));
        box->Placement.setValue(Base::Placement(Base::Vector3d(x, 0, 0), Base::Rotation()));
        return box;
    }

    App::Link* addArray(bool showElement)
    {
        auto link = static_cast<App::Link*>(_doc->addObject("App::Link", "Array"));
        link->LinkedObject.setValue(addBox("Source"));
        link->ShowElement.setValue(showElement);
        link->ElementCount.setValue(3);
        return link;
    }

    static void mark(App::Link* link, const std::vector<std::string>& subs)
    {
        // What ViewProviderLink::setElementColors() stores for the marks
        link->ColoredElements.setValue(link, subs);
    }

    /// Exports \a obj the way Import.export does and counts the labels the
    /// result flags invisible.
    int invisibleCount(App::DocumentObject* obj, bool exportHidden = true)
    {
        _doc->recompute();
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        Handle(TDocStd_Document) hDoc;
        app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), hDoc);
        Import::ExportOCAF2 ocaf(hDoc, [](App::DocumentObject*, const char*) {
            return std::map<std::string, App::Color>();
        });
        Import::ExportOCAFOptions options;
        options.exportHidden = exportHidden;
        ocaf.setExportOptions(options);
        std::vector<App::DocumentObject*> objs {obj};
        try {
            ocaf.exportObjects(objs);
        }
        catch (...) {
            app->Close(hDoc);
            throw;
        }

        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(hDoc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(hDoc->Main());
        int count = 0;
        for (TDF_ChildIterator it(shapeTool->Label(), Standard_True); it.More(); it.Next()) {
            if (!colorTool->IsVisible(it.Value())) {
                ++count;
            }
        }
        app->Close(hDoc);
        return count;
    }

    std::string _docName;
    App::Document* _doc = nullptr;
};

TEST_F(ExportMarksTest, hideMarksACollapsedArrayElement)
{
    auto array = addArray(false);
    EXPECT_EQ(invisibleCount(array), 0);
    mark(array, {"1.!hide"});
    EXPECT_EQ(invisibleCount(array), 1);
}

TEST_F(ExportMarksTest, hideMarksAnExpandedArrayElement)
{
    auto array = addArray(true);
    EXPECT_EQ(invisibleCount(array), 0);
    mark(array, {"1.!hide"});
    EXPECT_EQ(invisibleCount(array), 1);
}

TEST_F(ExportMarksTest, showMarkOverridesAHiddenChild)
{
    auto part = static_cast<App::Part*>(_doc->addObject("App::Part", "Asm"));
    part->addObject(addBox("Box1"));
    auto box2 = addBox("Box2", 20);
    part->addObject(box2);
    box2->Visibility.setValue(false);
    auto link = static_cast<App::Link*>(_doc->addObject("App::Link", "Link"));
    link->LinkedObject.setValue(part);

    EXPECT_EQ(invisibleCount(link), 1);
    mark(link, {"Box2.!show"});
    EXPECT_EQ(invisibleCount(link), 0);
}

TEST_F(ExportMarksTest, hideMarksAChild)
{
    auto part = static_cast<App::Part*>(_doc->addObject("App::Part", "Asm"));
    part->addObject(addBox("Box1"));
    part->addObject(addBox("Box2", 20));
    auto link = static_cast<App::Link*>(_doc->addObject("App::Link", "Link"));
    link->LinkedObject.setValue(part);

    EXPECT_EQ(invisibleCount(link), 0);
    mark(link, {"Box1.!hide"});
    EXPECT_EQ(invisibleCount(link), 1);
}

// A container's shape is the compound of its shown children, so with every
// child hidden it has none, and the export used to stop there: no file, no
// error. Exporting hidden objects, the children go out flagged invisible.
TEST_F(ExportMarksTest, everyChildHiddenStillExports)
{
    auto part = static_cast<App::Part*>(_doc->addObject("App::Part", "Asm"));
    auto box1 = addBox("Box1");
    auto box2 = addBox("Box2", 20);
    part->addObject(box1);
    part->addObject(box2);
    box1->Visibility.setValue(false);
    box2->Visibility.setValue(false);
    EXPECT_EQ(invisibleCount(part), 2);

    auto link = static_cast<App::Link*>(_doc->addObject("App::Link", "Link"));
    link->LinkedObject.setValue(part);
    EXPECT_EQ(invisibleCount(link), 2);

    // One hidden by Visibility, the other by a mark
    box1->Visibility.setValue(true);
    mark(link, {"Box1.!hide"});
    EXPECT_EQ(invisibleCount(link), 2);
}

TEST_F(ExportMarksTest, nothingToExportIsAnError)
{
    auto part = static_cast<App::Part*>(_doc->addObject("App::Part", "Asm"));
    auto box = addBox("Box");
    part->addObject(box);
    box->Visibility.setValue(false);
    EXPECT_THROW(invisibleCount(part, false), Base::RuntimeError);
}
