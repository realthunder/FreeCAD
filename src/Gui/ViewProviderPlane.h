/***************************************************************************
 *   Copyright (c) 2012 Jürgen Riegel <juergen.riegel@web.de>              *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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


#ifndef GUI_ViewProviderPlane_H
#define GUI_ViewProviderPlane_H

#include "ViewProviderDatum.h"
#include "Selection.h"

class SoCoordinate3;
class SoTranslation;
class SoVertexProperty;

namespace Gui
{

class GuiExport ViewProviderPlane : public ViewProviderDatum, public SelectionObserver
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderPlane);
public:
    /// Constructor
    ViewProviderPlane();
    ~ViewProviderPlane() override;

    void attach ( App::DocumentObject * ) override;

protected:
    void updateDatumSize() override;
    /// A plane of a coordinate system shows its label on screen only while
    /// it may be picked (upstream b942275957)
    bool showLabelOnScreen() const override { return getRole().empty(); }

private:
    /// Constant screen size: a plane of a coordinate system is drawn as the
    /// corner of its first quadrant unless it is selected or hovered
    void onSelectionChanged(const SelectionChanges& msg) override;

private:
    SoCoordinate3 *pCoords = nullptr;
    SoVertexProperty *pFaceVertices = nullptr;
    SoTranslation *pTextTranslation = nullptr;
    bool isSelected = false;
    bool isHovered = false;
};

} //namespace Gui


#endif // GUI_ViewProviderPlane_H
