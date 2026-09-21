// Tests for the sandbox image host embedding (App::ExpressionSandbox::
// ImageHost).  The image is a cross-built artifact that may not exist
// on every box: tests locate it via the FCX_IMAGE and FCX_STDLIB
// environment variables, or -- with neither set -- through the host's
// own search, which ends at the <datadir>/Fcx bundle an install
// provides and a build tree mirrors.  They SKIP when it is nowhere, so
// the suite stays green without the wasm toolchain.

#include <functional>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <App/ExpressionImageHost.h>
#include <App/Expression.h>
#include <App/ExpressionEvaluator.h>
#include <App/ExpressionImageBridge.h>
#include <App/ExpressionLibrary.h>
#include <Base/FileInfo.h>
#include <Base/Vector3D.h>

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
        return FcxWire::fromCbor(res.value);
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

// ---- the memory budget (Outcome in App/ExpressionImageRuntime.h,
// ---- docs/Sandbox.md 7.17 D4): a growth past the ceiling is refused as
// ---- a MemoryError and the guest kept; the engine heap's limit stops
// ---- the guest, and the next evaluation boots a fresh one ----

class ExpressionImageMemoryTest: public ExpressionImageBudgetTest
{
protected:
    void SetUp() override
    {
        ExpressionImageBudgetTest::SetUp();  // may GTEST_SKIP
        if (IsSkipped())
            return;
        if (ImageHost::instance().runtime() != "pyodide")
            GTEST_SKIP() << "the memory budget is the pyodide runtime's";
        hadMemory = prefs()->GetInt("MemoryMB", -1);
        hadHeap = prefs()->GetInt("EngineHeapMB", -1);
        touched = true;
        // not the time budget under test
        prefs()->SetInt("BudgetMs", 10000);
        prefs()->SetInt("MemoryMB", 256);
        prefs()->SetInt("EngineHeapMB", 256);
        // the engine heap's limit applies at boot
        ImageHost::instance().reset();
    }

    void TearDown() override
    {
        if (touched) {
            if (hadMemory < 0)
                prefs()->RemoveInt("MemoryMB");
            else
                prefs()->SetInt("MemoryMB", hadMemory);
            if (hadHeap < 0)
                prefs()->RemoveInt("EngineHeapMB");
            else
                prefs()->SetInt("EngineHeapMB", hadHeap);
            ImageHost::instance().reset();
        }
        ExpressionImageBudgetTest::TearDown();
    }

    long hadMemory = -1;
    long hadHeap = -1;
    bool touched = false;
};

// One allocation past the ceiling: refused, and the guest keeps going
// with everything it had.
TEST_F(ExpressionImageMemoryTest, growthPastTheCeilingIsRefused)
{
    mark();
    auto res = ImageHost::instance().eval("len(bytearray(400 * 1024 * 1024))", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "MemoryError") << res.message;
    EXPECT_NE(res.message.find("256 MB memory budget (refused)"), std::string::npos)
        << res.message;

    const auto info = ImageHost::instance().memoryInfo();
    EXPECT_TRUE(info.live);
    EXPECT_GT(info.refusals, 0u);
    EXPECT_EQ(info.linearLimit, std::size_t(256) << 20);
    EXPECT_LE(info.linear, info.linearLimit);

    auto good = ImageHost::instance().eval("40 + 2", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 42);
    auto after = readMark();
    ASSERT_TRUE(after.ok) << "a refusal dropped the instance: " << after.excType << ": "
                          << after.message;
    EXPECT_EQ(value(after).get<int64_t>(), 42);
}

// Many allocations, each inside the ceiling, until one is not: the list
// unwinds with the exception and its megabytes are reused.
TEST_F(ExpressionImageMemoryTest, runawayGrowthIsRefused)
{
    auto res = ImageHost::instance().eval("len([bytearray(1 << 20) for x in iter(int, 1)])", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "MemoryError") << res.message;

    auto again = ImageHost::instance().eval("len(bytearray(64 << 20))", {});
    ASSERT_TRUE(again.ok) << again.excType << ": " << again.message;
    EXPECT_EQ(value(again).get<int64_t>(), int64_t(64) << 20);
}

// An array buffer is outside the Python heap and outside the engine
// heap: the allocator counts it against the same ceiling.
TEST_F(ExpressionImageMemoryTest, arrayBufferPastTheCeilingIsRefused)
{
    mark();
    auto res = ImageHost::instance().eval(
        "__import__('js').ArrayBuffer.new(300 * 1024 * 1024).byteLength", {});
    ASSERT_FALSE(res.ok) << "a 300 MB array buffer under a 256 MB ceiling";
    // JavaScript's RangeError, as pyodide raises it in Python; V8's
    // last-resort GC after the failed allocation must not stop the guest
    EXPECT_EQ(res.excType, "JsException") << res.message;
    EXPECT_GT(ImageHost::instance().memoryInfo().refusals, 0u);
    auto after = readMark();
    ASSERT_TRUE(after.ok) << "a refused buffer dropped the instance: " << after.excType << ": "
                          << after.message;
}

// A wasm memory made from JavaScript after boot is refused, whatever its
// size (the compile gate, below).
TEST_F(ExpressionImageMemoryTest, memoryConstructedPastTheCeilingIsRefused)
{
    auto res = ImageHost::instance().eval(
        "__import__('js').WebAssembly.Memory.new(__import__('pyodide').ffi.to_js("
        "{'initial': 8000}, dict_converter=__import__('js').Object.fromEntries))",
        {});
    ASSERT_FALSE(res.ok) << "a 500 MB memory under a 256 MB ceiling";
    EXPECT_NE(res.message.find("memory budget"), std::string::npos) << res.message;
}

// ---- the compile gate (host_shim.js): after boot no module may define
// ---- a memory of its own, and no memory may be made from JavaScript ----

// A memory section with an entry, behind a custom section the walk has
// to skip: refused, counted, and the guest keeps going.
TEST_F(ExpressionImageMemoryTest, moduleDefiningAMemoryIsRefused)
{
    mark();
    auto res = ImageHost::instance().eval(
        R"py(__import__('js').WebAssembly.Module.new(__import__('pyodide').ffi.to_js()py"
        R"py(b'\x00asm\x01\x00\x00\x00\x00\x03\x01ab\x05\x03\x01\x00\x01')))py",
        {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "JsException") << res.message;
    EXPECT_NE(res.message.find("defines its own memory"), std::string::npos) << res.message;
    EXPECT_GT(ImageHost::instance().memoryInfo().refusals, 0u);
    auto after = readMark();
    ASSERT_TRUE(after.ok) << "a refused module dropped the instance: " << after.excType << ": "
                          << after.message;
}

// The shape pyodide's own modules have: a memory IMPORTED, and an empty
// memory section is no memory.  Compiles, and the statics still answer.
TEST_F(ExpressionImageMemoryTest, moduleImportingItsMemoryCompiles)
{
    auto res = ImageHost::instance().eval(
        R"py(__import__('js').WebAssembly.Module.imports(__import__('js').WebAssembly.Module.new()py"
        R"py(__import__('pyodide').ffi.to_js()py"
        R"py(b'\x00asm\x01\x00\x00\x00\x02\x08\x01\x01e\x01m\x02\x00\x01\x05\x01\x00'))).length)py",
        {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    EXPECT_EQ(value(res).get<int64_t>(), 1);
}

TEST_F(ExpressionImageMemoryTest, memoryAfterBootIsRefused)
{
    auto res = ImageHost::instance().eval(
        "__import__('js').WebAssembly.Memory.new(__import__('pyodide').ffi.to_js("
        "{'initial': 1}, dict_converter=__import__('js').Object.fromEntries))",
        {});
    ASSERT_FALSE(res.ok) << "a one-page memory made after boot";
    EXPECT_NE(res.message.find("no new memory after boot"), std::string::npos) << res.message;
}

// The guest reaches the shim's realm: replacing Reflect.construct must not
// hand it the native constructor, and a typed array lying about its length
// must not hide a memory section from the walk.
TEST_F(ExpressionImageMemoryTest, tamperedIntrinsicsDoNotOpenTheGate)
{
    auto res = ImageHost::instance().eval(
        R"py(__import__('js').eval("(function () {)py"
        R"py( var P = Object.getPrototypeOf(Uint8Array.prototype);)py"
        R"py( var saved = Object.getOwnPropertyDescriptor(P, 'length');)py"
        R"py( var construct = Reflect.construct; var leaked = null; var out = 'compiled';)py"
        R"py( Reflect.construct = function (t, a, n) { leaked = t; return construct(t, a, n); };)py"
        R"py( try { new WebAssembly.Module(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])); })py"
        R"py( finally { Reflect.construct = construct; })py"
        R"py( var bytes = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 5, 3, 1, 0, 1]);)py"
        R"py( Object.defineProperty(P, 'length', { get: function () { return 8; }, configurable: true });)py"
        R"py( try { new WebAssembly.Module(bytes); } catch (e) { out = String(e); })py"
        R"py( finally { Object.defineProperty(P, 'length', saved); })py"
        R"py( return (leaked ? 'LEAKED ' : 'sealed ') + out; })()"))py",
        {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    const std::string out = value(res).get<std::string>();
    EXPECT_EQ(out.rfind("sealed ", 0), 0u) << out;
    EXPECT_NE(out.find("defines its own memory"), std::string::npos) << out;
}

// The engine's own heap cannot refuse; its limit stops the guest.
TEST_F(ExpressionImageMemoryTest, engineHeapLimitStopsTheGuest)
{
    mark();
    auto res = ImageHost::instance().eval(
        "__import__('js').eval('var a = []; for (;;) a.push(new Array(100000).fill(1.5))')", {});
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "MemoryError") << res.message;
    EXPECT_NE(res.message.find("stopped"), std::string::npos) << res.message;

    auto good = ImageHost::instance().eval("40 + 2", {});
    ASSERT_TRUE(good.ok) << good.excType << ": " << good.message;
    EXPECT_EQ(value(good).get<int64_t>(), 42);
    EXPECT_FALSE(readMark().ok) << "a stopped instance was kept";
}

TEST_F(ExpressionImageMemoryTest, memoryInfoReportsTheLiveGuest)
{
    auto res = ImageHost::instance().eval("1", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    const auto info = ImageHost::instance().memoryInfo();
    EXPECT_TRUE(info.live);
    EXPECT_GT(info.linear, std::size_t(16) << 20);
    EXPECT_LE(info.linear, info.linearLimit);
    EXPECT_GT(info.engineHeapUsed, 0u);
    EXPECT_GE(info.engineHeapLimit, std::size_t(200) << 20);
    EXPECT_LE(info.engineHeapLimit, std::size_t(320) << 20);
    EXPECT_EQ(info.refusals, 0u);

    ImageHost::instance().reset();
    EXPECT_FALSE(ImageHost::instance().memoryInfo().live);
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

TEST_F(ExpressionImageEvalTest, prefetchAnswersSiblingReads)
{
    // docs/Sandbox.md 7.20, C5: on a table that prefetches (a remote
    // guest's endpoint; turned on here to run the guest's half in
    // process), a read off one element of a list brings the same read of
    // the elements after it, and the guest answers those without a hop --
    // until an op that may write, and never past the statement.
    auto& host = ImageHost::instance();
    for (int i = 0; i < 40; ++i) {
        auto o = doc->addObject("App::FeaturePython", ("P" + std::to_string(i)).c_str());
        auto w = Base::freecad_dynamic_cast<App::PropertyFloat>(
            o->addDynamicProperty("App::PropertyFloat", "Width"));
        ASSERT_NE(w, nullptr);
        w->setValue(i);
        auto v = Base::freecad_dynamic_cast<App::PropertyVector>(
            o->addDynamicProperty("App::PropertyVector", "Pos"));
        ASSERT_NE(v, nullptr);
        v->setValue(Base::Vector3d(i, 0, 0));
    }
    struct PrefetchOff
    {
        ~PrefetchOff()
        {
            ImageHost::instance().setPrefetch(false);
        }
    } off;
    auto run = [&](const char* src, bool prefetch, const char* op) {
        host.setPrefetch(prefetch);
        host.clearHandles();
        host.resetStats();
        auto r = host.eval(src, objectBinding("d", doc));
        EXPECT_TRUE(r.ok) << src << ": " << r.excType << ": " << r.message;
        auto n = host.stats().ops[op];
        return std::make_pair(r.ok ? value(r) : json(), n);
    };

    // a declared attribute: 41 names, one hop each without the prefetch;
    // with it the list, the first miss (32 more) and the second (the rest)
    const char* names = "[o.Name for o in d.Objects]";
    auto [namesOff, hopsOff] = run(names, false, "get_attr");
    auto [namesOn, hopsOn] = run(names, true, "get_attr");
    EXPECT_EQ(namesOn, namesOff);
    ASSERT_TRUE(namesOn.is_array());
    EXPECT_EQ(namesOn.size(), 41u);
    EXPECT_EQ(hopsOff, 42u);
    EXPECT_EQ(hopsOn, 3u);

    // a property, the same
    const char* widths = "[o.Width for o in d.Objects[1:]]";
    auto [widthsOff, readsOff] = run(widths, false, "read_prop");
    auto [widthsOn, readsOn] = run(widths, true, "read_prop");
    EXPECT_EQ(widthsOn, widthsOff);
    EXPECT_EQ(readsOff, 40u);
    EXPECT_EQ(readsOn, 2u);

    // a write forgets what was prefetched: the read after it sees it
    auto [written, n1] = run(
        "(lambda l: [l[1].Width, setattr(l[2], 'Width', 99.0), l[2].Width])(d.Objects)",
        true, "read_prop");
    ASSERT_TRUE(written.is_array());
    ASSERT_EQ(written.size(), 3u);
    EXPECT_DOUBLE_EQ(written[0].get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(written[2].get<double>(), 99.0);
    EXPECT_EQ(n1, 2u);

    // a hit is a fresh value, as natively
    auto [fresh, n2] = run(
        "(lambda l: [l[1].Pos.x, l[2].Pos is l[2].Pos])(d.Objects)",
        true, "read_prop");
    ASSERT_TRUE(fresh.is_array());
    ASSERT_EQ(fresh.size(), 2u);
    EXPECT_EQ(fresh[1], json(false));
    EXPECT_EQ(n2, 1u);

    // a member that crosses as a handle prefetches nothing: no handle is
    // minted that the guest did not ask for
    auto [docs, hops] = run("len([o.Document for o in d.Objects])", true, "get_attr");
    EXPECT_EQ(docs, json(41));
    EXPECT_EQ(hops, 42u);
}

TEST_F(ExpressionImageEvalTest, writePropSameDocument)
{
    // write_prop and the write-family calls (addProperty,
    // removeProperty, setPropertyStatus, the Document's addObject and
    // removeObject): for a DOCUMENT principal the evaluation owner's
    // DOCUMENT -- the owner, any object of its document, the document
    // itself -- under doc.write.self, "self" being the same-origin
    // document (user ruling 2026-09-05; owner-only was rung 2's
    // scoping).  An object of another document is refused.  The reach
    // is the PRINCIPAL's (S1, docs/Sandbox.md 7.13): with no owner
    // there is no principal scope, which is host code driving the
    // image, and that reaches every open document as the session does.
    // A handle in the value dereferences to the live object.
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
        json m = FcxWire::fromCbor(a);
        m.update(FcxWire::fromCbor(b));
        m.update(FcxWire::fromCbor(c));
        auto v = json::to_cbor(m);
        return std::vector<unsigned char>(v.begin(), v.end());
    };

    // no owner named, no principal scope: host code driving the image
    // (a test, the InitGui runner's exec) reaches every open document,
    // as the session does (S1); the write lands
    auto r = host.eval("setattr(o, 'Width', 5.0)", pack());
    EXPECT_TRUE(r.ok) << r.excType << ": " << r.message;
    EXPECT_DOUBLE_EQ(width->getValue(), 5.0);
    width->setValue(21.0);

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
        json m = FcxWire::fromCbor(a);
        m.update(FcxWire::fromCbor(b));
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
        json m = FcxWire::fromCbor(a);
        m.update(FcxWire::fromCbor(b));
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

    // an undeclared name does not exist in the guest's module: file I/O,
    // which the surface leaves out on purpose (docs/Sandbox.md 7.17 (b))
    auto r = host.eval("__import__('Part').read('/etc/passwd')", pack());
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
    // Deliberately NOT V(1, 1, 0).  That point lies exactly on the
    // bisector of l (the x axis) and l3 (the y axis), so mirror() maps
    // it onto itself and the whole call is decided by rounding noise:
    // findDistance() projects to (7.9e-17, -7.9e-17, 0) here and hits
    // its `if not dist` / `if dist.Length == 0` guards on Windows,
    // returning None.  Both sides then raise, but on DIFFERENT
    // exceptions -- AttributeError from `.Curve` on a degenerate edge
    // here, TypeError from `point.add(None)` there -- so the guest/host
    // comparison disagreed about floating point rather than geometry.
    "circles.circlefrom2Lines1Point(l, l3, V(1, 0.5, 0))",
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
    // Off the l/l3 bisector for the reason given at circlefrom2Lines1Point
    // above -- this call delegates straight to it.
    "circles_incomplete.circleFrom2tan1pt(l, l3, V(1, 0.5, 0))",
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
            m.update(FcxWire::fromCbor(one));
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
    EXPECT_DOUBLE_EQ(FcxWire::fromCbor(res.value)
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
    // The in-image FreeCAD module's document graph (getDocument,
    // listDocuments -- S1, docs/Sandbox.md 7.13) is an app.query op on
    // the host: a PROMPT for a document principal, so a PermissionError
    // naming it until the user answers -- never a silent drill-down.
    // (Before S1 the name did not exist in the image: an AttributeError.)
    auto res = ImageHost::instance().evalExpression(
        obj, "_app.getDocument(<<FcxAccept>>)");
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.excType, "PermissionError")
        << res.excType << ": " << res.message;
    EXPECT_NE(res.message.find("app.query"), std::string::npos) << res.message;
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
    EXPECT_DOUBLE_EQ(FcxWire::fromCbor(granted.value).get<double>(), 30.0);

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

TEST_F(ExpressionImageBenchTest, DISABLED_BenchPrefetchInProcess)
{
    // The prefetch of sibling reads (docs/Sandbox.md 7.20, C5) on the
    // desktop's own guest, where a hop is a few us: what a loop saves, and
    // what a statement that reads one element or stops early pays for the
    // siblings it never reads.  Each case off, then on.
    auto& host = ImageHost::instance();
    for (int i = 0; i < 1000; ++i) {
        auto o = doc->addObject("App::FeaturePython", ("P" + std::to_string(i)).c_str());
        auto w = Base::freecad_dynamic_cast<App::PropertyFloat>(
            o->addDynamicProperty("App::PropertyFloat", "Width"));
        ASSERT_NE(w, nullptr);
        w->setValue(i);
    }
    struct PrefetchOff
    {
        ~PrefetchOff()
        {
            ImageHost::instance().setPrefetch(false);
        }
    } off;
    auto both = [&](const char* name, const char* src, int iters, const char* op,
                    const std::function<std::vector<unsigned char>()>& pack) {
        for (bool on : {false, true}) {
            host.setPrefetch(on);
            host.resetStats();
            std::string label = std::string("prefetch.") + name + (on ? ".on" : ".off");
            benchUs(label.c_str(), iters, [&] {
                auto r = host.eval(src, pack());
                ASSERT_TRUE(r.ok) << src << ": " << r.excType << ": " << r.message;
                host.clearHandles();
            });
            std::cout << "BENCH " << label << " " << op << " hops/eval "
                      << double(host.stats().ops[op]) / (iters + 1) << std::endl;
        }
    };
    auto docPack = [&] { return objectBinding("d", doc); };
    // what a statement costs before any hop: the pack, then the list itself
    both("pack", "42", 200, "get_attr", docPack);
    both("list1000", "len(d.Objects)", 200, "get_attr", docPack);
    both("names1000", "len([o.Name for o in d.Objects])", 20, "get_attr", docPack);
    both("widths1000", "sum(o.Width for o in d.Objects[1:])", 20, "read_prop", docPack);
    both("oneRead", "d.Objects[5].Name", 200, "get_attr", docPack);
    both("twoReads", "d.Objects[5].Name + d.Objects[900].Name", 200, "get_attr", docPack);
    both("breakAt40", "next(i for i, o in enumerate(d.Objects) if o.Name == 'P40')", 100,
         "get_attr", docPack);

    // shapes: a 1000-edge polygon's edges, when Part imports in this binary
    {
        Base::PyGILStateLocker lock;
        PyObject* part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            std::cout << "BENCH prefetch.edges skipped: no Part" << std::endl;
            return;
        }
        Py_DECREF(part);
    }
    uint64_t unused = exportFromSource(
        "import FreeCAD, Part\n"
        "poly = Part.makePolygon([FreeCAD.Vector(i, 3 * (i % 2), 0) for i in range(1001)])\n",
        "poly");
    (void)unused;
    host.clearHandles();
    auto shapePack = [&] {
        Base::PyGILStateLocker lock;
        PyObject* ns = PyDict_New();
        PyDict_SetItemString(ns, "__builtins__", PyEval_GetBuiltins());
        PyObject* r = PyRun_String(
            "import FreeCAD, Part\n"
            "poly = Part.makePolygon([FreeCAD.Vector(i, 3 * (i % 2), 0) for i in range(1001)])\n",
            Py_file_input, ns, ns);
        Py_XDECREF(r);
        PyObject* py = PyDict_GetItemString(ns, "poly");
        json h = {{"t", "h"}, {"id", host.exportObject(py)}, {"ty", Py_TYPE(py)->tp_name}};
        if (const char* fc = App::ExpressionSandbox::facadeKeyFor(Py_TYPE(py)))
            h["fc"] = fc;
        Py_DECREF(ns);
        json b;
        b["s"] = std::move(h);
        auto v = json::to_cbor(b);
        return std::vector<unsigned char>(v.begin(), v.end());
    };
    both("edgeLength1000", "sum(e.Length for e in s.Edges)", 10, "get_attr", shapePack);
    both("edgeFirst", "s.Edges[0].Length", 50, "get_attr", shapePack);
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
    json reply = FcxWire::fromCbor(replyBytes);
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
#include <App/ExpressionGuestProxy.h>
#include <App/PropertyExpressionEngine.h>
#include <App/PropertyPythonObject.h>
#include <App/PropertyUnits.h>

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

TEST_F(ExpressionRoutingTest, localMemberInALongNamedDocumentStaysLocal)
{
    // A document name past the short-string limit (15 characters) turned a
    // program's local `v.x` into a foreign-document reference: the check
    // read the name through a reference into a temporary String, and the
    // host's own failure to resolve `v` was shipped as the answer --
    // "Property 'v' not found".  Found by the 7.17 D3 fixtures, whose files
    // are named longer than any test document was.
    struct LongDocument
    {
        App::Document* doc =
            App::GetApplication().newDocument("FcxEvalTestLongDocumentName", "testUser");
        ~LongDocument()
        {
            App::GetApplication().closeDocument(doc->getName());
        }
    } longDoc;
    ASSERT_GT(std::strlen(longDoc.doc->getName()), 15u);
    auto owner = longDoc.doc->addObject("App::FeaturePython", "Obj");
    ASSERT_NE(owner, nullptr);

    const std::pair<const char*, double> cases[] = {
        {"v = vector(1, 2, 3)\nv.x", 1.0},
        {"base = 2\nbase * 3", 6.0},
    };
    Base::PyGILStateLocker lock;
    for (const auto& c : cases) {
        auto expr = App::Expression::parse(owner, c.first);
        ASSERT_NE(expr, nullptr) << c.first;
        PyObject* value = nullptr;
        try {
            value = App::ExpressionSandbox::evaluatePy(expr.get(),
                                                       App::Expression::OptionCallFrame);
        }
        catch (Base::Exception& e) {
            ADD_FAILURE() << c.first << ": " << e.what();
        }
        catch (Py::Exception&) {
            Base::PyException e;
            ADD_FAILURE() << c.first << ": " << e.what();
        }
        if (value) {
            EXPECT_DOUBLE_EQ(PyFloat_AsDouble(value), c.second) << c.first;
            if (PyErr_Occurred())
                PyErr_Clear();
            Py_DECREF(value);
        }
        ImageHost::instance().clearHandles();
    }
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

// ---- docs/Sandbox.md 7.17 D1: function objects in the image ----

namespace
{
std::string reprOf(PyObject* v)
{
    if (!v)
        return "<null>";
    PyObject* rep = PyObject_Repr(v);
    std::string s = rep ? PyUnicode_AsUTF8(rep) : "<repr failed>";
    Py_XDECREF(rep);
    return s;
}

/// Evaluate `src` under a call frame, routed through the image or
/// in-process, as a new reference; a failure comes back as nullptr with
/// its message in `err`.
PyObject* evalProgram(App::DocumentObject* owner, const char* src, bool routed, std::string& err)
{
    auto expr = App::Expression::parse(owner, src);
    if (!expr) {
        err = "parse produced nothing";
        return nullptr;
    }
    try {
        if (routed)
            return App::ExpressionSandbox::evaluatePy(expr.get(),
                                                      App::Expression::OptionCallFrame);
        return Py::new_reference_to(expr->getPyValue(App::Expression::OptionCallFrame));
    }
    catch (Py::Exception&) {
        Base::PyException e;
        err = e.what();
    }
    catch (Base::Exception& e) {
        err = e.what();
    }
    if (PyErr_Occurred())
        PyErr_Clear();
    return nullptr;
}
}  // namespace

TEST_F(ExpressionRoutingTest, programs)
{
    // A def evaluated routed and called, a function passed as a value, a
    // lambda in a comprehension, defaults and keywords, a list of
    // functions, and a body reading an identifier nothing outside it
    // reads -- not a dependency, so the pack must still carry it.  Each
    // equal to the native answer.
    const char* cases[] = {
        "def f(x):\n    return x * 3\nf(Width)",
        "def twice(g, x):\n    return g(g(x))\ntwice(lambda y: y + 1, Width)",
        "[(lambda k: k * k)(i) for i in [1, 2, 3]]",
        "def g():\n    return Width + 1\ng()",
        "def h(a, b=2):\n    return a * b\nh(b=5, a=Width)",
        "fs = [lambda v: v + 1, lambda v: v * 2]\n[fn(Width) for fn in fs]",
    };
    Base::PyGILStateLocker lock;
    for (const char* src : cases) {
        std::string nerr, rerr;
        PyObject* native = evalProgram(obj, src, false, nerr);
        ASSERT_NE(native, nullptr) << src << "\nnative: " << nerr;
        PyObject* routed = evalProgram(obj, src, true, rerr);
        EXPECT_NE(routed, nullptr) << src << "\nrouted: " << rerr;
        EXPECT_EQ(reprOf(routed), reprOf(native)) << src;
        Py_XDECREF(native);
        Py_XDECREF(routed);
        ImageHost::instance().clearHandles();
    }
}

namespace
{
/// repr() of a borrowed reference; "<null>" for nullptr.
std::string valueRepr(PyObject* value)
{
    if (!value)
        return "<null>";
    PyObject* r = PyObject_Repr(value);
    std::string s = r && PyUnicode_Check(r) ? PyUnicode_AsUTF8(r) : "<unprintable>";
    Py_XDECREF(r);
    if (PyErr_Occurred())
        PyErr_Clear();
    return s;
}

/// Call with one argument; the result's repr, or the exception's type name.
std::string callRepr(PyObject* fn, PyObject* arg)
{
    PyObject* res = PyObject_CallOneArg(fn, arg);
    if (!res) {
        PyObject* type = PyErr_Occurred();
        std::string name = type ? std::string("raised ") + ((PyTypeObject*)type)->tp_name : "?";
        PyErr_Clear();
        return name;
    }
    std::string s = valueRepr(res);
    Py_DECREF(res);
    return s;
}
}  // namespace

TEST_F(ExpressionRoutingTest, programsFunctionValueCrossesAsAStandIn)
{
    // docs/Sandbox.md 7.17 P3.  A function cannot leave its routed
    // evaluation -- its owner is the guest's adapter of that ONE evaluation
    // -- so what leaves is a host stand-in holding the source and the owner,
    // and a call is an evaluation of its own.  Routed = native: the repr,
    // a call made after the evaluation's handles are gone, and a body that
    // reads what is current when it is called.
    Base::PyGILStateLocker lock;
    auto& host = ImageHost::instance();
    auto* width = Base::freecad_dynamic_cast<App::PropertyFloat>(obj->getPropertyByName("Width"));
    ASSERT_NE(width, nullptr);
    const char* sources[] = {"lambda x: x * Width", "def f(x):\n    return x * Width\nf"};
    for (const char* src : sources) {
        width->setValue(21.0);
        std::string nerr, rerr;
        PyObject* native = evalProgram(obj, src, false, nerr);
        ASSERT_NE(native, nullptr) << nerr;
        PyObject* routed = evalProgram(obj, src, true, rerr);
        ASSERT_NE(routed, nullptr) << rerr;
        EXPECT_TRUE(App::ExpressionSandbox::isRoutedFunction(routed)) << src;
        EXPECT_EQ(valueRepr(routed), valueRepr(native)) << src;
        EXPECT_EQ(host.handleCount(), 0u);

        PyObject* two = PyLong_FromLong(2);
        const std::size_t before = host.evalCount();
        EXPECT_EQ(callRepr(routed, two), callRepr(native, two)) << src;
        EXPECT_EQ(host.evalCount(), before + 1) << "one call, one evaluation";

        // the body reads Width when called, natively and routed
        width->setValue(10.0);
        EXPECT_EQ(callRepr(routed, two), callRepr(native, two)) << src;
        Py_DECREF(two);
        Py_DECREF(native);
        Py_DECREF(routed);
    }
    host.clearHandles();
}

TEST_F(ExpressionRoutingTest, programsStandInCalledFromAnotherEvaluation)
{
    // A stand-in held in a property and called by a later routed evaluation
    // crosses as a guest callable whose call is one fcall hop -- the call a
    // nested evaluation on the host.  Natively the same property holds the
    // ExpressionPy.
    Base::PyGILStateLocker lock;
    auto& host = ImageHost::instance();
    auto* fn = Base::freecad_dynamic_cast<App::PropertyPythonObject>(
        obj->addDynamicProperty("App::PropertyPythonObject", "Fn"));
    ASSERT_NE(fn, nullptr);
    const char* def = "def f(x):\n    return x * Width + 1\nf";

    std::string err;
    PyObject* native = evalProgram(obj, def, false, err);
    ASSERT_NE(native, nullptr) << err;
    fn->setValue(Py::Object(native, true));
    PyObject* nres = evalProgram(obj, "Fn(4)", false, err);
    ASSERT_NE(nres, nullptr) << err;

    PyObject* routed = evalProgram(obj, def, true, err);
    ASSERT_NE(routed, nullptr) << err;
    fn->setValue(Py::Object(routed, true));
    host.resetStats();
    PyObject* rres = evalProgram(obj, "Fn(4)", true, err);
    ASSERT_NE(rres, nullptr) << err;
    EXPECT_EQ(valueRepr(rres), valueRepr(nres));
    EXPECT_EQ(host.stats().ops["fcall"], 1u);
    Py_DECREF(rres);
    Py_DECREF(nres);
    host.clearHandles();

    // fcall calls a routed function and nothing else behind a handle
    fn->setValue(Py::Long(3));
    PyObject* refused = evalProgram(obj, "Fn(4)", true, err);
    EXPECT_EQ(refused, nullptr);
    Py_XDECREF(refused);
    host.clearHandles();
}

TEST_F(ExpressionRoutingTest, programsChainCallRunsAsTheObjectItExtends)
{
    // docs/ProxyChain.md 2.5, RULED 2026-09-13, "Run as the feature's file".
    // A function defined by an object of THIS document writes an object of
    // another.  Called plainly it runs as its own file, and the target is
    // out of reach; bound the way the chain binds it, it runs as the target
    // and the write is the target's own.
    Base::PyGILStateLocker lock;
    auto& host = ImageHost::instance();
    App::Document* doc2 = App::GetApplication().newDocument("FcxRunAsTarget", "testUser");
    App::DocumentObject* target = doc2->addObject("App::FeaturePython", "Target");
    auto* marker = Base::freecad_dynamic_cast<App::PropertyInteger>(
        target->addDynamicProperty("App::PropertyInteger", "Marker"));
    ASSERT_NE(marker, nullptr);

    std::string err;
    PyObject* routed = evalProgram(obj, "def w(o):\n    o.Marker = 7\n    return True\nw", true, err);
    ASSERT_NE(routed, nullptr) << err;
    host.clearHandles();
    PyObject* targetPy = target->getPyObject();

    EXPECT_EQ(callRepr(routed, targetPy), "raised PermissionError");
    EXPECT_EQ(marker->getValue(), 0);
    host.clearHandles();

    PyObject* bound = App::ExpressionSandbox::bindRoutedFunction(routed, targetPy);
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(App::ExpressionSandbox::isRoutedFunction(bound));
    EXPECT_EQ(callRepr(bound, targetPy), "True");
    EXPECT_EQ(marker->getValue(), 7);
    host.clearHandles();

    Py_DECREF(bound);
    Py_DECREF(targetPy);
    Py_DECREF(routed);
    App::GetApplication().closeDocument(doc2->getName());
}

namespace
{
/// The flange of docs/Sandbox.md 7.17 as one expression, the text of
/// SandboxProgramFixtures.FLANGE_EXPRESSION.
const char* flangeProgram = "import Part\n"
                            "def hole(a):\n"
                            "    return Part.makeCylinder(HoleDia / 2, Thick * 3,"
                            " vector(Pcd / 2 * cos(a), Pcd / 2 * sin(a), -Thick))\n"
                            "body = Part.makeCylinder(Dia / 2, Thick)\n"
                            "body = body.cut(Part.makeCylinder(Bore / 2, Thick * 3,"
                            " vector(0, 0, -Thick)))\n"
                            "i = 0\n"
                            "while i < Bolts:\n"
                            "    body = body.cut(hole(i * 360deg / Bolts))\n"
                            "    i = i + 1\n"
                            "body\n";

/// The flange's six parameters on `owner`; false if one could not be added.
bool addFlangeParameters(App::DocumentObject* owner)
{
    const std::pair<const char*, double> lengths[] = {
        {"Dia", 60.0}, {"Thick", 8.0}, {"Bore", 20.0}, {"Pcd", 44.0}, {"HoleDia", 6.0}};
    for (const auto& l : lengths) {
        auto p = Base::freecad_dynamic_cast<App::PropertyLength>(
            owner->addDynamicProperty("App::PropertyLength", l.first));
        if (!p)
            return false;
        p->setValue(l.second);
    }
    auto bolts = Base::freecad_dynamic_cast<App::PropertyInteger>(
        owner->addDynamicProperty("App::PropertyInteger", "Bolts"));
    if (!bolts)
        return false;
    bolts->setValue(6);
    return true;
}

/// Every vertex of \a shape, as coordinates.
std::vector<Base::Vector3d> vertexesOf(PyObject* shape)
{
    std::vector<Base::Vector3d> out;
    PyObject* vs = PyObject_GetAttrString(shape, "Vertexes");
    if (!vs) {
        PyErr_Clear();
        return out;
    }
    const Py_ssize_t n = PySequence_Size(vs);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject* v = PySequence_GetItem(vs, i);
        double xyz[3] {};
        const char* names[3] {"X", "Y", "Z"};
        for (int k = 0; k < 3; ++k) {
            PyObject* c = v ? PyObject_GetAttrString(v, names[k]) : nullptr;
            xyz[k] = c ? PyFloat_AsDouble(c) : 0.0;
            Py_XDECREF(c);
        }
        Py_XDECREF(v);
        out.emplace_back(xyz[0], xyz[1], xyz[2]);
    }
    Py_DECREF(vs);
    return out;
}

/// How far the two shapes' vertices are apart, in ULPs of the largest
/// coordinate: for every vertex of one, the distance to the NEAREST
/// vertex of the other (the larger of the two directions).  Not a
/// comparison of sorted lists -- one last bit in X reorders two vertices
/// and then pairs unrelated points, which reads as a whole-element
/// divergence.  Negative when the vertex counts differ at all.
double vertexUlpsApart(PyObject* a, PyObject* b)
{
    const std::vector<Base::Vector3d> va = vertexesOf(a);
    const std::vector<Base::Vector3d> vb = vertexesOf(b);
    if (va.empty() || va.size() != vb.size())
        return -1.0;
    double worst = 0.0;
    double scale = 0.0;
    auto sweep = [&worst, &scale](const std::vector<Base::Vector3d>& from,
                                  const std::vector<Base::Vector3d>& to) {
        for (const auto& p : from) {
            double best = std::numeric_limits<double>::max();
            for (const auto& q : to) {
                const double d = std::max({std::fabs(p.x - q.x), std::fabs(p.y - q.y),
                                           std::fabs(p.z - q.z)});
                best = std::min(best, d);
            }
            worst = std::max(worst, best);
            scale = std::max({scale, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
        }
    };
    sweep(va, vb);
    sweep(vb, va);
    const double ulp = std::nextafter(scale, std::numeric_limits<double>::max()) - scale;
    return ulp > 0.0 ? worst / ulp : (worst == 0.0 ? 0.0 : -1.0);
}

bool partImportable()
{
    Base::PyGILStateLocker lock;
    PyObject* part = PyImport_ImportModule("Part");
    if (!part) {
        PyErr_Clear();
        return false;
    }
    Py_DECREF(part);
    return true;
}
}  // namespace

TEST_F(ExpressionRoutingTest, programsFlangeMatchesNative)
{
    // The D1 gate: the flange of docs/Sandbox.md 7.17 as one expression,
    // routed = native.  A body cut by a bore and a bolt circle, the hole a
    // def reading HoleDia and Pcd only inside itself.  Routed runs under
    // enforcement, where `import Part` is the guest's facade; the native
    // twin runs with enforcement off, as the corpus rig does, since
    // natively `import Part` is host.import, PROMPT for a document.
    if (!partImportable())
        GTEST_SKIP() << "the Part module is not importable in this test binary";
    ASSERT_TRUE(addFlangeParameters(obj));
    const char* src = flangeProgram;

    Base::PyGILStateLocker lock;
    std::string rerr, nerr;
    PyObject* routed = evalProgram(obj, src, true, rerr);
    ASSERT_NE(routed, nullptr) << rerr;

    auto security = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Security");
    security->SetBool("Enforce", false);
    PyObject* native = evalProgram(obj, src, false, nerr);
    security->RemoveBool("Enforce");
    ASSERT_NE(native, nullptr) << nerr;

    auto attr = [](PyObject* shape, const char* name) {
        PyObject* v = PyObject_GetAttrString(shape, name);
        std::string s = reprOf(v);
        Py_XDECREF(v);
        return s;
    };
    EXPECT_EQ(attr(routed, "ShapeType"), "'Compound'");
    EXPECT_EQ(attr(routed, "Volume"), attr(native, "Volume"));
    PyObject* faces = PyObject_GetAttrString(routed, "Faces");
    ASSERT_NE(faces, nullptr);
    EXPECT_EQ(PySequence_Size(faces), 10);
    Py_DECREF(faces);
    PyObject* rb = PyObject_CallMethod(routed, "exportBrepToString", nullptr);
    PyObject* nb = PyObject_CallMethod(native, "exportBrepToString", nullptr);
    ASSERT_NE(rb, nullptr);
    ASSERT_NE(nb, nullptr);
    // Byte-identical where the two libms agree, and within the last bit
    // where they do not -- the rule the corpus gate states and for the
    // same reason (the guest's wasm libm against the host's).  On macOS
    // the bolt circle's cos/sin differ in the last bit, which moves a
    // hole by 1e-14 of its radius: the volume is still identical to the
    // bit, and the BRep text is not.
    if (PyUnicode_Compare(rb, nb) != 0) {
        const double ulps = vertexUlpsApart(routed, native);
        std::cout << "flange: the BRep is not byte-identical; the vertices are " << ulps
                  << " ULPs of the largest coordinate apart" << std::endl;
        EXPECT_GE(ulps, 0.0) << "the shapes differ in structure, not in rounding";
        EXPECT_LE(ulps, 4.0) << "the BRep is not byte-identical and the vertices are " << ulps
                             << " ULPs of the largest coordinate apart, more than a last-bit "
                                "libm difference";
    }
    Py_DECREF(rb);
    Py_DECREF(nb);
    Py_DECREF(routed);
    Py_DECREF(native);
    ImageHost::instance().clearHandles();
}

// ---- docs/Sandbox.md sec 11 item 4: one shape program routed against
// ---- native.  The flange above, evaluated whole each iteration: routed
// ---- under enforcement (the guest runs the program, every Part call a
// ---- geom.call into the host), native with enforcement off.  Both pay the
// ---- same OCCT booleans; the difference is what routing costs a program.

TEST_F(ExpressionImageBenchTest, DISABLED_BenchFlangeProgram)
{
    if (!partImportable())
        GTEST_SKIP() << "the Part module is not importable in this test binary";
    ASSERT_TRUE(addFlangeParameters(obj));
    auto sandbox = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Sandbox");
    auto security = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Security");
    const int iters = 40;
    auto opsTotal = [] {
        std::size_t n = 0;
        for (const auto& op : ImageHost::instance().stats().ops)
            n += op.second;
        return n;
    };

    Base::PyGILStateLocker lock;
    auto evalOnce = [&](bool routed) {
        std::string err;
        PyObject* v = evalProgram(obj, flangeProgram, routed, err);
        EXPECT_NE(v, nullptr) << err;
        Py_XDECREF(v);
        ImageHost::instance().clearHandles();
    };

    sandbox->SetBool("Evaluate", true);
    evalOnce(true);
    std::size_t before = opsTotal();
    evalOnce(true);
    std::size_t ops = opsTotal() - before;
    double routed = benchUs("flange program routed", iters, [&] { evalOnce(true); });
    sandbox->RemoveBool("Evaluate");

    security->SetBool("Enforce", false);
    double native = benchUs("flange program native", iters, [&] { evalOnce(false); });
    security->RemoveBool("Enforce");

    std::cout << "BENCH flange program: " << ops << " bridge ops per evaluation, routed "
              << routed / native << "x native, +" << (routed - native) / 1000.0 << " ms"
              << std::endl;
}

// ---- docs/Sandbox.md 7.17 D2: expression libraries ----

namespace
{
const char* bracketsText = "k = 3\n"
                           "def triple(x):\n"
                           "    return x * k\n"
                           "def viaLater(x):\n"
                           "    return later(x) + 1\n"
                           "def later(x):\n"
                           "    return x * 2\n"
                           "def leak():\n"
                           "    return secret\n";

App::ExpressionLibrary*
addLibrary(App::Document* doc, const char* name, const char* module, const char* text)
{
    auto lib = Base::freecad_dynamic_cast<App::ExpressionLibrary>(
        doc->addObject("App::ExpressionLibrary", name));
    if (lib) {
        lib->Module.setValue(module);
        lib->Text.setValue(text);
    }
    return lib;
}

std::size_t libSourceOps()
{
    auto s = ImageHost::instance().stats();
    auto it = s.ops.find("lib.source");
    return it == s.ops.end() ? 0 : it->second;
}

/// routed = native for one program, as the repr of each
void expectRoutedMatchesNative(App::DocumentObject* owner, const char* src)
{
    std::string nerr, rerr;
    PyObject* native = evalProgram(owner, src, false, nerr);
    ASSERT_NE(native, nullptr) << src << "\nnative: " << nerr;
    PyObject* routed = evalProgram(owner, src, true, rerr);
    EXPECT_NE(routed, nullptr) << src << "\nrouted: " << rerr;
    EXPECT_EQ(reprOf(routed), reprOf(native)) << src;
    Py_XDECREF(native);
    Py_XDECREF(routed);
    ImageHost::instance().clearHandles();
}
}  // namespace

TEST_F(ExpressionRoutingTest, librariesMatchNative)
{
    // A module's constant, its functions by `import` and by `from`, a
    // function calling one defined after it (the module's names, resolved
    // when called), and a call inside a comprehension.
    ASSERT_NE(addLibrary(doc, "Lib", "brackets", bracketsText), nullptr);
    const char* cases[] = {
        "import brackets\nbrackets.triple(Width)",
        "from brackets import triple, viaLater\n[triple(1), viaLater(Width)]",
        "import brackets\nbrackets.k",
        "from brackets import triple\n[triple(i) for i in [1, 2, 3]]",
    };
    Base::PyGILStateLocker lock;
    for (const char* src : cases)
        expectRoutedMatchesNative(obj, src);

    // and a function does not see its caller's names, either way
    const char* leak = "secret = 5\nimport brackets\nbrackets.leak()";
    std::string nerr, rerr;
    PyObject* native = evalProgram(obj, leak, false, nerr);
    PyObject* routed = evalProgram(obj, leak, true, rerr);
    EXPECT_EQ(native, nullptr) << reprOf(native);
    EXPECT_EQ(routed, nullptr) << reprOf(routed);
    Py_XDECREF(native);
    Py_XDECREF(routed);
    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionRoutingTest, librariesGuestKeepsTheModule)
{
    // The guest asks for a library's text once and keeps the module while
    // its revision stands; an edit drops it, and the next import asks
    // again and runs the new text.
    auto lib = addLibrary(doc, "Lib", "brackets", bracketsText);
    ASSERT_NE(lib, nullptr);
    auto& host = ImageHost::instance();
    Base::PyGILStateLocker lock;
    host.resetStats();
    const char* src = "import brackets\nbrackets.triple(Width)";
    for (int i = 0; i < 3; ++i)
        expectRoutedMatchesNative(obj, src);
    EXPECT_EQ(libSourceOps(), 1u);

    lib->Text.setValue("k = 4\ndef triple(x):\n    return x * k\n");
    expectRoutedMatchesNative(obj, src);
    EXPECT_EQ(libSourceOps(), 2u);

    // a plain import is no library and never asks
    expectRoutedMatchesNative(obj, "import math\nmath.sqrt(Width)");
    EXPECT_EQ(libSourceOps(), 2u);
}

TEST_F(ExpressionRoutingTest, librariesImportedLibraryFollowsAnEdit)
{
    // `more` imports `brackets`: an edit to brackets rebuilds more's module
    // in the guest, as natively, rather than leave it holding a module whose
    // dict was cleared when brackets was dropped.
    auto lib = addLibrary(doc, "Lib", "brackets", bracketsText);
    ASSERT_NE(lib, nullptr);
    ASSERT_NE(addLibrary(doc,
                         "More",
                         "more",
                         "import brackets\ndef sixfold(x):\n    return brackets.triple(x) * 2\n"),
              nullptr);
    Base::PyGILStateLocker lock;
    const char* src = "import more\nmore.sixfold(Width)";
    expectRoutedMatchesNative(obj, src);
    lib->Text.setValue("k = 4\ndef triple(x):\n    return x * k\n");
    expectRoutedMatchesNative(obj, src);
    std::string err;
    PyObject* v = evalProgram(obj, src, true, err);
    ASSERT_NE(v, nullptr) << err;
    EXPECT_EQ(PyFloat_AsDouble(v), 21.0 * 4 * 2) << reprOf(v);
    Py_DECREF(v);
    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionRoutingTest, librariesTwoDocumentsDoNotMeet)
{
    // The same module name in two documents: the guest keeps one module per
    // principal, so neither sees the other's.
    ASSERT_NE(addLibrary(doc, "Lib", "brackets", bracketsText), nullptr);
    auto other = App::GetApplication().newDocument("FcxEvalOther", "testUser");
    auto otherObj = other->addObject("App::FeaturePython", "Obj");
    ASSERT_NE(addLibrary(other, "Lib", "brackets", "k = 7\ndef triple(x):\n    return x * k\n"),
              nullptr);
    {
        Base::PyGILStateLocker lock;
        const char* src = "import brackets\nbrackets.triple(2)";
        std::string err;
        PyObject* first = evalProgram(obj, src, true, err);
        ASSERT_NE(first, nullptr) << err;
        PyObject* second = evalProgram(otherObj, src, true, err);
        ASSERT_NE(second, nullptr) << err;
        EXPECT_NE(reprOf(first), reprOf(second));
        EXPECT_EQ(PyFloat_AsDouble(first) * 7, PyFloat_AsDouble(second) * 3)
            << reprOf(first) << " " << reprOf(second);
        Py_DECREF(first);
        Py_DECREF(second);
        ImageHost::instance().clearHandles();
        expectRoutedMatchesNative(otherObj, src);
    }
    App::GetApplication().closeDocument(other->getName());
}

TEST_F(ExpressionRoutingTest, librariesBracketMatchesNative)
{
    // The bracket of docs/Sandbox.md 7.17 as a library and a consumer:
    // routed under enforcement, native with enforcement off (natively
    // `import Part` is host.import), the BRep byte-identical.
    {
        Base::PyGILStateLocker lock;
        PyObject* part = PyImport_ImportModule("Part");
        if (!part) {
            PyErr_Clear();
            GTEST_SKIP() << "the Part module is not importable in this test binary";
        }
        Py_DECREF(part);
    }
    ASSERT_NE(addLibrary(doc,
                         "Lib",
                         "brackets",
                         "import Part\n"
                         "\n"
                         "def bracket(L, W, T):\n"
                         "    base = Part.makeBox(L, W, T)\n"
                         "    wall = Part.makeBox(T, W, L)\n"
                         "    return base.fuse(wall).removeSplitter()\n"),
              nullptr);
    const char* src = "from brackets import bracket\nbracket(Width * 2, Width, 5mm)";

    Base::PyGILStateLocker lock;
    std::string rerr, nerr;
    PyObject* routed = evalProgram(obj, src, true, rerr);
    ASSERT_NE(routed, nullptr) << rerr;
    auto security = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Expression/Security");
    security->SetBool("Enforce", false);
    PyObject* native = evalProgram(obj, src, false, nerr);
    security->RemoveBool("Enforce");
    ASSERT_NE(native, nullptr) << nerr;

    PyObject* rv = PyObject_GetAttrString(routed, "Volume");
    PyObject* nv = PyObject_GetAttrString(native, "Volume");
    EXPECT_EQ(reprOf(rv), reprOf(nv));
    Py_XDECREF(rv);
    Py_XDECREF(nv);
    PyObject* rb = PyObject_CallMethod(routed, "exportBrepToString", nullptr);
    PyObject* nb = PyObject_CallMethod(native, "exportBrepToString", nullptr);
    ASSERT_NE(rb, nullptr);
    ASSERT_NE(nb, nullptr);
    EXPECT_TRUE(PyUnicode_Compare(rb, nb) == 0) << "the BRep is not byte-identical";
    Py_DECREF(rb);
    Py_DECREF(nb);
    Py_DECREF(routed);
    Py_DECREF(native);
    ImageHost::instance().clearHandles();
}

// ---- docs/Sandbox.md 7.17 D2: a library linked from another file ----

namespace
{
/// A second document holding the brackets library, saved: a link names a file.
struct SourceDocument
{
    std::string name;
    std::string path;
    App::Document* doc = nullptr;
    App::ExpressionLibrary* lib = nullptr;

    explicit SourceDocument(const char* docName, const char* text = bracketsText)
        : name(docName)
    {
        doc = App::GetApplication().newDocument(docName, "testUser");
        name = doc->getName();
        lib = addLibrary(doc, "Lib", "brackets", text);
        path = Base::FileInfo::getTempPath() + name + ".FCStd";
        doc->saveAs(path.c_str());
    }
    ~SourceDocument()
    {
        if (App::GetApplication().getDocument(name.c_str()))
            App::GetApplication().closeDocument(name.c_str());
        Base::FileInfo(path).deleteFile();
    }
};

App::ExpressionLibrary* addLink(App::Document* doc,
                                const char* name,
                                const char* module,
                                App::ExpressionLibrary* source,
                                bool pinned = false)
{
    auto lib = Base::freecad_dynamic_cast<App::ExpressionLibrary>(
        doc->addObject("App::ExpressionLibrary", name));
    if (lib) {
        lib->Module.setValue(module);
        lib->Source.setValue(source);
        if (pinned)
            lib->Pinned.setValue(true);
    }
    return lib;
}

double routedNumber(App::DocumentObject* owner, const char* src)
{
    std::string err;
    PyObject* v = evalProgram(owner, src, true, err);
    EXPECT_NE(v, nullptr) << src << "\n" << err;
    double res = v ? PyFloat_AsDouble(v) : -1.0;
    Py_XDECREF(v);
    ImageHost::instance().clearHandles();
    return res;
}
}  // namespace

TEST_F(ExpressionRoutingTest, librariesLinkedLiveMatchesNative)
{
    // The source's module under the consumer's name, routed = native, and
    // an edit in the source followed.
    SourceDocument source("FcxLibSource");
    ASSERT_NE(source.lib, nullptr);
    ASSERT_NE(addLink(doc, "LibB", "brk", source.lib), nullptr);
    Base::PyGILStateLocker lock;
    expectRoutedMatchesNative(obj, "import brk\nbrk.triple(Width)");
    expectRoutedMatchesNative(obj, "from brk import viaLater\nviaLater(Width)");
    source.lib->Text.setValue("k = 4\ndef triple(x):\n    return x * k\n");
    expectRoutedMatchesNative(obj, "import brk\nbrk.triple(2)");
    EXPECT_EQ(routedNumber(obj, "import brk\nbrk.triple(2)"), 8.0);
}

TEST_F(ExpressionRoutingTest, librariesLinkedConsumersShareOneModule)
{
    // Two documents linking one source under the same name: the guest keys
    // the module by the source's principal and builds it once.
    SourceDocument source("FcxLibShared");
    ASSERT_NE(source.lib, nullptr);
    auto other = App::GetApplication().newDocument("FcxLibConsumer", "testUser");
    auto otherObj = other->addObject("App::FeaturePython", "Obj");
    EXPECT_NE(addLink(doc, "LibB", "brk", source.lib), nullptr);
    EXPECT_NE(addLink(other, "LibB", "brk", source.lib), nullptr);
    {
        Base::PyGILStateLocker lock;
        ImageHost::instance().resetStats();
        EXPECT_EQ(routedNumber(obj, "import brk\nbrk.triple(2)"), 6.0);
        EXPECT_EQ(routedNumber(otherObj, "import brk\nbrk.triple(2)"), 6.0);
        EXPECT_EQ(libSourceOps(), 1u);
    }
    App::GetApplication().closeDocument(other->getName());
}

TEST_F(ExpressionRoutingTest, librariesLinkedImportsInItsOwnFile)
{
    // The source's text imports a library of the SOURCE's document, which
    // the consumer's does not hold: routed, the nested lib.source names
    // that document, and an edit there is followed.
    SourceDocument source("FcxLibHome",
                          "import helpers\ndef quad(x):\n    return helpers.double(x) * 2\n");
    ASSERT_NE(source.lib, nullptr);
    auto helper = addLibrary(source.doc, "Helper", "helpers", "def double(x):\n    return x * 2\n");
    ASSERT_NE(helper, nullptr);
    ASSERT_NE(addLink(doc, "LibB", "brk", source.lib), nullptr);
    EXPECT_EQ(App::ExpressionLibrary::find(doc, "helpers"), nullptr);
    Base::PyGILStateLocker lock;
    expectRoutedMatchesNative(obj, "import brk\nbrk.quad(Width)");
    EXPECT_EQ(routedNumber(obj, "import brk\nbrk.quad(2)"), 8.0);
    helper->Text.setValue("def double(x):\n    return x * 3\n");
    expectRoutedMatchesNative(obj, "import brk\nbrk.quad(Width)");
    EXPECT_EQ(routedNumber(obj, "import brk\nbrk.quad(2)"), 12.0);
}

TEST_F(ExpressionRoutingTest, librariesPinnedMatchesNative)
{
    // Pinned: the snapshot taken at the pin, whatever the source does after.
    SourceDocument source("FcxLibPinned");
    ASSERT_NE(source.lib, nullptr);
    auto link = addLink(doc, "LibB", "brk", source.lib, true);
    ASSERT_NE(link, nullptr);
    EXPECT_STREQ(link->Snapshot.getValue(), bracketsText);
    source.lib->Text.setValue("k = 4\ndef triple(x):\n    return x * k\n");
    Base::PyGILStateLocker lock;
    expectRoutedMatchesNative(obj, "import brk\nbrk.triple(2)");
    EXPECT_EQ(routedNumber(obj, "import brk\nbrk.triple(2)"), 6.0);
}

TEST_F(ExpressionRoutingTest, librariesLinkUnresolvedFailsBothWays)
{
    // The source document gone, the link unpinned: the import fails with
    // the link's reason, natively and routed.
    auto source = std::make_unique<SourceDocument>("FcxLibGone");
    ASSERT_NE(source->lib, nullptr);
    auto link = addLink(doc, "LibB", "brk", source->lib);
    ASSERT_NE(link, nullptr);
    // the consumer never saved: no DocInfo watches the source for its link
    source.reset();
    ASSERT_EQ(link->getHolder(), nullptr);
    Base::PyGILStateLocker lock;
    std::string nerr, rerr;
    PyObject* native = evalProgram(obj, "import brk\nbrk.k", false, nerr);
    PyObject* routed = evalProgram(obj, "import brk\nbrk.k", true, rerr);
    EXPECT_EQ(native, nullptr) << reprOf(native);
    EXPECT_EQ(routed, nullptr) << reprOf(routed);
    EXPECT_NE(nerr.find("Source not found"), std::string::npos) << nerr;
    EXPECT_NE(rerr.find("Source not found"), std::string::npos) << rerr;
    Py_XDECREF(native);
    Py_XDECREF(routed);
    ImageHost::instance().clearHandles();
}

TEST_F(ExpressionImageEvalTest, programsSurfaceStampMatchesHost)
{
    // The guest's copy of the surface stamp is the host's.  A wheel built
    // from other annotations is a guest whose facades are not the host's
    // dispatch table, which nothing else would notice until a member
    // failed to cross.
    auto& host = ImageHost::instance();
    auto res = host.eval("__import__('_fcx').surface()", {});
    ASSERT_TRUE(res.ok) << res.excType << ": " << res.message;
    Base::PyGILStateLocker lock;
    PyObject* v = host.decodeResult(res);
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(PyTuple_Check(v) && PyTuple_GET_SIZE(v) == 2) << reprOf(v);
    EXPECT_EQ(PyLong_AsLong(PyTuple_GET_ITEM(v, 0)), App::ExpressionSandbox::surfaceVersion());
    EXPECT_STREQ(PyUnicode_AsUTF8(PyTuple_GET_ITEM(v, 1)), App::ExpressionSandbox::surfaceHash());
    Py_DECREF(v);
    EXPECT_GE(App::ExpressionSandbox::surfaceVersion(), 1);
    EXPECT_EQ(std::strlen(App::ExpressionSandbox::surfaceHash()), 64u);
}

TEST_F(ExpressionImageEvalTest, programsSurfaceRecordedAtSave)
{
    // Meta["ExpressionSurface"] is written as a document that carries an
    // expression saves, and not on one that carries none.
    auto out = Base::freecad_dynamic_cast<App::PropertyFloat>(
        obj->addDynamicProperty("App::PropertyFloat", "Out"));
    ASSERT_NE(out, nullptr);
    App::ObjectIdentifier path(*obj->getPropertyByName("Out"));
    obj->ExpressionEngine.setValue(
        path,
        std::shared_ptr<App::Expression>(App::Expression::parse(obj, "Width * 2").release()));
    std::string file = Base::FileInfo::getTempFileName() + ".FCStd";
    ASSERT_TRUE(doc->saveAs(file.c_str()));
    const char* stamp = doc->Meta.getValue("ExpressionSurface");
    ASSERT_NE(stamp, nullptr);
    EXPECT_EQ(std::string(stamp), std::to_string(App::ExpressionSandbox::surfaceVersion()));

    auto plain = App::GetApplication().newDocument("FcxSurfacePlain", "plain");
    plain->addObject("App::FeaturePython", "Nothing");
    std::string plainFile = Base::FileInfo::getTempFileName() + ".FCStd";
    ASSERT_TRUE(plain->saveAs(plainFile.c_str()));
    EXPECT_EQ(plain->Meta.getValue("ExpressionSurface"), nullptr);
    App::GetApplication().closeDocument(plain->getName());
    Base::FileInfo(file).deleteFile();
    Base::FileInfo(plainFile).deleteFile();
}

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
    EXPECT_EQ(FcxWire::fromCbor(res.value)
                  .get<std::string>(), "tuple");
    table.clear();
}
