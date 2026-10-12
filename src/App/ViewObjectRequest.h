/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
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

#ifndef APP_VIEWOBJECTREQUEST_H
#define APP_VIEWOBJECTREQUEST_H

#include <functional>
#include <FCGlobal.h>

namespace App
{

class DocumentObject;

/** @name An object's view provider, asked for before it was made
 *
 * A document read with a Gui does not have its view providers when the read
 * returns: a progressive load parks them and makes them as the event loop
 * gets to them (docs/DocumentLoad.md). `obj.ViewObject` is a property of the
 * object, set when its view provider is attached, and until then it says
 * None -- to a script that opens a file and writes a line width on the next
 * line. Asking for it by that name is needing it now: the Gui is told, and
 * makes what it parked. In files of their own, and not in DocumentObject.h,
 * which is every file's to compile again.
 */
//@{
/// What makes `obj`'s view provider now. The Gui's, set once.
AppExport void setViewObjectRequest(std::function<void(const DocumentObject&)> request);
/// Ask for `obj`'s view provider: nothing where no Gui answers.
AppExport void requestViewObject(const DocumentObject& obj);
//@}

}  // namespace App

#endif  // APP_VIEWOBJECTREQUEST_H
