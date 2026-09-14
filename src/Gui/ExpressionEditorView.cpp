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
# include <QApplication>
# include <QClipboard>
# include <QCloseEvent>
# include <QMessageBox>
# include <QPointer>
# include <QPushButton>
# include <QStackedWidget>
# include <QTextBlock>
# include <QTextCursor>
# include <QTimer>
# include <QToolBar>
#endif

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextDocumentLayout>
#include <QSet>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/regex.hpp>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyExpressionEngine.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "ExpressionEditorView.h"
#include "Application.h"
#include "Document.h"
#include "ExpressionSyntaxHighlighter.h"
#include "MainWindow.h"
#include "Selection.h"
#include "TextEdit.h"
#include "ToolBarManager.h"
#include "ViewPlacement.h"
#include "ViewProvider.h"

using namespace Gui;

////////////////////////////////////////////////////////////////////////////
// ExpressionText

namespace
{

const char* skipSpace(const char* t)
{
    while (*t && std::isspace(static_cast<unsigned char>(*t))) {
        ++t;
    }
    return t;
}

std::string encodeComment(const std::string& comment)
{
    if (comment.empty()) {
        return {};
    }
    if (comment[0] != '&' && comment.find('\n') == std::string::npos
        && comment.find('\r') == std::string::npos) {
        return comment;
    }
    std::string res = comment;
    boost::replace_all(res, "&", "&amp;");
    boost::replace_all(res, "\n", "&#10;");
    boost::replace_all(res, "\r", "&#13;");
    return "&" + res;
}

std::string decodeComment(const std::string& comment)
{
    if (comment.empty() || comment[0] != '&') {
        return comment;
    }
    std::string res = comment.substr(1);
    boost::replace_all(res, "&amp;", "&");
    boost::replace_all(res, "&#10;", "\n");
    boost::replace_all(res, "&#13;", "\r");
    return res;
}

// The body as dump wrote it ends in blank lines, and parsed with its line
// ends an expression comes back wrapped in a statement: another type, which
// Expression::isSame() tells apart from the binding it was dumped from, so
// an untouched block would count as a change. The trailing blank goes --
// except that a one-line compound statement ('def f(): return 1') ends at a
// line end in the grammar, and gets one back when it fails without.
App::ExpressionPtr parseBody(const App::DocumentObject* obj, const std::string& body)
{
    std::string text = body;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    try {
        return App::Expression::parse(obj, text);
    }
    catch (Base::Exception&) {
        try {
            return App::Expression::parse(obj, text + "\n");
        }
        catch (...) {
        }
        throw;
    }
}

}  // namespace

std::string ExpressionText::Block::key() const
{
    return docName + '#' + objName + '.' + propName + ' ' + path;
}

bool ExpressionText::Block::isUnbind() const
{
    const char* t = skipSpace(body.c_str());
    return *t == '#' && !*skipSpace(t + 1);
}

ExpressionText::Parsed ExpressionText::parse(const std::string& text)
{
    static const boost::regex header("^##@@ ([^ ]+) (\\w+)#(\\w+)\\.(\\w+) .+$");

    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        std::size_t end = text.find('\n', start);
        std::string line =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    Parsed res;
    const int count = static_cast<int>(lines.size());
    bool strayReported = false;
    for (int i = 0; i < count; ++i) {
        const std::string& line = lines[i];
        if (boost::starts_with(line, "##@@ ")) {
            boost::smatch m;
            if (i + 1 < count && boost::starts_with(lines[i + 1], "##@@")
                && boost::regex_match(line, m, header)) {
                Block block;
                block.path = m.str(1);
                block.docName = m.str(2);
                block.objName = m.str(3);
                block.propName = m.str(4);
                block.comment = lines[i + 1].substr(4);
                block.headerLine = i;
                res.blocks.push_back(std::move(block));
                ++i;
                continue;
            }
            res.errors.push_back({i, "Malformed expression header"});
            continue;
        }
        if (res.blocks.empty()) {
            if (!strayReported && *skipSpace(line.c_str())) {
                res.errors.push_back({i, "Text before the first expression header"});
                strayReported = true;
            }
            continue;
        }
        auto& body = res.blocks.back().body;
        body += line;
        body += '\n';
    }

    for (std::size_t k = 0; k < res.blocks.size(); ++k) {
        int next = k + 1 < res.blocks.size() ? res.blocks[k + 1].headerLine : count;
        res.blocks[k].lastLine = next - 1;
    }
    return res;
}

std::vector<App::DocumentObject*>
ExpressionText::inDocumentOrder(const std::vector<App::DocumentObject*>& objs)
{
    std::set<App::DocumentObject*> wanted(objs.begin(), objs.end());
    std::vector<App::DocumentObject*> res;
    for (auto doc : App::GetApplication().getDocuments()) {
        for (auto obj : doc->getObjects()) {
            if (wanted.count(obj)) {
                res.push_back(obj);
            }
        }
    }
    return res;
}

std::string ExpressionText::dump(const std::vector<App::DocumentObject*>& objs)
{
    std::ostringstream ss;
    std::vector<App::Property*> props;
    for (auto obj : objs) {
        props.clear();
        obj->getPropertyList(props);
        for (auto prop : props) {
            auto container = dynamic_cast<App::PropertyExpressionContainer*>(prop);
            if (!container) {
                continue;
            }
            for (auto& v : container->getExpressions()) {
                ss << "##@@ " << v.first.toString() << ' ' << obj->getFullName() << '.'
                   << container->getName() << " (" << obj->Label.getValue() << ')' << '\n';
                ss << "##@@" << encodeComment(v.second->comment) << '\n';
                ss << v.second->toStr(true, true) << '\n' << '\n';
            }
        }
    }
    return ss.str();
}

ExpressionText::ApplyResult ExpressionText::apply(const std::vector<Block>& blocks,
                                                  bool strict,
                                                  const char* transactionName)
{
    ApplyResult res;
    std::map<App::PropertyExpressionContainer*,
             std::map<App::ObjectIdentifier, App::ExpressionPtr>>
        exprs;

    auto missing = [&](const Block& block, const std::string& msg) {
        (strict ? res.errors : res.warnings).push_back({block.headerLine, msg});
    };

    for (auto& block : blocks) {
        auto doc = App::GetApplication().getDocument(block.docName.c_str());
        if (!doc) {
            missing(block, "Cannot find document '" + block.docName + "'");
            continue;
        }
        auto obj = doc->getObject(block.objName.c_str());
        if (!obj) {
            missing(block, "Cannot find object '" + block.docName + '#' + block.objName + "'");
            continue;
        }
        auto prop = dynamic_cast<App::PropertyExpressionContainer*>(
            obj->getPropertyByName(block.propName.c_str()));
        if (!prop) {
            missing(block,
                    "Invalid property '" + block.docName + '#' + block.objName + '.'
                        + block.propName + "'");
            continue;
        }
        try {
            App::ExpressionPtr expr;
            if (!block.isUnbind()) {
                expr = parseBody(obj, block.body);
                if (expr && !block.comment.empty()) {
                    expr->comment = decodeComment(block.comment);
                }
            }
            exprs[prop][App::ObjectIdentifier::parse(obj, block.path)] = std::move(expr);
        }
        catch (Base::Exception& e) {
            res.errors.push_back({block.headerLine + 2, e.what()});
        }
        catch (std::exception& e) {
            res.errors.push_back({block.headerLine + 2, e.what()});
        }
    }
    if (!res.errors.empty()) {
        return res;
    }

    // Leave alone what would not change: the same expression, or an unbind
    // of a property that is not bound.
    for (auto& v : exprs) {
        auto old = v.first->getExpressions();
        for (auto it = v.second.begin(); it != v.second.end();) {
            auto iter = old.find(it->first);
            bool same = it->second ? (iter != old.end() && it->second->isSame(*iter->second))
                                   : iter == old.end();
            if (same) {
                it = v.second.erase(it);
            }
            else {
                ++res.changed;
                ++it;
            }
        }
    }
    if (!res.changed) {
        return res;
    }

    App::AutoTransaction guard(transactionName);
    try {
        for (auto& v : exprs) {
            if (!v.second.empty()) {
                v.first->setExpressions(std::move(v.second));
            }
        }
    }
    catch (const Base::Exception& e) {
        e.ReportException();
        App::GetApplication().closeActiveTransaction(true);
        res.errors.push_back({-1, e.what()});
        res.changed = 0;
    }
    return res;
}

std::vector<ExpressionText::DiffLine> ExpressionText::diffLines(const QStringList& from,
                                                                const QStringList& to)
{
    std::vector<DiffLine> res;
    const int n = static_cast<int>(from.size());
    const int m = static_cast<int>(to.size());

    // The common head and tail need no search.
    int prefix = 0;
    while (prefix < n && prefix < m && from[prefix] == to[prefix]) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix
           && from[n - 1 - suffix] == to[m - 1 - suffix]) {
        ++suffix;
    }
    for (int i = 0; i < prefix; ++i) {
        res.push_back({DiffLine::Same, from[i]});
    }

    const int N = n - prefix - suffix;
    const int M = m - prefix - suffix;
    auto A = [&](int i) -> const QString& {
        return from[prefix + i];
    };
    auto B = [&](int i) -> const QString& {
        return to[prefix + i];
    };

    // Myers: v[k] is the furthest x on diagonal k. trace[d] keeps the
    // diagonals -d..d as they were before step d, which is all the
    // backtrack of step d reads.
    const int maxD = N + M;
    const int offset = maxD + 1;
    std::vector<int> v(2 * maxD + 3, 0);
    std::vector<std::vector<int>> trace;
    int found = -1;
    for (int d = 0; d <= maxD && found < 0; ++d) {
        trace.emplace_back(v.begin() + offset - d, v.begin() + offset + d + 1);
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1]))
                ? v[offset + k + 1]
                : v[offset + k - 1] + 1;
            int y = x - k;
            while (x < N && y < M && A(x) == B(y)) {
                ++x;
                ++y;
            }
            v[offset + k] = x;
            if (x >= N && y >= M) {
                found = d;
                break;
            }
        }
    }

    std::vector<DiffLine> middle;
    int x = N;
    int y = M;
    for (int d = found; d > 0; --d) {
        const auto& V = trace[d];
        auto at = [&](int k) {
            return V[k + d];
        };
        int k = x - y;
        int prevK = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        int prevX = at(prevK);
        int prevY = prevX - prevK;
        while (x > prevX && y > prevY) {
            middle.push_back({DiffLine::Same, A(x - 1)});
            --x;
            --y;
        }
        if (x == prevX) {
            middle.push_back({DiffLine::Added, B(y - 1)});
        }
        else {
            middle.push_back({DiffLine::Removed, A(x - 1)});
        }
        x = prevX;
        y = prevY;
    }
    while (x > 0 && y > 0) {
        middle.push_back({DiffLine::Same, A(x - 1)});
        --x;
        --y;
    }
    res.insert(res.end(), middle.rbegin(), middle.rend());

    for (int i = n - suffix; i < n; ++i) {
        res.push_back({DiffLine::Same, from[i]});
    }
    return res;
}

////////////////////////////////////////////////////////////////////////////
// ExpressionTextEditor

namespace Gui
{

/** The text editor of an ExpressionEditorView: a TextEditor that folds each
 * expression block under its first header line.
 *
 * Folding hides the block's other lines (QTextBlock::setVisible). The fold
 * arrow is drawn in the line number area and a click on it toggles; the
 * cursor arriving in a hidden line unfolds its block, and an edit that
 * leaves hidden lines under no header shows them again.
 */
class ExpressionTextEditor: public TextEditor
{
public:
    explicit ExpressionTextEditor(QWidget* parent)
        : TextEditor(parent)
    {
        getMarker()->installEventFilter(this);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
            revealCursor();
        });
        connect(document(), &QTextDocument::contentsChange, this, [this](int pos, int, int) {
            repairFold(document()->findBlock(pos));
        });
    }

    static bool isHeader(const QTextBlock& block)
    {
        return block.text().startsWith(QLatin1String("##@@ ")) && block.next().isValid()
            && block.next().text().startsWith(QLatin1String("##@@"));
    }

    static bool isFolded(const QTextBlock& header)
    {
        return header.next().isValid() && !header.next().isVisible();
    }

    void setFolded(const QTextBlock& header, bool fold)
    {
        if (!isHeader(header) || isFolded(header) == fold) {
            return;
        }
        relayout(header, hide(header, fold));
        if (fold && !textCursor().block().isVisible()) {
            setTextCursor(QTextCursor(header));
        }
    }

    /// The first header lines of the folded blocks.
    QSet<QString> foldedHeaders() const
    {
        QSet<QString> res;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (isHeader(block) && isFolded(block)) {
                res.insert(block.text());
            }
        }
        return res;
    }

    void foldHeaders(const QSet<QString>& headers)
    {
        if (headers.isEmpty()) {
            return;
        }
        bool changed = false;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (headers.contains(block.text()) && isHeader(block) && !isFolded(block)) {
                hide(block, true);
                changed = true;
            }
        }
        if (changed) {
            relayout(document()->begin(), document()->lastBlock());
        }
    }

    /// Some block can still fold.
    bool hasUnfolded() const
    {
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (isHeader(block) && !isFolded(block)) {
                return true;
            }
        }
        return false;
    }

    void foldAll()
    {
        bool changed = false;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (isHeader(block) && !isFolded(block)) {
                hide(block, true);
                changed = true;
            }
        }
        if (changed) {
            relayout(document()->begin(), document()->lastBlock());
            if (!textCursor().block().isVisible()) {
                QTextBlock header = textCursor().block();
                while (header.isValid() && !isHeader(header)) {
                    header = header.previous();
                }
                setTextCursor(QTextCursor(header.isValid() ? header : document()->begin()));
            }
        }
    }

    void unfoldAll()
    {
        bool changed = false;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (!block.isVisible()) {
                block.setVisible(true);
                changed = true;
            }
        }
        if (changed) {
            relayout(document()->begin(), document()->lastBlock());
        }
    }

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == getMarker() && event->type() == QEvent::MouseButtonPress) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                int y = static_cast<int>(mouse->position().y());
                QTextBlock block = cursorForPosition(QPoint(0, y)).block();
                if (isHeader(block)) {
                    setFolded(block, !isFolded(block));
                    return true;
                }
            }
        }
        return TextEditor::eventFilter(object, event);
    }

    void drawMarker(int line, int x, int y, QPainter* painter) override
    {
        QTextBlock block = document()->findBlockByNumber(line - 1);
        if (!isHeader(block)) {
            return;
        }
        const qreal h = fontMetrics().height();
        const qreal s = std::max<qreal>(3.0, h / 4.0);
        const QPointF c(x + s, y + h / 2.0);
        QPainterPath path;
        if (isFolded(block)) {
            path.moveTo(c.x() - s / 2, c.y() - s);
            path.lineTo(c.x() + s / 2, c.y());
            path.lineTo(c.x() - s / 2, c.y() + s);
        }
        else {
            path.moveTo(c.x() - s, c.y() - s / 2);
            path.lineTo(c.x() + s, c.y() - s / 2);
            path.lineTo(c.x(), c.y() + s / 2);
        }
        path.closeSubpath();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette().color(QPalette::WindowText));
        painter->drawPath(path);
        painter->restore();
    }

    void paintEvent(QPaintEvent* event) override
    {
        TextEditor::paintEvent(event);

        // A box after the header of a folded block, for what is hidden.
        QPainter painter(viewport());
        const QFontMetrics metrics = fontMetrics();
        const QString dots = QStringLiteral("...");
        for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            if (!block.isVisible()) {
                continue;
            }
            QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
            if (rect.top() > event->rect().bottom()) {
                break;
            }
            if (!isHeader(block) || !isFolded(block)) {
                continue;
            }
            qreal x = rect.left() + document()->documentMargin()
                + metrics.horizontalAdvance(block.text()) + metrics.horizontalAdvance(QLatin1Char(' '));
            QRectF box(x, rect.top() + 1, metrics.horizontalAdvance(dots) + 6, metrics.height() - 2);
            painter.setPen(palette().color(QPalette::Mid));
            painter.drawRoundedRect(box, 3, 3);
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(box, Qt::AlignCenter, dots);
        }
    }

private:
    /// Shows or hides the lines of the block under \a header, up to the
    /// next header; returns the last of them.
    static QTextBlock hide(const QTextBlock& header, bool fold)
    {
        QTextBlock last = header;
        for (QTextBlock block = header.next(); block.isValid(); block = block.next()) {
            if (block != header.next() && block.text().startsWith(QLatin1String("##@@ "))) {
                break;
            }
            block.setVisible(!fold);
            last = block;
        }
        return last;
    }

    void relayout(const QTextBlock& first, const QTextBlock& last)
    {
        int from = first.position();
        int length = last.position() + last.length() - from;
        document()->markContentsDirty(from, std::min(length, document()->characterCount() - from));
        if (auto layout = qobject_cast<QPlainTextDocumentLayout*>(document()->documentLayout())) {
            layout->requestUpdate();
            Q_EMIT layout->documentSizeChanged(layout->documentSize());
        }
        viewport()->update();
        getMarker()->update();
    }

    void revealCursor()
    {
        QTextBlock block = textCursor().block();
        if (block.isVisible()) {
            return;
        }
        for (QTextBlock prev = block.previous(); prev.isValid(); prev = prev.previous()) {
            if (isHeader(prev)) {
                setFolded(prev, false);
                ensureCursorVisible();
                return;
            }
        }
        unfoldAll();
    }

    /// An edit to a folded header can leave it no header: show what it hid.
    void repairFold(const QTextBlock& block)
    {
        if (!block.isValid() || !block.isVisible() || isHeader(block)) {
            return;
        }
        QTextBlock next = block.next();
        if (!next.isValid() || next.isVisible()) {
            return;
        }
        QTextBlock last = next;
        for (; next.isValid() && !next.isVisible(); next = next.next()) {
            next.setVisible(true);
            last = next;
        }
        // Not from inside the change notification: the layout is updating.
        QTimer::singleShot(0, this, [this] {
            relayout(document()->begin(), document()->lastBlock());
        });
    }
};

}  // namespace Gui

////////////////////////////////////////////////////////////////////////////
// ExpressionEditorView

TYPESYSTEM_SOURCE_ABSTRACT(Gui::ExpressionEditorView, Gui::MDIView)

namespace
{

std::vector<QPointer<ExpressionEditorView>>& openViews()
{
    static std::vector<QPointer<ExpressionEditorView>> views;
    return views;
}

void connectToolBar()
{
    static bool connected = false;
    if (connected) {
        return;
    }
    connected = true;
    // Both live as long as the application does.
    Application::Instance->signalActivateView.connect([](const MDIView*) {
        ExpressionEditorView::updateToolBar();
    });
    QObject::connect(ToolBarManager::getInstance(),
                     &ToolBarManager::toolBarsChanged,
                     getMainWindow(),
                     [] {
                         ExpressionEditorView::updateToolBar();
                     });
}

void addShortcut(QAbstractButton* button)
{
    if (button && button->shortcut().isEmpty()) {
        QString text = button->text();
        text.prepend(QLatin1Char('&'));
        button->setShortcut(QKeySequence::mnemonic(text));
    }
}

}  // namespace

ExpressionEditorView::ExpressionEditorView(Scope scope,
                                           std::vector<App::DocumentObjectT> objects,
                                           App::DocumentT document,
                                           Gui::Document* guiDoc)
    : MDIView(guiDoc, getMainWindow())
    , scope(scope)
    , objects(std::move(objects))
    , document(std::move(document))
{
    stack = new QStackedWidget(this);

    editor = new ExpressionTextEditor(stack);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto highlighter = new ExpressionSyntaxHighlighter(editor);
    editor->setSyntaxHighlighter(highlighter);
    highlighter->loadEditorColors();

    diffView = new TextEditor(stack);
    diffView->setLineWrapMode(QPlainTextEdit::NoWrap);
    diffView->setReadOnly(true);
    diffView->setUndoRedoEnabled(false);
    auto diffHighlighter = new ExpressionSyntaxHighlighter(diffView);
    diffView->setSyntaxHighlighter(diffHighlighter);
    diffHighlighter->loadEditorColors();

    stack->addWidget(editor);
    stack->addWidget(diffView);
    setCentralWidget(stack);

    connect(editor->document(),
            &QTextDocument::modificationChanged,
            this,
            &ExpressionEditorView::setWindowModified);
    MainWindow* mw = getMainWindow();
    connect(editor, &QPlainTextEdit::undoAvailable, mw, &MainWindow::updateEditorActions);
    connect(editor, &QPlainTextEdit::redoAvailable, mw, &MainWindow::updateEditorActions);
    connect(editor, &QPlainTextEdit::copyAvailable, mw, &MainWindow::updateEditorActions);

    load();
}

ExpressionEditorView::~ExpressionEditorView()
{
    auto& views = openViews();
    views.erase(std::remove_if(views.begin(),
                               views.end(),
                               [this](const QPointer<ExpressionEditorView>& v) {
                                   return !v || v == this;
                               }),
                views.end());
}

ExpressionEditorView* ExpressionEditorView::open(Scope scope)
{
    std::vector<App::DocumentObjectT> objs;
    App::DocumentT docT;
    App::Document* doc = App::GetApplication().getActiveDocument();
    Gui::Document* guiDoc = nullptr;

    switch (scope) {
        case Scope::Selection: {
            std::vector<App::DocumentObject*> sel;
            for (auto& s : Selection().getCompleteSelection()) {
                if (s.pObject) {
                    sel.push_back(s.pObject);
                }
            }
            sel = ExpressionText::inDocumentOrder(sel);
            if (sel.empty()) {
                return nullptr;
            }
            doc = sel.front()->getDocument();
            bool oneDoc = true;
            for (auto obj : sel) {
                objs.emplace_back(obj);
                oneDoc = oneDoc && obj->getDocument() == doc;
            }
            if (oneDoc) {
                guiDoc = Application::Instance->getDocument(doc);
            }
            break;
        }
        case Scope::ActiveDocument:
            if (!doc) {
                return nullptr;
            }
            docT = App::DocumentT(doc);
            guiDoc = Application::Instance->getDocument(doc);
            break;
        case Scope::AllDocuments:
            // Tied to no document, so that closing one does not close it.
            break;
    }

    for (auto& v : openViews()) {
        if (v && !v->aboutToClose && v->covers(scope, objs, docT)) {
            ViewPlacement::reveal(v, v->getGuiDocument(), true);
            return v;
        }
    }

    auto view = new ExpressionEditorView(scope, std::move(objs), docT, guiDoc);
    openViews().emplace_back(view);
    connectToolBar();

    // The views of the objects being edited stay where they are: a
    // spreadsheet whose cells are in the text is not the cell to reuse.
    Gui::Document* placeDoc = doc ? Application::Instance->getDocument(doc) : nullptr;
    auto showsEditedObject = [view](MDIView* child) {
        for (auto obj : view->scopeObjects()) {
            auto vp = Application::Instance->getViewProvider(obj);
            if (vp && vp->getMDIView() == child) {
                return true;
            }
        }
        return false;
    };
    ViewPlacement::place(view,
                         placeDoc ? ViewPlacement::Category::DocView
                                  : ViewPlacement::Category::Utility,
                         placeDoc,
                         showsEditedObject);
    updateToolBar();
    return view;
}

bool ExpressionEditorView::covers(Scope scope,
                                  const std::vector<App::DocumentObjectT>& objects,
                                  const App::DocumentT& document) const
{
    if (scope != this->scope) {
        return false;
    }
    switch (scope) {
        case Scope::Selection:
            return std::equal(objects.begin(),
                              objects.end(),
                              this->objects.begin(),
                              this->objects.end(),
                              [](const App::DocumentObjectT& a, const App::DocumentObjectT& b) {
                                  return a.getDocumentName() == b.getDocumentName()
                                      && a.getObjectName() == b.getObjectName();
                              });
        case Scope::ActiveDocument:
            return document.getDocumentName() == this->document.getDocumentName();
        case Scope::AllDocuments:
            return true;
    }
    return false;
}

std::vector<App::DocumentObject*> ExpressionEditorView::scopeObjects() const
{
    std::vector<App::DocumentObject*> res;
    switch (scope) {
        case Scope::Selection:
            for (auto& objT : objects) {
                if (auto obj = objT.getObject()) {
                    res.push_back(obj);
                }
            }
            break;
        case Scope::ActiveDocument:
            if (auto doc = document.getDocument()) {
                res = doc->getObjects();
            }
            break;
        case Scope::AllDocuments:
            for (auto doc : App::GetApplication().getDocuments()) {
                auto objs = doc->getObjects();
                res.insert(res.end(), objs.begin(), objs.end());
            }
            break;
    }
    return res;
}

void ExpressionEditorView::load()
{
    baseline = ExpressionText::dump(scopeObjects());
    replaceText(QString::fromUtf8(baseline.c_str()));
    updateTitle();
    if (isShowingDiff()) {
        updateDiff();
    }
}

TextEditor* ExpressionEditorView::getEditor() const
{
    return editor;
}

void ExpressionEditorView::replaceText(const QString& text)
{
    // The folds follow their header lines across the reload.
    QSet<QString> folded = editor->foldedHeaders();
    editor->unfoldAll();
    QTextDocument* doc = editor->document();
    if (doc->isEmpty()) {
        // The first load: nothing to undo back to.
        editor->setPlainText(text);
    }
    else {
        // An edit, so that Ctrl+Z brings back what Revert or Refresh
        // replaced.
        QTextCursor cursor(doc);
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(text);
        cursor.endEditBlock();
    }
    editor->foldHeaders(folded);
    doc->setModified(false);
}

void ExpressionEditorView::updateTitle()
{
    QString what;
    switch (scope) {
        case Scope::Selection: {
            auto objs = scopeObjects();
            if (objs.size() == 1) {
                what = QString::fromUtf8(objs.front()->Label.getValue());
            }
            else {
                what = tr("%n object(s)", "", static_cast<int>(objs.size()));
            }
            break;
        }
        case Scope::ActiveDocument:
            if (auto doc = document.getDocument()) {
                what = QString::fromUtf8(doc->Label.getValue());
            }
            else {
                what = QString::fromUtf8(document.getDocumentName().c_str());
            }
            break;
        case Scope::AllDocuments:
            what = tr("all documents");
            break;
    }
    setWindowTitle(tr("Expressions - %1").arg(what) + QStringLiteral("[*]"));
}

bool ExpressionEditorView::isShowingDiff() const
{
    return stack->currentWidget() == diffView;
}

QPlainTextEdit* ExpressionEditorView::currentEditor() const
{
    return isShowingDiff() ? diffView : editor;
}

void ExpressionEditorView::showDiff(bool show)
{
    if (show) {
        updateDiff();
    }
    stack->setCurrentWidget(show ? diffView : editor);
    currentEditor()->setFocus();
    getMainWindow()->updateEditorActions();
}

void ExpressionEditorView::updateDiff()
{
    QStringList from = QString::fromUtf8(baseline.c_str()).split(QLatin1Char('\n'));
    QStringList to = editor->toPlainText().split(QLatin1Char('\n'));
    auto lines = ExpressionText::diffLines(from, to);

    QTextBlockFormat same;
    QTextBlockFormat added;
    added.setBackground(QColor(0, 200, 0, 60));
    QTextBlockFormat removed;
    removed.setBackground(QColor(230, 0, 0, 60));

    diffView->clear();
    QTextCursor cursor(diffView->document());
    cursor.beginEditBlock();
    int firstChange = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto& line = lines[i];
        const QTextBlockFormat& fmt = line.kind == ExpressionText::DiffLine::Added ? added
            : line.kind == ExpressionText::DiffLine::Removed                     ? removed
                                                                                 : same;
        if (i == 0) {
            cursor.setBlockFormat(fmt);
        }
        else {
            cursor.insertBlock(fmt);
        }
        cursor.insertText(line.text);
        if (firstChange < 0 && line.kind != ExpressionText::DiffLine::Same) {
            firstChange = static_cast<int>(i);
        }
    }
    cursor.endEditBlock();

    if (firstChange < 0) {
        getMainWindow()->showMessage(tr("No changes to the expressions"), 3000);
        firstChange = 0;
    }
    QTextCursor pos(diffView->document()->findBlockByNumber(firstChange));
    diffView->setTextCursor(pos);
    diffView->centerCursor();
}

void ExpressionEditorView::gotoLine(int line)
{
    QTextBlock block = editor->document()->findBlockByNumber(line);
    if (block.isValid()) {
        editor->setTextCursor(QTextCursor(block));
        editor->centerCursor();
    }
    editor->setFocus();
}

void ExpressionEditorView::showErrors(const QString& title,
                                      const std::vector<ExpressionText::Error>& errors)
{
    if (errors.empty()) {
        return;
    }
    QStringList messages;
    for (auto& error : errors) {
        QString msg = QString::fromUtf8(error.message.c_str());
        messages << (error.line >= 0 ? tr("Line %1: %2").arg(error.line + 1).arg(msg) : msg);
    }
    showDiff(false);
    if (errors.front().line >= 0) {
        gotoLine(errors.front().line);
    }
    QMessageBox::critical(this, title, messages.join(QLatin1Char('\n')));
}

bool ExpressionEditorView::apply()
{
    auto parsed = ExpressionText::parse(editor->toPlainText().toUtf8().constData());
    if (!parsed.errors.empty()) {
        showErrors(tr("Expression error"), parsed.errors);
        return false;
    }

    // Blocks the edit removed: one question for all of them.
    std::set<std::string> present;
    for (auto& block : parsed.blocks) {
        present.insert(block.key());
    }
    std::vector<ExpressionText::Block> removed;
    for (auto& block : ExpressionText::parse(baseline).blocks) {
        if (!block.isUnbind() && !present.count(block.key())) {
            removed.push_back(std::move(block));
        }
    }
    std::vector<ExpressionText::Block> blocks = std::move(parsed.blocks);
    if (!removed.empty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Removed expressions"));
        box.setText(tr("%n expression(s) were removed from the text.",
                       "",
                       static_cast<int>(removed.size())));
        box.setInformativeText(tr("Unbind them from their properties, or keep them bound?"));
        auto unbind = box.addButton(tr("Unbind"), QMessageBox::YesRole);
        auto keep = box.addButton(tr("Keep bound"), QMessageBox::NoRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(unbind);
        addShortcut(unbind);
        addShortcut(keep);
        box.exec();
        if (box.clickedButton() == unbind) {
            for (auto& block : removed) {
                block.body = "#";
                blocks.push_back(std::move(block));
            }
        }
        else if (box.clickedButton() != keep) {
            return false;
        }
    }

    auto res = ExpressionText::apply(blocks, true, "Edit expressions");
    if (!res.errors.empty()) {
        showErrors(tr("Failed to apply expressions"), res.errors);
        return false;
    }

    // What the documents hold now, in the form dump writes it.
    load();
    getMainWindow()->showMessage(tr("%n expression(s) changed", "", res.changed), 3000);
    return true;
}

void ExpressionEditorView::revert()
{
    showDiff(false);
    replaceText(QString::fromUtf8(baseline.c_str()));
}

bool ExpressionEditorView::refresh(bool ask)
{
    if (ask && editor->document()->isModified()) {
        auto ret = QMessageBox::question(
            this,
            tr("Refresh expressions"),
            tr("The text has changes that are not applied. Discard them and reload?"),
            QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (ret != QMessageBox::Discard) {
            return false;
        }
    }
    load();
    return true;
}

std::vector<ExpressionText::Block> ExpressionEditorView::selectedBlocks() const
{
    QTextDocument* doc = editor->document();
    QTextCursor cursor = editor->textCursor();
    int first = doc->findBlock(cursor.selectionStart()).blockNumber();
    QTextBlock endBlock = doc->findBlock(cursor.selectionEnd());
    int last = endBlock.blockNumber();
    // A selection ending at the start of a line does not touch that line.
    if (last > first && cursor.selectionEnd() == endBlock.position()) {
        --last;
    }

    std::vector<ExpressionText::Block> res;
    for (auto& block : ExpressionText::parse(editor->toPlainText().toUtf8().constData()).blocks) {
        if (block.headerLine <= last && block.lastLine >= first) {
            res.push_back(std::move(block));
        }
    }
    return res;
}

void ExpressionEditorView::toggleFoldAll()
{
    showDiff(false);
    if (editor->hasUnfolded()) {
        editor->foldAll();
    }
    else {
        editor->unfoldAll();
    }
}

void ExpressionEditorView::unbindSelected()
{
    showDiff(false);
    auto blocks = selectedBlocks();
    if (blocks.empty()) {
        return;
    }
    QSet<QString> folded = editor->foldedHeaders();
    editor->unfoldAll();
    QTextDocument* doc = editor->document();
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    // From the last block up, so the line numbers of the others hold.
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
        if (it->isUnbind()) {
            continue;
        }
        int first = it->headerLine + 2;
        int last = it->lastLine;
        // The blank lines separating it from the next block stay.
        while (last >= first && doc->findBlockByNumber(last).text().trimmed().isEmpty()) {
            --last;
        }
        if (last < first) {
            QTextBlock block = doc->findBlockByNumber(first);
            if (block.isValid()) {
                cursor.setPosition(block.position());
                cursor.insertText(QStringLiteral("#\n"));
            }
            else {
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(QStringLiteral("\n#"));
            }
            continue;
        }
        QTextBlock begin = doc->findBlockByNumber(first);
        QTextBlock end = doc->findBlockByNumber(last);
        cursor.setPosition(begin.position());
        cursor.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("#"));
    }
    cursor.endEditBlock();
    editor->foldHeaders(folded);
}

bool ExpressionEditorView::onMsg(const char* msg, const char** /*ppReturn*/)
{
    if (aboutToClose) {
        return false;
    }
    if (strcmp(msg, "ExpressionApply") == 0) {
        apply();
        return true;
    }
    if (strcmp(msg, "ExpressionDiff") == 0) {
        showDiff(!isShowingDiff());
        return true;
    }
    if (strcmp(msg, "ExpressionRevert") == 0) {
        revert();
        return true;
    }
    if (strcmp(msg, "ExpressionRefresh") == 0) {
        refresh();
        return true;
    }
    if (strcmp(msg, "ExpressionUnbind") == 0) {
        unbindSelected();
        return true;
    }
    if (strcmp(msg, "ExpressionFoldAll") == 0) {
        toggleFoldAll();
        return true;
    }
    if (strcmp(msg, "Cut") == 0) {
        currentEditor()->cut();
        return true;
    }
    if (strcmp(msg, "Copy") == 0) {
        currentEditor()->copy();
        return true;
    }
    if (strcmp(msg, "Paste") == 0) {
        currentEditor()->paste();
        return true;
    }
    if (strcmp(msg, "Undo") == 0) {
        editor->undo();
        return true;
    }
    if (strcmp(msg, "Redo") == 0) {
        editor->redo();
        return true;
    }
    return false;
}

bool ExpressionEditorView::onHasMsg(const char* msg) const
{
    if (aboutToClose) {
        return false;
    }
    if (strcmp(msg, "AllowsOverlayOnHover") == 0) {
        return true;
    }
    bool modified = editor->document()->isModified();
    if (strcmp(msg, "ExpressionApply") == 0 || strcmp(msg, "ExpressionRevert") == 0) {
        return modified;
    }
    if (strcmp(msg, "ExpressionDiff") == 0 || strcmp(msg, "ExpressionRefresh") == 0) {
        return true;
    }
    if (strcmp(msg, "ExpressionFoldAll") == 0) {
        return !isShowingDiff() && !editor->document()->isEmpty();
    }
    if (strcmp(msg, "ExpressionUnbind") == 0) {
        // Cheap on purpose, it is polled; the command finds the blocks.
        return !isShowingDiff() && !editor->document()->isEmpty();
    }
    QPlainTextEdit* current = currentEditor();
    if (strcmp(msg, "Cut") == 0) {
        return !current->isReadOnly() && current->textCursor().hasSelection();
    }
    if (strcmp(msg, "Copy") == 0) {
        return current->textCursor().hasSelection();
    }
    if (strcmp(msg, "Paste") == 0) {
        return !current->isReadOnly() && !QApplication::clipboard()->text().isEmpty();
    }
    if (strcmp(msg, "Undo") == 0) {
        return editor->document()->isUndoAvailable();
    }
    if (strcmp(msg, "Redo") == 0) {
        return editor->document()->isRedoAvailable();
    }
    return false;
}

QStringList ExpressionEditorView::undoActions() const
{
    QStringList undo;
    if (editor->document()->isUndoAvailable()) {
        undo << tr("Edit text");
    }
    return undo;
}

QStringList ExpressionEditorView::redoActions() const
{
    QStringList redo;
    if (editor->document()->isRedoAvailable()) {
        redo << tr("Edit text");
    }
    return redo;
}

bool ExpressionEditorView::canClose()
{
    if (!editor->document()->isModified()) {
        return true;
    }
    setFocus();

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Unapplied expressions"));
    box.setText(tr("Do you want to apply the edited expressions before closing?"));
    box.setInformativeText(tr("If you don't apply them, your changes will be lost."));
    box.setStandardButtons(QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Apply);
    box.setEscapeButton(QMessageBox::Cancel);
    addShortcut(box.button(QMessageBox::Apply));
    addShortcut(box.button(QMessageBox::Discard));

    switch (box.exec()) {
        case QMessageBox::Apply:
            return apply();
        case QMessageBox::Discard:
            return true;
        default:
            return false;
    }
}

void ExpressionEditorView::closeEvent(QCloseEvent* event)
{
    MDIView::closeEvent(event);
    if (event->isAccepted()) {
        aboutToClose = true;
        MainWindow* mw = getMainWindow();
        mw->updateEditorActions();
        // The next active view, if any, is only known once this one is gone.
        QTimer::singleShot(0, mw, [] {
            updateToolBar();
        });
    }
}

const char* ExpressionEditorView::toolBarName()
{
    return "Expression editor";
}

void ExpressionEditorView::updateToolBar()
{
    static bool updating = false;
    MainWindow* mw = getMainWindow();
    if (updating || !mw) {
        return;
    }
    // setState() announces toolBarsChanged, which calls back here.
    Base::StateLocker guard(updating);

    auto manager = ToolBarManager::getInstance();
    const QString name = QString::fromLatin1(toolBarName());
    auto bars = manager->toolBars();
    auto it = bars.find(name);
    if (it == bars.end() || !it->second) {
        return;
    }
    auto active = qobject_cast<ExpressionEditorView*>(mw->activeWindow());
    bool show = active && !active->aboutToClose;
    // Only a real change: every setState() rebuilds the tool bar mirror.
    if (it->second->toggleViewAction()->isVisible() == show) {
        return;
    }
    manager->setState({name},
                      show ? ToolBarManager::State::ForceAvailable
                           : ToolBarManager::State::ForceHidden);
}

#include "moc_ExpressionEditorView.cpp"
