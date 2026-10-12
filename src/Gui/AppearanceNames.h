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

#ifndef GUI_APPEARANCENAMES_H
#define GUI_APPEARANCENAMES_H

#include <FCGlobal.h>

namespace App
{
class Property;
}

namespace Gui
{

class ViewProvider;

/** The property a look of what \a view shows is read and written by
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.1. Where the object holds its
 * looks -- a Part::Feature -- ShapeColor, Transparency, LineColor,
 * PointColor, ShapeAppearance and the Map* properties are the object's, in
 * its group "Appearances", and a dialog or a command writes those: what it
 * does is then what a script with no view provider does. Anything else, any
 * name the object has not got there, and a name the view provider keeps a
 * value of its own under (not App::Property::Legacy) is the view provider's.
 *
 * @param view: the view provider
 * @param name: the property's name
 * @return The object's property of that name, else the view provider's,
 *         else null
 */
GuiExport App::Property *appearanceProperty(const ViewProvider *view, const char *name);

}  // namespace Gui

#endif  // GUI_APPEARANCENAMES_H
