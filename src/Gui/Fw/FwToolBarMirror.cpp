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

#include "PreCompiled.h"

#include <algorithm>

#include <QAction>
#include <QActionEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QTextDocumentFragment>
#include <QToolBar>
#include <QWidgetAction>

#include <Base/Console.h>

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "Fw/FwQtView.h"
#include "Fw/FwStore.h"
#include "Fw/FwToolBarMirror.h"
#include "Fw/FwWidgets.h"
#include "MainWindow.h"
#include "ToolBarManager.h"

using namespace Gui;
using namespace Gui::Fw;

namespace
{
const QString kModelRef = QStringLiteral("IPY_MODEL_");

/// Write the keys of `values` that differ from the bag, as native code
/// would (one `propertiesChanged`, the fan-out's `update`), WITHOUT the
/// backend hearing it: the values come from the real action, and the
/// bound view would only write them back to it (QAction::setIcon emits
/// `changed` unconditionally, which would round once more).
void writeDiff(Widget* model, const QVariantMap& values)
{
    QVariantMap diff;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (model->property(it.key()) != it.value())
            diff.insert(it.key(), it.value());
    }
    if (diff.isEmpty())
        return;
    Backend* backend = model->backend();
    model->setBackend(nullptr);
    model->setProperties(diff, Source::Native);
    model->setBackend(backend);
}

/// Fill the bag silently (the initial state, before the object is
/// announced).
void writeInitial(Widget* model, const QVariantMap& values)
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        model->setInitial(it.key(), it.value());
}

QString plainText(const QString& text)
{
    if (Qt::mightBeRichText(text))
        return QTextDocumentFragment::fromHtml(text).toPlainText().trimmed();
    return text;
}

Command* commandByName(const QString& name)
{
    if (name.isEmpty() || !Application::Instance)
        return nullptr;
    return Application::Instance->commandManager().getCommandByName(name.toUtf8().constData());
}

/// The command a member action belongs to, or nullptr for a plain one.
Command* commandOfMember(::QAction* member)
{
    if (auto action = qobject_cast<Action*>(member->parent()))
        return action->command();
    return nullptr;
}

QString pixmapOf(Command* cmd)
{
    if (!cmd)
        return QString();
    const char* px = cmd->getPixmap();
    return px ? QString::fromUtf8(px) : QString();
}
}  // namespace

ToolBarMirror& ToolBarMirror::instance()
{
    static ToolBarMirror mirror;
    return mirror;
}

ToolBarMirror::ToolBarMirror()
{
    _rebuildTimer.setSingleShot(true);
    _rebuildTimer.setInterval(0);
    connect(&_rebuildTimer, &QTimer::timeout, this, &ToolBarMirror::rebuild);
    _flushTimer.setSingleShot(true);
    _flushTimer.setInterval(0);
    connect(&_flushTimer, &QTimer::timeout, this, &ToolBarMirror::flush);
}

ToolBarMirror::~ToolBarMirror() = default;

bool ToolBarMirror::owns(const QString& id)
{
    return id == listId() || id.startsWith(QLatin1String("cmd:"))
        || id.startsWith(QLatin1String("toolbar:")) || id.startsWith(QLatin1String("widget:"))
        || id.startsWith(QLatin1String("action:"));
}

void ToolBarMirror::start()
{
    if (_running)
        return;
    if (!getMainWindow() || !ToolBarManager::getInstance()) {
        Base::Console().Warning("ToolBarMirror: no main window to mirror\n");
        return;
    }
    _running = true;
    _rebuilds = 0;
    connect(ToolBarManager::getInstance(), &ToolBarManager::toolBarsChanged, this,
            &ToolBarMirror::scheduleRebuild, Qt::UniqueConnection);
    if (Application::Instance) {
        _connWorkbench = Application::Instance->signalActivateWorkbench.connect(
            [this](const char*) { scheduleRebuild(); }, fastsignals::advanced_tag {});
    }
    rebuild();
}

void ToolBarMirror::stop()
{
    if (!_running)
        return;
    _running = false;
    _rebuildTimer.stop();
    _flushTimer.stop();
    _connWorkbench.disconnect();
    if (ToolBarManager::getInstance())
        disconnect(ToolBarManager::getInstance(), nullptr, this, nullptr);
    for (::QToolBar* tb : _filtered) {
        tb->removeEventFilter(this);
        disconnect(tb, nullptr, this, nullptr);
    }
    _filtered.clear();
    for (::QAction* a : _watched)
        disconnect(a, nullptr, this, nullptr);
    _watched.clear();
    _dirty.clear();
    Store& store = Store::instance();
    // every id out first (the referrers before what they refer to, so a
    // subscriber never sees a dangling ref), then the objects, children
    // first: a widget item is its bar's child, a bar the list's
    if (_list)
        store.release(listId());
    for (auto it = _bars.constBegin(); it != _bars.constEnd(); ++it)
        store.release(barId(it.key()));
    for (auto it = _barWidgets.constBegin(); it != _barWidgets.constEnd(); ++it)
        for (const auto& w : it.value())
            if (w)
                store.release(store.idOf(w));
    for (auto it = _models.constBegin(); it != _models.constEnd(); ++it)
        if (it.value())
            store.release(store.idOf(it.value()));
    for (auto it = _barWidgets.constBegin(); it != _barWidgets.constEnd(); ++it)
        for (const auto& w : it.value())
            if (w)
                delete w.data();
    _barWidgets.clear();
    for (auto it = _bars.constBegin(); it != _bars.constEnd(); ++it)
        if (it.value())
            delete it.value().data();
    _bars.clear();
    _barContent.clear();
    if (_list)
        delete _list.data();
    for (auto it = _models.constBegin(); it != _models.constEnd(); ++it)
        if (it.value())
            delete it.value().data();
    _models.clear();
    _commands.clear();
    _order.clear();
}

void ToolBarMirror::scheduleRebuild()
{
    if (_running && !_rebuildTimer.isActive())
        _rebuildTimer.start();
}

bool ToolBarMirror::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionRemoved)
        scheduleRebuild();
    return QObject::eventFilter(watched, event);
}

void ToolBarMirror::markDirty(::QAction* action)
{
    if (!_running || _rebuilding)
        return;
    _dirty.insert(action);
    if (!_flushTimer.isActive())
        _flushTimer.start();
}

void ToolBarMirror::flush()
{
    QSet<::QAction*> dirty;
    dirty.swap(_dirty);
    for (::QAction* a : dirty) {
        auto it = _models.find(a);
        if (it != _models.end() && it.value())
            refresh(a, it.value(), false);
    }
}

void ToolBarMirror::watchAction(::QAction* real)
{
    if (_watched.contains(real))
        return;
    _watched.insert(real);
    connect(real, &::QAction::changed, this, [this, real]() { markDirty(real); });
    connect(real, &QObject::destroyed, this, [this, real]() {
        _watched.remove(real);
        _dirty.remove(real);
        _commands.remove(real);
        auto it = _models.find(real);
        if (it != _models.end()) {
            if (it.value()) {
                Store::instance().release(Store::instance().idOf(it.value()));
                delete it.value().data();
            }
            _models.erase(it);
        }
        scheduleRebuild();
    });
}

QString ToolBarMirror::areaOf(::QToolBar* bar)
{
    MainWindow* mw = getMainWindow();
    QWidget* parent = bar->parentWidget();
    if (parent && parent != mw) {
        const QString name = parent->objectName();
        if (name == QLatin1String("StatusBarArea"))
            return QStringLiteral("statusbar");
        if (name == QLatin1String("MenuBarLeftArea"))
            return QStringLiteral("menubar-left");
        if (name == QLatin1String("MenuBarRightArea"))
            return QStringLiteral("menubar-right");
        return QStringLiteral("floating");
    }
    if (!mw)
        return QStringLiteral("floating");
    switch (mw->toolBarArea(bar)) {
        case Qt::TopToolBarArea:
            return QStringLiteral("top");
        case Qt::LeftToolBarArea:
            return QStringLiteral("left");
        case Qt::RightToolBarArea:
            return QStringLiteral("right");
        case Qt::BottomToolBarArea:
            return QStringLiteral("bottom");
        default:
            return QStringLiteral("floating");
    }
}

QString ToolBarMirror::iconNameOf(::QAction* real, const QString& command, int index)
{
    Command* cmd = commandByName(command);
    if (!cmd)
        return QString();
    if (index > 0) {
        if (auto group = qobject_cast<ActionGroup*>(cmd->getAction())) {
            const QList<::QAction*> members = group->actions();
            if (index <= members.size())
                return pixmapOf(commandOfMember(members.at(index - 1)));
        }
        return QString();
    }
    if (auto group = qobject_cast<ActionGroup*>(cmd->getAction())) {
        // the group's face is its default member's
        const QList<::QAction*> members = group->actions();
        const int def = group->property("defaultAction").toInt();
        if (def >= 0 && def < members.size()) {
            const QString px = pixmapOf(commandOfMember(members.at(def)));
            if (!px.isEmpty())
                return px;
        }
    }
    Q_UNUSED(real)
    return pixmapOf(cmd);
}

void ToolBarMirror::refresh(::QAction* real, Fw::QAction* model, bool initial)
{
    const CommandRef ref = _commands.value(real);
    Command* cmd = commandByName(ref.name);
    QVariantMap v;
    v.insert(QStringLiteral("text"), real->text());
    v.insert(QStringLiteral("icon"), iconNameOf(real, ref.name, ref.index));
    QString tip;
    Command* tipOwner = ref.index > 0 ? commandByName(ref.memberCommand) : cmd;
    if (tipOwner && tipOwner->getToolTipText() && *tipOwner->getToolTipText()) {
        const char* context = dynamic_cast<PythonCommand*>(tipOwner) ? tipOwner->getName()
                                                                     : tipOwner->className();
        tip = QCoreApplication::translate(context, tipOwner->getToolTipText());
    }
    else {
        tip = plainText(real->toolTip());
    }
    v.insert(QStringLiteral("toolTip"), tip);
    v.insert(QStringLiteral("statusTip"), plainText(real->statusTip()));
    v.insert(QStringLiteral("shortcut"), real->shortcut().toString(QKeySequence::NativeText));
    v.insert(QStringLiteral("enabled"), real->isEnabled());
    v.insert(QStringLiteral("visible"), real->isVisible());
    v.insert(QStringLiteral("checkable"), real->isCheckable());
    v.insert(QStringLiteral("checked"), real->isChecked());
    v.insert(QStringLiteral("separator"), real->isSeparator());
    if (ref.index == 0 && cmd) {
        if (auto group = qobject_cast<ActionGroup*>(cmd->getAction())) {
            const QList<::QAction*> members = group->actions();
            QVariantList refs;
            for (int k = 1; k <= members.size(); ++k)
                refs.append(kModelRef + commandId(ref.name, k));
            v.insert(QStringLiteral("members"), refs);
            const QVariant def = group->property("defaultAction");
            v.insert(QStringLiteral("defaultAction"), def.isValid() ? def.toInt() : -1);
            v.insert(QStringLiteral("exclusive"), group->isExclusive());
            v.insert(QStringLiteral("dropDown"), group->hasDropDownMenu());
        }
    }
    if (initial)
        writeInitial(model, v);
    else
        writeDiff(model, v);
}

Fw::QAction* ToolBarMirror::modelOf(::QAction* real, const QString& id, const QString& command,
                                    int index, const QString& memberCommand)
{
    auto it = _models.find(real);
    if (it != _models.end() && it.value())
        return it.value();
    auto model = new Fw::QAction;
    if (!command.isEmpty()) {
        // the binding identity, silently: the model is not claiming to
        // have set anything
        model->setInitial(QStringLiteral("command"), command);
        model->setInitial(QStringLiteral("commandIndex"), index);
        model->setInitial(QStringLiteral("memberCommand"), memberCommand);
    }
    model->setInitial(QStringLiteral("objectName"), real->objectName());
    // bound by pointer, not through the command write filter: a client's
    // write to a mirrored action is the desktop user's own click
    FwQt::bindAction(model, real);
    _models.insert(real, model);
    CommandRef ref;
    ref.name = command;
    ref.index = index;
    ref.memberCommand = memberCommand;
    _commands.insert(real, ref);
    refresh(real, model, true);
    watchAction(real);
    Store::instance().adopt(id, model);
    return model;
}

void ToolBarMirror::rebuildCommandMap()
{
    // the real actions the commands own: the command's own action, and a
    // group's members by index (a member may be another command's own
    // action too: that command's entry stands, the group's index is the
    // member's identity only inside the group's `members`)
    if (!Application::Instance)
        return;
    QHash<::QAction*, CommandRef> fresh;
    for (const auto& pair : Application::Instance->commandManager().getCommands()) {
        Command* cmd = pair.second;
        Action* action = cmd ? cmd->getAction() : nullptr;
        if (!action || !action->action())
            continue;
        const QString name = QString::fromUtf8(cmd->getName());
        auto group = qobject_cast<ActionGroup*>(action);
        if (group) {
            const QList<::QAction*> members = group->actions();
            for (int k = 0; k < members.size(); ++k) {
                ::QAction* m = members.at(k);
                if (fresh.contains(m))
                    continue;
                CommandRef ref;
                ref.name = name;
                ref.index = k + 1;
                if (Command* owner = commandOfMember(m))
                    ref.memberCommand = QString::fromUtf8(owner->getName());
                fresh.insert(m, ref);
            }
        }
        CommandRef own;
        own.name = name;
        fresh.insert(action->action(), own);  // a command's own wins over a group slot
    }
    // keep the refs of the actions already modelled (their ids are out)
    for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
        if (!_commands.contains(it.key()))
            _commands.insert(it.key(), it.value());
    }
}

Fw::QAction* ToolBarMirror::commandModel(::QAction* real)
{
    auto ref = _commands.constFind(real);
    if (ref == _commands.constEnd())
        return nullptr;
    Fw::QAction* model = modelOf(real, commandId(ref->name, ref->index), ref->name, ref->index,
                                 ref->memberCommand);
    if (ref->index == 0) {
        // a group's members get their models with the group, in order
        if (Command* cmd = commandByName(ref->name)) {
            if (auto group = qobject_cast<ActionGroup*>(cmd->getAction())) {
                const QList<::QAction*> members = group->actions();
                for (int k = 0; k < members.size(); ++k) {
                    ::QAction* m = members.at(k);
                    if (_models.contains(m) && _models.value(m))
                        continue;
                    CommandRef mref;
                    mref.name = ref->name;
                    mref.index = k + 1;
                    if (Command* owner = commandOfMember(m))
                        mref.memberCommand = QString::fromUtf8(owner->getName());
                    _commands.insert(m, mref);
                    modelOf(m, commandId(ref->name, k + 1), ref->name, k + 1, mref.memberCommand);
                }
                // the members exist now: the group's own refs resolve
                refresh(real, model, false);
            }
        }
    }
    return model;
}

void ToolBarMirror::rebuild()
{
    if (!_running || !getMainWindow() || !ToolBarManager::getInstance())
        return;
    _rebuilding = true;
    ++_rebuilds;
    Store& store = Store::instance();
    MainWindow* mw = getMainWindow();
    ToolBarManager* mgr = ToolBarManager::getInstance();
    rebuildCommandMap();

    // the bars, in the desktop's order: by area, then by geometry for
    // the shown ones, the hidden ones after them in declared order
    struct Entry
    {
        QString name;
        ::QToolBar* bar;
        int areaRank;
        int row;
        int x;
        int declared;
    };
    static const QStringList areaOrder {
        QStringLiteral("top"), QStringLiteral("left"), QStringLiteral("right"),
        QStringLiteral("bottom"), QStringLiteral("statusbar"), QStringLiteral("menubar-left"),
        QStringLiteral("menubar-right"), QStringLiteral("floating")};
    QList<Entry> entries;
    int declared = 0;
    for (const auto& pair : mgr->toolBars()) {
        ::QToolBar* tb = pair.second;
        if (!tb)
            continue;
        // a bar of another workbench stays in the window, hidden, with
        // its toggle action hidden too (setup's hide loop, setState's
        // ForceHidden): not the desktop user's to show, so not a
        // client's either -- it leaves the mirror and comes back with
        // its workbench, which is the one snapshot-level diff a switch
        // or a sketch edit makes
        if (!tb->toggleViewAction()->isVisible())
            continue;
        Entry e;
        e.name = pair.first;
        e.bar = tb;
        e.areaRank = areaOrder.indexOf(areaOf(tb));
        const bool shown = tb->isVisibleTo(mw);
        const QPoint pos = mw->isAncestorOf(tb) ? tb->mapTo(mw, QPoint(0, 0)) : tb->pos();
        e.row = shown ? pos.y() : 1 << 30;
        e.x = shown ? pos.x() : declared;
        e.declared = declared++;
        entries.append(e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.areaRank != b.areaRank)
            return a.areaRank < b.areaRank;
        if (a.row != b.row)
            return a.row < b.row;
        if (a.x != b.x)
            return a.x < b.x;
        return a.declared < b.declared;
    });

    QSet<QString> seen;
    QStringList order;
    for (const Entry& e : entries) {
        ::QToolBar* tb = e.bar;
        const QString id = barId(e.name);
        seen.insert(e.name);
        order.append(id);
        if (!_filtered.contains(tb)) {
            _filtered.insert(tb);
            tb->installEventFilter(this);
            connect(tb, &::QToolBar::visibilityChanged, this,
                    [this](bool) { scheduleRebuild(); });
            connect(tb, &QObject::destroyed, this, [this, tb]() {
                _filtered.remove(tb);
                scheduleRebuild();
            });
        }
        // the content: models first (a subscriber sees them before the
        // bar that refers to them), then the signature
        QStringList signature;
        QList<QPointer<Widget>> items;   // what each entry refers to (null: separator)
        QList<QPointer<Widget>> fresh;   // the widget/plain models made for this bar
        const QList<::QAction*> actions = tb->actions();
        for (int n = 0; n < actions.size(); ++n) {
            ::QAction* a = actions.at(n);
            if (a->isSeparator()) {
                signature.append(QStringLiteral("sep"));
                items.append(QPointer<Widget>());
                continue;
            }
            if (auto wa = qobject_cast<QWidgetAction*>(a)) {
                QWidget* real = wa->defaultWidget();
                const QString cls = real ? QString::fromUtf8(real->metaObject()->className())
                                         : QStringLiteral("QWidgetAction");
                const QString wid = QStringLiteral("widget:%1#%2").arg(e.name).arg(n);
                signature.append(QStringLiteral("widget:%1:%2").arg(cls, wid));
                Widget* w = store.object(wid);
                if (!w) {
                    w = new Widget;
                    w->setQtClass(cls);
                    w->setInitial(QStringLiteral("objectName"),
                                  real ? real->objectName() : a->objectName());
                    w->setInitial(QStringLiteral("visible"), a->isVisible());
                    w->setInitial(QStringLiteral("enabled"), a->isEnabled());
                    w->setInitial(QStringLiteral("toolTip"), plainText(a->toolTip()));
                    store.adopt(wid, w);
                }
                fresh.append(w);
                items.append(w);
                continue;
            }
            if (Fw::QAction* m = commandModel(a)) {
                signature.append(store.idOf(m));
                items.append(m);
                continue;
            }
            // a plain action no command owns
            const QString aid = QStringLiteral("action:%1#%2").arg(e.name).arg(n);
            signature.append(aid);
            Fw::QAction* m = modelOf(a, aid, QString(), 0, QString());
            items.append(m);
        }

        auto bit = _bars.find(e.name);
        Fw::QToolBar* bar = bit != _bars.end() ? bit.value().data() : nullptr;
        const bool isNew = !bar;
        if (!bar) {
            bar = new Fw::QToolBar;
            bar->setInitial(QStringLiteral("objectName"), e.name);
            _bars.insert(e.name, bar);
        }
        QVariantMap v;
        v.insert(QStringLiteral("windowTitle"), tb->windowTitle());
        v.insert(QStringLiteral("visible"), tb->isVisibleTo(mw));
        v.insert(QStringLiteral("area"), areaOf(tb));
        v.insert(QStringLiteral("orientation"), static_cast<int>(tb->orientation()));
        v.insert(QStringLiteral("iconSize"), tb->iconSize().width());
        v.insert(QStringLiteral("toolButtonStyle"), static_cast<int>(tb->toolButtonStyle()));
        v.insert(QStringLiteral("movable"), tb->isMovable());
        v.insert(QStringLiteral("floatable"), tb->isFloatable());
        v.insert(QStringLiteral("enabled"), tb->isEnabled());
        if (isNew)
            writeInitial(bar, v);
        else
            writeDiff(bar, v);

        const bool contentChanged = _barContent.value(e.name) != signature;
        if (contentChanged) {
            // the widget models this bar no longer holds go
            for (const auto& old : _barWidgets.value(e.name)) {
                if (old && !fresh.contains(old)) {
                    store.release(store.idOf(old));
                    delete old.data();
                }
            }
            _barWidgets.insert(e.name, fresh);
            bar->clear();
            for (const auto& item : items) {
                if (!item)
                    bar->addSeparator();
                else if (qobject_cast<Fw::QAction*>(item))
                    bar->addAction(item);
                else
                    bar->addWidget(item);
            }
            _barContent.insert(e.name, signature);
        }
        if (isNew)
            store.adopt(id, bar);
        else if (contentChanged)
            store.notifyLayout(id);
    }

    // the bars that went
    for (auto it = _bars.begin(); it != _bars.end();) {
        if (seen.contains(it.key())) {
            ++it;
            continue;
        }
        store.release(barId(it.key()));
        for (const auto& old : _barWidgets.value(it.key())) {
            if (old) {
                store.release(store.idOf(old));
                delete old.data();
            }
        }
        _barWidgets.remove(it.key());
        _barContent.remove(it.key());
        if (it.value())
            delete it.value().data();
        it = _bars.erase(it);
    }

    // the order, once
    if (order != _order || !_list) {
        const bool isNew = !_list;
        if (!_list) {
            _list = new Widget;
            _list->setQtClass(QStringLiteral("QMainWindow"));
            _list->setInitial(QStringLiteral("objectName"), listId());
            new Layout(Layout::Bar, _list);
        }
        Layout* lay = _list->layout();
        lay->clear();
        for (const QString& id : order) {
            if (Widget* bar = store.object(id))
                lay->addWidget(bar);
        }
        _order = order;
        if (isNew)
            store.adopt(listId(), _list);
        else
            store.notifyLayout(listId());
    }
    _rebuilding = false;
    _dirty.clear();
    Q_EMIT rebuilt();
}

#include "moc_FwToolBarMirror.cpp"
