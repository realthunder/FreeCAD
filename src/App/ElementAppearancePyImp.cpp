/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include <sstream>

#include <Base/Interpreter.h>

#include "AppearanceList.h"
#include "DocumentObject.h"
#include "MaterialListPy.h"
#include "MaterialPy.h"
#include "PropertyElementAppearance.h"

// inclusion of the generated files (generated out of ElementAppearancePy.xml)
#include "ElementAppearancePy.h"
#include "ElementAppearancePy.cpp"

using namespace App;

namespace
{

using Kind = PropertyElementAppearance::Kind;

/// A colour out of a Python 3- or 4-tuple, or a packed integer. False, and
/// nothing set, for anything else.
bool colorOf(PyObject *value, Color &color)
{
    if (PyLong_Check(value)) {
        color.setPackedValue(static_cast<uint32_t>(PyLong_AsUnsignedLong(value)));
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return false;
        }
        return true;
    }
    if (!PySequence_Check(value) || PyUnicode_Check(value)) {
        return false;
    }
    Py::Sequence seq(value);
    if (seq.size() != 3 && seq.size() != 4) {
        return false;
    }
    for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(seq.size()); ++i) {
        if (!PyNumber_Check(seq[i].ptr())) {
            return false;
        }
    }
    color.r = static_cast<float>(Py::Float(seq[0]));
    color.g = static_cast<float>(Py::Float(seq[1]));
    color.b = static_cast<float>(Py::Float(seq[2]));
    color.a = seq.size() == 4 ? static_cast<float>(Py::Float(seq[3])) : 1.0F;
    return true;
}

/** The element a key means
 *
 * A string is an element as the shape counts it, a mapped name, or a kind;
 * an integer is a face by its number, as a list of faces counts it.
 * @throw Base::TypeError, Base::IndexError, Base::ValueError
 */
void elementOf(const PropertyElementAppearance &prop, PyObject *key, Kind &kind, int &index)
{
    if (PyLong_Check(key)) {
        const long number = PyLong_AsLong(key);
        if (number < 0 || PyErr_Occurred()) {
            PyErr_Clear();
            throw Base::IndexError("a face is numbered from 0");
        }
        kind = PropertyElementAppearance::Face;
        index = static_cast<int>(number);
        return;
    }
    if (!PyUnicode_Check(key)) {
        throw Base::TypeError("an element is named by a string, or a face by its number");
    }
    const char *name = PyUnicode_AsUTF8(key);
    if (!name || !prop.resolveElement(name, kind, index)) {
        PyErr_Clear();
        std::ostringstream str;
        str << "'" << (name ? name : "") << "' names no element";
        throw Base::ValueError(str.str());
    }
}

/// One entry written: a Material is the element's whole look, a colour its
/// colour and no more
void writeItem(PropertyElementAppearance &prop, PyObject *key, PyObject *value)
{
    Kind kind = PropertyElementAppearance::KindCount;
    int index = -1;
    elementOf(prop, key, kind, index);
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        prop.setLook(kind, index, *static_cast<MaterialPy *>(value)->getMaterialAppearancePtr());
        return;
    }
    Color color;
    if (colorOf(value, color)) {
        prop.setColor(kind, index, color);
        return;
    }
    throw Base::TypeError("a look is a Material, or a colour: (r, g, b[, a]) or a packed integer");
}

PyObject *materialPy(const MaterialAppearance &look)
{
    return new MaterialPy(new MaterialAppearance(look));
}

}  // namespace

// ---------------------------------------------------------------------
// The view and the property it is of

int ElementAppearancePy::initialization()
{
    return 0;
}

int ElementAppearancePy::finalization()
{
    if (isValid() && getPropertyElementAppearancePtr()) {
        getPropertyElementAppearancePtr()->unregisterView(this);
    }
    setTwinPointer(nullptr);
    return 0;
}

PropertyElementAppearance *ElementAppearancePy::live() const
{
    auto self = const_cast<ElementAppearancePy *>(this);
    if (!self->isValid() || !getPropertyElementAppearancePtr()) {
        PyErr_SetString(PyExc_ReferenceError, "the element appearance is no longer valid");
        return nullptr;
    }
    return getPropertyElementAppearancePtr();
}

std::string ElementAppearancePy::representation() const
{
    std::ostringstream str;
    auto self = const_cast<ElementAppearancePy *>(this);
    if (!self->isValid() || !getPropertyElementAppearancePtr()) {
        str << "<ElementAppearance (invalid)>";
        return str.str();
    }
    auto prop = getPropertyElementAppearancePtr();
    const int stated = static_cast<int>(prop->getStatedLooks().size());
    str << "<ElementAppearance (" << prop->getNamedCount() << " named, "
        << stated - prop->getNamedCount() << " numbered)>";
    return str.str();
}

void ElementAppearancePy::update(PropertyElementAppearance &prop, PyObject *dict)
{
    if (!PyDict_Check(dict)) {
        throw Base::TypeError("expected a dict of element to look");
    }
    PropertyElementAppearance::Edit edit(prop);
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t at = 0;
    while (PyDict_Next(dict, &at, &key, &value)) {
        writeItem(prop, key, value);
    }
}

// ---------------------------------------------------------------------
// Methods

PyObject *ElementAppearancePy::keys(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Py::List list;
        for (const auto &v : prop->getStatedLooks()) {
            list.append(Py::String(v.first));
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::items(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Py::List list;
        for (const auto &v : prop->getStatedLooks()) {
            list.append(Py::TupleN(Py::String(v.first), Py::asObject(materialPy(v.second))));
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::update(PyObject *args)
{
    PyObject *dict = nullptr;
    if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &dict)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        update(*prop, dict);
        Py_Return;
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::setLook(PyObject *args)
{
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    PyObject *own = Py_None;
    if (!PyArg_ParseTuple(args, "OO!|O", &key, &(MaterialPy::Type), &value, &own)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        uint16_t bits = PropertyElementAppearance::OwnAll;
        if (own != Py_None) {
            std::vector<std::string> names;
            if (PyUnicode_Check(own)) {
                names.emplace_back(PyUnicode_AsUTF8(own));
            }
            else {
                Py::Sequence seq(own);
                for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(seq.size()); ++i) {
                    names.push_back(Py::String(seq[i]).as_std_string("utf-8"));
                }
            }
            bits = PropertyElementAppearance::ownFromNames(names);
        }
        prop->setLook(kind, index, *static_cast<MaterialPy *>(value)->getMaterialAppearancePtr(),
                      bits);
        Py_Return;
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::setColor(PyObject *args)
{
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "OO", &key, &value)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        Color color;
        if (!colorOf(value, color)) {
            throw Base::TypeError("expected a colour: (r, g, b[, a]) or a packed integer");
        }
        prop->setColor(kind, index, color);
        Py_Return;
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::own(PyObject *args)
{
    PyObject *key = nullptr;
    if (!PyArg_ParseTuple(args, "O", &key)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        const auto names = PropertyElementAppearance::ownNames(prop->getOwn(kind, index));
        Py::Tuple tuple(static_cast<int>(names.size()));
        for (std::size_t i = 0; i < names.size(); ++i) {
            tuple.setItem(static_cast<int>(i), Py::String(names[i]));
        }
        return Py::new_reference_to(tuple);
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::isNamed(PyObject *args)
{
    PyObject *key = nullptr;
    if (!PyArg_ParseTuple(args, "O", &key)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        const bool named = index >= 0
            && prop->findNamed(PropertyElementAppearance::elementName(kind, index).c_str()) >= 0;
        return Py::new_reference_to(Py::Boolean(named));
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::remove(PyObject *args)
{
    PyObject *key = nullptr;
    if (!PyArg_ParseTuple(args, "O", &key)) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        if (PyUnicode_Check(key)) {
            return Py::new_reference_to(Py::Boolean(prop->removeLook(PyUnicode_AsUTF8(key))));
        }
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        return Py::new_reference_to(Py::Boolean(prop->removeLook(kind, index)));
    }
    PY_CATCH
}

PyObject *ElementAppearancePy::clear(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    auto prop = live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        prop->clear();
        Py_Return;
    }
    PY_CATCH
}

// ---------------------------------------------------------------------
// Attributes

Py::Object ElementAppearancePy::getFace() const
{
    return Py::asObject(
        materialPy(getPropertyElementAppearancePtr()->getBase(PropertyElementAppearance::Face)));
}

void ElementAppearancePy::setFace(Py::Object value)
{
    if (!PyObject_TypeCheck(value.ptr(), &(MaterialPy::Type))) {
        throw Py::TypeError("expected a Material");
    }
    getPropertyElementAppearancePtr()->setBase(
        PropertyElementAppearance::Face,
        *static_cast<MaterialPy *>(value.ptr())->getMaterialAppearancePtr());
}

Py::Object ElementAppearancePy::getEdge() const
{
    return Py::asObject(
        materialPy(getPropertyElementAppearancePtr()->getBase(PropertyElementAppearance::Edge)));
}

void ElementAppearancePy::setEdge(Py::Object value)
{
    if (!PyObject_TypeCheck(value.ptr(), &(MaterialPy::Type))) {
        throw Py::TypeError("expected a Material");
    }
    getPropertyElementAppearancePtr()->setBase(
        PropertyElementAppearance::Edge,
        *static_cast<MaterialPy *>(value.ptr())->getMaterialAppearancePtr());
}

Py::Object ElementAppearancePy::getVertex() const
{
    return Py::asObject(
        materialPy(getPropertyElementAppearancePtr()->getBase(PropertyElementAppearance::Vertex)));
}

void ElementAppearancePy::setVertex(Py::Object value)
{
    if (!PyObject_TypeCheck(value.ptr(), &(MaterialPy::Type))) {
        throw Py::TypeError("expected a Material");
    }
    getPropertyElementAppearancePtr()->setBase(
        PropertyElementAppearance::Vertex,
        *static_cast<MaterialPy *>(value.ptr())->getMaterialAppearancePtr());
}

Py::Tuple ElementAppearancePy::getNames() const
{
    auto prop = getPropertyElementAppearancePtr();
    const auto &shadows = prop->getShadowSubs();
    const auto &subs = prop->getSubValues();
    Py::Tuple tuple(static_cast<int>(subs.size()));
    for (std::size_t i = 0; i < subs.size(); ++i) {
        const std::string &name = i < shadows.size() && !shadows[i].second.empty()
            ? shadows[i].second
            : subs[i];
        tuple.setItem(static_cast<int>(i), Py::String(name));
    }
    return tuple;
}

PyObject *ElementAppearancePy::listOf(int kind) const
{
    // A value of its own that shares the storage: the first write to it
    // pays for a copy and reaches nothing here
    PyObject *made = PyObject_CallObject(reinterpret_cast<PyObject *>(&MaterialListPy::Type),
                                         nullptr);
    if (!made) {
        throw Py::Exception();
    }
    *static_cast<MaterialListPy *>(made)->getAppearanceListPtr()
        = getPropertyElementAppearancePtr()->getDrawn(static_cast<Kind>(kind));
    static_cast<MaterialListPy *>(made)->getAppearanceListPtr()->setBlobManager(nullptr);
    return made;
}

Py::Object ElementAppearancePy::getFaces() const
{
    return Py::asObject(listOf(PropertyElementAppearance::Face));
}

Py::Object ElementAppearancePy::getEdges() const
{
    return Py::asObject(listOf(PropertyElementAppearance::Edge));
}

Py::Object ElementAppearancePy::getVertices() const
{
    return Py::asObject(listOf(PropertyElementAppearance::Vertex));
}

Py::Object ElementAppearancePy::getObject() const
{
    auto object = Base::freecad_dynamic_cast<DocumentObject>(
        getPropertyElementAppearancePtr()->getContainer());
    if (!object) {
        return Py::None();
    }
    return Py::asObject(object->getPyObject());
}

PyObject *ElementAppearancePy::getCustomAttributes(const char * /*attr*/) const
{
    return nullptr;
}

int ElementAppearancePy::setCustomAttributes(const char * /*attr*/, PyObject * /*obj*/)
{
    return 0;
}

// ---------------------------------------------------------------------
// Indexing

Py_ssize_t ElementAppearancePy::sequence_length(PyObject *self)
{
    auto prop = static_cast<ElementAppearancePy *>(self)->live();
    if (!prop) {
        return -1;
    }
    Py_ssize_t count = 0;
    PY_TRY
    {
        count = static_cast<Py_ssize_t>(prop->getStatedLooks().size());
    }
    _PY_CATCH(return -1)
    return count;
}

int ElementAppearancePy::sequence_contains(PyObject *self, PyObject *key)
{
    auto prop = static_cast<ElementAppearancePy *>(self)->live();
    if (!prop) {
        return -1;
    }
    try {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        elementOf(*prop, key, kind, index);
        return index < 0 ? (prop->hasBase(kind) ? 1 : 0) : (prop->isStated(kind, index) ? 1 : 0);
    }
    catch (Base::Exception &) {
        // What names no element is not among them
        return 0;
    }
}

PyObject *ElementAppearancePy::mapping_subscript(PyObject *self, PyObject *key)
{
    auto prop = static_cast<ElementAppearancePy *>(self)->live();
    if (!prop) {
        return nullptr;
    }
    PY_TRY
    {
        Kind kind = PropertyElementAppearance::KindCount;
        int index = -1;
        try {
            elementOf(*prop, key, kind, index);
        }
        catch (Base::ValueError &e) {
            PyErr_SetString(PyExc_KeyError, e.what());
            return nullptr;
        }
        return materialPy(prop->getLook(kind, index));
    }
    PY_CATCH
}

int ElementAppearancePy::mapping_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    auto prop = static_cast<ElementAppearancePy *>(self)->live();
    if (!prop) {
        return -1;
    }
    PY_TRY
    {
        if (value) {
            writeItem(*prop, key, value);
            return 0;
        }
        bool removed = false;
        if (PyUnicode_Check(key)) {
            removed = prop->removeLook(PyUnicode_AsUTF8(key));
        }
        else {
            Kind kind = PropertyElementAppearance::KindCount;
            int index = -1;
            elementOf(*prop, key, kind, index);
            removed = prop->removeLook(kind, index);
        }
        if (!removed) {
            PyErr_SetObject(PyExc_KeyError, key);
            return -1;
        }
        return 0;
    }
    _PY_CATCH(return -1)
}
