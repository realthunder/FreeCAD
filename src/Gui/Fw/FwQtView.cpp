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
#include <QBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMetaProperty>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSpinBox>
#include <QTextEdit>
#include <QWidget>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>

#include "BitmapFactory.h"
#include "Fw/FwQtView.h"
#include "Fw/FwWidgets.h"
#include "InputField.h"
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
    connect(model, &Fw::Widget::propertiesChanged, this,
            [this](const QStringList& names, int source) {
                if (source != static_cast<int>(Fw::Source::Backend))
                    apply(names);
            });
    connect(model, &Fw::Widget::requested, this, &View::onRequest);
    connect(model, &Fw::Widget::layoutChanged, this, &View::onLayoutOp);
    connect(widget, &QObject::destroyed, this, [this]() {
        _widget = nullptr;
        release(false);
    });
    connectWidget();
}

View::~View()
{
    auto it = registry().find(_model);
    if (it != registry().end() && it.value() == this)
        registry().erase(it);
}

void View::release(bool deleteWidget)
{
    for (const auto& child : _children)
        if (child)
            child->release(false);
    _children.clear();
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
    QStringList keys = QStringList(model->touched().begin(), model->touched().end());
    if (!model->objectName().isEmpty() && !keys.contains(QLatin1String("objectName")))
        keys.append(QStringLiteral("objectName"));
    view->apply(keys);
    return view;
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
    // the range before the value, else Qt clamps the value
    QStringList first;
    for (const char* k : {"minimum", "maximum", "decimals", "checkable", "editable", "tristate"})
        if (keys.removeAll(QLatin1String(k)))
            first.append(QLatin1String(k));
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
    else if (name == QLatin1String("setParent")) {
        auto parentModel = qobject_cast<Fw::Widget*>(args.value(0).value<QObject*>());
        View* pv = parentModel ? of(parentModel) : nullptr;
        QWidget* pw = pv ? pv->widget() : nullptr;
        // a parent that is not realized keeps the widget where it is
        if (!parentModel || pw)
            w->setParent(pw);
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

#include "moc_FwQtView.cpp"
