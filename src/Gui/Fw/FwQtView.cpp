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

#ifndef _PreComp_
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QBoxLayout>
#include <QBrush>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMetaProperty>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeView>
#include <QTreeWidget>
#include <QWidget>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>

#include "BitmapFactory.h"
#include "FileDialog.h"
#include "Fw/FwQtView.h"
#include "Fw/FwWidgets.h"
#include "InputField.h"
#include "MainWindow.h"
#include "PrefWidgets.h"
#include "QuantitySpinBox.h"
#include "UiLoader.h"
#include "WidgetFactory.h"
#include "Widgets.h"

using namespace Gui;
using namespace Gui::FwQt;

namespace
{

QHash<const Fw::Widget*, View*>& registry()
{
    static QHash<const Fw::Widget*, View*> views;
    return views;
}

QIcon iconOf(const QString& path)
{
    if (path.isEmpty())
        return QIcon();
    if (path.startsWith(QLatin1String("theme:")))
        return QIcon::fromTheme(path.mid(6));
    return QIcon(path);
}

/// A pixmap for a bag path: `bitmap:<name>` is one of FreeCAD's own
/// through the BitmapFactory, rendered at `size` (an SVG icon at the
/// label's size, as `pixmapFromSvg` does); anything else is a file or
/// resource path.
QPixmap pixmapOf(const QString& path, const QSize& size)
{
    if (path.startsWith(QLatin1String("bitmap:"))) {
        QByteArray name = path.mid(7).toUtf8();
        QPixmap px = Gui::BitmapFactory().pixmapFromSvg(name.constData(), size);
        if (px.isNull())
            px = Gui::BitmapFactory().pixmap(name.constData());
        return px;
    }
    return QPixmap(path);
}

QVariantList colorList(const QColor& c)
{
    return QVariantList {c.redF(), c.greenF(), c.blueF(), c.alphaF()};
}

QColor colorOf(const QVariant& v)
{
    QVariantList c = v.toList();
    if (c.size() < 3)
        return QColor();
    return QColor::fromRgbF(static_cast<float>(c.at(0).toDouble()),
                            static_cast<float>(c.at(1).toDouble()),
                            static_cast<float>(c.at(2).toDouble()),
                            static_cast<float>(c.size() > 3 ? c.at(3).toDouble() : 1.0));
}

Gui::UiLoader& loader()
{
    static std::unique_ptr<Gui::UiLoader> instance;
    if (!instance) {
        GetWidgetFactorySupplier();
        instance = Gui::UiLoader::newInstance();
    }
    return *instance;
}

}  // namespace

/// The real view's rows by id: a persistent index per row, the model
/// behind the view (the widget's own for a QTreeWidget, a QListWidget,
/// a QTableWidget; a QStandardItemModel made here for a QTreeView, a
/// QListView, a QTableView that uic left without one).
struct View::Items
{
    QAbstractItemView* view = nullptr;
    QAbstractItemModel* model = nullptr;
    QStandardItemModel* owned = nullptr;
    QHash<int, QPersistentModelIndex> byId;
    QHash<QPersistentModelIndex, int> ids;
    void forget(int id)
    {
        auto it = byId.find(id);
        if (it == byId.end())
            return;
        ids.remove(it.value());
        byId.erase(it);
    }
    void prune()
    {
        for (auto it = byId.begin(); it != byId.end();) {
            if (!it.value().isValid()) {
                ids.remove(it.value());
                it = byId.erase(it);
            }
            else {
                ++it;
            }
        }
    }
};

// ---- construction ---------------------------------------------------------------

QWidget* Gui::FwQt::makeQtWidget(const QString& className, QWidget* parent)
{
    QWidget* w = nullptr;
    if (className.startsWith(QLatin1String("Gui::"))) {
        GetWidgetFactorySupplier();
        w = WidgetFactory().createWidget(className.toUtf8().constData(), parent);
    }
    else if (!className.isEmpty()) {
        w = loader().createWidget(className, parent);
    }
    if (!w) {
        Base::Console().Warning("FwQt: no host widget %s\n", qPrintable(className));
        w = new QWidget(parent);
    }
    return w;
}

QWidget* Gui::FwQt::realize(Fw::Widget* model, QWidget* parent)
{
    View* v = View::build(model, parent);
    return v ? v->widget() : nullptr;
}

QWidget* Gui::FwQt::widgetOf(const Fw::Widget* model)
{
    View* v = model ? View::of(model) : nullptr;
    return v ? v->widget() : nullptr;
}

View* View::of(const Fw::Widget* model)
{
    return registry().value(model);
}

View::View(Fw::Widget* model, QWidget* widget, bool bound)
    : QObject(model)
    , _model(model)
    , _widget(widget)
    , _bound(bound)
{
    registry().insert(model, this);
    model->setBackend(this);
    connect(widget, &QObject::destroyed, this, [this]() {
        _widget = nullptr;
        release(false);
    });
    connectWidget();
    initItems();
}

View::~View()
{
    auto it = registry().find(_model);
    if (it != registry().end() && it.value() == this)
        registry().erase(it);
    if (_model->backend() == this)
        _model->setBackend(nullptr);
}

void View::propertiesWritten(const QStringList& names, int source)
{
    if (source != static_cast<int>(Fw::Source::Backend))
        apply(names);
}

void View::requested(const QString& name, const QVariantList& args)
{
    onRequest(name, args);
}

void View::layoutChanged(const QVariantMap& op)
{
    onLayoutOp(op);
}

void View::release(bool deleteWidget)
{
    for (const auto& child : _children)
        if (child)
            child->release(false);
    _children.clear();
    _items.reset();
    QWidget* w = _widget.data();
    _widget = nullptr;
    if (w) {
        disconnect(w, nullptr, this, nullptr);
        if (deleteWidget) {
            w->setParent(nullptr);
            w->deleteLater();
        }
    }
    auto it = registry().find(_model);
    if (it != registry().end() && it.value() == this)
        registry().erase(it);
    if (_model->backend() == this)
        _model->setBackend(nullptr);
    disconnect(_model, nullptr, this, nullptr);
    deleteLater();
}

View* View::bind(Fw::Widget* model, QWidget* widget)
{
    if (!model || !widget)
        return nullptr;
    if (View* existing = of(model)) {
        if (existing->widget() == widget)
            return existing;
        existing->release(false);
    }
    auto view = new View(model, widget, true);
    view->readBack();
    QStringList keys = QStringList(model->touched().begin(), model->touched().end());
    view->apply(keys);
    return view;
}

View* View::build(Fw::Widget* model, QWidget* parent)
{
    if (!model)
        return nullptr;
    if (View* existing = of(model))
        return existing;
    if (qobject_cast<Fw::UiForm*>(model))
        return buildForm(model, parent);
    QWidget* w = makeQtWidget(model->qtClass(), parent);
    auto view = new View(model, w, false);
    view->initContainers();
    QStringList keys = QStringList(model->touched().begin(), model->touched().end());
    if (!model->objectName().isEmpty() && !keys.contains(QLatin1String("objectName")))
        keys.append(QStringLiteral("objectName"));
    view->apply(keys);
    return view;
}

void View::initContainers()
{
    // a container BUILT here (not uic's, whose pages exist) gets the
    // pages its model collected before it had a backend
    QWidget* w = _widget.data();
    if (!w)
        return;
    if (auto t = qobject_cast<Fw::QTabWidget*>(_model)) {
        auto qt = qobject_cast<QTabWidget*>(w);
        QStringList tabs = t->tabs();
        for (int i = 0; qt && i < t->count(); ++i)
            if (QWidget* page = widgetOf(QVariant::fromValue<QObject*>(t->widget(i)), qt))
                qt->addTab(page, tabs.value(i));
    }
    else if (auto st = qobject_cast<Fw::QStackedWidget*>(_model)) {
        auto qs = qobject_cast<QStackedWidget*>(w);
        for (int i = 0; qs && i < st->count(); ++i)
            if (QWidget* page = widgetOf(QVariant::fromValue<QObject*>(st->widget(i)), qs))
                qs->addWidget(page);
    }
    else if (auto sp = qobject_cast<Fw::QSplitter*>(_model)) {
        auto qs = qobject_cast<QSplitter*>(w);
        for (int i = 0; qs && i < sp->count(); ++i)
            if (QWidget* pane = widgetOf(QVariant::fromValue<QObject*>(sp->widget(i)), qs))
                qs->addWidget(pane);
    }
    else if (auto sa = qobject_cast<Fw::QScrollArea*>(_model)) {
        auto qa = qobject_cast<QScrollArea*>(w);
        if (qa && sa->widget())
            if (QWidget* content = widgetOf(QVariant::fromValue<QObject*>(sa->widget()), qa))
                qa->setWidget(content);
    }
}

View* View::buildForm(Fw::Widget* model, QWidget* parent)
{
    auto form = static_cast<Fw::UiForm*>(model);
    const QString path = form->uiFile();
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        throw Base::RuntimeError("FwQt: cannot open .ui file '" + path.toStdString() + "'");
    QWidget* w = loader().load(&file, parent);
    if (!w)
        throw Base::RuntimeError("FwQt: cannot load .ui file '" + path.toStdString() + "'");
    auto view = new View(model, w, true);
    view->readBack();
    try {
        const auto& named = form->namedWidgets();
        for (auto it = named.constBegin(); it != named.constEnd(); ++it) {
            QWidget* child = w->findChild<QWidget*>(it.key());
            if (!child) {
                Base::Console().Warning("FwQt: %s has no widget %s\n", qPrintable(path),
                                        qPrintable(it.key()));
                continue;
            }
            view->_children.append(bind(it.value(), child));
        }
    }
    catch (...) {
        view->release(true);
        throw;
    }
    QStringList keys = QStringList(model->touched().begin(), model->touched().end());
    view->apply(keys);
    return view;
}

// ---- the bag -> the widget ----------------------------------------------------------

void View::readBack()
{
    QWidget* w = _widget.data();
    if (!w)
        return;
    const QMetaObject* mo = w->metaObject();
    for (const QString& key : _model->propertyNames()) {
        if (key == QLatin1String("visible") || _model->isTouched(key))
            continue;
        int idx = mo->indexOfProperty(key.toUtf8().constData());
        if (idx < 0)
            continue;
        QVariant v = mo->property(idx).read(w);
        if (!v.isValid())
            continue;
        if (v.userType() == QMetaType::QColor) {
            _model->setInitial(key, colorList(v.value<QColor>()));
            continue;
        }
        if (v.userType() == QMetaType::QIcon || v.userType() == QMetaType::QPixmap)
            continue;
        if (v.userType() == QMetaType::QByteArray)
            v = QString::fromUtf8(v.toByteArray());
        else if (mo->property(idx).isEnumType() || mo->property(idx).isFlagType())
            v = v.toInt();
        else if (!v.canConvert<QString>() && !v.canConvert<QStringList>())
            continue;
        _model->setInitial(key, v);
    }
}

void View::apply(const QStringList& names)
{
    QWidget* w = _widget.data();
    if (!w || names.isEmpty())
        return;
    const bool was = _applying;
    _applying = true;
    QStringList keys = names;
    keys.removeDuplicates();
    // a quantity value comes with the text the guest formatted; the
    // value alone is applied and the widget formats its own text
    if (keys.contains(QLatin1String("rawValue")))
        keys.removeAll(QStringLiteral("text"));
    // the range before the value, else Qt clamps the value; an item
    // view's columns before its rows' selection
    QStringList first;
    for (const char* k : {"minimum", "maximum", "decimals", "checkable", "editable", "tristate",
                          "columnCount", "columns", "rowLabels", "tabs"})
        if (keys.removeAll(QLatin1String(k)))
            first.append(QLatin1String(k));
    for (const char* k : {"selection", "currentId", "currentColumn"})
        if (keys.removeAll(QLatin1String(k)))
            keys.append(QLatin1String(k));
    // a combo's list, then its index
    if (keys.removeAll(QStringLiteral("items")) || keys.removeAll(QStringLiteral("itemIcons"))) {
        first.append(QStringLiteral("items"));
        keys.removeAll(QStringLiteral("currentIndex"));
        keys.append(QStringLiteral("currentIndex"));
    }
    for (const QString& k : first)
        applyOne(k, _model->property(k));
    for (const QString& k : keys)
        applyOne(k, _model->property(k));
    _applying = was;
}

void View::applyOne(const QString& key, const QVariant& value)
{
    QWidget* w = _widget.data();
    if (!w)
        return;
    if (key == QLatin1String("visible")) {
        w->setVisible(value.toBool());
    }
    else if (key == QLatin1String("objectName")) {
        if (!value.toString().isEmpty())
            w->setObjectName(value.toString());
    }
    else if (key == QLatin1String("windowIcon")) {
        w->setWindowIcon(iconOf(value.toString()));
    }
    else if (key == QLatin1String("icon")) {
        if (auto b = qobject_cast<QAbstractButton*>(w))
            b->setIcon(iconOf(value.toString()));
    }
    else if (key == QLatin1String("pixmap")) {
        auto l = qobject_cast<QLabel*>(w);
        if (l && !value.toString().isEmpty())
            l->setPixmap(pixmapOf(value.toString(), l->size()));
    }
    else if (key == QLatin1String("color")) {
        w->setProperty("color", colorOf(value));
    }
    else if (key == QLatin1String("binding")) {
        // the model owns the binding; the real widget binds to the same
        // path (typed, not by name), so its f(x) label and expression
        // dialog work as they do today
        auto bound = dynamic_cast<Fw::ExpressionBound*>(_model);
        auto eb = dynamic_cast<Gui::ExpressionBinding*>(w);
        if (bound && eb) {
            if (bound->isBound()) {
                eb->bind(bound->boundPath());
                if (auto q = qobject_cast<Gui::QuantitySpinBox*>(w))
                    q->evaluateExpression();
            }
            else {
                eb->unbind();
            }
        }
    }
    else if (key == QLatin1String("expression")) {
        // derived from the document; the real widget reads its own binding
    }
    else if (key == QLatin1String("items")) {
        auto c = qobject_cast<QComboBox*>(w);
        if (!c)
            return;
        QStringList items = _model->property("items").toStringList();
        QStringList icons = _model->property("itemIcons").toStringList();
        c->clear();
        for (int i = 0; i < items.size(); ++i) {
            QString icon = icons.value(i);
            if (icon.isEmpty())
                c->addItem(items.at(i));
            else
                c->addItem(iconOf(icon), items.at(i));
        }
    }
    else if (key == QLatin1String("currentIndex")) {
        if (auto c = qobject_cast<QComboBox*>(w))
            c->setCurrentIndex(value.toInt());
        else if (auto t = qobject_cast<QTabWidget*>(w))
            t->setCurrentIndex(value.toInt());
        else if (auto s = qobject_cast<QStackedWidget*>(w))
            s->setCurrentIndex(value.toInt());
    }
    else if (key == QLatin1String("tabs")) {
        if (auto t = qobject_cast<QTabWidget*>(w)) {
            QStringList tabs = value.toStringList();
            for (int i = 0; i < tabs.size() && i < t->count(); ++i)
                t->setTabText(i, tabs.at(i));
        }
    }
    else if (key == QLatin1String("sizes")) {
        if (auto s = qobject_cast<QSplitter*>(w)) {
            QList<int> sizes;
            for (const QVariant& v : value.toList())
                sizes.append(v.toInt());
            if (!sizes.isEmpty())
                s->setSizes(sizes);
            // Qt keeps the ratio within the real width: the bag mirrors
            // what the splitter made of it (a backend write: not re-applied)
            QVariantList actual;
            for (int x : s->sizes())
                actual.append(x);
            if (actual != value.toList())
                _model->setProperties(QVariantMap {{QStringLiteral("sizes"), actual}},
                                      Fw::Source::Backend);
        }
    }
    else if (key == QLatin1String("result") || key == QLatin1String("width")
             || key == QLatin1String("height")) {
        // written by the backend
    }
    else if (key == QLatin1String("columnCount")) {
        if (_items)
            ensureColumns(value.toInt(), QModelIndex());
    }
    else if (key == QLatin1String("columns")) {
        if (_items) {
            QStringList labels = value.toStringList();
            ensureColumns(labels.size(), QModelIndex());
            // the widgets make their header items; a model's setHeaderData
            // makes none (a table's refuses a column without one)
            if (auto tree = qobject_cast<QTreeWidget*>(w)) {
                QStringList all = labels;
                for (int c = labels.size(); c < tree->columnCount(); ++c)
                    all.append(tree->headerItem()->text(c));
                tree->setHeaderLabels(all);
            }
            else if (auto table = qobject_cast<QTableWidget*>(w)) {
                table->setHorizontalHeaderLabels(labels);
            }
            else {
                for (int c = 0; c < labels.size(); ++c)
                    _items->model->setHeaderData(c, Qt::Horizontal, labels.at(c));
            }
        }
    }
    else if (key == QLatin1String("rowLabels")) {
        if (auto table = qobject_cast<QTableWidget*>(w))
            table->setVerticalHeaderLabels(value.toStringList());
        else if (_items && _items->owned)
            _items->owned->setVerticalHeaderLabels(value.toStringList());
    }
    else if (key == QLatin1String("selection")) {
        applySelection();
    }
    else if (key == QLatin1String("currentId") || key == QLatin1String("currentColumn")) {
        applyCurrent();
    }
    else if (key == QLatin1String("columnWidths")) {
        QVariantList widths = value.toList();
        for (int c = 0; c < widths.size(); ++c) {
            if (auto t = qobject_cast<QTreeView*>(w))
                t->setColumnWidth(c, widths.at(c).toInt());
            else if (auto t = qobject_cast<QTableView*>(w))
                t->setColumnWidth(c, widths.at(c).toInt());
        }
    }
    else if (key == QLatin1String("cellTypes")) {
        applyCellTypes();
    }
    else if (key == QLatin1String("headerHidden") && qobject_cast<QTableView*>(w)) {
        static_cast<QTableView*>(w)->horizontalHeader()->setVisible(!value.toBool());
    }
    else if (key == QLatin1String("editText")) {
        auto c = qobject_cast<QComboBox*>(w);
        if (c && c->isEditable())
            c->setEditText(value.toString());
    }
    else if (key == QLatin1String("plainText")) {
        if (auto t = qobject_cast<QTextEdit*>(w)) {
            if (t->toPlainText() != value.toString())
                t->setPlainText(value.toString());
        }
        else if (auto p = qobject_cast<QPlainTextEdit*>(w)) {
            if (p->toPlainText() != value.toString())
                p->setPlainText(value.toString());
        }
    }
    else if (key == QLatin1String("html")) {
        auto t = qobject_cast<QTextEdit*>(w);
        if (t && !value.toString().isEmpty())
            t->setHtml(value.toString());
    }
    else if ((key == QLatin1String("maximumWidth") || key == QLatin1String("maximumHeight"))
             && value.toInt() <= 0) {
        // a zero maximum would collapse the widget; the guest's default
    }
    else if (w->metaObject()->indexOfProperty(key.toUtf8().constData()) >= 0) {
        w->setProperty(key.toUtf8().constData(), value);
    }
}

// ---- the widget -> the bag ------------------------------------------------------------

void View::send(const QVariantMap& state)
{
    if (_applying)
        return;
    _model->setProperties(state, Fw::Source::Backend);
}

void View::event(const QString& name, const QVariantList& args)
{
    if (_applying)
        return;
    _model->notify(name, args);
}

void View::connectWidget()
{
    QWidget* w = _widget.data();
    if (!w)
        return;
    if (auto b = qobject_cast<QAbstractButton*>(w)) {
        connect(b, &QAbstractButton::toggled, this, [this](bool on) {
            send(QVariantMap {{QStringLiteral("checked"), on}});
        });
        connect(b, &QAbstractButton::clicked, this, [this, b](bool) {
            event(QStringLiteral("clicked"), QVariantList {b->isChecked()});
        });
        connect(b, &QAbstractButton::pressed, this, [this]() { event(QStringLiteral("pressed")); });
        connect(b, &QAbstractButton::released, this,
                [this]() { event(QStringLiteral("released")); });
        if (auto cb = qobject_cast<Gui::ColorButton*>(w)) {
            connect(cb, &Gui::ColorButton::changed, this, [this, cb]() {
                send(QVariantMap {{QStringLiteral("color"), colorList(cb->color())}});
            });
        }
        return;
    }
    if (auto g = qobject_cast<QGroupBox*>(w)) {
        connect(g, &QGroupBox::toggled, this, [this](bool on) {
            send(QVariantMap {{QStringLiteral("checked"), on}});
        });
        connect(g, &QGroupBox::clicked, this, [this, g](bool) {
            event(QStringLiteral("clicked"), QVariantList {g->isChecked()});
        });
        return;
    }
    if (auto l = qobject_cast<QLabel*>(w)) {
        connect(l, &QLabel::linkActivated, this, [this](const QString& link) {
            event(QStringLiteral("linkActivated"), QVariantList {link});
        });
        return;
    }
    if (auto e = qobject_cast<QLineEdit*>(w)) {
        connect(e, &QLineEdit::textEdited, this, [this](const QString& text) {
            send(QVariantMap {{QStringLiteral("text"), text}});
            event(QStringLiteral("textEdited"), QVariantList {text});
        });
        connect(e, &QLineEdit::returnPressed, this,
                [this]() { event(QStringLiteral("returnPressed")); });
        connect(e, &QLineEdit::editingFinished, this,
                [this]() { event(QStringLiteral("editingFinished")); });
        if (auto f = qobject_cast<Gui::InputField*>(w)) {
            connect(f, qOverload<double>(&Gui::InputField::valueChanged), this,
                    [this, f](double v) {
                        send(QVariantMap {{QStringLiteral("rawValue"), v},
                                          {QStringLiteral("text"), f->text()}});
                    });
            connect(f, &Gui::InputField::parseError, this, [this](const QString& t) {
                event(QStringLiteral("parseError"), QVariantList {t});
            });
        }
        return;
    }
    if (auto q = qobject_cast<Gui::QuantitySpinBox*>(w)) {
        connect(q, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this,
                [this, q](double v) {
                    send(QVariantMap {{QStringLiteral("rawValue"), v},
                                      {QStringLiteral("text"), q->text()}});
                });
        connect(q, &QAbstractSpinBox::editingFinished, this,
                [this]() { event(QStringLiteral("editingFinished")); });
        return;
    }
    if (auto s = qobject_cast<QSpinBox*>(w)) {
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
            send(QVariantMap {{QStringLiteral("value"), v}});
        });
        connect(s, &QAbstractSpinBox::editingFinished, this,
                [this]() { event(QStringLiteral("editingFinished")); });
        return;
    }
    if (auto d = qobject_cast<QDoubleSpinBox*>(w)) {
        connect(d, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
            send(QVariantMap {{QStringLiteral("value"), v}});
        });
        connect(d, &QAbstractSpinBox::editingFinished, this,
                [this]() { event(QStringLiteral("editingFinished")); });
        return;
    }
    if (auto s = qobject_cast<QSlider*>(w)) {
        connect(s, &QSlider::valueChanged, this, [this](int v) {
            send(QVariantMap {{QStringLiteral("value"), v}});
        });
        connect(s, &QSlider::sliderReleased, this,
                [this]() { event(QStringLiteral("sliderReleased")); });
        return;
    }
    if (auto c = qobject_cast<QComboBox*>(w)) {
        connect(c, &QComboBox::currentIndexChanged, this, [this](int i) {
            send(QVariantMap {{QStringLiteral("currentIndex"), i}});
        });
        connect(c, qOverload<int>(&QComboBox::activated), this, [this](int i) {
            event(QStringLiteral("activated"), QVariantList {i});
        });
        connect(c, qOverload<int>(&QComboBox::highlighted), this, [this](int i) {
            event(QStringLiteral("highlighted"), QVariantList {i});
        });
        connect(c, &QComboBox::editTextChanged, this, [this, c](const QString& text) {
            if (c->isEditable())
                send(QVariantMap {{QStringLiteral("editText"), text}});
        });
        return;
    }
    if (auto t = qobject_cast<QTextEdit*>(w)) {
        connect(t, &QTextEdit::textChanged, this, [this, t]() {
            send(QVariantMap {{QStringLiteral("plainText"), t->toPlainText()}});
        });
        return;
    }
    if (auto p = qobject_cast<QPlainTextEdit*>(w)) {
        connect(p, &QPlainTextEdit::textChanged, this, [this, p]() {
            send(QVariantMap {{QStringLiteral("plainText"), p->toPlainText()}});
        });
        return;
    }
    if (auto d = qobject_cast<QDialog*>(w)) {
        connect(d, &QDialog::accepted, this, [this]() { event(QStringLiteral("accepted")); });
        connect(d, &QDialog::rejected, this, [this]() { event(QStringLiteral("rejected")); });
        connect(d, &QDialog::finished, this, [this](int r) {
            send(QVariantMap {{QStringLiteral("result"), r}, {QStringLiteral("visible"), false}});
            event(QStringLiteral("finished"), QVariantList {r});
        });
        return;
    }
    if (auto b = qobject_cast<QDialogButtonBox*>(w)) {
        connect(b, &QDialogButtonBox::accepted, this, [this]() { event(QStringLiteral("accepted")); });
        connect(b, &QDialogButtonBox::rejected, this, [this]() { event(QStringLiteral("rejected")); });
        connect(b, &QDialogButtonBox::helpRequested, this,
                [this]() { event(QStringLiteral("helpRequested")); });
        connect(b, &QDialogButtonBox::clicked, this, [this, b](QAbstractButton* button) {
            event(QStringLiteral("clicked"), QVariantList {static_cast<int>(b->standardButton(button))});
        });
        return;
    }
    if (auto t = qobject_cast<QTabWidget*>(w)) {
        connect(t, &QTabWidget::currentChanged, this, [this](int i) {
            send(QVariantMap {{QStringLiteral("currentIndex"), i}});
        });
        connect(t, &QTabWidget::tabCloseRequested, this, [this](int i) {
            event(QStringLiteral("tabCloseRequested"), QVariantList {i});
        });
        connect(t, &QTabWidget::tabBarClicked, this, [this](int i) {
            event(QStringLiteral("tabBarClicked"), QVariantList {i});
        });
        return;
    }
    if (auto st = qobject_cast<QStackedWidget*>(w)) {
        connect(st, &QStackedWidget::currentChanged, this, [this](int i) {
            send(QVariantMap {{QStringLiteral("currentIndex"), i}});
        });
        return;
    }
    if (auto sp = qobject_cast<QSplitter*>(w)) {
        connect(sp, &QSplitter::splitterMoved, this, [this, sp](int pos, int index) {
            QVariantList sizes;
            for (int s : sp->sizes())
                sizes.append(s);
            send(QVariantMap {{QStringLiteral("sizes"), sizes}});
            event(QStringLiteral("splitterMoved"), QVariantList {pos, index});
        });
        return;
    }
    if (auto fc = qobject_cast<Gui::FileChooser*>(w)) {
        connect(fc, &Gui::FileChooser::fileNameChanged, this, [this](const QString& name) {
            send(QVariantMap {{QStringLiteral("fileName"), name}});
        });
        connect(fc, &Gui::FileChooser::fileNameSelected, this, [this](const QString& name) {
            event(QStringLiteral("fileNameSelected"), QVariantList {name});
        });
        return;
    }
}

// ---- requests and layout ops ------------------------------------------------------

void View::onRequest(const QString& name, const QVariantList& args)
{
    QWidget* w = _widget.data();
    if (!w)
        return;
    if (name == QLatin1String("setFocus")) {
        w->setFocus();
    }
    else if (name == QLatin1String("selectAll")) {
        QMetaObject::invokeMethod(w, "selectAll");
    }
    else if (name == QLatin1String("setSelection")) {
        if (auto e = qobject_cast<QLineEdit*>(w))
            e->setSelection(args.value(0).toInt(), args.value(1).toInt());
    }
    else if (name == QLatin1String("setCursorPosition")) {
        if (auto e = qobject_cast<QLineEdit*>(w))
            e->setCursorPosition(args.value(0).toInt());
    }
    else if (name == QLatin1String("selectNumber")) {
        if (auto q = qobject_cast<Gui::QuantitySpinBox*>(w))
            q->selectNumber();
    }
    else if (name == QLatin1String("setToLastUsedValue")) {
        if (auto p = qobject_cast<Gui::PrefQuantitySpinBox*>(w))
            p->setToLastUsedValue();
    }
    else if (name == QLatin1String("pushToHistory")) {
        if (auto p = qobject_cast<Gui::PrefQuantitySpinBox*>(w))
            p->pushToHistory();
    }
    else if (name == QLatin1String("setParent")) {
        auto parentModel = qobject_cast<Fw::Widget*>(args.value(0).value<QObject*>());
        View* pv = parentModel ? of(parentModel) : nullptr;
        QWidget* pw = pv ? pv->widget() : nullptr;
        // a parent that is not realized keeps the widget where it is
        if (!parentModel || pw)
            w->setParent(pw);
    }
    // dialogs
    else if (name == QLatin1String("accept")) {
        if (auto d = qobject_cast<QDialog*>(w))
            d->accept();
    }
    else if (name == QLatin1String("reject")) {
        if (auto d = qobject_cast<QDialog*>(w))
            d->reject();
    }
    else if (name == QLatin1String("done")) {
        if (auto d = qobject_cast<QDialog*>(w))
            d->done(args.value(0).toInt());
    }
    else if (name == QLatin1String("close")) {
        w->close();
    }
    else if (name == QLatin1String("move")) {
        w->move(args.value(0).toInt(), args.value(1).toInt());
    }
    else if (name == QLatin1String("resize")) {
        w->resize(args.value(0).toInt(), args.value(1).toInt());
    }
    else if (name == QLatin1String("raise")) {
        w->raise();
    }
    else if (name == QLatin1String("activateWindow")) {
        w->activateWindow();
    }
    else if (name == QLatin1String("adjustSize")) {
        w->adjustSize();
    }
    else if (name == QLatin1String("button")) {
        auto box = qobject_cast<QDialogButtonBox*>(w);
        QPushButton* b = box ? box->button(static_cast<QDialogButtonBox::StandardButton>(
                                   args.value(0).toInt()))
                             : nullptr;
        if (!b)
            return;
        const QString method = args.value(1).toString();
        QVariantList a = args.value(2).toList();
        if (method == QLatin1String("setEnabled"))
            b->setEnabled(a.value(0).toBool());
        else if (method == QLatin1String("setText"))
            b->setText(a.value(0).toString());
        else if (method == QLatin1String("setDefault"))
            b->setDefault(a.value(0).toBool());
        else if (method == QLatin1String("setAutoDefault"))
            b->setAutoDefault(a.value(0).toBool());
        else if (method == QLatin1String("setFocus"))
            b->setFocus();
        else if (method == QLatin1String("setIcon"))
            b->setIcon(iconOf(a.value(0).toString()));
        else if (method == QLatin1String("setToolTip"))
            b->setToolTip(a.value(0).toString());
        else if (method == QLatin1String("setVisible"))
            b->setVisible(a.value(0).toBool());
        else if (method == QLatin1String("click"))
            b->click();
    }
    // containers
    else if (name == QLatin1String("insertTab")) {
        if (auto t = qobject_cast<QTabWidget*>(w)) {
            QWidget* page = widgetOf(args.value(1), t);
            if (page) {
                QString icon = args.value(3).toString();
                if (icon.isEmpty())
                    t->insertTab(args.value(0).toInt(), page, args.value(2).toString());
                else
                    t->insertTab(args.value(0).toInt(), page, iconOf(icon), args.value(2).toString());
            }
        }
    }
    else if (name == QLatin1String("removeTab")) {
        if (auto t = qobject_cast<QTabWidget*>(w))
            t->removeTab(args.value(0).toInt());
    }
    else if (name == QLatin1String("setTabEnabled")) {
        if (auto t = qobject_cast<QTabWidget*>(w))
            t->setTabEnabled(args.value(0).toInt(), args.value(1).toBool());
    }
    else if (name == QLatin1String("setTabVisible")) {
        if (auto t = qobject_cast<QTabWidget*>(w))
            t->setTabVisible(args.value(0).toInt(), args.value(1).toBool());
    }
    else if (name == QLatin1String("setTabToolTip")) {
        if (auto t = qobject_cast<QTabWidget*>(w))
            t->setTabToolTip(args.value(0).toInt(), args.value(1).toString());
    }
    else if (name == QLatin1String("setTabIcon")) {
        if (auto t = qobject_cast<QTabWidget*>(w))
            t->setTabIcon(args.value(0).toInt(), iconOf(args.value(1).toString()));
    }
    else if (name == QLatin1String("insertWidget")) {
        if (auto st = qobject_cast<QStackedWidget*>(w)) {
            if (QWidget* page = widgetOf(args.value(1), st))
                st->insertWidget(args.value(0).toInt(), page);
        }
        else if (auto sp = qobject_cast<QSplitter*>(w)) {
            if (QWidget* pane = widgetOf(args.value(1), sp))
                sp->insertWidget(args.value(0).toInt(), pane);
        }
    }
    else if (name == QLatin1String("removeWidget")) {
        if (auto st = qobject_cast<QStackedWidget*>(w)) {
            if (QWidget* page = widgetOf(args.value(0), nullptr))
                st->removeWidget(page);
        }
    }
    else if (name == QLatin1String("setWidget")) {
        if (auto sa = qobject_cast<QScrollArea*>(w)) {
            QVariant ref = args.value(0);
            if (ref.isNull()) {
                if (QWidget* old = sa->takeWidget())
                    old->setParent(nullptr);
            }
            else if (QWidget* content = widgetOf(ref, sa)) {
                sa->setWidget(content);
            }
        }
    }
    else if (name == QLatin1String("setCollapsible")) {
        if (auto sp = qobject_cast<QSplitter*>(w))
            sp->setCollapsible(args.value(0).toInt(), args.value(1).toBool());
    }
    else if (name == QLatin1String("setStretchFactor")) {
        if (auto sp = qobject_cast<QSplitter*>(w))
            sp->setStretchFactor(args.value(0).toInt(), args.value(1).toInt());
    }
    else if (onItemRequest(name, args)) {
        // an item view's
    }
}

QWidget* View::widgetOf(const QVariant& ref, QWidget* parent)
{
    auto model = qobject_cast<Fw::Widget*>(ref.value<QObject*>());
    if (!model)
        return nullptr;
    if (View* v = of(model))
        return v->widget();
    if (!parent)
        return nullptr;
    View* v = build(model, parent);
    return v ? v->widget() : nullptr;
}

void View::onLayoutOp(const QVariantMap& op)
{
    QWidget* w = _widget.data();
    if (!w)
        return;
    const QString name = op.value(QStringLiteral("layout")).toString();
    QLayout* layout = name.isEmpty() ? nullptr : w->findChild<QLayout*>(name);
    if (!layout) {
        Base::Console().Warning("FwQt: no layout %s under %s\n", qPrintable(name),
                                qPrintable(w->objectName()));
        return;
    }
    const QString kind = op.value(QStringLiteral("op")).toString();
    QVariantList args = op.value(QStringLiteral("args")).toList();
    auto box = qobject_cast<QBoxLayout*>(layout);
    auto grid = qobject_cast<QGridLayout*>(layout);
    auto form = qobject_cast<QFormLayout*>(layout);
    if (kind == QLatin1String("addWidget") || kind == QLatin1String("insertWidget")) {
        QWidget* child = widgetOf(op.value(QStringLiteral("widget")), layout->parentWidget());
        if (!child)
            return;
        if (kind == QLatin1String("insertWidget") && box) {
            box->insertWidget(op.value(QStringLiteral("index")).toInt(), child,
                              args.value(0).toInt());
        }
        else if (grid && args.size() >= 2) {
            grid->addWidget(child, args.at(0).toInt(), args.at(1).toInt(),
                            args.size() > 2 ? args.at(2).toInt() : 1,
                            args.size() > 3 ? args.at(3).toInt() : 1);
        }
        else if (form && args.size() >= 2) {
            form->setWidget(args.at(0).toInt(),
                            static_cast<QFormLayout::ItemRole>(args.at(1).toInt()), child);
        }
        else if (box) {
            box->addWidget(child, args.value(0).toInt());
        }
        else {
            layout->addWidget(child);
        }
        child->show();
    }
    else if (kind == QLatin1String("removeWidget")) {
        if (QWidget* child = widgetOf(op.value(QStringLiteral("widget")), nullptr))
            layout->removeWidget(child);
    }
    else if (kind == QLatin1String("takeAt")) {
        delete layout->takeAt(op.value(QStringLiteral("index")).toInt());
    }
    else if (kind == QLatin1String("addLayout")) {
        QLayout* sub = w->findChild<QLayout*>(op.value(QStringLiteral("sublayout")).toString());
        if (!sub)
            return;
        if (grid && args.size() >= 2)
            grid->addLayout(sub, args.at(0).toInt(), args.at(1).toInt(),
                            args.size() > 2 ? args.at(2).toInt() : 1,
                            args.size() > 3 ? args.at(3).toInt() : 1);
        else if (box)
            box->addLayout(sub, args.value(0).toInt());
    }
    else if (kind == QLatin1String("addStretch")) {
        if (box)
            box->addStretch(args.value(0).toInt());
    }
    else if (kind == QLatin1String("addSpacing")) {
        if (box)
            box->addSpacing(args.value(0).toInt());
    }
    else if (kind == QLatin1String("setContentsMargins")) {
        if (args.size() >= 4)
            layout->setContentsMargins(args.at(0).toInt(), args.at(1).toInt(), args.at(2).toInt(),
                                       args.at(3).toInt());
    }
    else if (kind == QLatin1String("setSpacing")) {
        layout->setSpacing(args.value(0).toInt());
    }
}


// ---- item views (G3b) --------------------------------------------------------------


namespace
{

/// A delegate is a cell TYPE per column (docs/Sandbox.md 7.11, G3b):
/// what editor a column takes, derived on the guest from its delegate's
/// createEditor once, and rendered here.
class CellDelegate : public QStyledItemDelegate
{
public:
    QVariantList types;
    using QStyledItemDelegate::QStyledItemDelegate;

    QVariantMap typeOf(const QModelIndex& index) const
    {
        return types.value(index.column()).toMap();
    }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override
    {
        QVariantMap t = typeOf(index);
        const QString kind = t.value(QStringLiteral("type")).toString();
        if (kind == QLatin1String("combo")) {
            auto c = new QComboBox(parent);
            c->addItems(t.value(QStringLiteral("items")).toStringList());
            return c;
        }
        if (kind == QLatin1String("int")) {
            auto s = new QSpinBox(parent);
            s->setRange(t.value(QStringLiteral("minimum"), 0).toInt(),
                        t.value(QStringLiteral("maximum"), 99).toInt());
            return s;
        }
        if (kind == QLatin1String("double")) {
            auto s = new QDoubleSpinBox(parent);
            s->setDecimals(t.value(QStringLiteral("decimals"), 2).toInt());
            s->setRange(t.value(QStringLiteral("minimum"), 0.0).toDouble(),
                        t.value(QStringLiteral("maximum"), 99.99).toDouble());
            return s;
        }
        if (kind == QLatin1String("text"))
            return new QLineEdit(parent);
        if (kind == QLatin1String("check"))
            return nullptr;
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        QVariant v = index.data(Qt::EditRole);
        if (auto c = qobject_cast<QComboBox*>(editor)) {
            int i = c->findText(v.toString());
            c->setCurrentIndex(i < 0 ? 0 : i);
            return;
        }
        if (auto s = qobject_cast<QSpinBox*>(editor)) {
            s->setValue(v.toInt());
            return;
        }
        if (auto d = qobject_cast<QDoubleSpinBox*>(editor)) {
            d->setValue(v.toDouble());
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        // a cell's text is a string on both sides
        if (auto c = qobject_cast<QComboBox*>(editor)) {
            model->setData(index, c->currentText(), Qt::EditRole);
            return;
        }
        if (auto s = qobject_cast<QSpinBox*>(editor)) {
            s->interpretText();
            model->setData(index, QString::number(s->value()), Qt::EditRole);
            return;
        }
        if (auto d = qobject_cast<QDoubleSpinBox*>(editor)) {
            d->interpretText();
            model->setData(index, QString::number(d->value()), Qt::EditRole);
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }
};

QTreeWidgetItem* treeItemOf(QTreeWidget* tree, const QModelIndex& index)
{
    if (!index.isValid())
        return nullptr;
    QModelIndex parent = index.parent();
    if (!parent.isValid())
        return tree->topLevelItem(index.row());
    QTreeWidgetItem* p = treeItemOf(tree, parent);
    return p ? p->child(index.row()) : nullptr;
}

QHeaderView* headerOf(QAbstractItemView* view, bool vertical)
{
    if (auto t = qobject_cast<QTreeView*>(view))
        return vertical ? nullptr : t->header();
    if (auto t = qobject_cast<QTableView*>(view))
        return vertical ? t->verticalHeader() : t->horizontalHeader();
    return nullptr;
}

}  // namespace

QModelIndex View::indexOf(int id, int column) const
{
    if (!_items)
        return QModelIndex();
    QPersistentModelIndex p = _items->byId.value(id);
    if (!p.isValid())
        return QModelIndex();
    return p.sibling(p.row(), column);
}

int View::idOf(const QModelIndex& index) const
{
    if (!_items || !index.isValid())
        return 0;
    return _items->ids.value(QPersistentModelIndex(index.sibling(index.row(), 0)));
}

void View::itemsChanged(const QVariantMap& op)
{
    applyItemOp(op);
}

void View::initItems()
{
    auto view = qobject_cast<QAbstractItemView*>(_widget.data());
    if (!view)
        return;
    _items = std::make_unique<Items>();
    _items->view = view;
    if (!view->model()) {
        _items->owned = new QStandardItemModel(view);
        view->setModel(_items->owned);
    }
    _items->model = view->model();
    readBackItems();

    connect(view, &QAbstractItemView::clicked, this, [this](const QModelIndex& i) {
        event(QStringLiteral("itemClicked"), QVariantList {idOf(i), i.column()});
    });
    connect(view, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& i) {
        event(QStringLiteral("itemDoubleClicked"), QVariantList {idOf(i), i.column()});
    });
    connect(view, &QAbstractItemView::activated, this, [this](const QModelIndex& i) {
        event(QStringLiteral("itemActivated"), QVariantList {idOf(i), i.column()});
    });
    connect(view, &QAbstractItemView::pressed, this, [this](const QModelIndex& i) {
        event(QStringLiteral("itemPressed"), QVariantList {idOf(i), i.column()});
    });
    if (auto tree = qobject_cast<QTreeView*>(view)) {
        connect(tree, &QTreeView::expanded, this, [this](const QModelIndex& i) {
            event(QStringLiteral("itemExpanded"), QVariantList {idOf(i)});
        });
        connect(tree, &QTreeView::collapsed, this, [this](const QModelIndex& i) {
            event(QStringLiteral("itemCollapsed"), QVariantList {idOf(i)});
        });
    }
    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        QVariantList ids;
        for (const QModelIndex& i : _items->view->selectionModel()->selectedIndexes()) {
            int id = idOf(i);
            if (id && !ids.contains(id))
                ids.append(id);
        }
        send(QVariantMap {{QStringLiteral("selection"), ids}});
    });
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& cur, const QModelIndex&) {
                send(QVariantMap {{QStringLiteral("currentId"), idOf(cur)},
                                  {QStringLiteral("currentColumn"), std::max(cur.column(), 0)}});
            });
    connect(_items->model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex& br, const QList<int>&) {
                if (_applying)
                    return;
                for (int r = tl.row(); r <= br.row(); ++r) {
                    for (int c = tl.column(); c <= br.column(); ++c) {
                        QModelIndex idx = tl.sibling(r, c);
                        int id = idOf(idx);
                        if (!id)
                            continue;
                        QVariantMap cell;
                        cell.insert(QStringLiteral("text"), idx.data(Qt::EditRole).toString());
                        QVariant check = idx.data(Qt::CheckStateRole);
                        if (check.isValid())
                            cell.insert(QStringLiteral("check"), check.toInt());
                        event(QStringLiteral("itemEdited"), QVariantList {id, c, cell});
                    }
                }
            });
    if (QHeaderView* h = headerOf(view, false)) {
        connect(h, &QHeaderView::sectionClicked, this, [this](int i) {
            event(QStringLiteral("sectionClicked"), QVariantList {QStringLiteral("h"), i});
        });
    }
    // the rows the model holds already (a native model realized late)
    auto iv = qobject_cast<Fw::ItemView*>(_model);
    if (iv && !iv->topLevel().isEmpty()) {
        QVariantMap op;
        op.insert(QStringLiteral("item"), QStringLiteral("insert"));
        op.insert(QStringLiteral("parent"), 0);
        op.insert(QStringLiteral("index"), 0);
        op.insert(QStringLiteral("rows"), iv->snapshot());
        applyItemOp(op);
    }
}

void View::readBackItems()
{
    if (!_items)
        return;
    QAbstractItemModel* m = _items->model;
    int cols = m->columnCount();
    if (cols > 0 && !_model->isTouched(QStringLiteral("columnCount")))
        _model->setInitial(QStringLiteral("columnCount"), cols);
    QStringList labels;
    bool any = false;
    for (int c = 0; c < cols; ++c) {
        QString label = m->headerData(c, Qt::Horizontal).toString();
        any = any || !label.isEmpty();
        labels.append(label);
    }
    if (any && !_model->isTouched(QStringLiteral("columns")))
        _model->setInitial(QStringLiteral("columns"), labels);
    if (QHeaderView* h = headerOf(_items->view, false)) {
        QVariantList widths;
        for (int c = 0; c < cols; ++c)
            widths.append(h->sectionSize(c));
        if (!_model->isTouched(QStringLiteral("columnWidths")))
            _model->setInitial(QStringLiteral("columnWidths"), widths);
        if (auto t = qobject_cast<QTableView*>(_items->view))
            _model->setInitial(QStringLiteral("headerHidden"), t->horizontalHeader()->isHidden());
    }
}

void View::ensureColumns(int count, const QModelIndex& parent)
{
    if (!_items || count <= 0)
        return;
    QAbstractItemModel* m = _items->model;
    if (auto tree = qobject_cast<QTreeWidget*>(_items->view)) {
        if (tree->columnCount() < count)
            tree->setColumnCount(count);
        return;
    }
    if (auto table = qobject_cast<QTableWidget*>(_items->view)) {
        if (table->columnCount() < count)
            table->setColumnCount(count);
        return;
    }
    int have = m->columnCount(parent);
    if (have < count)
        m->insertColumns(have, count - have, parent);
    if (parent.isValid() && m->columnCount() < count)
        m->insertColumns(m->columnCount(), count - m->columnCount());
}

void View::applyCell(const QModelIndex& index, const QVariantMap& cell)
{
    if (!index.isValid())
        return;
    QAbstractItemModel* m = _items->model;
    for (auto it = cell.constBegin(); it != cell.constEnd(); ++it) {
        const QString& k = it.key();
        const QVariant& v = it.value();
        if (k == QLatin1String("text"))
            m->setData(index, v.toString(), Qt::EditRole);
        else if (k == QLatin1String("icon"))
            m->setData(index, QVariant::fromValue(iconOf(v.toString())), Qt::DecorationRole);
        else if (k == QLatin1String("toolTip"))
            m->setData(index, v.toString(), Qt::ToolTipRole);
        else if (k == QLatin1String("statusTip"))
            m->setData(index, v.toString(), Qt::StatusTipRole);
        else if (k == QLatin1String("whatsThis"))
            m->setData(index, v.toString(), Qt::WhatsThisRole);
        else if (k == QLatin1String("check"))
            m->setData(index,
                       v.isNull() ? QVariant()
                                  : QVariant::fromValue(static_cast<Qt::CheckState>(v.toInt())),
                       Qt::CheckStateRole);
        else if (k == QLatin1String("fg"))
            m->setData(index,
                       v.toList().isEmpty() ? QVariant() : QVariant::fromValue(QBrush(colorOf(v))),
                       Qt::ForegroundRole);
        else if (k == QLatin1String("bg"))
            m->setData(index,
                       v.toList().isEmpty() ? QVariant() : QVariant::fromValue(QBrush(colorOf(v))),
                       Qt::BackgroundRole);
        else if (k == QLatin1String("bold")) {
            QFont f = _items->view->font();
            f.setBold(v.toBool());
            m->setData(index, QVariant::fromValue(f), Qt::FontRole);
        }
        else if (k == QLatin1String("align"))
            m->setData(index, v.toInt(), Qt::TextAlignmentRole);
        else if (k == QLatin1String("flags") && !v.isNull())
            setItemFlags(index, v.toInt(), false);
    }
}

void View::setItemFlags(const QModelIndex& index, int flags, bool wholeRow)
{
    if (!index.isValid())
        return;
    const Qt::ItemFlags f(flags);
    if (auto tree = qobject_cast<QTreeWidget*>(_items->view)) {
        if (QTreeWidgetItem* item = treeItemOf(tree, index))
            item->setFlags(f);
        return;
    }
    const int cols = wholeRow ? _items->model->columnCount(index.parent()) : 1;
    for (int c = 0; c < cols; ++c) {
        QModelIndex idx = wholeRow ? index.sibling(index.row(), c) : index;
        if (auto table = qobject_cast<QTableWidget*>(_items->view)) {
            QTableWidgetItem* item = table->item(idx.row(), idx.column());
            if (!item) {
                item = new QTableWidgetItem;
                table->setItem(idx.row(), idx.column(), item);
            }
            item->setFlags(f);
        }
        else if (auto list = qobject_cast<QListWidget*>(_items->view)) {
            if (QListWidgetItem* item = list->item(idx.row()))
                item->setFlags(f);
        }
        else if (_items->owned) {
            QStandardItem* item = _items->owned->itemFromIndex(idx);
            if (!item) {
                item = new QStandardItem;
                if (idx.parent().isValid())
                    _items->owned->itemFromIndex(idx.parent())->setChild(idx.row(), idx.column(),
                                                                          item);
                else
                    _items->owned->setItem(idx.row(), idx.column(), item);
            }
            item->setFlags(f);
        }
    }
}

void View::insertItemRows(int parentId, int index, const QVariantList& rows)
{
    QAbstractItemModel* m = _items->model;
    QModelIndex parent = parentId ? indexOf(parentId, 0) : QModelIndex();
    if (parentId && !parent.isValid())
        return;
    int cols = 0;
    for (const QVariant& r : rows)
        cols = std::max(cols, static_cast<int>(r.toMap().value(QStringLiteral("cells")).toList().size()));
    if (parent.isValid() || cols > m->columnCount())
        ensureColumns(std::max(cols, m->columnCount()), parent);
    if (m->columnCount(parent) < 1)
        ensureColumns(1, parent);
    const int count = rows.size();
    if (index < 0 || index > m->rowCount(parent))
        index = m->rowCount(parent);
    if (!m->insertRows(index, count, parent))
        return;
    auto treeView = qobject_cast<QTreeView*>(_items->view);
    for (int i = 0; i < count; ++i) {
        QVariantMap r = rows.at(i).toMap();
        QModelIndex idx0 = m->index(index + i, 0, parent);
        const int id = r.value(QStringLiteral("id")).toInt();
        if (id) {
            QPersistentModelIndex p(idx0);
            _items->byId.insert(id, p);
            _items->ids.insert(p, id);
        }
        QVariantList cells = r.value(QStringLiteral("cells")).toList();
        for (int c = 0; c < cells.size(); ++c)
            applyCell(m->index(index + i, c, parent), cells.at(c).toMap());
        if (r.contains(QStringLiteral("flags")))
            setItemFlags(idx0, r.value(QStringLiteral("flags")).toInt(), true);
        if (r.value(QStringLiteral("hidden")).toBool())
            onItemRequest(QStringLiteral("_hide"), QVariantList {id, true});
        QVariantList kids = r.value(QStringLiteral("children")).toList();
        if (!kids.isEmpty() && id)
            insertItemRows(id, 0, kids);
        if (treeView && r.value(QStringLiteral("expanded")).toBool())
            treeView->setExpanded(idx0, true);
    }
}

void View::applyItemOp(const QVariantMap& op)
{
    if (!_items || !_widget)
        return;
    const bool was = _applying;
    _applying = true;
    QAbstractItemModel* m = _items->model;
    const QString kind = op.value(QStringLiteral("item")).toString();
    const int id = op.value(QStringLiteral("id")).toInt();
    if (kind == QLatin1String("insert")) {
        insertItemRows(op.value(QStringLiteral("parent")).toInt(),
                       op.contains(QStringLiteral("index"))
                           ? op.value(QStringLiteral("index")).toInt()
                           : -1,
                       op.value(QStringLiteral("rows")).toList());
    }
    else if (kind == QLatin1String("set")) {
        int col = op.value(QStringLiteral("col")).toInt();
        QModelIndex idx = indexOf(id, 0);
        if (idx.isValid()) {
            if (col >= m->columnCount(idx.parent()))
                ensureColumns(col + 1, idx.parent());
            applyCell(idx.sibling(idx.row(), col), op.value(QStringLiteral("cell")).toMap());
        }
    }
    else if (kind == QLatin1String("row")) {
        QModelIndex idx = indexOf(id, 0);
        QVariantMap r = op.value(QStringLiteral("row")).toMap();
        if (idx.isValid()) {
            auto treeView = qobject_cast<QTreeView*>(_items->view);
            if (r.contains(QStringLiteral("expanded")) && treeView)
                treeView->setExpanded(idx, r.value(QStringLiteral("expanded")).toBool());
            if (r.contains(QStringLiteral("hidden")))
                onItemRequest(QStringLiteral("_hide"),
                              QVariantList {id, r.value(QStringLiteral("hidden")).toBool()});
            if (r.contains(QStringLiteral("flags")))
                setItemFlags(idx, r.value(QStringLiteral("flags")).toInt(), true);
            if (r.contains(QStringLiteral("spanned")) && treeView)
                treeView->setFirstColumnSpanned(idx.row(), idx.parent(),
                                                r.value(QStringLiteral("spanned")).toBool());
        }
    }
    else if (kind == QLatin1String("remove")) {
        QModelIndex idx = indexOf(id, 0);
        if (idx.isValid())
            m->removeRows(idx.row(), 1, idx.parent());
        _items->forget(id);
        _items->prune();
    }
    else if (kind == QLatin1String("clear")) {
        if (m->rowCount() > 0)
            m->removeRows(0, m->rowCount());
        _items->byId.clear();
        _items->ids.clear();
    }
    else if (kind == QLatin1String("sort")) {
        int col = op.value(QStringLiteral("col")).toInt();
        auto order = static_cast<Qt::SortOrder>(op.value(QStringLiteral("order")).toInt());
        if (id == 0) {
            _items->view->model()->sort(col, order);
        }
        else if (auto tree = qobject_cast<QTreeWidget*>(_items->view)) {
            if (QTreeWidgetItem* item = treeItemOf(tree, indexOf(id, 0)))
                item->sortChildren(col, order);
        }
        else if (_items->owned) {
            if (QStandardItem* item = _items->owned->itemFromIndex(indexOf(id, 0)))
                item->sortChildren(col, order);
        }
        _items->prune();
    }
    _applying = was;
}

void View::applySelection()
{
    if (!_items)
        return;
    QItemSelection sel;
    QAbstractItemModel* m = _items->model;
    for (const QVariant& v : _model->property("selection").toList()) {
        QModelIndex idx = indexOf(v.toInt(), 0);
        if (!idx.isValid())
            continue;
        int last = std::max(m->columnCount(idx.parent()) - 1, 0);
        sel.select(idx, idx.sibling(idx.row(), last));
    }
    _items->view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
}

void View::applyCurrent()
{
    if (!_items)
        return;
    int id = _model->property("currentId").toInt();
    QModelIndex idx = id ? indexOf(id, _model->property("currentColumn").toInt()) : QModelIndex();
    if (idx.isValid() || !id)
        _items->view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
}

void View::applyCellTypes()
{
    if (!_items)
        return;
    QVariantList types = _model->property("cellTypes").toList();
    auto d = dynamic_cast<CellDelegate*>(_items->view->itemDelegate());
    if (!d) {
        d = new CellDelegate(_items->view);
        _items->view->setItemDelegate(d);
    }
    d->types = types;
}

void View::headerCall(const QString& which, const QString& method, const QVariantList& args)
{
    QHeaderView* h = headerOf(_items->view, which == QLatin1String("v"));
    if (!h)
        return;
    if (method == QLatin1String("setSectionResizeMode")) {
        if (args.size() >= 2)
            h->setSectionResizeMode(args.at(0).toInt(),
                                    static_cast<QHeaderView::ResizeMode>(args.at(1).toInt()));
        else
            h->setSectionResizeMode(static_cast<QHeaderView::ResizeMode>(args.value(0).toInt()));
    }
    else if (method == QLatin1String("setStretchLastSection"))
        h->setStretchLastSection(args.value(0).toBool());
    else if (method == QLatin1String("setDefaultSectionSize"))
        h->setDefaultSectionSize(args.value(0).toInt());
    else if (method == QLatin1String("setMinimumSectionSize"))
        h->setMinimumSectionSize(args.value(0).toInt());
    else if (method == QLatin1String("setMaximumSectionSize"))
        h->setMaximumSectionSize(args.value(0).toInt());
    else if (method == QLatin1String("resizeSection"))
        h->resizeSection(args.value(0).toInt(), args.value(1).toInt());
    else if (method == QLatin1String("resizeSections")) {
        h->resizeSections(static_cast<QHeaderView::ResizeMode>(
            args.isEmpty() ? static_cast<int>(QHeaderView::ResizeToContents) : args.at(0).toInt()));
    }
    else if (method == QLatin1String("setSortIndicator"))
        h->setSortIndicator(args.value(0).toInt(),
                            static_cast<Qt::SortOrder>(args.value(1).toInt()));
    else if (method == QLatin1String("setSortIndicatorShown"))
        h->setSortIndicatorShown(args.value(0).toBool());
    else if (method == QLatin1String("setSectionsClickable"))
        h->setSectionsClickable(args.value(0).toBool());
    else if (method == QLatin1String("setSectionsMovable"))
        h->setSectionsMovable(args.value(0).toBool());
    else if (method == QLatin1String("setDefaultAlignment"))
        h->setDefaultAlignment(Qt::Alignment(args.value(0).toInt()));
    else if (method == QLatin1String("setHighlightSections"))
        h->setHighlightSections(args.value(0).toBool());
    else if (method == QLatin1String("setVisible"))
        h->setVisible(args.value(0).toBool());
    else if (method == QLatin1String("hideSection"))
        h->hideSection(args.value(0).toInt());
    else if (method == QLatin1String("showSection"))
        h->showSection(args.value(0).toInt());
    else if (method == QLatin1String("moveSection"))
        h->moveSection(args.value(0).toInt(), args.value(1).toInt());
}

bool View::onItemRequest(const QString& name, const QVariantList& args)
{
    if (!_items)
        return false;
    QAbstractItemView* view = _items->view;
    auto treeView = qobject_cast<QTreeView*>(view);
    auto tableView = qobject_cast<QTableView*>(view);
    auto listView = qobject_cast<QListView*>(view);
    if (name == QLatin1String("expandAll")) {
        if (treeView)
            treeView->expandAll();
    }
    else if (name == QLatin1String("collapseAll")) {
        if (treeView)
            treeView->collapseAll();
    }
    else if (name == QLatin1String("expandToDepth")) {
        if (treeView)
            treeView->expandToDepth(args.value(0).toInt());
    }
    else if (name == QLatin1String("resizeColumnToContents")) {
        if (treeView)
            treeView->resizeColumnToContents(args.value(0).toInt());
        else if (tableView)
            tableView->resizeColumnToContents(args.value(0).toInt());
    }
    else if (name == QLatin1String("resizeColumnsToContents")) {
        if (treeView)
            for (int c = 0; c < _items->model->columnCount(); ++c)
                treeView->resizeColumnToContents(c);
        else if (tableView)
            tableView->resizeColumnsToContents();
    }
    else if (name == QLatin1String("resizeRowsToContents")) {
        if (tableView)
            tableView->resizeRowsToContents();
    }
    else if (name == QLatin1String("setColumnHidden")) {
        if (treeView)
            treeView->setColumnHidden(args.value(0).toInt(), args.value(1).toBool());
        else if (tableView)
            tableView->setColumnHidden(args.value(0).toInt(), args.value(1).toBool());
    }
    else if (name == QLatin1String("sortByColumn")) {
        auto order = static_cast<Qt::SortOrder>(args.value(1).toInt());
        if (treeView)
            treeView->sortByColumn(args.value(0).toInt(), order);
        else if (tableView)
            tableView->sortByColumn(args.value(0).toInt(), order);
        else if (listView)
            _items->model->sort(args.value(0).toInt(), order);
        _items->prune();
    }
    else if (name == QLatin1String("scrollTo")) {
        QModelIndex idx = indexOf(args.value(0).toInt(), 0);
        if (idx.isValid())
            view->scrollTo(idx);
    }
    else if (name == QLatin1String("scrollToTop"))
        view->scrollToTop();
    else if (name == QLatin1String("scrollToBottom"))
        view->scrollToBottom();
    else if (name == QLatin1String("edit")) {
        QModelIndex idx = indexOf(args.value(0).toInt(), args.value(1).toInt());
        if (idx.isValid())
            view->edit(idx);
    }
    else if (name == QLatin1String("setItemWidget")) {
        QModelIndex idx = indexOf(args.value(0).toInt(), args.value(1).toInt());
        if (idx.isValid()) {
            QVariant ref = args.value(2);
            QWidget* child = ref.isNull() ? nullptr : widgetOf(ref, view->viewport());
            view->setIndexWidget(idx, child);
        }
    }
    else if (name == QLatin1String("setRowHeight")) {
        if (tableView)
            tableView->setRowHeight(args.value(0).toInt(), args.value(1).toInt());
    }
    else if (name == QLatin1String("setSpan")) {
        if (tableView)
            tableView->setSpan(args.value(0).toInt(), args.value(1).toInt(),
                               args.value(2).toInt(), args.value(3).toInt());
    }
    else if (name == QLatin1String("clearSelection"))
        view->clearSelection();
    else if (name == QLatin1String("header"))
        headerCall(args.value(0).toString(), args.value(1).toString(), args.value(2).toList());
    else if (name == QLatin1String("_hide")) {
        QModelIndex idx = indexOf(args.value(0).toInt(), 0);
        bool on = args.value(1).toBool();
        if (!idx.isValid())
            return true;
        if (treeView)
            treeView->setRowHidden(idx.row(), idx.parent(), on);
        else if (tableView)
            tableView->setRowHidden(idx.row(), on);
        else if (listView)
            listView->setRowHidden(idx.row(), on);
    }
    else {
        return false;
    }
    return true;
}

// ---- windows of their own (G3b) -------------------------------------------------

QWidget* Gui::FwQt::realizeTopLevel(Fw::Widget* root)
{
    if (!root)
        return nullptr;
    if (View* v = View::of(root))
        return v->widget();
    QWidget* parent = root->qtClass() == QLatin1String("QDialog") ? getMainWindow() : nullptr;
    View* v = View::build(root, parent);
    QWidget* w = v ? v->widget() : nullptr;
    if (!w)
        return nullptr;
    if (parent && !qobject_cast<QDialog*>(w))
        w->setParent(nullptr);
    QObject::connect(root, &QObject::destroyed, w, &QObject::deleteLater);
    return w;
}

int Gui::FwQt::execDialog(Fw::Widget* root)
{
    QWidget* w = realizeTopLevel(root);
    auto dlg = qobject_cast<QDialog*>(w);
    if (!dlg)
        throw Base::TypeError("exec: the root is not a QDialog");
    return dlg->exec();
}

#include "moc_FwQtView.cpp"
