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

#include <Base/BoundBox.h>
#include <Gui/ViewProviderGeometryObject.h>
#include <Mod/Part/Gui/ViewProviderAttachExtension.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

class SoPickStyle;
class SbBox3f;
class SoGetBoundingBoxAction;

namespace PartDesignGui {

/** Base view provider of the PartDesign datum features.
 *
 * This used to be split in two: an abstract Gui::ViewProviderDatum carrying the
 * extents machinery, and this class carrying the editing. Upstream dissolved the
 * Gui half into this one and reused the Gui::ViewProviderDatum name for the datum
 * element view provider (what this fork called Gui::ViewProviderOriginFeature), so
 * the two halves are joined here. PartDesign is the only consumer either half ever
 * had, which is why the extents logic belongs at this level rather than in Gui.
 */
class PartDesignGuiExport ViewProviderDatum : public Gui::ViewProviderGeometryObject, PartGui::ViewProviderAttachExtension
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderDatum)
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesignGui::ViewProviderDatum);

public:
    /// constructor
    ViewProviderDatum();
    /// destructor
    ~ViewProviderDatum() override;

    void attach(App::DocumentObject *) override;
    bool onDelete(const std::vector<std::string> &) override;
    std::vector<std::string> getDisplayModes(void) const override;
    void setDisplayMode(const char* ModeName) override;

    /// grouping handling
    void setupContextMenu(QMenu*, QObject*, const char*) override;

    bool doubleClicked() override;

    /// indicates if the ViewProvider use the new Selection model
    bool useNewSelectionModel(void) const override { return true; }
    /// return a hit element to the selection path or 0
    std::string getElement(const SoDetail *) const override;
    SoDetail* getDetail(const char*) const override;

    /**
     * Enable/Disable the selectability of the datum
     * This differs from the normal ViewProvider selectability in that, that with this enabled one
     * can pick through the datum and select stuff behind it.
     */
    bool isPickable();
    void setPickable(bool val);

    /// Update the visual sizes. This overloaded version of the previous function to allow pass coin type
    void setExtents (const SbBox3f &bbox);

    /// update size to match the guessed bounding box
    virtual void updateExtents ();

    /// The datum type (Plane, Line or Point)
    // TODO remove this attribute (2015-09-08, Fat-Zer)
    QString datumType;
    QString datumText;

    /**
     * Computes appropriate bounding box for the given list of objects to be passed to setExtents ()
     * @param objs        the list of objects to traverse, due to we traverse the scene graph, the geo children
     *                    will likely be traversed too.
     */
    static SbBox3f getRelevantBoundBox (
            const std::vector <App::DocumentObject *> &objs);

    /// Default size used to produce the default bbox
    static double defaultSize();

    // Returned default bounding box if relevant is can't be used for some reason
    static SbBox3f defaultBoundBox ();

    // Returns a default margin factor (part of size )
    static double marginFactor () { return 0.1; };

    /// Returns a point of the feature it counts as its base
    Base::Vector3d getBasePoint () const;

protected:
    bool setEdit(int ModNum) override;
    void unsetEdit(int ModNum) override;

    /**
     * Update the visual size to match the given extents
     * @note should be reimplemented in the offspings
     * @note use FreeCAD-specific bbox here to simplify the math in derived classes
     */
    virtual void setExtents (Base::BoundBox3d /*bbox*/)
        { }

    /**
     * Guesses the context this datum belongs to and returns appropriate bounding box of all
     *  visible content of the feature
     *
     * Currently known contexts are:
     *  - PartDesign::Body
     *  - App::DocumentObjectGroup (App::Part as well as subclass)
     *  - Whole document
     */
    SbBox3f getRelevantBoundBox() const;

    // Get the separator to fill with datum content
    SoSeparator *getShapeRoot () { return pShapeSep; }

private:
    SoSeparator* pShapeSep;
    SoPickStyle* pPickStyle;
    std::string oldWb;

};

} // namespace PartDesignGui


#endif // PARTDESGIN_ViewProviderDatum_H
