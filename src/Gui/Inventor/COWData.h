/****************************************************************************
 *   Copyright (c) 2020 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef FC_COWDATA_H
#define FC_COWDATA_H

/** @file
 * The renderer's spelling of Base/COWData.h.
 *
 * The copy-on-write containers moved to Base so App and Part can use them
 * too. This header keeps the unqualified names the render cache, the VBOs
 * and the vertex caches were written against; they are using-declarations
 * of the Base templates rather than aliases of their own, because these
 * translation units also say "using namespace Base" and two distinct
 * templates of the same name would be ambiguous there.
 *
 * Allocation accounting is now OPT IN rather than implied by FC_DEBUG:
 * build with FC_COW_MEM_TRACE to count what these containers allocate and
 * call Base::reportCOWMemStats() to read it. Before, every debug build
 * paid for the counting and nothing ever printed it.
 */

#include <Base/COWData.h>

using Base::COWData;
using Base::COWHolder;
using Base::COWMap;
using Base::COWValue;
using Base::COWVector;

template<class T>
using SoFCAllocator = Base::COWAllocator<T>;

template<class KeyT, class ValueT>
using SbFCMap = Base::FCMap<KeyT, ValueT>;

template<class ValueT>
using SbFCSet = Base::FCSet<ValueT>;

template<class ValueT>
using SbFCVector = Base::FCVector<ValueT>;

#endif  // FC_COWDATA_H
// vim: noai:ts=2:sw=2
