/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#ifndef PARTDESIGN_FeatureMultiTransform_H
#define PARTDESIGN_FeatureMultiTransform_H

#include "FeatureTransformed.h"


namespace PartDesign
{

class PartDesignExport MultiTransform : public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::MultiTransform);

public:
    MultiTransform();

    App::PropertyLinkList Transformations;

   /** @name methods override feature */
    //@{
    short mustExecute() const override;

    /// returns the type name of the view provider
    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderMultiTransform";
    }
    //@}

    /** Create transformations
      * Returns a list containing the product of all transformations of the subfeatures given
      * by the Transformations property. Subfeatures can be Mirrored, LinearPattern, PolarPattern and
      * Scaled.
      */
    std::list<gp_Trsf> getTransformations(const std::vector<Part::TopoShape> &) override;

    /** An instance is left out by its own index, or when any sub-feature
      * leaves out one of the instances it is the product of (upstream
      * cdb4624675)
      */
    bool isTransformationSuppressed(int index) const override;

protected:
    void positionBySupport() override;
    void onChanged(const App::Property *) override;

private:
    /// Per instance of the last getTransformations(): left out by a sub-feature
    std::vector<bool> generatedSuppression;
};

} //namespace PartDesign


#endif // PARTDESIGN_FeatureMultiTransform_H
