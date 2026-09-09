/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#include <Base/PyObjectBase.h>

#include "ViewerContext.h"

using namespace Gui;

// Out of line so the class has one key function, and with it one vtable and
// one typeinfo, rather than a copy in every translation unit that sees the
// header.
ViewerContext::~ViewerContext() = default;

PyObject* ViewerContext::getPyObject()
{
    Py_INCREF(Py_None);
    return Py_None;
}

namespace
{
/// The innermost open ViewerScope's context, or null.
///
/// Thread-local because a scope is opened around the handling of one
/// event and events are handled on whatever thread the view lives on --
/// the GUI thread for every view there is today, but a global would make
/// that an assumption rather than an observation.
thread_local ViewerContext* s_current = nullptr;
}  // namespace

ViewerContext* ViewerContext::current()
{
    return s_current;
}

ViewerScope::ViewerScope(ViewerContext* context)
    : previous(s_current)
{
    s_current = context;
}

ViewerScope::~ViewerScope()
{
    s_current = previous;
}
