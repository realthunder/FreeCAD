/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_VIEWPROVIDERPYTHONFEATURE_FORWARD_H
#define GUI_VIEWPROVIDERPYTHONFEATURE_FORWARD_H

/** This header is ViewProviderFeaturePython.h now, upstream's name for it
 *
 * See docs/UpstreamCoreSync.md section 3, step 2c. The old class names are
 * still available as aliases, declared in the real header so that a
 * transitive include sees them too; this file only holds the old path.
 *
 * WARNING: the guard above is deliberately NOT the real header's guard.
 * Reusing it would define it before the include and skip the contents.
 */

#include <Gui/ViewProviderFeaturePython.h>

#endif  // GUI_VIEWPROVIDERPYTHONFEATURE_FORWARD_H
