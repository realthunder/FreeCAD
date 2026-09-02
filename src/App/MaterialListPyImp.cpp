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

#include <array>
#include <sstream>

#include <Base/Interpreter.h>

#include "AppearanceList.h"
#include "MaterialPy.h"
#include "PropertyStandard.h"

// inclusion of the generated files (generated out of MaterialListPy.xml)
#include "MaterialListPy.h"
#include "MaterialListPy.cpp"

using namespace App;

namespace
{

/// One material out of a Python object, or null with the error set
const MaterialAppearance *materialOf(PyObject *value)
{
    if (value && PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        return static_cast<MaterialPy *>(value)->getMaterialAppearancePtr();
    }
    PyErr_Format(PyExc_TypeError,
                 "expected a Material, not %s",
                 value ? Py_TYPE(value)->tp_name : "nothing");
    return nullptr;
}

/// Every material of a Python sequence, in order. Answers false with the
/// error set when anything in it is not a material.
bool materialsOf(PyObject *sequence, std::vector<MaterialAppearance> &values)
{
    Py::Object obj(sequence);
    if (!obj.isSequence() && !PyIter_Check(sequence)) {
        PyErr_SetString(PyExc_TypeError, "expected a sequence of Material");
        return false;
    }
    Py::Sequence seq(obj);
    values.reserve(values.size() + seq.size());
    for (Py_ssize_t i = 0; i < static_cast<Py_ssize_t>(seq.size()); ++i) {
        const MaterialAppearance *mat = materialOf(seq[i].ptr());
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

void MaterialListPy::attach(PropertyAppearanceList *prop)
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
    ownvalue = new AppearanceList(owner->getList());
    ownvalue->setBlobManager(nullptr);
    PropertyAppearanceList *prop = owner;
    owner = nullptr;
    setTwinPointer(ownvalue);
    prop->unregisterView(this);
}

const AppearanceList &MaterialListPy::list() const
{
    return owner ? owner->getList() : *getAppearanceListPtr();
}

void MaterialListPy::edit(const std::function<void(AppearanceList &)> &op, int touched)
{
    if (owner) {
        owner->editList(op, touched);
    }
    else {
        op(*getAppearanceListPtr());
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
    auto *value = new AppearanceList;
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
            *getAppearanceListPtr() = static_cast<MaterialListPy *>(source)->list();
            return 0;
        }
        if (PyLong_Check(source)) {
            const long count = PyLong_AsLong(source);
            if (count < 0) {
                PyErr_SetString(PyExc_ValueError, "negative list size");
                return -1;
            }
            getAppearanceListPtr()->setSize(static_cast<int>(count));
            return 0;
        }
        std::vector<MaterialAppearance> values;
        if (!materialsOf(source, values)) {
            return -1;
        }
        getAppearanceListPtr()->setValues(values);
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
    const MaterialAppearance *mat = materialOf(value);
    if (!mat || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const MaterialAppearance added = *mat;
        edit([&](AppearanceList &values) { values.set1Value(values.getSize(), added); });
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
        std::vector<MaterialAppearance> added;
        if (!materialsOf(value, added)) {
            return nullptr;
        }
        edit([&](AppearanceList &values) {
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
    const MaterialAppearance *mat = materialOf(value);
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
        const MaterialAppearance added = *mat;
        edit([&](AppearanceList &values) {
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
        auto *value = new AppearanceList(list());
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
            edit([&](AppearanceList &values) { values.setSize(static_cast<int>(count)); });
            Py_Return;
        }
        const MaterialAppearance *mat = materialOf(fill);
        if (!mat) {
            return nullptr;
        }
        const MaterialAppearance def = *mat;
        edit([&](AppearanceList &values) { values.setSize(static_cast<int>(count), def); });
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
        return new MaterialPy(new MaterialAppearance(list().getMaterial(idx)));
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
    const MaterialAppearance *mat = materialOf(value);
    if (!mat || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const MaterialAppearance written = *mat;
        if (where == -1) {
            // Every entry, which is what -1 means to every setter here --
            // not "the last one", which would make the per field API read
            // differently from the indexing beside it
            edit([&](AppearanceList &values) { values.setValue(written); });
            Py_Return;
        }
        int idx = 0;
        if (!indexOf(where, list().getSize(), idx, true)) {
            return nullptr;
        }
        edit([&](AppearanceList &values) { values.set1Value(idx, written); }, idx);
        Py_Return;
    }
    PY_CATCH
}


// ---------------------------------------------------------------------
// Per field access
//
// Every setter takes the entry index or leaves it out, and leaving it out
// means EVERY entry -- rather than the -1 an index could be mistaken for.
// The getters index the way Python does, negatives from the end.

namespace
{

/// A colour out of a Python 3- or 4-tuple, or a packed integer
bool colorOf(PyObject *value, Color &color)
{
    if (PyLong_Check(value)) {
        color.setPackedValue(static_cast<uint32_t>(PyLong_AsUnsignedLong(value)));
        return !PyErr_Occurred();
    }
    Py::Object obj(value);
    if (obj.isSequence()) {
        Py::Sequence seq(obj);
        if (seq.size() == 3 || seq.size() == 4) {
            color.r = static_cast<float>(Py::Float(seq[0]));
            color.g = static_cast<float>(Py::Float(seq[1]));
            color.b = static_cast<float>(Py::Float(seq[2]));
            color.a = seq.size() == 4 ? static_cast<float>(Py::Float(seq[3])) : 1.0F;
            return true;
        }
    }
    PyErr_SetString(PyExc_TypeError, "expected a colour: (r, g, b[, a]) or a packed integer");
    return false;
}

Py::Tuple colorTuple(const Color &color)
{
    Py::Tuple tuple(4);
    tuple.setItem(0, Py::Float(color.r));
    tuple.setItem(1, Py::Float(color.g));
    tuple.setItem(2, Py::Float(color.b));
    tuple.setItem(3, Py::Float(color.a));
    return tuple;
}

/// The slot a name means, or SlotCount with a ValueError set
uint8_t slotOf(const char *name)
{
    const uint8_t slot = SurfaceTexture::slotFromName(name);
    if (slot >= SurfaceTexture::SlotCount) {
        std::string known;
        for (uint8_t i = 0; i < SurfaceTexture::SlotCount; ++i) {
            known += i ? ", " : "";
            known += SurfaceTexture::slotName(i);
        }
        PyErr_Format(PyExc_ValueError, "no texture slot called '%s'; there is %s",
                     name, known.c_str());
    }
    return slot;
}

}  // namespace

PyObject *MaterialListPy::colorGet(PyObject *args, ColorGetter getter)
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
        return Py::new_reference_to(colorTuple((list().*getter)(idx)));
    }
    PY_CATCH
}

PyObject *MaterialListPy::colorSet(PyObject *args, ColorSetter setter,
                                   ColorAllSetter allsetter)
{
    Py_ssize_t where = 0;
    PyObject *value = nullptr;
    Color color;
    if (PyArg_ParseTuple(args, "nO", &where, &value)) {
        if (!colorOf(value, color) || !writable()) {
            return nullptr;
        }
        PY_TRY
        {
            int idx = 0;
            if (!indexOf(where, list().getSize(), idx, true)) {
                return nullptr;
            }
            edit([&](AppearanceList &values) { (values.*setter)(idx, color); }, idx);
            Py_Return;
        }
        PY_CATCH
    }
    PyErr_Clear();
    if (!PyArg_ParseTuple(args, "O", &value)) {
        return nullptr;
    }
    if (!colorOf(value, color) || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        edit([&](AppearanceList &values) { (values.*allsetter)(color); });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::floatGet(PyObject *args, FloatGetter getter)
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
        return Py::new_reference_to(Py::Float((list().*getter)(idx)));
    }
    PY_CATCH
}

PyObject *MaterialListPy::floatSet(PyObject *args, FloatSetter setter,
                                   FloatAllSetter allsetter)
{
    Py_ssize_t where = 0;
    double value = 0.0;
    if (PyArg_ParseTuple(args, "nd", &where, &value)) {
        if (!writable()) {
            return nullptr;
        }
        PY_TRY
        {
            int idx = 0;
            if (!indexOf(where, list().getSize(), idx, true)) {
                return nullptr;
            }
            edit([&](AppearanceList &values) { (values.*setter)(idx, static_cast<float>(value)); },
                 idx);
            Py_Return;
        }
        PY_CATCH
    }
    PyErr_Clear();
    if (!PyArg_ParseTuple(args, "d", &value)) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        edit([&](AppearanceList &values) { (values.*allsetter)(static_cast<float>(value)); });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::getDiffuseColor(PyObject *args)
{
    return colorGet(args, &AppearanceList::getDiffuseColor);
}

PyObject *MaterialListPy::setDiffuseColor(PyObject *args)
{
    return colorSet(args,
                    static_cast<ColorSetter>(&AppearanceList::setDiffuseColor),
                    static_cast<ColorAllSetter>(&AppearanceList::setDiffuseColor));
}

PyObject *MaterialListPy::getAmbientColor(PyObject *args)
{
    return colorGet(args, &AppearanceList::getAmbientColor);
}

PyObject *MaterialListPy::setAmbientColor(PyObject *args)
{
    return colorSet(args,
                    static_cast<ColorSetter>(&AppearanceList::setAmbientColor),
                    static_cast<ColorAllSetter>(&AppearanceList::setAmbientColor));
}

PyObject *MaterialListPy::getSpecularColor(PyObject *args)
{
    return colorGet(args, &AppearanceList::getSpecularColor);
}

PyObject *MaterialListPy::setSpecularColor(PyObject *args)
{
    return colorSet(args,
                    static_cast<ColorSetter>(&AppearanceList::setSpecularColor),
                    static_cast<ColorAllSetter>(&AppearanceList::setSpecularColor));
}

PyObject *MaterialListPy::getEmissiveColor(PyObject *args)
{
    return colorGet(args, &AppearanceList::getEmissiveColor);
}

PyObject *MaterialListPy::setEmissiveColor(PyObject *args)
{
    return colorSet(args,
                    static_cast<ColorSetter>(&AppearanceList::setEmissiveColor),
                    static_cast<ColorAllSetter>(&AppearanceList::setEmissiveColor));
}

PyObject *MaterialListPy::getShininess(PyObject *args)
{
    return floatGet(args, &AppearanceList::getShininess);
}

PyObject *MaterialListPy::setShininess(PyObject *args)
{
    return floatSet(args,
                    static_cast<FloatSetter>(&AppearanceList::setShininess),
                    static_cast<FloatAllSetter>(&AppearanceList::setShininess));
}

PyObject *MaterialListPy::getTransparency(PyObject *args)
{
    return floatGet(args, &AppearanceList::getTransparency);
}

PyObject *MaterialListPy::setTransparency(PyObject *args)
{
    return floatSet(args,
                    static_cast<FloatSetter>(&AppearanceList::setTransparency),
                    static_cast<FloatAllSetter>(&AppearanceList::setTransparency));
}

PyObject *MaterialListPy::getMetallic(PyObject *args)
{
    return floatGet(args, &AppearanceList::getMetallic);
}

PyObject *MaterialListPy::setMetallic(PyObject *args)
{
    return floatSet(args,
                    static_cast<FloatSetter>(&AppearanceList::setMetallic),
                    static_cast<FloatAllSetter>(&AppearanceList::setMetallic));
}

PyObject *MaterialListPy::getRoughness(PyObject *args)
{
    return floatGet(args, &AppearanceList::getRoughness);
}

PyObject *MaterialListPy::setRoughness(PyObject *args)
{
    return floatSet(args,
                    static_cast<FloatSetter>(&AppearanceList::setRoughness),
                    static_cast<FloatAllSetter>(&AppearanceList::setRoughness));
}

// ---------------------------------------------------------------------
// Textures
//
// A slot holds the CONTENT HASH of a file the store owns, so stating a
// texture is two steps: content in, then a slot naming it. setTextureFile()
// is both at once, which is what a script wants.

PyObject *MaterialListPy::insertTextureFile(PyObject *args)
{
    const char *path = nullptr;
    const char *extension = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &path, &extension)) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        // Not a value change: the content is a claim on a file, and the
        // slot that names it is what changes the appearance
        std::string hash;
        if (getOwner()) {
            hash = getOwner()->insertTextureFile(path, extension);
        }
        else {
            hash = getAppearanceListPtr()->insertTextureFile(path, extension);
        }
        if (hash.empty()) {
            PyErr_Format(PyExc_IOError, "cannot read '%s'", path);
            return nullptr;
        }
        return Py::new_reference_to(Py::String(hash));
    }
    PY_CATCH
}

PyObject *MaterialListPy::getTextureFile(PyObject *args)
{
    const char *hash = nullptr;
    if (!PyArg_ParseTuple(args, "s", &hash)) {
        return nullptr;
    }
    PY_TRY
    {
        return Py::new_reference_to(Py::String(list().getTextureFile(hash)));
    }
    PY_CATCH
}

PyObject *MaterialListPy::getTexture(PyObject *args)
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
        const SurfaceTexture texture = list().getTexture(idx);
        Py::Dict maps;
        for (uint8_t slot = 0; slot < SurfaceTexture::SlotCount; ++slot) {
            if (!texture.maps[slot].empty()) {
                maps.setItem(SurfaceTexture::slotName(slot), Py::String(texture.maps[slot]));
            }
        }
        return Py::new_reference_to(maps);
    }
    PY_CATCH
}

/// The (index, slot) an argument list states, with no index meaning every
/// entry -- which is answered as -1 here, because there is no entry to name
bool MaterialListPy::textureArgs(PyObject *args, const char *format, int &idx, uint8_t &slot,
                                 const char *&value)
{
    Py_ssize_t where = 0;
    const char *name = nullptr;
    std::string withIndex = std::string("n") + format;
    if (PyArg_ParseTuple(args, withIndex.c_str(), &where, &name, &value)) {
        if (!indexOf(where, list().getSize(), idx)) {
            return false;
        }
    }
    else {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, format, &name, &value)) {
            return false;
        }
        idx = -1;
    }
    slot = slotOf(name);
    return slot < SurfaceTexture::SlotCount;
}

/// Run \a op over one entry's texture record, or over every entry's
void MaterialListPy::editTexture(int idx, const std::function<void(SurfaceTexture &)> &op)
{
    edit(
        [&](AppearanceList &values) {
            if (idx >= 0) {
                SurfaceTexture texture = values.getTexture(idx);
                op(texture);
                values.setTexture(idx, texture);
                return;
            }
            const int count = values.getSize();
            if (count == 0) {
                // Nothing to state it on yet, and a uniform write on an
                // empty list is what setTexture() itself does: one entry
                SurfaceTexture texture;
                op(texture);
                values.setTexture(texture);
                return;
            }
            for (int i = 0; i < count; ++i) {
                SurfaceTexture texture = values.getTexture(i);
                op(texture);
                values.setTexture(i, texture);
            }
        },
        idx);
}

PyObject *MaterialListPy::setTexture(PyObject *args)
{
    int idx = -1;
    uint8_t slot = 0;
    const char *hash = nullptr;
    if (!textureArgs(args, "ss", idx, slot, hash) || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        const std::string named = hash;
        editTexture(idx, [&](SurfaceTexture &texture) { texture.maps[slot] = named; });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::setTextureFile(PyObject *args)
{
    int idx = -1;
    uint8_t slot = 0;
    const char *path = nullptr;
    if (!textureArgs(args, "ss", idx, slot, path) || !writable()) {
        return nullptr;
    }
    PY_TRY
    {
        std::string hash;
        if (getOwner()) {
            hash = getOwner()->insertTextureFile(path);
        }
        else {
            hash = getAppearanceListPtr()->insertTextureFile(path);
        }
        if (hash.empty()) {
            PyErr_Format(PyExc_IOError, "cannot read '%s'", path);
            return nullptr;
        }
        editTexture(idx, [&](SurfaceTexture &texture) { texture.maps[slot] = hash; });
        return Py::new_reference_to(Py::String(hash));
    }
    PY_CATCH
}

PyObject *MaterialListPy::clearTexture(PyObject *args)
{
    // (), (index), (slot) or (index, slot) -- the two one-argument forms
    // tell themselves apart by type
    Py_ssize_t where = 0;
    const char *name = nullptr;
    int idx = -1;
    if (PyArg_ParseTuple(args, "|ns", &where, &name)) {
        if (PyTuple_Size(args) > 0 && !indexOf(where, list().getSize(), idx)) {
            return nullptr;
        }
    }
    else {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "s", &name)) {
            return nullptr;
        }
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        uint8_t slot = SurfaceTexture::SlotCount;
        if (name) {
            slot = slotOf(name);
            if (slot >= SurfaceTexture::SlotCount) {
                return nullptr;
            }
        }
        editTexture(idx, [&](SurfaceTexture &texture) {
            if (slot < SurfaceTexture::SlotCount) {
                texture.maps[slot].clear();
            }
            else {
                for (auto &map : texture.maps) {
                    map.clear();
                }
            }
        });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::getTextureTransform(PyObject *args)
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
        const SurfaceTexture texture = list().getTexture(idx);
        Py::Tuple scale(2);
        scale.setItem(0, Py::Float(texture.scale[0]));
        scale.setItem(1, Py::Float(texture.scale[1]));
        Py::Tuple offset(2);
        offset.setItem(0, Py::Float(texture.offset[0]));
        offset.setItem(1, Py::Float(texture.offset[1]));
        Py::Dict transform;
        transform.setItem("Scale", scale);
        transform.setItem("Offset", offset);
        transform.setItem("Rotation", Py::Float(texture.rotation));
        return Py::new_reference_to(transform);
    }
    PY_CATCH
}

PyObject *MaterialListPy::setTextureTransform(PyObject *args, PyObject *kwds)
{
    static const std::array<const char *, 5> kwlist {"Index", "Scale", "Offset", "Rotation",
                                                     nullptr};
    PyObject *index = nullptr;
    PyObject *scale = nullptr;
    PyObject *offset = nullptr;
    PyObject *rotation = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOOO",
                                     const_cast<char **>(kwlist.data()),
                                     &index, &scale, &offset, &rotation)) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        int idx = -1;
        if (index && index != Py_None) {
            if (!PyIndex_Check(index)) {
                PyErr_SetString(PyExc_TypeError, "Index must be an integer");
                return nullptr;
            }
            if (!indexOf(PyNumber_AsSsize_t(index, PyExc_IndexError), list().getSize(), idx)) {
                return nullptr;
            }
        }
        float pair[2] = {0.0F, 0.0F};
        auto readPair = [&](PyObject *value) {
            Py::Sequence seq(value);
            if (seq.size() != 2) {
                throw Py::ValueError("expected a pair");
            }
            pair[0] = static_cast<float>(Py::Float(seq[0]));
            pair[1] = static_cast<float>(Py::Float(seq[1]));
        };
        float scaleValue[2] = {1.0F, 1.0F};
        float offsetValue[2] = {0.0F, 0.0F};
        float rotationValue = 0.0F;
        if (scale) {
            readPair(scale);
            scaleValue[0] = pair[0];
            scaleValue[1] = pair[1];
        }
        if (offset) {
            readPair(offset);
            offsetValue[0] = pair[0];
            offsetValue[1] = pair[1];
        }
        if (rotation) {
            rotationValue = static_cast<float>(Py::Float(Py::Object(rotation)));
        }
        editTexture(idx, [&](SurfaceTexture &texture) {
            if (scale) {
                texture.scale[0] = scaleValue[0];
                texture.scale[1] = scaleValue[1];
            }
            if (offset) {
                texture.offset[0] = offsetValue[0];
                texture.offset[1] = offsetValue[1];
            }
            if (rotation) {
                texture.rotation = rotationValue;
            }
        });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::clearOverrides(PyObject *args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        edit([](AppearanceList &values) { values.clearOverrides(); });
        Py_Return;
    }
    PY_CATCH
}

PyObject *MaterialListPy::clearOverride(PyObject *args)
{
    Py_ssize_t where = 0;
    if (!PyArg_ParseTuple(args, "n", &where)) {
        return nullptr;
    }
    if (!writable()) {
        return nullptr;
    }
    PY_TRY
    {
        int idx = 0;
        if (!indexOf(where, list().getSize(), idx)) {
            return nullptr;
        }
        edit([idx](AppearanceList &values) { values.clearOverride(idx); }, idx);
        Py_Return;
    }
    PY_CATCH
}

Py::Tuple MaterialListPy::getTextureSlots() const
{
    // "slots" is a Qt macro that expands to nothing, and the error it
    // makes here names the '.' rather than the identifier
    Py::Tuple names(static_cast<int>(SurfaceTexture::SlotCount));
    for (uint8_t slot = 0; slot < SurfaceTexture::SlotCount; ++slot) {
        names.setItem(slot, Py::String(SurfaceTexture::slotName(slot)));
    }
    return names;
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

Py::Object MaterialListPy::getBase() const
{
    return Py::asObject(new MaterialPy(new MaterialAppearance(list().getBase())));
}

void MaterialListPy::setBase(Py::Object value)
{
    const MaterialAppearance *mat = materialOf(value.ptr());
    if (!mat) {
        throw Py::Exception();
    }
    const MaterialAppearance base = *mat;
    edit([&](AppearanceList &values) { values.setBase(base); });
}

Py::Tuple MaterialListPy::getOverrides() const
{
    const std::vector<uint32_t> &overrides = list().getOverrides();
    Py::Tuple faces(static_cast<int>(overrides.size()));
    for (std::size_t i = 0; i < overrides.size(); ++i) {
        faces.setItem(i, Py::Long(static_cast<unsigned long>(overrides[i])));
    }
    return faces;
}

Py::Boolean MaterialListPy::getFollowMaterial() const
{
    return {list().isFollowingMaterial()};
}

void MaterialListPy::setFollowMaterial(Py::Boolean value)
{
    const bool enable = static_cast<bool>(value);
    edit([&](AppearanceList &values) { values.setFollowMaterial(enable); });
}

Py::Boolean MaterialListPy::getPBR() const
{
    return {list().isPBR()};
}

void MaterialListPy::setPBR(Py::Boolean value)
{
    const bool enable = static_cast<bool>(value);
    edit([&](AppearanceList &values) { values.setPBR(enable); });
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
        auto *item = new MaterialPy(new MaterialAppearance(list->list().getMaterial(idx)));
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
    const MaterialAppearance *mat = materialOf(value);
    if (!mat) {
        return -1;
    }
    int idx = 0;
    if (!indexOf(index, list->list().getSize(), idx, true)) {
        return -1;
    }
    PY_TRY
    {
        const MaterialAppearance written = *mat;
        list->edit([&](AppearanceList &values) { values.set1Value(idx, written); }, idx);
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
            std::vector<MaterialAppearance> values;
            values.reserve(count);
            for (Py_ssize_t i = 0, at = start; i < count; ++i, at += step) {
                values.push_back(list->list().getMaterial(static_cast<int>(at)));
            }
            auto *sliced = new AppearanceList;
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
        std::vector<MaterialAppearance> written;
        if (!materialsOf(value, written)) {
            return -1;
        }
        if (static_cast<Py_ssize_t>(written.size()) != count) {
            PyErr_SetString(PyExc_ValueError,
                            "a material list slice takes exactly as many entries as it holds");
            return -1;
        }
        list->edit([&](AppearanceList &values) {
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
        const MaterialAppearance &mat = *static_cast<MaterialPy *>(value)->getMaterialAppearancePtr();
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
        std::vector<MaterialAppearance> values;
        auto *list = static_cast<MaterialListPy *>(self);
        const int count = list->list().getSize();
        for (int i = 0; i < count; ++i) {
            values.push_back(list->list().getMaterial(i));
        }
        if (!materialsOf(other, values)) {
            return nullptr;
        }
        auto *joined = new AppearanceList;
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
        std::vector<MaterialAppearance> values;
        for (Py_ssize_t n = 0; n < times; ++n) {
            for (int i = 0; i < count; ++i) {
                values.push_back(list->list().getMaterial(i));
            }
        }
        auto *repeated = new AppearanceList;
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
        std::vector<MaterialAppearance> added;
        if (!materialsOf(other, added)) {
            return nullptr;
        }
        list->edit([&](AppearanceList &values) {
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
    const AppearanceList &left = static_cast<MaterialListPy *>(v)->list();
    bool equal = false;
    if (PyObject_TypeCheck(w, &(MaterialListPy::Type))) {
        equal = left.isSame(static_cast<MaterialListPy *>(w)->list());
    }
    else if (PySequence_Check(w)) {
        // Against a plain sequence of materials, so a script that used to
        // compare with the tuple this property handed out still can
        std::vector<MaterialAppearance> other;
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
