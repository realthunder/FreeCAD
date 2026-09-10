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

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QBoxLayout>
#include <QChildEvent>
#include <QComboBox>
#include <QEvent>
#include <QFocusFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSizeGrip>
#include <QSpacerItem>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBox>

#include <App/Application.h>
#include <Base/Console.h>
#include <Base/Parameter.h>

#include "Control.h"
#include "Fw/FwImage.h"
#include "Fw/FwPanelMirror.h"
#include "Fw/FwQtView.h"
#include "Fw/FwStore.h"
#include "Fw/FwWidgets.h"
#include "MainWindow.h"
#include "TaskView/TaskDialog.h"
#include "TaskView/TaskEditControl.h"
#include "TaskView/TaskView.h"

using namespace Gui;
using namespace Gui::Fw;

namespace
{
int& panelSerial()
{
    static int n = 0;
    return n;
}

int& widgetSerial()
{
    static int n = 0;
    return n;
}

/// A dialog button box's button: a push button model that says which
/// standard button it is (`QDialogButtonBox::StandardButton`), so a
/// client keys the dialog's OK, Cancel and Apply by flag.
class PanelButton : public Gui::Fw::QPushButton
{
public:
    explicit PanelButton(Widget* parent)
        : Gui::Fw::QPushButton(parent)
    {
        declare(QStringLiteral("standardButton"), 0);
    }
};

const QSet<QString>& knownSet()
{
    static const QSet<QString> set = [] {
        const QStringList names = knownClasses();
        return QSet<QString>(names.begin(), names.end());
    }();
    return set;
}

/// The class-table name a real widget's meta-object chain lands on
/// (`Gui::PrefCheckBox` -> "QCheckBox", Sketcher's ConstraintView ->
/// "QListWidget", anything else -> "QWidget" in the end).
QString tableClassOf(QWidget* w)
{
    if (qobject_cast<Gui::TaskView::TaskBox*>(w))
        return QStringLiteral("QGroupBox");
    if (qobject_cast<QToolBox*>(w))
        return QStringLiteral("QTabWidget");
    for (const QMetaObject* mo = w->metaObject(); mo; mo = mo->superClass()) {
        const QString name = QString::fromUtf8(mo->className());
        if (knownSet().contains(name))
            return name;
    }
    return QStringLiteral("QWidget");
}

/// Qt's own machinery inside a widget, not the panel's content.
bool internalChild(QWidget* c)
{
    if (c->isWindow() || qobject_cast<::QMenu*>(c) || qobject_cast<QSizeGrip*>(c)
        || qobject_cast<QFocusFrame*>(c))
        return true;
    return c->objectName().startsWith(QLatin1String("qt_"));
}

bool hasContentChildren(QWidget* w)
{
    for (QObject* o : w->children()) {
        if (!o->isWidgetType())
            continue;
        if (!internalChild(static_cast<QWidget*>(o)))
            return true;
    }
    return false;
}

/// Whether the walk goes INTO a widget: a container is walked, a leaf
/// (a control with a model, or a custom-painted widget with nothing
/// inside) is not.
bool isContainer(QWidget* w, const QString& cls)
{
    static const QSet<QString> containers = {
        QStringLiteral("QWidget"),        QStringLiteral("QFrame"),
        QStringLiteral("QGroupBox"),      QStringLiteral("QScrollArea"),
        QStringLiteral("QTabWidget"),     QStringLiteral("QStackedWidget"),
        QStringLiteral("QSplitter"),      QStringLiteral("QDialog"),
        QStringLiteral("QDialogButtonBox"), QStringLiteral("Gui::PrefCheckableGroupBox")};
    if (!containers.contains(cls))
        return false;
    if (cls == QLatin1String("QWidget") || cls == QLatin1String("QFrame"))
        return w->layout() || hasContentChildren(w);
    return true;
}

QString ptrKey(const void* p)
{
    return QString::number(reinterpret_cast<quintptr>(p), 16);
}

QString posKey(const QVariantList& pos)
{
    QStringList parts;
    for (const QVariant& v : pos)
        parts.append(v.toString());
    return parts.join(QLatin1Char(','));
}

/// Write the keys of `values` that differ from the bag as native code
/// would, WITHOUT the backend hearing it: the values come from the real
/// widget, and the bound view would only write them back.
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

void replayKey(QWidget* w, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
}

::QDialogButtonBox* buttonBoxOf(QWidget* view)
{
    if (!view)
        return nullptr;
    for (auto ctrl : view->findChildren<Gui::TaskView::TaskEditControl*>())
        if (auto box = ctrl->findChild<::QDialogButtonBox*>())
            return box;
    return nullptr;
}
}  // namespace

/// One walk's bookkeeping: what it visited, what it made (post-order,
/// the announce order), which containers' layouts changed.
struct PanelMirror::Walk
{
    QSet<QWidget*> visited;
    QList<QPair<QWidget*, Widget*>> created;
    QList<Widget*> changed;
};

PanelMirror& PanelMirror::instance()
{
    static PanelMirror mirror;
    return mirror;
}

PanelMirror::PanelMirror()
{
    _rebuildTimer.setSingleShot(true);
    _rebuildTimer.setInterval(0);
    connect(&_rebuildTimer, &QTimer::timeout, this, &PanelMirror::rebuild);
    _flushTimer.setSingleShot(true);
    _flushTimer.setInterval(0);
    connect(&_flushTimer, &QTimer::timeout, this, &PanelMirror::flush);
    _grabTimer.setSingleShot(true);
    connect(&_grabTimer, &QTimer::timeout, this, [this]() {
        // the grabs the rate cap held back: the last paint of a burst
        // is what the client should see
        QSet<QWidget*> pending;
        pending.swap(_pendingGrabs);
        for (QWidget* w : pending)
            if (_models.contains(w))
                _dirty.insert(w);
        flush();
    });
    _clock.start();
    _showTimer.setSingleShot(true);
    _showTimer.setInterval(0);
    connect(&_showTimer, &QTimer::timeout, this, [this]() {
        Gui::TaskView::TaskDialog* dlg = getMainWindow() ? Control().activeDialog() : nullptr;
        if (!dlg)
            return;
        QList<QWidget*> contents;
        for (const auto& c : _contents)
            if (c)
                contents.append(c.data());
        _contents.clear();
        show(QString::fromUtf8(dlg->metaObject()->className()), contents, _buttons.data());
    });
}

PanelMirror::~PanelMirror() = default;

bool PanelMirror::owns(const QString& id)
{
    return id == listId() || id.startsWith(QLatin1String("panel:"))
        || id.startsWith(QLatin1String("pw:"));
}

QString PanelMirror::panelId() const
{
    return _root ? _rootId : QString();
}

Widget* PanelMirror::modelOf(QWidget* widget) const
{
    return _models.value(widget).data();
}

void PanelMirror::start()
{
    if (_running)
        return;
    _running = true;
    _rebuilds = 0;
    _list = new Widget;
    _list->setQtClass(QStringLiteral("QWidget"));
    _list->setInitial(QStringLiteral("objectName"), listId());
    new Layout(Layout::VBox, _list);
    Store::instance().adopt(listId(), _list);
    if (!getMainWindow())
        return;  // no task view to follow (a test drives `show` itself)
    _connShow = Control().signalShowDialog.connect(
        [this](QWidget* view, std::vector<QWidget*>& contents) { onShowDialog(view, contents); },
        fastsignals::advanced_tag {});
    _connRemove = Control().signalRemoveDialog.connect(
        [this](QWidget*, std::vector<QWidget*>&) { onRemoveDialog(); },
        fastsignals::advanced_tag {});
    if (Gui::TaskView::TaskDialog* dlg = Control().activeDialog()) {
        QList<QWidget*> contents;
        for (QWidget* c : dlg->getDialogContent())
            contents.append(c);
        ::QDialogButtonBox* buttons = nullptr;
        for (auto view : getMainWindow()->findChildren<Gui::TaskView::TaskView*>()) {
            buttons = buttonBoxOf(view);
            if (buttons)
                break;
        }
        show(QString::fromUtf8(dlg->metaObject()->className()), contents, buttons);
    }
}

void PanelMirror::stop()
{
    if (!_running)
        return;
    _showTimer.stop();
    _connShow.disconnect();
    _connRemove.disconnect();
    hide();
    _running = false;
    _rebuildTimer.stop();
    _flushTimer.stop();
    if (_list) {
        Store::instance().release(listId());
        delete _list.data();
    }
}

void PanelMirror::onShowDialog(QWidget* view, const std::vector<QWidget*>& contents)
{
    // the signal comes before the dialog is active and before
    // `modifyStandardButtons` and `open()` ran: walk on the next tick,
    // when the panel is what the desktop user sees
    _contents.clear();
    for (QWidget* c : contents)
        _contents.append(c);
    _buttons = buttonBoxOf(view);
    _showTimer.start();
}

void PanelMirror::onRemoveDialog()
{
    _showTimer.stop();
    _contents.clear();
    hide();
}

bool PanelMirror::allowed(const QString& dialogClass) const
{
    ParameterGrp::handle grp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Fw");
    const QString pref = QString::fromStdString(grp->GetASCII("PanelMirror", "all")).trimmed();
    if (pref.isEmpty() || pref == QLatin1String("all"))
        return true;
    if (pref == QLatin1String("none"))
        return false;
    for (const QString& name : pref.split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (name.trimmed() == dialogClass)
            return true;
    return false;
}

void PanelMirror::show(const QString& dialogClass, const QList<QWidget*>& contents,
                       ::QDialogButtonBox* buttons)
{
    if (!_running)
        start();
    if (_root)
        hide();
    if (!allowed(dialogClass)) {
        Base::Console().Log("PanelMirror: %s not mirrored (Preferences/Fw/PanelMirror)\n",
                            qPrintable(dialogClass));
        return;
    }
    _dialogClass = dialogClass;
    _contents.clear();
    for (QWidget* c : contents)
        if (c)
            _contents.append(c);
    _buttons = buttons;
    _root = createWidget(QStringLiteral("QDialog"), _list);
    _root->setQtClass(dialogClass);
    QString title;
    for (const auto& c : _contents) {
        if (auto box = qobject_cast<Gui::TaskView::TaskBox*>(c.data()))
            title = box->headerText();
        else if (c)
            title = c->windowTitle();
        if (!title.isEmpty())
            break;
    }
    _root->setInitial(QStringLiteral("windowTitle"), title);
    _root->setInitial(QStringLiteral("visible"), true);
    _rootId = QStringLiteral("panel:%1").arg(++panelSerial());
    Store::instance().adopt(_rootId, _root, false);
    connect(_root, &Widget::requested, this, &PanelMirror::onRootRequest);
    rebuild();
}

void PanelMirror::hide()
{
    if (!_root)
        return;
    _rebuildTimer.stop();
    _flushTimer.stop();
    _grabTimer.stop();
    _pictures.clear();
    _grabbedAt.clear();
    _pendingGrabs.clear();
    _pictureCapTold = false;
    Store& store = Store::instance();
    // a client's own request may have closed the dialog (its reject):
    // the close is the desktop's fact and must reach that client too
    Store::OriginScope scope(0);
    const QString id = _rootId;
    // the root's close implies the subtree: one message, the children
    // released without one
    store.release(id, true);
    for (auto it = _models.begin(); it != _models.end(); ++it) {
        unwatchWidget(it.key());
        if (it.value()) {
            const QString wid = store.idOf(it.value());
            if (!wid.isEmpty())
                store.release(wid, false);
        }
    }
    if (_list && _list->layout())
        _list->layout()->removeWidget(_root);
    delete _root.data();  // the models are its descendants
    _root = nullptr;
    _rootId.clear();
    _models.clear();
    _signatures.clear();
    _watched.clear();
    _dirty.clear();
    _contents.clear();
    _buttons = nullptr;
    if (_list)
        store.notifyLayout(listId());
    Q_EMIT hidden(id);
}

void PanelMirror::scheduleRebuild()
{
    if (_root && !_walking && !_rebuildTimer.isActive())
        _rebuildTimer.start();
}

void PanelMirror::scheduleFlush()
{
    if (!_flushTimer.isActive())
        _flushTimer.start();
}

void PanelMirror::markDirty(QWidget* widget)
{
    // a grab renders the widget, which paints: not evidence of a change
    if (!_root || _walking || _grabbing || !_models.contains(widget))
        return;
    _dirty.insert(widget);
    scheduleFlush();
}

bool PanelMirror::eventFilter(QObject* watched, QEvent* event)
{
    if (!watched->isWidgetType())
        return QObject::eventFilter(watched, event);
    auto w = static_cast<QWidget*>(watched);
    if (!_models.contains(w)) {
        // a scroll area (an item view, a text edit) paints its viewport,
        // not itself: the viewport's paint is the view's evidence
        auto area = qobject_cast<QAbstractScrollArea*>(w->parentWidget());
        if (area && area->viewport() == w && _models.contains(area))
            w = area;
    }
    switch (event->type()) {
        case QEvent::Paint:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::EnabledChange:
        case QEvent::ToolTipChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::LanguageChange:
        case QEvent::WindowTitleChange:
        case QEvent::ReadOnlyChange:
        case QEvent::PaletteChange:
            markDirty(w);
            break;
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved: {
            QObject* child = static_cast<QChildEvent*>(event)->child();
            if (child && child->isWidgetType())
                scheduleRebuild();
            break;
        }
        case QEvent::LayoutRequest:
            scheduleRebuild();
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

void PanelMirror::flush()
{
    QSet<QWidget*> dirty;
    dirty.swap(_dirty);
    for (QWidget* w : dirty) {
        auto it = _models.find(w);
        if (it != _models.end() && it.value())
            refresh(w, it.value(), false);
    }
}

// ---- the walk --------------------------------------------------------------------

void PanelMirror::withoutBackends(const std::function<void()>& fn)
{
    // a model's layout is rebuilt from the real one: the bound view must
    // not apply the ops to the real layout it came from
    QList<QPair<QPointer<Widget>, Backend*>> saved;
    for (auto it = _models.constBegin(); it != _models.constEnd(); ++it) {
        if (it.value()) {
            saved.append({it.value(), it.value()->backend()});
            it.value()->setBackend(nullptr);
        }
    }
    fn();
    for (const auto& pair : saved) {
        if (pair.first)
            pair.first->setBackend(pair.second);
    }
}

void PanelMirror::rebuild()
{
    if (!_root)
        return;
    _walking = true;
    ++_rebuilds;
    Store& store = Store::instance();
    Store::OriginScope scope(0);  // the opens are the desktop's, whoever caused them
    const bool rootNew = !_signatures.contains(_root);
    Walk w;
    withoutBackends([&]() { walk(w); });

    // the opens, referenced before referrer: the created list is in
    // post-order, and the root comes last
    for (const auto& pair : w.created) {
        const QString id = store.idOf(pair.second);
        if (!id.isEmpty())
            store.announceOpen(id);
    }
    if (rootNew)
        store.announceOpen(_rootId);
    for (Widget* container : w.changed) {
        const QString id = store.idOf(container);
        if (!id.isEmpty())
            store.notifyLayout(id);
    }
    // what went: after the layout updates that stopped naming it
    QList<QWidget*> gone;
    for (auto it = _models.constBegin(); it != _models.constEnd(); ++it)
        if (!w.visited.contains(it.key()))
            gone.append(it.key());
    for (QWidget* real : gone)
        releaseModel(real, true);
    if (rootNew) {
        _list->layout()->addWidget(_root);
        store.notifyLayout(listId());
    }
    _walking = false;
    if (rootNew)
        Q_EMIT shown(_rootId);
    Q_EMIT rebuilt();
}

void PanelMirror::walk(Walk& w)
{
    QStringList signature;
    auto lay = new Layout(Layout::VBox);
    bool isNew = false;
    for (const auto& c : _contents) {
        if (!c)
            continue;
        Widget* m = mirrorWidget(w, c.data(), _root, isNew);
        lay->addWidget(m);
        signature.append(QStringLiteral("box:") + ptrKey(m));
    }
    if (_buttons) {
        Widget* m = mirrorWidget(w, _buttons.data(), _root, isNew);
        lay->addWidget(m);
        signature.append(QStringLiteral("buttons:") + ptrKey(m));
    }
    const bool rootNew = !_signatures.contains(_root);
    if (rootNew || _signatures.value(_root) != signature) {
        Layout* old = _root->layout();
        _root->setLayout(lay);
        delete old;
        _signatures.insert(_root, signature);
        if (!rootNew)
            w.changed.append(_root);
    }
    else {
        delete lay;
    }
}

Widget* PanelMirror::newModel(QWidget* real, Widget* parentModel, bool picture)
{
    Widget* model = nullptr;
    auto box = qobject_cast<::QDialogButtonBox*>(real->parentWidget());
    auto button = qobject_cast<::QAbstractButton*>(real);
    if (box && button) {
        model = new PanelButton(parentModel);
        model->setInitial(QStringLiteral("standardButton"),
                          static_cast<int>(box->standardButton(button)));
    }
    else if (picture) {
        // a leaf the table does not know, with nothing inside: what it
        // paints, as a label's pixmap (M2)
        model = createWidget(QStringLiteral("QLabel"), parentModel);
        _pictures.insert(real);
    }
    else {
        model = createWidget(tableClassOf(real), parentModel);
    }
    // the real class name, which the DOM keys its rendering on -- except
    // a box, whose subclasses (a workbench's TaskBox-derived panel) are
    // all the one box to a client
    if (qobject_cast<Gui::TaskView::TaskBox*>(real))
        model->setQtClass(QStringLiteral("Gui::TaskView::TaskBox"));
    else
        model->setQtClass(QString::fromUtf8(real->metaObject()->className()));
    model->setInitial(QStringLiteral("objectName"), real->objectName());
    return model;
}

Widget* PanelMirror::mirrorWidget(Walk& w, QWidget* real, Widget* parentModel, bool& isNew)
{
    w.visited.insert(real);
    Widget* model = _models.value(real).data();
    isNew = !model;
    const QString cls = tableClassOf(real);
    const bool container = isContainer(real, cls);
    if (!model) {
        bool picture = !container && cls == QLatin1String("QWidget");
        if (picture && _pictures.size() >= maxPictures()) {
            if (!_pictureCapTold) {
                _pictureCapTold = true;
                Base::Console().Log("PanelMirror: more than %d picture leaves, the rest are bare\n",
                                    maxPictures());
            }
            picture = false;
        }
        model = newModel(real, parentModel, picture);
        _models.insert(real, model);
        Store::instance().adopt(QStringLiteral("pw:%1").arg(++widgetSerial()), model, false);
    }
    else if (model->parentWidget() != parentModel) {
        // moved between containers: the model tree follows, silently
        // (the real move already happened)
        model->QObject::setParent(parentModel);
    }
    if (container) {
        QStringList signature;
        Layout* old = model->layout();
        buildContent(w, real, model, signature);
        if (isNew || _signatures.value(model) != signature) {
            delete old;
            _signatures.insert(model, signature);
            if (!isNew)
                w.changed.append(model);
        }
        else {
            // unchanged: keep the layout that was sent
            Layout* fresh = model->layout();
            model->setLayout(old);
            delete fresh;
        }
    }
    if (isNew) {
        bindModel(real, model);
        refresh(real, model, true);
        watchWidget(real);
        w.created.append({real, model});
    }
    return model;
}

void PanelMirror::buildContent(Walk& w, QWidget* real, Widget* model, QStringList& signature)
{
    Layout* lay = nullptr;
    bool childIsNew = false;
    auto addPage = [&](QWidget* page, const QString& tag) {
        if (!page)
            return;
        Widget* m = mirrorWidget(w, page, model, childIsNew);
        lay->addWidget(m);
        signature.append(tag + ptrKey(m));
    };
    if (auto box = qobject_cast<::QDialogButtonBox*>(real)) {
        lay = new Layout(Layout::HBox);
        for (::QAbstractButton* b : box->buttons())
            addPage(b, QStringLiteral("button:"));
    }
    else if (auto tabs = qobject_cast<::QTabWidget*>(real)) {
        lay = new Layout(Layout::VBox);
        for (int i = 0; i < tabs->count(); ++i)
            addPage(tabs->widget(i), QStringLiteral("page:"));
    }
    else if (auto toolbox = qobject_cast<QToolBox*>(real)) {
        lay = new Layout(Layout::VBox);
        for (int i = 0; i < toolbox->count(); ++i)
            addPage(toolbox->widget(i), QStringLiteral("page:"));
    }
    else if (auto stack = qobject_cast<::QStackedWidget*>(real)) {
        lay = new Layout(Layout::VBox);
        for (int i = 0; i < stack->count(); ++i)
            addPage(stack->widget(i), QStringLiteral("page:"));
    }
    else if (auto splitter = qobject_cast<::QSplitter*>(real)) {
        lay = new Layout(splitter->orientation() == Qt::Horizontal ? Layout::HBox : Layout::VBox);
        for (int i = 0; i < splitter->count(); ++i)
            addPage(splitter->widget(i), QStringLiteral("page:"));
    }
    else if (auto scroll = qobject_cast<::QScrollArea*>(real)) {
        lay = new Layout(Layout::VBox);
        addPage(scroll->widget(), QStringLiteral("content:"));
    }
    else {
        QLayout* realLayout = nullptr;
        bool strays = true;
        if (auto box = qobject_cast<Gui::TaskView::TaskBox*>(real)) {
            realLayout = box->groupLayout();
            strays = false;  // the header and the fold machinery are chrome
        }
        else {
            realLayout = real->layout();
        }
        if (realLayout)
            lay = buildLayout(w, realLayout, model, signature);
        else
            lay = new Layout(Layout::VBox);
        if (strays) {
            // a child no layout holds (a container built by hand)
            for (QObject* o : real->children()) {
                if (!o->isWidgetType())
                    continue;
                auto c = static_cast<QWidget*>(o);
                if (internalChild(c) || w.visited.contains(c))
                    continue;
                addPage(c, QStringLiteral("child:"));
            }
        }
    }
    model->setLayout(lay);
}

Layout* PanelMirror::buildLayout(Walk& w, QLayout* real, Widget* owner, QStringList& signature)
{
    Layout::Kind kind = Layout::VBox;
    auto grid = qobject_cast<QGridLayout*>(real);
    auto form = qobject_cast<QFormLayout*>(real);
    auto box = qobject_cast<QBoxLayout*>(real);
    if (grid)
        kind = Layout::Grid;
    else if (form)
        kind = Layout::Form;
    else if (box)
        kind = (box->direction() == QBoxLayout::LeftToRight
                || box->direction() == QBoxLayout::RightToLeft)
            ? Layout::HBox
            : Layout::VBox;
    auto lay = new Layout(kind);
    lay->setObjectName(real->objectName());
    const QMargins m = real->contentsMargins();
    lay->setContentsMargins(m.left(), m.top(), m.right(), m.bottom());
    if (real->spacing() >= 0)
        lay->setSpacing(real->spacing());
    signature.append(QStringLiteral("lay:%1:%2:%3,%4,%5,%6:%7")
                         .arg(lay->className(), real->objectName())
                         .arg(m.left())
                         .arg(m.top())
                         .arg(m.right())
                         .arg(m.bottom())
                         .arg(real->spacing()));
    if (grid) {
        QVariantList colStretch, rowStretch, colMin;
        bool anyCol = false, anyRow = false, anyMin = false;
        for (int c = 0; c < grid->columnCount(); ++c) {
            colStretch.append(grid->columnStretch(c));
            colMin.append(grid->columnMinimumWidth(c));
            anyCol = anyCol || grid->columnStretch(c);
            anyMin = anyMin || grid->columnMinimumWidth(c);
        }
        for (int r = 0; r < grid->rowCount(); ++r) {
            rowStretch.append(grid->rowStretch(r));
            anyRow = anyRow || grid->rowStretch(r);
        }
        if (anyCol)
            lay->setExtra(QStringLiteral("columnStretch"), colStretch);
        if (anyRow)
            lay->setExtra(QStringLiteral("rowStretch"), rowStretch);
        if (anyMin)
            lay->setExtra(QStringLiteral("columnMinimumWidth"), colMin);
        if (grid->horizontalSpacing() != grid->verticalSpacing()) {
            lay->setExtra(QStringLiteral("horizontalSpacing"), grid->horizontalSpacing());
            lay->setExtra(QStringLiteral("verticalSpacing"), grid->verticalSpacing());
        }
        signature.append(QStringLiteral("gridx:") + posKey(colStretch) + QLatin1Char('/')
                         + posKey(rowStretch) + QLatin1Char('/') + posKey(colMin));
    }
    bool childIsNew = false;
    for (int i = 0; i < real->count(); ++i) {
        QLayoutItem* item = real->itemAt(i);
        if (!item)
            continue;
        QVariantList pos;
        if (grid) {
            int row = 0, col = 0, rs = 1, cs = 1;
            grid->getItemPosition(i, &row, &col, &rs, &cs);
            pos = QVariantList {row, col, rs, cs};
        }
        else if (form) {
            int row = 0;
            QFormLayout::ItemRole role = QFormLayout::FieldRole;
            form->getItemPosition(i, &row, &role);
            pos = QVariantList {row, static_cast<int>(role)};
        }
        const int stretch = box ? box->stretch(i) : 0;
        const int align = static_cast<int>(item->alignment());
        if (QWidget* cw = item->widget()) {
            if (internalChild(cw))
                continue;
            Widget* cm = mirrorWidget(w, cw, owner, childIsNew);
            lay->addWidget(cm, pos);
            lay->setItemStretch(lay->count() - 1, stretch);
            lay->setItemAlignment(lay->count() - 1, align);
            signature.append(QStringLiteral("w:%1@%2#%3/%4")
                                 .arg(ptrKey(cm), posKey(pos))
                                 .arg(stretch)
                                 .arg(align));
        }
        else if (QLayout* sub = item->layout()) {
            signature.append(QStringLiteral("sub@%1#%2/%3(").arg(posKey(pos)).arg(stretch).arg(align));
            Layout* sm = buildLayout(w, sub, owner, signature);
            lay->addLayout(sm, pos);
            lay->setItemStretch(lay->count() - 1, stretch);
            lay->setItemAlignment(lay->count() - 1, align);
            signature.append(QStringLiteral(")"));
        }
        else if (QSpacerItem* sp = item->spacerItem()) {
            const QSize hint = sp->sizeHint();
            const int hp = static_cast<int>(sp->sizePolicy().horizontalPolicy());
            const int vp = static_cast<int>(sp->sizePolicy().verticalPolicy());
            lay->addSpacer(hint.width(), hint.height(), pos, hp, vp);
            signature.append(QStringLiteral("sp:%1,%2,%3,%4@%5")
                                 .arg(hint.width())
                                 .arg(hint.height())
                                 .arg(hp)
                                 .arg(vp)
                                 .arg(posKey(pos)));
        }
    }
    return lay;
}

// ---- binding, watching, reading ------------------------------------------------------

void PanelMirror::bindModel(QWidget* real, Widget* model)
{
    FwQt::View* view = FwQt::View::bind(model, real);
    // an item view's rows are the panel's: reflected, not owned (M2)
    if (view && qobject_cast<::QAbstractItemView*>(real))
        view->reflectItems();
    connect(model, &Widget::propertiesChanged, this,
            [this, real, model](const QStringList& names, int source) {
                onModelWritten(real, model, names, source);
            });
    connect(model, &Widget::requested, this,
            [this, real, model](const QString& name, const QVariantList& args) {
                onModelRequest(real, model, name, args);
            });
}

void PanelMirror::watchWidget(QWidget* real)
{
    if (_watched.contains(real))
        return;
    _watched.insert(real);
    real->installEventFilter(this);
    if (auto area = qobject_cast<QAbstractScrollArea*>(real))
        area->viewport()->installEventFilter(this);
    connect(real, &QObject::destroyed, this, [this, real]() {
        _watched.remove(real);
        releaseModel(real, false);
        scheduleRebuild();
    });
    if (auto box = qobject_cast<Gui::TaskView::TaskBox*>(real))
        connect(box, &Gui::TaskView::TaskBox::toggledExpansion, this,
                [this, real]() { markDirty(real); });
    if (auto toolbox = qobject_cast<QToolBox*>(real))
        connect(toolbox, &QToolBox::currentChanged, this, [this, real](int) { markDirty(real); });
}

void PanelMirror::unwatchWidget(QWidget* real)
{
    if (!_watched.remove(real))
        return;
    real->removeEventFilter(this);
    if (auto area = qobject_cast<QAbstractScrollArea*>(real))
        area->viewport()->removeEventFilter(this);
    disconnect(real, nullptr, this, nullptr);
}

void PanelMirror::releaseModel(QWidget* real, bool announce)
{
    auto it = _models.find(real);
    if (it == _models.end())
        return;
    QPointer<Widget> model = it.value();
    _models.erase(it);
    _dirty.remove(real);
    forgetPicture(real);
    if (_watched.contains(real))
        unwatchWidget(real);
    if (!model)
        return;
    _signatures.remove(model.data());
    Store& store = Store::instance();
    // the descendants first, quietly: a client drops them with this one
    for (Widget* child : model->findChildren<Widget*>()) {
        const QString cid = store.idOf(child);
        if (!cid.isEmpty())
            store.release(cid, false);
        _signatures.remove(child);
        QWidget* childReal = _models.key(child, nullptr);
        if (childReal) {
            _models.remove(childReal);
            _dirty.remove(childReal);
            forgetPicture(childReal);
            unwatchWidget(childReal);
        }
    }
    const QString id = store.idOf(model.data());
    if (!id.isEmpty())
        store.release(id, announce);
    delete model.data();
}

QVariantMap PanelMirror::read(QWidget* real, Widget* model) const
{
    static const QSet<QString> skip = {
        QStringLiteral("visible"),     QStringLiteral("font"),        QStringLiteral("focus"),
        QStringLiteral("actions"),     QStringLiteral("watchEvents"), QStringLiteral("menu"),
        QStringLiteral("forAction"),   QStringLiteral("defaultAction"),
        QStringLiteral("standardButton")};
    QStringList keys;
    for (const QString& key : model->propertyNames())
        if (!skip.contains(key))
            keys.append(key);
    QVariantMap v = FwQt::View::readProperties(real, keys);
    v.insert(QStringLiteral("visible"), !real->isHidden());
    QVariantMap font;
    if (real->font().bold())
        font.insert(QStringLiteral("bold"), true);
    if (real->font().italic())
        font.insert(QStringLiteral("italic"), true);
    v.insert(QStringLiteral("font"), font);
    if (auto combo = qobject_cast<::QComboBox*>(real)) {
        QStringList items;
        for (int i = 0; i < combo->count(); ++i)
            items.append(combo->itemText(i));
        v.insert(QStringLiteral("items"), items);
    }
    else if (auto tabs = qobject_cast<::QTabWidget*>(real)) {
        QStringList names;
        for (int i = 0; i < tabs->count(); ++i)
            names.append(tabs->tabText(i));
        v.insert(QStringLiteral("tabs"), names);
    }
    else if (auto toolbox = qobject_cast<QToolBox*>(real)) {
        QStringList names;
        for (int i = 0; i < toolbox->count(); ++i)
            names.append(toolbox->itemText(i));
        v.insert(QStringLiteral("tabs"), names);
        v.insert(QStringLiteral("currentIndex"), toolbox->currentIndex());
    }
    else if (auto box = qobject_cast<Gui::TaskView::TaskBox*>(real)) {
        v.insert(QStringLiteral("title"), box->headerText());
        v.insert(QStringLiteral("checkable"), box->isExpandable());
        v.insert(QStringLiteral("checked"), box->isGroupVisible());
        v.insert(QStringLiteral("flat"), !box->hasHeader());
    }
    else if (auto view = qobject_cast<::QAbstractItemView*>(real)) {
        // the header, which no Q_PROPERTY carries
        if (QAbstractItemModel* m = view->model()) {
            const int cols = m->columnCount();
            QStringList labels;
            for (int c = 0; c < cols; ++c)
                labels.append(m->headerData(c, Qt::Horizontal).toString());
            v.insert(QStringLiteral("columnCount"), std::max(cols, 1));
            v.insert(QStringLiteral("columns"), labels);
        }
    }
    return v;
}

QString PanelMirror::grabPicture(QWidget* real)
{
    if (!real->isVisible() || real->width() <= 0 || real->height() <= 0)
        return QString();
    const qint64 now = _clock.elapsed();
    const qint64 last = _grabbedAt.value(real, -grabIntervalMs());
    if (now - last < grabIntervalMs()) {
        // too soon after the last one (a blinking caret would stream at
        // the blink rate): once more when the interval is up
        _pendingGrabs.insert(real);
        if (!_grabTimer.isActive())
            _grabTimer.start(static_cast<int>(grabIntervalMs() - (now - last)));
        return QString();
    }
    _grabbedAt.insert(real, now);
    ++_grabs;
    // at 1x whatever the screen's ratio; the size capped
    QPixmap pixmap(real->size());
    pixmap.fill(Qt::transparent);
    _grabbing = true;
    real->render(&pixmap, QPoint(), QRegion(), QWidget::DrawChildren);
    _grabbing = false;
    if (pixmap.width() > maxPictureSide() || pixmap.height() > maxPictureSide())
        pixmap = pixmap.scaled(maxPictureSide(), maxPictureSide(), Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    return ImageStore::instance().add(pixmap);
}

void PanelMirror::forgetPicture(QWidget* real)
{
    _pictures.remove(real);
    _grabbedAt.remove(real);
    _pendingGrabs.remove(real);
}

void PanelMirror::refresh(QWidget* real, Widget* model, bool initial)
{
    QVariantMap v = read(real, model);
    // what has no name to send and travels by image id (M2)
    if (_pictures.contains(real)) {
        const QString id = grabPicture(real);
        if (!id.isNull())
            v.insert(QStringLiteral("pixmap"), id);
    }
    else if (auto button = qobject_cast<::QAbstractButton*>(real)) {
        if (!button->icon().isNull())
            v.insert(QStringLiteral("icon"),
                     ImageStore::instance().ofIcon(button->icon(), button->iconSize()));
    }
    else if (auto label = qobject_cast<::QLabel*>(real)) {
        const QPixmap pixmap = label->pixmap();
        if (!pixmap.isNull())
            v.insert(QStringLiteral("pixmap"), ImageStore::instance().ofPixmap(pixmap));
    }
    else if (qobject_cast<::QAbstractItemView*>(real) && !initial) {
        // what no model signal carries: rows hidden by the view, a
        // tree's expansion
        if (FwQt::View* view = FwQt::View::of(model))
            view->syncReflectedRows();
    }
    if (initial) {
        for (auto it = v.constBegin(); it != v.constEnd(); ++it)
            model->setInitial(it.key(), it.value());
    }
    else {
        writeDiff(model, v);
    }
}

// ---- a client's writes and requests --------------------------------------------------

void PanelMirror::onModelWritten(QWidget* real, Widget* model, const QStringList& names,
                                 int source)
{
    Q_UNUSED(model)
    if (source != static_cast<int>(Source::Client) || _relaying)
        return;
    // a streamed client's write landed in the real widget: what the
    // widget made of it (a formatted quantity, a clamped value) must
    // reach the writer too, so the widget is re-read outside the
    // writer's origin; and the setters fire `textChanged`, not the
    // edit signals a panel connects.  The bound view hears the relayed
    // signal as the user's edit and writes the (equal) value once more
    // under the same origin: the guard stops it there.
    markDirty(real);
    _relaying = true;
    if (auto edit = qobject_cast<::QLineEdit*>(real)) {
        if (names.contains(QLatin1String("text")))
            Q_EMIT edit->textEdited(edit->text());
    }
    else if (auto combo = qobject_cast<::QComboBox*>(real)) {
        if (names.contains(QLatin1String("currentIndex")) && combo->currentIndex() >= 0)
            Q_EMIT combo->activated(combo->currentIndex());
    }
    _relaying = false;
}

void PanelMirror::onModelRequest(QWidget* real, Widget* model, const QString& name,
                                 const QVariantList& args)
{
    Q_UNUSED(model)
    Q_UNUSED(args)
    if (name == QLatin1String("click")) {
        if (auto b = qobject_cast<::QAbstractButton*>(real))
            b->click();
    }
    else if (name == QLatin1String("toggle")) {
        if (auto b = qobject_cast<::QAbstractButton*>(real))
            b->toggle();
    }
    else if (name == QLatin1String("editingFinished") || name == QLatin1String("returnPressed")) {
        replayKey(real, Qt::Key_Return);
    }
    else if (name == QLatin1String("escape")) {
        replayKey(real, Qt::Key_Escape);
    }
}

void PanelMirror::onRootRequest(const QString& name, const QVariantList& args)
{
    ::QDialogButtonBox* box = _buttons.data();
    auto clickRole = [box](::QDialogButtonBox::ButtonRole role) {
        if (!box)
            return false;
        for (::QAbstractButton* b : box->buttons()) {
            if (box->buttonRole(b) == role) {
                b->click();
                return true;
            }
        }
        return false;
    };
    if (name == QLatin1String("accept")) {
        if (!clickRole(::QDialogButtonBox::AcceptRole) && getMainWindow())
            Control().accept();
    }
    else if (name == QLatin1String("reject")) {
        if (!clickRole(::QDialogButtonBox::RejectRole) && getMainWindow())
            Control().reject();
    }
    else if (name == QLatin1String("clicked")) {
        ::QAbstractButton* b = box ? box->button(static_cast<::QDialogButtonBox::StandardButton>(
                                       args.value(0).toInt()))
                                 : nullptr;
        if (b)
            b->click();
    }
    else if (name == QLatin1String("helpRequested")) {
        clickRole(::QDialogButtonBox::HelpRole);
    }
}

#include "moc_FwPanelMirror.cpp"
