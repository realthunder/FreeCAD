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
