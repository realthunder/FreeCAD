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

#include <boost/algorithm/string/predicate.hpp>

#include <Base/MatrixPy.h>
#include <Base/PlacementPy.h>
#include <Base/Reader.h>

#include <Base/Quantity.h>
#include <Base/QuantityPy.h>
#include <Base/Rotation.h>
#include <Base/RotationPy.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/VectorPy.h>
#include <Base/Writer.h>

#include "PropertyGeo.h"

#include "Document.h"
#include "Placement.h"
#include "ObjectIdentifier.h"


using namespace App;
using namespace Base;
using namespace std;




//**************************************************************************
//**************************************************************************
// PropertyVector
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyVector , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyVector::PropertyVector() = default;


PropertyVector::~PropertyVector() = default;

//**************************************************************************
// Base class implementer


void PropertyVector::setValue(const Base::Vector3d &vec)
{
    aboutToSetValue();
    _cVec=vec;
    hasSetValue();
}

void PropertyVector::setValue(double x, double y, double z)
{
    aboutToSetValue();
    _cVec.Set(x,y,z);
    hasSetValue();
}

const Base::Vector3d & PropertyVector::getValue()const
{
    return _cVec;
}

PyObject *PropertyVector::getPyObject()
{
    return new Base::VectorPy(_cVec);
}

void PropertyVector::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(Base::VectorPy::Type))) {
        Base::VectorPy  *pcObject = static_cast<Base::VectorPy*>(value);
        Base::Vector3d* val = pcObject->getVectorPtr();
        setValue(*val);
    }
    else if (PyTuple_Check(value)&&PyTuple_Size(value)==3) {
        PyObject* item;
        Base::Vector3d cVec;
        // x
        item = PyTuple_GetItem(value,0);
        if (PyFloat_Check(item))
            cVec.x = PyFloat_AsDouble(item);
        else if (PyLong_Check(item))
            cVec.x = (double)PyLong_AsLong(item);
        else
            throw Base::TypeError("Not allowed type used in tuple (float expected)...");
        // y
        item = PyTuple_GetItem(value,1);
        if (PyFloat_Check(item))
            cVec.y = PyFloat_AsDouble(item);
        else if (PyLong_Check(item))
            cVec.y = (double)PyLong_AsLong(item);
        else
            throw Base::TypeError("Not allowed type used in tuple (float expected)...");
        // z
        item = PyTuple_GetItem(value,2);
        if (PyFloat_Check(item))
            cVec.z = PyFloat_AsDouble(item);
        else if (PyLong_Check(item))
            cVec.z = (double)PyLong_AsLong(item);
        else
            throw Base::TypeError("Not allowed type used in tuple (float expected)...");
        setValue( cVec );
    }
    else {
        std::string error = std::string("type must be 'Vector' or tuple of three floats, not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyVector::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<PropertyVector valueX=\"" <<  _cVec.x << "\" valueY=\"" <<  _cVec.y << "\" valueZ=\"" <<  _cVec.z <<"\"/>\n";
}

void PropertyVector::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyVector");
    // get the value of my Attribute
    aboutToSetValue();
    _cVec.x = reader.getAttributeAsFloat("valueX");
    _cVec.y = reader.getAttributeAsFloat("valueY");
    _cVec.z = reader.getAttributeAsFloat("valueZ");
    hasSetValue();
}


Property *PropertyVector::Copy() const
{
    PropertyVector *p= new PropertyVector();
    p->_cVec = _cVec;
    return p;
}

void PropertyVector::Paste(const Property &from)
{
    aboutToSetValue();
    _cVec = dynamic_cast<const PropertyVector&>(from)._cVec;
    hasSetValue();
}

void PropertyVector::getPaths(std::vector<ObjectIdentifier> &paths) const
{
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("x")));
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("y")));
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("z")));
}

App::any PropertyVector::getPathValue(const ObjectIdentifier &path) const
{
    Base::Unit unit = getUnit();
    if(!unit.isEmpty()) {
        std::string p = path.getSubPathStr();
        if (p == ".x" || p == ".y" || p == ".z") {
            // Convert double to quantity
            return Base::Quantity(App::any_cast<double>(Property::getPathValue(path)), unit);
        }
    }
    return Property::getPathValue(path);
}

bool PropertyVector::getPyPathValue(const ObjectIdentifier &path, Py::Object &res) const
{
    Base::Unit unit = getUnit();
    if(unit.isEmpty())
        return false;

    int i=0;
    for(auto &c : path.getPropertyComponents(1)) {
        if(i == 0) {
            if(!c.isSimple())
                return false;
            if(c.getName() == "x") {
                res = Py::asObject(new QuantityPy(new Quantity(getValue().x,unit)));
            } else if(c.getName() == "y") {
                res = Py::asObject(new QuantityPy(new Quantity(getValue().y,unit)));
            } else if(c.getName() == "z") {
                res = Py::asObject(new QuantityPy(new Quantity(getValue().z,unit)));
            } else
                return false;
            continue;
        }
        res = c.get(res);
    }
    return true;
}

static inline Base::Vector3d _interpolate(const Base::Vector3d &from, const Base::Vector3d &to, float t)
{
    return Base::Vector3d((to.x - from.x) * t + from.x,
                          (to.y - from.y) * t + from.y,
                          (to.z - from.z) * t + from.z);
}

void PropertyVector::interpolate(const Property &from, const Property &to, float t)
{
    auto fromValue = dynamic_cast<const PropertyVector&>(from).getValue();
    auto toValue = dynamic_cast<const PropertyVector&>(to).getValue();
    if (fromValue != toValue)
        setValue(_interpolate(fromValue, toValue, t));
}

//**************************************************************************
// PropertyVectorDistance
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyVectorDistance , App::PropertyVector)

//**************************************************************************
// Construction/Destruction


PropertyVectorDistance::PropertyVectorDistance() = default;

PropertyVectorDistance::~PropertyVectorDistance() = default;

//**************************************************************************
// PropertyPosition
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPosition , App::PropertyVector)

//**************************************************************************
// Construction/Destruction


PropertyPosition::PropertyPosition() = default;

PropertyPosition::~PropertyPosition() = default;

//**************************************************************************
// PropertyPosition
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyDirection , App::PropertyVector)

//**************************************************************************
// Construction/Destruction


PropertyDirection::PropertyDirection() = default;

PropertyDirection::~PropertyDirection() = default;

void PropertyDirection::interpolate(const Property &from, const Property &to, float t)
{
    auto fromValue = dynamic_cast<const PropertyDirection&>(from).getValue();
    auto toValue = dynamic_cast<const PropertyDirection&>(to).getValue();
    Base::Rotation rot(fromValue, toValue);
    Base::Vector3d axis;
    double angle;
    rot.getRawValue(axis,angle);
    rot.setValue(axis, angle*t);
    setValue(rot.multVec(fromValue));
}

//**************************************************************************
// PropertyVectorList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyVectorList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction

PropertyVectorList::PropertyVectorList() = default;

PropertyVectorList::~PropertyVectorList() = default;

//**************************************************************************
// Base class implementer

void PropertyVectorList::setValue(double x, double y, double z)
{
    setValue(Base::Vector3d(x,y,z));
}

PyObject *PropertyVectorList::getPyObject()
{
    PyObject* list = PyList_New(getSize());

    for (int i = 0;i < getSize(); i++)
        PyList_SetItem(list, i, new VectorPy(_lValueList[i]));

    return list;
}

Base::Vector3d PropertyVectorList::getPyValue(PyObject *item) const {
    PropertyVector val;
    val.setPyObject( item );
    return val.getValue();
}

bool PropertyVectorList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(const auto &v : _lValueList)
        writer.Stream() << v.x << ' ' << v.y << ' ' << v.z << '\n';
    return false;
}

void PropertyVectorList::restoreXML(Base::XMLReader &reader)
{
    unsigned count = reader.getAttributeAsUnsigned("count");
    auto &s = reader.beginCharStream(false);
    std::vector<Base::Vector3d> values(count);
    for(auto &v : values) 
        s >> v.x >> v.y >> v.z;
    reader.endCharStream();
    setValues(std::move(values));
}

void PropertyVectorList::saveStream(Base::OutputStream &str) const
{
    if (!isSinglePrecision()) {
        for (std::vector<Base::Vector3d>::const_iterator it = _lValueList.begin(); it != _lValueList.end(); ++it) {
            str << it->x << it->y << it->z;
        }
    }
    else {
        for (std::vector<Base::Vector3d>::const_iterator it = _lValueList.begin(); it != _lValueList.end(); ++it) {
            float x = (float)it->x;
            float y = (float)it->y;
            float z = (float)it->z;
            str << x << y << z;
        }
    }
}

void PropertyVectorList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Base::Vector3d> values(uCt);
    if (!isSinglePrecision()) {
        for (std::vector<Base::Vector3d>::iterator it = values.begin(); it != values.end(); ++it) {
            str >> it->x >> it->y >> it->z;
        }
    }
    else {
        float x,y,z;
        for (std::vector<Base::Vector3d>::iterator it = values.begin(); it != values.end(); ++it) {
            str >> x >> y >> z;
            it->Set(x, y, z);
        }
    }
    setValues(std::move(values));
}

Property *PropertyVectorList::Copy() const
{
    PropertyVectorList *p= new PropertyVectorList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyVectorList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyVectorList&>(from)._lValueList);
}

unsigned int PropertyVectorList::getMemSize () const
{
    return static_cast<unsigned int>(_lValueList.size() * sizeof(Base::Vector3d));
}

void PropertyVectorList::interpolateValue(int index, const Base::Vector3d &from, const Base::Vector3d &to, float t)
{
    if (from != to) {
        set1Value(index, _interpolate(from, to, t));
    }
}

//**************************************************************************
// _PropertyVectorList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void _PropertyVectorList::setValue(float x, float y, float z)
{
    setValue(Base::Vector3f(x,y,z));
}

PyObject *_PropertyVectorList::getPyObject(void)
{
    Py::List res;
    for(const auto &v : _lValueList)
        res.append(Py::Object(new VectorPy(Vector3d(v.x,v.y,v.z))));
    return Py::new_reference_to(res);
}

Base::Vector3f _PropertyVectorList::getPyValue(PyObject *item) const {
    PropertyVector val;
    val.setPyObject( item );
    const auto &v = val.getValue();
    return Base::Vector3f(v.x,v.y,v.z);
}

bool _PropertyVectorList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(const auto &v : _lValueList)
        writer.Stream() << v.x << ' ' << v.y << ' ' << v.z << '\n';
    return false;
}

void _PropertyVectorList::restoreXML(Base::XMLReader &reader)
{
    unsigned count = reader.getAttributeAsUnsigned("count");
    auto &s = reader.beginCharStream(false);
    std::vector<Base::Vector3f> values(count);
    for(auto &v : values) 
        s >> v.x >> v.y >> v.z;
    reader.endCharStream();
    setValues(std::move(values));
}

void _PropertyVectorList::saveStream(Base::OutputStream &str) const
{
    for(const auto &v : _lValueList)
        str << v.x << v.y << v.z;
}

void _PropertyVectorList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Base::Vector3f> values(uCt);
    for(auto &v : _lValueList)
        str >> v.x >> v.y >> v.z;
    setValues(std::move(values));
}

Property *_PropertyVectorList::Copy(void) const
{
    _PropertyVectorList *p= new _PropertyVectorList();
    p->_lValueList = _lValueList;
    return p;
}

void _PropertyVectorList::Paste(const Property &from)
{
    setValues(dynamic_cast<const _PropertyVectorList&>(from)._lValueList);
}

unsigned int _PropertyVectorList::getMemSize (void) const
{
    return static_cast<unsigned int>(_lValueList.size() * sizeof(Base::Vector3f));
}

void _PropertyVectorList::interpolateValue(int index, const Base::Vector3f &from, const Base::Vector3f &to, float t)
{
    if (from != to) {
        Base::Vector3f v((to.x - from.x) * t + from.x,
                         (to.y - from.y) * t + from.y,
                         (to.z - from.z) * t + from.z);
        set1Value(index, v);
    }
}

//**************************************************************************
//**************************************************************************
// PropertyMatrix
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyMatrix , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyMatrix::PropertyMatrix() = default;


PropertyMatrix::~PropertyMatrix() = default;

//**************************************************************************
// Base class implementer


void PropertyMatrix::setValue(const Base::Matrix4D &mat)
{
    aboutToSetValue();
    _cMat=mat;
    hasSetValue();
}


const Base::Matrix4D & PropertyMatrix::getValue()const
{
    return _cMat;
}

PyObject *PropertyMatrix::getPyObject()
{
    return new Base::MatrixPy(_cMat);
}

void PropertyMatrix::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(Base::PlacementPy::Type))) {
        Base::PlacementPy  *pcObject = static_cast<Base::PlacementPy*>(value);
        setValue( pcObject->value().toMatrix() );
    }
    else if (PyObject_TypeCheck(value, &(Base::MatrixPy::Type))) {
        Base::MatrixPy  *pcObject = static_cast<Base::MatrixPy*>(value);
        setValue( pcObject->value() );
    }
    else if (PyTuple_Check(value)&&PyTuple_Size(value)==16) {
        PyObject* item;
        Base::Matrix4D cMatrix;

        for (int x=0; x<4;x++) {
            for (int y=0; y<4;y++) {
                item = PyTuple_GetItem(value,x+y*4);
                if (PyFloat_Check(item))
                    cMatrix[x][y] = PyFloat_AsDouble(item);
                else if (PyLong_Check(item))
                    cMatrix[x][y] = (double)PyLong_AsLong(item);
                else
                    throw Base::TypeError("Not allowed type used in matrix tuple (a number expected)...");
            }
        }

        setValue( cMatrix );
    }
    else {
        std::string error = std::string("type must be 'Matrix' or tuple of 16 float or int, not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyMatrix::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<PropertyMatrix";
    writer.Stream() << " a11=\"" <<  _cMat[0][0] << "\" a12=\"" <<  _cMat[0][1] << "\" a13=\"" <<  _cMat[0][2] << "\" a14=\"" <<  _cMat[0][3] << "\"";
    writer.Stream() << " a21=\"" <<  _cMat[1][0] << "\" a22=\"" <<  _cMat[1][1] << "\" a23=\"" <<  _cMat[1][2] << "\" a24=\"" <<  _cMat[1][3] << "\"";
    writer.Stream() << " a31=\"" <<  _cMat[2][0] << "\" a32=\"" <<  _cMat[2][1] << "\" a33=\"" <<  _cMat[2][2] << "\" a34=\"" <<  _cMat[2][3] << "\"";
    writer.Stream() << " a41=\"" <<  _cMat[3][0] << "\" a42=\"" <<  _cMat[3][1] << "\" a43=\"" <<  _cMat[3][2] << "\" a44=\"" <<  _cMat[3][3] << "\"";
    writer.Stream() <<"/>\n";
}

void PropertyMatrix::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyMatrix");
    // get the value of my Attribute
    aboutToSetValue();
    _cMat[0][0] = reader.getAttributeAsFloat("a11");
    _cMat[0][1] = reader.getAttributeAsFloat("a12");
    _cMat[0][2] = reader.getAttributeAsFloat("a13");
    _cMat[0][3] = reader.getAttributeAsFloat("a14");

    _cMat[1][0] = reader.getAttributeAsFloat("a21");
    _cMat[1][1] = reader.getAttributeAsFloat("a22");
    _cMat[1][2] = reader.getAttributeAsFloat("a23");
    _cMat[1][3] = reader.getAttributeAsFloat("a24");

    _cMat[2][0] = reader.getAttributeAsFloat("a31");
    _cMat[2][1] = reader.getAttributeAsFloat("a32");
    _cMat[2][2] = reader.getAttributeAsFloat("a33");
    _cMat[2][3] = reader.getAttributeAsFloat("a34");

    _cMat[3][0] = reader.getAttributeAsFloat("a41");
    _cMat[3][1] = reader.getAttributeAsFloat("a42");
    _cMat[3][2] = reader.getAttributeAsFloat("a43");
    _cMat[3][3] = reader.getAttributeAsFloat("a44");
    hasSetValue();
}


Property *PropertyMatrix::Copy() const
{
    PropertyMatrix *p= new PropertyMatrix();
    p->_cMat = _cMat;
    return p;
}

void PropertyMatrix::Paste(const Property &from)
{
    aboutToSetValue();
    _cMat = dynamic_cast<const PropertyMatrix&>(from)._cMat;
    hasSetValue();
}

//**************************************************************************
// PropertyMatrixList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyMatrixList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction

PropertyMatrixList::PropertyMatrixList()
{

}

PropertyMatrixList::~PropertyMatrixList()
{

}

//**************************************************************************
// Base class implementer

PyObject *PropertyMatrixList::getPyObject(void)
{
    PyObject* list = PyList_New( getSize() );

    for (int i = 0;i<getSize(); i++)
        PyList_SetItem( list, i, new Base::MatrixPy(new Base::Matrix4D(_lValueList[i])));

    return list;
}

Base::Matrix4D PropertyMatrixList::getPyValue(PyObject *item) const {
    PropertyMatrix val;
    val.setPyObject( item );
    return val.getValue();
}

bool PropertyMatrixList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(const auto &m : _lValueList) {
        writer.Stream() << m[0][0] << ' ' << m[0][1] << ' ' << m[0][2] << ' ' << m[0][3] << '\n'
                        << m[1][0] << ' ' << m[1][1] << ' ' << m[1][2] << ' ' << m[1][3] << '\n'
                        << m[2][0] << ' ' << m[2][1] << ' ' << m[2][2] << ' ' << m[2][3] << '\n'
                        << m[3][0] << ' ' << m[3][1] << ' ' << m[3][2] << ' ' << m[3][3] << "\n\n";
    }
    return false;
}

void PropertyMatrixList::restoreXML(Base::XMLReader &reader)
{
    unsigned count = reader.getAttributeAsUnsigned("count");
    auto &s = reader.beginCharStream(false);
    std::vector<Base::Matrix4D> values;
    values.reserve(count);
    for (unsigned i=0; i<count; ++i) {
        double a11, a12, a13, a14,
               a21, a22, a23, a24,
               a31, a32, a33, a34,
               a41, a42, a43, a44;
        s >> a11 >> a12 >> a13 >> a14
          >> a21 >> a22 >> a23 >> a24
          >> a31 >> a32 >> a33 >> a34
          >> a41 >> a42 >> a43 >> a44;
        values.emplace_back(a11, a12, a13, a14,
                            a21, a22, a23, a24,
                            a31, a32, a33, a34,
                            a41, a42, a43, a44);
    }
    reader.endCharStream();
    setValues(std::move(values));
}

void PropertyMatrixList::saveStream(Base::OutputStream &str) const
{
    for (auto & m : _lValueList) {
        str << m[0][0] << m[0][1] << m[0][2] << m[0][3]
            << m[1][0] << m[1][1] << m[1][2] << m[1][3]
            << m[2][0] << m[2][1] << m[2][2] << m[2][3]
            << m[3][0] << m[3][1] << m[3][2] << m[3][3];
    }
}

void PropertyMatrixList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Base::Matrix4D> values;
    values.reserve(uCt);
    for (unsigned i=0; i<uCt; ++i) {
        double a11, a12, a13, a14,
               a21, a22, a23, a24,
               a31, a32, a33, a34,
               a41, a42, a43, a44;
        str >> a11 >> a12 >> a13 >> a14
            >> a21 >> a22 >> a23 >> a24
            >> a31 >> a32 >> a33 >> a34
            >> a41 >> a42 >> a43 >> a44;
        values.emplace_back(a11, a12, a13, a14,
                            a21, a22, a23, a24,
                            a31, a32, a33, a34,
                            a41, a42, a43, a44);
    }
    setValues(std::move(values));
}

Property *PropertyMatrixList::Copy(void) const
{
    PropertyMatrixList *p= new PropertyMatrixList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyMatrixList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyMatrixList&>(from)._lValueList);
}

unsigned int PropertyMatrixList::getMemSize (void) const
{
    return static_cast<unsigned int>(_lValueList.size() * sizeof(Base::Matrix4D));
}



//**************************************************************************
//**************************************************************************
// PropertyPlacement
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPlacement , App::Property)

//**************************************************************************
// Construction/Destruction


PropertyPlacement::PropertyPlacement() = default;


PropertyPlacement::~PropertyPlacement() = default;

//**************************************************************************
// Base class implementer


void PropertyPlacement::setValue(const Base::Placement &pos)
{
    aboutToSetValue();
    _cPos=pos;
    hasSetValue();
}

bool PropertyPlacement::setValueIfChanged(const Base::Placement &pos,double tol,double atol)
{
    if(_cPos.getPosition().IsEqual(pos.getPosition(),tol)
            && _cPos.getRotation().isSame(pos.getRotation(),atol))
    {
        return false;
    }
    setValue(pos);
    return true;
}


const Base::Placement & PropertyPlacement::getValue()const
{
    return _cPos;
}

void PropertyPlacement::getPaths(std::vector<ObjectIdentifier> &paths) const
{
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Base"))
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("x")));
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Base"))
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("y")));
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Base"))
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("z")));
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Rotation"))
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Angle")));

    // Unlike the above path (which provides units that are not available
    // through python, the following paths provides the same value. They are no
    // longer needed, because the expression completer will now dig into all
    // python attributes.

    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Rotation"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("x")));
    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Rotation"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("y")));
    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Rotation"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("z")));
}

namespace {
double toDouble(const App::any &value)
{
    double avalue{};

    if (value.type() == typeid(Base::Quantity))
        avalue = App::any_cast<Base::Quantity>(value).getValue();
    else if (value.type() == typeid(double))
        avalue = App::any_cast<double>(value);
    else if (value.type() == typeid(int))
        avalue =  App::any_cast<int>(value);
    else if (value.type() == typeid(unsigned int))
        avalue =  App::any_cast<unsigned int >(value);
    else if (value.type() == typeid(short))
        avalue =  App::any_cast<short>(value);
    else if (value.type() == typeid(unsigned short))
        avalue =  App::any_cast<unsigned short>(value);
    else if (value.type() == typeid(long))
        avalue =  App::any_cast<long>(value);
    else if (value.type() == typeid(unsigned long))
        avalue =  App::any_cast<unsigned long>(value);
    else
        throw std::bad_cast();
    return avalue;
}
}

void PropertyPlacement::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    auto updateAxis = [=](int index, double coord) {
        Base::Vector3d axis;
        double angle;
        Base::Vector3d base = _cPos.getPosition();
        Base::Rotation rot = _cPos.getRotation();
        rot.getRawValue(axis, angle);
        axis[index] = coord;
        rot.setValue(axis, angle);
        Base::Placement plm(base, rot);
        setValue(plm);
    };

    auto updateYawPitchRoll = [=](int index, double angle) {
        Base::Vector3d base = _cPos.getPosition();
        Base::Rotation rot = _cPos.getRotation();
        double yaw, pitch, roll;
        rot.getYawPitchRoll(yaw, pitch, roll);
        if (index == 0) {
            if (angle < -180.0 || angle > 180.0)
                throw Base::ValueError("Yaw angle is out of range [-180, +180]");
            yaw = angle;
        }
        else if (index == 1) {
            if (angle < -90.0 || angle > 90.0)
                throw Base::ValueError("Pitch angle is out of range [-90, +90]");
            pitch = angle;
        }
        else if (index == 2) {
            if (angle < -180.0 || angle > 180.0)
                throw Base::ValueError("Roll angle is out of range [-180, +180]");
            roll = angle;
        }
        rot.setYawPitchRoll(yaw, pitch, roll);
        Base::Placement plm(base, rot);
        setValue(plm);
    };

    std::string subpath = path.getSubPathStr();
    if (subpath == ".Rotation.Angle") {
        double avalue = toDouble(value);
        Property::setPathValue(path, Base::toRadians(avalue));
    }
    else if (subpath == ".Rotation.Axis.x") {
        updateAxis(0, toDouble(value));
    }
    else if (subpath == ".Rotation.Axis.y") {
        updateAxis(1, toDouble(value));
    }
    else if (subpath == ".Rotation.Axis.z") {
        updateAxis(2, toDouble(value));
    }
    else if (subpath == ".Rotation.Yaw") {
        updateYawPitchRoll(0, toDouble(value));
    }
    else if (subpath == ".Rotation.Pitch") {
        updateYawPitchRoll(1, toDouble(value));
    }
    else if (subpath == ".Rotation.Roll") {
        updateYawPitchRoll(2, toDouble(value));
    }
    else {
        Property::setPathValue(path, value);
    }
}

App::any PropertyPlacement::getPathValue(const ObjectIdentifier &path) const
{
    auto getAxis = [](const Base::Placement& plm) {
        Base::Vector3d axis;
        double angle;
        const Base::Rotation& rot = plm.getRotation();
        rot.getRawValue(axis, angle);
        return axis;
    };

    auto getYawPitchRoll = [](const Base::Placement& plm) {
        Base::Vector3d ypr;
        const Base::Rotation& rot = plm.getRotation();
        rot.getYawPitchRoll(ypr.x, ypr.y, ypr.z);
        return ypr;
    };

    std::string p = path.getSubPathStr();

    if (p == ".Rotation.Angle") {
        // Convert angle to degrees
        return Base::Quantity(Base::toDegrees(App::any_cast<double>(Property::getPathValue(path))), Unit::Angle);
    }
    else if (p == ".Base.x" || p == ".Base.y" || p == ".Base.z") {
        // Convert double to quantity
        return Base::Quantity(App::any_cast<double>(Property::getPathValue(path)), Unit::Length);
    }
    else if (p == ".Rotation.Axis.x") {
        return getAxis(_cPos).x;
    }
    else if (p == ".Rotation.Axis.y") {
        return getAxis(_cPos).y;
    }
    else if (p == ".Rotation.Axis.z") {
        return getAxis(_cPos).z;
    }
    else if (p == ".Rotation.Yaw") {
        return getYawPitchRoll(_cPos).x;
    }
    else if (p == ".Rotation.Pitch") {
        return getYawPitchRoll(_cPos).y;
    }
    else if (p == ".Rotation.Roll") {
        return getYawPitchRoll(_cPos).z;
    }
    else {
        return Property::getPathValue(path);
    }
}

bool PropertyPlacement::getPyPathValue(const ObjectIdentifier &path, Py::Object &res) const
{
    auto getAxis = [](const Base::Placement& plm) {
        Base::Vector3d axis;
        double angle;
        const Base::Rotation& rot = plm.getRotation();
        rot.getRawValue(axis, angle);
        return axis;
    };

    auto getYawPitchRoll = [](const Base::Placement& plm) {
        Base::Vector3d ypr;
        const Base::Rotation& rot = plm.getRotation();
        rot.getYawPitchRoll(ypr.x, ypr.y, ypr.z);
        return ypr;
    };

    std::string p = path.getSubPathStr();
    if (p == ".Rotation.Angle") {
        Base::Vector3d axis; double angle;
        _cPos.getRotation().getValue(axis,angle);
        res = Py::asObject(new QuantityPy(new Quantity(Base::toDegrees(angle),Unit::Angle)));
        return true;
    }
    else if (p == ".Base.x") {
        res = Py::asObject(new QuantityPy(new Quantity(_cPos.getPosition().x,Unit::Length)));
        return true;
    }
    else if (p == ".Base.y") {
        res = Py::asObject(new QuantityPy(new Quantity(_cPos.getPosition().y,Unit::Length)));
        return true;
    }
    else if (p == ".Base.z") {
        res = Py::asObject(new QuantityPy(new Quantity(_cPos.getPosition().z,Unit::Length)));
        return true;
    }
    else if (p == ".Rotation.Axis.x") {
        res = Py::Float(getAxis(_cPos).x);
        return true;
    }
    else if (p == ".Rotation.Axis.y") {
        res = Py::Float(getAxis(_cPos).y);
        return true;
    }
    else if (p == ".Rotation.Axis.z") {
        res = Py::Float(getAxis(_cPos).z);
        return true;
    }
    else if (p == ".Rotation.Yaw") {
        res = Py::Float(getYawPitchRoll(_cPos).x);
        return true;
    }
    else if (p == ".Rotation.Pitch") {
        res = Py::Float(getYawPitchRoll(_cPos).y);
        return true;
    }
    else if (p == ".Rotation.Roll") {
        res = Py::Float(getYawPitchRoll(_cPos).z);
        return true;
    }

    return false;
}

PyObject *PropertyPlacement::getPyObject()
{
    return new Base::PlacementPy(new Base::Placement(_cPos));
}

void PropertyPlacement::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(Base::MatrixPy::Type))) {
        Base::MatrixPy  *pcObject = static_cast<Base::MatrixPy*>(value);
        Base::Matrix4D mat = pcObject->value();
        Base::Placement p;
        p.fromMatrix(mat);
        setValue(p);
    }
    else if (PyObject_TypeCheck(value, &(Base::PlacementPy::Type))) {
        setValue(*static_cast<Base::PlacementPy*>(value)->getPlacementPtr());
    }
    else {
        std::string error = std::string("type must be 'Matrix' or 'Placement', not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyPlacement::Save (Base::Writer &writer) const
{
    Vector3d axis;
    double rfAngle;
    _cPos.getRotation().getRawValue(axis, rfAngle);

    writer.Stream() << writer.ind() << "<PropertyPlacement"
                    << " Px=\"" <<  _cPos.getPosition().x 
                    << "\" Py=\"" <<  _cPos.getPosition().y
                    << "\" Pz=\"" <<  _cPos.getPosition().z

                    << "\" Q0=\"" <<  _cPos.getRotation()[0]
                    << "\" Q1=\"" <<  _cPos.getRotation()[1]
                    << "\" Q2=\"" <<  _cPos.getRotation()[2]
                    << "\" Q3=\"" <<  _cPos.getRotation()[3]

                    << "\" A=\"" <<  rfAngle

                    << "\" Ox=\"" <<  axis.x
                    << "\" Oy=\"" <<  axis.y
                    << "\" Oz=\"" <<  axis.z 

                    << "\"/>\n";
}

void PropertyPlacement::Restore(Base::XMLReader &reader)
{
    // read my Element
    reader.readElement("PropertyPlacement");
    // get the value of my Attribute
    aboutToSetValue();

    if (reader.hasAttribute("A")) {
        _cPos = Base::Placement(Vector3d(reader.getAttributeAsFloat("Px"),
                                         reader.getAttributeAsFloat("Py"),
                                         reader.getAttributeAsFloat("Pz")),
                       Rotation(Vector3d(reader.getAttributeAsFloat("Ox"),
                                         reader.getAttributeAsFloat("Oy"),
                                         reader.getAttributeAsFloat("Oz")),
                                reader.getAttributeAsFloat("A")));
    }
    else {
        _cPos = Base::Placement(Vector3d(reader.getAttributeAsFloat("Px"),
                                         reader.getAttributeAsFloat("Py"),
                                         reader.getAttributeAsFloat("Pz")),
                                Rotation(reader.getAttributeAsFloat("Q0"),
                                         reader.getAttributeAsFloat("Q1"),
                                         reader.getAttributeAsFloat("Q2"),
                                         reader.getAttributeAsFloat("Q3")));
    }

    hasSetValue();
}


Property *PropertyPlacement::Copy() const
{
    PropertyPlacement *p= new PropertyPlacement();
    p->_cPos = _cPos;
    return p;
}

void PropertyPlacement::Paste(const Property &from)
{
    aboutToSetValue();
    _cPos = dynamic_cast<const PropertyPlacement&>(from)._cPos;
    hasSetValue();
}

static inline Base::Placement _interpolate(const Base::Placement from, const Base::Placement to, float t)
{
    return Base::Placement(_interpolate(from.getPosition(), to.getPosition(), t),
                           Base::Rotation::slerp(from.getRotation(), to.getRotation(), t));
}

void PropertyPlacement::interpolate(const Property &from, const Property &to, float t)
{
    const auto &fromValue = dynamic_cast<const PropertyPlacement&>(from).getValue();
    const auto &toValue = dynamic_cast<const PropertyPlacement&>(to).getValue();
    if (fromValue != toValue)
        setValue(_interpolate(fromValue, toValue, t));
}

//**************************************************************************
// PropertyPlacementList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPlacementList , App::PropertyLists)

//**************************************************************************
// Construction/Destruction

PropertyPlacementList::PropertyPlacementList() = default;

PropertyPlacementList::~PropertyPlacementList() = default;

//**************************************************************************
// Base class implementer

PyObject *PropertyPlacementList::getPyObject()
{
    PyObject* list = PyList_New( getSize() );

    for (int i = 0;i<getSize(); i++)
        PyList_SetItem( list, i, new Base::PlacementPy(new Base::Placement(_lValueList[i])));

    return list;
}

Base::Placement PropertyPlacementList::getPyValue(PyObject *item) const {
    PropertyPlacement val;
    val.setPyObject( item );
    return val.getValue();
}

bool PropertyPlacementList::saveXML(Base::Writer &writer) const
{
    writer.Stream() << ">\n";
    for(const auto &v : _lValueList) {
        Vector3d axis;
        double fAngle;
        v.getRotation().getValue(axis, fAngle);
        writer.Stream() << v.getPosition().x  << ' '
                        << v.getPosition().y << ' '
                        << v.getPosition().z << ' '
                        << fAngle << ' '
                        << axis.x << ' '
                        << axis.y << ' '
                        << axis.z << '\n';
    }
    return false;
}

void PropertyPlacementList::restoreXML(Base::XMLReader &reader)
{
    unsigned count = reader.getAttributeAsUnsigned("count");
    auto &s = reader.beginCharStream(false);
    std::vector<Base::Placement> values(count);
    for(auto &v : values) {
        Base::Vector3d pos,axis;
        double rfAngle;
        s >> pos.x >> pos.y >> pos.z >> rfAngle >> axis.x >> axis.y >> axis.z;
        v.setRotation(Base::Rotation(axis,rfAngle));
        v.setPosition(pos);
    }
    reader.endCharStream();
    setValues(std::move(values));
}

void PropertyPlacementList::saveStream(Base::OutputStream &str) const
{
    if (!isSinglePrecision()) {
        for (std::vector<Base::Placement>::const_iterator it = _lValueList.begin(); it != _lValueList.end(); ++it) {
            str << it->getPosition().x << it->getPosition().y << it->getPosition().z
                << it->getRotation()[0] << it->getRotation()[1] << it->getRotation()[2] << it->getRotation()[3] ;
        }
    }
    else {
        for (std::vector<Base::Placement>::const_iterator it = _lValueList.begin(); it != _lValueList.end(); ++it) {
            float x = (float)it->getPosition().x;
            float y = (float)it->getPosition().y;
            float z = (float)it->getPosition().z;
            float q0 = (float)it->getRotation()[0];
            float q1 = (float)it->getRotation()[1];
            float q2 = (float)it->getRotation()[2];
            float q3 = (float)it->getRotation()[3];
            str << x << y << z << q0 << q1 << q2 << q3;
        }
    }
}

void PropertyPlacementList::restoreStream(Base::InputStream &str, unsigned uCt)
{
    std::vector<Base::Placement> values(uCt);
    if (!isSinglePrecision()) {
        for (std::vector<Base::Placement>::iterator it = values.begin(); it != values.end(); ++it) {
            Base::Vector3d pos;
            double q0, q1, q2, q3;
            str >> pos.x >> pos.y >> pos.z >> q0 >> q1 >> q2 >> q3;
            Base::Rotation rot(q0,q1,q2,q3);
            it->setPosition(pos);
            it->setRotation(rot);
        }
    }
    else {
        float x,y,z,q0,q1,q2,q3;
        for (std::vector<Base::Placement>::iterator it = values.begin(); it != values.end(); ++it) {
            str >> x >> y >> z >> q0 >> q1 >> q2 >> q3;
            Base::Vector3d pos(x, y, z);
            Base::Rotation rot(q0,q1,q2,q3);
            it->setPosition(pos);
            it->setRotation(rot);
        }
    }
    setValues(std::move(values));
}

Property *PropertyPlacementList::Copy() const
{
    PropertyPlacementList *p= new PropertyPlacementList();
    p->_lValueList = _lValueList;
    return p;
}

void PropertyPlacementList::Paste(const Property &from)
{
    setValues(dynamic_cast<const PropertyPlacementList&>(from)._lValueList);
}

unsigned int PropertyPlacementList::getMemSize () const
{
    return static_cast<unsigned int>(_lValueList.size() * sizeof(Base::Placement));
}

void PropertyPlacementList::interpolateValue(int index, const Base::Placement &from, const Base::Placement &to, float t)
{
    if (from != to) {
        set1Value(index, _interpolate(from, to, t));
    }
}

//**************************************************************************
//**************************************************************************
// PropertyPlacement
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyPlacementLink , App::PropertyLink)

//**************************************************************************
// Construction/Destruction


PropertyPlacementLink::PropertyPlacementLink() = default;


PropertyPlacementLink::~PropertyPlacementLink() = default;

App::Placement * PropertyPlacementLink::getPlacementObject() const
{
    if (_pcLink->getTypeId().isDerivedFrom(App::Placement::getClassTypeId()))
        return dynamic_cast<App::Placement*>(_pcLink);
    else
        return nullptr;

}

//**************************************************************************
// Base class implementer

Property *PropertyPlacementLink::Copy() const
{
    PropertyPlacementLink *p= new PropertyPlacementLink();
    p->_pcLink = _pcLink;
    return p;
}

void PropertyPlacementLink::Paste(const Property &from)
{
    aboutToSetValue();
    _pcLink = dynamic_cast<const PropertyPlacementLink&>(from)._pcLink;
    hasSetValue();
}

//**************************************************************************
//**************************************************************************
// PropertyRotation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyRotation , App::Property)

PropertyRotation::PropertyRotation() = default;


PropertyRotation::~PropertyRotation() = default;

void PropertyRotation::setValue(const Base::Rotation &rot)
{
    aboutToSetValue();
    _rot = rot;
    hasSetValue();
}

bool PropertyRotation::setValueIfChanged(const Base::Rotation &rot, double atol)
{
    if (_rot.isSame(rot, atol)) {
        return false;
    }

    setValue(rot);
    return true;
}


const Base::Rotation & PropertyRotation::getValue() const
{
    return _rot;
}

void PropertyRotation::getPaths(std::vector<ObjectIdentifier> &paths) const
{
    paths.push_back(ObjectIdentifier(*this)
                    << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Angle")));


    // Unlike the above path (which provides units that are not available
    // through python, the following paths provides the same value. They are no
    // longer needed, because the expression completer will now dig into all
    // python attributes.
    //
    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("x")));
    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("y")));
    // paths.push_back(ObjectIdentifier(*this)
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("Axis"))
    //                 << ObjectIdentifier::SimpleComponent(ObjectIdentifier::String("z")));
}

void PropertyRotation::setPathValue(const ObjectIdentifier &path, const App::any &value)
{
    auto updateAxis = [=](int index, double coord) {
        Base::Vector3d axis;
        double angle;
        _rot.getRawValue(axis, angle);

        axis[index] = coord;
        setValue(Base::Rotation{axis, angle});
    };

    std::string subpath = path.getSubPathStr();
    if (subpath == ".Angle") {
        double avalue = toDouble(value);
        Property::setPathValue(path, Base::toRadians(avalue));
    }
    else if (subpath == ".Axis.x") {
        updateAxis(0, toDouble(value));
    }
    else if (subpath == ".Axis.y") {
        updateAxis(1, toDouble(value));
    }
    else if (subpath == ".Axis.z") {
        updateAxis(2, toDouble(value));
    }
    else {
        Property::setPathValue(path, value);
    }
}

App::any PropertyRotation::getPathValue(const ObjectIdentifier &path) const
{
    auto getAxis = [](const Base::Rotation& rot) {
        Base::Vector3d axis;
        double angle;
        rot.getRawValue(axis, angle);
        return axis;
    };
    std::string p = path.getSubPathStr();

    if (p == ".Angle") {
        // Convert angle to degrees
        return Base::Quantity(Base::toDegrees(App::any_cast<double>(Property::getPathValue(path))), Unit::Angle);
    }
    else if (p == ".Axis.x") {
        return getAxis(_rot).x;
    }
    else if (p == ".Axis.y") {
        return getAxis(_rot).y;
    }
    else if (p == ".Axis.z") {
        return getAxis(_rot).z;
    }
    else {
        return Property::getPathValue(path);
    }
}

bool PropertyRotation::getPyPathValue(const ObjectIdentifier &path, Py::Object &res) const
{
    auto getAxis = [](const Base::Rotation& rot) {
        Base::Vector3d axis;
        double angle;
        rot.getRawValue(axis, angle);
        return axis;
    };

    std::string p = path.getSubPathStr();
    if (p == ".Angle") {
        Base::Vector3d axis; double angle;
        _rot.getValue(axis,angle);
        res = Py::asObject(new QuantityPy(new Quantity(Base::toDegrees(angle),Unit::Angle)));
        return true;
    }
    else if (p == ".Axis.x") {
        res = Py::Float(getAxis(_rot).x);
        return true;
    }
    else if (p == ".Axis.y") {
        res = Py::Float(getAxis(_rot).y);
        return true;
    }
    else if (p == ".Axis.z") {
        res = Py::Float(getAxis(_rot).z);
        return true;
    }

    return false;
}

PyObject *PropertyRotation::getPyObject()
{
    return new Base::RotationPy(new Base::Rotation(_rot));
}

void PropertyRotation::setPyObject(PyObject *value)
{
    if (PyObject_TypeCheck(value, &(Base::MatrixPy::Type))) {
        Base::MatrixPy *object = static_cast<Base::MatrixPy*>(value);
        Base::Matrix4D mat = object->value();
        Base::Rotation p;
        p.setValue(mat);
        setValue(p);
    }
    else if (PyObject_TypeCheck(value, &(Base::RotationPy::Type))) {
        setValue(*static_cast<Base::RotationPy*>(value)->getRotationPtr());
    }
    else {
        std::string error = std::string("type must be 'Matrix' or 'Rotation', not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyRotation::Save (Base::Writer &writer) const
{
    Vector3d axis;
    double rfAngle;
    _rot.getRawValue(axis, rfAngle);

    writer.Stream() << writer.ind() << "<PropertyRotation";
    writer.Stream() << " A=\"" <<  rfAngle << "\""
                    << " Ox=\"" <<  axis.x << "\""
                    << " Oy=\"" <<  axis.y << "\""
                    << " Oz=\"" <<  axis.z << "\""
                    << "/>\n";
}

void PropertyRotation::Restore(Base::XMLReader &reader)
{
    reader.readElement("PropertyRotation");
    aboutToSetValue();

    _rot = Rotation(Vector3d(reader.getAttributeAsFloat("Ox"),
                             reader.getAttributeAsFloat("Oy"),
                             reader.getAttributeAsFloat("Oz")),
                             reader.getAttributeAsFloat("A"));
    hasSetValue();
}

Property *PropertyRotation::Copy() const
{
    PropertyRotation *p = new PropertyRotation();
    p->_rot = _rot;
    return p;
}

void PropertyRotation::Paste(const Property &from)
{
    aboutToSetValue();
    _rot = dynamic_cast<const PropertyRotation&>(from)._rot;
    hasSetValue();
}

void PropertyRotation::interpolate(const Property &from, const Property &to, float t)
{
    const auto &fromValue = dynamic_cast<const PropertyRotation&>(from).getValue();
    const auto &toValue = dynamic_cast<const PropertyRotation&>(to).getValue();
    if (fromValue != toValue)
        setValue(Base::Rotation::slerp(fromValue, toValue, t));
}
// ------------------------------------------------------------

TYPESYSTEM_SOURCE_ABSTRACT(App::PropertyGeometry , App::Property)

PropertyGeometry::PropertyGeometry() = default;

PropertyGeometry::~PropertyGeometry() = default;

// ------------------------------------------------------------

TYPESYSTEM_SOURCE_ABSTRACT(App::PropertyComplexGeoData , App::PropertyGeometry)

PropertyComplexGeoData::PropertyComplexGeoData() = default;

PropertyComplexGeoData::~PropertyComplexGeoData() = default;

std::string PropertyComplexGeoData::getElementMapVersion(bool) const {
    auto data = getComplexData();
    if(!data)
        return std::string();
    auto owner = Base::freecad_dynamic_cast<DocumentObject>(getContainer());
    std::ostringstream ss;
    if(owner && owner->getDocument()
             && owner->getDocument()->getStringHasher()==data->Hasher)
        ss << "1.";
    else
        ss << "0.";
    ss << data->getElementMapVersion();
    return ss.str();
}

bool PropertyComplexGeoData::checkElementMapVersion(const char * ver) const
{
    auto data = getComplexData();
    if(!data)
        return false;
    auto owner = Base::freecad_dynamic_cast<DocumentObject>(getContainer());
    std::ostringstream ss;
    const char *prefix;
    if(owner && owner->getDocument()
             && owner->getDocument()->getStringHasher() == data->Hasher)
        prefix = "1.";
    else
        prefix = "0.";
    if (!boost::starts_with(ver, prefix))
        return true;
    return data->checkElementMapVersion(ver+2);
}

bool PropertyComplexGeoData::isSame(const Property &_other) const
{
    if(!_other.isDerivedFrom(PropertyComplexGeoData::getClassTypeId()))
        return false;
    auto data = getComplexData();
    auto other = static_cast<const PropertyComplexGeoData&>(_other).getComplexData();

    if(other == data)
        return true;
    if(!data || !other)
        return false;
    return data->isSame(*other);
}

void PropertyComplexGeoData::afterRestore()
{
    auto data = getComplexData();
    if (data && data->isRestoreFailed()) {
        data->resetRestoreFailure();
        auto owner = Base::freecad_dynamic_cast<DocumentObject>(getContainer());
        if (owner && owner->getDocument() && !owner->getDocument()->testStatus(App::Document::PartialDoc))
            owner->getDocument()->addRecomputeObject(owner);
    }
    PropertyGeometry::afterRestore();
}
