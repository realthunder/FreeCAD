/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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


#ifndef PARTGUI_ViewProviderAddSub_H
#define PARTGUI_ViewProviderAddSub_H

#include <App/DocumentObserver.h>
#include "ViewProvider.h"

namespace Gui {
class SoFCPathAnnotation;
}

namespace PartDesignGui {

class PartDesignGuiExport ViewProviderAddSub : public ViewProvider
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderAddSub);

public:
    /// constructor
    ViewProviderAddSub();
    /// destructor
    ~ViewProviderAddSub() override;
    
    void attach(App::DocumentObject*) override;
    void reattach(App::DocumentObject *) override;
    void beforeDelete() override;
    bool onDelete(const std::vector<std::string> &) override;
    void updateData(const App::Property*) override;
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;
    void finishRestoring() override;
    bool getDetailPath(const char *subname,
                       SoFullPath *pPath,
                       bool append,
                       SoDetail *&det) const override;
    QIcon getIcon(void) const override;
    
    bool isPreviewMode() const;
    void setPreviewDisplayMode(bool);
    virtual void checkAddSubColor();

protected:
    virtual void setAddSubColor(const App::Color &color, float t);
    /// Colours the preview \a color, translucent or not as the user chose
    void applyPreviewColor(const App::Color &color);
    virtual void updateAddSubShapeIndicator();
    virtual PartGui::ViewProviderPartExt * getAddSubView();
    /// The shape property the preview draws
    virtual const char *getPreviewShapeName() const { return "AddSubShape"; }
    /// Places \a shape, in this feature's frame, in the base feature's view
    void updatePreviewTransform(const Part::TopoShape &shape);
    /// Moves the preview into the view of a base feature that changed
    void refreshPreviewBase();

protected:
    Gui::CoinPtr<Gui::SoFCPathAnnotation>   previewGroup;
    Gui::CoinPtr<SoTransform>   previewTransform;

private:
    int                         defaultChild;
    std::string                 displayMode;
    App::DocumentObjectT        baseFeature;
    int                         baseChild = -1;
    std::unique_ptr<PartGui::ViewProviderPartExt> pAddSubView;
    bool                        previewActive = false;
};

} // namespace PartDesignGui


#endif // PARTGUI_ViewProviderBoolean_H
