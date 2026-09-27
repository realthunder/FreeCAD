/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer                                    *
 *                                   <jrheinlaender@users.sourceforge.net> *
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
# include <sstream>
# include <QAction>
# include <QApplication>
# include <QRegularExpression>
# include <QRegularExpressionMatch>
# include <QTextStream>
# include <QListWidget>
# include <QMessageBox>
# include <QPointer>
# include <QScrollBar>
# include <QTimer>
# include <Precision.hxx>
#endif

#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <boost/algorithm/string/predicate.hpp>

#include <Base/Tools.h>
#include <Base/Console.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <App/MappedElement.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/Selection.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/MetaTypes.h>
#include <Gui/PrefWidgets.h>
#include <Gui/Tools.h>

#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/FeatureOffset.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "TaskSketchBasedParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"

FC_LOG_LEVEL_INIT("PartDesignGui",true,true)

using namespace PartDesignGui;
using namespace Gui;

namespace {
struct SubInfo
{
    App::SubObjectT objT;
    std::vector<std::string> subs;
    SubInfo(){}
    SubInfo(const App::SubObjectT &objT,
            const std::vector<std::string> &subs)
        :objT(objT)
        ,subs(subs)
    {}
};
} //anonymous namespace
Q_DECLARE_METATYPE(SubInfo)

/* TRANSLATOR PartDesignGui::TaskSketchBasedParameters */

namespace PartDesignGui {

/** The table of LinkSubWidget, its object column frozen
 *
 * The frozen column is Qt's: a second view laid over the table's left edge,
 * sharing its model, selection and delegate, showing only column 0. It keeps
 * its width, row heights and vertical scroll in step with the table.
 */
class LinkSubTable : public QTableWidget
{
public:
    explicit LinkSubTable(QWidget *parent)
        : QTableWidget(parent)
        , frozen(new QTableView(this))
    {
        for (QTableView *view : {static_cast<QTableView*>(this), frozen}) {
            view->horizontalHeader()->hide();
            view->verticalHeader()->hide();
            view->verticalHeader()->setMinimumSectionSize(1);
            view->setShowGrid(false);
            view->setWordWrap(false);
            view->setMouseTracking(true);
            view->setSelectionMode(QAbstractItemView::SingleSelection);
            view->setEditTriggers(QAbstractItemView::DoubleClicked
                                  | QAbstractItemView::EditKeyPressed);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        frozen->setModel(model());
        frozen->setSelectionModel(selectionModel());
        frozen->setFocusProxy(this);
        frozen->setFrameShape(QFrame::NoFrame);
        frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        viewport()->stackUnder(frozen);

        connect(horizontalHeader(), &QHeaderView::sectionResized,
                this, [this](int logical, int, int size) {
            if (logical == 0) {
                frozen->setColumnWidth(0, size);
                updateFrozenGeometry();
            }
        });
        connect(verticalHeader(), &QHeaderView::sectionResized,
                this, [this](int logical, int, int size) {
            frozen->setRowHeight(logical, size);
        });
        connect(frozen->verticalScrollBar(), &QAbstractSlider::valueChanged,
                verticalScrollBar(), &QAbstractSlider::setValue);
        connect(verticalScrollBar(), &QAbstractSlider::valueChanged,
                frozen->verticalScrollBar(), &QAbstractSlider::setValue);
    }

    QTableView *frozenView() const
    {
        return frozen;
    }

    /// Fit the columns to their text, and show only the object column in the
    /// frozen view. Call after the rows are rebuilt.
    void fitColumns(int padding)
    {
        resizeColumnsToContents();
        for (int col = 0; col < columnCount(); ++col) {
            setColumnWidth(col, columnWidth(col) + padding);
            frozen->setColumnHidden(col, col != 0);
        }
        if (columnCount())
            frozen->setColumnWidth(0, columnWidth(0));
        for (int row = 0; row < rowCount(); ++row)
            frozen->setRowHeight(row, rowHeight(row));
        updateFrozenGeometry();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTableWidget::resizeEvent(event);
        updateFrozenGeometry();
    }

    QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override
    {
        QModelIndex current = QTableWidget::moveCursor(cursorAction, modifiers);
        // Keep the element the cursor lands on out from under the frozen column
        if (cursorAction == MoveLeft && current.column() > 0
                && visualRect(current).topLeft().x() < frozen->columnWidth(0)) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                    + visualRect(current).topLeft().x() - frozen->columnWidth(0));
        }
        return current;
    }

    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override
    {
        if (index.column() > 0) {
            QTableWidget::scrollTo(index, hint);
            return;
        }
        // The object column never scrolls sideways; only bring its row in
        int value = horizontalScrollBar()->value();
        QTableWidget::scrollTo(index, hint);
        horizontalScrollBar()->setValue(value);
    }

private:
    void updateFrozenGeometry()
    {
        QRect rect = viewport()->geometry();
        frozen->setGeometry(rect.x(), rect.y(),
                            columnCount() ? columnWidth(0) : 0, rect.height());
    }

private:
    QTableView *frozen;
};

} // namespace PartDesignGui

namespace {

using LinkRows = std::vector<std::pair<App::DocumentObject*, std::vector<std::string>>>;

void writeLinkRows(App::PropertyLinkBase &prop, LinkRows &&rows)
{
    if (auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkSub>(&prop)) {
        if (rows.empty()) {
            propLink->setValue(nullptr);
            return;
        }
        auto &row = rows.front();
        if (row.second.empty())
            row.second.emplace_back();
        propLink->setValue(row.first, std::move(row.second));
    }
    else if (auto propList = Base::freecad_dynamic_cast<App::PropertyLinkSubList>(&prop)) {
        propList->setSubListValues(rows);
    }
}

/// Merge a picked (and imported) element into the rows: an element of a
/// listed object joins its row, another object starts one. A whole object
/// does not replace the elements already chosen of it; an element replaces
/// the whole object.
bool mergeLinkRow(LinkRows &rows, const App::SubObjectT &objT)
{
    auto obj = objT.getSubObject();
    if (!obj)
        return false;
    std::string element = objT.getOldElementName();
    auto it = std::find_if(rows.begin(), rows.end(),
                           [obj](const auto &row) { return row.first == obj; });
    if (it == rows.end()) {
        rows.emplace_back(obj, std::vector<std::string>());
        if (element.size())
            rows.back().second.push_back(std::move(element));
        return true;
    }
    if (element.empty())
        return false;
    auto &subs = it->second;
    if (std::find(subs.begin(), subs.end(), element) != subs.end())
        return false;
    subs.push_back(std::move(element));
    return true;
}

} // anonymous namespace

LinkSubWidgetDelegate::LinkSubWidgetDelegate(QObject *parent) : QItemDelegate(parent)
{
}

QWidget *LinkSubWidgetDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */,
                                             const QModelIndex & index) const
{
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return nullptr;
    App::DocumentObject *obj;
    auto prop = owner->getProperty(&obj);
    if (!prop)
        return nullptr;
    App::ObjectIdentifier path(*prop);
    bool hasExpression = !!obj->getExpression(path).expression;
    if (index.column() != 0 || owner->multiObject) {
        if (hasExpression)
            return nullptr;
        return new QLineEdit(parent);
    }
    auto editor = new Gui::ExpLineEdit(parent);
    editor->bind(path);
    return editor;
}

void LinkSubWidgetDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
        return;
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return;
    auto rows = owner->getRows();
    if (index.row() >= (int)rows.size() || !rows[index.row()].first)
        return;
    const auto &row = rows[index.row()];
    if (index.column() == 0)
        lineEdit->setText(QString::fromUtf8(row.first->getNameInDocument()));
    else if (index.column() - 1 < (int)row.second.size())
        lineEdit->setText(QString::fromUtf8(row.second[index.column()-1].c_str()));
}

void LinkSubWidgetDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                        const QModelIndex &index) const
{
    (void)model;
    auto lineEdit = qobject_cast<QLineEdit*>(editor);
    if (!lineEdit)
        return;
    auto owner = qobject_cast<LinkSubWidget*>(this->parent());
    if (!owner)
        return;
    App::DocumentObject *obj;
    auto prop = owner->getProperty(&obj);
    if (!prop)
        return;
    App::ObjectIdentifier path(*prop);
    if (obj->getExpression(path).expression)
        return;

    auto rows = owner->getRows();
    if (index.row() >= (int)rows.size())
        return;
    auto &row = rows[index.row()];
    if (index.column() == 0) {
        auto newLink = obj->getDocument()->getObject(lineEdit->text().toUtf8().constData());
        if (!newLink) {
            QMessageBox::critical(Gui::getMainWindow(),
                    QObject::tr("Error"), QObject::tr("Object not found"));
            return;
        }
        row.first = newLink;
    } else if (index.column() - 1 < (int)row.second.size())
        row.second[index.column()-1] = lineEdit->text().toUtf8().constData();
    else
        return;

    if (!row.first)
        return;
    // Write once the edit is over: setRows() rebuilds the table, which would
    // pull the editor from under the view still committing it
    QPointer<LinkSubWidget> ownerPtr(owner);
    QTimer::singleShot(0, owner, [ownerPtr, rows = std::move(rows)]() mutable {
        if (ownerPtr)
            ownerPtr->setRows(std::move(rows));
    });
}

LinkSubWidget::LinkSubWidget(TaskSketchBasedParameters *parent,
                             const QString &title,
                             App::PropertyLinkSub &prop,
                             bool singleElement)
    :QWidget(parent)
    ,parentTask(parent)
    ,selectionMode(TaskSketchBasedParameters::SelectionMode::refAdd)
    ,linkProp(&prop)
    ,singleElement(singleElement)
{
    init(title);
}

LinkSubWidget::LinkSubWidget(TaskSketchBasedParameters *parent,
                             const QString &title,
                             App::PropertyLinkSubList &prop)
    :QWidget(parent)
    ,parentTask(parent)
    ,selectionMode(TaskSketchBasedParameters::SelectionMode::refAdd)
    ,linkProp(&prop)
    ,multiObject(true)
{
    init(title);
}

void LinkSubWidget::init(const QString &title)
{
    selectionConf.setFlag(AllowSelection::EDGE);
    selectionConf.setFlag(AllowSelection::FACE);
    selectionConf.setFlag(AllowSelection::PLANAR, false);
    selectionConf.setFlag(AllowSelection::WHOLE);
    selectionConf.setFlag(AllowSelection::WIRE);
    selectionConf.setFlag(AllowSelection::POINT);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setSpacing(2);
    setLayout(hlayout);
    hlayout->setContentsMargins(0,0,0,0);
    setContentsMargins(0,0,0,0);
    button = new QPushButton(this);
    hlayout->addWidget(button, 0, Qt::AlignTop);

    table = new LinkSubTable(this);
    hlayout->addWidget(table);

    clearButton = new QPushButton(this);
    hlayout->addWidget(clearButton, 0, Qt::AlignTop);

    button->setText(title);
    button->setCheckable(true);
    QObject::connect(button, &QPushButton::clicked, [this](bool checked) {onButton(checked);});
    if (button->toolTip().isEmpty())
        button->setToolTip(tr("Click to enter selection mode"));

    QAction* remove = new QAction(tr("Remove"), this);
    remove->setShortcut(Gui::QtTools::deleteKeySequence());
    QObject::connect(remove, &QAction::triggered, [this](){onDelete();});
    for (QTableView *view : {static_cast<QTableView*>(table), table->frozenView()}) {
        // A delegate each: one shared would send each view the other's
        // commits ("commitData called with an editor that does not belong
        // to this view")
        view->setItemDelegate(new LinkSubWidgetDelegate(this));
        QObject::connect(view, &QAbstractItemView::entered,
                         [this](const QModelIndex &index) {onItemEntered(index);});
        view->installEventFilter(this);
        view->addAction(remove);
        view->setContextMenuPolicy(Qt::ActionsContextMenu);
    }
    table->horizontalScrollBar()->installEventFilter(this);

    clearButton->setIcon(Gui::BitmapFactory().pixmap("edit-cleartext"));
    if (clearButton->toolTip().isEmpty())
        clearButton->setToolTip(tr("Temporary clear link references for new selection"));
    QObject::connect(clearButton, &QPushButton::clicked, [this]() {onClear();});

    if (auto prop = getProperty()) {
        conn = prop->signalChanged.connect([this](const App::Property &) {toggleShowOnTop();});
    }

    connModeChange = parentTask->signalSelectionModeChanged.connect([this]() {
        if (parentTask->getSelectionMode() == selectionMode) {
            toggleShowOnTop(true);
            button->setChecked(true);
        } else if (button->isChecked()) {
            refresh();
            disableShowOnTop();
            button->setChecked(false);
        }
    });

    updateHeight();
}

App::PropertyLinkBase *LinkSubWidget::getProperty(App::DocumentObject **pObj) const
{
    auto obj = linkProp.getObject();
    if (!obj)
        return nullptr;
    if (pObj)
        *pObj = obj;
    auto prop = obj->getPropertyByName(linkProp.getPropertyName().c_str());
    if (multiObject)
        return Base::freecad_dynamic_cast<App::PropertyLinkSubList>(prop);
    return Base::freecad_dynamic_cast<App::PropertyLinkSub>(prop);
}

void LinkSubWidget::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (parentTask->getSelectionMode() != selectionMode)
        return;
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        App::SubObjectT ref(msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object);
        if (pickFilter && !pickFilter(msg, ref))
            return;
        if (addLink(ref) && singleElement)
            parentTask->exitSelectionMode();
    }
}

void LinkSubWidget::setSelectionConfig(const AllowSelectionFlags &conf)
{
    selectionConf = conf;
}

void LinkSubWidget::startSelection()
{
    if (parentTask->getSelectionMode() != selectionMode)
        parentTask->onSelectReference(button, selectionMode, selectionConf);
}

void LinkSubWidget::setTitle(const QString &title)
{
    button->setText(title);
}

LinkSubWidget::LinkRows LinkSubWidget::getRows() const
{
    LinkRows rows;
    for (int row = 0; row < table->rowCount(); ++row) {
        auto item = table->item(row, 0);
        if (!item)
            continue;
        rows.emplace_back(qvariant_cast<App::SubObjectT>(item->data(Qt::UserRole)).getSubObject(),
                          std::vector<std::string>());
        for (int col = 1; col < table->columnCount(); ++col) {
            item = table->item(row, col);
            if (!item || !(item->flags() & Qt::ItemIsEnabled))
                break;
            rows.back().second.emplace_back(item->text().toUtf8().constData());
        }
    }
    return rows;
}

bool LinkSubWidget::setRows(LinkRows &&rows)
{
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    if (!prop)
        return false;
    try {
        parentTask->setupTransaction();
        writeLinkRows(*prop, std::move(rows));
        parentTask->recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
    refresh();
    return true;
}

void LinkSubWidget::onDelete()
{
    QModelIndex index = table->currentIndex();
    if (!index.isValid())
        return;
    auto rows = getRows();
    if (index.row() >= (int)rows.size())
        return;
    if (index.column() == 0) {
        if (!multiObject) {
            onClear();
            return;
        }
        rows.erase(rows.begin() + index.row());
    } else {
        auto &subs = rows[index.row()].second;
        if (index.column() - 1 >= (int)subs.size())
            return;
        subs.erase(subs.begin() + index.column() - 1);
    }
    setRows(std::move(rows));
}

void LinkSubWidget::onButton(bool checked)
{
    if (checked) {
        if (parentTask->getSelectionMode() == TaskSketchBasedParameters::SelectionMode::none) {
            auto sels = Gui::Selection().getSelectionT("*", Gui::ResolveMode::NoResolve);
            if (sels.size()) {
                if (setLinks(sels)) {
                    button->setChecked(false);
                    return;
                }
            }
        }
        parentTask->onSelectReference(button, selectionMode, selectionConf);
    } else {
        parentTask->exitSelectionMode();
        refresh();
    }
}

void LinkSubWidget::toggleShowOnTop(bool init)
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, linkProp.getPropertyName().c_str(), init);
}

void LinkSubWidget::disableShowOnTop()
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, nullptr);
}

void LinkSubWidget::onClear()
{
    Gui::Selection().clearSelection();
    table->setRowCount(0);
    table->setColumnCount(0);
    table->fitColumns(0);
    updateHeight();
    if (parentTask->getSelectionMode() != selectionMode)
        onButton(true);
}

void LinkSubWidget::updateHeight()
{
    int frame = table->frameWidth();
    int rowHeight = std::max(button->sizeHint().height(),
                             table->fontMetrics().height() + 2);
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    table->frozenView()->verticalHeader()->setDefaultSectionSize(rowHeight);
    int rows = std::clamp(table->rowCount(), 1, maxVisibleRows);
    auto scrollbar = table->horizontalScrollBar();
    int height = rows * rowHeight + 2 * frame
        + (scrollbar->isVisible() ? scrollbar->sizeHint().height() : 0);
    table->setMinimumHeight(height);
    table->setMaximumHeight(height);
}

void LinkSubWidget::onItemEntered(const QModelIndex &index)
{
    auto rows = getRows();
    if (!index.isValid() || index.row() >= (int)rows.size())
        return;
    const auto &row = rows[index.row()];
    if (!row.first)
        return;
    if (index.column() == 0)
        PartDesignGui::highlightObjectOnTop(row.first);
    else if (index.column() - 1 < (int)row.second.size())
        PartDesignGui::highlightObjectOnTop(
                App::SubObjectT(row.first, row.second[index.column()-1].c_str()));
}

bool LinkSubWidget::eventFilter(QObject *o, QEvent *ev)
{
    bool isView = o == table || o == table->frozenView();
    switch(ev->type()) {
    case QEvent::Show:
    case QEvent::Hide:
        if (o == table->horizontalScrollBar())
            updateHeight();
        break;
    case QEvent::Leave:
        if (isView)
            Gui::Selection().rmvPreselect();
        break;
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        QKeyEvent * kevent = static_cast<QKeyEvent*>(ev);
        if (isView && kevent->modifiers() == Qt::NoModifier) {
            if (kevent->matches(Gui::QtTools::deleteKeySequence())) {
                kevent->accept();
                if (ev->type() == QEvent::KeyPress)
                    onDelete();
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void LinkSubWidget::addRow(App::DocumentObject *link,
                           const std::vector<std::string> &subs,
                           bool hasExpression)
{
    auto linkColor = QApplication::palette().color(QPalette::Link);
    auto makeItem = [&](const QString &text) {
        auto item = new QTableWidgetItem(text);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setTextAlignment(Qt::AlignCenter);
        if (hasExpression)
            item->setForeground(linkColor);
        return item;
    };

    int row = table->rowCount();
    table->setRowCount(row + 1);
    if (table->columnCount() < (int)subs.size() + 1)
        table->setColumnCount((int)subs.size() + 1);

    App::SubObjectT linkT(link);
    App::DocumentObject *owner = linkProp.getObject();
    auto item = makeItem(QString::fromUtf8(linkT.getObjectFullName(
                    owner ? owner->getDocument()->getName() : nullptr).c_str()));
    item->setData(Qt::UserRole, QVariant::fromValue(linkT));
    table->setItem(row, 0, item);
    int col = 1;
    for (const auto &sub : subs)
        table->setItem(row, col++, makeItem(QString::fromUtf8(sub.c_str())));
}

void LinkSubWidget::refresh()
{
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    if (!prop) {
        linkInited = true;
        return;
    }

    // Flatten the property: one entry per element, or per whole object
    std::vector<App::DocumentObject*> links;
    std::vector<std::string> subs;
    std::vector<App::PropertyLinkBase::ShadowSub> shadows;
    if (auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkSub>(prop)) {
        if (auto link = propLink->getValue()) {
            subs = propLink->getSubValues(false);
            shadows = propLink->getShadowSubs();
            if (subs.empty())
                subs.emplace_back();
            links.assign(subs.size(), link);
        }
    }
    else if (auto propList = Base::freecad_dynamic_cast<App::PropertyLinkSubList>(prop)) {
        links = propList->getValues();
        subs = propList->getSubValues(false);
        shadows = propList->getShadowSubs();
    }
    shadows.resize(subs.size());

    // On first show, a reference to an element that is gone is replaced by
    // the element it most likely became
    bool touched = false;
    if (!linkInited) {
        std::set<std::pair<App::DocumentObject*, std::string>> subSet;
        for (std::size_t i = 0; i < subs.size(); ++i) {
            subSet.emplace(links[i], shadows[i].first);
            subSet.emplace(links[i], shadows[i].second);
        }
        std::string indexedName;
        for (std::size_t i = 0; i < subs.size(); ++i) {
            if (!App::GeoFeature::hasMissingElement(subs[i].c_str()) || !links[i])
                continue;
            auto related = Part::Feature::getRelatedElements(links[i], shadows[i].first.c_str());
            if (related.empty())
                continue;
            indexedName.clear();
            related.front().index.appendToStringBuffer(indexedName);
            FC_WARN("guess element reference in " << prop->getFullName()
                    << ": " << subs[i] << " -> " << indexedName);
            if (subSet.emplace(links[i], indexedName).second)
                subs[i] = indexedName;
            else
                subs[i].clear();
            touched = true;
        }
    }

    LinkRows rows;
    for (std::size_t i = 0; i < subs.size(); ++i) {
        if (!links[i])
            continue;
        auto it = std::find_if(rows.begin(), rows.end(),
                               [&](const auto &row) { return row.first == links[i]; });
        if (it == rows.end()) {
            rows.emplace_back(links[i], std::vector<std::string>());
            it = rows.end() - 1;
        }
        if (subs[i].size())
            it->second.push_back(subs[i]);
    }

    {
        QSignalBlocker guard(table);
        table->setRowCount(0);
        table->setColumnCount(0);
        App::ObjectIdentifier path(*prop);
        bool hasExpression = !!obj->getExpression(path).expression;
        for (const auto &row : rows)
            addRow(row.first, row.second, hasExpression);
        // Cells past the end of a shorter row are neither picked nor edited
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int col = 1; col < table->columnCount(); ++col) {
                if (!table->item(row, col)) {
                    auto item = new QTableWidgetItem;
                    item->setFlags(Qt::NoItemFlags);
                    table->setItem(row, col, item);
                }
            }
        }
        table->fitColumns(10);
    }
    updateHeight();

    if (touched) {
        linkInited = true;
        setRows(std::move(rows));
    }
    linkInited = true;
}

bool LinkSubWidget::setLinks(const std::vector<App::SubObjectT> &objs)
{
    App::DocumentObject *obj;
    auto prop = getProperty(&obj);
    if (!prop || objs.empty())
        return false;
    try {
        if (multiObject) {
            LinkRows rows;
            for (const auto &objT : objs)
                mergeLinkRow(rows, PartDesignGui::importExternalObject(objT, false));
            if (rows.empty())
                return false;
            return setLinkRows(std::move(rows));
        }
        parentTask->setupTransaction();
        auto propLink = static_cast<App::PropertyLinkSub*>(prop);
        if (singleElement) {
            // A reference in the edited feature's own body is linked as it
            // is; importExternalElement() would bind even that, as it binds
            // anything picked with an element
            App::SubObjectT ref = objs.front();
            auto sobj = ref.getSubObject();
            auto body = PartDesign::Body::findBodyOf(obj);
            if (sobj && body && PartDesign::Body::findBodyOf(sobj) == body)
                ref = App::SubObjectT(sobj, ref.getOldElementName().c_str());
            else
                ref = PartDesignGui::importExternalElement(ref);
            propLink->setValue(ref.getObject(), {ref.getSubName()});
        } else if (!PartDesignGui::importExternalElements(*propLink, objs))
            return false;
        parentTask->recomputeFeature();
        if (auto o = propLink->getValue()) {
            if (hideLinked && !o->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
                o->Visibility.setValue(false);
        }
        refresh();
        return true;
    } catch (Base::Exception &e) {
        e.ReportException();
    }
    return false;
}

bool LinkSubWidget::setLinkRows(LinkRows &&rows)
{
    if (hideLinked) {
        for (const auto &row : rows) {
            if (row.first && !row.first->isDerivedFrom(PartDesign::Feature::getClassTypeId()))
                row.first->Visibility.setValue(false);
        }
    }
    return setRows(std::move(rows));
}

bool LinkSubWidget::addLink(const App::SubObjectT &objT)
{
    if (!multiObject) {
        App::DocumentObject *obj;
        auto prop = static_cast<App::PropertyLinkSub*>(getProperty(&obj));
        if (!prop)
            return false;
        std::vector<App::SubObjectT> links;
        if (table->rowCount() == 0 || !prop->getValue() || singleElement)
            links.push_back(objT);
        else {
            if (auto linked = prop->getValue()) {
                if (prop->getSubValues().empty()) {
                    links.emplace_back(linked, "");
                }
                else {
                    for (const auto &sub : prop->getSubValues()) {
                        if (!App::GeoFeature::hasMissingElement(sub.c_str())) {
                            links.emplace_back(linked, sub.c_str());
                        }
                    }
                }
            }
            links.push_back(objT);
        }
        if (!setLinks(links))
            return false;
        selectLast(nullptr);
        return true;
    }

    // Several objects: the current rows, less the elements that are gone, and
    // the pick
    LinkRows rows;
    if (table->rowCount()) {
        rows = getRows();
        for (auto &row : rows) {
            auto &subs = row.second;
            subs.erase(std::remove_if(subs.begin(), subs.end(), [](const std::string &sub) {
                return App::GeoFeature::hasMissingElement(sub.c_str());
            }), subs.end());
        }
    }
    App::SubObjectT imported;
    try {
        imported = PartDesignGui::importExternalObject(objT, false);
    } catch (Base::Exception &e) {
        e.ReportException();
        return false;
    }
    if (!mergeLinkRow(rows, imported))
        return false;
    if (!setLinkRows(std::move(rows)))
        return false;
    selectLast(imported.getSubObject());
    return true;
}

void LinkSubWidget::selectLast(App::DocumentObject *link)
{
    // The last element of the row of the given object, or of the last row
    for (int row = table->rowCount() - 1; row >= 0; --row) {
        auto item = table->item(row, 0);
        if (!item)
            continue;
        if (link && qvariant_cast<App::SubObjectT>(item->data(Qt::UserRole)).getSubObject() != link)
            continue;
        int col = table->columnCount() - 1;
        for (; col > 0; --col) {
            auto cell = table->item(row, col);
            if (cell && (cell->flags() & Qt::ItemIsEnabled))
                break;
        }
        table->setCurrentCell(row, col);
        return;
    }
}

void LinkSubWidget::setSelectionMode(TaskSketchBasedParameters::SelectionMode mode)
{
    selectionMode = mode;
}

//////////////////////////////////////////////////////////////////////////////////////

LinkSubListWidget::LinkSubListWidget(TaskSketchBasedParameters *parent,
                                     const QString &groupTitle,
                                     const QString &title,
                                     App::PropertyLinkSubList &prop)
    :QGroupBox(groupTitle, parent)
    ,parentTask(parent)
    ,selectionMode(TaskSketchBasedParameters::SelectionMode::refSection)
    ,linkProp(&prop)
{
    selectionConf.setFlag(AllowSelection::EDGE);
    selectionConf.setFlag(AllowSelection::FACE);
    selectionConf.setFlag(AllowSelection::PLANAR, false);
    selectionConf.setFlag(AllowSelection::WHOLE);
    selectionConf.setFlag(AllowSelection::WIRE);
    selectionConf.setFlag(AllowSelection::POINT);

    auto layout = new QVBoxLayout();
    setLayout(layout);
    button = new QPushButton(this);
    button->setToolTip(tr("Click to enter selection mode"));
    button->setText(title);
    button->setCheckable(true);
    QObject::connect(button, &QPushButton::clicked, [this](bool checked) {onButton(checked);});
    layout->addWidget(button);

    listWidget = new QListWidget(this);
    listWidget->setMinimumHeight(200);
    listWidget->setMouseTracking(true);
    listWidget->installEventFilter(this);
    layout->addWidget(listWidget);
    QObject::connect(listWidget, &QListWidget::itemEntered, [this](QListWidgetItem *item) {
        PartDesignGui::highlightObjectOnTop(qvariant_cast<SubInfo>(item->data(Qt::UserRole)).objT);
    });
    QObject::connect(listWidget->model(), &QAbstractItemModel::rowsMoved, [this](){onItemMoved();});

    QAction* remove = new QAction(tr("Remove"), this);
    remove->setShortcut(Gui::QtTools::deleteKeySequence());
    listWidget->addAction(remove);
    listWidget->setContextMenuPolicy(Qt::ActionsContextMenu);
    QObject::connect(remove, &QAction::triggered, [this](){onDelete();});

    if (auto prop = getProperty()) {
        conn = prop->signalChanged.connect([this](const App::Property &) {toggleShowOnTop();});
    }

    connModeChange = parentTask->signalSelectionModeChanged.connect([this]() {
        if (parentTask->getSelectionMode() == selectionMode) {
            toggleShowOnTop(true);
            button->setChecked(true);
        } else if (button->isChecked()) {
            disableShowOnTop();
            button->setChecked(false);
            refresh();
        }
    });
}

void LinkSubListWidget::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (parentTask->getSelectionMode() != selectionMode)
        return;
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        App::SubObjectT ref(msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object);
        addLinks({ref});
    }
}

void LinkSubListWidget::setSelectionConfig(const AllowSelectionFlags &conf)
{
    selectionConf = conf;
}

void LinkSubListWidget::setSelectionMode(TaskSketchBasedParameters::SelectionMode mode)
{
    selectionMode = mode;
}

bool LinkSubListWidget::addLinks(const std::vector<App::SubObjectT> &objs)
{
    auto prop = getProperty();
    if (!prop)
        return false;
    try {
        auto links = prop->getSubListValues();
        bool touched = false;
        std::set<App::DocumentObjectT> newSections;
        for (auto ref : objs) {
            ref = PartDesignGui::importExternalObject(ref);
            auto refObj = ref.getObject();
            if (!refObj)
                continue;
            bool found = false;
            for (auto &v : links) {
                if (v.first != refObj)
                    continue;
                if (ref.getSubName().empty()) {
                    if (v.second.size() != 1 || v.second.front().size()) {
                        v.second.clear();
                        v.second.emplace_back();
                        touched = true;
                    }
                    found = true;
                    break;
                }
                for (auto it=v.second.begin(); it!=v.second.end();) {
                    if (it->empty()) {
                        it = v.second.erase(it);
                        touched = true;
                    }
                    else if (*it == ref.getSubName()) {
                        found = true;
                        break;
                    } else
                        ++it;
                }
                if (!found) {
                    v.second.push_back(ref.getSubName());
                    touched = found = true;
                }
                break;
            }
            if (!found) {
                newSections.emplace(refObj);
                std::vector<std::string> subs;
                if (!ref.getSubName().empty())
                    subs.push_back(ref.getSubName());
                links.emplace_back(refObj, std::move(subs));
                touched = true;
            }
        }
        if (!touched)
            return false;
        for (auto it = links.begin(); it!=links.end(); ) {
            if (it->second.empty()) {
                ++it;
                continue;
            }
            for (auto itSub = it->second.begin(); itSub!=it->second.end(); ) {
                if (App::GeoFeature::hasMissingElement(itSub->c_str()))
                    itSub = it->second.erase(itSub);
                else
                    ++itSub;
            }
            if (it->second.empty())
                it = links.erase(it);
            else
                ++it;
        }
        parentTask->setupTransaction();
        prop->setSubListValues(links);
        parentTask->recomputeFeature();
        for (const auto &o : newSections) {
            if (auto obj = o.getObject())
                obj->Visibility.setValue(false);
        }
        refresh();
        return true;
    } catch (Base::Exception &e) {
        e.ReportException();
    }
    return false;
}

void LinkSubListWidget::addItem(App::DocumentObject *obj,
                                const std::vector<std::string> &subs,
                                bool select)
{
    auto prop = getProperty();
    if (!prop)
        return;

    App::SubObjectT objT(obj);
    QString label = QString::fromUtf8(objT.getObjectFullName(objT.getDocumentName().c_str()).c_str());
    if (subs.size() && !subs[0].empty()) {
        bool first = true;
        for (const auto &sub : subs) {
            if (first) {
                first = false;
                label += QStringLiteral("(");
            }
            else
                label += QStringLiteral(", ");
            label += QString::fromUtf8(sub.c_str());
        }
        label += QStringLiteral(")");
    }

    QListWidgetItem *item = nullptr;
    for (int i=0; i<listWidget->count(); ++i) {
        auto listItem = listWidget->item(i);
        if (qvariant_cast<SubInfo>(listItem->data(Qt::UserRole)).objT == objT) {
            item = listItem;
            break;
        }
    }
    if (!item) {
        item = new QListWidgetItem(listWidget);
        auto vp = Gui::Application::Instance->getViewProvider(obj);
        if (vp)
            item->setIcon(vp->getIcon());
    }
    item->setData(Qt::UserRole, QVariant::fromValue(SubInfo(objT, subs)));
    item->setText(label);
    if (select) {
        QSignalBlocker blocker(listWidget);
        item->setSelected(true);
        listWidget->scrollToItem(item);
    }
}

void LinkSubListWidget::onDelete()
{
    auto prop = getProperty();
    if (!prop)
        return;

    // Delete the selected profile
    for(auto item : listWidget->selectedItems())
        delete item;

    std::vector<App::PropertyLinkSubList::SubSet> subset;
    for (int i=0; i<listWidget->count(); ++i) {
        auto data = qvariant_cast<SubInfo>(listWidget->item(i)->data(Qt::UserRole));
        if (auto obj = data.objT.getObject())
            subset.emplace_back(obj, data.subs);
    }
    try {
        parentTask->setupTransaction();
        prop->setSubListValues(subset);
        parentTask->recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void LinkSubListWidget::onItemMoved()
{
    QAbstractItemModel* model = qobject_cast<QAbstractItemModel*>(sender());
    if (!model)
        return;

    auto prop = getProperty();
    if (!prop)
        return;

    std::vector<App::PropertyLinkSubList::SubSet> subset;
    int rows = model->rowCount();
    for (int i = 0; i < rows; i++) {
        QModelIndex index = model->index(i, 0);
        auto data = qvariant_cast<SubInfo>(index.data(Qt::UserRole));
        if (auto obj = data.objT.getObject())
            subset.emplace_back(obj, data.subs);
    }
    try {
        parentTask->setupTransaction();
        prop->setSubListValues(subset);
        parentTask->recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void LinkSubListWidget::onButton(bool checked) {
    if (checked) {
        if (parentTask->getSelectionMode() == TaskSketchBasedParameters::SelectionMode::none) {
            auto sels = Gui::Selection().getSelectionT("*", Gui::ResolveMode::NoResolve);
            if (sels.size()) {
                if (addLinks(sels)) {
                    button->setChecked(false);
                    return;
                }
            }
        }
        parentTask->onSelectReference(button, selectionMode, selectionConf);
    } else {
        parentTask->exitSelectionMode();
        refresh();
    }
}

bool LinkSubListWidget::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Leave:
        Gui::Selection().rmvPreselect();
        break;
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        QKeyEvent * kevent = static_cast<QKeyEvent*>(ev);
        if (o == listWidget && kevent->modifiers() == Qt::NoModifier) {
            if (kevent->matches(Gui::QtTools::deleteKeySequence())) {
                kevent->accept();
                if (ev->type() == QEvent::KeyPress)
                    onDelete();
            }
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void LinkSubListWidget::toggleShowOnTop(bool init)
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, linkProp.getPropertyName().c_str(), init);
}

void LinkSubListWidget::disableShowOnTop()
{
    auto vp = Base::freecad_dynamic_cast<Gui::ViewProviderDocumentObject>(
                Gui::Application::Instance->getViewProvider(linkProp.getObject()));
    PartDesignGui::toggleShowOnTop(vp, lastReferences, nullptr);
}

void LinkSubListWidget::refresh()
{
    auto prop = getProperty();
    if (!prop)
        return;

    auto subset = prop->getSubListValues();

    if (!linkInited && prop->getShadowSubs().size() == prop->getValues().size()) {
        std::map<App::DocumentObject*, std::set<std::string>> resolveMap;
        size_t i = 0;
        std::string indexedName;
        bool hasResolved = false;
        for (const auto &shadow : prop->getShadowSubs()) {
            auto link = prop->getValues()[i++];

            if (App::GeoFeature::hasMissingElement(shadow.second.c_str())) {
                auto related = Part::Feature::getRelatedElements(link, shadow.first.c_str());
                if (!related.empty()) {
                    hasResolved = true;
                    indexedName.clear();
                    const auto &element = related.front();
                    element.index.appendToStringBuffer(indexedName);
                    FC_WARN("guess element reference in " << prop->getFullName()
                            << ": " << shadow.second << " -> " << element.index);
                    resolveMap[link].insert(indexedName);
                    break;
                }
            }
        }
        if (hasResolved) {
            for (auto &v : subset) {
                auto it = resolveMap.find(v.first);
                if (it == resolveMap.end())
                    continue;
                for (auto itSub = v.second.begin(); itSub != v.second.end();) {
                    if (it->second.count(*itSub))
                        itSub = v.second.erase(itSub);
                    else
                        ++itSub;
                }
                v.second.insert(v.second.end(), it->second.begin(), it->second.end());
            }
            try {
                parentTask->setupTransaction();
                prop->setSubListValues(subset);
                parentTask->recomputeFeature();
            } catch (Base::Exception &e) {
                e.ReportException();
            }
        }
    }

    QSignalBlocker guard(listWidget);
    listWidget->clear();
    for (const auto &v : subset)
        addItem(v.first, v.second);

    linkInited = true;
}


//////////////////////////////////////////////////////////////////////////////////////////

TaskSketchBasedParameters::TaskSketchBasedParameters(PartDesignGui::ViewProvider *vp, QWidget *parent,
                                                     const std::string& pixmapname, const QString& parname)
    : TaskFeatureParameters(vp, parent, pixmapname, parname)
{
}

void TaskSketchBasedParameters::addProfileEdit(QBoxLayout *boxLayout)
{
    if (!vp)
        return;
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    profileWidget = new LinkSubWidget(this, tr("Profile"), pcSketchBased->Profile);
    profileWidget->setSelectionMode(SelectionMode::refProfile);
    boxLayout->insertWidget(0, profileWidget);
}

void TaskSketchBasedParameters::initUI(QWidget *widget) {
    if(!vp)
        return;

    QBoxLayout * boxLayout = qobject_cast<QBoxLayout*>(widget->layout());
    if (!boxLayout)
        return;

    addProfileEdit(boxLayout);
    PartDesignGui::addTaskCheckBox(vp, widget);
    addOperationCombo(boxLayout);
    addUpdateViewCheckBox(boxLayout);
    addFittingWidgets(boxLayout);
    _refresh();
}

void TaskSketchBasedParameters::addFittingWidgets(QBoxLayout *parentLayout)
{
    if (!vp)
        return;
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    if (pcSketchBased->Fit.testStatus(App::Property::Hidden))
        return;
    auto groupBox = new QGroupBox(tr("Fitting"));
    auto boxLayout = new QVBoxLayout();
    groupBox->setLayout(boxLayout);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Tolerance"), this));
    fitEdit = new Gui::PrefQuantitySpinBox(this);
    fitEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/ProfileFit"));
    fitEdit->bind(pcSketchBased->Fit);
    fitEdit->setUnit(Base::Unit::Length);
    fitEdit->setKeyboardTracking(false);
    fitEdit->setToolTip(QApplication::translate("Property", pcSketchBased->Fit.getDocumentation()));
    layout->addWidget(fitEdit);
    connect(fitEdit, SIGNAL(valueChanged(double)), this, SLOT(onFitChanged(double)));
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Join type")));
    fitJoinType = new QComboBox(this);
    for (int i=0;;++i) {
        const char * type = Part::Offset::JoinEnums[i];
        if (!type)
            break;
        fitJoinType->addItem(tr(type));
    }
    connect(fitJoinType, SIGNAL(currentIndexChanged(int)), this, SLOT(onFitJoinChanged(int)));
    layout->addWidget(fitJoinType);
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Inner fit"), this));
    innerFitEdit = new Gui::PrefQuantitySpinBox(this);
    innerFitEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/ProfileInnerFit"));
    innerFitEdit->bind(pcSketchBased->InnerFit);
    innerFitEdit->setUnit(Base::Unit::Length);
    innerFitEdit->setKeyboardTracking(false);
    innerFitEdit->setToolTip(QApplication::translate(
                "Property", pcSketchBased->InnerFit.getDocumentation()));
    layout->addWidget(innerFitEdit);
    connect(innerFitEdit, SIGNAL(valueChanged(double)), this, SLOT(onInnerFitChanged(double)));
    boxLayout->addLayout(layout);

    layout = new QHBoxLayout();
    layout->addWidget(new QLabel(tr("Inner join type")));
    innerFitJoinType = new QComboBox(this);
    for (int i=0;;++i) {
        const char * type = Part::Offset::JoinEnums[i];
        if (!type)
            break;
        innerFitJoinType->addItem(tr(type));
    }
    connect(innerFitJoinType, SIGNAL(currentIndexChanged(int)), this, SLOT(onInnerFitJoinChanged(int)));
    layout->addWidget(innerFitJoinType);
    boxLayout->addLayout(layout);

    parentLayout->addWidget(groupBox);
}

void TaskSketchBasedParameters::_refresh()
{
    if (!vp || !vp->getObject())
        return;

    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    if (fitEdit) {
        QSignalBlocker guard(fitEdit);
        fitEdit->setValue(pcSketchBased->Fit.getValue());
    }

    if (fitJoinType) {
        QSignalBlocker guard(fitJoinType);
        fitJoinType->setCurrentIndex(pcSketchBased->FitJoin.getValue());
    }

    if (innerFitEdit) {
        QSignalBlocker guard(innerFitEdit);
        innerFitEdit->setValue(pcSketchBased->InnerFit.getValue());
    }

    if (innerFitJoinType) {
        QSignalBlocker guard(innerFitJoinType);
        innerFitJoinType->setCurrentIndex(pcSketchBased->InnerFitJoin.getValue());
    }

    if (profileWidget)
        profileWidget->refresh();

    TaskFeatureParameters::_refresh();
}

void TaskSketchBasedParameters::saveHistory(void)
{
    if (fitEdit)
        fitEdit->pushToHistory();
    if (innerFitEdit)
        innerFitEdit->pushToHistory();
    TaskFeatureParameters::saveHistory();
}

void TaskSketchBasedParameters::onFitChanged(double v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->Fit.setValue(v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onFitJoinChanged(int v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->FitJoin.setValue((long)v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onInnerFitChanged(double v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->InnerFit.setValue(v);
    recomputeFeature();
}

void TaskSketchBasedParameters::onInnerFitJoinChanged(int v)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    pcSketchBased->InnerFitJoin.setValue((long)v);
    recomputeFeature();
}

bool TaskSketchBasedParameters::reselectBaseElement(const Gui::SelectionChanges& msg)
{
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    App::DocumentObject* selObj = pcSketchBased->getDocument()->getObject(msg.pObjectName);
    if (selObj != pcSketchBased)
        return false;

    // The feature itself is selected, trace the selected element back to
    // its base
    auto baseShape = pcSketchBased->getBaseShape(true);
    auto base = pcSketchBased->getBaseObject();
    if (baseShape.isNull() || !base)
        return true;
    auto history = Part::Feature::getElementHistory(pcSketchBased,msg.pSubName,true,true);
    const char *element = 0;
    std::string tmp;
    for(auto &hist : history) {
        if (hist.obj != base)
            continue;
        tmp.clear();
        element = hist.element.appendToBufferWithPrefix(tmp);
        if (!baseShape.getSubShape(element, true).IsNull())
            break;
        element = nullptr;
    }
    if(element) {
        if(msg.pOriginalMsg) {
            // We are about change the sketched base object shape, meaning
            // that this selected element may be gone soon. So remove it
            // from the selection to avoid warning.
            Gui::Selection().rmvSelection(msg.pOriginalMsg->pDocName,
                                          msg.pOriginalMsg->pObjectName,
                                          msg.pOriginalMsg->pSubName);
        }

        App::SubObjectT sel = (msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object).getParent();
        auto objs = sel.getSubObjectList();
        int i=0, idx = -1;
        for (auto obj : objs) {
            ++i;
            if (obj->getLinkedObject()->isDerivedFrom(PartDesign::Body::getClassTypeId()))
                idx = i;
        }
        if (idx < 0)
            return true;
        objs.resize(idx);
        objs.push_back(base);
        sel = App::SubObjectT(objs);
        sel.setSubName((sel.getSubName() + element).c_str());
        Gui::Selection().addSelection(sel);
    }
    return true;
}

const QString TaskSketchBasedParameters::onSelectUpToFace(const Gui::SelectionChanges& msg,
                                                          App::PropertyLinkSub* prop)
{
    // Note: The validity checking has already been done in ReferenceSelection.cpp
    PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
    App::DocumentObject* selObj = pcSketchBased->getDocument()->getObject(msg.pObjectName);
    if (reselectBaseElement(msg))
        return QString();

    App::SubObjectT objT = msg.pOriginalMsg ? msg.pOriginalMsg->Object : msg.Object;

    // Remove subname for planes and datum features
    if (PartDesign::Feature::isDatum(selObj))
        objT.setSubName(objT.getSubNameNoElement());

    objT = PartDesignGui::importExternalObject(objT, false, false);
    if (auto sobj = objT.getSubObject()) {
        (prop ? prop : &pcSketchBased->UpToFace)->setValue(sobj, {objT.getOldElementName()});
        recomputeFeature();
        auto subElement = objT.getOldElementName();
        if (subElement.size()) {
            return QStringLiteral("%1:%2").arg(
                    QString::fromUtf8(sobj->getNameInDocument()),
                    QString::fromUtf8(subElement.c_str()));
        } else
            return QString::fromUtf8(sobj->getNameInDocument());
    }

    return QString();
}

void TaskSketchBasedParameters::onSelectReference(QWidget *blinkWidget,
                                                  SelectionMode mode,
                                                  const AllowSelectionFlags &conf)
{
    exitSelectionMode(false);
    if (!vp || mode == SelectionMode::none)
        return;
    PartDesign::ProfileBased* pcSketchBased = dynamic_cast<PartDesign::ProfileBased*>(vp->getObject());
    // The solid this feature will be fused to
    App::DocumentObject* prevSolid = pcSketchBased->getBaseObject( /* silent =*/ true );
    if (blinkWidget) {
        QString altText = tr("Selecting");
        auto prop = blinkWidget->property("blinkText");
        if (prop.isValid())
            altText = prop.toString();
        addBlinkWidget(blinkWidget, altText);
        this->blinkWidget = blinkWidget;
    }
    std::unique_ptr<Gui::SelectionFilterGate> gateRefPtr(new ReferenceSelection(prevSolid, conf));
    std::unique_ptr<Gui::SelectionFilterGate> gateDepPtr(new NoDependentsSelection(pcSketchBased));
    setSelectionMode(mode, new CombineSelectionFilterGates(gateRefPtr, gateDepPtr));
}


void TaskSketchBasedParameters::_exitSelectionMode()
{
    if (selectionMode == SelectionMode::none)
        return;

    if (blinkWidget) {
        removeBlinkWidget(this->blinkWidget);
        blinkWidget = nullptr;
    }
    if (Gui::Selection().currentSelectionGate() == selectionGate)
        Gui::Selection().rmvSelectionGate();
    selectionMode = SelectionMode::none;
    selectionGate = nullptr;
}

void TaskSketchBasedParameters::exitSelectionMode(bool clearSelection)
{
    if (selectionMode != SelectionMode::none) {
        auto oldMode = selectionMode;
        _exitSelectionMode();
        onSelectionModeChanged(oldMode);
        signalSelectionModeChanged();
        if (clearSelection)
            Gui::Selection().clearSelection();
    }
}

void TaskSketchBasedParameters::setSelectionMode(SelectionMode mode, Gui::SelectionGate *gate)
{
    if (mode == selectionMode)
        return;

    exitSelectionMode(false);
    if (mode == SelectionMode::none)
        return;

    auto oldMode = selectionMode;
    Gui::Selection().clearSelection();
    selectionMode = mode;
    selectionGate = gate;
    if (gate)
        Gui::Selection().addSelectionGate(gate);
    onSelectionModeChanged(oldMode);
    signalSelectionModeChanged();
}

TaskSketchBasedParameters::SelectionMode TaskSketchBasedParameters::getSelectionMode() const
{
    return selectionMode;
}

void TaskSketchBasedParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (selectionGate && Gui::Selection().currentSelectionGate() != selectionGate)
        exitSelectionMode(false);
    else if (vp && selectionMode != SelectionMode::none)
        _onSelectionChanged(msg);
}

QVariant TaskSketchBasedParameters::setUpToFace(const QString& text)
{
    if (text.isEmpty())
        return {};

    QStringList parts = text.split(QChar::fromLatin1(':'));
    if (parts.length() < 2)
        parts.push_back(QStringLiteral(""));

    // Check whether this is the name of an App::Plane or Part::Datum feature
    App::DocumentObject* obj = vp->getObject()->getDocument()->getObject(parts[0].toUtf8());
    if (!obj)
        return {};

    if (obj->isDerivedFrom<App::Plane>()) {
        // everything is OK (we assume a Part can only have exactly 3 App::Plane objects located at the base of the feature tree)
        return {};
    }
    else if (obj->isDerivedFrom<Part::Datum>()) {
        // it's up to the document to check that the datum plane is in the same body
        return {};
    }
    else {
        // We must expect that "parts[1]" is the translation of "Face" followed by an ID.
        QString name;
        QTextStream str(&name);
        str << "^" << tr("Face") << "(\\d+)$";

        std::string upToFace;
        QRegularExpression rx(name);
        QRegularExpressionMatch match;
        if (parts[1].indexOf(rx, 0, &match) < 0)
            upToFace = parts[1].toUtf8().constData();
        else {
            int faceId = match.captured(1).toInt();
            std::stringstream ss;
            ss << "Face" << faceId;
            upToFace = ss.str();
        }

        PartDesign::ProfileBased* pcSketchBased = static_cast<PartDesign::ProfileBased*>(vp->getObject());
        pcSketchBased->UpToFace.setValue(obj, {upToFace});
        recomputeFeature();

        return QByteArray(upToFace.c_str());
    }
}

QVariant TaskSketchBasedParameters::objectNameByLabel(const QString& label,
                                                      const QVariant& suggest) const
{
    // search for an object with the given label
    App::Document* doc = this->vp->getObject()->getDocument();
    // for faster access try the suggestion
    if (suggest.isValid()) {
        App::DocumentObject* obj = doc->getObject(suggest.toByteArray());
        if (obj && QString::fromUtf8(obj->Label.getValue()) == label) {
            return QVariant(QByteArray(obj->getNameInDocument()));
        }
    }

    // go through all objects and check the labels
    std::string name = label.toUtf8().data();
    std::vector<App::DocumentObject*> objs = doc->getObjects();
    for (auto obj : objs) {
        if (name == obj->Label.getValue()) {
            return QVariant(QByteArray(obj->getNameInDocument()));
        }
    }

    return {}; // no such feature found
}

QString TaskSketchBasedParameters::getFaceReference(const QString& obj, const QString& sub) const
{
    App::Document* doc = this->vp->getObject()->getDocument();
    QString o = obj.left(obj.indexOf(QStringLiteral(":")));

    if (o.isEmpty())
        return {};

    return QStringLiteral(R"((App.getDocument("%1").%2, ["%3"]))")
            .arg(QString::fromUtf8(doc->getName()), o, sub);
}

QString TaskSketchBasedParameters::make2DLabel(const App::DocumentObject* section,
                                               const std::vector<std::string>& subValues)
{
    if (section->isDerivedFrom(Part::Part2DObject::getClassTypeId())) {
        return QString::fromUtf8(section->Label.getValue());
    }
    else if (subValues.empty()) {
        Base::Console().Error("No valid subelement linked in %s\n", section->Label.getValue());
        return {};
    }
    else {
        return QString::fromStdString((std::string(section->getNameInDocument()) + ":" + subValues[0]));
    }
}

TaskSketchBasedParameters::~TaskSketchBasedParameters()
{
    _exitSelectionMode();
    Gui::Selection().clearSelection();
}


//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgSketchBasedParameters::TaskDlgSketchBasedParameters(PartDesignGui::ViewProvider *vp)
    : TaskDlgFeatureParameters(vp)
{
}

TaskDlgSketchBasedParameters::~TaskDlgSketchBasedParameters() = default;

//==== calls from the TaskView ===============================================================


bool TaskDlgSketchBasedParameters::accept() {
    if (!vp)
        return true;

    App::DocumentObject* feature = vp->getObject();

    // Make sure the feature is what we are expecting
    // Should be fine but you never know...
    if (!feature->isDerivedFrom<PartDesign::ProfileBased>()) {
        THROWM(Base::TypeError, "Bad object processed in the sketch based dialog.")
    }

    // First verify that the feature can be built and then hide the profile as otherwise
    // it will remain hidden if the feature's recompute fails
    if (TaskDlgFeatureParameters::accept()) {
        App::DocumentObject* sketch = static_cast<PartDesign::ProfileBased*>(feature)->Profile.getValue();
        Gui::cmdAppObjectHide(sketch);
        return true;
    }

    return false;
}

bool TaskDlgSketchBasedParameters::reject()
{
    return TaskDlgFeatureParameters::reject();
}

#include "moc_TaskSketchBasedParameters.cpp"
