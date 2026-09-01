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

#pragma once

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>

#include <FCGlobal.h>

#include <Base/Parameter.h>
#include <Gui/PrefWidgets.h>
#include <Gui/WidgetFactory.h>


#include <Mod/Material/App/MaterialFilter.h>
#include <Mod/Material/App/MaterialFilterPy.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/Materials.h>

namespace MatGui
{
class WidgetFactoryInst;
class MaterialTreeWidgetPy;

/** The Material Tree widget class
 * This widget is intended for use wherever materials are used. It is a light weight
 * alternative to the full Materials editor.
 *
 * The widget itself is the combination of a number of smaller widgets. A simple text
 * field shows any currently selected material. An arrow will expand a tree to show
 * the widget library, allowing the user to select the material they require.
 *
 * When expanded, the user will be presented the option to launch the full material
 * editor. This will allow them to create/copy/modify as required.
 *
 * Additionally, they will be given the option to create a material card based on the
 * current settings.
 *
 * \author David Carter
 */
class MatGuiExport MaterialTreeWidget: public QWidget, public Base::BaseClass
{
    Q_OBJECT
    Q_PROPERTY(QSize treeSizeHint READ treeSizeHint WRITE
                   setTreeSizeHint)  // clazy:exclude=qproperty-without-notify

    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    explicit MaterialTreeWidget(const Materials::MaterialFilter& filter,
                                QWidget* parent = nullptr);
    explicit MaterialTreeWidget(
        const std::shared_ptr<std::list<std::shared_ptr<Materials::MaterialFilter>>>& filterList,
        QWidget* parent = nullptr);
    explicit MaterialTreeWidget(QWidget* parent = nullptr);
    ~MaterialTreeWidget() override;

    /** How big a card's icon is drawn here, in pixels
     *
     * The user's IconSize preference, clamped to what is legible. Public
     * because the other picture-lists in the same dialogs -- the surface
     * finishes above this one -- have to be drawn at the same size for
     * the dialog to read as one thing.
     */
    static int iconExtent();

    /** Set the material by specifying its UUID
     */
    void setMaterial(const QString& uuid);
    /** Put one synthetic row at the top of the list, above every library
     *
     * A look picker needs a way back to "whatever the object's material
     * card says", and that way has to sit in the list itself rather than
     * only on a button beside it (docs/MaterialStorage.md 15.5). The row
     * is not a card: it carries \a id where a card carries its UUID, and
     * choosing it emits leadingEntrySelected() instead of
     * materialSelected(). Pass an empty \a id to take the row away.
     */
    void setLeadingEntry(const QString& id, const QString& text, const QString& toolTip = {});
    /** get the material UUID
     */
    QString getMaterialUUID() const;
    /** Set the material filter
     */
    void setFilter(const Materials::MaterialFilter& filter);
    void setFilter(
        const std::shared_ptr<std::list<std::shared_ptr<Materials::MaterialFilter>>>& filterList);
    void setActiveFilter(const QString& name);

    void setExpanded(bool open);
    bool getExpanded()
    {
        return m_expanded;
    }

    /* Indicates if we should show favourite materials
     */
    bool includeFavorites() const
    {
        return _filterOptions.includeFavorites();
    }
    void setIncludeFavorites(bool value)
    {
        _filterOptions.setIncludeFavorites(value);
    }

    /* Indicates if we should show recent materials
     */
    bool includeRecent() const
    {
        return _filterOptions.includeRecent();
    }
    void setIncludeRecent(bool value)
    {
        _filterOptions.setIncludeRecent(value);
    }

    /* Indicates if we should include empty folders
     */
    bool includeEmptyFolders() const
    {
        return _filterOptions.includeEmptyFolders();
    }
    void setIncludeEmptyFolders(bool value)
    {
        _filterOptions.setIncludeEmptyFolders(value);
    }

    /* Indicates if we should include empty libraries
     */
    bool includeEmptyLibraries() const
    {
        return _filterOptions.includeEmptyLibraries();
    }
    void setIncludeEmptyLibraries(bool value)
    {
        Base::Console().log("setIncludeEmptyLibraries(%s)\n", (value ? "true" : "false"));
        _filterOptions.setIncludeEmptyLibraries(value);
    }

    /* Indicates if we should include materials in the older format
     */
    bool includeLegacy() const
    {
        return _filterOptions.includeLegacy();
    }
    void setIncludeLegacy(bool legacy)
    {
        _filterOptions.setIncludeLegacy(legacy);
    }

    QSize sizeHint() const override;
    QSize treeSizeHint() const;
    void setTreeSizeHint(const QSize& hint);

Q_SIGNALS:
    /** Emits this signal when a material has been selected */
    void materialSelected(const std::shared_ptr<Materials::Material>& material);
    /** Emits this signal when the synthetic top row has been chosen
     *
     * There is no card behind it, so materialSelected() cannot carry it.
     */
    void leadingEntrySelected();
    void onMaterial(const QString& uuid);
    void onExpanded(bool expanded);

private Q_SLOTS:
    void expandClicked(bool checked);
    void editorClicked(bool checked);
    void onSelectMaterial(const QItemSelection& selected, const QItemSelection& deselected);
    void onDoubleClick(const QModelIndex& index);
    void onFilter(const QString& text);
    /// A row clicked in the list: a card is the answer and closes it, a
    /// library or folder is a branch to open.
    void onTreeClicked(const QModelIndex& index);

private:
    // UI minimum sizes
    static const int minimumWidth = 250;
    static const int minimumTreeWidth = 250;
    static const int minimumTreeHeight = 500;

    static const int defaultFavorites = 0;
    static const int defaultRecents = 5;

    void setup();

    QLineEdit* m_material;
    QPushButton* m_expand;
    /// The window the list lives in. Everything below is inside it.
    QFrame* m_popup;
    QTreeView* m_materialTree;
    QPushButton* m_editor;
    QComboBox* m_filterCombo;
    bool m_expanded;
    QSize m_treeSizeHint;

    QString m_materialDisplay;
    QString m_uuid;
    /// The synthetic top row: its id, its label and its tip. Empty id,
    /// no row -- see setLeadingEntry().
    QString m_leadingId;
    QString m_leadingText;
    QString m_leadingToolTip;
    /// What the box held before the last keystroke, which is how a
    /// deletion is told from an insertion -- see inlineComplete().
    QString m_typed;

    std::list<QString> _favorites;
    std::list<QString> _recents;
    Materials::MaterialFilter _filter;
    Materials::MaterialFilterTreeWidgetOptions _filterOptions;
    std::shared_ptr<std::list<std::shared_ptr<Materials::MaterialFilter>>> _filterList;
    int _recentMax;
    MaterialTreeWidgetPy* pyTreeWidget {nullptr};

    // friends
    friend class Gui::WidgetFactoryInst;

protected:
    Materials::MaterialManager& getMaterialManager()
    {
        return Materials::MaterialManager::getManager();
    }

    void getFavorites();

    void getRecents();
    void saveRecents();
    void addRecent(const QString& uuid);
    bool isRecent(const QString& uuid) const;
    void saveWidgetSettings();
    void saveMaterialTreeChildren(const Base::Reference<ParameterGrp>& param,
                                  QTreeView* tree,
                                  QStandardItemModel* model,
                                  QStandardItem* item);
    void saveMaterialTree();

    /** Create the widgets UI objects
     */
    void createLayout();

    /// Put the list on screen under the box, and take it away again.
    void showPopup();
    void hidePopup();
    /** Hide every row that neither matches \a text nor holds something
     * that does, and open the branches that survived.
     *
     * Rows hidden in the view rather than filtered through a proxy
     * model: everything else here reaches for the QStandardItemModel
     * directly (itemFromIndex, invisibleRootItem), and a proxy in
     * between would have made every one of those wrong.
     *
     * \return whether anything under \a parent is still shown.
     */
    bool filterTree(const QModelIndex& parent, const QString& text);
    /// The first card still shown whose name starts with \a text, which
    /// is the one the box completes to. Depth first, so it is the first
    /// one a reader would come to going down the list.
    QModelIndex firstPrefixMatch(const QModelIndex& parent, const QString& text) const;
    /// Write the rest of that card's name after what was typed,
    /// selected, and make it the current row of the list.
    void inlineComplete(const QString& typed);
    /// Typing, arrow keys and Escape in the box; the popup closing
    /// itself when something outside it is clicked.
    bool eventFilter(QObject* watched, QEvent* event) override;

    bool findInTree(const QStandardItem& node, QModelIndex* index, const QString& uuid);
    QModelIndex findInTree(const QString& uuid);
    void updateMaterial(const QString& uuid);
    void createMaterialTree();
    void fillMaterialTree();
    void updateMaterialTree();
    void addExpanded(QStandardItem* parent, QStandardItem* child);
    void addExpanded(QStandardItem* parent,
                     QStandardItem* child,
                     const Base::Reference<ParameterGrp>& param);
    void addExpanded(QStandardItemModel* model, QStandardItem* child);
    void addExpanded(QStandardItemModel* model,
                     QStandardItem* child,
                     const Base::Reference<ParameterGrp>& param);
    void addRecents(QStandardItem* parent);
    void addFavorites(QStandardItem* parent);
    /// The icon a card shows: its own rendered appearance where it has
    /// one and the render has landed, else \a fallback (the library icon).
    QIcon cardIcon(const QString& uuid, const QIcon& fallback);
    /// The card's name and description, with its rendered icon shown
    /// larger than a tree row can afford.
    QString cardToolTip(const QString& uuid);
    /// Restate one card's icon after its render finished.
    void refreshIcon(const QString& uuid);
    void addMaterials(
        QStandardItem& parent,
        const std::shared_ptr<std::map<QString, std::shared_ptr<Materials::MaterialTreeNode>>>&
            modelTree,
        const QIcon& folderIcon,
        const QIcon& icon,
        const Base::Reference<ParameterGrp>& param);
    void setFilterVisible(bool open);
    void fillFilterCombo();
    bool hasMultipleFilters() const
    {
        return (_filterList && _filterList->size() > 1);
    }
};

/**
 * The PrefColorButton class.
 */
class MatGuiExport PrefMaterialTreeWidget: public MaterialTreeWidget, public Gui::PrefWidget
{
    Q_OBJECT

    Q_PROPERTY(QByteArray prefEntry READ entryName WRITE
                   setEntryName)  // clazy:exclude=qproperty-without-notify
    Q_PROPERTY(QByteArray prefPath READ paramGrpPath WRITE
                   setParamGrpPath)  // clazy:exclude=qproperty-without-notify

public:
    explicit PrefMaterialTreeWidget(QWidget* parent = nullptr);
    ~PrefMaterialTreeWidget() override;

    /** This fork's Gui::PrefWidget has a third pure virtual upstream does
     * not (setAutoSave), so without this the class stays abstract and the
     * generated .ui code cannot instantiate it. Saves on the tree's own
     * selection signal.
     */
    void setAutoSave(bool enable) override;

protected:
    // restore from/save to parameters
    void restorePreferences() override;
    void savePreferences() override;
};

}  // namespace MatGui