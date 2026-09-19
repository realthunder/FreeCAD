/***************************************************************************
 *   Copyright (c) 2012 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef ORIGINFEATURE_H
#define ORIGINFEATURE_H

#include "Datums.h"

namespace App
{

/** Former name of App::DatumElement.
 *
 * Upstream deleted this header when it split the datum types out into
 * Datums.h. It stays here, and the type keeps answering to the old name
 * through Base::Type::addLegacyName (App/Application.cpp), so that code
 * and user macros written against App::OriginFeature keep working.
 *
 * App::Plane and App::Line kept their own names and are declared in
 * Datums.h; including this header still reaches them.
 */
using OriginFeature = DatumElement;

} //namespace App

#endif /* end of include guard: ORIGINFEATURE_H */
