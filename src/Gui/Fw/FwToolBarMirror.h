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

#ifndef GUI_FW_TOOLBARMIRROR_H
#define GUI_FW_TOOLBARMIRROR_H

/* The tool bar mirror (docs/Sandbox.md 7.18): the desktop's native
 * tool bars -- the ToolBarManager's live QAction set -- as `QToolBar`
 * and `QAction` models in the store, for a subscriber that renders
 * them somewhere else (the browser's DOM tier, docs/ThinClient.md
 * 8.11).
 *
 * Ids are the client's keys: `cmd:<Command name>` for a command's
 * action, `cmd:<group>#<k>` for the member k of a group (k the model's
 * `commandIndex`, 1-based over ActionGroup::actions()), `toolbar:<name>`
 * for a bar (the ToolBarManager key), `widget:<bar>#<n>` for a widget
 * action (the workbench combo, a spin box), `action:<bar>#<n>` for a
 * plain action no command owns, and `toolbars` -- the list object whose
 * bar layout is the bars in the desktop's order.
 *
 * A command's model is bound to the real action the existing way
 * (`command` / `commandIndex`, `FwQt::realizeAction`), so a client's
 * write goes model -> action; the mirror keeps the model current from
 * the action's `changed`, coalesced per 0 ms tick, writing only what
 * differs.  Structure -- the bar set, a bar's content, the order --
 * is rebuilt on `ToolBarManager::toolBarsChanged`, on a workbench
 * activation, on a bar's visibility change and on actions added to or
 * removed from a bar; a rebuild closes the bars that went, opens the
 * new ones, re-sends a changed bar's layout and the order once.
 *
 * Runs only while something asked (`start`), stops with the last
 * subscriber (`stop`): a desktop with no client mirrors nothing.
 */

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <fastsignals/signal.h>

#include <FCGlobal.h>

class QAction;
class QToolBar;

namespace Gui
{
namespace Fw
{

class QAction;
class QToolBar;
class Widget;

class GuiExport ToolBarMirror : public QObject
{
    Q_OBJECT
public:
    static ToolBarMirror& instance();

    /// Mirror the tool bars into the store (idempotent).
    void start();
    /// Take every mirrored object out of the store.
    void stop();
    bool isRunning() const
    {
        return _running;
    }
    /// Whether a store id is one of the mirror's.
    static bool owns(const QString& id);
    /// The id of the list object.
    static QString listId()
    {
        return QStringLiteral("toolbars");
    }
    static QString barId(const QString& name)
    {
        return QStringLiteral("toolbar:") + name;
    }
    static QString commandId(const QString& command, int index = 0)
    {
        return index > 0 ? QStringLiteral("cmd:%1#%2").arg(command).arg(index)
                         : QStringLiteral("cmd:") + command;
    }

    /// Rebuild the structure now (what the triggers do on a 0 ms tick).
    void rebuild();
    /// Flush the coalesced action state now.
    void flush();
    /// How many rebuilds ran since `start` (tests).
    int rebuildCount() const
    {
        return _rebuilds;
    }

Q_SIGNALS:
    /// A rebuild finished.
    void rebuilt();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ToolBarMirror();
    ~ToolBarMirror() override;

    void scheduleRebuild();
    void markDirty(::QAction* action);
    void refresh(::QAction* real, Fw::QAction* model, bool initial);
    Fw::QAction* modelOf(::QAction* real, const QString& id, const QString& command, int index,
                         const QString& memberCommand);
    Fw::QAction* commandModel(::QAction* real);
    void watchAction(::QAction* real);
    void rebuildCommandMap();
    static QString iconNameOf(::QAction* real, const QString& command, int index);
    static QString areaOf(::QToolBar* bar);

    bool _running = false;
    bool _rebuilding = false;
    int _rebuilds = 0;
    QTimer _rebuildTimer;
    QTimer _flushTimer;
    /// real action -> its model (commands and plain actions alike)
    QHash<::QAction*, QPointer<Fw::QAction>> _models;
    /// real action -> (command name, member index) for the commands
    struct CommandRef
    {
        QString name;
        int index = 0;         ///< 0 the command's own, k a group member
        QString memberCommand; ///< a member's own command, or empty
    };
    QHash<::QAction*, CommandRef> _commands;
    QSet<::QAction*> _watched;
    QSet<::QAction*> _dirty;
    /// bar name -> its model, and the content signature it was sent with
    QHash<QString, QPointer<Fw::QToolBar>> _bars;
    QHash<QString, QStringList> _barContent;
    QHash<QString, QList<QPointer<Widget>>> _barWidgets;  ///< widget/plain items
    QSet<::QToolBar*> _filtered;
    QPointer<Widget> _list;
    QStringList _order;
    fastsignals::advanced_scoped_connection _connWorkbench;
};

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_TOOLBARMIRROR_H
