// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log and the shape property (docs/TransactionLog.md sec
// 20.2, decision 6b): a shape that keeps the file it was last saved to
// answers the log with that file's hash instead of exporting again.

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/TransactionLog.h>
#include <App/TransactionValue.h>
#include <Base/FileInfo.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <src/App/InitApplication.h>

namespace {

class TransactionLogShapeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _mode = App::DocumentParams::getTransactionLog();
        App::DocumentParams::setTransactionLog(1);
        _docName = App::GetApplication().getUniqueDocumentName("txnshape");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _doc->setUndoMode(1);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
        App::DocumentParams::setTransactionLog(_mode);
    }

    /// The op of `seq` on Shape of `obj`, or null.
    static const App::LogOp* shapeOp(std::vector<App::LogOp>& ops, long cid)
    {
        for (auto& o : ops) {
            if (o.cid == cid && o.prop == "Shape" && o.op == "set")
                return &o;
        }
        return nullptr;
    }

    long _mode {0};
    std::string _docName;
    App::Document* _doc {};
};

TEST_F(TransactionLogShapeTest, savedShapeAnswersWithItsBlob)
{
    _doc->openTransaction("box");
    auto box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    _doc->commitTransaction();
    _doc->recompute();

    auto log = _doc->getTransactionLog();
    ASSERT_TRUE(log);

    // Not saved yet: the first edit's before value has to carry the geometry.
    _doc->openTransaction("edit 1");
    box->Length.setValue(20);
    _doc->commitTransaction();
    _doc->recompute();
    auto& store = log->store();
    auto txns = store.transactions();
    const App::LogOp* op = nullptr;
    for (auto& t : txns) {
        auto ops = store.ops(t.seq);
        if (auto o = shapeOp(ops, box->getID())) {
            if (!o->vbefore.empty())
                op = new App::LogOp(*o);
        }
    }
    ASSERT_TRUE(op);
    App::CapturedValue exported;
    ASSERT_TRUE(log->readValue(op->vbefore, exported));
    EXPECT_EQ(exported.fragment.find("hash="), std::string::npos) << exported.fragment;
    EXPECT_EQ(exported.attachments.size(), 1u);
    delete op;

    // Saved: the shape holds its blob, and the next edit's before value is
    // the fragment naming it, with nothing attached.
    const std::string path = Base::FileInfo::getTempPath() + "txnshape.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    ASSERT_TRUE(box->Shape.getBlob());
    const std::string blobHash = box->Shape.getBlob()->hash();

    _doc->openTransaction("edit 2");
    box->Length.setValue(30);
    _doc->commitTransaction();
    _doc->recompute();
    // The shape is written twice per edit -- once as the feature reacts
    // to Length, once by the recompute -- and the first write's copy is
    // the one that still holds the saved file.
    txns = store.transactions();
    size_t referencing = 0, exporting = 0;
    for (auto& t : txns) {
        auto ops = store.ops(t.seq);
        auto o = shapeOp(ops, box->getID());
        if (!o || o->vbefore.empty())
            continue;
        App::CapturedValue v;
        ASSERT_TRUE(log->readValue(o->vbefore, v));
        if (v.fragment.find("hash=\"" + blobHash + "\"") != std::string::npos) {
            ++referencing;
            EXPECT_TRUE(v.attachments.empty());
            EXPECT_LT(v.fragment.size(), 512u);
        }
        else {
            ++exporting;
            EXPECT_EQ(v.attachments.size(), 1u);
        }
    }
    EXPECT_EQ(referencing, 1u);
    EXPECT_GE(exporting, 2u);

    // The live shape has changed since: it answers with no blob, and the
    // log still holds the file the value named.
    EXPECT_FALSE(box->Shape.contentBlob());
    EXPECT_TRUE(Base::FileInfo(_doc->getFileBlobManager().find(blobHash)
                                   ? _doc->getFileBlobManager().find(blobHash)->path()
                                   : std::string()).exists());

    Base::FileInfo(path).deleteFile();
}

}  // namespace
