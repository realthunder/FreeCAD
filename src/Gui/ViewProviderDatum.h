/***************************************************************************
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

#ifndef GUI_VIEWPROVIDERDATUM_H
#define GUI_VIEWPROVIDERDATUM_H

#include "ViewProviderGeometryObject.h"
#include "ParamHandler.h"

class SoAsciiText;
class SoFont;
class SoScale;
class SoSwitch;

namespace Gui
{

class SoFCSelection;
class SoAutoZoomTranslation;

/**
 * View provider associated with an App::DatumElement.
 *
 * Two size models, switched by ViewParams DatumScreenSize. On (the default),
 * upstream's: the datum keeps a constant size on screen, laid out in screen
 * units under an SoAutoZoomTranslation and sized by DatumScale, DatumPlaneSize
 * and DatumLineSize. Off, the fork's: the datum is drawn in world units scaled
 * by Size, which ViewProviderCoordinateSystem sets to fit the objects of the
 * owning body or part.
 */
class GuiExport ViewProviderDatum: public ViewProviderGeometryObject {
    PROPERTY_HEADER_WITH_OVERRIDE(Gui::ViewProviderDatum);

public:
    /// The display size of the feature
    App::PropertyFloat  Size;

    ViewProviderDatum ();
    ~ViewProviderDatum () override;

    /// Get point derived classes will add their specific stuff
    SoSeparator * getDatumRoot () { return pOriginFeatureRoot; }
    /// Former name of getDatumRoot(), kept so existing callers still compile
    SoSeparator * getOriginFeatureRoot () { return getDatumRoot (); }

    /// Get pointer to the text label associated with the feature
    SoAsciiText * getLabel () { return pLabel; }

    void attach(App::DocumentObject *) override;
    void updateData(const App::Property *) override;
    std::vector<std::string> getDisplayModes () const override;
    void setDisplayMode (const char* ModeName) override;

    /// @name Suppress ViewProviderGeometryObject's behaviour
    ///@{
    bool setEdit ( int ) override
        { return false; }
    void unsetEdit ( int ) override
        { }
    ///@}

    QIcon getIcon() const override;

    /// Whether datums keep a constant size on screen (ViewParams DatumScreenSize)
    static bool isScreenSize();
    /// The on-screen size of a datum plane, or an axis, in screen units
    static float screenPlaneSize();
    static float screenLineSize();

    /** Enlarge the datum while it may be picked, e.g. by the attachment
     * editor; constant screen size only (upstream b942275957)
     */
    void setTemporaryScale(double factor);
    void resetTemporarySize();
    /// Show or hide the label, which planes of a coordinate system hide in
    /// constant screen size unless it is shown this way
    void setLabelVisibility(bool visible);

protected:
    void onChanged ( const App::Property* prop ) override;
    bool onDelete ( const std::vector<std::string> & ) override;

    /// Lay the geometry out for the current size model; called on attach and
    /// whenever a datum size parameter changes
    virtual void updateDatumSize();
    /// Whether the label shows in constant screen size when not forced on
    virtual bool showLabelOnScreen() const { return true; }
    /// The label text in constant screen size
    virtual std::string screenLabel() const;
    /// The object's role, "XY_Plane", "X_Axis", ..., empty for a lone datum
    std::string getRole() const;
    /// Whether the geometry is laid out for constant screen size
    bool screenSize = false;
    /// Put the scale node and font size of the current size model in place
    void applySizeModel();
    /// Set the label's text and visibility for the current size model
    void updateLabel();
protected:
    SoSeparator    * pOriginFeatureRoot;
    SoFCSelection  * pHighlight;
    SoScale        * pScale;
    SoAutoZoomTranslation * pZoom;
    SoFont         * pFont;
    SoSwitch       * pLabelSwitch;
    SoAsciiText    * pLabel;
    double temporaryScale = 1.0;
    bool labelForced = false;
    ParamHandlers handlers;
};

} /* Gui */

#endif /* end of include guard: GUI_VIEWPROVIDERDATUM_H */
