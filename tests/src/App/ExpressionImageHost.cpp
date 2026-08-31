// Tests for the sandbox image host embedding (App::ExpressionSandbox::
// ImageHost).  The image is a cross-built artifact that may not exist
// on every box: tests locate it via the FCX_IMAGE and FCX_STDLIB
// environment variables (falling back to the dev-tree default paths)
// and SKIP when unavailable, so the suite stays green without the
// wasm toolchain.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

#include <App/ExpressionImageHost.h>
#include <Base/FileInfo.h>

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
        std::string repo = envOr("FCX_REPO", std::string());
        std::string image = envOr("FCX_IMAGE",
            repo.empty() ? std::string() : repo + "/build/wasi-image/fcx_image.wasm");
        std::string stdlib = envOr("FCX_STDLIB", std::string());
        if (image.empty() || stdlib.empty() || !Base::FileInfo(image).exists()
                || !Base::FileInfo(stdlib).exists()) {
            GTEST_SKIP() << "sandbox image not available "
                            "(set FCX_IMAGE and FCX_STDLIB)";
        }
        ImageHost::instance().configure(image, stdlib);
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

// ---- image->host bridge ops (get_attr/call/get_item/len/release,
// ---- ExpressionImageBridge.cpp): live host objects cross as handles ----

#include <Python.h>

#include <App/ExpressionImageBridge.h>
#include <App/ExpressionSecurityRuntime.h>
#include <Base/Interpreter.h>
#include "InitApplication.h"

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
    // the eval globals died with the call; the proxy's __del__ sent a
    // release op for the binding handle
    EXPECT_EQ(ImageHost::instance().handleCount(), 0u);
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
    // removeProperty IS an XML member of DocumentObjectPy -- but it
    // carries no <Sandbox/> annotation, so the facade does not have it
    // and read_prop finds no such property: DENY by default.
    auto res = ImageHost::instance().eval("o.removeProperty",
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
    req["a"] = "removeProperty";
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
    // One mid-eval reach-back (read_prop) on top of a python-lang eval:
    // the cost pack-first evaluation exists to avoid.
    auto bind = objectBinding("o", obj);
    auto res = ImageHost::instance().eval("o.Width", bind);
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    benchUs("image.eval.oneBridgeHop", 2000, [&] {
        auto r = ImageHost::instance().eval("o.Width", bind);
        (void)r;
    });
    benchUs("image.eval.noBridgeHop", 2000, [&] {
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

TEST_F(ExpressionRoutingTest, pythonModeIsRefusedNotSilentlyNative)
{
    // python-mode sheets are a separate step; while routing is on they
    // must refuse rather than run in the host behind the boundary
    auto expr = App::Expression::parse(obj, "Width * 2");
    EXPECT_THROW(App::ExpressionSandbox::evaluate(
                     expr.get(), App::Expression::OptionPythonMode),
                 Base::RuntimeError);
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
