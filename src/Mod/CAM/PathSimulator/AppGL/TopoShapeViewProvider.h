// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Shai Seger <shaise at gmail>                       *
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

#pragma once

#include <Gui/ViewProvider.h>

class SoMaterial;

namespace Part
{
class TopoShape;
}

namespace CAMSimulator
{

class TopoShapeViewProvider: public Gui::ViewProvider
{
public:
    TopoShapeViewProvider();
    TopoShapeViewProvider& operator=(TopoShapeViewProvider&& vp);

    void clear();
    void setShape(const Part::TopoShape& shape);

    void setShapeVisible(bool b);
    /// The shape's diffuse colour. TopoShape::exportFaceSet writes no
    /// material of its own when it is given no face colours, so
    /// without this the shape inherits whatever the traversal happens
    /// to be carrying -- which through the renderer is nothing, and
    /// draws black.
    void setShapeColor(float r, float g, float b);

private:
    SoMaterial* pcMaterial = nullptr;
    SoSwitch* pcSwitch = nullptr;
    SoNode* pcShape = nullptr;
};

}  // namespace CAMSimulator
