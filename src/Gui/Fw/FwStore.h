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

#ifndef GUI_FW_STORE_H
#define GUI_FW_STORE_H

/* The host widget layer (docs/Sandbox.md 7.12): the store of the
 * sandbox guest's objects.
 *
 * The guest's `freecad.widgets` models each open a Jupyter comm whose
 * state is the Qt properties under a `q_` prefix (docs/Sandbox.md
 * 7.11).  The store creates the host class of each model by name and
 * keeps it by comm id: a guest `update` is `setProperties` from
 * `Source::Guest` (its keys and `_touched` list), a guest `custom` is
 * a request to the backend (`setFocus`, `setParent`, ...) or a layout
 * op on the owning widget, and what a backend writes into the bag or
 * emits as an event goes back to the guest through the sink -- an
 * `update` with the `q_` keys, or a `custom` `{"event", "args"}`.
 *
 * The store knows nothing of the toolkit or of Python: the sink and
 * the message decoding live with the bridge (src/Gui/SandboxGui.cpp),
 * the rendering with a backend (FwQtView.h).  A native form never
 * enters the store; it is plain objects a dialog owns.
 */

#include <functional>

#include <QHash>
#include <QPointer>
#include <QSet>

#include "FwCore.h"

namespace Gui
{
namespace Fw
{

class GuiExport Store : public QObject
{
    Q_OBJECT
public:
    static Store& instance();

    /// The guest module whose models the store answers to.
    static const char* moduleName()
    {
        return "freecad.widgets";
    }
    /// Whether a comm's opening state is one of ours (`_model_module`).
    static bool owns(const QVariantMap& state);

    // -- guest -> host ----------------------------------------------------

    /// A comm opened with `state` (the full model state): the object of
    /// its `_model_name`, the `q_` values written silently, `_touched`
    /// as the guest lists it, `qtClass`, and for a form its `uiFile` and
    /// `widgets` map.  Replaces an object already under `id`.
    Widget* commOpen(const QString& id, const QVariantMap& state);
    /// A state diff from the guest.  False when `id` is not ours.
    bool commUpdate(const QString& id, const QVariantMap& state);
    /// A custom message from the guest's model.  False when not ours.
    bool commCustom(const QString& id, const QVariantMap& content);
    /// The comm closed: the object goes (a backend detaches on destroyed).
    bool commClose(const QString& id);

    // -- host -> guest ----------------------------------------------------

    /// `method` is "update" (content: the `q_` state) or "custom".
    using Sink = std::function<void(const QString& id, const QString& method,
                                    const QVariantMap& content)>;
    void setSink(Sink sink)
    {
        _sink = std::move(sink);
    }
    bool hasSink() const
    {
        return static_cast<bool>(_sink);
    }

    // -- the fan-out and host producers (docs/Sandbox.md 7.18 (a)) --------

    /// A host producer's object (the tool bar mirror's) under a synthetic
    /// id: watched like a comm's, announced as `open`, kept across a
    /// guest `reset()`, and never closed by the guest (`commClose` of an
    /// adopted id answers false).  Replaces an object already under `id`.
    void adopt(const QString& id, Widget* widget, bool announce = true);
    /// Announce an object adopted quietly (`adopt(id, w, false)`) as
    /// `open` now: a producer that adopts a whole tree first, so every
    /// object's `parent` ref resolves, and announces it in reference
    /// order after.  Nothing for an id that is not adopted.
    void announceOpen(const QString& id);
    bool isAdopted(const QString& id) const
    {
        return _adopted.contains(id);
    }
    /// Take an adopted object out (announced as `close`); the producer
    /// deletes it.  False when `id` is not adopted.
    /// `announce` false takes the object out without a `close` message:
    /// a subtree whose root's close implies it (the panel mirror's).
    bool release(const QString& id, bool announce = true);
    /// The object's whole state for a late subscriber: `{"model",
    /// "qtClass", "state" (the q_ keys), "layout" (Layout::spec with
    /// the refs as IPY_MODEL_ strings, absent without one), "parent"
    /// (a ref, or absent)}`.  Empty for an unknown id.
    QVariantMap snapshot(const QString& id) const;
    /// Every id, an object AFTER the objects it references (its
    /// layout's items, refs in its properties -- not its parent, which
    /// names it through its own layout and so comes after it): the
    /// order a subscriber can rebuild in.
    QStringList snapshotOrder() const;
    /// A streamed client's write: `commUpdate` from `Source::Client`
    /// under `origin`, so the fan-out can skip the writer; a bound view
    /// applies it to the real widget (a `Backend` write it would not).
    bool applyUpdate(const QString& id, const QVariantMap& state, quint64 origin);
    /// A streamed client's request (`{"event", "args"}`), likewise.
    bool applyCustom(const QString& id, const QVariantMap& content, quint64 origin);
    /// Send the object's layout out (an `update` whose content is
    /// `{"layoutSpec": spec}`): a producer whose layout changed.
    void notifyLayout(const QString& id);

    /// Who is writing: a tag carried on every `message` the write
    /// causes.  0 is the desktop (and the guest); a stream sets its
    /// client's id while it applies that client's write.
    class GuiExport OriginScope
    {
    public:
        explicit OriginScope(quint64 origin);
        ~OriginScope();

    private:
        quint64 _saved;
    };
    static quint64 currentOrigin();

    // -- the objects --------------------------------------------------------

    Widget* object(const QString& id) const;
    /// The comm id of a store object; empty for a native one.
    QString idOf(const QObject* widget) const;
    /// The object an `IPY_MODEL_<id>` reference (or a bare id) names.
    Widget* resolve(const QString& ref) const;
    QStringList ids() const;
    int count() const
    {
        return _objects.size();
    }
    /// Drop every object (a guest reset).
    void reset();

    struct Stats
    {
        int opened = 0;
        int updated = 0;
        int customs = 0;
        int closed = 0;
        int sent = 0;
        int events = 0;
    };
    const Stats& stats() const
    {
        return _stats;
    }
    void resetStats()
    {
        _stats = Stats();
    }

Q_SIGNALS:
    void objectOpened(Gui::Fw::Widget* widget);
    void objectClosing(Gui::Fw::Widget* widget);
    /// Everything that leaves the store, for any number of subscribers
    /// (the sink is one: the guest).  `method` is "open" (content: the
    /// snapshot), "update" (the q_ keys written, or a `layoutSpec`),
    /// "custom" (`{"event", "args"}`) or "close" (empty); `origin` is
    /// the writer's tag (see OriginScope) -- a subscriber streaming to
    /// that writer skips the message.
    void message(const QString& id, const QString& method, const QVariantMap& content,
                 quint64 origin);

private:
    Store();
    void applyState(Widget* w, const QVariantMap& state, bool initial,
                    Source source = Source::Guest);
    void watch(Widget* w, const QString& id);
    void insert(const QString& id, Widget* w);
    void announce(const QString& id, const QString& method, const QVariantMap& content);

    QHash<QString, QPointer<Widget>> _objects;
    QHash<const QObject*, QString> _ids;
    QSet<QString> _adopted;
    Sink _sink;
    Stats _stats;
    /// an item op the guest's own comm is applying (not echoed to it)
    bool _guestItemOp = false;
    /// adopted quietly and not announced yet: nothing about such an
    /// object goes out before its open (a mirror registers a whole
    /// tree, then announces it in reference order)
    QSet<QString> _pending;
};

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_STORE_H
