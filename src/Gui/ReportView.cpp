/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QContextMenuEvent>
# include <QGridLayout>
# include <QMenu>
# include <QTextCursor>
# include <QTextStream>
# include <QTime>
# include <QTimer>
# include <QMouseEvent>
# include <QAbstractTextDocumentLayout>
# include <QPainter>
# include <QPainterPath>
# include <QScrollBar>
# include <QTextBlock>
# include <QTextCharFormat>
# include <deque>
#endif

#include <atomic>
#include <Base/Interpreter.h>
#include <App/Color.h>

#include "ReportView.h"
#include "GeneralParams.h"
#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "DockWindowManager.h"
#include "FileDialog.h"
#include "PrefWidgets.h"
#include "PythonConsole.h"
#include "PythonConsolePy.h"
#include "Tools.h"
#include "MessageCollapse.h"
#include "ReportViewParams.h"
#include "Command.h"


using namespace Gui;
using namespace Gui::DockWnd;

/* TRANSLATOR Gui::DockWnd::ReportView */

/**
 *  Constructs a ReportView which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 */
ReportView::ReportView( QWidget* parent )
  : QWidget(parent)
{
    setObjectName(QStringLiteral("ReportOutput"));

    resize( 529, 162 );
    auto tabLayout = new QGridLayout( this );
    tabLayout->setSpacing( 0 );
    tabLayout->setContentsMargins( 0, 0, 0, 0 );

    tabWidget = new QTabWidget( this );
    tabWidget->setObjectName(QStringLiteral("tabWidget"));
    tabWidget->setTabPosition(QTabWidget::South);
    tabWidget->setTabShape(QTabWidget::Rounded);
    tabLayout->addWidget( tabWidget, 0, 0 );


    // create the output window for 'Report view'
    tabOutput = new ReportOutput();
    tabOutput->setWindowTitle(tr("Output"));
    tabOutput->setWindowIcon(BitmapFactory().pixmap("MacroEditor"));
    int output = tabWidget->addTab(tabOutput, tabOutput->windowTitle());
    tabWidget->setTabIcon(output, tabOutput->windowIcon());

    // create the python console
    tabPython = new PythonConsole();
    tabPython->setWordWrapMode(QTextOption::NoWrap);
    tabPython->setWindowTitle(tr("Python console"));
    tabPython->setWindowIcon(BitmapFactory().iconFromTheme("applications-python"));
    int python = tabWidget->addTab(tabPython, tabPython->windowTitle());
    tabWidget->setTabIcon(python, tabPython->windowIcon());
    tabWidget->setCurrentIndex(0);

    // raise the tab page set in the preferences
    ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("General");
    int index = GeneralParams::getAutoloadTab();
    tabWidget->setCurrentIndex(index);
}

/**
 *  Destroys the object and frees any allocated resources
 */
ReportView::~ReportView() = default;

void ReportView::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        tabOutput->setWindowTitle(tr("Output"));
        tabPython->setWindowTitle(tr("Python console"));
        for (int i=0; i<tabWidget->count();i++)
            tabWidget->setTabText(i, tabWidget->widget(i)->windowTitle());
    }
}

// ----------------------------------------------------------

namespace Gui {
struct TextBlockData : public QTextBlockUserData
{
    struct State {
        int length;
        ReportHighlighter::Paragraph type;
    };
    QVector<State> block;

    //! the repeats this line was shown in place of, kept for expanding it
    //!
    //! They live on the block rather than in a map beside the view because the
    //! view drops blocks off the top on its own (maximumBlockCount), and data
    //! hung on the block goes when the block does.
    QStringList folded;
    ReportHighlighter::Paragraph foldedType = ReportHighlighter::Message;
    //! how many blocks the open fold put after this one, zero while it is closed
    int expanded = 0;
};
}

ReportHighlighter::ReportHighlighter(QTextEdit* edit)
  : QSyntaxHighlighter(edit), type(Message)
{
    QPalette pal = edit->palette();
    txtCol = pal.windowText().color();
    logCol = Qt::blue;
    warnCol = QColor(255, 170, 0);
    errCol = Qt::red;
}

ReportHighlighter::~ReportHighlighter() = default;

void ReportHighlighter::highlightBlock (const QString & text)
{
    if (text.isEmpty())
        return;
    auto ud = static_cast<TextBlockData*>(this->currentBlockUserData());
    if (!ud) {
        ud = new TextBlockData;
        this->setCurrentBlockUserData(ud);
    }

    TextBlockData::State b;
    b.length = text.length();
    b.type = this->type;
    //a block is highlighted again whenever an edit touches it, and a state
    //repeating the one before it spans nothing, so keep it out rather than let
    //the vector grow by one on every rehighlight
    //and recoloring writes nothing: the type current then belongs to whatever
    //line came last, not to this block
    if (ud->block.isEmpty()
        || (!recoloring
            && (ud->block.last().length != b.length || ud->block.last().type != b.type))) {
        ud->block.append(b);
    }

    //a line standing in for others is not marked here: its mark is drawn in
    //the margin, by the view (ReportOutput::paintEvent)

    QVector<TextBlockData::State> block = ud->block;
    int start = 0;
    for (const auto & it : block) {
        QTextCharFormat fmt;
        bool formatted = true;
        switch (it.type)
        {
        case Message:
            fmt.setForeground(txtCol);
            break;
        case Warning:
            fmt.setForeground(warnCol);
            break;
        case Error:
            fmt.setForeground(errCol);
            break;
        case LogText:
            fmt.setForeground(logCol);
            break;
        case Critical:
            fmt.setForeground(criticalCol);
            break;
        default:
            formatted = false;
            break;
        }

        if (formatted) {
            setFormat(start, it.length-start, fmt);
        }

        start = it.length;
    }
}

void ReportHighlighter::setParagraphType(ReportHighlighter::Paragraph t)
{
    type = t;
}

void ReportHighlighter::recolor()
{
    if (!document() || document()->isEmpty()) {
        return;
    }
    recoloring = true;
    rehighlight();
    recoloring = false;
}

void ReportHighlighter::setTextColor( const QColor& col )
{
    if (txtCol != col) {
        txtCol = col;
        recolor();
    }
}

void ReportHighlighter::setLogColor( const QColor& col )
{
    if (logCol != col) {
        logCol = col;
        recolor();
    }
}

void ReportHighlighter::setWarningColor( const QColor& col )
{
    if (warnCol != col) {
        warnCol = col;
        recolor();
    }
}

void ReportHighlighter::setErrorColor( const QColor& col )
{
    if (errCol != col) {
        errCol = col;
        recolor();
    }
}

void ReportHighlighter::setCriticalColor( const QColor& col )
{
    if (criticalCol != col) {
        criticalCol = col;
        recolor();
    }
}

namespace {

// ----------------------------------------------------------

/**
 * The CustomReportEvent class is used to send report events in the methods Log(),
 * Error(), Warning() and Message() of the ReportOutput class to itself instead of
 * printing the messages directly in its text view.
 *
 * This makes the methods Log(), Error(), Warning() and Message() thread-safe.
 * @author Werner Mayer
 */
class CustomReportEvent : public QEvent
{
public:
    CustomReportEvent(ReportHighlighter::Paragraph p, const QString& s)
    : QEvent(eventType())
    {
        par = p;
        msg = s;
        ++counter;
    }
    ~CustomReportEvent() override
    { 
        --counter;
    }
    const QString& message() const
    { return msg; }
    ReportHighlighter::Paragraph messageType() const
    { return par; }
    static QEvent::Type eventType() {
        static int _type = QEvent::registerEventType();
        return static_cast<QEvent::Type>(_type);
    }

public:
    static std::atomic<int> counter;

private:
    ReportHighlighter::Paragraph par;
    QString msg;
};

std::atomic<int> CustomReportEvent::counter;

} // anonymous namespace

// ----------------------------------------------------------

/**
 * The ReportOutputObserver class is used to check if messages sent to the
 * report view are warnings or errors, and if so and if the user has not
 * disabled this in preferences, the report view is toggled on so the
 * user always gets the warnings/errors
 */

ReportOutputObserver::ReportOutputObserver(ReportOutput *report)
  : QObject(report)
{
    this->reportView = report;
}

void ReportOutputObserver::showReportView()
{
    // get the QDockWidget parent of the report view
    DockWindowManager::instance()->activate(reportView);
}

bool ReportOutputObserver::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == CustomReportEvent::eventType() && obj == reportView.data()) {
        CustomReportEvent* cr = static_cast<CustomReportEvent*>(event);
        if (cr) {
            if (ReportViewParams::getCommandRedirect().size()
                    && cr->message().startsWith(ReportViewParams::getCommandRedirect()))
            {
                auto cmd = cr->message().right(cr->message().size() - ReportViewParams::getCommandRedirect().size()).trimmed();
                if (cmd.size() && cmd[cmd.size()-1] == QLatin1Char('\n'))
                    cmd = cmd.left(cmd.size()-1);
                try {
                    Command::_runCommand(nullptr, 0, Command::Doc, cmd.toUtf8().constData());
                } catch (Base::Exception &e) {
                    e.ReportException();
                }
                return true;
            }

            switch(cr->messageType()) {
            case ReportHighlighter::Warning:
                if(ReportViewParams::getcheckShowReportViewOnWarning())
                    showReportView();
                break;
            case ReportHighlighter::Error:
                if(ReportViewParams::getcheckShowReportViewOnError())
                    showReportView();
                break;
            case ReportHighlighter::Message:
                if(ReportViewParams::getcheckShowReportViewOnNormalMessage())
                    showReportView();
                break;
            case ReportHighlighter::LogText:
                if(ReportViewParams::getcheckShowReportViewOnLogMessage())
                    showReportView();
                break;
            case ReportHighlighter::Critical:
                if(ReportViewParams::getcheckShowReportViewOnCritical())
                    showReportView();
                break;
            default:
                break;
            }
        }
        return false;  //true would prevent the messages reaching the report view
    }

    // standard event processing
    return QObject::eventFilter(obj, event);
}

// ----------------------------------------------------------

class ReportOutput::Data
{
public:
    Data()
    {
        if (!default_stdout) {
            Base::PyGILStateLocker lock;
            default_stdout = PySys_GetObject("stdout");
            replace_stdout = new OutputStdout();
            redirected_stdout = false;
        }

        if (!default_stderr) {
            Base::PyGILStateLocker lock;
            default_stderr = PySys_GetObject("stderr");
            replace_stderr = new OutputStderr();
            redirected_stderr = false;
        }
    }
    ~Data()
    {
        if (replace_stdout) {
            Py_DECREF(replace_stdout);
            replace_stdout = nullptr;
        }

        if (replace_stderr) {
            Py_DECREF(replace_stderr);
            replace_stderr = nullptr;
        }
    }

    // make them static because redirection should done only once
    static bool redirected_stdout;
    static PyObject* default_stdout;
    static PyObject* replace_stdout;

    static bool redirected_stderr;
    static PyObject* default_stderr;
    static PyObject* replace_stderr;

    fastsignals::scoped_connection connParam;

    ReportHighlighter::Paragraph pendingType;
    QStringList pendingMessage;

    //! a line shown recently, and the repeats of it waiting to be shown
    struct RecentLine
    {
        ReportHighlighter::Paragraph type;
        MessageFold fold;
    };
    std::deque<RecentLine> recent;
    QTimer* dupTimer = nullptr;
};

//! stamp a message with the time it arrived, as the view stamps what it shows
static QString withTimecode(const QString& text)
{
    if (!ReportViewParams::getcheckShowReportTimecode()) {
        return text;
    }
    return QTime::currentTime().toString(QStringLiteral("hh:mm:ss  ")) + text;
}

//! put the repeat count inside the line rather than after its newline
//!
//! The count is how many repeats this line stands in for, not how many times the
//! line occurred - the occurrence that was shown when the run started speaks for
//! itself, so the counts across a run still add up to the number of messages sent.
static QString withRepeatCount(const QString& text, int count)
{
    int end = text.size();
    while (end > 0
           && (text.at(end - 1) == QLatin1Char('\n') || text.at(end - 1) == QLatin1Char('\r'))) {
        --end;
    }
    QString result = text;
    result.insert(end, QStringLiteral(" (x%1)").arg(count));
    return result;
}

bool ReportOutput::Data::redirected_stdout = false;
PyObject* ReportOutput::Data::default_stdout = nullptr;
PyObject* ReportOutput::Data::replace_stdout = nullptr;

bool ReportOutput::Data::redirected_stderr = false;
PyObject* ReportOutput::Data::default_stderr = nullptr;
PyObject* ReportOutput::Data::replace_stderr = nullptr;

/* TRANSLATOR Gui::DockWnd::ReportOutput */

/**
 *  Constructs a ReportOutput which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 */
ReportOutput::ReportOutput(QWidget* parent)
  : QTextEdit(parent)
  , WindowParameter("OutputWindow")
  , d(new Data)
  , gotoEnd(true)
  , blockStart(true)
{
    bLog = false;
    reportHl = new ReportHighlighter(this);

    //nothing else will come along to flush a held line once a burst stops
    d->dupTimer = new QTimer(this);
    d->dupTimer->setSingleShot(true);
    connect(d->dupTimer, &QTimer::timeout, this, &ReportOutput::flushDuplicates);

    restoreFont();
    setReadOnly(true);
    clear();
    fitFoldMargin();
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    Base::Console().AttachObserver(this);
    // still this group's: MaxLines, which the context menu's spin box stores
    getWindowParameter()->Attach(this);
    getWindowParameter()->NotifyAll();
    // The view's own settings come from ReportViewParams, which knows their
    // defaults and says when one changed -- from the preferences, the
    // context menu, the omni search or a script alike.
    d->connParam = ReportViewParams::signalParamChanged().connect(
        [this](const char *name) { applySetting(name); });
    for (const char *name : {"checkMessage", "checkLogging", "checkWarning", "checkError",
                             "checkCritical", "colorText", "colorLogging", "colorWarning",
                             "colorError", "checkGoToEnd", "RedirectPythonOutput",
                             "RedirectPythonErrors"}) {
        applySetting(name);
    }

    _prefs = WindowParameter::getDefaultParameter()->GetGroup("Editor");
    _prefs->Attach(this);
    _prefs->Notify("FontSize");
    _prefs->Notify("MaxLines");

    // scroll to bottom at startup to make sure that last appended text is visible
    ensureCursorVisible();
}

/**
 *  Destroys the object and frees any allocated resources
 */
ReportOutput::~ReportOutput()
{
    getWindowParameter()->Detach(this);
    _prefs->Detach(this);
    Base::Console().DetachObserver(this);
    delete reportHl;
    delete d;
}

void ReportOutput::restoreFont()
{
    QFont serifFont(QStringLiteral("Courier"), 10, QFont::Normal);
    setFont(serifFont);
}

void ReportOutput::SendLog(const std::string& notifiername, const std::string& msg, Base::LogStyle level,
                           Base::IntendedRecipient recipient, Base::ContentType content)
{
    // Do not log translated messages, or messages intended only to the user to the Report View
    if( recipient == Base::IntendedRecipient::User ||
        content == Base::ContentType::Translated)
        return;

    ReportHighlighter::Paragraph style = ReportHighlighter::LogText;
    switch (level) {
        case Base::LogStyle::Warning:
            style = ReportHighlighter::Warning;
            break;
        case Base::LogStyle::Message:
            style = ReportHighlighter::Message;
            break;
        case Base::LogStyle::Error:
            style = ReportHighlighter::Error;
            break;
        case Base::LogStyle::Log:
            style = ReportHighlighter::LogText;
            break;
        case Base::LogStyle::Critical:
            style = ReportHighlighter::Critical;
            break;
        default:
            break;
    }

    QString qMsg;

    if(!notifiername.empty()) {
        qMsg = QStringLiteral("%1: %2").arg(QString::fromUtf8(notifiername.c_str()),
                                            QString::fromUtf8(msg.c_str()));
    }
    else {
        qMsg = QString::fromUtf8(msg.c_str());
    }

    // This truncates log messages that are too long
    if (style == ReportHighlighter::LogText) {
        int messageSize = ReportViewParams::getLogMessageSize();
        if (messageSize <= 0) {
#ifdef FC_DEBUG
            messageSize = 16*1024;
#else
            messageSize = 2048;
#endif
        }
        if (qMsg.size()>messageSize) {
            qMsg.truncate(messageSize);
            qMsg += QStringLiteral("...\n");
        }
    }

    // Send the event to itself to allow thread-safety. Qt will delete it when done.
    auto ev = new CustomReportEvent(style, qMsg);
    QApplication::postEvent(this, ev);
}

void ReportOutput::customEvent ( QEvent* ev )
{
    // Appends the text stored in the event to the text view
    if ( ev->type() ==  CustomReportEvent::eventType() ) {
        CustomReportEvent* ce = static_cast<CustomReportEvent*>(ev);
        if (holdDuplicate(ce->messageType(), ce->message())) {
            return;
        }
        appendReport(ce->messageType(), ce->message());
    }
}

//! true when this line repeats one of the last few shown and is being held back
//!
//! Only what is on screen is thinned out. This runs on the reader's side of
//! SendLog's queued event, so the log file, the Python console and every other
//! console observer have already been handed the message in full.
bool ReportOutput::holdDuplicate(ReportHighlighter::Paragraph type, const QString& text)
{
    const int window = static_cast<int>(ReportViewParams::getDuplicateWindow());
    if (window <= 0) {
        return false;
    }

    const std::size_t key =
        messageCollapseKey(text, static_cast<int>(ReportViewParams::getDuplicateKeyLength()));

    for (auto& line : d->recent) {
        if (line.type == type && line.fold.key() == key) {
            line.fold.add(text, withTimecode(text));
            //timed from the first repeat, not the last, so a line repeating without
            //pause still reports every DuplicateTimeout instead of never
            if (!d->dupTimer->isActive()) {
                const int timeout = static_cast<int>(ReportViewParams::getDuplicateTimeout());
                if (timeout > 0) {
                    d->dupTimer->start(timeout);
                }
            }
            return true;
        }
    }

    //An unrelated message does NOT end the holds. It used to, so that repeats
    //stayed in front of the line that ended them - but the floods worth
    //collapsing are mixed, and one message of another kind arriving between two
    //repeats was enough to publish the hold at (x1) and start again. The timer
    //is what ends a hold now, and the messages behind the fold carry the time
    //each of them arrived, so nothing is lost by their line landing later.
    d->recent.push_back({type, MessageFold(key)});
    while (static_cast<int>(d->recent.size()) > window) {
        //whatever the line falling out of the window was holding still has to be
        //shown: nothing else will ever look at it again
        flushHeld(d->recent.front().type, d->recent.front().fold);
        d->recent.pop_front();
    }
    return false;
}

//! show one held line, carrying the number of repeats it stood in for
void ReportOutput::flushHeld(ReportHighlighter::Paragraph type, MessageFold& fold)
{
    if (fold.isEmpty()) {
        return;
    }
    const int held = fold.count();
    //the first one queued speaks for the rest: they only differ where the key was
    //not looking, which is what it was keyed to ignore. It is the unstamped copy -
    //appendReport stamps what it shows.
    const QString shown = fold.exemplar();
    QStringList folded = fold.held();
    fold.clear();

    //always counted, including (x1): without it a line that arrived exactly
    //twice comes out as a bare repeat, which reads as the suppression having
    //done nothing at all
    appendReport(type, withRepeatCount(shown, held), &folded);
}

//! show every held line, each carrying the number of repeats it stood in for
//!
//! Going through appendReport is what also writes out the batch queue, so the
//! occurrence that was shown before the repeats started lands with them.
void ReportOutput::flushDuplicates()
{
    d->dupTimer->stop();
    for (auto& line : d->recent) {
        flushHeld(line.type, line.fold);
    }
}

//! write out whatever the batching is holding, so an ordered insert can follow
void ReportOutput::writePending()
{
    if (d->pendingMessage.isEmpty()) {
        return;
    }
    reportHl->setParagraphType(d->pendingType);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(d->pendingMessage.join(QString()));
    cursor.endEditBlock();
    d->pendingMessage.clear();
}

//! attach the held messages to the line that was shown in their place
void ReportOutput::keepFolded(const QTextBlock& block,
                              ReportHighlighter::Paragraph type,
                              const QStringList& folded)
{
    if (!block.isValid()) {
        return;
    }
    //the highlighter owns the block's user data and puts its own state there, so
    //this rides along in the same object rather than replacing it
    auto* data = static_cast<TextBlockData*>(block.userData());
    if (!data) {
        data = new TextBlockData;
        const_cast<QTextBlock&>(block).setUserData(data);
    }
    data->folded = folded;
    data->foldedType = type;
    //the mark says the line is foldable, and it is drawn from this data, which
    //was not there yet when the line was painted
    viewport()->update();
}

//! the margin the marks are drawn in
//!
//! The document's own margin, on purpose: it is the one indent every line gets
//! alike, so a line with a mark and a line without start in the same column,
//! and it survives the document being cleared, which a format put on the root
//! frame or on the blocks does not.
void ReportOutput::fitFoldMargin()
{
    const qreal margin = fontMetrics().height() + 2;
    if (!qFuzzyCompare(document()->documentMargin(), margin)) {
        document()->setDocumentMargin(margin);
    }
}

QRectF ReportOutput::foldMarkRect(const QTextBlock& block) const
{
    const QRectF line = document()->documentLayout()->blockBoundingRect(block);
    const qreal row = fontMetrics().height();
    const qreal side = qMin(row, document()->documentMargin()) - 4;
    const qreal x = (document()->documentMargin() - side) / 2 - horizontalScrollBar()->value();
    const qreal y = line.top() + (row - side) / 2 - verticalScrollBar()->value();
    return {x, y, side, side};
}

//! the collapsed line whose mark is under this point, invalid when there is none
//!
//! Only the mark answers. The line's text used to, with an underline to say
//! so, which made a line that could not be clicked into for a selection.
QTextBlock ReportOutput::foldedBlockAt(const QPoint& pos) const
{
    if (pos.x() + horizontalScrollBar()->value() >= document()->documentMargin()) {
        return {};
    }
    //whatever line is at this height; the cursor is asked inside the text
    QTextBlock block =
        cursorForPosition(QPoint(qRound(document()->documentMargin()) + 1, pos.y())).block();
    if (!block.isValid()) {
        return {};
    }
    auto* data = static_cast<TextBlockData*>(block.userData());
    if (data && !data->folded.isEmpty()
        && foldMarkRect(block).adjusted(-2, -2, 2, 2).contains(pos)) {
        return block;
    }
    return {};
}

//! the text, and in the margin a mark for each line that stands in for others
//!
//! A triangle pointing at the line while it is closed and down while its
//! messages are shown, in the colour of the text, so that it follows a theme.
void ReportOutput::paintEvent(QPaintEvent* ev)
{
    QTextEdit::paintEvent(ev);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Text));

    const QRect exposed = ev->rect();
    QTextBlock block =
        cursorForPosition(QPoint(qRound(document()->documentMargin()) + 1, exposed.top())).block();
    for (; block.isValid(); block = block.next()) {
        const QRectF mark = foldMarkRect(block);
        if (mark.top() > exposed.bottom()) {
            break;
        }
        auto* data = static_cast<TextBlockData*>(block.userData());
        if (!data || data->folded.isEmpty()) {
            continue;
        }
        const QRectF r = mark.adjusted(1, 1, -1, -1);
        QPainterPath path;
        if (data->expanded > 0) {
            path.moveTo(r.left(), r.top() + r.height() * 0.25);
            path.lineTo(r.right(), r.top() + r.height() * 0.25);
            path.lineTo(r.center().x(), r.bottom() - r.height() * 0.1);
        }
        else {
            path.moveTo(r.left() + r.width() * 0.25, r.top());
            path.lineTo(r.right() - r.width() * 0.1, r.center().y());
            path.lineTo(r.left() + r.width() * 0.25, r.bottom());
        }
        path.closeSubpath();
        painter.drawPath(path);
    }
}

//! open the fold on this line, or close it again
//!
//! The line itself stays either way: it is what the reader clicks a second time,
//! and leaving it in place is what makes this a fold rather than a one-way reveal
//! that reappends its messages on every click.
void ReportOutput::toggleFold(const QTextBlock& block)
{
    auto* data = static_cast<TextBlockData*>(block.userData());
    if (!data || data->folded.isEmpty()) {
        return;
    }

    //the end of this line's text, before the separator that starts the next block
    const int start = block.position() + block.length() - 1;

    if (data->expanded > 0) {
        QTextBlock last = block;
        for (int i = 0; i < data->expanded && last.next().isValid(); ++i) {
            last = last.next();
        }
        data->expanded = 0;
        QTextCursor cursor(document());
        cursor.beginEditBlock();
        cursor.setPosition(start);
        cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.endEditBlock();
        viewport()->update();
        return;
    }

    QStringList lines;
    lines.reserve(data->folded.size());
    for (int i = 0; i < data->folded.size(); ++i) {
        //the messages are held with their newlines; the block separators supply
        //those again, so strip them before joining
        QString line = data->folded.at(i);
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        lines.append(messageFoldBranch(i, data->folded.size()) + line);
    }
    data->expanded = lines.size();

    reportHl->setParagraphType(data->foldedType);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.setPosition(start);
    cursor.insertText(QStringLiteral("\n") + lines.join(QStringLiteral("\n")));
    cursor.endEditBlock();
    //the line itself did not change, and its mark has to turn
    viewport()->update();
}

void ReportOutput::appendReport(ReportHighlighter::Paragraph messageType, const QString& message,
                                const QStringList* folded)
{
    bool showTimecode = ReportViewParams::getcheckShowReportTimecode();
    QString text = message;

    // The time code can only be set when the cursor is at the block start
    if (showTimecode && blockStart) {
        QTime time = QTime::currentTime();
        text.prepend(time.toString(QStringLiteral("hh:mm:ss  ")));
    }
    blockStart = text.endsWith(QLatin1Char('\n'));

    bool flushed = false;
    QTextDocument *document = this->document();

    //a line carrying held messages skips the batching: it has to end up in a block
    //of its own, now, so that the messages can be hung on that block
    if (folded) {
        writePending();
        reportHl->setParagraphType(messageType);
        QTextCursor cursor(document);
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::End);
        const int start = cursor.position();
        cursor.insertText(text);
        cursor.endEditBlock();
        keepFolded(document->findBlock(start), messageType, *folded);
        if (gotoEnd) {
            cursor.movePosition(QTextCursor::End);
            setTextCursor(cursor);
            ensureCursorVisible();
        }
        return;
    }

    // Try to batch process text input because text layout is an expensive
    // operation
    if (CustomReportEvent::counter > 1
            && (d->pendingMessage.isEmpty()
                || d->pendingType == messageType))
    {
        d->pendingType = messageType;
        d->pendingMessage.append(text);
        int maxCount = document->maximumBlockCount();
        if (maxCount > 0
                && d->pendingMessage.size() + document->blockCount() > maxCount)
        {
            document->clear();
            if (d->pendingMessage.size() > maxCount)
                d->pendingMessage.erase(d->pendingMessage.begin(),
                        d->pendingMessage.begin() + d->pendingMessage.size() - maxCount);
        }
    }
    else {
        if (d->pendingMessage.size()) {
            if (d->pendingType == messageType) {
                d->pendingMessage.append(text);
                text.clear();
            }
            reportHl->setParagraphType(d->pendingType);
            QTextCursor cursor(document);
            cursor.beginEditBlock();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(d->pendingMessage.join(QString()));
            cursor.endEditBlock();
            d->pendingMessage.clear();
            flushed = true;
        }
        if (text.size()) {
            if (CustomReportEvent::counter > 1) {
                d->pendingType = messageType;
                d->pendingMessage.append(text);
            }
            else {
                reportHl->setParagraphType(messageType);
                QTextCursor cursor(document);
                cursor.beginEditBlock();
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(text);
                cursor.endEditBlock();
                flushed = true;
            }
        }
    }

    if (flushed && gotoEnd) {
        QTextCursor cursor(document);
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
        ensureCursorVisible();
    }
}


//! open or close the fold whose mark was clicked
void ReportOutput::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        QTextBlock block = foldedBlockAt(ev->pos());
        if (block.isValid()) {
            toggleFold(block);
            return;
        }
    }
    QTextEdit::mousePressEvent(ev);
}

//! point at the mark of a collapsed line, so it reads as something to click
void ReportOutput::mouseMoveEvent(QMouseEvent* ev)
{
    viewport()->setCursor(foldedBlockAt(ev->pos()).isValid() ? Qt::PointingHandCursor
                                                             : Qt::IBeamCursor);
    QTextEdit::mouseMoveEvent(ev);
}

bool ReportOutput::event(QEvent* event)
{
    if (event && event->type() == QEvent::ShortcutOverride) {
        auto kevent = static_cast<QKeyEvent*>(event);
        if (kevent == QKeySequence::Copy)
            kevent->accept();
    }
    return QTextEdit::event(event);
}

void ReportOutput::changeEvent(QEvent *ev)
{
    if (ev->type() == QEvent::StyleChange) {
        applySetting("colorText");
    }
    QTextEdit::changeEvent(ev);
}

void ReportOutput::contextMenuEvent ( QContextMenuEvent * e )
{
    bool bShowOnLog = ReportViewParams::getcheckShowReportViewOnLogMessage();
    bool bShowOnNormal = ReportViewParams::getcheckShowReportViewOnNormalMessage();
    bool bShowOnWarn = ReportViewParams::getcheckShowReportViewOnWarning();
    bool bShowOnError = ReportViewParams::getcheckShowReportViewOnError();
    bool bShowOnCritical = ReportViewParams::getcheckShowReportViewOnCritical();

    auto menu = new QMenu(this);
    auto optionMenu = new QMenu( menu );
    optionMenu->setTitle(tr("Options"));
    menu->addMenu(optionMenu);
    menu->addSeparator();

    auto displayMenu = new QMenu(optionMenu);
    displayMenu->setTitle(tr("Display message types"));
    optionMenu->addMenu(displayMenu);

    QAction* logMsg = displayMenu->addAction(tr("Normal messages"), this, &ReportOutput::onToggleNormalMessage);
    logMsg->setCheckable(true);
    logMsg->setChecked(bMsg);

    QAction* logAct = displayMenu->addAction(tr("Log messages"), this, &ReportOutput::onToggleLogMessage);
    logAct->setCheckable(true);
    logAct->setChecked(bLog);

    QAction* wrnAct = displayMenu->addAction(tr("Warnings"), this, &ReportOutput::onToggleWarning);
    wrnAct->setCheckable(true);
    wrnAct->setChecked(bWrn);

    QAction* errAct = displayMenu->addAction(tr("Errors"), this, &ReportOutput::onToggleError);
    errAct->setCheckable(true);
    errAct->setChecked(bErr);

    QAction* logCritical = displayMenu->addAction(tr("Critical messages"), this, &ReportOutput::onToggleCritical);
    logCritical->setCheckable(true);
    logCritical->setChecked(bCritical);

    auto showOnMenu = new QMenu (optionMenu);
    showOnMenu->setTitle(tr("Show Report view on"));
    optionMenu->addMenu(showOnMenu);

    QAction* showNormAct = showOnMenu->addAction(tr("Normal messages"), this, &ReportOutput::onToggleShowReportViewOnNormalMessage);
    showNormAct->setCheckable(true);
    showNormAct->setChecked(bShowOnNormal);

    QAction* showLogAct = showOnMenu->addAction(tr("Log messages"), this, &ReportOutput::onToggleShowReportViewOnLogMessage);
    showLogAct->setCheckable(true);
    showLogAct->setChecked(bShowOnLog);

    QAction* showWrnAct = showOnMenu->addAction(tr("Warnings"), this, &ReportOutput::onToggleShowReportViewOnWarning);
    showWrnAct->setCheckable(true);
    showWrnAct->setChecked(bShowOnWarn);

    QAction* showErrAct = showOnMenu->addAction(tr("Errors"), this, &ReportOutput::onToggleShowReportViewOnError);
    showErrAct->setCheckable(true);
    showErrAct->setChecked(bShowOnError);

    QAction* showCriticalAct = showOnMenu->addAction(tr("Critical messages"), this, SLOT(onToggleShowReportViewOnCritical()));
    showCriticalAct->setCheckable(true);
    showCriticalAct->setChecked(bShowOnCritical);

    optionMenu->addSeparator();

    QAction* stdoutAct = optionMenu->addAction(tr("Redirect Python output"), this, &ReportOutput::onToggleRedirectPythonStdout);
    stdoutAct->setCheckable(true);
    stdoutAct->setChecked(d->redirected_stdout);

    QAction* stderrAct = optionMenu->addAction(tr("Redirect Python errors"), this, &ReportOutput::onToggleRedirectPythonStderr);
    stderrAct->setCheckable(true);
    stderrAct->setChecked(d->redirected_stderr);

    optionMenu->addSeparator();
    QAction* botAct = optionMenu->addAction(tr("Go to end"), this, &ReportOutput::onToggleGoToEnd);
    botAct->setCheckable(true);
    botAct->setChecked(gotoEnd);

    // Use Qt's internal translation of the Copy & Select All commands
    const char* context = "QWidgetTextControl";
    QString copyStr = QCoreApplication::translate(context, "&Copy");
    QAction* copy = menu->addAction(copyStr, this, &ReportOutput::copy);
    copy->setShortcut(QKeySequence(QKeySequence::Copy));
    copy->setEnabled(textCursor().hasSelection());
    QIcon icon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    if (!icon.isNull())
        copy->setIcon(icon);

    menu->addSeparator();
    QString selectStr = QCoreApplication::translate(context, "Select All");
    QAction* select = menu->addAction(selectStr, this, &ReportOutput::selectAll);
    select->setShortcut(QKeySequence(QKeySequence::SelectAll));

    menu->addAction(tr("Clear"), this, &ReportOutput::clear);

    auto spinBox = new PrefSpinBox(menu);
    spinBox->setMinimum(0);
    spinBox->setMaximum(99999999);
    spinBox->setSingleStep(1000);
    spinBox->setValue(this->document()->maximumBlockCount());
    spinBox->setParamGrpPath("OutputWindow");
    spinBox->setEntryName("MaxLines");
    spinBox->initAutoSave();
    Action::addWidget(menu, QObject::tr("Maximum lines"), QString(), spinBox);

    menu->addSeparator();
    menu->addAction(tr("Save As..."), this, &ReportOutput::onSaveAs);

    menu->exec(e->globalPos());
    delete menu;
}

void ReportOutput::onSaveAs()
{
    QString fn = FileDialog::getSaveFileName(this, tr("Save Report Output"), QString(),
        QStringLiteral("%1 (*.txt *.log)").arg(tr("Plain Text Files")));
    if (!fn.isEmpty()) {
        QFileInfo fi(fn);
        if (fi.completeSuffix().isEmpty())
            fn += QStringLiteral(".log");
        QFile f(fn);
        if (f.open(QIODevice::WriteOnly)) {
            QTextStream t (&f);
            t << toPlainText();
            f.close();
        }
    }
}

bool ReportOutput::isError() const
{
    return bErr;
}

bool ReportOutput::isWarning() const
{
    return bWrn;
}

bool ReportOutput::isLogMessage() const
{
    return bLog;
}

bool ReportOutput::isNormalMessage() const
{
    return bMsg;
}


bool ReportOutput::isCritical() const
{
    return bCritical;
}

// The toggles store the setting; applySetting() is what acts on it, here as
// for a change that came from anywhere else.
void ReportOutput::onToggleError()
{
    ReportViewParams::setcheckError(!ReportViewParams::getcheckError());
}

void ReportOutput::onToggleWarning()
{
    ReportViewParams::setcheckWarning(!ReportViewParams::getcheckWarning());
}

void ReportOutput::onToggleLogMessage()
{
    ReportViewParams::setcheckLogging(!ReportViewParams::getcheckLogging());
}

void ReportOutput::onToggleNormalMessage()
{
    ReportViewParams::setcheckMessage(!ReportViewParams::getcheckMessage());
}

void ReportOutput::onToggleCritical()
{
    ReportViewParams::setcheckCritical(!ReportViewParams::getcheckCritical());
}

void ReportOutput::onToggleShowReportViewOnWarning()
{
    ReportViewParams::setcheckShowReportViewOnWarning(!ReportViewParams::getcheckShowReportViewOnWarning());
}

void ReportOutput::onToggleShowReportViewOnError()
{
    ReportViewParams::setcheckShowReportViewOnError(!ReportViewParams::getcheckShowReportViewOnError());
}

void ReportOutput::onToggleShowReportViewOnNormalMessage()
{
    ReportViewParams::setcheckShowReportViewOnNormalMessage(!ReportViewParams::getcheckShowReportViewOnNormalMessage());
}

void ReportOutput::onToggleShowReportViewOnCritical()
{
    ReportViewParams::setcheckShowReportViewOnCritical(!ReportViewParams::getcheckShowReportViewOnCritical());
}

void ReportOutput::onToggleShowReportViewOnLogMessage()
{
    ReportViewParams::setcheckShowReportViewOnLogMessage(!ReportViewParams::getcheckShowReportViewOnLogMessage());
}

void ReportOutput::onToggleRedirectPythonStdout()
{
    ReportViewParams::setRedirectPythonOutput(!ReportViewParams::getRedirectPythonOutput());
}

void ReportOutput::onToggleRedirectPythonStderr()
{
    ReportViewParams::setRedirectPythonErrors(!ReportViewParams::getRedirectPythonErrors());
}

void ReportOutput::onToggleGoToEnd()
{
    ReportViewParams::setcheckGoToEnd(!ReportViewParams::getcheckGoToEnd());
}

/* One of the view's settings, applied. Called for each at construction and
 * whenever ReportViewParams says one changed; every branch can be run again
 * with nothing changed.
 */
void ReportOutput::applySetting(const char *name)
{
    if (!name)
        return;
    if (strcmp(name, "checkLogging") == 0) {
        bLog = ReportViewParams::getcheckLogging();
    }
    else if (strcmp(name, "checkWarning") == 0) {
        bWrn = ReportViewParams::getcheckWarning();
    }
    else if (strcmp(name, "checkError") == 0) {
        bErr = ReportViewParams::getcheckError();
    }
    else if (strcmp(name, "checkMessage") == 0) {
        bMsg = ReportViewParams::getcheckMessage();
    }
    else if (strcmp(name, "checkCritical") == 0) {
        bCritical = ReportViewParams::getcheckCritical();
    }
    else if (strcmp(name, "colorText") == 0) {
        unsigned long col = ReportViewParams::getcolorText();
        if (col == 0) {
            QPalette pal = palette();
            QColor color = pal.windowText().color();
            unsigned int text = (color.red() << 24) | (color.green() << 16) | (color.blue() << 8);
            col = static_cast<unsigned long>(text);
        }
        reportHl->setTextColor(App::Color::fromPackedRGB<QColor>(col));
    }
    else if (strcmp(name, "colorLogging") == 0) {
        reportHl->setLogColor(App::Color::fromPackedRGB<QColor>(ReportViewParams::getcolorLogging()));
    }
    else if (strcmp(name, "colorWarning") == 0) {
        reportHl->setWarningColor(App::Color::fromPackedRGB<QColor>(ReportViewParams::getcolorWarning()));
    }
    else if (strcmp(name, "colorError") == 0) {
        reportHl->setErrorColor(App::Color::fromPackedRGB<QColor>(ReportViewParams::getcolorError()));
    }
    else if (strcmp(name, "checkGoToEnd") == 0) {
        gotoEnd = ReportViewParams::getcheckGoToEnd();
    }
    else if (strcmp(name, "RedirectPythonOutput") == 0) {
        const bool on = ReportViewParams::getRedirectPythonOutput();
        if (on != d->redirected_stdout) {
            d->redirected_stdout = on;
            Base::PyGILStateLocker lock;
            PySys_SetObject("stdout", on ? d->replace_stdout : d->default_stdout);
        }
    }
    else if (strcmp(name, "RedirectPythonErrors") == 0) {
        const bool on = ReportViewParams::getRedirectPythonErrors();
        if (on != d->redirected_stderr) {
            d->redirected_stderr = on;
            Base::PyGILStateLocker lock;
            PySys_SetObject("stderr", on ? d->replace_stderr : d->default_stderr);
        }
    }
}

void ReportOutput::OnChange(Base::Subject<const char*> &rCaller, const char * sReason)
{
    ParameterGrp& rclGrp = ((ParameterGrp&)rCaller);
    // The view's own settings are applySetting()'s, told by ReportViewParams.
    // What is left here is what the Editor group and MaxLines say.
    if (strcmp(sReason, "colorCriticalText") == 0) {
        unsigned long col = rclGrp.GetUnsigned( sReason );
        reportHl->setTextColor( QColor( (col >> 24) & 0xff,(col >> 16) & 0xff,(col >> 8) & 0xff) );
    }
    else if (strcmp(sReason, "FontSize") == 0 || strcmp(sReason, "Font") == 0) {
        int fontSize = rclGrp.GetInt("FontSize", 10);
        QString fontFamily = QString::fromUtf8(rclGrp.GetASCII("Font", "Courier").c_str());

        QFont font(fontFamily, fontSize);
        setFont(font);
        fitFoldMargin();
        QFontMetrics metric(font);
        int width = QtTools::horizontalAdvance(metric, QStringLiteral("0000"));
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
        setTabStopWidth(width);
#else
        setTabStopDistance(width);
#endif
    }
    else if (strcmp(sReason, "MaxLines") == 0) {
        this->document()->setMaximumBlockCount(rclGrp.GetInt("MaxLines", 10000));
    }
}

#include "moc_ReportView.cpp"
