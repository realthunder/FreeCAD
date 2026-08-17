/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef GUI_SELECTION_FORWARD_H
#define GUI_SELECTION_FORWARD_H

/** These headers live in Gui/Selection/ now, where upstream keeps them
 *
 * Upstream moved the selection sources into their own directory
 * (docs/UpstreamCoreSync.md section 3, step 2b). This header holds the call
 * sites that still say the old path; new and ported code includes
 * <Gui/Selection/Selection.h> directly, and this file is only ever removed
 * from, never added to.
 *
 * WARNING: the guard below is deliberately NOT the moved header's own guard.
 * Reusing it would define it before the include and the real contents would
 * be skipped entirely.
 */

#include <Gui/Selection/Selection.h>

#endif  // GUI_SELECTION_FORWARD_H
