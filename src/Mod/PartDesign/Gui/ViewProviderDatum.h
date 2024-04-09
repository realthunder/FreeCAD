/***************************************************************************
 *   Copyright (c) 2013 Jan Rheinlaender                                   *
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


#ifndef PARTDESGIN_ViewProviderDatum_H
#define PARTDESGIN_ViewProviderDatum_H

#include <QCoreApplication>

#include <Gui/ViewProviderDatum.h>
#include <Mod/Part/Gui/ViewProviderAttachExtension.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

class SoPickStyle;
class SbBox3f;
class SoGetBoundingBoxAction;

namespace PartDesignGui {

class PartDesignGuiExport ViewProviderDatum : public Gui::ViewProviderDatum, PartGui::ViewProviderAttachExtension
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderDatum)
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesignGui::ViewProviderDatum);

public:
    /// constructor
    ViewProviderDatum();
    /// destructor
    ~ViewProviderDatum() override;

    /// grouping handling
    void setupContextMenu(QMenu*, QObject*, const char*) override;

    bool doubleClicked() override;

    Base::Vector3d getBasePoint () const override;

protected:
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;

private:
    std::string oldWb;

};

} // namespace PartDesignGui


#endif // PARTDESGIN_ViewProviderDatum_H
