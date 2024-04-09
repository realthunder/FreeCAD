/***************************************************************************
 *   Copyright (c) 2013 Jürgen Riegel <juergen.riegel@web.de>              *
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
#ifndef _PreComp_
#ifdef FC_OS_WIN32
#define _USE_MATH_DEFINES
#endif  // FC_OS_WIN32
#include <array>
#endif

#include <unordered_map>
#include <boost/functional/hash.hpp>

#include "Quantity.h"
#include "Exception.h"
#include "UnitsApi.h"
#include <boost/math/special_functions/fpclassify.hpp>

/** \defgroup Units Units system
    \ingroup BASE
    \brief The quantities and units system enables FreeCAD to work transparently with many different
   units
*/

// suppress annoying warnings from generated source files
#ifdef _MSC_VER
#pragma warning(disable : 4003)
#pragma warning(disable : 4018)
#pragma warning(disable : 4065)
#pragma warning(disable : 4273)
#pragma warning(disable : 4335)  // disable MAC file format warning on VC
#endif

using namespace Base;

// ====== Static attributes =========================
// NOLINTNEXTLINE
int QuantityFormat::defaultDenominator = 8;  // for 1/8"


QuantityFormat::QuantityFormat()
    : option(OmitGroupSeparator | RejectGroupSeparator)
    , format(Fixed)
    , precision(UnitsApi::getDecimals())
    , denominator(defaultDenominator)
{}

QuantityFormat::QuantityFormat(QuantityFormat::NumberFormat format, int decimals)
    : option(OmitGroupSeparator | RejectGroupSeparator)
    , format(format)
    , precision(decimals < 0 ? UnitsApi::getDecimals() : decimals)
    , denominator(defaultDenominator)
{}

// ----------------------------------------------------------------------------

Quantity::Quantity()
    : myValue {0.0}
{}

Quantity::Quantity(double value, const Unit& unit)
    : myValue {value}
    , myUnit {unit}
{}

Quantity::Quantity(double value, const QString& unit)
    : myValue {0.0}
{
    if (unit.isEmpty()) {
        this->myValue = value;
        this->myUnit = Unit();
        return;
    }

    try {
        auto tmpQty = parse(unit);
        this->myUnit = tmpQty.getUnit();
        this->myValue = value * tmpQty.getValue();
    }
    catch (const Base::ParserError&) {
        this->myValue = 0.0;
        this->myUnit = Unit();
    }
}

double Quantity::getValueAs(const Quantity& other) const
{
    return myValue / other.getValue();
}

bool Quantity::operator==(const Quantity& that) const
{
    return (this->myValue == that.myValue) && (this->myUnit == that.myUnit);
}

bool Quantity::operator!=(const Quantity& that) const
{
    return !(*this == that);
}

bool Quantity::operator<(const Quantity& that) const
{
    if (this->myUnit != that.myUnit) {
        throw Base::UnitsMismatchError(
            "Quantity::operator <(): quantities need to have same unit to compare");
    }

    return (this->myValue < that.myValue);
}

bool Quantity::operator>(const Quantity& that) const
{
    if (this->myUnit != that.myUnit) {
        throw Base::UnitsMismatchError(
            "Quantity::operator >(): quantities need to have same unit to compare");
    }

    return (this->myValue > that.myValue);
}

bool Quantity::operator<=(const Quantity& that) const
{
    if (this->myUnit != that.myUnit) {
        throw Base::UnitsMismatchError(
            "Quantity::operator <=(): quantities need to have same unit to compare");
    }

    return (this->myValue <= that.myValue);
}

bool Quantity::operator>=(const Quantity& that) const
{
    if (this->myUnit != that.myUnit) {
        throw Base::UnitsMismatchError(
            "Quantity::operator >=(): quantities need to have same unit to compare");
    }

    return (this->myValue >= that.myValue);
}

Quantity Quantity::operator*(const Quantity& other) const
{
    return Quantity(this->myValue * other.myValue, this->myUnit * other.myUnit);
}

Quantity Quantity::operator*(double factor) const
{
    return Quantity(this->myValue * factor, this->myUnit);
}

Quantity Quantity::operator/(const Quantity& other) const
{
    return Quantity(this->myValue / other.myValue, this->myUnit / other.myUnit);
}

Quantity Quantity::operator/(double factor) const
{
    return Quantity(this->myValue / factor, this->myUnit);
}

Quantity Quantity::pow(const Quantity& other) const
{
    if (!other.myUnit.isEmpty()) {
        throw Base::UnitsMismatchError("Quantity::pow(): exponent must not have a unit");
    }

    return Quantity(std::pow(this->myValue, other.myValue),
                    this->myUnit.pow(static_cast<signed char>(other.myValue)));
}

Quantity Quantity::pow(double exp) const
{
    return Quantity(std::pow(this->myValue, exp), this->myUnit.pow(exp));
}

Quantity Quantity::concat(const Quantity &other) const
{
    if (this->myUnit != other.myUnit)
        throw Base::UnitsMismatchError("Quantity: Unit mismatch in concat operation");
    if(myValue >= 0.0)
        return Quantity(this->myValue + other.myValue,this->myUnit);
    else
        return Quantity(this->myValue - other.myValue,this->myUnit);
}

Quantity Quantity::operator+(const Quantity& other) const
{
    if (!this->myUnit.isEmpty() && !other.myUnit.isEmpty() && this->myUnit != other.myUnit)
        throw Base::UnitsMismatchError("Quantity::operator +(): Unit mismatch in plus operation");
    return Quantity(this->myValue + other.myValue, this->myUnit.isEmpty() ? other.myUnit : this->myUnit);
}

Quantity& Quantity::operator+=(const Quantity& other)
{
    if (!this->myUnit.isEmpty() && !other.myUnit.isEmpty() && this->myUnit != other.myUnit) {
        throw Base::UnitsMismatchError("Quantity::operator +=(): Unit mismatch in plus operation");
    }

    myValue += other.myValue;
    if (this->myUnit.isEmpty())
        myUnit = other.myUnit;

    return *this;
}

Quantity Quantity::operator-(const Quantity& other) const
{
    if (!this->myUnit.isEmpty() && !other.myUnit.isEmpty() && this->myUnit != other.myUnit) {
        throw Base::UnitsMismatchError("Quantity::operator -(): Unit mismatch in minus operation");
    }

    return Quantity(this->myValue - other.myValue, this->myUnit.isEmpty() ? other.myUnit : this->myUnit);
}

Quantity& Quantity::operator-=(const Quantity& other)
{
    if (!this->myUnit.isEmpty() && !other.myUnit.isEmpty() && this->myUnit != other.myUnit) {
        throw Base::UnitsMismatchError("Quantity::operator -=(): Unit mismatch in minus operation");
    }

    myValue -= other.myValue;
    if (this->myUnit.isEmpty()) {
        setUnit(other.myUnit);
    }

    return *this;
}

Quantity Quantity::operator-() const
{
    return Quantity(-(this->myValue), this->myUnit);
}

QString Quantity::getUserString(double& factor, QString& unitString) const
{
    return Base::UnitsApi::schemaTranslate(*this, factor, unitString);
}

QString Quantity::getUserString(UnitsSchema* schema, double& factor, QString& unitString) const
{
    return schema->schemaTranslate(*this, factor, unitString);
}

QString Quantity::getSafeUserString() const
{
    auto retString = getUserString();
    if (Q_LIKELY(this->myValue != 0)) {
        auto feedbackQty = parse(retString);
        auto feedbackVal = feedbackQty.getValue();
        if (feedbackVal == 0) {
            retString = QStringLiteral("%1 %2").arg(this->myValue).arg(this->getUnit().getString());
        }
    }
    return retString;
}

/// true if it has a number without a unit
bool Quantity::isDimensionless() const
{
    return isValid() && myUnit.isEmpty();
}

/// true if it has a specific unit or no dimension.
bool Quantity::isDimensionlessOrUnit(const Unit& unit) const
{
    return isDimensionless() || myUnit == unit;
}

// true if it has a number and a valid unit
bool Quantity::isQuantity() const
{
    return isValid() && !myUnit.isEmpty();
}

// true if it has a number with or without a unit
bool Quantity::isValid() const
{
    return !boost::math::isnan(myValue);
}

void Quantity::setInvalid()
{
    myValue = std::numeric_limits<double>::quiet_NaN();
}

// === Predefined types =====================================================

const Quantity Quantity::NanoMetre(1.0e-6, Unit(1));
const Quantity Quantity::MicroMetre(1.0e-3, Unit(1));
const Quantity Quantity::MilliMetre(1.0, Unit(1));
const Quantity Quantity::CentiMetre(10.0, Unit(1));
const Quantity Quantity::DeciMetre(100.0, Unit(1));
const Quantity Quantity::Metre(1.0e3, Unit(1));
const Quantity Quantity::KiloMetre(1.0e6, Unit(1));

const Quantity Quantity::MilliLiter(1000.0, Unit(3));
const Quantity Quantity::Liter(1.0e6, Unit(3));

const Quantity Quantity::Hertz(1.0, Unit(0, 0, -1));
const Quantity Quantity::KiloHertz(1.0e3, Unit(0, 0, -1));
const Quantity Quantity::MegaHertz(1.0e6, Unit(0, 0, -1));
const Quantity Quantity::GigaHertz(1.0e9, Unit(0, 0, -1));
const Quantity Quantity::TeraHertz(1.0e12, Unit(0, 0, -1));

const Quantity Quantity::MicroGram(1.0e-9, Unit(0, 1));
const Quantity Quantity::MilliGram(1.0e-6, Unit(0, 1));
const Quantity Quantity::Gram(1.0e-3, Unit(0, 1));
const Quantity Quantity::KiloGram(1.0, Unit(0, 1));
const Quantity Quantity::Ton(1.0e3, Unit(0, 1));

const Quantity Quantity::Second(1.0, Unit(0, 0, 1));
const Quantity Quantity::Minute(60.0, Unit(0, 0, 1));
const Quantity Quantity::Hour(3600.0, Unit(0, 0, 1));

const Quantity Quantity::Ampere(1.0, Unit(0, 0, 0, 1));
const Quantity Quantity::MilliAmpere(0.001, Unit(0, 0, 0, 1));
const Quantity Quantity::KiloAmpere(1000.0, Unit(0, 0, 0, 1));
const Quantity Quantity::MegaAmpere(1.0e6, Unit(0, 0, 0, 1));

const Quantity Quantity::Kelvin(1.0, Unit(0, 0, 0, 0, 1));
const Quantity Quantity::MilliKelvin(0.001, Unit(0, 0, 0, 0, 1));
const Quantity Quantity::MicroKelvin(0.000001, Unit(0, 0, 0, 0, 1));

const Quantity Quantity::MilliMole(0.001, Unit(0, 0, 0, 0, 0, 1));
const Quantity Quantity::Mole(1.0, Unit(0, 0, 0, 0, 0, 1));

const Quantity Quantity::Candela(1.0, Unit(0, 0, 0, 0, 0, 0, 1));

const Quantity Quantity::Inch(25.4, Unit(1));
const Quantity Quantity::Foot(304.8, Unit(1));
const Quantity Quantity::Thou(0.0254, Unit(1));
const Quantity Quantity::Yard(914.4, Unit(1));
const Quantity Quantity::Mile(1609344.0, Unit(1));

const Quantity Quantity::MilePerHour(447.04, Unit(1, 0, -1));
const Quantity Quantity::SquareFoot(92903.04, Unit(2));
const Quantity Quantity::CubicFoot(28316846.592, Unit(3));

const Quantity Quantity::Pound(0.45359237, Unit(0, 1));
const Quantity Quantity::Ounce(0.0283495231, Unit(0, 1));
const Quantity Quantity::Stone(6.35029318, Unit(0, 1));
const Quantity Quantity::Hundredweights(50.80234544, Unit(0, 1));

const Quantity Quantity::PoundForce(4448.22, Unit(1, 1, -2));  // lbf are ~= 4.44822 Newton

const Quantity Quantity::Newton(1000.0, Unit(1, 1, -2));  // Newton (kg*m/s^2)
const Quantity Quantity::MilliNewton(1.0, Unit(1, 1, -2));
const Quantity Quantity::KiloNewton(1e+6, Unit(1, 1, -2));
const Quantity Quantity::MegaNewton(1e+9, Unit(1, 1, -2));

const Quantity Quantity::NewtonPerMeter(1.00, Unit(0, 1, -2));  // Newton per meter (N/m or kg/s^2)
const Quantity Quantity::MilliNewtonPerMeter(1e-3, Unit(0, 1, -2));
const Quantity Quantity::KiloNewtonPerMeter(1e3, Unit(0, 1, -2));
const Quantity Quantity::MegaNewtonPerMeter(1e6, Unit(0, 1, -2));

const Quantity Quantity::Pascal(0.001, Unit(-1, 1, -2));  // Pascal (kg/m/s^2 or N/m^2)
const Quantity Quantity::KiloPascal(1.00, Unit(-1, 1, -2));
const Quantity Quantity::MegaPascal(1000.0, Unit(-1, 1, -2));
const Quantity Quantity::GigaPascal(1e+6, Unit(-1, 1, -2));

const Quantity Quantity::MilliBar(0.1, Unit(-1, 1, -2));
const Quantity Quantity::Bar(100.0, Unit(-1, 1, -2));  // 1 bar = 100 kPa

const Quantity
    Quantity::Torr(101.325 / 760.0,
                   Unit(-1, 1, -2));  // Torr is a defined fraction of Pascal (kg/m/s^2 or N/m^2)
const Quantity
    Quantity::mTorr(0.101325 / 760.0,
                    Unit(-1, 1, -2));  // Torr is a defined fraction of Pascal (kg/m/s^2 or N/m^2)
const Quantity
    Quantity::yTorr(0.000101325 / 760.0,
                    Unit(-1, 1, -2));  // Torr is a defined fraction of Pascal (kg/m/s^2 or N/m^2)

const Quantity Quantity::PSI(6.894744825494, Unit(-1, 1, -2));   // pounds/in^2
const Quantity Quantity::KSI(6894.744825494, Unit(-1, 1, -2));   // 1000 x pounds/in^2
const Quantity Quantity::MPSI(6894744.825494, Unit(-1, 1, -2));  // 1000 ksi

const Quantity Quantity::Watt(1e+6, Unit(2, 1, -3));  // Watt (kg*m^2/s^3)
const Quantity Quantity::MilliWatt(1e+3, Unit(2, 1, -3));
const Quantity Quantity::KiloWatt(1e+9, Unit(2, 1, -3));
const Quantity Quantity::VoltAmpere(1e+6, Unit(2, 1, -3));  // VoltAmpere (kg*m^2/s^3)

const Quantity Quantity::Volt(1e+6, Unit(2, 1, -3, -1));  // Volt (kg*m^2/A/s^3)
const Quantity Quantity::MilliVolt(1e+3, Unit(2, 1, -3, -1));
const Quantity Quantity::KiloVolt(1e+9, Unit(2, 1, -3, -1));

const Quantity Quantity::MegaSiemens(1.0, Unit(-2, -1, 3, 2));
const Quantity Quantity::KiloSiemens(1e-3, Unit(-2, -1, 3, 2));
const Quantity Quantity::Siemens(1e-6, Unit(-2, -1, 3, 2));  // Siemens (A^2*s^3/kg/m^2)
const Quantity Quantity::MilliSiemens(1e-9, Unit(-2, -1, 3, 2));
const Quantity Quantity::MicroSiemens(1e-12, Unit(-2, -1, 3, 2));

const Quantity Quantity::Ohm(1e+6, Unit(2, 1, -3, -2));  // Ohm (kg*m^2/A^2/s^3)
const Quantity Quantity::KiloOhm(1e+9, Unit(2, 1, -3, -2));
const Quantity Quantity::MegaOhm(1e+12, Unit(2, 1, -3, -2));

const Quantity Quantity::Coulomb(1.0, Unit(0, 0, 1, 1));  // Coulomb (A*s)

const Quantity Quantity::Tesla(1.0, Unit(0, 1, -2, -1));   // Tesla (kg/s^2/A)
const Quantity Quantity::Gauss(1e-4, Unit(0, 1, -2, -1));  // 1 G = 1e-4 T

const Quantity Quantity::Weber(1e6, Unit(2, 1, -2, -1));  // Weber (kg*m^2/s^2/A)

// disable Oersted because people need to input e.g. a field strength of
// 1 ampere per meter -> 1 A/m and not get the recalculation to Oersted
// const Quantity Quantity::Oersted(0.07957747, Unit(-1, 0, 0, 1));// Oersted (A/m)

const Quantity Quantity::PicoFarad(1e-18, Unit(-2, -1, 4, 2));
const Quantity Quantity::NanoFarad(1e-15, Unit(-2, -1, 4, 2));
const Quantity Quantity::MicroFarad(1e-12, Unit(-2, -1, 4, 2));
const Quantity Quantity::MilliFarad(1e-9, Unit(-2, -1, 4, 2));
const Quantity Quantity::Farad(1e-6, Unit(-2, -1, 4, 2));  // Farad (s^4*A^2/m^2/kg)

const Quantity Quantity::NanoHenry(1e-3, Unit(2, 1, -2, -2));
const Quantity Quantity::MicroHenry(1.0, Unit(2, 1, -2, -2));
const Quantity Quantity::MilliHenry(1e+3, Unit(2, 1, -2, -2));
const Quantity Quantity::Henry(1e+6, Unit(2, 1, -2, -2));  // Henry (kg*m^2/s^2/A^2)

const Quantity Quantity::Joule(1e+6, Unit(2, 1, -2));  // Joule (kg*m^2/s^2)
const Quantity Quantity::MilliJoule(1e+3, Unit(2, 1, -2));
const Quantity Quantity::KiloJoule(1e+9, Unit(2, 1, -2));
const Quantity Quantity::NewtonMeter(1e+6, Unit(2, 1, -2));              // Joule (kg*m^2/s^2)
const Quantity Quantity::VoltAmpereSecond(1e+6, Unit(2, 1, -2));         // Joule (kg*m^2/s^2)
const Quantity Quantity::WattSecond(1e+6, Unit(2, 1, -2));               // Joule (kg*m^2/s^2)
const Quantity Quantity::KiloWattHour(3.6e+12, Unit(2, 1, -2));          // 1 kWh = 3.6e6 J
const Quantity Quantity::ElectronVolt(1.602176634e-13, Unit(2, 1, -2));  // 1 eV = 1.602176634e-19 J
const Quantity Quantity::KiloElectronVolt(1.602176634e-10, Unit(2, 1, -2));
const Quantity Quantity::MegaElectronVolt(1.602176634e-7, Unit(2, 1, -2));
const Quantity Quantity::Calorie(4.1868e+6, Unit(2, 1, -2));  // 1 cal = 4.1868 J
const Quantity Quantity::KiloCalorie(4.1868e+9, Unit(2, 1, -2));

const Quantity Quantity::KMH(277.778, Unit(1, 0, -1));  // km/h
const Quantity Quantity::MPH(447.04, Unit(1, 0, -1));   // Mile/h

const Quantity Quantity::AngMinute(1.0 / 60.0, Unit(0, 0, 0, 0, 0, 0, 0, 1));    // angular minute
const Quantity Quantity::AngSecond(1.0 / 3600.0, Unit(0, 0, 0, 0, 0, 0, 0, 1));  // angular second
const Quantity
    Quantity::Degree(1.0,
                     Unit(0, 0, 0, 0, 0, 0, 0, 1));  // degree         (internal standard angle)
const Quantity Quantity::Radian(180 / M_PI, Unit(0, 0, 0, 0, 0, 0, 0, 1));  // radian
const Quantity Quantity::Gon(360.0 / 400.0, Unit(0, 0, 0, 0, 0, 0, 0, 1));  // gon


// === Parser & Scanner stuff ===============================================

// include the Scanner and the Parser for the 'Quantity's

// NOLINTNEXTLINE
Quantity QuantResult;

/* helper function for tuning number strings with groups in a locale agnostic way... */
// NOLINTBEGIN
double num_change(char* yytext, char dez_delim, char grp_delim)
{
    double ret_val {};
    const int num = 40;
    std::array<char, num> temp {};
    int iter = 0;
    for (char* ch = yytext; *ch != '\0'; ch++) {
        // skip group delimiter
        if (*ch == grp_delim) {
            continue;
        }
        // check for a dez delimiter other then dot
        if (*ch == dez_delim && dez_delim != '.') {
            temp[iter++] = '.';
        }
        else {
            temp[iter++] = *ch;
        }
        // check buffer overflow
        if (iter >= num) {
            return 0.0;
        }
    }

    temp[iter] = '\0';

    ret_val = atof(temp.data());
    return ret_val;
}
// NOLINTEND

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#endif

// error func
void Quantity_yyerror(const char* errorinfo)
{
    throw Base::ParserError(errorinfo);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif


#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif

namespace QuantityParser
{

// NOLINTNEXTLINE
#define YYINITDEPTH 20
// show parser the lexer method
#define yylex QuantityLexer
int QuantityLexer();

// Parser, defined in QuantityParser.y
// NOLINTNEXTLINE
#include "QuantityParser.c"

#ifndef DOXYGEN_SHOULD_SKIP_THIS
// Scanner, defined in QuantityParser.l
// NOLINTNEXTLINE
#include "QuantityLexer.c"
#endif  // DOXYGEN_SHOULD_SKIP_THIS
}  // namespace QuantityParser

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

Quantity Quantity::parse(const QString& string)
{
    return parse(string.toUtf8().constData());
}

Quantity Quantity::parse(const char *string)
{
    // parse from buffer
    QuantityParser::YY_BUFFER_STATE my_string_buffer = QuantityParser::yy_scan_string (string);
    // set the global return variables
    QuantResult = Quantity(DOUBLE_MIN);
    // run the parser
    QuantityParser::yyparse();
    // free the scan buffer
    QuantityParser::yy_delete_buffer(my_string_buffer);

    return QuantResult;
}

struct CStringHasher {
    inline std::size_t operator()(const char *s) const {
        if(!s) return 0;
        return boost::hash_range(s,s+std::strlen(s));
    }
    inline bool operator()(const char *a, const char *b) const {
        if(!a) return !b;
        if(!b) return false;
        return std::strcmp(a,b)==0;
    }
};

const std::vector<UnitInfo> &Quantity::unitInfo() {
    static std::vector<UnitInfo> _info = {
        {"nm", 0, "NanoMetre", Quantity::NanoMetre , "Nano meter"},
        {"um", 0, "MicroMetre", Quantity::MicroMetre, "Micro meter"},
        {"\xC2\xB5m", "um", "MicroMetre", Quantity::MicroMetre, "Micro meter"},
        {"mm", 0, "MilliMetre", Quantity::MilliMetre, "Milli meter (internal standard length)"},
        {"cm", 0, "CentiMetre", Quantity::CentiMetre, "Centi meter"},
        {"dm", 0, "DeciMetre", Quantity::DeciMetre, "Deci meter"},
        {"m", 0, "Metre", Quantity::Metre, "Metre"},
        {"km", 0, "KiloMetre", Quantity::KiloMetre, "Kilo meter"},

        {"ml", 0, "MilliLiter", Quantity::MilliLiter, "Milli Liter"},
        {"l", 0, "Liter", Quantity::Liter, "Liter dm^3"},

        {"Hz", 0, "Hertz", Quantity::Hertz, "Hertz"},
        {"kHz", 0, "KiloHertz", Quantity::KiloHertz, "Kilo Hertz"},
        {"MHz", 0, "MegaHertz", Quantity::MegaHertz, "Mega Hertz"},
        {"GHz", 0, "GigaHertz", Quantity::GigaHertz, "Giga Hertz"},
        {"THz", 0, "TeraHertz", Quantity::TeraHertz, "Tera Hertz"},

        {"ug", 0, "MicroGram", Quantity::MicroGram, "Micro gram"},
        {"\xC2\xB5g", "ug", "MicroGram", Quantity::MicroGram, "Micro gram"},
        {"mg", 0, "MilliGram", Quantity::MilliGram, "Milli gram"},
        {"g", 0, "Gram", Quantity::Gram, "Gram"},
        {"kg", 0, "KiloGram", Quantity::KiloGram, "Kilo gram (internal standard for mass)"},
        {"t", 0, "Ton", Quantity::Ton, "Metric Tonne"},

        {"s", 0, "Second", Quantity::Second, "Second (internal standard time)"},
        {"min", 0, "Minute", Quantity::Minute, "Minute"},
        {"h", 0, "Hour", Quantity::Hour, "Hour"},

        {"A", 0, "Ampere", Quantity::Ampere, "Ampere (internal standard electric current)"},
        {"mA", 0, "MilliAmpere", Quantity::MilliAmpere, "Milli Ampere"},
        {"kA", 0, "KiloAmpere", Quantity::KiloAmpere, "Kilo Ampere"},
        {"MA", 0, "MegaAmpere", Quantity::MegaAmpere, "Mega Ampere"},

        {"K", 0, "Kelvin", Quantity::Kelvin, "Kelvin (internal standard thermodynamic temperature)"},
        {"mK", 0, "MilliKelvin", Quantity::MilliKelvin, "Milli Kelvin"},
        {"uK", 0, "MicroKelvin", Quantity::MicroKelvin, "Micro Kelvin"},
        {"\xC2\xB5K", "uK", "MicroKelvin", Quantity::MicroKelvin, "Micro Kelvin"},

        {"mmol", 0, "MilliMole", Quantity::MilliMole, "Milli Mole"},
        {"mol", 0, "Mole", Quantity::Mole, "Mole (internal standard amount of substance)"},

        {"cd", 0, "Candela", Quantity::Candela, "Candela (internal standard luminous intensity)"},

        {"in", 0, "Inch", Quantity::Inch, "Inch (Deprecated! use 'inch' instead)"},
        {"inch", 0, "Inch", Quantity::Inch, "Inch"},
        {"\"", "inch", "Inch", Quantity::Inch, "Inch"},
        {"ft", 0, "Foot", Quantity::Foot, "Foot"},
        {"'", "ft", "Foot", Quantity::Foot, "Foot"},
        {"thou", 0, "Thou", Quantity::Thou, "Thou (inch/1000)"},
        {"mil", 0, "Mil", Quantity::Thou, "Mil  (inch/1000, the thou in US)"},
        {"yd", 0, "Yard", Quantity::Yard, "Uard"},
        {"mi", 0, "Mile", Quantity::Mile, "Mile"},

        {"sqft", 0, "SquareFoot", Quantity::SquareFoot, "Square foot"},
        {"cft", 0, "CubicFoot", Quantity::CubicFoot, "Cubic foot"},

        {"lb", 0, "Pound", Quantity::Pound, "Pound"},
        {"lbm", 0, "Pound", Quantity::Pound, "Pound"},
        {"oz", 0, "Ounce", Quantity::Ounce, "Ounce"},
        {"st", 0, "Stone", Quantity::Stone, "Stone"},
        {"cwt", 0, "Hundredweights", Quantity::Hundredweights, "Hundredweights"},

        {"N", 0, "Newton", Quantity::Newton, "Newton"},
        {"mN", 0, "MilliNewton", Quantity::MilliNewton, "Milli Newton"},
        {"kN", 0, "KiloNewton", Quantity::KiloNewton, "Kilo Newton"},
        {"MN", 0, "MegaNewton", Quantity::MegaNewton, "Mega Newton"},

        {"N/m", 0, "NewtonPerMeter", Quantity::NewtonPerMeter, "Newton per meter"},
        {"kg/s^2", "N/m", "NewtonPerMeter", Quantity::NewtonPerMeter, "Newton per meter"},
        {"mN/m", 0, "MilliNewtonPerMeter", Quantity::MilliNewtonPerMeter, "Milli Newton per meter"},
        {"kN/m", 0, "KiloNewtonPerMeter", Quantity::KiloNewtonPerMeter, "Kilo Newton per meter"},
        {"MN/m", 0, "MegaNewtonPerMeter", Quantity::MegaNewtonPerMeter, "Mega Newton per meter"},

        {"Pa", 0, "Pascal", Quantity::Pascal, "Pascal"},
        {"kPa", 0, "KiloPascal", Quantity::KiloPascal, "Kilo Pascal"},
        {"MPa", 0, "MegaPascal", Quantity::MegaPascal, "Mega Pascal"},
        {"GPa", 0, "GigaPascal", Quantity::GigaPascal, "Giga Pascal"},

        {"bar", 0, "Bar", Quantity::Bar, "Bar"},
        {"mbar", 0, "MilliBar", Quantity::MilliBar, "Milli Bar"},

        {"lbf", 0, "PoundForce", Quantity::PoundForce, "Pound"},

        {"Torr", 0, "Torr", Quantity::Torr, "Portion of Pascal ( 101325/760 )"},
        {"mTorr", 0, "mTorr", Quantity::mTorr, "Milli Torr"}, //
        {"uTorr", 0, "yTorr", Quantity::yTorr, "Micro Torr"}, //
        {"\xC2\xB5Torr", "uTorr", "uTorr", Quantity::yTorr, "Micro Torr"}, //

        {"psi", 0, "PSI", Quantity::PSI, "Pounds/in^2"},
        {"ksi", 0, "KSI", Quantity::KSI, "1000 x pounds/in^2"},
        {"Mpsi", 0, "MPSI", Quantity::MPSI, "1000 ksi"},

        {"W", 0, "Watt", Quantity::Watt, "Watt"},
        {"mW", 0, "MilliWatt", Quantity::MilliWatt, "Milli Watt"},
        {"kW", 0, "KiloWatt", Quantity::KiloWatt, "Kilo Watt"},
        {"VA", 0, "VoltAmpere", Quantity::VoltAmpere, "VoltAmpere"},

        {"V", 0, "Volt", Quantity::Volt, "Volt"},
        {"kV", 0, "KiloVolt", Quantity::KiloVolt, "kilo Volt"},
        {"mV", 0, "MilliVolt", Quantity::MilliVolt, "Milli Volt"},

        {"S", 0, "Siemens", Quantity::Siemens, "Siemens"},
        {"MS", 0, "MegaSiemens", Quantity::MegaSiemens, "Mega Siemens"},
        {"kS", 0, "KiloSiemens", Quantity::KiloSiemens, "Kilo Siemens"},
        {"mS", 0, "MilliSiemens", Quantity::MilliSiemens, "Milli Siemens"},
        {"uS", 0, "MicroSiemens", Quantity::MicroSiemens, "Micro Siemens"},
        {"\xC2\xB5S", "uS", "MicroSiemens", Quantity::MicroSiemens, "Micro Siemens"},

        {"Ohm", 0, "Ohm", Quantity::Ohm, "Ohm"},
        {"kOhm", 0, "KiloOhm", Quantity::KiloOhm, "Kilo Ohm"},
        {"MOhm", 0, "MegaOhm", Quantity::MegaOhm, "Mega Ohm"},

        {"C", 0, "Coulomb", Quantity::Coulomb, "Coulomb (A*s)"},

        {"T", 0, "Tesla", Quantity::Tesla, "Tesla (kg/s^2/A)"},
        {"G", 0, "Gauss", Quantity::Gauss, "Gauss (1 G = 1e-4 T)"},

        {"Wb", 0, "Weber", Quantity::Weber, "Weber"},

        // {"Oe", 0, "Oersted", Quantity::Oersted, "Oersted (A/m)"},

        {"F", 0, "Farad", Quantity::Farad, "Farad"},
        {"mF", 0, "MilliFarad", Quantity::MilliFarad, "Milli Farad"},
        {"uF", 0, "MicroFarad", Quantity::MicroFarad, "Micro Farad"},
        {"\xC2\xB5" "F", "uF", "MicroFarad", Quantity::MicroFarad, "Micro Farad"},
        {"nF", 0, "NanoFarad", Quantity::NanoFarad, "Nano Farad"},
        {"pF", 0, "PicoFarad", Quantity::PicoFarad, "Pico Farad"},

        {"H", 0, "Henry", Quantity::Henry, "Henry"},
        {"mH", 0, "MilliHenry", Quantity::MilliHenry, "Milli Henry"},
        {"uH", 0, "MicroHenry", Quantity::MicroHenry, "Micro Henry"},
        {"\xC2\xB5H", "uH", "MicroHenry", Quantity::MicroHenry, "Micro Henry"},
        {"nH", 0, "NanoHenry", Quantity::NanoHenry, "Nano Henry"},

        {"J", 0, "Joule", Quantity::Joule, "Joule"},
        {"mJ", 0, "MilliJoule", Quantity::MilliJoule, "Milli Joule"},
        {"kJ", 0, "KiloJoule", Quantity::KiloJoule, "kilo Joule"},
        {"Nm", 0, "NewtonMeter", Quantity::NewtonMeter, "N*m = Joule"},
        {"VAs", 0, "VoltAmpereSecond", Quantity::VoltAmpereSecond, "V*A*s = Joule"},
        {"CV", "Ws", "WattSecond", Quantity::WattSecond, "CV"}, // what is this?
        {"Ws", 0, "WattSecond", Quantity::WattSecond, "W*s = Joule"},
        {"kWh", 0, "KiloWattHour", Quantity::KiloWattHour, "1 kWh = 3.6e6 J"},
        {"eV", 0, "ElectronVolt", Quantity::ElectronVolt, "Electron volt, 1 ev = 1.602176634e-19 J"},
        {"keV", 0, "KiloElectronVolt", Quantity::KiloElectronVolt, "Kilo electron volt"}, 
        {"MeV", 0, "MegaElectronVolt", Quantity::MegaElectronVolt, "Mega electron volt"}, 
        {"cal", 0, "Calorie", Quantity::Calorie, "Calorie, 1 cal = 4.1868 J"},
        {"kcal", 0, "KiloCalorie", Quantity::KiloCalorie, "Kilo Calorie"}, 

        {"kmh", 0, "KMH", Quantity::KMH, "Kilo meter per hour "},
        {"km/h", "kmh", "KMH", Quantity::KMH, "Kilo meter per hour "},
        {"mph", 0, "MPH", Quantity::MPH, "Mile per hour "},
        {"mi/h", "mph", "MPH", Quantity::MPH, "Mile per hour "},

        {"M", 0, "AngularMinute", Quantity::AngMinute, "Angle minute"},
        {"\xE2\x80\xB2", "M", "AngularMinute", Quantity::AngMinute, "Angle minute"},
        {"AS", 0, "AngularSecond", Quantity::AngSecond, "Angle second "},
        {"\xE2\x80\xB3", "AS", "AngularSecond", Quantity::AngSecond, "Angle second"},
        {"deg", 0, "Degree", Quantity::Degree, "Degree (internal standard angle)"},
        {"\xC2\xB0", "deg", "Degree", Quantity::Degree, "Degree (internal standard angle)"},
        {"rad", 0, "Radian", Quantity::Radian, "Radian"},
        {"gon", 0, "Gon", Quantity::Gon, "Gradian, 90 deg/100"},
    };

    return _info;
}

const UnitInfo *Quantity::getUnitInfo(const char *unit) {
    static std::unordered_map<const char *,const UnitInfo*,CStringHasher,CStringHasher> _map;
    if(_map.empty()) {
        for(const auto &info : unitInfo()) {
            _map[info.display] = &info;
        }
    }
    auto iter = _map.find(unit);
    if(iter == _map.end())
        return 0;
    return iter->second;
}

bool Quantity::fromUnitString(Quantity &q, const char *unit) {
    const UnitInfo *info = getUnitInfo(unit);
    if(!info)
        return false;
    q = info->quantity;
    return true;
}
