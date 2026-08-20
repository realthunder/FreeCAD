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

#include "MaterialList.h"
#include "MaterialPy.h"
#include "PropertyStandard.h"

// inclusion of the generated files (generated out of MaterialListPy.xml)
#include "MaterialListPy.h"
#include "MaterialListPy.cpp"

using namespace App;

namespace
{

/// One material out of a Python object, or null with the error set
const Material *materialOf(PyObject *value)
{
    if (value && PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        return static_cast<MaterialPy *>(value)->getMaterialPtr();
    }
    PyErr_Format(PyExc_TypeError,
                 "expected a Material, not %s",
                 value ? Py_TYPE(value)->tp_name : "nothing");
    return nullptr;
}

/// Every material of a Python sequence, in order. Answers false with the
/// error set when anything in it is not a material.
bool materialsOf(PyObject *sequence, std::vector<Material> &values)
{
    Py::Object obj(sequence);
    if (!obj.isSequence() && !PyIter_Check(sequence)) {
        PyErr_SetString(PyExc_TypeError, "expected a sequence of Material");
        return false;
    }
    Py::Sequence seq(obj);
    values.reserve(values.size() + seq.size());
    for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(seq.size()); ++i) {
        const Material *mat = materialOf(seq[i].ptr());
        if (!mat) {
            return false;
        }
        values.push_back(*mat);
    }
    return true;
}

/// A Python index against a list of \a count entries, negatives counted
/// from the end. Answers false with IndexError set when it is outside.
bool indexOf(Py_ssize_t given, int count, int &idx, bool allowEnd = false)
{
    Py_ssize_t value = given < 0 ? given + count : given;
    if (value < 0 || value > count || (value == count && !allowEnd)) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
        return false;
    }
    idx = static_cast<int>(value);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------
// Attached and detached
//
// The twin pointer is the OWNER's value while attached and this object's
// own once detached, so every generated accessor reads the live one either
// way. Writes never go through it: they go through edit(), which routes an
// attached write into the property's own change signalling so it records an
// undo step and redraws.

void MaterialListPy::attach(PropertyMaterialList *prop)
{
    if (owner == prop) {
        return;
    }
    delete ownvalue;
    ownvalue = nullptr;
    owner = prop;
    setTwinPointer(&prop->_list);
    prop->registerView(this);
}

void MaterialListPy::detachFromOwner()
{
    if (!owner) {
        return;
    }
    // The value as it stands, which costs a pointer -- and a manager of its
    // own, because the document's store belongs to the document and this
    // list is about to outlive its say in that
    ownvalue = new MaterialList(owner->getList());
    ownvalue->setBlobManager(nullptr);
    PropertyMaterialList *prop = owner;
    owner = nullptr;
    setTwinPointer(ownvalue);
    prop->unregisterView(this);
}

const MaterialList &MaterialListPy::list() const
{
    return owner ? owner->getList() : *getMaterialListPtr();
}

void MaterialListPy::edit(const std::function<void(MaterialList &)> &op, int touched)
{
    if (owner) {
        owner->editList(op, touched);
    }
    else {
        op(*getMaterialListPtr());
    }
}

bool MaterialListPy::writable()
{
    if (!isValid()) {
        PyErr_SetString(PyExc_ReferenceError, "the material list is no longer valid");
        return false;
    }
    if (isConst()) {
        PyErr_SetString(PyExc_ReferenceError, "this material list is read-only");
        return false;
    }
    return true;
}

int MaterialListPy::initialization()
{
    return 0;
}

int MaterialListPy::finalization()
{
    if (owner) {
        owner->unregisterView(this);
        owner = nullptr;
    }
    delete ownvalue;
    ownvalue = nullptr;
    setTwinPointer(nullptr);
    return 0;
}

// ---------------------------------------------------------------------
// Construction

PyObject *MaterialListPy::PyMake(PyTypeObject * /*type*/, PyObject * /*args*/, PyObject * /*kwds*/)
{
    auto *value = new MaterialList;
    auto *self = new MaterialListPy(value);
    self->ownvalue = value;
    return self;
}

int MaterialListPy::PyInit(PyObject *args, PyObject * /*kwds*/)
{
    PyObject *source = nullptr;
    if (!PyArg_ParseTuple(args, "|O", &source)) {
        return -1;
    }
    if (!source) {
        return 0;
    }
    PY_TRY
    {
        if (PyObject_TypeCheck(source, &(MaterialListPy::Type))) {
            // Shares the storage; the first write to either side pays
            *getMaterialListPtr() = static_cast<MaterialListPy *>(source)->list();
            return 0;
        }
        if (PyLong_Check(source)) {
            const long count = PyLong_AsLong(source);
            if (count < 0) {
                PyErr_SetString(PyExc_ValueError, "negative list size");
                return -1;
            }
            getMaterialListPtr()->setSize(static_cast<int>(count));
            return 0;
        }
        std::vector<Material> values;
        if (!materialsOf(source, values)) {
            return -1;
        }
        getMaterialListPtr()->setValues(values);
        return 0;
    }
    _PY_CATCH(return -1)
}

std::string MaterialListPy::representation() const
{
    std::ostringstream str;
    str << "<MaterialList (" << list().getSize() << " entries"
        << (owner ? ", attached" : "") << ")>";
    return str.str();
}

// ---------------------------------------------------------------------
// Methods

PyObject *MaterialListPy::append(PyObject *args)
{
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "O", &value)) {
        return nullptr;
    }
    const Material *mat = materialOf(value);
    if (!mat || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const Material added = *mat;
        edit([&](MaterialList &values) { values.set1Value(values.getSize(), added); });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::extend(PyObject *args)
{
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "O", &value)) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        std::vector<Material> added;
        if (!materialsOf(value, added)) {
            return nullptr;
        }
        edit([&](MaterialList &values) {
            for (const auto &mat : added) {
                values.set1Value(values.getSize(), mat);
            }
        });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::insert(PyObject *args)
{
    Py_ssize_t where = 0;
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "nO", &where, &value)) {
        return nullptr;
    }
    const Material *mat = materialOf(value);
    if (!mat || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const int count = list().getSize();
        int idx = 0;
        if (!indexOf(where, count, idx, true)) {
            return nullptr;
        }
        const Material added = *mat;
        edit([&](MaterialList &values) {
            // Shift by one from the back, which is the only spelling the
            // per field storage has for an insertion
            values.set1Value(values.getSize(), values.getMaterial(values.getSize() - 1));
            for (int i = values.getSize() - 2; i > idx; --i) {
                values.set1Value(i, values.getMaterial(i - 1));
            }
            values.set1Value(idx, added);
        });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::copy(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    PY_TRY
    {
        auto *value = new MaterialList(list());
        auto *result = new MaterialListPy(value);
        result->ownvalue = value;
        return result;
    }
    PY_CATCH
}

PyObject *MaterialListPy::detach(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    PY_TRY
    {
        detachFromOwner();
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::setSize(PyObject *args)
{
    Py_ssize_t count = 0;
    PyObject *fill = nullptr;
    if (!PyArg_ParseTuple(args, "n|O", &count, &fill)) {
        return nullptr;
    }
    if (count < 0) {
        PyErr_SetString(PyExc_ValueError, "negative list size");
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        if (!fill) {
            edit([&](MaterialList &values) { values.setSize(static_cast<int>(count)); });
            Py_Return;
        }
        const Material *mat = materialOf(fill);
        if (!mat) {
            return nullptr;
        }
        const Material def = *mat;
        edit([&](MaterialList &values) { values.setSize(static_cast<int>(count), def); });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::getMaterial(PyObject *args)
{
    Py_ssize_t where = 0;
    if (!PyArg_ParseTuple(args, "n", &where)) {
        return nullptr;
    }
    PY_TRY
    {
        int idx = 0;
        if (!indexOf(where, list().getSize(), idx)) {
            return nullptr;
        }
        return new MaterialPy(new Material(list().getMaterial(idx)));
    }
    PY_CATCH
}

PyObject *MaterialListPy::setMaterial(PyObject *args)
{
    Py_ssize_t where = 0;
    PyObject *value = nullptr;
    if (!PyArg_ParseTuple(args, "nO", &where, &value)) {
        return nullptr;
    }
    const Material *mat = materialOf(value);
    if (!mat || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const Material written = *mat;
        if (where == -1) {
            // Every entry, which is what -1 means to every setter here --
            // not "the last one", which would make the per field API read
            // differently from the indexing beside it
            edit([&](MaterialList &values) { values.setValue(written); });
            Py_Return;
        }
        int idx = 0;
        if (!indexOf(where, list().getSize(), idx, true)) {
            return nullptr;
        }
        edit([&](MaterialList &values) { values.set1Value(idx, written); }, idx);
        Py_Return;
    }
    PY_CATCH
}

// ---------------------------------------------------------------------
// Attributes

Py::Int MaterialListPy::getCount() const
{
    return Py::Int(list().getSize());
}

Py::Boolean MaterialListPy::getIsAttached() const
{
    return {owner != nullptr};
}

Py::Boolean MaterialListPy::getPBR() const
{
    return {list().isPBR()};
}

void MaterialListPy::setPBR(Py::Boolean value)
{
    const bool enable = static_cast<bool>(value);
    edit([&](MaterialList &values) { values.setPBR(enable); });
}

PyObject *MaterialListPy::getCustomAttributes(const char * /*attr*/) const
{
    return nullptr;
}

int MaterialListPy::setCustomAttributes(const char * /*attr*/, PyObject * /*obj*/)
{
    return 0;
}

// ---------------------------------------------------------------------
// The sequence protocol
//
// WARNING: the generator installs these RAW into the slot tables: no isValid, no
// isConst, no exception translation and no notification, unlike the method
// and attribute wrappers it writes. Everything they need, they do here.

Py_ssize_t MaterialListPy::sequence_length(PyObject *self)
{
    return static_cast<MaterialListPy *>(self)->list().getSize();
}

PyObject *MaterialListPy::sequence_item(PyObject *self, Py_ssize_t index)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (!list->isValid()) {
        PyErr_SetString(PyExc_ReferenceError, "the material list is no longer valid");
        return nullptr;
    }
    int idx = 0;
    if (!indexOf(index, list->list().getSize(), idx)) {
        return nullptr;
    }
    PY_TRY
    {
        auto *item = new MaterialPy(new Material(list->list().getMaterial(idx)));
        // A copy with a link back: writing a field on it writes the whole
        // entry to this index, which is what makes
        // vp.ShapeAppearance[0].DiffuseColor = c reach the object
        list->trackReturnedItem(item, idx);
        return item;
    }
    PY_CATCH
}

int MaterialListPy::sequence_ass_item(PyObject *self, Py_ssize_t index, PyObject *value)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (!list->writable()) {
        return -1;
    }
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete an entry of a material list");
        return -1;
    }
    const Material *mat = materialOf(value);
    if (!mat) {
        return -1;
    }
    int idx = 0;
    if (!indexOf(index, list->list().getSize(), idx, true)) {
        return -1;
    }
    PY_TRY
    {
        const Material written = *mat;
        list->edit([&](MaterialList &values) { values.set1Value(idx, written); }, idx);
        return 0;
    }
    _PY_CATCH(return -1)
}

PyObject *MaterialListPy::mapping_subscript(PyObject *self, PyObject *item)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (PyIndex_Check(item)) {
        const Py_ssize_t index = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (index == -1 && PyErr_Occurred()) {
            return nullptr;
        }
        return sequence_item(self, index);
    }
    if (PySlice_Check(item)) {
        PY_TRY
        {
            Py_ssize_t start = 0;
            Py_ssize_t stop = 0;
            Py_ssize_t step = 0;
            Py_ssize_t count = 0;
            if (PySlice_GetIndicesEx(item, list->list().getSize(), &start, &stop, &step, &count)
                < 0) {
                return nullptr;
            }
            // A slice of a list is a LIST, detached: it is a new value and
            // writing to it must not reach whatever this one is a view of
            std::vector<Material> values;
            values.reserve(count);
            for (Py_ssize_t i = 0, at = start; i < count; ++i, at += step) {
                values.push_back(list->list().getMaterial(static_cast<int>(at)));
            }
            auto *sliced = new MaterialList;
            sliced->setValues(values);
            auto *result = new MaterialListPy(sliced);
            result->ownvalue = sliced;
            return result;
        }
        PY_CATCH
    }
    PyErr_Format(PyExc_TypeError,
                 "material list indices must be integers or slices, not %s",
                 Py_TYPE(item)->tp_name);
    return nullptr;
}

int MaterialListPy::mapping_ass_subscript(PyObject *self, PyObject *item, PyObject *value)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (PyIndex_Check(item)) {
        const Py_ssize_t index = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (index == -1 && PyErr_Occurred()) {
            return -1;
        }
        return sequence_ass_item(self, index, value);
    }
    if (!PySlice_Check(item)) {
        PyErr_Format(PyExc_TypeError,
                     "material list indices must be integers or slices, not %s",
                     Py_TYPE(item)->tp_name);
        return -1;
    }
    if (!list->writable()) {
        return -1;
    }
    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete entries of a material list");
        return -1;
    }
    PY_TRY
    {
        Py_ssize_t start = 0;
        Py_ssize_t stop = 0;
        Py_ssize_t step = 0;
        Py_ssize_t count = 0;
        if (PySlice_GetIndicesEx(item, list->list().getSize(), &start, &stop, &step, &count) < 0) {
            return -1;
        }
        std::vector<Material> written;
        if (!materialsOf(value, written)) {
            return -1;
        }
        if (static_cast<Py_ssize_t>(written.size()) != count) {
            PyErr_SetString(PyExc_ValueError,
                            "a material list slice takes exactly as many entries as it holds");
            return -1;
        }
        list->edit([&](MaterialList &values) {
            for (Py_ssize_t i = 0, at = start; i < count; ++i, at += step) {
                values.set1Value(static_cast<int>(at), written[i]);
            }
        });
        return 0;
    }
    _PY_CATCH(return -1)
}

int MaterialListPy::sequence_contains(PyObject *self, PyObject *value)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (!value || !PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        return 0;
    }
    PY_TRY
    {
        const Material &mat = *static_cast<MaterialPy *>(value)->getMaterialPtr();
        const int count = list->list().getSize();
        for (int i = 0; i < count; ++i) {
            if (list->list().getMaterial(i) == mat) {
                return 1;
            }
        }
        return 0;
    }
    _PY_CATCH(return -1)
}

PyObject *MaterialListPy::sequence_concat(PyObject *self, PyObject *other)
{
    PY_TRY
    {
        std::vector<Material> values;
        auto *list = static_cast<MaterialListPy *>(self);
        const int count = list->list().getSize();
        for (int i = 0; i < count; ++i) {
            values.push_back(list->list().getMaterial(i));
        }
        if (!materialsOf(other, values)) {
            return nullptr;
        }
        auto *joined = new MaterialList;
        joined->setValues(values);
        auto *result = new MaterialListPy(joined);
        result->ownvalue = joined;
        return result;
    }
    PY_CATCH
}

PyObject *MaterialListPy::sequence_repeat(PyObject *self, Py_ssize_t times)
{
    PY_TRY
    {
        auto *list = static_cast<MaterialListPy *>(self);
        const int count = list->list().getSize();
        std::vector<Material> values;
        for (Py_ssize_t n = 0; n < times; ++n) {
            for (int i = 0; i < count; ++i) {
                values.push_back(list->list().getMaterial(i));
            }
        }
        auto *repeated = new MaterialList;
        repeated->setValues(values);
        auto *result = new MaterialListPy(repeated);
        result->ownvalue = repeated;
        return result;
    }
    PY_CATCH
}

PyObject *MaterialListPy::sequence_inplace_concat(PyObject *self, PyObject *other)
{
    auto *list = static_cast<MaterialListPy *>(self);
    if (!list->writable()) {
        return nullptr;
    }
    PY_TRY
    {
        std::vector<Material> added;
        if (!materialsOf(other, added)) {
            return nullptr;
        }
        list->edit([&](MaterialList &values) {
            for (const auto &mat : added) {
                values.set1Value(values.getSize(), mat);
            }
        });
        Py_INCREF(self);
        return self;
    }
    PY_CATCH
}

// ---------------------------------------------------------------------
// Comparison

PyObject *MaterialListPy::richCompare(PyObject *v, PyObject *w, int op)
{
    if (op != Py_EQ && op != Py_NE) {
        PyErr_SetString(PyExc_TypeError, "material lists have no ordering");
        return nullptr;
    }
    if (!PyObject_TypeCheck(v, &(MaterialListPy::Type))) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    const MaterialList &left = static_cast<MaterialListPy *>(v)->list();
    bool equal = false;
    if (PyObject_TypeCheck(w, &(MaterialListPy::Type))) {
        equal = left.isSame(static_cast<MaterialListPy *>(w)->list());
    }
    else if (PySequence_Check(w)) {
        // Against a plain sequence of materials, so a script that used to
        // compare with the tuple this property handed out still can
        std::vector<Material> other;
        if (!materialsOf(w, other)) {
            PyErr_Clear();
            Py_RETURN_NOTIMPLEMENTED;
        }
        equal = static_cast<int>(other.size()) == left.getSize();
        for (std::size_t i = 0; equal && i < other.size(); ++i) {
            equal = left.getMaterial(static_cast<int>(i)) == other[i];
        }
    }
    else {
        Py_RETURN_NOTIMPLEMENTED;
    }
    if (op == Py_NE) {
        equal = !equal;
    }
    return Py::new_reference_to(Py::Boolean(equal));
}
