// SPDX-License-Identifier: LGPL-2.1-or-later

/// The expression editor (src/Gui/ExpressionEditorView.h) below its window:
/// the text form it edits, the diff it shows and the highlighter it colors
/// with.
///
/// The text form is what the copy and paste entries of Std_Expressions
/// already exchanged, so parse, dump and apply are checked against a live
/// document: a dump applied back changes nothing, an edit rebinds, a lone
/// '#' unbinds, and a body that fails to parse changes nothing at all.

#include <gtest/gtest.h>

#include <array>
#include <random>
#include <sstream>
#include <string>

#include <QApplication>
#include <QPlainTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <App/FeatureTest.h>
#include <App/ObjectIdentifier.h>
#include <Gui/ExpressionEditorView.h>
#include <Gui/ExpressionSyntaxHighlighter.h>

using namespace Gui;

namespace
{

class ExpressionEditorTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        App::Application::Config()["ExeName"] = "ExpressionEditor_tests_run";
        int argc = 1;
        static std::array<char, 32> exename {"ExpressionEditor_tests_run"};
        std::array<char*, 2> argv {exename.data(), nullptr};
        App::Application::init(argc, argv.data());

        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int qargc = 1;
        static std::array<char, 32> qexe {"ExpressionEditor_tests_run"};
        static std::array<char*, 2> qargv {qexe.data(), nullptr};
        app = new QApplication(qargc, qargv.data());
    }

    static void TearDownTestSuite()
    {
        delete app;
        app = nullptr;
    }

    static QApplication* app;
};

QApplication* ExpressionEditorTest::app = nullptr;

// ---------------------------------------------------------------------------
// The highlighter

// One color per kind, so a format names what the highlighter decided.
const QColor kText(10, 0, 0);
const QColor kComment(20, 0, 0);
const QColor kNumber(30, 0, 0);
const QColor kString(40, 0, 0);
const QColor kKeyword(50, 0, 0);
const QColor kClass(60, 0, 0);
const QColor kDefine(70, 0, 0);
const QColor kOperator(80, 0, 0);
const QColor kHeader(90, 0, 0);

class Highlighted
{
public:
    explicit Highlighted(const char* text)
    {
        // SyntaxHighlighter takes a QObject parent, which QSyntaxHighlighter
        // does not attach to a document: that is setDocument's job.
        auto highlighter = new ExpressionSyntaxHighlighter(&doc);
        highlighter->setDocument(&doc);
        highlighter->setColor(QStringLiteral("Text"), kText);
        highlighter->setColor(QStringLiteral("Comment"), kComment);
        highlighter->setColor(QStringLiteral("Number"), kNumber);
        highlighter->setColor(QStringLiteral("String"), kString);
        highlighter->setColor(QStringLiteral("Keyword"), kKeyword);
        highlighter->setColor(QStringLiteral("Class name"), kClass);
        highlighter->setColor(QStringLiteral("Define name"), kDefine);
        highlighter->setColor(QStringLiteral("Operator"), kOperator);
        highlighter->setColor(QStringLiteral("Python output"), kHeader);
        doc.setPlainText(QString::fromUtf8(text));
        highlighter->rehighlight();
    }

    /// The foreground at \a column of \a line, invalid when unformatted.
    QColor at(int line, int column) const
    {
        QTextBlock block = doc.findBlockByNumber(line);
        for (const auto& range : block.layout()->formats()) {
            if (column >= range.start && column < range.start + range.length) {
                return range.format.foreground().color();
            }
        }
        return {};
    }

private:
    QTextDocument doc;
};

TEST_F(ExpressionEditorTest, hashIsACommentOnlyBeforeABlank)
{
    Highlighted h("x = Box#Shape  # note");
    EXPECT_EQ(h.at(0, 2), kOperator);
    EXPECT_EQ(h.at(0, 4), kText);
    EXPECT_EQ(h.at(0, 7), kOperator);  // Doc#Obj, not a comment
    EXPECT_EQ(h.at(0, 8), kText);
    EXPECT_EQ(h.at(0, 15), kComment);
    EXPECT_EQ(h.at(0, 20), kComment);
}

TEST_F(ExpressionEditorTest, numbersCarryTheirUnit)
{
    Highlighted h("5mm + 1.5e3 * 360deg");
    EXPECT_EQ(h.at(0, 0), kNumber);
    EXPECT_EQ(h.at(0, 2), kNumber);
    EXPECT_EQ(h.at(0, 4), kOperator);
    EXPECT_EQ(h.at(0, 6), kNumber);
    EXPECT_EQ(h.at(0, 10), kNumber);
    EXPECT_EQ(h.at(0, 19), kNumber);
}

TEST_F(ExpressionEditorTest, angleBracketsAreAString)
{
    Highlighted h("<<My Label>>.Length");
    EXPECT_EQ(h.at(0, 0), kString);
    EXPECT_EQ(h.at(0, 11), kString);
    EXPECT_EQ(h.at(0, 12), kOperator);
    EXPECT_EQ(h.at(0, 13), kText);
}

TEST_F(ExpressionEditorTest, tripleQuotedStringSpansLines)
{
    Highlighted h("a = \"\"\"one\ntwo\"\"\" + b");
    EXPECT_EQ(h.at(0, 4), kString);
    EXPECT_EQ(h.at(0, 7), kString);
    EXPECT_EQ(h.at(1, 0), kString);
    EXPECT_EQ(h.at(1, 5), kString);
    EXPECT_EQ(h.at(1, 7), kOperator);
    EXPECT_EQ(h.at(1, 9), kText);
}

TEST_F(ExpressionEditorTest, pythonModeTakesEveryHashAsAComment)
{
    Highlighted h("#@pybegin\nx = 1 #c\n#@pyend\ny#z");
    EXPECT_EQ(h.at(0, 0), kKeyword);
    EXPECT_EQ(h.at(1, 4), kNumber);
    EXPECT_EQ(h.at(1, 6), kComment);
    EXPECT_EQ(h.at(2, 0), kKeyword);
    EXPECT_EQ(h.at(3, 1), kOperator);  // back out: a reference again
}

TEST_F(ExpressionEditorTest, keywordsDefinitionsAndBuiltins)
{
    Highlighted h("def f(a): return cos(a)\nx.cos(1)");
    EXPECT_EQ(h.at(0, 0), kKeyword);
    EXPECT_EQ(h.at(0, 4), kDefine);
    EXPECT_EQ(h.at(0, 6), kText);
    EXPECT_EQ(h.at(0, 10), kKeyword);
    EXPECT_EQ(h.at(0, 17), kClass);
    EXPECT_EQ(h.at(1, 2), kText);  // a member, not the builtin
}

TEST_F(ExpressionEditorTest, headerLines)
{
    Highlighted h("##@@ .Integer Doc#Obj.ExpressionEngine (Obj)\n##@@\n1");
    EXPECT_EQ(h.at(0, 0), kHeader);
    EXPECT_EQ(h.at(0, 20), kHeader);
    EXPECT_EQ(h.at(1, 0), kHeader);
    EXPECT_EQ(h.at(2, 0), kNumber);
}

TEST_F(ExpressionEditorTest, blocksAlternate)
{
    // Three blocks; the second's comment starts with a blank, so its comment
    // line reads "##@@ ..." and must not count as another header.
    QTextDocument doc;
    // The layout a QPlainTextEdit gives its document. Without one an edit
    // announces no contentsChange, and the highlighter never hears of it.
    doc.setDocumentLayout(new QPlainTextDocumentLayout(&doc));
    auto highlighter = new ExpressionSyntaxHighlighter(&doc);
    highlighter->setDocument(&doc);
    doc.setPlainText(QStringLiteral("##@@ .a D#O.ExpressionEngine (O)\n"
                                    "##@@\n"
                                    "1\n"
                                    "\n"
                                    "##@@ .b D#O.ExpressionEngine (O)\n"
                                    "##@@ a comment\n"
                                    "2\n"
                                    "##@@ .c D#O.ExpressionEngine (O)\n"
                                    "##@@\n"
                                    "\"\"\"x\n"
                                    "##@@ not a header, inside a string\"\"\"\n"));
    highlighter->rehighlight();
    std::string parity;
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        parity += ExpressionSyntaxHighlighter::isAlternateBlock(block.userState()) ? '1' : '0';
    }
    // The trailing empty line is the third block's too.
    EXPECT_EQ(parity, "111100011111");

    // The second block's header removed, both lines: its body joins the first
    // block, and the block after it flips.
    QTextCursor cursor(doc.findBlockByNumber(4));
    cursor.setPosition(doc.findBlockByNumber(6).position(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    parity.clear();
    for (auto block = doc.begin(); block.isValid(); block = block.next()) {
        parity += ExpressionSyntaxHighlighter::isAlternateBlock(block.userState()) ? '1' : '0';
    }
    EXPECT_EQ(parity, "1111100000");
}

// ---------------------------------------------------------------------------
// The text form

TEST_F(ExpressionEditorTest, parseBlocks)
{
    auto parsed = ExpressionText::parse("##@@ .Integer Doc#Obj.ExpressionEngine (Obj)\n"
                                        "##@@\n"
                                        "Float * 2\n"
                                        "\n"
                                        "##@@ .Float Doc#Obj.ExpressionEngine (Obj)\n"
                                        "##@@note\n"
                                        "#\n");
    EXPECT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.blocks.size(), 2U);

    auto& first = parsed.blocks[0];
    EXPECT_EQ(first.path, ".Integer");
    EXPECT_EQ(first.docName, "Doc");
    EXPECT_EQ(first.objName, "Obj");
    EXPECT_EQ(first.propName, "ExpressionEngine");
    EXPECT_EQ(first.comment, "");
    EXPECT_EQ(first.body, "Float * 2\n\n");
    EXPECT_EQ(first.headerLine, 0);
    EXPECT_EQ(first.lastLine, 3);
    EXPECT_FALSE(first.isUnbind());
    EXPECT_EQ(first.key(), "Doc#Obj.ExpressionEngine .Integer");

    auto& second = parsed.blocks[1];
    EXPECT_EQ(second.comment, "note");
    EXPECT_EQ(second.headerLine, 4);
    EXPECT_EQ(second.lastLine, 7);
    EXPECT_TRUE(second.isUnbind());
}

TEST_F(ExpressionEditorTest, parseReportsStrayTextAndBadHeaders)
{
    auto parsed = ExpressionText::parse("stray\n##@@ nope\n##@@\n");
    EXPECT_TRUE(parsed.blocks.empty());
    ASSERT_EQ(parsed.errors.size(), 2U);
    EXPECT_EQ(parsed.errors[0].line, 0);
    EXPECT_EQ(parsed.errors[1].line, 1);
}

std::string exprText(App::DocumentObject* obj)
{
    auto exprs = obj->ExpressionEngine.getExpressions();
    if (exprs.size() != 1) {
        return "<" + std::to_string(exprs.size()) + " expressions>";
    }
    std::ostringstream ss;
    ss << exprs.begin()->second->toStr();
    return ss.str();
}

TEST_F(ExpressionEditorTest, dumpApplyRoundTrip)
{
    auto doc = App::GetApplication().newDocument("ExprEdit", "ExprEdit", false);
    auto a = doc->addObject("App::FeatureTest", "A");
    auto b = doc->addObject("App::FeatureTest", "B");
    a->setExpression(App::ObjectIdentifier::parse(a, "Integer"),
                     std::shared_ptr<App::Expression>(App::Expression::parse(a, "B.Integer + 1")));
    b->setExpression(App::ObjectIdentifier::parse(b, "Float"),
                     std::shared_ptr<App::Expression>(App::Expression::parse(b, "2.5")));
    // A program, which prints over more than one line.
    auto c = doc->addObject("App::FeatureTest", "C");
    c->setExpression(
        App::ObjectIdentifier::parse(c, "Integer"),
        std::shared_ptr<App::Expression>(App::Expression::parse(c, "x = 2\nx * B.Integer")));

    auto parsed = ExpressionText::parse(ExpressionText::dump({a, b, c}));
    ASSERT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.blocks.size(), 3U);
    EXPECT_EQ(parsed.blocks[0].objName, "A");
    EXPECT_EQ(parsed.blocks[1].objName, "B");
    EXPECT_EQ(parsed.blocks[2].objName, "C");

    // The dump says what the document holds: applying it changes nothing.
    auto same = ExpressionText::apply(parsed.blocks, true, "test");
    EXPECT_TRUE(same.errors.empty());
    EXPECT_EQ(same.changed, 0);

    // Rebind A, with an encoded two-line comment; unbind B.
    parsed.blocks[0].body = "B.Integer + 2\n";
    parsed.blocks[0].comment = "&one&#10;two";
    parsed.blocks[1].body = "#\n";
    auto res = ExpressionText::apply(parsed.blocks, true, "test");
    EXPECT_TRUE(res.errors.empty());
    EXPECT_EQ(res.changed, 2);
    EXPECT_EQ(exprText(a), "B.Integer + 2");
    EXPECT_EQ(a->ExpressionEngine.getExpressions().begin()->second->comment, "one\ntwo");
    EXPECT_TRUE(b->ExpressionEngine.getExpressions().empty());
    EXPECT_EQ(c->ExpressionEngine.getExpressions().size(), 1U);

    // A body that does not parse: an error on its line, and nothing changes.
    parsed.blocks[0].body = "B.Integer +\n";
    parsed.blocks[1].body = "3\n";
    auto bad = ExpressionText::apply(parsed.blocks, true, "test");
    ASSERT_EQ(bad.errors.size(), 1U);
    EXPECT_EQ(bad.errors[0].line, parsed.blocks[0].headerLine + 2);
    EXPECT_EQ(bad.changed, 0);
    EXPECT_EQ(exprText(a), "B.Integer + 2");
    EXPECT_TRUE(b->ExpressionEngine.getExpressions().empty());

    // A block naming no object: an error when strict, a warning otherwise.
    ExpressionText::Block ghost = parsed.blocks[1];
    ghost.objName = "Nope";
    auto strict = ExpressionText::apply({ghost}, true, "test");
    EXPECT_EQ(strict.errors.size(), 1U);
    auto lenient = ExpressionText::apply({ghost}, false, "test");
    EXPECT_TRUE(lenient.errors.empty());
    EXPECT_EQ(lenient.warnings.size(), 1U);
    EXPECT_EQ(lenient.changed, 0);

    App::GetApplication().closeDocument(doc->getName());
}

// ---------------------------------------------------------------------------
// The diff

std::string render(const std::vector<ExpressionText::DiffLine>& lines)
{
    std::string res;
    for (auto& line : lines) {
        if (!res.empty()) {
            res += ' ';
        }
        res += line.kind == ExpressionText::DiffLine::Added ? '+'
            : line.kind == ExpressionText::DiffLine::Removed ? '-'
                                                             : '=';
        res += line.text.toStdString();
    }
    return res;
}

QStringList split(const char* letters)
{
    QStringList res;
    for (const char* c = letters; *c; ++c) {
        res << QString(QLatin1Char(*c));
    }
    return res;
}

TEST_F(ExpressionEditorTest, diffSmallCases)
{
    using ExpressionText::diffLines;
    EXPECT_EQ(render(diffLines(split("abc"), split("axc"))), "=a -b +x =c");
    EXPECT_EQ(render(diffLines(split(""), split("ab"))), "+a +b");
    EXPECT_EQ(render(diffLines(split("ab"), split(""))), "-a -b");
    EXPECT_EQ(render(diffLines(split("ab"), split("ab"))), "=a =b");
    EXPECT_EQ(render(diffLines(split(""), split(""))), "");
}

TEST_F(ExpressionEditorTest, diffIsMinimalAndReconstructsBothSides)
{
    std::mt19937 rng(20260914);
    std::uniform_int_distribution<int> length(0, 12);
    std::uniform_int_distribution<int> letter(0, 2);
    for (int round = 0; round < 500; ++round) {
        QStringList from;
        QStringList to;
        for (int i = length(rng); i > 0; --i) {
            from << QString(QLatin1Char(char('a' + letter(rng))));
        }
        for (int i = length(rng); i > 0; --i) {
            to << QString(QLatin1Char(char('a' + letter(rng))));
        }

        auto lines = ExpressionText::diffLines(from, to);
        QStringList left;
        QStringList right;
        int edits = 0;
        for (auto& line : lines) {
            if (line.kind != ExpressionText::DiffLine::Added) {
                left << line.text;
            }
            if (line.kind != ExpressionText::DiffLine::Removed) {
                right << line.text;
            }
            edits += line.kind != ExpressionText::DiffLine::Same;
        }
        ASSERT_EQ(left, from);
        ASSERT_EQ(right, to);

        // Minimal: as many edits as the longest common subsequence allows.
        const auto n = from.size();
        const auto m = to.size();
        std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
        for (qsizetype i = 1; i <= n; ++i) {
            for (qsizetype j = 1; j <= m; ++j) {
                lcs[i][j] = from[i - 1] == to[j - 1] ? lcs[i - 1][j - 1] + 1
                                                     : std::max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }
        ASSERT_EQ(edits, int(n + m) - 2 * lcs[n][m]);
    }
}

}  // namespace
