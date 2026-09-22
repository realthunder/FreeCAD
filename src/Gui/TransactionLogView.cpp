/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
# include <QDateTime>
# include <QFontDatabase>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QLabel>
# include <QLineEdit>
# include <QMenu>
# include <QPlainTextEdit>
# include <QPushButton>
# include <QSplitter>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/TransactionLog.h>
#include <Base/Console.h>

#include "TransactionLogView.h"
#include "Application.h"
#include "Document.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::DockWnd;

namespace {

enum TxnColumn { TxnSeq, TxnKind, TxnOrigin, TxnName, TxnTime, TxnParent, TxnColumns };
enum OpColumn { OpIdx, OpOp, OpContainer, OpProp, OpType, OpBefore, OpAfter, OpDerived, OpColumns };

QString shortRef(const std::string& ref)
{
    if (ref.empty())
        return QStringLiteral("-");
    return QString::fromStdString(ref.substr(0, 12));
}

QString containerText(const App::LogOp& op)
{
    if (op.ckind == "doc")
        return QStringLiteral("doc");
    QString s = QStringLiteral("%1:%2").arg(QString::fromStdString(op.ckind)).arg(op.cid);
    if (!op.cname.empty())
        s += QStringLiteral(" %1").arg(QString::fromStdString(op.cname));
    return s;
}

} // namespace

TransactionLogView::TransactionLogView(Gui::Document* pcDocument, QWidget* parent)
    : DockWindow(pcDocument, parent)
{
    setWindowTitle(tr("Transaction log"));

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    auto bar = new QHBoxLayout();
    _filter = new QLineEdit(this);
    _filter->setPlaceholderText(tr("Filter by object, property, name or kind"));
    _filter->setClearButtonEnabled(true);
    bar->addWidget(_filter, 1);
    _resolve = new QPushButton(tr("Resolve pending"), this);
    _resolve->setToolTip(tr("Serialise the live value behind every pending 'after' reference"));
    bar->addWidget(_resolve);
    layout->addLayout(bar);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_status);

    auto splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter, 1);

    _transactions = new QTreeWidget(splitter);
    _transactions->setColumnCount(TxnColumns);
    _transactions->setHeaderLabels({tr("Seq"), tr("Kind"), tr("Origin"), tr("Name"),
                                    tr("Time"), tr("Parent")});
    _transactions->setRootIsDecorated(false);
    _transactions->setAlternatingRowColors(true);
    _transactions->setUniformRowHeights(true);
    _transactions->setSelectionMode(QAbstractItemView::SingleSelection);
    _transactions->setContextMenuPolicy(Qt::CustomContextMenu);
    _transactions->header()->setStretchLastSection(false);
    _transactions->header()->setSectionResizeMode(TxnName, QHeaderView::Stretch);

    _ops = new QTreeWidget(splitter);
    _ops->setColumnCount(OpColumns);
    _ops->setHeaderLabels({tr("#"), tr("Op"), tr("Container"), tr("Property"), tr("Type"),
                           tr("Before"), tr("After"), tr("Derived")});
    _ops->setRootIsDecorated(false);
    _ops->setAlternatingRowColors(true);
    _ops->setUniformRowHeights(true);
    _ops->setSelectionMode(QAbstractItemView::SingleSelection);
    _ops->header()->setStretchLastSection(false);
    _ops->header()->setSectionResizeMode(OpContainer, QHeaderView::Stretch);

    _value = new QPlainTextEdit(splitter);
    _value->setReadOnly(true);
    _value->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    _value->setFont(mono);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 2);

    _refreshTimer.setSingleShot(true);
    _refreshTimer.setInterval(0);
    connect(&_refreshTimer, &QTimer::timeout, this, &TransactionLogView::refresh);

    connect(_transactions, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onTransactionSelected);
    connect(_transactions, &QTreeWidget::customContextMenuRequested,
            this, &TransactionLogView::onTransactionContextMenu);
    connect(_ops, &QTreeWidget::itemSelectionChanged, this, &TransactionLogView::onOpSelected);
    connect(_filter, &QLineEdit::textChanged, this, &TransactionLogView::onFilterChanged);
    connect(_resolve, &QPushButton::clicked, this, &TransactionLogView::onResolvePending);

    //NOLINTBEGIN
    _connActiveDoc = Application::Instance->signalActiveDocument.connect(
        [this](const Gui::Document& doc) {
            attach(doc.getDocument());
        });
    _connDeleteDoc = App::GetApplication().signalDeleteDocument.connect(
        [this](const App::Document& doc) {
            if (&doc == _doc)
                detach();
        });
    // A document made while the log is on gets its log at its first
    // commit; a refresh then picks the store up.
    _connNewDoc = App::GetApplication().signalNewDocument.connect(
        [this](const App::Document&, bool) {
            scheduleRefresh();
        });
    //NOLINTEND

    if (auto gdoc = Application::Instance->activeDocument())
        attach(gdoc->getDocument());
    else
        updateStatus();
}

TransactionLogView::~TransactionLogView() = default;

App::TransactionLog* TransactionLogView::log() const
{
    return _doc ? _doc->getTransactionLog() : nullptr;
}

void TransactionLogView::attach(App::Document* doc)
{
    if (doc == _doc)
        return;
    detach();
    _doc = doc;
    if (!_doc) {
        updateStatus();
        return;
    }
    //NOLINTBEGIN
    _connections.emplace_back(_doc->signalCommitTransaction.connect(
        [this](const App::Document&) { scheduleRefresh(); }));
    _connections.emplace_back(_doc->signalRecomputed.connect(
        [this](const App::Document&, const std::vector<App::DocumentObject*>&) {
            scheduleRefresh();
        }));
    _connections.emplace_back(_doc->signalUndo.connect(
        [this](const App::Document&) { scheduleRefresh(); }));
    _connections.emplace_back(_doc->signalRedo.connect(
        [this](const App::Document&) { scheduleRefresh(); }));
    //NOLINTEND
    reload();
}

void TransactionLogView::detach()
{
    _connections.clear();
    _doc = nullptr;
    _lastSeq = 0;
    _transactions->clear();
    _ops->clear();
    _value->clear();
    updateStatus();
}

void TransactionLogView::onUpdate()
{
    scheduleRefresh();
}

void TransactionLogView::scheduleRefresh()
{
    _stale = true;
    if (isVisible())
        _refreshTimer.start();
}

void TransactionLogView::showEvent(QShowEvent* ev)
{
    DockWindow::showEvent(ev);
    if (_stale)
        _refreshTimer.start();
}

void TransactionLogView::hideEvent(QHideEvent* ev)
{
    _refreshTimer.stop();
    DockWindow::hideEvent(ev);
}

void TransactionLogView::reload()
{
    _transactions->clear();
    _ops->clear();
    _value->clear();
    _lastSeq = 0;
    refresh();
}

void TransactionLogView::refresh()
{
    _stale = false;
    auto l = log();
    if (!l) {
        updateStatus();
        return;
    }
    try {
        int64_t last = l->store().lastSeq();
        if (last < _lastSeq) {
            // Truncated or replaced underneath us: start over.
            _transactions->clear();
            _lastSeq = 0;
        }
        if (last > _lastSeq)
            appendTransactions(_lastSeq + 1);
        _lastSeq = last;
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
    updateStatus();
}

void TransactionLogView::appendTransactions(int64_t fromSeq)
{
    auto l = log();
    if (!l)
        return;
    const QString filter = _filter->text().trimmed();
    QTreeWidgetItem* lastItem = nullptr;
    for (const auto& t : l->store().transactions(fromSeq, 0)) {
        auto item = new QTreeWidgetItem(_transactions);
        item->setText(TxnSeq, QString::number(t.seq));
        item->setData(TxnSeq, Qt::UserRole, QVariant::fromValue(static_cast<qlonglong>(t.seq)));
        item->setText(TxnKind, QString::fromStdString(t.kind));
        item->setText(TxnOrigin, QString::fromStdString(t.origin));
        item->setText(TxnName, QString::fromStdString(t.name));
        item->setText(TxnTime, QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(t.time * 1000))
                                   .toString(QStringLiteral("HH:mm:ss.zzz")));
        item->setText(TxnParent, QString::number(t.parent));
        item->setData(TxnName, Qt::UserRole, QString::fromStdString(t.script));
        item->setTextAlignment(TxnSeq, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(TxnParent, Qt::AlignRight | Qt::AlignVCenter);
        if (!t.script.empty())
            item->setToolTip(TxnName, QString::fromStdString(t.script));
        if (!filter.isEmpty()) {
            bool match = false;
            for (int c = 0; c < TxnColumns && !match; ++c)
                match = item->text(c).contains(filter, Qt::CaseInsensitive);
            if (!match)
                match = item->data(TxnName, Qt::UserRole).toString()
                            .contains(filter, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
        lastItem = item;
    }
    if (lastItem)
        _transactions->scrollToItem(lastItem);
    for (int c = 0; c < TxnColumns; ++c) {
        if (c != TxnName)
            _transactions->resizeColumnToContents(c);
    }
}

void TransactionLogView::onTransactionSelected()
{
    _ops->clear();
    _value->clear();
    auto items = _transactions->selectedItems();
    if (items.isEmpty())
        return;
    auto item = items.front();
    int64_t seq = item->data(TxnSeq, Qt::UserRole).toLongLong();
    showOps(seq);
    // A transaction with no ops -- the recompute record -- carries its
    // payload in the script column; show it where a value would go.
    if (_ops->topLevelItemCount() == 0)
        _value->setPlainText(item->data(TxnName, Qt::UserRole).toString());
}

void TransactionLogView::showOps(int64_t seq)
{
    auto l = log();
    if (!l)
        return;
    try {
        for (const auto& op : l->store().ops(seq)) {
            auto item = new QTreeWidgetItem(_ops);
            item->setText(OpIdx, QString::number(op.idx));
            item->setText(OpOp, QString::fromStdString(op.op));
            item->setText(OpContainer, containerText(op));
            if (!op.ctype.empty())
                item->setToolTip(OpContainer, QString::fromStdString(op.ctype));
            item->setText(OpProp, QString::fromStdString(op.prop));
            item->setText(OpType, QString::fromStdString(op.ptype));
            item->setText(OpBefore, shortRef(op.vbefore));
            item->setText(OpAfter, op.op == "set" && op.vafter.empty()
                                       ? tr("pending") : shortRef(op.vafter));
            item->setText(OpDerived, op.derived ? QStringLiteral("yes") : QString());
            item->setData(OpBefore, Qt::UserRole, QString::fromStdString(op.vbefore));
            item->setData(OpAfter, Qt::UserRole, QString::fromStdString(op.vafter));
            item->setData(OpProp, Qt::UserRole, QString::fromStdString(op.meta));
            if (!op.meta.empty())
                item->setToolTip(OpProp, QString::fromStdString(op.meta));
            item->setTextAlignment(OpIdx, Qt::AlignRight | Qt::AlignVCenter);
        }
        for (int c = 0; c < OpColumns; ++c) {
            if (c != OpContainer)
                _ops->resizeColumnToContents(c);
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
}

void TransactionLogView::onOpSelected()
{
    _value->clear();
    auto items = _ops->selectedItems();
    if (items.isEmpty())
        return;
    auto item = items.front();
    QString before = item->data(OpBefore, Qt::UserRole).toString();
    QString after = item->data(OpAfter, Qt::UserRole).toString();
    QString meta = item->data(OpProp, Qt::UserRole).toString();
    QString text;
    if (!meta.isEmpty())
        text += tr("meta: %1\n\n").arg(meta);
    // The after value is what the property became; show it first, the
    // before value after it, each under its full ref.
    auto section = [this, &text](const QString& label, const QString& ref) {
        if (ref.isEmpty())
            return;
        text += QStringLiteral("== %1 %2\n").arg(label, ref);
        App::CapturedValue v;
        auto l = log();
        if (l && l->readValue(ref.toStdString(), v)) {
            text += QString::fromStdString(v.fragment);
            if (!text.endsWith(QLatin1Char('\n')))
                text += QLatin1Char('\n');
            for (const auto& a : v.attachments)
                text += tr("-- attachment %1: %2 bytes\n")
                            .arg(QString::fromStdString(a.name)).arg(a.bytes.size());
        }
        else {
            text += tr("(value not in store)\n");
        }
        text += QLatin1Char('\n');
    };
    section(QStringLiteral("after"), after);
    section(QStringLiteral("before"), before);
    _value->setPlainText(text);
}

void TransactionLogView::onFilterChanged(const QString& text)
{
    const QString filter = text.trimmed();
    for (int i = 0; i < _transactions->topLevelItemCount(); ++i) {
        auto item = _transactions->topLevelItem(i);
        bool match = filter.isEmpty();
        for (int c = 0; c < TxnColumns && !match; ++c)
            match = item->text(c).contains(filter, Qt::CaseInsensitive);
        if (!match)
            match = item->data(TxnName, Qt::UserRole).toString().contains(filter, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void TransactionLogView::onResolvePending()
{
    auto l = log();
    if (!l)
        return;
    try {
        l->resolvePending();
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
    // Refs on the ops shown change from pending to resolved.
    onTransactionSelected();
    updateStatus();
}

void TransactionLogView::onTransactionContextMenu(const QPoint& pos)
{
    auto item = _transactions->itemAt(pos);
    if (!item)
        return;
    QMenu menu(this);
    auto copyScript = menu.addAction(tr("Copy script"));
    copyScript->setEnabled(!item->data(TxnName, Qt::UserRole).toString().isEmpty());
    auto copyRow = menu.addAction(tr("Copy row"));
    auto chosen = menu.exec(_transactions->viewport()->mapToGlobal(pos));
    if (chosen == copyScript) {
        QApplication::clipboard()->setText(item->data(TxnName, Qt::UserRole).toString());
    }
    else if (chosen == copyRow) {
        QStringList cells;
        for (int c = 0; c < TxnColumns; ++c)
            cells << item->text(c);
        QApplication::clipboard()->setText(cells.join(QLatin1Char('\t')));
    }
}

void TransactionLogView::updateStatus()
{
    if (!_doc) {
        _status->setText(tr("No active document"));
        _resolve->setEnabled(false);
        return;
    }
    auto l = log();
    if (!l) {
        if (App::DocumentParams::getTransactionLog() == 0)
            _status->setText(tr("%1: log off (Preferences/Document/TransactionLog = 0)")
                                 .arg(QString::fromUtf8(_doc->getName())));
        else
            _status->setText(tr("%1: no log yet (made at the first commit)")
                                 .arg(QString::fromUtf8(_doc->getName())));
        _resolve->setEnabled(false);
        return;
    }
    _resolve->setEnabled(true);
    size_t versions = 0;
    try {
        versions = l->store().versions().size();
    }
    catch (Base::Exception&) {
    }
    _status->setText(tr("%1: %2 transactions, %3 versions, %4 pending, session %5  --  %6")
                         .arg(QString::fromUtf8(_doc->getName()))
                         .arg(_lastSeq)
                         .arg(static_cast<qulonglong>(versions))
                         .arg(static_cast<qulonglong>(l->pendingCount()))
                         .arg(static_cast<qlonglong>(l->session()))
                         .arg(QString::fromStdString(l->path())));
}

#include "moc_TransactionLogView.cpp"
