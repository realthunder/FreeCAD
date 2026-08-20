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
#include <QMenu>


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
    if (!m_expanded) {
        // When not expanded, the size height is the same as m_material
        QSize size = m_material->sizeHint();
        size.setWidth(minimumWidth);
        return size;
    }
    return QWidget::sizeHint();
}

QSize MaterialTreeWidget::treeSizeHint() const
{
    return m_treeSizeHint;
}

void MaterialTreeWidget::setTreeSizeHint(const QSize& hint)
{
    m_treeSizeHint = hint;
    m_materialTree->setMinimumSize(m_treeSizeHint);
    m_materialTree->adjustSize();
    adjustSize();
}

void MaterialTreeWidget::createLayout()
{
    m_material = new QLineEdit(this);
    m_expand = new QPushButton(this);
    m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
    m_materialTree = new QTreeView(this);
    m_filterCombo = new QComboBox(this);
    m_editor = new QPushButton(tr("Launch Editor"), this);

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
    const int extent = qBound(MinIconExtent,
                              int(treeParam->GetInt("IconSize", DefaultIconExtent)),
                              MaxIconExtent);
    m_materialTree->setIconSize(QSize(extent, extent));

    auto materialLayout = new QHBoxLayout();
    materialLayout->addWidget(m_material);
    materialLayout->addWidget(m_expand);
    // materialLayout->setSizeConstraint(QLayout::SetMinimumSize);

    auto treeLayout = new QHBoxLayout();
    treeLayout->addWidget(m_materialTree);

    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_filterCombo);
    buttonLayout->addItem(new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Preferred));
    buttonLayout->addWidget(m_editor);

    auto layout = new QVBoxLayout();
    layout->setContentsMargins(0, 9, 0, 9);
    layout->addItem(materialLayout);
    layout->addItem(treeLayout);
    layout->addItem(buttonLayout);
    setLayout(layout);

    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

    // Set the filter if using a filter list
    if (hasMultipleFilters()) {
        _filter = *_filterList->front();
    }

    fillFilterCombo();

    // Start in the previous expanded state
    auto expanded = treeParam->GetBool("WidgetExpanded", false);
    setExpanded(expanded);

    connect(m_expand, &QPushButton::clicked, this, &MaterialTreeWidget::expandClicked);
    connect(m_editor, &QPushButton::clicked, this, &MaterialTreeWidget::editorClicked);
    connect(m_filterCombo,
            &QComboBox::currentTextChanged,
            this,
            &MaterialTreeWidget::onFilter);
}

void MaterialTreeWidget::setExpanded(bool open)
{
    m_materialTree->setVisible(open);
    m_editor->setVisible(open);

    setFilterVisible(open);

    m_expanded = open;

    if (open) {
        m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarShadeButton));
    }
    else {
        m_expand->setIcon(style()->standardIcon(QStyle::SP_TitleBarUnshadeButton));
    }

    // m_materialTree->adjustSize();
    adjustSize();
    Q_EMIT onExpanded(m_expanded);
}

void MaterialTreeWidget::setFilterVisible(bool open)
{
    if (open && hasMultipleFilters()) {
        m_filterCombo->setVisible(true);
    }
    else {
        m_filterCombo->setVisible(false);
    }
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

void MaterialTreeWidget::setMaterial(const QString& uuid)
{
    if (uuid.isEmpty()) {
        // Nothing is selected
        QItemSelectionModel* selectionModel = m_materialTree->selectionModel();
        selectionModel->clear();
        m_material->clear();

        return;
    }

    updateMaterial(uuid);

    // Now select the material in the tree
    auto index = findInTree(uuid);
    if (index.isValid()) {
        QItemSelectionModel* selectionModel = m_materialTree->selectionModel();
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
        QIcon icon = MaterialIcons::instance().icon(uuid,
                                                    material->getMaterialAppearance(),
                                                    material->getName());
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
            uuid, material->getMaterialAppearance(), material->getName()));
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

    if (!uuid.isEmpty()) {
        Q_EMIT materialSelected(getMaterialManager().getMaterial(uuid));
        Q_EMIT onMaterial(uuid);
    }
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
