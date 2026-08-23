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


#ifndef BASE_UNITSSCHEMA_H
#define BASE_UNITSSCHEMA_H

#include <memory>
#include <string>

#include <FCGlobal.h>

#include "UnitsSchemasSpecs.h"

namespace Base
{
class Quantity;

/** Units systems */
enum class UnitSystem
{
    SI1 = 0,             /** internal (mm,kg,s) SI system
                            (http://en.wikipedia.org/wiki/International_System_of_Units) */
    SI2 = 1,             /** MKS (m,kg,s) SI system */
    Imperial1 = 2,       /** the Imperial system (http://en.wikipedia.org/wiki/Imperial_units) */
    ImperialDecimal = 3, /** Imperial with length in inch only */
    Centimeters = 4,     /** All lengths in centimeters, areas and volumes in square/cubic meters */
    ImperialBuilding = 5, /** All lengths in feet + inches + fractions */
    MmMin = 6, /** Lengths in mm, Speed in mm/min. Angle in degrees. Useful for small parts & CNC */
    ImperialCivil = 7, /** Lengths in ft, Speed in ft/sec. Used in Civil Eng in North America */
    FemMilliMeterNewton = 8, /** Lengths in mm, Mass in t, TimeSpan in s, thus force is in N */
    MeterDecimal = 9,        /** Lengths in metres always */
    NumUnitSystemTypes       // must be the last item!
};
/** One schema, built from its specification.
 *
 * Every schema is the same class now; what makes one differ from another is
 * the data it is constructed from -- see UnitsSchemasData.h. The hand-written
 * subclass per schema this fork used to carry is gone.
 */
class BaseExport UnitsSchema
{
public:
    explicit UnitsSchema(UnitsSchemaSpec spec);
    UnitsSchema() = delete;

    [[nodiscard]] bool isMultiUnitLength() const;
    [[nodiscard]] bool isMultiUnitAngle() const;
    [[nodiscard]] std::string getBasicLengthUnit() const;
    [[nodiscard]] std::string getName() const;
    [[nodiscard]] std::string getDescription() const;
    [[nodiscard]] int getNum() const;

    std::string translate(const Quantity& quant) const;
    std::string translate(const Quantity& quant, double& factor, std::string& unitString) const;

private:
    [[nodiscard]] static std::string toLocale(
        const Quantity& quant,
        double factor,
        const std::string& unitString
    );

    UnitsSchemaSpec spec;
};



}  // namespace Base


#endif  // BASE_UNITSSCHEMA_H
