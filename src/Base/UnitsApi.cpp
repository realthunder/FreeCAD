/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <FreeCAD@juergen-riegel.net>         *
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
#ifdef __GNUC__
#include <unistd.h>
#endif

#include <CXX/WrapPython.h>
#include <memory>
#include <QCoreApplication>
#include <QString>
#include "Exception.h"

#include "UnitsApi.h"
#include "UnitsSchemaCentimeters.h"
#include "UnitsSchemaInternal.h"
#include "UnitsSchemaImperial1.h"
#include "UnitsSchemaMKS.h"
#include "UnitsSchemaMmMin.h"
#include "UnitsSchemaFemMilliMeterNewton.h"
#include "UnitsSchemaMeterDecimal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif
#ifndef DOUBLE_MAX
#define DOUBLE_MAX 1.7976931348623157E+308 /* max decimal value of a "double"*/
#endif
#ifndef DOUBLE_MIN
#define DOUBLE_MIN 2.2250738585072014E-308 /* min decimal value of a "double"*/
#endif

using namespace Base;

// === static attributes  ================================================

UnitsSchemaPtr UnitsApi::UserPrefSystem(new UnitsSchemaInternal());
UnitSystem UnitsApi::currentSystem = UnitSystem::SI1;

int UnitsApi::UserPrefDecimals = 2;

std::string UnitsApi::getDescription(UnitSystem system)
{
    // QT_TRANSLATE_NOOP keeps the literals where lupdate finds them, with one
    // translate() call for the whole switch.
    const char* description = QT_TRANSLATE_NOOP("UnitsApi", "Unknown schema");
    switch (system) {
        case UnitSystem::SI1:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Standard (mm, kg, s, degree)");
            break;
        case UnitSystem::SI2:
            description = QT_TRANSLATE_NOOP("UnitsApi", "MKS (m, kg, s, degree)");
            break;
        case UnitSystem::Imperial1:
            description = QT_TRANSLATE_NOOP("UnitsApi", "US customary (in, lb)");
            break;
        case UnitSystem::ImperialDecimal:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Imperial decimal (in, lb)");
            break;
        case UnitSystem::Centimeters:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Building Euro (cm, m², m³)");  // nonascii-ok
            break;
        case UnitSystem::ImperialBuilding:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Building US (ft-in, sqft, cft)");
            break;
        case UnitSystem::MmMin:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Metric small parts & CNC(mm, mm/min)");
            break;
        case UnitSystem::ImperialCivil:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Imperial for Civil Eng (ft, ft/sec)");
            break;
        case UnitSystem::FemMilliMeterNewton:
            description = QT_TRANSLATE_NOOP("UnitsApi", "FEM (mm, N, s)");
            break;
        case UnitSystem::MeterDecimal:
            description = QT_TRANSLATE_NOOP("UnitsApi", "Meter decimal (m, m², m³)");  // nonascii-ok
            break;
        default:
            break;
    }

    return QCoreApplication::translate("UnitsApi", description).toStdString();
}

UnitsSchemaPtr UnitsApi::createSchema(UnitSystem system)
{
    switch (system) {
        case UnitSystem::SI1:
            return std::make_unique<UnitsSchemaInternal>();
        case UnitSystem::SI2:
            return std::make_unique<UnitsSchemaMKS>();
        case UnitSystem::Imperial1:
            return std::make_unique<UnitsSchemaImperial1>();
        case UnitSystem::ImperialDecimal:
            return std::make_unique<UnitsSchemaImperialDecimal>();
        case UnitSystem::Centimeters:
            return std::make_unique<UnitsSchemaCentimeters>();
        case UnitSystem::ImperialBuilding:
            return std::make_unique<UnitsSchemaImperialBuilding>();
        case UnitSystem::MmMin:
            return std::make_unique<UnitsSchemaMmMin>();
        case UnitSystem::ImperialCivil:
            return std::make_unique<UnitsSchemaImperialCivil>();
        case UnitSystem::FemMilliMeterNewton:
            return std::make_unique<UnitsSchemaFemMilliMeterNewton>();
        case UnitSystem::MeterDecimal:
            return std::make_unique<UnitsSchemaMeterDecimal>();
        default:
            break;
    }

    return nullptr;
}

void UnitsApi::setSchema(UnitSystem system)
{
    if (UserPrefSystem) {
        UserPrefSystem->resetSchemaUnits();  // for schemas changed the Quantity constants
    }

    UserPrefSystem = createSchema(system);
    currentSystem = system;

    // for wrong value fall back to standard schema
    if (!UserPrefSystem) {
        UserPrefSystem = std::make_unique<UnitsSchemaInternal>();
        currentSystem = UnitSystem::SI1;
    }

    UserPrefSystem->setSchemaUnits();  // if necessary a unit schema can change the constants in
                                       // Quantity (e.g. mi=1.8km rather then 1.6km).
}

std::string UnitsApi::toString(const Base::Quantity& quantity, const QuantityFormat& format)
{
    // QString::arg formats numbers in the C locale, which is what this contract promises.
    QString value = QStringLiteral("'%1 %2'")
                        .arg(quantity.getValue(), 0, format.toFormat(), format.precision)
                        .arg(QString::fromStdString(quantity.getUnit().getString()));
    return value.toStdString();
}

std::string UnitsApi::toNumber(const Base::Quantity& quantity, const QuantityFormat& format)
{
    return toNumber(quantity.getValue(), format);
}

std::string UnitsApi::toNumber(double value, const QuantityFormat& format)
{
    QString number = QStringLiteral("%1").arg(value, 0, format.toFormat(), format.precision);
    return number.toStdString();
}

// return true if the current user schema uses multiple units for length (ex. Ft/In)
bool UnitsApi::isMultiUnitLength()
{
    return UserPrefSystem->isMultiUnitLength();
}

// return true if the current user schema uses multiple units for angles (ex. DMS)
bool UnitsApi::isMultiUnitAngle()
{
    return UserPrefSystem->isMultiUnitAngle();
}

std::string UnitsApi::getBasicLengthUnit()
{
    return UserPrefSystem->getBasicLengthUnit();
}

// === static translation methods ==========================================

std::string
UnitsApi::schemaTranslate(const Base::Quantity& quant, double& factor, std::string& unitString)
{
    return UserPrefSystem->schemaTranslate(quant, factor, unitString);
}

double UnitsApi::toDouble(PyObject* args, const Base::Unit& u)
{
    if (PyUnicode_Check(args)) {
        // Parse the string
        Quantity q = Quantity::parse(PyUnicode_AsUTF8(args));
        if (q.getUnit() == u) {
            return q.getValue();
        }
        THROWM(Base::UnitsMismatchError, "Wrong unit type!")
    }
    if (PyFloat_Check(args)) {
        return PyFloat_AsDouble(args);
    }
    if (PyLong_Check(args)) {
        return static_cast<double>(PyLong_AsLong(args));
    }

    THROWM(Base::UnitsMismatchError, "Wrong parameter type!")
}

Quantity UnitsApi::toQuantity(PyObject* args, const Base::Unit& u)
{
    double d {};
    if (PyUnicode_Check(args)) {
        // Parse the string
        Quantity q = Quantity::parse(PyUnicode_AsUTF8(args));
        d = q.getValue();
    }
    else if (PyFloat_Check(args)) {
        d = PyFloat_AsDouble(args);
    }
    else if (PyLong_Check(args)) {
        d = static_cast<double>(PyLong_AsLong(args));
    }
    else {
        THROWM(Base::UnitsMismatchError, "Wrong parameter type!")
    }

    return Quantity(d, u);
}

void UnitsApi::setDecimals(int prec)
{
    UserPrefDecimals = prec;
}

int UnitsApi::getDecimals()
{
    return UserPrefDecimals;
}
