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
# include <map>
# include <set>
# include <unordered_map>
# include <QApplication>
# include <QCheckBox>
# include <QClipboard>
# include <QComboBox>
# include <QDateTime>
# include <QFontDatabase>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QInputDialog>
# include <QLabel>
# include <QLineEdit>
# include <QMenu>
# include <QMessageBox>
# include <QPainter>
# include <QPainterPath>
# include <QPlainTextEdit>
# include <QStyledItemDelegate>
# include <QPushButton>
# include <QSplitter>
# include <QStackedWidget>
# include <QStandardItemModel>
# include <QTabWidget>
# include <QScrollBar>
# include <QTreeView>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentParams.h>
#include <App/FileBlobManager.h>
#include <App/TransactionLog.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include "TransactionLogView.h"
#include "Application.h"
#include "Document.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::DockWnd;

namespace {

enum TxnColumn { TxnGraph, TxnSeq, TxnKind, TxnOrigin, TxnName, TxnTime, TxnParent, TxnInverts,
                 TxnBranch, TxnColumns };

// Item data roles of a transaction row, besides the seq on TxnSeq.
constexpr int RoleParent = Qt::UserRole + 1;   // on TxnParent: the parent seq
constexpr int RoleBranch = Qt::UserRole + 2;   // on TxnBranch: the branch id
constexpr int RoleRecord = Qt::UserRole + 3;   // on TxnKind: true for a row with no ops
enum OpColumn { OpIdx, OpOp, OpContainer, OpProp, OpType, OpBefore, OpAfter, OpDerived, OpColumns };
enum VerColumn { VerNum, VerKind, VerName, VerBranch, VerSeq, VerSchema, VerCreated, VerDocXml,
                 VerEntries, VerColumns };
enum ManColumn { ManEntry, ManSource, ManHash, ManColumns };

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

/** The graph column (sec 26), laid out by layoutGraph() over the rows
 * shown, newest first as git draws it: every row a node on a lane, each
 * lane waiting for the parent of the row above it, lanes waiting for the
 * same row converging on it -- a fork, seen from its branches. Lanes are
 * reused, never shifted. Labels follow the lanes: the branch heads, the
 * current one bold, and the versions taken at a row.
 */
struct TransactionLogView::GraphLayout
{
    struct Lane
    {
        int lane;
        int to;          // where its top half ends: itself, or the node it joins
        int64_t branch;  // the colour
    };
    struct Row
    {
        int lane {0};
        int64_t branch {0};
        bool record {false};
        std::vector<Lane> top;      // lanes coming in from above
        std::vector<Lane> bottom;   // lanes going on below
        QStringList heads;
        bool current {false};
        QStringList versions;
    };
    std::unordered_map<qlonglong, Row> rows;
    int lanes {0};
};

namespace {

constexpr int LaneWidth = 14;
constexpr int LaneMargin = 8;

QColor branchColour(int64_t branch)
{
    static const QColor palette[] = {
        QColor(0x1f, 0x77, 0xb4), QColor(0xd6, 0x5f, 0x0e), QColor(0x2c, 0xa0, 0x2c),
        QColor(0x94, 0x67, 0xbd), QColor(0xc2, 0x3b, 0x5a), QColor(0x17, 0x9e, 0xa8),
        QColor(0x8c, 0x6d, 0x2c), QColor(0x60, 0x70, 0x80),
    };
    return palette[static_cast<size_t>(branch > 0 ? branch - 1 : 0) % 8];
}

class GraphDelegate: public QStyledItemDelegate
{
public:
    GraphDelegate(const TransactionLogView::GraphLayout& layout, QObject* parent)
        : QStyledItemDelegate(parent), _layout(layout)
    {}

    const TransactionLogView::GraphLayout::Row* rowOf(const QModelIndex& index) const
    {
        const qlonglong seq = index.sibling(index.row(), TxnSeq).data(Qt::UserRole).toLongLong();
        auto it = _layout.rows.find(seq);
        return it == _layout.rows.end() ? nullptr : &it->second;
    }

    static QFont labelFont(const QFont& base, bool bold)
    {
        QFont f(base);
        f.setPointSizeF(base.pointSizeF() * 0.9);
        f.setBold(bold);
        return f;
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        // As tall as the list's rows, which carry text: the two views are
        // scrolled together and a row must sit level in both.
        QSize size = QStyledItemDelegate::sizeHint(option, index.sibling(index.row(), TxnSeq));
        int width = LaneMargin * 2 + _layout.lanes * LaneWidth;
        if (auto row = rowOf(index)) {
            for (const auto& h : row->heads)
                width += QFontMetrics(labelFont(option.font, true)).horizontalAdvance(h) + 12;
            for (const auto& v : row->versions)
                width += QFontMetrics(labelFont(option.font, false)).horizontalAdvance(v) + 12;
        }
        size.setWidth(width);
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);   // background, selection
        auto row = rowOf(index);
        if (!row)
            return;
        const QRect r = option.rect;
        const qreal top = r.top();
        const qreal bottom = r.bottom() + 1;
        const qreal mid = r.center().y() + 0.5;
        auto x = [&](int lane) { return r.left() + LaneMargin + lane * LaneWidth + 0.5; };

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        auto pen = [](int64_t branch) {
            QPen p(branchColour(branch));
            p.setWidthF(2.0);
            return p;
        };
        for (const auto& l : row->top) {
            painter->setPen(pen(l.branch));
            if (l.to == l.lane) {
                painter->drawLine(QPointF(x(l.lane), top), QPointF(x(l.lane), mid));
            }
            else {
                QPainterPath path(QPointF(x(l.lane), top));
                path.cubicTo(QPointF(x(l.lane), mid), QPointF(x(l.to), top),
                             QPointF(x(l.to), mid));
                painter->drawPath(path);
            }
        }
        for (const auto& l : row->bottom) {
            painter->setPen(pen(l.branch));
            painter->drawLine(QPointF(x(l.lane), mid), QPointF(x(l.lane), bottom));
        }
        // The node: filled for a change, hollow for a record.
        const QColor colour = branchColour(row->branch);
        painter->setPen(QPen(colour, 2.0));
        painter->setBrush(row->record ? option.palette.base() : QBrush(colour));
        const qreal radius = row->record ? 3.0 : 4.0;
        painter->drawEllipse(QPointF(x(row->lane), mid), radius, radius);

        // Labels, right of the lanes.
        qreal lx = r.left() + LaneMargin * 2 + _layout.lanes * LaneWidth;
        auto label = [&](const QString& text, const QColor& fill, const QColor& fg, bool bold) {
            const QFont font = labelFont(option.font, bold);
            const QFontMetrics fm(font);
            const qreal w = fm.horizontalAdvance(text) + 8;
            const qreal h = std::min<qreal>(fm.height() + 2, r.height() - 2);
            QRectF box(lx, mid - h / 2, w, h);
            painter->setPen(QPen(fill.darker(130), 1.0));
            painter->setBrush(fill);
            painter->drawRoundedRect(box, 3, 3);
            painter->setFont(font);
            painter->setPen(fg);
            painter->drawText(box, Qt::AlignCenter, text);
            lx += w + 4;
        };
        for (int i = 0; i < row->heads.size(); ++i) {
            const QColor fill = branchColour(row->branch);
            label(row->heads[i], fill, Qt::white, row->current && i == 0);
        }
        for (const auto& v : row->versions)
            label(v, option.palette.alternateBase().color(), option.palette.text().color(), false);
        painter->restore();
    }

private:
    const TransactionLogView::GraphLayout& _layout;
};

} // namespace

TransactionLogView::TransactionLogView(Gui::Document* pcDocument, QWidget* parent)
    : DockWindow(pcDocument, parent)
    , _graph(std::make_unique<GraphLayout>())
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
    _snapshot = new QPushButton(tr("Snapshot"), this);
    _snapshot->setToolTip(tr("Take an unnamed version of the document as it stands (sec 16.3)"));
    bar->addWidget(_snapshot);
    layout->addLayout(bar);

    // Branches (sec 26): the one the document is on, switched here; a new
    // one from the head; and whether rows of other branches show.
    auto branchBar = new QHBoxLayout();
    branchBar->addWidget(new QLabel(tr("Branch:"), this));
    _branch = new QComboBox(this);
    _branch->setToolTip(tr("The branch the document is on; choosing another switches to its "
                           "head in place (not an undo step)"));
    _branch->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    branchBar->addWidget(_branch);
    _newBranch = new QPushButton(tr("Branch..."), this);
    _newBranch->setToolTip(tr("A new branch from the current head, switched to"));
    branchBar->addWidget(_newBranch);
    _renameBranch = new QPushButton(tr("Rename..."), this);
    _renameBranch->setToolTip(tr("Rename the branch shown in the switcher"));
    branchBar->addWidget(_renameBranch);
    _deleteBranch = new QPushButton(tr("Delete..."), this);
    _deleteBranch->setToolTip(tr("Delete another branch: the rows only it holds and its "
                                 "versions, but those a branch forked from (sec 16.7)"));
    branchBar->addWidget(_deleteBranch);
    _allBranches = new QCheckBox(tr("All branches"), this);
    _allBranches->setToolTip(tr("Show the rows of every branch, not only this branch's history"));
    branchBar->addWidget(_allBranches);
    _hideRecords = new QCheckBox(tr("Hide records"), this);
    _hideRecords->setToolTip(tr("Hide the rows that changed nothing: recompute records, "
                                "snapshots, saves, switches"));
    branchBar->addWidget(_hideRecords);
    branchBar->addStretch(1);
    layout->addLayout(branchBar);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_status);

    auto splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter, 1);

    // Two views over the store on the first pane -- the transactions and
    // the versions -- each with its own detail on the second: the ops of
    // a transaction, the manifest of a version. The value pane serves both.
    _tabs = new QTabWidget(splitter);
    _tabs->setDocumentMode(true);
    // The graph and the list side by side: the graph a pane of its own, so
    // lanes and labels scroll sideways without moving the list, and the
    // splitter sizes it or folds it away.
    auto txnSplitter = new QSplitter(Qt::Horizontal, _tabs);
    _tabs->addTab(txnSplitter, tr("Transactions"));
    _graphView = new QTreeView(txnSplitter);
    _transactions = new QTreeWidget(txnSplitter);
    _transactions->setColumnCount(TxnColumns);
    _transactions->setHeaderLabels({tr("Graph"), tr("Seq"), tr("Kind"), tr("Origin"), tr("Name"),
                                    tr("Time"), tr("Parent"), tr("Inverts"), tr("Branch")});
    _transactions->hideColumn(TxnGraph);
    _graphView->setModel(_transactions->model());
    _graphView->setSelectionModel(_transactions->selectionModel());
    _graphView->setItemDelegateForColumn(TxnGraph, new GraphDelegate(*_graph, _graphView));
    for (int c = 0; c < TxnColumns; ++c)
        _graphView->setColumnHidden(c, c != TxnGraph);
    _graphView->setRootIsDecorated(false);
    _graphView->setAlternatingRowColors(true);
    _graphView->setUniformRowHeights(true);
    _graphView->setSelectionMode(QAbstractItemView::SingleSelection);
    _graphView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _graphView->setContextMenuPolicy(Qt::CustomContextMenu);
    _graphView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    _graphView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _graphView->header()->setStretchLastSection(false);
    _graphView->header()->setSectionResizeMode(TxnGraph, QHeaderView::ResizeToContents);
    // The list's horizontal bar is kept, so the two viewports are the same
    // height and a row sits level in both.
    _transactions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    _graphView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    connect(_transactions->verticalScrollBar(), &QScrollBar::valueChanged,
            _graphView->verticalScrollBar(), &QScrollBar::setValue);
    connect(_graphView->verticalScrollBar(), &QScrollBar::valueChanged,
            _transactions->verticalScrollBar(), &QScrollBar::setValue);
    connect(_graphView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = _graphView->indexAt(pos);
        if (index.isValid())
            transactionMenu(_transactions->topLevelItem(index.row()),
                            _graphView->viewport()->mapToGlobal(pos));
    });
    txnSplitter->setChildrenCollapsible(true);
    txnSplitter->setStretchFactor(0, 0);
    txnSplitter->setStretchFactor(1, 1);
    txnSplitter->setSizes({180, 900});
    _transactions->setRootIsDecorated(false);
    _transactions->setAlternatingRowColors(true);
    _transactions->setUniformRowHeights(true);
    _transactions->setSelectionMode(QAbstractItemView::SingleSelection);
    _transactions->setContextMenuPolicy(Qt::CustomContextMenu);
    _transactions->header()->setStretchLastSection(false);
    _transactions->header()->setSectionResizeMode(TxnName, QHeaderView::Stretch);

    _versions = new QTreeWidget(_tabs);
    _tabs->addTab(_versions, tr("Versions"));
    _versions->setColumnCount(VerColumns);
    _versions->setHeaderLabels({tr("Num"), tr("Kind"), tr("Name"), tr("Branch"), tr("Seq"),
                                tr("Schema"), tr("Created"), tr("Document.xml"), tr("Entries")});
    _versions->setRootIsDecorated(false);
    _versions->setAlternatingRowColors(true);
    _versions->setUniformRowHeights(true);
    _versions->setSelectionMode(QAbstractItemView::SingleSelection);
    _versions->setContextMenuPolicy(Qt::CustomContextMenu);
    _versions->header()->setStretchLastSection(false);
    _versions->header()->setSectionResizeMode(VerName, QHeaderView::Stretch);

    _detail = new QStackedWidget(splitter);
    _ops = new QTreeWidget(_detail);
    _detail->addWidget(_ops);
    _ops->setColumnCount(OpColumns);
    _ops->setHeaderLabels({tr("#"), tr("Op"), tr("Container"), tr("Property"), tr("Type"),
                           tr("Before"), tr("After"), tr("Derived")});
    _ops->setRootIsDecorated(false);
    _ops->setAlternatingRowColors(true);
    _ops->setUniformRowHeights(true);
    _ops->setSelectionMode(QAbstractItemView::SingleSelection);
    _ops->header()->setStretchLastSection(false);
    _ops->header()->setSectionResizeMode(OpContainer, QHeaderView::Stretch);

    _manifest = new QTreeWidget(_detail);
    _detail->addWidget(_manifest);
    _manifest->setColumnCount(ManColumns);
    _manifest->setHeaderLabels({tr("Entry"), tr("Stored as"), tr("Hash")});
    _manifest->setRootIsDecorated(false);
    _manifest->setAlternatingRowColors(true);
    _manifest->setUniformRowHeights(true);
    _manifest->setSelectionMode(QAbstractItemView::SingleSelection);
    _manifest->header()->setStretchLastSection(false);
    _manifest->header()->setSectionResizeMode(ManEntry, QHeaderView::Stretch);

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
    connect(_versions, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onVersionSelected);
    connect(_versions, &QTreeWidget::customContextMenuRequested,
            this, &TransactionLogView::onVersionContextMenu);
    connect(_manifest, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onManifestSelected);
    connect(_tabs, &QTabWidget::currentChanged, this, &TransactionLogView::onTabChanged);
    connect(_filter, &QLineEdit::textChanged, this, &TransactionLogView::onFilterChanged);
    connect(_resolve, &QPushButton::clicked, this, &TransactionLogView::onResolvePending);
    connect(_snapshot, &QPushButton::clicked, this, &TransactionLogView::onSnapshot);
    connect(_branch, qOverload<int>(&QComboBox::activated), this,
            &TransactionLogView::onBranchChosen);
    connect(_newBranch, &QPushButton::clicked, this, &TransactionLogView::onNewBranch);
    connect(_deleteBranch, &QPushButton::clicked, this, &TransactionLogView::onDeleteBranch);
    connect(_renameBranch, &QPushButton::clicked, this, &TransactionLogView::onRenameBranch);
    connect(_allBranches, &QCheckBox::toggled, this, &TransactionLogView::applyVisibility);
    connect(_hideRecords, &QCheckBox::toggled, this, &TransactionLogView::applyVisibility);

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
    // The restore record and version 1 (sec 16.6) are written at the end of
    // the restore; a document reloaded in place keeps its identity.
    _connRestoreDoc = App::GetApplication().signalFinishRestoreDocument.connect(
        [this](const App::Document& doc) {
            if (&doc == _doc)
                reload();
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
    // Rows can go as well as come (a trim, a deleted branch): rebuilt whole.
    _connections.emplace_back(_doc->signalBranchesChanged.connect(
        [this](const App::Document&) { reload(); }));
    //NOLINTEND
    reload();
}

void TransactionLogView::detach()
{
    _connections.clear();
    _doc = nullptr;
    _lastSeq = 0;
    _lastVersion = 0;
    _transactions->clear();
    _ops->clear();
    _versions->clear();
    _manifest->clear();
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
    _versions->clear();
    _manifest->clear();
    _value->clear();
    _lastSeq = 0;
    _lastVersion = 0;
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
        refreshVersions();
        refreshBranches();
        applyVisibility();
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
    updateStatus();
}

void TransactionLogView::refreshVersions()
{
    auto l = log();
    if (!l)
        return;
    // Few rows, rebuilt whole whenever the count moved either way.
    auto versions = l->store().versions();
    int64_t last = versions.empty() ? 0 : versions.back().num;
    if (last == _lastVersion && static_cast<int>(versions.size()) == _versions->topLevelItemCount())
        return;
    _lastVersion = last;
    _versions->clear();
    _manifest->clear();
    std::map<int64_t, std::string> branches;
    for (const auto& b : l->store().branches())
        branches[b.id] = b.name;
    for (const auto& v : versions) {
        auto item = new QTreeWidgetItem(_versions);
        item->setText(VerNum, QString::number(v.num));
        item->setData(VerNum, Qt::UserRole, QVariant::fromValue(static_cast<qlonglong>(v.num)));
        item->setText(VerKind, QString::fromStdString(v.kind));
        item->setText(VerName, QString::fromStdString(v.name));
        item->setText(VerBranch, QString::fromStdString(branches[v.branch]));
        item->setText(VerSeq, QString::number(v.seq));
        item->setText(VerSchema, QString::number(v.schema));
        item->setText(VerCreated,
                      QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(v.created * 1000))
                          .toString(QStringLiteral("HH:mm:ss.zzz")));
        item->setText(VerDocXml, shortRef(v.docxml_hash));
        item->setToolTip(VerDocXml, QString::fromStdString(v.docxml_hash));
        item->setText(VerEntries, QString::number(l->store().manifest(v.num).size()));
        item->setToolTip(VerNum, QString::fromStdString(v.uuid));
        for (int c : {VerNum, VerSeq, VerSchema, VerEntries})
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    }
    for (int c = 0; c < VerColumns; ++c) {
        if (c != VerName)
            _versions->resizeColumnToContents(c);
    }
}

void TransactionLogView::onTabChanged(int index)
{
    _detail->setCurrentIndex(index == 0 ? 0 : 1);
    _value->clear();
    if (index == 0)
        onTransactionSelected();
    else
        onVersionSelected();
}

void TransactionLogView::onVersionSelected()
{
    _manifest->clear();
    _value->clear();
    auto items = _versions->selectedItems();
    if (items.isEmpty())
        return;
    showManifest(items.front()->data(VerNum, Qt::UserRole).toLongLong());
}

void TransactionLogView::showManifest(int64_t num)
{
    auto l = log();
    if (!l)
        return;
    try {
        for (const auto& e : l->store().manifest(num)) {
            auto item = new QTreeWidgetItem(_manifest);
            item->setText(ManEntry, QString::fromStdString(e.entry));
            // What the entity is and how it is kept: a blob as a file of
            // the document's store or as a delta (sec 23.16).
            App::LogEntity entity;
            std::string what = "(missing)";
            if (l->store().getEntity(e.hash, entity))
                what = entity.kind + " " + entity.enc;
            item->setText(ManSource, QString::fromStdString(what));
            item->setText(ManHash, QString::fromStdString(e.hash));
        }
        for (int c = 0; c < ManColumns; ++c) {
            if (c != ManEntry)
                _manifest->resizeColumnToContents(c);
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
}

void TransactionLogView::onManifestSelected()
{
    _value->clear();
    auto items = _manifest->selectedItems();
    if (items.isEmpty() || !_doc)
        return;
    auto item = items.front();
    const std::string hash = item->text(ManHash).toStdString();
    QString text = QStringLiteral("== %1 %2\n").arg(item->text(ManEntry), item->text(ManHash));
    auto l = log();
    App::LogEntity e;
    if (!l || !l->store().getEntity(hash, e)) {
        text += tr("(not in the store)\n");
    }
    else if (e.kind == "blob") {
        // A blob (sec 23.16): the document's store holds the newest as a
        // file, the log an older one as a patch toward its successor.
        if (auto blob = l->heldBlob(hash))
            text += tr("blob, %1 bytes, at %2\n")
                        .arg(e.size)
                        .arg(QString::fromStdString(blob->path()));
        else if (e.enc == "delta")
            text += tr("blob, %1 bytes, kept as a %2 byte patch toward %3\n")
                        .arg(e.size)
                        .arg(e.data.size())
                        .arg(QString::fromStdString(e.base));
        else
            text += tr("blob, %1 bytes, %2\n").arg(e.size).arg(QString::fromStdString(e.enc));
    }
    else {
        // An XML entry: a composite of skeleton and parts (sec 23.3),
        // shown composed, with what it is made of first; or the bytes
        // themselves when it was read rather than written.
        App::CapturedValue v;
        if (e.kind == "composite") {
            std::string data;
            App::TransactionLog::Composite c;
            if (l->readBytes(hash, data) && c.decode(data))
                text += tr("composite: skeleton %1, %2 parts\n")
                            .arg(QString::fromStdString(c.skeleton))
                            .arg(c.parts.size());
        }
        if (l->readValue(hash, v))
            text += QString::fromStdString(v.fragment);
        else
            text += tr("(value not in store)\n");
    }
    _value->setPlainText(text);
}

void TransactionLogView::appendTransactions(int64_t fromSeq)
{
    auto l = log();
    if (!l)
        return;
    QTreeWidgetItem* lastItem = nullptr;
    std::map<int64_t, QString> branches;
    for (const auto& b : l->store().branches())
        branches[b.id] = QString::fromStdString(b.name);
    const QColor recordColour = _transactions->palette().color(QPalette::Disabled, QPalette::Text);
    for (const auto& t : l->store().transactions(fromSeq, 0)) {
        // Newest first, as git lists history: each new row goes on top.
        auto item = new QTreeWidgetItem();
        _transactions->insertTopLevelItem(0, item);
        item->setText(TxnSeq, QString::number(t.seq));
        item->setData(TxnSeq, Qt::UserRole, QVariant::fromValue(static_cast<qlonglong>(t.seq)));
        item->setText(TxnKind, QString::fromStdString(t.kind));
        item->setText(TxnOrigin, QString::fromStdString(t.origin));
        item->setText(TxnName, QString::fromStdString(t.name));
        item->setText(TxnTime, QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(t.time * 1000))
                                   .toString(QStringLiteral("HH:mm:ss.zzz")));
        item->setText(TxnParent, QString::number(t.parent));
        item->setData(TxnParent, RoleParent, QVariant::fromValue(static_cast<qlonglong>(t.parent)));
        item->setData(TxnBranch, RoleBranch, QVariant::fromValue(static_cast<qlonglong>(t.branch)));
        // A record changed nothing (sec 11): greyed, and hidden on request.
        const bool record = l->store().ops(t.seq).empty();
        item->setData(TxnKind, RoleRecord, record);
        if (record) {
            for (int c = 0; c < TxnColumns; ++c)
                item->setForeground(c, recordColour);
        }
        item->setData(TxnName, Qt::UserRole, QString::fromStdString(t.script));
        item->setTextAlignment(TxnSeq, Qt::AlignRight | Qt::AlignVCenter);
        item->setTextAlignment(TxnParent, Qt::AlignRight | Qt::AlignVCenter);
        if (t.inverts > 0) {
            item->setText(TxnInverts, QString::number(t.inverts));
            item->setTextAlignment(TxnInverts, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (!t.script.empty())
            item->setToolTip(TxnName, QString::fromStdString(t.script));
        item->setText(TxnBranch, branches[t.branch]);
        lastItem = item;
    }
    if (lastItem)
        _transactions->scrollToItem(lastItem);
    for (int c = 0; c < TxnColumns; ++c) {
        if (c != TxnName && c != TxnGraph)
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

void TransactionLogView::onFilterChanged(const QString&)
{
    applyVisibility();
}

void TransactionLogView::applyVisibility()
{
    // A row shows when it matches the filter and, unless every branch is
    // asked for, lies on the current branch's chain (sec 26).
    const QString filter = _filter->text().trimmed();
    std::set<int64_t> chain;
    const bool all = _allBranches->isChecked();
    auto l = log();
    if (!all && l) {
        try {
            for (const auto& t : l->store().chain(l->head()))
                chain.insert(t.seq);
        }
        catch (Base::Exception& e) {
            FC_ERR("transaction log view: " << e.what());
        }
    }
    for (int i = 0; i < _transactions->topLevelItemCount(); ++i) {
        auto item = _transactions->topLevelItem(i);
        bool match = filter.isEmpty();
        for (int c = 0; c < TxnColumns && !match; ++c)
            match = item->text(c).contains(filter, Qt::CaseInsensitive);
        if (!match)
            match = item->data(TxnName, Qt::UserRole).toString().contains(filter, Qt::CaseInsensitive);
        if (match && !all && l)
            match = chain.count(item->data(TxnSeq, Qt::UserRole).toLongLong()) != 0;
        if (match && _hideRecords->isChecked())
            match = !item->data(TxnKind, RoleRecord).toBool();
        item->setHidden(!match);
        _graphView->setRowHidden(i, QModelIndex(), !match);
    }
    layoutGraph();
}

void TransactionLogView::layoutGraph()
{
    auto& layout = *_graph;
    layout.rows.clear();
    layout.lanes = 0;
    auto l = log();
    if (!l)
        return;

    // Every row's parent, and which rows show; a row's graph parent is its
    // nearest ancestor that shows, so a filter keeps the graph whole.
    std::unordered_map<qlonglong, qlonglong> parentOf;
    std::unordered_map<qlonglong, QTreeWidgetItem*> shown;
    std::vector<QTreeWidgetItem*> order;   // top to bottom: newest first
    for (int i = 0; i < _transactions->topLevelItemCount(); ++i) {
        auto item = _transactions->topLevelItem(i);
        const qlonglong seq = item->data(TxnSeq, Qt::UserRole).toLongLong();
        parentOf[seq] = item->data(TxnParent, RoleParent).toLongLong();
        if (!item->isHidden()) {
            shown[seq] = item;
            order.push_back(item);
        }
    }
    auto visibleParent = [&](qlonglong seq) -> qlonglong {
        for (int guard = 0; guard < 1000000; ++guard) {
            auto it = parentOf.find(seq);
            if (it == parentOf.end() || it->second <= 0)
                return 0;
            seq = it->second;
            if (shown.count(seq))
                return seq;
        }
        return 0;
    };
    // The labels: each branch's head, on its nearest row that shows; the
    // versions, on the row each was taken at.
    auto nearestShown = [&](qlonglong seq) -> qlonglong {
        if (shown.count(seq))
            return seq;
        return visibleParent(seq);
    };
    std::unordered_map<qlonglong, QStringList> heads;
    std::unordered_map<qlonglong, bool> current;
    std::unordered_map<qlonglong, QStringList> versions;
    const bool all = _allBranches->isChecked();
    try {
        for (const auto& b : l->store().branches()) {
            // Another branch's head is not this history's: in the view of
            // one branch it would slide down to the fork and mislead.
            if (!all && b.id != l->branch())
                continue;
            const qlonglong at = nearestShown(b.head);
            if (!at)
                continue;
            QString name = QString::fromStdString(b.name);
            if (b.closed != 0)
                name += tr(" (closed)");
            if (b.id == l->branch()) {
                heads[at].prepend(name);
                current[at] = true;
            }
            else {
                heads[at].append(name);
            }
        }
        for (const auto& v : l->store().versions()) {
            if (!shown.count(v.seq))
                continue;
            QString text = QStringLiteral("v%1").arg(v.num);
            if (!v.name.empty())
                text += QLatin1Char(' ') + QString::fromStdString(v.name);
            versions[v.seq].append(text);
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }

    // The lanes, top to bottom.
    std::vector<qlonglong> waiting;    // the row each lane waits for, 0: free
    std::vector<int64_t> colour;       // the branch that set it going
    for (auto item : order) {
        const qlonglong seq = item->data(TxnSeq, Qt::UserRole).toLongLong();
        GraphLayout::Row row;
        row.branch = item->data(TxnBranch, RoleBranch).toLongLong();
        row.record = item->data(TxnKind, RoleRecord).toBool();
        int node = -1;
        for (size_t i = 0; i < waiting.size(); ++i) {
            if (waiting[i] == seq) {
                node = static_cast<int>(i);
                break;
            }
        }
        if (node < 0) {
            // A head: the first free lane.
            for (size_t i = 0; i < waiting.size() && node < 0; ++i) {
                if (!waiting[i])
                    node = static_cast<int>(i);
            }
            if (node < 0) {
                node = static_cast<int>(waiting.size());
                waiting.push_back(0);
                colour.push_back(0);
            }
        }
        row.lane = node;
        for (size_t i = 0; i < waiting.size(); ++i) {
            if (!waiting[i])
                continue;
            const int to = waiting[i] == seq ? node : static_cast<int>(i);
            row.top.push_back({static_cast<int>(i), to, colour[i]});
            if (waiting[i] == seq)
                waiting[i] = 0;   // joined here
        }
        const qlonglong parent = visibleParent(seq);
        if (parent) {
            waiting[node] = parent;
            colour[node] = row.branch;
        }
        for (size_t i = 0; i < waiting.size(); ++i) {
            if (waiting[i])
                row.bottom.push_back({static_cast<int>(i), static_cast<int>(i), colour[i]});
        }
        row.heads = heads[seq];
        row.current = current[seq];
        row.versions = versions[seq];
        layout.lanes = std::max<int>(layout.lanes, static_cast<int>(waiting.size()));
        layout.rows.emplace(seq, std::move(row));
    }
    _graphView->resizeColumnToContents(TxnGraph);
    _graphView->viewport()->update();
    _graphView->verticalScrollBar()->setValue(_transactions->verticalScrollBar()->value());
}

void TransactionLogView::refreshBranches()
{
    auto l = log();
    Base::FlagToggler<> filling(_fillingBranches);
    _branch->clear();
    if (!l) {
        _branch->setEnabled(false);
        _newBranch->setEnabled(false);
        _deleteBranch->setEnabled(false);
        _renameBranch->setEnabled(false);
        return;
    }
    int current = -1;
    for (const auto& b : l->store().branches()) {
        QString text = QString::fromStdString(b.name);
        if (b.closed != 0)
            text += tr(" (closed)");
        _branch->addItem(text, QString::fromStdString(b.name));
        if (b.closed != 0) {
            // A closed branch is not switched to (sec 26.6); branch from
            // one of its versions instead.
            if (auto model = qobject_cast<QStandardItemModel*>(_branch->model()))
                model->item(_branch->count() - 1)->setEnabled(false);
        }
        if (b.id == l->branch())
            current = _branch->count() - 1;
    }
    _branch->setCurrentIndex(current);
    _branch->setEnabled(true);
    _newBranch->setEnabled(_doc != nullptr);
    _deleteBranch->setEnabled(_doc != nullptr && _branch->count() > 1);
    _renameBranch->setEnabled(_doc != nullptr && _branch->currentIndex() >= 0);
}

void TransactionLogView::onRenameBranch()
{
    if (!_doc || _branch->currentIndex() < 0)
        return;
    const QString name = _branch->itemData(_branch->currentIndex()).toString();
    bool ok = false;
    QString text = QInputDialog::getText(this, tr("Rename branch"), tr("New name of %1:").arg(name),
                                         QLineEdit::Normal, name, &ok);
    if (!ok || text.trimmed().isEmpty() || text.trimmed() == name)
        return;
    try {
        _doc->renameBranch(name.toStdString(), text.trimmed().toStdString());
    }
    catch (Base::Exception& e) {
        FC_ERR("rename branch " << name.toStdString() << ": " << e.what());
        _status->setText(tr("Branch not renamed -- the report view says why"));
    }
}

void TransactionLogView::onDeleteBranch()
{
    auto l = log();
    if (!l || !_doc)
        return;
    QStringList names;
    for (const auto& b : l->store().branches()) {
        if (b.id != l->branch())
            names << QString::fromStdString(b.name);
    }
    if (names.isEmpty())
        return;
    bool ok = false;
    QString name = QInputDialog::getItem(this, tr("Delete branch"), tr("Branch:"), names, 0,
                                         false, &ok);
    if (!ok || name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete branch"),
                              tr("Delete branch %1 and the history only it holds? "
                                 "This cannot be undone.").arg(name))
            != QMessageBox::Yes)
        return;
    try {
        _doc->deleteBranch(name.toStdString());
    }
    catch (Base::Exception& e) {
        FC_ERR("delete branch " << name.toStdString() << ": " << e.what());
    }
}

void TransactionLogView::onBranchChosen(int index)
{
    if (_fillingBranches || !_doc || index < 0)
        return;
    const std::string name = _branch->itemData(index).toString().toStdString();
    try {
        _doc->switchBranch(name);
    }
    catch (Base::Exception& e) {
        FC_ERR("switch to branch " << name << ": " << e.what());
    }
    refresh();
}

void TransactionLogView::onNewBranch()
{
    createBranch(0, 0);
}

void TransactionLogView::createBranch(int64_t version, int64_t seq)
{
    if (!_doc)
        return;
    const QString from = version ? tr("version %1").arg(version)
                         : seq  ? tr("row %1").arg(seq)
                                : tr("the current head");
    bool ok = false;
    QString text = QInputDialog::getText(this, tr("New branch"),
                                         tr("Name of the branch from %1:").arg(from),
                                         QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;
    try {
        _doc->createBranch(text.trimmed().toStdString(), version, seq);
    }
    catch (Base::Exception& e) {
        FC_ERR("new branch " << text.toStdString() << ": " << e.what());
        _status->setText(tr("No branch made -- the report view says why"));
        return;
    }
    refresh();
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

void TransactionLogView::onSnapshot()
{
    if (!_doc)
        return;
    try {
        _doc->snapshotToLog();
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
    refresh();
    if (_tabs->currentIndex() == 1 && _versions->topLevelItemCount() > 0)
        _versions->setCurrentItem(_versions->topLevelItem(_versions->topLevelItemCount() - 1));
}

void TransactionLogView::onTransactionContextMenu(const QPoint& pos)
{
    transactionMenu(_transactions->itemAt(pos), _transactions->viewport()->mapToGlobal(pos));
}

void TransactionLogView::transactionMenu(QTreeWidgetItem* item, const QPoint& global)
{
    if (!item)
        return;
    QMenu menu(this);
    auto copyScript = menu.addAction(tr("Copy script"));
    copyScript->setEnabled(!item->data(TxnName, Qt::UserRole).toString().isEmpty());
    auto copyRow = menu.addAction(tr("Copy row"));
    // Sec 24.4: undo this row though it is not the last step. Only a row
    // with ops has anything to undo.
    const int64_t seq = item->data(TxnSeq, Qt::UserRole).toLongLong();
    const QString kind = item->text(TxnKind);
    menu.addSeparator();
    auto undoRow = menu.addAction(tr("Undo row %1").arg(seq));
    undoRow->setToolTip(tr("A new transaction applying this row reversed; refused when "
                           "anything since changed what it touched (sec 24.4)"));
    undoRow->setEnabled(_doc
                        && (kind == QLatin1String("user") || kind == QLatin1String("implicit")
                            || kind == QLatin1String("undo") || kind == QLatin1String("redo")));
    auto branchHere = menu.addAction(tr("Branch from here..."));
    branchHere->setToolTip(tr("A new branch whose history ends at this row, switched to (sec 26)"));
    branchHere->setEnabled(_doc != nullptr);
    auto chosen = menu.exec(global);
    if (chosen == branchHere) {
        createBranch(0, seq);
        return;
    }
    if (chosen == undoRow) {
        try {
            if (!_doc->undoLogged(seq))
                _status->setText(tr("Undo of row %1 refused -- the report view says why").arg(seq));
        }
        catch (Base::Exception& e) {
            FC_ERR("undo of row " << seq << ": " << e.what());
        }
        return;
    }
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

void TransactionLogView::onVersionContextMenu(const QPoint& pos)
{
    auto item = _versions->itemAt(pos);
    if (!item || !_doc)
        return;
    const int64_t num = item->data(VerNum, Qt::UserRole).toLongLong();
    const bool named = item->text(VerKind) == QLatin1String("named");
    QMenu menu(this);
    auto restore = menu.addAction(tr("Restore to version %1").arg(num));
    restore->setToolTip(tr("One undoable transaction making the document what this "
                           "version was (sec 24.5)"));
    auto name = menu.addAction(named ? tr("Rename version %1...").arg(num)
                                     : tr("Name version %1...").arg(num));
    name->setToolTip(tr("A named version is never evicted (sec 16.3)"));
    auto unname = named ? menu.addAction(tr("Make version %1 unnamed").arg(num)) : nullptr;
    menu.addSeparator();
    auto branchFrom = menu.addAction(tr("Branch from version %1...").arg(num));
    branchFrom->setToolTip(tr("A new branch from this version, switched to; the version is "
                              "named if it was not (sec 17.1)"));
    const QString branchName = item->text(VerBranch);
    auto trimTo = menu.addAction(tr("Trim %1 to version %2...").arg(branchName).arg(num));
    trimTo->setToolTip(tr("Remove the history of the branch before this version, but what "
                          "another branch holds and the named versions (sec 16.7)"));
    trimTo->setEnabled(!branchName.isEmpty());
    auto chosen = menu.exec(_versions->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == branchFrom) {
        createBranch(num, 0);
        return;
    }
    if (chosen == trimTo) {
        if (QMessageBox::question(this, tr("Trim branch"),
                                  tr("Remove the history of %1 before version %2? "
                                     "This cannot be undone.").arg(branchName).arg(num))
                != QMessageBox::Yes)
            return;
        try {
            _doc->trimBranch(branchName.toStdString(), num);
        }
        catch (Base::Exception& e) {
            FC_ERR("trim " << branchName.toStdString() << ": " << e.what());
        }
        return;
    }
    App::Document* doc = _doc;
    try {
        if (chosen == restore) {
            // A transaction: the panel refreshes on its commit.
            doc->restoreVersion(num);
            return;
        }
        auto l = log();
        if (!l)
            return;
        if (chosen == name) {
            bool ok = false;
            QString text = QInputDialog::getText(this, tr("Name version %1").arg(num),
                                                 tr("Name:"), QLineEdit::Normal,
                                                 item->text(VerName), &ok);
            if (!ok || text.trimmed().isEmpty())
                return;
            l->store().nameVersion(num, text.trimmed().toStdString());
        }
        else if (chosen == unname) {
            l->store().nameVersion(num, std::string());
        }
        _lastVersion = -1;   // rows rebuilt
        refresh();
    }
    catch (Base::Exception& e) {
        FC_ERR("version " << num << ": " << e.what());
    }
}

void TransactionLogView::updateStatus()
{
    if (!_doc) {
        _status->setText(tr("No active document"));
        _resolve->setEnabled(false);
        _snapshot->setEnabled(false);
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
        _snapshot->setEnabled(false);
        return;
    }
    _resolve->setEnabled(true);
    _snapshot->setEnabled(true);
    size_t versions = 0;
    try {
        versions = l->store().versions().size();
    }
    catch (Base::Exception&) {
    }
    const long mode = App::DocumentParams::getTransactionLog();
    QString modeText = mode == 2 ? tr("embedded") : tr("session");
    try {
        App::LogBranch branch;
        if (l->store().getBranch(l->branch(), branch))
            modeText += QStringLiteral(", ") + tr("branch %1").arg(QString::fromStdString(branch.name));
    }
    catch (Base::Exception&) {
    }
    _status->setText(tr("%1 [%7]: %2 transactions, %3 versions, %4 pending, session %5  --  %6")
                         .arg(QString::fromUtf8(_doc->getName()))
                         .arg(_lastSeq)
                         .arg(static_cast<qulonglong>(versions))
                         .arg(static_cast<qulonglong>(l->pendingCount()))
                         .arg(static_cast<qlonglong>(l->session()))
                         .arg(QString::fromStdString(l->path()))
                         .arg(modeText));
}

#include "moc_TransactionLogView.cpp"
