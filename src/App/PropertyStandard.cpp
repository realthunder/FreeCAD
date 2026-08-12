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

#include <cctype>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/math/special_functions/round.hpp>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Interpreter.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Quantity.h>
#include <Base/Stream.h>
#include <Base/Tools.h>

#include "PropertyStandard.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentParams.h"
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
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
        throw Base::RuntimeError("Cannot get value from invalid enumeration");
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
    else if (PyTuple_Check(value) && PyTuple_Size(value) == 4) {
        long values[4];
        for (int i=0; i<4; i++) {
            PyObject* item;
            item = PyTuple_GetItem(value,i);
            if (PyLong_Check(item))
                values[i] = PyLong_AsLong(item);
            else
                throw Base::TypeError("Type in tuple must be int");
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
        std::string error = std::string("type must be int, not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
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
    throw Base::TypeError(error);
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
                throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
                throw Base::TypeError("Type in tuple must be float or int");
        }

        double stepSize = values[3];
        // need a value > 0
        if (stepSize < DBL_EPSILON)
            throw Base::ValueError("Step size must be greater than zero");

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
        std::string error = std::string("type must be float, not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
            std::vector<std::string> objectLabels;
            std::vector<App::DocumentObject*>::const_iterator it;
            std::vector<App::DocumentObject*> objs = doc->getObjects();
            bool match = false;
            for (it = objs.begin();it != objs.end();++it) {
                if (*it == obj)
                    continue; // don't compare object with itself
                std::string objLabel = (*it)->Label.getValue();
                if (!match && objLabel == newLabel)
                    match = true;
                objectLabels.push_back(objLabel);
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
                    if(*c == 0 && std::find(objectLabels.begin(), objectLabels.end(),
                                            obj->getNameInDocument())==objectLabels.end())
                    {
                        label = obj->getNameInDocument();
                        changed = true;
                    }
                }
                if(!changed)
                    label = Base::Tools::getUniqueName(label, objectLabels, 3);
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
        setValue(App::any_cast<Quantity>(value).getUserString().toUtf8().constData());
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
        throw Base::TypeError(error);
    }

    try {
        // assign the string
        Base::Uuid uid;
        uid.setValue(string);
        setValue(uid);
    }
    catch (const std::exception& e) {
        throw Base::RuntimeError(e.what());
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
            throw Base::UnicodeError("UTF8 conversion failure at PropertyStringList::getPyObject()");
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
        throw Base::TypeError(error);
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
            throw Base::UnicodeError("UTF8 conversion failure at PropertyMap::getPyObject()");
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
                throw Base::TypeError(error);
            }

            // check on the item:
            PyObject* item = PyList_GetItem(itemList, i);
            if (PyUnicode_Check(item)) {
                values[keyStr] = PyUnicode_AsUTF8(item);
            }
            else {
                std::string error = std::string("type in list must be string or unicode, not ");
                error += item->ob_type->tp_name;
                throw Base::TypeError(error);
            }
        }
        
        setValues(std::move(values));
    }
    else {
        std::string error = std::string("type must be a dict object");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
        throw Base::TypeError(error);
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
                throw Base::TypeError("Type in tuple must be consistent (float)");
            item = PyTuple_GetItem(value,2);
            if (PyFloat_Check(item))
                cCol.b = (float)PyFloat_AsDouble(item);
            else
                throw Base::TypeError("Type in tuple must be consistent (float)");
            if (PyTuple_Size(value) == 4) {
                item = PyTuple_GetItem(value,3);
                if (PyFloat_Check(item))
                    cCol.a = (float)PyFloat_AsDouble(item);
                else
                    throw Base::TypeError("Type in tuple must be consistent (float)");
            }
        }
        else if (PyLong_Check(item)) {
            cCol.r = PyLong_AsLong(item)/255.0;
            item = PyTuple_GetItem(value,1);
            if (PyLong_Check(item))
                cCol.g = PyLong_AsLong(item)/255.0;
            else
                throw Base::TypeError("Type in tuple must be consistent (integer)");
            item = PyTuple_GetItem(value,2);
            if (PyLong_Check(item))
                cCol.b = PyLong_AsLong(item)/255.0;
            else
                throw Base::TypeError("Type in tuple must be consistent (integer)");
            if (PyTuple_Size(value) == 4) {
                item = PyTuple_GetItem(value,3);
                if (PyLong_Check(item))
                    cCol.a = PyLong_AsLong(item)/255.0;
                else
                    throw Base::TypeError("Type in tuple must be consistent (integer)");
            }
        }
        else {
            throw Base::TypeError("Type in tuple must be float or integer");
        }
    }
    else if (PyLong_Check(value)) {
        cCol.setPackedValue(PyLong_AsUnsignedLong(value));
    }
    else {
        std::string error = std::string("type must be integer or tuple of float or tuple integer, not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }

    setValue( cCol );
}

void PropertyColor::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<PropertyColor value=\""
    <<  _cCol.getPackedValue() <<"\"/>\n";
}

void PropertyColor::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyColor");
    // get the value of my Attribute
    unsigned long rgba = reader.getAttributeAsUnsigned("value");
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
    writer.Stream() << ">\n" << std::hex;
    for(const auto &c : _lValueList)
        writer.Stream() << c.getPackedValue() << '\n';
    writer.Stream() << std::dec;
    return false;
}

void PropertyColorList::restoreXML(Base::XMLReader &reader)
{
    int count = reader.getAttributeAsInteger("count");
    std::vector<Color> values(count);
    auto &s = reader.beginCharStream() >> std::hex;
    for(int i=0;i<count;++i) {
        uint32_t v;
        s >> v;
        values[i].setPackedValue(v);
    }
    s >> std::dec;
    setValues(std::move(values));
    reader.endCharStream();
}

void PropertyColorList::saveStream(Base::OutputStream &str) const
{
    for (auto it : _lValueList) {
        str << it.getPackedValue();
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
// PropertyMaterial
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyMaterial , App::Property)

PropertyMaterial::PropertyMaterial()
{
    if (DocumentParams::getEnableMaterialEdit())
        setStatus(MaterialEdit, true);
}

PropertyMaterial::~PropertyMaterial() = default;

void PropertyMaterial::setValue(const Material &mat)
{
    aboutToSetValue();
    _cMat=mat;
    hasSetValue();
}

const Material& PropertyMaterial::getValue() const
{
    return _cMat;
}

void PropertyMaterial::setAmbientColor(const Color& col)
{
    aboutToSetValue();
    _cMat.ambientColor = col;
    hasSetValue();
}

void PropertyMaterial::setDiffuseColor(const Color& col)
{
    aboutToSetValue();
    _cMat.diffuseColor = col;
    hasSetValue();
}

void PropertyMaterial::setSpecularColor(const Color& col)
{
    aboutToSetValue();
    _cMat.specularColor = col;
    hasSetValue();
}

void PropertyMaterial::setEmissiveColor(const Color& col)
{
    aboutToSetValue();
    _cMat.emissiveColor = col;
    hasSetValue();
}

void PropertyMaterial::setShininess(float val)
{
    aboutToSetValue();
    _cMat.shininess = val;
    hasSetValue();
}

void PropertyMaterial::setTransparency(float val)
{
    aboutToSetValue();
    _cMat.transparency = val;
    hasSetValue();
}

PyObject *PropertyMaterial::getPyObject()
{
    return new MaterialPy(new Material(_cMat));
}

void PropertyMaterial::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        setValue(*static_cast<MaterialPy*>(value)->getMaterialPtr());
    }
    else {
        std::string error = std::string("type must be 'Material', not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyMaterial::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<PropertyMaterial ambientColor=\""
        <<  _cMat.ambientColor.getPackedValue()
        << "\" diffuseColor=\""  <<  _cMat.diffuseColor.getPackedValue()
        << "\" specularColor=\"" <<  _cMat.specularColor.getPackedValue()
        << "\" emissiveColor=\"" <<  _cMat.emissiveColor.getPackedValue()
        << "\" shininess=\""     <<  _cMat.shininess
        << "\" transparency=\""  <<  _cMat.transparency
        << "\"/>\n";
}

void PropertyMaterial::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyMaterial");
    // get the value of my Attribute
    aboutToSetValue();
    _cMat.ambientColor.setPackedValue(reader.getAttributeAsUnsigned("ambientColor"));
    _cMat.diffuseColor.setPackedValue(reader.getAttributeAsUnsigned("diffuseColor"));
    _cMat.specularColor.setPackedValue(reader.getAttributeAsUnsigned("specularColor"));
    _cMat.emissiveColor.setPackedValue(reader.getAttributeAsUnsigned("emissiveColor"));
    _cMat.shininess = (float)reader.getAttributeAsFloat("shininess");
    _cMat.transparency = (float)reader.getAttributeAsFloat("transparency");
    hasSetValue();
}

const char* PropertyMaterial::getEditorName() const
{
    if(testStatus(MaterialEdit))
        return "Gui::PropertyEditor::PropertyMaterialItem";
    return "";
}

Property *PropertyMaterial::Copy() const
{
    PropertyMaterial *p= new PropertyMaterial();
    p->_cMat = _cMat;
    return p;
}

void PropertyMaterial::Paste(const Property &from)
{
    aboutToSetValue();
    _cMat = dynamic_cast<const PropertyMaterial&>(from)._cMat;
    hasSetValue();
}

bool PropertyMaterial::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    return other.isDerivedFrom(PropertyMaterial::getClassTypeId())
        && this->getValue() == static_cast<const PropertyMaterial&>(other).getValue();
}


//**************************************************************************
// PropertyMaterialList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyMaterialList, App::PropertyLists)

namespace {

/** Where a count would be, in the per field doc file encoding
 *
 * The count PropertyLists::SaveDocFile writes ahead of the values is an
 * entry count, and no list has 2^32-1 entries, so the impossible value says
 * "what follows is per field" to a reader that knows the encoding and
 * cannot be mistaken for a list by one that does not. It is only ever
 * written at schema 6 or later, which no reader unaware of it opens anyway.
 */
constexpr uint32_t FieldStreamMarker = 0xffffffff;

/// What a string field costs, its contents included
std::size_t stringsMemSize(const std::vector<std::string> &field)
{
    std::size_t size = field.size() * sizeof(std::string);
    for (const auto &value : field) {
        size += value.size();
    }
    return size;
}

/// Resolve one entry of a field that may be 0, 1 or count long
template<class T>
inline const T &fieldAt(const std::vector<T> &values, int idx, const T &def)
{
    if (values.empty())
        return def;
    return values.size() == 1 ? values.front() : values[idx];
}

/// Collapse a field to the smallest of 0, 1 and its current length
template<class T>
void collapseField(std::vector<T> &values, const T &def)
{
    if (values.empty())
        return;
    const T &first = values.front();
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (!(values[i] == first))
            return;
    }
    // swap rather than resize: a field that has just been read from a
    // 10,000 entry document should give the memory back, not merely stop
    // counting it
    if (first == def) {
        std::vector<T>().swap(values);
    }
    else if (values.size() > 1) {
        std::vector<T>(1, first).swap(values);
    }
}

/// Grow a field so that one entry can differ from the others
template<class T>
void expandField(std::vector<T> &values, int count, const T &def)
{
    if (static_cast<int>(values.size()) == count)
        return;
    values.assign(count, values.empty() ? def : values.front());
}

/** Follow a change of entry count, without materialising a uniform field
 *
 * A field only has to be written out entry by entry when the value arriving
 * disagrees with the one already there -- which is what keeps a growing
 * import of identically coloured faces linear.
 */
template<class T>
void resizeField(std::vector<T> &values, int oldCount, int newCount,
                 const T &fill, const T &def)
{
    if (newCount < oldCount) {
        if (newCount == 0)
            std::vector<T>().swap(values);
        else if (static_cast<int>(values.size()) > newCount && values.size() > 1)
            values.resize(newCount);
        return;
    }
    if (newCount == oldCount)
        return;
    const T current = values.empty() ? def : values.front();
    if (values.size() <= 1 && current == fill)
        return;
    if (values.size() <= 1)
        values.assign(oldCount, current);
    values.resize(newCount, fill);
}

/// Write one entry of a field, expanding it only if the value is new
template<class T>
bool setFieldAt(std::vector<T> &values, int idx, int count, const T &value, const T &def)
{
    if (fieldAt(values, idx, def) == value)
        return false;
    expandField(values, count, def);
    values[idx] = value;
    return true;
}

} // namespace

//**************************************************************************
// Construction/Destruction

PropertyMaterialList::PropertyMaterialList() = default;

PropertyMaterialList::~PropertyMaterialList() = default;

const Material &PropertyMaterialList::defaultMaterial()
{
    static const Material def;
    return def;
}

//**************************************************************************
// Storage

void PropertyMaterialList::touchFields()
{
    _normalized = false;
}

void PropertyMaterialList::normalize()
{
    const Material &def = defaultMaterial();
    if (_count == 0) {
        std::vector<Color>().swap(_ambient);
        std::vector<Color>().swap(_diffuse);
        std::vector<Color>().swap(_specular);
        std::vector<Color>().swap(_emissive);
        std::vector<float>().swap(_shininess);
        std::vector<float>().swap(_transparency);
        std::vector<std::string>().swap(_image);
        std::vector<std::string>().swap(_imagePath);
        std::vector<std::string>().swap(_uuid);
        std::vector<int8_t>().swap(_type);
    }
    else {
        collapseField(_ambient, def.ambientColor);
        collapseField(_diffuse, def.diffuseColor);
        collapseField(_specular, def.specularColor);
        collapseField(_emissive, def.emissiveColor);
        collapseField(_shininess, def.shininess);
        collapseField(_transparency, def.transparency);
        collapseField(_image, def.image);
        collapseField(_imagePath, def.imagePath);
        collapseField(_uuid, def.uuid);
        collapseField(_type, static_cast<int8_t>(def.getType()));
    }
    _normalized = true;
}

void PropertyMaterialList::ensureNormalized() const
{
    // Normalising is deferred so that a loop setting one entry at a time
    // does not rescan the whole list on every step. It changes what is
    // stored but not what the property means, which is why it may happen
    // under a const call -- everything that compares or writes this property
    // asks for the normal form first.
    if (!_normalized)
        const_cast<PropertyMaterialList*>(this)->normalize();
}

void PropertyMaterialList::setSize(int newSize)
{
    setSize(newSize, defaultMaterial());
}

void PropertyMaterialList::setSize(int newSize, const Material &def)
{
    if (newSize == _count)
        return;
    if (newSize < 0)
        throw Base::ValueError("negative list size");

    atomic_change guard(*this);
    touchFields();
    const Material &zero = defaultMaterial();
    resizeField(_ambient, _count, newSize, def.ambientColor, zero.ambientColor);
    resizeField(_diffuse, _count, newSize, def.diffuseColor, zero.diffuseColor);
    resizeField(_specular, _count, newSize, def.specularColor, zero.specularColor);
    resizeField(_emissive, _count, newSize, def.emissiveColor, zero.emissiveColor);
    resizeField(_shininess, _count, newSize, def.shininess, zero.shininess);
    resizeField(_transparency, _count, newSize, def.transparency, zero.transparency);
    resizeField(_image, _count, newSize, def.image, zero.image);
    resizeField(_imagePath, _count, newSize, def.imagePath, zero.imagePath);
    resizeField(_uuid, _count, newSize, def.uuid, zero.uuid);
    resizeField(_type, _count, newSize, static_cast<int8_t>(def.getType()),
                static_cast<int8_t>(zero.getType()));
    _count = newSize;
    clearTouchList();
    guard.tryInvoke();
}

Material PropertyMaterialList::getMaterial(int idx) const
{
    Material mat;
    if (idx < 0 || idx >= _count)
        return mat;
    // The type goes on FIRST. Material::setType() rewrites every colour and
    // both floats with that type's preset, so a setType() after the fields
    // are laid in throws all of them away and the list hands back the
    // preset instead of what it stores -- silently, because the per field
    // getters below are unaffected and keep telling the truth.
    const Material &def = defaultMaterial();
    mat.setType(static_cast<Material::MaterialType>(
                fieldAt(_type, idx, static_cast<int8_t>(def.getType()))));
    mat.ambientColor = fieldAt(_ambient, idx, def.ambientColor);
    mat.diffuseColor = fieldAt(_diffuse, idx, def.diffuseColor);
    mat.specularColor = fieldAt(_specular, idx, def.specularColor);
    mat.emissiveColor = fieldAt(_emissive, idx, def.emissiveColor);
    mat.shininess = fieldAt(_shininess, idx, def.shininess);
    mat.transparency = fieldAt(_transparency, idx, def.transparency);
    mat.image = fieldAt(_image, idx, def.image);
    mat.imagePath = fieldAt(_imagePath, idx, def.imagePath);
    mat.uuid = fieldAt(_uuid, idx, def.uuid);
    return mat;
}

void PropertyMaterialList::setValue(const Material &mat)
{
    setValues(std::vector<Material>(1, mat));
}

void PropertyMaterialList::setValues(std::vector<Material> &&values)
{
    setValues(static_cast<const std::vector<Material>&>(values));
}

void PropertyMaterialList::setValues(const std::vector<Material> &values)
{
    atomic_change guard(*this);
    touchFields();
    _touchList.clear();
    _count = static_cast<int>(values.size());
    std::vector<Color>().swap(_ambient);
    std::vector<Color>().swap(_diffuse);
    std::vector<Color>().swap(_specular);
    std::vector<Color>().swap(_emissive);
    std::vector<float>().swap(_shininess);
    std::vector<float>().swap(_transparency);
    std::vector<std::string>().swap(_image);
    std::vector<std::string>().swap(_imagePath);
    std::vector<std::string>().swap(_uuid);
    std::vector<int8_t>().swap(_type);
    if (_count) {
        _ambient.reserve(_count);
        _diffuse.reserve(_count);
        _specular.reserve(_count);
        _emissive.reserve(_count);
        _shininess.reserve(_count);
        _transparency.reserve(_count);
        _image.reserve(_count);
        _imagePath.reserve(_count);
        _uuid.reserve(_count);
        _type.reserve(_count);
        for (const auto &mat : values) {
            _ambient.push_back(mat.ambientColor);
            _diffuse.push_back(mat.diffuseColor);
            _specular.push_back(mat.specularColor);
            _emissive.push_back(mat.emissiveColor);
            _shininess.push_back(mat.shininess);
            _transparency.push_back(mat.transparency);
            _image.push_back(mat.image);
            _imagePath.push_back(mat.imagePath);
            _uuid.push_back(mat.uuid);
            _type.push_back(static_cast<int8_t>(mat.getType()));
        }
        normalize();
    }
    else {
        _normalized = true;
    }
    guard.tryInvoke();
}

void PropertyMaterialList::set1Value(int idx, const Material &mat)
{
    if (idx < -1 || idx > _count)
        throw Base::RuntimeError("index out of bound");

    atomic_change guard(*this, false);
    if (idx == -1 || idx == _count) {
        guard.aboutToChange();
        idx = _count;
        setSize(_count + 1, mat);
    }
    else {
        if (getMaterial(idx) == mat)
            return;
        guard.aboutToChange();
        touchFields();
        const Material &def = defaultMaterial();
        setFieldAt(_ambient, idx, _count, mat.ambientColor, def.ambientColor);
        setFieldAt(_diffuse, idx, _count, mat.diffuseColor, def.diffuseColor);
        setFieldAt(_specular, idx, _count, mat.specularColor, def.specularColor);
        setFieldAt(_emissive, idx, _count, mat.emissiveColor, def.emissiveColor);
        setFieldAt(_shininess, idx, _count, mat.shininess, def.shininess);
        setFieldAt(_transparency, idx, _count, mat.transparency, def.transparency);
        setFieldAt(_image, idx, _count, mat.image, def.image);
        setFieldAt(_imagePath, idx, _count, mat.imagePath, def.imagePath);
        setFieldAt(_uuid, idx, _count, mat.uuid, def.uuid);
        setFieldAt(_type, idx, _count, static_cast<int8_t>(mat.getType()),
                   static_cast<int8_t>(def.getType()));
    }
    _touchList.insert(idx);
    guard.tryInvoke();
}

//**************************************************************************
// Per field access

Color PropertyMaterialList::getAmbientColor(int idx) const
{
    return fieldAt(_ambient, idx, defaultMaterial().ambientColor);
}

Color PropertyMaterialList::getDiffuseColor(int idx) const
{
    return fieldAt(_diffuse, idx, defaultMaterial().diffuseColor);
}

Color PropertyMaterialList::getSpecularColor(int idx) const
{
    return fieldAt(_specular, idx, defaultMaterial().specularColor);
}

Color PropertyMaterialList::getEmissiveColor(int idx) const
{
    return fieldAt(_emissive, idx, defaultMaterial().emissiveColor);
}

float PropertyMaterialList::getShininess(int idx) const
{
    return fieldAt(_shininess, idx, defaultMaterial().shininess);
}

float PropertyMaterialList::getTransparency(int idx) const
{
    return fieldAt(_transparency, idx, defaultMaterial().transparency);
}

const std::string &PropertyMaterialList::getImage(int idx) const
{
    return fieldAt(_image, idx, defaultMaterial().image);
}

const std::string &PropertyMaterialList::getImagePath(int idx) const
{
    return fieldAt(_imagePath, idx, defaultMaterial().imagePath);
}

const std::string &PropertyMaterialList::getUuid(int idx) const
{
    return fieldAt(_uuid, idx, defaultMaterial().uuid);
}

Material::MaterialType PropertyMaterialList::getType(int idx) const
{
    return static_cast<Material::MaterialType>(
            fieldAt(_type, idx, static_cast<int8_t>(defaultMaterial().getType())));
}

/** Take a whole field
 *
 * A field of one is uniform and a field as long as the list is per entry.
 * A vector that is neither is a statement about how long the list should
 * be, the way assigning a colour list of a different length is; an empty
 * one returns the field to its default without disturbing the count. The
 * values normalise on the way in.
 */
template<class T>
void PropertyMaterialList::setField(std::vector<T> &field, const std::vector<T> &values,
                                    const T &def)
{
    if (field == values)
        return;
    atomic_change guard(*this);
    const int newCount = static_cast<int>(values.size());
    if (newCount != _count && (newCount > 1 || _count == 0))
        setSize(newCount);
    touchFields();
    field = values;
    collapseField(field, def);
    guard.tryInvoke();
}

void PropertyMaterialList::setAmbientColors(const std::vector<Color> &colors)
{
    setField(_ambient, colors, defaultMaterial().ambientColor);
}

void PropertyMaterialList::setDiffuseColors(const std::vector<Color> &colors)
{
    setField(_diffuse, colors, defaultMaterial().diffuseColor);
}

void PropertyMaterialList::setSpecularColors(const std::vector<Color> &colors)
{
    setField(_specular, colors, defaultMaterial().specularColor);
}

void PropertyMaterialList::setEmissiveColors(const std::vector<Color> &colors)
{
    setField(_emissive, colors, defaultMaterial().emissiveColor);
}

void PropertyMaterialList::setShininessValues(const std::vector<float> &values)
{
    setField(_shininess, values, defaultMaterial().shininess);
}

void PropertyMaterialList::setTransparencies(const std::vector<float> &values)
{
    setField(_transparency, values, defaultMaterial().transparency);
}

void PropertyMaterialList::setImages(const std::vector<std::string> &values)
{
    setField(_image, values, defaultMaterial().image);
}

void PropertyMaterialList::setImagePaths(const std::vector<std::string> &values)
{
    setField(_imagePath, values, defaultMaterial().imagePath);
}

void PropertyMaterialList::setUuids(const std::vector<std::string> &values)
{
    setField(_uuid, values, defaultMaterial().uuid);
}

/// Write one entry of one field, growing the list if it names a new entry
template<class T>
void PropertyMaterialList::setFieldValue(std::vector<T> &field, int idx, const T &value,
                                         const T &def)
{
    if (idx < 0 || idx > _count)
        throw Base::RuntimeError("index out of bound");
    atomic_change guard(*this, false);
    if (idx == _count) {
        guard.aboutToChange();
        setSize(_count + 1);
    }
    else if (fieldAt(field, idx, def) == value) {
        return;
    }
    else {
        guard.aboutToChange();
    }
    touchFields();
    setFieldAt(field, idx, _count, value, def);
    _touchList.insert(idx);
    guard.tryInvoke();
}

void PropertyMaterialList::setAmbientColor(int idx, const Color &col)
{
    setFieldValue(_ambient, idx, col, defaultMaterial().ambientColor);
}

void PropertyMaterialList::setDiffuseColor(int idx, const Color &col)
{
    setFieldValue(_diffuse, idx, col, defaultMaterial().diffuseColor);
}

void PropertyMaterialList::setSpecularColor(int idx, const Color &col)
{
    setFieldValue(_specular, idx, col, defaultMaterial().specularColor);
}

void PropertyMaterialList::setEmissiveColor(int idx, const Color &col)
{
    setFieldValue(_emissive, idx, col, defaultMaterial().emissiveColor);
}

void PropertyMaterialList::setShininess(int idx, float value)
{
    setFieldValue(_shininess, idx, value, defaultMaterial().shininess);
}

void PropertyMaterialList::setTransparency(int idx, float value)
{
    setFieldValue(_transparency, idx, value, defaultMaterial().transparency);
}

void PropertyMaterialList::setImage(int idx, const std::string &value)
{
    setFieldValue(_image, idx, value, defaultMaterial().image);
}

void PropertyMaterialList::setImagePath(int idx, const std::string &value)
{
    setFieldValue(_imagePath, idx, value, defaultMaterial().imagePath);
}

void PropertyMaterialList::setUuid(int idx, const std::string &value)
{
    setFieldValue(_uuid, idx, value, defaultMaterial().uuid);
}

/// Give every entry the same value for one field, and none of it to storage
template<class T>
void PropertyMaterialList::setUniformField(std::vector<T> &field, const T &value, const T &def)
{
    if (field.empty() && value == def)
        return;  // already the default everywhere, including on an empty list
    if (_count && field.size() <= 1 && fieldAt(field, 0, def) == value)
        return;
    atomic_change guard(*this);
    touchFields();
    if (_count == 0)
        setSize(1);
    if (value == def)
        std::vector<T>().swap(field);
    else
        std::vector<T>(1, value).swap(field);
    guard.tryInvoke();
}

void PropertyMaterialList::setAmbientColor(const Color &col)
{
    setUniformField(_ambient, col, defaultMaterial().ambientColor);
}

void PropertyMaterialList::setDiffuseColor(const Color &col)
{
    setUniformField(_diffuse, col, defaultMaterial().diffuseColor);
}

void PropertyMaterialList::setSpecularColor(const Color &col)
{
    setUniformField(_specular, col, defaultMaterial().specularColor);
}

void PropertyMaterialList::setEmissiveColor(const Color &col)
{
    setUniformField(_emissive, col, defaultMaterial().emissiveColor);
}

void PropertyMaterialList::setShininess(float value)
{
    setUniformField(_shininess, value, defaultMaterial().shininess);
}

void PropertyMaterialList::setTransparency(float value)
{
    setUniformField(_transparency, value, defaultMaterial().transparency);
}

//**************************************************************************
// Base class implementer

void PropertyMaterialList::setImage(const std::string &value)
{
    setUniformField(_image, value, defaultMaterial().image);
}

void PropertyMaterialList::setImagePath(const std::string &value)
{
    setUniformField(_imagePath, value, defaultMaterial().imagePath);
}

void PropertyMaterialList::setUuid(const std::string &value)
{
    setUniformField(_uuid, value, defaultMaterial().uuid);
}

PyObject *PropertyMaterialList::getPyObject()
{
    Py::Tuple tuple(getSize());

    for (int i = 0; i<getSize(); i++) {
        tuple.setItem(i, Py::asObject(new MaterialPy(new Material(getMaterial(i)))));
    }

    return Py::new_reference_to(tuple);
}

void PropertyMaterialList::setPyObject(PyObject *value)
{
    try {
        setValue(getPyValue(value));
        return;
    }
    catch (...) {
    }
    PropertyLists::setPyObject(value);
}

Material PropertyMaterialList::getPyValue(PyObject *value) const {
    if (PyObject_TypeCheck(value, &(MaterialPy::Type)))
        return *static_cast<MaterialPy*>(value)->getMaterialPtr();
    else {
        std::string error = std::string("type must be 'Material', not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyMaterialList::setPyValues(const std::vector<PyObject*> &vals,
                                       const std::vector<int> &indices)
{
    if (indices.empty()) {
        std::vector<Material> values;
        values.reserve(vals.size());
        for (auto *item : vals)
            values.push_back(getPyValue(item));
        setValues(std::move(values));
        return;
    }
    assert(vals.size() == indices.size());
    atomic_change guard(*this);
    int i = 0;
    for (auto index : indices)
        set1Value(index, getPyValue(vals[i++]));
    guard.tryInvoke();
}

unsigned int PropertyMaterialList::getMemSize() const
{
    ensureNormalized();
    return static_cast<unsigned int>(
            (_ambient.size() + _diffuse.size() + _specular.size() + _emissive.size())
                * sizeof(Color)
            + (_shininess.size() + _transparency.size()) * sizeof(float)
            + _type.size() * sizeof(int8_t)
            + stringsMemSize(_image) + stringsMemSize(_imagePath)
            + stringsMemSize(_uuid));
}

unsigned int PropertyMaterialList::getSaveSize(Base::Writer &writer) const
{
    if (writer.getSchemaVersion() >= 6)
        return getMemSize();
    // The compatible encoding spells out a whole material per entry however
    // little of it the storage holds, so a uniform list of ten thousand
    // faces is four bytes in memory and a quarter of a megabyte on the way
    // out. Weighed as the former it would land inline in Document.xml.
    return static_cast<unsigned int>(_count) * (4 * sizeof(uint32_t) + 2 * sizeof(float));
}

//**************************************************************************
// Persistence
//
// Two encodings. The one every FreeCAD reads spells out each entry in full;
// the per field one, written only at a schema that already excludes other
// readers, writes each field once at whatever length it actually has.

bool PropertyMaterialList::saveXML(Base::Writer &writer) const
{
    ensureNormalized();
    // The per field form also when a string has to survive: the inline form
    // is fork-only at every schema -- upstream's reader looks for a file
    // attribute and ignores a count -- so there is nothing to lose by using
    // the encoding that can carry them, and data to lose by not.
    if (writer.getSchemaVersion() >= 6 || hasTextureOrCard())
        return saveFieldXML(writer);

    writer.Stream() << ">\n" << std::hex;
    for (int i = 0; i < _count; ++i) {
        writer.Stream() << getAmbientColor(i).getPackedValue()
                        << ' ' << getDiffuseColor(i).getPackedValue()
                        << ' ' << getSpecularColor(i).getPackedValue()
                        << ' ' << getEmissiveColor(i).getPackedValue()
                        << ' ' << getShininess(i)
                        << ' ' << getTransparency(i)
                        << '\n';
    }
    writer.Stream() << std::dec;
    return false;
}

void PropertyMaterialList::restoreXML(Base::XMLReader &reader)
{
    unsigned uCt = reader.getAttributeAsUnsigned("count");
    if (reader.hasAttribute("fields")) {
        restoreFieldXML(reader, uCt);
        return;
    }

    auto &s = reader.beginCharStream() >> std::hex;
    std::vector<Material> values(uCt);
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
    setValues(std::move(values));
}

void PropertyMaterialList::saveStream(Base::OutputStream &str) const
{
    ensureNormalized();
    for (int i = 0; i < _count; ++i) {
        str << getAmbientColor(i).getPackedValue();
        str << getDiffuseColor(i).getPackedValue();
        str << getSpecularColor(i).getPackedValue();
        str << getEmissiveColor(i).getPackedValue();
        str << getShininess(i);
        str << getTransparency(i);
    }
}

void PropertyMaterialList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Material> values(uCt);
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
    setValues(std::move(values));
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
void PropertyMaterialList::Save(Base::Writer &writer) const
{
    ensureNormalized();
    if (writer.getSchemaVersion() < 6 && hasTextureOrCard() && !writer.isForceXML()
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

void PropertyMaterialList::Restore(Base::XMLReader &reader)
{
    reader.readElement(xmlName());
    // Remembered for RestoreDocFile, which is called later and separately
    _fileVersion = reader.hasAttribute("version")
        ? static_cast<int>(reader.getAttributeAsInteger("version"))
        : 0;
    std::string file(reader.getAttribute("file", ""));
    if (!file.empty()) {
        reader.addFile(file.c_str(), this);
    }
    else if (reader.hasAttribute("count")) {
        restoreXML(reader);
    }
    else if (getSize()) {
        setSize(0);
    }
}

void PropertyMaterialList::SaveDocFile(Base::Writer &writer) const
{
    if (writer.getSchemaVersion() < 6) {
        Base::OutputStream str(writer.Stream(), writer.isPreferBinary());
        str << static_cast<uint32_t>(_count);
        saveStream(str);
        if (hasTextureOrCard()) {
            saveStringStream(str);
        }
        return;
    }
    ensureNormalized();
    Base::OutputStream str(writer.Stream(), writer.isPreferBinary());
    str << FieldStreamMarker;
    str << static_cast<uint32_t>(_count);
    saveFieldStream(str);
}

void PropertyMaterialList::RestoreDocFile(Base::Reader &reader)
{
    Base::InputStream str(reader, !boost::ends_with(reader.getFileName(), ".txt"));
    uint32_t uCt = 0;
    str >> uCt;
    if (uCt == FieldStreamMarker) {
        str >> uCt;
        restoreFieldStream(str, uCt);
    }
    else {
        restoreStream(str, uCt);
        // Version 3 is the colours we have always read, followed by three
        // strings per entry. Written by upstream, and by this fork when it
        // has any to write.
        if (_fileVersion >= 3) {
            restoreStringStream(str, uCt);
        }
    }
}

/// Upstream's second pass: image, imagePath and uuid, entry by entry
void PropertyMaterialList::saveStringStream(Base::OutputStream &str) const
{
    for (int i = 0; i < _count; ++i) {
        str << getImage(i);
        str << getImagePath(i);
        str << getUuid(i);
    }
}

void PropertyMaterialList::restoreStringStream(Base::InputStream &str, unsigned uCt)
{
    atomic_change guard(*this);
    touchFields();
    std::vector<std::string> image(uCt);
    std::vector<std::string> imagePath(uCt);
    std::vector<std::string> uuid(uCt);
    for (unsigned i = 0; i < uCt; ++i) {
        str >> image[i];
        str >> imagePath[i];
        str >> uuid[i];
    }
    _image.swap(image);
    _imagePath.swap(imagePath);
    _uuid.swap(uuid);
    normalize();
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
};

} // namespace

void PropertyMaterialList::saveFieldStream(Base::OutputStream &str) const
{
    uint16_t mask = 0;
    if (!_ambient.empty())      mask |= FieldAmbient;
    if (!_diffuse.empty())      mask |= FieldDiffuse;
    if (!_specular.empty())     mask |= FieldSpecular;
    if (!_emissive.empty())     mask |= FieldEmissive;
    if (!_shininess.empty())    mask |= FieldShininess;
    if (!_transparency.empty()) mask |= FieldTransparency;
    if (!_type.empty())         mask |= FieldType;
    if (!_image.empty())        mask |= FieldImage;
    if (!_imagePath.empty())    mask |= FieldImagePath;
    if (!_uuid.empty())         mask |= FieldUuid;
    str << mask;

    auto writeColors = [&str](const std::vector<Color> &field) {
        if (field.empty())
            return;
        str << static_cast<uint32_t>(field.size());
        for (const auto &col : field)
            str << col.getPackedValue();
    };
    auto writeFloats = [&str](const std::vector<float> &field) {
        if (field.empty())
            return;
        str << static_cast<uint32_t>(field.size());
        for (float value : field)
            str << value;
    };

    writeColors(_ambient);
    writeColors(_diffuse);
    writeColors(_specular);
    writeColors(_emissive);
    writeFloats(_shininess);
    writeFloats(_transparency);
    if (!_type.empty()) {
        str << static_cast<uint32_t>(_type.size());
        for (int8_t value : _type)
            str << value;
    }
    // std::string over this stream is already a length and its bytes, which
    // is the same shape upstream writes its strings in
    auto writeStrings = [&str](const std::vector<std::string> &field) {
        if (field.empty())
            return;
        str << static_cast<uint32_t>(field.size());
        for (const auto &value : field)
            str << value;
    };
    writeStrings(_image);
    writeStrings(_imagePath);
    writeStrings(_uuid);
}

void PropertyMaterialList::restoreFieldStream(Base::InputStream &str, unsigned uCt)
{
    atomic_change guard(*this);
    touchFields();
    _touchList.clear();
    _count = static_cast<int>(uCt);

    uint16_t mask = 0;
    str >> mask;

    auto readColors = [&str, uCt](std::vector<Color> &field, bool present) {
        std::vector<Color>().swap(field);
        if (!present)
            return;
        uint32_t count = 0;
        str >> count;
        if (count != 1 && count != uCt)
            throw Base::FileException("material field length does not match the list");
        field.resize(count);
        uint32_t packed = 0;
        for (auto &col : field) {
            str >> packed;
            col.setPackedValue(packed);
        }
    };
    auto readFloats = [&str, uCt](std::vector<float> &field, bool present) {
        std::vector<float>().swap(field);
        if (!present)
            return;
        uint32_t count = 0;
        str >> count;
        if (count != 1 && count != uCt)
            throw Base::FileException("material field length does not match the list");
        field.resize(count);
        for (auto &value : field)
            str >> value;
    };

    readColors(_ambient, (mask & FieldAmbient) != 0);
    readColors(_diffuse, (mask & FieldDiffuse) != 0);
    readColors(_specular, (mask & FieldSpecular) != 0);
    readColors(_emissive, (mask & FieldEmissive) != 0);
    readFloats(_shininess, (mask & FieldShininess) != 0);
    readFloats(_transparency, (mask & FieldTransparency) != 0);
    std::vector<int8_t>().swap(_type);
    if ((mask & FieldType) != 0) {
        uint32_t count = 0;
        str >> count;
        if (count != 1 && count != uCt)
            throw Base::FileException("material field length does not match the list");
        _type.resize(count);
        for (auto &value : _type)
            str >> value;
    }
    auto readStrings = [&str, uCt](std::vector<std::string> &field, bool present) {
        std::vector<std::string>().swap(field);
        if (!present)
            return;
        uint32_t count = 0;
        str >> count;
        if (count != 1 && count != uCt)
            throw Base::FileException("material field length does not match the list");
        field.resize(count);
        for (auto &value : field)
            str >> value;
    };
    readStrings(_image, (mask & FieldImage) != 0);
    readStrings(_imagePath, (mask & FieldImagePath) != 0);
    readStrings(_uuid, (mask & FieldUuid) != 0);
    _normalized = true;
    guard.tryInvoke();
}

bool PropertyMaterialList::saveFieldXML(Base::Writer &writer) const
{
    writer.Stream() << " fields=\"1\">\n";

    auto writeColors = [&writer](char key, const std::vector<Color> &field) {
        if (field.empty())
            return;
        writer.Stream() << key << ' ' << field.size() << std::hex;
        for (const auto &col : field)
            writer.Stream() << ' ' << col.getPackedValue();
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

    writeColors('a', _ambient);
    writeColors('d', _diffuse);
    writeColors('s', _specular);
    writeColors('e', _emissive);
    writeFloats('h', _shininess);
    writeFloats('t', _transparency);
    if (!_type.empty()) {
        writer.Stream() << "y " << _type.size();
        for (int8_t value : _type)
            writer.Stream() << ' ' << static_cast<int>(value);
        writer.Stream() << '\n';
    }
    // Hex, because these are file paths and identifiers: a space would end
    // the token and an angle bracket would end the element. An empty string
    // is a lone '-', which no hex byte can be mistaken for.
    auto writeStrings = [&writer](char key, const std::vector<std::string> &field) {
        if (field.empty())
            return;
        writer.Stream() << key << ' ' << field.size();
        for (const auto &value : field) {
            writer.Stream() << ' ';
            if (value.empty()) {
                writer.Stream() << '-';
                continue;
            }
            writer.Stream() << std::hex;
            for (unsigned char byte : value) {
                writer.Stream() << (byte >> 4) << (byte & 0xf);
            }
            writer.Stream() << std::dec;
        }
        writer.Stream() << '\n';
    };
    writeStrings('i', _image);
    writeStrings('p', _imagePath);
    writeStrings('u', _uuid);
    return false;
}

void PropertyMaterialList::restoreFieldXML(Base::XMLReader &reader, unsigned uCt)
{
    atomic_change guard(*this);
    touchFields();
    _touchList.clear();
    _count = static_cast<int>(uCt);
    std::vector<Color>().swap(_ambient);
    std::vector<Color>().swap(_diffuse);
    std::vector<Color>().swap(_specular);
    std::vector<Color>().swap(_emissive);
    std::vector<float>().swap(_shininess);
    std::vector<float>().swap(_transparency);
    std::vector<std::string>().swap(_image);
    std::vector<std::string>().swap(_imagePath);
    std::vector<std::string>().swap(_uuid);
    std::vector<int8_t>().swap(_type);

    auto &s = reader.beginCharStream();
    std::string key;
    while (s >> key) {
        unsigned count = 0;
        if (!(s >> count))
            break;
        if (count != 1 && count != uCt)
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
        case 'a': readColors(_ambient); break;
        case 'd': readColors(_diffuse); break;
        case 's': readColors(_specular); break;
        case 'e': readColors(_emissive); break;
        case 'h': readFloats(_shininess); break;
        case 't': readFloats(_transparency); break;
        case 'i':
        case 'p':
        case 'u': {
            auto &field = key[0] == 'i' ? _image : (key[0] == 'p' ? _imagePath : _uuid);
            field.resize(count);
            std::string token;
            for (auto &value : field) {
                if (!(s >> token) || token == "-") {
                    continue;
                }
                if (token.size() % 2 != 0)
                    throw Base::FileException("odd-length hex in a material string");
                value.resize(token.size() / 2);
                for (std::size_t i = 0; i < value.size(); ++i) {
                    value[i] = static_cast<char>(
                            std::stoi(token.substr(i * 2, 2), nullptr, 16));
                }
            }
            break;
        }
        case 'y': {
            _type.resize(count);
            int value = 0;
            for (auto &entry : _type) {
                s >> value;
                entry = static_cast<int8_t>(value);
            }
            break;
        }
        default:
            throw Base::FileException("unknown material field");
        }
    }
    reader.endCharStream();
    _normalized = true;
    guard.tryInvoke();
}

const char* PropertyMaterialList::getEditorName() const
{
    if(testStatus(NoMaterialListEdit))
        return "";
    return "Gui::PropertyEditor::PropertyMaterialListItem";
}

bool PropertyMaterialList::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    auto list = Base::freecad_dynamic_cast<const PropertyMaterialList>(&other);
    if (!list || list->_count != _count)
        return false;
    ensureNormalized();
    list->ensureNormalized();
    return _ambient == list->_ambient
        && _diffuse == list->_diffuse
        && _specular == list->_specular
        && _emissive == list->_emissive
        && _shininess == list->_shininess
        && _transparency == list->_transparency
        && _image == list->_image
        && _imagePath == list->_imagePath
        && _uuid == list->_uuid
        && _type == list->_type;
}

Property *PropertyMaterialList::Copy() const
{
    ensureNormalized();
    PropertyMaterialList *p = new PropertyMaterialList();
    p->_count = _count;
    p->_ambient = _ambient;
    p->_diffuse = _diffuse;
    p->_specular = _specular;
    p->_emissive = _emissive;
    p->_shininess = _shininess;
    p->_transparency = _transparency;
    p->_image = _image;
    p->_imagePath = _imagePath;
    p->_uuid = _uuid;
    p->_type = _type;
    return p;
}

void PropertyMaterialList::Paste(const Property &from)
{
    const auto &other = dynamic_cast<const PropertyMaterialList&>(from);
    other.ensureNormalized();
    atomic_change guard(*this);
    touchFields();
    _touchList.clear();
    _count = other._count;
    _ambient = other._ambient;
    _diffuse = other._diffuse;
    _specular = other._specular;
    _emissive = other._emissive;
    _shininess = other._shininess;
    _transparency = other._transparency;
    _image = other._image;
    _imagePath = other._imagePath;
    _uuid = other._uuid;
    _type = other._type;
    _normalized = true;
    guard.tryInvoke();
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
            throw Base::TypeError("Invalid type");
        if(!t.isDerivedFrom(Persistence::getClassTypeId()))
            throw Base::TypeError("Type must be derived from Base::Persistence");
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
