// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log, phase 1 (docs/TransactionLog.md sec 15): what a
// commit writes to the store, the pending-after rule of sec 20.2, and the
// promise that matters -- a log replays to an identical document.

#include "gtest/gtest.h"

#include <iterator>
#include <map>
#include <sqlite3.h>
#include <zipios++/zipfile.h>

#include "App/Application.h"
#include "Base/Interpreter.h"
#include "App/PropertyPythonObject.h"
#include "App/AutoTransaction.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/DocumentParams.h"
#include "App/FeatureTest.h"
#include "App/PropertyFile.h"
#include "App/PropertyHistory.h"
#include "App/TransactionLog.h"
#include "App/TransactionValue.h"
#include "App/Transactions.h"
#include "Base/FileInfo.h"
#include "Base/Stream.h"
#include <src/App/InitApplication.h>

// A stand-in for a view provider (docs/TransactionLog.md sec 24.9): a
// transactional container that is not a document object, belongs to one,
// and reports its changes to that object's document.
class TxnLogFakeView: public App::TransactionalObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(TxnLogFakeView);

public:
    TxnLogFakeView()
    {
        ADD_PROPERTY(Shade, (0));
    }
    App::PropertyInteger Shade;
    App::DocumentObject* owner {nullptr};
    const App::DocumentObject* getTransactionOwner() const override
    {
        return owner;
    }
    bool isAttachedToDocument() const override
    {
        return owner && owner->isAttachedToDocument();
    }
    void onBeforeChange(const App::Property* prop) override
    {
        if (owner && owner->getDocument())
            onBeforeChangeProperty(owner->getDocument(), prop);
        App::TransactionalObject::onBeforeChange(prop);
    }
};

PROPERTY_SOURCE(TxnLogFakeView, App::TransactionalObject)

namespace {

class TransactionLogTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        if (TxnLogFakeView::getClassTypeId().isBad()) {
            TxnLogFakeView::init();
            // What the Gui does for its view providers: a record type.
            static App::TransactionProducer<App::TransactionObject> producer(
                TxnLogFakeView::getClassTypeId());
        }
    }

    void SetUp() override
    {
        _mode = App::DocumentParams::getTransactionLog();
        App::DocumentParams::setTransactionLog(1);
        _docName = App::GetApplication().getUniqueDocumentName("txnlog");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _doc->setUndoMode(1);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
        App::DocumentParams::setTransactionLog(_mode);
    }

    App::Document* doc() { return _doc; }
    App::TransactionLog& log()
    {
        auto l = _doc->getTransactionLog();
        EXPECT_TRUE(l);
        return *l;
    }

    App::FeatureTest* make(const char* name)
    {
        return static_cast<App::FeatureTest*>(_doc->addObject("App::FeatureTest", name));
    }

    /// Head state of every (cid, prop) as the log tells it: the latest
    /// resolved ref, in op order; create/remove tracked as presence.
    struct Head
    {
        std::map<long, std::pair<std::string, std::string>> objects;  // id -> name, type
        std::map<std::pair<long, std::string>, std::string> values;
    };

    Head replayHead()
    {
        Head h;
        auto& store = log().store();
        for (auto& t : store.transactions()) {
            for (auto& o : store.ops(t.seq)) {
                if (o.op == "create") {
                    h.objects[o.cid] = {o.cname, o.ctype};
                }
                else if (o.op == "remove") {
                    h.objects.erase(o.cid);
                    for (auto it = h.values.begin(); it != h.values.end();) {
                        if (it->first.first == o.cid)
                            it = h.values.erase(it);
                        else
                            ++it;
                    }
                }
                else if (o.op == "set") {
                    if (!o.vafter.empty())
                        h.values[{o.cid, o.prop}] = o.vafter;
                    else if (!o.vbefore.empty() && h.values.count({o.cid, o.prop}) == 0)
                        h.values[{o.cid, o.prop}] = "";  // pending, unknown yet
                }
                else if (o.op == "delprop") {
                    h.values.erase({o.cid, o.prop});
                }
            }
        }
        return h;
    }

private:
    long _mode {0};
    std::string _docName;
    App::Document* _doc {};
};

TEST_F(TransactionLogTest, createSetRemoveAreLogged)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();

    doc()->openTransaction("set");
    obj->Integer.setValue(42);
    doc()->commitTransaction();

    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_EQ(txns.size(), 2u);
    EXPECT_EQ(txns[0].name, "create");
    EXPECT_EQ(txns[1].name, "set");
    EXPECT_EQ(txns[1].parent, txns[0].seq);

    auto ops = store.ops(txns[0].seq);
    ASSERT_FALSE(ops.empty());
    EXPECT_EQ(ops[0].op, "create");
    EXPECT_EQ(ops[0].cname, "Obj");
    EXPECT_EQ(ops[0].ctype, "App::FeatureTest");
    // One set per persisted property follows the create, its value written
    // with the commit (sec 25.4: nothing is left pending).
    size_t sets = 0;
    for (auto& o : ops) {
        if (o.op == "set") {
            ++sets;
            EXPECT_TRUE(o.vbefore.empty());
            EXPECT_EQ(o.vafter.size(), 40u) << o.prop;
        }
    }
    EXPECT_GT(sets, 5u);

    // The set: before is the copy the undo system took, after written.
    ops = store.ops(txns[1].seq);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].op, "set");
    EXPECT_EQ(ops[0].prop, "Integer");
    EXPECT_EQ(ops[0].vbefore.size(), 40u);
    EXPECT_EQ(ops[0].vafter.size(), 40u);
    EXPECT_FALSE(ops[0].derived);
    // ... and that before is the create's after on Integer.
    for (auto& o : store.ops(txns[0].seq)) {
        if (o.op == "set" && o.prop == "Integer")
            EXPECT_EQ(o.vafter, ops[0].vbefore);
    }

    App::CapturedValue v;
    ASSERT_TRUE(log().readValue(store.ops(txns[1].seq)[0].vbefore, v));
    EXPECT_NE(v.fragment.find("Integer"), std::string::npos);
    EXPECT_NE(v.fragment.find("4711"), std::string::npos);

    doc()->openTransaction("remove");
    doc()->removeObject("Obj");
    doc()->commitTransaction();
    txns = store.transactions();
    ASSERT_EQ(txns.size(), 3u);
    ops = store.ops(txns[2].seq);
    EXPECT_EQ(ops.back().op, "remove");
    // The remove's befores resolved the set's pending after.
    EXPECT_EQ(store.ops(txns[1].seq)[0].vafter.size(), 40u);
    EXPECT_EQ(log().pendingCount(), 0u);
}

TEST_F(TransactionLogTest, unchangedWriteIsNotLogged)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    log().resolvePending();

    doc()->openTransaction("noop");
    obj->Integer.setValue(obj->Integer.getValue());
    doc()->commitTransaction();
    EXPECT_EQ(log().store().transactions().size(), 1u);
}

TEST_F(TransactionLogTest, replaysToIdenticalDocument)
{
    doc()->openTransaction("create");
    auto a = make("A");
    auto b = make("B");
    doc()->commitTransaction();

    doc()->openTransaction("edit");
    a->Integer.setValue(7);
    a->Float.setValue(2.5);
    a->String.setValue("seven");
    a->Vector.setValue(Base::Vector3d(1, 2, 3));
    b->IntegerList.setValues({1, 2, 3});
    doc()->commitTransaction();

    doc()->openTransaction("edit again");
    a->Integer.setValue(8);
    b->Link.setValue(a);
    doc()->commitTransaction();

    doc()->openTransaction("dynamic");
    auto dyn = a->addDynamicProperty("App::PropertyString", "Extra", "Base", "doc");
    ASSERT_TRUE(dyn);
    static_cast<App::PropertyString*>(dyn)->setValue("extra");
    doc()->commitTransaction();

    doc()->openTransaction("drop B");
    doc()->removeObject("B");
    doc()->commitTransaction();

    log().resolvePending();
    EXPECT_EQ(log().pendingCount(), 0u);

    Head head = replayHead();
    ASSERT_EQ(head.objects.size(), 1u);
    EXPECT_EQ(head.objects.begin()->second.first, "A");

    // Rebuild A from the head refs into a fresh document and compare
    // every property's bytes with the live one.
    std::string name2 = App::GetApplication().getUniqueDocumentName("replay");
    auto doc2 = App::GetApplication().newDocument(name2.c_str(), "testUser");
    auto a2 = static_cast<App::FeatureTest*>(doc2->addObject(
        head.objects.begin()->second.second.c_str(), "A"));
    ASSERT_TRUE(a2);
    size_t restored = 0;
    for (auto& kv : head.values) {
        ASSERT_FALSE(kv.second.empty()) << kv.first.second;
        App::Property* prop = a2->getPropertyByName(kv.first.second.c_str());
        if (!prop) {
            // The dynamic one: recreate it from the addprop op's type.
            prop = a2->addDynamicProperty("App::PropertyString", kv.first.second.c_str());
        }
        ASSERT_TRUE(prop) << kv.first.second;
        App::CapturedValue v;
        ASSERT_TRUE(log().readValue(kv.second, v)) << kv.first.second;
        App::restoreValue(*prop, v);
        ++restored;
    }
    EXPECT_GT(restored, 5u);

    std::map<std::string, App::Property*> live, copy;
    a->getPropertyMap(live);
    a2->getPropertyMap(copy);
    for (auto& kv : live) {
        short t = a->getPropertyType(kv.second);
        if ((t & App::Prop_Transient) || (t & App::Prop_NoPersist))
            continue;
        auto it = copy.find(kv.first);
        ASSERT_NE(it, copy.end()) << kv.first;
        if (kv.first == "Link" || kv.first == "Source1" || kv.first == "Source2")
            continue;  // cross-document links do not compare by bytes
        App::CapturedValue x = App::captureValue(*doc(), *kv.second);
        App::CapturedValue y = App::captureValue(*doc2, *it->second);
        EXPECT_EQ(x.fragment, y.fragment) << kv.first;
    }
    EXPECT_EQ(a2->Integer.getValue(), 8);
    EXPECT_EQ(a2->String.getValue(), std::string("seven"));
    EXPECT_EQ(static_cast<App::PropertyString*>(a2->getPropertyByName("Extra"))->getValue(),
              std::string("extra"));
    App::GetApplication().closeDocument(name2.c_str());
}

TEST_F(TransactionLogTest, implicitTransactionsGroupByInvocation)
{
    // Undo off: nothing is kept for undo, but the log still records.
    doc()->setUndoMode(0);
    auto obj = make("Obj");
    ASSERT_TRUE(obj);
    // The create opened an implicit transaction of its own (no scope is
    // active here); close it so the scope below is its own transaction.
    EXPECT_TRUE(doc()->hasPendingTransaction());
    doc()->commitTransaction();
    {
        App::Application::InvocationScope scope("test");
        obj->Integer.setValue(1);
        obj->Float.setValue(2.0);
        // Still open: the invocation has not returned.
        EXPECT_TRUE(doc()->hasPendingTransaction());
    }
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_EQ(doc()->getAvailableUndoNames().size(), 0u);

    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 2u);   // the create (outside any scope), the scope
    const auto& last = txns.back();
    EXPECT_EQ(last.kind, "implicit");
    EXPECT_EQ(last.origin, "test");
    EXPECT_NE(last.name.find("test"), std::string::npos);
    size_t sets = 0;
    for (auto& o : store.ops(last.seq)) {
        if (o.op == "set") {
            ++sets;
            EXPECT_TRUE(o.prop == "Integer" || o.prop == "Float") << o.prop;
            EXPECT_EQ(o.vbefore.size(), 40u);
        }
    }
    EXPECT_EQ(sets, 2u);

    // An explicit transaction closes an implicit one first.
    obj->Integer.setValue(5);
    EXPECT_TRUE(doc()->hasPendingTransaction());
    doc()->openTransaction("explicit");
    obj->Integer.setValue(6);
    doc()->commitTransaction();
    txns = store.transactions();
    ASSERT_GE(txns.size(), 4u);
    EXPECT_EQ(txns[txns.size() - 2].kind, "implicit");
    EXPECT_EQ(txns.back().kind, "user");
    EXPECT_EQ(txns.back().name, "explicit");

    // Undo on: the implicit transaction is an undo step as well.
    doc()->setUndoMode(1);
    {
        App::Application::InvocationScope scope("again");
        obj->Integer.setValue(9);
    }
    EXPECT_EQ(doc()->getAvailableUndoNames().size(), 1u);
    EXPECT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 6);
}

TEST_F(TransactionLogTest, recomputeRecordAndSession)
{
    auto& store = log().store();
    auto sessions = store.sessions();
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0].id, log().session());
    EXPECT_EQ(sessions[0].env, log().environment());
    EXPECT_EQ(sessions[0].closed, 0.0);
    // Identity is off by default: nothing personal in the row.
    EXPECT_TRUE(sessions[0].user.empty());
    EXPECT_NE(store.environmentJson(log().environment()).find("BuildVersionMajor"),
              std::string::npos);

    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    obj->Integer.setValue(3);
    doc()->recompute();

    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 2u);
    const auto& rec = txns.back();
    EXPECT_EQ(rec.kind, "recompute");
    EXPECT_EQ(rec.session, log().session());
    EXPECT_NE(rec.script.find("\"objects\":[{\"id\":" + std::to_string(obj->getID())),
              std::string::npos) << rec.script;
    EXPECT_NE(rec.script.find("\"name\":\"Obj\""), std::string::npos);
    EXPECT_EQ(rec.script.find("error"), std::string::npos);
    EXPECT_TRUE(store.ops(rec.seq).empty());
    // The implicit transaction holding the write comes before the record.
    EXPECT_EQ(txns[txns.size() - 2].kind, "implicit");
}

TEST_F(TransactionLogTest, writerOutlivesTransaction)
{
    // Undo off: the transaction is deleted at commit, and with it the
    // copies the writer serialises unless it holds a share of them (sec
    // 20.2, decision 4). Values are read back after the queue drains.
    doc()->setUndoMode(0);
    auto obj = make("Obj");
    doc()->commitTransaction();
    std::string big(200000, 'x');
    for (int i = 0; i < 20; ++i) {
        App::Application::InvocationScope scope("edit");
        obj->Integer.setValue(100 + i);
        obj->String.setValue(big + std::to_string(i));
    }
    EXPECT_FALSE(doc()->hasPendingTransaction());
    // Seq numbers are handed out ahead of the writes.
    EXPECT_EQ(log().lastSeq(), 21);

    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_EQ(txns.size(), 21u);
    // Every before value is there, and the copy was the value at the time:
    // the i-th edit's before on String is the (i-1)-th string.
    int seen = 0;
    for (size_t k = 2; k < txns.size(); ++k) {
        for (auto& o : store.ops(txns[k].seq)) {
            if (o.prop != "String")
                continue;
            ASSERT_EQ(o.vbefore.size(), 40u) << txns[k].seq;
            App::CapturedValue v;
            ASSERT_TRUE(log().readValue(o.vbefore, v));
            EXPECT_NE(v.fragment.find(big + std::to_string(k - 2)), std::string::npos) << k;
            ++seen;
        }
    }
    EXPECT_EQ(seen, 19);
    // The head after ref is pending until resolved; then it is the last.
    log().resolvePending();
    EXPECT_EQ(log().pendingCount(), 0u);
    auto ops = store.ops(txns.back().seq);
    for (auto& o : ops) {
        if (o.prop == "String") {
            App::CapturedValue v;
            ASSERT_TRUE(log().readValue(o.vafter, v));
            EXPECT_NE(v.fragment.find(big + "19"), std::string::npos);
        }
    }
}

TEST_F(TransactionLogTest, saveRecordAndVersion)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(11);
    doc()->commitTransaction();
    // The after values are written with the commit (sec 25.4).
    EXPECT_EQ(log().pendingCount(), 0u);

    const std::string path = Base::FileInfo::getTempPath() + "txnlog-save.FCStd";
    ASSERT_TRUE(doc()->saveAs(path.c_str()));

    // A snapshot resolves what was pending first (sec 21).
    EXPECT_EQ(log().pendingCount(), 0u);

    // saveAs renames the transient directory, and the store moved with it.
    auto& store = log().store();
    EXPECT_EQ(log().path().find(doc()->TransientDir.getStrValue()), 0u) << log().path();
    auto versions = store.versions();
    ASSERT_EQ(versions.size(), 1u);
    const auto& v = versions[0];
    EXPECT_EQ(v.num, 1);
    EXPECT_EQ(v.kind, "unnamed");
    EXPECT_EQ(v.branch, 1);
    EXPECT_FALSE(v.uuid.empty());
    EXPECT_EQ(v.env, log().environment());
    EXPECT_GT(v.schema, 0);
    EXPECT_EQ(v.docxml_hash.size(), 40u);

    // The version's Document.xml is the file's Document.xml, byte for byte:
    // its hash is the hash of the archive entry, and the value holds it.
    zipios::ZipFile zip(path);
    std::unique_ptr<std::istream> entry(zip.getInputStream("Document.xml"));
    ASSERT_TRUE(entry);
    std::string fromFile((std::istreambuf_iterator<char>(*entry)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(fromFile.empty());
    EXPECT_EQ(App::hashBytes(fromFile), v.docxml_hash);
    // The manifest names the entry's composite (sec 23.3), which reads
    // back as the file's bytes.
    auto manifest = store.manifest(v.num);
    ASSERT_GE(manifest.size(), 1u);
    EXPECT_EQ(manifest[0].entry, "Document.xml");
    EXPECT_EQ(manifest[0].source, "entity");
    App::CapturedValue stored;
    ASSERT_TRUE(log().readValue(manifest[0].hash, stored));
    EXPECT_EQ(stored.fragment, fromFile);

    // A file on disk is matched to its version by that hash.
    App::LogVersion found;
    ASSERT_TRUE(store.findVersion(v.docxml_hash, found));
    EXPECT_EQ(found.num, v.num);
    EXPECT_FALSE(store.findVersion(std::string(40, '0'), found));

    // The save row follows the version's sequence and names it.
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 1u);
    const auto& save = txns.back();
    EXPECT_EQ(save.kind, "save");
    EXPECT_EQ(save.parent, v.seq);
    EXPECT_NE(save.script.find("\"version\":1"), std::string::npos) << save.script;
    EXPECT_NE(save.script.find(v.docxml_hash), std::string::npos);
    EXPECT_TRUE(store.ops(save.seq).empty());

    // Truncation keeps the value a manifest names.
    store.truncate(save.seq);
    EXPECT_TRUE(store.hasEntity(manifest[0].hash));

    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, historyInitialisedFromFile)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(11);
    doc()->commitTransaction();
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-open.FCStd";
    ASSERT_TRUE(doc()->saveAs(path.c_str()));

    zipios::ZipFile zip(path);
    std::unique_ptr<std::istream> entry(zip.getInputStream("Document.xml"));
    ASSERT_TRUE(entry);
    std::string fromFile((std::istreambuf_iterator<char>(*entry)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(fromFile.empty());
    // openDocument() hands back a document already open at that path, so
    // the file is opened under another name.
    const std::string copy = Base::FileInfo::getTempPath() + "txnlog-open-copy.FCStd";
    ASSERT_TRUE(Base::FileInfo(path).copyTo(copy.c_str()));

    // Both archive readers: the random-access one and the forward-only one
    // the tap has to be ended in front of (sec 16.6).
    const bool randomAccess = App::DocumentParams::getArchiveRandomAccess();
    for (bool mode : {true, false}) {
        App::DocumentParams::setArchiveRandomAccess(mode);
        App::Document* opened = App::GetApplication().openDocument(copy.c_str(), false);
        App::DocumentParams::setArchiveRandomAccess(randomAccess);
        ASSERT_TRUE(opened) << "random access " << mode;
        auto olog = opened->getTransactionLog();
        ASSERT_TRUE(olog);
        auto& store = olog->store();
        // The store followed the transient directory the restored Uid renamed.
        EXPECT_EQ(olog->path().find(opened->TransientDir.getStrValue()), 0u)
            << olog->path() << " not under " << opened->TransientDir.getValue();
        EXPECT_TRUE(Base::FileInfo(olog->path()).exists());

        // Version 1 is the file as found, and the restore record names it.
        auto versions = store.versions();
        ASSERT_EQ(versions.size(), 1u) << "random access " << mode;
        const auto& v = versions[0];
        EXPECT_EQ(v.num, 1);
        EXPECT_EQ(v.kind, "unnamed");
        EXPECT_EQ(v.seq, 0);
        EXPECT_GT(v.schema, 0);
        EXPECT_EQ(v.docxml_hash, App::hashBytes(fromFile)) << "random access " << mode;
        App::CapturedValue stored;
        ASSERT_TRUE(olog->readValue(v.docxml_hash, stored));
        EXPECT_EQ(stored.fragment, fromFile);
        auto manifest = store.manifest(v.num);
        ASSERT_GE(manifest.size(), 1u);
        EXPECT_EQ(manifest[0].entry, "Document.xml");

        auto txns = store.transactions();
        ASSERT_EQ(txns.size(), 1u);
        EXPECT_EQ(txns[0].kind, "restore");
        EXPECT_EQ(txns[0].parent, 0);
        EXPECT_NE(txns[0].script.find("\"version\":1"), std::string::npos) << txns[0].script;
        EXPECT_TRUE(store.ops(txns[0].seq).empty());
        EXPECT_EQ(olog->pendingCount(), 0u);

        // The ops start at the next transaction, after the restore row.
        opened->openTransaction("edit");
        static_cast<App::FeatureTest*>(opened->getObject("Obj"))->Integer.setValue(12);
        opened->commitTransaction();
        txns = store.transactions();
        ASSERT_EQ(txns.size(), 2u);
        EXPECT_EQ(txns[1].parent, txns[0].seq);
        EXPECT_FALSE(store.ops(txns[1].seq).empty());

        App::GetApplication().closeDocument(opened->getName());
    }

    Base::FileInfo(path).deleteFile();
    Base::FileInfo(copy).deleteFile();
}

TEST_F(TransactionLogTest, snapshotAndCadence)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();

    // On demand: a version like a save's, with no file written, and a
    // `snapshot` record naming it. Nothing is pending (sec 25.4).
    EXPECT_EQ(log().pendingCount(), 0u);
    int64_t num = doc()->snapshotToLog();
    EXPECT_EQ(num, 1);
    EXPECT_EQ(log().pendingCount(), 0u);
    auto& store = log().store();
    auto versions = store.versions();
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_EQ(versions[0].kind, "unnamed");
    auto manifest = store.manifest(1);
    ASSERT_GE(manifest.size(), 1u);
    EXPECT_EQ(manifest[0].entry, "Document.xml");
    App::CapturedValue xml;
    ASSERT_TRUE(log().readValue(manifest[0].hash, xml));
    EXPECT_NE(xml.fragment.find("Document SchemaVersion="), std::string::npos)
        << xml.fragment.substr(0, 400);
    EXPECT_NE(xml.fragment.find("Obj"), std::string::npos);
    auto txns = store.transactions();
    EXPECT_EQ(txns.back().kind, "snapshot");
    EXPECT_TRUE(Base::FileInfo(doc()->FileName.getValue()).fileName().empty());

    // The cadence: every N commits since the last version.
    const long every = App::DocumentParams::getTransactionLogSnapshotTransactions();
    App::DocumentParams::setTransactionLogSnapshotTransactions(3);
    for (int i = 0; i < 7; ++i) {
        doc()->openTransaction("edit");
        obj->Integer.setValue(10 + i);
        doc()->commitTransaction();
    }
    App::DocumentParams::setTransactionLogSnapshotTransactions(every);
    // 7 commits after the on-demand version: versions at the 3rd and 6th.
    EXPECT_EQ(store.versions().size(), 3u);
    // A snapshot is never taken inside an open transaction.
    App::DocumentParams::setTransactionLogSnapshotTransactions(1);
    doc()->openTransaction("open");
    obj->Integer.setValue(99);
    EXPECT_EQ(doc()->snapshotToLog(), 0);
    doc()->commitTransaction();
    App::DocumentParams::setTransactionLogSnapshotTransactions(every);
    EXPECT_EQ(store.versions().size(), 4u);
}

TEST_F(TransactionLogTest, evictsUnnamedVersions)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();

    const long keep = App::DocumentParams::getTransactionLogKeepVersions();
    App::DocumentParams::setTransactionLogKeepVersions(2);
    std::vector<std::string> docxml;
    for (int i = 0; i < 4; ++i) {
        doc()->openTransaction("edit");
        obj->Integer.setValue(i);
        doc()->commitTransaction();
        ASSERT_EQ(doc()->snapshotToLog(), i + 1);
        App::LogVersion v;
        ASSERT_TRUE(log().store().getVersion(i + 1, v));
        docxml.push_back(v.docxml_hash);
    }
    App::DocumentParams::setTransactionLogKeepVersions(keep);

    // A named version is never evicted (sec 16.3).
    App::DocumentParams::setTransactionLogKeepVersions(1);
    ASSERT_TRUE(log().store().nameVersion(3, "release"));
    EXPECT_FALSE(log().store().nameVersion(99, "x"));
    doc()->openTransaction("edit");
    obj->Integer.setValue(42);
    doc()->commitTransaction();
    ASSERT_EQ(doc()->snapshotToLog(), 5);
    App::DocumentParams::setTransactionLogKeepVersions(keep);
    {
        auto vs = log().store().versions();
        ASSERT_EQ(vs.size(), 2u);
        EXPECT_EQ(vs[0].num, 3);
        EXPECT_EQ(vs[0].kind, "named");
        EXPECT_EQ(vs[0].name, "release");
        EXPECT_EQ(vs[1].num, 5);
        ASSERT_TRUE(log().store().nameVersion(3, ""));
        EXPECT_EQ(log().store().versions()[0].kind, "unnamed");
    }
    // Back to the state the first checks below expect.
    ASSERT_TRUE(log().store().nameVersion(3, "release"));

    // The two newest unnamed versions remain; the evicted ones took their
    // Document.xml values with them, the survivors kept theirs.
    auto& store = log().store();
    auto versions = store.versions();
    ASSERT_EQ(versions.size(), 2u);
    EXPECT_EQ(versions[0].num, 3);
    EXPECT_EQ(versions[1].num, 5);
    EXPECT_TRUE(store.manifest(1).empty());
    EXPECT_TRUE(store.manifest(4).empty());
    EXPECT_FALSE(store.hasEntity(docxml[0]));
    EXPECT_FALSE(store.hasEntity(docxml[1]));
    EXPECT_TRUE(store.hasEntity(docxml[2]));
    // Version 3's Document.xml was superseded by 4's and is a patch toward
    // it (sec 23.2), so 4's stays as the anchor 3 decodes through -- an
    // orphan the collector holds for as long as the delta needs it (23.5).
    App::LogEntity named;
    ASSERT_TRUE(store.getEntity(docxml[2], named));
    if (named.enc == "delta") {
        EXPECT_EQ(named.base, docxml[3]);
        EXPECT_TRUE(store.hasEntity(docxml[3]));
    }
    else {
        EXPECT_FALSE(store.hasEntity(docxml[3]));
    }
    // Ops are never evicted, nor their values: the edits' before refs read.
    for (auto& t : store.transactions()) {
        for (auto& o : store.ops(t.seq)) {
            if (!o.vbefore.empty())
                EXPECT_TRUE(store.hasEntity(o.vbefore)) << t.seq;
        }
    }
    EXPECT_GE(store.transactions().size(), 9u);
}

TEST_F(TransactionLogTest, restoresAVersion)
{
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    obj->String.setValue("one");
    doc()->commitTransaction();
    ASSERT_EQ(doc()->snapshotToLog(), 1);

    doc()->openTransaction("edit");
    obj->Integer.setValue(2);
    obj->String.setValue("two");
    doc()->commitTransaction();
    doc()->openTransaction("add");
    make("Later");
    doc()->commitTransaction();
    ASSERT_EQ(doc()->snapshotToLog(), 2);
    ASSERT_TRUE(doc()->getObject("Later"));

    // A snapshot is written the way an archive is (sec 23.9, "one
    // format"): one Document.xml, never split, whatever the property says.
    for (auto& e : log().store().manifest(1))
        EXPECT_NE(e.entry, "Obj.xml");
    // Back to version 1 (sec 24.5): one transaction -- the same object,
    // as it was, the later one gone -- and the log going on from there.
    const long laterId = doc()->getObject("Later")->getID();
    ASSERT_TRUE(doc()->restoreVersion(1));
    auto restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_EQ(restored, obj);
    EXPECT_EQ(restored->Integer.getValue(), 1);
    EXPECT_STREQ(restored->String.getValue(), "one");
    EXPECT_FALSE(doc()->getObject("Later"));
    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 1u);
    EXPECT_EQ(txns.back().kind, "restore");
    EXPECT_EQ(txns.back().name, "Restore version 1");
    EXPECT_EQ(store.versions().size(), 2u);   // a restore is not a new version
    EXPECT_EQ(doc()->getAvailableUndoNames().front(), "Restore version 1");
    EXPECT_FALSE(App::GetApplication().getDocument("VersionRestore"));
    EXPECT_EQ(App::GetApplication().getActiveDocument(), doc());

    // Undoable like any step: back to where it was, Later under its id.
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 2);
    ASSERT_TRUE(doc()->getObject("Later"));
    EXPECT_EQ(doc()->getObject("Later")->getID(), laterId);
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 1);
    EXPECT_FALSE(doc()->getObject("Later"));

    doc()->openTransaction("after");
    restored->Integer.setValue(3);
    doc()->commitTransaction();
    txns = store.transactions();
    EXPECT_EQ(txns.back().name, "after");

    // Forward again, to version 2: Later comes back under its id.
    ASSERT_TRUE(doc()->restoreVersion(2));
    restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Integer.getValue(), 2);
    ASSERT_TRUE(doc()->getObject("Later"));
    EXPECT_EQ(doc()->getObject("Later")->getID(), laterId);
    // Already version 2: nothing to do, no step.
    const int steps = doc()->getAvailableUndos();
    ASSERT_TRUE(doc()->restoreVersion(2));
    EXPECT_EQ(doc()->getAvailableUndos(), steps);
    EXPECT_THROW(doc()->restoreVersion(99), Base::Exception);
}

TEST_F(TransactionLogTest, embeddedHistoryRoundTrips)
{
    App::DocumentParams::setTransactionLog(2);   // embedded
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    ASSERT_EQ(doc()->snapshotToLog(), 1);
    ASSERT_TRUE(log().store().nameVersion(1, "v1"));
    doc()->openTransaction("edit");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    const int64_t seqBefore = log().lastSeq();

    const std::string path = Base::FileInfo::getTempPath() + "txnlog-embed.FCStd";
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    // The save embedded the copy and named the version it becomes.
    auto history = Base::freecad_dynamic_cast<App::PropertyHistory>(doc()->getPropertyByName("History"));
    ASSERT_TRUE(history);
    EXPECT_FALSE(history->isEmpty());
    auto version = Base::freecad_dynamic_cast<App::PropertyString>(doc()->getPropertyByName("Version"));
    ASSERT_TRUE(version);
    EXPECT_EQ(std::string(version->getValue()).substr(0, 2), "2 ");
    {
        zipios::ZipFile zip(path);
        bool db = false;
        for (const auto& entry : zip.entries())
            db = db || entry->getName().find(".db") != std::string::npos;
        EXPECT_TRUE(db);
    }

    // Opened elsewhere: the log continues from the embedded copy -- the
    // ops are there, v1 is there, and the file as found is version 2.
    const std::string copy = Base::FileInfo::getTempPath() + "txnlog-embed-copy.FCStd";
    ASSERT_TRUE(Base::FileInfo(path).copyTo(copy.c_str()));
    App::Document* opened = App::GetApplication().openDocument(copy.c_str(), false);
    ASSERT_TRUE(opened);
    auto olog = opened->getTransactionLog();
    ASSERT_TRUE(olog);
    auto& store = olog->store();
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), static_cast<size_t>(seqBefore + 1));
    EXPECT_EQ(txns.back().kind, "restore");
    // The copy holds the save's implicit transaction (the date stamp) and
    // is taken before the save's own record; the restore follows.
    EXPECT_EQ(txns.back().seq, seqBefore + 2);
    auto versions = store.versions();
    ASSERT_EQ(versions.size(), 2u);
    EXPECT_EQ(versions[0].num, 1);
    EXPECT_EQ(versions[0].kind, "named");
    EXPECT_EQ(versions[0].name, "v1");
    EXPECT_EQ(versions[1].num, 2);
    EXPECT_EQ(versions[1].kind, "unnamed");
    // The copy carries no cache-tier values, and reads its own ops' values.
    bool sawValue = false;
    for (auto& t : txns) {
        if (t.name != "edit")
            continue;
        for (auto& o : store.ops(t.seq)) {
            if (!o.vbefore.empty()) {
                EXPECT_TRUE(store.hasEntity(o.vbefore));
                sawValue = true;
            }
        }
    }
    EXPECT_TRUE(sawValue);
    // v1 restores from the embedded history.
    ASSERT_TRUE(opened->restoreVersion(1));
    EXPECT_EQ(static_cast<App::FeatureTest*>(opened->getObject("Obj"))->Integer.getValue(), 1);
    App::GetApplication().closeDocument(opened->getName());

    // A copy without history carries none, and the live document keeps its.
    const std::string plain = Base::FileInfo::getTempPath() + "txnlog-embed-plain.FCStd";
    ASSERT_TRUE(doc()->saveCopy(plain.c_str(), false));
    EXPECT_FALSE(history->isEmpty());
    {
        zipios::ZipFile zip(plain);
        for (const auto& entry : zip.entries())
            EXPECT_EQ(entry->getName().find(".db"), std::string::npos) << entry->getName();
    }
    Base::FileInfo(plain).deleteFile();

    // Saved again without the mode: the property is emptied, nothing
    // embedded, and an open finds no history to continue from.
    App::DocumentParams::setTransactionLog(1);
    ASSERT_TRUE(doc()->save());
    EXPECT_TRUE(history->isEmpty());
    ASSERT_TRUE(Base::FileInfo(path).copyTo(copy.c_str()));
    opened = App::GetApplication().openDocument(copy.c_str(), false);
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened->getTransactionLog()->store().versions().size(), 1u);
    App::GetApplication().closeDocument(opened->getName());

    Base::FileInfo(path).deleteFile();
    Base::FileInfo(copy).deleteFile();
}

}  // namespace

// Sec 23.1 / 23.2: an entity re-encoded as a delta against another reads
// back the same bytes under the same hash, a chain decodes through its
// bases, and the collector holds a base for as long as a delta needs it.
TEST_F(TransactionLogTest, deltaChainReadsAndHolds)
{
    std::string base(4000, 'a');
    for (size_t i = 0; i < base.size(); i += 97)
        base[i] = 'b' + (i % 20);
    std::string mid = base;
    mid.replace(1000, 10, "MIDDLE----");
    std::string top = mid;
    top.replace(3000, 10, "TOP-------");

    std::string patch;
    ASSERT_TRUE(App::TransactionLog::deltaEncode(mid, top, patch));
    EXPECT_LT(patch.size(), 400u);
    std::string back;
    ASSERT_TRUE(App::TransactionLog::deltaDecode(patch, top, mid.size(), back));
    EXPECT_EQ(back, mid);

    // Three xml entities as three versions would hold them: the newest is
    // full, the two older ones are reverse deltas toward it.
    auto& l = log();
    auto& store = l.store();
    auto put = [&](const std::string& bytes) {
        App::CapturedValue v;
        v.fragment = bytes;
        v.ok = true;
        // putValue is the worker's; a flushed store with no worker job
        // in flight is the same thing for a test.
        App::LogEntity e;
        e.kind = "xml";
        e.hash = App::hashBytes(bytes);
        e.size = bytes.size();
        e.data = bytes;
        store.putEntity(e);
        return e.hash;
    };
    const std::string hBase = put(base), hMid = put(mid), hTop = put(top);
    std::string p;
    ASSERT_TRUE(App::TransactionLog::deltaEncode(mid, top, p));
    store.reencodeEntity(hMid, "delta", hTop, p);
    ASSERT_TRUE(App::TransactionLog::deltaEncode(base, mid, p));
    store.reencodeEntity(hBase, "delta", hMid, p);

    App::LogEntity e;
    ASSERT_TRUE(store.getEntity(hBase, e));
    EXPECT_EQ(e.enc, "delta");
    EXPECT_EQ(e.base, hMid);
    EXPECT_EQ(store.basedOn(hMid), std::vector<std::string> {hBase});
    std::string bytes;
    ASSERT_TRUE(l.readBytes(hBase, bytes));
    EXPECT_EQ(bytes, base);
    ASSERT_TRUE(l.readBytes(hMid, bytes));
    EXPECT_EQ(bytes, mid);

    // Only the oldest is rooted (a manifest names it); the collector must
    // keep both bases it decodes through, and drop them once it goes.
    App::LogVersion v;
    v.seq = store.lastSeq();
    store.addVersion(v, {{"Document.xml", hBase, "entity"}});
    store.truncate(store.lastSeq() + 1);
    EXPECT_TRUE(store.hasEntity(hBase));
    EXPECT_TRUE(store.hasEntity(hMid));
    EXPECT_TRUE(store.hasEntity(hTop));
    ASSERT_TRUE(l.readBytes(hBase, bytes));
    EXPECT_EQ(bytes, base);
    store.evictVersion(v.num);
    EXPECT_FALSE(store.hasEntity(hBase));
    EXPECT_FALSE(store.hasEntity(hMid));
    EXPECT_FALSE(store.hasEntity(hTop));
}

// Sec 23.2, the policy: a property edited in a run leaves the newest value
// full and every older one a patch toward the value that replaced it; a
// version's entries are re-encoded toward the next version's; the chain
// is bounded; and everything reads back byte-identical through it.
TEST_F(TransactionLogTest, reverseDeltasFollowSupersession)
{
    const long hops = App::DocumentParams::getTransactionLogDeltaHops();
    App::DocumentParams::setTransactionLogDeltaHops(3);
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();

    // A big list that changes by one element per edit: what a sketch's
    // Geometry does.
    std::vector<long> values(5000);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = static_cast<long>(i * 7919);
    std::vector<std::string> afters;
    for (int i = 0; i < 6; ++i) {
        values[100 * i] = -1 - i;
        doc()->openTransaction("edit");
        obj->IntegerList.setValues(values);
        doc()->commitTransaction();
    }
    doc()->snapshotToLog();   // resolves the last after
    auto& l = log();
    auto& store = l.store();

    // Walk the set ops on IntegerList in order: each before is the
    // previous after.
    std::vector<std::string> chain;
    for (auto& t : store.transactions()) {
        for (auto& o : store.ops(t.seq)) {
            if (o.op == "set" && o.prop == "IntegerList" && !o.vbefore.empty())
                chain.push_back(o.vbefore);
        }
    }
    ASSERT_GE(chain.size(), 5u);
    App::LogOp last;
    // The newest after is full; with hops=3 the run re-anchors: at most
    // three deltas hang below any full entity.
    int deltas = 0, fulls = 0, run = 0, longest = 0;
    for (auto& h : chain) {
        App::LogEntity e;
        ASSERT_TRUE(store.getEntity(h, e)) << h;
        if (e.enc == "delta") {
            ++deltas;
            longest = std::max(longest, ++run);
            EXPECT_LT(e.data.size(), e.size / 4) << h;
        }
        else {
            ++fulls;
            run = 0;
        }
        App::CapturedValue v;
        ASSERT_TRUE(l.readValue(h, v));
        EXPECT_EQ(v.fragment.size(), e.size);
    }
    EXPECT_GE(deltas, 3);
    EXPECT_LE(longest, 3);

    // Two more versions: Document.xml of the older is a patch toward the
    // newer's, and the checkout of the older still reads.
    doc()->openTransaction("edit");
    obj->Integer.setValue(7);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    doc()->openTransaction("edit");
    obj->Integer.setValue(8);
    doc()->commitTransaction();
    const int64_t v2 = doc()->snapshotToLog();
    App::LogVersion a, b;
    ASSERT_TRUE(store.getVersion(v1, a));
    ASSERT_TRUE(store.getVersion(v2, b));
    App::LogEntity ea, eb;
    ASSERT_TRUE(store.getEntity(a.docxml_hash, ea));
    ASSERT_TRUE(store.getEntity(b.docxml_hash, eb));
    EXPECT_EQ(ea.enc, "delta");
    EXPECT_EQ(ea.base, b.docxml_hash);
    EXPECT_NE(eb.enc, "delta");
    // A composed version's docxml_hash names its composite (23.9); the
    // older one decodes through the newer and composes to the document.
    std::string bytes;
    ASSERT_TRUE(l.readBytes(a.docxml_hash, bytes));
    EXPECT_EQ(App::hashBytes(bytes), a.docxml_hash);
    App::CapturedValue older;
    ASSERT_TRUE(l.readValue(a.docxml_hash, older));
    EXPECT_NE(older.fragment.find("<FCDocument"), std::string::npos);

    doc()->restoreVersion(v1);
    obj = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(obj);
    EXPECT_EQ(obj->Integer.getValue(), 7);
    EXPECT_EQ(obj->IntegerList.getValues()[500], -6);
    App::DocumentParams::setTransactionLogDeltaHops(hops);
}

/// Sec 23.3: a version is a composite -- a skeleton plus the parts the log
/// already holds -- whether it was written by a save (record mode: every
/// body captured) or composed by a snapshot (compose mode: the sink claims
/// what the log holds and only the rest run Save). Both give the file's
/// bytes back, byte for byte, and the parts are the op values.
TEST_F(TransactionLogTest, composedSnapshotIsTheFile)
{
    // One entry for everything, with a shared-defaults block, so the
    // elision is exercised in both modes (23.3, "shared defaults stay").
    doc()->SplitXML.setValue(false);
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(11);
    obj->String.setValue("eleven");
    make("Plain");
    make("Other");
    doc()->commitTransaction();

    const std::string path = Base::FileInfo::getTempPath() + "txnlog-composed.FCStd";
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    zipios::ZipFile zip(path);
    std::unique_ptr<std::istream> entry(zip.getInputStream("Document.xml"));
    ASSERT_TRUE(entry);
    const std::string fromFile((std::istreambuf_iterator<char>(*entry)),
                               std::istreambuf_iterator<char>());
    ASSERT_FALSE(fromFile.empty());
    EXPECT_NE(fromFile.find("<Defaults"), std::string::npos) << "no shared-defaults block";

    auto& store = log().store();
    ASSERT_EQ(store.versions().size(), 1u);
    App::LogVersion v1;
    ASSERT_TRUE(store.getVersion(1, v1));
    // Record mode: the entry is a composite whose composition is the
    // archive's bytes, and the version is matched to the file by their
    // SHA-1 as before.
    auto manifest = store.manifest(1);
    ASSERT_FALSE(manifest.empty());
    EXPECT_EQ(manifest[0].entry, "Document.xml");
    App::LogEntity composite;
    ASSERT_TRUE(store.getEntity(manifest[0].hash, composite));
    EXPECT_EQ(composite.kind, "composite");
    EXPECT_EQ(v1.docxml_hash, App::hashBytes(fromFile));
    App::CapturedValue composed;
    ASSERT_TRUE(log().readValue(manifest[0].hash, composed));
    EXPECT_EQ(composed.fragment, fromFile);
    App::LogVersion found;
    ASSERT_TRUE(store.findVersion(App::hashBytes(fromFile), found));
    EXPECT_EQ(found.num, 1);

    // The parts: one per property written, by container and name; the
    // one for Obj.Integer is the op's resolved after value.
    std::string data;
    ASSERT_TRUE(log().readBytes(manifest[0].hash, data));
    App::TransactionLog::Composite c1;
    ASSERT_TRUE(c1.decode(data));
    EXPECT_FALSE(c1.skeleton.empty());
    App::LogEntity skeleton;
    ASSERT_TRUE(store.getEntity(c1.skeleton, skeleton));
    EXPECT_EQ(skeleton.kind, "skeleton");
    std::string integerPart;
    for (const auto& p : c1.parts) {
        if (p.container == obj->getFullName() && p.name == "Integer")
            integerPart = p.hash;
    }
    ASSERT_FALSE(integerPart.empty());
    Head head = replayHead();
    const std::pair<long, std::string> integerKey {obj->getID(), "Integer"};
    EXPECT_EQ(head.values[integerKey], integerPart);
    App::LogEntity part;
    ASSERT_TRUE(store.getEntity(integerPart, part));
    EXPECT_EQ(part.kind, "prop");
    // Elided: the defaults block carries what Plain's untouched
    // properties say, so they are not parts; Obj has the two it changed
    // on top of whatever cannot share a default.
    size_t objParts = 0, plainParts = 0;
    for (const auto& p : c1.parts) {
        if (p.container == obj->getFullName())
            ++objParts;
        else if (p.container == doc()->getObject("Plain")->getFullName())
            ++plainParts;
    }
    EXPECT_EQ(objParts, plainParts + 2);
    std::map<std::string, App::Property*> props;
    obj->getPropertyMap(props);
    EXPECT_LT(plainParts, props.size());

    // Compose mode, nothing changed: every part is claimed, and the
    // composition is still the file's bytes -- the elision by hash agrees
    // with the elision by bytes.
    const bool verify = App::DocumentParams::getTransactionLogVerify();
    App::DocumentParams::setTransactionLogVerify(true);
    ASSERT_EQ(doc()->snapshotToLog(), 2);
    App::DocumentParams::setTransactionLogVerify(verify);
    manifest = store.manifest(2);
    ASSERT_FALSE(manifest.empty());
    ASSERT_TRUE(log().readValue(manifest[0].hash, composed));
    EXPECT_EQ(composed.fragment, fromFile);
    ASSERT_TRUE(log().readBytes(manifest[0].hash, data));
    App::TransactionLog::Composite c2;
    ASSERT_TRUE(c2.decode(data));
    EXPECT_EQ(c2.skeleton, c1.skeleton);
    EXPECT_EQ(c2.parts.size(), c1.parts.size());

    // A change: the changed part is the new op value, the rest are the
    // same entities (the skeleton moves with the object's revision in the
    // <Objects> list, which is meta), and the checkout of the older
    // version is right.
    doc()->openTransaction("edit");
    obj->Integer.setValue(12);
    doc()->commitTransaction();
    ASSERT_EQ(doc()->snapshotToLog(), 3);
    manifest = store.manifest(3);
    ASSERT_TRUE(log().readBytes(manifest[0].hash, data));
    App::TransactionLog::Composite c3;
    ASSERT_TRUE(c3.decode(data));
    ASSERT_EQ(c3.parts.size(), c1.parts.size());
    head = replayHead();
    size_t changed = 0;
    for (size_t i = 0; i < c3.parts.size(); ++i) {
        EXPECT_EQ(c3.parts[i].name, c1.parts[i].name);
        if (c3.parts[i].hash != c1.parts[i].hash) {
            ++changed;
            EXPECT_EQ(c3.parts[i].name, "Integer");
            EXPECT_EQ(c3.parts[i].hash, head.values[integerKey]);
        }
    }
    EXPECT_EQ(changed, 1u);
    ASSERT_TRUE(log().readValue(manifest[0].hash, composed));
    EXPECT_NE(composed.fragment, fromFile);
    EXPECT_NE(composed.fragment.find("<Integer value=\"12\"/>"), std::string::npos);

    ASSERT_TRUE(doc()->restoreVersion(2));
    auto restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Integer.getValue(), 11);
    EXPECT_STREQ(restored->String.getValue(), "eleven");
    ASSERT_TRUE(doc()->restoreVersion(3));
    restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Integer.getValue(), 12);

    // An undo is not an op yet (sec 12): what it changed is serialised
    // again by the next snapshot, and the snapshot is right.
    doc()->openTransaction("edit");
    restored->Integer.setValue(13);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(restored->Integer.getValue(), 12);
    ASSERT_EQ(doc()->snapshotToLog(), 4);
    manifest = store.manifest(4);
    ASSERT_TRUE(log().readValue(manifest[0].hash, composed));
    EXPECT_NE(composed.fragment.find("<Integer value=\"12\"/>"), std::string::npos);
    EXPECT_EQ(composed.fragment.find("<Integer value=\"13\"/>"), std::string::npos);

    Base::FileInfo(path).deleteFile();
}

namespace {

/// 800 lines of text, one of them changed when `changed` names it.
std::string blobText(int changed)
{
    std::string s;
    for (int i = 0; i < 800; ++i) {
        s += "line " + std::to_string(i) + " holds " + std::to_string(i == changed ? -1 : i * 7)
            + "\n";
    }
    return s;
}

std::string readFile(const std::string& path)
{
    Base::ifstream in(Base::FileInfo(path), std::ios::in | std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_F(TransactionLogTest, blobsAreEntitiesTheLogHolds)
{
    // Sec 23.16. Undo off: nothing but the property and the log holds a file.
    doc()->setUndoMode(0);
    const std::string tmp = Base::FileInfo::getTempPath();
    auto setContent = [&](App::DocumentObject* obj, const std::string& bytes) {
        const std::string src = tmp + "txnlog-blob.txt";
        {
            Base::ofstream out(Base::FileInfo(src), std::ios::out | std::ios::binary);
            out << bytes;
        }
        doc()->openTransaction("content");
        static_cast<App::PropertyFileIncluded*>(obj->getPropertyByName("File"))
            ->setValue(src.c_str(), "data.txt");
        doc()->commitTransaction();
        Base::FileInfo(src).deleteFile();
    };
    auto content = [&]() {
        auto obj = doc()->getObject("Obj");
        EXPECT_TRUE(obj);
        auto prop = obj ? dynamic_cast<App::PropertyFileIncluded*>(obj->getPropertyByName("File"))
                        : nullptr;
        EXPECT_TRUE(prop);
        return prop ? readFile(prop->getValue()) : std::string();
    };

    doc()->openTransaction("create");
    auto obj = make("Obj");
    ASSERT_TRUE(obj->addDynamicProperty("App::PropertyFileIncluded", "File"));
    doc()->commitTransaction();
    setContent(obj, blobText(-1));

    const std::string path = tmp + "txnlog-blob.FCStd";
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    auto& store = log().store();
    // A version's blob is an entity under the name the archive gives it,
    // stored as the file the log holds.
    auto blobOf = [&](int64_t num) {
        for (const auto& e : store.manifest(num)) {
            App::LogEntity x;
            if (store.getEntity(e.hash, x) && x.kind == "blob" && e.entry.find("Obj.File") == 0)
                return e;
        }
        return App::LogManifestEntry();
    };
    const auto b1 = blobOf(1);
    ASSERT_FALSE(b1.hash.empty());
    EXPECT_EQ(b1.entry, "Obj.File.txt");
    App::LogEntity e1;
    ASSERT_TRUE(store.getEntity(b1.hash, e1));
    EXPECT_EQ(e1.enc, "file");
    EXPECT_EQ(e1.size, blobText(-1).size());
    EXPECT_TRUE(log().heldBlob(b1.hash));

    // One line changes: the next version's file supersedes it, and the
    // older content is a patch toward the newer (sec 23.2), its file let go.
    setContent(obj, blobText(400));
    ASSERT_TRUE(doc()->save());
    const auto b2 = blobOf(2);
    ASSERT_FALSE(b2.hash.empty());
    EXPECT_NE(b2.hash, b1.hash);
    EXPECT_EQ(b2.entry, b1.entry);
    ASSERT_TRUE(store.getEntity(b1.hash, e1));
    EXPECT_EQ(e1.enc, "delta");
    EXPECT_EQ(e1.base, b2.hash);
    EXPECT_LT(e1.data.size() * 10, e1.size);
    EXPECT_FALSE(log().heldBlob(b1.hash));
    EXPECT_FALSE(doc()->getFileBlobManager().find(b1.hash));
    EXPECT_TRUE(log().heldBlob(b2.hash));
    // The op values name the same files, with an edge to them.
    size_t named = 0;
    for (auto& t : store.transactions()) {
        for (auto& o : store.ops(t.seq)) {
            if (o.prop != "File")
                continue;
            for (const auto& ref : {o.vbefore, o.vafter}) {
                App::LogEntity v;
                if (ref.empty() || !store.getEntity(ref, v))
                    continue;
                for (const auto& r : v.refs) {
                    if (r.role == "blob") {
                        EXPECT_TRUE(r.target == b1.hash || r.target == b2.hash) << r.target;
                        ++named;
                    }
                }
            }
        }
    }
    EXPECT_GE(named, 2u);

    // Both versions check out, the older one decoded from its patch; the
    // restore is a transaction the file's change is recorded in (sec 24.8).
    ASSERT_TRUE(doc()->restoreVersion(1));
    EXPECT_EQ(content(), blobText(-1));
    {
        auto last = store.transactions().back();
        EXPECT_EQ(last.kind, "restore");
        bool file = false;
        for (auto& o : store.ops(last.seq))
            file = file || (o.op == "set" && o.prop == "File");
        EXPECT_TRUE(file);
    }
    ASSERT_TRUE(doc()->restoreVersion(2));
    EXPECT_EQ(content(), blobText(400));

    // Something unrelated: no patch to be had, so the older stays a file.
    // With one unnamed version kept, the two before it go, and so do their
    // blobs -- but the ops still name them, so the entities stay.
    std::string noise;
    for (int i = 0; i < 20000; ++i)
        noise += static_cast<char>((i * 7919 + (i >> 3) * 104729) & 0xff);
    setContent(doc()->getObject("Obj"), noise);
    const long keep = App::DocumentParams::getTransactionLogKeepVersions();
    App::DocumentParams::setTransactionLogKeepVersions(1);
    ASSERT_EQ(doc()->snapshotToLog(), 3);
    App::DocumentParams::setTransactionLogKeepVersions(keep);
    EXPECT_EQ(store.versions().size(), 1u);
    App::LogEntity e2;
    ASSERT_TRUE(store.getEntity(b2.hash, e2));
    EXPECT_EQ(e2.enc, "file");
    EXPECT_TRUE(log().heldBlob(b2.hash));
    EXPECT_TRUE(store.hasEntity(b1.hash));

    // Dropping the ops drops the last thing naming them: the entities go,
    // and the log lets go of the file.
    store.truncate(log().lastSeq() + 1);
    EXPECT_FALSE(store.hasEntity(b1.hash));
    EXPECT_FALSE(store.hasEntity(b2.hash));
    EXPECT_FALSE(log().heldBlob(b2.hash));
    EXPECT_FALSE(doc()->getFileBlobManager().find(b2.hash));
    const auto b3 = blobOf(3);
    ASSERT_FALSE(b3.hash.empty());
    EXPECT_TRUE(log().heldBlob(b3.hash));

    Base::FileInfo(path).deleteFile();
}

namespace {

std::map<std::string, App::LogOp> setsByProp(App::TransactionStore& store, int64_t seq)
{
    std::map<std::string, App::LogOp> out;
    for (auto& o : store.ops(seq)) {
        if (o.op == "set")
            out[o.prop] = o;
    }
    return out;
}

}  // namespace

TEST_F(TransactionLogTest, undoAndRedoAreLoggedAsInverses)
{
    // docs/TransactionLog.md sec 24.2: an undo is a transaction of its own
    // whose ops are the undone one's with before and after swapped, naming
    // the row it inverts; a redo inverts the undo. Derived stays derived.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    doc()->recompute();

    doc()->openTransaction("edit");
    obj->Integer.setValue(7);
    doc()->recompute();   // inside the transaction: its outputs are part of the step
    doc()->commitTransaction();
    const int execs = obj->ExecCount.getValue();

    auto& store = log().store();
    const int64_t edit = store.transactions().back().seq;
    ASSERT_EQ(store.transactions().back().name, "edit");

    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 4711);
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 7);
    EXPECT_EQ(obj->ExecCount.getValue(), execs);
    log().resolvePending();

    auto txns = store.transactions(edit);
    ASSERT_EQ(txns.size(), 3u);
    EXPECT_EQ(txns[0].inverts, 0);
    EXPECT_EQ(txns[1].kind, "undo");
    EXPECT_EQ(txns[1].inverts, edit);
    EXPECT_EQ(txns[1].name, "edit");
    EXPECT_EQ(txns[2].kind, "redo");
    EXPECT_EQ(txns[2].inverts, txns[1].seq);

    auto forward = setsByProp(store, edit);
    auto undo = setsByProp(store, txns[1].seq);
    auto redo = setsByProp(store, txns[2].seq);
    for (const char* name : {"Integer", "ExecCount"}) {
        ASSERT_TRUE(forward.count(name)) << name;
        ASSERT_TRUE(undo.count(name)) << name;
        ASSERT_TRUE(redo.count(name)) << name;
        const auto& f = forward[name];
        EXPECT_EQ(f.vbefore.size(), 40u) << name;
        EXPECT_EQ(f.vafter.size(), 40u) << name;
        EXPECT_NE(f.vbefore, f.vafter) << name;
        // The inverse: no new value, the refs swapped.
        EXPECT_EQ(undo[name].vbefore, f.vafter) << name;
        EXPECT_EQ(undo[name].vafter, f.vbefore) << name;
        EXPECT_EQ(redo[name].vbefore, f.vbefore) << name;
        EXPECT_EQ(redo[name].vafter, f.vafter) << name;
    }
    EXPECT_FALSE(forward["Integer"].derived);
    EXPECT_TRUE(forward["ExecCount"].derived);
    EXPECT_FALSE(undo["Integer"].derived);
    EXPECT_TRUE(undo["ExecCount"].derived);
    EXPECT_TRUE(redo["ExecCount"].derived);

    // A recompute with no transaction open is an undo step of its own
    // (sec 24.1, TransactionOnRecompute ignored under the log).
    obj->touch();
    doc()->recompute();
    auto names = doc()->getAvailableUndoNames();
    ASSERT_FALSE(names.empty());
    EXPECT_NE(names.front().find("recompute"), std::string::npos) << names.front();
    EXPECT_EQ(doc()->getAvailableRedos(), 0);
}

TEST_F(TransactionLogTest, coldUndoRevertsFromTheLog)
{
    // docs/TransactionLog.md sec 24.3: past the hot window a step keeps its
    // name and log row only, and undoing it applies the row reversed from
    // the log -- objects recreated under their id and name, dynamic
    // properties added back, links restored.
    doc()->setMaxUndoStackSize(2);
    doc()->openTransaction("create");
    auto a = make("A");
    auto b = make("B");
    doc()->commitTransaction();
    const long idB = b->getID();

    doc()->openTransaction("link");
    a->Link.setValue(b);
    doc()->commitTransaction();

    doc()->openTransaction("dyn");
    auto note = b->addDynamicProperty("App::PropertyString", "Note", "Extra", "a note");
    ASSERT_TRUE(note);
    static_cast<App::PropertyString*>(note)->setValue("hi");
    doc()->commitTransaction();

    doc()->openTransaction("remove");
    a->Link.setValue(nullptr);
    doc()->removeObject("B");
    doc()->commitTransaction();

    for (int i = 1; i <= 4; ++i) {
        doc()->openTransaction("edit");
        a->Integer.setValue(i);
        doc()->commitTransaction();
    }
    ASSERT_EQ(doc()->getAvailableUndos(), 8);
    EXPECT_EQ(doc()->getAvailableUndoNames()[7], "create");

    auto check = [&](int undone) {
        SCOPED_TRACE(undone);
        a = static_cast<App::FeatureTest*>(doc()->getObject("A"));
        auto bb = static_cast<App::FeatureTest*>(doc()->getObject("B"));
        if (undone == 8) {
            EXPECT_FALSE(a);
            EXPECT_FALSE(bb);
            return;
        }
        ASSERT_TRUE(a);
        EXPECT_EQ(a->Integer.getValue(), undone >= 4 ? 4711 : 4 - undone);
        // Undone in order: four edits, the remove, the dynamic property,
        // the link, the create.
        EXPECT_EQ(bool(bb), undone >= 5);
        EXPECT_EQ(a->Link.getValue(), undone == 5 || undone == 6 ? bb : nullptr);
        if (bb) {
            EXPECT_EQ(bb->getID(), idB);
            auto p = bb->getPropertyByName("Note");
            EXPECT_EQ(bool(p), undone == 5);
            if (p) {
                EXPECT_STREQ(static_cast<App::PropertyString*>(p)->getValue(), "hi");
                EXPECT_STREQ(bb->getPropertyGroup(p), "Extra");
            }
        }
    };

    for (int round = 0; round < 2; ++round) {
        for (int i = 1; i <= 8; ++i) {
            ASSERT_TRUE(doc()->undo()) << "round " << round << " undo " << i;
            check(i);
        }
        EXPECT_FALSE(doc()->undo());
        for (int i = 7; i >= 0; --i) {
            ASSERT_TRUE(doc()->redo()) << "round " << round << " redo " << 8 - i;
            check(i);
        }
    }

    auto& store = log().store();
    int undos = 0, redos = 0;
    for (auto& t : store.transactions()) {
        if (t.kind == "undo")
            ++undos;
        else if (t.kind == "redo")
            ++redos;
        if (t.kind == "undo" || t.kind == "redo")
            EXPECT_GT(t.inverts, 0) << t.seq;
    }
    EXPECT_EQ(undos, 16);
    EXPECT_EQ(redos, 16);
}

TEST_F(TransactionLogTest, selectiveUndoRefusesWhatChangedSince)
{
    // docs/TransactionLog.md sec 24.4: a row that is not the tip is undone
    // by a new transaction when nothing since touched what it set, and
    // refused when something did.
    doc()->openTransaction("create");
    auto a = make("A");
    doc()->commitTransaction();
    auto lastSeq = [&]() { return log().store().transactions().back().seq; };

    doc()->openTransaction("int");
    a->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t intSeq = lastSeq();
    doc()->openTransaction("float");
    a->Float.setValue(2.5);
    doc()->commitTransaction();
    const int64_t floatSeq = lastSeq();

    // Integer untouched since: undone, Float kept.
    ASSERT_TRUE(doc()->undoLogged(intSeq));
    EXPECT_EQ(a->Integer.getValue(), 4711);
    EXPECT_DOUBLE_EQ(a->Float.getValue(), 2.5);
    auto row = log().store().transactions().back();
    EXPECT_EQ(row.kind, "undo");
    EXPECT_EQ(row.inverts, intSeq);
    EXPECT_EQ(row.name, "Undo int");
    EXPECT_EQ(doc()->getAvailableUndoNames().front(), "Undo int");

    // Undone already: the row's Integer is not what it left any more.
    EXPECT_FALSE(doc()->undoLogged(intSeq));

    // Float changed since: refused, nothing moves.
    doc()->openTransaction("float again");
    a->Float.setValue(3.5);
    doc()->commitTransaction();
    const size_t rows = log().store().transactions().size();
    EXPECT_FALSE(doc()->undoLogged(floatSeq));
    EXPECT_DOUBLE_EQ(a->Float.getValue(), 3.5);
    EXPECT_EQ(log().store().transactions().size(), rows);

    // The selective undo is an undo step like any: undo the float edit,
    // then the selective undo itself.
    ASSERT_TRUE(doc()->undo());
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(a->Integer.getValue(), 1);

    // A created object edited since cannot be uncreated.
    doc()->openTransaction("make B");
    auto b = make("B");
    doc()->commitTransaction();
    const int64_t makeSeq = lastSeq();
    doc()->openTransaction("edit B");
    b->Integer.setValue(9);
    doc()->commitTransaction();
    EXPECT_FALSE(doc()->undoLogged(makeSeq));
    EXPECT_TRUE(doc()->getObject("B"));

    // A derived value is left to recompute: the owner is touched.
    doc()->recompute();
    doc()->openTransaction("string");
    a->String.setValue("x");
    doc()->recompute();
    doc()->commitTransaction();
    const int64_t stringSeq = lastSeq();
    const int execs = a->ExecCount.getValue();
    ASSERT_TRUE(doc()->undoLogged(stringSeq));
    EXPECT_STRNE(a->String.getValue(), "x");
    EXPECT_EQ(a->ExecCount.getValue(), execs);
    EXPECT_TRUE(a->isTouched());
}

TEST_F(TransactionLogTest, coldUndoReachesViewProviders)
{
    // Sec 24.9: a view provider's change is logged under its object's id,
    // and a cold undo applies it through the resolver the Gui registers.
    std::map<const App::DocumentObject*, TxnLogFakeView*> views;
    App::Document::setViewResolver([&views](const App::DocumentObject* obj) {
        auto it = views.find(obj);
        return it == views.end() ? nullptr : static_cast<App::PropertyContainer*>(it->second);
    });
    // View state is undo state only under ViewObjectTransaction: without
    // it a view provider's change opens no transaction of its own.
    const bool viewTxn = App::DocumentParams::getViewObjectTransaction();
    App::DocumentParams::setViewObjectTransaction(true);
    struct Reset
    {
        bool viewTxn;
        ~Reset()
        {
            App::Document::setViewResolver({});
            App::DocumentParams::setViewObjectTransaction(viewTxn);
        }
    } reset {viewTxn};

    doc()->setMaxUndoStackSize(1);
    doc()->openTransaction("create");
    auto a = make("A");
    doc()->commitTransaction();
    TxnLogFakeView view;
    view.owner = a;
    views[a] = &view;

    doc()->openTransaction("shade");
    view.Shade.setValue(5);
    doc()->commitTransaction();
    auto& store = log().store();
    const auto shadeRow = store.transactions().back();
    ASSERT_EQ(shadeRow.name, "shade");
    auto ops = store.ops(shadeRow.seq);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].ckind, "view");
    EXPECT_EQ(ops[0].cid, a->getID());
    EXPECT_EQ(ops[0].prop, "Shade");

    for (int i = 1; i <= 2; ++i) {
        doc()->openTransaction("edit");
        a->Integer.setValue(i);
        doc()->commitTransaction();
    }
    ASSERT_TRUE(doc()->undo());
    ASSERT_TRUE(doc()->undo());   // cold
    EXPECT_EQ(view.Shade.getValue(), 5);
    ASSERT_TRUE(doc()->undo());   // cold: the view provider's change
    EXPECT_EQ(view.Shade.getValue(), 0);
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(view.Shade.getValue(), 5);

    // Selective undo of the view change once it is not the tip.
    doc()->openTransaction("edit");
    a->Integer.setValue(7);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->undoLogged(shadeRow.seq));
    EXPECT_EQ(view.Shade.getValue(), 0);
    EXPECT_EQ(a->Integer.getValue(), 7);
}

TEST_F(TransactionLogTest, viewChangesAreLoggedWhateverTheSetting)
{
    // Sec 24.10: with the log on, a view provider's saved property is
    // document data -- recorded with ViewObjectTransaction off, in an
    // implicit transaction of its own when none is open -- except while
    // DerivedViewWrites says the Gui is only fitting view state to the model.
    std::map<const App::DocumentObject*, TxnLogFakeView*> views;
    App::Document::setViewResolver([&views](const App::DocumentObject* obj) {
        auto it = views.find(obj);
        return it == views.end() ? nullptr : static_cast<App::PropertyContainer*>(it->second);
    });
    const bool viewTxn = App::DocumentParams::getViewObjectTransaction();
    App::DocumentParams::setViewObjectTransaction(false);
    struct Reset
    {
        bool viewTxn;
        ~Reset()
        {
            App::Document::setViewResolver({});
            App::DocumentParams::setViewObjectTransaction(viewTxn);
        }
    } reset {viewTxn};

    doc()->openTransaction("create");
    auto a = make("A");
    doc()->commitTransaction();
    TxnLogFakeView view;
    view.owner = a;
    views[a] = &view;
    auto& store = log().store();
    const size_t rows = store.transactions().size();

    view.Shade.setValue(3);   // no transaction open, the setting off
    EXPECT_TRUE(doc()->hasPendingTransaction());
    doc()->commitTransaction();
    auto txns = store.transactions();
    ASSERT_EQ(txns.size(), rows + 1);
    EXPECT_EQ(txns.back().kind, "implicit");
    auto ops = store.ops(txns.back().seq);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].ckind, "view");
    EXPECT_EQ(ops[0].prop, "Shade");
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(view.Shade.getValue(), 0);
    ASSERT_TRUE(doc()->redo());

    {
        App::DerivedViewWrites derived;
        view.Shade.setValue(4);
    }
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_EQ(store.transactions().size(), rows + 3);   // the undo and the redo rows
}

TEST_F(TransactionLogTest, pythonObjectValuesAreCapturedOnTheMainThread)
{
    // Sec 24.10: a Proxy's value is pickled through the interpreter; the
    // copy the undo system took has no container, and its Save must not
    // need one, nor run on the worker.
    doc()->openTransaction("create");
    auto obj = doc()->addObject("App::FeaturePython", "FP");
    doc()->commitTransaction();
    ASSERT_TRUE(obj);
    auto proxy = dynamic_cast<App::PropertyPythonObject*>(obj->getPropertyByName("Proxy"));
    ASSERT_TRUE(proxy);
    for (const char* cls : {"First", "Second"}) {
        doc()->openTransaction(cls);
        {
            Base::PyGILStateLocker lock;
            Base::Interpreter().runString(
                (std::string("class ") + cls + ":\n    pass\n").c_str());
            proxy->setValue(Base::Interpreter().runStringObject((std::string(cls) + "()").c_str()));
        }
        doc()->commitTransaction();
    }
    log().resolvePending();
    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 2u);
    bool found = false;
    for (auto& o : store.ops(txns.back().seq)) {
        if (o.prop != "Proxy")
            continue;
        found = true;
        App::CapturedValue before;
        ASSERT_TRUE(log().readValue(o.vbefore, before));
        EXPECT_NE(before.fragment.find("First"), std::string::npos) << before.fragment;
    }
    EXPECT_TRUE(found);
}

TEST_F(TransactionLogTest, recomputeRecordSurvivesARemovalWithUndoOff)
{
    // With undo off the commit at the end of a recompute deletes its
    // implicit transaction, and with it an object the recompute removed;
    // the recompute record was read from the recomputed objects after that
    // commit, a use-after-free the CAM suite crashed on with the log on.
    doc()->setUndoMode(0);
    {
        Base::PyGILStateLocker lock;
        Base::Interpreter().runString("class TxnLogRemover:\n"
                                      "    def execute(self, obj):\n"
                                      "        d = obj.Document\n"
                                      "        if d.getObject('Temp'):\n"
                                      "            d.removeObject('Temp')\n");
    }
    auto temp = make("Temp");
    auto remover = doc()->addObject("App::FeaturePython", "Remover");
    ASSERT_TRUE(temp && remover);
    {
        Base::PyGILStateLocker lock;
        auto proxy = dynamic_cast<App::PropertyPythonObject*>(remover->getPropertyByName("Proxy"));
        ASSERT_TRUE(proxy);
        proxy->setValue(Base::Interpreter().runStringObject("TxnLogRemover()"));
    }
    doc()->commitTransaction();
    remover->touch();
    temp->touch();
    doc()->recompute();
    EXPECT_FALSE(doc()->getObject("Temp"));
    // The record is written and names what was recomputed; the removal,
    // which a recompute defers past its end, is logged after it.
    auto& store = log().store();
    bool record = false, removal = false;
    for (auto& t : store.transactions()) {
        if (t.kind == "recompute" && t.script.find("Remover") != std::string::npos)
            record = true;
        for (auto& o : store.ops(t.seq)) {
            if (o.op == "remove" && o.cname == "Temp")
                removal = record;
        }
    }
    EXPECT_TRUE(record);
    EXPECT_TRUE(removal);
}

TEST_F(TransactionLogTest, recomputeRecordSurvivesAnObserverRemovingWithUndoOff)
{
    // An observer of signalRecomputed may delete what was recomputed -- with
    // undo off, at once -- and the record was read after it ran: the use
    // after free CAMTests.TestPathHelix crashed on with the log on, where a
    // Python observer clears the document (docs/TransactionLog.md sec 24.12).
    doc()->setUndoMode(0);
    auto temp = make("Temp");
    ASSERT_TRUE(temp);
    doc()->commitTransaction();
    auto connection = doc()->signalRecomputed.connect(
        [](const App::Document& d, const std::vector<App::DocumentObject*>&) {
            auto& owner = const_cast<App::Document&>(d);
            if (owner.getObject("Temp"))
                owner.removeObject("Temp");
        });
    temp->touch();
    doc()->recompute();
    connection.disconnect();
    EXPECT_FALSE(doc()->getObject("Temp"));
    bool record = false;
    for (auto& t : log().store().transactions()) {
        if (t.kind == "recompute" && t.script.find("Temp") != std::string::npos)
            record = true;
    }
    EXPECT_TRUE(record);
}

TEST_F(TransactionLogTest, implicitTransactionClosesWhenUndoModeChanges)
{
    // Writes made with undo off open an implicit transaction for the log.
    // Turning undo on before it closed made them an undo step the user never
    // had: one create and one rename became two steps in
    // OmniControl.test_documentReachOfEditOps (docs/TransactionLog.md sec
    // 24.13). The mode change closes it under the mode it was opened in.
    doc()->setUndoMode(0);
    auto obj = make("Obj");
    ASSERT_TRUE(obj);
    EXPECT_TRUE(doc()->hasPendingTransaction());
    doc()->setUndoMode(1);
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_EQ(doc()->getAvailableUndos(), 0);
    auto txns = log().store().transactions();
    ASSERT_FALSE(txns.empty());
    EXPECT_EQ(txns.back().kind, "implicit");

    doc()->openTransaction("Rename");
    obj->Integer.setValue(3);
    doc()->commitTransaction();
    EXPECT_EQ(doc()->getAvailableUndos(), 1);
    EXPECT_TRUE(doc()->undo());
    EXPECT_EQ(doc()->getAvailableUndos(), 0);
    EXPECT_TRUE(doc()->getObject("Obj"));

    // And the other way: an implicit step opened with undo on is one, and
    // turning undo off clears it with the rest.
    obj->Integer.setValue(4);
    EXPECT_TRUE(doc()->hasPendingTransaction());
    doc()->setUndoMode(0);
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_EQ(doc()->getAvailableUndos(), 0);
}

TEST_F(TransactionLogTest, implicitTransactionIsNotMirroredIntoTheActiveDocument)
{
    // A bare write on a document that is not the active one mirrored its
    // implicit transaction into the active document as "-> <implicit>",
    // which was not implicit and so never closed with it: an empty step in
    // someone else's undo list (DocumentObserverCases.testDocument,
    // docs/TransactionLog.md sec 24.13).
    auto obj = make("Obj");
    ASSERT_TRUE(obj);
    doc()->commitTransaction();
    std::string otherName = App::GetApplication().getUniqueDocumentName("txnother");
    auto other = App::GetApplication().newDocument(otherName.c_str(), "testUser");
    other->setUndoMode(1);
    App::GetApplication().setActiveDocument(other);
    ASSERT_EQ(App::GetApplication().getActiveDocument(), other);

    obj->Integer.setValue(7);
    EXPECT_TRUE(doc()->hasPendingTransaction());
    EXPECT_FALSE(other->hasPendingTransaction());
    App::GetApplication().commitImplicitTransactions();
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_FALSE(other->hasPendingTransaction());
    EXPECT_EQ(other->getAvailableUndos(), 0);
    App::GetApplication().closeDocument(otherName.c_str());
}

TEST_F(TransactionLogTest, commitWritesItsAfterValuesAndKeepsTheCopy)
{
    // sec 25.4: every commit's after values are copied and written with it,
    // and the copy of a set property is what the next write's undo record
    // takes instead of copying again.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    EXPECT_EQ(log().pendingCount(), 0u);

    doc()->openTransaction("first");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    EXPECT_EQ(log().pendingCount(), 0u);
    // The set's after copy is kept for the next write.
    EXPECT_EQ(App::TransactionCopyCache::size(), 1u);

    doc()->openTransaction("second");
    obj->Integer.setValue(2);
    EXPECT_EQ(App::TransactionCopyCache::size(), 0u);   // taken by the undo record
    doc()->commitTransaction();

    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_EQ(txns.size(), 3u);
    auto first = store.ops(txns[1].seq);
    auto second = store.ops(txns[2].seq);
    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0].vafter.size(), 40u);
    EXPECT_EQ(second[0].vafter.size(), 40u);
    // The adopted copy is the first commit's after: the same value.
    EXPECT_EQ(second[0].vbefore, first[0].vafter);
    App::CapturedValue v;
    ASSERT_TRUE(log().readValue(second[0].vafter, v));
    EXPECT_NE(v.fragment.find("value=\"2\""), std::string::npos) << v.fragment;

    // Undo restores from the adopted copy.
    EXPECT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 1);
    EXPECT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 2);

    // A write nothing records drops the kept copy (NoModify: no
    // transaction opens for it).
    doc()->openTransaction("third");
    obj->Integer.setValue(3);
    doc()->commitTransaction();
    EXPECT_EQ(App::TransactionCopyCache::size(), 1u);
    obj->Integer.setStatus(App::Property::NoModify, true);
    obj->Integer.setValue(4);
    obj->Integer.setStatus(App::Property::NoModify, false);
    EXPECT_FALSE(doc()->hasPendingTransaction());
    EXPECT_EQ(App::TransactionCopyCache::size(), 0u);
}

TEST_F(TransactionLogTest, blobStoreRecoversALeftoverDirectory)
{
    // docs/TransactionLog.md sec 25.2 item 5: what a crashed session left in
    // its blob directory is taken over -- the newest complete generation of
    // each segment, never one the crash cut short -- and handed back by hash.
    auto& source = doc()->getFileBlobManager();
    const std::string a(6000, 'a'), b(7000, 'b'), c(8000, 'c'), d(9000, 'd');
    auto ha = source.adoptBytes(a, "bin");
    auto hb = source.adoptBytes(b, "bin");
    source.flush();
    const std::string from = doc()->TransientDir.getStrValue() + "/blobs";
    auto readFile = [](const std::string& path) {
        Base::ifstream in(Base::FileInfo(path), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    auto writeFile = [](const std::string& path, const std::string& bytes) {
        Base::ofstream out(Base::FileInfo(path), std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    ASSERT_TRUE(Base::FileInfo(from + "/seg-1.1").exists());
    const std::string first = readFile(from + "/seg-1.1");
    auto hc = source.adoptBytes(c, "bin");
    source.flush();   // seg-1.2: a, b and c
    ASSERT_TRUE(Base::FileInfo(from + "/seg-1.2").exists());
    const std::string second = readFile(from + "/seg-1.2");

    std::string otherName = App::GetApplication().getUniqueDocumentName("txnrecover");
    auto other = App::GetApplication().newDocument(otherName.c_str(), "testUser");
    const std::string to = other->TransientDir.getStrValue() + "/blobs";
    Base::FileInfo(to).createDirectory();
    // The crash left both generations, a segment cut short, and a loose file.
    writeFile(to + "/seg-1.1", first);
    writeFile(to + "/seg-1.2", second);
    writeFile(to + "/seg-2.1", second.substr(0, second.size() / 2));
    writeFile(to + "/loose.dat", d);

    auto& target = other->getFileBlobManager();
    EXPECT_EQ(target.recoverStore(), 4u);
    EXPECT_FALSE(Base::FileInfo(to + "/seg-1.1").exists());   // superseded
    EXPECT_FALSE(Base::FileInfo(to + "/seg-2.1").exists());   // incomplete
    EXPECT_TRUE(Base::FileInfo(to + "/seg-1.2").exists());
    for (const std::string* bytes : {&a, &b, &c, &d}) {
        auto blob = target.recovered(App::FileBlobManager::hashBytes(*bytes));
        ASSERT_TRUE(blob);
        std::string back;
        ASSERT_TRUE(blob->read(back));
        EXPECT_EQ(back, *bytes);
    }
    EXPECT_FALSE(target.recovered(App::FileBlobManager::hashBytes("nothing")));
    target.endRecovery();
    App::GetApplication().closeDocument(otherName.c_str());
}

TEST_F(TransactionLogTest, recoversACrashedSessionFromItsLog)
{
    // docs/TransactionLog.md sec 25: the transient directory a crashed
    // session leaves is enough to rebuild its document -- the newest version
    // with the log's tail replayed over it -- and the history carries on.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    obj->String.setValue("anchored");
    doc()->commitTransaction();
    ASSERT_GT(doc()->snapshotToLog(), 0);   // the anchor

    doc()->openTransaction("set");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    doc()->openTransaction("second");
    auto second = make("Second");
    second->addDynamicProperty("App::PropertyString", "Note", "Recovery");
    static_cast<App::PropertyString*>(second->getPropertyByName("Note"))->setValue("tail");
    second->Float.setValue(2.5);
    doc()->commitTransaction();
    doc()->openTransaction("gone");
    make("Gone");
    doc()->commitTransaction();
    doc()->openTransaction("remove");
    doc()->removeObject("Gone");
    doc()->commitTransaction();
    doc()->openTransaction("undone");
    obj->Integer.setValue(99);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->undo());
    ASSERT_EQ(obj->Integer.getValue(), 2);
    log().flush();
    const auto undoNames = doc()->getAvailableUndoNames();
    const auto redoNames = doc()->getAvailableRedoNames();

    // The crash: the directory as it stands, with nothing closed.
    const std::string crashed = Base::FileInfo::getTempPath() + "txnlog-crashed";
    Base::FileInfo(crashed).deleteDirectoryRecursive();
    for (const char* sub : {"history", "blobs"}) {
        const std::string from = doc()->TransientDir.getStrValue() + "/" + sub;
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
    EXPECT_FALSE(Base::FileInfo(crashed).exists());
    auto robj = dynamic_cast<App::FeatureTest*>(recovered->getObject("Obj"));
    auto rsecond = dynamic_cast<App::FeatureTest*>(recovered->getObject("Second"));
    ASSERT_TRUE(robj);
    ASSERT_TRUE(rsecond);
    EXPECT_FALSE(recovered->getObject("Gone"));
    EXPECT_EQ(robj->getID(), obj->getID());
    EXPECT_EQ(rsecond->getID(), second->getID());
    EXPECT_EQ(robj->Integer.getValue(), 2);
    EXPECT_STREQ(robj->String.getValue(), "anchored");
    EXPECT_DOUBLE_EQ(rsecond->Float.getValue(), 2.5);
    auto note = dynamic_cast<App::PropertyString*>(rsecond->getPropertyByName("Note"));
    ASSERT_TRUE(note);
    EXPECT_STREQ(note->getValue(), "tail");

    // The history carries on: the stacks the session had, as cold steps.
    EXPECT_EQ(recovered->getAvailableUndoNames(), undoNames);
    EXPECT_EQ(recovered->getAvailableRedoNames(), redoNames);
    EXPECT_TRUE(recovered->redo());
    EXPECT_EQ(robj->Integer.getValue(), 99);
    EXPECT_TRUE(recovered->undo());
    EXPECT_TRUE(recovered->undo());   // "remove": Gone comes back
    EXPECT_TRUE(recovered->getObject("Gone"));
    bool record = false;
    for (auto& t : recovered->getTransactionLog()->store().transactions()) {
        if (t.kind == "recover")
            record = true;
    }
    EXPECT_TRUE(record);
    App::GetApplication().closeDocument(name.c_str());
}

TEST_F(TransactionLogTest, branchesAreChainsInTheStore)
{
    // docs/TransactionLog.md sec 26 (4.a): a branch is a named tip into the
    // parent tree; appending on it moves its head, and its history is the
    // chain from there, whatever else the store holds.
    const std::string path = Base::FileInfo::getTempFileName("txnlog-branches") + ".db";
    {
        auto store = App::TransactionStore::openSQLite(path);
        App::LogBranch main;
        ASSERT_TRUE(store->findBranch("main", main));
        EXPECT_EQ(main.id, 1);
        EXPECT_EQ(main.head, 0);

        auto row = [&](int64_t parent, int64_t branch, const char* prop, const char* value) {
            App::LogTransaction t;
            t.parent = parent;
            t.branch = branch;
            t.kind = "user";
            t.name = prop;
            std::vector<App::LogOp> ops(1);
            ops[0].op = "set";
            ops[0].ckind = "obj";
            ops[0].cid = 7;
            ops[0].prop = prop;
            ops[0].vafter = value;
            return store->append(t, ops);
        };
        const int64_t a1 = row(0, 1, "A", "a1");
        const int64_t a2 = row(a1, 1, "B", "b1");
        App::LogBranch side;
        side.name = "side";
        side.fromSeq = a2;
        side.head = a2;
        side.idBase = 100000;
        const int64_t sideId = store->addBranch(side);
        EXPECT_EQ(sideId, 2);
        const int64_t m3 = row(a2, 1, "A", "a2");        // main goes on
        const int64_t s3 = row(a2, sideId, "B", "b2");   // side forks off a2
        const int64_t s4 = row(s3, sideId, "A", "a3");
        const int64_t m4 = row(m3, 1, "B", "b3");

        ASSERT_TRUE(store->getBranch(1, main));
        EXPECT_EQ(main.head, m4);
        ASSERT_TRUE(store->getBranch(sideId, side));
        EXPECT_EQ(side.head, s4);
        EXPECT_EQ(side.idBase, 100000);
        App::LogBranch dup;
        dup.name = "side";
        EXPECT_THROW(store->addBranch(dup), Base::Exception);

        auto seqs = [](const std::vector<App::LogTransaction>& rows) {
            std::vector<int64_t> out;
            for (const auto& t : rows)
                out.push_back(t.seq);
            return out;
        };
        EXPECT_EQ(seqs(store->chain(m4)), (std::vector<int64_t> {a1, a2, m3, m4}));
        EXPECT_EQ(seqs(store->chain(s4)), (std::vector<int64_t> {a1, a2, s3, s4}));
        EXPECT_EQ(seqs(store->chain(s4, a2 + 1)), (std::vector<int64_t> {s3, s4}));
        for (const auto& t : store->chain(s4, s3))
            EXPECT_EQ(t.branch, sideId);

        // The newest op on a property, on one branch's chain or on any.
        App::LogOp op;
        ASSERT_TRUE(store->lastOpOn("obj", 7, "A", a1, m4, op));
        EXPECT_EQ(op.vafter, "a2");
        ASSERT_TRUE(store->lastOpOn("obj", 7, "A", a1, s4, op));
        EXPECT_EQ(op.vafter, "a3");
        ASSERT_TRUE(store->lastOpOn("obj", 7, "B", a2, m4, op));
        EXPECT_EQ(op.vafter, "b3");
        EXPECT_FALSE(store->lastOpOn("obj", 7, "B", s3, s4, op));
        ASSERT_TRUE(store->lastOpOn("obj", 7, "A", a1, 0, op));
        EXPECT_EQ(op.txn, s4);

        EXPECT_TRUE(store->renameBranch(sideId, "renamed"));
        EXPECT_TRUE(store->findBranch("renamed", side));
        EXPECT_FALSE(store->renameBranch(99, "x"));
    }
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, schema4StoreMovesOntoMain)
{
    // A store written before branches (schema 4) opens with every row and
    // version on `main`, whose head is the newest row.
    const std::string path = Base::FileInfo::getTempFileName("txnlog-schema4") + ".db";
    {
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
        const char* sql =
            "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);"
            "INSERT INTO meta VALUES('schema','4');"
            "CREATE TABLE txn(seq INTEGER PRIMARY KEY, parent INTEGER, id INTEGER, kind TEXT,"
            " origin TEXT, name TEXT, time REAL, script TEXT, session INTEGER,"
            " inverts INTEGER DEFAULT 0);"
            "INSERT INTO txn VALUES(1,0,0,'user','','one',10.0,'',1,0);"
            "INSERT INTO txn VALUES(2,1,0,'user','','two',11.0,'',1,0);"
            "INSERT INTO txn VALUES(3,2,0,'save','','save',12.0,'',1,0);"
            "CREATE TABLE version(num INTEGER PRIMARY KEY, uuid TEXT, branch TEXT, kind TEXT,"
            " name TEXT, seq INTEGER, env INTEGER, docxml_hash TEXT, schema INTEGER,"
            " created REAL);"
            "INSERT INTO version VALUES(1,'u','main','unnamed','',2,0,'h',5,12.0);";
        char* err = nullptr;
        EXPECT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, &err), SQLITE_OK) << (err ? err : "");
        sqlite3_free(err);
        sqlite3_close(db);
    }
    {
        // Read-only, as the embedded guard opens a copy (sec 16.4): read
        // as it is, nothing migrated, and no throw.
        Base::FileInfo(path).setPermissions(Base::FileInfo::ReadOnly);
        auto store = App::TransactionStore::openSQLite(path);
        EXPECT_EQ(store->getMeta("schema"), "4");
    }
    Base::FileInfo(path).setPermissions(Base::FileInfo::ReadWrite);
    {
        auto store = App::TransactionStore::openSQLite(path);
        EXPECT_EQ(store->getMeta("schema"), "5");
        auto branches = store->branches();
        ASSERT_EQ(branches.size(), 1u);
        EXPECT_EQ(branches[0].name, "main");
        EXPECT_EQ(branches[0].head, 3);
        EXPECT_DOUBLE_EQ(branches[0].created, 10.0);
        for (const auto& t : store->transactions())
            EXPECT_EQ(t.branch, 1);
        EXPECT_EQ(store->chain(3).size(), 3u);
        App::LogVersion v;
        ASSERT_TRUE(store->getVersion(1, v));
        EXPECT_EQ(v.branch, 1);
    }
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, branchesSwitchInPlace)
{
    // docs/TransactionLog.md sec 26 (4.b): a branch from the head leaves the
    // document and its steps as they are; a switch makes the document the
    // other branch's head in place, with that branch's own steps; object
    // ids never collide across branches.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    doc()->openTransaction("edit");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    const auto mainUndos = doc()->getAvailableUndoNames();
    ASSERT_EQ(mainUndos.size(), 2u);

    const int64_t side = doc()->createBranch("side");
    ASSERT_EQ(side, 2);
    EXPECT_EQ(log().branch(), side);
    EXPECT_EQ(obj->Integer.getValue(), 2);
    EXPECT_EQ(doc()->getAvailableUndoNames(), mainUndos);   // still hot, still valid
    App::LogBranch sideRow;
    ASSERT_TRUE(log().store().getBranch(side, sideRow));
    ASSERT_GT(sideRow.fromVersion, 0);
    App::LogVersion forkVersion;
    ASSERT_TRUE(log().store().getVersion(sideRow.fromVersion, forkVersion));
    EXPECT_EQ(forkVersion.kind, "named");
    EXPECT_GT(sideRow.idBase, obj->getID() + 65535);
    EXPECT_THROW(doc()->createBranch("side"), Base::Exception);

    doc()->openTransaction("side edit");
    obj->Integer.setValue(3);
    auto sideOnly = make("SideOnly");
    sideOnly->String.setValue("side");
    doc()->commitTransaction();
    const long sideOnlyId = sideOnly->getID();
    EXPECT_GT(sideOnlyId, sideRow.idBase);
    const auto sideUndos = doc()->getAvailableUndoNames();

    ASSERT_TRUE(doc()->switchBranch("main"));
    EXPECT_EQ(log().branch(), 1);
    EXPECT_EQ(obj->Integer.getValue(), 2);   // the same object, in place
    EXPECT_FALSE(doc()->getObject("SideOnly"));
    EXPECT_EQ(doc()->getAvailableUndoNames(), mainUndos);
    EXPECT_EQ(doc()->getAvailableRedos(), 0);

    doc()->openTransaction("main edit");
    obj->Integer.setValue(4);
    auto mainOnly = make("MainOnly");
    doc()->commitTransaction();
    EXPECT_LT(mainOnly->getID(), sideRow.idBase);

    ASSERT_TRUE(doc()->switchBranch("side"));
    EXPECT_EQ(obj->Integer.getValue(), 3);
    EXPECT_FALSE(doc()->getObject("MainOnly"));
    auto back = dynamic_cast<App::FeatureTest*>(doc()->getObject("SideOnly"));
    ASSERT_TRUE(back);
    EXPECT_EQ(back->getID(), sideOnlyId);
    EXPECT_STREQ(back->String.getValue(), "side");
    EXPECT_EQ(doc()->getAvailableUndoNames(), sideUndos);

    // Undo and redo are the branch's own, cold, and logged on it.
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 2);
    EXPECT_FALSE(doc()->getObject("SideOnly"));
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 3);
    ASSERT_TRUE(doc()->getObject("SideOnly"));
    doc()->openTransaction("side again");
    auto sideTwo = make("SideTwo");
    doc()->commitTransaction();
    EXPECT_GT(sideTwo->getID(), sideOnlyId);   // the side's counter went on

    ASSERT_TRUE(doc()->switchBranch("main"));
    EXPECT_EQ(obj->Integer.getValue(), 4);
    EXPECT_TRUE(doc()->getObject("MainOnly"));
    EXPECT_FALSE(doc()->getObject("SideTwo"));

    // Each branch's rows are its chain; the switches are records on them.
    auto& store = log().store();
    for (const auto& t : store.chain(log().head())) {
        if (t.kind == "switch")
            EXPECT_EQ(t.branch, 1);
        EXPECT_NE(t.name, "side edit");
    }
    App::LogBranch sideNow;
    ASSERT_TRUE(store.getBranch(side, sideNow));
    bool sawSideEdit = false;
    for (const auto& t : store.chain(sideNow.head))
        sawSideEdit = sawSideEdit || t.name == "side edit";
    EXPECT_TRUE(sawSideEdit);
    EXPECT_THROW(doc()->switchBranch("nowhere"), Base::Exception);
}

TEST_F(TransactionLogTest, branchFromAnOlderVersion)
{
    // A branch from a version behind the head checks that version out; the
    // one it came from stays as it was.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    ASSERT_GT(v1, 0);
    doc()->openTransaction("later");
    obj->Integer.setValue(7);
    make("Later");
    doc()->commitTransaction();

    const int64_t old = doc()->createBranch("old", v1);
    ASSERT_GT(old, 0);
    EXPECT_EQ(obj->Integer.getValue(), 1);
    EXPECT_FALSE(doc()->getObject("Later"));
    App::LogVersion v;
    ASSERT_TRUE(log().store().getVersion(v1, v));
    EXPECT_EQ(v.kind, "named");
    // Undo reaches the rows since the document opened that this branch has:
    // the create, not "later".
    const auto undos = doc()->getAvailableUndoNames();
    ASSERT_EQ(undos.size(), 1u);
    EXPECT_EQ(undos.front(), "create");

    doc()->openTransaction("old edit");
    obj->Integer.setValue(8);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));
    EXPECT_EQ(obj->Integer.getValue(), 7);
    EXPECT_TRUE(doc()->getObject("Later"));
    ASSERT_TRUE(doc()->switchBranch("old"));
    EXPECT_EQ(obj->Integer.getValue(), 8);
    EXPECT_FALSE(doc()->getObject("Later"));
}

TEST_F(TransactionLogTest, evictionKeepsEachBranchsNewest)
{
    // Sec 26.2 item 5: the newest version of every branch outlives the
    // limit, so a switch checks out a version of its own branch.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    const long keep = App::DocumentParams::getTransactionLogKeepVersions();
    App::DocumentParams::setTransactionLogKeepVersions(1);
    doc()->createBranch("side");   // snapshots main's tip, named as the fork
    doc()->openTransaction("side");
    obj->Integer.setValue(5);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));   // snapshots side's tip
    for (int i = 0; i < 3; ++i) {
        doc()->openTransaction("edit");
        obj->Integer.setValue(10 + i);
        doc()->commitTransaction();
        ASSERT_GT(doc()->snapshotToLog(), 0);
    }
    App::DocumentParams::setTransactionLogKeepVersions(keep);
    std::map<int64_t, int> perBranch;
    for (const auto& v : log().store().versions())
        ++perBranch[v.branch];
    EXPECT_TRUE(perBranch.count(2));   // side's tip survived main's snapshots
    ASSERT_TRUE(doc()->switchBranch("side"));
    EXPECT_EQ(obj->Integer.getValue(), 5);
}

TEST_F(TransactionLogTest, recoveryContinuesOnTheBranch)
{
    // A crash on a branch recovers that branch's head, and the log goes on
    // on it.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    doc()->createBranch("side");
    doc()->openTransaction("side");
    obj->Integer.setValue(6);
    doc()->commitTransaction();
    log().flush();

    const std::string crashed = Base::FileInfo::getTempPath() + "txnlog-crashed-branch";
    Base::FileInfo(crashed).deleteDirectoryRecursive();
    for (const char* sub : {"history", "blobs"}) {
        const std::string from = doc()->TransientDir.getStrValue() + "/" + sub;
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
    auto robj = dynamic_cast<App::FeatureTest*>(recovered->getObject("Obj"));
    ASSERT_TRUE(robj);
    EXPECT_EQ(robj->Integer.getValue(), 6);
    EXPECT_EQ(recovered->getTransactionLog()->branch(), 2);
    ASSERT_TRUE(recovered->switchBranch("main"));
    EXPECT_EQ(robj->Integer.getValue(), 1);
    App::GetApplication().closeDocument(name.c_str());
}
