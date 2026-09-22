// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log, phase 1 (docs/TransactionLog.md sec 15): what a
// commit writes to the store, the pending-after rule of sec 20.2, and the
// promise that matters -- a log replays to an identical document.

#include "gtest/gtest.h"

#include <map>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/DocumentParams.h"
#include "App/FeatureTest.h"
#include "App/TransactionLog.h"
#include "App/TransactionValue.h"
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

}  // namespace
