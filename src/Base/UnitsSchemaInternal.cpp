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

#include <QString>

#include "UnitsSchemaInternal.h"
#include <cmath>


using namespace Base;


QString
UnitsSchemaInternal::schemaTranslate(const Quantity& quant, double& factor, QString& unitString)
{
    double UnitValue = std::abs(quant.getValue());
    Unit unit = quant.getUnit();

    // In order to get the right factor always express the target
    // units as internal units where length is in mm and mass in kg
    // Example:
    // For W/mm/K we get the factor of 1000000.0 because
    // W/mm/K = kg*m^2/s^3/mm/K
    // = 10e6 * kg*mm^2/s^3/mm/K
    // = 10e6 * kg*mm/s^3/K

    // now do special treatment on all cases seems necessary:
    if (unit == Unit::Length) {  // Length handling ============================
        if (UnitValue < 1e-6) {  // smaller than 0.001 nm -> scientific notation
            unitString = QStringLiteral("mm");
            factor = 1.0;
        }
        else if (UnitValue < 1e-3) {
            unitString = QStringLiteral("nm");
            factor = 1e-6;
        }
        else if (UnitValue < 0.1) {
            static const QString unitUm = QString::fromUtf8("\xC2\xB5m");
            unitString = unitUm;;
            factor = 1e-3;
        }
        else if (UnitValue < 1e4) {
            unitString = QStringLiteral("mm");
            factor = 1.0;
        }
        else if (UnitValue < 1e7) {
            unitString = QStringLiteral("m");
            factor = 1e3;
        }
        else if (UnitValue < 1e10) {
            unitString = QStringLiteral("km");
            factor = 1e6;
        }
        else {  // bigger than 1000 km -> scientific notation
            unitString = QStringLiteral("m");
            factor = 1e3;
        }
    }
    else if (unit == Unit::Area) {
        if (UnitValue < 100) {
            unitString = QStringLiteral("mm^2");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("cm^2");
            factor = 100;
        }
        else if (UnitValue < 1e12) {
            unitString = QStringLiteral("m^2");
            factor = 1e6;
        }
        else {  // bigger than 1 square kilometer
            unitString = QStringLiteral("km^2");
            factor = 1e12;
        }
    }
    else if (unit == Unit::Volume) {
        if (UnitValue < 1e3) {  // smaller than 1 ul
            unitString = QStringLiteral("mm^3");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("ml");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("l");
            factor = 1e6;
        }
        else {  // bigger than 1000 l
            unitString = QStringLiteral("m^3");
            factor = 1e9;
        }
    }
    else if (unit == Unit::Angle) {
        // TODO Cascade for the Areas
        // default action for all cases without special treatment:
        unitString = getAngleUnit();
        factor = 1.0;
    }
    else if (unit == Unit::Mass) {
        if (UnitValue < 1e-6) {
            static const QString unitUg = QString::fromUtf8("\xC2\xB5g");
            unitString = unitUg;
            factor = 1e-9;
        }
        else if (UnitValue < 1e-3) {
            unitString = QStringLiteral("mg");
            factor = 1e-6;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("g");
            factor = 1e-3;
        }
        else if (UnitValue < 1e3) {
            unitString = QStringLiteral("kg");
            factor = 1.0;
        }
        else {
            unitString = QStringLiteral("t");
            factor = 1e3;
        }
    }
    else if (unit == Unit::Density) {
        if (UnitValue < 0.0001) {
            unitString = QStringLiteral("kg/m^3");
            factor = 1e-9;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("kg/cm^3");
            factor = 0.001;
        }
        else {
            unitString = QStringLiteral("kg/mm^3");
            factor = 1.0;
        }
    }
    else if (unit == Unit::ThermalConductivity) {
        if (UnitValue > 1e6) {
            unitString = QStringLiteral("W/mm/K");
            factor = 1e6;
        }
        else {
            unitString = QStringLiteral("W/m/K");
            factor = 1000.0;
        }
    }
    else if (unit == Unit::ThermalExpansionCoefficient) {
        if (UnitValue < 0.001) {
            static const QString unitUmmk = QString::fromUtf8("\xC2\xB5m/m/K");  // micro-meter/meter/K
            unitString = unitUmmk;
            factor = 1e-6;
        }
        else {
            unitString = QStringLiteral("mm/mm/K");
            factor = 1.0;
        }
    }
    else if (unit == Unit::VolumetricThermalExpansionCoefficient) {
        if (UnitValue < 0.001) {
            unitString = QStringLiteral("mm^3/m^3/K");
            factor = 1e-9;
        }
        else {
            unitString = QStringLiteral("m^3/m^3/K");
            factor = 1.0;
        }
    }
    else if (unit == Unit::SpecificHeat) {
        unitString = QStringLiteral("J/kg/K");
        factor = 1e6;
    }
    else if (unit == Unit::ThermalTransferCoefficient) {
        unitString = QStringLiteral("W/m^2/K");
        factor = 1.0;
    }
    else if ((unit == Unit::Pressure) || (unit == Unit::Stress)) {
        if (UnitValue < 10.0) {  // Pa is the smallest
            unitString = QStringLiteral("Pa");
            factor = 0.001;
        }
        else if (UnitValue < 10000.0) {
            unitString = QStringLiteral("kPa");
            factor = 1.0;
        }
        else if (UnitValue < 10000000.0) {
            unitString = QStringLiteral("MPa");
            factor = 1000.0;
        }
        else if (UnitValue < 10000000000.0) {
            unitString = QStringLiteral("GPa");
            factor = 1e6;
        }
        else {  // bigger -> scientific notation
            unitString = QStringLiteral("Pa");
            factor = 0.001;
        }
    }
    else if ((unit == Unit::Stiffness)) {
        if (UnitValue < 1) {  // mN/m is the smallest
            unitString = QStringLiteral("mN/m");
            factor = 1e-3;
        }
        if (UnitValue < 1e3) {
            unitString = QStringLiteral("N/m");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("kN/m");
            factor = 1e3;
        }
        else {
            unitString = QStringLiteral("MN/m");
            factor = 1e6;
        }
    }
    else if (unit == Unit::Force) {
        if (UnitValue < 1e3) {
            unitString = QStringLiteral("mN");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("N");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("kN");
            factor = 1e6;
        }
        else {
            unitString = QStringLiteral("MN");
            factor = 1e9;
        }
    }
    else if (unit == Unit::Power) {
        if (UnitValue < 1e6) {
            unitString = QStringLiteral("mW");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("W");
            factor = 1e6;
        }
        else {
            unitString = QStringLiteral("kW");
            factor = 1e9;
        }
    }
    else if (unit == Unit::ElectricPotential) {
        if (UnitValue < 1e6) {
            unitString = QStringLiteral("mV");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("V");
            factor = 1e6;
        }
        else if (UnitValue < 1e12) {
            unitString = QStringLiteral("kV");
            factor = 1e9;
        }
        else {  // > 1000 kV scientificc notation
            unitString = QStringLiteral("V");
            factor = 1e6;
        }
    }
    else if (unit == Unit::Work) {
        if (UnitValue < 1.602176634e-10) {
            unitString = QStringLiteral("eV");
            factor = 1.602176634e-13;
        }
        else if (UnitValue < 1.602176634e-7) {
            unitString = QStringLiteral("keV");
            factor = 1.602176634e-10;
        }
        else if (UnitValue < 1.602176634e-4) {
            unitString = QStringLiteral("MeV");
            factor = 1.602176634e-7;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("mJ");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("J");
            factor = 1e6;
        }
        else if (UnitValue < 1e12) {
            unitString = QStringLiteral("kJ");
            factor = 1e9;
        }
        else if (UnitValue < 3.6e+15) {
            unitString = QStringLiteral("kWh");
            factor = 3.6e+12;
        }
        else {  // bigger than 1000 kWh -> scientific notation
            unitString = QStringLiteral("J");
            factor = 1e6;
        }
    }
    else if (unit == Unit::SpecificEnergy) {
        unitString = QStringLiteral("m^2/s^2");
        factor = 1e6;
    }
    else if (unit == Unit::HeatFlux) {
        unitString = QStringLiteral("W/m^2");
        factor = 1;  //  unit signature (0,1,-3,0,0) is length independent
    }
    else if (unit == Unit::ElectricCharge) {
        unitString = QStringLiteral("C");
        factor = 1.0;
    }
    else if (unit == Unit::CurrentDensity) {
        if (UnitValue <= 1e3) {
            unitString = QStringLiteral("A/m^2");
            factor = 1e-6;
        }
        else {
            unitString = QStringLiteral("A/mm^2");
            factor = 1;
        }
    }
    else if (unit == Unit::MagneticFluxDensity) {
        if (UnitValue <= 1e-3) {
            unitString = QStringLiteral("G");
            factor = 1e-4;
        }
        else {
            unitString = QStringLiteral("T");
            factor = 1.0;
        }
    }
    else if (unit == Unit::MagneticFieldStrength) {
        unitString = QStringLiteral("A/m");
        factor = 1e-3;
    }
    else if (unit == Unit::MagneticFlux) {
        unitString = QStringLiteral("Wb");
        factor = 1e6;
    }
    else if (unit == Unit::Magnetization) {
        unitString = QStringLiteral("A/m");
        factor = 1e-3;
    }
    else if (unit == Unit::ElectricalConductance) {
        if (UnitValue < 1e-9) {
            static const QString unituS = QString::fromUtf8("\xC2\xB5S");
            unitString = unituS;
            factor = 1e-12;
        }
        else if (UnitValue < 1e-6) {
            unitString = QStringLiteral("mS");
            factor = 1e-9;
        }
        else {
            unitString = QStringLiteral("S");
            factor = 1e-6;
        }
    }
    else if (unit == Unit::ElectricalResistance) {
        if (UnitValue < 1e9) {
            unitString = QStringLiteral("Ohm");
            factor = 1e6;
        }
        else if (UnitValue < 1e12) {
            unitString = QStringLiteral("kOhm");
            factor = 1e9;
        }
        else {
            unitString = QStringLiteral("MOhm");
            factor = 1e12;
        }
    }
    else if (unit == Unit::ElectricalConductivity) {
        if (UnitValue < 1e-3) {
            unitString = QStringLiteral("mS/m");
            factor = 1e-12;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("S/m");
            factor = 1e-9;
        }
        else if (UnitValue < 1e3) {
            unitString = QStringLiteral("kS/m");
            factor = 1e-6;
        }
        else {
            unitString = QStringLiteral("MS/m");
            factor = 1e-3;
        }
    }
    else if (unit == Unit::ElectricalCapacitance) {
        if (UnitValue < 1e-15) {
            unitString = QStringLiteral("pF");
            factor = 1e-18;
        }
        else if (UnitValue < 1e-12) {
            unitString = QStringLiteral("nF");
            factor = 1e-15;
        }
        else if (UnitValue < 1e-9) {
            // \x reads everything to the end, therefore split
            static const QString unitpF = QString::fromUtf8("\xC2\xB5" "F");
            unitString = unitpF;
            factor = 1e-12;
        }
        else if (UnitValue < 1e-6) {
            unitString = QStringLiteral("mF");
            factor = 1e-9;
        }
        else {
            unitString = QStringLiteral("F");
            factor = 1e-6;
        }
    }
    else if (unit == Unit::ElectricalInductance) {
        if (UnitValue < 1.0) {
            unitString = QStringLiteral("nH");
            factor = 1e-3;
        }
        else if (UnitValue < 1e3) {
            static const QString unituH = QString::fromUtf8("\xC2\xB5H");
            unitString = unituH;
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("mH");
            factor = 1e3;
        }
        else {
            unitString = QStringLiteral("H");
            factor = 1e6;
        }
    }
    else if (unit == Unit::VacuumPermittivity) {
        unitString = QStringLiteral("F/m");
        factor = 1e-9;
    }
    else if (unit == Unit::Frequency) {
        if (UnitValue < 1e3) {
            unitString = QStringLiteral("Hz");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("kHz");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("MHz");
            factor = 1e6;
        }
        else if (UnitValue < 1e12) {
            unitString = QStringLiteral("GHz");
            factor = 1e9;
        }
        else {
            unitString = QStringLiteral("THz");
            factor = 1e12;
        }
    }
    else if (unit == Unit::Velocity) {
        unitString = QStringLiteral("mm/s");
        factor = 1.0;
    }
    else if (unit == Unit::DynamicViscosity) {
        unitString = QStringLiteral("Pa*s");
        factor = 0.001;
    }
    else if (unit == Unit::KinematicViscosity) {
        if (UnitValue < 1e3) {
            unitString = QStringLiteral("mm^2/s");
            factor = 1.0;
        }
        else {
            unitString = QStringLiteral("m^2/s");
            factor = 1e6;
        }
    }
    else if (unit == Unit::VolumeFlowRate) {
        if (UnitValue < 1e3) {
            unitString = QStringLiteral("mm^3/s");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            unitString = QStringLiteral("ml/s");
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("l/s");
            factor = 1e6;
        }
        else {
            unitString = QStringLiteral("m^3/s");
            factor = 1e9;
        }
    }
    else if (unit == Unit::DissipationRate) {
        unitString = QStringLiteral("m^2/s^3");
        factor = 1e6;
    }
    else if (unit == Unit::InverseLength) {
        if (UnitValue < 1e-6) {  // smaller than 0.001 1/km -> scientific notation
            unitString = QStringLiteral("1/m");
            factor = 1e-3;
        }
        else if (UnitValue < 1e-3) {
            unitString = QStringLiteral("1/km");
            factor = 1e-6;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("1/m");
            factor = 1e-3;
        }
        else if (UnitValue < 1e3) {
            unitString = QStringLiteral("1/mm");
            factor = 1.0;
        }
        else if (UnitValue < 1e6) {
            static const QString unit1um = QString::fromUtf8("1/\xC2\xB5m");
            unitString = unit1um;
            factor = 1e3;
        }
        else if (UnitValue < 1e9) {
            unitString = QStringLiteral("1/nm");
            factor = 1e6;
        }
        else {  // larger -> scientific notation
            unitString = QStringLiteral("1/m");
            factor = 1e-3;
        }
    }
    else if (unit == Unit::InverseArea) {
        if (UnitValue < 1e-12) {  // smaller than 0.001 1/km^2 -> scientific notation
            unitString = QStringLiteral("1/m^2");
            factor = 1e-6;
        }
        else if (UnitValue < 1e-6) {
            unitString = QStringLiteral("1/km^2");
            factor = 1e-12;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("1/m^2");
            factor = 1e-6;
        }
        else if (UnitValue < 1e2) {
            unitString = QStringLiteral("1/cm^2");
            factor = 1e-2;
        }
        else {
            unitString = QStringLiteral("1/mm^2");
            factor = 1.0;
        }
    }
    else if (unit == Unit::InverseVolume) {
        if (UnitValue < 1e-6) {
            unitString = QStringLiteral("1/m^3");
            factor = 1e-9;
        }
        else if (UnitValue < 1e-3) {
            unitString = QStringLiteral("1/l");
            factor = 1e-6;
        }
        else if (UnitValue < 1.0) {
            unitString = QStringLiteral("1/ml");
            factor = 1e-3;
        }
        else {
            unitString = QStringLiteral("1/mm^3");
            factor = 1.0;
        }
    }
    else {
        // default action for all cases without special treatment:
        unitString = quant.getUnit().getString();
        factor = 1.0;
    }

    return toLocale(quant, factor, unitString);
}
