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
#include <QWidget>
#endif

#include <CXX/Objects.hxx>
#include <Base/Interpreter.h>

#include "Control.h"
#include "Fw/FwPy.h"
#include "Fw/FwQtPanel.h"
#include "Fw/FwQtView.h"
#include "Fw/FwStore.h"
#include "Fw/FwToolBarMirror.h"
#include "SceneControl.h"
#include "SceneWidgets.h"
#include "Fw/FwWidgets.h"
#include "PythonWrapper.h"

using namespace Gui;
using namespace Gui::Fw;

PyObject* Gui::Fw::variantToPy(const QVariant& value)
{
    switch (value.typeId()) {
        case QMetaType::UnknownType:
        case QMetaType::Nullptr:
            Py_RETURN_NONE;
        case QMetaType::Bool:
            return PyBool_FromLong(value.toBool() ? 1 : 0);
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Short:
        case QMetaType::UShort:
        case QMetaType::Long:
        case QMetaType::ULong:
            return PyLong_FromLongLong(value.toLongLong());
        case QMetaType::Double:
        case QMetaType::Float:
            return PyFloat_FromDouble(value.toDouble());
        case QMetaType::QString:
            return PyUnicode_FromString(value.toString().toUtf8().constData());
        case QMetaType::QByteArray:
            return PyUnicode_FromString(value.toByteArray().constData());
        case QMetaType::QStringList: {
            QStringList l = value.toStringList();
            PyObject* list = PyList_New(l.size());
            for (int i = 0; i < l.size(); ++i)
                PyList_SET_ITEM(list, i, PyUnicode_FromString(l.at(i).toUtf8().constData()));
            return list;
        }
        case QMetaType::QVariantList: {
            QVariantList l = value.toList();
            PyObject* list = PyList_New(l.size());
            for (int i = 0; i < l.size(); ++i)
                PyList_SET_ITEM(list, i, variantToPy(l.at(i)));
            return list;
        }
        case QMetaType::QVariantMap: {
            QVariantMap m = value.toMap();
            PyObject* dict = PyDict_New();
            for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
                PyObject* v = variantToPy(it.value());
                PyDict_SetItemString(dict, it.key().toUtf8().constData(), v);
                Py_DECREF(v);
            }
            return dict;
        }
        default:
            if (value.canConvert<QString>())
                return PyUnicode_FromString(value.toString().toUtf8().constData());
            Py_RETURN_NONE;
    }
}

QVariant Gui::Fw::pyToVariant(PyObject* obj)
{
    if (!obj || obj == Py_None)
        return QVariant();
    if (PyBool_Check(obj))
        return QVariant(obj == Py_True);
    if (PyLong_Check(obj)) {
        long long v = PyLong_AsLongLong(obj);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return QVariant();
        }
        if (v >= INT_MIN && v <= INT_MAX)
            return QVariant(static_cast<int>(v));
        return QVariant(static_cast<qlonglong>(v));
    }
    if (PyFloat_Check(obj))
        return QVariant(PyFloat_AsDouble(obj));
    if (PyUnicode_Check(obj))
        return QVariant(QString::fromUtf8(PyUnicode_AsUTF8(obj)));
    if (PyBytes_Check(obj))
        return QVariant(QString::fromUtf8(PyBytes_AsString(obj)));
    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t n = PySequence_Size(obj);
        QVariantList out;
        bool allStrings = n > 0;
        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject* item = PySequence_GetItem(obj, i);
            if (!PyUnicode_Check(item))
                allStrings = false;
            out.append(pyToVariant(item));
            Py_XDECREF(item);
        }
        if (allStrings) {
            QStringList sl;
            for (const auto& v : out)
                sl.append(v.toString());
            return QVariant(sl);
        }
        return QVariant(out);
    }
    if (PyDict_Check(obj)) {
        QVariantMap out;
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            if (!PyUnicode_Check(key))
                continue;
            out.insert(QString::fromUtf8(PyUnicode_AsUTF8(key)), pyToVariant(value));
        }
        return QVariant(out);
    }
    return QVariant();
}

namespace
{

Store& store()
{
    return Store::instance();
}

PyObject* py_ids(PyObject*, PyObject*)
{
    return variantToPy(store().ids());
}

PyObject* py_count(PyObject*, PyObject*)
{
    return PyLong_FromLong(store().count());
}

PyObject* py_reset(PyObject*, PyObject*)
{
    store().reset();
    Py_RETURN_NONE;
}

PyObject* py_stats(PyObject*, PyObject*)
{
    const Store::Stats& s = store().stats();
    QVariantMap m;
    m.insert(QStringLiteral("opened"), s.opened);
    m.insert(QStringLiteral("updated"), s.updated);
    m.insert(QStringLiteral("customs"), s.customs);
    m.insert(QStringLiteral("closed"), s.closed);
    m.insert(QStringLiteral("sent"), s.sent);
    m.insert(QStringLiteral("events"), s.events);
    return variantToPy(m);
}

PyObject* py_resetStats(PyObject*, PyObject*)
{
    store().resetStats();
    Py_RETURN_NONE;
}

Widget* objectArg(PyObject* args)
{
    const char* id = nullptr;
    if (!PyArg_ParseTuple(args, "s", &id))
        return nullptr;
    Widget* w = store().resolve(QString::fromUtf8(id));
    if (!w)
        PyErr_Format(PyExc_KeyError, "no form widget '%s'", id);
    return w;
}

PyObject* py_resolve(PyObject*, PyObject* args)
{
    const char* ref = nullptr;
    if (!PyArg_ParseTuple(args, "s", &ref))
        return nullptr;
    Widget* w = store().resolve(QString::fromUtf8(ref));
    if (!w)
        Py_RETURN_NONE;
    return variantToPy(store().idOf(w));
}

/// A layout spec's objects as their store ids (a Python reader).
QVariant withIds(const QVariant& v)
{
    if (v.typeId() == QMetaType::QVariantMap) {
        QVariantMap out;
        QVariantMap in = v.toMap();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it)
            out.insert(it.key(), withIds(it.value()));
        return out;
    }
    if (v.typeId() == QMetaType::QVariantList) {
        QVariantList out;
        for (const QVariant& x : v.toList())
            out.append(withIds(x));
        return out;
    }
    if (v.canConvert<QObject*>() && v.typeId() != QMetaType::QString) {
        QObject* o = v.value<QObject*>();
        return o ? QVariant(store().idOf(o)) : QVariant();
    }
    return v;
}

PyObject* py_info(PyObject*, PyObject* args)
{
    Widget* w = objectArg(args);
    if (!w)
        return nullptr;
    QVariantMap m;
    m.insert(QStringLiteral("id"), store().idOf(w));
    m.insert(QStringLiteral("class"), w->modelName());
    m.insert(QStringLiteral("qtClass"), w->qtClass());
    m.insert(QStringLiteral("objectName"), w->objectName());
    m.insert(QStringLiteral("properties"), w->properties());
    QStringList touched(w->touched().begin(), w->touched().end());
    touched.sort();
    m.insert(QStringLiteral("touched"), touched);
    FwQt::View* v = FwQt::View::of(w);
    m.insert(QStringLiteral("bound"), v != nullptr && v->widget() != nullptr);
    if (auto form = qobject_cast<UiForm*>(w)) {
        m.insert(QStringLiteral("uiFile"), form->uiFile());
        QVariantMap named;
        const auto& nw = form->namedWidgets();
        for (auto it = nw.constBegin(); it != nw.constEnd(); ++it)
            named.insert(it.key(), store().idOf(it.value()));
        m.insert(QStringLiteral("widgets"), named);
    }
    if (auto view = qobject_cast<ItemView*>(w))
        m.insert(QStringLiteral("items"), view->snapshot());
    if (Layout* lay = w->layout())
        m.insert(QStringLiteral("layout"), withIds(lay->spec()));
    QVariantList actions;
    for (Widget* a : w->actions())
        actions.append(store().idOf(a));
    if (!actions.isEmpty())
        m.insert(QStringLiteral("actions"), actions);
    QString parentId = w->parentWidget() ? store().idOf(w->parentWidget()) : QString();
    m.insert(QStringLiteral("parent"), parentId.isEmpty() ? QVariant() : QVariant(parentId));
    return variantToPy(m);
}

PyObject* py_find(PyObject*, PyObject* args, PyObject* kwds)
{
    static const char* kwlist[] = {"className", "objectName", nullptr};
    const char* cls = nullptr;
    const char* name = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|zz", const_cast<char**>(kwlist), &cls, &name))
        return nullptr;
    QStringList out;
    for (const QString& id : store().ids()) {
        Widget* w = store().object(id);
        if (!w)
            continue;
        if (cls && w->modelName() != QString::fromUtf8(cls)
            && w->qtClass() != QString::fromUtf8(cls))
            continue;
        if (name && w->objectName() != QString::fromUtf8(name))
            continue;
        out.append(id);
    }
    return variantToPy(out);
}

PyObject* py_widget(PyObject*, PyObject* args)
{
    Widget* w = objectArg(args);
    if (!w)
        return nullptr;
    FwQt::View* v = FwQt::View::of(w);
    if (!v || !v->widget())
        Py_RETURN_NONE;
    try {
        PythonWrapper wrap;
        wrap.loadWidgetsModule();
        return Py::new_reference_to(wrap.fromQWidget(v->widget()));
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject* py_setProperty(PyObject*, PyObject* args)
{
    const char* id = nullptr;
    const char* name = nullptr;
    PyObject* value = nullptr;
    if (!PyArg_ParseTuple(args, "ssO", &id, &name, &value))
        return nullptr;
    Widget* w = store().resolve(QString::fromUtf8(id));
    if (!w) {
        PyErr_Format(PyExc_KeyError, "no form widget '%s'", id);
        return nullptr;
    }
    return PyBool_FromLong(w->setProperty(name, pyToVariant(value)) ? 1 : 0);
}

/// The active PanelDialog's accept or reject, as the OK/Cancel button
/// would drive it: the hook answers, and a True closes the dialog.
/// (This fork's TaskView never hands the dialog its button box, so
/// `Control.activeTaskDialog().accept()` clicks nothing.)
PyObject* panelButton(bool accept)
{
    auto active = Gui::Control().activeDialog();
    if (!active) {
        PyErr_SetString(PyExc_RuntimeError, "no task dialog is active");
        return nullptr;
    }
    auto dlg = dynamic_cast<FwQt::PanelDialog*>(active);
    if (!dlg) {
        // a native dialog (a ported panel among them): the task view's
        // own OK/Cancel path, the one the button box would take
        if (accept)
            Gui::Control().accept();
        else
            Gui::Control().reject();
        return PyBool_FromLong(Gui::Control().activeDialog() != active ? 1 : 0);
    }
    bool ok = accept ? dlg->accept() : dlg->reject();
    if (ok)
        Gui::Control().closeDialog();
    return PyBool_FromLong(ok ? 1 : 0);
}

PyObject* py_accept(PyObject*, PyObject*)
{
    return panelButton(true);
}

PyObject* py_reject(PyObject*, PyObject*)
{
    return panelButton(false);
}

PyObject* py_knownClasses(PyObject*, PyObject*)
{
    return variantToPy(knownClasses());
}

// -- the fan-out, the mirror and the stream (docs/Sandbox.md 7.18) ----

/// The store's `message` emissions since the last drain, once a gate
/// asked for them: [{"id", "method", "content", "origin"}].
QVariantList& messageLog()
{
    static QVariantList log;
    static bool connected = false;
    if (!connected) {
        connected = true;
        QObject::connect(&Store::instance(), &Store::message,
                         [](const QString& id, const QString& method, const QVariantMap& content,
                            quint64 origin) {
                             QVariantMap m;
                             m.insert(QStringLiteral("id"), id);
                             m.insert(QStringLiteral("method"), method);
                             m.insert(QStringLiteral("content"), content);
                             m.insert(QStringLiteral("origin"), static_cast<qulonglong>(origin));
                             log.append(m);
                         });
    }
    return log;
}

PyObject* py_messages(PyObject*, PyObject*)
{
    QVariantList out = messageLog();
    messageLog().clear();
    return variantToPy(out);
}

PyObject* py_watchMessages(PyObject*, PyObject*)
{
    messageLog();
    Py_RETURN_NONE;
}

PyObject* py_snapshot(PyObject*, PyObject* args)
{
    const char* id = nullptr;
    if (!PyArg_ParseTuple(args, "s", &id))
        return nullptr;
    return variantToPy(store().snapshot(QString::fromUtf8(id)));
}

PyObject* py_snapshotOrder(PyObject*, PyObject*)
{
    return variantToPy(store().snapshotOrder());
}

PyObject* py_mirrorToolBars(PyObject*, PyObject* args)
{
    int on = 1;
    if (!PyArg_ParseTuple(args, "|p", &on))
        return nullptr;
    if (on)
        ToolBarMirror::instance().start();
    else
        ToolBarMirror::instance().stop();
    return PyBool_FromLong(ToolBarMirror::instance().isRunning());
}

PyObject* py_mirrorFlush(PyObject*, PyObject*)
{
    ToolBarMirror::instance().flush();
    return PyLong_FromLong(ToolBarMirror::instance().rebuildCount());
}

/// What the injected sender collected: [(client, json)].
QVariantList& pushLog()
{
    static QVariantList log;
    static bool injected = false;
    if (!injected) {
        injected = true;
        SceneWidgetStream::instance().setSender([](uint64_t client, const std::string& json) {
            // a client above 1000 is "gone" (a test's lost connection)
            if (client > 1000)
                return false;
            QVariantMap entry;
            entry.insert(QStringLiteral("client"), static_cast<qulonglong>(client));
            entry.insert(QStringLiteral("json"), QString::fromStdString(json));
            log.append(entry);
            return true;
        });
    }
    return log;
}

PyObject* py_control(PyObject*, PyObject* args)
{
    const char* json = nullptr;
    unsigned long long client = 1;
    int viewOnly = 0;
    if (!PyArg_ParseTuple(args, "s|Kp", &json, &client, &viewOnly))
        return nullptr;
    pushLog();
    installSceneWidgetOps();
    std::string reply = handleSceneControlRequest(json, std::string(), viewOnly != 0, client);
    return PyUnicode_FromString(reply.c_str());
}

PyObject* py_pushed(PyObject*, PyObject*)
{
    QVariantList out = pushLog();
    pushLog().clear();
    return variantToPy(out);
}

PyMethodDef Methods[] = {
    {"messages", py_messages, METH_NOARGS,
     "messages() -> the store's message emissions since the last call"
     " ([{id, method, content, origin}]); starts collecting on first use"},
    {"watchMessages", py_watchMessages, METH_NOARGS,
     "watchMessages() -> start collecting the store's messages"},
    {"snapshot", py_snapshot, METH_VARARGS, "snapshot(id) -> the object's whole state"},
    {"snapshotOrder", py_snapshotOrder, METH_NOARGS,
     "snapshotOrder() -> every id, the referenced before the referrer"},
    {"mirrorToolBars", py_mirrorToolBars, METH_VARARGS,
     "mirrorToolBars(on=True) -> bool: start or stop the tool bar mirror"},
    {"mirrorFlush", py_mirrorFlush, METH_NOARGS,
     "mirrorFlush() -> flush the mirror's coalesced state; the rebuild count"},
    {"control", py_control, METH_VARARGS,
     "control(json, client=1, viewOnly=False) -> the reply: a scene control request,"
     " the widget stream's pushes collected for pushed() (a client above 1000 is gone)"},
    {"pushed", py_pushed, METH_NOARGS, "pushed() -> [{client, json}] since the last call"},
    {"ids", py_ids, METH_NOARGS, "ids() -> the comm ids of the store's objects"},
    {"count", py_count, METH_NOARGS, "count() -> how many objects the store holds"},
    {"reset", py_reset, METH_NOARGS, "reset() -> drop every object"},
    {"stats", py_stats, METH_NOARGS, "stats() -> the store's counters"},
    {"resetStats", py_resetStats, METH_NOARGS, "resetStats()"},
    {"resolve", py_resolve, METH_VARARGS, "resolve(ref) -> id or None ('IPY_MODEL_<id>' or id)"},
    {"info", py_info, METH_VARARGS,
     "info(id) -> dict: class, qtClass, objectName, properties, touched, bound, ..."},
    {"find", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(py_find)),
     METH_VARARGS | METH_KEYWORDS, "find(className=None, objectName=None) -> [id]"},
    {"widget", py_widget, METH_VARARGS, "widget(id) -> the realized Qt widget or None"},
    {"setProperty", py_setProperty, METH_VARARGS,
     "setProperty(id, name, value) -> bool: write the bag as native code would"},
    {"knownClasses", py_knownClasses, METH_NOARGS, "knownClasses() -> [Qt class name]"},
    {"accept", py_accept, METH_NOARGS,
     "accept() -> bool: the active task dialog's OK; True closed the dialog"},
    {"reject", py_reject, METH_NOARGS,
     "reject() -> bool: the active task dialog's Cancel; True closed the dialog"},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef ModuleDef = {PyModuleDef_HEAD_INIT,
                         "FormWidgets",
                         "The host widget layer's store (docs/Sandbox.md 7.12)",
                         -1,
                         Methods,
                         nullptr,
                         nullptr,
                         nullptr,
                         nullptr};

}  // namespace

void Gui::Fw::addPyModule(PyObject* parent)
{
    PyObject* module = PyModule_Create(&ModuleDef);
    if (!module)
        return;
    Py_INCREF(module);
    PyModule_AddObject(parent, "FormWidgets", module);
}
