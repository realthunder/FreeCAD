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

#ifndef GUI_FW_PANELMIRROR_H
#define GUI_FW_PANELMIRROR_H

/* The panel mirror (docs/Sandbox.md 7.19): the desktop's task panel --
 * the real widget tree `Control().showDialog` put up, whatever
 * workbench built it, C++ or Python, unmodified -- as models in the
 * store, for a subscriber that renders them somewhere else (the
 * browser's DOM tier, docs/ThinClient.md 8.11).  The workbench's slots,
 * event filters, timers and Coin trackers keep running on the host,
 * where the real widget is; the client sees a reflection and writes
 * back through the store into the real widget, whose own signals run
 * the panel's slots.
 *
 * Ids: `panel` is the list object whose layout is the dialogs up, in
 * order (one until nested modals join it); `panel:<n>` a dialog root (a
 * `QDialog` model whose `qtClass` is the TaskDialog's class name), n
 * minted per process and never reused; `pw:<n>` every mirrored widget,
 * likewise.  A root's layout is its `TaskBox`es (each a `QGroupBox`
 * model with `qtClass` "Gui::TaskView::TaskBox") and the task view's
 * button box (a `QDialogButtonBox` model whose buttons are `QPushButton`
 * models carrying `standardButton`); a box's layout is the walk of its
 * content: the real layout tree as `Layout`s of the same kind, the
 * widgets as the model their class chain lands on through
 * `Fw::createWidget`, bound to the real widget by `FwQt::View::bind`.
 *
 * The bag is kept current from the widget's own evidence: an event
 * filter marks a widget dirty on Paint, Show, Hide and the change
 * events, and a 0 ms flush re-reads the dirty widgets' keys through the
 * meta-object and writes only what differs.  Structure -- children
 * added or removed, a layout request -- schedules a re-walk on a 0 ms
 * tick, diffed against the real -> model map: new widgets opened, gone
 * ones released, a changed container re-sent as one layout update.
 *
 * Opens go out in reference order with the root last, so a subscriber
 * mounts once; the root's close implies the subtree (the children are
 * released without a message).  Runs only while something asked
 * (`start`), stops with the last subscriber (`stop`).
 *
 * M2: an item view's rows are REFLECTED (`FwQt::View::reflectItems`):
 * the real model's rows go out in the open's `items` and change as
 * item ops, a client's item op lands in the real model.  A leaf the
 * class table does not know and that has nothing inside (a custom-
 * painted widget) is a PICTURE: a `QLabel` model with the real class as
 * `qtClass` and a `pixmap` key holding an `img:` id from `ImageStore`,
 * re-grabbed on the widget's repaint (rate-capped per widget, the
 * device pixel ratio ignored, the longest side capped), a new id sent
 * only when the bytes changed, so many pictures per panel at most.
 * Button icons and label pixmaps travel the same way.
 */

#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <functional>

#include <fastsignals/signal.h>

#include <FCGlobal.h>

class QLayout;
class QWidget;

namespace Gui
{
namespace Fw
{

class Layout;
class Widget;

class GuiExport PanelMirror : public QObject
{
    Q_OBJECT
public:
    static PanelMirror& instance();

    /// Follow the task panel (idempotent): the dialog up now is mirrored
    /// at once, later ones as `Control` shows them.
    void start();
    /// Take every mirrored object out of the store.
    void stop();
    bool isRunning() const
    {
        return _running;
    }
    /// Whether a store id is one of the mirror's.
    static bool owns(const QString& id);
    static QString listId()
    {
        return QStringLiteral("panel");
    }
    /// The id of the dialog root up now, or empty.
    QString panelId() const;

    /// Mirror a dialog's content now: `dialogClass` the TaskDialog's
    /// class name, `contents` its boxes (what `getDialogContent` gives),
    /// `buttons` the task view's button box or nullptr.  What the
    /// `Control` hook does on its tick; a test calls it directly.  A
    /// dialog already up is hidden first.
    void show(const QString& dialogClass, const QList<QWidget*>& contents,
              ::QDialogButtonBox* buttons);
    /// Close the dialog up (the root's close message; the subtree goes
    /// with it, silently).
    void hide();
    /// Re-walk the dialog now (what the structure triggers do on a tick).
    void rebuild();
    /// Re-read the dirty widgets now (what the flush timer does).
    void flush();
    /// How many walks ran since `start` (tests).
    int rebuildCount() const
    {
        return _rebuilds;
    }
    /// The model of a real widget, or nullptr when it is not mirrored.
    Widget* modelOf(QWidget* widget) const;
    /// Whether a real widget is mirrored as a picture (M2).
    bool isPicture(QWidget* widget) const
    {
        return _pictures.contains(widget);
    }
    int pictureCount() const
    {
        return _pictures.size();
    }
    /// The picture quotas: the longest side of a grab, the least time
    /// between two grabs of one widget, how many pictures a panel holds.
    static int maxPictureSide()
    {
        return 1024;
    }
    static int grabIntervalMs()
    {
        return 100;
    }
    static int maxPictures()
    {
        return 32;
    }
    /// How many grabs ran since `start` (tests).
    int grabCount() const
    {
        return _grabs;
    }

Q_SIGNALS:
    void shown(const QString& id);
    void hidden(const QString& id);
    void rebuilt();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PanelMirror();
    ~PanelMirror() override;

    struct Walk;
    void scheduleRebuild();
    void scheduleFlush();
    void markDirty(QWidget* widget);
    void onShowDialog(QWidget* view, const std::vector<QWidget*>& contents);
    void onRemoveDialog();
    bool allowed(const QString& dialogClass) const;
    void walk(Walk& walk);
    Widget* mirrorWidget(Walk& walk, QWidget* real, Widget* parentModel, bool& isNew);
    void buildContent(Walk& walk, QWidget* real, Widget* model, QStringList& signature);
    Layout* buildLayout(Walk& walk, QLayout* real, Widget* owner, QStringList& signature);
    Widget* newModel(QWidget* real, Widget* parentModel, bool picture);
    /// Grab a picture leaf now: its image id, or a null string when the
    /// grab was deferred (rate cap) or the widget has nothing to show.
    QString grabPicture(QWidget* real);
    void forgetPicture(QWidget* real);
    void bindModel(QWidget* real, Widget* model);
    void watchWidget(QWidget* real);
    void unwatchWidget(QWidget* real);
    void releaseModel(QWidget* real, bool announce);
    QVariantMap read(QWidget* real, Widget* model) const;
    void refresh(QWidget* real, Widget* model, bool initial);
    void onModelWritten(QWidget* real, Widget* model, const QStringList& names, int source);
    void onModelRequest(QWidget* real, Widget* model, const QString& name,
                        const QVariantList& args);
    void onRootRequest(const QString& name, const QVariantList& args);
    void withoutBackends(const std::function<void()>& fn);

    bool _running = false;
    bool _walking = false;
    bool _relaying = false;
    bool _grabbing = false;
    bool _pictureCapTold = false;
    int _rebuilds = 0;
    int _grabs = 0;
    QTimer _rebuildTimer;
    QTimer _flushTimer;
    QTimer _showTimer;
    QTimer _grabTimer;
    QElapsedTimer _clock;
    /// the picture leaves, when each was last grabbed, which wait for
    /// the rate cap
    QSet<QWidget*> _pictures;
    QHash<QWidget*, qint64> _grabbedAt;
    QSet<QWidget*> _pendingGrabs;
    /// the dialog up: its class, boxes and button box
    QString _dialogClass;
    QList<QPointer<QWidget>> _contents;
    QPointer<::QDialogButtonBox> _buttons;
    QPointer<Widget> _list;
    QPointer<Widget> _root;
    QString _rootId;
    /// real widget -> its model
    QHash<QWidget*, QPointer<Widget>> _models;
    /// a container model -> the signature its layout was sent with
    QHash<Widget*, QStringList> _signatures;
    QSet<QWidget*> _watched;
    QSet<QWidget*> _dirty;
    fastsignals::advanced_scoped_connection _connShow;
    fastsignals::advanced_scoped_connection _connRemove;
};

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_PANELMIRROR_H
