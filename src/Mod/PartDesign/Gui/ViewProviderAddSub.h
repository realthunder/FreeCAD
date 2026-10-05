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
struct VisibilityEntry;
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
    /** Show or drop the edit preview: the base feature with the tinted
     * tool shape over it, in place of this feature.
     *
     * With \a occurrence -- this feature as it is being edited, a top
     * level object and the path down to it -- and an edit session whose
     * views can take it, the preview belongs to the SESSION: the tool
     * hangs in the session's editing root, and each view of the session
     * hides this feature and shows its base through its own visibility
     * table (Gui::EditingRoot::addSessionNode, setVisibilitySwaps). No
     * Visibility is written and the document's scene is not touched, so
     * a view outside the session -- another document's, a served client
     * that is not editing -- keeps showing the feature as it was.
     *
     * Otherwise, as before: the tool becomes a child of the base
     * feature's switch and the two Visibility properties are swapped,
     * which is document state and shows in every view. That is what a
     * view with no table of its own gets (render-cache modes 0-2).
     */
    void setPreviewDisplayMode(bool on,
                               const App::SubObjectT &occurrence = App::SubObjectT());
    /// Whether the preview is the edit session's own (see above)
    bool isPreviewInSession() const { return !previewSession.empty(); }
    /// The base feature the session's views show in this feature's place,
    /// as an occurrence; empty when the preview is not the session's or
    /// has no base
    const App::SubObjectT &previewBaseOccurrence() const { return previewBase; }
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
    /// What the session's views swap for \a occurrence's preview, and the
    /// WORLD frame the tool is drawn in: the base's occurrence, else this
    /// feature's own. False when the occurrence is not this feature, or
    /// its base is not a sibling of it.
    bool resolvePreview(const App::SubObjectT &occurrence,
                        Base::Matrix4D &world,
                        std::vector<Gui::VisibilityEntry> &entries,
                        App::SubObjectT &base) const;
    bool showPreviewInSession(const App::SubObjectT &occurrence);
    /// False when the preview was not the session's
    bool dropPreviewFromSession();

private:
    /// The occurrence the session's preview was asked for, and the name
    /// of the document whose session holds it: empty unless it does.
    App::SubObjectT             previewOccurrence;
    App::SubObjectT             previewBase;
    std::string                 previewSession;
    int                         defaultChild;
    std::string                 displayMode;
    App::DocumentObjectT        baseFeature;
    int                         baseChild = -1;
    std::unique_ptr<PartGui::ViewProviderPartExt> pAddSubView;
    bool                        previewActive = false;
};

} // namespace PartDesignGui


#endif // PARTGUI_ViewProviderBoolean_H
