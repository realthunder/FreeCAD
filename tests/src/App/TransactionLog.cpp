// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log, phase 1 (docs/TransactionLog.md sec 15): what a
// commit writes to the store, the pending-after rule of sec 20.2, and the
// promise that matters -- a log replays to an identical document.

#include "gtest/gtest.h"

#include <iterator>
#include <map>
#include <zipios++/zipfile.h>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/DocumentParams.h"
#include "App/FeatureTest.h"
#include "App/TransactionLog.h"
#include "App/TransactionValue.h"
#include "Base/FileInfo.h"
#include <src/App/InitApplication.h>

namespace {

class TransactionLogTest: public ::testing::Test
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
    // One pending set per persisted property follows the create; by now
    // the one on Integer has been resolved by the next transaction's copy.
    size_t sets = 0;
    for (auto& o : ops) {
        if (o.op == "set") {
            ++sets;
            EXPECT_TRUE(o.vbefore.empty());
            if (o.prop == "Integer")
                EXPECT_EQ(o.vafter.size(), 40u);
            else
                EXPECT_TRUE(o.vafter.empty()) << o.prop;
        }
    }
    EXPECT_GT(sets, 5u);

    // The set: before is the copy the undo system took, after pending.
    ops = store.ops(txns[1].seq);
    ASSERT_EQ(ops.size(), 1u);
    EXPECT_EQ(ops[0].op, "set");
    EXPECT_EQ(ops[0].prop, "Integer");
    EXPECT_EQ(ops[0].vbefore.size(), 40u);
    EXPECT_TRUE(ops[0].vafter.empty());
    EXPECT_FALSE(ops[0].derived);
    // ... and that before resolved the create's pending set on Integer.
    ops = store.ops(txns[0].seq);
    for (auto& o : ops) {
        if (o.op == "set" && o.prop == "Integer")
            EXPECT_EQ(o.vafter.size(), 40u);
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
    // The after ref of the set above is pending until the snapshot.
    EXPECT_GT(log().pendingCount(), 0u);

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
    EXPECT_EQ(v.branch, "main");
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
    App::CapturedValue stored;
    ASSERT_TRUE(log().readValue(v.docxml_hash, stored));
    EXPECT_EQ(stored.fragment, fromFile);

    auto manifest = store.manifest(v.num);
    ASSERT_GE(manifest.size(), 1u);
    EXPECT_EQ(manifest[0].entry, "Document.xml");
    EXPECT_EQ(manifest[0].hash, v.docxml_hash);
    EXPECT_EQ(manifest[0].source, "value");

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
    EXPECT_TRUE(store.hasValue(v.docxml_hash));

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
    // `snapshot` record naming it; the pending after refs resolve first.
    EXPECT_GT(log().pendingCount(), 0u);
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
    EXPECT_FALSE(store.hasValue(docxml[0]));
    EXPECT_FALSE(store.hasValue(docxml[1]));
    EXPECT_TRUE(store.hasValue(docxml[2]));
    EXPECT_FALSE(store.hasValue(docxml[3]));
    // Ops are never evicted, nor their values: the edits' before refs read.
    for (auto& t : store.transactions()) {
        for (auto& o : store.ops(t.seq)) {
            if (!o.vbefore.empty())
                EXPECT_TRUE(store.hasValue(o.vbefore)) << t.seq;
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

    // With SplitXML on, the object's data is its own entry, in the
    // manifest with the rest.
    {
        bool split = false;
        for (auto& e : log().store().manifest(1))
            split = split || e.entry == "Obj.xml";
        EXPECT_EQ(split, doc()->SplitXML.getValue());
    }
    // Back to version 1: the object as it was, the later one gone, the
    // checkout recorded, and the log going on from there.
    ASSERT_TRUE(doc()->restoreVersion(1));
    auto restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Integer.getValue(), 1);
    EXPECT_STREQ(restored->String.getValue(), "one");
    EXPECT_FALSE(doc()->getObject("Later"));
    auto& store = log().store();
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 1u);
    EXPECT_EQ(txns.back().kind, "checkout");
    EXPECT_NE(txns.back().script.find("\"version\":1"), std::string::npos);
    EXPECT_EQ(store.versions().size(), 2u);   // a checkout is not a new version
    EXPECT_EQ(log().pendingCount(), 0u);

    doc()->openTransaction("after");
    restored->Integer.setValue(3);
    doc()->commitTransaction();
    txns = store.transactions();
    EXPECT_EQ(txns.back().name, "after");
    EXPECT_EQ(txns[txns.size() - 2].kind, "checkout");

    // Forward again, to version 2.
    ASSERT_TRUE(doc()->restoreVersion(2));
    restored = static_cast<App::FeatureTest*>(doc()->getObject("Obj"));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->Integer.getValue(), 2);
    EXPECT_TRUE(doc()->getObject("Later"));
    EXPECT_THROW(doc()->restoreVersion(99), Base::Exception);
}

}  // namespace
