/***************************************************************************
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
 *   Copyright (c) 2016 Stefan Tröger <stefantroeger@gmx.net>              *
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

#ifndef GUI_VIEWPROVIDERORIGINGROUPEXTENSION_H
#define GUI_VIEWPROVIDERORIGINGROUPEXTENSION_H

#include <memory>
#include "ViewProviderGeoFeatureGroup.h"


namespace Gui
{

class GuiExport ViewProviderOriginGroupExtension : public ViewProviderGeoFeatureGroupExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderOriginGroupExtension);

public:
    /// Constructor
    ViewProviderOriginGroupExtension();
    ~ViewProviderOriginGroupExtension() override;

    void extensionClaimChildren(std::vector<App::DocumentObject*> &)const override;
    void extensionClaimChildren3D(std::vector<App::DocumentObject*> &)const override;

    void extensionAttach(App::DocumentObject *pcObject) override;
    void extensionUpdateData(const App::Property* prop) override;
    void extensionFinishRestoring() override;

    virtual void updateOriginSize();

    /** Whether sizing over the content must wait: while a document
     * restores or recomputes, or while a progressive load is still
     * building the visuals. An unbuilt visual answers a bounds question
     * from its shape, and an untriangulated curved shape's box is the
     * loose box of its poles -- a helix read 227 against the 208 it
     * draws. The origins and the automatic datums sized after a load
     * both ask this, so they agree on when the load is over.
     */
    static bool sizingMustWait();

    virtual bool extensionCanDragObject(App::DocumentObject*) const override;

protected:
    void slotChangedObjectApp ( const App::DocumentObject& obj );
    void slotChangedObjectGui ( const Gui::ViewProviderDocumentObject& obj );

private:
    void constructChildren ( std::vector<App::DocumentObject*> &children ) const;

private:
    class Private;
    std::shared_ptr<Private> pimpl;
};

using ViewProviderOriginGroupExtensionPython = ViewProviderExtensionPythonT<Gui::ViewProviderOriginGroupExtension>;

} //namespace Gui

#endif // GUI_VIEWPROVIDERORIGINGROUPEXTENSION_H
