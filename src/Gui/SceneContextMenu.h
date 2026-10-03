/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef GUI_SCENECONTEXTMENU_H
#define GUI_SCENECONTEXTMENU_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <App/DocumentObserver.h>
#include <FCGlobal.h>
#include <Gui/Renderer/ClientAccess.h>

class QAction;
class QMenu;

namespace Gui
{

class ViewProviderDocumentObject;

/** A browser's right-click menu: the desktop's 3D-view context menu, built
 * on the host for one client and sent as JSON (docs/ThinClient.md sec
 * 8.11b).
 *
 * The menu is the desktop's own: the active workbench's "View" entries
 * and the picked object's submenu (ViewProvider::setupContextMenu), built
 * inside the client's view, so that the selection and the active document
 * those entries consult are the client's. It is held here until the client
 * picks an entry, which runs as the desktop would run it -- in the client's
 * view again -- and under the same rule as the control channel's own ops:
 * what a connection that is not a host may run is what the `edit` and
 * `command` ops admit, everything else is sent marked refused.
 */
class GuiExport SceneContextMenu: public QObject
{
    Q_OBJECT

public:
    /// What an entry is, which decides who may run it and where
    enum class Kind
    {
        /// A command's own action (Command::getAction()); judged as the
        /// `command` op judges it
        Command,
        /// A view provider's edit-mode entry (the receiver it was given);
        /// run as the `edit` op runs one
        Edit,
        /// Leave the edit the object is in, as the tree's entry does
        FinishEdit,
        /// A camera command the browser has of its own: never run here
        Local,
        /// One of "Pick geometry"'s: select what the ray went through, in
        /// the client's selection, as a pick does
        Pick,
        /// Anything else, code the host runs as it is -- possibly modal
        Action,
    };

    /// For \a client of the served document \a doc, about \a target (empty
    /// for a click on nothing), with \a picks everything the click's ray went
    /// through, for "Pick geometry"
    SceneContextMenu(uint64_t client, const std::string& doc, const App::SubObjectT& target,
                     std::vector<App::SubObjectT> picks = {}, QObject* parent = nullptr);
    ~SceneContextMenu() override;

    /// Build the menu in the client's view and describe it, every entry
    /// judged for \a access. The target's own submenu comes first, as on
    /// the desktop.
    QJsonArray build(Render::ClientAccess access);

    /// Run entry \a item. Null on success, else an error code and, in
    /// \a message, why. \a extend adds a pick to the selection rather than
    /// replacing it, as Ctrl does on the desktop's.
    const char* trigger(int item, Render::ClientAccess access, QString& message,
                        bool extend = false);

    int token() const
    {
        return _token;
    }
    uint64_t client() const
    {
        return _client;
    }
    const App::SubObjectT& target() const
    {
        return _target;
    }

    /// Whether \a kind (running \a command, for a command) may run on a
    /// connection of \a access, and if not, why
    static bool allowed(Kind kind, const QString& command, Render::ClientAccess access,
                        QString* reason = nullptr);

    /// The browser's own camera action a command stands for ("fitAll",
    /// "isometric", "front" ...), empty for none
    static QString localAction(const QString& command);

private Q_SLOTS:
    /// The receiver of a view provider's edit-mode entries
    void onEditEntry();

private:
    struct Entry
    {
        QPointer<QAction> action;
        Kind kind = Kind::Action;
        QString command;
        /// An edit entry run by its mode alone, not its own code
        /// (ViewProviderDocumentObject::EditEntryProperty)
        bool direct = false;
        /// A pick's index in _picks
        int pick = -1;
    };
    /// Enter the target's edit mode \a mode under the edit op's rules
    void runEdit(int mode);
    /// "Pick geometry", put after \a after (the target's own submenu), or
    /// first
    void addPickMenu(QAction* after);
    QJsonObject describeEntry(QAction* action, int id, Render::ClientAccess access);
    /// Every entry of the menu built, judged for \a access
    QJsonArray describe(Render::ClientAccess access);
    ViewProviderDocumentObject* targetViewProvider() const;

    uint64_t _client;
    std::string _doc;
    int _token;
    App::SubObjectT _target;
    std::vector<App::SubObjectT> _picks;
    /// The view provider the target's entries were built from: an entry
    /// is run only while the target still resolves to it, since an entry's
    /// code may hold it
    ViewProviderDocumentObject* _builtFrom = nullptr;
    std::unique_ptr<QMenu> _menu;
    QPointer<QAction> _finishEdit;
    /// The menu is the client's edit mode's own (ViewProvider::editContextMenu)
    bool _inEdit = false;
    QHash<int, Entry> _entries;
    /// What the last edit entry answered, for trigger()'s reply
    QJsonObject _editReply;
};

/// Register the context menu's control ops: `contextMenu` (build one for a
/// ray), `contextMenu.trigger` and `contextMenu.close`. Idempotent.
GuiExport void installSceneContextMenuOps();

/// Forget \a client's menu: it left.
GuiExport void dropSceneContextMenu(uint64_t client);

}  // namespace Gui

#endif  // GUI_SCENECONTEXTMENU_H
