/***************************************************************************
 *   Copyright (c) 2025 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_VIEWPROVIDERORIGINFEATURE_H
#define GUI_VIEWPROVIDERORIGINFEATURE_H

/* Former home of Gui::ViewProviderOriginFeature, which is now
 * Gui::ViewProviderDatum (the name upstream gives it). Kept so that code
 * including this header, in this tree or outside it, still compiles.
 */

#include "ViewProviderDatum.h"

namespace Gui
{
using ViewProviderOriginFeature = ViewProviderDatum;
}

#endif // GUI_VIEWPROVIDERORIGINFEATURE_H
