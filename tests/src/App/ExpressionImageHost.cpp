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

TEST_F(ExpressionImageBridgeTest, getAttrCrossesBridge)
{
    uint64_t id = exportFromSource("class T:\n"
                                   "    answer = 42\n"
                                   "o = T()\n", "o");
    auto res = ImageHost::instance().eval("o.answer", handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<int64_t>(), 42);
}

TEST_F(ExpressionImageBridgeTest, boundMethodCallWithArgs)
{
    uint64_t id = exportFromSource("class T:\n"
                                   "    def add(self, a, b):\n"
                                   "        return a + b\n"
                                   "o = T()\n", "o");
    auto res = ImageHost::instance().eval("o.add(2, 3)",
                                          handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<int64_t>(), 5);
}

TEST_F(ExpressionImageBridgeTest, chainedHandleResult)
{
    // child() returns a non-marshalable object: it must come back as a
    // fresh handle whose attributes resolve through the bridge again
    uint64_t id = exportFromSource("class Kid:\n"
                                   "    name = 'kid'\n"
                                   "class T:\n"
                                   "    def child(self):\n"
                                   "        return Kid()\n"
                                   "o = T()\n", "o");
    auto res = ImageHost::instance().eval("o.child().name",
                                          handleBinding("o", id));
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<std::string>(), "kid");
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
    uint64_t id = exportFromSource("class T:\n"
                                   "    answer = 1\n"
                                   "o = T()\n", "o");
    EXPECT_EQ(ImageHost::instance().handleCount(), 1u);
    auto res = ImageHost::instance().eval("o.answer", handleBinding("o", id));
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
    // principal would not exercise the gate at all).
    const std::string principal =
        "document:sha256:" + std::string(64, 'b');

    uint64_t id = exportFromSource("class T:\n"
                                   "    secret = 7\n"
                                   "o = T()\n", "o");
    {
        // arbitrary-instance getattr is gated (unsafe.getattr) once a
        // principal scope is active on the evaluating thread
        Runtime::Scope scope(principal.c_str());
        auto res = ImageHost::instance().eval("o.secret",
                                              handleBinding("o", id));
        ASSERT_FALSE(res.ok);
        EXPECT_EQ(res.excType, "PermissionError");
    }
    Runtime::instance().grant(principal, Permission::UnsafeGetattr, "*",
                              true, "session");
    uint64_t id2 = exportFromSource("class T:\n"
                                    "    secret = 7\n"
                                    "o = T()\n", "o");
    {
        Runtime::Scope scope(principal.c_str());
        auto res = ImageHost::instance().eval("o.secret",
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
#include <App/ExpressionImageBridge.h>
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
