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
# include <QDialog>
# include <QDialogButtonBox>
# include <QDir>
# include <QEventLoop>
# include <QPointer>
# include <QFile>
# include <QFileInfo>
# include <QFontDatabase>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QInputDialog>
# include <QLabel>
# include <QLineEdit>
# include <QLocale>
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
# include <QSlider>
# include <QTreeView>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <map>
#include <set>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObserver.h>
#include <App/DocumentParams.h>
#include <App/FileBlobManager.h>
#include <App/FileHistory.h>
#include <App/TransactionLog.h>
#include <Base/Console.h>
#include <Base/Quantity.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>

#include "TransactionLogView.h"
#include "Application.h"
#include "Document.h"
#include "FileDialog.h"
#include "SceneRequests.h"
#include "Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;
using namespace Gui::DockWnd;

namespace {

enum TxnColumn { TxnGraph, TxnSeq, TxnKind, TxnOrigin, TxnName, TxnAuthor, TxnTime, TxnParent,
                 TxnInverts, TxnMerged, TxnBranch, TxnColumns };

// Item data roles of a transaction row, besides the seq on TxnSeq.
constexpr int RoleParent = Qt::UserRole + 1;   // on TxnParent: the parent seq
constexpr int RoleBranch = Qt::UserRole + 2;   // on TxnBranch: the branch id
constexpr int RoleRecord = Qt::UserRole + 3;   // on TxnKind: true for a row with no ops
constexpr int RoleMerged = Qt::UserRole + 4;   // on TxnMerged: a merge's second parent
enum OpColumn { OpIdx, OpOp, OpContainer, OpProp, OpType, OpBefore, OpAfter, OpDerived, OpColumns };
enum VerColumn { VerNum, VerKind, VerName, VerBranch, VerSeq, VerSchema, VerCreated, VerDocXml,
                 VerEntries, VerColumns };
enum ManColumn { ManEntry, ManSource, ManHash, ManColumns };
enum ChangeColumn { ChgObject, ChgProp, ChgType, ChgWhat, ChgColumns };
enum KeptColumn { KeptWhat, KeptObject, KeptProp, KeptRow, KeptValue, KeptColumns };
enum Tab { TabTransactions, TabVersions, TabKept };

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
        std::vector<Lane> out;      // from this node down to another lane: a merge
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
        for (const auto& l : row->out) {
            painter->setPen(pen(l.branch));
            QPainterPath path(QPointF(x(l.lane), mid));
            path.cubicTo(QPointF(x(l.lane), bottom), QPointF(x(l.to), mid),
                         QPointF(x(l.to), bottom));
            painter->drawPath(path);
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

/** The graph's pane. Its one column is as wide as its lanes and labels
 * and the pane is whatever the splitter gives it, so a row's band stopped
 * where the column did, short of the list it belongs to. The band runs on
 * to the pane's edge: the eye follows a row from its node into the list.
 */
class GraphView: public QTreeView
{
public:
    using QTreeView::QTreeView;

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override
    {
        QTreeView::drawRow(painter, option, index);
        const int from = header()->length() - header()->offset();
        const int width = viewport()->width() - from;
        if (width <= 0)
            return;
        // By the style, as the column's own is: a style sheet's alternate
        // colour and its selection are what the rest of the row has.
        QStyleOptionViewItem opt = option;
        opt.rect = QRect(from, option.rect.y(), width, option.rect.height());
        // Every other row as the view counts them, which is by place: a
        // row hidden above -- a record -- is not counted, so the model's
        // row number is not it. The rows are one height.
        const int height = option.rect.height();
        const int place = height > 0 ? (option.rect.y() + verticalOffset()) / height
                                     : index.row();
        opt.features.setFlag(QStyleOptionViewItem::Alternate,
                             alternatingRowColors() && (place & 1));
        opt.state.setFlag(QStyle::State_Selected,
                          selectionModel() && selectionModel()->isSelected(index));
        style()->drawPrimitive(QStyle::PE_PanelItemViewRow, &opt, painter, this);
    }
};

} // namespace

/** Values at two states, shown in the 3D view (docs/TransactionLog.md sec
 * 32.9, 32.10): which two, where between them, and whether what did not
 * change is left out. Each object's view provider is asked to show its own
 * (ViewProviderDocumentObject::showValueDiff) and says whether it can; what
 * they show is taken away when other values are given, or this goes.
 *
 * One value is shown over its object wherever that is. Of many -- the whole
 * of a row, of a version, of a merge -- those of the objects chosen are
 * shown (sec 32.13): a file holds what has nothing to do with each other,
 * and all of it at once says nothing. The objects are chosen as an
 * attachment's references are: each has a button here, "Add..." takes what
 * is selected or the next thing picked, and an object's own button picks
 * it again. What is taken is no longer selected, so that the highlight of
 * the selection is not drawn over what is shown. They stay chosen from one
 * row or version to the next.
 *
 * A shape is shown one of two ways (sec 32.16), and which is asked of the
 * view provider by name: by its material -- the solid only one of the two
 * states has, which is where it changed -- or by its faces, which of them
 * changed. Anything that is not two solids is shown by its faces.
 */
class Gui::DockWnd::TransactionDiffBar: public QWidget, public Gui::SelectionObserver
{
public:
    /// Two values of a property, each by its ref in the log -- empty for
    /// the one the object has now, NoValue where there was none -- or not
    /// `there`.
    struct Pair
    {
        bool there {false};
        std::string a;
        std::string b;
    };
    /// Property `prop` of object `cid`, and its values for each pair. With
    /// `document`, of the object in that open document and not in the one
    /// the values are of: what a merge would make and the document has not
    /// (sec 32.17). It is shown whatever is chosen -- it cannot be.
    struct Value
    {
        long cid {0};
        std::string prop;
        std::vector<Pair> pairs;
        std::string document;
    };
    /// The ref of a property that was not there: everything of the other
    /// value is new, or gone.
    static const char* NoValue;

    /// `pairs`: what each pair setValues() is given is called.
    TransactionDiffBar(QWidget* parent, const QStringList& pairs)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("TransactionDiffBar"));
        auto rows = new QVBoxLayout(this);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(2);
        auto objects = new QHBoxLayout();
        objects->setContentsMargins(0, 0, 0, 0);
        objects->addWidget(new QLabel(QObject::tr("Objects:"), this));
        _slots = new QHBoxLayout();
        _slots->setContentsMargins(0, 0, 0, 0);
        objects->addLayout(_slots);
        _add = new QPushButton(QObject::tr("Add..."), this);
        _add->setObjectName(QStringLiteral("diffAdd"));
        _add->setCheckable(true);
        _add->setToolTip(QObject::tr("Choose the objects to see the changes of: those selected, "
                                     "or with none selected the next one picked in the tree or "
                                     "the view"));
        objects->addWidget(_add);
        objects->addStretch(1);
        rows->addLayout(objects);
        QObject::connect(_add, &QPushButton::clicked, this, [this]() { pick(Adding); });
        auto layout = new QHBoxLayout();
        layout->setContentsMargins(0, 0, 0, 0);
        rows->addLayout(layout);
        layout->addWidget(new QLabel(QObject::tr("In the 3D view:"), this));
        _pair = new QComboBox(this);
        _pair->setObjectName(QStringLiteral("diffPair"));
        _pair->addItems(pairs);
        _pair->addItem(QObject::tr("nothing"));
        // A pair is named again by setPairLabel(): as wide as what it says
        _pair->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        layout->addWidget(_pair);
        _mode = new QComboBox(this);
        _mode->setObjectName(QStringLiteral("diffMode"));
        _mode->addItem(QObject::tr("Material"), QStringLiteral("material"));
        _mode->addItem(QObject::tr("Faces"), QStringLiteral("faces"));
        _mode->setToolTip(QObject::tr("Material: the solid only one of the two has, and what "
                                      "both have.\nFaces: what became of each face -- as it "
                                      "was, changed, new or gone. It is what is shown of "
                                      "anything that is not two solids."));
        layout->addWidget(_mode);
        _position = new QSlider(Qt::Horizontal, this);
        _position->setObjectName(QStringLiteral("diffPosition"));
        _position->setRange(0, 100);
        _position->setValue(100);
        _position->setFixedWidth(120);
        _position->setToolTip(QObject::tr("From the first of the two to the second"));
        layout->addWidget(_position);
        _only = new QCheckBox(QObject::tr("Differences only"), this);
        _only->setObjectName(QStringLiteral("diffOnly"));
        layout->addWidget(_only);
        _said = new QLabel(this);
        _said->setObjectName(QStringLiteral("diffStatus"));
        layout->addWidget(_said, 1);
        QObject::connect(_pair, qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int index) {
                             // Whoever gives the values may have this pair's
                             // to make first
                             if (_pairChosen)
                                 _pairChosen(index);
                             update();
                         });
        QObject::connect(_mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int) { update(); });
        QObject::connect(_position, &QSlider::valueChanged, this, [this](int) { update(); });
        QObject::connect(_only, &QCheckBox::toggled, this, [this](bool) { update(); });
        // What is picked is taken once the selection has settled: not from
        // inside its own signal, which taking it clears
        _again.setSingleShot(true);
        _again.setInterval(0);
        QObject::connect(&_again, &QTimer::timeout, this, [this]() { take(); });
        // The material of two heavy solids is not waited for (sec 32.16):
        // their view says when it is there, and is asked now and then
        _pending.setSingleShot(true);
        _pending.setInterval(200);
        QObject::connect(&_pending, &QTimer::timeout, this, [this]() {
            for (const auto& shown : _shown) {
                auto view = viewOf(shown);
                if (!view)
                    continue;
                const auto said = view->getValueDiff();
                auto it = said.find("material.pending");
                if (it != said.end() && it->second > 0) {
                    _pending.start();
                    return;
                }
            }
            update();
        });
        update();
    }

    /// The document the objects are chosen of. Those of another go.
    void setDocument(App::Document* doc)
    {
        const std::string name = doc ? doc->getName() : std::string();
        if (name == _home)
            return;
        _home = name;
        _chosen.clear();
        pick(NotPicking);
        showChosen();
    }

    /// Choose what is selected now, if anything is: for where nothing can
    /// be picked later -- a dialog that takes the mouse.
    void chooseSelected()
    {
        if (!selected().empty()) {
            _picking = Adding;
            take();
        }
    }

    ~TransactionDiffBar() override
    {
        hideAll();
    }

    /// What pair `index` is called: the panel's one pair is a row's before
    /// against after, or a version against now.
    void setPairLabel(int index, const QString& text)
    {
        if (_pair->itemText(index) != text)
            _pair->setItemText(index, text);
    }

    /** The values to show, of `doc`. With `chosenOnly`, those of the
     * objects selected and no others, following the selection. `note` is
     * said after what was found: what of the two states has no object here
     * to be shown over.
     */
    void setValues(App::Document* doc, const std::vector<Value>& values, bool chosenOnly,
                   const QString& note = QString())
    {
        _values.clear();
        if (doc)
            setDocument(doc);
        _chosenOnly = chosenOnly;
        for (const auto& value : values) {
            App::Document* of = value.document.empty()
                                    ? doc
                                    : App::GetApplication().getDocument(value.document.c_str());
            auto obj = of ? of->getObjectByID(value.cid) : nullptr;
            if (!obj)
                continue;
            _values.push_back({App::DocumentObjectT(obj), value.prop, value.pairs,
                               !value.document.empty()});
        }
        _note = note;
        update();
    }

    /// One value, shown over its object wherever that is
    void setValue(App::Document* doc, long cid, const std::string& prop, std::vector<Pair> pairs)
    {
        setValues(doc, {{cid, prop, std::move(pairs), std::string()}}, false);
    }

    /// The pair chosen now, by its place
    int pair() const
    {
        return _pair->currentIndex();
    }

    /// Called when another pair is chosen, before its values are shown: for
    /// a pair whose values are made only when asked for (sec 32.17).
    void whenPairChosen(std::function<void(int)> fn)
    {
        _pairChosen = std::move(fn);
    }

    /// What is said after what was found while pair `index` is chosen, in
    /// place of the note setValues() was given.
    void setPairNote(int index, const QString& note)
    {
        _pairNotes[index] = note;
        update();
    }

    void clear()
    {
        setValues(nullptr, {}, false);
    }

private:
    struct Held
    {
        App::DocumentObjectT object;
        std::string prop;
        std::vector<Pair> pairs;
        bool elsewhere {false};   ///< of another document's object
    };

    static ViewProviderDocumentObject* viewOf(const App::DocumentObjectT& which)
    {
        auto obj = which.getObject();
        return obj ? Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                         Application::Instance->getViewProvider(obj))
                   : nullptr;
    }

    void hideAll()
    {
        for (const auto& shown : _shown) {
            if (auto view = viewOf(shown))
                view->hideValueDiff();
        }
        _shown.clear();
    }

    enum Picking { NotPicking = -2, Adding = -1 };   // or the place picked again

    std::vector<App::DocumentObject*> selected() const
    {
        if (_home.empty())
            return {};
        return Selection().getObjectsOfType(App::DocumentObject::getClassTypeId(), _home.c_str());
    }

    void onSelectionChanged(const SelectionChanges& msg) override
    {
        if (_picking == NotPicking)
            return;
        if (msg.Type == SelectionChanges::AddSelection
                || msg.Type == SelectionChanges::SetSelection)
            _again.start();
    }

    /// Ask for the objects to add, or for the one at `place` again: taken
    /// now where something is selected, else when something is. Asked for
    /// what is being asked already, it is no longer asked.
    void pick(int place)
    {
        _picking = place == _picking ? NotPicking : place;
        if (_picking != NotPicking && !selected().empty()) {
            take();
            return;
        }
        showPicking();
        update();
    }

    void take()
    {
        const auto objects = selected();
        if (_picking == NotPicking || objects.empty())
            return;
        auto has = [this](App::DocumentObject* obj) {
            for (const auto& chosen : _chosen) {
                if (chosen.getObject() == obj)
                    return true;
            }
            return false;
        };
        if (_picking >= 0 && _picking < static_cast<int>(_chosen.size())) {
            if (!has(objects.front()))
                _chosen[_picking] = App::DocumentObjectT(objects.front());
        }
        else {
            for (auto obj : objects) {
                if (!has(obj))
                    _chosen.emplace_back(obj);
            }
        }
        _picking = NotPicking;
        // Not left selected: its highlight would be drawn over what is shown
        Selection().clearSelection(_home.c_str());
        showChosen();
        update();
    }

    /// A button for each object chosen, picked again by it, and one to let
    /// it go.
    void showChosen()
    {
        while (auto item = _slots->takeAt(0)) {
            if (auto widget = item->widget()) {
                // It may be the one whose click this is: gone from sight
                // and from being found now, deleted when that returns
                widget->hide();
                widget->setObjectName(QString());
                widget->deleteLater();
            }
            delete item;
        }
        _buttons.clear();
        _chosen.erase(std::remove_if(_chosen.begin(), _chosen.end(),
                                     [](const App::DocumentObjectT& which) {
                                         return !which.getObject();
                                     }),
                      _chosen.end());
        for (int i = 0; i < static_cast<int>(_chosen.size()); ++i) {
            auto obj = _chosen[i].getObject();
            auto again = new QPushButton(QString::fromUtf8(obj->Label.getValue()), this);
            again->setObjectName(QStringLiteral("diffObject%1").arg(i));
            again->setCheckable(true);
            again->setToolTip(QObject::tr("%1: click to choose another in its place")
                                  .arg(QString::fromUtf8(obj->getNameInDocument())));
            QObject::connect(again, &QPushButton::clicked, this, [this, i]() { pick(i); });
            _slots->addWidget(again);
            _buttons.push_back(again);
            auto gone = new QPushButton(QStringLiteral("x"), this);
            gone->setObjectName(QStringLiteral("diffRemove%1").arg(i));
            gone->setFlat(true);
            gone->setFixedWidth(gone->fontMetrics().horizontalAdvance(QLatin1Char('x')) * 3);
            gone->setToolTip(QObject::tr("No longer chosen"));
            QObject::connect(gone, &QPushButton::clicked, this, [this, i]() {
                if (i < static_cast<int>(_chosen.size()))
                    _chosen.erase(_chosen.begin() + i);
                _picking = NotPicking;
                showChosen();
                update();
            });
            _slots->addWidget(gone);
        }
        showPicking();
    }

    void showPicking()
    {
        _add->setChecked(_picking == Adding);
        for (int i = 0; i < static_cast<int>(_buttons.size()); ++i)
            _buttons[i]->setChecked(_picking == i);
    }

    void update()
    {
        const int index = _pair->currentIndex();
        std::vector<App::DocumentObjectT> shown;
        std::set<ViewProviderDocumentObject*> views;
        std::map<std::string, double> found;
        const std::string mode = _mode->currentData().toString().toStdString();
        std::set<App::DocumentObject*> chosen;
        for (const auto& which : _chosen) {
            if (auto obj = which.getObject())
                chosen.insert(obj);
        }
        if (chosen.size() != _chosen.size())
            showChosen();   // one of them is gone
        std::set<App::DocumentObject*> changed;
        for (const auto& value : _values) {
            if (index < 0 || index >= static_cast<int>(value.pairs.size())
                    || !value.pairs[index].there)
                continue;
            auto obj = value.object.getObject();
            if (!obj)
                continue;
            if (!value.elsewhere)
                changed.insert(obj);
            if (_chosenOnly && !value.elsewhere && !chosen.count(obj))
                continue;
            auto view = viewOf(value.object);
            // One thing of an object at a time: its first value that shows
            if (!view || views.count(view))
                continue;
            const Pair& pair = value.pairs[index];
            if (!view->showValueDiff(value.prop.c_str(), pair.a, pair.b,
                                     _position->value() / 100.f, _only->isChecked(), mode))
                continue;
            views.insert(view);
            shown.push_back(value.object);
            for (const auto& count : view->getValueDiff())
                found[count.first] += count.second;
        }
        for (const auto& was : _shown) {
            auto view = viewOf(was);
            if (view && !views.count(view))
                view->hideValueDiff();
        }
        _shown = std::move(shown);

        QString text;
        if (!_shown.empty()) {
            // Each object says which it is shown by: the material is of two
            // solids, and of anything else it is the faces
            const int solids = static_cast<int>(found["material"]);
            QStringList parts;
            if (solids > 0) {
                // The three in one unit, that of what changed: the unit a
                // volume is said in goes by its size, and three of them
                // each in its own are not compared at a glance
                double factor = 1.;
                std::string unit;
                const double changed = std::max(found["volume.added"], found["volume.removed"]);
                Base::Quantity(changed > 0. ? changed : found["volume.common"],
                               Base::Unit::Volume).getUserString(factor, unit);
                auto volume = [&](double value) {
                    return QStringLiteral("%1 %2").arg(
                        QLocale().toString(value / (factor != 0. ? factor : 1.), 'f',
                                           Base::UnitsApi::getDecimals()),
                        QString::fromStdString(unit));
                };
                parts << QObject::tr("%1 added, %2 removed, %3 in both")
                             .arg(volume(found["volume.added"]), volume(found["volume.removed"]),
                                  volume(found["volume.common"]));
            }
            if (solids < static_cast<int>(_shown.size())) {
                QString faces = QObject::tr("%1 as they were, %2 changed, %3 new, %4 gone")
                                    .arg(static_cast<int>(found["same"]))
                                    .arg(static_cast<int>(found["changed"] + found["moved"]))
                                    .arg(static_cast<int>(found["added"]))
                                    .arg(static_cast<int>(found["removed"]));
                if (found["material.pending"] > 0)
                    faces = QObject::tr("working out the material; the faces meanwhile: %1")
                                .arg(faces);
                else if (found["material.none"] > 0)
                    faces = QObject::tr("not two solids, by the faces: %1").arg(faces);
                parts << faces;
            }
            text = parts.join(QStringLiteral("; "));
            if (_shown.size() > 1)
                text = QObject::tr("%1 objects: %2").arg(_shown.size()).arg(text);
        }
        else if (_chosenOnly && !changed.empty()) {
            // Said of what the pair has, not of what a view could show of it
            const int count = static_cast<int>(changed.size());
            text = chosen.empty()
                       ? QObject::tr("%n object(s) with changes: add one to see it", "", count)
                       : QObject::tr("No shape of what is chosen changed; %n object(s) have "
                                     "changes", "", count);
        }
        if (found["material.pending"] > 0)
            _pending.start();
        else
            _pending.stop();
        if (_picking != NotPicking)
            text = QObject::tr("Pick an object in the tree or the view");
        auto own = _pairNotes.find(index);
        const QString note = own != _pairNotes.end() ? own->second : _note;
        if (!note.isEmpty() && index >= 0 && index < _pair->count() - 1)
            text += (text.isEmpty() ? QString() : QStringLiteral("; ")) + note;
        _said->setText(text);
    }

    QComboBox* _pair {nullptr};
    QComboBox* _mode {nullptr};
    QSlider* _position {nullptr};
    QCheckBox* _only {nullptr};
    QLabel* _said {nullptr};
    std::vector<Held> _values;
    /// The document the objects are chosen of, and those chosen
    std::string _home;
    std::vector<App::DocumentObjectT> _chosen;
    int _picking {NotPicking};
    QHBoxLayout* _slots {nullptr};
    QPushButton* _add {nullptr};
    std::vector<QPushButton*> _buttons;
    bool _chosenOnly {false};
    QTimer _again;
    QTimer _pending;
    QString _note;
    std::map<int, QString> _pairNotes;
    std::function<void(int)> _pairChosen;
    std::vector<App::DocumentObjectT> _shown;
};

const char* TransactionDiffBar::NoValue = "-";

/// The whole of the row or the version selected: every value of it, what
/// the two are called, and what of it has no object here.
struct TransactionLogView::Whole
{
    QString label;
    std::vector<TransactionDiffBar::Value> values;
    QString note;

    void clear()
    {
        label.clear();
        values.clear();
        note.clear();
    }
};

TransactionLogView::TransactionLogView(Gui::Document* pcDocument, QWidget* parent)
    : DockWindow(pcDocument, parent)
    , _graph(std::make_unique<GraphLayout>())
{
    setWindowTitle(tr("Transaction log"));
    _whole = std::make_unique<Whole>();

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
    _mergeBranch = new QPushButton(tr("Merge..."), this);
    _mergeBranch->setToolTip(tr("Merge another branch into this one: what it changed and this "
                                "one did not, as one undoable step (sec 28)"));
    branchBar->addWidget(_mergeBranch);
    _importFile = new QPushButton(tr("Merge from file..."), this);
    _importFile->setToolTip(tr("Bring another copy of this file in: what it has done since the "
                               "two parted comes as a branch named after it, each row under "
                               "its author, and is then merged like any branch (sec 30.13)"));
    branchBar->addWidget(_importFile);
    _openBranch = new QPushButton(tr("Branch to document"), this);
    _openBranch->setToolTip(tr("Make a branch here and open it in another document of this "
                               "file; this one stays where it is, and either is merged into "
                               "the other with Merge... (sec 30)"));
    branchBar->addWidget(_openBranch);
    _allBranches = new QCheckBox(tr("All branches"), this);
    _allBranches->setToolTip(tr("Show the rows of every branch, not only this branch's history"));
    branchBar->addWidget(_allBranches);
    _hideRecords = new QCheckBox(tr("Hide records"), this);
    _hideRecords->setToolTip(tr("Hide the rows that changed nothing: recompute records, "
                                "snapshots, saves, switches"));
    branchBar->addWidget(_hideRecords);
    _showLogins = new QCheckBox(tr("Logins"), this);
    _showLogins->setToolTip(tr("Show the rows that record a connection being admitted to a "
                               "shared session (sec 30.6)"));
    branchBar->addWidget(_showLogins);
    branchBar->addStretch(1);
    layout->addLayout(branchBar);

    _status = new QLabel(this);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_status);

    // The requests (sec 30.20 H2): what someone else has done to a copy of
    // this file and offers to it. Shown only while there is one.
    _requests = new QTreeWidget(this);
    _requests->setObjectName(QStringLiteral("TransactionRequests"));
    _requests->setRootIsDecorated(false);
    _requests->setUniformRowHeights(true);
    _requests->setHeaderLabels(
        {tr("Request"), tr("From"), tr("When"), tr("What"), tr("Conflicts")});
    _requests->setToolTip(tr("Changes offered to this file: a file a client of the shared "
                             "session sent, which is kept and not read until it is brought "
                             "in here, and every branch brought in from a file that this "
                             "branch has not merged. Double-click to bring in and merge; "
                             "the menu deletes one or opens it in a document of its own"));
    _requests->setContextMenuPolicy(Qt::CustomContextMenu);
    _requests->setMaximumHeight(120);
    _requests->hide();
    layout->addWidget(_requests);

    auto splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter, 1);
    // The op selected, before against after, in the 3D view (sec 32.9)
    _diffBar = new TransactionDiffBar(this, {tr("before against after")});
    layout->addWidget(_diffBar);

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
    _graphView = new GraphView(txnSplitter);
    _transactions = new QTreeWidget(txnSplitter);
    _transactions->setColumnCount(TxnColumns);
    _transactions->setHeaderLabels({tr("Graph"), tr("Seq"), tr("Kind"), tr("Origin"), tr("Name"),
                                    tr("Author"), tr("Time"), tr("Parent"), tr("Inverts"),
                                    tr("Merged"), tr("Branch")});
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
    // Several rows may be picked to be applied together (sec 31.12).
    _transactions->setSelectionMode(QAbstractItemView::ExtendedSelection);
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

    // What a save keeps of the values a recompute wrote (sec 32.12)
    _kept = new QTreeWidget(_tabs);
    _kept->setObjectName(QStringLiteral("TransactionKept"));
    _tabs->addTab(_kept, tr("Kept"));
    _tabs->setTabToolTip(TabKept, tr("The values a recompute wrote -- shapes -- that a save "
                                     "writes into the file though no kept version reaches "
                                     "them: each one kept by itself, and every one of an "
                                     "object kept (sec 32.11)"));
    _kept->setColumnCount(KeptColumns);
    _kept->setHeaderLabels({tr("Kept"), tr("Object"), tr("Property"), tr("Row"), tr("Value")});
    _kept->setAlternatingRowColors(true);
    _kept->setUniformRowHeights(true);
    _kept->setSelectionMode(QAbstractItemView::SingleSelection);
    _kept->setContextMenuPolicy(Qt::CustomContextMenu);
    _kept->header()->setStretchLastSection(false);
    _kept->header()->setSectionResizeMode(KeptObject, QHeaderView::Stretch);

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
    _ops->setContextMenuPolicy(Qt::CustomContextMenu);
    _ops->header()->setStretchLastSection(false);
    _ops->header()->setSectionResizeMode(OpContainer, QHeaderView::Stretch);

    // A version's detail: what is not now as it was there, by object -- the
    // whole of it is this list, and the view shows the objects chosen
    // (sec 32.13) -- and the entries of its manifest.
    auto versionDetail = new QTabWidget(_detail);
    versionDetail->setDocumentMode(true);
    _detail->addWidget(versionDetail);
    _changes = new QTreeWidget(versionDetail);
    _changes->setObjectName(QStringLiteral("TransactionVersionChanges"));
    versionDetail->addTab(_changes, tr("Changed since"));
    _changes->setColumnCount(ChgColumns);
    _changes->setHeaderLabels({tr("Object"), tr("Property"), tr("Type"), tr("Since the version")});
    _changes->setRootIsDecorated(false);
    _changes->setAlternatingRowColors(true);
    _changes->setUniformRowHeights(true);
    _changes->setSelectionMode(QAbstractItemView::SingleSelection);
    _changes->header()->setStretchLastSection(false);
    _changes->header()->setSectionResizeMode(ChgObject, QHeaderView::Stretch);

    _manifest = new QTreeWidget(versionDetail);
    versionDetail->addTab(_manifest, tr("Entries"));
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
    connect(_ops, &QTreeWidget::customContextMenuRequested,
            this, &TransactionLogView::onOpContextMenu);
    connect(_versions, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onVersionSelected);
    connect(_versions, &QTreeWidget::customContextMenuRequested,
            this, &TransactionLogView::onVersionContextMenu);
    connect(_manifest, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onManifestSelected);
    connect(_changes, &QTreeWidget::itemSelectionChanged,
            this, &TransactionLogView::onChangeSelected);
    connect(_kept, &QTreeWidget::itemSelectionChanged, this, &TransactionLogView::onKeptSelected);
    connect(_kept, &QTreeWidget::customContextMenuRequested,
            this, &TransactionLogView::onKeptContextMenu);
    connect(_tabs, &QTabWidget::currentChanged, this, &TransactionLogView::onTabChanged);
    connect(_filter, &QLineEdit::textChanged, this, &TransactionLogView::onFilterChanged);
    connect(_resolve, &QPushButton::clicked, this, &TransactionLogView::onResolvePending);
    connect(_snapshot, &QPushButton::clicked, this, &TransactionLogView::onSnapshot);
    connect(_branch, qOverload<int>(&QComboBox::activated), this,
            &TransactionLogView::onBranchChosen);
    connect(_newBranch, &QPushButton::clicked, this, &TransactionLogView::onNewBranch);
    connect(_deleteBranch, &QPushButton::clicked, this, &TransactionLogView::onDeleteBranch);
    connect(_renameBranch, &QPushButton::clicked, this, &TransactionLogView::onRenameBranch);
    connect(_mergeBranch, &QPushButton::clicked, this, &TransactionLogView::onMergeBranch);
    connect(_importFile, &QPushButton::clicked, this, &TransactionLogView::onImportFile);
    connect(_openBranch, &QPushButton::clicked, this, &TransactionLogView::onOpenBranch);
    connect(_requests, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) { onRequestActivated(item); });
    connect(_requests, &QTreeWidget::customContextMenuRequested, this,
            &TransactionLogView::onRequestContextMenu);
    // The preview reads values: asked once things have settled, not at
    // every commit.
    _previewTimer.setSingleShot(true);
    _previewTimer.setInterval(800);
    connect(&_previewTimer, &QTimer::timeout, this, &TransactionLogView::previewRequests);
    connect(_allBranches, &QCheckBox::toggled, this, &TransactionLogView::applyVisibility);
    connect(_hideRecords, &QCheckBox::toggled, this, &TransactionLogView::applyVisibility);
    connect(_showLogins, &QCheckBox::toggled, this, &TransactionLogView::applyVisibility);

    //NOLINTBEGIN
    _connRequests = SceneRequests::signalChanged().connect([this]() { scheduleRefresh(); });
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
    _diffBar->setDocument(_doc);
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
    // A merge being looked at is of the document that leaves
    if (_mergeDialog)
        _mergeDialog->reject();
    _doc = nullptr;
    _diffBar->setDocument(nullptr);
    _previewed.clear();
    _previewTimer.stop();
    _requests->clear();
    _requests->hide();
    _lastSeq = 0;
    _lastVersion = 0;
    _transactions->clear();
    _ops->clear();
    _versions->clear();
    _manifest->clear();
    _changes->clear();
    _whole->clear();
    _kept->clear();
    _keptSaid.clear();
    _tabs->setTabText(TabKept, tr("Kept"));
    _value->clear();
    _diffBar->clear();
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
    _diffBar->clear();
    DockWindow::hideEvent(ev);
}

void TransactionLogView::reload()
{
    _transactions->clear();
    _ops->clear();
    _versions->clear();
    _manifest->clear();
    _changes->clear();
    _whole->clear();
    _kept->clear();
    _keptSaid.clear();
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
        refreshKept();
        refreshBranches();
        refreshRequests();
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
    _changes->clear();
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
    // The kept have no detail of their own: the value pane says it all
    _detail->setVisible(index != TabKept);
    _detail->setCurrentIndex(index == TabVersions ? 1 : 0);
    _value->clear();
    if (index == TabTransactions) {
        onTransactionSelected();
    }
    else if (index == TabVersions) {
        onVersionSelected();
    }
    else {
        // Asked of the store again: what Python kept, no signal said
        refreshKept();
        onKeptSelected();
    }
}

void TransactionLogView::refreshKept()
{
    // Each value kept by itself, and under each object kept the values the
    // log has of it (sec 32.12). Few rows: rebuilt whole, and only when
    // what they say has moved, so that what is selected stays so.
    auto l = log();
    if (!l || !_doc)
        return;
    struct Line
    {
        bool under {false};   // a value of the object kept above it
        QString what;
        long cid {0};
        QString prop;
        int64_t seq {0};      // the row it is of
        bool rowThere {false};
        std::string ref;      // none: an object kept
        QString value;
    };
    std::vector<Line> lines;
    int kept = 0;
    try {
        auto& store = l->store();
        auto rowThere = [&store](int64_t seq) {
            const auto rows = store.transactions(seq, 1);
            return !rows.empty() && rows.front().seq == seq;
        };
        for (const auto& value : store.keptValues()) {
            lines.push_back({false, tr("one value"), value.cid, QString::fromStdString(value.prop),
                             value.seq, rowThere(value.seq), value.hash, shortRef(value.hash)});
            ++kept;
        }
        for (long cid : store.keptObjects()) {
            std::vector<Line> values;
            for (const auto& prop : store.derivedPropsOf(cid)) {
                for (const auto& set : store.valuesOf(cid, prop)) {
                    if (set.second.empty() || !store.hasEntity(set.second))
                        continue;
                    values.push_back({true, QString(), cid, QString::fromStdString(prop),
                                      set.first, true, set.second, shortRef(set.second)});
                }
            }
            lines.push_back({false, tr("every value"), cid, QString(), 0, false, std::string(),
                             tr("%n value(s)", "", static_cast<int>(values.size()))});
            lines.insert(lines.end(), values.begin(), values.end());
            ++kept;
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
        return;
    }
    auto objectText = [this](long cid) {
        auto obj = _doc->getObjectByID(cid);
        return obj && obj->getNameInDocument()
                   ? QStringLiteral("obj:%1 %2").arg(cid).arg(
                         QString::fromUtf8(obj->getNameInDocument()))
                   : tr("obj:%1, not in the document").arg(cid);
    };
    QString said;
    for (const auto& line : lines) {
        said += QStringLiteral("%1|%2|%3|%4|%5|%6\n")
                    .arg(line.under ? 1 : 0).arg(objectText(line.cid), line.prop)
                    .arg(line.seq).arg(line.rowThere ? 1 : 0)
                    .arg(QString::fromStdString(line.ref) + line.value);
    }
    if (said == _keptSaid && (_kept->topLevelItemCount() > 0) == !lines.empty())
        return;
    _keptSaid = said;

    QString selected;
    qlonglong selectedSeq = 0;
    bool wasSelected = false;
    if (auto items = _kept->selectedItems(); !items.isEmpty()) {
        wasSelected = true;
        selected = items.front()->data(KeptWhat, Qt::UserRole).toString()
                   + QLatin1Char('@') + items.front()->data(KeptObject, Qt::UserRole).toString();
        selectedSeq = items.front()->data(KeptRow, Qt::UserRole).toLongLong();
    }
    const QSignalBlocker blocker(_kept);
    _kept->clear();
    QTreeWidgetItem* object = nullptr;
    QTreeWidgetItem* again = nullptr;
    for (const auto& line : lines) {
        auto item = line.under && object ? new QTreeWidgetItem(object)
                                         : new QTreeWidgetItem(_kept);
        if (!line.under)
            object = line.ref.empty() ? item : nullptr;
        item->setText(KeptWhat, line.what);
        item->setText(KeptObject, objectText(line.cid));
        item->setText(KeptProp, line.prop);
        if (line.seq)
            item->setText(KeptRow, line.rowThere ? QString::number(line.seq)
                                                 : tr("%1, gone").arg(line.seq));
        item->setText(KeptValue, line.value);
        if (!line.ref.empty())
            item->setToolTip(KeptValue, QString::fromStdString(line.ref));
        if (line.seq && !line.rowThere)
            item->setToolTip(KeptRow, tr("The row it was kept at is no longer in the log; "
                                         "the value is"));
        item->setData(KeptWhat, Qt::UserRole, QString::fromStdString(line.ref));
        item->setData(KeptObject, Qt::UserRole, static_cast<qlonglong>(line.cid));
        item->setData(KeptRow, Qt::UserRole,
                      static_cast<qlonglong>(line.rowThere ? line.seq : 0));
        item->setTextAlignment(KeptRow, Qt::AlignRight | Qt::AlignVCenter);
        if (wasSelected && !again && selectedSeq == item->data(KeptRow, Qt::UserRole).toLongLong()
                && selected == item->data(KeptWhat, Qt::UserRole).toString() + QLatin1Char('@')
                                   + item->data(KeptObject, Qt::UserRole).toString())
            again = item;
    }
    _kept->expandAll();
    for (int c = 0; c < KeptColumns; ++c) {
        if (c != KeptObject)
            _kept->resizeColumnToContents(c);
    }
    _tabs->setTabText(TabKept, kept ? tr("Kept (%1)").arg(kept) : tr("Kept"));
    if (again)
        _kept->setCurrentItem(again);
    else if (wasSelected && _tabs->currentIndex() == TabKept)
        QTimer::singleShot(0, this, &TransactionLogView::onKeptSelected);
}

void TransactionLogView::keptChanged()
{
    refreshKept();
    onTabChanged(_tabs->currentIndex());
}

void TransactionLogView::onKeptSelected()
{
    _value->clear();
    _diffBar->clear();
    auto items = _kept->selectedItems();
    if (items.isEmpty() || !_doc || _tabs->currentIndex() != TabKept)
        return;
    auto item = items.front();
    const QString ref = item->data(KeptWhat, Qt::UserRole).toString();
    if (ref.isEmpty()) {
        _value->setPlainText(tr("Every value a recompute writes of this object is written into "
                                "the file by a save: those the log has now are listed under "
                                "it, and each one to come is kept as it is made.\n\nThey are "
                                "kept by the rows that made them: a row trimmed or squashed "
                                "takes its value with it. Keep one by itself to hold it past "
                                "that."));
        return;
    }
    // The value kept, over the object as it is now (sec 32.9)
    const long cid = static_cast<long>(item->data(KeptObject, Qt::UserRole).toLongLong());
    _diffBar->setPairLabel(0, tr("the kept value against now"));
    _diffBar->setValue(_doc, cid, item->text(KeptProp).toStdString(),
                       {{true, ref.toStdString(), std::string()}});
    _value->setPlainText(valueText(tr("kept"), ref));
}

void TransactionLogView::onKeptContextMenu(const QPoint& pos)
{
    auto item = _kept->itemAt(pos);
    auto l = log();
    if (!item || !_doc || !l)
        return;
    const QString ref = item->data(KeptWhat, Qt::UserRole).toString();
    const qlonglong cid = item->data(KeptObject, Qt::UserRole).toLongLong();
    const qlonglong seq = item->data(KeptRow, Qt::UserRole).toLongLong();
    const QString prop = item->text(KeptProp);
    QMenu menu(this);
    QAction* letGo = nullptr;
    QAction* stop = nullptr;
    QAction* alone = nullptr;
    QAction* row = nullptr;
    bool keptAlone = false;
    if (ref.isEmpty()) {
        stop = menu.addAction(tr("Stop keeping every value of %1").arg(item->text(KeptObject)));
        stop->setToolTip(tr("Those kept by themselves stay kept"));
    }
    else if (!item->parent()) {
        letGo = menu.addAction(tr("Let go of this value"));
        letGo->setToolTip(tr("A save then leaves it out of the file, unless a kept version "
                             "reaches it or its object is kept"));
    }
    else {
        try {
            for (const auto& kept : l->store().keptValues())
                keptAlone = keptAlone || kept.hash == ref.toStdString();
        }
        catch (Base::Exception&) {
        }
        alone = menu.addAction(tr("Keep this value by itself"));
        alone->setCheckable(true);
        alone->setChecked(keptAlone);
        alone->setToolTip(tr("It then stays when its row goes, in a trim or a squash, and "
                             "when the object is no longer kept"));
    }
    if (seq)
        row = menu.addAction(tr("Show row %1").arg(seq));
    auto chosen = menu.exec(_kept->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == letGo) {
        releaseValue(ref);
    }
    else if (chosen == stop) {
        keepObject(cid, false);
    }
    else if (chosen == alone) {
        keepValue(seq, cid, prop, !keptAlone);
    }
    else if (chosen == row) {
        for (int i = 0; i < _transactions->topLevelItemCount(); ++i) {
            auto found = _transactions->topLevelItem(i);
            if (found->data(TxnSeq, Qt::UserRole).toLongLong() != seq)
                continue;
            _tabs->setCurrentIndex(TabTransactions);
            _transactions->setCurrentItem(found);
            _transactions->scrollToItem(found);
            break;
        }
    }
}

QString TransactionLogView::valueText(const QString& label, const QString& ref) const
{
    QString text;
    if (ref.isEmpty())
        return text;
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
    return text;
}

void TransactionLogView::onVersionSelected()
{
    _manifest->clear();
    _changes->clear();
    _whole->clear();
    _value->clear();
    _diffBar->clear();
    auto items = _versions->selectedItems();
    if (items.isEmpty())
        return;
    showManifest(items.front()->data(VerNum, Qt::UserRole).toLongLong());
    showVersionDiff(items.front()->text(VerSeq).toLongLong());
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
    // The author of a row is its session's user (sec 30.6).
    std::map<int64_t, App::LogUser> users;
    for (auto& u : l->store().users())
        users[u.id] = u;
    std::map<int64_t, App::LogSession> sessions;
    for (auto& s : l->store().sessions())
        sessions[s.id] = s;
    for (const auto& t : l->store().transactions(fromSeq, 0)) {
        // Newest first, as git lists history: each new row goes on top.
        auto item = new QTreeWidgetItem();
        _transactions->insertTopLevelItem(0, item);
        item->setText(TxnSeq, QString::number(t.seq));
        item->setData(TxnSeq, Qt::UserRole, QVariant::fromValue(static_cast<qlonglong>(t.seq)));
        item->setText(TxnKind, QString::fromStdString(t.kind));
        item->setText(TxnOrigin, QString::fromStdString(t.origin));
        item->setText(TxnName, QString::fromStdString(t.name));
        {
            // A name a login verified, or the desktop's, stands alone; one
            // nothing verified says so beside it.
            const App::LogSession& session = sessions[t.session];
            const App::LogUser& author = users[session.user];
            QString name = QString::fromStdString(author.name);
            if (author.kind == "invited" || author.kind == "declared"
                    || author.kind == "enrolled")
                name += QStringLiteral(" (%1)").arg(QString::fromStdString(author.kind));
            item->setText(TxnAuthor, name);
            QString tip = tr("%1 user, session %2")
                              .arg(QString::fromStdString(author.kind))
                              .arg(session.id);
            if (!session.access.empty())
                tip += tr(", admitted with %1 access").arg(QString::fromStdString(session.access));
            item->setToolTip(TxnAuthor, tip);
        }
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
        // A merge's second parent (sec 28.2): the head merged in.
        item->setData(TxnMerged, RoleMerged,
                      QVariant::fromValue(static_cast<qlonglong>(t.mergeFrom)));
        if (t.mergeFrom > 0) {
            item->setText(TxnMerged, QString::number(t.mergeFrom));
            item->setTextAlignment(TxnMerged, Qt::AlignRight | Qt::AlignVCenter);
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
    _whole->clear();
    _value->clear();
    _diffBar->clear();
    auto items = _transactions->selectedItems();
    if (items.isEmpty())
        return;
    auto item = items.front();
    int64_t seq = item->data(TxnSeq, Qt::UserRole).toLongLong();
    showOps(seq);
    if (items.size() == 1)
        showRowDiff(seq);
    // A transaction with no ops -- the recompute record -- carries its
    // payload in the script column; show it where a value would go.
    if (_ops->topLevelItemCount() == 0)
        _value->setPlainText(item->data(TxnName, Qt::UserRole).toString());
}

void TransactionLogView::showRowDiff(int64_t seq)
{
    // What the row made of every value it set (sec 32.10), of which the
    // view shows the objects chosen (sec 32.13): the first value before and
    // the last after. A value with none before was made by the row -- its
    // object, or its property.
    auto l = log();
    if (!l || !_doc)
        return;
    try {
        std::vector<TransactionDiffBar::Value> values;
        std::map<std::pair<long, std::string>, size_t> at;
        std::set<long> absent;
        for (const auto& op : l->store().ops(seq)) {
            if (op.ckind != "obj" || op.op != "set" || op.vafter.empty())
                continue;
            if (!_doc->getObjectByID(op.cid))
                absent.insert(op.cid);
            auto found = at.find({op.cid, op.prop});
            if (found == at.end()) {
                at[{op.cid, op.prop}] = values.size();
                values.push_back({op.cid, op.prop,
                                  {{true, op.vbefore.empty() ? TransactionDiffBar::NoValue
                                                             : op.vbefore, op.vafter}}});
            }
            else {
                values[found->second].pairs[0].b = op.vafter;
            }
        }
        *_whole = Whole {tr("before against after"), std::move(values),
                  absent.empty() ? QString()
                                 : tr("%n object(s) of the row are not in the document", "",
                                      static_cast<int>(absent.size()))};
        showWhole();
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view: " << e.what());
    }
}

void TransactionLogView::showWhole()
{
    // The whole of what is selected in the list above: of it the view shows
    // the objects chosen
    if (_whole->label.isEmpty()) {
        _diffBar->clear();
        return;
    }
    _diffBar->setPairLabel(0, _whole->label);
    _diffBar->setValues(_doc, _whole->values, true, _whole->note);
}

void TransactionLogView::showVersionDiff(int64_t seq)
{
    // The version against the document as it is (sec 32.10): every value
    // that is not now what it was at the version's row. The list says them
    // all; the view shows those of the objects chosen (sec 32.13).
    _diffBar->clear();
    if (!_doc || seq <= 0)
        return;
    try {
        std::vector<TransactionDiffBar::Value> values;
        int gone = 0;
        const QSignalBlocker blocker(_changes);
        for (const auto& c : _doc->compareStates(seq, 0)) {
            auto item = new QTreeWidgetItem(_changes);
            QString whose = QStringLiteral("doc");
            if (c.ckind != "doc") {
                whose = QStringLiteral("%1:%2").arg(QString::fromStdString(c.ckind)).arg(c.cid);
                if (!c.object.empty())
                    whose += QStringLiteral(" %1").arg(QString::fromStdString(c.object));
            }
            item->setText(ChgObject, whose);
            item->setText(ChgProp, QString::fromStdString(c.prop));
            item->setText(ChgType, QString::fromStdString(c.ptype));
            item->setText(ChgWhat, c.prop.empty() ? (c.inA ? tr("removed") : tr("made"))
                                   : c.a.empty() ? (c.derived ? tr("new, by a recompute")
                                                              : tr("new"))
                                   : c.b.empty() ? tr("gone")
                                   : c.derived ? tr("changed, by a recompute") : tr("changed"));
            item->setData(ChgObject, Qt::UserRole,
                          c.ckind == "obj" ? static_cast<qlonglong>(c.cid) : 0);
            item->setData(ChgProp, Qt::UserRole, QString::fromStdString(c.a));
            item->setData(ChgWhat, Qt::UserRole, QString::fromStdString(c.b));
            item->setData(ChgType, Qt::UserRole, c.inA && c.inB);
            if (c.ckind != "obj")
                continue;
            if (c.prop.empty()) {
                gone += c.inA && !c.inB ? 1 : 0;
                continue;
            }
            if (!c.inB || (c.a.empty() && c.inA))
                continue;
            // Now is what the object has: the empty ref
            values.push_back({c.cid, c.prop,
                              {{true, c.a.empty() ? TransactionDiffBar::NoValue : c.a,
                                std::string()}}});
        }
        for (int c = 0; c < ChgColumns; ++c) {
            if (c != ChgObject)
                _changes->resizeColumnToContents(c);
        }
        *_whole = Whole {tr("the version against now"), std::move(values),
                  gone ? tr("%n object(s) of the version are not in the document", "", gone)
                       : QString()};
        showWhole();
    }
    catch (Base::Exception& e) {
        _status->setText(tr("Not compared: %1").arg(QString::fromUtf8(e.what())));
    }
}

void TransactionLogView::onChangeSelected()
{
    // One line of what a version is not now: its value there and now, and
    // over its object in the view. None selected, the objects chosen again.
    _value->clear();
    auto items = _changes->selectedItems();
    if (items.isEmpty()) {
        showWhole();
        return;
    }
    auto item = items.front();
    const QString there = item->data(ChgProp, Qt::UserRole).toString();
    const QString now = item->data(ChgWhat, Qt::UserRole).toString();
    const long cid = static_cast<long>(item->data(ChgObject, Qt::UserRole).toLongLong());
    const QString prop = item->text(ChgProp);
    _diffBar->clear();
    if (cid && !prop.isEmpty() && !now.isEmpty()) {
        _diffBar->setPairLabel(0, tr("the version against now"));
        _diffBar->setValue(_doc, cid, prop.toStdString(),
                           {{true, there.isEmpty() ? TransactionDiffBar::NoValue
                                                   : there.toStdString(), std::string()}});
    }
    _value->setPlainText(valueText(tr("now"), now) + valueText(tr("at the version"), there));
}

void TransactionLogView::showOps(int64_t seq)
{
    auto l = log();
    if (!l)
        return;
    try {
        // What a save keeps of the values a recompute wrote (sec 32.11)
        std::set<std::string> keptValues;
        for (const auto& kept : l->store().keptValues())
            keptValues.insert(kept.hash);
        const auto all = l->store().keptObjects();
        const std::set<long> keptObjects(all.begin(), all.end());
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
            const bool kept = op.derived && op.ckind == "obj"
                              && (keptValues.count(op.vafter) || keptObjects.count(op.cid));
            item->setText(OpDerived, kept ? tr("yes, kept")
                                         : op.derived ? QStringLiteral("yes")
                                         : op.followed ? tr("followed") : QString());
            item->setData(OpBefore, Qt::UserRole, QString::fromStdString(op.vbefore));
            item->setData(OpAfter, Qt::UserRole, QString::fromStdString(op.vafter));
            item->setData(OpProp, Qt::UserRole, QString::fromStdString(op.meta));
            item->setData(OpContainer, Qt::UserRole,
                          op.ckind == "obj" ? static_cast<qlonglong>(op.cid) : 0);
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
    if (items.isEmpty()) {
        showWhole();
        return;
    }
    _diffBar->clear();
    auto item = items.front();
    QString before = item->data(OpBefore, Qt::UserRole).toString();
    QString after = item->data(OpAfter, Qt::UserRole).toString();
    // What the op made of the value, over the object as it is now. An op
    // with no value before made the property, and has nothing to be against.
    const long cid = static_cast<long>(item->data(OpContainer, Qt::UserRole).toLongLong());
    if (cid && !before.isEmpty() && !after.isEmpty()) {
        _diffBar->setPairLabel(0, tr("before against after"));
        _diffBar->setValue(_doc, cid, item->text(OpProp).toStdString(),
                           {{true, before.toStdString(), after.toStdString()}});
    }
    QString meta = item->data(OpProp, Qt::UserRole).toString();
    QString text;
    if (!meta.isEmpty())
        text += tr("meta: %1\n\n").arg(meta);
    // The after value is what the property became; show it first, the
    // before value after it, each under its full ref.
    text += valueText(QStringLiteral("after"), after);
    text += valueText(QStringLiteral("before"), before);
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
        // A login is hidden unless asked for (sec 30.6 U2).
        if (match && !_showLogins->isChecked())
            match = item->text(TxnKind) != QLatin1String("login");
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
        // A merge's second parent (sec 28.2), with every branch shown: a
        // lane from this node down to the row merged in -- the one already
        // waiting for it, when the branch went on after the merge.
        int started = -1;
        const qlonglong merged
            = all ? nearestShown(item->data(TxnMerged, RoleMerged).toLongLong()) : 0;
        if (merged && merged != parent) {
            int lane = -1;
            for (size_t i = 0; i < waiting.size() && lane < 0; ++i) {
                if (waiting[i] == merged && static_cast<int>(i) != node)
                    lane = static_cast<int>(i);
            }
            if (lane < 0) {
                for (size_t i = 0; i < waiting.size() && lane < 0; ++i) {
                    if (!waiting[i] && static_cast<int>(i) != node)
                        lane = static_cast<int>(i);
                }
                if (lane < 0) {
                    lane = static_cast<int>(waiting.size());
                    waiting.push_back(0);
                    colour.push_back(0);
                }
                waiting[lane] = merged;
                colour[lane] = shown[merged]->data(TxnBranch, RoleBranch).toLongLong();
                started = lane;
            }
            row.out.push_back({node, lane, colour[lane]});
        }
        for (size_t i = 0; i < waiting.size(); ++i) {
            if (waiting[i] && static_cast<int>(i) != started)
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
        _mergeBranch->setEnabled(false);
        _importFile->setEnabled(false);
        _openBranch->setEnabled(false);
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
    _mergeBranch->setEnabled(_doc != nullptr && _branch->count() > 1);
    _importFile->setEnabled(_doc != nullptr && !l->detached());
    _openBranch->setEnabled(_doc != nullptr && !l->detached());
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

namespace {

/// A value as one line of text: its fragment, and how many files go with
/// it. A derived value -- a shape -- is named by its ref alone: reading it
/// would read its geometry.
QString valueText(App::TransactionLog* log, const std::string& ref, bool derived)
{
    if (ref.empty())
        return QStringLiteral("-");
    App::CapturedValue v;
    if (derived || !log || !log->readValue(ref, v))
        return shortRef(ref);
    QString text = QString::fromStdString(v.fragment).simplified();
    if (!v.attachments.empty())
        text += QStringLiteral(" [+%1]").arg(v.attachments.size());
    return text;
}

/** What a merge would do, and a side for each conflict (docs/TransactionLog.md
 * sec 28.2 item 10): every change the other branch made, the conflicts
 * first, each with the value at the base, ours and theirs as text. A
 * conflict starts on ours; a view-provider conflict is listed with the
 * rest and keeps ours unless changed here (sec 28.6 Q2).
 */
class MergeDialog: public QDialog
{
public:
    enum Column { Kind, Object, Property, Base, Ours, Theirs, Takes, Columns };

    /// What the merge would make, with the sides given (sec 32.17)
    using Outcome = std::function<App::Document::MergeOutcome(
        const std::map<std::string, std::string>&)>;
    /// The pairs of the bar, by their place
    enum Pair { OursTheirs, BaseOurs, BaseTheirs, OursResult };

    /// `rows`: the preview is of rows picked out of the log (sec 31.12),
    /// applied here, and not of a branch merged.
    MergeDialog(const App::Document::MergePreview& preview, const QString& ours,
                App::TransactionLog* log, App::Document* doc, QWidget* parent, bool rows = false,
                Outcome outcome = Outcome())
        : QDialog(parent)
        , _outcome(std::move(outcome))
    {
        setObjectName(QStringLiteral("TransactionMergeDialog"));
        setWindowTitle((rows ? QObject::tr("Apply %1 to %2") : QObject::tr("Merge %1 into %2"))
                           .arg(QString::fromStdString(preview.branch), ours));
        auto layout = new QVBoxLayout(this);
        QString summary = rows ? QObject::tr("%1 changes, %2 conflicts.")
                                     .arg(preview.changes.size())
                                     .arg(preview.conflicts)
                               : QObject::tr("%1 changes since row %2, %3 conflicts.")
                                     .arg(preview.changes.size())
                                     .arg(preview.base)
                                     .arg(preview.conflicts);
        if (rows)
            summary += QLatin1Char(' ')
                     + QObject::tr("The rows stay their branch's; what they set is written "
                                   "here as one step, and recomputed.");
        else if (!preview.forward.empty())
            summary += QLatin1Char(' ')
                     + QObject::tr("This branch has not moved since: a fast-forward, the "
                                   "other's %1 rows taken as they are. What they changed "
                                   "is marked to recompute.")
                           .arg(preview.forward.size());
        else if (preview.fastForward)
            summary += QLatin1Char(' ')
                     + QObject::tr("This branch has changed nothing since: the other is taken "
                                   "whole, and what it changed is recomputed.");
        else
            summary += QLatin1Char(' ')
                     + QObject::tr("Derived values are not merged; the result is recomputed.");
        // Sec 31.12: what asks nothing may be left out all the same.
        summary += QLatin1Char(' ')
                 + QObject::tr("Untick a change to leave it out.");
        layout->addWidget(new QLabel(summary, this));

        _tree = new QTreeWidget(this);
        _tree->setColumnCount(Columns);
        _tree->setHeaderLabels({QObject::tr("Change"), QObject::tr("Object"),
                                QObject::tr("Property"), QObject::tr("Base"), QObject::tr("Ours"),
                                QObject::tr("Theirs"), QObject::tr("Takes")});
        _tree->setRootIsDecorated(false);
        _tree->setAlternatingRowColors(true);
        _tree->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(_tree, 1);

        // Conflicts first, then what goes in, then what does not.
        auto rank = [](const std::string& kind) {
            return kind == "conflict" ? 0 : kind == "merge" ? 1 : kind == "take" ? 1
                 : kind == "view" ? 2 : kind == "derived" ? 3 : 4;
        };
        std::vector<const App::Document::MergeChange*> order;
        for (const auto& c : preview.changes)
            order.push_back(&c);
        std::stable_sort(order.begin(), order.end(),
                         [&](const auto* a, const auto* b) { return rank(a->kind) < rank(b->kind); });
        const QColor quiet = _tree->palette().color(QPalette::Disabled, QPalette::Text);
        for (const auto* c : order) {
            auto item = new QTreeWidgetItem(_tree);
            item->setData(Object, Qt::UserRole, static_cast<int>(_changes.size()));
            _changes.push_back(c);
            item->setText(Kind, QString::fromStdString(c->kind + " " + c->op));
            item->setText(Object, c->ckind == "doc" ? QObject::tr("(document)")
                                                    : QString::fromStdString(c->object));
            item->setText(Property, QString::fromStdString(c->prop));
            item->setData(Kind, Qt::UserRole, QString::fromStdString(c->key));
            if (!c->note.empty())
                item->setToolTip(Kind, QString::fromStdString(c->note));
            if (!c->prop.empty()) {
                item->setText(Base, valueText(log, c->base, c->derived));
                item->setText(Ours, valueText(log, c->ours, c->derived));
                item->setText(Theirs, valueText(log, c->theirs, c->derived));
                for (int col : {Base, Ours, Theirs})
                    item->setToolTip(col, item->text(col));
            }
            else if (!c->note.empty()) {
                item->setText(Base, QString::fromStdString(c->note));
            }
            const bool conflict = c->kind == "conflict";
            if (conflict || c->kind == "view") {
                auto side = new QComboBox(_tree);
                side->addItems({QStringLiteral("ours"), QStringLiteral("theirs")});
                // A conflict starts on the side the setting says, ours
                // unless it was changed -- or on none, where the setting
                // is to ask: nothing is merged until each has one. A
                // view's keeps ours (sec 28.6 Q2)
                side->setPlaceholderText(QObject::tr("pick a side"));
                if (conflict && App::DocumentParams::getMergeConflictSide() == 1)
                    side->setCurrentIndex(1);
                else if (conflict && App::DocumentParams::getMergeConflictSide() == 2)
                    side->setCurrentIndex(-1);
                _tree->setItemWidget(item, Takes, side);
                _sides.emplace_back(c->key, side);
                QObject::connect(side, qOverload<int>(&QComboBox::currentIndexChanged), this,
                                 [this](int) { sidesChanged(); });
                if (conflict) {
                    _conflicts.push_back(side);
                    QFont bold = item->font(Kind);
                    bold.setBold(true);
                    for (int col = 0; col < Columns; ++col)
                        item->setFont(col, bold);
                }
            }
            else if (c->kind == "take") {
                item->setText(Takes, QStringLiteral("theirs"));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Kind, Qt::Checked);
                _taken.push_back(item);
            }
            else if (c->kind == "merge") {
                // Merged by what it holds (sec 31.8): nothing to pick, and
                // the note says what of theirs goes in.
                item->setText(Takes, QObject::tr("both"));
                item->setToolTip(Takes, QString::fromStdString(c->note));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Kind, Qt::Checked);
                _taken.push_back(item);
            }
            else if (c->kind == "name") {
                // What an expression named by its place is named before
                // the merge (sec 31.11): ours, saying the names.
                item->setText(Takes, QObject::tr("named"));
                item->setToolTip(Takes, QString::fromStdString(c->note));
                for (int col = 0; col < Columns; ++col)
                    item->setForeground(col, quiet);
            }
            else if (c->kind == "unit") {
                // One thing with the conflict above it (sec 31.5): it goes
                // the way that one is picked.
                item->setText(Takes, QObject::tr("as %1").arg(QString::fromStdString(c->key)));
                for (int col = 0; col < Columns; ++col)
                    item->setForeground(col, quiet);
            }
            else {
                item->setText(Takes, c->kind == "derived" ? QObject::tr("recomputed")
                                                           : QStringLiteral("-"));
                for (int col = 0; col < Columns; ++col)
                    item->setForeground(col, quiet);
            }
        }
        for (int col = 0; col < Columns; ++col)
            _tree->resizeColumnToContents(col);
        for (int col : {Base, Ours, Theirs})
            _tree->setColumnWidth(col, std::min(_tree->columnWidth(col), 260));

        // The properties of one sketch merged together have one key (sec
        // 31.10): left out, they are all left out.
        QObject::connect(_tree, &QTreeWidget::itemChanged, this,
                         [this](QTreeWidgetItem* changed, int column) {
                             if (column != Kind || _linking)
                                 return;
                             _linking = true;
                             const QString key = changed->data(Kind, Qt::UserRole).toString();
                             for (auto item : _taken) {
                                 if (item != changed
                                         && item->data(Kind, Qt::UserRole).toString() == key)
                                     item->setCheckState(Kind, changed->checkState(Kind));
                             }
                             _linking = false;
                             sidesChanged();
                         });

        // The change selected, in the 3D view (sec 32.9). Ours is what the
        // document has: an empty ref.
        // And ours against what the merge would make (sec 32.17): made
        // when it is asked for, in a document of its own.
        QStringList pairs {QObject::tr("ours against theirs"), QObject::tr("base against ours"),
                           QObject::tr("base against theirs")};
        if (_outcome)
            pairs << QObject::tr("ours against the result");
        _diff = new TransactionDiffBar(this, pairs);
        layout->addWidget(_diff);
        _home = doc ? doc->getName() : "";
        _show = [this, doc]() {
            // The result's value of a property, where it has been made and
            // has one: what the object of this id has in its document. Of
            // a fast-forward the result is theirs.
            auto pairsOf = [this](const App::Document::MergeChange& c) {
                std::vector<TransactionDiffBar::Pair> pairs {
                    {!c.theirs.empty(), std::string(), c.theirs},
                    {!c.base.empty(), c.base, std::string()},
                    {!c.base.empty() && !c.theirs.empty(), c.base, c.theirs}};
                if (_resultIsTheirs)
                    pairs.push_back(pairs.front());
                else
                    pairs.push_back({_result.count({c.cid, c.prop}) > 0, std::string(),
                                     _resultRef});
                return pairs;
            };
            // What the result has and the document has not -- theirs made
            // it: all of it new, drawn by the view the result's document
            // has of it
            auto madeThere = [this](long cid, const std::string& prop) {
                return TransactionDiffBar::Value {
                    cid, prop, {{}, {}, {}, {true, TransactionDiffBar::NoValue, std::string()}},
                    resultDocument()};
            };
            const auto items = _tree->selectedItems();
            if (!items.isEmpty()) {
                const auto* c = _changes[items.front()->data(Object, Qt::UserRole).toInt()];
                if (c->ckind == "obj" && c->prop.empty() && _there.count(c->cid))
                    _diff->setValues(doc, {madeThere(c->cid, _there[c->cid])}, false);
                else if (c->ckind != "obj" || c->prop.empty())
                    _diff->clear();
                else
                    _diff->setValue(doc, c->cid, c->prop, pairsOf(*c));
                return;
            }
            // No change selected: all of them, over what the view shows
            // (sec 32.10). What theirs made has no object here.
            std::vector<TransactionDiffBar::Value> values;
            std::set<std::pair<long, std::string>> listed;
            int made = 0;
            for (const auto* c : _changes) {
                if (c->ckind != "obj")
                    continue;
                if (c->prop.empty()) {
                    made += c->op == "create" ? 1 : 0;
                }
                else {
                    values.push_back({c->cid, c->prop, pairsOf(*c), std::string()});
                    listed.insert({c->cid, c->prop});
                }
            }
            for (const auto& made : _there)
                values.push_back(madeThere(made.first, made.second));
            // The result reaches further than the changes: what is made of
            // what changed is computed again, and no line here says so
            for (const auto& value : _result) {
                if (listed.count(value))
                    continue;
                values.push_back({value.first, value.second,
                                  {{}, {}, {}, {true, std::string(), _resultRef}},
                                  std::string()});
            }
            _diff->setValues(doc, values, true,
                             made ? QObject::tr("%n object(s) of theirs are not in the document",
                                                "", made)
                                  : QString());
        };
        QObject::connect(_tree, &QTreeWidget::itemSelectionChanged, this, [this]() { _show(); });
        _diff->whenPairChosen([this](int index) {
            if (index == OursResult)
                makeResult();
        });
        // The result's document closed by another hand: its views leave
        // the 3D views before they go with it
        //NOLINTBEGIN
        _resultGone = Application::Instance->signalDeleteDocument.connect(
            [this](const Gui::Document& gone) {
                if (_resultRef.empty() || !gone.getDocument()
                        || resultDocument() != gone.getDocument()->getName())
                    return;
                takeOutViewsThere();
                _result.clear();
                _resultRef.clear();
                _made = false;
            });
        //NOLINTEND
        // What is selected as it opens is chosen; more is picked from here,
        // the document being in reach (sec 32.15)
        _diff->setDocument(doc);
        _diff->chooseSelected();
        _show();

        // The document stays in reach while this is open (sec 32.15), and
        // may be changed under it
        _stale = new QLabel(QObject::tr("The document has changed, and this is no longer what "
                                        "would be done. Preview again: the sides picked here "
                                        "are asked again."), this);
        _stale->setObjectName(QStringLiteral("stale"));
        _stale->setWordWrap(true);
        _stale->hide();
        layout->addWidget(_stale);

        auto buttons = new QDialogButtonBox(this);
        auto merge = buttons->addButton(rows ? QObject::tr("Apply") : QObject::tr("Merge"),
                                        QDialogButtonBox::AcceptRole);
        merge->setObjectName(QStringLiteral("merge"));
        _merge = merge;
        _mergeSays = merge->text();
        _again = buttons->addButton(QObject::tr("Preview again"), QDialogButtonBox::ActionRole);
        _again->setObjectName(QStringLiteral("again"));
        _again->hide();
        QObject::connect(_again, &QPushButton::clicked, this, [this]() { done(Again); });
        buttons->addButton(QDialogButtonBox::Cancel);
        auto allOurs = buttons->addButton(QObject::tr("All ours"), QDialogButtonBox::ActionRole);
        auto allTheirs = buttons->addButton(QObject::tr("All theirs"), QDialogButtonBox::ActionRole);
        allOurs->setEnabled(!_conflicts.empty());
        allTheirs->setEnabled(!_conflicts.empty());
        QObject::connect(allOurs, &QPushButton::clicked, this, [this]() { setAll(0); });
        QObject::connect(allTheirs, &QPushButton::clicked, this, [this]() { setAll(1); });
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(900, 480);
        allowMerge();
    }

    ~MergeDialog() override
    {
        dropResult();
    }

    /// What exec would not return: the preview is to be made again
    enum { Again = 2 };

    /// The document changed and the preview is no longer what would be
    /// done: nothing is merged from here, and the result made is of a
    /// document that is no more.
    void setStale()
    {
        _stale->show();
        _again->show();
        _isStale = true;
        allowMerge();
        dropResult();
        _diff->setPairNote(OursResult, QObject::tr("the document has changed: preview again"));
        _show();
    }

    /// The document the result was made in, where it stands; for a check
    std::string resultDocument() const
    {
        return _resultRef.empty() ? std::string() : _resultRef.substr(4);
    }

    /// What a preview says, to tell whether one made later says another
    static std::string said(const App::Document::MergePreview& preview)
    {
        std::string out = std::to_string(preview.base) + '/'
                          + std::to_string(preview.forward.size()) + '\n';
        for (const auto& c : preview.changes) {
            for (const std::string* part : {&c.kind, &c.op, &c.key, &c.base, &c.ours, &c.theirs,
                                            &c.merged}) {
                out += *part;
                out += '|';
            }
            out += '\n';
        }
        return out;
    }

    /// The side of every conflict that has one, by its key; and `ours` for
    /// every change that asks nothing and was unticked (sec 31.12).
    std::map<std::string, std::string> picks() const
    {
        std::map<std::string, std::string> out;
        for (const auto& kv : _sides) {
            if (kv.second->currentIndex() >= 0)
                out[kv.first] = kv.second->currentText().toStdString();
        }
        for (auto item : _taken) {
            if (item->checkState(Kind) == Qt::Unchecked)
                out[item->data(Kind, Qt::UserRole).toString().toStdString()] = "ours";
        }
        return out;
    }

private:
    void setAll(int index)
    {
        // One result for them all, and not one for each
        _settingAll = true;
        for (auto side : _conflicts)
            side->setCurrentIndex(index);
        _settingAll = false;
        sidesChanged();
    }

    /// The views of what the result has and the document has not, where
    /// its document still stands
    std::vector<ViewProviderDocumentObject*> viewsThere() const
    {
        std::vector<ViewProviderDocumentObject*> views;
        const std::string name = resultDocument();
        auto there = name.empty() ? nullptr : App::GetApplication().getDocument(name.c_str());
        for (const auto& made : _there) {
            auto obj = there ? there->getObjectByID(made.first) : nullptr;
            auto view = obj ? Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                                  Application::Instance->getViewProvider(obj))
                            : nullptr;
            if (view)
                views.push_back(view);
        }
        return views;
    }

    /// The 3D views of the document the merge is of
    std::vector<View3DInventorViewer*> viewers() const
    {
        std::vector<View3DInventorViewer*> out;
        auto doc = App::GetApplication().getDocument(_home.c_str());
        auto gui = doc ? Application::Instance->getDocument(doc) : nullptr;
        if (!gui)
            return out;
        for (auto view : gui->getMDIViewsOfType(View3DInventor::getClassTypeId()))
            out.push_back(static_cast<View3DInventor*>(view)->getViewer());
        return out;
    }

    /// Out of the views, before the document they are of goes
    void takeOutViewsThere()
    {
        for (auto view : viewsThere()) {
            view->hideValueDiff();
            for (auto viewer : viewers())
                viewer->removeViewProvider(view);
        }
        _there.clear();
    }

    /// Close the document the result was made in, and forget its values
    void dropResult()
    {
        _result.clear();
        _resultIsTheirs = false;
        _made = false;
        takeOutViewsThere();
        const std::string name = resultDocument();
        _resultRef.clear();
        if (!name.empty() && App::GetApplication().getDocument(name.c_str()))
            App::GetApplication().closeDocument(name.c_str());
    }

    /// Nothing is merged from a stale preview, nor while a conflict has no
    /// side: the button says how many are asked.
    void allowMerge()
    {
        if (!_merge)
            return;
        int asked = 0;
        for (auto side : _conflicts)
            asked += side->currentIndex() < 0 ? 1 : 0;
        _merge->setEnabled(!_isStale && asked == 0);
        _merge->setToolTip(asked ? QObject::tr("%n conflict(s) have no side: pick one for each",
                                               "", asked)
                                 : QString());
        _merge->setText(asked ? QObject::tr("%1 (%n to pick)", "", asked).arg(_mergeSays)
                              : _mergeSays);
    }

    /// A side was picked, or a change left out: the result made is of
    /// other sides, and is made again where it is what is looked at.
    void sidesChanged()
    {
        allowMerge();
        if (_settingAll || !_made)
            return;
        dropResult();
        if (_diff->pair() == OursResult)
            makeResult();
        else
            _show();
    }

    /// What the merge would make, with the sides as they are picked now
    /// (sec 32.17): made once, and again when a side changes.
    void makeResult()
    {
        if (!_outcome || _made || _isStale || _making)
            return;
        Base::StateLocker guard(_making);
        dropResult();
        QString note;
        try {
            Gui::WaitCursor wait;
            const auto outcome = _outcome(picks());
            _made = true;
            if (outcome.theirs) {
                _resultIsTheirs = true;
                note = QObject::tr("a fast-forward: the result is theirs");
            }
            else if (outcome.document) {
                _resultRef = std::string("doc:") + outcome.document->getName();
                int made = 0;
                for (const auto& value : outcome.values) {
                    if (value.here) {
                        _result.insert({value.cid, value.prop});
                        continue;
                    }
                    // What the merge makes: the view the result's document
                    // has of it goes into this document's 3D views, where
                    // the merge would show it -- what it is made of, which
                    // theirs hid, is left out. Its own picture is not
                    // drawn: what is shown of it is all of it as new.
                    ++made;
                    auto obj = outcome.document->getObjectByID(value.cid);
                    auto view = obj ? Base::freecad_dynamic_cast<ViewProviderDocumentObject>(
                                          Application::Instance->getViewProvider(obj))
                                    : nullptr;
                    if (!view || !view->isShow())
                        continue;
                    view->hide();
                    if (auto pick = Base::freecad_dynamic_cast<App::PropertyBool>(
                            view->getPropertyByName("Selectable")))
                        pick->setValue(false);
                    for (auto viewer : viewers())
                        viewer->addViewProvider(view);
                    _there[value.cid] = value.prop;
                }
                QStringList parts;
                if (outcome.values.empty())
                    parts << QObject::tr("the result is the document as it is");
                if (made)
                    parts << QObject::tr("%n object(s) made by the merge", "", made);
                if (!outcome.removed.empty())
                    parts << QObject::tr("%n object(s) are removed", "",
                                         static_cast<int>(outcome.removed.size()));
                if (!outcome.failed.empty()) {
                    QStringList names;
                    for (const auto& name : outcome.failed)
                        names << QString::fromStdString(name);
                    parts << QObject::tr("%n object(s) fail to compute in the result: %1", "",
                                         static_cast<int>(names.size()))
                                 .arg(names.join(QStringLiteral(", ")));
                }
                note = parts.join(QStringLiteral("; "));
            }
            else if (!outcome.unresolved.empty()) {
                note = QObject::tr("%n conflict(s) have no side", "",
                                   static_cast<int>(outcome.unresolved.size()));
            }
            else {
                note = QObject::tr("the result is the document as it is");
            }
        }
        catch (Base::Exception& e) {
            note = QObject::tr("no result: %1").arg(QString::fromUtf8(e.what()));
        }
        _diff->setPairNote(OursResult, note);
        _show();
    }

    QTreeWidget* _tree {nullptr};
    /// The change of each row, by the number the row holds
    std::vector<const App::Document::MergeChange*> _changes;
    TransactionDiffBar* _diff {nullptr};
    QLabel* _stale {nullptr};
    QPushButton* _merge {nullptr};
    QString _mergeSays;   ///< what the button says with nothing asked
    QPushButton* _again {nullptr};
    std::vector<std::pair<std::string, QComboBox*>> _sides;
    std::vector<QComboBox*> _conflicts;
    /// The rows that go in with nothing asked: each may be unticked.
    std::vector<QTreeWidgetItem*> _taken;
    bool _linking {false};
    /// What the merge would make (sec 32.17): how it is made, the values
    /// it has of the objects the document has, and what they are read by
    /// -- "doc:" and the name of the document it was made in.
    Outcome _outcome;
    fastsignals::scoped_connection _resultGone;
    std::string _home;   ///< the document the merge is of, by name
    std::function<void()> _show;
    std::set<std::pair<long, std::string>> _result;
    /// What the result has and the document has not, shown: the geometry
    /// of each, by its id
    std::map<long, std::string> _there;
    std::string _resultRef;
    bool _resultIsTheirs {false};
    bool _made {false};
    bool _making {false};
    bool _isStale {false};
    bool _settingAll {false};
};

} // namespace

void TransactionLogView::onOpenBranch()
{
    if (!_doc)
        return;
    try {
        _doc->openNewBranch();
    }
    catch (Base::Exception& e) {
        FC_ERR("open a branch of " << _doc->getName() << ": " << e.what());
        _status->setText(tr("No branch opened -- the report view says why"));
    }
}

int TransactionLogView::askBeside(QDialog& dialog, const std::string& said,
                                  const std::function<std::string()>& preview,
                                  const std::function<void()>& stale)
{
    // Not modal (sec 32.15): the view is turned, objects are picked and the
    // document is even changed while the preview is open. This waits for
    // its answer all the same, in a loop of its own, so that whoever asked
    // goes on from it. What can happen meanwhile: the document changes --
    // the preview is made again and, where it says another thing, the
    // dialog is told; the document leaves the panel -- detach() rejects the
    // dialog; another merge is asked for -- refused where it is asked.
    QPointer<TransactionLogView> self(this);
    App::Document* doc = _doc;
    bool told = false;
    QTimer check;
    check.setSingleShot(true);
    check.setInterval(300);
    QObject::connect(&check, &QTimer::timeout, &dialog, [&]() {
        if (told || _doc != doc)
            return;
        std::string now;
        try {
            now = preview();
        }
        catch (Base::Exception&) {
            // No longer to be had: the branch is gone, or merged
        }
        if (now != said) {
            told = true;
            stale();
        }
    });
    std::vector<fastsignals::scoped_connection> watch;
    //NOLINTBEGIN
    watch.emplace_back(doc->signalCommitTransaction.connect(
        [&check](const App::Document&) { check.start(); }));
    watch.emplace_back(doc->signalUndo.connect([&check](const App::Document&) { check.start(); }));
    watch.emplace_back(doc->signalRedo.connect([&check](const App::Document&) { check.start(); }));
    watch.emplace_back(doc->signalBranchesChanged.connect(
        [&check](const App::Document&) { check.start(); }));
    //NOLINTEND
    _mergeDialog = &dialog;
    dialog.setModal(false);
    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    QEventLoop loop;
    QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
    watch.clear();
    if (!self)
        return QDialog::Rejected;
    _mergeDialog = nullptr;
    return _doc == doc ? dialog.result() : static_cast<int>(QDialog::Rejected);
}

bool TransactionLogView::mergeIsOpen()
{
    if (!_mergeDialog)
        return false;
    _mergeDialog->raise();
    _mergeDialog->activateWindow();
    _status->setText(tr("A merge is being looked at already: answer it first"));
    return true;
}

void TransactionLogView::onMergeBranch()
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
    const QString name = QInputDialog::getItem(this, tr("Merge a branch"), tr("Merge into this branch:"),
                                               names, 0, false, &ok);
    if (ok && !name.isEmpty())
        mergeBranch(name);
}

void TransactionLogView::mergeBranch(const QString& name)
{
    auto l = log();
    if (!l || !_doc || mergeIsOpen())
        return;
    try {
        std::map<std::string, std::string> picks;
        for (;;) {
            const auto preview = _doc->previewMerge(name.toStdString());
            if (preview.changes.empty() && preview.forward.empty()) {
                _status->setText(tr("Nothing of %1 to merge").arg(name));
                return;
            }
            App::LogBranch mine;
            l->store().getBranch(l->branch(), mine);
            App::Document* doc = _doc;
            QPointer<MergeDialog> dialog(new MergeDialog(
                preview, QString::fromStdString(mine.name), l, _doc, this, false,
                [doc, name](const std::map<std::string, std::string>& sides) {
                    return doc->previewMergeResult(name.toStdString(), sides);
                }));
            const int answer = askBeside(
                *dialog, MergeDialog::said(preview),
                [this, name]() { return MergeDialog::said(_doc->previewMerge(name.toStdString())); },
                [dialog]() {
                    if (dialog)
                        dialog->setStale();
                });
            if (!dialog)
                return;   // gone with the panel
            picks = dialog->picks();
            delete dialog.data();
            if (answer == MergeDialog::Again)
                continue;
            if (answer != QDialog::Accepted)
                return;
            break;
        }
        const auto result = _doc->mergeBranch(name.toStdString(), picks);
        if (!result.unresolved.empty())
            _status->setText(tr("Merge of %1 refused: %2 conflicts have no side")
                                 .arg(name).arg(result.unresolved.size()));
        else if (result.forwarded)
            _status->setText(tr("Merged %1: fast-forward, %2 rows taken as they are")
                                 .arg(name).arg(result.forwarded));
        else if (result.unchanged)
            _status->setText(tr("Merged %1 as row %2: nothing here changed, and there is "
                                "nothing to undo").arg(name).arg(result.seq));
        else if (!result.failed.empty())
            _status->setText(tr("Merged %1 as row %2; %3 objects failed to recompute")
                                 .arg(name).arg(result.seq).arg(result.failed.size()));
        else
            _status->setText(tr("Merged %1 as row %2").arg(name).arg(result.seq));
    }
    catch (Base::Exception& e) {
        FC_ERR("merge of branch " << name.toStdString() << ": " << e.what());
        _status->setText(tr("Branch not merged -- the report view says why"));
    }
}

void TransactionLogView::rollBackMerge()
{
    if (!_doc)
        return;
    try {
        const auto undone = _doc->undoneMerge();
        if (undone.seq <= 0) {
            _status->setText(tr("No merge undone at the head to roll back"));
            return;
        }
        const QString name = QString::fromStdString(undone.branch);
        if (QMessageBox::question(
                this, tr("Roll back a merge"),
                tr("Roll back the merge of %1?\n\nIts %2 rows -- the merge and its undo -- "
                   "leave the log, and the merge can no longer be redone. The document does "
                   "not change. %1 is then no longer merged: merging it again asks everything "
                   "again.").arg(name).arg(undone.rows))
                != QMessageBox::Yes)
            return;
        const auto done = _doc->rollBackMerge();
        if (done.seq > 0)
            _status->setText(tr("Merge of %1 rolled back to row %2: %3 rows taken out")
                                 .arg(name).arg(done.before).arg(done.rows));
        else
            _status->setText(tr("Merge of %1 not rolled back").arg(name));
    }
    catch (Base::Exception& e) {
        FC_ERR("roll back of a merge: " << e.what());
        _status->setText(tr("Merge not rolled back -- the report view says why"));
    }
}

void TransactionLogView::applyRows(const QString& seqs)
{
    // docs/TransactionLog.md sec 31.12: rows of another branch, picked in
    // the list, applied to this one as a step of its own.
    auto l = log();
    if (!l || !_doc || mergeIsOpen())
        return;
    std::vector<int64_t> rows;
    for (const QString& part : seqs.split(QLatin1Char(','), Qt::SkipEmptyParts))
        rows.push_back(part.trimmed().toLongLong());
    try {
        std::map<std::string, std::string> picks;
        QString what;
        for (;;) {
            const auto preview = _doc->previewPick(rows);
            what = QString::fromStdString(preview.branch);
            if (preview.changes.empty()) {
                _status->setText(tr("Nothing of %1 to apply").arg(what));
                return;
            }
            App::LogBranch mine;
            l->store().getBranch(l->branch(), mine);
            App::Document* doc = _doc;
            QPointer<MergeDialog> dialog(new MergeDialog(
                preview, QString::fromStdString(mine.name), l, _doc, this, true,
                [doc, rows](const std::map<std::string, std::string>& sides) {
                    return doc->previewPickResult(rows, sides);
                }));
            const int answer = askBeside(
                *dialog, MergeDialog::said(preview),
                [this, rows]() { return MergeDialog::said(_doc->previewPick(rows)); },
                [dialog]() {
                    if (dialog)
                        dialog->setStale();
                });
            if (!dialog)
                return;   // gone with the panel
            picks = dialog->picks();
            delete dialog.data();
            if (answer == MergeDialog::Again)
                continue;
            if (answer != QDialog::Accepted)
                return;
            break;
        }
        const auto result = _doc->pickRows(rows, picks);
        if (!result.unresolved.empty())
            _status->setText(tr("%1 not applied: %2 conflicts have no side")
                                 .arg(what).arg(result.unresolved.size()));
        else if (!result.seq)
            _status->setText(tr("%1 applied: nothing changed").arg(what));
        else if (!result.failed.empty())
            _status->setText(tr("Applied %1 as row %2; %3 objects failed to recompute")
                                 .arg(what).arg(result.seq).arg(result.failed.size()));
        else
            _status->setText(tr("Applied %1 as row %2").arg(what).arg(result.seq));
    }
    catch (Base::Exception& e) {
        FC_ERR("apply of row(s) " << seqs.toStdString() << ": " << e.what());
        _status->setText(tr("Rows not applied -- the report view says why"));
    }
}

void TransactionLogView::onImportFile()
{
    if (!_doc)
        return;
    const QString path = FileDialog::getOpenFileName(
        this, tr("Merge from file"), QString(),
        QStringLiteral("%1 (*.FCStd)").arg(tr("FreeCAD document")));
    if (!path.isEmpty())
        importFile(path, QString());
}

void TransactionLogView::importFile(const QString& path, const QString& branch)
{
    importFrom(path, branch, QString());
}

bool TransactionLogView::importFrom(const QString& path, const QString& branch,
                                    const QString& sender, qulonglong sent, bool merge)
{
    auto l = log();
    if (!l || !_doc)
        return false;
    const QString file = QFileInfo(path).fileName();
    try {
        // F6: the branch asked for; else the one the copy's file reopens
        // on, which the user confirms or changes when the copy has more
        // than one with something to bring.
        QString from = branch;
        if (from.isEmpty()) {
            // What has something to bring: a branch that shares a row, or
            // the file itself where it shares none (sec 30.22), which comes
            // as an independent branch.
            QStringList names;
            int current = 0;
            for (const auto& b : _doc->forkBranches(path.toStdString())) {
                if (!b.ahead || (!b.base && !b.independent))
                    continue;
                if (b.current)
                    current = names.size();
                names << QString::fromStdString(b.name);
            }
            if (names.isEmpty()) {
                // A file the log holds is let go, with a record saying it
                // had nothing to give (sec 30.29).
                if (sent)
                    _doc->importSentFile(static_cast<int64_t>(sent));
                _status->setText(tr("Nothing new in %1").arg(file));
                return true;
            }
            from = names[current];
            if (names.size() > 1) {
                bool ok = false;
                from = QInputDialog::getItem(this, tr("Merge from file"),
                                             tr("The branch of %1 to bring in:").arg(file), names,
                                             current, false, &ok);
                if (!ok || from.isEmpty())
                    return false;
            }
        }
        const auto result =
            sent ? _doc->importSentFile(static_cast<int64_t>(sent), from.toStdString())
                 : _doc->importFork(path.toStdString(), from.toStdString(),
                                    sender.toStdString());
        refresh();
        if (!result.rows && !result.stoppedAt) {
            _status->setText(tr("Nothing new in %1").arg(file));
            return true;
        }
        const QString name = QString::fromStdString(result.branch);
        if (result.independent)
            Base::Console().Message("Merge from %s: it shares no history with this file, and "
                                    "came as the independent branch %s\n",
                                    file.toUtf8().constData(), result.branch.c_str());
        if (result.stoppedAt)
            Base::Console().Warning("Merge from %s: %s\n", file.toUtf8().constData(),
                                    result.reason.c_str());
        // The merge is the owner's to make (sec 30.3): the preview, then.
        if (result.rows && merge)
            mergeBranch(name);
        else if (result.rows)
            _status->setText(tr("%1 brought in as branch %2, not merged").arg(file, name));
        if (result.stoppedAt)
            _status->setText(tr("%1: %2 rows came as branch %3, then one could not be applied "
                                "-- the report view says why")
                                 .arg(file).arg(result.rows).arg(name));
        return true;
    }
    catch (Base::Exception& e) {
        FC_ERR("merge from " << path.toStdString() << ": " << e.what());
        _status->setText(tr("%1 not brought in -- the report view says why").arg(file));
    }
    return false;
}

namespace
{
constexpr int RoleRequestId = Qt::UserRole + 20;       // a sent file's number
constexpr int RoleRequestBranch = Qt::UserRole + 21;   // a branch's name
constexpr int RoleRequestFrom = Qt::UserRole + 22;     // the copy's branch it came from
constexpr int RoleRequestStopped = Qt::UserRole + 23;  // its import stopped at a row

QString whenText(double time)
{
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(time * 1000))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}
}  // namespace

void TransactionLogView::refreshRequests()
{
    _requests->clear();
    auto l = log();
    if (!l || !_doc || l->detached()) {
        _requests->hide();
        return;
    }
    // H6: a file a client sent is here as it came, and has not been read.
    for (const auto& r : SceneRequests::list(_doc->getName())) {
        auto item = new QTreeWidgetItem(_requests);
        item->setData(0, RoleRequestId, QVariant::fromValue(static_cast<qulonglong>(r.id)));
        item->setText(0, r.name);
        QString from = r.sender;
        if (r.senderKind != QLatin1String("verified") && r.senderKind != QLatin1String("local"))
            from += QStringLiteral(" (%1)").arg(r.senderKind);
        item->setText(1, from);
        item->setText(2, whenText(r.time));
        item->setText(3, tr("%1, sent and not read").arg(QLocale().formattedDataSize(r.size)));
        item->setToolTip(0, tr("Sent by %1. Nothing of it has been opened: bringing it in "
                               "reads it, as a file handed over any other way would be read")
                                .arg(from));
    }
    // H1: a branch an import made that this branch has not taken.
    bool ask = false;
    const int64_t ours = l->head();
    for (const auto& r : _doc->importRequests(false)) {
        App::LogBranch branch;
        if (!l->store().findBranch(r.branch, branch))
            continue;
        auto known = _previewed.find(r.branch);
        const bool fresh = known != _previewed.end() && known->second.theirs == branch.head
            && known->second.ours == ours;
        if (fresh && known->second.conflicts == -2)
            continue;   // the preview found nothing of it to give
        ask = ask || !fresh;
        auto item = new QTreeWidgetItem(_requests);
        item->setData(0, RoleRequestBranch, QString::fromStdString(r.branch));
        item->setData(0, RoleRequestFrom, QString::fromStdString(r.from));
        item->setData(0, RoleRequestStopped, r.stoppedAt != 0);
        item->setText(0, QString::fromStdString(r.branch));
        QStringList who;
        for (const auto& name : r.authors)
            who << QString::fromStdString(name);
        QString from = who.join(QStringLiteral(", "));
        if (!r.sender.empty())
            from += tr("; sent by %1").arg(QString::fromStdString(r.sender));
        item->setText(1, from);
        item->setText(2, whenText(r.when));
        QString what = tr("%n operation(s)", nullptr, static_cast<int>(r.rows));
        if (r.independent)
            what += tr(", shares no history");
        if (r.stoppedAt)
            what += tr(", stopped at a row");
        item->setText(3, what);
        item->setText(4, !fresh ? QStringLiteral("...")
                         : known->second.conflicts < 0 ? QStringLiteral("?")
                                                      : QString::number(known->second.conflicts));
        item->setToolTip(0, tr("Brought in from %1 and not merged into this branch")
                                .arg(QString::fromStdString(r.file)));
    }
    _requests->setVisible(_requests->topLevelItemCount() > 0);
    if (_requests->isVisible()) {
        for (int c = 0; c < _requests->columnCount(); ++c)
            _requests->resizeColumnToContents(c);
    }
    if (ask && isVisible())
        _previewTimer.start();
}

void TransactionLogView::previewRequests()
{
    auto l = log();
    if (!l || !_doc || l->detached())
        return;
    try {
        std::map<std::string, int> found;
        for (const auto& r : _doc->importRequests(true))
            found[r.branch] = r.conflicts;
        _previewed.clear();
        for (const auto& r : _doc->importRequests(false)) {
            App::LogBranch branch;
            if (!l->store().findBranch(r.branch, branch))
                continue;
            Previewed p;
            p.theirs = branch.head;
            p.ours = l->head();
            auto it = found.find(r.branch);
            p.conflicts = it == found.end() ? -2 : it->second;
            _previewed[r.branch] = p;
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log view, requests: " << e.what());
    }
    refreshRequests();
}

void TransactionLogView::onRequestActivated(QTreeWidgetItem* item)
{
    if (!item)
        return;
    const QString branch = item->data(0, RoleRequestBranch).toString();
    if (!branch.isEmpty())
        mergeBranch(branch);
    else
        bringRequest(item->data(0, RoleRequestId).toULongLong());
}

void TransactionLogView::onRequestContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = _requests->itemAt(pos);
    if (!item || !_doc)
        return;
    const QString branch = item->data(0, RoleRequestBranch).toString();
    const qulonglong id = item->data(0, RoleRequestId).toULongLong();
    QMenu menu(this);
    if (!branch.isEmpty()) {
        menu.addAction(tr("Merge..."), this, [this, branch]() { mergeBranch(branch); });
        // An import that stopped left the file it read held (sec 30.29):
        // read again, it goes on from the row it stopped at.
        if (item->data(0, RoleRequestStopped).toBool()) {
            bool held = false;
            for (const auto& f : _doc->sentFiles())
                held = held || f.branch == branch.toStdString();
            if (held) {
                const QString from = item->data(0, RoleRequestFrom).toString();
                menu.addAction(tr("Bring in again"), this,
                               [this, branch, from]() { bringAgain(branch, from); });
            }
        }
        menu.addAction(tr("Open in a document of its own"), this, [this, branch]() {
            try {
                App::Document::openFileBranch(_doc->getFileHistory().shared_from_this(),
                                              branch.toStdString(), 0, true);
            }
            catch (Base::Exception& e) {
                FC_ERR("open request " << branch.toStdString() << ": " << e.what());
                _status->setText(tr("Not opened -- the report view says why"));
            }
        });
        menu.addSeparator();
        menu.addAction(tr("Delete"), this, [this, branch]() {
            if (QMessageBox::question(this, tr("Delete a request"),
                                      tr("Delete branch %1, and with it what was brought "
                                         "in from the file?").arg(branch))
                    == QMessageBox::Yes)
                deleteRequest(branch);
        });
    }
    else {
        menu.addAction(tr("Bring in and merge..."), this, [this, id]() { bringRequest(id); });
        menu.addAction(tr("Bring in only"), this, [this, id]() { bringRequestOnly(id); });
        SceneRequest request;
        if (SceneRequests::find(id, request, _doc->getName())) {
            menu.addAction(tr("Save a copy as..."), this, [this, id, request]() {
                const QString path = FileDialog::getSaveFileName(
                    this, tr("Save a copy of the sent file"), request.name,
                    tr("FreeCAD document (*.FCStd)"));
                if (!path.isEmpty())
                    saveRequestAs(id, path);
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Delete, unread"), this, [this, id]() { dropRequest(id); });
        if (SceneRequests::list(_doc->getName()).size() > 1)
            menu.addAction(tr("Delete all, unread"), this, [this]() {
                if (QMessageBox::question(this, tr("Delete the sent files"),
                                          tr("Delete every file that was sent and has not "
                                             "been read?"))
                        == QMessageBox::Yes)
                    dropAllRequests();
            });
    }
    menu.exec(_requests->viewport()->mapToGlobal(pos));
}

void TransactionLogView::bringRequest(qulonglong id)
{
    bringSent(id, true);
}

void TransactionLogView::bringRequestOnly(qulonglong id)
{
    bringSent(id, false);
}

void TransactionLogView::bringSent(qulonglong id, bool merge)
{
    SceneRequest request;
    if (!_doc || !SceneRequests::find(id, request, _doc->getName()))
        return;
    if (!request.inLog) {
        // One of a document that keeps no log: a file of this process.
        // H7: the import's record names who sent it, and how that name is
        // known.
        const QString sender = QStringLiteral("%1 (%2)").arg(request.sender, request.senderKind);
        if (importFrom(request.path, QString(), sender, 0, merge))
            SceneRequests::drop(id, false, _doc->getName());
        return;
    }
    // The log holds it (sec 30.29). Which of its branches to bring is read
    // from the file, so its bytes go to one for the asking; the import
    // itself is of the row, which is what lets the log let the bytes go
    // once the branch is merged.
    const QString dir = QString::fromStdString(App::Application::getTempPath())
        + QStringLiteral("BrowserRequests");
    QDir().mkpath(dir);
    const QString path = SceneRequests::uniquePath(dir, request.name);
    try {
        _doc->writeSentFile(static_cast<int64_t>(id), path.toStdString());
    }
    catch (Base::Exception& e) {
        FC_ERR("bring in " << request.name.toStdString() << ": " << e.what());
        _status->setText(tr("%1 not brought in -- the report view says why").arg(request.name));
        return;
    }
    importFrom(path, QString(), QString(), id, merge);
    QFile::remove(path);
    scheduleRefresh();
}

void TransactionLogView::bringAgain(const QString& branch, const QString& from)
{
    if (!_doc)
        return;
    App::Document::SentFile sent;
    for (const auto& f : _doc->sentFiles()) {
        if (f.branch == branch.toStdString())
            sent = f;
    }
    if (!sent.seq) {
        _status->setText(tr("No sent file is held for branch %1").arg(branch));
        return;
    }
    const QString file = QString::fromStdString(sent.name);
    try {
        const auto result = _doc->importSentFile(sent.seq, from.toStdString());
        refresh();
        if (result.stoppedAt) {
            Base::Console().Warning("Bring in %s again: %s\n", sent.name.c_str(),
                                    result.reason.c_str());
            _status->setText(tr("%1: %2 more rows came to branch %3, then one could not be "
                                "applied -- the report view says why")
                                 .arg(file).arg(result.rows).arg(branch));
        }
        else if (result.rows) {
            _status->setText(tr("%1: %2 more rows came to branch %3, not merged")
                                 .arg(file).arg(result.rows).arg(branch));
        }
        else {
            _status->setText(tr("Nothing new in %1").arg(file));
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("bring in " << sent.name << " again: " << e.what());
        _status->setText(tr("%1 not brought in -- the report view says why").arg(file));
    }
    scheduleRefresh();
}

void TransactionLogView::saveRequestAs(qulonglong id, const QString& path)
{
    SceneRequest request;
    if (!_doc || path.isEmpty() || !SceneRequests::find(id, request, _doc->getName()))
        return;
    try {
        if (request.inLog)
            _doc->writeSentFile(static_cast<int64_t>(id), path.toStdString());
        else if (!QFile::copy(request.path, path))
            throw Base::RuntimeError("cannot copy the sent file");
        _status->setText(tr("%1 saved as %2, still unread here").arg(request.name, path));
    }
    catch (Base::Exception& e) {
        FC_ERR("save " << request.name.toStdString() << ": " << e.what());
        _status->setText(tr("%1 not saved -- the report view says why").arg(request.name));
    }
}

void TransactionLogView::dropRequest(qulonglong id)
{
    SceneRequest request;
    if (!_doc || !SceneRequests::find(id, request, _doc->getName()))
        return;
    SceneRequests::drop(id, false, _doc->getName());
    _status->setText(tr("%1 deleted, unread").arg(request.name));
}

void TransactionLogView::dropAllRequests()
{
    if (!_doc)
        return;
    int dropped = 0;
    // One at a time, and looked up again each time: dropping one writes a
    // record, and a row's number is not for keeps.
    for (int guard = 0; guard < 10000; ++guard) {
        const auto waiting = SceneRequests::list(_doc->getName());
        if (waiting.empty() || !SceneRequests::drop(waiting.front().id, false, _doc->getName()))
            break;
        ++dropped;
    }
    _status->setText(tr("%n sent file(s) deleted, unread", nullptr, dropped));
}

void TransactionLogView::deleteRequest(const QString& branch)
{
    if (!_doc || branch.isEmpty())
        return;
    try {
        _doc->deleteBranch(branch.toStdString());
        _previewed.erase(branch.toStdString());
        _status->setText(tr("Request %1 deleted").arg(branch));
    }
    catch (Base::Exception& e) {
        FC_ERR("delete request " << branch.toStdString() << ": " << e.what());
        _status->setText(tr("Not deleted -- the report view says why"));
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

void TransactionLogView::undoRows(const QString& seqs)
{
    if (!_doc)
        return;
    std::vector<int64_t> rows;
    for (const QString& part : seqs.split(QLatin1Char(','), Qt::SkipEmptyParts))
        rows.push_back(part.trimmed().toLongLong());
    if (rows.empty())
        return;
    try {
        if (!_doc->undoLogged(rows)) {
            _status->setText(
                rows.size() == 1
                    ? tr("Undo of row %1 refused -- the report view says why").arg(rows.front())
                    : tr("Undo of %1 rows refused -- the report view says why")
                          .arg(rows.size()));
        }
    }
    catch (Base::Exception& e) {
        FC_ERR("undo of row " << seqs.toStdString() << ": " << e.what());
    }
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
    // The rows selected, or this one where it is not among them.
    QList<QTreeWidgetItem*> rows = _transactions->selectedItems();
    if (!rows.contains(item))
        rows = {item};
    QStringList picked;
    bool undoable = _doc != nullptr;
    for (auto row : rows) {
        picked << QString::number(row->data(TxnSeq, Qt::UserRole).toLongLong());
        const QString kind = row->text(TxnKind);
        undoable = undoable
            && (kind == QLatin1String("user") || kind == QLatin1String("implicit")
                || kind == QLatin1String("undo") || kind == QLatin1String("redo")
                || kind == QLatin1String("pick"));
    }
    menu.addSeparator();
    // Sec 33.13: several rows selected are undone as one step.
    auto undoRow = menu.addAction(picked.size() == 1 ? tr("Undo row %1").arg(seq)
                                                     : tr("Undo %1 rows").arg(picked.size()));
    undoRow->setToolTip(picked.size() == 1
                            ? tr("A new transaction applying this row reversed; refused when "
                                 "anything since changed what it touched (sec 24.4)")
                            : tr("One new transaction applying these rows reversed, newest "
                                 "first; refused when anything else since changed what they "
                                 "touched (sec 33.13)"));
    undoRow->setEnabled(undoable);
    // Sec 31.12: the same rows applied here -- rows of another branch;
    // this branch's own are what undo and redo are for.
    bool mine = false;
    if (auto l = log()) {
        for (const auto& t : l->store().history(l->head()))
            mine = mine || picked.contains(QString::number(t.seq));
    }
    auto applyHere = menu.addAction(picked.size() == 1 ? tr("Apply row %1 here...").arg(seq)
                                                       : tr("Apply %1 rows here...").arg(picked.size()));
    applyHere->setToolTip(tr("What the row set, written to this branch as a step of its own; "
                             "its branch is not merged by it (sec 31.12)"));
    applyHere->setEnabled(_doc && !mine);
    auto branchHere = menu.addAction(tr("Branch from here..."));
    branchHere->setToolTip(tr("A new branch whose history ends at this row, switched to (sec 26)"));
    branchHere->setEnabled(_doc != nullptr);
    // Sec 31.21: a merge undone at the head, and nothing since, taken out
    // of the log -- asked when the menu opens, not kept: the rows are
    // written behind the document, and asking waits for them.
    App::Document::UndoneMerge undone;
    if (_doc) {
        try {
            undone = _doc->undoneMerge();
        }
        catch (Base::Exception& e) {
            FC_LOG("merge undone at the head: " << e.what());
        }
    }
    QAction* rollBack = nullptr;
    if (undone.seq > 0) {
        menu.addSeparator();
        rollBack = menu.addAction(
            tr("Roll back the merge of %1...").arg(QString::fromStdString(undone.branch)));
        rollBack->setToolTip(tr("The merge undone at the head leaves the log with its undo: "
                                "%1 is no longer merged, and merging it asks everything again "
                                "(sec 31.21)").arg(QString::fromStdString(undone.branch)));
    }
    auto chosen = menu.exec(global);
    if (chosen == branchHere) {
        createBranch(0, seq);
        return;
    }
    if (rollBack && chosen == rollBack) {
        rollBackMerge();
        return;
    }
    if (chosen == applyHere) {
        applyRows(picked.join(QLatin1Char(',')));
        return;
    }
    if (chosen == undoRow) {
        undoRows(picked.join(QLatin1Char(',')));
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

namespace {

/// The versions a squash to `to` may start from (docs/TransactionLog.md
/// sec 16.7): those behind it on its own history, oldest first.
std::vector<App::LogVersion> versionsBehind(App::TransactionStore& store,
                                            const App::LogVersion& to)
{
    std::vector<App::LogVersion> behind;
    const auto chain = store.chain(to.seq);
    if (chain.empty())
        return behind;
    std::set<int64_t> points;
    for (const auto& t : chain) {
        points.insert(t.seq);
        points.insert(t.parent);
    }
    // A version taken at no row is behind only the history that starts
    // from it: its own branch's, or that of a branch made from it.
    App::LogBranch first;
    store.getBranch(chain.front().branch, first);
    for (const auto& v : store.versions()) {
        if (v.seq >= to.seq || !points.count(v.seq))
            continue;
        if (v.seq == 0 && v.branch != first.id && v.num != first.fromVersion)
            continue;
        behind.push_back(v);
    }
    return behind;
}

} // namespace

void TransactionLogView::squashTo(qlonglong version)
{
    // docs/TransactionLog.md sec 16.7: the rows between an older version
    // and this one folded into one row, the older one chosen from those
    // behind it. What the log refuses is said in the status line.
    auto l = log();
    if (!l || !_doc)
        return;
    try {
        auto& store = l->store();
        App::LogVersion to;
        if (!store.getVersion(version, to))
            return;
        const auto behind = versionsBehind(store, to);
        if (behind.empty()) {
            _status->setText(tr("No version behind version %1 to squash from").arg(version));
            return;
        }
        QStringList names;
        for (auto it = behind.rbegin(); it != behind.rend(); ++it) {
            names << (it->name.empty()
                          ? tr("Version %1").arg(it->num)
                          : tr("Version %1 (%2)").arg(it->num).arg(QString::fromStdString(it->name)));
        }
        bool ok = false;
        const QString chosen =
            QInputDialog::getItem(this, tr("Squash versions"),
                                  tr("Squash to version %1 from:").arg(version), names, 0, false, &ok);
        const int index = static_cast<int>(names.indexOf(chosen));
        if (!ok || index < 0)
            return;
        const App::LogVersion& from = behind[behind.size() - 1 - static_cast<size_t>(index)];
        const size_t rows = store.chain(to.seq, from.seq + 1).size();
        if (QMessageBox::question(this, tr("Squash versions"),
                                  tr("Fold the %1 rows from version %2 to version %3 into one? "
                                     "The unnamed versions between them go. This cannot be undone.")
                                      .arg(rows).arg(from.num).arg(version))
                != QMessageBox::Yes)
            return;
        const int64_t first = from.num;
        const size_t folded = _doc->squashVersions(first, version);
        _status->setText(tr("Squashed %1 rows from version %2 into row %3")
                             .arg(folded).arg(first).arg(to.seq));
    }
    catch (Base::Exception& e) {
        _status->setText(tr("Not squashed: %1").arg(QString::fromUtf8(e.what())));
    }
}

void TransactionLogView::keepValue(qlonglong seq, qlonglong cid, const QString& prop, bool keep)
{
    if (!_doc)
        return;
    try {
        const auto refs = _doc->keepLogValue(static_cast<long>(cid), prop.toStdString(), seq, keep);
        if (refs.empty())
            _status->setText(keep ? tr("Nothing kept: the log holds no such value")
                                  : tr("Nothing let go: no such value was kept"));
        else
            _status->setText(keep ? tr("%n value(s) kept: a save writes them into the file", "",
                                       static_cast<int>(refs.size()))
                                  : tr("%n value(s) let go", "", static_cast<int>(refs.size())));
    }
    catch (Base::Exception& e) {
        FC_ERR("keep a value: " << e.what());
    }
    keptChanged();
}

void TransactionLogView::keepObject(qlonglong cid, bool keep)
{
    if (!_doc)
        return;
    try {
        _doc->keepLogValuesOf(static_cast<long>(cid), keep);
        auto obj = _doc->getObjectByID(static_cast<long>(cid));
        const QString name = obj ? QString::fromUtf8(obj->Label.getValue())
                                 : QStringLiteral("obj:%1").arg(cid);
        _status->setText(keep ? tr("Every value of %1 is kept: a save writes them into the file")
                                    .arg(name)
                              : tr("The values of %1 are no longer kept").arg(name));
    }
    catch (Base::Exception& e) {
        FC_ERR("keep an object's values: " << e.what());
    }
    keptChanged();
}

void TransactionLogView::keepSelectedAt(qlonglong seq, bool keep)
{
    if (!_doc)
        return;
    const auto objects = Selection().getObjectsOfType(App::DocumentObject::getClassTypeId(),
                                                      _doc->getName());
    if (objects.empty()) {
        _status->setText(tr("Nothing is selected: select the objects to keep as they are there"));
        return;
    }
    size_t values = 0;
    try {
        for (auto obj : objects)
            values += _doc->keepLogValue(obj->getID(), std::string(), seq, keep).size();
    }
    catch (Base::Exception& e) {
        FC_ERR("keep the selected objects: " << e.what());
    }
    _status->setText(keep ? tr("%1 values of %2 objects kept as they are at row %3")
                                .arg(values).arg(objects.size()).arg(seq)
                          : tr("%1 values of %2 objects let go").arg(values).arg(objects.size()));
    refreshKept();
}

void TransactionLogView::releaseValue(const QString& ref)
{
    if (!_doc)
        return;
    try {
        _status->setText(_doc->releaseLogValue(ref.toStdString())
                             ? tr("%n value(s) let go", "", 1)
                             : tr("Nothing let go: no such value was kept"));
    }
    catch (Base::Exception& e) {
        FC_ERR("let go of a value: " << e.what());
    }
    keptChanged();
}

void TransactionLogView::onOpContextMenu(const QPoint& pos)
{
    auto item = _ops->itemAt(pos);
    auto rows = _transactions->selectedItems();
    auto l = log();
    if (!item || !_doc || !l || rows.size() != 1)
        return;
    const qlonglong cid = item->data(OpContainer, Qt::UserRole).toLongLong();
    const QString after = item->data(OpAfter, Qt::UserRole).toString();
    if (!cid || after.isEmpty())
        return;
    const qlonglong seq = rows.front()->data(TxnSeq, Qt::UserRole).toLongLong();
    bool valueKept = false;
    bool objectKept = false;
    try {
        for (const auto& kept : l->store().keptValues())
            valueKept = valueKept || kept.hash == after.toStdString();
        for (long id : l->store().keptObjects())
            objectKept = objectKept || id == cid;
    }
    catch (Base::Exception&) {
    }
    QMenu menu(this);
    auto value = menu.addAction(tr("Keep this value in the file"));
    value->setCheckable(true);
    value->setChecked(valueKept);
    value->setToolTip(tr("A value a recompute wrote is left out of the history a save writes, "
                         "unless a version kept reaches it (sec 32.11)"));
    auto object = menu.addAction(tr("Keep every value of %1 in the file").arg(item->text(OpContainer)));
    object->setCheckable(true);
    object->setChecked(objectKept);
    object->setToolTip(tr("Each shape its recomputes made, and will make: the file grows by "
                          "each"));
    const QString prop = item->text(OpProp);
    auto chosen = menu.exec(_ops->viewport()->mapToGlobal(pos));
    if (chosen == value)
        keepValue(seq, cid, prop, !valueKept);
    else if (chosen == object)
        keepObject(cid, !objectKept);
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
    auto open = menu.addAction(tr("Open version %1").arg(num));
    open->setToolTip(tr("The version as a document of its own, beside this one, on the "
                        "file's one log; its first change makes it a branch (sec 27.7)"));
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
    auto squash = menu.addAction(tr("Squash to version %1 from...").arg(num));
    squash->setToolTip(tr("Fold the rows between an older version and this one into one "
                          "row of their net change; both versions stay (sec 16.7)"));
    bool anyBehind = false;
    try {
        App::LogVersion to;
        if (auto l = log())
            anyBehind = l->store().getVersion(num, to) && !versionsBehind(l->store(), to).empty();
    }
    catch (Base::Exception&) {
    }
    squash->setEnabled(anyBehind);
    menu.addSeparator();
    auto keepHere = menu.addAction(tr("Keep the selected objects as they are at version %1")
                                       .arg(num));
    keepHere->setToolTip(tr("What a recompute made of them there -- their shapes -- is written "
                            "into the file by a save, though the version is not kept "
                            "(sec 32.11)"));
    auto letGoHere = menu.addAction(tr("Let go of the selected objects as they are at "
                                       "version %1").arg(num));
    auto chosen = menu.exec(_versions->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == keepHere || chosen == letGoHere) {
        keepSelectedAt(item->text(VerSeq).toLongLong(), chosen == keepHere);
        return;
    }
    if (chosen == squash) {
        squashTo(num);
        return;
    }
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
        if (chosen == open) {
            App::Document* opened = doc->openVersion(num);
            if (auto gdoc = Application::Instance->getDocument(opened))
                Application::Instance->setActiveDocument(gdoc);
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
        // A branch made from another (sec 30.3 S.a): which, and what
        // stands between the two until one is merged into the other.
        if (branch.target) {
            const auto state = _doc->branchState();
            if (!state.target.empty()) {
                modeText += QStringLiteral(", ")
                          + tr("from %1").arg(QString::fromStdString(state.target));
                if (state.ahead)
                    modeText += QStringLiteral(", ")
                              + tr("%1 ahead").arg(static_cast<qulonglong>(state.ahead));
                if (state.behind)
                    modeText += QStringLiteral(", ")
                              + tr("%1 behind").arg(static_cast<qulonglong>(state.behind));
            }
        }
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
