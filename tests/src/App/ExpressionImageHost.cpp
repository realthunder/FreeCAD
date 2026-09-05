// Tests for the sandbox image host embedding (App::ExpressionSandbox::
// ImageHost).  The image is a cross-built artifact that may not exist
// on every box: tests locate it via the FCX_IMAGE and FCX_STDLIB
// environment variables, or -- with neither set -- through the host's
// own search, which ends at the <datadir>/Fcx bundle an install
// provides and a build tree mirrors.  They SKIP when it is nowhere, so
// the suite stays green without the wasm toolchain.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

#include <App/ExpressionImageHost.h>
#include <Base/FileInfo.h>

#include "InitApplication.h"

using json = nlohmann::json;
using App::ExpressionSandbox::ImageHost;
using App::ExpressionSandbox::ImageResult;

static std::string envOr(const char* name, const std::string& dflt)
{
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : dflt;
}

class ExpressionImageHostTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The host's own search reads preferences and the resource
        // directory, so it needs an Application -- the fixture used to
        // need none, because the environment answered everything.
        tests::initApplication();
        // In order of how much the caller said: FCX_IMAGE / FCX_STDLIB
        // (or FCX_REPO) name an image explicitly; with none of them set
        // the host resolves its own -- the preference, then the
        // <datadir>/Fcx bundle an install provides and the build tree
        // mirrors.  Letting the default answer means a plain ctest run
        // covers the PACKAGED lookup, not only the developer one.
        std::string repo = envOr("FCX_REPO", std::string());
        std::string image = envOr("FCX_IMAGE",
            repo.empty() ? std::string() : repo + "/build/wasi-image/fcx_image.wasm");
        std::string stdlib = envOr("FCX_STDLIB", std::string());
        if (!image.empty() && !stdlib.empty())
            ImageHost::instance().configure(image, stdlib);
        auto where = ImageHost::instance().location();
        if (!Base::FileInfo(where.image).isFile()
                || !Base::FileInfo(where.stdlib).isDir()) {
            GTEST_SKIP() << "sandbox guest for runtime '"
                         << ImageHost::instance().runtime()
                         << "' not available at " << where.image
                         << " (pyodide: set FCX_PYODIDE or build with "
                            "FREECAD_PYODIDE_DIR; wasi: set FCX_IMAGE and "
                            "FCX_STDLIB, or build with FREECAD_EXPR_IMAGE_DIR)";
        }
    }

    void TearDown() override
    {
        ImageHost::instance().reset();
    }

    static json value(const ImageResult& res)
    {
        return json::from_cbor(res.value.begin(), res.value.end());
    }
};

TEST_F(ExpressionImageHostTest, plainArithmetic)
{
    auto res = ImageHost::instance().eval("2 ** 10 + 0.5", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 1024.5);
}

TEST_F(ExpressionImageHostTest, boolStaysBool)
{
    auto res = ImageHost::instance().eval("1 == 1", {});
    ASSERT_TRUE(res.ok);
    json v = value(res);
    ASSERT_TRUE(v.is_boolean());
    EXPECT_TRUE(v.get<bool>());
}

TEST_F(ExpressionImageHostTest, typedBindingsRoundTrip)
{
    json bindings;
    bindings["a"] = {{"t", "vec"}, {"v", {1.0, 2.0, 3.0}}};
    bindings["b"] = {{"t", "vec"}, {"v", {0.0, 0.0, 1.0}}};
    auto cbor = json::to_cbor(bindings);
    auto res = ImageHost::instance().eval(
        "a.cross(b)", {cbor.begin(), cbor.end()});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    json v = value(res);
    EXPECT_EQ(v.value("t", ""), "vec");
    ASSERT_EQ(v["v"].size(), 3U);
    EXPECT_DOUBLE_EQ(v["v"][0].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(v["v"][1].get<double>(), -1.0);
    EXPECT_DOUBLE_EQ(v["v"][2].get<double>(), 0.0);
}

TEST_F(ExpressionImageHostTest, quantityArithmetic)
{
    json bindings;
    bindings["q"] = {{"t", "quantity"},
                     {"v", 10.0},
                     {"u", {1, 0, 0, 0, 0, 0, 0, 0}}};
    auto cbor = json::to_cbor(bindings);
    auto res = ImageHost::instance().eval(
        "q * 2 + Units.Quantity('5 mm')", {cbor.begin(), cbor.end()});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    json v = value(res);
    EXPECT_EQ(v.value("t", ""), "quantity");
    EXPECT_DOUBLE_EQ(v["v"].get<double>(), 25.0);
    EXPECT_EQ(v["u"][0].get<int>(), 1);
}

TEST_F(ExpressionImageHostTest, pythonErrorCrosses)
{
    auto res = ImageHost::instance().eval("1 / 0", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "ZeroDivisionError");
}

TEST_F(ExpressionImageHostTest, nonMarshalableResultRefused)
{
    auto res = ImageHost::instance().eval("lambda x: x", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "MarshalError");
}

TEST_F(ExpressionImageHostTest, hostFsUnreachable)
{
    auto res = ImageHost::instance().eval("open('/etc/passwd').read()", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "FileNotFoundError");
}

TEST_F(ExpressionImageHostTest, importOsCannotSpawn)
{
    auto res = ImageHost::instance().eval(
        "__import__('subprocess').run(['ls'])", {});
    ASSERT_FALSE(res.ok);
    // WASI has no process spawn; the exact errno text is libc's business
    EXPECT_NE(res.excType, "");
}

TEST_F(ExpressionImageHostTest, evalAfterErrorStillWorks)
{
    auto bad = ImageHost::instance().eval("1 / 0", {});
    ASSERT_FALSE(bad.ok);
    auto good = ImageHost::instance().eval("40 + 2", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 42);
}

// ---- the time budget (Outcome in App/ExpressionImageRuntime.h): a
// ---- runaway guest is stopped, the host answers TimeoutError, and the
// ---- next evaluation works ----

#include <App/Application.h>
#include <Base/Parameter.h>

class ExpressionImageBudgetTest: public ExpressionImageHostTest
{
protected:
    static ParameterGrp::handle prefs()
    {
        return App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
    }

    void SetUp() override
    {
        ExpressionImageHostTest::SetUp();  // may GTEST_SKIP
        hadBudget = prefs()->GetInt("BudgetMs", -1);
        hadGrace = prefs()->GetInt("GraceMs", -1);
        prefs()->SetInt("BudgetMs", 300);
        prefs()->SetInt("GraceMs", 700);
    }

    void TearDown() override
    {
        if (hadBudget < 0)
            prefs()->RemoveInt("BudgetMs");
        else
            prefs()->SetInt("BudgetMs", hadBudget);
        if (hadGrace < 0)
            prefs()->RemoveInt("GraceMs");
        else
            prefs()->SetInt("GraceMs", hadGrace);
        ExpressionImageHostTest::TearDown();
    }

    /// Leave a mark in the guest interpreter, so a later test can tell
    /// the SAME instance from a fresh one.
    static void mark()
    {
        auto res = ImageHost::instance().eval(
            "setattr(__import__('sys'), 'fcx_budget_mark', 42)", {});
        ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    }
    static ImageResult readMark()
    {
        return ImageHost::instance().eval("__import__('sys').fcx_budget_mark", {});
    }

    long hadBudget = -1;
    long hadGrace = -1;
};

// A loop in BYTECODE: the soft stage reaches it through the interpreter's
// own signal check, so pyodide keeps its instance (the interrupt is an
// ordinary exception there); the wasi runtime has no soft stage and
// takes the hard one, which drops the instance.
TEST_F(ExpressionImageBudgetTest, runawayBytecodeLoopIsStopped)
{
    mark();
    auto res = ImageHost::instance().eval("next(x for x in iter(int, 1) if x)", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "TimeoutError") << res.message;
    EXPECT_NE(res.message.find("300 ms"), std::string::npos) << res.message;

    auto good = ImageHost::instance().eval("40 + 2", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 42);

    auto after = readMark();
    if (ImageHost::instance().runtime() == "pyodide") {
        ASSERT_TRUE(after.ok) << "pyodide dropped its instance on a soft interrupt: "
                              << after.excType << ": " << after.message;
        EXPECT_EQ(value(after).get<int64_t>(), 42);
    }
    else {
        EXPECT_FALSE(after.ok) << "a terminated instance was kept";
    }
}

// A loop in NATIVE code never reaches a bytecode check: only the hard
// stage can stop it, on both runtimes, and the instance is dropped.
TEST_F(ExpressionImageBudgetTest, runawayNativeLoopIsTerminated)
{
    mark();
    auto res = ImageHost::instance().eval("sum(iter(int, 1))", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "TimeoutError") << res.message;
    EXPECT_NE(res.message.find("terminated"), std::string::npos) << res.message;

    auto good = ImageHost::instance().eval("40 + 2", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 42);
    EXPECT_FALSE(readMark().ok) << "a terminated instance was kept";
}

// After a soft interrupt the surviving instance must still be a whole
// interpreter: typed values, an error, and a value again.
TEST_F(ExpressionImageBudgetTest, instanceIsWholeAfterInterrupt)
{
    auto res = ImageHost::instance().eval("next(x for x in iter(int, 1) if x)", {});
    ASSERT_FALSE(res.ok);
    ASSERT_EQ(res.excType, "TimeoutError") << res.message;

    json bindings;
    bindings["q"] = {{"t", "quantity"}, {"v", 10.0}, {"u", {1, 0, 0, 0, 0, 0, 0, 0}}};
    auto cbor = json::to_cbor(bindings);
    auto q = ImageHost::instance().eval("q * 2 + Units.Quantity('5 mm')",
                                        {cbor.begin(), cbor.end()});
    ASSERT_TRUE(q.ok) << q.excType << ": " << q.message;
    EXPECT_DOUBLE_EQ(value(q)["v"].get<double>(), 25.0);
    auto bad = ImageHost::instance().eval("1 / 0", {});
    ASSERT_FALSE(bad.ok);
    EXPECT_EQ(bad.excType, "ZeroDivisionError");
    auto good = ImageHost::instance().eval("sum(range(10))", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 45);
}

// BudgetMs = 0 is "unbounded": nothing arms, and (on wasi) the epoch
// deadline must not trap a plain evaluation.
TEST_F(ExpressionImageBudgetTest, zeroBudgetIsUnbounded)
{
    prefs()->SetInt("BudgetMs", 0);
    auto res = ImageHost::instance().eval("sum(range(100000))", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<int64_t>(), 4999950000);
}

// ---- image->host bridge ops (get_attr/call/get_item/len/release,
// ---- ExpressionImageBridge.cpp): live host objects cross as handles ----

#include <Python.h>

#include <App/ExpressionImageBridge.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Interpreter.h>

class ExpressionImageBridgeTest: public ExpressionImageHostTest
{
protected:
    void SetUp() override
    {
        tests::initApplication();
        // never read or write the real user grants.json from tests
        static bool redirected;
        if (!redirected) {
            redirected = true;
            App::ExpressionSecurity::Runtime::instance().setPolicyFile(
                std::string(std::tmpnam(nullptr)) + "-imgbridge-policy.json");
        }
        ExpressionImageHostTest::SetUp();  // may GTEST_SKIP
    }

    void TearDown() override
    {
        if (!IsSkipped())
            ImageHost::instance().clearHandles();
        ExpressionImageHostTest::TearDown();
    }

    /// Run python source, export ns[name] into the handle table.
    static uint64_t exportFromSource(const char* source, const char* name)
    {
        Base::PyGILStateLocker lock;
        PyObject* ns = PyDict_New();
        PyDict_SetItemString(ns, "__builtins__", PyEval_GetBuiltins());
        PyObject* r = PyRun_String(source, Py_file_input, ns, ns);
        if (!r)
            PyErr_Print();
        EXPECT_NE(r, nullptr) << "test object source failed";
        Py_XDECREF(r);
        PyObject* obj = PyDict_GetItemString(ns, name);  // borrowed
        EXPECT_NE(obj, nullptr);
        uint64_t id = obj ? ImageHost::instance().exportObject(obj) : 0;
        Py_DECREF(ns);
        return id;
    }

    static std::vector<unsigned char> handleBinding(const char* var,
                                                    uint64_t id)
    {
        json b;
        b[var] = {{"t", "h"}, {"id", id}, {"ty", "object"}};
        auto v = json::to_cbor(b);
        return {v.begin(), v.end()};
    }
};

TEST_F(ExpressionImageBridgeTest, undeclaredAttrDoesNotGetattr)
{
    // The closed facade table (docs/ExpressionSandbox.md sec 7.5): an
    // arbitrary object's attribute is NOT reachable.  The in-image
    // proxy falls through to read_prop, which the host answers from
    // the C++ property system only -- a plain Python object has none.
    uint64_t id = exportFromSource("class T:\n"
                                   "    answer = 42\n"
                                   "o = T()\n", "o");
    auto res = ImageHost::instance().eval("o.answer", handleBinding("o", id));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "AttributeError");
}

TEST_F(ExpressionImageBridgeTest, undeclaredMethodNotCallable)
{
    uint64_t id = exportFromSource("class T:\n"
                                   "    def add(self, a, b):\n"
                                   "        return a + b\n"
                                   "o = T()\n", "o");
    auto res = ImageHost::instance().eval("o.add(2, 3)",
                                          handleBinding("o", id));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "AttributeError");
}

TEST_F(ExpressionImageBridgeTest, forgedGetAttrIsProtocolError)
{
    // Even a hand-rolled op (a compromised image) cannot reach an
    // undeclared member: the host answers ProtocolError, never getattr.
    App::ExpressionSandbox::HandleTable table;
    uint64_t id = 0;
    {
        Base::PyGILStateLocker lock;
        PyObject* d = PyDict_New();
        id = table.add(d);
        Py_DECREF(d);
    }
    json req;
    req["op"] = "get_attr";
    req["h"] = id;
    req["a"] = "keys";
    json reply = App::ExpressionSandbox::dispatchHostOp(table, req);
    EXPECT_FALSE(reply.value("ok", false));
    EXPECT_EQ(reply.value("exc", ""), "ProtocolError");

    // the pre-facade call form (callable handle, no member) is gone
    json callReq;
    callReq["op"] = "call";
    callReq["h"] = id;
    callReq["a"] = json::array();
    json callReply = App::ExpressionSandbox::dispatchHostOp(table, callReq);
    EXPECT_FALSE(callReply.value("ok", false));
    EXPECT_EQ(callReply.value("exc", ""), "ProtocolError");
    {
        Base::PyGILStateLocker lock;
        table.clear();
    }
}

TEST_F(ExpressionImageBridgeTest, getItemAndLen)
{
    uint64_t id = exportFromSource("o = {'items': [1, 2, 3]}\n", "o");
    auto res = ImageHost::instance().eval("sum(o['items']) + len(o)",
                                          handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<int64_t>(), 7);
}

TEST_F(ExpressionImageBridgeTest, proxyDropReleasesHandle)
{
    uint64_t id = exportFromSource("o = {'a': 1}\n", "o");
    EXPECT_EQ(ImageHost::instance().handleCount(), 1u);
    auto res = ImageHost::instance().eval("o['a']", handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    // the eval globals died with the call and the proxy's __del__
    // queued a release that rode the reply -- but releases are DEFERRED
    // for the length of the transaction, so the reply stays decodable.
    // The next transaction applies them.
    EXPECT_EQ(ImageHost::instance().handleCount(), 1u);
    auto res2 = ImageHost::instance().eval("1", {});
    ASSERT_TRUE(res2.ok);
    EXPECT_EQ(ImageHost::instance().handleCount(), 0u);
}

TEST_F(ExpressionImageBridgeTest, resultHoldingAHostObjectDecodes)
{
    // The spreadsheet binding idiom `tuple(.cells, <<B4>>, <<ZZ4>>)`
    // returns a tuple whose first element IS a host object.  The image
    // destroys its proxy as the evaluation unwinds, before the host
    // decodes the reply, so without deferred releases this came back as
    // a stale handle and the evaluation failed -- found by the corpus
    // gate, in real files.
    Base::PyGILStateLocker lock;
    uint64_t id = exportFromSource("o = {'a': 1}\n", "o");
    auto res = ImageHost::instance().eval("(o, 'B4')", handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    PyObject* v = ImageHost::instance().decodeResult(res);
    ASSERT_NE(v, nullptr) << "stale handle: the reply outlived its entry";
    ASSERT_TRUE(PyTuple_Check(v));
    ASSERT_EQ(PyTuple_GET_SIZE(v), 2);
    EXPECT_TRUE(PyDict_Check(PyTuple_GET_ITEM(v, 0)));
    Py_DECREF(v);
}

TEST_F(ExpressionImageBridgeTest, staleHandleRaises)
{
    uint64_t id = exportFromSource("o = {'a': 1}\n", "o");
    ImageHost::instance().clearHandles();
    auto res = ImageHost::instance().eval("o['a']", handleBinding("o", id));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "ReferenceError");
}

TEST_F(ExpressionImageBridgeTest, permissionGateDeniesThenGrantWorks)
{
    using App::ExpressionSecurity::Permission;
    using App::ExpressionSecurity::Runtime;
    // A document principal: the v1 catalog DENIES unsafe.getattr for
    // documents (addons default to allow across the board, so an addon
    // principal would not exercise the gate at all).  With the closed
    // facade table, the remaining unsafe-gated bridge surface is
    // get_item/len on a non-builtin container: __getitem__ there is
    // arbitrary host code.
    const std::string principal =
        "document:sha256:" + std::string(64, 'b');

    uint64_t id = exportFromSource("class T:\n"
                                   "    def __getitem__(self, k):\n"
                                   "        return 7\n"
                                   "o = T()\n", "o");
    {
        Runtime::Scope scope(principal.c_str());
        auto res = ImageHost::instance().eval("o[0]",
                                              handleBinding("o", id));
        ASSERT_FALSE(res.ok);
        EXPECT_EQ(res.excType, "PermissionError");
    }
    Runtime::instance().grant(principal, Permission::UnsafeGetattr, "*",
                              true, "session");
    uint64_t id2 = exportFromSource("class T:\n"
                                    "    def __getitem__(self, k):\n"
                                    "        return 7\n"
                                    "o = T()\n", "o");
    {
        Runtime::Scope scope(principal.c_str());
        auto res = ImageHost::instance().eval("o[0]",
                                              handleBinding("o", id2));
        ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
        EXPECT_EQ(value(res).get<int64_t>(), 7);
    }
    Runtime::instance().clearPending(principal, Permission::UnsafeGetattr,
                                     "*");
}

// ---- the ExpressionCore carve (docs/ExpressionImage.md): the real
// ---- parser + AST walker run IN the image; identifiers resolve from
// ---- the bindings pack evalExpression() pre-packs host-side ----

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyStandard.h>

class ExpressionImageEvalTest: public ExpressionImageBridgeTest
{
protected:
    void SetUp() override
    {
        ExpressionImageBridgeTest::SetUp();
        if (IsSkipped())
            return;
        doc = App::GetApplication().newDocument("FcxEvalTest", "testUser");
        obj = doc->addObject("App::FeaturePython", "Obj");
        auto width = Base::freecad_dynamic_cast<App::PropertyFloat>(
            obj->addDynamicProperty("App::PropertyFloat", "Width"));
        ASSERT_NE(width, nullptr);
        width->setValue(21.0);
    }

    void TearDown() override
    {
        if (doc)
            App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
        obj = nullptr;
        ExpressionImageBridgeTest::TearDown();
    }

    App::Document* doc = nullptr;
    App::DocumentObject* obj = nullptr;

    /// Export a live object into the image host's handle table and
    /// build a {"var": handle} binding the way encodeHostValue would
    /// (type tag + facade key), for driving the raw eval path.
    static std::vector<unsigned char> objectBinding(const char* var,
                                                    App::PropertyContainer* o,
                                                    const char* boundMember = nullptr)
    {
        Base::PyGILStateLocker lock;
        PyObject* py = o->getPyObject();
        uint64_t id = ImageHost::instance().exportObject(py);
        json h = {{"t", "h"}, {"id", id}, {"ty", Py_TYPE(py)->tp_name}};
        if (const char* fc = App::ExpressionSandbox::facadeKeyFor(Py_TYPE(py)))
            h["fc"] = fc;
        if (boundMember)
            h["m"] = boundMember;
        Py_DECREF(py);
        json b;
        b[var] = std::move(h);
        auto v = json::to_cbor(b);
        return {v.begin(), v.end()};
    }
};

TEST_F(ExpressionImageEvalTest, bridgeCountersPerOp)
{
    // stats(): evaluations, handles minted, and every guest->host op by
    // wire name -- the instrument that prices a workload's crossing.
    auto& host = ImageHost::instance();
    host.resetStats();
    auto zero = host.stats();
    EXPECT_EQ(zero.evals, 0u);
    EXPECT_EQ(zero.handles, 0u);
    EXPECT_TRUE(zero.ops.empty());

    // one binding = one handle minted; o.Width = one read_prop; the
    // proxy's __del__ at the end of the eval queues a release that
    // rides the reply -- never a hop of its own
    auto res = host.eval("o.Width", objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    auto one = host.stats();
    EXPECT_EQ(one.evals, 1u);
    EXPECT_EQ(one.handles, 1u);
    EXPECT_EQ(one.ops["read_prop"], 1u);
    EXPECT_EQ(one.ops.count("release"), 0u) << "a release crossed as an op";

    // no caching in the proxy: three more reads are three more hops
    res = host.eval("o.Width + o.Width + o.Width", objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    auto more = host.stats();
    EXPECT_EQ(more.evals, 2u);
    // one id per object: the pack finds the entry the previous
    // evaluation's deferred release has not yet dropped, so the same
    // object exported again mints nothing (G1d, ArchComponent's
    // `obj in o.Hosts`)
    EXPECT_EQ(more.handles, 1u);
    EXPECT_EQ(more.ops["read_prop"], 4u);

    // a bare literal crosses nothing but the evaluation itself
    res = host.eval("1", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    auto lit = host.stats();
    EXPECT_EQ(lit.evals, 3u);
    EXPECT_EQ(lit.handles, 1u);
    EXPECT_EQ(lit.ops["read_prop"], 4u);

    host.resetStats();
    EXPECT_EQ(host.stats().evals, 0u);
    EXPECT_TRUE(host.stats().ops.empty());
    host.clearHandles();
}

TEST_F(ExpressionImageEvalTest, writePropSameDocument)
{
    // write_prop and the write-family calls (addProperty,
    // removeProperty, setPropertyStatus, the Document's addObject and
    // removeObject): the evaluation owner's DOCUMENT -- the owner, any
    // object of its document, the document itself -- under
    // doc.write.self, "self" being the same-origin document (user
    // ruling 2026-09-05; owner-only was rung 2's scoping).  An object
    // of another document is refused.  A handle in the value
    // dereferences to the live object.
    auto& host = ImageHost::instance();
    auto other = doc->addObject("App::FeaturePython", "Other");
    auto otherWidth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        other->addDynamicProperty("App::PropertyFloat", "Width"));
    ASSERT_NE(otherWidth, nullptr);
    otherWidth->setValue(1.0);
    auto width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    auto doc2 = App::GetApplication().newDocument("FcxWriteForeign", "testUser");
    struct CloseDoc
    {
        App::Document* d;
        ~CloseDoc()
        {
            App::GetApplication().closeDocument(d->getName());
        }
    } closeDoc {doc2};
    auto foreign = doc2->addObject("App::FeaturePython", "Foreign");
    auto foreignWidth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        foreign->addDynamicProperty("App::PropertyFloat", "Width"));
    ASSERT_NE(foreignWidth, nullptr);
    foreignWidth->setValue(2.0);

    // three objects in one pack: o is the owner, p its sibling, q
    // belongs to another document
    auto pack = [&]() {
        auto a = objectBinding("o", obj);
        auto b = objectBinding("p", other);
        auto c = objectBinding("q", foreign);
        json m = json::from_cbor(a.begin(), a.end());
        m.update(json::from_cbor(b.begin(), b.end()));
        m.update(json::from_cbor(c.begin(), c.end()));
        auto v = json::to_cbor(m);
        return std::vector<unsigned char>(v.begin(), v.end());
    };

    // no owner named: nothing may be written
    auto r = host.eval("setattr(o, 'Width', 5.0)", pack());
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "PermissionError") << r.message;
    EXPECT_DOUBLE_EQ(width->getValue(), 21.0);

    // the owner writes itself: the proxy's __setattr__, visible on the
    // host at once, and counted
    host.resetStats();
    r = host.eval("setattr(o, 'Width', 5.0) or o.Width", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(value(r).get<double>(), 5.0);
    EXPECT_DOUBLE_EQ(width->getValue(), 5.0);
    EXPECT_EQ(host.stats().ops["write_prop"], 1u);

    // the owner writing a SIBLING (same document): allowed, the
    // same-origin write (ArchStairs sets its railings' Base)
    r = host.eval("setattr(p, 'Width', 7.0) or p.Width", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(value(r).get<double>(), 7.0);
    EXPECT_DOUBLE_EQ(otherWidth->getValue(), 7.0);

    // the owner writing an object of ANOTHER document: refused, untouched
    r = host.eval("setattr(q, 'Width', 9.0)", pack(), obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "PermissionError") << r.message;
    EXPECT_DOUBLE_EQ(foreignWidth->getValue(), 2.0);

    // the Document itself: addObject/removeObject on the owner's
    // document ride the call op behind the same gate; another
    // document's are refused
    r = host.eval("o.Document.addObject('App::FeaturePython', 'Made').Name", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_EQ(value(r).get<std::string>(), "Made");
    ASSERT_NE(doc->getObject("Made"), nullptr);
    r = host.eval("o.Document.removeObject('Made')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_EQ(doc->getObject("Made"), nullptr);
    r = host.eval("q.Document.addObject('App::FeaturePython', 'Smuggled')", pack(), obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "PermissionError") << r.message;
    EXPECT_EQ(doc2->getObject("Smuggled"), nullptr);
    r = host.eval("q.Document.removeObject('Foreign')", pack(), obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "PermissionError") << r.message;
    EXPECT_EQ(doc2->getObject("Foreign"), foreign);

    // a property that does not exist, a read-only one
    r = host.eval("setattr(o, 'Nope', 1.0)", pack(), obj);
    EXPECT_EQ(r.excType, "AttributeError") << r.message;
    r = host.eval("setattr(o, 'ExpressionEngine', 1.0)", pack(), obj);
    EXPECT_FALSE(r.ok) << "ExpressionEngine took a float";

    // the write family rides the declared call op with the same gate
    r = host.eval("o.addProperty('App::PropertyFloat', 'Depth')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    ASSERT_NE(obj->getPropertyByName("Depth"), nullptr);
    r = host.eval("setattr(o, 'Depth', 3.0) or o.Depth", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(value(r).get<double>(), 3.0);
    r = host.eval("p.addProperty('App::PropertyFloat', 'Depth')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_NE(other->getPropertyByName("Depth"), nullptr);
    r = host.eval("q.addProperty('App::PropertyFloat', 'Depth')", pack(), obj);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "PermissionError") << r.message;
    EXPECT_EQ(foreign->getPropertyByName("Depth"), nullptr);
    r = host.eval("o.setPropertyStatus('Depth', 'ReadOnly')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_TRUE(obj->getPropertyByName("Depth")->testStatus(App::Property::ReadOnly));
    // ReadOnly is the editor's status: native setattr writes through
    // it, and so does write_prop; Immutable is the one refusal.
    r = host.eval("setattr(o, 'Depth', 4.0) or o.Depth", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(value(r).get<double>(), 4.0);
    r = host.eval("o.setPropertyStatus('Depth', 'Immutable')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.eval("setattr(o, 'Depth', 5.0)", pack(), obj);
    EXPECT_FALSE(r.ok) << "an Immutable property took a write";
    EXPECT_EQ(r.excType, "AttributeError") << r.message;
    r = host.eval("o.Depth", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(value(r).get<double>(), 4.0);
    r = host.eval("o.setPropertyStatus('Depth', '-Immutable')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.eval("o.removeProperty('Depth')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_EQ(obj->getPropertyByName("Depth"), nullptr);

    // a handle in the value: the live object, not its wire face
    r = host.eval("o.addProperty('App::PropertyLink', 'Ref')", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.eval("setattr(o, 'Ref', p) or o.Ref.Name", pack(), obj);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_EQ(value(r).get<std::string>(), "Other");
    auto link = Base::freecad_dynamic_cast<App::PropertyLink>(obj->getPropertyByName("Ref"));
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->getValue(), other);

    host.clearHandles();
}

namespace
{

/// Any host object as a {"var": handle} binding (objectBinding for
/// PropertyContainers; this one for shapes and geometry).  Steals the
/// reference.
std::vector<unsigned char> pyBinding(const char* var, PyObject* py)
{
    Base::PyGILStateLocker lock;
    uint64_t id = ImageHost::instance().exportObject(py);
    json h = {{"t", "h"}, {"id", id}, {"ty", Py_TYPE(py)->tp_name}};
    if (const char* fc = App::ExpressionSandbox::facadeKeyFor(Py_TYPE(py)))
        h["fc"] = fc;
    Py_DECREF(py);
    json b;
    b[var] = std::move(h);
    auto v = json::to_cbor(b);
    return {v.begin(), v.end()};
}

}  // namespace

TEST_F(ExpressionImageEvalTest, partSurfaceOnHandles)
{
    // G1 step 5: the annotated Part surface -- TopoShape and its
    // sub-shapes, curves and surfaces -- reached from the guest on
    // handles: handle-tier attributes give handles, value-tier ones
    // cross by value, declared calls run on the host with handle
    // arguments dereferenced, and each answer equals the host's own.
    PyObject* box = nullptr;
    PyObject* other = nullptr;
    {
        Base::PyGILStateLocker lock;
        PyObject* part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            GTEST_SKIP() << "the Part module is not importable in this test binary";
        }
        box = PyObject_CallMethod(part, "makeBox", "ddd", 2.0, 3.0, 4.0);
        other = PyObject_CallMethod(part, "makeBox", "ddd", 1.0, 1.0, 1.0);
        Py_DECREF(part);
        ASSERT_NE(box, nullptr);
        ASSERT_NE(other, nullptr);
    }
    auto& host = ImageHost::instance();
    auto pack = [&]() {
        Py_INCREF(box);
        Py_INCREF(other);
        auto a = pyBinding("s", box);
        auto b = pyBinding("t", other);
        json m = json::from_cbor(a.begin(), a.end());
        m.update(json::from_cbor(b.begin(), b.end()));
        auto v = json::to_cbor(m);
        return std::vector<unsigned char>(v.begin(), v.end());
    };

    // each expression reduced to something the wire carries by value,
    // then compared against the host's repr of the same expression
    const char* cases[] = {
        "s.ShapeType",
        "s.Volume",
        "s.isValid()",
        "s.isClosed()",
        "len(s.Edges)",
        "len(s.Faces)",
        "len(s.Vertexes)",
        "s.Edges[0].Length",
        "s.Edges[0].ShapeType",
        "s.Edges[0].Curve.Direction.z",
        "s.Edges[0].Curve.value(1.0).x",
        "s.Edges[0].Curve.TypeId",
        "s.Edges[0].valueAt(0.5).z",
        "s.Edges[0].firstVertex().Point.y",
        "s.Faces[0].Surface.Axis.z",
        "s.Faces[0].OuterWire.Length",
        "s.Faces[0].normalAt(0.5, 0.5).x",
        "s.Faces[0].CenterOfMass.y",
        "s.Vertexes[0].Point.z",
        "s.BoundBox.ZMax",
        "s.copy().Volume",
        "s.common(t).Volume",
        "s.cut(t).Volume",
        "s.fuse(t).Volume",
        "s.hashCode() == s.hashCode()",
        "s.isDerivedFrom('Part::TopoShape')",
        "s.TypeId",
        "s.section(t).ShapeType",
        "s.Solids[0].Mass",
        "s.Edges[0].Curve.length()",
        "s.Edges[0].Curve.discretize(3)[1].x",
    };
    host.resetStats();
    for (const char* expr : cases) {
        auto g = host.eval(expr, pack());
        ASSERT_TRUE(g.ok) << expr << ": " << g.excType << ": " << g.message;
        std::string native;
        {
            Base::PyGILStateLocker lock;
            PyObject* g2 = PyDict_New();
            PyDict_SetItemString(g2, "__builtins__", PyEval_GetBuiltins());
            PyDict_SetItemString(g2, "s", box);
            PyDict_SetItemString(g2, "t", other);
            PyObject* r = PyRun_String(expr, Py_eval_input, g2, g2);
            Py_DECREF(g2);
            ASSERT_NE(r, nullptr) << expr;
            PyObject* rep = PyObject_Repr(r);
            Py_DECREF(r);
            native = PyUnicode_AsUTF8(rep);
            Py_DECREF(rep);
        }
        // the guest's answer, as the host would repr it
        std::string guest;
        {
            Base::PyGILStateLocker lock;
            PyObject* v = host.decodeResult(g);
            ASSERT_NE(v, nullptr) << expr;
            PyObject* rep = PyObject_Repr(v);
            Py_DECREF(v);
            guest = PyUnicode_AsUTF8(rep);
            Py_DECREF(rep);
        }
        EXPECT_EQ(guest, native) << expr;
        host.clearHandles();
    }
    auto st = host.stats();
    EXPECT_GT(st.ops["get_attr"], 0u);
    EXPECT_GT(st.ops["call"], 0u);

    // an undeclared member of a sub-shape is unreachable; a declared
    // one the object lacks fails as it does natively
    auto r = host.eval("s.Edges[0].tolerance", pack());
    EXPECT_FALSE(r.ok);
    r = host.eval("s.Edges[0].Surface", pack());
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "AttributeError") << r.message;
    host.clearHandles();
    {
        Base::PyGILStateLocker lock;
        Py_DECREF(box);
        Py_DECREF(other);
    }
}

TEST_F(ExpressionImageEvalTest, partModuleFacade)
{
    // G1 step 5b: the Part module facade -- the 32 names Draft's App
    // side uses, as a guest `Part` module whose callables run on the
    // host (mod_call) and hand back handles, whose constant reads once
    // (mod_get), whose OCCError is a guest class the bridge raises when
    // the host reply names it.  Each answer equals the host's own.
    PyObject* box = nullptr;
    PyObject* other = nullptr;
    PyObject* fc = nullptr;
    {
        Base::PyGILStateLocker lock;
        PyObject* part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            GTEST_SKIP() << "the Part module is not importable in this test binary";
        }
        box = PyObject_CallMethod(part, "makeBox", "ddd", 2.0, 3.0, 4.0);
        other = PyObject_CallMethod(part, "makeBox", "ddd", 1.0, 1.0, 1.0);
        Py_DECREF(part);
        fc = PyImport_ImportModule("FreeCAD");
        ASSERT_NE(box, nullptr);
        ASSERT_NE(other, nullptr);
        ASSERT_NE(fc, nullptr);
    }
    auto& host = ImageHost::instance();
    auto pack = [&]() {
        Py_INCREF(box);
        Py_INCREF(other);
        auto a = pyBinding("s", box);
        auto b = pyBinding("t", other);
        json m = json::from_cbor(a.begin(), a.end());
        m.update(json::from_cbor(b.begin(), b.end()));
        auto v = json::to_cbor(m);
        return std::vector<unsigned char>(v.begin(), v.end());
    };
    // the same text runs on both sides: __import__ reaches the guest's
    // facade module there and the real module here
    auto native = [&](const char* expr, std::string& out) {
        Base::PyGILStateLocker lock;
        PyObject* g = PyDict_New();
        PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
        PyDict_SetItemString(g, "FreeCAD", fc);
        PyDict_SetItemString(g, "s", box);
        PyDict_SetItemString(g, "t", other);
        PyObject* r = PyRun_String(expr, Py_eval_input, g, g);
        Py_DECREF(g);
        if (!r) {
            PyErr_Print();
            return false;
        }
        PyObject* rep = PyObject_Repr(r);
        Py_DECREF(r);
        out = PyUnicode_AsUTF8(rep);
        Py_DECREF(rep);
        return true;
    };
    const char* cases[] = {
        "__import__('Part').LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(3, 4, 0)).length()",
        "__import__('Part').LineSegment(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(3, 4, 0)).EndPoint.y",
        "__import__('Part').makeCircle(2.0).Length",
        "__import__('Part').makeCircle(2.0).Curve.Radius",
        "__import__('Part').makePolygon([FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(1, 0, 0),"
        " FreeCAD.Vector(1, 1, 0), FreeCAD.Vector(0, 0, 0)]).isClosed()",
        "__import__('Part').Face(__import__('Part').makePolygon([FreeCAD.Vector(0, 0, 0),"
        " FreeCAD.Vector(1, 0, 0), FreeCAD.Vector(1, 1, 0), FreeCAD.Vector(0, 0, 0)])).Area",
        "__import__('Part').makeCompound([s, t]).Volume",
        "__import__('Part').Vertex(FreeCAD.Vector(1, 2, 3)).Point.y",
        "__import__('Part').Circle(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(0, 0, 1), 2.0).Radius",
        "__import__('Part').Plane().Axis.z",
        "__import__('Part').Point(FreeCAD.Vector(1, 2, 3)).toShape().Point.z",
        "len(__import__('Part').sortEdges(s.Edges))",
        "__import__('Part').OCC_VERSION",
        "__import__('Part').OCC_VERSION",
    };
    host.resetStats();
    for (const char* expr : cases) {
        auto g = host.eval(expr, pack());
        ASSERT_TRUE(g.ok) << expr << ": " << g.excType << ": " << g.message;
        std::string want;
        ASSERT_TRUE(native(expr, want)) << expr;
        std::string got;
        {
            Base::PyGILStateLocker lock;
            PyObject* v = host.decodeResult(g);
            ASSERT_NE(v, nullptr) << expr;
            PyObject* rep = PyObject_Repr(v);
            Py_DECREF(v);
            got = PyUnicode_AsUTF8(rep);
            Py_DECREF(rep);
        }
        EXPECT_EQ(got, want) << expr;
        host.clearHandles();
    }
    auto st = host.stats();
    EXPECT_GT(st.ops["mod_call"], 0u);
    EXPECT_EQ(st.ops["mod_get"], 1u) << "the constant is read once, then cached";

    // an undeclared name does not exist in the guest's module
    auto r = host.eval("__import__('Part').makeSphere(1.0)", pack());
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.excType, "AttributeError") << r.message;

    // the host's Part.OCCError arrives as the guest's Part.OCCError:
    // the same script classifies the failure identically on both sides
    const char* script =
        "import Part, FreeCAD\n"
        "V = FreeCAD.Vector\n"
        "try:\n"
        "    Part.Face(Part.makePolygon([V(0, 0, 0), V(1, 0, 0)]))\n"
        "    R = 'no error'\n"
        "except Part.OCCError as e:\n"
        "    R = 'OCCError'\n"
        "except Exception as e:\n"
        "    R = type(e).__name__\n";
    r = host.exec(script, "fcxexc");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.eval("__import__('fcxexc').R", {});
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    std::string nativeClass;
    {
        Base::PyGILStateLocker lock;
        PyObject* g = PyDict_New();
        PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
        PyObject* ran = PyRun_String(script, Py_file_input, g, g);
        ASSERT_NE(ran, nullptr);
        Py_DECREF(ran);
        PyObject* R = PyDict_GetItemString(g, "R");
        nativeClass = PyUnicode_AsUTF8(R);
        Py_DECREF(g);
    }
    EXPECT_EQ(value(r).get<std::string>(), nativeClass);
    EXPECT_EQ(nativeClass, "OCCError") << "the fixture no longer raises OCCError natively";

    host.clearHandles();
    {
        Base::PyGILStateLocker lock;
        Py_DECREF(box);
        Py_DECREF(other);
        Py_DECREF(fc);
    }
}

// ---- the G1 step-3 gate (docs/Sandbox.md sec 7.6): Draft's vector
// ---- algebra runs in the guest on the in-image value classes with no
// ---- bridge hop, and agrees with the same source run on the host.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{

/// A Draft workbench source file: the build/install tree's Mod/Draft
/// (Application::getHomePath), else FCX_REPO/src/Mod/Draft; empty when
/// neither has it.
std::string readDraftFile(const char* name)
{
    std::vector<std::string> candidates = {
        App::Application::getHomePath() + "Mod/Draft/" + name,
    };
    std::string repo = envOr("FCX_REPO", std::string());
    if (!repo.empty())
        candidates.push_back(repo + "/src/Mod/Draft/" + name);
    for (const auto& p : candidates) {
        std::ifstream in(p, std::ios::binary);
        if (in)
            return std::string(std::istreambuf_iterator<char>(in), {});
    }
    return {};
}

/// Stand-ins for what DraftVecUtils imports around itself: the
/// parameter store (draftutils.params), the message sink, the precision
/// helper, the deprecation decorator.  The real ones pull PySide and the
/// host's parameter database, which is G1b's loader problem; what runs
/// UNMODIFIED here is DraftVecUtils itself.  Same script on both sides.
const char* const DraftStubs =
    "import sys, types\n"
    "def _mod(name, **attrs):\n"
    "    m = types.ModuleType(name)\n"
    "    m.__dict__.update(attrs)\n"
    "    sys.modules[name] = m\n"
    "    return m\n"
    "pkg = _mod('draftutils')\n"
    "pkg.__path__ = []\n"
    "pkg.params = _mod('draftutils.params',\n"
    "    get_param=lambda entry, path='Mod/Draft', ret_default=False, silent=False:\n"
    "        {'precision': 6}.get(entry))\n"
    "_noop = lambda *a, **k: None\n"
    "pkg.messages = _mod('draftutils.messages', _msg=_noop, _wrn=_noop, _err=_noop, _log=_noop)\n"
    "pkg.utils = _mod('draftutils.utils', precision=lambda: 6)\n"
    "if 'freecad' not in sys.modules:\n"
    "    try:\n"
    "        import freecad\n"
    "    except ImportError:\n"
    "        _mod('freecad').__path__ = []\n"
    "sys.modules['freecad'].deprecation = _mod('freecad.deprecation',\n"
    "    deprecated=lambda *a, **k: (lambda f: f))\n";

/// The harness module the expressions below reach through.
const char* const DraftHarness =
    "import math\n"
    "import FreeCAD\n"
    "import DraftVecUtils as D\n"
    "V = FreeCAD.Vector\n"
    "pi = math.pi\n";

/// Undo the stubs and the pushed modules on the host side.
const char* const DraftCleanup =
    "import sys\n"
    "for n in ('fcxtest', 'DraftVecUtils', 'draftutils.utils', 'draftutils.messages',\n"
    "          'draftutils.params', 'draftutils', 'freecad.deprecation'):\n"
    "    sys.modules.pop(n, None)\n"
    "fc = sys.modules.get('freecad')\n"
    "if fc is not None and hasattr(fc, 'deprecation'):\n"
    "    del fc.deprecation\n";

/// Run `source` on the host as module `name` (in sys.modules); the
/// mirror of ImageHost::exec(source, name).
bool hostModule(const char* name, const std::string& source)
{
    Base::PyGILStateLocker lock;
    PyObject* mod = PyModule_New(name);
    if (!mod)
        return false;
    PyObject* g = PyModule_GetDict(mod);
    PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
    PyDict_SetItemString(PyImport_GetModuleDict(), name, mod);
    PyObject* r = PyRun_String(source.c_str(), Py_file_input, g, g);
    Py_DECREF(mod);
    if (!r) {
        PyErr_Print();
        return false;
    }
    Py_DECREF(r);
    return true;
}

bool hostEvalFloat(const std::string& expr, double& out)
{
    Base::PyGILStateLocker lock;
    PyObject* g = PyDict_New();
    PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
    PyObject* r = PyRun_String(expr.c_str(), Py_eval_input, g, g);
    Py_DECREF(g);
    if (!r) {
        PyErr_Print();
        return false;
    }
    out = PyFloat_AsDouble(r);
    Py_DECREF(r);
    return !PyErr_Occurred();
}

}  // namespace

// ---- the G1a gate (docs/Sandbox.md sec 7.6): draftgeoutils in the
// ---- guest on handles, every answer equal to the same source run on
// ---- the host.

namespace
{

/// The modules pushed into the guest AND registered on the host from
/// the same source, in dependency order.  geo_arrays (document objects
/// and frames) is not geometry and is left out.
const char* const GeoUtilsModules[] = {
    "general", "edges", "geometry", "intersections", "wires", "faces", "arcs",
    "circles", "offsets", "sort_edges", "fillets", "cuboids", "linear_algebra",
    "circle_inversion", "circles_apollonius", "circles_incomplete",
};

/// Stubs around draftgeoutils: WorkingPlane (imported, never used by
/// the functions below), draftutils.gui_utils (get_3d_view, GUI only),
/// lazy_loader, plus the DraftVecUtils set.
const char* const GeoUtilsStubs =
    "import sys, types\n"
    "def _mod(name, **attrs):\n"
    "    m = types.ModuleType(name)\n"
    "    m.__dict__.update(attrs)\n"
    "    sys.modules[name] = m\n"
    "    return m\n"
    "_mod('WorkingPlane')\n"
    "sys.modules['draftutils'].gui_utils = _mod('draftutils.gui_utils', get_3d_view=lambda: None)\n"
    // lazy_loader.LazyLoader: a module stand-in that imports on first
    // attribute access.  The real one (src/3rdParty/lazy_loader) does
    // the same through `from __future__ import ...`, which the wasi
    // guest's 16-file stdlib slice does not carry.
    "class _LazyLoader(types.ModuleType):\n"
    "    def __init__(self, local_name, parent_globals, name):\n"
    "        super().__init__(name)\n"
    "        self.__dict__['_fcx_target'] = name\n"
    "    def __getattr__(self, item):\n"
    "        return getattr(__import__(self.__dict__['_fcx_target']), item)\n"
    "_lz = _mod('lazy_loader')\n"
    "_lz.__path__ = []\n"
    "_lz.lazy_loader = _mod('lazy_loader.lazy_loader', LazyLoader=_LazyLoader)\n";

/// The harness module, same text both sides: the pushed modules by
/// their short names, the shape names, and _run(expr, env) -> the
/// normalised result or ('E', exception type).  Normalisation reduces
/// shapes to (kind, measures, counts), values to rounded numbers, so
/// a guest answer built from handles and a host answer built from the
/// objects compare as text.
const char* const GeoUtilsHarness =
    "import math\n"
    "import FreeCAD\n"
    "import Part\n"
    "import DraftVecUtils\n"
    "from draftgeoutils import general, edges, geometry, wires, faces, arcs, circles\n"
    "from draftgeoutils import intersections, offsets, sort_edges, fillets, cuboids\n"
    "from draftgeoutils import linear_algebra, circle_inversion, circles_apollonius\n"
    "from draftgeoutils import circles_incomplete\n"
    "V = FreeCAD.Vector\n"
    "def _r(v):\n"
    "    return round(v, 6) + 0.0\n"
    "def _N(x):\n"
    "    if x is None or isinstance(x, (bool, int, str)):\n"
    "        return x if x is not None else 'None'\n"
    "    if isinstance(x, float):\n"
    "        return _r(x)\n"
    "    if isinstance(x, (list, tuple)) or type(x).__name__ == 'ShapeList':\n"
    "        return [_N(i) for i in x]\n"
    "    if isinstance(x, FreeCAD.Vector):\n"
    "        return ('V', _r(x.x), _r(x.y), _r(x.z))\n"
    "    if isinstance(x, FreeCAD.Rotation):\n"
    "        return ('R', _N(x.Axis), _r(x.Angle))\n"
    "    if isinstance(x, FreeCAD.Placement):\n"
    "        return ('P', _N(x.Base), _N(x.Rotation))\n"
    "    if isinstance(x, FreeCAD.Matrix):\n"
    "        return ('M', [_r(v) for v in x.A])\n"
    "    st = getattr(x, 'ShapeType', None)\n"
    "    if st is not None:\n"
    "        return ('S', st, _r(x.Length), _r(x.Area), _r(x.Volume),\n"
    "                len(x.Edges), len(x.Vertexes), _N(x.BoundBox.Center))\n"
    "    ti = getattr(x, 'TypeId', None)\n"
    "    if ti:\n"
    "        return ('G', ti)\n"
    "    return type(x).__name__\n"
    "def _run(expr, env):\n"
    "    try:\n"
    "        return _N(eval(expr, globals(), env))\n"
    "    except Exception as e:\n"
    "        return ('E', type(e).__name__, str(e)[:120])\n";

/// The shapes both sides work on, made on the host and bound as
/// handles: a box and one of its faces, three lines, a closed and an
/// open polygon, a crossing polyline, three circles, an arc, a spline
/// edge, a plane face.
const char* const GeoUtilsFixtures =
    "import Part, FreeCAD\n"
    "V = FreeCAD.Vector\n"
    "s = Part.makeBox(2, 3, 4)\n"
    "f = s.Faces[5]\n"
    "l = Part.makeLine(V(0, 0, 0), V(2, 0, 0))\n"
    "l2 = Part.makeLine(V(2, 0, 0), V(2, 2, 0))\n"
    "l3 = Part.makeLine(V(0, 0, 0), V(0, 2, 0))\n"
    "w = Part.makePolygon([V(0, 0, 0), V(2, 0, 0), V(2, 2, 0), V(0, 2, 0), V(0, 0, 0)])\n"
    "w2 = Part.makePolygon([V(0, 0, 0), V(2, 0, 0), V(2, 2, 0)])\n"
    "w3 = Part.makePolygon([V(-1, 1, 0), V(3, 1, 0)])\n"
    "c = Part.makeCircle(1.0)\n"
    "c2 = Part.makeCircle(1.0, V(3, 0, 0))\n"
    "c3 = Part.makeCircle(1.0, V(0, 3, 0))\n"
    "a = Part.makeCircle(1.0, V(0, 0, 0), V(0, 0, 1), 0, 90)\n"
    "_b = Part.BSplineCurve()\n"
    "_b.interpolate([V(0, 0, 0), V(1, 1, 0), V(2, 0, 0)])\n"
    "b = _b.toShape()\n"
    "pl = Part.makePlane(2, 2)\n";

const char* const GeoUtilsNames[] = {
    "s", "f", "l", "l2", "l3", "w", "w2", "w3", "c", "c2", "c3", "a", "b", "pl",
};

/// One call per public function the fixtures can feed (the harness
/// classifies a failure by exception type, so a call the fixture does
/// not suit still has to fail the SAME way on both sides).
const char* const GeoUtilsCalls[] = {
    // general
    "general.precision()",
    "general.vec(l)",
    "general.vec(l, True)",
    "general.edg(V(0, 0, 0), V(1, 1, 0))",
    "general.getVerts(w)",
    "general.v1(l)",
    "general.isNull(s)",
    "general.isNull(V(0, 0, 0))",
    "general.isPtOnEdge(V(1, 0, 0), l)",
    "general.isPtOnEdge(V(1, 1, 0), l)",
    "general.hasCurves(s)",
    "general.hasCurves(c)",
    "general.isAligned(l, 'x')",
    "general.isAligned(l, 'y')",
    "general.getQuad(f)",
    "general.areColinear(l, l2)",
    "general.areColinear(l, Part.makeLine(V(3, 0, 0), V(5, 0, 0)))",
    "general.hasOnlyWires(w)",
    "general.geomType(c)",
    "general.geomType(l)",
    "general.geomType(b)",
    "general.isValidPath(w)",
    "general.findClosest(V(0, 0, 0), [V(1, 0, 0), V(0.5, 0, 0)])",
    "general.getBoundaryAngles(0.1, [0.0, 1.0, 2.0])",
    // edges
    "edges.findEdge(l, [l2, l])",
    "edges.orientEdge(a)",
    "edges.orientEdge(l, V(0, 0, 1))",
    "edges.isSameLine(l, l)",
    "edges.isSameLine(l, l2)",
    "edges.is_line(b)",
    "edges.invert(l)",
    "edges.invert(w)",
    "edges.findMidpoint(a)",
    "edges.findMidpoint(l)",
    "edges.getTangent(c)",
    "edges.getTangent(l)",
    // wires
    "wires.findWires(s.Edges)",
    "wires.isReallyClosed(w)",
    "wires.isReallyClosed(w2)",
    "wires.rebaseWire(w, 1)",
    "wires.removeInterVertices(w)",
    "wires.superWire(w.Edges, True)",
    "wires.flattenWire(w)",
    "wires.curvetowire(c, 8)",
    "wires.curvetosegment(c, 1.0)",
    "wires.get_placement_perpendicular_to_wire(w)",
    "wires.get_extended_wire(w2, 1.0, 1.0)",
    // geometry
    "geometry.findPerpendicular(V(1, 1, 0), [l])",
    "geometry.findDistance(V(1, 1, 0), l)",
    "geometry.findDistance(V(1, 1, 0), c)",
    "geometry.get_spline_normal(b)",
    "geometry.get_shape_normal(f)",
    "geometry.get_normal(w)",
    "geometry.get_normal(l)",
    "geometry.getRotation(V(1, 0, 0), V(0, 1, 0))",
    "geometry.is_planar(w)",
    "geometry.is_straight_line(l)",
    "geometry.is_straight_line(w2)",
    "geometry.are_coplanar(f, w)",
    "geometry.find_plane(f)",
    "geometry.calculatePlacement(w)",
    "geometry.mirror(V(1, 1, 0), l)",
    "geometry.uv_vectors_from_face(f)",
    "geometry.placement_from_face(f)",
    "geometry.placement_from_points(V(0, 0, 0), V(1, 0, 0), V(0, 1, 0))",
    "geometry.distance_to_plane(V(1, 2, 3), V(0, 0, 0), V(0, 0, 1))",
    "geometry.project_point_on_plane(V(1, 2, 3), V(0, 0, 0), V(0, 0, 1))",
    // faces
    "faces.concatenate(s)",
    "faces.getBoundary(pl)",
    "faces.is_coplanar([f, f])",
    "faces.bind(w, w2)",
    "faces.cleanFaces(s)",
    "faces.removeSplitter(s)",
    // arcs
    "arcs.isClockwise(a)",
    "arcs.isWideAngle(a)",
    "arcs.arcFrom2Pts(V(1, 0, 0), V(0, 1, 0), V(0, 0, 0))",
    "arcs.arcFromSpline(b)",
    // circles
    "circles.findClosestCircle(V(0, 0, 0), [c, c2])",
    "circles.getCircleFromSpline(b)",
    "circles.circlefrom1Line2Points(l, V(0, 1, 0), V(2, 1, 0))",
    "circles.circlefrom2Lines1Point(l, l3, V(1, 1, 0))",
    "circles.circleFrom2LinesRadius(l, l3, 1.0)",
    "circles.circleFrom3LineTangents(l, l2, l3)",
    "circles.circleFromPointLineRadius(V(1, 1, 0), l, 1.0)",
    "circles.circleFrom2PointsRadius(V(0, 0, 0), V(2, 0, 0), 2.0)",
    "circles.findHomotheticCenterOfCircles(c, c2)",
    "circles.findRadicalAxis(c, c2)",
    "circles.findRadicalCenter(c, c2, c3)",
    // intersections
    "intersections.findIntersection(l, l3)",
    "intersections.findIntersection(l, l2, True, True)",
    "intersections.wiresIntersect(w, w3)",
    "intersections.connect([l, l2])",
    "intersections.angleBisection(l, l3)",
    // offsets
    "offsets.offset(l, V(0, 1, 0))",
    "offsets.offsetWire(w, V(0, 0.5, 0))",
    "offsets.pocket2d(pl, 0.3)",
    // sort_edges
    "sort_edges.sortEdges(list(reversed(w.Edges)))",
    "sort_edges.sortEdgesOld(list(reversed(w.Edges)))",
    // fillets
    "fillets.fillet([l, l2], 0.5)",
    "fillets.fillet([l, l2], 0.5, True)",
    "fillets.filletWire(w2, 0.3)",
    // cuboids
    "cuboids.isCubic(s)",
    "cuboids.getCubicDimensions(s)",
    // linear_algebra
    "linear_algebra.linearFromPoints(V(0, 0, 0), V(1, 1, 0))",
    "linear_algebra.determinant([[1, 2], [3, 4]], 2)",
    // circle_inversion
    "circle_inversion.pointInversion(c, V(3, 0, 0))",
    "circle_inversion.polarInversion(c, l2)",
    "circle_inversion.circleInversion(c, c2)",
    // circles_apollonius
    "circles_apollonius.outerSoddyCircle(c, c2, c3)",
    "circles_apollonius.innerSoddyCircle(c, c2, c3)",
    "circles_apollonius.circleFrom3CircleTangents(c, c2, c3)",
    // circles_incomplete
    "circles_incomplete.circleFrom2tan1pt(l, l3, V(1, 1, 0))",
    "circles_incomplete.circleFrom2tan1rad(l, l3, 0.5)",
    "circles_incomplete.circleFrom1tan2pt(l, V(0, 1, 0), V(2, 1, 0))",
    "circles_incomplete.circleFrom1tan1pt1rad(l, V(1, 1, 0), 1.0)",
    "circles_incomplete.circleFrom3tan(l, l2, l3)",
};

/// The GeoUtilsFixtures run on the host: a dict holding the shapes.
PyObject* makeGeoUtilsFixtures()
{
    Base::PyGILStateLocker lock;
    PyObject* fixtures = PyDict_New();
    PyDict_SetItemString(fixtures, "__builtins__", PyEval_GetBuiltins());
    PyObject* r = PyRun_String(GeoUtilsFixtures, Py_file_input, fixtures, fixtures);
    if (!r) {
        PyErr_Print();
        Py_DECREF(fixtures);
        return nullptr;
    }
    Py_DECREF(r);
    return fixtures;
}

/// The G1a comparison: every GeoUtilsCalls expression evaluated in the
/// guest through fcxgu._run on the fixtures bound as handles, and on
/// the host through the same harness on the objects themselves; each
/// pair compared as text.  Returns how many agree; `bothError` counts
/// the agreed ones that failed alike, listed in `failingAlike`.  The
/// harness module `fcxgu` must be in place on both sides.
int geoUtilsAgreement(ImageHost& host, PyObject* fixtures, int& bothError,
                      std::string& failingAlike)
{
    std::string envText = "dict(";
    for (const char* n : GeoUtilsNames)
        envText += std::string(n) + "=" + n + ", ";
    envText += ")";
    auto pack = [&]() {
        Base::PyGILStateLocker lock;
        json m = json::object();
        for (const char* n : GeoUtilsNames) {
            PyObject* py = PyDict_GetItemString(fixtures, n);
            Py_INCREF(py);
            auto one = pyBinding(n, py);
            m.update(json::from_cbor(one.begin(), one.end()));
        }
        auto v = json::to_cbor(m);
        return std::vector<unsigned char>(v.begin(), v.end());
    };

    int agreed = 0;
    bothError = 0;
    failingAlike.clear();
    for (const char* call : GeoUtilsCalls) {
        std::string expr = std::string("__import__('fcxgu')._run(") + json(call).dump() + ", "
            + envText + ")";
        auto g = host.eval(expr, pack());
        EXPECT_TRUE(g.ok) << call << ": " << g.excType << ": " << g.message;
        if (!g.ok)
            break;
        std::string want;
        std::string got;
        {
            Base::PyGILStateLocker lock;
            PyObject* r = PyRun_String(expr.c_str(), Py_eval_input, fixtures, fixtures);
            if (!r)
                PyErr_Print();
            EXPECT_NE(r, nullptr) << call;
            if (!r)
                break;
            PyObject* rep = PyObject_Repr(r);
            Py_DECREF(r);
            want = PyUnicode_AsUTF8(rep);
            Py_DECREF(rep);
            PyObject* v = host.decodeResult(g);
            EXPECT_NE(v, nullptr) << call;
            if (!v)
                break;
            rep = PyObject_Repr(v);
            Py_DECREF(v);
            got = PyUnicode_AsUTF8(rep);
            Py_DECREF(rep);
        }
        EXPECT_EQ(got, want) << call;
        if (got == want) {
            ++agreed;
            if (want.rfind("('E', ", 0) == 0) {
                ++bothError;
                failingAlike += std::string("\n    ") + call + " -> " + want;
            }
        }
        host.clearHandles();
    }
    return agreed;
}

constexpr int GeoUtilsCallCount = sizeof(GeoUtilsCalls) / sizeof(*GeoUtilsCalls);

}  // namespace

TEST_F(ExpressionImageEvalTest, draftgeoutilsOnHandles)
{
    std::string vecUtils = readDraftFile("DraftVecUtils.py");
    if (vecUtils.empty())
        GTEST_SKIP() << "Mod/Draft/DraftVecUtils.py not found (set FCX_REPO)";
    std::vector<std::pair<std::string, std::string>> sources;  // (module, text)
    for (const char* m : GeoUtilsModules) {
        std::string text = readDraftFile((std::string("draftgeoutils/") + m + ".py").c_str());
        ASSERT_FALSE(text.empty()) << m;
        sources.emplace_back(std::string("draftgeoutils.") + m, text);
    }
    PyObject* part = nullptr;
    {
        Base::PyGILStateLocker lock;
        part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            GTEST_SKIP() << "the Part module is not importable in this test binary";
        }
    }
    auto& host = ImageHost::instance();

    // the same push on both sides; the first failure ends the test
    auto push = [&](const char* module, const std::string& text) {
        auto r = host.exec(text, module ? module : "");
        EXPECT_TRUE(r.ok) << (module ? module : "stubs") << ": " << r.excType << ": " << r.message;
        if (!r.ok)
            return false;
        bool ok = hostModule(module ? module : "_fcx_geo_stubs", text);
        EXPECT_TRUE(ok) << (module ? module : "stubs") << " on the host";
        return ok;
    };
    if (!push(nullptr, DraftStubs) || !push(nullptr, GeoUtilsStubs)
            || !push("DraftVecUtils", vecUtils) || !push("draftgeoutils", ""))
        return;
    for (const auto& [module, text] : sources)
        if (!push(module.c_str(), text))
            return;
    if (!push("fcxgu", GeoUtilsHarness))
        return;

    // fixtures on the host; each bound as a handle for the guest
    PyObject* fixtures = makeGeoUtilsFixtures();
    ASSERT_NE(fixtures, nullptr);

    host.resetStats();
    int bothError = 0;
    std::string failingAlike;
    int agreed = geoUtilsAgreement(host, fixtures, bothError, failingAlike);
    auto st = host.stats();
    std::cout << "draftgeoutils: " << agreed << "/" << GeoUtilsCallCount
              << " calls agree, " << bothError << " of them failing alike; bridge ops:";
    for (const auto& [k, v] : st.ops)
        std::cout << " " << k << "=" << v;
    std::cout << failingAlike << std::endl;
    EXPECT_LT(bothError * 4, agreed) << "too many calls fail on both sides: fixtures unsuitable";

    {
        Base::PyGILStateLocker lock;
        Py_DECREF(fixtures);
        Py_DECREF(part);
        PyObject* g = PyDict_New();
        PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
        std::string cleanup = std::string(DraftCleanup)
            + "for n in [m for m in sys.modules if m == 'fcxgu' or m == 'WorkingPlane'"
              " or m.startswith('draftgeoutils') or m.startswith('lazy_loader')"
              " or m == 'draftutils.gui_utils']:\n"
              "    sys.modules.pop(n, None)\n";
        PyObject* c = PyRun_String(cleanup.c_str(), Py_file_input, g, g);
        Py_XDECREF(c);
        Py_DECREF(g);
        if (PyErr_Occurred())
            PyErr_Clear();
    }
}

TEST_F(ExpressionImageEvalTest, draftVecUtilsInGuestZeroHops)
{
    std::string source = readDraftFile("DraftVecUtils.py");
    if (source.empty())
        GTEST_SKIP() << "Mod/Draft/DraftVecUtils.py not found (set FCX_REPO)";

    auto& host = ImageHost::instance();
    auto r = host.exec(DraftStubs);
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.exec(source, "DraftVecUtils");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    r = host.exec(DraftHarness, "fcxtest");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;

    ASSERT_TRUE(hostModule("_fcx_draft_stubs", DraftStubs));
    ASSERT_TRUE(hostModule("DraftVecUtils", source));
    ASSERT_TRUE(hostModule("fcxtest", DraftHarness));

    // Every public function but typecheck (which returns None), each
    // reduced to one float so both sides compare on the wire's terms.
    const char* cases[] = {
        "T.D.precision()",
        "len(T.D.toString(T.V(1, 2, 3)))",
        "T.D.tup(T.V(1, 2, 3))[2]",
        "T.D.neg(T.V(1, -2, 3)).y",
        "T.D.equals(T.V(1, 2, 3), T.V(1, 2, 3.0000001))",
        "T.D.scale(T.V(1, 2, 3), 2).z",
        "T.D.scaleTo(T.V(3, 4, 0), 10).x",
        "T.D.dist(T.V(1, 2, 3), T.V(4, 6, 3))",
        "T.D.angle(T.V(1, 0, 0), T.V(0, 1, 0))",
        "T.D.angle(T.V(1, 0, 0), T.V(0, -1, 0), T.V(0, 0, 1))",
        "T.D.project(T.V(1, 1, 0), T.V(2, 0, 0)).x",
        "T.D.rotate2D(T.V(1, 0, 0), T.pi / 2).y",
        "T.D.rotate(T.V(1, 0, 0), T.pi / 2, T.V(0, 0, 1)).y",
        "T.D.getRotation(T.V(0, 1, 0))[3]",
        "T.D.isNull(T.V(1e-9, 0, 0))",
        "T.D.find(T.V(1, 0, 0), [T.V(5, 0, 0), T.V(1, 0, 0)])",
        "T.D.closest(T.V(0, 0, 0), [T.V(5, 0, 0), T.V(1, 0, 0), T.V(3, 0, 0)])",
        "T.D.isColinear([T.V(0, 0, 0), T.V(1, 1, 0), T.V(2, 2, 0)])",
        "T.D.rounded(T.V(1.23456789, 0, 0)).x",
        "T.D.getPlaneRotation(T.V(1, 0, 0), T.V(0, 1, 0)).A22",
        "len(T.D.removeDoubles([T.V(0, 0, 0), T.V(0, 0, 0), T.V(1, 0, 0)]))",
        "T.D.get_spherical_coords(1, 1, 0)[1]",
        "T.D.get_cartesian_coords(2, T.pi / 2, T.pi / 2)[1]",
    };

    host.resetStats();
    std::size_t n = 0;
    for (const char* expr : cases) {
        std::string wrapped = std::string("float((lambda T: ") + expr
            + ")(__import__('fcxtest')))";
        auto g = host.eval(wrapped, {});
        ASSERT_TRUE(g.ok) << expr << ": " << g.excType << ": " << g.message;
        double native = 0;
        ASSERT_TRUE(hostEvalFloat(wrapped, native)) << expr;
        EXPECT_NEAR(value(g).get<double>(), native, 1e-12) << expr;
        ++n;
    }
    auto st = host.stats();
    EXPECT_EQ(st.evals, n);
    EXPECT_EQ(st.handles, 0u) << "a value class crossed as a handle";
    EXPECT_TRUE(st.ops.empty()) << "bridge ops: " << [&] {
        std::string s;
        for (const auto& [k, v] : st.ops)
            s += k + "=" + std::to_string(v) + " ";
        return s;
    }();

    {
        Base::PyGILStateLocker lock;
        PyObject* g = PyDict_New();
        PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
        PyObject* c = PyRun_String(DraftCleanup, Py_file_input, g, g);
        Py_XDECREF(c);
        Py_DECREF(g);
        if (PyErr_Occurred())
            PyErr_Clear();
    }
}

// ---- the G1b gate (docs/Sandbox.md sec 5.6, 7.6): Draft's App side
// ---- boots with the pyodide guest as the bundled fcx_draft wheel --
// ---- no exec, no stubs.  Every module of the wheel imports there, the
// ---- preference reader answers from the host through its facade, and
// ---- the G1a calls agree from the wheel.

namespace
{

/// The harness, same text both sides: the wheel's module inventory
/// (its packages walked, params named by hand -- the facade has no
/// file), an import of each, and the preference reader.
const char* const WheelHarness =
    "import importlib, json, pkgutil\n"
    "from draftutils import params\n"
    "PACKAGES = ('draftgeoutils', 'draftutils', 'draftfunctions', 'draftmake', 'draftobjects')\n"
    "TOP = ('DraftVecUtils', 'DraftGeomUtils', 'WorkingPlane', 'draftutils.params')\n"
    "def _modules():\n"
    "    names = list(TOP)\n"
    "    for p in PACKAGES:\n"
    "        pkg = importlib.import_module(p)\n"
    "        names += sorted(p + '.' + m.name for m in pkgutil.iter_modules(pkg.__path__))\n"
    "    return names\n"
    "def _import_all():\n"
    "    failed = []\n"
    "    names = _modules()\n"
    "    for n in names:\n"
    "        try:\n"
    "            importlib.import_module(n)\n"
    "        except Exception as e:\n"
    "            failed.append('%s: %s: %s' % (n, type(e).__name__, str(e)[:120]))\n"
    "    return json.dumps({'count': len(names), 'failed': failed})\n"
    "def _param(expr):\n"
    "    return repr(eval(expr, {'params': params}))\n";

/// What the wheel's App side reads through draftutils.params: the
/// Draft group by default, the View group through get_param_view.
const char* const ParamReads[] = {
    "params.get_param('precision')",
    "params.get_param('gridSpacing')",
    "params.get_param('DefaultAnnoScaleMultiplier')",
    "params.get_param('dimsymbol')",
    "params.get_param('svgDashedLine')",
    "params.get_param_view('MarkerSize')",
    "params.get_param_view('DefaultShapeColor')",
};

/// Evaluate on the host; the result's str() (the harness already
/// returns a repr, so this is the text the guest sends).
bool hostEvalStr(const std::string& expr, std::string& out)
{
    Base::PyGILStateLocker lock;
    PyObject* g = PyDict_New();
    PyDict_SetItemString(g, "__builtins__", PyEval_GetBuiltins());
    PyObject* r = PyRun_String(expr.c_str(), Py_eval_input, g, g);
    Py_DECREF(g);
    if (!r) {
        PyErr_Print();
        return false;
    }
    PyObject* rep = PyObject_Str(r);
    Py_DECREF(r);
    if (!rep)
        return false;
    out = PyUnicode_AsUTF8(rep);
    Py_DECREF(rep);
    return true;
}

}  // namespace

TEST_F(ExpressionImageEvalTest, draftWheelInGuest)
{
    auto& host = ImageHost::instance();
    if (host.runtime() != "pyodide")
        GTEST_SKIP() << "bundled wheels load on the pyodide runtime only";
    {
        // the wheel the build tree packs into <datadir>/Pyodide/wheels
        // (src/Mod/Draft/CMakeLists.txt), where the layout finds it
        namespace fs = std::filesystem;
        bool bundled = false;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(
                 App::Application::getResourceDir() + "Pyodide/wheels", ec)) {
            const std::string fn = e.path().filename().string();
            bundled = bundled || (fn.rfind("fcx_draft-", 0) == 0 && fn.find("-py3-none-any.whl") != std::string::npos);
        }
        if (!bundled)
            GTEST_SKIP() << "no fcx_draft wheel bundled under " << App::Application::getResourceDir()
                         << "Pyodide/wheels (BUILD_DRAFT with BUILD_EXPR_PYODIDE_HOST packs one)";
    }
    PyObject* part = nullptr;
    {
        Base::PyGILStateLocker lock;
        part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            GTEST_SKIP() << "the Part module is not importable in this test binary";
        }
    }

    // 1. the harness on both sides; every module of the wheel imports
    //    in the guest (the guest's `from draftutils import params` is
    //    the facade, the host's the real reader)
    auto r = host.exec(WheelHarness, "fcxwheel");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    ASSERT_TRUE(hostModule("fcxwheel", WheelHarness));
    host.resetStats();
    auto g = host.eval("__import__('fcxwheel')._import_all()", {});
    ASSERT_TRUE(g.ok) << g.excType << ": " << g.message;
    json report = json::parse(value(g).get<std::string>());
    EXPECT_GE(report.value("count", 0), 100) << "the wheel's inventory is short: " << report.dump();
    EXPECT_TRUE(report["failed"].empty()) << "modules that do not import in the guest:\n"
                                          << report["failed"].dump(2);
    auto st = host.stats();
    std::cout << "fcx_draft: " << report.value("count", 0) << " modules imported in the guest;"
              << " bridge ops during import:";
    for (const auto& [k, v] : st.ops)
        std::cout << " " << k << "=" << v;
    std::cout << std::endl;

    // 2. the preference reader: each read equal to the host's own
    for (const char* expr : ParamReads) {
        std::string wrapped = std::string("__import__('fcxwheel')._param(") + json(expr).dump() + ")";
        auto gv = host.eval(wrapped, {});
        ASSERT_TRUE(gv.ok) << expr << ": " << gv.excType << ": " << gv.message;
        std::string want;
        ASSERT_TRUE(hostEvalStr(wrapped, want)) << expr;
        EXPECT_EQ(value(gv).get<std::string>(), want) << expr;
    }

    // 3. the G1a calls, this time from the wheel on both sides
    r = host.exec(GeoUtilsHarness, "fcxgu");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    ASSERT_TRUE(hostModule("fcxgu", GeoUtilsHarness));
    PyObject* fixtures = makeGeoUtilsFixtures();
    ASSERT_NE(fixtures, nullptr);
    int bothError = 0;
    std::string failingAlike;
    int agreed = geoUtilsAgreement(host, fixtures, bothError, failingAlike);
    std::cout << "draftgeoutils from the wheel: " << agreed << "/" << GeoUtilsCallCount
              << " calls agree, " << bothError << " of them failing alike" << failingAlike
              << std::endl;
    EXPECT_EQ(agreed, GeoUtilsCallCount);
    EXPECT_LT(bothError * 4, agreed) << "too many calls fail on both sides: fixtures unsuitable";

    {
        Base::PyGILStateLocker lock;
        Py_DECREF(fixtures);
        Py_DECREF(part);
        PyObject* d = PyImport_GetModuleDict();
        PyDict_DelItemString(d, "fcxgu");
        PyDict_DelItemString(d, "fcxwheel");
        if (PyErr_Occurred())
            PyErr_Clear();
    }
}

TEST_F(ExpressionImageEvalTest, arithmeticInImage)
{
    auto res = ImageHost::instance().evalExpression(obj, "2 ^ 10 + 0.5");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 1024.5);
}

TEST_F(ExpressionImageEvalTest, unitArithmeticInImage)
{
    auto res = ImageHost::instance().evalExpression(obj, "10mm + 1cm");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    json v = value(res);
    EXPECT_EQ(v.value("t", ""), "quantity");
    EXPECT_DOUBLE_EQ(v["v"].get<double>(), 20.0);
    EXPECT_EQ(v["u"][0].get<int>(), 1);
}

TEST_F(ExpressionImageEvalTest, identifierResolvesFromPack)
{
    auto res = ImageHost::instance().evalExpression(obj, "Width * 2");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 42.0);
}

TEST_F(ExpressionImageEvalTest, unresolvedIdentifierFailsInImage)
{
    auto res = ImageHost::instance().evalExpression(obj, "Nope.Nothing");
    ASSERT_FALSE(res.ok);
    EXPECT_NE(res.message.find("Nope"), std::string::npos) << res.message;
}

TEST_F(ExpressionImageEvalTest, parseErrorIsNativeParserError)
{
    auto res = ImageHost::instance().evalExpression(obj, "1 +");
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "ParserError");
    EXPECT_NE(res.message.find("syntax error"), std::string::npos)
        << res.message;
}

TEST_F(ExpressionImageEvalTest, pseudoModuleRunsInImage)
{
    auto res = ImageHost::instance().evalExpression(
        obj, "_math.degrees(3.141592653589793)");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 180.0);
}

TEST_F(ExpressionImageEvalTest, conditionalAndComparison)
{
    auto res = ImageHost::instance().evalExpression(
        obj, "Width > 10 ? <<big>> : <<small>>");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<std::string>(), "big");
}

TEST_F(ExpressionImageEvalTest, foreignDocGrantCycleAtPackTime)
{
    using App::ExpressionSecurity::Permission;
    using App::ExpressionSecurity::Runtime;

    // the bindings pack is resolved under the owner's document
    // principal: a foreign-document identifier PROMPTs at pack time,
    // a grant lets the packed evaluation through, a deny blocks again
    auto doc2 = App::GetApplication().newDocument("FcxEvalOther", "testUser");
    auto obj2 = doc2->addObject("App::FeaturePython", "Remote");
    auto depth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj2->addDynamicProperty("App::PropertyFloat", "Depth"));
    ASSERT_NE(depth, nullptr);
    depth->setValue(15.0);

    const std::string src = "FcxEvalOther#Remote.Depth * 2";
    auto denied = ImageHost::instance().evalExpression(obj, src);
    ASSERT_FALSE(denied.ok);
    EXPECT_EQ(denied.excType, "PermissionError");

    std::string principal = Runtime::instance().documentPrincipal(doc);
    Runtime::instance().grant(principal, Permission::DocForeign,
                              "FcxEvalOther", true, "session");
    auto granted = ImageHost::instance().evalExpression(obj, src);
    ASSERT_TRUE(granted.ok) << granted.excType << ": " << granted.message;
    EXPECT_DOUBLE_EQ(value(granted).get<double>(), 30.0);

    Runtime::instance().grant(principal, Permission::DocForeign,
                              "FcxEvalOther", false, "session");
    auto revoked = ImageHost::instance().evalExpression(obj, src);
    ASSERT_FALSE(revoked.ok);
    EXPECT_EQ(revoked.excType, "PermissionError");

    Runtime::instance().clearPending(principal, Permission::DocForeign,
                                     "FcxEvalOther");
    App::GetApplication().closeDocument(doc2->getName());
}

TEST_F(ExpressionImageEvalTest, resolveAliasOpAnswers)
{
    // the dedicated resolve_alias bridge op (RangeExpression's mid-eval
    // reach-back), exercised directly against a sheet-like stub with a
    // local handle table
    App::ExpressionSandbox::HandleTable table;
    uint64_t id = 0;
    {
        Base::PyGILStateLocker lock;
        PyObject* ns = PyDict_New();
        PyDict_SetItemString(ns, "__builtins__", PyEval_GetBuiltins());
        PyObject* r = PyRun_String(
            "class S:\n"
            "    def getCellFromAlias(self, a):\n"
            "        return 'B2' if a == 'foo' else ''\n"
            "o = S()\n",
            Py_file_input, ns, ns);
        ASSERT_NE(r, nullptr);
        Py_DECREF(r);
        PyObject* o = PyDict_GetItemString(ns, "o");  // borrowed
        ASSERT_NE(o, nullptr);
        id = table.add(o);
        Py_DECREF(ns);
    }
    json req;
    req["op"] = "resolve_alias";
    req["h"] = id;
    req["a"] = "foo";
    json reply = App::ExpressionSandbox::dispatchHostOp(table, req);
    ASSERT_TRUE(reply.value("ok", false))
        << reply.value("exc", "") << ": " << reply.value("msg", "");
    EXPECT_EQ(reply["val"].get<std::string>(), "B2");
    {
        Base::PyGILStateLocker lock;
        table.clear();
    }
}

// ---- the generated facades (docs/ExpressionSandbox.md sec 7.5):
// ---- declared members forward, dynamic properties ride read_prop,
// ---- everything else is unreachable ----

TEST_F(ExpressionImageEvalTest, facadeDeclaredAttrCrosses)
{
    auto res = ImageHost::instance().eval("o.Name", objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<std::string>(), "Obj");
}

TEST_F(ExpressionImageEvalTest, dynamicPropertyAnsweredByReadProp)
{
    // Width is a dynamic property, not an XML member: the facade has no
    // such attribute, so the proxy falls through to read_prop and the
    // host answers from getPropertyByName -- never host getattr.
    auto res = ImageHost::instance().eval("o.Width * 2",
                                          objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 42.0);
}

TEST_F(ExpressionImageEvalTest, facadeChainDocumentGetObject)
{
    // handle-tier attr -> declared call -> read_prop, all on facades
    auto res = ImageHost::instance().eval("o.Document.getObject('Obj').Width",
                                          objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 21.0);
}

TEST_F(ExpressionImageEvalTest, undeclaredXmlMemberUnreachable)
{
    // adjustRelativeLinks IS an XML member of DocumentObjectPy -- but
    // it carries no <Sandbox/> annotation, so the facade does not have
    // it and read_prop finds no such property: DENY by default.
    // (removeProperty, then renameProperty, played this part until
    // each was declared as a write-family call.)
    auto res = ImageHost::instance().eval("o.adjustRelativeLinks",
                                          objectBinding("o", obj));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "AttributeError");

    // and a forged get_attr against the host is a protocol error
    Base::PyGILStateLocker lock;
    App::ExpressionSandbox::HandleTable table;
    PyObject* py = obj->getPyObject();
    uint64_t id = table.add(py);
    Py_DECREF(py);
    json req;
    req["op"] = "get_attr";
    req["h"] = id;
    req["a"] = "adjustRelativeLinks";
    json reply = App::ExpressionSandbox::dispatchHostOp(table, req);
    EXPECT_FALSE(reply.value("ok", false));
    EXPECT_EQ(reply.value("exc", ""), "ProtocolError");
    table.clear();
}

TEST_F(ExpressionImageEvalTest, boundMemberWireFormCallable)
{
    // A bound method of a declared call-tier member crosses as its
    // base handle plus "m" and stays callable in-image; this is what
    // keeps pack-resolved method identifiers working.
    auto res = ImageHost::instance().eval("f('Obj').Name",
                                          objectBinding("f", doc, "getObject"));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<std::string>(), "Obj");
}

TEST_F(ExpressionImageEvalTest, declaredCallPassesPermissionClassification)
{
    using App::ExpressionSecurity::Runtime;
    // Declared members still route through the sec 3 permission check
    // per principal.  A method bound to a Document/DocumentObject is a
    // document read (checkCallablePermission -> DocReadSelf), ALLOWED
    // for the owner's own document even under a document principal --
    // so the facade call chain works while an undeclared member does
    // not, and the gate is still consulted (not bypassed).
    const std::string principal =
        "document:sha256:" + std::string(64, 'c');
    Runtime::Scope scope(principal.c_str());
    auto res = ImageHost::instance().eval("o.Document.getObject('Obj').Width",
                                          objectBinding("o", obj));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(value(res).get<double>(), 21.0);
}

// ---- ES sec 10 acceptance (docs/ExpressionSandbox.md): hostile
// ---- expressions from a real, saved-and-reloaded .FCStd cannot reach
// ---- the host FS / modules / object graph through the image, and the
// ---- grant lifecycle works end to end ----

#include <cstdio>

class ExpressionImageAcceptanceTest: public ExpressionImageBridgeTest
{
protected:
    void SetUp() override
    {
        ExpressionImageBridgeTest::SetUp();
        if (IsSkipped())
            return;
        path = std::string(std::tmpnam(nullptr)) + "-fcxaccept.FCStd";
        auto tmp = App::GetApplication().newDocument("FcxAccept", "testUser");
        auto o = tmp->addObject("App::FeaturePython", "Obj");
        auto width = Base::freecad_dynamic_cast<App::PropertyFloat>(
            o->addDynamicProperty("App::PropertyFloat", "Width"));
        ASSERT_NE(width, nullptr);
        width->setValue(21.0);
        // a stored (engine-bound) expression, so the reloaded document
        // carries real expression code and gets a real document-hash
        // principal
        o->addDynamicProperty("App::PropertyFloat", "Out");
        o->setExpression(App::ObjectIdentifier(o, "Out"),
                         App::Expression::parse(o, "Width * 2"));
        ASSERT_TRUE(tmp->saveAs(path.c_str()));
        App::GetApplication().closeDocument(tmp->getName());

        doc = App::GetApplication().openDocument(path.c_str());
        ASSERT_NE(doc, nullptr);
        obj = doc->getObject("Obj");
        ASSERT_NE(obj, nullptr);
    }

    void TearDown() override
    {
        if (doc)
            App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
        obj = nullptr;
        if (!path.empty())
            std::remove(path.c_str());
        ExpressionImageBridgeTest::TearDown();
    }

    App::Document* doc = nullptr;
    App::DocumentObject* obj = nullptr;
    std::string path;
};

TEST_F(ExpressionImageAcceptanceTest, storedExpressionStillEvaluates)
{
    auto res = ImageHost::instance().evalExpression(obj, "Width * 2");
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_DOUBLE_EQ(json::from_cbor(res.value.begin(), res.value.end())
                         .get<double>(), 42.0);
}

TEST_F(ExpressionImageAcceptanceTest, hostFsUnreachableFromExpression)
{
    // _py resolves to the IMAGE's builtins (never packed host-side), and
    // the image's own expression engine blocks open()/__import__ before
    // WASI even gets a chance (CallableExpression::securityCheck).  Either
    // way /etc/passwd never crosses -- no host FS reach.
    auto res = ImageHost::instance().evalExpression(
        obj, "_py.open(<</etc/passwd>>).read()");
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.message.find("root:"), std::string::npos) << res.message;
}

TEST_F(ExpressionImageAcceptanceTest, importedOsCannotSpawnFromExpression)
{
    auto res = ImageHost::instance().evalExpression(
        obj, "_py.__import__(<<subprocess>>).run(<<ls>>)");
    ASSERT_FALSE(res.ok);
    EXPECT_NE(res.excType, "") << res.message;
}

TEST_F(ExpressionImageAcceptanceTest, appModuleHasNoDocumentGraph)
{
    // the in-image FreeCAD module is the Ring 0 math slice: the host's
    // App.getDocument()...Proxy drill-down simply does not exist there
    auto res = ImageHost::instance().evalExpression(
        obj, "_app.getDocument(<<FcxAccept>>)");
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "AttributeError")
        << res.excType << ": " << res.message;
}

TEST_F(ExpressionImageAcceptanceTest, selfDrilldownDenied)
{
    // _self maps to unsafe.getattr (the frozen v1 catalog), DENY for a
    // document principal, whenever it reaches past a plain property of
    // the object.  (_self.Proxy stays a document read -- Proxy IS a
    // property; recompute is a method, so it is the drill-down.)  The
    // reopened .FCStd yields a real document:sha256 principal.
    auto res = ImageHost::instance().evalExpression(obj, "_self.recompute");
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "PermissionError")
        << res.excType << ": " << res.message;
}

TEST_F(ExpressionImageAcceptanceTest, foreignDocGrantRevokeCycle)
{
    using App::ExpressionSecurity::Permission;
    using App::ExpressionSecurity::Runtime;

    std::string path2 = std::string(std::tmpnam(nullptr)) + "-fcxother.FCStd";
    auto other = App::GetApplication().newDocument("FcxOther", "testUser");
    auto remote = other->addObject("App::FeaturePython", "Remote");
    auto depth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        remote->addDynamicProperty("App::PropertyFloat", "Depth"));
    ASSERT_NE(depth, nullptr);
    depth->setValue(15.0);
    ASSERT_TRUE(other->saveAs(path2.c_str()));
    App::GetApplication().closeDocument(other->getName());
    other = App::GetApplication().openDocument(path2.c_str());
    ASSERT_NE(other, nullptr);
    ASSERT_NE(other->getObject("Remote"), nullptr);

    // Reference the reopened doc by its actual internal name (openDocument
    // can suffix on collision).
    const std::string otherName = other->getName();
    const std::string src = otherName + "#Remote.Depth * 2";
    auto denied = ImageHost::instance().evalExpression(obj, src);
    ASSERT_FALSE(denied.ok);
    EXPECT_EQ(denied.excType, "PermissionError")
        << denied.excType << ": " << denied.message;

    std::string principal = Runtime::instance().documentPrincipal(doc);
    Runtime::instance().grant(principal, Permission::DocForeign,
                              otherName, true, "session");
    auto granted = ImageHost::instance().evalExpression(obj, src);
    ASSERT_TRUE(granted.ok) << granted.excType << ": " << granted.message;
    EXPECT_DOUBLE_EQ(json::from_cbor(granted.value.begin(),
                                     granted.value.end()).get<double>(), 30.0);

    Runtime::instance().grant(principal, Permission::DocForeign,
                              otherName, false, "session");
    auto revoked = ImageHost::instance().evalExpression(obj, src);
    ASSERT_FALSE(revoked.ok);
    EXPECT_EQ(revoked.excType, "PermissionError");

    Runtime::instance().clearPending(principal, Permission::DocForeign,
                                     otherName);
    App::GetApplication().closeDocument(other->getName());
    std::remove(path2.c_str());
}

// ---- the switch-over measurement (docs/ExpressionImage.md "The
// ---- evaluation switch-over"): what one desktop round trip through
// ---- the image costs against the in-process engine, in ONE binary on
// ---- ONE box.  DISABLED_ so the suite never pays for it; run with
// ---- --gtest_also_run_disabled_tests --gtest_filter='*Bench*'.

#include <chrono>
#include <iostream>

class ExpressionImageBenchTest: public ExpressionImageEvalTest
{
protected:
    /// Mean wall time of `fn` over `iters` runs, reported in us.
    template<class F>
    static double benchUs(const char* name, int iters, F&& fn)
    {
        fn();  // warm: first call pays instantiation / cache fills
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i)
            fn();
        auto t1 = std::chrono::steady_clock::now();
        double us =
            std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
        std::cout << "BENCH " << name << " " << us << " us  (n=" << iters
                  << ")" << std::endl;
        return us;
    }
};

TEST_F(ExpressionImageBenchTest, DISABLED_BenchNativeEngine)
{
    Base::PyGILStateLocker lock;
    const std::string arith = "1 + 2 * 3 - 4 / 5";
    const std::string prop = "Width * 2";

    benchUs("native.parse+eval.arith", 20000, [&] {
        auto e = App::Expression::parse(obj, arith.c_str(), arith.size());
        auto v = e->getValueAsAny();
        (void)v;
    });
    auto cached = App::Expression::parse(obj, arith.c_str(), arith.size());
    benchUs("native.eval.arith.cachedAST", 20000, [&] {
        auto v = cached->getValueAsAny();
        (void)v;
    });
    benchUs("native.parse+eval.prop", 20000, [&] {
        auto e = App::Expression::parse(obj, prop.c_str(), prop.size());
        auto v = e->getValueAsAny();
        (void)v;
    });
    benchUs("native.parse.only.arith", 20000, [&] {
        auto e = App::Expression::parse(obj, arith.c_str(), arith.size());
        (void)e;
    });
}

TEST_F(ExpressionImageBenchTest, DISABLED_BenchImageRoundTrip)
{
    // (a) the wire floor: smallest possible request, no pack, no
    //     in-image expression parse
    auto floorRes = ImageHost::instance().eval("1", {});
    ASSERT_TRUE(floorRes.ok) << floorRes.excType << ": " << floorRes.message;
    benchUs("image.wire.floor", 2000, [&] {
        auto r = ImageHost::instance().eval("1", {});
        (void)r;
    });

    // (b) the full desktop path for arithmetic: host parse + pack +
    //     owner export + CBOR + wasm call + in-image parse + walk
    auto a = ImageHost::instance().evalExpression(obj, "1 + 2 * 3 - 4 / 5");
    ASSERT_TRUE(a.ok) << a.excType << ": " << a.message;
    benchUs("image.evalExpression.arith", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(obj, "1 + 2 * 3 - 4 / 5");
        (void)r;
    });

    // (c) the same with one identifier: adds a host-side property
    //     resolve + marshal into the bindings pack
    auto p = ImageHost::instance().evalExpression(obj, "Width * 2");
    ASSERT_TRUE(p.ok) << p.excType << ": " << p.message;
    benchUs("image.evalExpression.prop", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(obj, "Width * 2");
        (void)r;
    });

    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionImageBenchTest, DISABLED_BenchImageBridgeHop)
{
    // Mid-eval reach-backs (read_prop) on top of a python-lang eval: the
    // cost pack-first evaluation exists to avoid.
    //
    // The pack is built FRESH per iteration.  The guest copies the
    // bindings into per-eval globals and releases the handle when those
    // die, and the host flushes that release on the next call -- so a
    // pack reused across iterations makes every eval after the first a
    // stale-handle error round trip, which is what an earlier version
    // of this bench measured (its "+78-80 us" was that error path).
    // r.ok is asserted inside the loop for the same reason.
    //
    // Three rungs with the same per-iteration pack cost (export, proxy,
    // release): zero hops, one hop, four hops.  The marginal hop is the
    // slope (four - one) / 3; (one - zero) includes the first getattr's
    // proxy setup.
    auto run = [&](const char* name, const char* src) {
        return benchUs(name, 2000, [&] {
            auto r = ImageHost::instance().eval(src, objectBinding("o", obj));
            ASSERT_TRUE(r.ok) << src << ": " << r.excType << ": " << r.message;
        });
    };
    double zero = run("image.eval.pack.noHop", "42.0");
    double one = run("image.eval.pack.oneHop", "o.Width");
    double four = run("image.eval.pack.fourHops",
                      "o.Width + o.Width + o.Width + o.Width");
    std::cout << "BENCH image.eval.hop.marginal " << (four - one) / 3.0
              << " us  first " << (one - zero) << " us" << std::endl;
    benchUs("image.eval.noPack", 2000, [&] {
        auto r = ImageHost::instance().eval("42.0", {});
        (void)r;
    });
    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionImageBenchTest, DISABLED_BenchImageInstantiation)
{
    // Warm instantiation from the .cwasm: the per-principal zygote cost
    // that pooling has to amortize.
    for (int i = 0; i < 3; ++i) {
        ImageHost::instance().reset();
        auto t0 = std::chrono::steady_clock::now();
        auto r = ImageHost::instance().eval("1", {});
        auto t1 = std::chrono::steady_clock::now();
        ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
        std::cout << "BENCH image.instantiate+firstEval "
                  << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << " ms" << std::endl;
    }
}

TEST_F(ExpressionImageBenchTest, DISABLED_BenchImageBreakdown)
{
    // Split the round trip into host-side pack, crossing, and in-image
    // evaluation, using only ops that exist today (no image rebuild).

    // no owner: no parse, no pack, no owner handle -- crossing plus the
    // in-image expression parse of a single literal
    benchUs("image.expr.noOwner.literal", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(nullptr, "1");
        (void)r;
    });
    // same source WITH an owner: adds host parse + owner export + the
    // security scope, and nothing else (a literal has no identifiers)
    benchUs("image.expr.owner.literal", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(obj, "1");
        (void)r;
    });
    // CPython compile+eval of the same literal, same crossing
    benchUs("image.python.literal", 2000, [&] {
        auto r = ImageHost::instance().eval("1", {});
        (void)r;
    });
    benchUs("image.python.arith", 2000, [&] {
        auto r = ImageHost::instance().eval("1 + 2 * 3 - 4 / 5", {});
        (void)r;
    });

    // per-binding marginal cost: five dynamic properties in one
    // expression against one
    for (int i = 0; i < 5; ++i) {
        std::string name = "W" + std::to_string(i);
        auto p = Base::freecad_dynamic_cast<App::PropertyFloat>(
            obj->addDynamicProperty("App::PropertyFloat", name.c_str()));
        ASSERT_NE(p, nullptr);
        p->setValue(double(i + 1));
    }
    benchUs("image.expr.oneBinding", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(obj, "W0 + 1");
        (void)r;
    });
    benchUs("image.expr.fiveBindings", 2000, [&] {
        auto r = ImageHost::instance().evalExpression(
            obj, "W0 + W1 + W2 + W3 + W4");
        (void)r;
    });

    // the host half of the five-binding pack, measured alone (parse +
    // identifier enumeration + resolve + marshal), no crossing
    {
        Base::PyGILStateLocker lock;
        const std::string src = "W0 + W1 + W2 + W3 + W4";
        benchUs("host.pack.fiveBindings", 5000, [&] {
            auto e = App::Expression::parse(obj, src.c_str(), src.size());
            std::map<App::ObjectIdentifier, bool> ids;
            e->getIdentifiers(ids);
            App::ExpressionSandbox::HandleTable table;
            json bindings = json::object();
            for (auto& v : ids) {
                Py::Object value = v.first.getPyValue(true);
                bindings[v.first.toString()] =
                    App::ExpressionSandbox::encodeHostValue(table, value.ptr());
            }
            auto cbor = json::to_cbor(bindings);
            (void)cbor;
            table.clear();
        });
    }

    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionImageBenchTest, DISABLED_BenchTransportFloor)
{
    // An op the image rejects before touching CPython or the parser:
    // this is the pure transport cost (fcx_alloc + fcx_call + two
    // fcx_free wasmtime calls, two memcpys, CBOR both ways).
    json ping;
    ping["op"] = "ping";
    auto req = json::to_cbor(ping);
    std::vector<unsigned char> reqBytes(req.begin(), req.end());
    std::vector<unsigned char> replyBytes;
    ASSERT_TRUE(ImageHost::instance().rawCall(reqBytes, replyBytes));
    json reply = json::from_cbor(replyBytes.begin(), replyBytes.end());
    EXPECT_FALSE(reply.value("ok", true));
    benchUs("image.transport.floor", 5000, [&] {
        std::vector<unsigned char> out;
        ImageHost::instance().rawCall(reqBytes, out);
    });

    // the same request with a kilobyte of payload: the slope of the
    // transport in request size
    ping["pad"] = std::string(1024, 'x');
    auto big = json::to_cbor(ping);
    std::vector<unsigned char> bigBytes(big.begin(), big.end());
    benchUs("image.transport.1kB", 5000, [&] {
        std::vector<unsigned char> out;
        ImageHost::instance().rawCall(bigBytes, out);
    });
}

// ---- the evaluation switch-over (docs/ExpressionImage.md): with the
// ---- preference on, a stored expression evaluates IN the image, and an
// ---- image failure fails the evaluation rather than falling back ----

#include <App/ExpressionEvaluator.h>
#include <App/PropertyExpressionEngine.h>

class ExpressionRoutingTest: public ExpressionImageEvalTest
{
protected:
    void SetUp() override
    {
        ExpressionImageEvalTest::SetUp();
        if (IsSkipped())
            return;
        param = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
        param->SetBool("Evaluate", true);
    }

    void TearDown() override
    {
        if (param)
            param->RemoveBool("Evaluate");
        param = ParameterGrp::handle();
        ExpressionImageEvalTest::TearDown();
    }

    ParameterGrp::handle param;
};

TEST_F(ExpressionRoutingTest, routedEvaluationMatchesNative)
{
    ASSERT_TRUE(App::ExpressionSandbox::evaluationRouted());
    auto expr = App::Expression::parse(obj, "Width * 2");
    ASSERT_NE(expr, nullptr);

    auto routed = App::ExpressionSandbox::evaluate(expr.get());
    EXPECT_DOUBLE_EQ(App::any_cast<double>(routed), 42.0);

    // the same expression evaluated in-process: same type, same value
    auto native = expr->getValueAsAny();
    EXPECT_DOUBLE_EQ(App::any_cast<double>(native),
                     App::any_cast<double>(routed));
}

TEST_F(ExpressionRoutingTest, routingOffKeepsTheNativePath)
{
    param->SetBool("Evaluate", false);
    EXPECT_FALSE(App::ExpressionSandbox::evaluationRouted());
    auto expr = App::Expression::parse(obj, "Width * 2");
    EXPECT_DOUBLE_EQ(
        App::any_cast<double>(App::ExpressionSandbox::evaluate(expr.get())),
        42.0);
}

TEST_F(ExpressionRoutingTest, boundPropertyRecomputesThroughTheImage)
{
    // the real desktop path: PropertyExpressionEngine evaluating a
    // stored binding, with the router in front of it
    auto out = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj->addDynamicProperty("App::PropertyFloat", "Out"));
    ASSERT_NE(out, nullptr);
    App::ObjectIdentifier path(*obj->getPropertyByName("Out"));
    obj->ExpressionEngine.setValue(
        path,
        std::shared_ptr<App::Expression>(
            App::Expression::parse(obj, "Width * 2 + 1").release()));

    std::size_t before = ImageHost::instance().evalCount();
    auto ret = obj->ExpressionEngine.execute();
    ASSERT_EQ(ret, App::DocumentObject::StdReturn) << (ret ? ret->Why : "");
    EXPECT_DOUBLE_EQ(out->getValue(), 43.0);
    // and it really crossed: the value alone cannot tell the paths apart
    EXPECT_GT(ImageHost::instance().evalCount(), before);
}

TEST_F(ExpressionRoutingTest, imageErrorDoesNotFallBackToTheHost)
{
    using App::ExpressionSecurity::PermissionNeededException;

    // A foreign-document reference is denied to the document principal.
    // Routed, the denial happens at pack time INSIDE evalExpression and
    // comes back as an image error, so the router raises a plain
    // Base::Exception -- NOT the native path's
    // PermissionNeededException.  That difference is the proof that the
    // evaluation really crossed and that the failure was not quietly
    // retried in the host.
    auto doc2 = App::GetApplication().newDocument("FcxRouteOther", "testUser");
    auto obj2 = doc2->addObject("App::FeaturePython", "Remote");
    auto depth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj2->addDynamicProperty("App::PropertyFloat", "Depth"));
    ASSERT_NE(depth, nullptr);
    depth->setValue(15.0);

    const std::string src = std::string(doc2->getName()) + "#Remote.Depth * 2";
    auto expr = App::Expression::parse(obj, src.c_str(), src.size());
    ASSERT_NE(expr, nullptr);

    EXPECT_THROW(App::ExpressionSandbox::evaluate(expr.get()),
                 Base::RuntimeError);
    EXPECT_THROW(expr->getValueAsAny(), PermissionNeededException);

    // and once granted, the routed evaluation produces the value
    using App::ExpressionSecurity::Permission;
    using App::ExpressionSecurity::Runtime;
    std::string principal = Runtime::instance().documentPrincipal(doc);
    Runtime::instance().grant(principal, Permission::DocForeign,
                              doc2->getName(), true, "session");
    EXPECT_DOUBLE_EQ(
        App::any_cast<double>(App::ExpressionSandbox::evaluate(expr.get())),
        30.0);
    Runtime::instance().revoke(principal, Permission::DocForeign,
                               doc2->getName());
    Runtime::instance().clearPending(principal, Permission::DocForeign,
                                     doc2->getName());
    App::GetApplication().closeDocument(doc2->getName());
}

// ---- the eval options cross with the request (switch-over slice B):
// ---- the spreadsheet evaluates every cell with OptionCallFrame, and
// ---- with OptionPythonMode too when the sheet is in python mode ----

TEST_F(ExpressionRoutingTest, pythonModeCrossesIntoTheImage)
{
    // `hex` is not an expression function: it resolves only because a
    // python-mode call frame makes CPython's builtins visible.  So the
    // value is proof that BOTH options reached the image -- python mode
    // is a flag ON the call frame, and without OptionCallFrame there is
    // no frame to set it on.
    const int opts = App::Expression::OptionCallFrame
            | App::Expression::OptionPythonMode;
    auto expr = App::Expression::parse(obj, "hex(255)", 0, false, true);
    ASSERT_NE(expr, nullptr);

    std::size_t before = ImageHost::instance().evalCount();
    auto routed = App::ExpressionSandbox::evaluate(expr.get(), opts);
    EXPECT_GT(ImageHost::instance().evalCount(), before);
    EXPECT_EQ(App::any_cast<std::string>(routed), std::string("0xff"));

    // and the native engine agrees, which is the parity claim
    EXPECT_EQ(App::any_cast<std::string>(expr->getValueAsAny(opts)),
              std::string("0xff"));
}

TEST_F(ExpressionRoutingTest, pythonModeReachesTheImagesParserToo)
{
    // A comma-separated list is python-mode SYNTAX -- the lexer starts
    // in a different state.  The image re-parses the source it is sent,
    // so the mode has to reach the parse and not just the walk.
    const int opts = App::Expression::OptionCallFrame
            | App::Expression::OptionPythonMode;
    auto expr = App::Expression::parse(obj, "sorted([3, 1, 2])", 0, false, true);
    ASSERT_NE(expr, nullptr);

    Base::PyGILStateLocker lock;
    Py::Object routed(App::ExpressionSandbox::evaluatePy(expr.get(), opts), true);
    ASSERT_TRUE(PyList_Check(routed.ptr()));
    ASSERT_EQ(PyList_GET_SIZE(routed.ptr()), 3);
    EXPECT_EQ(PyLong_AsLong(PyList_GET_ITEM(routed.ptr(), 0)), 1);
    EXPECT_EQ(PyLong_AsLong(PyList_GET_ITEM(routed.ptr(), 2)), 3);
}

TEST_F(ExpressionRoutingTest, withoutPythonModeTheImageRefusesTheSameWay)
{
    // The negative half: the identical source WITHOUT the mode fails in
    // the image exactly as it fails natively, rather than succeeding
    // because the image happened to be more permissive.
    auto expr = App::Expression::parse(obj, "hex(255)");
    ASSERT_NE(expr, nullptr);
    EXPECT_THROW(App::ExpressionSandbox::evaluate(
                     expr.get(), App::Expression::OptionCallFrame),
                 Base::Exception);
    EXPECT_THROW(expr->getValueAsAny(App::Expression::OptionCallFrame),
                 Base::Exception);
}

// ---- error-TEXT parity for a reference into a foreign document.  The
// ---- image has no foreign documents at all, so left to itself it
// ---- always answers "Document 'X' not found" -- the wrong reason
// ---- whenever X exists on the host and the property inside it did
// ---- not.  The corpus gate's only two non-`same` rows were this. ----

TEST_F(ExpressionRoutingTest, foreignDocErrorTextMatchesNative)
{
    using App::ExpressionSecurity::Permission;
    using App::ExpressionSecurity::Runtime;

    auto doc2 = App::GetApplication().newDocument("FcxErrOther", "testUser");
    auto obj2 = doc2->addObject("App::FeaturePython", "Pad");
    auto depth = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj2->addDynamicProperty("App::PropertyFloat", "Depth"));
    ASSERT_NE(depth, nullptr);
    depth->setValue(3.0);

    // the foreign reference itself has to be permitted, or the wall the
    // test hits is the policy one and it proves nothing about text
    std::string principal = Runtime::instance().documentPrincipal(doc);
    Runtime::instance().grant(principal, Permission::DocForeign,
                              doc2->getName(), true, "session");

    const std::string base = std::string(doc2->getName()) + "#";
    struct Case { const char* what; std::string src; };
    const Case cases[] = {
        {"missing property", base + "Pad.Configuration + 1"},
        {"missing object", base + "Nope.Depth + 1"},
        // a document that does not exist HERE either: both engines
        // already agreed, and must keep agreeing
        {"missing document", std::string("FcxNoSuchDoc#Pad.Depth + 1")},
    };

    for (const auto& c : cases) {
        auto expr = App::Expression::parse(obj, c.src.c_str(), c.src.size());
        ASSERT_NE(expr, nullptr) << c.what;

        std::string nativeMsg;
        try {
            expr->getValueAsAny(App::Expression::OptionCallFrame);
            FAIL() << c.what << ": native evaluation unexpectedly succeeded";
        }
        catch (Base::Exception& e) {
            nativeMsg = e.what();
        }

        std::string routedMsg;
        try {
            App::ExpressionSandbox::evaluate(expr.get(),
                                             App::Expression::OptionCallFrame);
            FAIL() << c.what << ": routed evaluation unexpectedly succeeded";
        }
        catch (Base::Exception& e) {
            routedMsg = e.what();
        }
        EXPECT_EQ(nativeMsg, routedMsg) << c.what;
    }

    Runtime::instance().revoke(principal, Permission::DocForeign,
                               doc2->getName());
    Runtime::instance().clearPending(principal, Permission::DocForeign,
                                     doc2->getName());
    App::GetApplication().closeDocument(doc2->getName());
}

TEST_F(ExpressionRoutingTest, unresolvableLocalNameIsNotShippedAsAnError)
{
    // The narrow scope of the fix above matters: a name the host cannot
    // resolve may be a variable BOUND DURING the evaluation.  Ship a
    // negative pack entry for `a` and this stops working.
    const char* src = "a = Width + 1; a * 2";
    auto expr = App::Expression::parse(obj, src);
    ASSERT_NE(expr, nullptr);
    EXPECT_DOUBLE_EQ(
        App::any_cast<double>(App::ExpressionSandbox::evaluate(
            expr.get(), App::Expression::OptionCallFrame)),
        44.0);
}

// ---- wire type identity: a tuple is not a list (the corpus gate found
// ---- this -- an Enum property fed from a cell range came back as a
// ---- list, changing the stored value's type) ----

TEST_F(ExpressionImageEvalTest, tupleCrossesBackAsTuple)
{
    Base::PyGILStateLocker lock;
    auto res = ImageHost::instance().eval("(1, 2, 3)", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    PyObject* v = ImageHost::instance().decodeResult(res);
    ASSERT_NE(v, nullptr);
    EXPECT_TRUE(PyTuple_Check(v));
    EXPECT_EQ(PyTuple_GET_SIZE(v), 3);
    Py_DECREF(v);

    // and a list is still a list
    auto res2 = ImageHost::instance().eval("[1, 2, 3]", {});
    ASSERT_TRUE(res2.ok) << res2.excType << ": " << res2.message;
    PyObject* v2 = ImageHost::instance().decodeResult(res2);
    ASSERT_NE(v2, nullptr);
    EXPECT_TRUE(PyList_Check(v2));
    Py_DECREF(v2);
}

TEST_F(ExpressionImageEvalTest, tupleCrossesIntoTheImageAsTuple)
{
    Base::PyGILStateLocker lock;
    App::ExpressionSandbox::HandleTable table;
    PyObject* t = Py_BuildValue("(iii)", 1, 2, 3);
    ASSERT_NE(t, nullptr);
    json b;
    b["t"] = App::ExpressionSandbox::encodeHostValue(table, t);
    Py_DECREF(t);
    auto cbor = json::to_cbor(b);
    auto res = ImageHost::instance().eval(
        "t.__class__.__name__", {cbor.begin(), cbor.end()});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(json::from_cbor(res.value.begin(), res.value.end())
                  .get<std::string>(), "tuple");
    table.clear();
}

// ---- rung 2 (docs/Sandbox.md 7.6, G1c): a scripted object's Proxy
// ---- lives in the guest; the host's Proxy property holds a stand-in
// ---- whose hooks cross, and a hook's write runs the host's onChanged,
// ---- whose hook crosses again -- a round trip inside a round trip ----

#include <algorithm>
#include <initializer_list>

#include <App/ExpressionGuestProxy.h>
#include <App/PropertyPythonObject.h>
#include <Base/Writer.h>

TEST_F(ExpressionImageEvalTest, guestProxyHooksNest)
{
    auto& host = ImageHost::instance();
    // A scripted-object class pushed into the guest: __init__ installs
    // itself as the Proxy, execute() writes a property (write_prop),
    // onChanged records what changed -- state on the instance, in the
    // guest, exactly as a Draft object keeps its props_changed list.
    auto r = host.exec(
        "class Probe:\n"
        "    def __init__(self, obj):\n"
        "        self.log = []\n"
        "        obj.Proxy = self\n"
        "    def execute(self, obj):\n"
        "        self.log.append('execute')\n"
        "        obj.Width = obj.Width * 2\n"
        "        self.log.append('after')\n"
        "    def onChanged(self, obj, prop):\n"
        "        self.log.append('changed:' + prop)\n"
        "    def dumps(self):\n"
        "        return {'log': list(self.log)}\n"
        "    def loads(self, state):\n"
        "        self.log = list(state['log'])\n",
        "fcxprobe");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    host.resetStats();

    auto* proxyProp = Base::freecad_dynamic_cast<App::PropertyPythonObject>(
        obj->getPropertyByName("Proxy"));
    ASSERT_NE(proxyProp, nullptr);
    PyObject* standIn = nullptr;
    {
        Base::PyGILStateLocker lock;
        PyObject* py = obj->getPyObject();
        PyObject* args = Py_BuildValue("(O)", py);
        Py_DECREF(py);
        auto n = host.proxyNew("fcxprobe", "Probe", args, false, obj);
        Py_DECREF(args);
        ASSERT_TRUE(n.ok) << n.excType << ": " << n.message;
        standIn = host.decodeResult(n);
        ASSERT_NE(standIn, nullptr);
        ASSERT_TRUE(App::ExpressionSandbox::isGuestProxy(standIn));
        // one stand-in per guest proxy: the Proxy property holds this one
        EXPECT_EQ(proxyProp->getValue().ptr(), standIn);
        // and it reads as the guest class, for PropertyPythonObject::Save
        PyObject* mod = PyObject_GetAttrString(standIn, "__module__");
        ASSERT_NE(mod, nullptr);
        EXPECT_STREQ(PyUnicode_AsUTF8(mod), "fcxprobe");
        Py_DECREF(mod);
        EXPECT_STREQ(Py_TYPE(standIn)->tp_name, "Probe");
        // exactly the hooks the class defines, plus dumps/loads
        EXPECT_TRUE(PyObject_HasAttrString(standIn, "execute"));
        EXPECT_TRUE(PyObject_HasAttrString(standIn, "onChanged"));
        EXPECT_TRUE(PyObject_HasAttrString(standIn, "dumps"));
        EXPECT_FALSE(PyObject_HasAttrString(standIn, "onDocumentRestored"));
        EXPECT_FALSE(PyObject_HasAttrString(standIn, "mustExecute"));
    }

    // The recompute: FeaturePythonT::execute -> stand-in -> guest
    // execute() -> write_prop Width -> host onChanged -> stand-in ->
    // guest onChanged, INSIDE the outer round trip.
    auto width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    width->setValue(10.5);  // a real change: onChanged('Width') crosses on its own
    obj->touch();
    doc->recompute();
    EXPECT_FALSE(obj->isError());
    EXPECT_DOUBLE_EQ(width->getValue(), 21.0);

    // the guest's state through dumps(), as Save would read it: the
    // Proxy install, the property write, then execute's own write
    // nested between its two marks
    std::string state;
    {
        Base::PyGILStateLocker lock;
        state = proxyProp->toString();
    }
    json j = json::parse(state);
    ASSERT_TRUE(j.contains("log")) << state;
    std::vector<std::string> log = j["log"];
    std::vector<std::string> want = {"changed:Proxy", "changed:Width", "execute",
                                     "changed:Width", "after"};
    EXPECT_EQ(log, want);
    auto st = host.stats();
    EXPECT_GE(st.proxyCalls, 5u);  // new, 2x onChanged, execute, nested onChanged, dumps
    EXPECT_GE(st.ops["write_prop"], 2u);  // Proxy, Width

    // loads() through the stand-in restores guest state
    {
        Base::PyGILStateLocker lock;
        proxyProp->fromString("{\"log\": [\"restored\"]}");
        state = proxyProp->toString();
    }
    EXPECT_EQ(json::parse(state)["log"], json::array({"restored"}));

    {
        Base::PyGILStateLocker lock;
        Py_DECREF(standIn);
    }
}

TEST_F(ExpressionImageEvalTest, draftWireInGuest)
{
    // The G1c gate: a Draft Wire whose Proxy is constructed in the
    // guest from the bundled wheel, against one made natively from the
    // same points -- BRep byte-identical, the properties equal, the
    // saved <Python> element equal.
    auto& host = ImageHost::instance();
    if (host.runtime() != "pyodide")
        GTEST_SKIP() << "bundled wheels load on the pyodide runtime only";
    {
        namespace fs = std::filesystem;
        bool bundled = false;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(
                 App::Application::getResourceDir() + "Pyodide/wheels", ec)) {
            const std::string fn = e.path().filename().string();
            bundled = bundled || (fn.rfind("fcx_draft-", 0) == 0 && fn.find("-py3-none-any.whl") != std::string::npos);
        }
        if (!bundled)
            GTEST_SKIP() << "no fcx_draft wheel bundled under " << App::Application::getResourceDir()
                         << "Pyodide/wheels";
    }
    {
        Base::PyGILStateLocker lock;
        PyObject* draft = PyImport_ImportModule("Draft");
        if (!draft) {
            PyErr_Clear();
            GTEST_SKIP() << "Draft is not importable on the host in this test binary";
        }
        Py_DECREF(draft);
    }
    // Wire.__init__ reads a Draft preference through the draftutils.params
    // facade: prefs.read, ALLOW for a document principal (decision 2 of
    // docs/Sandbox.md 7.6), so the guest construction below needs no grant.
    App::GetApplication().setActiveDocument(doc);

    // 1. the native reference
    ASSERT_TRUE(hostModule(
        "fcxwire",
        "import FreeCAD, Draft\n"
        "V = FreeCAD.Vector\n"
        "PTS = [V(0, 0, 0), V(2, 0, 0), V(2, 2, 0)]\n"
        "native = Draft.make_wire(PTS)\n"
        "FreeCAD.ActiveDocument.recompute()\n"
        "def obj(name):\n"
        "    return FreeCAD.ActiveDocument.getObject(name)\n"
        "def brep(o):\n"
        "    return o.Shape.exportBrepToString()\n"
        "def facts(o):\n"
        "    return repr((o.Points, o.Start, o.End, o.Length, o.Area, o.Closed, o.MakeFace,\n"
        "                 type(o.Proxy).__module__, type(o.Proxy).__name__, o.Proxy.dumps()))\n"));
    std::string nativeName;
    ASSERT_TRUE(hostEvalStr("__import__('fcxwire').native.Name", nativeName));
    App::DocumentObject* native = doc->getObject(nativeName.c_str());
    ASSERT_NE(native, nullptr);
    EXPECT_FALSE(native->isError());

    // 2. the twin: the same C++ type and extension, the Proxy IN THE GUEST
    App::DocumentObject* twin = doc->addObject("Part::FeaturePython", "WireGuest");
    ASSERT_NE(twin, nullptr);
    std::string ignored;
    ASSERT_TRUE(hostEvalStr("str(__import__('fcxwire').obj('WireGuest')"
                            ".addExtension('Part::AttachExtensionPython'))", ignored));
    host.resetStats();
    {
        Base::PyGILStateLocker lock;
        PyObject* py = twin->getPyObject();
        PyObject* args = Py_BuildValue("(O)", py);
        Py_DECREF(py);
        auto n = host.proxyNew("draftobjects.wire", "Wire", args, false, twin);
        Py_DECREF(args);
        ASSERT_TRUE(n.ok) << n.excType << ": " << n.message;
        PyObject* standIn = host.decodeResult(n);
        ASSERT_NE(standIn, nullptr);
        EXPECT_TRUE(App::ExpressionSandbox::isGuestProxy(standIn));
        Py_DECREF(standIn);
    }
    // the writes make_wire does natively, each an onChanged crossing
    ASSERT_TRUE(hostModule(
        "fcxwire2",
        "import FreeCAD, fcxwire\n"
        "o = fcxwire.obj('WireGuest')\n"
        "o.Points = fcxwire.PTS\n"
        "o.Closed = False\n"
        "o.AttachmentSupport = None\n"
        "FreeCAD.ActiveDocument.recompute()\n"));
    EXPECT_FALSE(twin->isError());
    auto st = host.stats();
    std::cout << "Draft Wire in the guest: proxy calls " << st.proxyCalls << "; bridge ops:";
    for (const auto& [k, v] : st.ops)
        std::cout << " " << k << "=" << v;
    std::cout << std::endl;
    EXPECT_GE(st.ops["write_prop"], 1u) << "the Shape must arrive through write_prop";

    // 3. byte-identical
    std::string brepNative, brepGuest, factsNative, factsGuest;
    ASSERT_TRUE(hostEvalStr("__import__('fcxwire').brep(__import__('fcxwire').native)", brepNative));
    ASSERT_TRUE(hostEvalStr("__import__('fcxwire').brep(__import__('fcxwire').obj('WireGuest'))", brepGuest));
    EXPECT_FALSE(brepNative.empty());
    EXPECT_EQ(brepNative, brepGuest);
    ASSERT_TRUE(hostEvalStr("__import__('fcxwire').facts(__import__('fcxwire').native)", factsNative));
    ASSERT_TRUE(hostEvalStr("__import__('fcxwire').facts(__import__('fcxwire').obj('WireGuest'))", factsGuest));
    EXPECT_EQ(factsNative, factsGuest);

    // 4. the saved <Python> element: module, class and state, the same
    {
        Base::PyGILStateLocker lock;
        App::Property* pn = native->getPropertyByName("Proxy");
        App::Property* pg = twin->getPropertyByName("Proxy");
        ASSERT_NE(pn, nullptr);
        ASSERT_NE(pg, nullptr);
        Base::StringWriter wn, wg;
        wn.setForceXML(true);
        wg.setForceXML(true);
        pn->Save(wn);
        pg->Save(wg);
        EXPECT_EQ(wn.getString(), wg.getString());
        EXPECT_NE(wg.getString().find("module=\"draftobjects.wire\" class=\"Wire\""), std::string::npos)
            << wg.getString();
    }

    {
        Base::PyGILStateLocker lock;
        PyObject* d = PyImport_GetModuleDict();
        PyDict_DelItemString(d, "fcxwire2");
        PyDict_DelItemString(d, "fcxwire");
        if (PyErr_Occurred())
            PyErr_Clear();
    }
}

// ---- G1c step (f): the Restore route.  With routing ON a saved
// ---- <Python module=".." class=".."> is allocated in the GUEST
// ---- (proxy_new alloc) and the property holds the stand-in; a module
// ---- the guest cannot serve fails CLOSED; with routing OFF the file
// ---- restores natively as it always has ----

namespace
{

const char* const ProbeSource =
    "class Probe:\n"
    "    def __init__(self, obj):\n"
    "        self.log = []\n"
    "        obj.Proxy = self\n"
    "    def execute(self, obj):\n"
    "        self.log.append('execute')\n"
    "        obj.Width = obj.Width * 2\n"
    "        self.log.append('after')\n"
    "    def onChanged(self, obj, prop):\n"
    "        self.log.append('changed:' + prop)\n"
    "    def dumps(self):\n"
    "        return {'log': list(self.log)}\n"
    "    def loads(self, state):\n"
    "        self.log = list(state['log'])\n";

/// The Proxy property's value as a borrowed pointer (nullptr when the
/// object has none).
PyObject* proxyOf(App::DocumentObject* o)
{
    auto* p = Base::freecad_dynamic_cast<App::PropertyPythonObject>(o->getPropertyByName("Proxy"));
    return p ? p->getValue().ptr() : nullptr;
}

/// `type(o.Proxy).__module__` on the host, "" when there is no Proxy
/// or it is None.
std::string proxyModuleOf(App::DocumentObject* o)
{
    Base::PyGILStateLocker lock;
    PyObject* proxy = proxyOf(o);
    if (!proxy || proxy == Py_None)
        return std::string();
    PyObject* mod = PyObject_GetAttrString(proxy, "__module__");
    if (!mod) {
        PyErr_Clear();
        return std::string();
    }
    std::string s = PyUnicode_Check(mod) ? PyUnicode_AsUTF8(mod) : "";
    Py_DECREF(mod);
    return s;
}

void dropHostModules(std::initializer_list<const char*> names)
{
    Base::PyGILStateLocker lock;
    PyObject* d = PyImport_GetModuleDict();
    for (const char* n : names)
        PyDict_DelItemString(d, n);
    if (PyErr_Occurred())
        PyErr_Clear();
}

}  // namespace

TEST_F(ExpressionImageEvalTest, guestProxyRestoreRoute)
{
    auto& host = ImageHost::instance();
    // the same class on both sides: pushed into the guest (what the
    // routed restore imports) and registered on the host (what the
    // native restore imports)
    auto r = host.exec(ProbeSource, "fcxprobe");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    ASSERT_TRUE(hostModule("fcxprobe", ProbeSource));
    // and one the host alone has: a document naming it must NOT get it
    // through the routed restore
    ASSERT_TRUE(hostModule("fcxhostonly",
                           "class Only:\n"
                           "    def __init__(self, obj=None):\n"
                           "        if obj is not None:\n"
                           "            obj.Proxy = self\n"
                           "    def execute(self, obj):\n"
                           "        obj.Width = obj.Width + 1\n"
                           "    def dumps(self):\n"
                           "        return None\n"
                           "    def loads(self, state):\n"
                           "        pass\n"));
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    const std::string path = std::string(std::tmpnam(nullptr)) + "-fcxrestore.FCStd";
    struct Cleanup
    {
        ParameterGrp::handle param;
        std::string path;
        ~Cleanup()
        {
            param->RemoveBool("Evaluate");
            std::remove(path.c_str());
            dropHostModules({"fcxprobe", "fcxhostonly"});
        }
    } cleanup {param, path};

    // 1. a guest Proxy on Obj, recomputed once; a host Proxy on Obj2
    {
        Base::PyGILStateLocker lock;
        PyObject* py = obj->getPyObject();
        PyObject* args = Py_BuildValue("(O)", py);
        Py_DECREF(py);
        auto n = host.proxyNew("fcxprobe", "Probe", args, false, obj);
        Py_DECREF(args);
        ASSERT_TRUE(n.ok) << n.excType << ": " << n.message;
        PyObject* standIn = host.decodeResult(n);
        ASSERT_NE(standIn, nullptr);
        Py_DECREF(standIn);
    }
    auto width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    width->setValue(10.5);
    obj->touch();
    doc->recompute();
    ASSERT_FALSE(obj->isError());
    EXPECT_DOUBLE_EQ(width->getValue(), 21.0);
    App::DocumentObject* obj2 = doc->addObject("App::FeaturePython", "Obj2");
    ASSERT_NE(obj2, nullptr);
    {
        Base::PyGILStateLocker lock;
        PyObject* py = obj2->getPyObject();
        PyObject* ns = Py_BuildValue("{s:O}", "o", py);
        Py_DECREF(py);
        PyDict_SetItemString(ns, "__builtins__", PyEval_GetBuiltins());
        PyObject* res = PyRun_String("__import__('fcxhostonly').Only(o)", Py_eval_input, ns, ns);
        Py_DECREF(ns);
        ASSERT_NE(res, nullptr);
        Py_DECREF(res);
    }
    EXPECT_EQ(proxyModuleOf(obj2), "fcxhostonly");

    // 2. save, reopen with routing ON
    param->SetBool("Evaluate", true);
    ASSERT_TRUE(App::ExpressionSandbox::proxyRestoreRouted());
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(doc->getName());
    doc = nullptr;
    obj = nullptr;
    host.resetStats();
    doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(doc, nullptr);
    obj = doc->getObject("Obj");
    ASSERT_NE(obj, nullptr);
    obj2 = doc->getObject("Obj2");
    ASSERT_NE(obj2, nullptr);
    {
        Base::PyGILStateLocker lock;
        PyObject* proxy = proxyOf(obj);
        ASSERT_NE(proxy, nullptr);
        EXPECT_TRUE(App::ExpressionSandbox::isGuestProxy(proxy))
            << "with routing on the restored Proxy must be a stand-in";
        // the host-only module was NOT imported for the document
        PyObject* proxy2 = proxyOf(obj2);
        EXPECT_TRUE(proxy2 == nullptr || proxy2 == Py_None)
            << "a Proxy module the guest cannot serve must fail closed";
    }
    EXPECT_EQ(proxyModuleOf(obj), "fcxprobe");
    EXPECT_EQ(proxyModuleOf(obj2), "");
    // the saved state went through the stand-in's loads(): the guest
    // instance carries the pre-save log
    {
        auto* p = Base::freecad_dynamic_cast<App::PropertyPythonObject>(obj->getPropertyByName("Proxy"));
        ASSERT_NE(p, nullptr);
        std::string state;
        {
            Base::PyGILStateLocker lock;
            state = p->toString();
        }
        json j = json::parse(state);
        ASSERT_TRUE(j.contains("log")) << state;
        std::vector<std::string> log = j["log"];
        std::vector<std::string> want = {"execute", "changed:Width", "after"};
        EXPECT_NE(std::search(log.begin(), log.end(), want.begin(), want.end()), log.end())
            << state;
    }
    // and the restored object recomputes through the guest
    width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    EXPECT_DOUBLE_EQ(width->getValue(), 21.0);
    width->setValue(5.0);
    obj->touch();
    doc->recompute();
    EXPECT_FALSE(obj->isError());
    EXPECT_DOUBLE_EQ(width->getValue(), 10.0);
    EXPECT_GE(host.stats().proxyCalls, 2u);  // alloc, loads, execute, ...

    // 3. the same file with routing OFF: native, as it always was
    param->SetBool("Evaluate", false);
    App::GetApplication().closeDocument(doc->getName());
    doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(doc, nullptr);
    obj = doc->getObject("Obj");
    obj2 = doc->getObject("Obj2");
    ASSERT_NE(obj, nullptr);
    ASSERT_NE(obj2, nullptr);
    {
        Base::PyGILStateLocker lock;
        EXPECT_FALSE(App::ExpressionSandbox::isGuestProxy(proxyOf(obj)));
    }
    EXPECT_EQ(proxyModuleOf(obj), "fcxprobe");
    EXPECT_EQ(proxyModuleOf(obj2), "fcxhostonly");
    width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    width->setValue(5.0);
    obj->touch();
    doc->recompute();
    EXPECT_FALSE(obj->isError());
    EXPECT_DOUBLE_EQ(width->getValue(), 10.0);
}

TEST_F(ExpressionImageEvalTest, draftWireRestoreInGuest)
{
    // The step (f) gate on a real object: a Draft Wire whose Proxy was
    // constructed in the guest, saved, reopened with routing ON -> the
    // Proxy is a stand-in again and the recompute is BRep byte-identical
    // to the pre-save shape; reopened with routing OFF -> a native
    // draftobjects.wire.Wire, the same shape.
    auto& host = ImageHost::instance();
    if (host.runtime() != "pyodide")
        GTEST_SKIP() << "bundled wheels load on the pyodide runtime only";
    {
        namespace fs = std::filesystem;
        bool bundled = false;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(
                 App::Application::getResourceDir() + "Pyodide/wheels", ec)) {
            const std::string fn = e.path().filename().string();
            bundled = bundled || (fn.rfind("fcx_draft-", 0) == 0 && fn.find("-py3-none-any.whl") != std::string::npos);
        }
        if (!bundled)
            GTEST_SKIP() << "no fcx_draft wheel bundled under " << App::Application::getResourceDir()
                         << "Pyodide/wheels";
    }
    {
        Base::PyGILStateLocker lock;
        PyObject* draft = PyImport_ImportModule("Draft");
        if (!draft) {
            PyErr_Clear();
            GTEST_SKIP() << "Draft is not importable on the host in this test binary";
        }
        Py_DECREF(draft);
    }
    App::GetApplication().setActiveDocument(doc);
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    const std::string path = std::string(std::tmpnam(nullptr)) + "-fcxwire.FCStd";
    struct Cleanup
    {
        ParameterGrp::handle param;
        std::string path;
        ~Cleanup()
        {
            param->RemoveBool("Evaluate");
            std::remove(path.c_str());
            dropHostModules({"fcxwirer"});
        }
    } cleanup {param, path};

    ASSERT_TRUE(hostModule(
        "fcxwirer",
        "import FreeCAD\n"
        "V = FreeCAD.Vector\n"
        "PTS = [V(0, 0, 0), V(2, 0, 0), V(2, 2, 0)]\n"
        "def obj(name):\n"
        "    return FreeCAD.ActiveDocument.getObject(name)\n"
        "def brep(o):\n"
        "    return o.Shape.exportBrepToString()\n"
        "def facts(o):\n"
        "    return repr((o.Points, o.Start, o.End, o.Length, o.Area, o.Closed, o.MakeFace,\n"
        "                 type(o.Proxy).__module__, type(o.Proxy).__name__, o.Proxy.dumps()))\n"
        "def setup(name):\n"
        "    o = obj(name)\n"
        "    o.addExtension('Part::AttachExtensionPython')\n"
        "def feed(name):\n"
        "    o = obj(name)\n"
        "    o.Points = PTS\n"
        "    o.Closed = False\n"
        "    o.AttachmentSupport = None\n"
        "    FreeCAD.ActiveDocument.recompute()\n"));

    // 1. the Wire with its Proxy in the guest, recomputed
    App::DocumentObject* twin = doc->addObject("Part::FeaturePython", "WireGuest");
    ASSERT_NE(twin, nullptr);
    std::string ignored;
    ASSERT_TRUE(hostEvalStr("str(__import__('fcxwirer').setup('WireGuest'))", ignored));
    {
        Base::PyGILStateLocker lock;
        PyObject* py = twin->getPyObject();
        PyObject* args = Py_BuildValue("(O)", py);
        Py_DECREF(py);
        auto n = host.proxyNew("draftobjects.wire", "Wire", args, false, twin);
        Py_DECREF(args);
        ASSERT_TRUE(n.ok) << n.excType << ": " << n.message;
        PyObject* standIn = host.decodeResult(n);
        ASSERT_NE(standIn, nullptr);
        Py_DECREF(standIn);
    }
    ASSERT_TRUE(hostEvalStr("str(__import__('fcxwirer').feed('WireGuest'))", ignored));
    ASSERT_FALSE(twin->isError());
    std::string brepBefore, factsBefore;
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').brep(__import__('fcxwirer').obj('WireGuest'))", brepBefore));
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').facts(__import__('fcxwirer').obj('WireGuest'))", factsBefore));
    ASSERT_FALSE(brepBefore.empty());

    // 2. reopen with routing ON: a stand-in, recomputed byte-identical
    param->SetBool("Evaluate", true);
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(doc->getName());
    doc = nullptr;
    obj = nullptr;
    host.resetStats();
    doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(doc, nullptr);
    App::GetApplication().setActiveDocument(doc);
    twin = doc->getObject("WireGuest");
    ASSERT_NE(twin, nullptr);
    {
        Base::PyGILStateLocker lock;
        PyObject* proxy = proxyOf(twin);
        ASSERT_NE(proxy, nullptr);
        EXPECT_TRUE(App::ExpressionSandbox::isGuestProxy(proxy));
    }
    EXPECT_EQ(proxyModuleOf(twin), "draftobjects.wire");
    std::string brepAfter, factsAfter;
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').facts(__import__('fcxwirer').obj('WireGuest'))", factsAfter));
    EXPECT_EQ(factsBefore, factsAfter);  // the state came back through loads()
    twin->touch();
    doc->recompute();
    EXPECT_FALSE(twin->isError());
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').brep(__import__('fcxwirer').obj('WireGuest'))", brepAfter));
    EXPECT_EQ(brepBefore, brepAfter);
    {
        auto st = host.stats();
        std::cout << "Draft Wire restored into the guest: proxy calls " << st.proxyCalls
                  << "; bridge ops:";
        for (const auto& [k, v] : st.ops)
            std::cout << " " << k << "=" << v;
        std::cout << std::endl;
        EXPECT_GE(st.proxyCalls, 3u);  // alloc, loads, onDocumentRestored, execute, ...
    }

    // 3. reopen with routing OFF: native, the same shape
    param->SetBool("Evaluate", false);
    App::GetApplication().closeDocument(doc->getName());
    doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(doc, nullptr);
    App::GetApplication().setActiveDocument(doc);
    twin = doc->getObject("WireGuest");
    ASSERT_NE(twin, nullptr);
    {
        Base::PyGILStateLocker lock;
        EXPECT_FALSE(App::ExpressionSandbox::isGuestProxy(proxyOf(twin)));
    }
    EXPECT_EQ(proxyModuleOf(twin), "draftobjects.wire");
    twin->touch();
    doc->recompute();
    EXPECT_FALSE(twin->isError());
    std::string brepNative, factsNative;
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').brep(__import__('fcxwirer').obj('WireGuest'))", brepNative));
    ASSERT_TRUE(hostEvalStr("__import__('fcxwirer').facts(__import__('fcxwirer').obj('WireGuest'))", factsNative));
    EXPECT_EQ(brepBefore, brepNative);
    EXPECT_EQ(factsBefore, factsNative);
}

// ---- G1d: host reads and writes of a guest Proxy's attributes
// ---- (proxy_get / proxy_set through the stand-in's getattr and
// ---- setattr), and the construction dispatch: a class's __new__ ->
// ---- proxyConstruct builds the instance in the guest ----

TEST_F(ExpressionImageEvalTest, guestProxyAttrsAndConstruct)
{
    auto& host = ImageHost::instance();
    // the same class on both sides, with the __new__ Draft's roots
    // have: in the guest FreeCAD has no ExpressionSandbox, so the
    // class allocates natively there; on the host it constructs in the
    // guest when the session routes
    const std::string source =
        "import FreeCAD as App\n"
        "class Probe:\n"
        "    kind = 'probe'\n"
        "    def __new__(cls, *args, **kwargs):\n"
        "        sandbox = getattr(App, 'ExpressionSandbox', None)\n"
        "        if sandbox is not None:\n"
        "            inst = sandbox.proxyConstruct(cls, *args, **kwargs)\n"
        "            if inst is not None:\n"
        "                return inst\n"
        "        return object.__new__(cls)\n"
        "    def __init__(self, obj, tp='Probe'):\n"
        "        obj.Proxy = self\n"
        "        self.Type = tp\n"
        "    def execute(self, obj):\n"
        "        obj.Width = obj.Width + 1\n"
        "    def describe(self, obj):\n"
        "        return '%s:%s' % (self.Type, obj.Name)\n"
        "    def dumps(self):\n"
        "        return self.Type\n"
        "    def loads(self, state):\n"
        "        self.Type = state\n";
    auto r = host.exec(source, "fcxattrs");
    ASSERT_TRUE(r.ok) << r.excType << ": " << r.message;
    ASSERT_TRUE(hostModule("fcxattrs", source));
    ASSERT_TRUE(hostModule(
        "fcxattrsrig",
        "import FreeCAD as App\n"
        "from fcxattrs import Probe\n"
        "S = App.ExpressionSandbox\n"
        "def native(o):\n"
        "    p = Probe(o, tp='Native')\n"
        "    return '%s %s %s' % (S.proxyInfo(p) is None, p is o.Proxy, p.Type)\n"
        "def routed(o):\n"
        "    p = Probe(o, tp='Routed')\n"
        "    out = [S.proxyInfo(p) is not None and p is o.Proxy]\n"
        "    out.append(p.Type)\n"
        "    out.append(p.kind)\n"
        "    out.append(hasattr(p, 'nothing'))\n"
        "    out.append(hasattr(p, 'mustExecute'))\n"
        "    out.append(p.describe(o))\n"
        "    p.Type = 'Changed'\n"
        "    out.append(p.Type)\n"
        "    try:\n"
        "        p.Type = o\n"
        "        out.append('stored a handle')\n"
        "    except TypeError:\n"
        "        out.append('TypeError')\n"
        "    out.append(p.dumps())\n"
        "    return ' '.join(str(x) for x in out)\n"));
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    struct Cleanup
    {
        ParameterGrp::handle param;
        ~Cleanup()
        {
            param->RemoveBool("Evaluate");
            dropHostModules({"fcxattrs", "fcxattrsrig"});
        }
    } cleanup {param};
    const std::string objExpr =
        "__import__('FreeCAD').getDocument('FcxEvalTest').getObject('";

    // routing off: Probe(obj) is a native instance, __init__ ran here
    std::string out;
    param->SetBool("Evaluate", false);
    ASSERT_TRUE(hostEvalStr("__import__('fcxattrsrig').native(" + objExpr + "Obj'))", out));
    EXPECT_EQ(out, "True True Native");

    // routing on: Probe(obj2) constructs in the guest -- the stand-in
    // is what the expression got back and what the property holds;
    // Type and the class attribute read through the guest; a missing
    // attribute is an AttributeError (hasattr False); a hook the class
    // lacks answers without a trip; a method read is a forwarder that
    // crosses with the object as owner; a write lands in the guest; a
    // document object cannot be stored there
    App::DocumentObject* obj2 = doc->addObject("App::FeaturePython", "Obj2");
    ASSERT_NE(obj2, nullptr);
    auto width2 = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj2->addDynamicProperty("App::PropertyFloat", "Width"));
    ASSERT_NE(width2, nullptr);
    param->SetBool("Evaluate", true);
    host.resetStats();
    ASSERT_TRUE(hostEvalStr("__import__('fcxattrsrig').routed(" + objExpr + "Obj2'))", out));
    EXPECT_EQ(out, "True Routed probe False False Routed:Obj2 Changed TypeError Changed");
    auto st = host.stats();
    // the construction, Type, kind, nothing, describe (read + call),
    // Type (set + read), the refused set never crosses, dumps
    EXPECT_EQ(st.proxyCalls, 9u) << "construction + reads + the call + the write + dumps";
    std::cout << "guest Proxy attrs: proxy calls " << st.proxyCalls << ", host ops:";
    for (const auto& [k, v] : st.hostOps)
        std::cout << " " << k << "=" << v;
    std::cout << ", bridge ops:";
    for (const auto& [k, v] : st.ops)
        std::cout << " " << k << "=" << v;
    std::cout << std::endl;

    // and it is a working Proxy: the recompute runs execute() in the guest
    width2->setValue(1.0);
    obj2->touch();
    doc->recompute();
    EXPECT_FALSE(obj2->isError());
    EXPECT_DOUBLE_EQ(width2->getValue(), 2.0);
    {
        Base::PyGILStateLocker lock;
        auto* proxyProp = Base::freecad_dynamic_cast<App::PropertyPythonObject>(
            obj2->getPropertyByName("Proxy"));
        ASSERT_NE(proxyProp, nullptr);
        EXPECT_TRUE(App::ExpressionSandbox::isGuestProxy(proxyProp->getValue().ptr()));
    }
}

/// A corpus gate (G1d): a test document of one workbench's scripted
/// objects, generated natively by `generator._create_objects(doc)`
/// (drafttests.draft_test_objects, bimtests.bim_test_objects) and
/// carried by a bundled wheel (`wheelPrefix`).  Two passes each: the
/// reopen gate saves the native build and reopens it natively then
/// routed (the Restore route); the built gate builds it twice, natively
/// then routed (the construction dispatch).  A pass snapshots every
/// object -- proxy kind, shape hash, vertices, state -- and the routed
/// pass is compared to the native one: every scripted object's Proxy a
/// guest stand-in, none imported or constructed on the host, no object
/// missing, and (strict) none invalid, any shape difference within a
/// few ULPs of the coordinate (libm's last bit, see the Polygon note).
struct CorpusGate
{
    const char* label;
    const char* generator;
    const char* wheelPrefix;
    int minScripted;
    /// a corpus whose failures are still being listed reports invalid
    /// objects and shape differences instead of failing on them
    bool strict;
};

const CorpusGate DraftCorpus {"Draft test document", "drafttests.draft_test_objects", "fcx_draft-",
                              60, true};
// strict since 2026-09-05: the four writers to sibling objects
// (Stairs, PipeConnector, Schedule, Report) recompute under the
// same-document write gate, and Schedule's IFC property save meets the
// nativeifc guest shim -- nothing of BIM's App side fails routed
const CorpusGate BimCorpus {"BIM test document", "bimtests.bim_test_objects", "fcx_bim-", 10,
                            true};

std::string corpusRig(const CorpusGate& gate)
{
    return std::string("import hashlib, json, math\n"
                       "import FreeCAD as App\n"
                       "import importlib\n"
                       "gen = importlib.import_module(")
        + json(gate.generator).dump()
        + ")\n"
          "S = App.ExpressionSandbox\n"
          "def build(path):\n"
          "    doc = App.newDocument('FcxCorpus')\n"
          "    gen._create_objects(doc)\n"
          "    doc.recompute()\n"
          "    doc.saveAs(path)\n"
          "    App.closeDocument(doc.Name)\n"
          "    return 'ok'\n"
          "def sig(o):\n"
          "    try:\n"
          "        sh = o.Shape\n"
          "    except Exception:\n"
          "        return None\n"
          "    if sh.isNull():\n"
          "        return 'null'\n"
          "    return hashlib.sha1(sh.exportBrepToString().encode()).hexdigest()\n"
          "def verts(o):\n"
          "    try:\n"
          "        return sorted((v.X, v.Y, v.Z) for v in o.Shape.Vertexes)\n"
          "    except Exception:\n"
          "        return None\n"
          "def proxy(o):\n"
          "    p = getattr(o, 'Proxy', None)\n"
          "    if p is None:\n"
          "        return 'none'\n"
          "    return 'guest' if S.proxyInfo(p) else 'host'\n"
          "SNAP = {}\n"
          "def pass_(path, routed):\n"
          "    doc = App.openDocument(path)\n"
          "    restored = {o.Name: proxy(o) for o in doc.Objects}\n"
          "    for o in doc.Objects:\n"
          "        o.touch()\n"
          "    doc.recompute()\n"
          "    SNAP[routed] = {o.Name: (restored[o.Name], sig(o), verts(o), 'Invalid' in o.State)\n"
          "                    for o in doc.Objects}\n"
          "    App.closeDocument(doc.Name)\n"
          "    return 'ok'\n"
          "def build_pass(routed):\n"
          "    doc = App.newDocument('FcxCorpusBuild')\n"
          "    gen._create_objects(doc)\n"
          "    doc.recompute()\n"
          "    SNAP[routed] = {o.Name: (proxy(o), sig(o), verts(o), 'Invalid' in o.State)\n"
          "                    for o in doc.Objects}\n"
          "    App.closeDocument(doc.Name)\n"
          "    return 'ok'\n"
          "def compare():\n"
          "    n, r = SNAP[False], SNAP[True]\n"
          "    out = dict(objects=len(n), guest=0, host=0, none=0, invalid=[], differ=[], missing=[],\n"
          "               hosts=[], nones=[], native_invalid=[])\n"
          "    for name, (pn, sn, vn, ninv) in n.items():\n"
          "        if ninv:\n"
          "            out['native_invalid'].append(name)\n"
          "        if name not in r:\n"
          "            out['missing'].append(name)\n"
          "            continue\n"
          "        pr, sr, vr, inv = r[name]\n"
          "        if pn != 'none':\n"
          "            out[pr] += 1\n"
          "            if pr == 'host':\n"
          "                out['hosts'].append(name)\n"
          "            elif pr == 'none':\n"
          "                out['nones'].append(name)\n"
          "        if inv and not ninv:\n"
          "            out['invalid'].append(name)\n"
          "        if sn is not None and sn != sr:\n"
          "            d = ulps = None\n"
          "            if vn and vr and len(vn) == len(vr):\n"
          "                d = max(abs(a - b) for pa, pb in zip(vn, vr) for a, b in zip(pa, pb))\n"
          "                scale = max(abs(c) for p in vn + vr for c in p)\n"
          "                ulps = d / math.ulp(scale) if scale else (0.0 if d == 0 else None)\n"
          "            out['differ'].append([name, d, ulps])\n"
          "    return json.dumps(out)\n";
}

/// Why the gate cannot run: the runtime is not pyodide, the wheel is
/// not bundled, or the generator does not import on the host; empty
/// when it can.
std::string corpusSkipReason(const CorpusGate& gate)
{
    auto& host = ImageHost::instance();
    if (host.runtime() != "pyodide")
        return "bundled wheels load on the pyodide runtime only";
    namespace fs = std::filesystem;
    bool bundled = false;
    std::error_code ec;
    for (const auto& e :
         fs::directory_iterator(App::Application::getResourceDir() + "Pyodide/wheels", ec)) {
        const std::string fn = e.path().filename().string();
        bundled = bundled
            || (fn.rfind(gate.wheelPrefix, 0) == 0
                && fn.find("-py3-none-any.whl") != std::string::npos);
    }
    if (!bundled)
        return std::string("no ") + gate.wheelPrefix + "* wheel bundled under "
            + App::Application::getResourceDir() + "Pyodide/wheels";
    Base::PyGILStateLocker lock;
    PyObject* m = PyImport_ImportModule(gate.generator);
    if (!m) {
        PyErr_Clear();
        return std::string(gate.generator) + " is not importable on the host in this test binary";
    }
    Py_DECREF(m);
    return std::string();
}

void corpusReport(const CorpusGate& gate, const char* how, const json& j,
                  const ImageHost::Stats& st)
{
    std::cout << gate.label << " " << how << " routed: " << j["objects"] << " objects, proxies guest "
              << j["guest"] << " host " << j["host"] << " none " << j["none"] << "; proxy calls "
              << st.proxyCalls << " (";
    for (const auto& [k, v] : st.hostOps)
        std::cout << " " << k << "=" << v;
    std::cout << " ); bridge ops:";
    for (const auto& [k, v] : st.ops)
        std::cout << " " << k << "=" << v;
    std::cout << "\n  invalid natively: " << j["native_invalid"].dump()
              << "\n  invalid routed only: " << j["invalid"].dump()
              << "\n  differ: " << j["differ"].dump() << std::endl;
    EXPECT_GE(j["objects"].get<int>(), gate.minScripted);
    EXPECT_GE(j["guest"].get<int>(), gate.minScripted) << "every Proxy must be a stand-in";
    EXPECT_EQ(j["host"].get<int>(), 0)
        << "no Proxy may be imported or constructed on the host with routing on: "
        << j["hosts"].dump();
    EXPECT_EQ(j["none"].get<int>(), 0) << "every Proxy module must be served by the guest: "
                                       << j["nones"].dump();
    if (!gate.strict) {
        // an object a failing make_* never created is missing here
        if (!j["missing"].empty())
            std::cout << "  missing: " << j["missing"].dump() << std::endl;
        return;
    }
    EXPECT_TRUE(j["missing"].empty()) << j["missing"].dump();
    EXPECT_TRUE(j["invalid"].empty()) << "objects failing to recompute in the guest: "
                                      << j["invalid"].dump();
    // A shape that is not byte-identical must be within libm's last
    // bit.  Measured 2026-09-04: the guest's wasm libm (musl's msun
    // cos) returns cos(3 * 2pi/5) one ULP from the correctly rounded
    // value glibc returns (the true value sits 0.03 ULP from the
    // rounding midpoint), so Polygon's pentagon has one vertex
    // coordinate one ULP off, 2.8e-14 at radius 250.  The bound is
    // therefore in ULPs of the object's largest coordinate, not an
    // absolute distance: a few ULPs cover a product's own rounding on
    // top of the trig, and anything larger is a real divergence.
    for (const auto& d : j["differ"]) {
        ASSERT_TRUE(d.is_array() && d.size() == 3);
        EXPECT_TRUE(d[2].is_number()) << d[0] << ": shapes differ in structure, not in rounding";
        if (d[2].is_number()) {
            EXPECT_LE(d[2].get<double>(), 4.0)
                << d[0] << ": " << d[2] << " ULPs of the largest coordinate (delta " << d[1]
                << "), more than a last-bit libm difference";
        }
    }
}

/// The reopen gate: the Restore route (docs/Sandbox.md 3.5, 7.6).
void corpusReopenRouted(const CorpusGate& gate)
{
    const std::string why = corpusSkipReason(gate);
    if (!why.empty())
        GTEST_SKIP() << why;
    auto& host = ImageHost::instance();
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    const std::string path = std::string(std::tmpnam(nullptr)) + "-fcxobjs.FCStd";
    struct Cleanup
    {
        ParameterGrp::handle param;
        std::string path;
        ~Cleanup()
        {
            param->RemoveBool("Evaluate");
            std::remove(path.c_str());
            dropHostModules({"fcxobjs"});
        }
    } cleanup {param, path};

    ASSERT_TRUE(hostModule("fcxobjs", corpusRig(gate)));
    std::string ok;
    param->SetBool("Evaluate", false);
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').build(" + json(path).dump() + ")", ok));
    ASSERT_EQ(ok, "ok");
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').pass_(" + json(path).dump() + ", False)", ok));
    param->SetBool("Evaluate", true);
    host.resetStats();
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').pass_(" + json(path).dump() + ", True)", ok));
    auto st = host.stats();
    std::string summary;
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').compare()", summary));
    corpusReport(gate, "reopened", json::parse(summary), st);
}

/// The built gate: the construction dispatch (7.6 G1d) -- every make_*
/// runs on the host, every class's __new__ constructs its Proxy in the
/// guest.
void corpusBuiltRouted(const CorpusGate& gate)
{
    const std::string why = corpusSkipReason(gate);
    if (!why.empty())
        GTEST_SKIP() << why;
    auto& host = ImageHost::instance();
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    struct Cleanup
    {
        ParameterGrp::handle param;
        ~Cleanup()
        {
            param->RemoveBool("Evaluate");
            dropHostModules({"fcxobjs"});
        }
    } cleanup {param};
    ASSERT_TRUE(hostModule("fcxobjs", corpusRig(gate)));
    std::string ok;
    param->SetBool("Evaluate", false);
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').build_pass(False)", ok));
    ASSERT_EQ(ok, "ok");
    param->SetBool("Evaluate", true);
    host.resetStats();
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').build_pass(True)", ok));
    ASSERT_EQ(ok, "ok");
    auto st = host.stats();
    std::string summary;
    ASSERT_TRUE(hostEvalStr("__import__('fcxobjs').compare()", summary));
    corpusReport(gate, "built", json::parse(summary), st);
}

// ---- G1d: the Draft test document (111 objects, 70 scripted) reopened
// ---- with routing ON -- every saved Proxy a guest stand-in, every
// ---- object recomputing, every shape equal to the native re-execute
// ---- (or within libm's last bit) -- and BUILT in a routed session ----

TEST_F(ExpressionImageEvalTest, draftTestObjectsReopenRouted)
{
    corpusReopenRouted(DraftCorpus);
}

TEST_F(ExpressionImageEvalTest, draftTestObjectsBuiltRouted)
{
    corpusBuiltRouted(DraftCorpus);
}

// ---- the same two gates over the BIM corpus (bimtests.bim_test_objects,
// ---- the fcx_bim wheel), strict since 2026-09-05: 68 objects, 59
// ---- guest Proxies, none invalid, one shape a quarter ULP off ----

TEST_F(ExpressionImageEvalTest, bimTestObjectsReopenRouted)
{
    corpusReopenRouted(BimCorpus);
}

TEST_F(ExpressionImageEvalTest, bimTestObjectsBuiltRouted)
{
    corpusBuiltRouted(BimCorpus);
}
