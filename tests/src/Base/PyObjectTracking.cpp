// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <Base/PyObjectBase.h>

// PyObjectBase tracks the values an ATTRIBUTE hands out, so that
// obj.Placement.Base.x = 1 reaches the object: __getattro stamps the
// child with its parent, and any write on the child re-assigns it to
// that attribute. Sequence slots are installed raw by the binding
// generator and never saw any of it, which is why
// obj.Placement.Base[0] = 1 has always been a silent no-op.
//
// These tests pin the item half of the mechanism, over a throwaway list
// type standing in for the real ones (docs/PythonValueBindings.md).

namespace
{

// ---------------------------------------------------------------------
// A value with one settable attribute, which is all it takes to make it
// notify whoever handed it out

class ItemPy: public Base::PyObjectBase
{
public:
    static PyTypeObject Type;  // NOLINT

    explicit ItemPy(int value);

    int value {0};

    int _setattr(const char* attr, PyObject* py) override
    {
        if (std::string(attr) == "value") {
            value = static_cast<int>(PyLong_AsLong(py));
            return 0;
        }
        return PyObjectBase::_setattr(attr, py);
    }

    PyObject* _getattr(const char* attr) override
    {
        if (std::string(attr) == "value") {
            return PyLong_FromLong(value);
        }
        return PyObjectBase::_getattr(attr);
    }
};

// ---------------------------------------------------------------------
// A list of ints whose sq_item hands out ItemPy values

class ListPy: public Base::PyObjectBase
{
public:
    static PyTypeObject Type;  // NOLINT
    static PySequenceMethods Sequence;  // NOLINT

    ListPy();

    std::vector<int> values;
    /// How many times an item write reached the container, so a test can
    /// tell "wrote the same value" from "did not write at all"
    int writes {0};

    static Py_ssize_t sequence_length(PyObject* self)
    {
        return static_cast<Py_ssize_t>(static_cast<ListPy*>(self)->values.size());
    }

    static PyObject* sequence_item(PyObject* self, Py_ssize_t index)
    {
        auto* list = static_cast<ListPy*>(self);
        if (index < 0 || index >= static_cast<Py_ssize_t>(list->values.size())) {
            PyErr_SetString(PyExc_IndexError, "index out of range");
            return nullptr;
        }
        auto* item = new ItemPy(list->values[index]);
        // The one line a list-like type owes its items
        list->trackReturnedItem(item, index);
        return item;
    }

    static int sequence_ass_item(PyObject* self, Py_ssize_t index, PyObject* value)
    {
        auto* list = static_cast<ListPy*>(self);
        if (index < 0 || index >= static_cast<Py_ssize_t>(list->values.size())) {
            PyErr_SetString(PyExc_IndexError, "index out of range");
            return -1;
        }
        if (!value || !PyObject_TypeCheck(value, &ItemPy::Type)) {
            PyErr_SetString(PyExc_TypeError, "expected an item");
            return -1;
        }
        list->values[index] = static_cast<ItemPy*>(value)->value;
        ++list->writes;
        return 0;
    }
};

PyTypeObject ItemPy::Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "TestItem",  // NOLINT
    sizeof(ItemPy),
};

PyTypeObject ListPy::Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0) "TestList",  // NOLINT
    sizeof(ListPy),
};

PySequenceMethods ListPy::Sequence = {};  // NOLINT

ItemPy::ItemPy(int val)
    : PyObjectBase(nullptr, &Type)
    , value(val)
{}

ListPy::ListPy()
    : PyObjectBase(nullptr, &Type)
{}

void readyTypes()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    for (PyTypeObject* type : {&ItemPy::Type, &ListPy::Type}) {
        type->tp_dealloc = Base::PyObjectBase::PyDestructor;
        type->tp_getattro = Base::PyObjectBase::__getattro;
        type->tp_setattro = Base::PyObjectBase::__setattro;
        type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
        type->tp_base = &Base::PyObjectBase::Type;
    }

    ListPy::Sequence.sq_length = ListPy::sequence_length;
    ListPy::Sequence.sq_item = ListPy::sequence_item;
    ListPy::Sequence.sq_ass_item = ListPy::sequence_ass_item;
    ListPy::Type.tp_as_sequence = &ListPy::Sequence;

    ASSERT_EQ(PyType_Ready(&Base::PyObjectBase::Type), 0);
    ASSERT_EQ(PyType_Ready(&ItemPy::Type), 0);
    ASSERT_EQ(PyType_Ready(&ListPy::Type), 0);
}

class PyObjectTrackingTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!Py_IsInitialized()) {
            Py_InitializeEx(0);
        }
        readyTypes();
    }
};

}  // namespace

TEST_F(PyObjectTrackingTest, writingToAnItemReachesTheListItCameFrom)
{
    auto* list = new ListPy();
    list->values = {1, 2, 3};

    PyObject* item = PySequence_GetItem(list, 1);
    ASSERT_NE(item, nullptr);

    ASSERT_EQ(PyObject_SetAttrString(item, "value", PyLong_FromLong(42)), 0);
    EXPECT_EQ(list->values[1], 42);
    EXPECT_EQ(list->writes, 1);
    // and the entries either side are untouched
    EXPECT_EQ(list->values[0], 1);
    EXPECT_EQ(list->values[2], 3);

    Py_DECREF(item);
    Py_DECREF(list);
}

TEST_F(PyObjectTrackingTest, anItemKeepsTheListAliveWhileItCanStillWriteToIt)
{
    auto* list = new ListPy();
    list->values = {7};

    PyObject* item = PySequence_GetItem(list, 0);
    ASSERT_NE(item, nullptr);
    // The link holds a strong reference to the parent, so a list read as
    // a temporary -- vp.ShapeAppearance[0] -- is still there to be
    // written to
    EXPECT_GT(Py_REFCNT(list), 1);

    Py_DECREF(list);  // the caller's own reference
    ASSERT_EQ(PyObject_SetAttrString(item, "value", PyLong_FromLong(9)), 0);
    EXPECT_EQ(list->values[0], 9);

    Py_DECREF(item);
}

TEST_F(PyObjectTrackingTest, readingTheSameIndexAgainRetiresTheValueHandedOutBefore)
{
    // The item half of bug #0002902: two live children writing back to
    // one slot means the older one silently undoes a later write.
    auto* list = new ListPy();
    list->values = {1};

    PyObject* first = PySequence_GetItem(list, 0);
    PyObject* second = PySequence_GetItem(list, 0);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    ASSERT_EQ(PyObject_SetAttrString(second, "value", PyLong_FromLong(5)), 0);
    EXPECT_EQ(list->values[0], 5);

    ASSERT_EQ(PyObject_SetAttrString(first, "value", PyLong_FromLong(99)), 0);
    EXPECT_EQ(list->values[0], 5);  // the retired one writes nowhere
    EXPECT_EQ(list->writes, 1);

    Py_DECREF(first);
    Py_DECREF(second);
    Py_DECREF(list);
}

TEST_F(PyObjectTrackingTest, anItemThatOptedOutOfTrackingWritesNowhere)
{
    auto* list = new ListPy();
    list->values = {1};

    PyObject* item = PySequence_GetItem(list, 0);
    ASSERT_NE(item, nullptr);
    static_cast<Base::PyObjectBase*>(item)->resetAttribute();

    ASSERT_EQ(PyObject_SetAttrString(item, "value", PyLong_FromLong(3)), 0);
    EXPECT_EQ(list->values[0], 1);
    EXPECT_EQ(list->writes, 0);

    Py_DECREF(item);
    Py_DECREF(list);
}

TEST_F(PyObjectTrackingTest, aConstItemIsNotAWayIntoTheList)
{
    auto* list = new ListPy();
    list->values = {1};

    // A container that hands out a const item -- what a read-only
    // property does -- gets no link at all, so nothing can write back
    // even before the const check refuses the write
    auto* item = new ItemPy(1);
    item->setConst();
    list->trackReturnedItem(item, 0);

    item->value = 8;
    item->startNotify();
    EXPECT_EQ(list->values[0], 1);
    EXPECT_EQ(list->writes, 0);

    Py_DECREF(item);
    Py_DECREF(list);
}
