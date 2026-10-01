/******************************************************************************
 *   Copyright (c) 2013 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
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


#ifndef PARTDESIGN_FeatureBoolean_H
#define PARTDESIGN_FeatureBoolean_H

#include <App/GeoFeatureGroupExtension.h>
#include <App/PropertyStandard.h>
#include "Feature.h"


namespace PartDesign
{

/**
 * Abstract superclass of all features that are created by transformation of another feature
 * Transformations are translation, rotation and mirroring
 */
class PartDesignExport Boolean : public PartDesign::Feature, public App::GeoFeatureGroupExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::Boolean);
    using inherited = PartDesign::Feature;

public:
    Boolean();

    /// The type of the boolean operation
    App::PropertyEnumeration    Type;

    App::PropertyBool Refine;
    /// As FeatureAddSub::FuzzyTolerance (upstream 73f848a3d5)
    App::PropertyFloatConstraint FuzzyTolerance;
    /// The tool shapes, in the frame of the base shape, as the edit preview
    /// draws them. Not saved; kept current by execute(), paused or not.
    Part::PropertyPartShape ToolShape;
    /** The Boolean owns every tool, and a tool's shape is taken in the
     *  Boolean's frame as it is (upstream 9c7a761589's name). On for a
     *  Boolean restored from a file that does not say, off for a new one,
     *  which owns only its SubShapeBinders -- what the Boolean command makes
     *  -- and takes any other tool as a reference: it stays where it is,
     *  in its Part, and its shape is brought into the body from where it is.
     */
    App::PropertyBool UseLegacyBodyPlacement;

   /** @name methods override feature */
    //@{
    /// Recalculate the feature
    App::DocumentObjectExecReturn *execute() override;
    short mustExecute() const override;
    /// returns the type name of the view provider
    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderBoolean";
    }
    void onChanged(const App::Property* prop) override;
    //@}

    void onNewSolidChanged() override;
    void unsetupObject() override;
    void setPauseRecompute(bool enable) override;

    /** @name tools as references (see UseLegacyBodyPlacement) */
    //@{
    std::vector<App::DocumentObject*> addObjects(std::vector<App::DocumentObject*> objects) override;
    std::vector<App::DocumentObject*> setObjects(std::vector<App::DocumentObject*> objects) override;
    /// Whether the Boolean owns \a obj, not only whether it is a tool
    bool hasObject(const App::DocumentObject* obj, bool recursive = false) const override;
    //@}

protected:
    /** The operands in the order the boolean takes them: the base first,
     *  then the tools. \a hasBase says whether the base is the base
     *  feature's shape rather than a tool standing in for it.
     */
    App::DocumentObjectExecReturn *collectOperands(std::vector<TopoShape> &shapes,
                                                   bool &hasBase) const;
    void updateToolShape(const std::vector<TopoShape> &shapes, bool hasBase);
    /// A tool's shape in the body's frame
    TopoShape getToolShape(const App::DocumentObject *tool) const;
    /// Whether the Boolean owns \a tool rather than referring to it
    bool ownsTool(const App::DocumentObject *tool) const;

    void handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *PropName) override;


private:
    static const char* TypeEnums[];

};

} //namespace PartDesign


#endif // PARTDESIGN_FeatureBoolean_H
