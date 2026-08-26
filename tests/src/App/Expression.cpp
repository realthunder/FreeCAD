#include "gtest/gtest.h"

#include "App/ExpressionParser.h"
#include "App/ExpressionTokenizer.h"

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
// clang-format on
