// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log and the shape property (docs/TransactionLog.md sec
// 20.2, decision 6b): a shape that keeps the file it was last saved to
// answers the log with that file's hash instead of exporting again.

#include "gtest/gtest.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/FileBlobManager.h>
#include <App/TransactionLog.h>
#include <App/TransactionValue.h>
#include <Base/FileInfo.h>
#include <Mod/Part/App/FeatureCompound.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/PrimitiveFeature.h>
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

// Sec 23.16: a shape's part in a composed snapshot is what a save writes --
// the file the snapshot's own pass made -- never the value the log took from
// a detached copy, which names no file (or another one) and no hasher.
TEST_F(TransactionLogShapeTest, composedSnapshotChecksOutAChangedShape)
{
    _doc->openTransaction("box");
    auto box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    _doc->commitTransaction();
    _doc->recompute();
    const std::string path = Base::FileInfo::getTempPath() + "txnshape-composed.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    for (double length : {20.0, 30.0}) {
        _doc->openTransaction("length");
        box->Length.setValue(length);
        _doc->commitTransaction();
        _doc->recompute();
        ASSERT_GT(_doc->snapshotToLog(), 0);
    }
    auto log = _doc->getTransactionLog();
    ASSERT_TRUE(log);
    auto& store = log->store();
    ASSERT_EQ(store.versions().size(), 3u);

    // The box's file, by the name the archive gives it, in every version;
    // each older one a patch toward the next -- a length edit moves a few
    // numbers -- and the newest the file the log holds.
    std::vector<std::string> files;
    for (int64_t num = 1; num <= 3; ++num) {
        for (const auto& e : store.manifest(num)) {
            if (e.entry == "Box.Shape.brp")
                files.push_back(e.hash);
        }
    }
    ASSERT_EQ(files.size(), 3u);
    for (size_t i = 0; i < files.size(); ++i) {
        App::LogEntity e;
        ASSERT_TRUE(store.getEntity(files[i], e));
        EXPECT_EQ(e.kind, "blob");
        if (i + 1 < files.size()) {
            EXPECT_EQ(e.enc, "delta") << i;
            EXPECT_EQ(e.base, files[i + 1]) << i;
            EXPECT_FALSE(log->heldBlob(files[i])) << i;
        }
        else {
            EXPECT_EQ(e.enc, "file");
            EXPECT_TRUE(log->heldBlob(files[i]));
        }
    }

    for (auto [num, volume] : {std::pair<int64_t, double> {2, 2000.0}, {1, 1000.0}, {3, 3000.0}}) {
        ASSERT_TRUE(_doc->restoreVersion(num));
        auto restored = static_cast<Part::Box*>(_doc->getObject("Box"));
        ASSERT_TRUE(restored);
        ASSERT_FALSE(restored->Shape.getShape().isNull()) << num;
        GProp_GProps props;
        BRepGProp::VolumeProperties(restored->Shape.getShape().getShape(), props);
        EXPECT_NEAR(props.Mass(), volume, 1e-6) << num;
    }
    Base::FileInfo(path).deleteFile();
}

// Sec 23.16: a value that names a file names what that file borrows too, so
// the log holds the lenders' files with it.
TEST_F(TransactionLogShapeTest, borrowedFilesAreHeld)
{
    _doc->openTransaction("parts");
    auto box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    auto cyl = static_cast<Part::Cylinder*>(_doc->addObject("Part::Cylinder", "Cylinder"));
    auto compound = static_cast<Part::Compound*>(_doc->addObject("Part::Compound", "Compound"));
    compound->Links.setValues({box, cyl});
    _doc->commitTransaction();
    _doc->recompute();
    const std::string path = Base::FileInfo::getTempPath() + "txnshape-borrow.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    const App::FileBlobHandle held = compound->Shape.getBlob();
    ASSERT_TRUE(held);
    const std::vector<std::string> sources = held->sources();
    ASSERT_EQ(sources.size(), 2u) << "the compound's file borrows its two parts";

    // The compound changes; its before value names the saved file.
    _doc->openTransaction("length");
    box->Length.setValue(20);
    _doc->commitTransaction();
    _doc->recompute();
    auto log = _doc->getTransactionLog();
    ASSERT_TRUE(log);
    auto& store = log->store();
    App::LogEntity e;
    ASSERT_TRUE(store.getEntity(held->hash(), e));
    EXPECT_EQ(e.kind, "blob");
    std::vector<std::string> reads;
    for (const auto& r : e.refs) {
        if (r.role == "blob")
            reads.push_back(r.target);
    }
    std::sort(reads.begin(), reads.end());
    std::vector<std::string> expected = sources;
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(reads, expected);
    for (const auto& hash : sources) {
        App::LogEntity source;
        ASSERT_TRUE(store.getEntity(hash, source)) << hash;
        EXPECT_EQ(source.kind, "blob");
    }
    Base::FileInfo(path).deleteFile();
}

}  // namespace

namespace {

double volumeOf(const Part::Box* box)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(box->Shape.getShape().getShape(), props);
    return props.Mass();
}

// docs/TransactionLog.md sec 25: a crashed session with shapes -- values the
// log keeps as blobs in the pack store -- comes back with its shapes, and
// the version it replays over is a saved file's.
TEST_F(TransactionLogShapeTest, recoversShapesFromTheLeftoverStore)
{
    _doc->openTransaction("box");
    auto box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    _doc->commitTransaction();
    _doc->recompute();
    const std::string path = Base::FileInfo::getTempPath() + "txnshape-recover.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));   // the anchor

    _doc->openTransaction("length");
    box->Length.setValue(25);
    _doc->commitTransaction();
    _doc->recompute();
    auto cyl = static_cast<Part::Cylinder*>(_doc->addObject("Part::Cylinder", "Cylinder"));
    cyl->Radius.setValue(3);
    _doc->recompute();
    // Edited and never recomputed: touched at the crash, and after.
    _doc->openTransaction("height");
    box->Height.setValue(7);
    _doc->commitTransaction();
    ASSERT_TRUE(box->isTouched());
    auto log = _doc->getTransactionLog();
    ASSERT_TRUE(log);
    log->flush();
    _doc->getFileBlobManager().flush();

    const std::string crashed = Base::FileInfo::getTempPath() + "txnshape-crashed";
    Base::FileInfo(crashed).deleteDirectoryRecursive();
    for (const char* sub : {"history", "blobs"}) {
        const std::string from = _doc->TransientDir.getStrValue() + "/" + sub;
        const std::string to = crashed + "/" + sub;
        Base::FileInfo(to).createDirectories();
        if (!Base::FileInfo(from).isDir())
            continue;
        for (const auto& file : Base::FileInfo(from).getDirectoryContent()) {
            if (file.isFile())
                file.copyTo((to + "/" + file.fileName()).c_str());
        }
    }

    auto recovered = App::GetApplication().recoverDocument(crashed.c_str(), false);
    ASSERT_TRUE(recovered);
    const std::string name = recovered->getName();
    EXPECT_STREQ(recovered->FileName.getValue(), _doc->FileName.getValue());
    auto rbox = dynamic_cast<Part::Box*>(recovered->getObject("Box"));
    auto rcyl = dynamic_cast<Part::Cylinder*>(recovered->getObject("Cylinder"));
    ASSERT_TRUE(rbox);
    ASSERT_TRUE(rcyl);
    EXPECT_DOUBLE_EQ(rbox->Length.getValue(), 25.0);
    EXPECT_DOUBLE_EQ(rbox->Height.getValue(), 7.0);
    EXPECT_DOUBLE_EQ(rcyl->Radius.getValue(), 3.0);
    auto volume = [](const TopoDS_Shape& shape) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props);
        return props.Mass();
    };
    ASSERT_FALSE(rbox->Shape.getShape().isNull());
    ASSERT_FALSE(rcyl->Shape.getShape().isNull());
    EXPECT_NEAR(volume(rbox->Shape.getShape().getShape()),
                volume(box->Shape.getShape().getShape()), 1e-6);
    EXPECT_NEAR(volume(rcyl->Shape.getShape().getShape()),
                volume(cyl->Shape.getShape().getShape()), 1e-6);
    // The shapes are the log's, not recomputed, and the touched state is
    // the session's: the cylinder was recomputed, the box edited since.
    EXPECT_FALSE(rcyl->isTouched());
    EXPECT_TRUE(rbox->isTouched());
    App::GetApplication().closeDocument(name.c_str());
    Base::FileInfo(path).deleteFile();
}

}  // namespace

// Sec 24.3: a cold undo restores a shape whose file the log keeps only as a
// delta -- the file is decoded back into the document's store, and the
// shape comes back without a recompute.
TEST_F(TransactionLogShapeTest, coldUndoReadoptsADeltaBlob)
{
    _doc->openTransaction("box");
    auto box = static_cast<Part::Box*>(_doc->addObject("Part::Box", "Box"));
    _doc->recompute();
    _doc->commitTransaction();
    const std::string path = Base::FileInfo::getTempPath() + "txnshape-cold.FCStd";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    ASSERT_TRUE(box->Shape.getBlob());
    const std::string saved = box->Shape.getBlob()->hash();

    _doc->setMaxUndoStackSize(1);
    for (double length : {20.0, 30.0}) {
        _doc->openTransaction("length");
        box->Length.setValue(length);
        _doc->recompute();   // the outputs are part of the step
        _doc->commitTransaction();
        ASSERT_GT(_doc->snapshotToLog(), 0);
    }
    auto log = _doc->getTransactionLog();
    ASSERT_TRUE(log);
    auto& store = log->store();
    App::LogEntity e;
    ASSERT_TRUE(store.getEntity(saved, e));
    EXPECT_EQ(e.enc, "delta");
    EXPECT_FALSE(_doc->getFileBlobManager().find(saved)) << "nothing holds the file any more";

    ASSERT_TRUE(_doc->undo());   // hot
    EXPECT_NEAR(volumeOf(box), 2000.0, 1e-6);
    const bool touchedHot = box->isTouched();
    ASSERT_TRUE(_doc->undo());   // cold: from the log
    EXPECT_DOUBLE_EQ(box->Length.getValue(), 10.0);
    EXPECT_EQ(box->isTouched(), touchedHot);
    EXPECT_NEAR(volumeOf(box), 1000.0, 1e-6);
    ASSERT_TRUE(_doc->getFileBlobManager().find(saved));
    EXPECT_TRUE(log->heldBlob(saved));
    ASSERT_TRUE(store.getEntity(saved, e));
    EXPECT_EQ(e.enc, "file");

    ASSERT_TRUE(_doc->redo());
    ASSERT_TRUE(_doc->redo());
    EXPECT_NEAR(volumeOf(box), 3000.0, 1e-6);
    Base::FileInfo(path).deleteFile();
}
