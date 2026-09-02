/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <functional>
#include <sstream>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/math/special_functions/round.hpp>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/ProgramVersion.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Quantity.h>
#include <Base/PyWrapParseTupleAndKeywords.h>
#include <Base/Stream.h>
#include <Base/Tools.h>

#include "PropertyStandard.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentParams.h"
#include "MaterialListPy.h"
#include "MaterialPy.h"
#include "ObjectIdentifier.h"

using namespace App;
using namespace Base;
using namespace std;




//**************************************************************************
//**************************************************************************
// PropertyInteger
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyInteger , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyInteger::PropertyInteger()
{
    _lValue = 0;
}


PropertyInteger::~PropertyInteger() = default;

//**************************************************************************
// Base class implementer


bool PropertyInteger::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyInteger::getClassTypeId())
        && this->getValue() == static_cast<const PropertyInteger&>(other).getValue();
}

void PropertyInteger::setValue(long lValue)
{
    aboutToSetValue();
    _lValue=lValue;
    hasSetValue();
}

long PropertyInteger::getValue() const
{
    return _lValue;
}

PyObject *PropertyInteger::getPyObject()
{
    return Py_BuildValue("l", _lValue);
}

void PropertyInteger::setPyObject(PyObject *value)
{
    if (PyLong_Check(value)) {
        aboutToSetValue();
        _lValue = PyLong_AsLong(value);
        hasSetValue();
    }
    else {
        std::string error = std::string("type must be int, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyInteger::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Integer value=\"" <<  _lValue <<"\"/>\n";
}

void PropertyInteger::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Integer");
    // get the value of my Attribute
    setValue(reader.getAttributeAsInteger("value"));
}

Property *PropertyInteger::Copy() const
{
    PropertyInteger *p= new PropertyInteger();
    p->_lValue = _lValue;
    return p;
}

void PropertyInteger::Paste(const Property &from)
{
    aboutToSetValue();
    _lValue = dynamic_cast<const PropertyInteger&>(from)._lValue;
    hasSetValue();
}

void PropertyInteger::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    verifyPath(path);

    if (value.type() == typeid(long))
        setValue(App::any_cast<long>(value));
    else if (value.type() == typeid(int))
        setValue(App::any_cast<int>(value));
    else if (value.type() == typeid(double))
        setValue(boost::math::round(App::any_cast<double>(value)));
    else if (value.type() == typeid(float))
        setValue(boost::math::round(App::any_cast<float>(value)));
    else if (value.type() == typeid(Quantity))
        setValue(boost::math::round(App::any_cast<const Quantity &>(value).getValue()));
    else
        throw bad_cast();
}

void PropertyInteger::interpolate(const Property &from, const Property &to, float t)
{
    auto fromValue = dynamic_cast<const PropertyInteger&>(from)._lValue;
    auto toValue = dynamic_cast<const PropertyInteger&>(to)._lValue;
    if (fromValue != toValue)
        setValue(boost::math::round((toValue - fromValue) * t + fromValue));
}

//**************************************************************************
//**************************************************************************
// PropertyPath
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPath , App::Property)

//**************************************************************************
// Construction/Destruction

PropertyPath::PropertyPath() = default;

PropertyPath::~PropertyPath() = default;


//**************************************************************************
// Base class implementer


//**************************************************************************
// Setter/getter for the property

void PropertyPath::setValue(const boost::filesystem::path &Path)
{
    aboutToSetValue();
    _cValue = Path;
    hasSetValue();
}

void PropertyPath::setValue(const char * Path)
{
    aboutToSetValue();
#if (BOOST_FILESYSTEM_VERSION == 2)
    _cValue = boost::filesystem::path(Path,boost::filesystem::no_check );
    //_cValue = boost::filesystem::path(Path,boost::filesystem::native );
    //_cValue = boost::filesystem::path(Path,boost::filesystem::windows_name );
#else
    _cValue = boost::filesystem::path(Path);
#endif
    hasSetValue();
}

const boost::filesystem::path &PropertyPath::getValue() const
{
    return _cValue;
}

PyObject *PropertyPath::getPyObject()
{
#if (BOOST_FILESYSTEM_VERSION == 2)
    std::string str = _cValue.native_file_string();
#else
    std::string str = _cValue.string();
#endif

    // Returns a new reference, don't increment it!
    PyObject *p = PyUnicode_DecodeUTF8(str.c_str(),str.size(),nullptr);
    if (!p) throw Base::UnicodeError("UTF8 conversion failure at PropertyPath::getPyObject()");
    return p;
}

void PropertyPath::setPyObject(PyObject *value)
{
    std::string path;
    if (PyUnicode_Check(value)) {
        path = PyUnicode_AsUTF8(value);
    }
    else {
        std::string error = std::string("type must be str or unicode, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }

    // assign the path
    setValue(path.c_str());
}


void PropertyPath::Save (Base::Writer &writer) const
{
    std::string val = encodeAttribute(_cValue.string());
    writer.Stream() << writer.ind() << "<Path value=\"" <<  val <<"\"/>\n";
}

void PropertyPath::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Path");
    // get the value of my Attribute
    setValue(reader.getAttribute("value"));
}

Property *PropertyPath::Copy() const
{
    PropertyPath *p= new PropertyPath();
    p->_cValue = _cValue;
    return p;
}

void PropertyPath::Paste(const Property &from)
{
    aboutToSetValue();
    _cValue = dynamic_cast<const PropertyPath&>(from)._cValue;
    hasSetValue();
}

unsigned int PropertyPath::getMemSize () const
{
    return static_cast<unsigned int>(_cValue.string().size());
}

bool PropertyPath::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyPath::getClassTypeId())
        && this->getValue() == static_cast<const PropertyPath&>(other).getValue();
}

//**************************************************************************
//**************************************************************************
// PropertyEnumeration
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyEnumeration, App::PropertyInteger)

//**************************************************************************
// Construction/Destruction


PropertyEnumeration::PropertyEnumeration()
{
    _editorTypeName = "Gui::PropertyEditor::PropertyEnumItem";
}

PropertyEnumeration::PropertyEnumeration(const App::Enumeration &e)
{
    _enum = e;
}

PropertyEnumeration::~PropertyEnumeration() = default;

void PropertyEnumeration::setEnums(const char **plEnums)
{
    // For backward compatibility, if the property container is not attached to
    // any document (i.e. its full name starts with '?'), do not notify, or
    // else existing code may crash.
    bool notify = !boost::starts_with(getFullName(), "?");
    if (notify)
        aboutToSetValue();
    _enum.setEnums(plEnums);
    if (notify)
        hasSetValue();
}

void PropertyEnumeration::setEnums(const std::vector<std::string> &Enums)
{
    setEnumVector(Enums);
}

void PropertyEnumeration::setValue(const char *value)
{
    aboutToSetValue();
    _enum.setValue(value);
    hasSetValue();
}

void PropertyEnumeration::setValue(long value)
{
    aboutToSetValue();
    _enum.setValue(value);
    hasSetValue();
}

void PropertyEnumeration::setValue(const Enumeration &source)
{
    aboutToSetValue();
    _enum = source;
    hasSetValue();
}

long PropertyEnumeration::getValue() const
{
    return _enum.getInt();
}

bool PropertyEnumeration::isValue(const char *value) const
{
    return _enum.isValue(value);
}

bool PropertyEnumeration::isPartOf(const char *value) const
{
    return _enum.contains(value);
}

const char * PropertyEnumeration::getValueAsString() const
{
    if (!_enum.isValid())
        THROWM(Base::RuntimeError, "Cannot get value from invalid enumeration")
    return _enum.getCStr();
}

const Enumeration & PropertyEnumeration::getEnum() const
{
    return _enum;
}

std::vector<std::string> PropertyEnumeration::getEnumVector() const
{
    return _enum.getEnumVector();
}

void PropertyEnumeration::setEnumVector(const std::vector<std::string> &values)
{
    // For backward compatibility, if the property container is not attached to
    // any document (i.e. its full name starts with '?'), do not notify, or
    // else existing code may crash.
    bool notify = !boost::starts_with(getFullName(), "?");
    if (notify)
        aboutToSetValue();
    _enum.setEnums(values);
    if (notify)
        hasSetValue();
}

bool PropertyEnumeration::hasEnums() const
{
    return _enum.hasEnums();
}

bool PropertyEnumeration::isValid() const
{
    return _enum.isValid();
}

void PropertyEnumeration::Save(Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Integer value=\"" <<  _enum.getInt() <<"\"";
    if (persistEnums && _enum.isCustom())
        writer.Stream() << " CustomEnum=\"true\"";
    writer.Stream() << "/>\n";
    if (persistEnums && _enum.isCustom()) {
        std::vector<std::string> items = getEnumVector();
        writer.Stream() << writer.ind() << "<CustomEnumList count=\"" <<  items.size() <<"\">\n";
        writer.incInd();
        for(auto & item : items) {
            std::string val = encodeAttribute(item);
            writer.Stream() << "<Enum value=\"" <<  val <<"\"/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</CustomEnumList>\n";
    }
}

void PropertyEnumeration::setPersistEnums(bool enable)
{
    persistEnums = enable;
}

void PropertyEnumeration::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Integer");
    // get the value of my Attribute
    long val = reader.getAttributeAsInteger("value");

    aboutToSetValue();

    if (reader.hasAttribute("CustomEnum")) {
        reader.readElement("CustomEnumList");
        int count = reader.getAttributeAsInteger("count");
        std::vector<std::string> values(count);

        for(int i = 0; i < count; i++) {
            reader.readElement("Enum");
            values[i] = reader.getAttribute("value");
        }

        reader.readEndElement("CustomEnumList");

        _enum.setEnums(values);
    }

    if (val < 0) {
        // If the enum is empty at this stage do not print a warning
        if (_enum.hasEnums()) {
            Base::Console().DeveloperWarning(std::string("PropertyEnumeration"), "Enumeration index %d is out of range, ignore it\n", val);
        }
        val = getValue();
    }

    _enum.setValue(val);
    hasSetValue();
}

PyObject * PropertyEnumeration::getPyObject()
{
    if (!_enum.isValid()) {
        Py_Return;
    }

    return Py_BuildValue("s", getValueAsString());
}

void PropertyEnumeration::setPyObject(PyObject *value)
{
    if (PyLong_Check(value)) {
        long val = PyLong_AsLong(value);
        if (_enum.isValid()) {
            aboutToSetValue();
            _enum.setValue(val, true);
            hasSetValue();
        }
        return;
    }
    else if (PyUnicode_Check(value)) {
        std::string str = PyUnicode_AsUTF8(value);
        if (_enum.contains(str.c_str())) {
            aboutToSetValue();
            _enum.setValue(str);
            hasSetValue();
        }
        else {
            FC_THROWM(Base::ValueError, "'" << str 
                    << "' is not part of the enumeration in "
                    << getFullName());
        }
        return;
    }
    else if (PySequence_Check(value)) {

        try {
            std::vector<std::string> values;

            int idx = -1;
            Py::Sequence seq(value);

            if(seq.size() == 2) {
                Py::Object v(seq[0].ptr());
                if(!v.isString() && v.isSequence()) {
                    idx = Py::Int(seq[1].ptr());
                    seq = v;
                }
            }

            values.resize(seq.size());

            for (int i = 0; i < seq.size(); ++i)
                values[i] = Py::Object(seq[i].ptr()).as_string();

            aboutToSetValue();
            _enum.setEnums(values);
            if (idx>=0)
                _enum.setValue(idx,true);
            hasSetValue();
            return;
        } catch (Py::Exception &) {
            Base::PyException e;
            e.ReportException();
        }
    }

    FC_THROWM(Base::TypeError, "PropertyEnumeration " << getFullName()
            << " expects type to be int, string, or list(string), or list(list, int)");
}

Property * PropertyEnumeration::Copy() const
{
    return new PropertyEnumeration(_enum);
}

void PropertyEnumeration::Paste(const Property &from)
{
    const PropertyEnumeration& prop = dynamic_cast<const PropertyEnumeration&>(from);
    setValue(prop._enum);
}

void PropertyEnumeration::setPathValue(const ObjectIdentifier &, const App::any &value)
{
    if (value.type() == typeid(int))
        setValue(App::any_cast<int>(value));
    else if (value.type() == typeid(long))
        setValue(App::any_cast<long>(value));
    else if (value.type() == typeid(double))
        setValue(App::any_cast<double>(value));
    else if (value.type() == typeid(float))
        setValue(App::any_cast<float>(value));
    else if (value.type() == typeid(short))
        setValue(App::any_cast<short>(value));
    else if (value.type() == typeid(std::string))
        setValue(App::any_cast<const std::string &>(value).c_str());
    else if (value.type() == typeid(char*))
        setValue(App::any_cast<char*>(value));
    else if (value.type() == typeid(const char*))
        setValue(App::any_cast<const char*>(value));
    else {
        Base::PyGILStateLocker lock;
        Py::Object pyValue = pyObjectFromAny(value);
        setPyObject(pyValue.ptr());
    }
}

bool PropertyEnumeration::setPyPathValue(const ObjectIdentifier &, const Py::Object &value)
{
    setPyObject(value.ptr());
    return true;
}

App::any PropertyEnumeration::getPathValue(const ObjectIdentifier &path) const
{
    std::string p = path.getSubPathStr();
    if (p == ".Enum" || p == ".All") {
        Base::PyGILStateLocker lock;
        Py::Object res;
        getPyPathValue(path, res);
        return pyObjectToAny(res,false);
    }
    else if (p == ".String") {
        auto v = getValueAsString();
        return std::string(v?v:"");
    } else
        return getValue();
}

bool PropertyEnumeration::getPyPathValue(const ObjectIdentifier &path, Py::Object &r) const
{
    std::string p = path.getSubPathStr();
    if (p == ".Enum" || p == ".All") {
        Base::PyGILStateLocker lock;
        Py::Tuple res(_enum.maxValue()+1);
        std::vector<std::string> enums = _enum.getEnumVector();
        PropertyString tmp;
        for(int i=0;i< int(enums.size());++i) {
            tmp.setValue(enums[i]);
            res.setItem(i,Py::asObject(tmp.getPyObject()));
        }
        if (p == ".Enum")
            r = res;
        else {
            Py::Tuple tuple(2);
            tuple.setItem(0, res);
            tuple.setItem(1, Py::Int(getValue()));
            r = tuple;
        }
    } else if (p == ".String") {
        auto v = getValueAsString();
        r = Py::String(v?v:"");
    } else 
        r = Py::Int(getValue());
    return true;
}

bool PropertyEnumeration::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyEnumeration::getClassTypeId())
        && getEnum() == static_cast<const PropertyEnumeration&>(other).getEnum();
}

//**************************************************************************
//**************************************************************************
// PropertyIntegerConstraint
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyIntegerConstraint, App::PropertyInteger)

//**************************************************************************
// Construction/Destruction


PropertyIntegerConstraint::PropertyIntegerConstraint() = default;

PropertyIntegerConstraint::~PropertyIntegerConstraint()
{
    if (_ConstStruct && _ConstStruct->isDeletable())
        delete _ConstStruct;
}

bool PropertyIntegerConstraint::isSame(const Property &_other) const
{
    if (&_other == this)
        return true;
    if (!_other.isDerivedFrom(PropertyIntegerConstraint::getClassTypeId()))
        return false;
    const auto &other = static_cast<const PropertyIntegerConstraint&>(_other);
    if (this->getValue() != other.getValue())
        return false;

    if (this->_ConstStruct == other._ConstStruct)
        return true;

    return this->_ConstStruct
        && other._ConstStruct
        && *this->_ConstStruct == *other._ConstStruct;
}

Property *PropertyIntegerConstraint::Copy(void) const
{
    PropertyIntegerConstraint *p= new PropertyIntegerConstraint();
    p->_lValue = _lValue;
    if (_ConstStruct && _ConstStruct->isDeletable())
        p->setConstraints(new Constraints(*_ConstStruct));
    else
        p->setConstraints(_ConstStruct);
    return p;
}

void PropertyIntegerConstraint::Paste(const Property &from)
{
    aboutToSetValue();
    auto &other = dynamic_cast<const PropertyIntegerConstraint&>(from);
    _lValue = other._lValue;
    if (other._ConstStruct && other._ConstStruct->isDeletable())
        setConstraints(new Constraints(*other._ConstStruct));
    else
        setConstraints(other._ConstStruct);
    hasSetValue();
}

void PropertyIntegerConstraint::setConstraints(const Constraints* sConstrain)
{
    if (_ConstStruct != sConstrain) {
        if (_ConstStruct && _ConstStruct->isDeletable())
            delete _ConstStruct;
    }

    _ConstStruct = sConstrain;
}

const PropertyIntegerConstraint::Constraints*  PropertyIntegerConstraint::getConstraints() const
{
    return _ConstStruct;
}

long PropertyIntegerConstraint::getMinimum() const
{
    if (_ConstStruct)
        return _ConstStruct->LowerBound;
    // return the min of int, not long
    return std::numeric_limits<int>::min();
}

long PropertyIntegerConstraint::getMaximum() const
{
    if (_ConstStruct)
        return _ConstStruct->UpperBound;
    // return the max of int, not long
    return std::numeric_limits<int>::max();
}

long PropertyIntegerConstraint::getStepSize() const
{
    if (_ConstStruct)
        return _ConstStruct->StepSize;
    return 1;
}

void PropertyIntegerConstraint::setPyObject(PyObject *value)
{
    if (PyLong_Check(value)) {
        long temp = PyLong_AsLong(value);
        if (_ConstStruct) {
            if (temp > _ConstStruct->UpperBound)
                temp = _ConstStruct->UpperBound;
            else if(temp < _ConstStruct->LowerBound)
                temp = _ConstStruct->LowerBound;
        }

        aboutToSetValue();
        _lValue = temp;
        hasSetValue();
    }
    else if (PyDict_Check(value)) {
        // Upstream spells the constrained form as a dict, with everything but
        // the value optional. Translate it into the 4-tuple below, so the
        // clamping and the constraint bookkeeping stay one code path.
        long values[4] = {0,
                          std::numeric_limits<int>::lowest(),
                          std::numeric_limits<int>::max(),
                          1};
        Py::Tuple dummy;
        static const std::array<const char*, 5> kw = {"value", "min", "max", "step", nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(dummy.ptr(), value, "l|lll", kw,
                                                 &values[0], &values[1], &values[2], &values[3]))
            throw Py::Exception();

        Py::Tuple tuple(4);
        for (int i=0; i<4; i++)
            tuple.setItem(i, Py::Long(values[i]));
        setPyObject(tuple.ptr());
    }
    else if (PyTuple_Check(value) && PyTuple_Size(value) == 4) {
        long values[4];
        for (int i=0; i<4; i++) {
            PyObject* item;
            item = PyTuple_GetItem(value,i);
            if (PyLong_Check(item))
                values[i] = PyLong_AsLong(item);
            else
                THROWM(Base::TypeError, "Type in tuple must be int")
        }

        aboutToSetValue();

        Constraints* c = new Constraints();
        c->setDeletable(true);
        c->LowerBound = values[1];
        c->UpperBound = values[2];
        c->StepSize = std::max<long>(1, values[3]);
        if (values[0] > c->UpperBound)
            values[0] = c->UpperBound;
        else if (values[0] < c->LowerBound)
            values[0] = c->LowerBound;
        setConstraints(c);

        _lValue = values[0];
        hasSetValue();
    }
    else {
        std::string error = std::string("type must be int, dict or tuple, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

//**************************************************************************
//**************************************************************************
// PropertyPercent
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPercent , App::PropertyIntegerConstraint)

const PropertyIntegerConstraint::Constraints percent = {0,100,1};

//**************************************************************************
// Construction/Destruction


PropertyPercent::PropertyPercent()
{
    _ConstStruct = &percent;
}

PropertyPercent::~PropertyPercent() = default;

//**************************************************************************
//**************************************************************************
// PropertyIntegerList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyIntegerList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction


PropertyIntegerList::PropertyIntegerList() = default;

PropertyIntegerList::~PropertyIntegerList() = default;

//**************************************************************************
// Base class implementer

PyObject *PropertyIntegerList::getPyObject()
{
    PyObject* list = PyList_New(getSize());
    for(int i = 0;i<getSize(); i++)
        PyList_SetItem( list, i, PyLong_FromLong(_lValueList[i]));
    return list;
}

long PropertyIntegerList::getPyValue(PyObject *item) const {
    if (PyLong_Check(item))
        return PyLong_AsLong(item);
    std::string error = std::string("type in list must be int, not ");
    error += item->ob_type->tp_name;
    THROWM(Base::TypeError, error)
}

bool PropertyIntegerList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    if(writer.getFileVersion()>1) {
        for(auto &v : _lValueList)
            writer.Stream() << v << '\n';
    } else {
        for(auto &v : _lValueList)
            writer.Stream() << "<I v=\"" <<  v <<"\"/>\n";
    }
    return false;
}

void PropertyIntegerList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");

    std::vector<long> values(count);
    if(reader.FileVersion>1) {
        auto &s = reader.beginCharStream();
        for(int i = 0; i < count; i++)
            s >> values[i];
        reader.endCharStream();
    } else {
        for(int i = 0; i < count; i++) {
            reader.readElement("I");
            values[i] = reader.getAttributeAsInteger("v");
        }
    }
    //assignment
    setValues(std::move(values));
}

Property *PropertyIntegerList::Copy() const
{
    PropertyIntegerList *p= new PropertyIntegerList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyIntegerList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyIntegerList&>(from)._lValueList);
}

void PropertyIntegerList::interpolateValue(int index, const long &from, const long &to, float t)
{
    if (from != to) {
        set1Value(index, boost::math::round((to - from) * t + from));
    }
}

//**************************************************************************
//**************************************************************************
// PropertyIntegerSet
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyIntegerSet , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyIntegerSet::PropertyIntegerSet() = default;

PropertyIntegerSet::~PropertyIntegerSet() = default;


//**************************************************************************
// Base class implementer

void PropertyIntegerSet::setValue(long lValue)
{
    aboutToSetValue();
    _lValueSet.clear();
    _lValueSet.insert(lValue);
    hasSetValue();
}

void PropertyIntegerSet::setValues(const std::set<long>& values)
{
    aboutToSetValue();
    _lValueSet = values;
    hasSetValue();
}

PyObject *PropertyIntegerSet::getPyObject()
{
    PyObject* set = PySet_New(nullptr);
    for(long it : _lValueSet)
        PySet_Add(set,PyLong_FromLong(it));
    return set;
}

void PropertyIntegerSet::setPyObject(PyObject *value)
{
    if (PySequence_Check(value)) {

        Py_ssize_t nSize = PySequence_Length(value);
        std::set<long> values;

        for (Py_ssize_t i=0; i<nSize;++i) {
            PyObject* item = PySequence_GetItem(value, i);
            if (!PyLong_Check(item)) {
                std::string error = std::string("type in list must be int, not ");
                error += item->ob_type->tp_name;
                THROWM(Base::TypeError, error)
            }
            values.insert(PyLong_AsLong(item));
        }

        setValues(values);
    }
    else if (PyLong_Check(value)) {
        setValue(PyLong_AsLong(value));
    }
    else {
        std::string error = std::string("type must be int or list of int, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyIntegerSet::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<IntegerSet count=\"" <<  _lValueSet.size() <<"\">\n";
    if(writer.getFileVersion()>1) {
        for(auto &v : _lValueSet)
            writer.Stream() << v << '\n';
    } else {
        for(std::set<long>::const_iterator it=_lValueSet.begin();it!=_lValueSet.end();++it)
            writer.Stream() << "<I v=\"" <<  *it <<"\"/>\n";
    }
    writer.Stream() << writer.ind() << "</IntegerSet>\n" ;
}

void PropertyIntegerSet::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("IntegerSet");
    // get the value of my Attribute
    int count = reader.getAttributeAsInteger("count");

    std::set<long> values;
    if(reader.FileVersion > 1) {
        auto &s = reader.beginCharStream();
        for(int i = 0; i < count; i++) {
            long v;
            s >> v;
            values.insert(v);
        }
        reader.endCharStream();
    } else {
        for(int i = 0; i < count; i++) {
            reader.readElement("I");
            values.insert(reader.getAttributeAsInteger("v"));
        }
    }

    reader.readEndElement("IntegerSet");

    //assignment
    setValues(values);
}

Property *PropertyIntegerSet::Copy() const
{
    PropertyIntegerSet *p= new PropertyIntegerSet();
    p->_lValueSet = _lValueSet;
    return p;
}

void PropertyIntegerSet::Paste(const Property &from)
{
    aboutToSetValue();
    _lValueSet = dynamic_cast<const PropertyIntegerSet&>(from)._lValueSet;
    hasSetValue();
}

unsigned int PropertyIntegerSet::getMemSize () const
{
    return static_cast<unsigned int>(_lValueSet.size() * sizeof(long));
}

bool PropertyIntegerSet::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyIntegerSet::getClassTypeId())
        && getValues() == static_cast<const PropertyIntegerSet&>(other).getValues();
}

//**************************************************************************
//**************************************************************************
// PropertyFloat
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFloat , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyFloat::PropertyFloat()
{
    _dValue = 0.0;
}

PropertyFloat::~PropertyFloat() = default;

//**************************************************************************
// Base class implementer

void PropertyFloat::setValue(double lValue)
{
    aboutToSetValue();
    _dValue=lValue;
    hasSetValue();
}

double PropertyFloat::getValue() const
{
    return _dValue;
}

PyObject *PropertyFloat::getPyObject()
{
    return Py_BuildValue("d", _dValue);
}

void PropertyFloat::setPyObject(PyObject *value)
{
    if (PyFloat_Check(value)) {
        aboutToSetValue();
        _dValue = PyFloat_AsDouble(value);
        hasSetValue();
    }
    else if(PyLong_Check(value)) {
        aboutToSetValue();
        _dValue = PyLong_AsLong(value);
        hasSetValue();
    }
    else {
        std::string error = std::string("type must be float or int, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyFloat::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Float value=\"" <<  _dValue <<"\"/>\n";
}

void PropertyFloat::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Float");
    // get the value of my Attribute
    setValue(reader.getAttributeAsFloat("value"));
}

Property *PropertyFloat::Copy() const
{
    PropertyFloat *p= new PropertyFloat();
    p->_dValue = _dValue;
    return p;
}

void PropertyFloat::Paste(const Property &from)
{
    aboutToSetValue();
    _dValue = dynamic_cast<const PropertyFloat&>(from)._dValue;
    hasSetValue();
}

void PropertyFloat::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    verifyPath(path);

    if (value.type() == typeid(double))
        setValue(App::any_cast<double>(value));
    else if (value.type() == typeid(float))
        setValue(App::any_cast<float>(value));
    else if (value.type() == typeid(long))
        setValue(App::any_cast<long>(value));
    else if (value.type() == typeid(int))
        setValue(App::any_cast<int>(value));
    else if (value.type() == typeid(Quantity))
        setValue((App::any_cast<const Quantity&>(value)).getValue());
    else if (value.type() == typeid(unsigned long))
        setValue(App::any_cast<unsigned long>(value));
    else
        throw bad_cast();
}

App::any PropertyFloat::getPathValue(const ObjectIdentifier &path) const
{
    verifyPath(path);
    return _dValue;
}

bool PropertyFloat::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyFloat::getClassTypeId())
        && getValue() == static_cast<const PropertyFloat&>(other).getValue();
}

void PropertyFloat::interpolate(const Property &from, const Property &to, float t)
{
    auto fromValue = dynamic_cast<const PropertyFloat&>(from)._dValue;
    auto toValue = dynamic_cast<const PropertyFloat&>(to)._dValue;
    if (fromValue != toValue)
        setValue((toValue - fromValue) * t + fromValue);
}

//**************************************************************************
//**************************************************************************
// PropertyFloatConstraint
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFloatConstraint, App::PropertyFloat)

//**************************************************************************
// Construction/Destruction


PropertyFloatConstraint::PropertyFloatConstraint() = default;

PropertyFloatConstraint::~PropertyFloatConstraint()
{
    if (_ConstStruct && _ConstStruct->isDeletable())
        delete _ConstStruct;
}

Property *PropertyFloatConstraint::Copy(void) const
{
    PropertyFloatConstraint *p= new PropertyFloatConstraint();
    p->_dValue = _dValue;
    if (_ConstStruct && _ConstStruct->isDeletable())
        p->setConstraints(new Constraints(*_ConstStruct));
    else
        p->setConstraints(_ConstStruct);
    return p;
}

void PropertyFloatConstraint::Paste(const Property &from)
{
    aboutToSetValue();
    auto &other = dynamic_cast<const PropertyFloatConstraint&>(from);
    _dValue = other._dValue;
    if (other._ConstStruct && other._ConstStruct->isDeletable())
        setConstraints(new Constraints(*other._ConstStruct));
    else
        setConstraints(other._ConstStruct);
    hasSetValue();
}

void PropertyFloatConstraint::setConstraints(const Constraints* sConstrain)
{
    if (_ConstStruct != sConstrain) {
        if (_ConstStruct && _ConstStruct->isDeletable())
            delete _ConstStruct;
    }
    _ConstStruct = sConstrain;
}

const PropertyFloatConstraint::Constraints*  PropertyFloatConstraint::getConstraints() const
{
    return _ConstStruct;
}

double PropertyFloatConstraint::getMinimum() const
{
    if (_ConstStruct)
        return _ConstStruct->LowerBound;
    return std::numeric_limits<double>::min();
}

double PropertyFloatConstraint::getMaximum() const
{
    if (_ConstStruct)
        return _ConstStruct->UpperBound;
    return std::numeric_limits<double>::max();
}

double PropertyFloatConstraint::getStepSize() const
{
    if (_ConstStruct)
        return _ConstStruct->StepSize;
    return 1.0;
}

void PropertyFloatConstraint::setPyObject(PyObject *value)
{
    if (PyFloat_Check(value)) {
        double temp = PyFloat_AsDouble(value);
        if (_ConstStruct) {
            if (temp > _ConstStruct->UpperBound)
                temp = _ConstStruct->UpperBound;
            else if (temp < _ConstStruct->LowerBound)
                temp = _ConstStruct->LowerBound;
        }

        aboutToSetValue();
        _dValue = temp;
        hasSetValue();
    }
    else if (PyLong_Check(value)) {
        double temp = (double)PyLong_AsLong(value);
        if (_ConstStruct) {
            if (temp > _ConstStruct->UpperBound)
                temp = _ConstStruct->UpperBound;
            else if (temp < _ConstStruct->LowerBound)
                temp = _ConstStruct->LowerBound;
        }

        aboutToSetValue();
        _dValue = temp;
        hasSetValue();
    }
    else if (PyDict_Check(value)) {
        // See PropertyIntegerConstraint::setPyObject -- same dict form, same
        // reason for translating it into the tuple this class already takes.
        double values[4] = {0.0,
                            std::numeric_limits<double>::lowest(),
                            std::numeric_limits<double>::max(),
                            1.0};
        Py::Tuple dummy;
        static const std::array<const char*, 5> kw = {"value", "min", "max", "step", nullptr};
        if (!Base::Wrapped_ParseTupleAndKeywords(dummy.ptr(), value, "d|ddd", kw,
                                                 &values[0], &values[1], &values[2], &values[3]))
            throw Py::Exception();

        Py::Tuple tuple(4);
        for (int i=0; i<4; i++)
            tuple.setItem(i, Py::Float(values[i]));
        setPyObject(tuple.ptr());
    }
    else if (PyTuple_Check(value) && PyTuple_Size(value) == 4) {
        double values[4];
        for (int i=0; i<4; i++) {
            PyObject* item;
            item = PyTuple_GetItem(value,i);
            if (PyFloat_Check(item))
                values[i] = PyFloat_AsDouble(item);
            else if (PyLong_Check(item))
                values[i] = PyLong_AsLong(item);
            else
                THROWM(Base::TypeError, "Type in tuple must be float or int")
        }

        double stepSize = values[3];
        // need a value > 0
        if (stepSize < DBL_EPSILON)
            THROWM(Base::ValueError, "Step size must be greater than zero")

        aboutToSetValue();

        Constraints* c = new Constraints();
        c->setDeletable(true);
        c->LowerBound = values[1];
        c->UpperBound = values[2];
        c->StepSize = stepSize;
        if (values[0] > c->UpperBound)
            values[0] = c->UpperBound;
        else if (values[0] < c->LowerBound)
            values[0] = c->LowerBound;
        setConstraints(c);

        _dValue = values[0];
        hasSetValue();
    }
    else {
        std::string error = std::string("type must be float, dict or tuple, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

bool PropertyFloatConstraint::isSame(const Property &_other) const
{
    if (&_other == this)
        return true;
    if (!_other.isDerivedFrom(PropertyFloatConstraint::getClassTypeId()))
        return false;
    auto &other = static_cast<const PropertyFloatConstraint&>(_other);
    if (this->getValue() != other.getValue())
        return false;

    if (this->_ConstStruct == other._ConstStruct)
        return true;

    return this->_ConstStruct
        && other._ConstStruct
        && *this->_ConstStruct == *other._ConstStruct;
}

//**************************************************************************
// PropertyPrecision
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPrecision, App::PropertyFloatConstraint)

//**************************************************************************
// Construction/Destruction
//
const PropertyFloatConstraint::Constraints PrecisionStandard = {0.0,DBL_MAX,0.001};

PropertyPrecision::PropertyPrecision()
{
    setConstraints(&PrecisionStandard);
}

PropertyPrecision::~PropertyPrecision() = default;


//**************************************************************************
// PropertyFloatList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFloatList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction


PropertyFloatList::PropertyFloatList() = default;

PropertyFloatList::~PropertyFloatList() = default;

//**************************************************************************
// Base class implementer

PyObject *PropertyFloatList::getPyObject()
{
    PyObject* list = PyList_New(getSize());
    for (int i = 0;i<getSize(); i++)
         PyList_SetItem( list, i, PyFloat_FromDouble(_lValueList[i]));
    return list;
}

double PropertyFloatList::getPyValue(PyObject *item) const {
    if (PyFloat_Check(item)) {
        return PyFloat_AsDouble(item);
    } else if (PyLong_Check(item)) {
        return static_cast<double>(PyLong_AsLong(item));
    } else {
        std::string error = std::string("type in list must be float, not ");
        error += item->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

bool PropertyFloatList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(auto &v : _lValueList)
        writer.Stream() << v << '\n';
    return false;
}

void PropertyFloatList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");
    std::vector<double> values(count);
    auto &s = reader.beginCharStream();
    for(int i=0;i<count;++i)
        s >> values[i];
    setValues(std::move(values));
}

void PropertyFloatList::saveStream(Base::OutputStream &str) const
{
    if (!isSinglePrecision()) {
        for (double it : _lValueList) {
            str << it;
        }
    }
    else {
        for (double it : _lValueList) {
            float v = static_cast<float>(it);
            str << v;
        }
    }
}

void PropertyFloatList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<double> values(uCt);
    if (!isSinglePrecision()) {
        for (double & it : values) {
            str >> it;
        }
    }
    else {
        for (double & it : values) {
            float val;
            str >> val;
            it = val;
        }
    }
    setValues(std::move(values));
}

Property *PropertyFloatList::Copy() const
{
    PropertyFloatList *p= new PropertyFloatList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyFloatList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyFloatList&>(from)._lValueList);
}

void PropertyFloatList::interpolateValue(int index, const double &from, const double &to, float t)
{
    if (from != to) {
        set1Value(index, (to - from) * t + from);
    }
}

//**************************************************************************
//**************************************************************************
// _PropertyFloatList (single precision float list)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

PyObject *_PropertyFloatList::getPyObject(void)
{
    PyObject* list = PyList_New(getSize());
    for (int i = 0;i<getSize(); i++)
         PyList_SetItem( list, i, PyFloat_FromDouble(_lValueList[i]));
    return list;
}

float _PropertyFloatList::getPyValue(PyObject *item) const {
    if (PyFloat_Check(item)) {
        return PyFloat_AsDouble(item);
    } else if (PyLong_Check(item)) {
        return static_cast<float>(PyLong_AsLong(item));
    } else {
        std::string error = std::string("type in list must be float, not ");
        error += item->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

bool _PropertyFloatList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(auto &v : _lValueList)
        writer.Stream() << v << '\n';
    return false;
}

void _PropertyFloatList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");
    std::vector<float> values(count);
    auto &s = reader.beginCharStream();
    for(int i=0;i<count;++i)
        s >> values[i];
    setValues(std::move(values));
}

void _PropertyFloatList::saveStream(Base::OutputStream &str) const {
    for (auto &v : _lValueList)
        str << v;
}

void _PropertyFloatList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<float> values(uCt);
    for(auto &v : values)
        str >> v;
    setValues(std::move(values));
}

Property *_PropertyFloatList::Copy(void) const
{
    _PropertyFloatList *p= new _PropertyFloatList();
    p->_lValueList = _lValueList;
    return p;
}

void _PropertyFloatList::Paste(const Property &from)
{
    setValues(dynamic_cast<const _PropertyFloatList&>(from)._lValueList);
}

void _PropertyFloatList::interpolateValue(int index, const float &from, const float &to, float t)
{
    if (from != to) {
        set1Value(index, (to - from) * t + from);
    }
}

//**************************************************************************
// PropertyString
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyString , App::Property)

PropertyString::PropertyString() = default;

PropertyString::~PropertyString() = default;

void PropertyString::setValue(const char* newLabel)
{
    if(!newLabel)
        return;

    if(_cValue == newLabel)
        return;

    std::string _newLabel;

    std::vector<std::pair<Property*,std::unique_ptr<Property> > > propChanges;
    std::string label;
    auto obj = dynamic_cast<DocumentObject*>(getContainer());
    bool commit = false;

    if(obj && obj->isAttachedToDocument() && this==&obj->Label &&
       (!obj->getDocument()->testStatus(App::Document::Restoring)||
        obj->getDocument()->testStatus(App::Document::Importing)) &&
       // a progressive import keeps duplicate labels exactly like the
       // synchronous importer's Restoring-guarded object creation does
       !obj->getDocument()->testStatus(App::Document::LiveImport) &&
       !obj->getDocument()->isPerformingTransaction())
    {
        // Special handling on importing. If the imported label starts with the
        // same base with the object's internal name, change the label to start
        // with the internal name.
        if (newLabel && obj->getDocument()->testStatus(App::Document::Importing)) {
            int len = 0;
            const char *a=newLabel, *b=obj->getNameInDocument();
            for (; *a && *b; ++a, ++b) {
                unsigned char ac = static_cast<unsigned char>(*a);
                unsigned char bc = static_cast<unsigned char>(*b);
                if (ac == bc) {
                    if(ac == '_' || std::isalpha(ac))
                        continue;
                    if (std::isdigit(ac)) {
                        len = a - newLabel;
                        break;
                    }
                } else {
                    if (std::isdigit(bc) && ac != '_' && !std::isalpha(ac))
                        len = a - newLabel;
                    break;
                }
            }
            if (!len && *b == 0) {
                unsigned char ac = static_cast<unsigned char>(*a);
                if (ac == 0 || (ac != '_' && !std::isalpha(ac)))
                    len = a - newLabel;
            }
            if (len) {
                for (; newLabel[len] && std::isdigit(newLabel[len]); ++len);
                _newLabel = obj->getNameInDocument();
                _newLabel += newLabel + len;
                newLabel = _newLabel.c_str();
            }
        }

        // allow object to control label change

        App::Document* doc = obj->getDocument();
        if(doc && !DocumentParams::getDuplicateLabels() && !obj->allowDuplicateLabel()) {
            // Only a label that is already taken needs any work here, and
            // answering that compares in place: collecting every label into a
            // vector first costs a string copy per object in the document,
            // paid on every label anyone sets. An import that names 13636
            // objects spent seconds of its time on those copies.
            const std::vector<App::DocumentObject*> &objs = doc->getObjects();
            bool match = false;
            for (auto it : objs) {
                if (it != obj  // don't compare object with itself
                        && strcmp(it->Label.getValue(), newLabel) == 0) {
                    match = true;
                    break;
                }
            }

            // make sure that there is a name conflict otherwise we don't have to do anything
            if (match && *newLabel) {
                label = newLabel;
                // remove number from end to avoid lengthy names
                size_t lastpos = label.length()-1;
                while (label[lastpos] >= 48 && label[lastpos] <= 57) {
                    // if 'lastpos' becomes 0 then all characters are digits. In this case we use
                    // the complete label again
                    if (lastpos == 0) {
                        lastpos = label.length()-1;
                        break;
                    }
                    lastpos--;
                }

                bool changed = false;
                label = label.substr(0,lastpos+1);
                if(label != obj->getNameInDocument()
                        && boost::starts_with(obj->getNameInDocument(),label))
                {
                    // In case the label has the same base name as object's
                    // internal name, use it as the label instead.
                    const char *objName = obj->getNameInDocument();
                    const char *c = &objName[lastpos+1];
                    for(;*c;++c) {
                        if(*c<48 || *c>57)
                            break;
                    }
                    if(*c == 0)
                    {
                        bool nameTaken = false;
                        for (auto it : objs) {
                            if (it != obj && strcmp(it->Label.getValue(), objName) == 0) {
                                nameTaken = true;
                                break;
                            }
                        }
                        if (!nameTaken) {
                            label = obj->getNameInDocument();
                            changed = true;
                        }
                    }
                }
                if(!changed) {
                    auto it = objs.begin();
                    auto next = [&]() -> const char * {
                        for (; it != objs.end(); ) {
                            auto o = *it++;
                            if (o != obj)
                                return o->Label.getValue();
                        }
                        return nullptr;
                    };
                    label = Base::Tools::getUniqueName(label, next, 3);
                }
            }
        }

        if(label.empty())
            label = newLabel;
        obj->onBeforeChangeLabel(label);
        newLabel = label.c_str();

        if(!obj->getDocument()->testStatus(App::Document::Restoring)) {
            // Only update label reference if we are not restoring. When
            // importing (which also counts as restoring), it is possible the
            // new object changes its label. However, we cannot update label
            // references here, because object restoring is not based on
            // dependency order. It can only be done in afterRestore().
            //
            // See PropertyLinkBase::restoreLabelReference() for more details.
            propChanges = PropertyLinkBase::updateLabelReferences(obj,newLabel);
        }

        if(!propChanges.empty() && !GetApplication().getActiveTransaction()) {
            commit = true;
            std::ostringstream str;
            str << "Change " << obj->getNameInDocument() << ".Label";
            GetApplication().setActiveTransaction(str.str().c_str());
        }
    }

    aboutToSetValue();
    _cValue = newLabel;
    hasSetValue();

    for(auto &change : propChanges)
        change.first->Paste(*change.second.get());

    if(commit)
        GetApplication().closeActiveTransaction();
}

void PropertyString::setValue(const std::string &sString)
{
    setValue(sString.c_str());
}

const char* PropertyString::getValue() const
{
    return _cValue.c_str();
}

PyObject *PropertyString::getPyObject()
{
    PyObject *p = PyUnicode_DecodeUTF8(_cValue.c_str(),_cValue.size(),nullptr);
    if (!p) throw Base::UnicodeError("UTF8 conversion failure at PropertyString::getPyObject()");
    return p;
}

void PropertyString::setPyObject(PyObject *value)
{
    std::string string;
    if (PyUnicode_Check(value)) {
        string = PyUnicode_AsUTF8(value);
    }
    else {
        try {
            string = Py::Object(value).as_string();
        } catch (Py::Exception &) {
            Base::PyException::ThrowException();
        }
    }

    // assign the string
    setValue(string);
}

void PropertyString::Save (Base::Writer &writer) const
{
    std::string val;
    auto obj = dynamic_cast<DocumentObject*>(getContainer());
    writer.Stream() << writer.ind() << "<String ";
    bool exported = false;
    if(obj && obj->isAttachedToDocument() &&
       obj->isExporting() && &obj->Label==this)
    {
        if(obj->allowDuplicateLabel())
            writer.Stream() <<"restore=\"1\" ";
        else if(_cValue==obj->getNameInDocument()) {
            writer.Stream() <<"restore=\"0\" ";
            val = encodeAttribute(obj->getExportName());
            exported = true;
        }
    }
    if(!exported)
        val = encodeAttribute(_cValue);
    writer.Stream() <<"value=\"" << val <<"\"/>\n";
}

void PropertyString::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("String");
    // get the value of my Attribute
    auto obj = dynamic_cast<DocumentObject*>(getContainer());
    if(obj && &obj->Label==this) {
        if(reader.hasAttribute("restore")) {
            int restore = reader.getAttributeAsInteger("restore");
            if(restore == 1) {
                aboutToSetValue();
                _cValue = reader.getAttribute("value");
                hasSetValue();
            }else
                setValue(reader.getName(reader.getAttribute("value")));
        } else
            setValue(reader.getAttribute("value"));
    }else
        setValue(reader.getAttribute("value"));
}

Property *PropertyString::Copy() const
{
    PropertyString *p= new PropertyString();
    p->_cValue = _cValue;
    return p;
}

void PropertyString::Paste(const Property &from)
{
    setValue(dynamic_cast<const PropertyString&>(from)._cValue);
}

unsigned int PropertyString::getMemSize () const
{
    return static_cast<unsigned int>(_cValue.size());
}

void PropertyString::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    verifyPath(path);
    if (value.type() == typeid(bool))
        setValue(App::any_cast<bool>(value)?"True":"False");
    else if (value.type() == typeid(int))
        setValue(std::to_string(App::any_cast<int>(value)));
    else if (value.type() == typeid(long))
        setValue(std::to_string(App::any_cast<long>(value)));
    else if (value.type() == typeid(double))
        setValue(std::to_string(App::any_cast<double>(value)));
    else if (value.type() == typeid(float))
        setValue(std::to_string(App::any_cast<float>(value)));
    else if (value.type() == typeid(Quantity))
        setValue(App::any_cast<Quantity>(value).getUserString());
    else if (value.type() == typeid(std::string))
        setValue(App::any_cast<const std::string &>(value));
    else {
        Base::PyGILStateLocker lock;
        setValue(pyObjectFromAny(value).as_string());
    }
}

App::any PropertyString::getPathValue(const ObjectIdentifier &path) const
{
    verifyPath(path);
    return _cValue;
}

bool PropertyString::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    // ⚠️ getStrValue(), not getValue(): getValue() hands back a const char*,
    // so comparing two of them compares the addresses of two buffers and says
    // "different" for every pair of strings that are not literally the same
    // object. Every other property here compares a value or a reference, which
    // is why this was the only one that answered no to a string equal to itself.
    return other.isDerivedFrom(PropertyString::getClassTypeId())
        && _cValue == static_cast<const PropertyString&>(other).getStrValue();
}

//**************************************************************************
//**************************************************************************
// PropertyUUID
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyUUID , App::Property)

PropertyUUID::PropertyUUID() = default;

PropertyUUID::~PropertyUUID() = default;

void PropertyUUID::setValue(const Base::Uuid &id)
{
    aboutToSetValue();
    _uuid = id;
    hasSetValue();
}

void PropertyUUID::setValue(const char* sString)
{
    if (sString) {
        aboutToSetValue();
        _uuid.setValue(sString);
        hasSetValue();
    }
}

void PropertyUUID::setValue(const std::string &sString)
{
    aboutToSetValue();
    _uuid.setValue(sString);
    hasSetValue();
}

const std::string& PropertyUUID::getValueStr() const
{
    return _uuid.getValue();
}

const Base::Uuid& PropertyUUID::getValue() const
{
    return _uuid;
}

PyObject *PropertyUUID::getPyObject()
{
    PyObject *p = PyUnicode_FromString(_uuid.getValue().c_str());
    return p;
}

void PropertyUUID::setPyObject(PyObject *value)
{
    std::string string;
    if (PyUnicode_Check(value)) {
        string = PyUnicode_AsUTF8(value);
    }
    else {
        std::string error = std::string("type must be unicode or str, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }

    try {
        // assign the string
        Base::Uuid uid;
        uid.setValue(string);
        setValue(uid);
    }
    catch (const std::exception& e) {
        THROWM(Base::RuntimeError, e.what())
    }
}

void PropertyUUID::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Uuid value=\"" << _uuid.getValue() <<"\"/>\n";
}

void PropertyUUID::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Uuid");
    // get the value of my Attribute
    setValue(reader.getAttribute("value"));
}

Property *PropertyUUID::Copy() const
{
    PropertyUUID *p= new PropertyUUID();
    p->_uuid = _uuid;
    return p;
}

void PropertyUUID::Paste(const Property &from)
{
    aboutToSetValue();
    _uuid = dynamic_cast<const PropertyUUID&>(from)._uuid;
    hasSetValue();
}

unsigned int PropertyUUID::getMemSize () const
{
    return static_cast<unsigned int>(sizeof(_uuid));
}

bool PropertyUUID::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyUUID::getClassTypeId())
        && this->getValue() == static_cast<const PropertyUUID&>(other).getValue();
}

//**************************************************************************
// PropertyFont
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFont , App::PropertyString)

PropertyFont::PropertyFont() = default;

PropertyFont::~PropertyFont() = default;

//**************************************************************************
// PropertyStringList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyStringList , App::PropertyLists)

PropertyStringList::PropertyStringList() = default;

PropertyStringList::~PropertyStringList() = default;

//**************************************************************************
// Base class implementer

void PropertyStringList::setValues(const std::list<std::string>& lValue)
{
    std::vector<std::string> vals;
    vals.reserve(lValue.size());
    for(const auto &v : lValue)
        vals.push_back(v);
    setValues(std::move(vals));
}

PyObject *PropertyStringList::getPyObject()
{
    PyObject* list = PyList_New(getSize());

    for (int i = 0;i<getSize(); i++) {
        PyObject* item = PyUnicode_DecodeUTF8(_lValueList[i].c_str(), _lValueList[i].size(), nullptr);
        if (!item) {
            Py_DECREF(list);
            THROWM(Base::UnicodeError, "UTF8 conversion failure at PropertyStringList::getPyObject()")
        }
        PyList_SetItem(list, i, item);
    }

    return list;
}

std::string PropertyStringList::getPyValue(PyObject *item) const
{
    std::string ret;
    if (PyUnicode_Check(item)) {
        ret = PyUnicode_AsUTF8(item);
    } else if (PyBytes_Check(item)) {
        ret = PyBytes_AsString(item);
    } else {
        std::string error = std::string("type in list must be str or unicode, not ");
        error += item->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
    return ret;
}

unsigned int PropertyStringList::getMemSize () const
{
    size_t size=0;
    for(int i = 0;i<getSize(); i++)
        size += _lValueList[i].size();
    return static_cast<unsigned int>(size);
}

bool PropertyStringList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(int i = 0;i<getSize(); i++) {
        std::string val = encodeAttribute(_lValueList[i]);
        writer.Stream() << "<String value=\"" <<  val <<"\"/>\n";
    }
    return false;
}

void PropertyStringList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");

    std::vector<std::string> values(count);
    for(int i = 0; i < count; i++) {
        reader.readElement("String");
        values[i] = reader.getAttribute("value");
    }
    // assignment
    setValues(std::move(values));
}

Property *PropertyStringList::Copy() const
{
    PropertyStringList *p= new PropertyStringList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyStringList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyStringList&>(from)._lValueList);
}


//**************************************************************************
// PropertyMap
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyMap , App::Property)

PropertyMap::PropertyMap() = default;

PropertyMap::~PropertyMap() = default;

//**************************************************************************
// Base class implementer


int PropertyMap::getSize() const
{
    return static_cast<int>(_lValueList.size());
}

void PropertyMap::setValue(const std::string& key,const std::string& value)
{
    aboutToSetValue();
    _lValueList[key] = value;
    hasSetValue();
}

void PropertyMap::setValue(const char *key, const char *value)
{
    if(!key)
        return;
    if(!value) {
        auto it = _lValueList.find(key);
        if(it == _lValueList.end())
            return;
        aboutToSetValue();
        _lValueList.erase(it);
        hasSetValue();
        return;
    }

    aboutToSetValue();
    _lValueList[key] = value;
    hasSetValue();
}

void PropertyMap::setValues(const std::map<std::string,std::string>& map)
{
    aboutToSetValue();
    _lValueList=map;
    hasSetValue();
}

void PropertyMap::setValues(std::map<std::string,std::string>&& map)
{
    aboutToSetValue();
    _lValueList=std::move(map);
    hasSetValue();
}

const std::string& PropertyMap::operator[] (const std::string& key) const
{
    static std::string empty;
    std::map<std::string,std::string>::const_iterator it = _lValueList.find(key);
    if(it!=_lValueList.end())
        return it->second;
    else
        return empty;
}

const char *PropertyMap::getValue(const char *key) const {
    if(!key)
        return 0;
    auto it = _lValueList.find(key);
    if(it == _lValueList.end())
        return 0;
    return it->second.c_str();
}

PyObject *PropertyMap::getPyObject()
{
    PyObject* dict = PyDict_New();

    for (std::map<std::string,std::string>::const_iterator it = _lValueList.begin();it!= _lValueList.end(); ++it) {
        PyObject* item = PyUnicode_DecodeUTF8(it->second.c_str(), it->second.size(), nullptr);
        if (!item) {
            Py_DECREF(dict);
            THROWM(Base::UnicodeError, "UTF8 conversion failure at PropertyMap::getPyObject()")
        }
        PyDict_SetItemString(dict,it->first.c_str(),item);
        Py_DECREF(item);
    }

    return dict;
}

void PropertyMap::setPyObject(PyObject *value)
{
    if (PyDict_Check(value)) {

        std::map<std::string,std::string> values;
        // get key and item list
        PyObject* keyList = PyDict_Keys(value);

        PyObject* itemList = PyDict_Values(value);
        Py_ssize_t nSize = PyList_Size(keyList);

        for (Py_ssize_t i=0; i<nSize;++i) {

            // check on the key:
            std::string keyStr;
            PyObject* key = PyList_GetItem(keyList, i);
            if (PyUnicode_Check(key)) {
                keyStr = PyUnicode_AsUTF8(key);
            }
            else {
                std::string error = std::string("type of the key need to be unicode or string, not");
                error += key->ob_type->tp_name;
                THROWM(Base::TypeError, error)
            }

            // check on the item:
            PyObject* item = PyList_GetItem(itemList, i);
            if (PyUnicode_Check(item)) {
                values[keyStr] = PyUnicode_AsUTF8(item);
            }
            else {
                std::string error = std::string("type in list must be string or unicode, not ");
                error += item->ob_type->tp_name;
                THROWM(Base::TypeError, error)
            }
        }
        
        setValues(std::move(values));
    }
    else {
        std::string error = std::string("type must be a dict object");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

unsigned int PropertyMap::getMemSize () const
{
    size_t size=0;
    for (const auto & it : _lValueList) {
        size += it.second.size();
        size += it.first.size();
    }
    return size;
}

void PropertyMap::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Map count=\"" <<  getSize() <<"\">\n";
    for (const auto & it : _lValueList) {
        writer.Stream() << "<Item key=\"" << encodeAttribute(it.first)
                        <<"\" value=\"" << encodeAttribute(it.second) <<"\"/>\n";
    }
    writer.Stream() << writer.ind() << "</Map>\n";
}

void PropertyMap::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Map");
    // get the value of my Attribute
    int count = reader.getAttributeAsInteger("count");

    std::map<std::string,std::string> values;
    for(int i = 0; i < count; i++) {
        reader.readElement("Item");
        values[reader.getAttribute("key")] = reader.getAttribute("value");
    }

    reader.readEndElement("Map");

    // assignment
    setValues(values);
}

Property *PropertyMap::Copy() const
{
    PropertyMap *p= new PropertyMap();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyMap::Paste(const Property &from)
{
    aboutToSetValue();
    _lValueList = dynamic_cast<const PropertyMap&>(from)._lValueList;
    hasSetValue();
}

bool PropertyMap::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyMap::getClassTypeId())
        && getValues() == static_cast<const PropertyMap &>(other).getValues();
}


//**************************************************************************
//**************************************************************************
// PropertyBool
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyBool , App::Property)

//**************************************************************************
// Construction/Destruction

PropertyBool::PropertyBool()
{
    _lValue = false;
}

PropertyBool::~PropertyBool() = default;

//**************************************************************************
// Setter/getter for the property

void PropertyBool::setValue(bool lValue)
{
    aboutToSetValue();
    _lValue=lValue;
    hasSetValue();
}

bool PropertyBool::getValue() const
{
    return _lValue;
}

PyObject *PropertyBool::getPyObject()
{
    return PyBool_FromLong(_lValue ? 1 : 0);
}

void PropertyBool::setPyObject(PyObject *value)
{
    if (PyBool_Check(value) || PyLong_Check(value)) {
        setValue(Base::asBoolean(value));
    }
    else {
        std::string error = std::string("type must be bool, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyBool::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Bool value=\"" ;
    if (_lValue)
        writer.Stream() << "true" <<"\"/>" ;
    else
        writer.Stream() << "false" <<"\"/>" ;
    writer.Stream() << '\n';
}

void PropertyBool::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("Bool");
    // get the value of my Attribute
    string b = reader.getAttribute("value");
    (b == "true") ? setValue(true) : setValue(false);
}


Property *PropertyBool::Copy() const
{
    PropertyBool *p= new PropertyBool();
    p->_lValue = _lValue;
    return p;
}

void PropertyBool::Paste(const Property &from)
{
    aboutToSetValue();
    _lValue = dynamic_cast<const PropertyBool&>(from)._lValue;
    hasSetValue();
}

void PropertyBool::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    verifyPath(path);

    if (value.type() == typeid(bool))
        setValue(App::any_cast<bool>(value));
    else if (value.type() == typeid(int))
        setValue(App::any_cast<int>(value) != 0);
    else if (value.type() == typeid(long))
        setValue(App::any_cast<long>(value) != 0);
    else if (value.type() == typeid(double))
        setValue(boost::math::round(App::any_cast<double>(value)));
    else if (value.type() == typeid(float))
        setValue(boost::math::round(App::any_cast<float>(value)));
    else if (value.type() == typeid(Quantity))
        setValue(App::any_cast<const Quantity&>(value).getValue() != 0);
    else
        throw bad_cast();
}

App::any PropertyBool::getPathValue(const ObjectIdentifier &path) const
{
    verifyPath(path);

    return _lValue;
}

bool PropertyBool::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyBool::getClassTypeId())
        && this->getValue() == static_cast<const PropertyBool&>(other).getValue();
}

//**************************************************************************
//**************************************************************************
// PropertyBoolList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyBoolList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction


PropertyBoolList::PropertyBoolList() = default;

PropertyBoolList::~PropertyBoolList() = default;

//**************************************************************************
// Base class implementer

PyObject *PropertyBoolList::getPyObject()
{
    PyObject* tuple = PyTuple_New(getSize());
    for(int i = 0;i<getSize(); i++) {
        bool v = _lValueList[i];
        if (v) {
            PyTuple_SetItem(tuple, i, PyBool_FromLong(1));
        }
        else {
            PyTuple_SetItem(tuple, i, PyBool_FromLong(0));
        }
    }
    return tuple;
}

void PropertyBoolList::setPyObject(PyObject *value)
{
    // string is also a sequence and must be treated differently
    std::string str;
    if (PyUnicode_Check(value)) {
        str = PyUnicode_AsUTF8(value);
        boost::dynamic_bitset<> values(str);
        setValues(values);
    }else
        inherited::setPyObject(value);
}

bool PropertyBoolList::getPyValue(PyObject *item) const {
    if (PyBool_Check(item)) {
        return Base::asBoolean(item);
    } else if (PyLong_Check(item)) {
        return (PyLong_AsLong(item) ? true : false);
    } else {
        std::string error = std::string("type in list must be bool or int, not ");
        error += item->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyBoolList::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<BoolList value=\"" ;
    std::string bitset;
    boost::to_string(_lValueList, bitset);
    writer.Stream() << bitset <<"\"/>" ;
    writer.Stream() << '\n';
}

void PropertyBoolList::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("BoolList");
    // get the value of my Attribute
    string str = reader.getAttribute("value");
    boost::dynamic_bitset<> bitset(str);
    setValues(std::move(bitset));
}

Property *PropertyBoolList::Copy() const
{
    PropertyBoolList *p= new PropertyBoolList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyBoolList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyBoolList&>(from)._lValueList);
}

unsigned int PropertyBoolList::getMemSize () const
{
    return static_cast<unsigned int>(_lValueList.size());
}

//**************************************************************************
//**************************************************************************
// PropertyColor
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyColor , App::Property)

//**************************************************************************
// Construction/Destruction

PropertyColor::PropertyColor() = default;

PropertyColor::~PropertyColor() = default;

//**************************************************************************
// Base class implementer

void PropertyColor::setValue(const Color &col)
{
    aboutToSetValue();
    _cCol=col;
    hasSetValue();
}

void PropertyColor::setValue(uint32_t rgba)
{
    aboutToSetValue();
    _cCol.setPackedValue(rgba);
    hasSetValue();
}

void PropertyColor::setValue(float r, float g, float b, float a)
{
    aboutToSetValue();
    _cCol.set(r,g,b,a);
    hasSetValue();
}

const Color& PropertyColor::getValue() const
{
    return _cCol;
}

PyObject *PropertyColor::getPyObject()
{
    PyObject* rgba = PyTuple_New(4);
    PyObject* r = PyFloat_FromDouble(_cCol.r);
    PyObject* g = PyFloat_FromDouble(_cCol.g);
    PyObject* b = PyFloat_FromDouble(_cCol.b);
    PyObject* a = PyFloat_FromDouble(_cCol.a);

    PyTuple_SetItem(rgba, 0, r);
    PyTuple_SetItem(rgba, 1, g);
    PyTuple_SetItem(rgba, 2, b);
    PyTuple_SetItem(rgba, 3, a);

    return rgba;
}

void PropertyColor::setPyObject(PyObject *value)
{
    App::Color cCol;
    if (PyTuple_Check(value) && (PyTuple_Size(value) == 3 || PyTuple_Size(value) == 4) ) {
        PyObject* item;
        item = PyTuple_GetItem(value,0);
        if (PyFloat_Check(item)) {
            cCol.r = (float)PyFloat_AsDouble(item);
            item = PyTuple_GetItem(value,1);
            if (PyFloat_Check(item))
                cCol.g = (float)PyFloat_AsDouble(item);
            else
                THROWM(Base::TypeError, "Type in tuple must be consistent (float)")
            item = PyTuple_GetItem(value,2);
            if (PyFloat_Check(item))
                cCol.b = (float)PyFloat_AsDouble(item);
            else
                THROWM(Base::TypeError, "Type in tuple must be consistent (float)")
            if (PyTuple_Size(value) == 4) {
                item = PyTuple_GetItem(value,3);
                if (PyFloat_Check(item))
                    cCol.a = (float)PyFloat_AsDouble(item);
                else
                    THROWM(Base::TypeError, "Type in tuple must be consistent (float)")
            }
        }
        else if (PyLong_Check(item)) {
            cCol.r = PyLong_AsLong(item)/255.0;
            item = PyTuple_GetItem(value,1);
            if (PyLong_Check(item))
                cCol.g = PyLong_AsLong(item)/255.0;
            else
                THROWM(Base::TypeError, "Type in tuple must be consistent (integer)")
            item = PyTuple_GetItem(value,2);
            if (PyLong_Check(item))
                cCol.b = PyLong_AsLong(item)/255.0;
            else
                THROWM(Base::TypeError, "Type in tuple must be consistent (integer)")
            if (PyTuple_Size(value) == 4) {
                item = PyTuple_GetItem(value,3);
                if (PyLong_Check(item))
                    cCol.a = PyLong_AsLong(item)/255.0;
                else
                    THROWM(Base::TypeError, "Type in tuple must be consistent (integer)")
            }
        }
        else {
            THROWM(Base::TypeError, "Type in tuple must be float or integer")
        }
    }
    else if (PyLong_Check(value)) {
        cCol.setPackedValue(PyLong_AsUnsignedLong(value));
    }
    else {
        std::string error = std::string("type must be integer or tuple of float or tuple integer, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }

    setValue( cCol );
}

namespace {

/** Swap a colour between the two meanings of its alpha component
 *
 * In memory the component is an opacity, as upstream defines it. In a document
 * it is a transparency, in every file written before upstream 1.1 -- which
 * includes every file this fork writes, since it still calls itself 0.22.
 * Nothing else about a colour differs between the two, so this is the whole
 * conversion, and it is its own inverse: the same call serves the way in and
 * the way out, and only which side of Save/Restore it sits on says which.
 */
void convertAlpha(Color &color)
{
    color.a = 1.0F - color.a;
}

/// The same on the packed form, so a value read as an attribute need not be
/// unpacked and repacked to be converted.
unsigned long convertPackedAlpha(unsigned long rgba)
{
    constexpr unsigned long alphaMax = 0xff;
    return (rgba & ~alphaMax) | (alphaMax - (rgba & alphaMax));
}

/** Where a count would be, in the per field doc file encoding
 *
 * The count PropertyLists::SaveDocFile writes ahead of the values is an
 * entry count, and no list has 2^32-1 entries, so the impossible value says
 * "what follows is per field" to a reader that knows the encoding and
 * cannot be mistaken for a list by one that does not. It is only ever
 * written at schema 5 or later, which no reader unaware of it opens anyway.
 */
constexpr uint32_t FieldStreamMarker = 0xffffffff;

/// Whether colours read through this reader are in the other convention
bool restoreConverts(const Base::XMLReader &reader)
{
    return !Base::alphaIsOpacity(reader);
}

/// The same for a property restoring from its own archive entry
bool restoreConverts(const Base::Reader &reader)
{
    return !Base::alphaIsOpacity(reader);
}

/// Whether colours written by this build have to be converted on the way out
bool saveConverts()
{
    return !Base::writerAlphaIsOpacity();
}

/// A colour's packed form as a document stores it
uint32_t packedForSave(const Color &color, bool convert)
{
    unsigned long packed = color.getPackedValue();
    return static_cast<uint32_t>(convert ? convertPackedAlpha(packed) : packed);
}

} // namespace

void PropertyColor::Save (Base::Writer &writer) const
{
    unsigned long rgba = _cCol.getPackedValue();
    if (saveConverts())
        rgba = convertPackedAlpha(rgba);
    writer.Stream() << writer.ind() << "<PropertyColor value=\""
    <<  rgba <<"\"/>\n";
}

void PropertyColor::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyColor");
    // get the value of my Attribute
    unsigned long rgba = reader.getAttributeAsUnsigned("value");
    if (restoreConverts(reader))
        rgba = convertPackedAlpha(rgba);
    setValue(rgba);
}

Property *PropertyColor::Copy() const
{
    PropertyColor *p= new PropertyColor();
    p->_cCol = _cCol;
    return p;
}

void PropertyColor::Paste(const Property &from)
{
    aboutToSetValue();
    _cCol = dynamic_cast<const PropertyColor&>(from)._cCol;
    hasSetValue();
}

bool PropertyColor::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyColor::getClassTypeId())
        && this->getValue() == static_cast<const PropertyColor&>(other).getValue();
}

void PropertyColor::interpolate(const Property &from, const Property &to, float t)
{
    auto fromValue = dynamic_cast<const PropertyColor&>(from)._cCol;
    auto toValue = dynamic_cast<const PropertyColor&>(to)._cCol;
    if (fromValue != toValue) {
        Color c;
        c.r = (toValue.r - fromValue.r) * t + fromValue.r;
        c.g = (toValue.g - fromValue.g) * t + fromValue.g;
        c.b = (toValue.b - fromValue.b) * t + fromValue.b;
        c.a = (toValue.a - fromValue.a) * t + fromValue.a;
        setValue(c);
    }
}

//**************************************************************************
// PropertyColorList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyColorList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction

PropertyColorList::PropertyColorList() = default;

PropertyColorList::~PropertyColorList() = default;

//**************************************************************************
// Base class implementer

PyObject *PropertyColorList::getPyObject()
{
    PyObject* list = PyList_New(getSize());

    for(int i = 0;i<getSize(); i++) {
        PyObject* rgba = PyTuple_New(4);
        PyObject* r = PyFloat_FromDouble(_lValueList[i].r);
        PyObject* g = PyFloat_FromDouble(_lValueList[i].g);
        PyObject* b = PyFloat_FromDouble(_lValueList[i].b);
        PyObject* a = PyFloat_FromDouble(_lValueList[i].a);

        PyTuple_SetItem(rgba, 0, r);
        PyTuple_SetItem(rgba, 1, g);
        PyTuple_SetItem(rgba, 2, b);
        PyTuple_SetItem(rgba, 3, a);

        PyList_SetItem( list, i, rgba );
    }

    return list;
}

Color PropertyColorList::getPyValue(PyObject *item) const {
    PropertyColor col;
    col.setPyObject(item);
    return col.getValue();
}

bool PropertyColorList::saveXML(Base::Writer &writer) const
{
    const bool convert = saveConverts();
    writer.Stream() << ">\n" << std::hex;
    for(const auto &c : _lValueList) {
        unsigned long packed = c.getPackedValue();
        if (convert)
            packed = convertPackedAlpha(packed);
        writer.Stream() << packed << '\n';
    }
    writer.Stream() << std::dec;
    return false;
}

void PropertyColorList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");
    bool convert = restoreConverts(reader);
    std::vector<Color> values(count);
    auto &s = reader.beginCharStream() >> std::hex;
    for(int i=0;i<count;++i) {
        uint32_t v;
        s >> v;
        values[i].setPackedValue(v);
        if (convert)
            convertAlpha(values[i]);
    }
    s >> std::dec;
    setValues(std::move(values));
    reader.endCharStream();
}

void PropertyColorList::RestoreDocFile(Base::Reader &reader)
{
    // The values arrive through restoreStream, which is handed a byte stream
    // and no document, so the conversion happens here rather than there --
    // and after the read, because the list is what gets converted, not the
    // bytes.
    //
    // Note that this reads and writes through getValues()/setValues(), which
    // is what lets it serve a subclass whose values live somewhere else --
    // PropertyDiffuseColor keeps them in a ShapeAppearance. Both are virtual
    // for that reason; see App::PropertyListsT::getValues.
    PropertyLists::RestoreDocFile(reader);
    if (!restoreConverts(reader) || !getSize())
        return;
    std::vector<Color> values = getValues();
    for (auto &color : values)
        convertAlpha(color);
    setValues(std::move(values));
}

void PropertyColorList::saveStream(Base::OutputStream &str) const
{
    const bool convert = saveConverts();
    for (auto it : _lValueList) {
        unsigned long packed = it.getPackedValue();
        str << static_cast<uint32_t>(convert ? convertPackedAlpha(packed) : packed);
    }
}

void PropertyColorList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Color> values(uCt);
    uint32_t value; // must be 32 bit long
    for (auto & it : values) {
        str >> value;
        it.setPackedValue(value);
    }
    setValues(std::move(values));
}

Property *PropertyColorList::Copy() const
{
    PropertyColorList *p= new PropertyColorList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyColorList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyColorList&>(from)._lValueList);
}

void PropertyColorList::interpolateValue(int index, const Color &from, const Color &to, float t)
{
    if (from != to) {
        Color c;
        c.r = (to.r - from.r) * t + from.r;
        c.g = (to.g - from.g) * t + from.g;
        c.b = (to.b - from.b) * t + from.b;
        c.a = (to.a - from.a) * t + from.a;
        set1Value(index, c);
    }
}

//**************************************************************************
//**************************************************************************
// PropertyAppearance
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyAppearance , App::Property)

PropertyAppearance::PropertyAppearance()
{
    if (DocumentParams::getEnableMaterialEdit())
        setStatus(MaterialEdit, true);
}

PropertyAppearance::~PropertyAppearance() = default;

void PropertyAppearance::setValue(const MaterialAppearance &mat)
{
    aboutToSetValue();
    _cMat=mat;
    hasSetValue();
}

const MaterialAppearance& PropertyAppearance::getValue() const
{
    return _cMat;
}

void PropertyAppearance::setAmbientColor(const Color& col)
{
    aboutToSetValue();
    _cMat.ambientColor = col;
    hasSetValue();
}

void PropertyAppearance::setDiffuseColor(const Color& col)
{
    aboutToSetValue();
    _cMat.diffuseColor = col;
    hasSetValue();
}

void PropertyAppearance::setSpecularColor(const Color& col)
{
    aboutToSetValue();
    _cMat.specularColor = col;
    hasSetValue();
}

void PropertyAppearance::setEmissiveColor(const Color& col)
{
    aboutToSetValue();
    _cMat.emissiveColor = col;
    hasSetValue();
}

void PropertyAppearance::setShininess(float val)
{
    aboutToSetValue();
    _cMat.shininess = val;
    hasSetValue();
}

void PropertyAppearance::setTransparency(float val)
{
    aboutToSetValue();
    _cMat.transparency = val;
    hasSetValue();
}

PyObject *PropertyAppearance::getPyObject()
{
    return new MaterialPy(new MaterialAppearance(_cMat));
}

void PropertyAppearance::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        setValue(*static_cast<MaterialPy*>(value)->getMaterialAppearancePtr());
    }
    else {
        std::string error = std::string("type must be 'Material', not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyAppearance::Save (Base::Writer &writer) const
{
    const bool convert = saveConverts();
    auto packed = [convert](const Color &color) {
        unsigned long value = color.getPackedValue();
        return convert ? convertPackedAlpha(value) : value;
    };
    // The element name stays PropertyMaterial. It is the file format, not
    // the type name: every document ever written says it, and
    // BIM/OfflineRenderingUtils.py parses and generates exactly that tag.
    writer.Stream() << writer.ind() << "<PropertyMaterial ambientColor=\""
        <<  packed(_cMat.ambientColor)
        << "\" diffuseColor=\""  <<  packed(_cMat.diffuseColor)
        << "\" specularColor=\"" <<  packed(_cMat.specularColor)
        << "\" emissiveColor=\"" <<  packed(_cMat.emissiveColor)
        << "\" shininess=\""     <<  _cMat.shininess
        << "\" transparency=\""  <<  _cMat.transparency
        << '"';
    // Attributes, and only when there is one to state: an unfinished
    // material writes exactly the bytes it always did, and a reader that
    // knows nothing of finishes ignores what it does not query -- which is
    // what makes an attribute the right carrier here (8.3, 9.4.2).
    if (_cMat.finish.isSet()) {
        writer.Stream() << " finish=\"" << unsigned(_cMat.finish.pattern)
            << "\" finishPitch=\"" << _cMat.finish.pitch
            << "\" finishDepth=\"" << _cMat.finish.depth
            << "\" finishAngle=\"" << _cMat.finish.angle
            << '"';
    }
    writer.Stream() << "/>\n";
}

void PropertyAppearance::Restore(Base::XMLReader &reader)
{
    // read my Element -- named for what this property used to be called,
    // and frozen there; see Save.
    reader.readElement("PropertyMaterial");
    // get the value of my Attribute
    aboutToSetValue();
    _cMat.ambientColor.setPackedValue(reader.getAttributeAsUnsigned("ambientColor"));
    _cMat.diffuseColor.setPackedValue(reader.getAttributeAsUnsigned("diffuseColor"));
    _cMat.specularColor.setPackedValue(reader.getAttributeAsUnsigned("specularColor"));
    _cMat.emissiveColor.setPackedValue(reader.getAttributeAsUnsigned("emissiveColor"));
    _cMat.shininess = (float)reader.getAttributeAsFloat("shininess");
    _cMat.transparency = (float)reader.getAttributeAsFloat("transparency");
    _cMat.finish.pattern =
        (uint8_t)reader.getAttributeAsUnsigned("finish", "0");
    _cMat.finish.pitch = (float)reader.getAttributeAsFloat("finishPitch", "0");
    _cMat.finish.depth = (float)reader.getAttributeAsFloat("finishDepth", "0");
    _cMat.finish.angle = (float)reader.getAttributeAsFloat("finishAngle", "0");
    _cMat.finish.normalize();
    // Only the colours changed meaning; transparency is transparency in every
    // version that ever wrote this element.
    if (restoreConverts(reader)) {
        convertAlpha(_cMat.ambientColor);
        convertAlpha(_cMat.diffuseColor);
        convertAlpha(_cMat.specularColor);
        convertAlpha(_cMat.emissiveColor);
    }
    hasSetValue();
}

const char* PropertyAppearance::getEditorName() const
{
    if(testStatus(MaterialEdit))
        return "Gui::PropertyEditor::PropertyAppearanceItem";
    return "";
}

Property *PropertyAppearance::Copy() const
{
    PropertyAppearance *p= new PropertyAppearance();
    p->_cMat = _cMat;
    return p;
}

void PropertyAppearance::Paste(const Property &from)
{
    aboutToSetValue();
    _cMat = dynamic_cast<const PropertyAppearance&>(from)._cMat;
    hasSetValue();
}

bool PropertyAppearance::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyAppearance::getClassTypeId())
        && this->getValue() == static_cast<const PropertyAppearance&>(other).getValue();
}


//**************************************************************************
// PropertyAppearanceList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyAppearanceList, App::PropertyLists)


//**************************************************************************
// Construction/Destruction

PropertyAppearanceList::PropertyAppearanceList() = default;

// Releasing the handles is all the content needs -- an undo snapshot or
// another property may still hold the same blob, and the file goes when the
// last handle does. What does need saying is a death mid-restore: a property
// still queued for content it will now never take has to withdraw, or the
// manager dispatches into a dangling referrer.
PropertyAppearanceList::~PropertyAppearanceList()
{
    if (_pendingBlobManager) {
        _pendingBlobManager->removePendingReferrer(this);
    }
    // Every Python view of this property becomes a plain value holding
    // what it can still see -- a pointer's worth of work each. Moved out
    // first: detaching unregisters, which is a write to the vector being
    // walked.
    const std::vector<MaterialListPy *> views = std::move(_views);
    _views.clear();
    for (auto *view : views) {
        view->detachFromOwner();
    }
}

//**************************************************************************
// The value, and the change signalling around it
//
// Everything below is one line of delegation plus the signalling the value
// itself knows nothing about. App::AppearanceList holds the storage and every
// field rule; this class holds the undo record, the touch list and the
// document notification.

const MaterialAppearance &PropertyAppearanceList::defaultMaterial()
{
    return AppearanceList::defaultMaterial();
}

/** Run a write against the value and signal it only if it changed
 *
 * The value is copy-on-write, which is what makes this possible: the
 * snapshot costs a pointer, so the write can simply be MADE and the outcome
 * read off the storage identity -- a setter that decides nothing changed
 * returns without detaching, and the pointer is still the snapshot's.
 *
 * The old value then goes back for exactly as long as it takes to open the
 * atomic change, because aboutToSetValue() is what records the property for
 * undo and it has to see the value the change is FROM. Three pointer
 * assignments, and no per-field "would this change anything" predicate to
 * keep in step with the setter beside it.
 */
template<class Op>
void PropertyAppearanceList::change(Op &&op, int touched)
{
    const AppearanceList before = _list;
    op();
    if (_list.isSameData(before)) {
        return;
    }
    const AppearanceList after = _list;
    _list = before;
    atomic_change guard(*this);
    _list = after;
    if (touched >= 0) {
        _touchList.insert(touched);
    }
    else {
        _touchList.clear();
    }
    guard.tryInvoke();
}

void PropertyAppearanceList::setList(const AppearanceList &list)
{
    change([&] {
        _list = list;
        _list.setBlobManager(&blobManager());
    });
}


void PropertyAppearanceList::setSize(int newSize)
{
    change([&] { _list.setSize(newSize); });
}

void PropertyAppearanceList::setSize(int newSize, const MaterialAppearance &def)
{
    change([&] { _list.setSize(newSize, def); });
}

void PropertyAppearanceList::setValue(const MaterialAppearance &mat)
{
    change([&] { _list.setValue(mat); });
}

void PropertyAppearanceList::setBase(const MaterialAppearance &mat)
{
    change([&] { _list.setBase(mat); });
}

void PropertyAppearanceList::setFollowMaterial(bool enable)
{
    change([&] { _list.setFollowMaterial(enable); });
}

void PropertyAppearanceList::followMaterial(const MaterialAppearance &card)
{
    change([&] { _list.followMaterial(card); });
}

void PropertyAppearanceList::clearOverrides()
{
    change([&] { _list.clearOverrides(); });
}

void PropertyAppearanceList::clearOverride(int idx)
{
    change([&] { _list.clearOverride(idx); }, idx);
}

void PropertyAppearanceList::setValues(const std::vector<MaterialAppearance> &values)
{
    change([&] { _list.setValues(values); });
}

void PropertyAppearanceList::setAmbientColors(const std::vector<Color> &colors)
{
    change([&] { _list.setAmbientColors(colors); });
}

void PropertyAppearanceList::setDiffuseColors(const std::vector<Color> &colors)
{
    change([&] { _list.setDiffuseColors(colors); });
}

void PropertyAppearanceList::setSpecularColors(const std::vector<Color> &colors)
{
    change([&] { _list.setSpecularColors(colors); });
}

void PropertyAppearanceList::setEmissiveColors(const std::vector<Color> &colors)
{
    change([&] { _list.setEmissiveColors(colors); });
}

void PropertyAppearanceList::setShininessValues(const std::vector<float> &values)
{
    change([&] { _list.setShininessValues(values); });
}

void PropertyAppearanceList::setTransparencies(const std::vector<float> &values)
{
    change([&] { _list.setTransparencies(values); });
}

void PropertyAppearanceList::setImages(const std::vector<std::string> &values)
{
    change([&] { _list.setImages(values); });
}

void PropertyAppearanceList::setImagePaths(const std::vector<std::string> &values)
{
    change([&] { _list.setImagePaths(values); });
}

void PropertyAppearanceList::setUuids(const std::vector<std::string> &values)
{
    change([&] { _list.setUuids(values); });
}

void PropertyAppearanceList::setMaterialXs(const std::vector<std::string> &values)
{
    change([&] { _list.setMaterialXs(values); });
}

void PropertyAppearanceList::setFinishes(const std::vector<SurfaceFinish> &values)
{
    change([&] { _list.setFinishes(values); });
}

void PropertyAppearanceList::setTextures(const std::vector<SurfaceTexture> &values)
{
    change([&] { _list.setTextures(values); });
}

void PropertyAppearanceList::setAmbientColor(const Color &col)
{
    change([&] { _list.setAmbientColor(col); });
}

void PropertyAppearanceList::setDiffuseColor(const Color &col)
{
    change([&] { _list.setDiffuseColor(col); });
}

void PropertyAppearanceList::setSpecularColor(const Color &col)
{
    change([&] { _list.setSpecularColor(col); });
}

void PropertyAppearanceList::setEmissiveColor(const Color &col)
{
    change([&] { _list.setEmissiveColor(col); });
}

void PropertyAppearanceList::setDiffuseRGB(const Color &col)
{
    change([&] { _list.setDiffuseRGB(col); });
}

void PropertyAppearanceList::setSpecularRGB(const Color &col)
{
    change([&] { _list.setSpecularRGB(col); });
}

void PropertyAppearanceList::setShininess(float value)
{
    change([&] { _list.setShininess(value); });
}

void PropertyAppearanceList::setTransparency(float value)
{
    change([&] { _list.setTransparency(value); });
}

void PropertyAppearanceList::setImage(const std::string &value)
{
    change([&] { _list.setImage(value); });
}

void PropertyAppearanceList::setImagePath(const std::string &value)
{
    change([&] { _list.setImagePath(value); });
}

void PropertyAppearanceList::setUuid(const std::string &value)
{
    change([&] { _list.setUuid(value); });
}

void PropertyAppearanceList::setMaterialX(const std::string &value)
{
    change([&] { _list.setMaterialX(value); });
}

void PropertyAppearanceList::setFinish(const SurfaceFinish &value)
{
    change([&] { _list.setFinish(value); });
}

void PropertyAppearanceList::setTexture(const SurfaceTexture &value)
{
    change([&] { _list.setTexture(value); });
}

void PropertyAppearanceList::setPBR(bool enable)
{
    change([&] { _list.setPBR(enable); });
}

void PropertyAppearanceList::convertPBR(bool enable)
{
    change([&] { _list.convertPBR(enable); });
}

void PropertyAppearanceList::setMetallicValues(const std::vector<float> &values)
{
    change([&] { _list.setMetallicValues(values); });
}

void PropertyAppearanceList::setRoughnessValues(const std::vector<float> &values)
{
    change([&] { _list.setRoughnessValues(values); });
}

void PropertyAppearanceList::setMetallic(float value)
{
    change([&] { _list.setMetallic(value); });
}

void PropertyAppearanceList::setRoughness(float value)
{
    change([&] { _list.setRoughness(value); });
}

void PropertyAppearanceList::set1Value(int idx, const MaterialAppearance &mat)
{
    change([&] { _list.set1Value(idx, mat); }, idx);
}

void PropertyAppearanceList::setAmbientColor(int idx, const Color &col)
{
    change([&] { _list.setAmbientColor(idx, col); }, idx);
}

void PropertyAppearanceList::setDiffuseColor(int idx, const Color &col)
{
    change([&] { _list.setDiffuseColor(idx, col); }, idx);
}

void PropertyAppearanceList::setSpecularColor(int idx, const Color &col)
{
    change([&] { _list.setSpecularColor(idx, col); }, idx);
}

void PropertyAppearanceList::setEmissiveColor(int idx, const Color &col)
{
    change([&] { _list.setEmissiveColor(idx, col); }, idx);
}

void PropertyAppearanceList::setShininess(int idx, float value)
{
    change([&] { _list.setShininess(idx, value); }, idx);
}

void PropertyAppearanceList::setTransparency(int idx, float value)
{
    change([&] { _list.setTransparency(idx, value); }, idx);
}

void PropertyAppearanceList::setImage(int idx, const std::string &value)
{
    change([&] { _list.setImage(idx, value); }, idx);
}

void PropertyAppearanceList::setImagePath(int idx, const std::string &value)
{
    change([&] { _list.setImagePath(idx, value); }, idx);
}

void PropertyAppearanceList::setUuid(int idx, const std::string &value)
{
    change([&] { _list.setUuid(idx, value); }, idx);
}

void PropertyAppearanceList::setMaterialX(int idx, const std::string &value)
{
    change([&] { _list.setMaterialX(idx, value); }, idx);
}

void PropertyAppearanceList::setFinish(int idx, const SurfaceFinish &value)
{
    change([&] { _list.setFinish(idx, value); }, idx);
}

void PropertyAppearanceList::setTexture(int idx, const SurfaceTexture &value)
{
    change([&] { _list.setTexture(idx, value); }, idx);
}

void PropertyAppearanceList::setMetallic(int idx, float value)
{
    change([&] { _list.setMetallic(idx, value); }, idx);
}

void PropertyAppearanceList::setRoughness(int idx, float value)
{
    change([&] { _list.setRoughness(idx, value); }, idx);
}

MaterialAppearance PropertyAppearanceList::getMaterial(int idx) const
{
    return _list.getMaterial(idx);
}

Color PropertyAppearanceList::getAmbientColor(int idx) const
{
    return _list.getAmbientColor(idx);
}

Color PropertyAppearanceList::getDiffuseColor(int idx) const
{
    return _list.getDiffuseColor(idx);
}

Color PropertyAppearanceList::getSpecularColor(int idx) const
{
    return _list.getSpecularColor(idx);
}

Color PropertyAppearanceList::getEmissiveColor(int idx) const
{
    return _list.getEmissiveColor(idx);
}

float PropertyAppearanceList::getShininess(int idx) const
{
    return _list.getShininess(idx);
}

float PropertyAppearanceList::getTransparency(int idx) const
{
    return _list.getTransparency(idx);
}

const std::string &PropertyAppearanceList::getImage(int idx) const
{
    return _list.getImage(idx);
}

const std::string &PropertyAppearanceList::getImagePath(int idx) const
{
    return _list.getImagePath(idx);
}

const std::string &PropertyAppearanceList::getUuid(int idx) const
{
    return _list.getUuid(idx);
}

const std::string &PropertyAppearanceList::getMaterialX(int idx) const
{
    return _list.getMaterialX(idx);
}

SurfaceFinish PropertyAppearanceList::getFinish(int idx) const
{
    return _list.getFinish(idx);
}

SurfaceTexture PropertyAppearanceList::getTexture(int idx) const
{
    return _list.getTexture(idx);
}

MaterialAppearance::MaterialType PropertyAppearanceList::getType(int idx) const
{
    return _list.getType(idx);
}

float PropertyAppearanceList::getMetallic(int idx) const
{
    return _list.getMetallic(idx);
}

float PropertyAppearanceList::getRoughness(int idx) const
{
    return _list.getRoughness(idx);
}

MaterialAppearance PropertyAppearanceList::getPhongMaterial(int idx) const
{
    return _list.getPhongMaterial(idx);
}

bool PropertyAppearanceList::variesOnlyInDiffuse() const
{
    return _list.variesOnlyInDiffuse();
}


void PropertyAppearanceList::setValues(std::vector<MaterialAppearance> &&values)
{
    setValues(static_cast<const std::vector<MaterialAppearance> &>(values));
}

unsigned int PropertyAppearanceList::getMemSize() const
{
    return _list.getMemSize();
}

bool PropertyAppearanceList::isSame(const Property &other) const
{
    if (&other == this) {
        return true;
    }
    auto list = Base::freecad_dynamic_cast<const PropertyAppearanceList>(&other);
    return list && _list.isSame(list->_list);
}

//**************************************************************************
// The texture maps as stored content
//
// The value holds the content and resolves the slots; what belongs here is
// the document the content goes into and the restore queue, neither of
// which a value can know about.

std::string PropertyAppearanceList::insertTextureFile(const char *path, const char *extension)
{
    _list.setBlobManager(&blobManager());
    return _list.insertTextureFile(path, extension);
}

std::string PropertyAppearanceList::getTextureFile(const std::string &hash) const
{
    return _list.getTextureFile(hash);
}

void PropertyAppearanceList::collectBlobs(FileBlobManager &manager,
                                        const DocumentObject *object) const
{
    _list.noteTextureBlobs(manager, FileBlobManager::referrerOf(this, object));
}

void PropertyAppearanceList::assignRestoredBlob(const FileBlobHandle &blob)
{
    // No value change: this completes the restore of a value the document
    // already had, and touching it here would mark a document modified just
    // by being opened. So it goes straight to the value rather than through
    // change().
    _list.assignRestoredBlob(blob);
    if (blob) {
        // A manifest arriving is the first this list hears of the files it
        // names, so they are asked for now, from the store or the queue.
        const auto manifests = _list.materialXHashes();
        if (std::find(manifests.begin(), manifests.end(), blob->hash()) != manifests.end()) {
            requestMaterialXChildren(blob->hash());
        }
    }
    if (_list.holdsEveryNamedBlob()) {
        // Nothing is still queued, so nothing has to be withdrawn on the
        // way out
        _pendingBlobManager = nullptr;
    }
}

FileBlobManager &PropertyAppearanceList::blobManager() const
{
    if (auto container = getContainer()) {
        // A view provider answers with the document of the object it
        // presents, which is where its appearance belongs
        if (auto doc = container->getOwnerDocument()) {
            return doc->getFileBlobManager();
        }
    }
    return FileBlobManager::defaultManager();
}


void PropertyAppearanceList::requestMaterialXChildren(const std::string &manifestHash)
{
    FileBlobManager *manager = nullptr;
    for (const auto &hash : _list.materialXChildren(manifestHash)) {
        if (hash.empty() || _list.wd().textureBlobs.find(hash) != _list.wd().textureBlobs.end()) {
            continue;
        }
        if (!manager) {
            manager = &blobManager();
        }
        if (auto blob = manager->find(hash)) {
            _list.wd().textureBlobs[hash] = std::move(blob);
            continue;
        }
        _pendingBlobManager = manager;
        manager->addPendingReferrer(hash, this);
    }
}

void PropertyAppearanceList::requestTextureBlobs()
{
    // The manifests first: one that the store already holds hands over its
    // children at once, one that does not is queued and asks for them when
    // it arrives (assignRestoredBlob).
    for (const auto &hash : _list.materialXHashes()) {
        auto &d = _list.wd();
        if (d.textureBlobs.find(hash) == d.textureBlobs.end()) {
            auto &manager = blobManager();
            if (auto blob = manager.find(hash)) {
                d.textureBlobs[hash] = std::move(blob);
            }
            else {
                _pendingBlobManager = &manager;
                manager.addPendingReferrer(hash, this);
                continue;
            }
        }
        requestMaterialXChildren(hash);
    }
    if (_list.wd().texturePalette.empty()) {
        return;
    }
    FileBlobManager *manager = nullptr;
    for (const auto &value : _list.wd().texturePalette) {
        for (const auto &hash : value.maps) {
            if (hash.empty() || _list.wd().textureBlobs.find(hash) != _list.wd().textureBlobs.end()) {
                continue;
            }
            if (!manager) {
                manager = &blobManager();
            }
            // Once per DISTINCT hash: two slots over one content ask once,
            // and the assignment that answers them is idempotent anyway
            if (auto blob = manager->find(hash)) {
                _list.wd().textureBlobs[hash] = std::move(blob);
                continue;
            }
            _pendingBlobManager = manager;
            manager->addPendingReferrer(hash, this);
        }
    }
}



//**************************************************************************
// Storage




void PropertyAppearanceList::registerView(MaterialListPy *view)
{
    _views.push_back(view);
}

void PropertyAppearanceList::unregisterView(MaterialListPy *view)
{
    _views.erase(std::remove(_views.begin(), _views.end(), view), _views.end());
}

void PropertyAppearanceList::editList(const std::function<void(AppearanceList &)> &op, int touched)
{
    change([&] { op(_list); }, touched);
}

PyObject *PropertyAppearanceList::getPyObject()
{
    // A live view, not a copy of the list: it reads this value and writes
    // through editList(). Fresh each time, because the value it is a view
    // of is this property's and outlives no wrapper.
    _list.setBlobManager(&blobManager());
    auto *view = new MaterialListPy(&_list);
    view->attach(this);
    if (testStatus(App::Property::Immutable) || testStatus(App::Property::ReadOnly)) {
        // A read-only property hands out a value nothing can write through
        view->setConst();
    }
    return view;
}

void PropertyAppearanceList::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(MaterialListPy::Type))) {
        auto *view = static_cast<MaterialListPy *>(value);
        if (view->getOwner() == this) {
            // Its own writes have already landed here; assigning it back
            // is the round trip, not a change
            return;
        }
        // Whatever it was a view of, it is a value from here on -- and
        // this property takes a share of that value, which costs a pointer
        view->detachFromOwner();
        view->resetAttribute();
        setList(view->list());
        return;
    }
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        // One material for the whole list, mode and all
        setValue(*static_cast<MaterialPy*>(value)->getMaterialAppearancePtr());
        return;
    }
    PropertyLists::setPyObject(value);
}

MaterialAppearance PropertyAppearanceList::getPyValue(PyObject *value) const {
    if (PyObject_TypeCheck(value, &(MaterialPy::Type)))
        return *static_cast<MaterialPy*>(value)->getMaterialAppearancePtr();
    else {
        std::string error = std::string("type must be 'Material', not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

void PropertyAppearanceList::setPyValues(const std::vector<PyObject*> &vals,
                                       const std::vector<int> &indices)
{
    // Values first: getPyValue throws on anything that is not a material,
    // and it must throw before the change is signalled
    std::vector<MaterialAppearance> values;
    values.reserve(vals.size());
    for (auto *item : vals)
        values.push_back(getPyValue(item));
    if (indices.empty()) {
        // A whole-list assignment restates the mode from its first entry
        setValues(std::move(values));
        return;
    }
    assert(vals.size() == indices.size());
    atomic_change guard(*this);
    int i = 0;
    for (auto index : indices)
        set1Value(index, values[i++]);
    guard.tryInvoke();
}

unsigned int PropertyAppearanceList::getSaveSize(Base::Writer &writer) const
{
    if (writer.getSchemaVersion() >= 5)
        return getMemSize();
    // The compatible encoding spells out a whole material per entry however
    // little of it the storage holds, so a uniform list of ten thousand
    // faces is four bytes in memory and a quarter of a megabyte on the way
    // out. Weighed as the former it would land inline in Document.xml.
    return static_cast<unsigned int>(_list.rd().count) * (4 * sizeof(uint32_t) + 2 * sizeof(float));
}

//**************************************************************************
// Persistence
//
// Two encodings. The one every FreeCAD reads spells out each entry in full;
// the per field one, written only at a schema that already excludes other
// readers, writes each field once at whatever length it actually has.

bool PropertyAppearanceList::saveXML(Base::Writer &writer) const
{
    _list.ensureNormalized();
    // The per field form also when a string has to survive: the inline form
    // is fork-only at every schema -- upstream's reader looks for a file
    // attribute and ignores a count -- so there is nothing to lose by using
    // the encoding that can carry them, and data to lose by not.
    //
    // A surface finish deliberately does NOT force it. The compatible
    // encodings cannot state one, and rather than give up their
    // compatibility for it, the finish is written beside this property as
    // its own -- see PropertySurfaceFinishList, and section 9.4 of
    // docs/ShapeAppearanceDesign.md.
    if (writer.getSchemaVersion() >= 5 || hasTextureOrCard())
        return saveFieldXML(writer);

    const bool convert = saveConverts();
    writer.Stream() << ">\n" << std::hex;
    if (_list.rd().pbr) {
        // This encoding cannot state the mode, so it states the Phong
        // derivation instead -- the same look an old build should show
        for (int i = 0; i < _list.rd().count; ++i) {
            const MaterialAppearance mat = getPhongMaterial(i);
            writer.Stream() << packedForSave(mat.ambientColor, convert)
                            << ' ' << packedForSave(mat.diffuseColor, convert)
                            << ' ' << packedForSave(mat.specularColor, convert)
                            << ' ' << packedForSave(mat.emissiveColor, convert)
                            << ' ' << mat.shininess
                            << ' ' << mat.transparency
                            << '\n';
        }
        writer.Stream() << std::dec;
        return false;
    }
    for (int i = 0; i < _list.rd().count; ++i) {
        writer.Stream() << packedForSave(getAmbientColor(i), convert)
                        << ' ' << packedForSave(getDiffuseColor(i), convert)
                        << ' ' << packedForSave(getSpecularColor(i), convert)
                        << ' ' << packedForSave(getEmissiveColor(i), convert)
                        << ' ' << getShininess(i)
                        << ' ' << getTransparency(i)
                        << '\n';
    }
    writer.Stream() << std::dec;
    return false;
}

void PropertyAppearanceList::restoreXML(Base::XMLReader &reader)
{
    unsigned uCt = reader.getAttributeAsUnsigned("count");
    const bool convert = restoreConverts(reader);
    // Absent everywhere but a field-encoded PBR list, so this also resets
    // the mode when an old-era element is restored over a PBR property
    _list.wd().pbr = reader.getAttributeAsInteger("pbr", "0") != 0;
    // The same for the follow flag. A file that cannot state it restores
    // false, and the view provider derives it once (MaterialStorage.md
    // 15.4) with both the card and the appearance in hand.
    _list.wd().follow = reader.getAttributeAsInteger("follow", "0") != 0;
    if (reader.hasAttribute("fields")) {
        restoreFieldXML(reader, uCt);
        return;
    }

    auto &s = reader.beginCharStream() >> std::hex;
    std::vector<MaterialAppearance> values(uCt);
    for(auto &m : values) {
        uint32_t ambient,diffuse,specular,emissive;
        s >> ambient >> diffuse >> specular >> emissive >> m.shininess >> m.transparency;
        m.ambientColor.setPackedValue(ambient);
        m.diffuseColor.setPackedValue(diffuse);
        m.specularColor.setPackedValue(specular);
        m.emissiveColor.setPackedValue(emissive);
    }
    s >> std::dec;
    reader.endCharStream();
    _list.restoreValues(std::move(values), convert);
}

void PropertyAppearanceList::saveStream(Base::OutputStream &str) const
{
    _list.ensureNormalized();
    const bool convert = saveConverts();
    if (_list.rd().pbr) {
        // As in saveXML's compatible branch: the Phong derivation, since
        // the mode itself cannot travel here
        for (int i = 0; i < _list.rd().count; ++i) {
            const MaterialAppearance mat = getPhongMaterial(i);
            str << packedForSave(mat.ambientColor, convert);
            str << packedForSave(mat.diffuseColor, convert);
            str << packedForSave(mat.specularColor, convert);
            str << packedForSave(mat.emissiveColor, convert);
            str << mat.shininess;
            str << mat.transparency;
        }
        return;
    }
    for (int i = 0; i < _list.rd().count; ++i) {
        str << packedForSave(getAmbientColor(i), convert);
        str << packedForSave(getDiffuseColor(i), convert);
        str << packedForSave(getSpecularColor(i), convert);
        str << packedForSave(getEmissiveColor(i), convert);
        str << getShininess(i);
        str << getTransparency(i);
    }
}

namespace {

/// The compatible encoding's per-entry sequence, into whole materials whose
/// slots hold exactly what the file states -- landed by restoreValues, which
/// knows what the file's era means by them.
std::vector<MaterialAppearance> parseMaterialStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<MaterialAppearance> values(uCt);
    uint32_t value; // must be 32 bit long
    float valueF;
    for (auto & it : values) {
        str >> value;
        it.ambientColor.setPackedValue(value);
        str >> value;
        it.diffuseColor.setPackedValue(value);
        str >> value;
        it.specularColor.setPackedValue(value);
        str >> value;
        it.emissiveColor.setPackedValue(value);
        str >> valueF;
        it.shininess = valueF;
        str >> valueF;
        it.transparency = valueF;
    }
    return values;
}

} // namespace

void PropertyAppearanceList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    // The generic hook, which no caller with a document reaches (this class
    // overrides RestoreDocFile); a bare stream states no version, and no
    // conversion is the reading that leaves its bytes meaning what they say.
    _list.wd().pbr = false;
    _list.wd().follow = false;
    _list.restoreValues(parseMaterialStream(str, uCt), false);
}

/** The element, and whether it promises a second pass
 *
 * Upstream reads a material list only out of its own archive entry, and
 * knows the strings are there only because the element says version="3".
 * So when there is something to say -- a texture or a material card, which
 * nothing in this fork produces yet -- say it their way, in their file
 * shape, and let their reader have it too. With the strings empty, which is
 * every document today, this writes exactly what it always did.
 */
void PropertyAppearanceList::Save(Base::Writer &writer) const
{
    _list.ensureNormalized();
    // ⭐ The finish goes out ahead of the material element, as an element of
    // its own inside this property's -- as though an older format had kept it
    // in a property beside the appearance and this one had merged it in.
    // Below schema 5 the material encodings are upstream's and cannot state a
    // finish; rather than give up their compatibility for it, this states it
    // alongside. Upstream's reader asks for the material element by name and
    // Base::XMLReader::readElement walks past any element that is not it, so
    // the same file is theirs to open and ours to open back with nothing
    // lost. At schema 5 and above the per field encoding carries the finish
    // itself and nothing extra is written at all.
    if (writer.getSchemaVersion() < 5 && hasFinish()) {
        // Dense: the companion element states one finish per ENTRY, which
        // is what a reader with no notion of a base can use
        PropertySurfaceFinishList carrier;
        carrier.setValue(_list.getFinishes());
        carrier.Save(writer);
    }
    // The texture goes out the same way and for the same reason: below
    // schema 5 the material encodings are upstream's and have nowhere to put
    // one -- so without this a texture would vanish from every document
    // written in upstream's format, which is every document that came from
    // one (Document::Restore keeps a file's own schema) and every one a user
    // caps at 4 to keep readable.
    if (writer.getSchemaVersion() < 5 && hasTexture()) {
        PropertySurfaceTextureList carrier;
        std::vector<SurfaceTexture> palette;
        std::vector<uint16_t> index;
        _list.getTextures(palette, index);
        carrier.setValue(palette, index);
        carrier.Save(writer);
    }
    if (writer.getSchemaVersion() < 5 && hasTextureOrCard() && !writer.isForceXML()
            && canSaveStream(writer)) {
        writer.Stream() << writer.ind() << '<' << xmlName() << " file=\""
                        << (getSize()
                                ? writer.addFile(
                                        getFileName(writer.isPreferBinary() ? ".bin" : ".txt"),
                                        this)
                                : "")
                        << "\" version=\"3\"/>\n";
        return;
    }
    PropertyLists::Save(writer);
}

void PropertyAppearanceList::Restore(Base::XMLReader &reader)
{
    _pendingFinish.clear();
    _pendingTexturePalette.clear();
    _pendingTextureIndex.clear();
    // Scan to the material element exactly as readElement would -- callers do
    // not all arrive positioned on it -- but notice the companion element if
    // it comes past on the way (Save writes it ahead of the material one).
    // ⚠️ Asking readElement for the companion BY NAME is not an option: when
    // it is absent, that scan runs straight past the material element that is
    // there, and the property restores as empty.
    while (true) {
        if (!reader.readNextElement()) {
            // What readElement would have said, and equally non-fatal
            Base::Console().Error("Document XML element '%s' not found\n", xmlName());
            return;
        }
        if (strcmp(reader.localName(), xmlName()) == 0) {
            break;
        }
        if (strcmp(reader.localName(), "SurfaceFinishList") == 0) {
            PropertySurfaceFinishList carrier;
            carrier.RestoreHere(reader);
            _pendingFinish = carrier.takeValues();
        }
        if (strcmp(reader.localName(), "SurfaceTextureList") == 0) {
            PropertySurfaceTextureList carrier;
            carrier.RestoreHere(reader);
            carrier.takeValues(_pendingTexturePalette, _pendingTextureIndex);
        }
    }
    // Remembered for RestoreDocFile, which is called later and separately
    _fileVersion = reader.hasAttribute("version")
        ? static_cast<int>(reader.getAttributeAsInteger("version"))
        : 0;
    std::string file(reader.getAttribute("file", ""));
    if (!file.empty()) {
        // The values arrive in RestoreDocFile, long after this returns, and
        // that read clears the finish field -- so the finish waits for it
        reader.addFile(file.c_str(), this);
        return;
    }
    if (reader.hasAttribute("count")) {
        restoreXML(reader);
    }
    else {
        _list.wd().pbr = false;
        _list.wd().follow = false;
        if (getSize())
            setSize(0);
    }
    applyPendingFinish();
    applyPendingTexture();
}

/** Land a base and the override list a file states
 *
 * The checks are the ones the storage's invariants (12.6) turn into a file
 * format: sorted, unique, in range, and a field line exactly as long as the
 * override list or not there at all. Every one of these numbers came out of
 * a file, so none of them is evidence.
 */
void PropertyAppearanceList::installBase(const MaterialAppearance &base, int8_t type)
{
    AppearanceList::Data &d = _list.wd();
    bool first = true;
    uint32_t last = 0;
    for (uint32_t idx : d.overrides) {
        if (idx >= static_cast<uint32_t>(d.count) || (!first && idx <= last))
            throw Base::FileException("overriding faces are not sorted, or name no entry");
        last = idx;
        first = false;
    }
    const std::size_t n = d.overrides.size();
    auto check = [n](const auto &field) {
        if (!field.empty() && field.size() != n)
            throw Base::FileException("material field length does not match the override list");
    };
    check(d.ambient);
    check(d.diffuse);
    check(d.specular);
    check(d.emissive);
    check(d.shininess);
    check(d.image);
    check(d.imagePath);
    check(d.uuid);
    check(d.materialx);
    check(d.type);
    check(d.finish);
    check(d.textureIndex);
    for (uint16_t slot : d.textureIndex) {
        if (slot >= d.texturePalette.size())
            throw Base::FileException("texture index names no palette entry");
    }
    d.base = base;
    AppearanceList::setMaterialType(d.base, type);
    d.base.transparency = d.base.diffuseColor.transparency();
    d.base.pbr = d.pbr;
    // Stated by the file, so nothing is left for the heuristic to choose
    d.baseDerived = true;
    d.normalized = false;
    _list.normalize();
}

void PropertyAppearanceList::applyPendingFinish()
{
    if (_pendingFinish.empty() || _list.wd().count == 0) {
        _pendingFinish.clear();
        return;
    }
    std::vector<SurfaceFinish> finish;
    finish.swap(_pendingFinish);
    setFinishes(finish);
}

void PropertyAppearanceList::applyPendingTexture()
{
    std::vector<SurfaceTexture> palette;
    std::vector<uint16_t> index;
    palette.swap(_pendingTexturePalette);
    index.swap(_pendingTextureIndex);
    if (!palette.empty() && _list.getSize() != 0) {
        // The check the companion element could not make: it is restored
        // before the list it belongs to has a length
        const int count = _list.getSize();
        if (!index.empty() && static_cast<int>(index.size()) != count)
            throw Base::FileException("texture index length does not match the list");
        // The companion states one record per ENTRY; which of them the
        // object is, is the base's question and setTextures asks it
        std::vector<SurfaceTexture> values;
        values.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const std::size_t slot = index.empty() ? 0 : index[static_cast<std::size_t>(i)];
            values.push_back(slot < palette.size() ? palette[slot] : SurfaceTexture());
        }
        atomic_change guard(*this);
        _list.setTextures(values);
        guard.tryInvoke();
    }
    // Unconditional, because the palette may equally have come from the
    // field encoding, which lands it long before this runs. Whatever put it
    // there, this is the point at which every hash in it is known.
    requestTextureBlobs();
}

void PropertyAppearanceList::SaveDocFile(Base::Writer &writer) const
{
    if (writer.getSchemaVersion() < 5) {
        Base::OutputStream str(writer.Stream(), writer.isPreferBinary());
        str << static_cast<uint32_t>(_list.rd().count);
        saveStream(str);
        if (hasTextureOrCard()) {
            saveStringStream(str);
        }
        return;
    }
    _list.ensureNormalized();
    Base::OutputStream str(writer.Stream(), writer.isPreferBinary());
    str << FieldStreamMarker;
    str << static_cast<uint32_t>(_list.rd().count);
    saveFieldStream(str);
}

void PropertyAppearanceList::RestoreDocFile(Base::Reader &reader)
{
    // Asked of the reader that registered this entry: the entry itself is
    // read after the XML pass and knows no document version.
    const bool legacy = restoreConverts(reader);
    Base::InputStream str(reader, !boost::ends_with(reader.getFileName(), ".txt"));
    uint32_t uCt = 0;
    str >> uCt;
    if (uCt == FieldStreamMarker) {
        str >> uCt;
        restoreFieldStream(str, uCt, legacy);
    }
    else {
        // The compatible stream can state neither a mode nor a follow flag
        _list.wd().pbr = false;
        _list.wd().follow = false;
        _list.restoreValues(parseMaterialStream(str, uCt), legacy);
        // Version 3 is the colours we have always read, followed by three
        // strings per entry. Written by upstream, and by this fork when it
        // has any to write.
        if (_fileVersion >= 3) {
            restoreStringStream(str, uCt);
        }
    }
    // Now that the materials are in: the finish read from the companion
    // element back in Restore, which restoreValues above has just cleared
    applyPendingFinish();
    applyPendingTexture();
}

/// Upstream's second pass: image, imagePath and uuid, entry by entry
void PropertyAppearanceList::saveStringStream(Base::OutputStream &str) const
{
    for (int i = 0; i < _list.rd().count; ++i) {
        str << getImage(i);
        str << getImagePath(i);
        str << getUuid(i);
    }
}

void PropertyAppearanceList::restoreStringStream(Base::InputStream &str, unsigned uCt)
{
    atomic_change guard(*this);
    std::vector<std::string> image(uCt);
    std::vector<std::string> imagePath(uCt);
    std::vector<std::string> uuid(uCt);
    for (unsigned i = 0; i < uCt; ++i) {
        str >> image[i];
        str >> imagePath[i];
        str >> uuid[i];
    }
    // Through the field writes rather than into the storage: these arrive
    // one per entry, over a list the colour pass has already given a base
    _list.setImages(image);
    _list.setImagePaths(imagePath);
    _list.setUuids(uuid);
    guard.tryInvoke();
}

namespace {

/// Which fields a per field encoding carries, in the order it carries them
enum FieldBit {
    FieldAmbient = 1 << 0,
    FieldDiffuse = 1 << 1,
    FieldSpecular = 1 << 2,
    FieldEmissive = 1 << 3,
    FieldShininess = 1 << 4,
    FieldTransparency = 1 << 5,
    FieldType = 1 << 6,
    FieldImage = 1 << 7,
    FieldImagePath = 1 << 8,
    FieldUuid = 1 << 9,
    /// Not a field: the list's PBR mode riding the mask. A bit has no
    /// payload, so a reader that predates it skips nothing and loses
    /// nothing -- it only tests the bits it knows.
    FieldPBR = 1 << 10,
    FieldFinish = 1 << 11,
    FieldTexture = 1 << 12,
    /** The base and the faces that override it
     *
     * ONE bit for both (docs/ShapeAppearanceDesign.md 12.3), because they
     * are never present separately and the mask has sixteen bits in all:
     * the run's head count is the number of overriding faces and its
     * payload is their indices followed by the base. Without this bit every
     * field run is one value per ENTRY, which is what every file written
     * before the base holds.
     */
    FieldBase = 1 << 13,
    /** The base is the object's material card's look, and follows it
     *
     * A flag, not a field -- but unlike FieldPBR, which shipped with this
     * format and is grandfathered into FieldFlags, it writes a run of ZERO
     * bytes. That is what lets a build which predates the bit step over it:
     * it reads a run head it does not understand and skips the length the
     * head states, which is nothing (docs/ShapeAppearanceDesign.md 9.4.2).
     */
    FieldFollow = 1 << 14,
    /** The ESCAPE: an extension mask lives in this bit's run
     *
     * The last bit of the sixteen. Rather than spend it on one more field,
     * its run carries a 32-bit mask of EXTENDED fields, and their runs
     * follow flat after every run this mask names, each behind the same
     * head. A build that predates the bit skips its run by length like any
     * unknown field, stops at the end of the sixteen and never reads the
     * extended runs -- harmless, because RestoreDocFile gives this property
     * an archive file of its own, so the trailing bytes go unread rather
     * than being read as something else (docs/MaterialStorage.md 17.9).
     */
    FieldExtension = 1 << 15,
};

/// Bits of the extension mask (FieldExtension), ascending like the main ones
enum ExtendedField : uint32_t {
    /** The MaterialX manifest hash (MaterialAppearance::materialx)
     *
     * A string field, but not a RunStrings: RunBase is read positionally and
     * cannot grow a member, so this field's run carries the BASE value first
     * and then the overrides' -- self-contained, which also serves the
     * uniform case, where the base value is written as a column of one and
     * the base slot as empty.
     */
    ExtMaterialX = 1u << 0,
};

/** Bits this build knows about
 *
 * A bit outside it is a field a later build added: its run is stepped over
 * by the byte length in its head and the fields around it still land.
 */
constexpr uint16_t KnownFields = FieldAmbient | FieldDiffuse | FieldSpecular
    | FieldEmissive | FieldShininess | FieldTransparency | FieldType
    | FieldImage | FieldImagePath | FieldUuid | FieldPBR | FieldFinish
    | FieldTexture | FieldBase | FieldFollow | FieldExtension;

/** Bits that are flags rather than fields, and so have no run to read
 *
 * FieldPBR alone, and it stays a special case rather than becoming a
 * reserved range: a range would have to be carved out of the same 16 bits
 * the fields grow into, and the byte length in the run head makes it
 * unnecessary. A flag added later writes a run of ZERO bytes, which every
 * reader steps over exactly as it steps over a field it does not know --
 * so the presence of the bit is still the whole value, and no reader has
 * to be told in advance which bits carry a payload.
 */
constexpr uint16_t FieldFlags = FieldPBR;

/** How a run is built, stated ahead of every run
 *
 * The field bits say WHICH fields a file carries; this says HOW each run
 * is shaped. It rides in the run head beside the run's BYTE LENGTH, and
 * between them a reader can get past anything: a field bit it does not
 * know, and -- because the length needs no understanding of the payload at
 * all -- a run SHAPE it does not know either. Without them the encoding is
 * positional and only tolerates unknown fields that happen to sit last,
 * which holds for exactly one round of additions.
 *
 * See docs/ShapeAppearanceDesign.md 9.4.2: this costs 9 bytes per present
 * field, of which there are under a dozen, and cannot be added once a build
 * is published.
 */
enum FieldRunType : uint8_t {
    RunColors = 0,
    RunFloats = 1,
    RunInt8 = 2,
    RunStrings = 3,
    RunFinish = 4,
    RunTexture = 5,
    /// The overriding faces' indices, then every field of the ONE entry
    /// they override
    RunBase = 6,
    /// Nothing at all: the bit that names the run is the whole value, and
    /// the run is there so that a reader which does not know the bit can
    /// step over it
    RunFlag = 7,
    /// The extension mask (FieldExtension): one uint32, whose bits name the
    /// runs that follow every run of the main mask
    RunExtension = 8,
    /// A string field that carries its base value ahead of the column, for
    /// a field RunBase cannot hold (see ExtMaterialX)
    RunStringsBase = 9,
};

/// The records of one finish run. Shared by the material list's per field
/// stream and by PropertySurfaceFinishList, so the two cannot drift apart.
void writeFinishRecords(Base::OutputStream &str, const std::vector<SurfaceFinish> &field)
{
    for (const auto &value : field) {
        str << value.pattern;
        str << value.pitch;
        str << value.depth;
        str << value.angle;
    }
}

void readFinishRecords(Base::InputStream &str, std::vector<SurfaceFinish> &field,
                       uint32_t count)
{
    field.resize(count);
    for (auto &value : field) {
        str >> value.pattern;
        str >> value.pitch;
        str >> value.depth;
        str >> value.angle;
        value.normalize();
    }
}

/** One texture run: the palette and the index, each stating its own length
 *
 * The slot count leads, so that a build with more slots than this one --
 * glTF may yet gain a map -- writes a record this one can still read: the
 * slots it knows land and the rest are dropped, rather than the whole run
 * going out of step. The same reason the run head states a byte length.
 */
void writeTextureRun(Base::OutputStream &str, const std::vector<SurfaceTexture> &palette,
                     const std::vector<uint16_t> &index)
{
    str << static_cast<uint8_t>(SurfaceTexture::SlotCount);
    str << static_cast<uint32_t>(palette.size());
    for (const auto &value : palette) {
        for (const auto &hash : value.maps)
            str << hash;
        str << value.scale[0];
        str << value.scale[1];
        str << value.offset[0];
        str << value.offset[1];
        str << value.rotation;
    }
    str << static_cast<uint32_t>(index.size());
    for (uint16_t slot : index)
        str << slot;
}

/// One string as a hex token, leading space included. Hex because these are
/// paths, identifiers and content hashes: a space would end the token and an
/// angle bracket would end the element. The empty string is a lone '-',
/// which no hex byte can be mistaken for.
void writeHexToken(std::ostream &out, const std::string &value)
{
    out << ' ';
    if (value.empty()) {
        out << '-';
        return;
    }
    out << std::hex;
    for (unsigned char byte : value) {
        out << (byte >> 4) << (byte & 0xf);
    }
    out << std::dec;
}

/// How many whitespace tokens writeTextureTokens writes
std::size_t textureTokenCount(const std::vector<SurfaceTexture> &palette,
                              const std::vector<uint16_t> &index)
{
    // slot count, palette size, the records, index size, the index
    return 2 + palette.size() * (SurfaceTexture::SlotCount + 5) + 1 + index.size();
}

/** The palette and the index as text tokens, each half stating its length
 *
 * Shared by the XML field form's 'x' key and by PropertySurfaceTextureList,
 * so the two cannot drift apart -- the same reason writeFinishRecords is
 * shared. Every token carries its own leading space, so a caller decides
 * what goes ahead of them.
 */
void writeTextureTokens(std::ostream &out, const std::vector<SurfaceTexture> &palette,
                        const std::vector<uint16_t> &index)
{
    // max_digits10, so a value read back is the float that was written
    const auto precision = out.precision(9);
    out << ' ' << static_cast<unsigned>(SurfaceTexture::SlotCount)
        << ' ' << palette.size();
    for (const auto &value : palette) {
        for (const auto &hash : value.maps)
            writeHexToken(out, hash);
        out << ' ' << value.scale[0] << ' ' << value.scale[1]
            << ' ' << value.offset[0] << ' ' << value.offset[1]
            << ' ' << value.rotation;
    }
    out << ' ' << index.size();
    for (uint16_t slot : index)
        out << ' ' << slot;
    out.precision(precision);
}

/// One hex token as its bytes. A lone '-' is the empty string, which no hex
/// byte can be mistaken for.
std::string hexToken(const std::string &token)
{
    if (token == "-")
        return {};
    if (token.size() % 2 != 0)
        throw Base::FileException("odd-length hex in a material string");
    std::string value(token.size() / 2, '\0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<char>(std::stoi(token.substr(i * 2, 2), nullptr, 16));
    }
    return value;
}

/// How many whitespace tokens writeBaseTokens writes
std::size_t baseTokenCount(const MaterialAppearance &base)
{
    std::vector<SurfaceTexture> palette;
    if (!(base.texture == SurfaceTexture()))
        palette.push_back(base.texture);
    // four colours, shininess, type, three strings, four finish numbers
    return 13 + textureTokenCount(palette, std::vector<uint16_t>());
}

/** The base entry as one line's tokens
 *
 * The 'b' key of docs/ShapeAppearanceDesign.md 12.3. Every other key is one
 * field of every override; this is every field of ONE entry, so it states
 * them in the order the keys themselves go out, and the count ahead of it
 * is what lets a reader that does not know the key step over it (9.4.2).
 */
void writeBaseTokens(std::ostream &out, const MaterialAppearance &base, bool convert)
{
    out << std::hex;
    out << ' ' << packedForSave(base.ambientColor, convert)
        << ' ' << packedForSave(base.diffuseColor, convert)
        << ' ' << packedForSave(base.specularColor, convert)
        << ' ' << packedForSave(base.emissiveColor, convert);
    out << std::dec;
    // max_digits10, so a value read back is the float that was written
    const auto precision = out.precision(9);
    out << ' ' << base.shininess;
    out << ' ' << static_cast<int>(base.getType());
    writeHexToken(out, base.image);
    writeHexToken(out, base.imagePath);
    writeHexToken(out, base.uuid);
    out << ' ' << static_cast<unsigned>(base.finish.pattern)
        << ' ' << base.finish.pitch
        << ' ' << base.finish.depth
        << ' ' << base.finish.angle;
    std::vector<SurfaceTexture> palette;
    if (!(base.texture == SurfaceTexture()))
        palette.push_back(base.texture);
    writeTextureTokens(out, palette, std::vector<uint16_t>());
    out.precision(precision);
}

/// A palette length a file states but the index cannot address. Checked
/// before the allocation, not after: the number came out of a file.
void checkPaletteSize(std::size_t size)
{
    if (size > AppearanceList::MaxPaletteSize)
        throw Base::FileException("texture palette is longer than the index can address");
}

/// An index is one slot per entry of the list, or absent because the field
/// is uniform. Nothing else, and checked before the allocation. A negative
/// \a expected is a reader that cannot know yet -- the companion element is
/// restored before the list it belongs to has a length -- and there the
/// material list checks instead (applyPendingTexture).
void checkIndexSize(std::size_t size, int expected)
{
    if (expected >= 0 && size != 0 && size != static_cast<std::size_t>(expected))
        throw Base::FileException("texture index length does not match the list");
}

/// \a count is what the run head said, so the index length can be checked
/// BEFORE it is allocated: both numbers came out of a file
void readTextureRun(Base::InputStream &str, std::vector<SurfaceTexture> &palette,
                    std::vector<uint16_t> &index, int count)
{
    uint8_t slotCount = 0;
    str >> slotCount;
    uint32_t size = 0;
    str >> size;
    checkPaletteSize(size);
    palette.resize(size);
    std::string hash;
    for (auto &value : palette) {
        for (uint8_t slot = 0; slot < slotCount; ++slot) {
            str >> hash;
            if (slot < SurfaceTexture::SlotCount)
                value.maps[slot] = hash;
            // else: a slot this build has no name for, read and dropped
        }
        str >> value.scale[0];
        str >> value.scale[1];
        str >> value.offset[0];
        str >> value.offset[1];
        str >> value.rotation;
        value.normalize();
    }
    uint32_t indexSize = 0;
    str >> indexSize;
    checkIndexSize(indexSize, count);
    index.resize(indexSize);
    for (auto &slot : index)
        str >> slot;
}

/// The same run out of the XML keyed form, whose tokens are text
void readTextureKey(std::istream &s, std::vector<SurfaceTexture> &palette,
                    std::vector<uint16_t> &index, int count)
{
    unsigned slotCount = 0;
    unsigned size = 0;
    if (!(s >> slotCount >> size))
        return;
    checkPaletteSize(size);
    palette.resize(size);
    std::string token;
    for (auto &value : palette) {
        for (unsigned slot = 0; slot < slotCount; ++slot) {
            if (!(s >> token))
                return;
            if (slot < SurfaceTexture::SlotCount)
                value.maps[slot] = hexToken(token);
            // else: a slot this build has no name for, read and dropped
        }
        s >> value.scale[0] >> value.scale[1]
          >> value.offset[0] >> value.offset[1] >> value.rotation;
        value.normalize();
    }
    unsigned indexSize = 0;
    if (!(s >> indexSize))
        return;
    checkIndexSize(indexSize, count);
    // Grown as the tokens actually arrive rather than resized to what the
    // file claims: the claim is not evidence, and the companion element has
    // nothing to check it against
    index.reserve(std::min<std::size_t>(indexSize, 4096));
    unsigned slot = 0;
    for (unsigned i = 0; i < indexSize && (s >> slot); ++i)
        index.push_back(static_cast<uint16_t>(slot));
}

/// The 'b' key back out of the XML form, whose tokens are text. The type
/// comes back separately: MaterialAppearance::setType() would rewrite every colour
/// this has just read.
void readBaseTokens(std::istream &s, MaterialAppearance &base, int8_t &type)
{
    uint32_t packed = 0;
    s >> std::hex;
    s >> packed;
    base.ambientColor.setPackedValue(packed);
    s >> packed;
    base.diffuseColor.setPackedValue(packed);
    s >> packed;
    base.specularColor.setPackedValue(packed);
    s >> packed;
    base.emissiveColor.setPackedValue(packed);
    s >> std::dec;
    s >> base.shininess;
    int value = 0;
    s >> value;
    type = static_cast<int8_t>(value);
    std::string token;
    if (s >> token)
        base.image = hexToken(token);
    if (s >> token)
        base.imagePath = hexToken(token);
    if (s >> token)
        base.uuid = hexToken(token);
    unsigned pattern = 0;
    s >> pattern >> base.finish.pitch >> base.finish.depth >> base.finish.angle;
    base.finish.pattern = static_cast<uint8_t>(pattern);
    base.finish.normalize();
    // A palette of one and no index at all, which is the only shape a
    // single entry's texture can be in
    std::vector<SurfaceTexture> palette;
    std::vector<uint16_t> index;
    readTextureKey(s, palette, index, 0);
    if (!palette.empty())
        base.texture = palette.front();
}

} // namespace

void PropertyAppearanceList::saveFieldStream(Base::OutputStream &str) const
{
    // A list that arrived dense and never had a base chosen gets one here:
    // this encoding STATES the base, so writing a default one would record
    // the answer nobody gave and the heuristic would never run again
    // (docs/ShapeAppearanceDesign.md 12.4). It changes what is stored, not
    // what any entry resolves to.
    _list.ensureBase();
    const AppearanceList::Data &d = _list.rd();
    const MaterialAppearance &def = AppearanceList::defaultMaterial();
    // A list nothing overrides writes what it always wrote: one value per
    // field that differs from what an unstated one reads as. Only a list
    // with overriding faces states a base and an override list at all.
    const bool sparse = !d.overrides.empty();
    std::vector<Color> uAmbient, uDiffuse, uSpecular, uEmissive;
    std::vector<float> uShininess;
    std::vector<int8_t> uType;
    std::vector<std::string> uImage, uImagePath, uUuid, uMaterialX;
    std::vector<SurfaceFinish> uFinish;
    std::vector<SurfaceTexture> uPalette;
    if (!sparse) {
        auto one = [](const auto &value, const auto &fallback, auto &field) {
            if (!(value == fallback))
                field.push_back(value);
        };
        one(d.base.ambientColor, def.ambientColor, uAmbient);
        one(d.base.diffuseColor, AppearanceList::storedDiffuse(def), uDiffuse);
        one(d.base.specularColor, _list.specularDefault(), uSpecular);
        one(d.base.emissiveColor, def.emissiveColor, uEmissive);
        one(d.base.shininess, _list.shininessDefault(), uShininess);
        one(static_cast<int8_t>(d.base.getType()), static_cast<int8_t>(def.getType()), uType);
        one(d.base.image, def.image, uImage);
        one(d.base.imagePath, def.imagePath, uImagePath);
        one(d.base.uuid, def.uuid, uUuid);
        one(d.base.materialx, def.materialx, uMaterialX);
        one(d.base.finish, def.finish, uFinish);
        one(d.base.texture, def.texture, uPalette);
    }
    const std::vector<Color> &ambient = sparse ? d.ambient : uAmbient;
    const std::vector<Color> &diffuse = sparse ? d.diffuse : uDiffuse;
    const std::vector<Color> &specular = sparse ? d.specular : uSpecular;
    const std::vector<Color> &emissive = sparse ? d.emissive : uEmissive;
    const std::vector<float> &shininess = sparse ? d.shininess : uShininess;
    const std::vector<int8_t> &type = sparse ? d.type : uType;
    const std::vector<std::string> &image = sparse ? d.image : uImage;
    const std::vector<std::string> &imagePath = sparse ? d.imagePath : uImagePath;
    const std::vector<std::string> &uuid = sparse ? d.uuid : uUuid;
    const std::vector<std::string> &materialx = sparse ? d.materialx : uMaterialX;
    const std::vector<SurfaceFinish> &finish = sparse ? d.finish : uFinish;
    const std::vector<SurfaceTexture> &palette = sparse ? d.texturePalette : uPalette;
    static const std::vector<uint16_t> noIndex;
    const std::vector<uint16_t> &index = sparse ? d.textureIndex : noIndex;

    // No FieldTransparency: the quantity rides the diffuse alpha (which the
    // conversion below writes to disk AS a transparency). The bit is still
    // understood on the way in, for the files written while it was a field
    // of its own.
    uint16_t mask = 0;
    if (!ambient.empty())   mask |= FieldAmbient;
    if (!diffuse.empty())   mask |= FieldDiffuse;
    if (!specular.empty())  mask |= FieldSpecular;
    if (!emissive.empty())  mask |= FieldEmissive;
    if (!shininess.empty()) mask |= FieldShininess;
    if (!type.empty())      mask |= FieldType;
    if (!image.empty())     mask |= FieldImage;
    if (!imagePath.empty()) mask |= FieldImagePath;
    if (!uuid.empty())      mask |= FieldUuid;
    if (!finish.empty())    mask |= FieldFinish;
    if (!palette.empty())   mask |= FieldTexture;
    if (d.pbr)              mask |= FieldPBR;
    if (sparse)             mask |= FieldBase;
    if (d.follow)           mask |= FieldFollow;
    // The extended fields, behind the escape bit. In the sparse shape the
    // base value rides the field's own run, so the base alone is reason
    // enough for the run to exist.
    uint32_t ext = 0;
    if (!materialx.empty() || (sparse && !d.base.materialx.empty()))
        ext |= ExtMaterialX;
    if (ext)                mask |= FieldExtension;
    str << mask;

    // Runs go out in ascending bit order, each behind a head of its shape,
    // its byte length and its entry count. The length is what a reader that
    // knows neither the field nor the shape steps over (9.4.2), and writing
    // it means building the payload first -- into a scratch stream in the
    // same mode and byte order, so the bytes handed on are exactly the ones
    // the reader would have seen written directly.
    auto writeRun = [&str](uint8_t type, std::size_t count,
                           const std::function<void(Base::OutputStream&)> &payload) {
        std::ostringstream buf(std::ios::out | std::ios::binary);
        Base::OutputStream run(buf, str.isBinary());
        run.setByteOrder(str.byteOrder());
        payload(run);
        const std::string bytes = buf.str();
        str << type;
        str << static_cast<uint32_t>(bytes.size());
        str << static_cast<uint32_t>(count);
        for (char byte : bytes)
            str << byte;
    };

    // The per field encoding is this fork's own, but it is written into the
    // same document as the compatible one and is read back by the same gate,
    // so it stores colours the way that document stores them.
    const bool convert = saveConverts();
    auto writeColors = [&writeRun, convert](const std::vector<Color> &field) {
        if (field.empty())
            return;
        writeRun(RunColors, field.size(), [&field, convert](Base::OutputStream &run) {
            for (const auto &col : field)
                run << packedForSave(col, convert);
        });
    };
    auto writeFloats = [&writeRun](const std::vector<float> &field) {
        if (field.empty())
            return;
        writeRun(RunFloats, field.size(), [&field](Base::OutputStream &run) {
            for (float value : field)
                run << value;
        });
    };

    writeColors(ambient);
    writeColors(diffuse);
    writeColors(specular);
    writeColors(emissive);
    writeFloats(shininess);
    if (!type.empty()) {
        writeRun(RunInt8, type.size(), [&type](Base::OutputStream &run) {
            for (int8_t value : type)
                run << value;
        });
    }
    // std::string over this stream is already a length and its bytes, which
    // is the same shape upstream writes its strings in
    auto writeStrings = [&writeRun](const std::vector<std::string> &field) {
        if (field.empty())
            return;
        writeRun(RunStrings, field.size(), [&field](Base::OutputStream &run) {
            for (const auto &value : field)
                run << value;
        });
    };
    writeStrings(image);
    writeStrings(imagePath);
    writeStrings(uuid);
    if (!finish.empty()) {
        writeRun(RunFinish, finish.size(), [&finish](Base::OutputStream &run) {
            writeFinishRecords(run, finish);
        });
    }
    if (!palette.empty()) {
        // The entry count the head states follows the same rule every other
        // field's does -- one for a list nothing overrides, |overrides| for
        // one with faces of its own -- while the palette and the index
        // state their own lengths inside, because neither is that count.
        writeRun(RunTexture, index.empty() ? 1 : index.size(),
                 [&palette, &index](Base::OutputStream &run) {
                     writeTextureRun(run, palette, index);
                 });
    }
    // Ascending bit order: the base run (13) and then the follow flag (14),
    // whose run is empty because the bit is the whole value
    auto writeFollow = [&writeRun, &d]() {
        if (d.follow)
            writeRun(RunFlag, 0, [](Base::OutputStream &) {});
    };
    // Bit 15 last: the extension mask in its own run, then the extended
    // runs in ascending order of THEIR bits, each behind the same head
    auto writeExtension = [&writeRun, &d, &materialx, ext, sparse]() {
        if (!ext)
            return;
        writeRun(RunExtension, 0, [ext](Base::OutputStream &run) { run << ext; });
        if (ext & ExtMaterialX) {
            writeRun(RunStringsBase, materialx.size(),
                     [&d, &materialx, sparse](Base::OutputStream &run) {
                         run << (sparse ? d.base.materialx : std::string());
                         for (const auto &value : materialx)
                             run << value;
                     });
        }
    };
    if (!sparse) {
        writeFollow();
        writeExtension();
        return;
    }
    // The head count is the number of overriding faces, and the payload is
    // a flags byte, their indices, and the base -- which is one entry, so
    // it holds one of everything, in the order the field runs above go out.
    //
    // The flags ride the PAYLOAD rather than a bit of the mask because the
    // mask has sixteen bits in all and this run already has to be present
    // for anything the flags could say about the base.
    writeRun(RunBase, d.overrides.size(), [&d, convert](Base::OutputStream &run) {
        for (uint32_t idx : d.overrides)
            run << idx;
        run << packedForSave(d.base.ambientColor, convert);
        run << packedForSave(d.base.diffuseColor, convert);
        run << packedForSave(d.base.specularColor, convert);
        run << packedForSave(d.base.emissiveColor, convert);
        run << d.base.shininess;
        run << static_cast<int8_t>(d.base.getType());
        run << d.base.image;
        run << d.base.imagePath;
        run << d.base.uuid;
        writeFinishRecords(run, std::vector<SurfaceFinish>(1, d.base.finish));
        std::vector<SurfaceTexture> one(1, d.base.texture);
        writeTextureRun(run, one, std::vector<uint16_t>());
    });
    writeFollow();
    writeExtension();
}

void PropertyAppearanceList::restoreFieldStream(Base::InputStream &str, unsigned uCt, bool legacy)
{
    atomic_change guard(*this);
    _list.touchFields();
    _touchList.clear();
    _list.wd().count = static_cast<int>(uCt);
    // A run this fork stopped writing but read back from the files written
    // while the quantity was a field of its own; merged into the diffuse
    // alphas below.
    std::vector<float> transparency;

    uint16_t mask = 0;
    str >> mask;
    // Before the fields land: the mode decides what an absent field reads
    // as when the base is derived below
    _list.wd().pbr = (mask & FieldPBR) != 0;

    std::vector<uint32_t>().swap(_list.wd().overrides);
    std::vector<Color>().swap(_list.wd().ambient);
    std::vector<Color>().swap(_list.wd().diffuse);
    std::vector<Color>().swap(_list.wd().specular);
    std::vector<Color>().swap(_list.wd().emissive);
    std::vector<float>().swap(_list.wd().shininess);
    std::vector<std::string>().swap(_list.wd().image);
    std::vector<std::string>().swap(_list.wd().imagePath);
    std::vector<std::string>().swap(_list.wd().uuid);
    std::vector<std::string>().swap(_list.wd().materialx);
    std::vector<int8_t>().swap(_list.wd().type);
    std::vector<SurfaceFinish>().swap(_list.wd().finish);
    std::vector<SurfaceTexture>().swap(_list.wd().texturePalette);
    std::vector<uint16_t>().swap(_list.wd().textureIndex);

    MaterialAppearance base;
    int8_t baseType = static_cast<int8_t>(AppearanceList::defaultMaterial().getType());
    bool follow = false;
    uint32_t ext = 0;
    const bool sparse = (mask & FieldBase) != 0;

    // Ascending bit order, which is the order they were written in, each run
    // behind a head of its shape, its byte length and its entry count. A bit
    // this build does not know is a field added later, and a shape it does
    // not know is a record added later: either way the length says where the
    // run ends, so the fields around it still land
    // (docs/ShapeAppearanceDesign.md 9.4.2).
    for (uint32_t bit = 1; bit <= 0x8000U; bit <<= 1) {
        if ((mask & bit) == 0 || (bit & FieldFlags) != 0)
            continue;   // a flag bit carries no run to read
        uint8_t type = 0;
        str >> type;
        uint32_t bytes = 0;
        str >> bytes;
        uint32_t count = 0;
        str >> count;

        // Reading a run head is not the same as understanding the run; this
        // is what gets past the ones that are not understood
        auto skipRun = [&str, bytes]() {
            char byte = 0;
            for (uint32_t i = 0; i < bytes; ++i)
                str >> byte;
        };
        if ((bit & KnownFields) == 0) {
            skipRun();   // a field added by a later build
            continue;
        }
        // The exact length is checked once the whole element is in, by
        // installBase or by the dense adoption; this only keeps a number out
        // of a file from sizing an allocation
        if (bit != FieldBase && count != 1 && count > uCt)
            throw Base::FileException("material field length does not match the list");

        std::vector<Color> colors;
        std::vector<float> floats;
        std::vector<int8_t> int8s;
        std::vector<std::string> strings;
        std::vector<SurfaceFinish> finishes;
        std::vector<SurfaceTexture> palette;
        std::vector<uint16_t> index;
        std::vector<uint32_t> indices;
        switch (type) {
        case RunColors: {
            colors.resize(count);
            uint32_t packed = 0;
            for (auto &col : colors) {
                str >> packed;
                col.setPackedValue(packed);
            }
            break;
        }
        case RunFloats:
            floats.resize(count);
            for (auto &value : floats)
                str >> value;
            break;
        case RunInt8:
            int8s.resize(count);
            for (auto &value : int8s)
                str >> value;
            break;
        case RunStrings:
            strings.resize(count);
            for (auto &value : strings)
                str >> value;
            break;
        case RunFinish:
            readFinishRecords(str, finishes, count);
            break;
        case RunTexture:
            readTextureRun(str, palette, index, static_cast<int>(count));
            break;
        case RunFlag:
            break;   // nothing to read: the bit that named it is the value
        case RunExtension:
            str >> ext;   // which extended runs follow the sixteen
            break;
        case RunBase: {
            indices.resize(count);
            for (auto &value : indices)
                str >> value;
            uint32_t packed = 0;
            str >> packed;
            base.ambientColor.setPackedValue(packed);
            str >> packed;
            base.diffuseColor.setPackedValue(packed);
            str >> packed;
            base.specularColor.setPackedValue(packed);
            str >> packed;
            base.emissiveColor.setPackedValue(packed);
            str >> base.shininess;
            str >> baseType;
            str >> base.image;
            str >> base.imagePath;
            str >> base.uuid;
            readFinishRecords(str, finishes, 1);
            if (!finishes.empty())
                base.finish = finishes.front();
            readTextureRun(str, palette, index, 0);
            if (!palette.empty())
                base.texture = palette.front();
            std::vector<SurfaceTexture>().swap(palette);
            std::vector<SurfaceFinish>().swap(finishes);
            break;
        }
        default:
            // A record shape added by a later build. The byte length is
            // exactly what makes this survivable: the field is dropped and
            // the ones after it still land.
            skipRun();
            continue;
        }

        switch (bit) {
        case FieldAmbient: _list.wd().ambient.swap(colors); break;
        case FieldDiffuse: _list.wd().diffuse.swap(colors); break;
        case FieldSpecular: _list.wd().specular.swap(colors); break;
        case FieldEmissive: _list.wd().emissive.swap(colors); break;
        case FieldShininess: _list.wd().shininess.swap(floats); break;
        case FieldTransparency: transparency.swap(floats); break;
        case FieldType: _list.wd().type.swap(int8s); break;
        case FieldImage: _list.wd().image.swap(strings); break;
        case FieldImagePath: _list.wd().imagePath.swap(strings); break;
        case FieldUuid: _list.wd().uuid.swap(strings); break;
        case FieldFinish: _list.wd().finish.swap(finishes); break;
        case FieldTexture:
            _list.wd().texturePalette.swap(palette);
            _list.wd().textureIndex.swap(index);
            break;
        case FieldBase: _list.wd().overrides.swap(indices); break;   // and base above
        case FieldFollow: follow = true; break;   // the bit IS the value
        case FieldExtension: break;   // the mask was read above; its runs come next
        default: break;   // unreachable: an unknown bit was skipped above
        }
    }
    // The extended runs, in ascending order of their bits in the extension
    // mask, each behind the same head as the sixteen above -- and stepped
    // over by the same length when this build knows neither the bit nor the
    // shape.
    for (uint32_t bit = 1; ext && bit != 0; bit <<= 1) {
        if ((ext & bit) == 0)
            continue;
        uint8_t type = 0;
        str >> type;
        uint32_t bytes = 0;
        str >> bytes;
        uint32_t count = 0;
        str >> count;
        auto skipRun = [&str, bytes]() {
            char byte = 0;
            for (uint32_t i = 0; i < bytes; ++i)
                str >> byte;
        };
        if (bit != ExtMaterialX || type != RunStringsBase) {
            skipRun();   // a field or a shape added by a later build
            continue;
        }
        if (count != 1 && count > uCt)
            throw Base::FileException("material field length does not match the list");
        std::string baseValue;
        str >> baseValue;
        std::vector<std::string> strings(count);
        for (auto &value : strings)
            str >> value;
        if (sparse)
            base.materialx = baseValue;
        _list.wd().materialx.swap(strings);
    }
    if (legacy) {
        for (auto *field : {&_list.wd().ambient, &_list.wd().diffuse, &_list.wd().specular, &_list.wd().emissive}) {
            for (auto &color : *field)
                convertAlpha(color);
        }
    }
    _list.wd().follow = follow;
    if (sparse) {
        if (legacy) {
            for (auto *color : {&base.ambientColor, &base.diffuseColor, &base.specularColor,
                                &base.emissiveColor}) {
                convertAlpha(*color);
            }
        }
        installBase(base, baseType);
    }
    else {
        _list.applyRestoredTransparency(transparency, legacy);
        _list.adoptDense();
    }
    guard.tryInvoke();
}

bool PropertyAppearanceList::saveFieldXML(Base::Writer &writer) const
{
    // The mode is an attribute, not a key line in the char stream: an old
    // fork build ignores an attribute it does not query but throws on an
    // unknown field key
    if (_list.rd().pbr)
        writer.Stream() << " pbr=\"1\"";
    // The follow flag rides an attribute for the same reason the mode does:
    // an old fork build ignores an attribute it does not query but throws
    // on an unknown field key
    if (_list.rd().follow)
        writer.Stream() << " follow=\"1\"";
    writer.Stream() << " fields=\"1\">\n";

    // As in saveFieldStream: this encoding states the base, so one is
    // chosen here if nobody chose it earlier
    _list.ensureBase();

    const bool convert = saveConverts();
    auto writeColors = [&writer, convert](char key, const std::vector<Color> &field) {
        if (field.empty())
            return;
        writer.Stream() << key << ' ' << field.size() << std::hex;
        for (const auto &col : field)
            writer.Stream() << ' ' << packedForSave(col, convert);
        writer.Stream() << std::dec << '\n';
    };
    // max_digits10, so that what is read back is the float that was written:
    // a value that does not survive its own round trip cannot be compared
    // byte for byte against a recorded default either
    auto writeFloats = [&writer](char key, const std::vector<float> &field) {
        if (field.empty())
            return;
        const auto precision = writer.Stream().precision(9);
        writer.Stream() << key << ' ' << field.size();
        for (float value : field)
            writer.Stream() << ' ' << value;
        writer.Stream() << '\n';
        writer.Stream().precision(precision);
    };
    auto writeStrings = [&writer](char key, const std::vector<std::string> &field) {
        if (field.empty())
            return;
        writer.Stream() << key << ' ' << field.size();
        for (const auto &value : field)
            writeHexToken(writer.Stream(), value);
        writer.Stream() << '\n';
    };
    // The base value first, then the column, under one key: the 'b' line is
    // positional and cannot grow a token, so this field carries its own
    // base. The count is still the honest token count, which is what lets a
    // reader that does not know the key step over it (9.4.2).
    auto writeMaterialX = [&writer](const std::string &base,
                                    const std::vector<std::string> &field) {
        if (base.empty() && field.empty())
            return;
        writer.Stream() << "m " << (1 + field.size());
        writeHexToken(writer.Stream(), base);
        for (const auto &value : field)
            writeHexToken(writer.Stream(), value);
        writer.Stream() << '\n';
    };
    auto writeTypes = [&writer](const std::vector<int8_t> &field) {
        if (field.empty())
            return;
        writer.Stream() << "y " << field.size();
        for (int8_t value : field)
            writer.Stream() << ' ' << static_cast<int>(value);
        writer.Stream() << '\n';
    };
    // Four tokens per entry, which is why the number after a key counts
    // TOKENS and not entries: it is the only thing that lets a reader step
    // over a key it does not know (9.4.2). Every other field writes one
    // token per entry, so for them the two counts are the same number and
    // nothing about their lines changed.
    auto writeFinishes = [&writer](const std::vector<SurfaceFinish> &field) {
        if (field.empty())
            return;
        const auto precision = writer.Stream().precision(9);
        writer.Stream() << "f " << field.size() * 4;
        for (const auto &value : field) {
            writer.Stream() << ' ' << static_cast<unsigned>(value.pattern)
                            << ' ' << value.pitch
                            << ' ' << value.depth
                            << ' ' << value.angle;
        }
        writer.Stream() << '\n';
        writer.Stream().precision(precision);
    };
    // Self-describing, unlike every other key: the palette and the index
    // state their own lengths, because neither of them is the entry count.
    // The leading number is still the honest token count, which is all a
    // reader that does not know the key needs to step over it (9.4.2).
    auto writeTexture = [&writer](const std::vector<SurfaceTexture> &palette,
                                  const std::vector<uint16_t> &index) {
        if (palette.empty())
            return;
        writer.Stream() << "x " << textureTokenCount(palette, index);
        writeTextureTokens(writer.Stream(), palette, index);
        writer.Stream() << '\n';
    };

    const auto &d = _list.rd();
    if (d.overrides.empty()) {
        // Nothing varies, so there is nothing for a base key to say that
        // the fields do not: this writes the uniform value of each field
        // that differs from what an unstated one reads as, which is byte
        // for byte what this encoding wrote before there was a base at all.
        const MaterialAppearance &def = AppearanceList::defaultMaterial();
        auto one = [](const auto &value, const auto &def) {
            using T = typename std::decay<decltype(value)>::type;
            return value == def ? std::vector<T>() : std::vector<T>(1, value);
        };
        writeColors('a', one(d.base.ambientColor, def.ambientColor));
        writeColors('d', one(d.base.diffuseColor, AppearanceList::storedDiffuse(def)));
        writeColors('s', one(d.base.specularColor, _list.specularDefault()));
        writeColors('e', one(d.base.emissiveColor, def.emissiveColor));
        writeFloats('h', one(d.base.shininess, _list.shininessDefault()));
        writeTypes(one(static_cast<int8_t>(d.base.getType()),
                       static_cast<int8_t>(def.getType())));
        writeStrings('i', one(d.base.image, def.image));
        writeStrings('p', one(d.base.imagePath, def.imagePath));
        writeStrings('u', one(d.base.uuid, def.uuid));
        writeTexture(one(d.base.texture, def.texture), std::vector<uint16_t>());
        writeFinishes(one(d.base.finish, def.finish));
        // Uniform: the value is the column of one, and the base slot is empty
        writeMaterialX(std::string(), one(d.base.materialx, def.materialx));
        return false;
    }

    // The base in full, then the faces that override it and what they hold
    // (docs/ShapeAppearanceDesign.md 12.3). The base goes first, and the
    // override list before the fields, so that a reader knows how long a
    // field line has to be before it reads one.
    writer.Stream() << "b " << baseTokenCount(d.base);
    writeBaseTokens(writer.Stream(), d.base, convert);
    writer.Stream() << '\n';
    writer.Stream() << "o " << d.overrides.size();
    for (uint32_t idx : d.overrides)
        writer.Stream() << ' ' << idx;
    writer.Stream() << '\n';
    writeColors('a', d.ambient);
    writeColors('d', d.diffuse);
    writeColors('s', d.specular);
    writeColors('e', d.emissive);
    writeFloats('h', d.shininess);
    writeTypes(d.type);
    writeStrings('i', d.image);
    writeStrings('p', d.imagePath);
    writeStrings('u', d.uuid);
    writeTexture(d.texturePalette, d.textureIndex);
    writeFinishes(d.finish);
    writeMaterialX(d.base.materialx, d.materialx);
    return false;
}

void PropertyAppearanceList::restoreFieldXML(Base::XMLReader &reader, unsigned uCt)
{
    const bool legacy = restoreConverts(reader);
    atomic_change guard(*this);
    _list.touchFields();
    _touchList.clear();
    _list.wd().count = static_cast<int>(uCt);
    // As in restoreFieldStream: read but no longer written.
    std::vector<float> transparency;
    std::vector<uint32_t>().swap(_list.wd().overrides);
    std::vector<Color>().swap(_list.wd().ambient);
    std::vector<Color>().swap(_list.wd().diffuse);
    std::vector<Color>().swap(_list.wd().specular);
    std::vector<Color>().swap(_list.wd().emissive);
    std::vector<float>().swap(_list.wd().shininess);
    std::vector<std::string>().swap(_list.wd().image);
    std::vector<std::string>().swap(_list.wd().imagePath);
    std::vector<std::string>().swap(_list.wd().uuid);
    std::vector<std::string>().swap(_list.wd().materialx);
    std::vector<int8_t>().swap(_list.wd().type);
    std::vector<SurfaceFinish>().swap(_list.wd().finish);
    std::vector<SurfaceTexture>().swap(_list.wd().texturePalette);
    std::vector<uint16_t>().swap(_list.wd().textureIndex);

    // The base and the override list, which say whether this file is in the
    // sparse shape at all: without an 'o' key every field line is one value
    // per entry, which is what every document written before
    // ShapeAppearanceDesign 12 holds (12.3).
    MaterialAppearance base;
    int8_t baseType = static_cast<int8_t>(AppearanceList::defaultMaterial().getType());
    bool sparse = false;

    auto &s = reader.beginCharStream();
    std::string key;
    while (s >> key) {
        // The number is the count of TOKENS that follow, so that a key this
        // build does not know can still be stepped over. Every field but
        // the finish writes one token per entry, where the two are the same
        // number (docs/ShapeAppearanceDesign.md 9.4.2).
        unsigned tokens = 0;
        if (!(s >> tokens))
            break;

        // How many tokens one entry of this field takes; 0 says the key was
        // added by a later build. Decided BEFORE the length rules below, or
        // a future field wider than one token would throw on them instead of
        // being skipped -- which is the whole point of the token count.
        unsigned stride = 0;
        switch (key[0]) {
        case 'a': case 'd': case 's': case 'e': case 'h': case 't':
        case 'y': case 'i': case 'p': case 'u': case 'o':
            stride = 1;
            break;
        case 'f':
            stride = 4;
            break;
        case 'b':
        case 'x':
        case 'm':
            // Self-describing: the count rules below do not apply, and the
            // stride is only here to say the key IS known
            stride = 1;
            break;
        default:
            break;
        }
        if (stride == 0) {
            // Read it and drop it: a document from a later build must still
            // open (docs/ShapeAppearanceDesign.md 9.4.2).
            std::string token;
            for (unsigned i = 0; i < tokens && (s >> token); ++i) {
            }
            continue;
        }
        if (key[0] == 'b') {
            readBaseTokens(s, base, baseType);
            continue;
        }
        if (key[0] == 'o') {
            // One index per overriding face, which is what every field line
            // after this one is as long as
            if (tokens > uCt)
                throw Base::FileException("more overriding faces than the list has");
            sparse = true;
            auto &overrides = _list.wd().overrides;
            overrides.reserve(tokens);
            uint32_t idx = 0;
            for (unsigned i = 0; i < tokens && (s >> idx); ++i)
                overrides.push_back(idx);
            continue;
        }
        if (key[0] == 'm') {
            // The base value first, then the column (see saveFieldXML)
            if (tokens < 1)
                throw Base::FileException("MaterialX field states no base");
            std::string token;
            if (!(s >> token))
                break;
            if (sparse)
                base.materialx = hexToken(token);
            const unsigned count = tokens - 1;
            if (count != 1 && count > uCt)
                throw Base::FileException("material field length does not match the list");
            auto &field = _list.wd().materialx;
            field.resize(count);
            for (auto &value : field) {
                if (!(s >> token))
                    break;
                value = hexToken(token);
            }
            continue;
        }
        if (key[0] == 'x') {
            // The palette states its own length; the index does not get to,
            // because it is one slot per overriding face -- or, in a file
            // with no base in it, per entry
            readTextureKey(s, _list.wd().texturePalette, _list.wd().textureIndex,
                           sparse ? static_cast<int>(_list.wd().overrides.size())
                                  : static_cast<int>(uCt));
            continue;
        }
        if (tokens % stride != 0)
            throw Base::FileException("material field length is not a whole number of entries");
        const unsigned count = tokens / stride;
        // The exact length is checked below, once the whole element has been
        // read and it is known whether there was an override list at all;
        // this only keeps a number out of a file from sizing an allocation
        if (count != 1 && count > uCt)
            throw Base::FileException("material field length does not match the list");

        auto readColors = [&s, count](std::vector<Color> &field) {
            field.resize(count);
            s >> std::hex;
            uint32_t packed = 0;
            for (auto &col : field) {
                s >> packed;
                col.setPackedValue(packed);
            }
            s >> std::dec;
        };
        auto readFloats = [&s, count](std::vector<float> &field) {
            field.resize(count);
            for (auto &value : field)
                s >> value;
        };

        switch (key[0]) {
        case 'a': readColors(_list.wd().ambient); break;
        case 'd': readColors(_list.wd().diffuse); break;
        case 's': readColors(_list.wd().specular); break;
        case 'e': readColors(_list.wd().emissive); break;
        case 'h': readFloats(_list.wd().shininess); break;
        case 't': readFloats(transparency); break;
        case 'i':
        case 'p':
        case 'u': {
            auto &field = key[0] == 'i' ? _list.wd().image : (key[0] == 'p' ? _list.wd().imagePath : _list.wd().uuid);
            field.resize(count);
            std::string token;
            for (auto &value : field) {
                if (!(s >> token))
                    break;
                value = hexToken(token);
            }
            break;
        }
        case 'y': {
            _list.wd().type.resize(count);
            int value = 0;
            for (auto &entry : _list.wd().type) {
                s >> value;
                entry = static_cast<int8_t>(value);
            }
            break;
        }
        case 'f': {
            _list.wd().finish.resize(count);
            unsigned pattern = 0;
            for (auto &entry : _list.wd().finish) {
                s >> pattern >> entry.pitch >> entry.depth >> entry.angle;
                entry.pattern = static_cast<uint8_t>(pattern);
                entry.normalize();
            }
            break;
        }
        default:
            break;   // unreachable: an unknown key was skipped above
        }
    }
    reader.endCharStream();
    if (legacy) {
        for (auto *field : {&_list.wd().ambient, &_list.wd().diffuse, &_list.wd().specular, &_list.wd().emissive}) {
            for (auto &color : *field)
                convertAlpha(color);
        }
    }
    if (sparse) {
        if (legacy) {
            for (auto *color : {&base.ambientColor, &base.diffuseColor, &base.specularColor,
                                &base.emissiveColor}) {
                convertAlpha(*color);
            }
        }
        installBase(base, baseType);
    }
    else {
        // A transparency field of its own belongs to an era that had no
        // base, so it is only ever merged into a dense diffuse
        _list.applyRestoredTransparency(transparency, legacy);
        _list.adoptDense();
    }
    guard.tryInvoke();
}

const char* PropertyAppearanceList::getEditorName() const
{
    if(testStatus(NoMaterialListEdit))
        return "";
    return "Gui::PropertyEditor::PropertyAppearanceListItem";
}


Property *PropertyAppearanceList::Copy() const
{
    // A pointer, not fourteen vectors. The copy shares this property's
    // storage until either of them writes, which is what makes an undo
    // snapshot of a ten thousand face appearance free.
    auto *p = new PropertyAppearanceList();
    p->_list = _list;
    return p;
}

void PropertyAppearanceList::Paste(const Property &from)
{
    const auto &other = dynamic_cast<const PropertyAppearanceList &>(from);
    change([&] { _list = other._list; });
}

//**************************************************************************
// PropertySurfaceFinishList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Registered with a leading underscore: this is a serialization carrier the
// material list builds on the stack, not a value a user may add
// (App::Property::isInternalType).
TYPESYSTEM_SOURCE_P(App::PropertySurfaceFinishList)
void App::PropertySurfaceFinishList::init()
{
    initSubclass(App::PropertySurfaceFinishList::classTypeId,
                 "App::_PropertySurfaceFinishList", "App::Property",
                 &App::PropertySurfaceFinishList::create);
}

PropertySurfaceFinishList::PropertySurfaceFinishList() = default;

PropertySurfaceFinishList::~PropertySurfaceFinishList() = default;

void PropertySurfaceFinishList::Save(Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<SurfaceFinishList count=\""
                    << _values.size() << "\">\n";
    // max_digits10, so a value read back is the float that was written
    const auto precision = writer.Stream().precision(9);
    for (const auto &value : _values) {
        writer.Stream() << static_cast<unsigned>(value.pattern)
                        << ' ' << value.pitch
                        << ' ' << value.depth
                        << ' ' << value.angle << '\n';
    }
    writer.Stream().precision(precision);
    writer.Stream() << writer.ind() << "</SurfaceFinishList>\n";
}

void PropertySurfaceFinishList::Restore(Base::XMLReader &reader)
{
    reader.readElement("SurfaceFinishList");
    RestoreHere(reader);
}

void PropertySurfaceFinishList::RestoreHere(Base::XMLReader &reader)
{
    const unsigned count = reader.getAttributeAsUnsigned("count");
    std::vector<SurfaceFinish> values(count);
    if (count) {
        auto &s = reader.beginCharStream();
        for (auto &value : values) {
            unsigned pattern = 0;
            s >> pattern >> value.pitch >> value.depth >> value.angle;
            value.pattern = static_cast<uint8_t>(pattern);
            value.normalize();
        }
        reader.endCharStream();
    }
    reader.readEndElement("SurfaceFinishList");
    _values.swap(values);
}

Property *PropertySurfaceFinishList::Copy() const
{
    auto *p = new PropertySurfaceFinishList();
    p->_values = _values;
    return p;
}

void PropertySurfaceFinishList::Paste(const Property &from)
{
    _values = dynamic_cast<const PropertySurfaceFinishList&>(from)._values;
}

bool PropertySurfaceFinishList::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    auto list = Base::freecad_dynamic_cast<const PropertySurfaceFinishList>(&other);
    return list && list->_values == _values;
}

unsigned int PropertySurfaceFinishList::getMemSize() const
{
    return static_cast<unsigned int>(_values.size() * sizeof(SurfaceFinish));
}

PyObject *PropertySurfaceFinishList::getPyObject()
{
    const std::vector<SurfaceFinish> &values = getValues();
    Py::List list(static_cast<int>(values.size()));
    int i = 0;
    for (const auto &value : values) {
        Py::Tuple entry(4);
        entry.setItem(0, Py::String(SurfaceFinish::patternName(value.pattern)));
        entry.setItem(1, Py::Float(value.pitch));
        entry.setItem(2, Py::Float(value.depth));
        entry.setItem(3, Py::Float(value.angle));
        list[i++] = entry;
    }
    return Py::new_reference_to(list);
}

void PropertySurfaceFinishList::setPyObject(PyObject *value)
{
    if (!PySequence_Check(value))
        throw Base::TypeError("a sequence of (pattern, pitch, depth, angle) is required");
    Py::Sequence seq(value);
    std::vector<SurfaceFinish> values;
    values.reserve(seq.size());
    for (int i = 0; i < seq.size(); ++i) {
        Py::Sequence entry(seq[i].ptr());
        if (entry.size() != 4)
            throw Base::ValueError("a surface finish is (pattern, pitch, depth, angle)");
        SurfaceFinish finish;
        Py::Object pattern(entry[0].ptr());
        // The pattern by name, as everywhere else it is spelled; an integer
        // is taken too, which is what a round trip through a raw value does
        finish.pattern = PyNumber_Check(pattern.ptr())
            ? static_cast<uint8_t>(static_cast<long>(Py::Int(pattern)))
            : SurfaceFinish::patternFromName(
                    Py::String(pattern).as_std_string("utf-8").c_str());
        finish.pitch = static_cast<float>(Py::Float(Py::Object(entry[1].ptr())));
        finish.depth = static_cast<float>(Py::Float(Py::Object(entry[2].ptr())));
        finish.angle = static_cast<float>(Py::Float(Py::Object(entry[3].ptr())));
        finish.normalize();
        values.push_back(finish);
    }
    setValue(values);
}

//**************************************************************************
// PropertySurfaceTextureList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Registered with a leading underscore for the same reason the finish
// carrier is: a serialization carrier the material list builds on the
// stack, not a value a user may add (App::Property::isInternalType).
TYPESYSTEM_SOURCE_P(App::PropertySurfaceTextureList)
void App::PropertySurfaceTextureList::init()
{
    initSubclass(App::PropertySurfaceTextureList::classTypeId,
                 "App::_PropertySurfaceTextureList", "App::Property",
                 &App::PropertySurfaceTextureList::create);
}

PropertySurfaceTextureList::PropertySurfaceTextureList() = default;

PropertySurfaceTextureList::~PropertySurfaceTextureList() = default;

void PropertySurfaceTextureList::Save(Base::Writer &writer) const
{
    // count is the PALETTE length, which is what sizes the read; the index
    // states its own length among the tokens, as it does under the 'x' key
    writer.Stream() << writer.ind() << "<SurfaceTextureList count=\""
                    << _palette.size() << "\">\n";
    writeTextureTokens(writer.Stream(), _palette, _index);
    writer.Stream() << '\n' << writer.ind() << "</SurfaceTextureList>\n";
}

void PropertySurfaceTextureList::Restore(Base::XMLReader &reader)
{
    reader.readElement("SurfaceTextureList");
    RestoreHere(reader);
}

void PropertySurfaceTextureList::RestoreHere(Base::XMLReader &reader)
{
    const unsigned count = reader.getAttributeAsUnsigned("count");
    std::vector<SurfaceTexture> palette;
    std::vector<uint16_t> index;
    if (count) {
        auto &s = reader.beginCharStream();
        // The index length is whatever the tokens say here: only the
        // material list knows how many entries it has, so it is the one
        // that checks (applyPendingTexture)
        readTextureKey(s, palette, index, -1);
        reader.endCharStream();
    }
    reader.readEndElement("SurfaceTextureList");
    _palette.swap(palette);
    _index.swap(index);
}

Property *PropertySurfaceTextureList::Copy() const
{
    auto *p = new PropertySurfaceTextureList();
    p->_palette = _palette;
    p->_index = _index;
    return p;
}

void PropertySurfaceTextureList::Paste(const Property &from)
{
    const auto &other = dynamic_cast<const PropertySurfaceTextureList&>(from);
    _palette = other._palette;
    _index = other._index;
}

bool PropertySurfaceTextureList::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    auto list = Base::freecad_dynamic_cast<const PropertySurfaceTextureList>(&other);
    return list && list->_palette == _palette && list->_index == _index;
}

unsigned int PropertySurfaceTextureList::getMemSize() const
{
    return static_cast<unsigned int>(texturesMemSize(_palette)
                                     + _index.size() * sizeof(uint16_t));
}

PyObject *PropertySurfaceTextureList::getPyObject()
{
    Py::List palette(static_cast<int>(_palette.size()));
    int i = 0;
    for (const auto &value : _palette) {
        Py::Dict entry;
        for (uint8_t slot = 0; slot < SurfaceTexture::SlotCount; ++slot) {
            if (!value.maps[slot].empty()) {
                entry.setItem(SurfaceTexture::slotName(slot),
                              Py::String(value.maps[slot]));
            }
        }
        Py::Tuple scale(2);
        scale.setItem(0, Py::Float(value.scale[0]));
        scale.setItem(1, Py::Float(value.scale[1]));
        entry.setItem("Scale", scale);
        Py::Tuple offset(2);
        offset.setItem(0, Py::Float(value.offset[0]));
        offset.setItem(1, Py::Float(value.offset[1]));
        entry.setItem("Offset", offset);
        entry.setItem("Rotation", Py::Float(value.rotation));
        palette[i++] = entry;
    }
    Py::List index(static_cast<int>(_index.size()));
    i = 0;
    for (uint16_t slot : _index)
        index[i++] = Py::Long(static_cast<long>(slot));
    Py::Tuple result(2);
    result.setItem(0, palette);
    result.setItem(1, index);
    return Py::new_reference_to(result);
}

void PropertySurfaceTextureList::setPyObject(PyObject *value)
{
    // A carrier, not a value a script is meant to author: the appearance
    // property is where a texture is set, and the two halves only mean
    // anything together
    (void)value;
    throw Base::AttributeError("the texture carrier is read only; "
                               "set ShapeAppearance instead");
}

//**************************************************************************
// PropertyPersistentObject
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPersistentObject , App::PropertyString)

PyObject *PropertyPersistentObject::getPyObject(){
    if(_pObject)
        return _pObject->getPyObject();
    return inherited::getPyObject();
}

void PropertyPersistentObject::Save(Base::Writer &writer) const{
    inherited::Save(writer);
#define ELEMENT_PERSISTENT_OBJ "PersistentObject"
    writer.Stream() << writer.ind() << "<" ELEMENT_PERSISTENT_OBJ ">" << std::endl;
    if(_pObject) {
        writer.incInd();
        _pObject->Save(writer);
        writer.decInd();
    }
    writer.Stream() << writer.ind() << "</" ELEMENT_PERSISTENT_OBJ ">" << std::endl;
}

void PropertyPersistentObject::Restore(Base::XMLReader &reader){
    inherited::Restore(reader);
    reader.readElement(ELEMENT_PERSISTENT_OBJ);
    if(_pObject)
        _pObject->Restore(reader);
    reader.readEndElement(ELEMENT_PERSISTENT_OBJ);
}

Property *PropertyPersistentObject::Copy() const{
    auto *p= new PropertyPersistentObject();
    p->_cValue = _cValue;
    p->_pObject = _pObject;
    return p;
}

void PropertyPersistentObject::Paste(const Property &from){
    const auto &prop = dynamic_cast<const PropertyPersistentObject&>(from);
    if(_cValue!=prop._cValue || _pObject!=prop._pObject) {
        aboutToSetValue();
        _cValue = prop._cValue;
        _pObject = prop._pObject;
        hasSetValue();
    }
}

bool PropertyPersistentObject::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return false;
}

Property *PropertyPersistentObject::copyBeforeChange() const
{
    return nullptr;
}

unsigned int PropertyPersistentObject::getMemSize () const{
    auto size = inherited::getMemSize();
    if(_pObject)
        size += _pObject->getMemSize();
    return size;
}

void PropertyPersistentObject::setValue(const char *type) {
    if(!type) type = "";
    if(type[0]) {
        Base::Type::importModule(type);
        Base::Type t = Base::Type::fromName(type);
        if(t.isBad())
            THROWM(Base::TypeError, "Invalid type")
        if(!t.isDerivedFrom(Persistence::getClassTypeId()))
            THROWM(Base::TypeError, "Type must be derived from Base::Persistence")
        if(_pObject && _pObject->getTypeId()==t)
            return;
    }
    aboutToSetValue();
    _pObject.reset();
    _cValue = type;
    if(type[0])
        _pObject.reset(static_cast<Base::Persistence*>(Base::Type::createInstanceByName(type)));
    hasSetValue();
}
