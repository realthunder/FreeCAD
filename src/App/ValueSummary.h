/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef APP_VALUESUMMARY_H
#define APP_VALUESUMMARY_H

#include <functional>
#include <map>
#include <string>
#include <FCGlobal.h>

namespace App
{

class Document;
struct CapturedValue;

/** What a value says of itself in a line (docs/TransactionLog.md sec
 * 32.35), where the log's text of it says nothing to a reader: a shape's
 * kind, how many of each of its elements, its volume and its size. `facts`
 * are the same as numbers, by name.
 */
struct ValueSummary
{
    std::string text;
    std::map<std::string, double> facts;
};

/// A way to say what a value is: false where it is not one of its own.
using ValueSummariser =
    std::function<bool(const Document&, const CapturedValue&, ValueSummary&)>;

/// Said by the module whose value it is: each registered is asked in turn,
/// and the first that knows the value says it.
AppExport void registerValueSummary(ValueSummariser fn);

/// What `value`, of a property of `doc`, says of itself; false where no
/// module knows it.
AppExport bool summariseValue(const Document& doc, const CapturedValue& value,
                              ValueSummary& out);

} // namespace App

#endif // APP_VALUESUMMARY_H
