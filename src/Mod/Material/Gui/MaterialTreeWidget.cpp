// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QSignalBlocker>


#include <cstring>

#include <QHBoxLayout>
#include <QSpacerItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Command.h>
#include <Gui/ViewParams.h>

#include <Mod/Material/App/Exceptions.h>
#include <Mod/Material/App/MaterialFilter.h>
#include <Mod/Material/App/MaterialFilterPy.h>
#include <Mod/Material/App/ModelUuids.h>

#include "MaterialIcons.h"
#include "MaterialTreeWidget.h"
#include "MaterialsEditor.h"
#include "ui_MaterialsEditor.h"

Q_DECLARE_METATYPE(Materials::MaterialFilterPy*)

using Base::Console;
using namespace MatGui;

namespace
{
/// How much shorter than the style would make it each row of the material
/// tree is. The tree is a list to pick a name out of, and the taller its
/// rows the fewer names are in front of the user at the size this widget
/// is given; the style's own padding is sized for a form, where a row is
/// something to click into rather than something to read down.
constexpr int RowTrim = 4;

/** How big a card's icon is drawn, unless the user says otherwise
 *
 * The icons are renders of the materials, so this is how much of one
 * there is to see. Thirty-two is about where a surface finish starts to
 * read at all -- below it a knurl and a brushed lay both average out to
 * the same shiny grey cylinder -- and sixty-four is where it reads
 * without being looked for, which is what a picture chosen over a name
 * is for. Anyone who wants the list denser than the pictures sets
 * IconSize back down.
 */
constexpr int DefaultIconExtent = 64;
constexpr int MinIconExtent = 12;
constexpr int MaxIconExtent = 128;

/// The tree's rows, minus that padding and no shorter than what they have
/// to show.
class CompactRows: public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void initStyleOption(QStyleOptionViewItem* option,
                         const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        // Only a card carries a render worth enlarging -- it is the one
        // thing here that IS a picture. The folders and library headings
        // above them are stock icons with no sixty-four pixel version to
        // show, so scaling them up only blurs them, and it makes the
        // tree's structure shout as loudly as its contents. A card is
        // the item that names a material.
        if (!index.data(Qt::UserRole).toString().isEmpty()) {
            return;
        }
        const int small = option->widget
            ? option->widget->style()->pixelMetric(QStyle::PM_SmallIconSize,
                                                   option, option->widget)
            : 16;
        option->decorationSize = QSize(qMin(option->decorationSize.width(), small),
                                       qMin(option->decorationSize.height(), small));
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        // The floor is whatever the row actually carries -- the text, and
        // the icon where it is the taller of the two -- so trimming can
        // never crop either of them. Measured off a freshly initialised
        // option rather than the one passed in, because initStyleOption
        // is where a folder's decoration was cut back down and this has
        // to agree with what will actually be painted.
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const int content = qMax(opt.fontMetrics.height(),
                                 opt.decorationSize.height());
        size.setHeight(qMax(size.height() - RowTrim, content));
        return size;
    }
};
}  // namespace

/** Constructs a Material tree widget.
 */

TYPESYSTEM_SOURCE(MatGui::MaterialTreeWidget, Base::BaseClass)

MaterialTreeWidget::MaterialTreeWidget(const Materials::MaterialFilter& filter,
                                       QWidget* parent)
    : QWidget(parent)
    , m_expanded(false)
    , m_treeSizeHint(minimumTreeWidth, minimumTreeHeight)
    , _filter(filter)
    , _recentMax(defaultRecents)
{
    setup();
}

MaterialTreeWidget::MaterialTreeWidget(
    const std::shared_ptr<std::list<std::shared_ptr<Materials::MaterialFilter>>>& filterList,
    QWidget* parent)
    : QWidget(parent)
    , m_expanded(false)
    , m_treeSizeHint(minimumTreeWidth, minimumTreeHeight)
    , _filterList(filterList)
    , _recentMax(defaultRecents)
{
    setup();
}

MaterialTreeWidget::MaterialTreeWidget(QWidget* parent)
    : QWidget(parent)
    , m_expanded(false)
    , m_treeSizeHint(minimumTreeWidth, minimumTreeHeight)
    , _recentMax(defaultRecents)
{
    setup();
}

void MaterialTreeWidget::setup()
{
    getFavorites();
    getRecents();

    createLayout();
    createMaterialTree();

    // Icons render a few per event loop turn, so the tree is painted long
    // before they land; each one restates its own card as it arrives.
    connect(&MaterialIcons::instance(), &MaterialIcons::iconReady,
            this, &MaterialTreeWidget::refreshIcon);
}

/**
 * Destroys the widget and detaches it from its parameter group.
 */
MaterialTreeWidget::~MaterialTreeWidget()
{
    addRecent(m_uuid);
    saveWidgetSettings();
    saveMaterialTree();
}

QSize MaterialTreeWidget::sizeHint() const
{
    // One row, always: the list is a popup window now and asks this
    // widget for no room at all.
    QSize size = m_material->sizeHint();
    size.setWidth(minimumWidth);
    return size;
}

QSize MaterialTreeWidget::treeSizeHint() const
{
    return m_treeSizeHint;
}

void MaterialTreeWidget::setTreeSizeHint(const QSize& hint)
{
    // The size of the popup rather than of a panel inside this widget,
    // which is the same number doing the same job: how much list there
    // is to see at once.
    m_treeSizeHint = hint;
    m_materialTree->setMinimumSize(m_treeSizeHint);
    if (m_popup->isVisible()) {
        m_popup->resize(std::max(width(), m_treeSizeHint.width()),
                        m_treeSizeHint.height());
    }
}

int MaterialTreeWidget::iconExtent()
{
    auto treeParam = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget");
    return qBound(MinIconExtent,
                  int(treeParam->GetInt("IconSize", DefaultIconExtent)),
                  MaxIconExtent);
}

void MaterialTreeWidget::createLayout()
{
    m_material = new QLineEdit(this);
    m_material->setPlaceholderText(tr("Type to search materials"));
    m_material->setClearButtonEnabled(true);
    m_expand = new QPushButton(this);
    m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
    // The list is a popup, not a panel this widget makes room for. It
    // held the dialogs open at five hundred pixels of tree whether or
    // not anyone was choosing a material, and a picker is a thing you
    // use for two seconds. Everything that belongs to choosing -- the
    // tree, the filter, the way into the editor -- goes inside it.
    m_popup = new QFrame(this, Qt::Popup);
    m_popup->setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    m_materialTree = new QTreeView(m_popup);
    m_filterCombo = new QComboBox(m_popup);
    m_editor = new QPushButton(tr("Launch Editor"), m_popup);

    m_materialTree->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    m_materialTree->setMinimumSize(m_treeSizeHint);
    m_materialTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_materialTree->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_materialTree->setItemDelegate(new CompactRows(m_materialTree));
    // NOT uniform row heights: a card is as tall as its render and a
    // folder is as tall as a stock icon, so the view has to measure each
    // one. Taking the first row for all of them would crop every card to
    // the height of the "Favorites" heading above it.

    auto treeParam = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget");
    const int extent = iconExtent();
    m_materialTree->setIconSize(QSize(extent, extent));

    auto materialLayout = new QHBoxLayout();
    materialLayout->addWidget(m_material);
    materialLayout->addWidget(m_expand);
    // materialLayout->setSizeConstraint(QLayout::SetMinimumSize);

    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_filterCombo);
    buttonLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Preferred));
    buttonLayout->addWidget(m_editor);

    auto popupLayout = new QVBoxLayout(m_popup);
    popupLayout->setContentsMargins(0, 0, 0, 0);
    popupLayout->addWidget(m_materialTree);
    popupLayout->addItem(buttonLayout);

    auto layout = new QVBoxLayout();
    layout->setContentsMargins(0, 9, 0, 9);
    layout->addItem(materialLayout);
    setLayout(layout);

    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    // Set the filter if using a filter list
    if (hasMultipleFilters()) {
        _filter = *_filterList->front();
    }

    fillFilterCombo();

    // NOT the stored expanded state: that used to mean "the panel is
    // already open", which cost nothing but height. Opening a popup
    // window over the dialog the moment it appears is a different thing
    // entirely, and not one anybody asked for.
    m_expanded = false;
    setFilterVisible(true);

    // Typing searches, and searching shows what it found. Between them
    // this is the whole interaction: the list opens when it has
    // something to say and closes as soon as a material is picked.
    m_material->installEventFilter(this);
    m_popup->installEventFilter(this);
    connect(m_material, &QLineEdit::textEdited, this, [this](const QString& text) {
        // Growing text gets the rest of the name written for it and the
        // card it names highlighted in the list; shrinking text does
        // not, or Backspace would be undone as fast as it is pressed.
        const bool grew = text.length() > m_typed.length();
        m_typed = text;
        filterTree(QModelIndex(), text);
        showPopup();
        if (grew) {
            inlineComplete(text);
        }
    });
    connect(m_materialTree, &QTreeView::clicked, this, &MaterialTreeWidget::onTreeClicked);

    connect(m_expand, &QPushButton::clicked, this, &MaterialTreeWidget::expandClicked);
    connect(m_editor, &QPushButton::clicked, this, &MaterialTreeWidget::editorClicked);
    connect(m_filterCombo,
            &QComboBox::currentTextChanged,
            this,
            &MaterialTreeWidget::onFilter);
}

void MaterialTreeWidget::setExpanded(bool open)
{
    if (open) {
        showPopup();
    }
    else {
        hidePopup();
    }
}

void MaterialTreeWidget::showPopup()
{
    if (m_popup->isVisible()) {
        return;
    }
    // Under the box and as wide as the whole widget, which is where a
    // drop-down would be: the list belongs to the field it is filling
    // in, and lining them up is what says so.
    const QPoint corner = mapToGlobal(QPoint(0, height()));
    const QSize size(std::max(width(), m_treeSizeHint.width()),
                     m_treeSizeHint.height());
    m_popup->setGeometry(QRect(corner, size));
    m_popup->show();
    // The keyboard stays in the line edit -- typing is the point -- and
    // eventFilter() hands the arrow keys down to the tree.
    m_material->setFocus();

    m_expanded = true;
    m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarShadeButton));
    Q_EMIT onExpanded(m_expanded);
}

void MaterialTreeWidget::hidePopup()
{
    if (!m_popup->isVisible() && !m_expanded) {
        return;
    }
    m_popup->hide();
    m_expanded = false;
    m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
    Q_EMIT onExpanded(m_expanded);
}

bool MaterialTreeWidget::filterTree(const QModelIndex& parent, const QString& text)
{
    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    if (!model) {
        return false;
    }
    bool anyShown = false;
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        // Depth first: a library is worth showing when something inside
        // it matches, even though the library's own name does not.
        const bool childShown = filterTree(index, text);
        const bool selfShown =
            text.isEmpty() || index.data(Qt::DisplayRole).toString().contains(text, Qt::CaseInsensitive);
        const bool shown = selfShown || childShown;
        m_materialTree->setRowHidden(row, parent, !shown);
        if (childShown && !text.isEmpty()) {
            // A branch kept for what is inside it has to be open, or the
            // match sits behind a collapsed arrow and reads as no match.
            m_materialTree->expand(index);
        }
        anyShown = anyShown || shown;
    }
    return anyShown;
}

QModelIndex MaterialTreeWidget::firstPrefixMatch(const QModelIndex& parent,
                                                 const QString& text) const
{
    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    if (!model) {
        return {};
    }
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (m_materialTree->isRowHidden(row, parent)) {
            continue;
        }
        // A card only: a library called "Steel Library" is not something
        // the box can be completed to, because it cannot be chosen.
        if (!index.data(Qt::UserRole).toString().isEmpty()
            && index.data(Qt::DisplayRole).toString().startsWith(text, Qt::CaseInsensitive)) {
            return index;
        }
        const QModelIndex found = firstPrefixMatch(index, text);
        if (found.isValid()) {
            return found;
        }
    }
    return {};
}

void MaterialTreeWidget::inlineComplete(const QString& typed)
{
    if (typed.isEmpty()) {
        return;
    }
    const QModelIndex match = firstPrefixMatch(QModelIndex(), typed);
    if (!match.isValid()) {
        return;
    }
    const QString name = match.data(Qt::DisplayRole).toString();
    {
        // Only the text: selecting the row in the tree would apply the
        // material, and a name half typed is not a decision yet.
        QSignalBlocker block(m_material);
        m_material->setText(name);
        // The part nobody typed stays selected, so the next keystroke
        // replaces it and Enter accepts it.
        m_material->setSelection(int(typed.length()), int(name.length() - typed.length()));
    }
    // NoUpdate: current, not selected. Selecting is what applies a
    // material, and a name half typed is not a decision yet.
    m_materialTree->selectionModel()->setCurrentIndex(match, QItemSelectionModel::NoUpdate);
    m_materialTree->scrollTo(match);
}

void MaterialTreeWidget::onTreeClicked(const QModelIndex& index)
{
    // Selecting is what applies the material (onSelectMaterial); this
    // decides only whether the list has finished its job. A library or a
    // folder has no uuid and is something to open, not something to
    // choose.
    if (index.data(Qt::UserRole).toString().isEmpty()) {
        m_materialTree->setExpanded(index, !m_materialTree->isExpanded(index));
        return;
    }
    // A row reached with the arrow keys is current but not selected --
    // inlineComplete() leaves it that way on purpose -- and selecting is
    // what applies it. A row that was clicked is already selected, and
    // selecting it again changes nothing.
    m_materialTree->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
    hidePopup();
}

bool MaterialTreeWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_popup && event->type() == QEvent::Hide) {
        // A Qt::Popup closes itself on the first click outside it, so
        // this is where the arrow on the button learns about it.
        m_expanded = false;
        m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
        Q_EMIT onExpanded(false);
        return false;
    }
    if (watched != m_material) {
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
        // Clicking the box offers everything, the way clicking a
        // drop-down does. Whatever was typed stays as the filter.
        showPopup();
    }
    else if (event->type() == QEvent::KeyPress && m_popup->isVisible()) {
        auto key = static_cast<QKeyEvent*>(event)->key();
        switch (key) {
            case Qt::Key_Escape:
                hidePopup();
                return true;
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
                // Walking the list without leaving the box: the tree is
                // where the keys mean something, and the box is where
                // the next character has to land.
                QCoreApplication::sendEvent(m_materialTree, event);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: {
                const QModelIndex current = m_materialTree->currentIndex();
                if (current.isValid()) {
                    onTreeClicked(current);
                }
                return true;
            }
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MaterialTreeWidget::setFilterVisible(bool open)
{
    // The combo lives in the popup, so it is shown exactly when it has
    // something to choose between; \a open is what it used to depend on
    // and is now the popup's own business.
    Q_UNUSED(open)
    m_filterCombo->setVisible(hasMultipleFilters());
}

void MaterialTreeWidget::fillFilterCombo()
{
    m_filterCombo->clear();
    if (hasMultipleFilters()) {
        for (auto const& filter : *_filterList) {
            m_filterCombo->addItem(filter->name());
        }
    }
}


void MaterialTreeWidget::expandClicked(bool checked)
{
    Q_UNUSED(checked)

    // Toggle the open state
    setExpanded(!m_expanded);
}

void MaterialTreeWidget::editorClicked(bool checked)
{
    Q_UNUSED(checked)

    MaterialsEditor dialog(_filter, this);
    dialog.setModal(true);
    if (dialog.exec() == QDialog::Accepted) {
        // updateMaterialGeneral();
        // _material->resetEditState();
        // refreshMaterialTree();
        // _materialSelected = true;
        auto material = dialog.getMaterial();
        updateMaterialTree();
        setMaterial(material->getUUID());
    }

    // Gui::Application::Instance->commandManager().runCommandByName("Material_Edit");
    // Toggle the open state
    // setExpanded(!m_expanded);
}

void MaterialTreeWidget::updateMaterial(const QString& uuid)
{
    if (uuid.isEmpty() || uuid == m_uuid) {
        return;
    }

    m_uuid = uuid;

    if (uuid == m_leadingId) {
        // Not a card: it has a label of its own and the manager has never
        // heard of it.
        m_materialDisplay = m_leadingText;
        m_material->setText(m_materialDisplay);
        return;
    }

    // Fetch the material from the manager
    auto material = std::make_shared<Materials::Material>();
    try {
        material = std::make_shared<Materials::Material>(*getMaterialManager().getMaterial(uuid));
    }
    catch (Materials::MaterialNotFound const&) {
        Base::Console().log("*** Unable to load material '%s'\n", uuid.toStdString().c_str());
    }

    m_materialDisplay = material->getName();
    m_material->setText(m_materialDisplay);
}

bool MaterialTreeWidget::findInTree(const QStandardItem& node,
                                    QModelIndex* index,
                                    const QString& uuid)
{
    auto vv = node.data(Qt::UserRole);
    if (vv.isValid() && vv == uuid) {
        *index = node.index();
        return true;
    }

    if (node.hasChildren()) {
        for (int i = 0; i < node.rowCount(); i++) {
            auto child = node.child(i);
            if (findInTree(*child, index, uuid)) {
                return true;
            }
        }
    }

    return false;
}

QModelIndex MaterialTreeWidget::findInTree(const QString& uuid)
{
    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    auto root = model->invisibleRootItem();

    QModelIndex index;
    // Find the original item, not the reference in favourites or recents
    for (int i = 0; i < root->rowCount(); i++) {
        auto child = root->child(i);
        if (child->text() != tr("Favorites") && child->text() != tr("Recent")) {
            if (findInTree(*child, &index, uuid)) {
                return index;
            }
        }
    }

    return {};
}

void MaterialTreeWidget::setLeadingEntry(const QString& id,
                                         const QString& text,
                                         const QString& toolTip)
{
    if (m_leadingId == id && m_leadingText == text && m_leadingToolTip == toolTip) {
        return;
    }
    m_leadingId = id;
    m_leadingText = text;
    m_leadingToolTip = toolTip;
    // The row is built by fillMaterialTree(), so the list has to be built
    // again -- but only once the tree exists, which it does not yet while
    // a constructor is still running.
    if (m_materialTree && m_materialTree->model()) {
        updateMaterialTree();
        // A selection sitting on the row that just went away, or waiting
        // for one that has just arrived, has to be put back.
        if (!m_uuid.isEmpty()) {
            const QString held = m_uuid;
            m_uuid.clear();
            setMaterial(held);
        }
    }
}

void MaterialTreeWidget::setMaterial(const QString& uuid)
{
    QItemSelectionModel* selectionModel = m_materialTree->selectionModel();
    // Saying what the selection IS is not the same as a person choosing
    // it: onSelectMaterial applies what it is told about, so letting this
    // reach it would let a caller that shows the current look be answered
    // by having that look written back at it.
    const QSignalBlocker blocker(selectionModel);

    if (uuid.isEmpty()) {
        // Nothing is selected
        selectionModel->clear();
        m_material->clear();
        m_uuid.clear();
        m_materialDisplay.clear();

        return;
    }

    updateMaterial(uuid);

    // Now select the material in the tree
    auto index = findInTree(uuid);
    if (index.isValid()) {
        selectionModel->select(index, QItemSelectionModel::SelectCurrent);
        m_materialTree->scrollTo(index);
    }
}

QString MaterialTreeWidget::getMaterialUUID() const
{
    return m_uuid;
}

void MaterialTreeWidget::setFilter(const Materials::MaterialFilter& filter)
{
    if (_filterList) {
        _filterList.reset();
    }

    _filter = filter;

    fillFilterCombo();
    setFilterVisible(m_expanded);

    updateMaterialTree();
}

void MaterialTreeWidget::setFilter(
    const std::shared_ptr<std::list<std::shared_ptr<Materials::MaterialFilter>>>& filterList)
{
    if (_filterList) {
        _filterList.reset();
    }

    _filterList = filterList;
    if (hasMultipleFilters()) {
        _filter = *_filterList->front();
    }

    fillFilterCombo();
    setFilterVisible(m_expanded);

    updateMaterialTree();
}

void MaterialTreeWidget::setActiveFilter(const QString& name)
{
    if (_filterList) {
        for (auto const& filter : *_filterList) {
            if (filter->name() == name) {
                _filter = *filter;

                // Save the library/folder expansion state
                saveMaterialTree();

                updateMaterialTree();
                return;
            }
        }
    }
}

void MaterialTreeWidget::updateMaterialTree()
{
    _favorites.clear();
    _recents.clear();

    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    model->clear();

    getFavorites();
    getRecents();
    fillMaterialTree();
}

void MaterialTreeWidget::getFavorites()
{
    _favorites.clear();

    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/Favorites");
    auto count = param->GetInt("Favorites", 0);
    for (int i = 0; static_cast<long>(i) < count; i++) {
        QString key = QStringLiteral("FAV%1").arg(i);
        QString uuid = QString::fromStdString(param->GetASCII(key.toStdString().c_str(), ""));
        if (_filter.modelIncluded(uuid)) {
            _favorites.push_back(uuid);
        }
    }
}

void MaterialTreeWidget::getRecents()
{
    _recents.clear();

    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/Recent");
    _recentMax = static_cast<int>(param->GetInt("RecentMax", defaultRecents));
    auto count = param->GetInt("Recent", 0);
    for (int i = 0; static_cast<long>(i) < count; i++) {
        QString key = QStringLiteral("MRU%1").arg(i);
        QString uuid = QString::fromStdString(param->GetASCII(key.toStdString().c_str(), ""));
        if (_filter.modelIncluded(uuid)) {
            _recents.push_back(uuid);
        }
    }
}

void MaterialTreeWidget::saveRecents()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/Recent");

    // Clear out the existing favorites
    int count = param->GetInt("Recent", 0);
    for (int i = 0; static_cast<long>(i) < count; i++) {
        QString key = QStringLiteral("MRU%1").arg(i);
        param->RemoveASCII(key.toStdString().c_str());
    }

    // Add the current values
    int size = _recents.size();
    if (size > _recentMax) {
        size = _recentMax;
    }
    param->SetInt("Recent", size);
    int j = 0;
    for (auto& recent : _recents) {
        QString key = QStringLiteral("MRU%1").arg(j);
        param->SetASCII(key.toStdString().c_str(), recent.toStdString());

        j++;
        if (j >= size) {
            break;
        }
    }
}

void MaterialTreeWidget::addRecent(const QString& uuid)
{
    // Ensure it is a material. New, unsaved materials will not be
    try {
        auto material = Materials::MaterialManager::getManager().getMaterial(uuid);
        Q_UNUSED(material)
    }
    catch (const Materials::MaterialNotFound&) {
        return;
    }

    // Ensure no duplicates
    if (isRecent(uuid)) {
        _recents.remove(uuid);
    }

    _recents.push_front(uuid);
    while (_recents.size() > static_cast<std::size_t>(_recentMax)) {
        _recents.pop_back();
    }

    saveRecents();
}

bool MaterialTreeWidget::isRecent(const QString& uuid) const
{
    for (auto& it : _recents) {
        if (it == uuid) {
            return true;
        }
    }
    return false;
}

void MaterialTreeWidget::createMaterialTree()
{
    auto model = new QStandardItemModel(this);
    m_materialTree->setModel(model);
    m_materialTree->setHeaderHidden(true);

    // This needs to be done after the model is set
    QItemSelectionModel* selectionModel = m_materialTree->selectionModel();
    connect(selectionModel,
            &QItemSelectionModel::selectionChanged,
            this,
            &MaterialTreeWidget::onSelectMaterial);
    connect(m_materialTree, &QTreeView::doubleClicked, this, &MaterialTreeWidget::onDoubleClick);

    fillMaterialTree();
}

void MaterialTreeWidget::fillMaterialTree()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget/MaterialTree");

    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());

    if (!m_leadingId.isEmpty()) {
        // Above Favorites and Recent both: it is the list's default answer,
        // not one more card to scroll past (docs/MaterialStorage.md 15.5).
        auto row = new QStandardItem(m_leadingText);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        row->setData(m_leadingId, Qt::UserRole);
        if (!m_leadingToolTip.isEmpty()) {
            row->setToolTip(m_leadingToolTip);
        }
        model->appendRow(row);
    }

    if (_filterOptions.includeFavorites()) {
        auto lib = new QStandardItem(tr("Favorites"));
        lib->setFlags(Qt::ItemIsEnabled);
        addExpanded(model, lib, param);
        addFavorites(lib);
    }

    if (_filterOptions.includeRecent()) {
        auto lib = new QStandardItem(tr("Recent"));
        lib->setFlags(Qt::ItemIsEnabled);
        addExpanded(model, lib, param);
        addRecents(lib);
    }

    auto libraries = Materials::MaterialManager::getManager().getLibraries();
    for (const auto& library : *libraries) {
        auto materialTree =
            Materials::MaterialManager::getManager().getMaterialTree(*library,
                                                                     _filter,
                                                                     _filterOptions);

        bool showLibraries = _filterOptions.includeEmptyLibraries();
        if (!_filterOptions.includeEmptyLibraries() && materialTree->size() > 0) {
            showLibraries = true;
        }

        if (showLibraries) {
            auto lib = new QStandardItem(library->getName());
            lib->setFlags(Qt::ItemIsEnabled);
            addExpanded(model, lib, param);

            auto icon = MaterialsEditor::getIcon(library);
            QIcon folderIcon(QStringLiteral(":/icons/folder.svg"));

            addMaterials(*lib, materialTree, folderIcon, icon, param);
        }
    }
}

void MaterialTreeWidget::addExpanded(QStandardItem* parent, QStandardItem* child)
{
    parent->appendRow(child);
    m_materialTree->setExpanded(child->index(), true);
}

void MaterialTreeWidget::addExpanded(QStandardItem* parent,
                                     QStandardItem* child,
                                     const Base::Reference<ParameterGrp>& param)
{
    parent->appendRow(child);

    // Restore to any previous expansion state
    auto expand = param->GetBool(child->text().toStdString().c_str(), true);
    m_materialTree->setExpanded(child->index(), expand);
}

void MaterialTreeWidget::addExpanded(QStandardItemModel* model, QStandardItem* child)
{
    model->appendRow(child);
    m_materialTree->setExpanded(child->index(), true);
}

void MaterialTreeWidget::addExpanded(QStandardItemModel* model,
                                     QStandardItem* child,
                                     const Base::Reference<ParameterGrp>& param)
{
    model->appendRow(child);

    // Restore to any previous expansion state
    auto expand = param->GetBool(child->text().toStdString().c_str(), true);
    m_materialTree->setExpanded(child->index(), expand);
}

void MaterialTreeWidget::addRecents(QStandardItem* parent)
{
    for (auto& uuid : _recents) {
        try {
            auto material = getMaterialManager().getMaterial(uuid);
            auto icon = MaterialsEditor::getIcon(material->getLibrary());
            auto card = new QStandardItem(cardIcon(uuid, icon), material->getName());
            card->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            card->setData(QVariant(uuid), Qt::UserRole);
            card->setToolTip(cardToolTip(uuid));

            addExpanded(parent, card);
        }
        catch (const Materials::MaterialNotFound&) {
        }
    }
}

void MaterialTreeWidget::addFavorites(QStandardItem* parent)
{
    for (auto& uuid : _favorites) {
        try {
            auto material = getMaterialManager().getMaterial(uuid);
            auto icon = MaterialsEditor::getIcon(material->getLibrary());
            auto card = new QStandardItem(cardIcon(uuid, icon), material->getName());
            card->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            card->setData(QVariant(uuid), Qt::UserRole);
            card->setToolTip(cardToolTip(uuid));

            addExpanded(parent, card);
        }
        catch (const Materials::MaterialNotFound&) {
        }
    }
}
QIcon MaterialTreeWidget::cardIcon(const QString& uuid, const QIcon& fallback)
{
    // A card that states no appearance has nothing to render -- every one
    // of them would come back the same default grey sphere, which says
    // less than the library icon it would replace.
    try {
        auto material = getMaterialManager().getMaterial(uuid);
        if (!material || !material->hasAppearanceProperties()) {
            return fallback;
        }
        // A hatch pattern states a 2D fill and no surface, so there is
        // nothing to render it as: all 32 of the bundled ones carry the
        // same default appearance and would come back the same grey
        // sphere. Its swatch is drawn flat and bundled instead.
        if (!material->hasAppearanceModel(Materials::ModelUUIDs::ModelUUID_Rendering_Basic)
            && (material->hasAppearanceModel(Materials::ModelUUIDs::ModelUUID_Patterns_PAT)
                || material->hasAppearanceModel(
                    Materials::ModelUUIDs::ModelUUID_Patterns_PatternFile))) {
            QIcon swatch = MaterialIcons::instance().patternIcon(uuid, material->getName());
            return swatch.isNull() ? fallback : swatch;
        }
        QIcon icon = MaterialIcons::instance().icon(uuid,
                                                    material->getMaterialAppearance(),
                                                    material->getName(),
                                                    material->getRenderProperties());
        if (!icon.isNull()) {
            return icon;
        }
    }
    catch (const Materials::MaterialNotFound&) {
    }
    // Not rendered yet: the library icon stands in until iconReady() says
    // otherwise, so the tree paints immediately either way.
    return fallback;
}

QString MaterialTreeWidget::cardToolTip(const QString& uuid)
{
    // The tree can only afford so many pixels a row, and a material icon
    // is a picture whose whole job is to be looked at -- so the tooltip
    // shows the same render again at the size a tooltip can spare, with
    // what the card says about itself beside it. Same shape as
    // Gui::Action::createToolTip and the same knob for the size, since
    // it answers the same question about the same kind of picture.
    try {
        auto material = getMaterialManager().getMaterial(uuid);
        if (!material) {
            return {};
        }
        const QString name = material->getName();
        const QString description = material->getDescription();

        QString image;
        const int extent = int(Gui::ViewParams::getToolTipIconSize());
        if (extent > 0) {
            // Rich text names a FILE, which is why MaterialIcons keeps
            // the path it resolved: bundled resource, user override and
            // rendered cache entry are all somewhere on disk or in the
            // binary, and all three work here unchanged.
            const QString path = MaterialIcons::instance().iconPath(uuid);
            if (!path.isEmpty()) {
                image = QStringLiteral("<img src='%1' width='%2' height='%2'"
                                       " style='float:right; margin-left:0.6em;'/>")
                            .arg(path.toHtmlEscaped())
                            .arg(extent);
            }
        }

        QString tip = image
            + QStringLiteral("<p style='white-space:pre; margin:0 0 0.4em 0;'><b>%1</b></p>")
                  .arg(name.toHtmlEscaped());
        if (!description.isEmpty()
            && description.compare(name, Qt::CaseInsensitive) != 0) {
            tip += QStringLiteral("<p style='margin:0;'>%1</p>")
                       .arg(description.toHtmlEscaped());
        }
        return tip;
    }
    catch (const Materials::MaterialNotFound&) {
    }
    return {};
}

void MaterialTreeWidget::refreshIcon(const QString& uuid)
{
    auto* model = static_cast<QStandardItemModel*>(m_materialTree->model());
    if (!model) {
        return;
    }
    auto restate = [this, &uuid](QStandardItem* item) {
        auto material = getMaterialManager().getMaterial(uuid);
        item->setIcon(MaterialIcons::instance().icon(
            uuid, material->getMaterialAppearance(), material->getName(),
            material->getRenderProperties()));
        // The tooltip names the icon by path, and there was no path
        // until this render landed.
        item->setToolTip(cardToolTip(uuid));
    };
    std::function<void(QStandardItem*)> walk = [&](QStandardItem* item) {
        for (int row = 0; row < item->rowCount(); ++row) {
            QStandardItem* child = item->child(row);
            if (!child) {
                continue;
            }
            if (child->data(Qt::UserRole).toString() == uuid) {
                restate(child);
            }
            walk(child);
        }
    };
    for (int row = 0; row < model->rowCount(); ++row) {
        if (QStandardItem* item = model->item(row)) {
            if (item->data(Qt::UserRole).toString() == uuid) {
                restate(item);
            }
            walk(item);
        }
    }
}

void MaterialTreeWidget::addMaterials(
    QStandardItem& parent,
    const std::shared_ptr<std::map<QString, std::shared_ptr<Materials::MaterialTreeNode>>>&
        modelTree,
    const QIcon& folderIcon,
    const QIcon& icon,
    const Base::Reference<ParameterGrp>& param)
{
    auto childParam = param->GetGroup(parent.text().toStdString().c_str());
    for (auto& mat : *modelTree) {
        auto nodePtr = mat.second;
        if (nodePtr->getType() == Materials::MaterialTreeNode::NodeType::DataNode) {
            QString uuid = nodePtr->getUUID();

            auto card = new QStandardItem(cardIcon(uuid, icon), mat.first);
            card->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            card->setData(QVariant(uuid), Qt::UserRole);
            card->setToolTip(cardToolTip(uuid));

            addExpanded(&parent, card);
        }
        else {
            auto node = new QStandardItem(folderIcon, mat.first);
            addExpanded(&parent, node, childParam);
            node->setFlags(Qt::ItemIsEnabled);
            auto treeMap = nodePtr->getFolder();
            addMaterials(*node, treeMap, folderIcon, icon, childParam);
        }
    }
}

void MaterialTreeWidget::onSelectMaterial(const QItemSelection& selected,
                                          const QItemSelection& deselected)
{
    Q_UNUSED(deselected);

    if (selected.isEmpty()) {
        m_uuid.clear();
        return;
    }

    // Get the UUID before changing the underlying data model
    QString uuid;
    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    QModelIndexList indexes = selected.indexes();
    for (auto it = indexes.begin(); it != indexes.end(); it++) {
        QStandardItem* item = model->itemFromIndex(*it);

        if (item) {
            uuid = item->data(Qt::UserRole).toString();
            break;
        }
    }

    updateMaterial(uuid);
    std::string _uuid = uuid.toStdString();

    if (uuid.isEmpty()) {
        return;
    }
    if (uuid == m_leadingId) {
        Q_EMIT leadingEntrySelected();
        return;
    }
    Q_EMIT materialSelected(getMaterialManager().getMaterial(uuid));
    Q_EMIT onMaterial(uuid);
}

void MaterialTreeWidget::onDoubleClick(const QModelIndex& index)
{
    auto model = qobject_cast<QStandardItemModel*>(m_materialTree->model());
    auto item = model->itemFromIndex(index);

    if (item) {
        auto uuid = item->data(Qt::UserRole).toString();
        updateMaterial(uuid);
    }
}

void MaterialTreeWidget::onFilter(const QString& text)
{
    setActiveFilter(text);
}

void MaterialTreeWidget::saveWidgetSettings()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget");
    param->SetBool("WidgetExpanded", m_expanded);
}

void MaterialTreeWidget::saveMaterialTree()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/TreeWidget/MaterialTree");
    param->Clear();

    auto tree = m_materialTree;
    auto model = qobject_cast<QStandardItemModel*>(tree->model());

    auto root = model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); i++) {
        auto child = root->child(i);
        saveMaterialTreeChildren(param, tree, model, child);
    }
}

void MaterialTreeWidget::saveMaterialTreeChildren(const Base::Reference<ParameterGrp>& param,
                                                  QTreeView* tree,
                                                  QStandardItemModel* model,
                                                  QStandardItem* item)
{
    if (item->hasChildren()) {
        param->SetBool(item->text().toStdString().c_str(), tree->isExpanded(item->index()));

        auto treeParam = param->GetGroup(item->text().toStdString().c_str());
        for (int i = 0; i < item->rowCount(); i++) {
            auto child = item->child(i);

            saveMaterialTreeChildren(treeParam, tree, model, child);
        }
    }
}

// --------------------------------------------------------------------

PrefMaterialTreeWidget::PrefMaterialTreeWidget(QWidget* parent)
    : MaterialTreeWidget(parent)
    , PrefWidget()
{
    // Every preference widget in this fork opts into the global auto-apply
    // setting from its constructor; without this the material picker is the
    // one page that only writes its choice when the dialog is accepted.
    setAutoSave(Gui::PrefParam::AutoSave());
}

PrefMaterialTreeWidget::~PrefMaterialTreeWidget() = default;

void PrefMaterialTreeWidget::setAutoSave(bool enable)
{
    autoSave(enable, this, &MaterialTreeWidget::materialSelected);
}

void PrefMaterialTreeWidget::restorePreferences()
{
    if (getWindowParameter().isNull()) {
        failedToRestore(objectName());
        return;
    }

    const char* defaultUuid = "7f9fd73b-50c9-41d8-b7b2-575a030c1eeb";
    QString uuid = QString::fromStdString(getWindowParameter()->GetASCII(entryName(), defaultUuid));
    setMaterial(uuid);
}

void PrefMaterialTreeWidget::savePreferences()
{
    if (getWindowParameter().isNull()) {
        failedToSave(objectName());
        return;
    }

    getWindowParameter()->SetASCII(entryName(), getMaterialUUID().toStdString());
}
