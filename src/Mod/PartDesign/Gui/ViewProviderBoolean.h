/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer                                    *
 *                                   <jrheinlaender@users.sourceforge.net> *
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


#ifndef PARTGUI_ViewProviderBoolean_H
#define PARTGUI_ViewProviderBoolean_H

#include "ViewProviderAddSub.h"
#include <Gui/ViewProviderGeoFeatureGroupExtension.h>


namespace PartDesignGui {

/** A Boolean previews as the add/sub features do: while it is edited the
 *  boolean waits, and the base feature is drawn with the tools over it in
 *  the colour of the operation (Fuse additive, Cut subtractive, Common
 *  intersecting).
 */
class PartDesignGuiExport ViewProviderBoolean : public ViewProviderAddSub,
                                                public Gui::ViewProviderGeoFeatureGroupExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesignGui::ViewProviderBoolean);

public:
    /// constructor
    ViewProviderBoolean();
    /// destructor
    ~ViewProviderBoolean() override;

    App::PropertyEnumeration Display;

    /// grouping handling
    void setupContextMenu(QMenu*, QObject*, const char*) override;

    void attach(App::DocumentObject*) override;
    const char* getDefaultDisplayMode() const override;
    void onChanged(const App::Property* prop) override;
    void updateData(const App::Property*) override;
    void checkAddSubColor() override;

    void extensionModeSwitchChange() override;

protected:
    TaskDlgFeatureParameters *getEditDialog() override;
    const char *getPreviewShapeName() const override { return "ToolShape"; }
    
    static const char* DisplayEnum[];

};

} // namespace PartDesignGui


#endif // PARTGUI_ViewProviderBoolean_H
