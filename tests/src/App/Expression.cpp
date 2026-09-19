#include "gtest/gtest.h"

#include "App/ExpressionParser.h"
#include "App/ExpressionTokenizer.h"
#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "InitApplication.h"

// clang-format off
TEST(Expression, tokenize)
{
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8(""), 10), QString());
    // 0.0000 deg-
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8("0.00000 \xC2\xB0-"), 10), QString());
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8("0.00000 \xC2\xB0-s"), 11), QString::fromLatin1("s"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8("0.00000 \xC2\xB0-ss"), 12), QString::fromLatin1("ss"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8("0.00000 deg"), 5), QString());
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromUtf8("0.00000 deg"), 11), QString::fromLatin1("deg"));
}

TEST(Expression, tokenizePi)
{
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("p"), 1), QString::fromLatin1("p"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi"), 2), QString());
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi "), 3), QString());
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi r"), 4), QString::fromLatin1("r"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi ra"), 5), QString::fromLatin1("ra"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi rad"), 6), QString::fromLatin1("rad"));
    EXPECT_EQ(App::ExpressionTokenizer().perform(QString::fromLatin1("pi rad"), 2), QString());
}

TEST(Expression, toString)
{
    auto expr = App::UnitExpression::create(nullptr, "rad");
    EXPECT_EQ(expr->toString(), "rad");
}

TEST(Expression, test_pi_rad)
{
    auto expr = App::Expression::parse(nullptr, "pi rad");
    EXPECT_EQ(expr->toString(), "pi rad");
}

TEST(Expression, test_e_rad)
{
    auto expr = App::Expression::parse(nullptr, "e rad");
    EXPECT_EQ(expr->toString(), "e rad");
}

// A statement's identifiers print through their owner -- with none, `x`
// prints as nothing -- so the cases parse for an object of a document.
class ExpressionStatementPrint: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
    void SetUp() override
    {
        doc = App::GetApplication().newDocument("ExprStatementPrint", "testUser");
        owner = doc->addObject("App::FeaturePython", "Owner");
    }
    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }
    App::Document* doc = nullptr;
    App::DocumentObject* owner = nullptr;
};

TEST_F(ExpressionStatementPrint, oneLineCompoundStatementParsesAgain)
{
    // The whole expression one compound statement with its body on the same
    // line: printed without its line end it did not parse again, so a sheet
    // cell saved that way failed on reopen and a routed evaluation failed at
    // once.  Text already spanning lines prints as it did.
    const char* oneLine[] = {"if 1: 2\n", "while 0: 1\n", "for i in [1]: i\n",
                             "def f(): return 1\n", "def g(x): return x * 3\n"};
    for (const char* src : oneLine) {
        std::string printed;
        try {
            auto expr = App::Expression::parse(owner, src);
            ASSERT_TRUE(expr) << src;
            printed = expr->toString();
            EXPECT_EQ(printed.back(), '\n') << src;
            auto again = App::Expression::parse(owner, printed);
            ASSERT_TRUE(again) << printed;
            EXPECT_EQ(again->toString(), printed) << src;
        }
        catch (const Base::Exception& e) {
            ADD_FAILURE() << src << " printed as '" << printed << "': " << e.what();
        }
    }
    auto multi = App::Expression::parse(owner, "def f(x):\n    return x\n");
    ASSERT_TRUE(multi);
    EXPECT_EQ(multi->toString(), "def f(x):\n    return x");
    auto two = App::Expression::parse(owner, "x = 1\nif x: 2\n");
    ASSERT_TRUE(two);
    EXPECT_EQ(two->toString(), "x = 1\nif x: 2");
}
// clang-format on
