/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef GUI_OMNI_SEARCH_H
#define GUI_OMNI_SEARCH_H

#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QByteArray>
#include <QSortFilterProxyModel>
#include <QString>

#include <FCGlobal.h>
#include <App/DocumentObserver.h>
#include <App/ObjectIdentifier.h>

class QWidget;

namespace App {
class Document;
class DocumentObject;
class Property;
struct ParamInfo;
}

namespace Gui {

/** The search behind the omni search box, without the box.
 *
 * Three searches share one input grammar: a prefix picks the mode and the
 * rest is the query. Everything here is plain data or a Qt model -- the
 * floating box (OmniSearchBox) is one consumer, and the browser viewer's
 * control channel is meant to be another, so nothing in this namespace
 * touches a widget except createParamEditor(), which builds one.
 *
 * See docs/OmniSearch.md.
 */
namespace OmniSearch {

enum class Mode {
    /// "/" typed, no mode chosen yet
    Chooser,
    /// "/ query": documents, objects, sub-objects and properties
    Object,
    /// "/cmd query": registered commands
    Command,
    /// "/param query": generated application parameters
    Param,
};

struct Input {
    Mode mode = Mode::Chooser;
    /// The text after the prefix
    QString query;
    /// Where query starts in the full text, for splicing completions back
    int offset = 0;
};

/// The prefix that selects a mode, "/ ", "/cmd " or "/param "
GuiExport const char *modePrefix(Mode mode);

/** Split the box's text into mode and query.
 *
 * Text starting with a full prefix is that mode. A lone "/" or a
 * partial prefix ("/cm") is the chooser. Text that does not start with
 * "/" is an object query as typed.
 */
GuiExport Input parseInput(const QString &text);

/// What an object query resolved to
struct ObjectMatch {
    /// The (sub-)object named; empty for a document member (doc is set then)
    App::SubObjectT obj;
    /** The document a "#." query addressed: prop is then a property of
     * the document itself or of its active 3D view
     */
    App::Document *doc = nullptr;
    /// Set when the query named a property of the object
    App::Property *prop = nullptr;
    App::ObjectIdentifier path;
    /** The properties to edit together: prop, then the same-named property
     * of the same type on every other local object of a ".Name" query
     */
    std::vector<App::Property*> props;
};

/** Resolve an object query against an owner object.
 *
 * The owner is what App::ObjectIdentifier::parse() reads the query
 * relative to; any object of the document to search will do. A query
 * naming a real property yields a property match; otherwise the query
 * is taken as an object path, the way the tree's search box does it.
 * False when the text parses to neither.
 *
 * A query with a leading '.' names a member of the local objects -- the
 * selection -- when locals is given: it resolves on the owner (which is
 * expected to be the first of them) and ObjectMatch::props collects the
 * property from every local object that has it with the same type; with
 * an empty list such a query resolves to nothing. Without locals the
 * owner is the local object, as in an expression.
 *
 * A '#' addresses a document: "#" alone the owner's, "Doc#" or "<<Label>>#"
 * a named one. "#.Comment" names a property of the document itself and
 * "#.ActiveView.DrawStyle" one of its active 3D view (ObjectMatch::doc
 * is set, obj empty); "#Box.Length" is Box of that document, as the
 * expression grammar's "Doc#Box.Length". "Box.ViewObject.ShapeColor"
 * names a property of the object's view provider (the pseudo property
 * ViewObject followed by one name), for a sub-object and for ".ViewObject.X"
 * on the selection alike.
 */
GuiExport bool resolveObject(const QString &query, App::DocumentObject *owner, ObjectMatch &out,
                             const std::vector<App::DocumentObject*> *locals = nullptr);

/// A row the "#." completer offers
struct MemberMatch {
    /// The property name, or "ActiveView." for the row that opens the view's members
    QString name;
    /// The property documentation
    QString description;
};

/** The members a "#." head offers: for "#." or "Doc#." the document's
 * properties plus "ActiveView." when the document has a 3D view; for
 * "#.ActiveView." that view's properties. Hidden properties are left
 * out. Empty when head is not of that form or names no document.
 */
GuiExport std::vector<MemberMatch> documentMembers(const QString &head, App::DocumentObject *owner);

/** Split a "#." query for completion: head is "Doc#." or "Doc#.ActiveView."
 * (what documentMembers() takes) and tail the member name typed so far.
 * False for any other query, and once the tail has a dot of its own.
 */
GuiExport bool splitMemberQuery(const QString &query, QString &head, QString &tail);

struct CommandMatch {
    QByteArray name;
    QString title;
    QString description;
    QString shortcut;
    bool group = false;
    bool active = false;
};

/// Commands whose title, name, shortcut or description carry every keyword
GuiExport std::vector<CommandMatch> searchCommands(const QString &query);

struct ParamMatch {
    const App::ParamInfo *info = nullptr;
    /// ParamRegistry::getValue() at the time of the search
    std::string value;
};

/// Parameters whose path, name, title or documentation carry every keyword
GuiExport std::vector<ParamMatch> searchParams(const QString &query);

/** A Gui::PrefWidget editing the parameter, bound and restored.
 *
 * The widget is the one the parameter's preference page would show --
 * chosen from ParamInfo::proxy -- or, for a parameter without a proxy the
 * registry can describe, a basic one for its value type. It reads the
 * stored value; call its onSave() (through Gui::PrefWidget) to store an
 * edit.
 */
GuiExport QWidget *createParamEditor(const App::ParamInfo &info, QWidget *parent);

/// Show a value, in ParamRegistry::getValue() form, in an editor createParamEditor() built
GuiExport void setParamEditorValue(QWidget *widget, const App::ParamInfo &info, const std::string &value);

/** Item roles shared by the omni search models.
 *
 * The numbers line up with CommandListModel::Roles so that one delegate
 * and one keyword filter serve every list the box shows.
 */
enum Roles {
    TitleRole = Qt::UserRole + 1,
    DescriptionRole,
    ShortcutRole,
    IsGroupRole,
    IsActiveRole,
    SearchTextRole,
    /// quintptr, the App::ParamInfo of a parameter row
    ParamInfoRole,
    /// QString, the parameter's full path
    ParamPathRole,
    /// QString, the parameter's current value
    ParamValueRole,
};

} // namespace OmniSearch

/// One row per registered parameter, in registration order
class GuiExport ParamListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ParamListModel(QObject *parent = nullptr);

    /// Re-read the registry, after a library loaded more parameters
    void refresh();

    const App::ParamInfo *info(const QModelIndex &index) const;
    /// The ParamInfo behind an index of this model or of a proxy over it
    static const App::ParamInfo *infoOf(const QModelIndex &index);

    QVariant data(const QModelIndex &index, int role) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

private:
    std::vector<const App::ParamInfo*> entries;
};

/** Keeps the rows whose search text carries every keyword.
 *
 * Case-insensitive substring match of each whitespace-separated keyword,
 * App::ParamRegistry::matchKeywords(), over the source model's search
 * role (OmniSearch::SearchTextRole by default). No keywords keeps every
 * row.
 */
class GuiExport KeywordFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit KeywordFilterModel(QObject *parent = nullptr);

    void setKeywords(const QString &query);
    const std::vector<std::string> &keywords() const { return keys; }

    void setSearchRole(int role) { searchRole = role; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    std::vector<std::string> keys;
    int searchRole = OmniSearch::SearchTextRole;
};

} // namespace Gui

#endif // GUI_OMNI_SEARCH_H
