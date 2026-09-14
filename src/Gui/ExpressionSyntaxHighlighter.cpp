/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QSet>
# include <QTextCharFormat>
#endif

#include <App/Application.h>
#include <App/Color.h>
#include <App/ExpressionParser.h>
#include <Base/Parameter.h>

#include "ExpressionSyntaxHighlighter.h"

using namespace Gui;

namespace
{

// The block state: an open triple-quoted string in the low bits, the Python
// mode of '#@pybegin' above them, then the parity of the expression block
// and a flag saying the previous line was a first header line -- so that the
// comment line under it, which may itself start with "##@@ ", does not flip
// the parity again.
enum BlockState
{
    Standard = 0,
    TripleDouble = 1,
    TripleSingle = 2,
    QuoteMask = 3,
    PyMode = 4,
    AlternateBlock = 8,
    HeaderPending = 16,
};

bool isStringPrefix(const QString& word)
{
    if (word.size() > 2) {
        return false;
    }
    for (QChar c : word) {
        if (!QStringLiteral("rRbBfFuU").contains(c)) {
            return false;
        }
    }
    return true;
}

}  // namespace

class ExpressionSyntaxHighlighter::Private
{
public:
    Private()
    {
        // App/ExpressionParser.l, plus the constants.
        for (const char* kw : {"and",    "as",     "break", "continue", "def",    "del",
                               "elif",   "else",   "except", "finally", "for",    "from",
                               "global", "if",     "import", "in",      "is",     "lambda",
                               "nonlocal", "not",  "or",    "pass",     "raise",  "return",
                               "try",    "while",  "None",  "True",     "False"}) {
            keywords.insert(QString::fromLatin1(kw));
        }
    }

    QSet<QString> keywords;
};

ExpressionSyntaxHighlighter::ExpressionSyntaxHighlighter(QObject* parent)
    : SyntaxHighlighter(parent)
    , d(new Private)
{}

ExpressionSyntaxHighlighter::~ExpressionSyntaxHighlighter() = default;

bool ExpressionSyntaxHighlighter::isAlternateBlock(int userState)
{
    return userState >= 0 && (userState & AlternateBlock) != 0;
}

bool ExpressionSyntaxHighlighter::isBuiltinFunction(const QString& name)
{
    static const QSet<QString> functions = [] {
        QSet<QString> res;
        for (auto& info : App::FunctionExpression::getFunctions()) {
            res.insert(QString::fromLatin1(info.name));
        }
        return res;
    }();
    return functions.contains(name);
}

void ExpressionSyntaxHighlighter::loadEditorColors()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Editor");
    for (const char* name : {"Text", "Comment", "Block comment", "Number", "String", "Keyword",
                             "Class name", "Define name", "Operator", "Python output",
                             "Python error"}) {
        QString type = QString::fromLatin1(name);
        QColor col = color(type);
        auto value = static_cast<unsigned long>(App::Color::asPackedRGB<QColor>(col));
        value = hGrp->GetUnsigned(name, value);
        col.setRgb((value >> 24) & 0xff, (value >> 16) & 0xff, (value >> 8) & 0xff);
        setColor(type, col);
    }
}

void ExpressionSyntaxHighlighter::highlightBlock(const QString& text)
{
    int state = previousBlockState();
    if (state < 0) {
        state = Standard;
    }
    int quote = state & QuoteMask;
    bool py = (state & PyMode) != 0;
    const int parity = state & AlternateBlock;

    const int len = static_cast<int>(text.size());
    auto at = [&](int j) {
        return j < len ? text.at(j) : QChar();
    };

    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);

    // The header lines of the copy format. A header starts a new
    // expression, so whatever the previous body left open ends here.
    if (quote == 0 && text.startsWith(QLatin1String("##@@"))) {
        QTextCharFormat fmt(bold);
        fmt.setForeground(colorByType(SyntaxHighlighter::Output));
        setFormat(0, len, fmt);
        if (!(state & HeaderPending) && text.startsWith(QLatin1String("##@@ "))) {
            setCurrentBlockState((parity ^ AlternateBlock) | HeaderPending);
        }
        else {
            setCurrentBlockState(parity);
        }
        return;
    }

    QTextCharFormat keyword(bold);
    keyword.setForeground(colorByType(SyntaxHighlighter::Keyword));

    // A string from start (its prefix, if any) whose quote is at qpos.
    // Returns the position after what was formatted; a triple quote left
    // open sets 'quote' for the rest of the line and the next ones.
    auto scanString = [&](int start, int qpos) {
        QChar q = text.at(qpos);
        if (at(qpos + 1) == q && at(qpos + 2) == q) {
            quote = q == QLatin1Char('"') ? TripleDouble : TripleSingle;
            setFormat(start, qpos + 3 - start, colorByType(SyntaxHighlighter::String));
            return qpos + 3;
        }
        int j = qpos + 1;
        while (j < len && text.at(j) != q) {
            j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
        }
        int end = std::min(j + 1, len);
        setFormat(start, end - start, colorByType(SyntaxHighlighter::String));
        return end;
    };

    bool expectDefName = false;
    int i = 0;
    while (i < len) {
        if (quote) {
            QChar q = quote == TripleDouble ? QLatin1Char('"') : QLatin1Char('\'');
            int end = -1;
            for (int j = i; j < len; ++j) {
                if (text.at(j) == QLatin1Char('\\')) {
                    ++j;
                }
                else if (text.at(j) == q && at(j + 1) == q && at(j + 2) == q) {
                    end = j + 3;
                    break;
                }
            }
            if (end < 0) {
                setFormat(i, len - i, colorByType(SyntaxHighlighter::String));
                break;
            }
            setFormat(i, end - i, colorByType(SyntaxHighlighter::String));
            i = end;
            quote = 0;
            continue;
        }

        QChar ch = text.at(i);
        if (ch.isSpace()) {
            ++i;
            continue;
        }

        if (ch == QLatin1Char('#')) {
            if (text.mid(i, 9) == QLatin1String("#@pybegin")) {
                setFormat(i, 9, keyword);
                py = true;
                i += 9;
                continue;
            }
            if (text.mid(i, 7) == QLatin1String("#@pyend")) {
                setFormat(i, 7, keyword);
                py = false;
                i += 7;
                continue;
            }
            // Python mode: any '#' not starting a marker. Otherwise only
            // '#' before a blank or the line end -- Doc#Obj is a reference.
            QChar next = at(i + 1);
            bool comment = py ? next != QLatin1Char('@')
                              : (next.isNull() || next == QLatin1Char(' ')
                                 || next == QLatin1Char('\t'));
            if (comment) {
                setFormat(i, len - i, colorByType(SyntaxHighlighter::Comment));
                break;
            }
            setFormat(i, 1, colorByType(SyntaxHighlighter::Operator));
            ++i;
            continue;
        }

        if (ch == QLatin1Char('<') && at(i + 1) == QLatin1Char('<')) {
            int end = len;
            for (int j = i + 2; j < len; ++j) {
                if (text.at(j) == QLatin1Char('\\')) {
                    ++j;
                }
                else if (text.at(j) == QLatin1Char('>') && at(j + 1) == QLatin1Char('>')) {
                    end = j + 2;
                    break;
                }
            }
            setFormat(i, end - i, colorByType(SyntaxHighlighter::String));
            i = end;
            continue;
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            i = scanString(i, i);
            continue;
        }

        if (ch.isDigit() || (ch == QLatin1Char('.') && at(i + 1).isDigit())) {
            int j = i;
            while (j < len && (text.at(j).isDigit() || text.at(j) == QLatin1Char('.'))) {
                ++j;
            }
            QChar e = at(j);
            if (e == QLatin1Char('e') || e == QLatin1Char('E')) {
                QChar sign = at(j + 1);
                if (sign.isDigit()) {
                    j += 1;
                }
                else if ((sign == QLatin1Char('+') || sign == QLatin1Char('-'))
                         && at(j + 2).isDigit()) {
                    j += 2;
                }
                while (j < len && text.at(j).isDigit()) {
                    ++j;
                }
            }
            // The unit written against the number: 5mm, 360deg.
            while (j < len && text.at(j).isLetter()) {
                ++j;
            }
            setFormat(i, j - i, colorByType(SyntaxHighlighter::Number));
            i = j;
            continue;
        }

        if (ch.isLetter() || ch == QLatin1Char('_')) {
            int j = i;
            while (j < len && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('_'))) {
                ++j;
            }
            QString word = text.mid(i, j - i);
            QChar next = at(j);
            if ((next == QLatin1Char('"') || next == QLatin1Char('\'')) && isStringPrefix(word)) {
                i = scanString(i, j);
                continue;
            }
            bool member = i > 0 && text.at(i - 1) == QLatin1Char('.');
            if (!member && d->keywords.contains(word)) {
                setFormat(i, j - i, keyword);
                expectDefName = word == QLatin1String("def");
            }
            else if (expectDefName) {
                setFormat(i, j - i, colorByType(SyntaxHighlighter::Defname));
                expectDefName = false;
            }
            else {
                int k = j;
                while (k < len && text.at(k).isSpace()) {
                    ++k;
                }
                if (!member && at(k) == QLatin1Char('(') && isBuiltinFunction(word)) {
                    setFormat(i, j - i, colorByType(SyntaxHighlighter::Classname));
                }
                else {
                    setFormat(i, j - i, colorByType(SyntaxHighlighter::Text));
                }
            }
            i = j;
            continue;
        }

        setFormat(i, 1, colorByType(SyntaxHighlighter::Operator));
        ++i;
    }

    setCurrentBlockState(quote | (py ? PyMode : 0) | parity);
}
