/***************************************************************************
 *   Copyright (c) 2005 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef APP_COLOR_H
#define APP_COLOR_H

/** The colour class lives in Base now, where upstream keeps it
 *
 * A colour knows nothing about documents, so App was never the right layer
 * for it, and every upstream file that mentions a colour says Base::Color
 * (docs/UpstreamCoreSync.md section 2d). The alias below holds the call
 * sites that say App::Color; new and ported code says Base::Color, and this
 * header is only ever removed from, never added to.
 *
 * WARNING: an alias is not a class, so `namespace App { class Color; }` cannot
 * coexist with it. Forward-declare `namespace Base { class Color; }`.
 */
#include <Base/Color.h>

namespace App
{
using Color = Base::Color;
}

#endif // APP_COLOR_H
