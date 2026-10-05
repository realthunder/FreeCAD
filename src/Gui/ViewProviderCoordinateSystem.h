/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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

#ifndef GUI_VIEWPROVIDER_ViewProviderOrigin_H
#define GUI_VIEWPROVIDER_ViewProviderOrigin_H

#include <map>
#include <string>
#include <vector>

#include <App/DocumentObserver.h>
#include <App/PropertyGeo.h>

#include "ViewProviderDocumentObject.h"


namespace Gui {

class Document;

class GuiExport ViewProviderCoordinateSystem : public ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderCoordinateSystem);

public:
    /// Size of the origin as set by the part.
    App::PropertyVector Size;
    /// Margin added to the size of the origin.
    App::PropertyVector Margin;

    /// constructor.
    ViewProviderCoordinateSystem();
    /// destructor.
    ~ViewProviderCoordinateSystem() override;

    /// @name Override methods
    ///@{
    std::vector<App::DocumentObject*> claimChildren() const override;
    std::vector<App::DocumentObject*> claimChildren3D() const override;

    SoGroup* getChildRoot() const override {return pcGroupChildren;}

    void attach(App::DocumentObject* pcObject) override;
    std::vector<std::string> getDisplayModes() const override;
    void setDisplayMode(const char* ModeName) override;
    ///@}

    /** @name Temporary visibility mode
     * Control the visibility of origin and associated objects when needed
     */
    ///@{
    /** Set temporary visibility of some of origin's objects e.g. while rotating or mirroring
     *
     * Inside an edit whose views have a visibility table of their own
     * (Gui::Document::canSetEditVisibility) nothing is written: the origin
     * is shown in the views of the edit only, and where the edit goes
     * through the origin's container only in that occurrence -- not in a
     * Link to the container, not in a view outside the edit, not for a
     * served client that is not in it. The entries end with the edit, so
     * nothing is left shown by a panel that is never destroyed. Otherwise
     * Visibility is written and resetTemporaryVisibility() puts it back.
     */
    void setTemporaryVisibility (bool axis, bool planes);
    /// Returns true if the origin in temporary visibility mode
    bool isTemporaryVisibility ();
    /// Reset the visibility
    void resetTemporaryVisibility ();
    /// Enlarge the datum elements while they may be picked, in constant
    /// screen size (upstream b942275957)
    void setTemporaryScale (double factor);
    void resetTemporarySize ();
    /// Show or hide the plane labels, which constant screen size hides
    void setPlaneLabelVisibility (bool visible);
    ///@}

    bool canDragObjects() const override {
        return false;
    }

    bool doubleClicked() override;

    void setupContextMenu(QMenu* menu, QObject* receiver, const char* member) override;

    /// Returns default size. Use this if it is not possible to determine appropriate size by other means
    static double defaultSize();

    /// Base size of scaling
    static double baseSize();

    // the factor by which the axes are longer than the planes
    static constexpr float axesScaling = 1.5f;

    // default color for origini: light-blue (50, 150, 250, 255 stored as 0xRRGGBBAA)
    static const uint32_t defaultColor = 0x3296faff;

protected:
    void onChanged(const App::Property* prop) override;
    bool onDelete(const std::vector<std::string> &) override;
    void updateData(const App::Property *prop) override;
    bool setEdit(int) override;

private:
    SoGroup *pcGroupChildren;

    bool setTemporaryVisibilityInEdit(bool axis, bool planes);

    std::map<App::DocumentObject*, bool> tempVisMap;
    /// What setTemporaryVisibility put into an edit's views instead, and
    /// the document whose edit it is
    std::vector<App::SubObjectT> tempEditEntries;
    std::string tempEditDocument;
};

} // namespace Gui

#endif // GUI_VIEWPROVIDER_ViewProviderOrigin_H

