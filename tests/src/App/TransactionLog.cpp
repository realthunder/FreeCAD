// SPDX-License-Identifier: LGPL-2.1-or-later

// The transaction log, phase 1 (docs/TransactionLog.md sec 15): what a
// commit writes to the store, the pending-after rule of sec 20.2, and the
// promise that matters -- a log replays to an identical document.

#include "gtest/gtest.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <sqlite3.h>
#include <zipios++/zipfile.h>

#include "App/Actor.h"
#include "App/Application.h"
#include "Base/Interpreter.h"
#include "App/PropertyPythonObject.h"
#include "App/PropertyExpressionEngine.h"
#include "App/ObjectIdentifier.h"
#include "App/AutoTransaction.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/DocumentParams.h"
#include "App/Expression.h"
#include "App/FeatureTest.h"
#include "App/FileBlobManager.h"
#include "App/FileHistory.h"
#include "App/PropertyFile.h"
#include "App/PropertyHistory.h"
#include "App/TransactionLog.h"
#include "App/TransactionValue.h"
#include "App/Transactions.h"
#include "Base/Console.h"
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

    /// Close the fixture's document and give it a fresh one, for a case that
    /// needs the document gone in the middle.
    void closeAndRenew()
    {
        App::GetApplication().closeDocument(_docName.c_str());
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _doc->setUndoMode(1);
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
    // Identity is off by default: nothing personal in the row -- the
    // desktop user is `host` (sec 30.4 P3), the machine not named.
    auto users = store.users();
    ASSERT_EQ(users.size(), 1u);
    EXPECT_EQ(sessions[0].user, users[0].id);
    EXPECT_EQ(users[0].kind, "local");
    EXPECT_EQ(users[0].name, "host");
    EXPECT_TRUE(sessions[0].host.empty());
    EXPECT_TRUE(sessions[0].access.empty());
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
    // Sec 27.53: by id and time only -- the name is the file's name table's,
    // the environment the session's -- and only what was recomputed.
    EXPECT_EQ(rec.script.find("\"name\""), std::string::npos) << rec.script;
    EXPECT_EQ(rec.script.find("\"env\""), std::string::npos) << rec.script;
    EXPECT_NE(rec.script.find(",\"s\":"), std::string::npos) << rec.script;
    EXPECT_EQ(rec.script.find("error"), std::string::npos);
    auto other = make("Other");
    doc()->recompute();
    obj->Integer.setValue(4);
    doc()->recompute();
    // What was made up to date, not every object looked at: `Other` was
    // not touched.
    const auto second = store.transactions().back();
    ASSERT_EQ(second.kind, "recompute");
    EXPECT_NE(second.script.find("{\"id\":" + std::to_string(obj->getID()) + ","),
              std::string::npos) << second.script;
    EXPECT_EQ(second.script.find("{\"id\":" + std::to_string(other->getID()) + ","),
              std::string::npos) << second.script;
    // Nothing touched: no record at all.
    const auto count = store.transactions().size();
    doc()->recompute();
    EXPECT_EQ(store.transactions().size(), count);
    EXPECT_TRUE(store.ops(rec.seq).empty());
    // The implicit transaction holding the write comes before the record.
    EXPECT_EQ(txns[txns.size() - 2].kind, "implicit");
}

TEST_F(TransactionLogTest, touchedStateInTheRows)
{
    // Sec 27.58: a set records the touched state before it, and a recompute
    // record each object's before and, when not clean, after. Sec 27.59: a
    // recompute run inside a transaction is recorded after its row.
    using Obj = App::DocumentObject;
    auto& store = log().store();
    doc()->openTransaction("create");
    auto obj = make("Obj");
    doc()->commitTransaction();
    obj->touch();
    doc()->recompute();
    ASSERT_EQ(obj->getLogTouchedBits(), 0);
    ASSERT_FALSE(obj->Integer.isTouched());

    doc()->openTransaction("edit");
    obj->Integer.setValue(5);
    const int atRecompute = obj->getLogTouchedBits();
    doc()->recompute();
    doc()->commitTransaction();
    EXPECT_EQ(obj->getLogTouchedBits(), 0);
    auto txns = store.transactions();
    ASSERT_GE(txns.size(), 2u);
    const auto& rec = txns.back();
    const auto& edit = txns[txns.size() - 2];
    ASSERT_EQ(rec.kind, "recompute");
    EXPECT_EQ(edit.name, "edit");
    EXPECT_EQ(rec.parent, edit.seq);
    int integerBefore = -2;
    int derivedBefore = -2;
    for (const auto& o : store.ops(edit.seq)) {
        if (o.op == "set" && o.prop == "Integer")
            integerBefore = o.touched;
        if (o.op == "set" && o.derived)
            derivedBefore = o.touched;
    }
    EXPECT_EQ(integerBefore, 0);
    // A derived write's state is the record's.
    EXPECT_EQ(derivedBefore, -1);
    const std::string id = "{\"id\":" + std::to_string(obj->getID()) + ",";
    const auto at = rec.script.find(id);
    ASSERT_NE(at, std::string::npos) << rec.script;
    const std::string entry = rec.script.substr(at, rec.script.find('}', at) - at);
    EXPECT_NE(entry.find(",\"b\":" + std::to_string(atRecompute) + ",\"p\":[\"Integer\"]"),
              std::string::npos) << entry;
    EXPECT_EQ(entry.find("\"a\""), std::string::npos) << entry;

    // Two edits and no recompute: the second's before is the first's after.
    doc()->openTransaction("one");
    obj->Integer.setValue(6);
    doc()->commitTransaction();
    const int afterOne = obj->getLogTouchedBits();
    EXPECT_TRUE(afterOne & Obj::LogTouch);
    doc()->openTransaction("two");
    obj->Integer.setValue(7);
    doc()->commitTransaction();
    txns = store.transactions();
    ASSERT_EQ(txns.back().name, "two");
    int second = -2;
    for (const auto& o : store.ops(txns.back().seq)) {
        if (o.op == "set" && o.prop == "Integer")
            second = o.touched;
    }
    EXPECT_EQ(second, afterOne | Obj::LogPropTouched);

    // An aborted transaction takes its recompute record with it.
    const auto count = store.transactions().size();
    doc()->openTransaction("dropped");
    obj->Integer.setValue(8);
    doc()->recompute();
    doc()->abortTransaction();
    EXPECT_EQ(store.transactions().size(), count);
}

TEST_F(TransactionLogTest, undoAndRedoLeaveTheRowsTouchedState)
{
    // Sec 27.63: an undo leaves every object touched as it was before the
    // step -- recomputes after the step taken back too -- and a redo as it
    // was before the undo, hot or cold; not touched by every write the undo
    // made. And the undo's row records the state it left, which a restore
    // through the rows reads.
    using State = std::map<std::string, std::pair<int, std::vector<std::string>>>;
    auto state = [&]() {
        State s;
        for (auto obj : doc()->getObjects()) {
            std::vector<App::Property*> props;
            obj->getPropertyList(props);
            std::vector<std::string> touched;
            // The bit: a link's isTouched() is also its target's revision.
            for (auto p : props) {
                if (p->hasTouchedBit())
                    touched.emplace_back(p->getName());
            }
            std::sort(touched.begin(), touched.end());
            s[obj->getNameInDocument()] = {obj->getLogTouchedBits(), touched};
        }
        return s;
    };
    // What differs, for the failure message.
    auto diff = [](const State& got, const State& want) {
        std::ostringstream out;
        for (const auto& kv : want) {
            auto it = got.find(kv.first);
            if (it == got.end()) {
                out << " " << kv.first << " missing;";
                continue;
            }
            if (it->second.first != kv.second.first)
                out << " " << kv.first << " bits " << it->second.first << " want "
                    << kv.second.first << ";";
            for (const auto& p : it->second.second) {
                if (!std::count(kv.second.second.begin(), kv.second.second.end(), p))
                    out << " " << kv.first << "." << p << " touched;";
            }
            for (const auto& p : kv.second.second) {
                if (!std::count(it->second.second.begin(), it->second.second.end(), p))
                    out << " " << kv.first << "." << p << " clean;";
            }
        }
        for (const auto& kv : got) {
            if (!want.count(kv.first))
                out << " " << kv.first << " extra;";
        }
        return out.str();
    };
    for (int cold = 0; cold < 2; ++cold) {
        SCOPED_TRACE(cold ? "cold" : "hot");
        if (cold)
            closeAndRenew();
        doc()->setMaxUndoStackSize(cold ? 1 : 20);
        // Each operation: the undo count before and after it, and the
        // state before it.
        struct Before
        {
            int count;
            int after;
            State state;
        };
        std::vector<Before> before;
        auto op = [&](const std::function<void()>& fn) {
            before.push_back({doc()->getAvailableUndos(), 0, state()});
            fn();
            before.back().after = doc()->getAvailableUndos();
        };
        App::FeatureTest* a {};
        App::FeatureTest* b {};
        op([&]() {
            doc()->openTransaction("create");
            a = make("A");
            b = make("B");
            b->Link.setValue(a);
            doc()->commitTransaction();
        });
        op([&]() { doc()->recompute(); });
        op([&]() {
            doc()->openTransaction("one");
            a->Integer.setValue(1);
            doc()->commitTransaction();
        });
        op([&]() { doc()->recompute(); });
        op([&]() {
            doc()->openTransaction("two");
            a->Integer.setValue(2);
            doc()->commitTransaction();
        });
        op([&]() {
            doc()->openTransaction("three");
            b->Integer.setValue(3);
            doc()->recompute();
            doc()->commitTransaction();
        });
        op([&]() {
            doc()->openTransaction("four");
            a->Integer.setValue(4);
            doc()->commitTransaction();
        });
        const int steps = doc()->getAvailableUndos();
        ASSERT_GE(steps, 5);
        // The state the undo to `count` steps must leave: the one before the
        // operation that made step count + 1.
        auto expected = [&](int count) {
            for (const auto& o : before) {
                if (o.count == count && o.after > count)
                    return o.state;
            }
            ADD_FAILURE() << "no operation made step " << count + 1;
            return State();
        };
        std::vector<State> beforeUndo(steps + 1);
        for (int count = steps - 1; count >= 0; --count) {
            beforeUndo[count + 1] = state();
            ASSERT_TRUE(doc()->undo()) << count;
            EXPECT_EQ(diff(state(), expected(count)), "") << "undo to " << count;
        }
        for (int count = 1; count <= steps; ++count) {
            ASSERT_TRUE(doc()->redo()) << count;
            EXPECT_EQ(diff(state(), beforeUndo[count]), "") << "redo to " << count;
        }
        for (int count = steps - 1; count >= 0; --count) {
            ASSERT_TRUE(doc()->undo()) << count;
            EXPECT_EQ(diff(state(), expected(count)), "") << "undo again to " << count;
        }
        // Each undo's row carries the state it left.
        int recorded = 0;
        for (const auto& t : log().store().transactions()) {
            if ((t.kind == "undo" || t.kind == "redo")
                    && t.script.find("\"objects\"") != std::string::npos)
                ++recorded;
        }
        EXPECT_GE(recorded, 3 * (steps - 1));
    }
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
    // The copy is taken before the save's own record; the restore follows.
    // The save's stamps are bookkeeping, no transaction (sec 27.5).
    EXPECT_EQ(txns.back().seq, seqBefore + 1);
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
    store.addVersion(v, {{"Document.xml", hBase}});
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

    // The recompute's record follows the row it ran in (sec 27.59).
    auto& store = log().store();
    const auto rows = store.transactions();
    ASSERT_GE(rows.size(), 2u);
    EXPECT_EQ(rows.back().kind, "recompute");
    const int64_t edit = rows[rows.size() - 2].seq;
    ASSERT_EQ(rows[rows.size() - 2].name, "edit");

    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 4711);
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 7);
    EXPECT_EQ(obj->ExecCount.getValue(), execs);
    log().resolvePending();

    std::vector<App::LogTransaction> txns;
    for (auto& t : store.transactions(edit)) {
        if (t.kind != "recompute")
            txns.push_back(t);
    }
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
    // The newest row that is not a recompute's record, which follows the
    // row it ran in (sec 27.59).
    auto lastSeq = [&]() {
        const auto rows = log().store().transactions();
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            if (it->kind != "recompute")
                return it->seq;
        }
        return int64_t(0);
    };

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
    const std::string removerId = "{\"id\":" + std::to_string(remover->getID()) + ",";
    doc()->recompute();
    EXPECT_FALSE(doc()->getObject("Temp"));
    // The record is written and names what was recomputed; the removal,
    // which a recompute defers past its end, is logged after it.
    auto& store = log().store();
    bool record = false, removal = false;
    for (auto& t : store.transactions()) {
        if (t.kind == "recompute" && t.script.find(removerId) != std::string::npos)
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
    const std::string tempId = "{\"id\":" + std::to_string(temp->getID()) + ",";
    doc()->recompute();
    connection.disconnect();
    EXPECT_FALSE(doc()->getObject("Temp"));
    bool record = false;
    for (auto& t : log().store().transactions()) {
        if (t.kind == "recompute" && t.script.find(tempId) != std::string::npos)
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

TEST_F(TransactionLogTest, compositeAndManifestHoldWithoutEdges)
{
    // docs/TransactionLog.md sec 27.53-27.54: the hashes of `entity` and
    // `ref` are 20-byte blobs; a composite has no edge per value and a
    // version no manifest rows -- the collector reads both lists from the
    // entities themselves.
    const std::string path = Base::FileInfo::getTempFileName("txnlog-held") + ".db";
    const std::string skel(40, 'a'), part(40, 'b'), orphan(40, 'd');
    const std::string data = "skeleton " + skel + "\nc Obj\np 3 " + part + " Integer\n";
    const std::string comp = App::hashBytes(data);
    std::string list;
    {
        auto store = App::TransactionStore::openSQLite(path);
        auto put = [&](const std::string& hash, const char* kind, const std::string& bytes,
                       std::vector<App::LogRef> refs = {}) {
            App::LogEntity e;
            e.hash = hash;
            e.kind = kind;
            e.enc = "raw";
            e.tier = "durable";
            e.size = bytes.size();
            e.data = bytes;
            e.refs = std::move(refs);
            store->putEntity(e);
        };
        put(skel, "skeleton", "<a>");
        put(part, "prop", "1");
        put(orphan, "prop", "2");
        put(comp, "composite", data, {App::LogRef {skel, "skeleton", ""}});
        App::LogVersion v;
        v.kind = "named";
        store->addVersion(v, {{"Document.xml", comp}});
        list = v.manifest;
        ASSERT_FALSE(list.empty());

        auto manifest = store->manifest(v.num);
        ASSERT_EQ(manifest.size(), 1u);
        EXPECT_EQ(manifest[0].entry, "Document.xml");
        EXPECT_EQ(manifest[0].hash, comp);
        App::LogEntity e;
        ASSERT_TRUE(store->getEntity(list, e));
        EXPECT_EQ(e.kind, "manifest");
        ASSERT_TRUE(store->getEntity(comp, e));
        ASSERT_EQ(e.refs.size(), 1u);
        EXPECT_EQ(e.refs[0].target, skel);
        auto raw = store->entitiesStoredAs("raw");
        EXPECT_NE(std::find(raw.begin(), raw.end(), part), raw.end());

        // A collection: the composite's value is held through its data and
        // the composite through the manifest; the orphan goes.
        store->truncate(0);
        EXPECT_TRUE(store->hasEntity(list));
        EXPECT_TRUE(store->hasEntity(comp));
        EXPECT_TRUE(store->hasEntity(skel));
        EXPECT_TRUE(store->hasEntity(part));
        EXPECT_FALSE(store->hasEntity(orphan));
        store->dropTier("durable", {});
        EXPECT_TRUE(store->hasEntity(part));
        // As stored: 20 bytes each (the store's own connection is in WAL
        // mode, which a second reader sees through).
        {
            sqlite3* db = nullptr;
            ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
            sqlite3_stmt* s = nullptr;
            ASSERT_EQ(sqlite3_prepare_v2(db,
                                         "SELECT typeof(hash), length(hash) FROM entity"
                                         " UNION ALL SELECT typeof(entity), length(entity) FROM ref"
                                         " UNION ALL SELECT typeof(target), length(target) FROM ref",
                                         -1, &s, nullptr),
                      SQLITE_OK);
            int rows = 0;
            while (sqlite3_step(s) == SQLITE_ROW) {
                ++rows;
                EXPECT_STREQ(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)), "blob");
                EXPECT_EQ(sqlite3_column_int(s, 1), 20);
            }
            sqlite3_finalize(s);
            EXPECT_EQ(rows, 6);   // four entities, one edge
            sqlite3_close(db);
        }
        // The version gone, everything it held goes with it.
        store->evictVersion(v.num);
        EXPECT_FALSE(store->hasEntity(list));
        EXPECT_FALSE(store->hasEntity(comp));
        EXPECT_FALSE(store->hasEntity(part));
    }
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, dropTierWithEvictionIsTheTwoCalls)
{
    // docs/TransactionLog.md sec 27.69: the embedded copy's retention --
    // evict the versions, drop the cache tier -- as one collection keeps
    // exactly what the two calls one after the other keep.
    const std::string pathA = Base::FileInfo::getTempFileName("txnlog-tier") + ".db";
    const std::string pathB = Base::FileInfo::getTempFileName("txnlog-tier") + ".db";
    auto h = [](char c) { return std::string(40, c); };
    // Version 1 (evicted) lists p1 and the cache value p2; version 2 (kept)
    // lists p3 and the cache value k2. The ops name p2, k1, k3's delta d3,
    // k4 and k5; d4, the delta on k4, only version 1's composite lists.
    const std::string p1 = h('1'), p2 = h('2'), p3 = h('3'), k1 = h('4'), k2 = h('5');
    const std::string k3 = h('6'), d3 = h('7'), k4 = h('8'), d4 = h('9'), k5 = h('a');
    const std::string a5 = h('b'), skel = h('c');
    const std::string data1 = "skeleton " + skel + "\nc Obj\np 0 " + p1 + " A\np 1 " + p2
        + " B\np 2 " + d4 + " C\n";
    const std::string data2 = "skeleton " + skel + "\nc Obj\np 0 " + p3 + " A\np 1 " + k2
        + " B\n";
    const std::string c1 = App::hashBytes(data1), c2 = App::hashBytes(data2);
    int64_t v1 = 0;
    {
        auto store = App::TransactionStore::openSQLite(pathA);
        auto put = [&](const std::string& hash, const char* kind, const char* tier,
                       const std::string& bytes, const std::string& base = {},
                       std::vector<App::LogRef> refs = {}) {
            App::LogEntity e;
            e.hash = hash;
            e.kind = kind;
            e.enc = base.empty() ? "raw" : "delta";
            e.base = base;
            e.tier = tier;
            e.size = bytes.size();
            e.data = bytes;
            e.refs = std::move(refs);
            store->putEntity(e);
        };
        put(skel, "skeleton", "durable", "<a>");
        put(p1, "prop", "durable", "1");
        put(p2, "prop", "cache", "2");
        put(p3, "prop", "durable", "3");
        put(k1, "prop", "cache", "4");
        put(k2, "prop", "cache", "5");
        put(k3, "prop", "cache", "6");
        put(d3, "prop", "durable", "7", k3);
        put(k4, "prop", "cache", "8");
        put(d4, "prop", "durable", "9", k4);
        put(a5, "attach", "durable", "b");
        put(k5, "prop", "cache", "a", {}, {App::LogRef {a5, "attach", "file"}});
        put(c1, "composite", "durable", data1, {}, {App::LogRef {skel, "skeleton", ""}});
        put(c2, "composite", "durable", data2, {}, {App::LogRef {skel, "skeleton", ""}});
        App::LogVersion v;
        v.kind = "unnamed";
        store->addVersion(v, {{"Document.xml", c1}});
        v1 = v.num;
        App::LogVersion w;
        w.kind = "named";
        store->addVersion(w, {{"Document.xml", c2}});
        App::LogTransaction t;
        t.kind = "user";
        std::vector<App::LogOp> ops;
        for (const auto& value : {p2, k1, d3, k4, k5}) {
            App::LogOp op;
            op.op = "set";
            op.ckind = "obj";
            op.cid = 7;
            op.prop = "P";
            op.vafter = value;
            ops.push_back(op);
        }
        store->append(t, ops);
        store->copyTo(pathB);
        store->evictVersions({v1});
        store->dropTier("cache", {});
    }
    {
        auto store = App::TransactionStore::openSQLite(pathB);
        store->dropTier("cache", {v1});
    }
    auto contents = [](const std::string& path) {
        std::set<std::string> out;
        sqlite3* db = nullptr;
        EXPECT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        EXPECT_EQ(sqlite3_prepare_v2(db,
                                     "SELECT 'e ' || hex(hash) FROM entity"
                                     " UNION ALL SELECT 'r ' || hex(entity) || hex(target) || role"
                                     " FROM ref UNION ALL SELECT 'v ' || num FROM version",
                                     -1, &s, nullptr),
                  SQLITE_OK);
        while (sqlite3_step(s) == SQLITE_ROW)
            out.insert(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
        sqlite3_finalize(s);
        sqlite3_close(db);
        return out;
    };
    auto entity = [](const std::string& hash) {
        std::string upper = hash;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return "e " + upper;
    };
    const auto a = contents(pathA);
    const auto b = contents(pathB);
    EXPECT_EQ(a, b);
    // Kept: version 2's composite and what it lists, the cache value it
    // lists, and the cache base of a kept delta.
    for (const auto& kept : {c2, skel, p3, k2, d3, k3})
        EXPECT_TRUE(b.count(entity(kept))) << kept;
    // Gone: version 1 and what only it held; the cache values the ops alone
    // name, and what only they reached; the cache base of a delta gone.
    for (const auto& gone : {c1, p1, p2, k1, k5, a5, d4, k4})
        EXPECT_FALSE(b.count(entity(gone))) << gone;
    Base::FileInfo(pathA).deleteFile();
    Base::FileInfo(pathB).deleteFile();
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
    EXPECT_EQ(sideRow.idBase, 0);   // no stride: one counter for the file (sec 27.40)
    EXPECT_THROW(doc()->createBranch("side"), Base::Exception);

    doc()->openTransaction("side edit");
    obj->Integer.setValue(3);
    auto sideOnly = make("SideOnly");
    sideOnly->String.setValue("side");
    doc()->commitTransaction();
    const long sideOnlyId = sideOnly->getID();
    EXPECT_GT(sideOnlyId, obj->getID());
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
    EXPECT_GT(mainOnly->getID(), sideOnlyId);   // past the other branch's

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

TEST_F(TransactionLogTest, trimAndDeleteBranches)
{
    // docs/TransactionLog.md sec 16.7, 26 (4.e): a trim never takes what
    // another branch's history holds; a delete takes what only the branch
    // holds; what stays still checks out.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    ASSERT_GT(v1, 0);
    for (int i = 2; i <= 3; ++i) {
        doc()->openTransaction("edit");
        obj->Integer.setValue(i);
        doc()->commitTransaction();
    }
    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    obj->Integer.setValue(10);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));
    doc()->openTransaction("main edit");
    obj->Integer.setValue(4);
    doc()->commitTransaction();
    auto& store = log().store();
    const size_t before = store.transactions().size();

    // main's rows up to v1 are side's history too: nothing goes, v1 is named.
    EXPECT_EQ(doc()->trimBranch("main", v1), 0u);
    App::LogVersion v;
    ASSERT_TRUE(store.getVersion(v1, v));
    EXPECT_EQ(v.kind, "named");
    EXPECT_THROW(doc()->deleteBranch("main"), Base::Exception);   // the one we are on
    EXPECT_THROW(doc()->deleteBranch("nowhere"), Base::Exception);

    // Deleting side takes its own rows and its tip version, not the fork.
    App::LogBranch side;
    ASSERT_TRUE(store.findBranch("side", side));
    const int64_t fork = side.fromVersion;
    EXPECT_GT(doc()->deleteBranch("side"), 0u);
    EXPECT_FALSE(store.findBranch("side", side));
    EXPECT_EQ(store.branches().size(), 1u);
    for (const auto& t : store.transactions())
        EXPECT_NE(t.name, "side edit");
    ASSERT_TRUE(store.getVersion(fork, v));
    for (const auto& ver : store.versions())
        EXPECT_EQ(ver.branch, 1);

    // Now main's first rows are its alone: trimmed to v1, and the document
    // still branches from v1 and switches back.
    EXPECT_GT(doc()->trimBranch("main", v1), 0u);
    for (const auto& t : store.transactions())
        EXPECT_NE(t.name, "create");
    EXPECT_LT(store.transactions().size(), before);
    const auto undos = doc()->getAvailableUndoNames();
    EXPECT_EQ(std::count(undos.begin(), undos.end(), std::string("create")), 0);
    EXPECT_EQ(obj->Integer.getValue(), 4);
    doc()->createBranch("again", v1);
    EXPECT_EQ(obj->Integer.getValue(), 1);
    ASSERT_TRUE(doc()->switchBranch("main"));
    EXPECT_EQ(obj->Integer.getValue(), 4);

    // Trimmed to its head without a bridge (sec 27.71): every row only main
    // holds goes, the document stays as it is, and there is nothing left to
    // undo.
    EXPECT_GT(doc()->trimBranch("main", 0, false), 0u);
    EXPECT_EQ(obj->Integer.getValue(), 4);
    EXPECT_EQ(doc()->getAvailableUndos(), 0);
    // No rows from here to `again`: the version is read whole, into a
    // document joined to this file's history (sec 27.60), not a scratch
    // one with a store of its own.
    std::vector<std::string> read;
    auto connection = App::GetApplication().signalFinishRestoreDocument.connect(
        [&](const App::Document& d) {
            if (&d == doc())
                return;
            read.emplace_back(d.getName());
            EXPECT_TRUE(d.testStatus(App::Document::VersionDoc)) << d.getName();
            EXPECT_EQ(&d.getFileHistory(), &doc()->getFileHistory()) << d.getName();
            EXPECT_FALSE(d.getTransactionLog()) << d.getName();
        });
    ASSERT_TRUE(doc()->switchBranch("again"));
    connection.disconnect();
    EXPECT_EQ(read.size(), 1u);
    for (const auto& name : read)
        EXPECT_FALSE(App::GetApplication().getDocument(name.c_str())) << name;
    EXPECT_EQ(obj->Integer.getValue(), 1);
    ASSERT_TRUE(doc()->switchBranch("main"));
    EXPECT_EQ(obj->Integer.getValue(), 4);
    bool record = false;
    for (const auto& t : store.transactions())
        record = record || t.kind == "trim";
    EXPECT_TRUE(record);
}

TEST_F(TransactionLogTest, trimLeavesABridgeToABranchForkedBelow)
{
    // docs/TransactionLog.md sec 27.71 (user): where another branch shares
    // the history, a trim squashes the rows from the newest shared one up to
    // the kept version into that version's row, so the two histories still
    // meet in rows and a switch walks them -- no version is read whole.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    make("Old")->String.setValue("old");
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    ASSERT_GT(v1, 0);
    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    obj->Integer.setValue(10);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));
    for (int i = 2; i <= 4; ++i) {
        doc()->openTransaction("main edit");
        obj->Integer.setValue(i);
        doc()->commitTransaction();
    }
    doc()->openTransaction("main objects");
    make("New")->String.setValue("new");
    doc()->removeObject("Old");
    doc()->commitTransaction();
    auto& store = log().store();
    App::LogVersion fork;
    ASSERT_TRUE(store.getVersion(v1, fork));

    EXPECT_GT(doc()->trimBranch("main"), 0u);
    EXPECT_EQ(doc()->getAvailableUndos(), 0);
    App::LogBranch main, side;
    ASSERT_TRUE(store.findBranch("main", main));
    ASSERT_TRUE(store.findBranch("side", side));
    // The bridge hangs off the newest row side's history holds: at or
    // after the fork, before side's own.
    std::set<int64_t> onSide;
    for (const auto& t : store.chain(side.head))
        onSide.insert(t.seq);
    const auto chain = store.chain(main.head);
    int squash = 0;
    for (const auto& t : chain) {
        EXPECT_NE(t.name, "main edit");
        if (t.kind == "squash") {
            ++squash;
            EXPECT_TRUE(onSide.count(t.parent)) << t.parent;
            EXPECT_GE(t.parent, fork.seq);
        }
    }
    EXPECT_EQ(squash, 1);
    ASSERT_FALSE(chain.empty());
    EXPECT_EQ(chain.front().parent, 0);   // whole down to the start

    std::vector<std::string> read;
    auto connection = App::GetApplication().signalFinishRestoreDocument.connect(
        [&](const App::Document& d) {
            if (&d != doc())
                read.emplace_back(d.getName());
        });
    ASSERT_TRUE(doc()->switchBranch("side"));
    EXPECT_EQ(obj->Integer.getValue(), 10);
    EXPECT_TRUE(doc()->getObject("Old"));
    EXPECT_FALSE(doc()->getObject("New"));
    ASSERT_TRUE(doc()->switchBranch("main"));
    connection.disconnect();
    EXPECT_TRUE(read.empty()) << read.front();
    EXPECT_EQ(obj->Integer.getValue(), 4);
    EXPECT_FALSE(doc()->getObject("Old"));
    auto made = dynamic_cast<App::FeatureTest*>(doc()->getObject("New"));
    ASSERT_TRUE(made);
    EXPECT_EQ(std::string(made->String.getValue()), "new");
}

TEST_F(TransactionLogTest, squashFoldsTheNetChange)
{
    // docs/TransactionLog.md sec 16.7 (4.e): the rows between two versions
    // become one transaction of their net change, which undoes -- cold, from
    // the log -- and redoes like any.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    make("Old")->String.setValue("old");
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    ASSERT_GT(v1, 0);

    doc()->openTransaction("edit");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    doc()->openTransaction("temp");
    make("Temp");
    doc()->commitTransaction();
    doc()->openTransaction("untemp");
    doc()->removeObject("Temp");
    doc()->commitTransaction();
    doc()->openTransaction("note");
    obj->addDynamicProperty("App::PropertyString", "Note", "Squash");
    static_cast<App::PropertyString*>(obj->getPropertyByName("Note"))->setValue("n");
    doc()->commitTransaction();
    doc()->openTransaction("kept");
    make("Kept")->String.setValue("k");
    doc()->commitTransaction();
    doc()->openTransaction("drop old");
    doc()->removeObject("Old");
    doc()->commitTransaction();
    const int64_t mid = doc()->snapshotToLog();
    doc()->openTransaction("edit again");
    obj->Integer.setValue(3);
    doc()->commitTransaction();
    const int64_t v2 = doc()->snapshotToLog();
    ASSERT_GT(v2, mid);
    const long keptId = doc()->getObject("Kept")->getID();

    auto& store = log().store();
    EXPECT_THROW(doc()->squashVersions(v2, v1), Base::Exception);
    ASSERT_TRUE(store.nameVersion(mid, "mid"));
    EXPECT_THROW(doc()->squashVersions(v1, v2), Base::Exception);   // a named one between
    ASSERT_TRUE(store.nameVersion(mid, ""));

    EXPECT_GT(doc()->squashVersions(v1, v2), 5u);
    App::LogVersion first, last, gone;
    ASSERT_TRUE(store.getVersion(v1, first));
    ASSERT_TRUE(store.getVersion(v2, last));
    EXPECT_FALSE(store.getVersion(mid, gone));
    const auto rows = store.chain(last.seq, first.seq + 1);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].kind, "squash");
    EXPECT_EQ(rows[0].parent, first.seq);
    std::map<std::string, int> counts;
    bool tempMentioned = false;
    for (const auto& o : store.ops(rows[0].seq)) {
        ++counts[o.op];
        tempMentioned = tempMentioned || o.cname == "Temp";
    }
    EXPECT_FALSE(tempMentioned);   // born and gone inside: nothing
    EXPECT_EQ(counts["create"], 1);
    EXPECT_EQ(counts["remove"], 1);
    EXPECT_EQ(counts["addprop"], 1);

    // The document is what it was; the squash is one step, undone cold.
    EXPECT_EQ(obj->Integer.getValue(), 3);
    const auto undos = doc()->getAvailableUndoNames();
    ASSERT_FALSE(undos.empty());
    EXPECT_EQ(undos.front().rfind("Squash", 0), 0u);
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(obj->Integer.getValue(), 1);
    EXPECT_FALSE(obj->getPropertyByName("Note"));
    EXPECT_FALSE(doc()->getObject("Kept"));
    auto old = dynamic_cast<App::FeatureTest*>(doc()->getObject("Old"));
    ASSERT_TRUE(old);
    EXPECT_STREQ(old->String.getValue(), "old");
    EXPECT_FALSE(doc()->getObject("Temp"));
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(obj->Integer.getValue(), 3);
    auto note = dynamic_cast<App::PropertyString*>(obj->getPropertyByName("Note"));
    ASSERT_TRUE(note);
    EXPECT_STREQ(note->getValue(), "n");
    auto kept = dynamic_cast<App::FeatureTest*>(doc()->getObject("Kept"));
    ASSERT_TRUE(kept);
    EXPECT_EQ(kept->getID(), keptId);
    EXPECT_STREQ(kept->String.getValue(), "k");
    EXPECT_FALSE(doc()->getObject("Old"));

    // A branch forked between two versions keeps them from being squashed.
    const int64_t v3 = doc()->snapshotToLog();
    doc()->openTransaction("more");
    obj->Integer.setValue(5);
    doc()->commitTransaction();
    doc()->createBranch("fork");
    doc()->openTransaction("fork edit");
    obj->Integer.setValue(6);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));
    doc()->openTransaction("main more");
    obj->Integer.setValue(7);
    doc()->commitTransaction();
    const int64_t v4 = doc()->snapshotToLog();
    EXPECT_THROW(doc()->squashVersions(v3, v4), Base::Exception);
}

TEST_F(TransactionLogTest, fileHistoryIsTheFilesAndLivesInItsDirectory)
{
    // Sec 27.7, 5.b: the blob store and the log live in the history, in
    // the document's transient directory, registered under the file once
    // the document has one, and gone -- directory and registration -- with
    // the last document of the file.
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-filehistory.FCStd";
    Base::FileInfo(path).deleteFile();
    auto& history = doc()->getFileHistory();
    EXPECT_EQ(history.directory(), doc()->TransientDir.getStrValue());
    EXPECT_EQ(&history.blobs(), &doc()->getFileBlobManager());
    EXPECT_EQ(history.home(), doc());
    EXPECT_TRUE(history.path().empty());

    doc()->openTransaction("create");
    make("Obj")->Integer.setValue(1);
    doc()->commitTransaction();
    const std::string logPath = log().path();
    EXPECT_EQ(logPath.compare(0, history.directory().size(), history.directory()), 0);

    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    // A save renames nothing, but the file is known now.
    EXPECT_EQ(history.path(), App::FileHistory::canonicalPath(path));
    EXPECT_EQ(App::FileHistory::find(path).get(), &history);

    const std::string dir = history.directory();
    closeAndRenew();
    EXPECT_FALSE(App::FileHistory::find(path));
    EXPECT_FALSE(Base::FileInfo(dir).exists());

    // Reopened: a history of its own again, in the new transient directory
    // (the restore renamed it after the file's Uid), registered anew.
    App::Document* opened = App::GetApplication().openDocument(path.c_str(), false);
    ASSERT_TRUE(opened);
    auto& again = opened->getFileHistory();
    EXPECT_EQ(again.directory(), opened->TransientDir.getStrValue());
    EXPECT_TRUE(Base::FileInfo(again.directory()).isDir());
    EXPECT_EQ(App::FileHistory::find(path).get(), &again);
    auto olog = opened->getTransactionLog();
    ASSERT_TRUE(olog);
    EXPECT_EQ(olog->path().compare(0, again.directory().size(), again.directory()), 0);
    App::GetApplication().closeDocument(opened->getName());
    Base::FileInfo(path).deleteFile();
}

namespace {

int64_t cursorBranch(App::Document* doc)
{
    auto log = doc->getTransactionLog();
    return log ? log->branch() : -1;
}

} // namespace

TEST_F(TransactionLogTest, versionDocumentsShareTheFilesLog)
{
    // Sec 27.5 ruling 3, 27.7: versions of one file open as documents of
    // their own, on the file's one log and blob store; one version, one
    // document.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    doc()->openTransaction("two");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    const int64_t v2 = doc()->snapshotToLog();
    doc()->openTransaction("three");
    obj->Integer.setValue(3);
    doc()->commitTransaction();

    const std::string path = Base::FileInfo::getTempPath() + "txnlog-versions.FCStd";
    Base::FileInfo(path).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));

    App::Document* d1 = doc()->openVersion(v1, false);
    ASSERT_TRUE(d1);
    ASSERT_NE(d1, doc());
    EXPECT_TRUE(d1->testStatus(App::Document::VersionDoc));
    // Editable, named for the branch its first change will take (sec 27.23):
    // main moved on, so a branch of its own, `main@v<v1>`.
    EXPECT_EQ(d1->FileName.getStrValue(), doc()->FileName.getStrValue() + "@main@v"
                                              + std::to_string(v1) + "@v" + std::to_string(v1));
    EXPECT_EQ(&d1->getFileHistory(), &doc()->getFileHistory());
    EXPECT_EQ(&d1->getFileBlobManager(), &doc()->getFileBlobManager());
    // The file is still registered as itself, not as the version.
    EXPECT_EQ(App::FileHistory::find(path).get(), &doc()->getFileHistory());
    auto o1 = dynamic_cast<App::FeatureTest*>(d1->getObject("Obj"));
    ASSERT_TRUE(o1);
    EXPECT_EQ(o1->Integer.getValue(), 1);
    ASSERT_TRUE(d1->getTransactionLog());
    EXPECT_TRUE(d1->getTransactionLog()->detached());
    EXPECT_EQ(d1->getTransactionLog()->detachedAt(), v1);
    EXPECT_EQ(d1->getTransactionLog()->lastSeq(), log().lastSeq());

    // Opened once: asking again gives the same document.
    EXPECT_EQ(doc()->openVersion(v1, false), d1);
    EXPECT_EQ(d1->openVersion(v1, false), d1);
    App::Document* d2 = doc()->openVersion(v2, false);
    ASSERT_TRUE(d2);
    EXPECT_NE(d2, d1);
    EXPECT_EQ(dynamic_cast<App::FeatureTest*>(d2->getObject("Obj"))->Integer.getValue(), 2);
    EXPECT_EQ(log().documents().size(), 3u);

    // Read-only as a partial document is: no save.
    EXPECT_FALSE(d1->save());
    // The live document is untouched by any of it.
    EXPECT_EQ(obj->Integer.getValue(), 3);
    EXPECT_EQ(cursorBranch(doc()), 1);

    App::GetApplication().closeDocument(d2->getName());
    App::GetApplication().closeDocument(d1->getName());
    EXPECT_EQ(log().documents().size(), 1u);
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, aVersionBranchesAtItsFirstChange)
{
    // Sec 27.5 ruling 3: a version that is not a branch tip gets a branch
    // of its own when it first changes; ids never collide with the file's.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    doc()->openTransaction("two");
    obj->Integer.setValue(2);
    make("Later");
    doc()->commitTransaction();
    const size_t branchesBefore = log().store().branches().size();

    App::Document* d1 = doc()->openVersion(v1, false);
    ASSERT_TRUE(d1);
    auto vlog = d1->getTransactionLog();
    ASSERT_TRUE(vlog);
    // Opening is not changing: no branch, no row.
    EXPECT_TRUE(vlog->detached());
    EXPECT_EQ(log().lastSeq(), vlog->lastSeq());
    EXPECT_EQ(log().store().branches().size(), branchesBefore);

    auto o1 = dynamic_cast<App::FeatureTest*>(d1->getObject("Obj"));
    ASSERT_TRUE(o1);
    d1->openTransaction("version edit");
    o1->Integer.setValue(10);
    auto made = d1->addObject("App::FeatureTest", "Made");
    d1->commitTransaction();
    EXPECT_FALSE(vlog->detached());
    EXPECT_NE(vlog->branch(), log().branch());
    App::LogBranch branch;
    ASSERT_TRUE(log().store().getBranch(vlog->branch(), branch));
    EXPECT_EQ(branch.name, "main@v" + std::to_string(v1));
    EXPECT_EQ(branch.fromVersion, v1);
    EXPECT_EQ(log().holderOf(branch.id), d1);
    EXPECT_EQ(log().holderOf(1), doc());
    // The fork is kept: named.
    App::LogVersion fork;
    ASSERT_TRUE(log().store().getVersion(v1, fork));
    EXPECT_EQ(fork.kind, "named");
    // Ids start above everything the file handed out.
    long highest = 0;
    for (auto o : doc()->getObjects())
        highest = std::max(highest, o->getID());
    EXPECT_GT(made->getID(), highest);
    EXPECT_EQ(branch.idBase, 0);
    const long madeId = made->getID();

    // Each document is its own: the live one did not move.
    EXPECT_EQ(obj->Integer.getValue(), 2);
    EXPECT_FALSE(doc()->getObject("Made"));
    EXPECT_EQ(o1->Integer.getValue(), 10);
    EXPECT_FALSE(d1->getObject("Later"));
    // Undo in the version document undoes its own edit.
    ASSERT_TRUE(d1->undo());
    EXPECT_EQ(o1->Integer.getValue(), 1);
    EXPECT_EQ(obj->Integer.getValue(), 2);
    // One counter for the file: the live document goes on past the version
    // document's id (sec 27.40 item 1).
    doc()->openTransaction("after");
    auto after = make("AfterMade");
    doc()->commitTransaction();
    EXPECT_GT(after->getID(), madeId);   // `made` went with the undo

    App::GetApplication().closeDocument(d1->getName());
    EXPECT_FALSE(log().holderOf(branch.id));
}

TEST_F(TransactionLogTest, aNameSaysVersionOrBranch)
{
    // Sec 27.23, 27.24: after the file, `@v<num>`, `@<branch>@v<num>` or
    // `@<branch>@`; the file is the longest prefix that is one, so a branch
    // name may hold `@v`.
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-names.FCStd";
    Base::FileInfo(path).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    App::FileHistory::NameParts parts;
    EXPECT_FALSE(App::FileHistory::parseName(path, parts));
    ASSERT_TRUE(App::FileHistory::parseName(path + "@v3", parts));
    EXPECT_EQ(parts.file, path);
    EXPECT_TRUE(parts.branch.empty());
    EXPECT_EQ(parts.version, 3);
    ASSERT_TRUE(App::FileHistory::parseName(path + "@main@v3@v8", parts));
    EXPECT_EQ(parts.file, path);
    EXPECT_EQ(parts.branch, "main@v3");
    EXPECT_EQ(parts.version, 8);
    ASSERT_TRUE(App::FileHistory::parseName(path + "@main@v3@", parts));
    EXPECT_EQ(parts.branch, "main@v3");
    EXPECT_EQ(parts.version, 0);
    ASSERT_TRUE(App::FileHistory::parseName(path + "@v2@v5", parts));
    EXPECT_EQ(parts.branch, "v2");
    EXPECT_EQ(parts.version, 5);
    EXPECT_FALSE(App::FileHistory::parseName(path + "@", parts));
    EXPECT_FALSE(App::FileHistory::parseName(path + "@main", parts));
    EXPECT_FALSE(App::FileHistory::parseName(path + "@v0", parts));
    EXPECT_FALSE(App::FileHistory::parseName(path + "@v99999999999999999999", parts));
    std::string file = path + "@main@";
    EXPECT_EQ(App::FileHistory::splitVersion(file), -1);
    EXPECT_EQ(file, path);
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, aPinnedVersionIsFrozen)
{
    // Sec 27.22: a version is open at most twice, as the frozen instance
    // pins show -- every change refused -- and as an editable one.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    doc()->openTransaction("two");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-frozen.FCStd";
    Base::FileInfo(path).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    auto history = App::FileHistory::find(path);
    ASSERT_TRUE(history);

    App::Document* frozen = App::Document::openFileVersion(history, v1, false, nullptr, true);
    ASSERT_TRUE(frozen);
    EXPECT_TRUE(frozen->testStatus(App::Document::FrozenVersion));
    EXPECT_EQ(frozen->FileName.getStrValue(), doc()->FileName.getStrValue() + "@v" + std::to_string(v1));
    EXPECT_EQ(App::Document::openFileVersion(history, v1, false, nullptr, true), frozen);
    App::Document* editable = doc()->openVersion(v1, false);
    ASSERT_TRUE(editable);
    EXPECT_NE(editable, frozen);
    EXPECT_FALSE(editable->testStatus(App::Document::FrozenVersion));
    EXPECT_EQ(doc()->openVersion(v1, false), editable);

    auto fo = dynamic_cast<App::FeatureTest*>(frozen->getObject("Obj"));
    ASSERT_TRUE(fo);
    const int64_t rows = log().lastSeq();
    EXPECT_THROW(fo->Integer.setValue(99), Base::Exception);
    EXPECT_EQ(fo->Integer.getValue(), 1);
    EXPECT_THROW(frozen->addObject("App::FeatureTest", "More"), Base::Exception);
    EXPECT_THROW(frozen->removeObject("Obj"), Base::Exception);
    EXPECT_THROW(frozen->restoreVersion(v1), Base::Exception);
    EXPECT_THROW(frozen->saveVersionAsFile(), Base::Exception);
    fo->touch();
    EXPECT_EQ(frozen->recompute(), 0);
    EXPECT_FALSE(frozen->undo());
    // View state is not data.
    fo->Visibility.setValue(!fo->Visibility.getValue());
    EXPECT_EQ(log().lastSeq(), rows);

    // The editable instance takes edits, on a branch; the frozen one is
    // still the version.
    auto eo = dynamic_cast<App::FeatureTest*>(editable->getObject("Obj"));
    ASSERT_TRUE(eo);
    editable->openTransaction("edit");
    eo->Integer.setValue(7);
    editable->commitTransaction();
    EXPECT_EQ(eo->Integer.getValue(), 7);
    EXPECT_EQ(fo->Integer.getValue(), 1);
    EXPECT_EQ(editable->FileName.getStrValue(), doc()->FileName.getStrValue() + "@main@v"
                                                    + std::to_string(v1) + "@v"
                                                    + std::to_string(v1));

    // The tip form finds the holder of the branch.
    App::Document* tip = App::GetApplication().openDocument(
        (doc()->FileName.getStrValue() + "@main@v" + std::to_string(v1) + "@").c_str(), false);
    EXPECT_EQ(tip, editable);
    App::Document* own =
        App::GetApplication().openDocument((doc()->FileName.getStrValue() + "@main@").c_str(), false);
    EXPECT_EQ(own, doc());

    App::GetApplication().closeDocument(editable->getName());
    App::GetApplication().closeDocument(frozen->getName());
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, aFreeTipIsContinuedAndAHeldBranchIsNotSwitchedTo)
{
    // Sec 27.7, git's worktree rule: the version at a branch's tip, when
    // no document holds the branch, continues it; a branch another
    // document holds cannot be switched to.
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    obj->Integer.setValue(5);
    doc()->commitTransaction();
    const int64_t tip = doc()->snapshotToLog();
    ASSERT_TRUE(doc()->switchBranch("main"));
    App::LogBranch side;
    ASSERT_TRUE(log().store().findBranch("side", side));
    const size_t branchesBefore = log().store().branches().size();

    App::Document* d = doc()->openVersion(tip, false);
    ASSERT_TRUE(d);
    auto o = dynamic_cast<App::FeatureTest*>(d->getObject("Obj"));
    ASSERT_TRUE(o);
    EXPECT_EQ(o->Integer.getValue(), 5);
    d->openTransaction("continue side");
    o->Integer.setValue(6);
    d->commitTransaction();
    EXPECT_EQ(d->getTransactionLog()->branch(), side.id);
    EXPECT_EQ(log().store().branches().size(), branchesBefore);
    EXPECT_EQ(log().holderOf(side.id), d);

    // Held: the live document cannot switch to it, and stays where it is.
    EXPECT_THROW(doc()->switchBranch("side"), Base::Exception);
    EXPECT_EQ(log().branch(), 1);
    EXPECT_EQ(obj->Integer.getValue(), 1);

    // Let go: now it can, and it arrives at the version document's edit.
    App::GetApplication().closeDocument(d->getName());
    ASSERT_TRUE(doc()->switchBranch("side"));
    EXPECT_EQ(obj->Integer.getValue(), 6);
}

TEST_F(TransactionLogTest, theFilesLogOutlivesItsFirstDocument)
{
    // Sec 27.7: the history, its directory and its log live while any
    // document of the file does, the one that made it included.
    doc()->openTransaction("create");
    make("Obj")->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    // The live document is v1 until it changes, and would be what opening
    // v1 returns (one version, one document).
    EXPECT_EQ(doc()->openVersion(v1, false), doc());
    doc()->openTransaction("moved on");
    make("Other");
    doc()->commitTransaction();
    const std::string dir = doc()->getFileHistory().directory();
    App::Document* d1 = doc()->openVersion(v1, false);
    ASSERT_TRUE(d1);
    ASSERT_NE(d1, doc());
    const std::string name = d1->getName();

    closeAndRenew();
    EXPECT_TRUE(Base::FileInfo(dir).isDir());
    EXPECT_EQ(d1->getFileHistory().directory(), dir);
    EXPECT_FALSE(d1->getFileHistory().home());
    auto o = dynamic_cast<App::FeatureTest*>(d1->getObject("Obj"));
    ASSERT_TRUE(o);
    d1->openTransaction("after the first went");
    o->Integer.setValue(7);
    d1->commitTransaction();
    auto vlog = d1->getTransactionLog();
    ASSERT_TRUE(vlog);
    EXPECT_FALSE(vlog->detached());
    // Not main's tip (main moved on): a branch of its own.
    EXPECT_NE(vlog->branch(), 1);
    const auto rows = vlog->store().chain(vlog->head());
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows.back().name, "after the first went");

    App::GetApplication().closeDocument(name.c_str());
    EXPECT_FALSE(Base::FileInfo(dir).exists());
}

TEST_F(TransactionLogTest, aClosedFilesHistoryIsReadFromTheArchive)
{
    // Sec 27.13: a file's versions open with no document of the file open;
    // the history comes out of the archive, the file as found becomes the
    // version its copy numbers next, and the file opened later joins it.
    App::DocumentParams::setTransactionLog(2);   // embedded
    doc()->openTransaction("create");
    auto obj = make("Obj");
    obj->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t v1 = doc()->snapshotToLog();
    ASSERT_TRUE(log().store().nameVersion(v1, "one"));
    doc()->openTransaction("two");
    obj->Integer.setValue(2);
    doc()->commitTransaction();
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-archive.FCStd";
    Base::FileInfo(path).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    auto version = Base::freecad_dynamic_cast<App::PropertyString>(doc()->getPropertyByName("Version"));
    ASSERT_TRUE(version);
    const int64_t fileVersion = std::stoll(version->getValue());
    closeAndRenew();
    ASSERT_FALSE(App::FileHistory::find(path));

    std::string reason;
    auto history = App::FileHistory::openFile(path, &reason);
    ASSERT_TRUE(history) << reason;
    EXPECT_EQ(history->path(), App::FileHistory::canonicalPath(path));
    EXPECT_EQ(App::FileHistory::find(path), history);
    EXPECT_EQ(history->fileVersion(), fileVersion);
    EXPECT_FALSE(history->home());
    EXPECT_TRUE(Base::FileInfo(history->directory() + "/history/log.db").exists());

    App::Document* d1 = App::Document::openFileVersion(history, v1, false);
    ASSERT_TRUE(d1);
    EXPECT_TRUE(d1->testStatus(App::Document::VersionDoc));
    EXPECT_EQ(d1->FileName.getStrValue(),
              history->path() + "@main@v" + std::to_string(v1) + "@v" + std::to_string(v1));
    auto o1 = dynamic_cast<App::FeatureTest*>(d1->getObject("Obj"));
    ASSERT_TRUE(o1);
    EXPECT_EQ(o1->Integer.getValue(), 1);
    EXPECT_EQ(&d1->getFileHistory(), history.get());
    App::Document* df = App::Document::openFileVersion(history, fileVersion, false);
    ASSERT_TRUE(df);
    auto of = dynamic_cast<App::FeatureTest*>(df->getObject("Obj"));
    ASSERT_TRUE(of);
    EXPECT_EQ(of->Integer.getValue(), 2);
    const std::string d1Name = d1->getName();
    const std::string dfName = df->getName();

    // The file itself, opened now, joins the history the versions are on.
    App::Document* opened = App::GetApplication().openDocument(path.c_str(), false);
    ASSERT_TRUE(opened);
    EXPECT_EQ(&opened->getFileHistory(), history.get());
    auto live = dynamic_cast<App::FeatureTest*>(opened->getObject("Obj"));
    ASSERT_TRUE(live);
    EXPECT_EQ(live->Integer.getValue(), 2);
    ASSERT_TRUE(opened->getTransactionLog());
    EXPECT_EQ(opened->getTransactionLog()->branch(), 1);
    opened->openTransaction("live edit");
    live->Integer.setValue(3);
    opened->commitTransaction();
    EXPECT_EQ(o1->Integer.getValue(), 1);
    EXPECT_EQ(of->Integer.getValue(), 2);

    const std::string openedName = opened->getName();
    App::GetApplication().closeDocument(openedName.c_str());
    App::GetApplication().closeDocument(dfName.c_str());
    App::GetApplication().closeDocument(d1Name.c_str());
    const std::string dir = history->directory();
    history.reset();
    EXPECT_FALSE(App::FileHistory::find(path));
    EXPECT_FALSE(Base::FileInfo(dir).exists());

    // A file with no history in it has none to open.
    const std::string plain = Base::FileInfo::getTempPath() + "txnlog-archive-plain.FCStd";
    Base::FileInfo(plain).deleteFile();
    App::DocumentParams::setTransactionLog(1);
    doc()->openTransaction("plain");
    make("Plain");
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->saveAs(plain.c_str()));
    closeAndRenew();
    EXPECT_FALSE(App::FileHistory::openFile(plain, &reason));
    EXPECT_EQ(reason, "the file carries no history");
    Base::FileInfo(plain).deleteFile();
    Base::FileInfo(path).deleteFile();
}

TEST_F(TransactionLogTest, stringTableAndReferenceSets)
{
    // docs/TransactionLog.md sec 27.50 items 2 and 4: the store keeps the
    // file's strings once, and each version's and value's ids as ranges
    // that go with their entity.
    const std::string path = Base::FileInfo::getTempFileName("txnlog-strings") + ".db";
    const std::string kept(40, 'a'), gone(40, 'b');
    {
        auto store = App::TransactionStore::openSQLite(path);
        App::LogString s1;
        s1.id = 1;
        s1.data = std::string("E\0dge", 5);
        App::LogString s2;
        s2.id = 2;
        s2.flags = 8;
        s2.sids = "1 3:4";
        s2.data = "x";
        s2.postfix = ";:H";
        store->addStrings({s2, s1});
        App::LogString again = s1;
        again.data = "changed";
        store->addStrings({again});   // an id held is left as it is
        EXPECT_EQ(store->stringIds(), (std::vector<long> {1, 2}));
        auto all = store->strings();
        ASSERT_EQ(all.size(), 2u);
        EXPECT_EQ(all[0].data, s1.data);
        EXPECT_EQ(all[1].sids, "1 3:4");
        EXPECT_EQ(all[1].postfix, ";:H");
        EXPECT_EQ(all[1].flags, 8);
        auto some = store->strings({2});
        ASSERT_EQ(some.size(), 1u);
        EXPECT_EQ(some[0].id, 2);
        store->removeStrings({1});
        EXPECT_EQ(store->stringIds(), (std::vector<long> {2}));

        auto put = [&](const std::string& hash) {
            App::LogEntity e;
            e.hash = hash;
            e.enc = "raw";
            e.data = "v";
            e.size = 1;
            store->putEntity(e);
        };
        put(kept);
        put(gone);
        store->addStringRefs(kept, {{1, 3}, {10, 12}});
        store->addStringRefs(gone, {{4, 5}, {20, 20}});
        EXPECT_EQ(store->stringRefs(),
                  (std::vector<std::pair<long, long>> {{1, 5}, {10, 12}, {20, 20}}));
        // `kept` is named by an op, `gone` by nothing: a collection takes
        // `gone` and its ranges.
        App::LogTransaction t;
        t.kind = "user";
        std::vector<App::LogOp> ops(1);
        ops[0].op = "set";
        ops[0].ckind = "obj";
        ops[0].cid = 1;
        ops[0].prop = "P";
        ops[0].vafter = kept;
        store->append(t, ops);
        store->truncate(0);
        EXPECT_TRUE(store->hasEntity(kept));
        EXPECT_FALSE(store->hasEntity(gone));
        EXPECT_EQ(store->stringRefs(), (std::vector<std::pair<long, long>> {{1, 3}, {10, 12}}));
        store->clearStrings();
        EXPECT_TRUE(store->stringIds().empty());
    }
    Base::FileInfo(path).deleteFile();
}

// Sec 27.67: values restored in a batch run afterRestore() once all are in,
// and a property removed before then -- a copy-on-change link removes and
// adds again the properties it mirrors when its target is restored -- is
// dropped from the batch, not called on.
TEST_F(TransactionLogTest, restoreBatchForgetsARemovedProperty)
{
    auto a = make("A");
    auto kept = a->addDynamicProperty("App::PropertyExpressionEngine", "Kept");
    auto gone = a->addDynamicProperty("App::PropertyExpressionEngine", "Gone");
    ASSERT_TRUE(kept);
    ASSERT_TRUE(gone);
    a->setExpression(App::ObjectIdentifier::parse(a, "Integer"),
                     std::shared_ptr<App::Expression>(App::Expression::parse(a, "1 + 1")));
    const App::CapturedValue value = App::captureValue(*doc(), a->ExpressionEngine);
    ASSERT_TRUE(value.ok);
    {
        App::RestoreBatch batch;
        App::restoreValue(*kept, value);
        App::restoreValue(*gone, value);
        ASSERT_TRUE(a->removeDynamicProperty("Gone"));
        batch.finish();
    }
    // The engine that stayed got its expression once the batch was done.
    auto engine = static_cast<App::PropertyExpressionEngine*>(kept);
    EXPECT_EQ(engine->getExpressions().size(), 1u);
}

// Sec 28.2 item 1: a merge row has a second parent, and a row's history is
// what both edges reach; one branch's chain still follows `parent` alone.
TEST_F(TransactionLogTest, aRowsHistoryFollowsBothParents)
{
    const std::string path = Base::FileInfo::getTempFileName("txnlog-history") + ".db";
    {
        auto store = App::TransactionStore::openSQLite(path);
        auto row = [&](int64_t parent, int64_t branch, int64_t mergeFrom = 0) {
            App::LogTransaction t;
            t.parent = parent;
            t.branch = branch;
            t.kind = mergeFrom ? "merge" : "user";
            t.mergeFrom = mergeFrom;
            std::vector<App::LogOp> ops(1);
            ops[0].op = "set";
            ops[0].ckind = "obj";
            ops[0].cid = 7;
            ops[0].prop = "A";
            ops[0].vafter = "x";
            return store->append(t, ops);
        };
        auto seqs = [](const std::vector<App::LogTransaction>& rows) {
            std::vector<int64_t> out;
            for (const auto& t : rows)
                out.push_back(t.seq);
            return out;
        };
        const int64_t a1 = row(0, 1);
        App::LogBranch side;
        side.name = "side";
        side.fromSeq = a1;
        side.head = a1;
        const int64_t sideId = store->addBranch(side);
        const int64_t s2 = row(a1, sideId);
        const int64_t m3 = row(a1, 1);
        const int64_t s4 = row(s2, sideId);
        const int64_t merge = row(m3, 1, s4);   // side into main
        const int64_t s6 = row(s4, sideId);
        const int64_t back = row(s6, sideId, merge);   // main back into side

        EXPECT_EQ(seqs(store->chain(merge)), (std::vector<int64_t> {a1, m3, merge}));
        EXPECT_EQ(seqs(store->history(merge)), (std::vector<int64_t> {a1, s2, m3, s4, merge}));
        EXPECT_EQ(seqs(store->history(s6)), (std::vector<int64_t> {a1, s2, s4, s6}));
        EXPECT_EQ(seqs(store->history(back)),
                  (std::vector<int64_t> {a1, s2, m3, s4, merge, s6, back}));
        EXPECT_TRUE(store->history(0).empty());
        for (const auto& t : store->transactions()) {
            EXPECT_EQ(t.mergeFrom, t.seq == merge ? s4 : (t.seq == back ? merge : 0)) << t.seq;
        }
        // A rewritten row keeps what it is given.
        App::LogTransaction t = store->chain(merge).back();
        std::vector<App::LogOp> none;
        store->replaceTransactions(t, none, {});
        EXPECT_EQ(store->chain(merge).back().mergeFrom, s4);
    }
    Base::FileInfo(path).deleteFile();
}

// Sec 28.2 item 3: what theirs changed and ours left alone goes in, as one
// transaction with theirs' head as its second parent; the next merge starts
// where this one ended, and a merge back takes only what the other side did
// besides.
TEST_F(TransactionLogTest, mergeTakesWhatOursLeftAlone)
{
    doc()->openTransaction("create");
    auto a = make("A");
    a->Integer.setValue(1);
    a->String.setValue("a");
    auto b = make("B");
    b->Integer.setValue(1);
    doc()->commitTransaction();
    const int64_t fork = log().head();

    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    a->Integer.setValue(2);
    auto c = make("C");
    c->String.setValue("side");
    c->addDynamicProperty("App::PropertyInteger", "Extra", "Group", "doc");
    static_cast<App::PropertyInteger*>(c->getPropertyByName("Extra"))->setValue(7);
    doc()->commitTransaction();
    const long cId = c->getID();

    ASSERT_TRUE(doc()->switchBranch("main"));
    // Side's head once left: the switch put a snapshot record on it.
    log().flush();
    App::LogBranch sideRow;
    ASSERT_TRUE(log().store().findBranch("side", sideRow));
    const int64_t sideHead = sideRow.head;
    doc()->openTransaction("main edit");
    a->String.setValue("main");
    b->Integer.setValue(5);
    doc()->commitTransaction();
    EXPECT_FALSE(doc()->getObject("C"));

    auto preview = doc()->previewMerge("side");
    EXPECT_EQ(preview.base, fork);
    EXPECT_EQ(preview.theirs, sideHead);
    EXPECT_FALSE(preview.fastForward);
    EXPECT_EQ(preview.conflicts, 0u);
    std::map<std::string, std::string> kinds;
    for (const auto& ch : preview.changes)
        kinds[ch.key] = ch.kind + " " + ch.op;
    EXPECT_EQ(kinds["A.Integer"], "take set");
    EXPECT_EQ(kinds["C"], "take create");
    EXPECT_EQ(kinds.count("C.String"), 0u);   // goes in with its object
    EXPECT_EQ(kinds.count("A.String"), 0u);   // theirs did not change it
    EXPECT_EQ(a->Integer.getValue(), 1);      // a preview moves nothing

    const size_t undos = doc()->getAvailableUndoNames().size();
    auto result = doc()->mergeBranch("side");
    ASSERT_GT(result.seq, 0);
    EXPECT_TRUE(result.unresolved.empty());
    EXPECT_EQ(a->Integer.getValue(), 2);
    EXPECT_STREQ(a->String.getValue(), "main");
    EXPECT_EQ(b->Integer.getValue(), 5);
    auto merged = dynamic_cast<App::FeatureTest*>(doc()->getObject("C"));
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->getID(), cId);
    EXPECT_STREQ(merged->String.getValue(), "side");
    auto extra = dynamic_cast<App::PropertyInteger*>(merged->getPropertyByName("Extra"));
    ASSERT_TRUE(extra);
    EXPECT_EQ(extra->getValue(), 7);
    EXPECT_STREQ(extra->getGroup(), "Group");
    EXPECT_EQ(doc()->getAvailableUndoNames().size(), undos + 1);   // one step
    log().flush();
    App::LogTransaction row;
    for (const auto& t : log().store().chain(log().head())) {
        if (t.seq == result.seq)
            row = t;
    }
    EXPECT_EQ(row.kind, "merge");
    EXPECT_EQ(row.mergeFrom, sideHead);
    EXPECT_EQ(row.branch, 1);
    EXPECT_NE(row.script.find("\"merge\""), std::string::npos);

    // Undone and redone like any step.
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(a->Integer.getValue(), 1);
    EXPECT_STREQ(a->String.getValue(), "main");
    EXPECT_FALSE(doc()->getObject("C"));
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(a->Integer.getValue(), 2);
    ASSERT_TRUE(doc()->getObject("C"));
    EXPECT_EQ(doc()->getObject("C")->getID(), cId);

    // Merged already: nothing to do, no row.
    auto again = doc()->mergeBranch("side");
    EXPECT_EQ(again.seq, 0);
    EXPECT_TRUE(again.preview.changes.empty());

    // The merge back: main's own edits, and nothing of what side gave it.
    ASSERT_TRUE(doc()->switchBranch("side"));
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    b = dynamic_cast<App::FeatureTest*>(doc()->getObject("B"));
    EXPECT_STREQ(a->String.getValue(), "a");
    doc()->openTransaction("side again");
    a->Float.setValue(2.5);
    doc()->commitTransaction();
    auto backPreview = doc()->previewMerge("main");
    EXPECT_EQ(backPreview.base, sideHead);   // what main's merge took
    EXPECT_EQ(backPreview.conflicts, 0u);
    kinds.clear();
    for (const auto& ch : backPreview.changes)
        kinds[ch.key] = ch.kind + " " + ch.op;
    EXPECT_EQ(kinds["A.String"], "take set");
    EXPECT_EQ(kinds["B.Integer"], "take set");
    EXPECT_EQ(kinds.count("C"), 0u);
    EXPECT_EQ(kinds.count("A.Integer"), 0u);
    auto back = doc()->mergeBranch("main");
    ASSERT_GT(back.seq, 0);
    EXPECT_STREQ(a->String.getValue(), "main");
    EXPECT_EQ(b->Integer.getValue(), 5);
    EXPECT_EQ(a->Integer.getValue(), 2);
    EXPECT_DOUBLE_EQ(a->Float.getValue(), 2.5);

    // And main takes side's one more edit, from where it last merged.
    ASSERT_TRUE(doc()->switchBranch("main"));
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    auto third = doc()->previewMerge("side");
    EXPECT_EQ(third.conflicts, 0u);
    ASSERT_GT(doc()->mergeBranch("side").seq, 0);
    EXPECT_DOUBLE_EQ(a->Float.getValue(), 2.5);
    EXPECT_STREQ(a->String.getValue(), "main");
}

// Sec 28.2 item 3, 28.6 Q3: a property both changed, differently, is a
// conflict, and a conflict with no side refuses the merge with nothing
// moved; so are an object one side removed and the other changed or uses.
TEST_F(TransactionLogTest, mergeConflictsRefuseUntilPicked)
{
    doc()->openTransaction("create");
    auto a = make("A");
    a->Integer.setValue(1);
    a->String.setValue("a");
    auto gone = make("Gone");       // side removes it, main changes it
    auto used = make("Used");       // side removes it, main links to it
    auto back = make("Back");       // main removes it, side changes it
    back->String.setValue("base");
    back->Integer.setValue(1);
    doc()->commitTransaction();
    const long backId = back->getID();

    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    a->Integer.setValue(2);
    a->String.setValue("both");
    doc()->removeObject("Gone");
    doc()->removeObject("Used");
    back->Integer.setValue(9);
    doc()->commitTransaction();

    ASSERT_TRUE(doc()->switchBranch("main"));
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    gone = dynamic_cast<App::FeatureTest*>(doc()->getObject("Gone"));
    used = dynamic_cast<App::FeatureTest*>(doc()->getObject("Used"));
    ASSERT_TRUE(a && gone && used);
    doc()->openTransaction("main edit");
    a->Integer.setValue(3);
    a->String.setValue("both");
    gone->Integer.setValue(8);
    auto user = make("User");
    user->Link.setValue(used);
    doc()->removeObject("Back");
    doc()->commitTransaction();
    const int64_t headBefore = log().head();

    auto preview = doc()->previewMerge("side");
    std::map<std::string, App::Document::MergeChange> byKey;
    for (const auto& ch : preview.changes)
        byKey[ch.key] = ch;
    EXPECT_EQ(byKey["A.Integer"].kind, "conflict");
    EXPECT_NE(byKey["A.Integer"].ours, byKey["A.Integer"].theirs);
    EXPECT_NE(byKey["A.Integer"].base, byKey["A.Integer"].ours);
    EXPECT_EQ(byKey["A.String"].kind, "same");
    EXPECT_EQ(byKey["Gone"].kind, "conflict");
    EXPECT_EQ(byKey["Gone"].op, "remove");
    EXPECT_EQ(byKey["Used"].kind, "conflict");
    EXPECT_NE(byKey["Used"].note.find("User"), std::string::npos);
    EXPECT_EQ(byKey["Back"].kind, "conflict");
    EXPECT_EQ(byKey["Back"].op, "revive");
    EXPECT_EQ(preview.conflicts, 4u);

    // No side: refused, nothing moved, no row.
    auto refused = doc()->mergeBranch("side");
    EXPECT_EQ(refused.seq, 0);
    EXPECT_EQ(refused.unresolved.size(), 4u);
    EXPECT_EQ(a->Integer.getValue(), 3);
    EXPECT_TRUE(doc()->getObject("Gone"));
    EXPECT_FALSE(doc()->getObject("Back"));
    EXPECT_EQ(log().head(), headBefore);
    EXPECT_THROW(doc()->mergeBranch("side", {{"A.Integer", "mine"}}), Base::Exception);
    EXPECT_THROW(doc()->mergeBranch("nowhere"), Base::Exception);
    EXPECT_THROW(doc()->mergeBranch("main"), Base::Exception);

    // Sides picked, the rest by the fallback.
    auto result = doc()->mergeBranch("side", {{"A.Integer", "theirs"}, {"Back", "theirs"}},
                                     "ours");
    ASSERT_GT(result.seq, 0);
    EXPECT_TRUE(result.unresolved.empty());
    EXPECT_EQ(a->Integer.getValue(), 2);
    EXPECT_TRUE(doc()->getObject("Gone"));    // ours kept
    EXPECT_TRUE(doc()->getObject("Used"));
    EXPECT_EQ(user->Link.getValue(), used);
    auto revived = dynamic_cast<App::FeatureTest*>(doc()->getObject("Back"));
    ASSERT_TRUE(revived);                     // back under its id, as theirs has it
    EXPECT_EQ(revived->getID(), backId);
    EXPECT_EQ(revived->Integer.getValue(), 9);
    EXPECT_STREQ(revived->String.getValue(), "base");
    log().flush();
    std::string script;
    for (const auto& t : log().store().chain(log().head())) {
        if (t.seq == result.seq)
            script = t.script;
    }
    EXPECT_NE(script.find("\"sides\""), std::string::npos);
    EXPECT_NE(script.find("\"theirs\""), std::string::npos);

    // One step back to where ours was.
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(a->Integer.getValue(), 3);
    EXPECT_FALSE(doc()->getObject("Back"));
    ASSERT_TRUE(doc()->redo());
    EXPECT_EQ(a->Integer.getValue(), 2);

    // The conflicts kept as ours are not asked again.
    auto again = doc()->previewMerge("side");
    EXPECT_TRUE(again.changes.empty());
}

// Sec 28.6 Q3: every conflict kept as ours writes nothing of theirs, but a
// record with the second parent, so the base moves.
TEST_F(TransactionLogTest, mergeKeepingOursIsARecord)
{
    doc()->openTransaction("create");
    auto a = make("A");
    a->Integer.setValue(1);
    doc()->commitTransaction();
    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    a->Integer.setValue(2);
    doc()->commitTransaction();
    ASSERT_TRUE(doc()->switchBranch("main"));
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    doc()->openTransaction("main edit");
    a->Integer.setValue(3);
    doc()->commitTransaction();
    const size_t undos = doc()->getAvailableUndoNames().size();

    auto result = doc()->mergeBranch("side", {}, "ours");
    ASSERT_GT(result.seq, 0);
    EXPECT_EQ(a->Integer.getValue(), 3);
    EXPECT_EQ(doc()->getAvailableUndoNames().size(), undos);   // nothing to undo
    log().flush();
    EXPECT_TRUE(log().store().ops(result.seq).empty());
    App::LogBranch side;
    ASSERT_TRUE(log().store().findBranch("side", side));
    EXPECT_EQ(log().store().chain(log().head()).back().mergeFrom, side.head);
    EXPECT_TRUE(doc()->previewMerge("side").changes.empty());
}

// Sec 28.6 Q1: ours unchanged since the base takes theirs whole -- derived
// values with it, no recompute -- and one that did change recomputes.
TEST_F(TransactionLogTest, mergeFastForwardsWhenOursIsUnchanged)
{
    doc()->openTransaction("create");
    auto a = make("A");
    a->Integer.setValue(1);
    doc()->commitTransaction();
    doc()->recompute();
    const long execBase = a->ExecCount.getValue();

    doc()->createBranch("side");
    doc()->openTransaction("side edit");
    a->Integer.setValue(2);
    auto c = make("C");
    doc()->commitTransaction();
    doc()->recompute();
    doc()->recompute();
    const long execSide = a->ExecCount.getValue();
    ASSERT_GT(execSide, execBase);
    const long cExec = c->ExecCount.getValue();

    ASSERT_TRUE(doc()->switchBranch("main"));
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    EXPECT_EQ(a->ExecCount.getValue(), execBase);
    log().flush();
    // Main has not moved since the fork but for the record of the switch
    // back: side's rows are taken as they are (sec 30.4 P1).
    App::LogBranch mainRow;
    ASSERT_TRUE(log().store().getBranch(1, mainRow));
    const int64_t mainHead = mainRow.head;
    auto preview = doc()->previewMerge("side");
    EXPECT_TRUE(preview.fastForward);
    EXPECT_FALSE(preview.forward.empty());
    EXPECT_NE(preview.ours, preview.base);
    EXPECT_EQ(preview.conflicts, 0u);
    for (const auto& ch : preview.changes)
        EXPECT_NE(ch.kind, "derived") << ch.key;

    auto result = doc()->mergeBranch("side");
    ASSERT_GT(result.seq, 0);
    EXPECT_EQ(result.forwarded, preview.forward.size());
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    ASSERT_TRUE(a);
    EXPECT_EQ(a->Integer.getValue(), 2);
    // Theirs' own count, not one more: nothing was recomputed.
    EXPECT_EQ(a->ExecCount.getValue(), execSide);
    auto merged = dynamic_cast<App::FeatureTest*>(doc()->getObject("C"));
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->ExecCount.getValue(), cExec);
    EXPECT_FALSE(a->isTouched());
    log().flush();
    // The chain: side's rows as they were made, then main's record of the
    // switch, written again after them -- and no row of the merge's own.
    bool sawSwitch = false;
    bool sawEdit = false;
    int64_t last = 0;
    for (const auto& t : log().store().chain(log().head())) {
        EXPECT_GT(t.seq, last);   // a row is numbered after the one it follows
        last = t.seq;
        EXPECT_NE(t.seq, mainHead);
        EXPECT_NE(t.kind, "merge");
        if (t.name == "side edit") {
            EXPECT_EQ(t.kind, "user");
            EXPECT_EQ(t.branch, 1);   // side was made from main: main's now
            sawEdit = true;
        }
        if (t.kind == "switch" && t.seq > result.seq) {
            EXPECT_TRUE(sawEdit);
            sawSwitch = true;
        }
    }
    EXPECT_TRUE(sawSwitch);
    EXPECT_TRUE(sawEdit);
    // The steps are the rows of the chain, undone one at a time.
    int steps = 0;
    while (doc()->getObject("C") && steps < 8) {
        ASSERT_TRUE(doc()->undo());
        ++steps;
    }
    EXPECT_GT(steps, 0);
    a = dynamic_cast<App::FeatureTest*>(doc()->getObject("A"));
    ASSERT_TRUE(a);
    EXPECT_EQ(a->Integer.getValue(), 1);
    EXPECT_FALSE(doc()->getObject("C"));
}

namespace {

App::FeatureTest* featureOf(App::Document* doc, const char* name)
{
    return dynamic_cast<App::FeatureTest*>(doc->getObject(name));
}

void edit(App::Document* doc, const char* what, const std::function<void()>& change)
{
    doc->openTransaction(what);
    change();
    doc->commitTransaction();
}

App::Document::BranchState stateOf(App::Document* doc)
{
    doc->getTransactionLog()->flush();
    return doc->branchState();
}

} // namespace

// Sec 30.3 S.a: a branch opened in a second document of the file. Neither
// follows the other; one is merged into the other when that is asked for,
// and a merge into the one that has not moved takes the rows as they are.
TEST_F(TransactionLogTest, aBranchInASecondDocumentIsMergedWhenAsked)
{
    edit(doc(), "create", [&]() { make("A")->Integer.setValue(1); });
    App::Document* other = doc()->openNewBranch();
    ASSERT_TRUE(other);
    ASSERT_NE(other, doc());
    const std::string otherName = other->getName();
    auto state = stateOf(other);
    EXPECT_EQ(state.branch, "main~1");
    EXPECT_EQ(state.target, "main");
    EXPECT_EQ(state.ahead, 0u);
    EXPECT_EQ(state.behind, 0u);
    EXPECT_TRUE(stateOf(doc()).target.empty());
    EXPECT_EQ(other->getTransactionLog()->head(), log().head());
    ASSERT_TRUE(featureOf(other, "A"));
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 1);

    // What it does stays on its branch.
    edit(other, "longer", [&]() { featureOf(other, "A")->Integer.setValue(2); });
    edit(other, "new", [&]() {
        other->addObject("App::FeatureTest", "B");
        featureOf(other, "B")->String.setValue("theirs");
    });
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 1);
    EXPECT_FALSE(featureOf(doc(), "B"));
    state = stateOf(other);
    EXPECT_EQ(state.ahead, 2u);
    EXPECT_EQ(state.behind, 0u);

    // Merged when asked: main has not moved, so the rows are taken as made.
    auto preview = doc()->previewMerge("main~1");
    EXPECT_EQ(preview.conflicts, 0u);
    ASSERT_FALSE(preview.forward.empty());
    auto result = doc()->mergeBranch("main~1");
    EXPECT_EQ(result.forwarded, preview.forward.size());
    ASSERT_GT(result.seq, 0);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 2);
    ASSERT_TRUE(featureOf(doc(), "B"));
    EXPECT_EQ(featureOf(doc(), "B")->getID(), featureOf(other, "B")->getID());
    EXPECT_STREQ(featureOf(doc(), "B")->String.getValue(), "theirs");
    log().flush();
    int taken = 0;
    for (const auto& t : log().store().chain(log().head())) {
        if (t.name != "longer" && t.name != "new")
            continue;
        ++taken;
        EXPECT_EQ(t.kind, "user") << t.name;
        EXPECT_EQ(t.branch, 1) << t.name;   // main's now
    }
    EXPECT_EQ(taken, 2);
    EXPECT_EQ(result.seq, other->getTransactionLog()->head());
    for (const auto& t : log().store().chain(log().head()))
        EXPECT_NE(t.kind, "merge");   // no row of the merge's own
    state = stateOf(other);
    EXPECT_EQ(state.ahead, 0u);
    EXPECT_EQ(state.behind, 0u);
    // The other document is where it was, with its own steps.
    EXPECT_EQ(other->getAvailableUndoNames(), (std::vector<std::string> {"new", "longer"}));

    // The receiving document's steps are the rows of its chain.
    EXPECT_EQ(doc()->getAvailableUndoNames(),
              (std::vector<std::string> {"new", "longer", "create"}));
    ASSERT_TRUE(doc()->undo());
    EXPECT_FALSE(featureOf(doc(), "B"));
    EXPECT_TRUE(featureOf(other, "B"));
    ASSERT_TRUE(doc()->redo());
    ASSERT_TRUE(featureOf(doc(), "B"));

    // The other way: main does something, and the branch takes it when it
    // asks -- main's rows staying main's.
    edit(doc(), "ours", [&]() { featureOf(doc(), "A")->String.setValue("main"); });
    EXPECT_STRNE(featureOf(other, "A")->String.getValue(), "main");
    state = stateOf(other);
    EXPECT_EQ(state.ahead, 0u);
    EXPECT_GT(state.behind, 0u);
    auto pulled = other->mergeBranch("main");
    EXPECT_GT(pulled.forwarded, 0u);
    EXPECT_STREQ(featureOf(other, "A")->String.getValue(), "main");
    ASSERT_TRUE(featureOf(other, "B"));
    other->getTransactionLog()->flush();
    for (const auto& t : log().store().chain(other->getTransactionLog()->head())) {
        if (t.name == "ours")
            EXPECT_EQ(t.branch, 1);
    }
    state = stateOf(other);
    EXPECT_EQ(state.ahead, 0u);
    EXPECT_EQ(state.behind, 0u);

    // A branch is not its document: closed, it stays.
    App::GetApplication().closeDocument(otherName.c_str());
    App::LogBranch kept;
    EXPECT_TRUE(log().store().findBranch("main~1", kept));
    edit(doc(), "after", [&]() { featureOf(doc(), "A")->Integer.setValue(3); });
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
}

// Sec 30.4 P1: when both moved the merge is a row of its own, as sec 28
// has it, and the branch keeps its rows.
TEST_F(TransactionLogTest, aMergeOfTwoThatMovedIsARow)
{
    edit(doc(), "create", [&]() {
        make("A")->Integer.setValue(1);
        make("B")->Integer.setValue(1);
    });
    App::Document* other = doc()->openNewBranch("side");
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();

    edit(other, "theirs", [&]() { featureOf(other, "B")->Integer.setValue(7); });
    edit(doc(), "ours", [&]() { featureOf(doc(), "A")->Integer.setValue(3); });
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 1);
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 1);
    auto state = stateOf(other);
    EXPECT_EQ(state.ahead, 1u);
    EXPECT_EQ(state.behind, 1u);

    auto preview = doc()->previewMerge("side");
    EXPECT_TRUE(preview.forward.empty());
    auto result = doc()->mergeBranch("side");
    EXPECT_EQ(result.forwarded, 0u);
    ASSERT_GT(result.seq, 0);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 7);
    log().flush();
    bool sawMerge = false;
    for (const auto& t : log().store().chain(log().head())) {
        if (t.seq != result.seq)
            continue;
        sawMerge = true;
        EXPECT_EQ(t.kind, "merge");
        EXPECT_FALSE(log().store().ops(t.seq).empty());
    }
    EXPECT_TRUE(sawMerge);
    App::LogBranch side;
    ASSERT_TRUE(log().store().findBranch("side", side));
    bool kept = false;
    for (const auto& t : log().store().chain(side.head)) {
        if (t.name == "theirs") {
            EXPECT_EQ(t.branch, side.id);
            kept = true;
        }
    }
    EXPECT_TRUE(kept);
    // One step, undone like any.
    ASSERT_TRUE(doc()->undo());
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 1);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
    ASSERT_TRUE(doc()->redo());

    // The branch takes main, merge row and all, when it asks.
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 1);
    auto back = other->mergeBranch("main");
    EXPECT_TRUE(back.unresolved.empty());
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 3);
    EXPECT_EQ(featureOf(other, "B")->Integer.getValue(), 7);

    App::GetApplication().closeDocument(otherName.c_str());
}

// Sec 30.3 S.a: a conflict is found at the merge that was asked for, and
// nothing moves until a side is picked.
TEST_F(TransactionLogTest, aConflictWaitsForTheMergeThatIsAsked)
{
    edit(doc(), "create", [&]() { make("A")->Integer.setValue(1); });
    App::Document* other = doc()->openNewBranch("side");
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();

    edit(other, "theirs", [&]() { featureOf(other, "A")->Integer.setValue(7); });
    edit(doc(), "ours", [&]() { featureOf(doc(), "A")->Integer.setValue(3); });
    // Each keeps working where it is; nothing is told to the other.
    edit(other, "theirs more", [&]() { featureOf(other, "A")->String.setValue("side"); });
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 7);

    log().flush();
    const int64_t head = log().head();
    auto refused = doc()->mergeBranch("side");
    EXPECT_EQ(refused.unresolved.size(), 1u);
    EXPECT_EQ(refused.seq, 0);
    EXPECT_EQ(log().head(), head);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
    EXPECT_STREQ(featureOf(doc(), "A")->String.getValue(), "4711");

    auto merged = doc()->mergeBranch("side", {{"A.Integer", "theirs"}});
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_GT(merged.seq, 0);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 7);
    EXPECT_STREQ(featureOf(doc(), "A")->String.getValue(), "side");

    App::GetApplication().closeDocument(otherName.c_str());
}

// Sec 30.4 P1: records on the receiving branch since the base -- a
// snapshot, a save -- are not movement: the merge is still a fast-forward.
// Their versions, the state at the base, stay there; the records follow
// the rows taken.
TEST_F(TransactionLogTest, aFastForwardKeepsTheRecordsBetween)
{
    edit(doc(), "create", [&]() { make("A")->Integer.setValue(1); });
    App::Document* other = doc()->openNewBranch("side");
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    edit(other, "theirs", [&]() { featureOf(other, "A")->Integer.setValue(7); });

    const int64_t version = doc()->snapshotToLog();
    ASSERT_GT(version, 0);
    log().flush();
    App::LogVersion taken;
    ASSERT_TRUE(log().store().getVersion(version, taken));

    auto preview = doc()->previewMerge("side");
    EXPECT_NE(preview.ours, preview.base);
    ASSERT_FALSE(preview.forward.empty());
    auto result = doc()->mergeBranch("side");
    EXPECT_GT(result.forwarded, 0u);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 7);
    log().flush();
    App::LogVersion kept;
    ASSERT_TRUE(log().store().getVersion(version, kept));
    EXPECT_EQ(kept.seq, preview.base);
    bool sawBase = false;
    bool sawRow = false;
    bool sawRecord = false;
    int64_t last = 0;
    for (const auto& t : log().store().chain(log().head())) {
        EXPECT_GT(t.seq, last);
        last = t.seq;
        EXPECT_NE(t.seq, preview.ours);   // the record's old row is gone
        if (t.seq == preview.base)
            sawBase = true;
        if (t.name == "theirs") {
            EXPECT_TRUE(sawBase);
            sawRow = true;
        }
        if (sawRow && t.name != "theirs" && log().store().ops(t.seq).empty())
            sawRecord = true;
    }
    EXPECT_TRUE(sawRow);
    EXPECT_TRUE(sawRecord);
    // The other document still stands on its rows.
    EXPECT_EQ(other->getAvailableUndoNames(), (std::vector<std::string> {"theirs"}));
    ASSERT_TRUE(other->undo());
    EXPECT_EQ(featureOf(other, "A")->Integer.getValue(), 1);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 7);

    App::GetApplication().closeDocument(otherName.c_str());
}

namespace
{

App::Actor actor(App::Actor::Kind kind, const char* name, uint64_t login,
                 const char* access = "edit")
{
    App::Actor a;
    a.kind = kind;
    a.name = name;
    a.access = access;
    a.login = login;
    return a;
}

/// The user of row `seq`: its session's (sec 30.6).
App::LogUser authorOf(App::TransactionLog& log, int64_t seq)
{
    auto& store = log.store();
    int64_t session = 0;
    for (const auto& t : store.transactions(seq, 1)) {
        if (t.seq == seq)
            session = t.session;
    }
    int64_t user = 0;
    for (const auto& s : store.sessions()) {
        if (s.id == session)
            user = s.user;
    }
    for (const auto& u : store.users()) {
        if (u.id == user)
            return u;
    }
    return {};
}

/// The newest row named `name`, 0 for none.
int64_t rowNamed(App::TransactionLog& log, const std::string& name)
{
    int64_t seq = 0;
    for (const auto& t : log.store().transactions()) {
        if (t.name == name)
            seq = t.seq;
    }
    return seq;
}

}  // namespace

// Sec 30.3 S.b, 30.6: the author of a row is its session's user -- the
// desktop's for what nobody else did, the actor's for a transaction opened
// while one acted -- and one person's logins are one user.
TEST_F(TransactionLogTest, theAuthorOfARowIsItsSessionsUser)
{
    edit(doc(), "mine", [&]() { make("A")->Integer.setValue(1); });
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 7);
    const App::Actor again = actor(App::Actor::Verified, "alice@example.com", 8);
    const App::Actor bob = actor(App::Actor::Invited, "bob", 9);
    {
        App::ActorScope scope(alice);
        edit(doc(), "hers", [&]() { featureOf(doc(), "A")->Integer.setValue(2); });
    }
    {
        App::ActorScope scope(again);
        edit(doc(), "hers again", [&]() { featureOf(doc(), "A")->Integer.setValue(3); });
    }
    {
        App::ActorScope scope(bob);
        edit(doc(), "his", [&]() { featureOf(doc(), "A")->Integer.setValue(4); });
    }
    edit(doc(), "mine again", [&]() { featureOf(doc(), "A")->Integer.setValue(5); });

    auto mine = authorOf(log(), rowNamed(log(), "mine"));
    EXPECT_EQ(mine.kind, "local");
    EXPECT_EQ(mine.name, "host");
    EXPECT_EQ(authorOf(log(), rowNamed(log(), "mine again")).id, mine.id);
    auto hers = authorOf(log(), rowNamed(log(), "hers"));
    EXPECT_EQ(hers.kind, "verified");
    EXPECT_EQ(hers.name, "alice@example.com");
    auto his = authorOf(log(), rowNamed(log(), "his"));
    EXPECT_EQ(his.kind, "invited");
    EXPECT_EQ(his.name, "bob");

    // Two logins of one person: two sessions, one user.
    auto& store = log().store();
    std::map<std::string, int64_t> sessionOf;
    for (const auto& t : store.transactions())
        sessionOf[t.name] = t.session;
    EXPECT_NE(sessionOf["hers"], sessionOf["hers again"]);
    EXPECT_NE(sessionOf["hers"], log().session());
    EXPECT_EQ(authorOf(log(), rowNamed(log(), "hers again")).id, hers.id);
    EXPECT_EQ(sessionOf["mine"], log().session());
    EXPECT_EQ(sessionOf["mine again"], log().session());
    EXPECT_EQ(store.users().size(), 3u);
    for (const auto& s : store.sessions()) {
        if (s.id != log().session())
            EXPECT_EQ(s.access, "edit");
    }

    // A transaction is its opener's, whoever closes it, and whenever: an
    // implicit one a client's event opened is closed at the event loop,
    // where nobody acts.
    {
        App::ActorScope scope(alice);
        featureOf(doc(), "A")->Integer.setValue(6);
    }
    doc()->commitImplicitTransaction();
    const int64_t implicit = log().store().transactions().back().seq;
    EXPECT_EQ(log().store().transactions().back().kind, "implicit");
    EXPECT_EQ(authorOf(log(), implicit).id, hers.id);

    // The recompute recorded while a transaction is open is its author's.
    {
        App::ActorScope scope(bob);
        doc()->openTransaction("his recompute");
        featureOf(doc(), "A")->Integer.setValue(7);
        doc()->recompute();
    }
    doc()->commitTransaction();
    const auto last = log().store().transactions().back();
    EXPECT_EQ(last.kind, "recompute");
    EXPECT_EQ(authorOf(log(), last.seq).id, his.id);
    EXPECT_EQ(authorOf(log(), rowNamed(log(), "his recompute")).id, his.id);

    // An undo is a row of whoever undid: the same user here, since a user
    // undoes its own steps (sec 30.10), under the login it has now.
    int64_t made = 0;
    for (const auto& t : log().store().transactions()) {
        if (t.name == "his recompute")
            made = t.session;
    }
    {
        App::ActorScope scope(actor(App::Actor::Invited, "bob", 10));
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
    }
    const auto undone = log().store().transactions().back();
    EXPECT_EQ(undone.kind, "undo");
    EXPECT_EQ(authorOf(log(), undone.seq).id, his.id);
    EXPECT_NE(undone.session, made);
}

// Sec 30.6 U2: a login is a row with no ops, written when a connection is
// admitted, view-only ones too; leaving closes the session and writes
// nothing. U3: a name nothing verified is the same user each time, marked
// as declared.
TEST_F(TransactionLogTest, aLoginIsARecord)
{
    edit(doc(), "create", [&]() { make("A")->Integer.setValue(1); });
    const App::Actor carol = actor(App::Actor::Declared, "carol", 21, "view");
    const App::Actor later = actor(App::Actor::Declared, "carol", 22, "view");
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 23);

    const int64_t first = log().login(carol);
    ASSERT_GT(first, 0);
    const int64_t second = log().login(alice);
    ASSERT_GT(second, first);
    auto& store = log().store();
    for (int64_t seq : {first, second}) {
        const auto rows = store.transactions(seq, 1);
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].kind, "login");
        EXPECT_TRUE(store.ops(seq).empty());
    }
    const auto row = store.transactions(first, 1)[0];
    EXPECT_EQ(row.name, "Login carol");
    EXPECT_NE(row.script.find("\"kind\":\"declared\""), std::string::npos) << row.script;
    EXPECT_NE(row.script.find("\"access\":\"view\""), std::string::npos) << row.script;
    EXPECT_NE(row.script.find("\"verified\":false"), std::string::npos) << row.script;
    EXPECT_EQ(authorOf(log(), first).kind, "declared");
    EXPECT_NE(store.transactions(second, 1)[0].script.find("\"verified\":true"),
              std::string::npos);

    // The row is the login's own session, opened by it.
    int64_t session = row.session;
    EXPECT_NE(session, log().session());
    auto closedOf = [&](int64_t id) {
        for (const auto& s : log().store().sessions()) {
            if (s.id == id)
                return s.closed;
        }
        return -1.0;
    };
    EXPECT_EQ(closedOf(session), 0.0);
    const auto count = store.transactions().size();
    EXPECT_TRUE(log().logout(carol));
    EXPECT_GT(closedOf(session), 0.0);
    EXPECT_EQ(log().store().transactions().size(), count);   // no row for leaving
    EXPECT_FALSE(log().logout(carol));

    // The same declared name again: another session of the same user.
    const int64_t third = log().login(later);
    ASSERT_GT(third, second);
    EXPECT_NE(log().store().transactions(third, 1)[0].session, session);
    EXPECT_EQ(authorOf(log(), third).id, authorOf(log(), first).id);

    // The desktop user logs in by opening the log: no row.
    EXPECT_EQ(log().login(App::Actor()), 0);
    // An undo steps over the logins: they are not steps.
    EXPECT_EQ(doc()->getAvailableUndoNames(), (std::vector<std::string> {"create"}));
}

// Sec 30.6: records are not movement. A login on the receiving branch does
// not turn a merge that can fast-forward into a merge row, and the login is
// still on the chain after it.
TEST_F(TransactionLogTest, aLoginDoesNotStandInAFastForwardsWay)
{
    edit(doc(), "create", [&]() { make("A")->Integer.setValue(1); });
    App::Document* other = doc()->openNewBranch("side");
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 31);
    {
        App::ActorScope scope(alice);
        edit(other, "theirs", [&]() { featureOf(other, "A")->Integer.setValue(7); });
    }
    const int64_t login = log().login(actor(App::Actor::Declared, "carol", 32, "view"));
    ASSERT_GT(login, 0);

    auto preview = doc()->previewMerge("side");
    ASSERT_FALSE(preview.forward.empty());
    auto result = doc()->mergeBranch("side");
    EXPECT_GT(result.forwarded, 0u);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 7);
    log().flush();
    bool sawLogin = false;
    bool sawTheirs = false;
    for (const auto& t : log().store().chain(log().head())) {
        EXPECT_NE(t.kind, "merge");
        if (t.kind == "login") {
            sawLogin = true;
            // Appended again past the rows taken, under the session it was
            // made in.
            EXPECT_TRUE(sawTheirs);
            EXPECT_EQ(authorOf(log(), t.seq).name, "carol");
        }
        if (t.name == "theirs") {
            sawTheirs = true;
            // Taken as made: each row under its author (sec 30.4 P1).
            EXPECT_EQ(authorOf(log(), t.seq).name, "alice@example.com");
        }
    }
    EXPECT_TRUE(sawLogin);
    EXPECT_TRUE(sawTheirs);

    App::GetApplication().closeDocument(otherName.c_str());
}

namespace
{

std::vector<std::string> undosOf(App::Document* doc, const App::Actor* who)
{
    if (!who)
        return doc->getAvailableUndoNames();
    App::ActorScope scope(*who);
    return doc->getAvailableUndoNames();
}

std::vector<std::string> redosOf(App::Document* doc, const App::Actor* who)
{
    if (!who)
        return doc->getAvailableRedoNames();
    App::ActorScope scope(*who);
    return doc->getAvailableRedoNames();
}

using Names = std::vector<std::string>;

}  // namespace

// Sec 30.3 S.c, 30.10: each author has the steps it made. An undo takes
// the actor's newest step though another's lie above it -- through the log
// then, as a row of the actor's -- and a new step ends only its author's
// redo.
TEST_F(TransactionLogTest, eachAuthorUndoesItsOwnSteps)
{
    edit(doc(), "create", [&]() {
        make("A");
        make("B");
    });
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 41);
    const App::Actor bob = actor(App::Actor::Invited, "bob", 42);
    {
        App::ActorScope scope(alice);
        edit(doc(), "hers", [&]() { featureOf(doc(), "A")->Integer.setValue(2); });
    }
    {
        App::ActorScope scope(bob);
        edit(doc(), "his", [&]() { featureOf(doc(), "B")->Integer.setValue(3); });
    }
    EXPECT_EQ(undosOf(doc(), nullptr), (Names {"create"}));
    EXPECT_EQ(undosOf(doc(), &alice), (Names {"hers"}));
    EXPECT_EQ(undosOf(doc(), &bob), (Names {"his"}));
    EXPECT_EQ(doc()->getAvailableUndos(), 1);

    // Hers is not the last thing written: through the log, and bob's stays.
    const int64_t hers = rowNamed(log(), "hers");
    {
        App::ActorScope scope(alice);
        EXPECT_TRUE(doc()->stepNeedsLog(true));
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 4711);
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 3);
    auto last = log().store().transactions().back();
    EXPECT_EQ(last.kind, "undo");
    EXPECT_EQ(last.inverts, hers);
    EXPECT_EQ(authorOf(log(), last.seq).name, "alice@example.com");
    EXPECT_EQ(undosOf(doc(), &alice), (Names {}));
    EXPECT_EQ(redosOf(doc(), &alice), (Names {"hers"}));
    EXPECT_EQ(undosOf(doc(), &bob), (Names {"his"}));
    EXPECT_EQ(redosOf(doc(), &bob), (Names {}));

    // His, with her undo written since: through the log too.
    {
        App::ActorScope scope(bob);
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 4711);
    // And hers again, redone past his undo.
    {
        App::ActorScope scope(alice);
        ASSERT_TRUE(doc()->redo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 2);
    last = log().store().transactions().back();
    EXPECT_EQ(last.kind, "redo");
    EXPECT_EQ(authorOf(log(), last.seq).name, "alice@example.com");
    EXPECT_EQ(undosOf(doc(), &alice), (Names {"hers"}));
    EXPECT_EQ(redosOf(doc(), &bob), (Names {"his"}));

    // A new step of hers ends her redo, not his.
    {
        App::ActorScope scope(alice);
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
        EXPECT_EQ(doc()->getAvailableRedoNames(), (Names {"hers"}));
        edit(doc(), "hers anew", [&]() { featureOf(doc(), "A")->Integer.setValue(9); });
        EXPECT_EQ(doc()->getAvailableRedoNames(), (Names {}));
        EXPECT_EQ(doc()->getAvailableUndoNames(), (Names {"hers anew"}));
    }
    EXPECT_EQ(redosOf(doc(), &bob), (Names {"his"}));
    {
        App::ActorScope scope(bob);
        ASSERT_TRUE(doc()->redo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 3);
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 9);

    // The desktop's own step made both objects, and both have been written
    // since: refused, saying so, and nothing moves (sec 30.4 P2).
    const auto rows = log().store().transactions().size();
    EXPECT_FALSE(doc()->undo());
    EXPECT_NE(doc()->undoRefusal().find("changed since"), std::string::npos)
        << doc()->undoRefusal();
    EXPECT_EQ(log().store().transactions().size(), rows);
    EXPECT_EQ(undosOf(doc(), nullptr), (Names {"create"}));
    EXPECT_TRUE(featureOf(doc(), "A"));
}

// Sec 30.4 P2: an undo of a step someone else has since written over is
// refused. Once that someone takes their own step back the document is in
// the state the first step left, and it is undone from its copies.
TEST_F(TransactionLogTest, anUndoOverAnothersWriteIsRefused)
{
    edit(doc(), "create", [&]() { make("A"); });
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 51);
    const App::Actor bob = actor(App::Actor::Invited, "bob", 52);
    {
        App::ActorScope scope(alice);
        edit(doc(), "hers", [&]() { featureOf(doc(), "A")->Integer.setValue(2); });
    }
    {
        App::ActorScope scope(bob);
        edit(doc(), "his over hers", [&]() { featureOf(doc(), "A")->Integer.setValue(5); });
    }
    const int64_t his = rowNamed(log(), "his over hers");
    {
        App::ActorScope scope(alice);
        EXPECT_FALSE(doc()->undo());
        EXPECT_NE(doc()->undoRefusal().find("by row " + std::to_string(his)), std::string::npos)
            << doc()->undoRefusal();
        EXPECT_EQ(doc()->getAvailableUndoNames(), (Names {"hers"}));
        EXPECT_EQ(doc()->getAvailableRedoNames(), (Names {}));
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 5);
    {
        App::ActorScope scope(bob);
        EXPECT_FALSE(doc()->stepNeedsLog(true));   // his is the last thing written
        ASSERT_TRUE(doc()->undo());
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 2);
    {
        App::ActorScope scope(alice);
        EXPECT_FALSE(doc()->stepNeedsLog(true));   // the state hers left
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
        EXPECT_TRUE(doc()->undoRefusal().empty());
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 4711);
    // And back up, each its own, from copies all the way.
    {
        App::ActorScope scope(alice);
        EXPECT_FALSE(doc()->stepNeedsLog(false));
        ASSERT_TRUE(doc()->redo());
    }
    {
        App::ActorScope scope(bob);
        EXPECT_FALSE(doc()->stepNeedsLog(false));
        ASSERT_TRUE(doc()->redo());
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 5);
}

// Sec 30.10 item 6: steps made again from the rows -- a switch away and
// back here -- are their rows' authors', undone and redone ones too.
TEST_F(TransactionLogTest, stepsRebuiltFromTheRowsKeepTheirAuthors)
{
    edit(doc(), "create", [&]() {
        make("A");
        make("B");
    });
    const App::Actor alice = actor(App::Actor::Verified, "alice@example.com", 61);
    const App::Actor bob = actor(App::Actor::Invited, "bob", 62);
    {
        App::ActorScope scope(alice);
        edit(doc(), "hers", [&]() { featureOf(doc(), "A")->Integer.setValue(2); });
        edit(doc(), "hers too", [&]() { featureOf(doc(), "A")->Integer.setValue(3); });
    }
    {
        App::ActorScope scope(bob);
        edit(doc(), "his", [&]() { featureOf(doc(), "B")->Integer.setValue(7); });
    }
    {
        App::ActorScope scope(alice);
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();   // past his, through the log
    }
    doc()->createBranch("side");
    ASSERT_TRUE(doc()->switchBranch("main"));

    EXPECT_EQ(undosOf(doc(), nullptr), (Names {"create"}));
    EXPECT_EQ(undosOf(doc(), &alice), (Names {"hers"}));
    EXPECT_EQ(redosOf(doc(), &alice), (Names {"hers too"}));
    EXPECT_EQ(undosOf(doc(), &bob), (Names {"his"}));
    EXPECT_EQ(redosOf(doc(), &bob), (Names {}));
    // The last thing written was her undo: its redo step is where the
    // document stands, his step is not.
    {
        App::ActorScope scope(alice);
        EXPECT_FALSE(doc()->stepNeedsLog(false));
    }
    {
        App::ActorScope scope(bob);
        EXPECT_TRUE(doc()->stepNeedsLog(true));
        ASSERT_TRUE(doc()->undo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "B")->Integer.getValue(), 4711);
    {
        App::ActorScope scope(alice);
        ASSERT_TRUE(doc()->redo()) << doc()->undoRefusal();
    }
    EXPECT_EQ(featureOf(doc(), "A")->Integer.getValue(), 3);
}

// Sec 30.3 S.e: a row is known by its session's uuid and its ordinal there,
// which a copy of the file keeps where `seq` does not: two copies number on
// from the same seq for different rows. Two files are one history when they
// hold a row in common, and the base of a fork is the newest such row.
TEST_F(TransactionLogTest, aRowIsKnownAcrossCopiesOfItsFile)
{
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    edit(doc(), "two", [&]() { featureOf(doc(), "Obj")->Integer.setValue(2); });
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-fork-ours.FCStd";
    const std::string fork = Base::FileInfo::getTempPath() + "txnlog-fork-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    // Someone copies the file as it is now, and both go on.
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));
    log().flush();
    const int64_t atCopy = log().lastSeq();
    edit(doc(), "ours", [&]() { featureOf(doc(), "Obj")->Integer.setValue(3); });
    edit(doc(), "ours too", [&]() { featureOf(doc(), "Obj")->Integer.setValue(4); });

    // Every row has an identity, and no two the same.
    auto& store = log().store();
    std::set<std::pair<std::string, int64_t>> seen;
    for (const auto& t : store.transactions()) {
        App::LogRowId id;
        ASSERT_TRUE(store.rowId(t.seq, id)) << t.seq;
        EXPECT_EQ(id.ordinal, t.ordinal);
        EXPECT_TRUE(seen.emplace(id.session, id.ordinal).second) << t.seq;
        EXPECT_EQ(store.findRow(id), t.seq);
    }

    std::string reason;
    auto theirs = App::FileHistory::openFile(fork, &reason);
    ASSERT_TRUE(theirs) << reason;
    App::Document* other = App::Document::openFileVersion(theirs, theirs->fileVersion(), false);
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs", [&]() { featureOf(other, "Obj")->Integer.setValue(9); });
    auto otherLog = other->getTransactionLog();
    ASSERT_TRUE(otherLog);
    auto& otherStore = otherLog->store();

    // The rows each made after the copy share their numbers and nothing
    // else.
    int64_t theirRow = 0;
    for (const auto& t : otherStore.transactions()) {
        if (t.name == "theirs")
            theirRow = t.seq;
    }
    ASSERT_GT(theirRow, atCopy);
    App::LogRowId theirId;
    ASSERT_TRUE(otherStore.rowId(theirRow, theirId));
    EXPECT_EQ(store.findRow(theirId), 0) << "a row made in the copy is not one of ours";
    App::LogRowId ourId;
    if (store.rowId(theirRow, ourId))
        EXPECT_FALSE(ourId == theirId) << "the same seq, another row";

    // The base: the newest row both hold, which is where the copy was made.
    int64_t ours = 0;
    const int64_t base = App::TransactionLogCore::of(doc()->getFileHistory())
                             .forkBase(otherStore, otherLog->head(), ours);
    EXPECT_GT(base, 0);
    EXPECT_EQ(ours, base) << "the rows before the copy keep their numbers in both";
    EXPECT_LE(base, atCopy);
    App::LogRowId a, b;
    ASSERT_TRUE(store.rowId(ours, a));
    ASSERT_TRUE(otherStore.rowId(base, b));
    EXPECT_TRUE(a == b);
    // Nothing either made since is at or below it, and "two" is.
    for (const auto& t : store.transactions()) {
        if (t.name == "ours" || t.name == "ours too")
            EXPECT_GT(t.seq, ours);
        if (t.name == "two")
            EXPECT_LE(t.seq, ours);
    }
    EXPECT_GT(theirRow, base);
    // The other way round finds the same row.
    int64_t back = 0;
    EXPECT_EQ(App::TransactionLogCore::of(other->getFileHistory())
                  .forkBase(store, log().head(), back),
              ours);
    EXPECT_EQ(back, base);

    // A history that shares nothing is not one history.
    App::DocumentParams::setTransactionLog(1);
    App::Document* stranger = App::GetApplication().newDocument("txnlogStranger", "stranger");
    stranger->setUndoMode(1);
    edit(stranger, "alone", [&]() { stranger->addObject("App::FeatureTest", "X"); });
    auto strangerLog = stranger->getTransactionLog();
    ASSERT_TRUE(strangerLog);
    int64_t none = 7;
    EXPECT_EQ(App::TransactionLogCore::of(doc()->getFileHistory())
                  .forkBase(strangerLog->store(), strangerLog->head(), none),
              0);
    EXPECT_EQ(none, 0);

    App::GetApplication().closeDocument("txnlogStranger");
    App::GetApplication().closeDocument(otherName.c_str());
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aForkIsImportedAsABranch)
{
    // Sec 30.13, 30.14 (S.f): the copy's rows after the newest row both
    // files hold come in as a branch here, each under the author and the
    // identity it had; what the copy made comes under ids of this file,
    // and under a new name where its own is taken.
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() {
        make("Obj")->Integer.setValue(1);
        make("Gone");
    });
    const std::string path = Base::FileInfo::getTempPath() + "txnlog-import-ours.FCStd";
    const std::string fork = Base::FileInfo::getTempPath() + "txnlog-import-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));

    // This file goes on: a value, and an object under a name the copy
    // gives to one of its own.
    edit(doc(), "ours", [&]() {
        featureOf(doc(), "Obj")->String.setValue("ours");
        make("New")->Integer.setValue(100);
    });
    const long ourNew = featureOf(doc(), "New")->getID();

    // The copy goes on, elsewhere.
    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs", [&]() { featureOf(other, "Obj")->Integer.setValue(9); });
    edit(other, "theirs made", [&]() {
        auto made = static_cast<App::FeatureTest*>(other->addObject("App::FeatureTest", "New"));
        made->Integer.setValue(7);
        featureOf(other, "Obj")->Link.setValue(made);
    });
    // A version the copy names on the way (F5).
    const int64_t milestone = other->snapshotToLog();
    ASSERT_GT(milestone, 0);
    ASSERT_TRUE(other->getTransactionLog()->store().nameVersion(milestone, "milestone"));
    edit(other, "theirs removed", [&]() { other->removeObject("Gone"); });
    std::map<std::string, App::LogRowId> theirIds;
    {
        auto otherLog = other->getTransactionLog();
        ASSERT_TRUE(otherLog);
        auto& otherStore = otherLog->store();
        for (const auto& t : otherStore.transactions()) {
            if (t.name.rfind("theirs", 0) == 0)
                ASSERT_TRUE(otherStore.rowId(t.seq, theirIds[t.name])) << t.name;
        }
    }
    ASSERT_EQ(theirIds.size(), 3u);
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    // What the copy offers: the branch its file reopens on, three
    // operations past the row both hold.
    const auto offered = doc()->forkBranches(fork);
    ASSERT_FALSE(offered.empty());
    int current = 0;
    for (const auto& b : offered) {
        if (!b.current)
            continue;
        ++current;
        EXPECT_GT(b.base, 0);
        EXPECT_EQ(b.ahead, 3u);
    }
    EXPECT_EQ(current, 1);

    log().flush();
    const int64_t headBefore = log().head();
    const size_t docsBefore = App::GetApplication().getDocuments().size();
    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.rows, 3u);
    EXPECT_FALSE(result.extended);
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    EXPECT_EQ(result.branch, "txnlog-import-theirs");
    EXPECT_GT(result.seq, 0);
    // This document did not move, and the one the rows were replayed in
    // is gone.
    EXPECT_EQ(log().head(), headBefore);
    EXPECT_EQ(App::GetApplication().getDocuments().size(), docsBefore);
    EXPECT_EQ(App::GetApplication().getActiveDocument(), doc());
    EXPECT_FALSE(featureOf(doc(), "Obj")->Link.getValue());
    // P4: the copy's New is not this file's New.
    ASSERT_EQ(result.renamed.size(), 1u);
    const std::string theirNew = result.renamed.begin()->second;
    EXPECT_EQ(result.renamed.begin()->first, "New");
    EXPECT_NE(theirNew, "New");

    auto& store = log().store();
    App::LogBranch branch;
    ASSERT_TRUE(store.findBranch(result.branch, branch));
    EXPECT_EQ(branch.fromSeq, result.base);
    std::map<int64_t, App::LogSession> sessions;
    for (const auto& s : store.sessions())
        sessions[s.id] = s;
    std::map<int64_t, App::LogUser> users;
    for (const auto& u : store.users())
        users[u.id] = u;
    std::vector<std::string> names;
    long newId = 0;
    int records = 0;
    for (const auto& t : store.chain(branch.head, result.base + 1)) {
        if (t.kind == "import") {
            // The import's own record is this process's.
            EXPECT_EQ(t.seq, result.seq);
            EXPECT_EQ(users[sessions[t.session].user].kind, "local");
            ++records;
            continue;
        }
        if (store.ops(t.seq).empty())
            continue;   // the tip left as a version
        names.push_back(t.name);
        // F3: the row is the copy's own; F4: so is its author.
        App::LogRowId id;
        ASSERT_TRUE(store.rowId(t.seq, id)) << t.name;
        ASSERT_TRUE(theirIds.count(t.name)) << t.name;
        EXPECT_TRUE(id == theirIds[t.name]) << t.name;
        const App::LogUser& author = users[sessions[t.session].user];
        EXPECT_EQ(author.kind, "fork") << t.name;
        EXPECT_NE(author.name.find("txnlog-import-theirs"), std::string::npos) << author.name;
        EXPECT_GT(sessions[t.session].closed, 0);
        for (const auto& o : store.ops(t.seq)) {
            if (o.op == "create") {
                EXPECT_EQ(o.cname, theirNew);
                newId = o.cid;
            }
        }
    }
    EXPECT_EQ(records, 1);
    // F5: the version the copy named is a version of the branch, taken
    // where the copy took it -- its New made, its Gone not yet removed.
    EXPECT_EQ(result.versions, 1u);
    {
        int found = 0;
        for (const auto& v : store.versions()) {
            if (v.name != "milestone")
                continue;
            ++found;
            EXPECT_EQ(v.branch, branch.id);
            EXPECT_EQ(v.kind, "named");
            int64_t made = 0;
            int64_t removed = 0;
            for (const auto& t : store.chain(branch.head, result.base + 1)) {
                if (t.name == "theirs made")
                    made = t.seq;
                if (t.name == "theirs removed")
                    removed = t.seq;
            }
            EXPECT_GE(v.seq, made);
            EXPECT_LT(v.seq, removed);
        }
        EXPECT_EQ(found, 1);
    }
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "theirs");
    EXPECT_EQ(names[1], "theirs made");
    EXPECT_EQ(names[2], "theirs removed");
    EXPECT_GT(newId, 0);
    EXPECT_NE(newId, ourNew);

    // The merge is 28's: both moved, nothing conflicts.
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_GT(merged.seq, 0);
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 9);
    EXPECT_STREQ(featureOf(doc(), "Obj")->String.getValue(), "ours");
    ASSERT_TRUE(featureOf(doc(), "New"));
    EXPECT_EQ(featureOf(doc(), "New")->Integer.getValue(), 100);
    ASSERT_TRUE(featureOf(doc(), theirNew.c_str()));
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->Integer.getValue(), 7);
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->getID(), newId);
    // The link the copy set names its own New, under the name it has here.
    EXPECT_EQ(featureOf(doc(), "Obj")->Link.getValue(), featureOf(doc(), theirNew.c_str()));
    EXPECT_FALSE(doc()->getObject("Gone"));

    // F7: the copy goes on, and a second import continues the branch from
    // the last row brought over.
    other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs again", [&]() { featureOf(other, "New")->Integer.setValue(8); });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());
    const auto again = doc()->importFork(fork);
    EXPECT_TRUE(again.extended);
    EXPECT_EQ(again.branch, result.branch);
    EXPECT_EQ(again.rows, 1u) << again.reason;
    EXPECT_EQ(again.stoppedAt, 0) << again.reason;
    EXPECT_TRUE(again.renamed.empty());
    const auto more = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(more.unresolved.empty());
    // The copy's New, still: its id here was kept across the imports.
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->Integer.getValue(), 8);
    EXPECT_EQ(featureOf(doc(), "New")->Integer.getValue(), 100);
    // And nothing new is nothing done.
    const auto none = doc()->importFork(fork);
    EXPECT_EQ(none.rows, 0u);
    EXPECT_EQ(none.seq, 0);

    // A file that shares nothing is refused.
    App::DocumentParams::setTransactionLog(2);
    App::Document* stranger = App::GetApplication().newDocument("txnlogStranger", "stranger");
    stranger->setUndoMode(1);
    edit(stranger, "alone", [&]() { stranger->addObject("App::FeatureTest", "X"); });
    const std::string strange = Base::FileInfo::getTempPath() + "txnlog-import-stranger.FCStd";
    Base::FileInfo(strange).deleteFile();
    ASSERT_TRUE(stranger->saveAs(strange.c_str()));
    App::GetApplication().closeDocument("txnlogStranger");
    App::GetApplication().setActiveDocument(doc());
    EXPECT_THROW(doc()->importFork(fork, "no-such-branch"), Base::Exception);

    // A file that shares nothing is not refused (sec 30.22): what it is
    // comes as an independent branch, one that hangs off no row, and is
    // merged by what the two hold -- its object beside this file's.
    for (const auto& b : doc()->forkBranches(strange)) {
        if (b.current) {
            EXPECT_TRUE(b.independent);
            EXPECT_EQ(b.base, 0);
            EXPECT_EQ(b.ahead, 1u);
        }
    }
    const auto alone = doc()->importFork(strange);
    EXPECT_TRUE(alone.independent);
    EXPECT_EQ(alone.base, 0);
    EXPECT_EQ(alone.rows, 1u) << alone.reason;
    EXPECT_EQ(alone.stoppedAt, 0) << alone.reason;
    EXPECT_EQ(alone.branch, "txnlog-import-stranger");
    App::LogBranch apart;
    ASSERT_TRUE(store.findBranch(alone.branch, apart));
    EXPECT_EQ(apart.fromSeq, 0);
    {
        const auto chain = store.chain(apart.head);
        ASSERT_FALSE(chain.empty());
        EXPECT_EQ(chain.front().parent, 0);
    }
    const size_t objects = doc()->getObjects().size();
    const auto joined = doc()->mergeBranch(alone.branch);
    EXPECT_GT(joined.seq, 0);
    EXPECT_TRUE(joined.unresolved.empty());
    EXPECT_TRUE(doc()->getObject("X"));
    EXPECT_EQ(doc()->getObjects().size(), objects + 1);
    EXPECT_EQ(featureOf(doc(), "New")->Integer.getValue(), 100);
    // Brought again, unchanged: nothing.
    EXPECT_EQ(doc()->importFork(strange).rows, 0u);

    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    Base::FileInfo(strange).deleteFile();
}

TEST_F(TransactionLogTest, aForkImportTakesTheBranchAskedForAndTheFilesItNames)
{
    // Sec 30.14 F6: a copy with branches of its own gives the one that is
    // asked for, each to a branch of its own here; and a value that names
    // a file brings the file (sec 30.15).
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-import2-ours.FCStd";
    const std::string fork = tmp + "txnlog-import2-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));

    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    other->setUndoMode(1);
    ASSERT_GT(other->createBranch("side"), 0);
    edit(other, "theirs side", [&]() { featureOf(other, "Obj")->Integer.setValue(5); });
    ASSERT_TRUE(other->switchBranch("main"));
    const std::string bytes = blobText(-1);
    edit(other, "theirs file", [&]() {
        auto obj = other->getObject("Obj");
        ASSERT_TRUE(obj->addDynamicProperty("App::PropertyFileIncluded", "File"));
        const std::string src = tmp + "txnlog-import2-blob.txt";
        {
            Base::ofstream out(Base::FileInfo(src), std::ios::out | std::ios::binary);
            out << bytes;
        }
        static_cast<App::PropertyFileIncluded*>(obj->getPropertyByName("File"))
            ->setValue(src.c_str(), "data.txt");
        Base::FileInfo(src).deleteFile();
    });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    // Both branches are offered, the one the file reopens on marked.
    std::map<std::string, App::Document::ForkBranch> offered;
    for (const auto& b : doc()->forkBranches(fork))
        offered[b.name] = b;
    ASSERT_TRUE(offered.count("main"));
    ASSERT_TRUE(offered.count("side"));
    EXPECT_TRUE(offered["main"].current);
    EXPECT_FALSE(offered["side"].current);
    EXPECT_GT(offered["main"].base, 0);
    EXPECT_EQ(offered["side"].base, offered["main"].base);
    EXPECT_EQ(offered["main"].ahead, 1u);
    EXPECT_EQ(offered["side"].ahead, 1u);

    // None named: the one its file reopens on, to a branch named after
    // the file.
    const auto first = doc()->importFork(fork);
    EXPECT_EQ(first.from, "main");
    EXPECT_EQ(first.branch, "txnlog-import2-theirs");
    EXPECT_EQ(first.rows, 1u) << first.reason;
    EXPECT_EQ(first.stoppedAt, 0) << first.reason;
    // The other, asked for by name: a branch of its own, named for both.
    const auto second = doc()->importFork(fork, "side");
    EXPECT_EQ(second.from, "side");
    EXPECT_EQ(second.branch, "txnlog-import2-theirs@side");
    EXPECT_FALSE(second.extended);
    EXPECT_EQ(second.rows, 1u) << second.reason;
    EXPECT_EQ(second.base, first.base);
    auto& store = log().store();
    for (const auto& name : {first.branch, second.branch}) {
        App::LogBranch b;
        ASSERT_TRUE(store.findBranch(name, b)) << name;
        std::vector<std::string> names;
        for (const auto& t : store.chain(b.head, first.base + 1)) {
            if (!store.ops(t.seq).empty())
                names.push_back(t.name);
        }
        ASSERT_EQ(names.size(), 1u) << name;
        EXPECT_EQ(names[0], name == first.branch ? "theirs file" : "theirs side");
    }

    // Each merged when asked: the file with its content, then the value.
    const auto merged = doc()->mergeBranch(first.branch);
    EXPECT_TRUE(merged.unresolved.empty());
    auto obj = doc()->getObject("Obj");
    ASSERT_TRUE(obj);
    auto file = dynamic_cast<App::PropertyFileIncluded*>(obj->getPropertyByName("File"));
    ASSERT_TRUE(file);
    EXPECT_EQ(readFile(file->getValue()), bytes);
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 1);
    const auto side = doc()->mergeBranch(second.branch);
    EXPECT_TRUE(side.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 5);
    file = dynamic_cast<App::PropertyFileIncluded*>(obj->getPropertyByName("File"));
    ASSERT_TRUE(file);
    EXPECT_EQ(readFile(file->getValue()), bytes);

    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

namespace {

/// The warnings the console is sent, kept.
class WarningsHeard: public Base::ILogger
{
public:
    WarningsHeard()
    {
        bErr = bMsg = bLog = bCritical = bNotification = false;
        Base::Console().AttachObserver(this);
    }
    ~WarningsHeard() override
    {
        Base::Console().DetachObserver(this);
    }
    void SendLog(const std::string&, const std::string& msg, Base::LogStyle level,
                 Base::IntendedRecipient, Base::ContentType) override
    {
        if (level == Base::LogStyle::Warning)
            said.push_back(msg);
    }
    const char* Name() override
    {
        return "TxnLogWarningsHeard";
    }
    size_t saying(const char* what) const
    {
        size_t n = 0;
        for (const auto& m : said)
            n += m.find(what) != std::string::npos;
        return n;
    }
    std::vector<std::string> said;
};

} // namespace

TEST_F(TransactionLogTest, aVersionOpenedWaitsForNoHistoryFile)
{
    // Sec 30.37: a version taken after a save says, in its Document.xml,
    // which history file the document carried then -- and carries none
    // (sec 27.29). Opened as a document, or read for an import's replay, it
    // waited for that file and the blob store said one was missing.
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-nohist-ours.FCStd";
    const std::string fork = tmp + "txnlog-nohist-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    log().flush();
    int64_t saved = 0;
    for (const auto& v : log().store().versions())
        saved = std::max(saved, v.num);
    ASSERT_GT(saved, 0);
    // A save's version names the history file that save wrote. The next
    // save writes another, and the first is nobody's any more.
    edit(doc(), "two", [&]() { featureOf(doc(), "Obj")->Integer.setValue(2); });
    ASSERT_TRUE(doc()->save());
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));
    edit(doc(), "three", [&]() { featureOf(doc(), "Obj")->String.setValue("ours"); });
    ASSERT_TRUE(doc()->save());

    WarningsHeard heard;
    App::Document* version = doc()->openVersion(saved, false);
    ASSERT_TRUE(version);
    ASSERT_NE(version, doc());
    ASSERT_TRUE(featureOf(version, "Obj"));
    EXPECT_EQ(featureOf(version, "Obj")->Integer.getValue(), 1);
    EXPECT_EQ(heard.saying("is missing from the document"), 0u)
        << (heard.said.empty() ? std::string() : heard.said.front());
    const std::string versionName = version->getName();
    App::GetApplication().closeDocument(versionName.c_str());
    App::GetApplication().setActiveDocument(doc());
    heard.said.clear();

    // An import reads such a version to replay the copy's rows from.
    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs", [&]() { featureOf(other, "Obj")->Integer.setValue(3); });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());
    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.rows, 1u) << result.reason;
    EXPECT_EQ(heard.saying("is missing from the document"), 0u)
        << (heard.said.empty() ? std::string() : heard.said.front());
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aCopyThatTookFromThisFileNamesItsObjects)
{
    // Sec 30.33: a copy that has taken this file's rows holds this file's
    // objects under ids of its own, and names them so in every row it
    // writes after. Which numbers those are is chance -- here they are made
    // to be the worst there is: the copy's id for each object is this
    // file's id for another. Its edit and its removal are of the objects
    // they were made on.
    App::DocumentParams::setTransactionLog(2);   // embedded
    doc()->setLastObjectId(6000);
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-round-ours.FCStd";
    const std::string fork = tmp + "txnlog-round-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));

    edit(doc(), "ours made", [&]() {
        make("Mine")->Integer.setValue(10);
        make("Gone")->Integer.setValue(20);
        make("Other")->Integer.setValue(30);
    });
    const long mine = featureOf(doc(), "Mine")->getID();
    const long gone = featureOf(doc(), "Gone")->getID();
    const long kept = featureOf(doc(), "Other")->getID();
    ASSERT_EQ(gone, mine + 1);
    ASSERT_EQ(kept, mine + 2);
    ASSERT_TRUE(doc()->save());

    // The copy takes them: nothing of its own, so the rows as they are.
    // Its counter is one past this file's, so each lands on the next.
    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    std::string otherName = other->getName();
    other->setUndoMode(1);
    other->getFileHistory().noteObjectId(mine);
    const auto took = other->importFork(path);
    ASSERT_EQ(took.stoppedAt, 0) << took.reason;
    ASSERT_EQ(took.rows, 1u);
    const auto into = other->mergeBranch(took.branch);
    ASSERT_TRUE(into.unresolved.empty());
    ASSERT_TRUE(featureOf(other, "Mine"));
    ASSERT_EQ(featureOf(other, "Mine")->getID(), gone);
    ASSERT_EQ(featureOf(other, "Gone")->getID(), kept);
    edit(other, "theirs changed", [&]() { featureOf(other, "Mine")->Integer.setValue(11); });
    edit(other, "theirs removed", [&]() { other->removeObject("Gone"); });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    edit(doc(), "ours again", [&]() { featureOf(doc(), "Mine")->String.setValue("ours"); });
    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    EXPECT_EQ(result.rows, 2u);
    EXPECT_TRUE(result.renamed.empty());
    auto& store = log().store();
    App::LogBranch branch;
    ASSERT_TRUE(store.findBranch(result.branch, branch));
    int sets = 0;
    int removes = 0;
    for (const auto& t : store.chain(branch.head, result.base + 1)) {
        for (const auto& o : store.ops(t.seq)) {
            EXPECT_NE(o.op, "create") << t.name;
            // A removal's row says what the object's values were, too.
            if (o.op == "set" && o.prop == "Integer" && t.name == "theirs changed") {
                EXPECT_EQ(o.cid, mine) << t.name;
                ++sets;
            }
            else if (o.op == "set") {
                EXPECT_EQ(o.cid, gone) << t.name;
            }
            if (o.op == "remove") {
                EXPECT_EQ(o.cid, gone) << t.name;
                ++removes;
            }
        }
    }
    EXPECT_EQ(sets, 1);
    EXPECT_EQ(removes, 1);
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(merged.unresolved.empty());
    ASSERT_TRUE(featureOf(doc(), "Mine"));
    EXPECT_EQ(featureOf(doc(), "Mine")->getID(), mine);
    EXPECT_EQ(featureOf(doc(), "Mine")->Integer.getValue(), 11);
    EXPECT_STREQ(featureOf(doc(), "Mine")->String.getValue(), "ours");
    EXPECT_FALSE(doc()->getObject("Gone"));
    ASSERT_TRUE(featureOf(doc(), "Other"));
    EXPECT_EQ(featureOf(doc(), "Other")->getID(), kept);
    EXPECT_EQ(featureOf(doc(), "Other")->Integer.getValue(), 30);
    EXPECT_EQ(doc()->getObjects().size(), 3u);

    // Both change one value of it: that is a conflict, and is said.
    other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs again", [&]() { featureOf(other, "Mine")->Integer.setValue(12); });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());
    edit(doc(), "ours too", [&]() { featureOf(doc(), "Mine")->Integer.setValue(13); });
    const auto again = doc()->importFork(fork);
    EXPECT_EQ(again.stoppedAt, 0) << again.reason;
    EXPECT_EQ(again.rows, 1u);
    EXPECT_TRUE(again.extended);
    const auto refused = doc()->mergeBranch(again.branch);
    ASSERT_EQ(refused.unresolved.size(), 1u);
    EXPECT_EQ(refused.unresolved.front().key, "Mine.Integer");
    EXPECT_EQ(featureOf(doc(), "Mine")->Integer.getValue(), 13);
    const auto picked = doc()->mergeBranch(again.branch, {{"Mine.Integer", "theirs"}});
    EXPECT_TRUE(picked.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), "Mine")->Integer.getValue(), 12);
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aSentFileIsThereAfterACrash)
{
    // Sec 30.29, 30.38: a file kept in the log is a row and a blob, and
    // both are down before the call returns. What a crash leaves behind
    // recovers with the file waiting, its bytes as they came.
    doc()->openTransaction("create");
    make("Obj")->Integer.setValue(1);
    doc()->commitTransaction();
    std::string bytes;
    for (int i = 0; i < 5000; ++i)
        bytes += static_cast<char>((i * 31 + 7) & 0xff);
    const int64_t kept = doc()->keepSentFile(bytes, "sent.FCStd", "lei", "invited");
    ASSERT_GT(kept, 0);
    doc()->openTransaction("after");
    featureOf(doc(), "Obj")->Integer.setValue(2);
    doc()->commitTransaction();
    log().flush();

    const std::string crashed = Base::FileInfo::getTempPath() + "txnlog-crashed-sent";
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
    ASSERT_TRUE(featureOf(recovered, "Obj"));
    EXPECT_EQ(featureOf(recovered, "Obj")->Integer.getValue(), 2);
    const auto held = recovered->sentFiles();
    ASSERT_EQ(held.size(), 1u);
    EXPECT_EQ(held.front().name, "sent.FCStd");
    EXPECT_EQ(held.front().sender, "lei");
    EXPECT_EQ(held.front().senderKind, "invited");
    EXPECT_EQ(held.front().size, static_cast<int64_t>(bytes.size()));
    EXPECT_TRUE(held.front().branch.empty());
    const std::string out = Base::FileInfo::getTempPath() + "txnlog-crashed-sent.out";
    Base::FileInfo(out).deleteFile();
    recovered->writeSentFile(held.front().seq, out);
    {
        Base::FileInfo fi(out);
        Base::ifstream in(fi, std::ios::in | std::ios::binary);
        const std::string back((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        EXPECT_EQ(back, bytes);
    }
    Base::FileInfo(out).deleteFile();
    // And it can still be let go.
    EXPECT_TRUE(recovered->dropSentFile(held.front().seq));
    EXPECT_TRUE(recovered->sentFiles().empty());
    App::GetApplication().closeDocument(name.c_str());
}

TEST_F(TransactionLogTest, anObjectTheCopyBringsBackIsNotASecondOne)
{
    // Sec 30.37: the copy removes an object and undoes that. The undo's
    // row makes the object -- the one it was, under its id -- and an import
    // made a new one of it: for an object both had where they parted, and
    // for one of the copy's own that an earlier import had brought.
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() {
        make("Obj")->Integer.setValue(1);
        make("Back")->Integer.setValue(2);
    });
    const long back = featureOf(doc(), "Back")->getID();
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-undo-ours.FCStd";
    const std::string fork = tmp + "txnlog-undo-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));

    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs removed", [&]() { other->removeObject("Back"); });
    ASSERT_FALSE(other->getObject("Back"));
    ASSERT_TRUE(other->undo());
    ASSERT_TRUE(featureOf(other, "Back"));
    ASSERT_EQ(featureOf(other, "Back")->getID(), back);
    edit(other, "theirs changed", [&]() { featureOf(other, "Back")->Integer.setValue(5); });
    edit(other, "theirs made", [&]() {
        static_cast<App::FeatureTest*>(other->addObject("App::FeatureTest", "Theirs"))
            ->Integer.setValue(7);
    });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    edit(doc(), "ours", [&]() { featureOf(doc(), "Obj")->String.setValue("ours"); });
    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    EXPECT_TRUE(result.renamed.empty());
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_EQ(doc()->getObjects().size(), 3u);
    ASSERT_TRUE(featureOf(doc(), "Back"));
    EXPECT_EQ(featureOf(doc(), "Back")->getID(), back);
    EXPECT_EQ(featureOf(doc(), "Back")->Integer.getValue(), 5);
    ASSERT_TRUE(featureOf(doc(), "Theirs"));
    const long theirs = featureOf(doc(), "Theirs")->getID();

    // The copy's own object, which that import brought: removed, back.
    other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs removed again", [&]() { other->removeObject("Theirs"); });
    ASSERT_TRUE(other->undo());
    ASSERT_TRUE(featureOf(other, "Theirs"));
    edit(other, "theirs changed again", [&]() {
        featureOf(other, "Theirs")->Integer.setValue(8);
    });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());
    const auto again = doc()->importFork(fork);
    EXPECT_EQ(again.stoppedAt, 0) << again.reason;
    EXPECT_TRUE(again.extended);
    EXPECT_TRUE(again.renamed.empty());
    const auto more = doc()->mergeBranch(again.branch);
    EXPECT_TRUE(more.unresolved.empty());
    EXPECT_EQ(doc()->getObjects().size(), 3u);
    ASSERT_TRUE(featureOf(doc(), "Theirs"));
    EXPECT_EQ(featureOf(doc(), "Theirs")->getID(), theirs);
    EXPECT_EQ(featureOf(doc(), "Theirs")->Integer.getValue(), 8);
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aMergeOfThisFilesRowsStaysAMerge)
{
    // Sec 30.33, 30.34: the copy had work of its own when it took this
    // file's rows, so what it took is in a merge row of its own -- a row
    // that makes this file's objects. Brought back, the row makes them
    // under the ids and the names they have here, and is a merge still:
    // its second parent the row of this file it merged, which is then where
    // the merge of the branch starts from.
    App::DocumentParams::setTransactionLog(2);   // embedded
    doc()->setLastObjectId(6000);
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-back-ours.FCStd";
    const std::string fork = tmp + "txnlog-back-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));

    edit(doc(), "ours made", [&]() { make("Mine")->Integer.setValue(10); });
    const long mine = featureOf(doc(), "Mine")->getID();
    log().flush();
    int64_t oursMade = 0;
    for (const auto& t : log().store().transactions()) {
        if (t.name == "ours made")
            oursMade = t.seq;
    }
    ASSERT_GT(oursMade, 0);
    ASSERT_TRUE(doc()->save());

    // The copy has a Mine of its own -- under the very id this file's has
    // -- so this file's comes there under another name, and another id.
    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs made", [&]() {
        auto made = static_cast<App::FeatureTest*>(other->addObject("App::FeatureTest", "Mine"));
        made->Integer.setValue(7);
    });
    ASSERT_EQ(featureOf(other, "Mine")->getID(), mine);
    const auto took = other->importFork(path);
    ASSERT_EQ(took.stoppedAt, 0) << took.reason;
    ASSERT_EQ(took.renamed.size(), 1u);
    const std::string there = took.renamed.begin()->second;
    ASSERT_NE(there, "Mine");
    const auto into = other->mergeBranch(took.branch);
    ASSERT_TRUE(into.unresolved.empty());
    ASSERT_TRUE(featureOf(other, there.c_str()));
    const long mineThere = featureOf(other, there.c_str())->getID();
    ASSERT_NE(mineThere, mine);
    edit(other, "theirs changed", [&]() {
        featureOf(other, there.c_str())->Integer.setValue(11);
        featureOf(other, "Obj")->Link.setValue(featureOf(other, there.c_str()));
    });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    EXPECT_EQ(result.rows, 3u);
    // The copy's own Mine is not this file's, and is renamed; this file's,
    // which the copy had renamed, is Mine again and no renaming to report.
    ASSERT_EQ(result.renamed.size(), 1u);
    EXPECT_EQ(result.renamed.begin()->first, "Mine");
    const std::string theirMine = result.renamed.begin()->second;
    EXPECT_NE(theirMine, "Mine");
    auto& store = log().store();
    App::LogBranch branch;
    ASSERT_TRUE(store.findBranch(result.branch, branch));
    int merges = 0;
    for (const auto& t : store.chain(branch.head, result.base + 1)) {
        const auto ops = store.ops(t.seq);
        if (ops.empty())
            continue;
        if (t.name == "theirs made" || t.name == "theirs changed") {
            EXPECT_EQ(t.kind, "user") << t.name;
            EXPECT_EQ(t.mergeFrom, 0) << t.name;
            continue;
        }
        ++merges;
        EXPECT_EQ(t.kind, "merge") << t.name;
        EXPECT_EQ(t.mergeFrom, oursMade) << t.name;
        int creates = 0;
        for (const auto& o : ops) {
            if (o.op != "create")
                continue;
            ++creates;
            EXPECT_EQ(o.cid, mine);
            EXPECT_EQ(o.cname, "Mine");
        }
        EXPECT_EQ(creates, 1);
    }
    EXPECT_EQ(merges, 1);

    // Nothing here has changed since the copy took it: nothing to ask.
    const auto preview = doc()->previewMerge(result.branch);
    EXPECT_EQ(preview.base, oursMade);
    EXPECT_EQ(preview.conflicts, 0u);
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_EQ(doc()->getObjects().size(), 3u);
    ASSERT_TRUE(featureOf(doc(), "Mine"));
    EXPECT_EQ(featureOf(doc(), "Mine")->getID(), mine);
    EXPECT_EQ(featureOf(doc(), "Mine")->Integer.getValue(), 11);
    ASSERT_TRUE(featureOf(doc(), theirMine.c_str()));
    EXPECT_EQ(featureOf(doc(), theirMine.c_str())->Integer.getValue(), 7);
    // The link the copy set names this file's object, by its name there.
    EXPECT_EQ(featureOf(doc(), "Obj")->Link.getValue(), featureOf(doc(), "Mine"));

    // A second round: this file changes the object the copy made, and the
    // copy takes that. Nothing comes a second time on that side either.
    edit(doc(), "ours again", [&]() {
        featureOf(doc(), theirMine.c_str())->Integer.setValue(8);
    });
    ASSERT_TRUE(doc()->save());
    other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    otherName = other->getName();
    other->setUndoMode(1);
    const size_t before = other->getObjects().size();
    const auto back = other->importFork(path);
    EXPECT_EQ(back.stoppedAt, 0) << back.reason;
    EXPECT_TRUE(back.renamed.empty());
    const auto round = other->mergeBranch(back.branch);
    EXPECT_TRUE(round.unresolved.empty());
    EXPECT_EQ(other->getObjects().size(), before);
    ASSERT_TRUE(featureOf(other, "Mine"));
    EXPECT_EQ(featureOf(other, "Mine")->getID(), mine);
    EXPECT_EQ(featureOf(other, "Mine")->Integer.getValue(), 8);
    ASSERT_TRUE(featureOf(other, there.c_str()));
    EXPECT_EQ(featureOf(other, there.c_str())->getID(), mineThere);
    EXPECT_EQ(featureOf(other, there.c_str())->Integer.getValue(), 11);
    EXPECT_EQ(featureOf(other, "Obj")->Link.getValue(), featureOf(other, there.c_str()));
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aMergeThatLeftSomethingOutIsARowLikeAny)
{
    // Sec 30.34: a replay may leave things out -- a view provider's ops
    // where there is no Gui. A merge row kept a merge says the branch holds
    // this file's rows up to the one it names, and what the copy left out
    // would read as taken back. Here the copy's replayed row is made to
    // lack one of the two changes of the row it stands for: its merge comes
    // as a row like any, and this file's change stays.
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() { make("Obj")->Integer.setValue(1); });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-left-ours.FCStd";
    const std::string fork = tmp + "txnlog-left-theirs.FCStd";
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    ASSERT_TRUE(Base::FileInfo(path).copyTo(fork.c_str()));
    edit(doc(), "ours", [&]() {
        featureOf(doc(), "Obj")->String.setValue("ours");
        featureOf(doc(), "Obj")->Integer.setValue(5);
    });
    ASSERT_TRUE(doc()->save());

    App::Document* other = App::GetApplication().openDocument(fork.c_str());
    ASSERT_TRUE(other);
    const std::string otherName = other->getName();
    other->setUndoMode(1);
    edit(other, "theirs made", [&]() { other->addObject("App::FeatureTest", "Theirs"); });
    const auto took = other->importFork(path);
    ASSERT_EQ(took.stoppedAt, 0) << took.reason;
    ASSERT_EQ(took.rows, 1u);
    {
        auto otherLog = other->getTransactionLog();
        ASSERT_TRUE(otherLog);
        otherLog->flush();
        auto& theirs = otherLog->store();
        App::LogBranch came;
        ASSERT_TRUE(theirs.findBranch(took.branch, came));
        int rewritten = 0;
        for (const auto& t : theirs.chain(came.head, took.base + 1)) {
            if (t.name != "ours")
                continue;
            std::vector<App::LogOp> ops;
            for (const auto& o : theirs.ops(t.seq)) {
                if (o.prop != "String")
                    ops.push_back(o);
            }
            ASSERT_EQ(ops.size(), 1u);
            theirs.replaceTransactions(t, ops, {});
            ++rewritten;
        }
        ASSERT_EQ(rewritten, 1);
    }
    const auto into = other->mergeBranch(took.branch);
    ASSERT_TRUE(into.unresolved.empty());
    ASSERT_EQ(featureOf(other, "Obj")->Integer.getValue(), 5);
    ASSERT_STRNE(featureOf(other, "Obj")->String.getValue(), "ours");
    edit(other, "theirs changed", [&]() { featureOf(other, "Theirs")->Integer.setValue(3); });
    ASSERT_TRUE(other->save());
    App::GetApplication().closeDocument(otherName.c_str());
    App::GetApplication().setActiveDocument(doc());

    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    auto& store = log().store();
    App::LogBranch branch;
    ASSERT_TRUE(store.findBranch(result.branch, branch));
    for (const auto& t : store.chain(branch.head, result.base + 1)) {
        if (!store.ops(t.seq).empty()) {
            EXPECT_NE(t.kind, "merge") << t.name;
            EXPECT_EQ(t.mergeFrom, 0) << t.name;
        }
    }
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_STREQ(featureOf(doc(), "Obj")->String.getValue(), "ours");
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 5);
    ASSERT_TRUE(featureOf(doc(), "Theirs"));
    EXPECT_EQ(featureOf(doc(), "Theirs")->Integer.getValue(), 3);
    Base::FileInfo(path).deleteFile();
    Base::FileInfo(fork).deleteFile();
}

TEST_F(TransactionLogTest, aFileWithNoHistoryIsImportedAsOneRow)
{
    // Sec 30.19 (S.g): a copy handed out without the history says which
    // save it is (G1, G2); edited where there is no log and brought back,
    // it comes as one row -- the difference from the state at that save --
    // on a branch from that save's row.
    App::DocumentParams::setTransactionLog(2);   // embedded
    edit(doc(), "create", [&]() {
        make("Obj")->Integer.setValue(1);
        make("Gone");
    });
    const std::string tmp = Base::FileInfo::getTempPath();
    const std::string path = tmp + "txnlog-state-ours.FCStd";
    const std::string fork = tmp + "txnlog-state-theirs.FCStd";
    const std::string strange = tmp + "txnlog-state-stranger.FCStd";
    for (const auto& file : {path, fork, strange})
        Base::FileInfo(file).deleteFile();
    ASSERT_TRUE(doc()->saveAs(path.c_str()));
    edit(doc(), "ours", [&]() { featureOf(doc(), "Obj")->String.setValue("ours"); });
    log().flush();
    const int64_t atCopy = log().head();
    ASSERT_TRUE(doc()->saveCopy(fork.c_str(), false));

    // G1: the file's own save is known by its id, and G2: so is the copy's,
    // at the row the copy's state is -- which is not the file's save.
    auto& store = log().store();
    App::FileHistory::Saved own;
    App::FileHistory::Saved saved;
    ASSERT_TRUE(App::FileHistory::savedAs(path, own));
    ASSERT_TRUE(App::FileHistory::savedAs(fork, saved));
    EXPECT_TRUE(own.history);
    EXPECT_FALSE(saved.history);
    ASSERT_FALSE(own.saveId.empty());
    ASSERT_FALSE(saved.saveId.empty());
    EXPECT_NE(own.saveId, saved.saveId);
    int64_t version = 0;
    int64_t seq = 0;
    ASSERT_TRUE(App::TransactionLog::savedAt(store, own.saveId, version, seq));
    EXPECT_EQ(version, own.version);
    EXPECT_LT(seq, atCopy);
    ASSERT_TRUE(App::TransactionLog::savedAt(store, saved.saveId, version, seq));
    EXPECT_EQ(seq, atCopy);
    // The live document says what it said: the stamp was the copy's alone.
    auto mark = dynamic_cast<App::PropertyString*>(doc()->getPropertyByName("Version"));
    ASSERT_TRUE(mark);
    EXPECT_NE(std::string(mark->getValue()).find(own.saveId), std::string::npos);

    // This file goes on: an object under a name the copy will use too.
    edit(doc(), "ours later", [&]() { make("New")->Integer.setValue(100); });

    // The copy is edited where there is no log, and saved.
    auto elsewhere = [&](const std::function<void(App::Document*)>& change) {
        App::DocumentParams::setTransactionLog(0);
        App::Document* other = App::GetApplication().openDocument(fork.c_str());
        ASSERT_TRUE(other);
        EXPECT_FALSE(other->getTransactionLog());
        const std::string name = other->getName();
        change(other);
        ASSERT_TRUE(other->save());
        App::GetApplication().closeDocument(name.c_str());
        App::DocumentParams::setTransactionLog(2);
        App::GetApplication().setActiveDocument(doc());
    };
    elsewhere([&](App::Document* other) {
        featureOf(other, "Obj")->Integer.setValue(9);
        auto made = static_cast<App::FeatureTest*>(other->addObject("App::FeatureTest", "New"));
        made->Integer.setValue(7);
        featureOf(other, "Obj")->Link.setValue(made);
        other->removeObject("Gone");
        // What knows no log does not carry the log's own properties on.
        other->removeDynamicProperty("History");
        other->removeDynamicProperty("Branch");
    });

    // What it offers: itself, one thing to bring, from the copy's row.
    auto offered = doc()->forkBranches(fork);
    ASSERT_EQ(offered.size(), 1u);
    EXPECT_TRUE(offered[0].current);
    EXPECT_EQ(offered[0].base, atCopy);
    EXPECT_EQ(offered[0].ahead, 1u);

    log().flush();
    const int64_t headBefore = log().head();
    const size_t docsBefore = App::GetApplication().getDocuments().size();
    const auto result = doc()->importFork(fork);
    EXPECT_EQ(result.rows, 1u) << result.reason;
    EXPECT_EQ(result.stoppedAt, 0) << result.reason;
    EXPECT_EQ(result.base, atCopy);
    EXPECT_EQ(result.branch, "txnlog-state-theirs");
    EXPECT_FALSE(result.extended);
    EXPECT_EQ(log().head(), headBefore);
    EXPECT_EQ(App::GetApplication().getDocuments().size(), docsBefore);
    EXPECT_EQ(App::GetApplication().getActiveDocument(), doc());
    ASSERT_EQ(result.renamed.size(), 1u);
    const std::string theirNew = result.renamed.begin()->second;
    EXPECT_EQ(result.renamed.begin()->first, "New");
    EXPECT_NE(theirNew, "New");

    // One row on a branch from the copy's row, its author the file.
    App::LogBranch branch;
    ASSERT_TRUE(store.findBranch(result.branch, branch));
    EXPECT_EQ(branch.fromSeq, atCopy);
    std::map<int64_t, App::LogSession> sessions;
    for (const auto& s : store.sessions())
        sessions[s.id] = s;
    std::map<int64_t, App::LogUser> users;
    for (const auto& u : store.users())
        users[u.id] = u;
    int rows = 0;
    for (const auto& t : store.chain(branch.head, atCopy + 1)) {
        if (store.ops(t.seq).empty())
            continue;
        ++rows;
        const App::LogUser& author = users[sessions[t.session].user];
        EXPECT_EQ(author.kind, "fork");
        EXPECT_NE(author.name.find("(txnlog-state-theirs)"), std::string::npos) << author.name;
        std::set<std::string> ops;
        for (const auto& o : store.ops(t.seq)) {
            ops.insert(o.op);
            // Which file it is, and what it carries of a log, is not what
            // the document is: none of that is in the row.
            EXPECT_NE(o.ckind, "doc") << o.op << " " << o.prop;
        }
        EXPECT_TRUE(ops.count("create"));
        EXPECT_TRUE(ops.count("remove"));
        EXPECT_TRUE(ops.count("set"));
    }
    EXPECT_EQ(rows, 1);

    // The merge is any branch's: what both did, with nothing in conflict.
    const auto merged = doc()->mergeBranch(result.branch);
    EXPECT_GT(merged.seq, 0);
    EXPECT_TRUE(merged.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 9);
    EXPECT_STREQ(featureOf(doc(), "Obj")->String.getValue(), "ours");
    ASSERT_TRUE(featureOf(doc(), "New"));
    EXPECT_EQ(featureOf(doc(), "New")->Integer.getValue(), 100);
    ASSERT_TRUE(featureOf(doc(), theirNew.c_str()));
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->Integer.getValue(), 7);
    EXPECT_EQ(featureOf(doc(), "Obj")->Link.getValue(), featureOf(doc(), theirNew.c_str()));
    EXPECT_FALSE(doc()->getObject("Gone"));
    EXPECT_TRUE(doc()->getPropertyByName("History"));
    EXPECT_TRUE(doc()->getPropertyByName("Branch"));

    // G5: the same file again is nothing, and is offered as nothing.
    const auto none = doc()->importFork(fork);
    EXPECT_EQ(none.rows, 0u);
    EXPECT_EQ(none.seq, 0);
    offered = doc()->forkBranches(fork);
    ASSERT_EQ(offered.size(), 1u);
    EXPECT_EQ(offered[0].ahead, 0u);

    // Changed again: one more row on the same branch, the object that came
    // still the one it was.
    elsewhere([&](App::Document* other) { featureOf(other, "New")->Integer.setValue(8); });
    const auto again = doc()->importFork(fork);
    EXPECT_TRUE(again.extended);
    EXPECT_EQ(again.branch, result.branch);
    EXPECT_EQ(again.rows, 1u) << again.reason;
    EXPECT_TRUE(again.renamed.empty());
    const auto more = doc()->mergeBranch(result.branch);
    EXPECT_TRUE(more.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->Integer.getValue(), 8);
    EXPECT_EQ(featureOf(doc(), "New")->Integer.getValue(), 100);
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 9);

    // A branch asked of a file that has none is refused.
    EXPECT_THROW(doc()->importFork(fork, "main"), Base::Exception);

    // Sec 30.22: a file that names no save of this history is not refused.
    // This one is the file's kin all the same -- a copy that lost its
    // `Version` where there is no log -- with a value changed and an object
    // added. It comes as an independent branch, from nothing; an object
    // whose id and name are one of this file's is that object.
    ASSERT_TRUE(doc()->saveCopy(strange.c_str(), false));
    {
        App::DocumentParams::setTransactionLog(0);
        App::Document* other = App::GetApplication().openDocument(strange.c_str());
        ASSERT_TRUE(other);
        const std::string name = other->getName();
        other->removeDynamicProperty("Version");
        other->removeDynamicProperty("History");
        other->removeDynamicProperty("Branch");
        featureOf(other, "Obj")->Integer.setValue(50);
        other->addObject("App::FeatureTest", "Extra");
        ASSERT_TRUE(other->save());
        App::GetApplication().closeDocument(name.c_str());
        App::DocumentParams::setTransactionLog(2);
        App::GetApplication().setActiveDocument(doc());
    }
    App::FileHistory::Saved lost;
    ASSERT_TRUE(App::FileHistory::savedAs(strange, lost));
    EXPECT_TRUE(lost.saveId.empty());
    offered = doc()->forkBranches(strange);
    ASSERT_EQ(offered.size(), 1u);
    EXPECT_TRUE(offered[0].independent);
    EXPECT_EQ(offered[0].base, 0);
    EXPECT_EQ(offered[0].ahead, 1u);
    const auto kin = doc()->importFork(strange);
    EXPECT_TRUE(kin.independent);
    EXPECT_EQ(kin.base, 0);
    EXPECT_EQ(kin.rows, 1u) << kin.reason;
    EXPECT_EQ(kin.stoppedAt, 0) << kin.reason;
    EXPECT_TRUE(kin.renamed.empty());
    App::LogBranch apart;
    ASSERT_TRUE(store.findBranch(kin.branch, apart));
    EXPECT_EQ(apart.fromSeq, 0);
    {
        // The whole of it in one row that follows no row, the objects this
        // file knows under the ids it knows them by.
        const auto chain = store.chain(apart.head);
        ASSERT_FALSE(chain.empty());
        EXPECT_EQ(chain.front().parent, 0);
        std::map<std::string, long> made;
        for (const auto& t : chain) {
            for (const auto& o : store.ops(t.seq)) {
                if (o.op == "create")
                    made[o.cname] = o.cid;
            }
        }
        EXPECT_EQ(made["Obj"], featureOf(doc(), "Obj")->getID());
        EXPECT_EQ(made["New"], featureOf(doc(), "New")->getID());
        EXPECT_EQ(made[theirNew], featureOf(doc(), theirNew.c_str())->getID());
        ASSERT_TRUE(made.count("Extra"));
        EXPECT_FALSE(doc()->getObjectByID(made["Extra"]));
    }
    // Merged with no base: what is the same in both is the same, what
    // differs is for a side to be picked, what only the file has comes, and
    // nothing here is removed for the file not having it.
    // (Here both started from nothing, this document having been made new:
    // the base is no row, and what each holds is in its own rows. A
    // document opened from a file has no such rows; the Python case
    // testAFileThatSharesNoHistoryIsMergedByWhatItHolds is that one.)
    const auto preview = doc()->previewMerge(kin.branch);
    EXPECT_EQ(preview.base, 0);
    EXPECT_FALSE(preview.fastForward);
    EXPECT_EQ(preview.conflicts, 1u);
    std::map<std::string, std::string> kinds;
    for (const auto& c : preview.changes)
        kinds[c.key] = c.kind;
    EXPECT_EQ(kinds["Obj.Integer"], "conflict");
    EXPECT_EQ(kinds["Obj.String"], "same");
    EXPECT_EQ(kinds["Extra"], "take");
    const auto refused = doc()->mergeBranch(kin.branch);
    EXPECT_EQ(refused.seq, 0);
    EXPECT_EQ(refused.unresolved.size(), 1u);
    const size_t objects = doc()->getObjects().size();
    const auto joined = doc()->mergeBranch(kin.branch, {{"Obj.Integer", "theirs"}});
    EXPECT_GT(joined.seq, 0);
    EXPECT_TRUE(joined.unresolved.empty());
    EXPECT_EQ(featureOf(doc(), "Obj")->Integer.getValue(), 50);
    EXPECT_STREQ(featureOf(doc(), "Obj")->String.getValue(), "ours");
    EXPECT_TRUE(doc()->getObject("Extra"));
    EXPECT_EQ(doc()->getObjects().size(), objects + 1);
    EXPECT_EQ(featureOf(doc(), theirNew.c_str())->Integer.getValue(), 8);
    // The same file again is nothing; and once merged, nothing to merge.
    EXPECT_EQ(doc()->importFork(strange).rows, 0u);
    const auto after = doc()->previewMerge(kin.branch);
    EXPECT_TRUE(after.changes.empty());

    for (const auto& file : {path, fork, strange})
        Base::FileInfo(file).deleteFile();
}
