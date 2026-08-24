/****************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef AREA_CLIPPERENUMS_H
#define AREA_CLIPPERENUMS_H

#include <clipper2/clipper.h>

namespace AreaLib
{

/** Paste-friendly names for Clipper2's enumerators.
 *
 * The parameters below are declared with #AREA_PARAMS_CLIPPER_FILL and
 * friends, which build two things out of one list of names: the strings a
 * PropertyEnumeration shows, and a switch converting the index that property
 * stores into the value Clipper wants. The second half pastes a prefix onto
 * each name, and Clipper1's enumerators were flat -- \c pft onto \c NonZero
 * gave \c pftNonZero. Clipper2's are scoped, and no amount of pasting builds
 * \c Clipper2Lib::FillRule::NonZero out of a prefix and \c NonZero, because
 * the \c :: cannot come from a paste.
 *
 * So these are that scoped value under a name that can be pasted onto. The
 * alternative -- writing the qualified value into the list and letting the
 * property show it -- is what upstream did, and it puts the string
 * "Clipper2Lib::FillRule::NonZero" in front of the user and, worse, makes the
 * stored index mean the enumerator's own number: NonZero is 1, so a property
 * left at its default index 0 comes back as EvenOdd, and Round joins turn
 * into Miter.
 *
 * The order of the names in the parameter list is what documents persist, so
 * it must not change. These are only names for values; the order lives there.
 */
namespace ClipperEnum
{

// fill rules, listed as NonZero, EvenOdd, Positive, Negative
inline constexpr auto FillNonZero = Clipper2Lib::FillRule::NonZero;
inline constexpr auto FillEvenOdd = Clipper2Lib::FillRule::EvenOdd;
inline constexpr auto FillPositive = Clipper2Lib::FillRule::Positive;
inline constexpr auto FillNegative = Clipper2Lib::FillRule::Negative;

// offset join types, listed as Round, Square, Miter. Clipper2 adds Bevel,
// which is deliberately not offered: adding it anywhere but the end would
// renumber the ones already written into documents.
inline constexpr auto JoinRound = Clipper2Lib::JoinType::Round;
inline constexpr auto JoinSquare = Clipper2Lib::JoinType::Square;
inline constexpr auto JoinMiter = Clipper2Lib::JoinType::Miter;

// offset end types, listed as OpenRound, ClosedPolygon, ClosedLine,
// OpenSquare, OpenButt. Clipper2 renamed all five; these keep the old names
// so the property reads as it always has.
inline constexpr auto EndOpenRound = Clipper2Lib::EndType::Round;
inline constexpr auto EndClosedPolygon = Clipper2Lib::EndType::Polygon;
inline constexpr auto EndClosedLine = Clipper2Lib::EndType::Joined;
inline constexpr auto EndOpenSquare = Clipper2Lib::EndType::Square;
inline constexpr auto EndOpenButt = Clipper2Lib::EndType::Butt;

}  // namespace ClipperEnum

}  // namespace AreaLib

#endif  // AREA_CLIPPERENUMS_H
